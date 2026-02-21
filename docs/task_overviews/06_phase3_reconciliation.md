# Phase 3.3 — Exchange State Reconciliation

## Objective

Give the trading engine a **synchronization gate** on startup so it can
reconcile its internal state (positions and open orders) with the exchange
before processing any market data. Without this step, the engine would start
with empty maps and immediately produce incorrect PnL calculations or reject
execution reports for orders it doesn't know about.

---

## Why Reconciliation Matters

| Scenario | Without reconciliation | With reconciliation |
|----------|------------------------|---------------------|
| Engine restart after crash | PositionEngine starts flat; next fill computes PnL against avg_price=0 | PositionEngine starts with correct net_quantity and avg_price from exchange |
| Manual trade placed while engine was offline | OrderTracker rejects execution report as "unknown order_id" | OrderTracker already has the order in its active map |
| Overnight carry position | First sell fill treated as opening a short instead of closing a long | Existing long position is known; sell correctly computes realized PnL |

---

## Warm-up vs Live Sequence

```
┌──────────────────────────────────────────────────────────┐
│  WARM-UP PHASE  (main thread, single-threaded, no races) │
├──────────────────────────────────────────────────────────┤
│  1. Create OrderTracker         (subscribes to EventBus) │
│  2. Create PositionEngine       (subscribes to EventBus) │
│  3. ┌─ Synchronization Gate ──────────────────────────┐  │
│     │  a) IReconciler::reconcilePositions()           │  │
│     │     → PositionEngine::hydratePosition() × N     │  │
│     │  b) IReconciler::reconcileOrders()              │  │
│     │     → OrderTracker::hydrateOrder() × M          │  │
│     └─────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────┤
│  LIVE PHASE  (multi-threaded, events flowing)            │
├──────────────────────────────────────────────────────────┤
│  4. Start EventLoopThreads      (spawn worker threads)   │
│  5. Wire cross-thread forwarder (SignalEvent bridge)     │
│  6. Create DummyStrategy        (strategy_loop)          │
│  7. Create RiskEngine           (risk_execution_loop)    │
│  8. Create ExecutionEngine      (risk_execution_loop)    │
│  9. MarketDataGateway begins    (external, main thread)  │
└──────────────────────────────────────────────────────────┘
```

---

## Why Reconcile BEFORE Starting Event Loops

The reconciliation gate must complete before event loop threads are spawned.
If the loops were running during hydration, a race condition would occur:

```
Thread: main                          Thread: risk_execution_loop
─────────────                         ─────────────────────────────
hydratePosition(AAPL, 100, $150)      MarketDataEvent arrives
  ↓                                     ↓
  writing to positions_["AAPL"]       DummyStrategy → SignalEvent
                                        ↓
                                      RiskEngine → OrderEvent
                                        ↓
                                      ExecutionEngine → Filled report
                                        ↓
                                      PositionEngine::onFill()
                                        reading positions_["AAPL"]
                                        ← DATA RACE ←
```

By hydrating on the main thread *before* the risk_execution_loop thread
exists, `positions_` and `active_orders_` are accessed single-threaded. No
mutexes are needed, and the engine transitions to the live phase with a
correct, consistent baseline.

---

## New Components

| File | Role |
|------|------|
| `core/app/include/quant/risk/i_reconciler.hpp` | `IReconciler` pure virtual interface + `MockReconciler` implementation |

### IReconciler Interface

```cpp
class IReconciler {
 public:
  virtual ~IReconciler() = default;
  virtual std::vector<domain::Position> reconcilePositions() = 0;
  virtual std::vector<domain::Order>    reconcileOrders()    = 0;
};
```

- Called once, synchronously, on the main thread during the synchronization
  gate.
- Implementations may perform network I/O (e.g., REST API calls in a future
  `RestApiReconciler`).
- TradingEngine does NOT own the reconciler — it receives a non-owning raw
  pointer and uses it only during `start()`.

### MockReconciler

Returns hardcoded test data:

| Method | Returns |
|--------|---------|
| `reconcilePositions()` | One position: 100 shares AAPL at $150.00, realized_pnl=0 |
| `reconcileOrders()` | Empty vector (no open orders) |

---

## Modified Components

| File | Change |
|------|--------|
| `core/app/include/quant/risk/position_engine.hpp` | Added public `hydratePosition(const Position&)` |
| `core/app/src/risk/position_engine.cpp` | Implemented `hydratePosition()` — single-line map insert |
| `core/app/include/quant/risk/order_tracker.hpp` | Added public `hydrateOrder(const Order&)` |
| `core/app/src/risk/order_tracker.cpp` | Implemented `hydrateOrder()` — single-line map insert |
| `core/engine/include/quant/engine/trading_engine.hpp` | Added `#include "i_reconciler.hpp"`, changed `start()` to `start(IReconciler* = nullptr)` |
| `core/engine/src/trading_engine.cpp` | Reordered `start()`: create stateful components → reconcile → start loops → remaining |

---

## hydratePosition() / hydrateOrder() Contract

Both methods share the same calling contract:

| Property | Detail |
|----------|--------|
| **When** | Main thread, during synchronization gate, before event loops start |
| **Thread-safety** | NOT safe for concurrent use with live callbacks (onFill, onExecutionReport) |
| **Events published** | None — hydrated state is historical, not a live transition |
| **Validation** | None — the exchange is the source of truth |
| **Idempotency** | Overwrites any existing entry for the same key (symbol or order_id) |

---

## Impact on Existing Components

| Component | Impact |
|-----------|--------|
| `PositionEngine` | Added one public method. All existing logic unchanged. |
| `OrderTracker` | Added one public method. All existing logic unchanged. |
| `TradingEngine` | `start()` reordered internally. Default parameter preserves backward compatibility. |
| `main.cpp` | No changes — calls `engine.start()` with default `nullptr` reconciler. |
| All tests | No changes — 25/25 pass. Default parameter means no reconciliation in test runs. |
| Event definitions | No changes — no new events introduced. |
| CMakeLists.txt | No changes — `i_reconciler.hpp` is header-only with inline `MockReconciler`. |

---

## Future Extensions

| Phase | Extension |
|-------|-----------|
| Phase 4 | `RestApiReconciler` — queries exchange REST API, parses JSON responses |
| Phase 7 | `JournalReconciler` — rebuilds state from local trade journal, cross-references with exchange |
| Phase 5 | `hydratePosition()` may also seed `PositionEngine::order_cache_` if the reconciler returns in-flight orders with pending fills |

---

## Test Results

All 25 existing tests pass without modification:

```
100% tests passed, 0 tests failed out of 25
```
