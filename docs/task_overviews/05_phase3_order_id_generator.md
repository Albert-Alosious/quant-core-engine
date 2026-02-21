# Phase 3.4 — Ghost Rejected Fix & Central OrderId Generator

## 1. Ghost Rejected Bug — Root Cause & Fix

### Symptom

After the Phase 3.2 Order State Machine was deployed, engine logs showed an
unexpected `Rejected` status report appearing **before** the `Accepted` report
for every order:

```
[OrderTracker] order_id=1 transition New → Rejected
[OrderTracker] order_id=1 transition Rejected → Accepted   ← INVALID!
```

This violated the state machine: `Rejected` is a terminal state and cannot
transition to `Accepted`.

### Root Cause

Two issues combined to produce the ghost rejection:

1. **`ExecutionReportEvent` default status was `Filled`.**
   In `execution_report_event.hpp`, the `status` field was initialized as:
   ```cpp
   ExecutionStatus status{ExecutionStatus::Filled};
   ```
   When the `ExecutionStatus` enum was extended with `Accepted` at ordinal 0,
   a zero-initialized (default-constructed) `ExecutionReportEvent` would have
   `status == Accepted`. However, the explicit default `Filled` meant that any
   code path constructing the struct and setting only *some* fields could
   produce a misleading status.

2. **Uninitialized local variable in `OrderTracker::onExecutionReport`.**
   The `proposed` variable in the `ExecutionStatus → OrderStatus` mapping
   switch was declared without initialization:
   ```cpp
   domain::OrderStatus proposed;   // UNINITIALIZED
   ```
   If the compiler's zero-initialization happened to map to `OrderStatus::New`,
   a missing `default:` case or unexpected enum value would silently fall
   through, producing an invalid transition.

### Fix Applied

| File | Change |
|------|--------|
| `core/app/include/quant/events/execution_report_event.hpp` | Changed default from `ExecutionStatus::Filled` to `ExecutionStatus::Accepted` (ordinal 0, matches zero-init semantics). |
| `core/app/src/risk/order_tracker.cpp` | Initialized `proposed` to `domain::OrderStatus::New` and added a `default:` case that logs a warning and returns early. |

After these changes, only the correct `Accepted → Filled` sequence appears in
the logs.

---

## 2. Central OrderId Generator

### Problem Statement

Before this change, `RiskEngine` maintained a plain `std::uint64_t
next_order_id_` counter. This was safe only because a single `RiskEngine`
instance ran on a single thread. The architecture roadmap (Phase 4+) calls for
multiple strategies and risk modules, potentially on different threads. A
non-atomic, per-component counter would eventually produce **duplicate order
IDs** — a catastrophic correctness bug in any trading system.

### Design

A new header-only class `OrderIdGenerator` provides a single, authoritative
source of order IDs:

```
File: core/app/include/quant/concurrent/order_id_generator.hpp
```

#### Key properties

| Property | Detail |
|----------|--------|
| **Counter type** | `std::atomic<std::uint64_t>` |
| **Starting value** | 1 (ID 0 is reserved as "unset" sentinel) |
| **Increment** | `fetch_add(1, std::memory_order_relaxed)` |
| **Memory order** | `relaxed` — no cross-variable ordering is needed; the only guarantee required is uniqueness, which atomicity provides. |
| **Copyable / Movable** | Deleted — prevents accidental duplication of the counter, which would create two sources producing duplicate IDs. |

#### Why not a singleton?

`architecture.md` rule #6 prohibits global mutable state. The generator is
owned as a **value member** by `TradingEngine` and **injected by reference**
into `RiskEngine` (and any future order-creating components). This makes the
dependency explicit, testable, and free of hidden coupling.

### Ownership & Lifetime

```
TradingEngine
 ├── order_id_gen_   (OrderIdGenerator — value member, declared FIRST)
 ├── strategy_loop_
 ├── risk_execution_loop_
 ├── strategy_
 ├── order_tracker_
 ├── position_engine_
 ├── risk_engine_     ← holds OrderIdGenerator& reference
 └── execution_engine_
```

`order_id_gen_` is declared before all `unique_ptr` component members, so it
is destroyed **after** them. This guarantees the reference held by
`RiskEngine` is valid for the entire lifetime of that component.

### Integration

| File | Change |
|------|--------|
| `core/engine/include/quant/engine/trading_engine.hpp` | Added `OrderIdGenerator order_id_gen_;` as the first private member. |
| `core/engine/src/trading_engine.cpp` | Passes `order_id_gen_` to `RiskEngine`'s constructor. |
| `core/app/include/quant/risk/risk_engine.hpp` | Constructor changed from `RiskEngine(EventBus&)` to `RiskEngine(EventBus&, OrderIdGenerator&)`. Replaced `std::uint64_t next_order_id_` with `OrderIdGenerator& id_gen_`. |
| `core/app/src/risk/risk_engine.cpp` | Calls `id_gen_.next_id()` instead of `next_order_id_++`. |
| `tests/pipeline_integration_test.cpp` | Added `quant::OrderIdGenerator id_gen;` to the test fixture and passes it to `RiskEngine`. |

### Thread-Safety Guarantee

```
Thread A (risk_execution_loop)        Thread B (future risk module)
         │                                       │
    id_gen_.next_id()                       id_gen_.next_id()
         │                                       │
    fetch_add(1, relaxed)               fetch_add(1, relaxed)
         │                                       │
    returns 1                              returns 2
         ▼                                       ▼
    IDs are globally unique — atomic fetch_add is indivisible
```

Even though today only one thread calls `next_id()`, the atomic
implementation is a zero-cost safety net for future multi-strategy
architectures.

---

## 3. Test Results

All 25 existing tests pass without modification (other than the
`PipelineIntegrationTest` fixture update to pass the `OrderIdGenerator`):

```
100% tests passed, 0 tests failed out of 25
```
