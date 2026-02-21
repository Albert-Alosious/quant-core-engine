# Phase 3 — Price Propagation Fix

## Problem

`ExecutionReportEvent` and `PositionUpdateEvent` were reporting `price=0` and `avg_price=0`. The `PositionEngine`'s PnL calculations were correct, but they operated on zero-valued prices, producing meaningless results.

## Root Cause

The market price was being lost at **two points** in the pipeline:

1. **`DummyStrategy::onMarketData`** — Built a `SignalEvent` but never copied `MarketDataEvent.price` into it. `SignalEvent` did not even have a `price` field.

2. **`RiskEngine::onSignal`** — Hardcoded `order.price = 0.0` instead of reading the price from the incoming signal.

Downstream components (`ExecutionEngine`, `MockExecutionEngine`, `PositionEngine`) were already correctly passing through `order.price` → `fill_price` → PnL math. The bug was purely upstream.

## Fix Applied

### 1. Added `price` field to `SignalEvent`

**File:** `core/app/include/quant/events/event_types.hpp`

```cpp
struct SignalEvent {
  // ... existing fields ...
  double price{0.0};   // Market price that triggered this signal (NEW)
  // ...
};
```

### 2. Propagated price in `DummyStrategy::onMarketData`

**File:** `core/app/src/statergy/dummy_strategy.cpp`

```cpp
signal.price = event.price;   // NEW — was missing entirely
```

### 3. Propagated price in `RiskEngine::onSignal`

**File:** `core/app/src/risk/risk_engine.cpp`

```cpp
order.price = event.price;    // FIX — was hardcoded to 0.0
```

## Complete Price Propagation Path

```
MarketDataEvent.price         (e.g. 150.25 from Python feeder / ZeroMQ)
       │
       ▼
DummyStrategy::onMarketData
  signal.price = event.price
       │
       ▼
SignalEvent.price              (150.25 — crosses thread boundary via forwarder)
       │
       ▼
RiskEngine::onSignal
  order.price = event.price
       │
       ▼
domain::Order.price            (150.25 — wrapped in OrderEvent)
       │
       ▼
ExecutionEngine::onOrder / MockExecutionEngine::onOrder
  report.fill_price = order.price
       │
       ▼
ExecutionReportEvent.fill_price  (150.25)
       │
       ▼
PositionEngine::onFill
  applyFill(pos, signed_fill_qty, fill_price)
       │
       ▼
PositionUpdateEvent.position.average_price   (correct weighted average)
PositionUpdateEvent.position.realized_pnl    (correct PnL on closes)
```

## Impact on PositionEngine

No changes were required to the `PositionEngine` itself. Its PnL math was already correct — it faithfully consumed `ExecutionReportEvent.fill_price`. The fix ensures that `fill_price` now carries the actual market price instead of `0.0`, enabling:

- **`average_price`**: correctly computed as the weighted average of fill prices.
- **`realized_pnl`**: correctly computed as `closed_qty × (fill_price - avg_price) × direction_sign`.

## Files Modified

| File | Change |
|------|--------|
| `core/app/include/quant/events/event_types.hpp` | Added `double price{0.0}` to `SignalEvent` |
| `core/app/src/statergy/dummy_strategy.cpp` | Added `signal.price = event.price` |
| `core/app/src/risk/risk_engine.cpp` | Changed `order.price = 0.0` → `order.price = event.price` |
