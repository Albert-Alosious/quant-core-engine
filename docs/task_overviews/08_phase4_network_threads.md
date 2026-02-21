# Phase 4.1 — Network Layer Threads

## Objective

Isolate network I/O (market data ingestion, order execution) from the core
strategy and risk logic by introducing two dedicated I/O threads. This
ensures that ZMQ recv latency, future broker API round-trips, or network
hiccups never block the risk engine's event processing critical path.

---

## Thread Layout (After Phase 4.1)

```
┌─────────────────────────────────────────────────────────────────────┐
│  main thread                                                        │
│    engine.start()  →  wait for SIGINT  →  engine.stop()             │
└─────────────────────────────────────────────────────────────────────┘

┌───────────────────┐   ┌──────────────────┐   ┌───────────────────┐
│  strategy_loop    │   │  risk_loop       │   │ order_routing     │
│                   │   │                  │   │                   │
│  DummyStrategy    │   │  OrderTracker    │   │  ExecutionEngine  │
│                   │   │  PositionEngine  │   │  (or Mock)        │
│                   │   │  RiskEngine      │   │                   │
│                   │   │                  │   │                   │
│  EventLoopThread  │   │  EventLoopThread │   │  EventLoopThread  │
└───────────────────┘   └──────────────────┘   └───────────────────┘

┌───────────────────┐
│  market_data      │
│                   │
│  MarketDataGateway│
│  (ZMQ recv loop)  │
│                   │
│  raw std::thread  │
└───────────────────┘
```

**4 threads total** (up from 2 in Phase 3):

| Thread | Component(s) | I/O? |
|--------|-------------|------|
| `strategy_loop` | DummyStrategy | No — pure logic |
| `risk_loop` | OrderTracker, PositionEngine, RiskEngine | No — pure logic |
| `order_routing` | ExecutionEngine / MockExecutionEngine | Yes — future broker API |
| `market_data` | MarketDataGateway (ZMQ SUB socket) | Yes — ZMQ network recv |

---

## Cross-Thread Event Bridges

Events cross thread boundaries via `EventLoopThread::push()`, which
enqueues the event into the destination thread's `ThreadSafeQueue`. The
destination thread pops and publishes it on its own `EventBus`.

```
  market_data_thread                strategy_loop
  ──────────────────                ─────────────
  MarketDataGateway                DummyStrategy
  event_sink_(Event) ──push()──▶  strategy_loop queue
                                         │
                                  publishes MarketDataEvent
                                         │
                                  DummyStrategy::onMarketData()
                                         │
                                  publishes SignalEvent
                                         │
  ◀──────────────────── Bridge 1 ────────┘
                                         │
  risk_loop                              ▼
  ─────────                     risk_loop queue
  RiskEngine::onSignal()               │
         │                              │
  publishes OrderEvent                  │
         │                              │
         ├── Bridge 2 ──push()──▶ order_routing queue
         │                              │
         │                     ExecutionEngine::onOrder()
         │                              │
         │                     publishes ExecutionReportEvent
         │                              │
         ◀── Bridge 3 ──push()──────────┘
         │
  OrderTracker::onExecutionReport()
  PositionEngine::onFill()
```

| Bridge | Source Bus | Event Type | Destination Queue |
|--------|-----------|------------|-------------------|
| 1 | strategy_loop | SignalEvent | risk_loop |
| 2 | risk_loop | OrderEvent | order_routing |
| 3 | order_routing | ExecutionReportEvent | risk_loop |
| 4 | market_data | MarketDataEvent | strategy_loop (via pushEvent) |

---

## How I/O Latency Is Isolated

Before Phase 4, `ExecutionEngine` lived on the `risk_execution_loop`
alongside `RiskEngine` and `PositionEngine`. If the execution engine
blocked (e.g., waiting for a broker ACK), the risk engine's event
processing would stall.

After Phase 4, `ExecutionEngine` lives on its own `order_routing` thread.
The risk loop publishes `OrderEvent`, a bridge subscriber pushes it to the
order routing queue, and the execution engine processes it independently.
`ExecutionReportEvent` flows back via the reverse bridge. The risk loop
never waits for execution — it just publishes and continues.

Similarly, `MarketDataGateway` no longer blocks the main thread. It runs
in its own `market_data` thread, pushing decoded events into the strategy
loop via the engine's `pushEvent()` method.

---

## Logger Bug Fix (Ghost Rejected)

The `ExecutionReportEvent` logger in `main.cpp` used a ternary that only
checked for `Filled` and defaulted everything else to `"Rejected"`:

```cpp
// BEFORE (broken):
e.status == ExecutionStatus::Filled ? "Filled" : "Rejected"
```

When `Accepted` reports arrived, they were printed as `"Rejected"`, causing
the misleading "Ghost Rejected" log entries.

```cpp
// AFTER (fixed):
switch (s) {
  case ExecutionStatus::Accepted: return "Accepted";
  case ExecutionStatus::Filled:   return "Filled";
  case ExecutionStatus::Rejected:  return "Rejected";
}
```

---

## Decoupled `main.cpp`

Before Phase 4, `main()` was responsible for:
- Creating `MarketDataGateway`
- Calling `gateway.run()` (blocking the main thread)
- Holding a global pointer `g_gateway_ptr` for SIGINT handling

After Phase 4, `main()` is minimal:
1. Create `SimulationTimeProvider`
2. Create `TradingEngine(sim_clock)`
3. Subscribe logging callbacks
4. `engine.start()`
5. Wait for SIGINT via `std::condition_variable`
6. `engine.stop()`

The global `g_gateway_ptr` is eliminated. The SIGINT handler now signals a
condition variable instead of calling `gateway->stop()` directly.

---

## Test Adaptations

`TradingEngine` constructor now requires a `SimulationTimeProvider&`
reference and an optional market data endpoint string. For unit tests,
an empty endpoint `""` is passed, which skips `MarketDataThread` creation
entirely — no ZMQ sockets are opened, preventing test hangs when no
publisher is available.

`ExecutionEngine` now inherits from `IExecutionEngine` (previously it did
not), enabling polymorphic ownership via `unique_ptr<IExecutionEngine>`
inside `OrderRoutingThread`.

---

## New Files

| File | Role |
|------|------|
| `core/app/include/quant/network/market_data_thread.hpp` | Thread wrapper for MarketDataGateway |
| `core/app/src/network/market_data_thread.cpp` | Implementation |
| `core/app/include/quant/network/order_routing_thread.hpp` | Thread wrapper with EventLoopThread + IExecutionEngine |
| `core/app/src/network/order_routing_thread.cpp` | Implementation |

## Modified Files

| File | Change |
|------|--------|
| `main.cpp` | Fixed logger, removed gateway ownership, simplified to start/wait/stop |
| `core/app/include/quant/execution/execution_engine.hpp` | Added `IExecutionEngine` inheritance, `override` destructor |
| `core/engine/include/quant/engine/trading_engine.hpp` | New constructor, network thread members, removed `ExecutionEngine` ownership |
| `core/engine/src/trading_engine.cpp` | Rewrote start()/stop() for 4-thread lifecycle, cross-thread bridges |
| `core/app/CMakeLists.txt` | Added two new source files |
| `tests/trading_engine_test.cpp` | Fixture with SimulationTimeProvider, empty endpoint |

---

## Test Results

```
100% tests passed, 0 tests failed out of 25
Total Test time (real) =   0.09 sec
```
