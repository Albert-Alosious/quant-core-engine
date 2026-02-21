# Phase 3.1 — Position Engine

## Objective

Give the trading engine **persistent state** by tracking every fill and computing per-symbol position and realized PnL. This is the first step toward a fully stateful engine capable of portfolio-level risk management.

---

## New Components

| File | Role |
|------|------|
| `core/app/include/quant/domain/position.hpp` | `Position` struct — per-symbol trading state |
| `core/app/include/quant/events/position_update_event.hpp` | `PositionUpdateEvent` — immutable snapshot published after each fill |
| `core/app/include/quant/risk/position_engine.hpp` | `PositionEngine` header — declaration and detailed Doxygen documentation |
| `core/app/src/risk/position_engine.cpp` | `PositionEngine` implementation — PnL math and event wiring |

## Modified Components

| File | Change |
|------|--------|
| `core/app/include/quant/events/event.hpp` | Added `PositionUpdateEvent` to the `Event` `std::variant` |
| `core/app/CMakeLists.txt` | Added `src/risk/position_engine.cpp` to `core_lib` sources |
| `core/engine/include/quant/engine/trading_engine.hpp` | Added `std::unique_ptr<PositionEngine> position_engine_` member |
| `core/engine/src/trading_engine.cpp` | Instantiate `PositionEngine` in `start()`, destroy in `stop()` |
| `main.cpp` | Added `PositionUpdateEvent` logging subscriber for visual verification |

---

## Event Flow

```
ExecutionReportEvent (Filled)
       │
       ▼
 PositionEngine::onFill()
       │
       ├── Look up order_id in order_cache_ → {symbol, side}
       ├── Compute signed fill quantity (+Buy / -Sell)
       ├── Apply PnL math to positions_[symbol]
       │
       ▼
 PositionUpdateEvent (snapshot of updated Position)
       │
       ▼
 EventBus → downstream subscribers (logging, monitoring, future PortfolioEngine)
```

### Order Cache Design

`ExecutionReportEvent` carries `order_id`, `filled_quantity`, and `fill_price` — but **not** `symbol` or `side`. To avoid modifying the existing event contract, `PositionEngine` subscribes to **both**:

1. **`OrderEvent`** → caches `{order_id → symbol, side}` in `order_cache_`.
2. **`ExecutionReportEvent`** → looks up the cache to retrieve symbol and side, then applies PnL math.

Both events are published on the same `risk_execution_loop` thread. `PositionEngine` is created **before** `ExecutionEngine` in `TradingEngine::start()`, ensuring its `OrderEvent` subscription fires first — the cache is warm before `onFill()` runs.

---

## PnL Math — Strict Formulas

All quantities below use a **signed convention**:
- `net_quantity > 0` → long position
- `net_quantity < 0` → short position
- `signed_fill_qty > 0` for Buy, `< 0` for Sell

### Case 1: Increasing Position (same direction as existing)

```
new_avg_price = (current_qty × current_avg_price + signed_fill_qty × fill_price)
                / (current_qty + signed_fill_qty)

new_qty = current_qty + signed_fill_qty
```

Realized PnL: **unchanged**.

### Case 2: Decreasing Position (opposite direction, does NOT cross zero)

```
closed_qty = abs(signed_fill_qty)

realized_pnl += closed_qty × (fill_price - avg_price) × direction_sign
```

Where `direction_sign = +1` if closing a long, `-1` if closing a short.

```
new_qty = current_qty + signed_fill_qty
```

Average price: **unchanged** (the remaining position was entered at the same average).

### Case 3: Crossing Zero (reversal — fill exceeds existing position)

Split the fill into two parts:

**Part A — Close existing position to flat:**
```
close_qty = abs(current_qty)
realized_pnl += close_qty × (fill_price - avg_price) × direction_sign
```

**Part B — Open new position in opposite direction:**
```
open_qty = abs(signed_fill_qty) - close_qty
new_qty = sign(signed_fill_qty) × open_qty
new_avg_price = fill_price
```

---

## Thread Safety

`PositionEngine` lives entirely on the `risk_execution_loop` thread:

- **Construction**: called from `main()` thread during `TradingEngine::start()`, before any events flow.
- **Callbacks** (`onOrder`, `onFill`): invoked exclusively on the `risk_execution_loop` thread by the `EventBus` dispatch loop.
- **Internal state** (`positions_`, `order_cache_`): accessed single-threaded. **No mutex required.**
- **Destruction**: called from `main()` thread during `TradingEngine::stop()`, after the event loop has been drained and joined.

The `PositionUpdateEvent` published by `onFill()` is a **value copy** of the `Position` struct — it carries no references to `PositionEngine`'s internal state and is safe to read on any thread.

## Component Creation Order in TradingEngine::start()

The creation order on the `risk_execution_loop` bus is critical:

1. **PositionEngine** — subscribes to `OrderEvent` (cache) and `ExecutionReportEvent` (fills)
2. **RiskEngine** — subscribes to `SignalEvent`, publishes `OrderEvent`
3. **ExecutionEngine** — subscribes to `OrderEvent`, publishes `ExecutionReportEvent`

This ensures `PositionEngine`'s `OrderEvent` callback fires **before** `ExecutionEngine`'s, so the order cache is populated before `ExecutionEngine` synchronously publishes a fill.

Destruction order is the reverse: `ExecutionEngine` → `RiskEngine` → `PositionEngine` → `DummyStrategy`.
