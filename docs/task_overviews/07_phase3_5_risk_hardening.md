# Phase 3.5 — Risk Hardening

## Objective

Add hard pre-trade and post-trade risk controls to the engine before
connecting to external gateways (Phase 4). Without these guardrails, a
runaway strategy could accumulate unlimited exposure or bleed capital with
no circuit breaker.

---

## New Components

| File | Role |
|------|------|
| `core/app/include/quant/domain/risk_limits.hpp` | `RiskLimits` struct — engine-wide thresholds |
| `core/app/include/quant/events/risk_violation_event.hpp` | `RiskViolationEvent` — published when a hard limit is breached |

---

## Risk Controls Implemented

### 1. Max Drawdown Kill Switch (Post-Trade)

**Trigger:** After every fill, `PositionEngine::onFill()` checks whether
the symbol's `realized_pnl` has dropped below `limits_.max_drawdown`
(default: -500.0).

**Action:** If breached, `PositionEngine` publishes a `RiskViolationEvent`.
`RiskEngine` subscribes to this event and sets `halt_trading_ = true`.
Once activated, **every subsequent signal is silently dropped** — no new
orders can enter the pipeline.

**Event flow:**

```
ExecutionReportEvent (Filled)
       │
       ▼
PositionEngine::onFill()
       │
       ├── Apply PnL math → update position
       ├── Publish PositionUpdateEvent
       ├── Check: realized_pnl < max_drawdown?
       │     └── YES → Publish RiskViolationEvent
       │                    │
       │                    ▼
       │              RiskEngine::onRiskViolation()
       │                    │
       │                    └── halt_trading_ = true
       │                        (all future signals dropped)
       │
       └── Continue normal flow
```

**Recovery:** The kill switch cannot be reset without restarting the engine.
This is intentional — automated recovery from a drawdown breach requires
human review.

### 2. Max Position Pre-Trade Check

**Trigger:** Before converting any `SignalEvent` into an `OrderEvent`,
`RiskEngine::onSignal()` queries `PositionEngine` for the symbol's current
`net_quantity`.

**Check:**
```
abs(current_net_quantity) + order_quantity > max_position_per_symbol?
```

If the proposed exposure would exceed the limit (default: 1000 units), the
signal is dropped and a warning is logged to stderr.

**Why a direct read instead of an event?** Both `RiskEngine` and
`PositionEngine` live on the same `risk_execution_loop` thread. Reading
position state via a `const` accessor is single-threaded — no mutex needed,
no event latency. This is not cross-module coupling; it is a read-only
query within the same thread domain, which `architecture.md` rule #5
("Risk MUST sit between router and execution") explicitly requires.

---

## RiskLimits Struct

```cpp
struct RiskLimits {
  double max_position_per_symbol{1000.0};  // Pre-trade cap
  double max_drawdown{-500.0};             // Post-trade kill switch floor
};
```

- Value semantics, copied into `PositionEngine` and `RiskEngine` at
  construction time.
- Immutable for the engine's lifetime.
- Phase 6 will load these from a JSON/YAML configuration file.
- Phase 5 will add per-strategy limits alongside these engine-wide limits.

---

## RiskViolationEvent

```cpp
struct RiskViolationEvent {
  std::string symbol;
  std::string reason;
  double current_value{0.0};
  double limit_value{0.0};
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};
```

Added to the `Event` variant in `event.hpp`.

---

## Modified Components

| File | Change |
|------|--------|
| `core/app/include/quant/events/event.hpp` | Added `RiskViolationEvent` to `Event` variant |
| `core/app/include/quant/risk/position_engine.hpp` | Constructor takes `RiskLimits`, added `position()` const accessor, added `limits_` member |
| `core/app/src/risk/position_engine.cpp` | Constructor init, `position()` implementation, drawdown check in `onFill()` |
| `core/app/include/quant/risk/risk_engine.hpp` | Constructor takes `PositionEngine&` + `RiskLimits`, added halt flag, `onRiskViolation()`, violation subscription |
| `core/app/src/risk/risk_engine.cpp` | Constructor init, halt check + position check in `onSignal()`, `onRiskViolation()` implementation |
| `core/engine/include/quant/engine/trading_engine.hpp` | Added `risk_limits_` value member |
| `core/engine/src/trading_engine.cpp` | Pass `risk_limits_` and `*position_engine_` to component constructors |
| `tests/pipeline_integration_test.cpp` | Added `PositionEngine` + `RiskLimits` to test fixture |

---

## PositionEngine `position()` Accessor — Thread Safety Proof

```
Thread: risk_execution_loop (SINGLE thread)
─────────────────────────────────────────────
EventBus::dispatch(SignalEvent)
  ↓
  ├── RiskEngine::onSignal()
  │     ├── positions_.position("AAPL")   ← const read of positions_ map
  │     ├── Pre-trade check passes
  │     ├── Build Order, publish OrderEvent
  │     │
  │     │   EventBus::dispatch(OrderEvent)     [synchronous, same thread]
  │     │     ├── OrderTracker::onOrder()
  │     │     ├── PositionEngine::onOrder()    ← writes to order_cache_
  │     │     ├── ExecutionEngine::onOrder()
  │     │     │     ├── Publish ExecutionReportEvent(Accepted)
  │     │     │     ├── Publish ExecutionReportEvent(Filled)
  │     │     │     │     ├── PositionEngine::onFill()  ← writes to positions_
```

The `position()` read happens **before** `onFill()` writes. Both are on the
same thread, executing sequentially within the EventBus dispatch chain.
There is no concurrent access — no mutex is needed.

---

## Ghost Rejected Bug — Confirmation

Both execution engines were inspected:

| File | Lines | Finding |
|------|-------|---------|
| `execution_engine.cpp` | 21–46 | Only `Accepted` (line 30) then `Filled` (line 41). No `Rejected` logic. |
| `mock_execution_engine.cpp` | 29–58 | Only `Accepted` (line 40) then `Filled` (line 55). No `Rejected` logic. |

The root cause was fixed in **Phase 3.4**:
1. `ExecutionReportEvent::status` default changed from `Filled` to `Accepted`.
2. `OrderTracker::onExecutionReport()` local variable `proposed` initialized.

No code changes were needed for this step.

---

## Test Results

All 25 tests pass after a clean rebuild:

```
100% tests passed, 0 tests failed out of 25
```

**Note:** An incremental build produced a stale-object segfault in
`TradingEngineTest.EventBusAccessorsWork` due to the `Event` variant size
change. A clean rebuild (`rm -rf build`) resolved this. This is a known
CMake/incremental-build artifact when `std::variant` members change — all
translation units that include `event.hpp` must be recompiled consistently.
