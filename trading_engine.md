
# Quant Engine — Master Architecture & Implementation Plan

## Project Overview

This document captures the full architectural roadmap of the Quant Trading Engine, including:

* What has been implemented so far
* Architectural decisions taken
* Remaining implementation roadmap
* Detailed phase-by-phase evolution plan

The system follows a strict event-driven, multi-threaded architecture with clean ownership boundaries, no global mutable state, and high-performance C/C++ libraries powering the core strategy engine.

---

## ✅ Current State — Phase 1 Complete

### Architectural Foundation

**Core Principles**

* No global mutable state
* Event-driven communication only
* Strict thread ownership boundaries
* Deterministic lifecycle management
* RAII-compliant resource cleanup
* Modular CMake structure

---

### Current Module Layout

```text
core_lib
 ├── EventBus
 ├── EventLoopThread
 ├── Events
 ├── DummyStrategy
 ├── RiskEngine
 └── ExecutionEngine

engine_lib
 └── TradingEngine (orchestrator)

app
 └── quant_engine (binary)

```

---

### Thread Architecture

**Strategy Thread (strategy_loop)**

* EventBus
* DummyStrategy

**Risk/Execution Thread (risk_execution_loop)**

* RiskEngine
* ExecutionEngine

All cross-thread communication happens via: `EventLoopThread::push(Event)`
All module interactions happen via: `EventBus::publish(const Event&)`

---

### Phase 1 Features Implemented

* TradingEngine orchestrator
* Two dedicated event loop threads
* Cross-thread signal forwarder
* Strategy → Risk → Execution pipeline
* Full end-to-end integration test
* Idempotent start()/stop()
* RAII-safe destructor
* Multi-event handling
* Clean modular CMake structure

---

### Current Runtime Flow

```text
MarketDataEvent 
   ↓
DummyStrategy 
   ↓
SignalEvent 
   ↓ (cross-thread push)
RiskEngine 
   ↓
OrderEvent 
   ↓
ExecutionEngine 
   ↓
ExecutionReportEvent

```

System is stable, deterministic, and test-verified.

---

## 🚀 Phase Roadmap — Full Evolution Plan

---

## 🟡 PHASE 2 — Backtesting & Simulation Engine (Moved Up)

**Objective**
Build the deterministic simulation environment *before* introducing complex state. This ensures the live engine and backtester process data tick-by-tick using the exact same event-driven flow, eliminating look-ahead bias.

**2.1 Historical Data Replay**

* Parse historical tick/candle data.
* Publish `MarketDataEvent` matching the live feed format.

**2.2 Simulation Clock**

* Decouple system time from engine time.
* Drive all events via a simulated, strictly controlled timestamp.

**2.3 Mock Execution Engine**

* Simulate immediate fills for early testing.
* Ensure reproducible results across identical runs.

---

## 🟠 PHASE 3 — Stateful Trading Engine & Reconciliation

**Objective**
Introduce persistent trading state, realistic order lifecycles, and synchronization with the exchange.

**3.1 PositionEngine**

* Track net position per symbol.
* Maintain average price, realized PnL, and unrealized PnL.
* Lives on `risk_execution_loop`; Subscribes to `ExecutionReportEvent`.

**3.2 Order State Machine**

* Replace immediate fills with deterministic transitions: `New` -> `Accepted` -> `PartiallyFilled` -> `Filled` -> `Rejected` -> `Canceled`.

**3.3 Exchange State Reconciliation (Crucial)**

* Query exchange REST API on startup.
* Sync local `PositionEngine` with actual broker state before enabling strategies.

**3.4 Central OrderId Generator**

* Atomic, thread-safe, and deterministic ID sequence.

---

## 🔵 PHASE 4 — External Gateways & Control (The Python/Bot Bridge)

**Objective**
Implement secure, non-blocking communication between the C++ engine and external interfaces (e.g., Telegram bot, Market feeds).

**4.1 Network Layer Threads**

* `MarketDataThread`: Connects to Exchange WebSockets, parses JSON/FIX, and pushes `MarketDataEvent` to the core bus.
* `OrderRoutingThread`: Handles REST API calls to the exchange for execution.

**4.2 ZeroMQ (ØMQ) IPC Server**

* Implement ZeroMQ REQ/REP pattern for inter-process communication.
* Allow the Python Telegram bot to asynchronously request PnL, pause strategies, or query positions without blocking the C++ strategy thread.

**4.3 Security Hardening**

* Role-Based Access Control (RBAC) in the Telegram bot (strict User ID filtering).
* API Key permission enforcement (trading only, no withdrawals).

---

## 🟣 PHASE 5 — Multi-Strategy Architecture

**Objective**
Support multiple independent strategies running concurrently.

**5.1 StrategyManager**

* Owns multiple `IStrategy` instances.
* Broadcasts `MarketDataEvent` to active strategies.

**5.2 Strategy Interface**

* Introduce `class IStrategy`.
* Strategies become pluggable, dynamically configurable modules.

**5.3 Per-Strategy Risk Limits**

* Max position per strategy, max order size, and exposure caps enforced by `RiskEngine`.

---

## 🔴 PHASE 6 — Infrastructure Hardening

**6.1 Blocking Queue Upgrades**

* Replace polling queue with `condition_variable`-based blocking queue.

**6.2 Structured Logging**

* Thread-safe logger with severity levels, timestamps, and thread IDs.

**6.3 Configuration System**

* Load risk limits, strategy parameters, and thread settings from JSON/YAML.

**6.4 Metrics System**

* Track event throughput, queue depth, processing latency, and order turnaround time.

---

## 🟢 PHASE 7 — Persistence & Trade Journaling

**7.1 Trade Journal**

* Asynchronously persist Orders, Execution reports, and Positions to disk (SQLite or flat file).

**7.2 Recovery on Startup**

* Load local journal to rebuild state.
* Cross-reference with Phase 3.3 (Exchange Reconciliation) to ensure accuracy.

---

## ⚫ PHASE 8 — Advanced Execution Models

**Objective**
Introduce high-fidelity market realities to the simulation and live risk checks.

**8.1 Fixed-Point Arithmetic Engine**

* Migrate all pricing and PnL math to fixed-point integers (e.g., Price * 100,000,000) to eliminate floating-point precision drift.

**8.2 Slippage & Impact Modeling**

* Execution price modeling: `Theoretical Price ± (Slippage Model + Market Impact)`

**8.3 Latency & Liquidity Simulation**

* Artificial network delay injection.
* Simulated order book depth and partial liquidity handling.

---

## ⚪ PHASE 9 — Portfolio Layer

**9.1 PortfolioEngine**

* Tracks gross exposure, net exposure, capital usage, and cross-symbol aggregation.

**9.2 Portfolio Risk Limits**

* Max drawdown, gross exposure limits, and sector concentration limits.

---

## 🟤 PHASE 10 — Performance Optimization

**10.1 Lock-Free Queues**

* Optional replacement for blocking queues in hot-paths.

**10.2 Memory Optimization**

* Implement object pools (Ring Buffers).
* Strictly avoid hot-path heap allocations during live trading.

**10.3 Latency Benchmarking**

* Measure cross-thread handoff latency and end-to-end processing time.

---

## 🏁 PHASE 11 — Production Consolidation & System Stability

**11.1 Architectural Hardening Review**

* Re-audit thread boundaries; remove accidental cross-thread publish paths.
* Enforce strict ownership contracts and eliminate test-only shortcuts.

**11.2 Stability & Stress Testing**

* High event-rate stress tests and long-duration soak tests.
* Shutdown/startup cycling tests and memory growth validation.

**11.3 Codebase Simplification**

* Remove experimental abstractions, reduce over-engineering, and lock public interfaces.

---

### 🎯 Recommended Next Step

**Proceed to: Phase 2 — Backtesting & Simulation Engine**
By building the simulation clock and historical replay first, you guarantee that all subsequent state management, risk limits, and strategies can be tested instantly and accurately without risking live capital or dealing with slow paper-trading environments.

---

Would you like me to draft the C++ header files for the `MarketDataThread` or the ZeroMQ IPC layer next?