# Core Trading Engine Architecture Contract

This project implements a production-grade C++20 trading engine.

## Non-Negotiable Rules

1. Single executable binary.
2. Modular architecture.
3. Event-driven internal communication.
4. Strategy MUST NOT call execution directly.
5. Risk MUST sit between router and execution.
6. No global mutable state.
7. Thread-safe by design.
8. Separation of concerns enforced strictly.
9. All modules communicate via events.
10. Design must support multiple strategies and multiple risk modules.

## Threading Model (Initial Version)

- Market Data Thread
- Strategy Thread (or small pool)
- Risk + Execution Thread
- Logging Thread

## Core Components

- EventBus
- TradingEngine
- StrategyManager
- RiskManager
- OrderRouter
- ExecutionEngine
- PositionManager

## Event Types

- MarketDataEvent
- SignalEvent
- OrderEvent
- RiskRejectEvent
- FillEvent
- HeartbeatEvent

## Scalability Goals

- Easily add new strategy without modifying execution.
- Easily add new risk module without modifying strategy.
- Prepare for future Python monitoring via tcp.
- Internal event-driven design must allow future distributed architecture.

Any code that violates this document must be refactored.