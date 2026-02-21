#pragma once

#include "quant/domain/order.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/execution/i_execution_engine.hpp"
#include "quant/events/order_event.hpp"
#include "quant/events/execution_report_event.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// ExecutionEngine
// -----------------------------------------------------------------------------
// Responsibility: Listens for OrderEvent on its thread's EventBus, simulates
// an immediate fill, and publishes an ExecutionReportEvent.
//
// Why in architecture:
// - Separates risk (order creation and approval) from execution (sending
//   orders to a broker or exchange and handling fills).
// - Allows swapping the execution layer (simulation, paper, real broker)
//   without touching strategy or risk.
//
// Thread model:
// - Lives on the OrderRoutingThread's EventLoopThread (Phase 4+).
// - All callbacks run on that thread; no internal locking is needed.
// Ownership:
// - Holds a reference to the EventBus owned by the OrderRoutingThread's
//   internal EventLoopThread.
// -----------------------------------------------------------------------------
class ExecutionEngine final : public IExecutionEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // What: Subscribes to OrderEvent on the given bus. For each order, emits an
  // ExecutionReportEvent representing a full fill.
  // Why: Provides a minimal, synchronous execution simulation so we can test
  // the end-to-end event flow without connecting to a real exchange.
  // Thread-safety: Safe to construct from main() before events start
  // flowing. subscribe() itself is thread-safe.
  // Input: bus — EventBus belonging to risk_execution_loop.
  // Output: None.
  // -------------------------------------------------------------------------
  explicit ExecutionEngine(EventBus& bus);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // What: Unsubscribes from OrderEvent.
  // Why: RAII cleanup; prevents callbacks running after destruction.
  // Thread-safety: Safe to destroy from main() after the loop is stopped.
  // -------------------------------------------------------------------------
  ~ExecutionEngine() override;

  ExecutionEngine(const ExecutionEngine&) = delete;
  ExecutionEngine& operator=(const ExecutionEngine&) = delete;
  ExecutionEngine(ExecutionEngine&&) = delete;
  ExecutionEngine& operator=(ExecutionEngine&&) = delete;

 private:
  // -------------------------------------------------------------------------
  // onOrder(event)
  // -------------------------------------------------------------------------
  // What: Converts an OrderEvent into an ExecutionReportEvent with a
  // simulated full fill.
  // Why: Models the execution layer as a black box that turns orders into
  // fills. In a real engine this would send to a broker API and react to
  // asynchronous responses.
  // Thread model: Runs only on the risk_execution_loop thread.
  // -------------------------------------------------------------------------
  void onOrder(const OrderEvent& event);

  EventBus& bus_;
  EventBus::SubscriptionId subscription_id_{0};
};

}  // namespace quant

