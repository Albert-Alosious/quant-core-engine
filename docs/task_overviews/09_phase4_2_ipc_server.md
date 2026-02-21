# Phase 4.2 — IPC Gateway (Telemetry + Commands)

## Objective

Provide real-time visibility into the running engine and manual operator
control via ZeroMQ IPC. External clients (Python monitoring scripts,
Telegram bots, dashboards) can subscribe to a live telemetry feed and
send commands without modifying or restarting the C++ binary.

---

## Dual-Socket Architecture

The `IpcServer` runs a single dedicated thread that manages two ZMQ sockets:

```
                     ┌─────────────────────────────────────┐
                     │         IPC Server Thread           │
                     │                                     │
                     │  ┌───────────┐    ┌──────────────┐  │
 risk_loop ──push()──▶  │ Telemetry │    │   Command    │  │
  (bridge subs)      │  │   Queue   │    │    (REP)     │  │
                     │  └─────┬─────┘    └──────┬───────┘  │
                     │        │                 │          │
                     │    try_pop()          recv()        │
                     │    format JSON       dispatch       │
                     │        │                 │          │
                     │  ┌─────▼─────┐    ┌──────▼───────┐  │
                     │  │   PUB     │    │     REP      │  │
                     │  │  :5557    │    │    :5556     │  │
                     │  └───────────┘    └──────────────┘  │
                     └─────────────────────────────────────┘
                           │                    │
                     ZMQ SUB clients      ZMQ REQ client
                     (monitoring,         (operator CLI,
                      dashboards)          Python script)
```

| Socket | Type | Port | Direction | Purpose |
|--------|------|------|-----------|---------|
| Command | REP | 5556 | Bidirectional | Receive command, return JSON response |
| Telemetry | PUB | 5557 | Outbound | Broadcast events as JSON to subscribers |

---

## Telemetry Feed (PUB Socket)

### Event Flow

```
risk_loop thread                        ipc_server thread
────────────────                        ─────────────────
OrderTracker publishes OrderUpdateEvent
  ↓
  bridge subscriber → ipc_server_->pushTelemetry(e)
                              ↓
                      ThreadSafeQueue<Event>
                              ↓
                      processTelemetry() → try_pop()
                              ↓
                      formatTelemetry() → JSON string
                              ↓
                      pub_socket_->send()
                              ↓
                      External SUB clients
```

The bridge subscribers are registered on the `risk_loop_` EventBus during
`TradingEngine::start()`. They call `pushTelemetry()` which is a
non-blocking `ThreadSafeQueue::push()` — the risk_loop thread is never
blocked by JSON formatting or ZMQ I/O.

### Telemetry JSON Schemas

**OrderUpdateEvent:**
```json
{
  "type": "order_update",
  "order_id": 1,
  "symbol": "AAPL",
  "side": "Buy",
  "status": "Filled",
  "previous_status": "Accepted",
  "quantity": 1.0,
  "price": 150.25,
  "filled_quantity": 1.0
}
```

**PositionUpdateEvent:**
```json
{
  "type": "position_update",
  "symbol": "AAPL",
  "net_quantity": 1.0,
  "average_price": 150.25,
  "realized_pnl": 0.0
}
```

**RiskViolationEvent:**
```json
{
  "type": "risk_violation",
  "symbol": "AAPL",
  "reason": "Max Drawdown Exceeded",
  "current_value": -510.0,
  "limit_value": -500.0
}
```

---

## Command Protocol (REP Socket)

A REQ client sends a plain-text command string. The server responds with
a JSON string.

### Supported Commands

| Command | Response | Side-effects |
|---------|----------|-------------|
| `PING` | `{"status":"ok","response":"PONG"}` | None |
| `STATUS` | `{"status":"ok","halted":false,"positions":[...]}` | None (read-only) |
| `HALT` | `{"status":"ok","response":"Trading halted"}` | Activates kill switch |
| (other) | `{"status":"error","response":"Unknown command: ..."}` | None |

### STATUS Response Example

```json
{
  "status": "ok",
  "halted": false,
  "positions": [
    {
      "symbol": "AAPL",
      "net_quantity": 3.0,
      "average_price": 150.50,
      "realized_pnl": 12.75
    }
  ]
}
```

### HALT Command

Calls `RiskEngine::haltTrading()`, which sets an `std::atomic<bool>` flag.
All future `onSignal()` calls on the risk_loop thread observe the flag and
drop signals immediately. The kill switch cannot be reset without
restarting the engine.

---

## Thread-Safety Guarantees

### PositionEngine: `std::shared_mutex`

`PositionEngine::positions_` is accessed from two threads:

| Thread | Method | Access | Lock |
|--------|--------|--------|------|
| risk_loop | `onFill()` | Write | `unique_lock` |
| risk_loop | `hydratePosition()` | Write | `unique_lock` |
| risk_loop | `position()` | Read | `shared_lock` |
| IPC server | `getSnapshots()` | Read | `shared_lock` |

Multiple readers can hold `shared_lock` concurrently. Writers wait for
all readers to release before acquiring `unique_lock`. The lock is scoped
tightly in `onFill()` — it covers only the position mutation and snapshot
copy, not the `bus_.publish()` calls.

### RiskEngine: `std::atomic<bool>`

`RiskEngine::halt_trading_` was changed from `bool` to `std::atomic<bool>`:

| Thread | Method | Operation |
|--------|--------|-----------|
| risk_loop | `onSignal()` | `.load(relaxed)` |
| risk_loop | `onRiskViolation()` | `.store(true)` |
| IPC server | `haltTrading()` | `.store(true)` |
| IPC server | `isHalted()` | `.load(relaxed)` |

Relaxed ordering is sufficient because the halt flag is a monotonic
one-way latch (false → true, never reset). No other data depends on
the ordering of this flag relative to other memory operations.

---

## Shutdown Ordering

```
1. market_data_thread_.reset()   — stop inflow
2. ipc_server_.reset()           — stop IPC (joins thread before components
                                    it queries are destroyed)
3. risk_engine_.reset()          — unsubscribe from risk bus
4. position_engine_.reset()      — unsubscribe from risk bus
5. order_tracker_.reset()        — unsubscribe from risk bus
6. strategy_.reset()             — unsubscribe from strategy bus
7. order_routing_thread_.reset() — stop execution
8. strategy_loop_.stop()         — join strategy thread
9. risk_loop_.stop()             — join risk thread
```

The IPC server is stopped at step 2, before any component it accesses
(`position_engine_`, `risk_engine_`) is destroyed. This prevents the
IPC thread from calling `getSnapshots()` or `isHalted()` on a dead object.

---

## New Files

| File | Role |
|------|------|
| `core/app/include/quant/network/ipc_server.hpp` | Dual-socket IPC server |
| `core/app/src/network/ipc_server.cpp` | Implementation |

## Modified Files

| File | Change |
|------|--------|
| `core/app/include/quant/risk/position_engine.hpp` | Added `shared_mutex`, `getSnapshots()` |
| `core/app/src/risk/position_engine.cpp` | Locking in `onFill()`, `hydratePosition()`, `position()`, `getSnapshots()` |
| `core/app/include/quant/risk/risk_engine.hpp` | `halt_trading_` → `atomic<bool>`, added `haltTrading()`, `isHalted()` |
| `core/app/src/risk/risk_engine.cpp` | Atomic operations, new method implementations |
| `core/engine/include/quant/engine/trading_engine.hpp` | `IpcServer` member, `executeCommand()`, IPC endpoint params |
| `core/engine/src/trading_engine.cpp` | Wire IPC server, telemetry bridges, `executeCommand()` |
| `core/app/CMakeLists.txt` | Added `ipc_server.cpp` |
| `tests/trading_engine_test.cpp` | Pass empty IPC endpoints |

---

## Test Results

```
100% tests passed, 0 tests failed out of 25
Total Test time (real) =   0.08 sec
```
