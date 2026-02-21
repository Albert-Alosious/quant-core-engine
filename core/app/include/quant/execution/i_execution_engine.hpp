#pragma once

namespace quant {

// -----------------------------------------------------------------------------
// IExecutionEngine — abstract interface for execution engines
// -----------------------------------------------------------------------------
//
// @brief  Polymorphic base for all execution engine implementations.
//
// @details
// The execution engine is fully event-driven: it subscribes to OrderEvent
// on the EventBus in its constructor and publishes ExecutionReportEvent when
// an order is processed. There are no public "execute" methods — the bus
// drives everything.
//
// Why the interface has no methods beyond the destructor:
//   - The execution engine's entry point is the EventBus callback, not a
//     method call. TradingEngine creates the engine, and after that the bus
//     delivers work to it automatically.
//   - The interface exists so that TradingEngine (or a backtest harness)
//     can hold a std::unique_ptr<IExecutionEngine> and swap between:
//       * ExecutionEngine      — original Phase 1 engine (system_clock time)
//       * MockExecutionEngine  — deterministic simulation engine (ITimeProvider)
//       * A future live broker engine
//     without changing any orchestration code.
//
// Ownership:
//   TradingEngine owns the IExecutionEngine via unique_ptr. The engine
//   internally holds a reference to the EventBus (which TradingEngine also
//   owns, through EventLoopThread).
//
// Thread model:
//   Implementations live on the risk_execution_loop thread. All EventBus
//   callbacks run on that thread.
// -----------------------------------------------------------------------------
class IExecutionEngine {
 public:
  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // @brief  Virtual destructor for safe polymorphic deletion.
  //
  // @details
  // TradingEngine destroys the engine via unique_ptr<IExecutionEngine>.
  // Without a virtual destructor, the derived class's destructor (which
  // unsubscribes from the bus) would not run, leaking the subscription and
  // risking callbacks into freed memory.
  // -------------------------------------------------------------------------
  virtual ~IExecutionEngine() = default;
};

}  // namespace quant
