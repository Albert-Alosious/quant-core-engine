#pragma once

#include "quant/domain/order.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/order_event.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// RiskEngine
// -----------------------------------------------------------------------------
// Responsibility: Listens for SignalEvent on the risk_execution_loop's
// EventBus, converts them into domain::Order objects, wraps them in
// OrderEvent, and publishes them back to the same bus.
//
// Why in architecture:
// - Enforces the rule that strategy never calls execution directly.
// - Serves as the boundary between strategy intent (SignalEvent) and actual
//   orders sent toward execution.
// - Lives on the Risk + Execution Thread (risk_execution_loop) so all
//   risk and order creation logic runs in a single, well-defined thread.
//
// Thread model:
// - RiskEngine is constructed on main() but its callbacks (onSignal) run
//   on the risk_execution_loop thread, because that loop publishes
//   SignalEvent via its EventBus.
// - Internal order id counter is plain std::uint64_t because only this
//   thread updates it—no atomic needed.
// Ownership:
// - RiskEngine does not own the EventBus; it only holds a reference to the
//   bus owned by EventLoopThread.
// -----------------------------------------------------------------------------
class RiskEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // What: Subscribes to SignalEvent on the provided EventBus. When a signal
  // arrives, onSignal() will be invoked on the risk_execution_loop thread.
  // Why: Risk must see all signals that have already crossed from the
  // strategy thread into the risk_execution_loop.
  // Thread-safety: Safe to construct from main() before events start
  // flowing. subscribe() itself is thread-safe.
  // Input: bus — EventBus belonging to risk_execution_loop.
  // Output: None.
  // -------------------------------------------------------------------------
  explicit RiskEngine(EventBus& bus);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // What: Unsubscribes from the SignalEvent stream.
  // Why: RAII cleanup; ensures no callbacks run after RiskEngine is
  // destroyed.
  // Thread-safety: Safe to destroy from main() after the loop is stopped.
  // -------------------------------------------------------------------------
  ~RiskEngine();

  RiskEngine(const RiskEngine&) = delete;
  RiskEngine& operator=(const RiskEngine&) = delete;
  RiskEngine(RiskEngine&&) = delete;
  RiskEngine& operator=(RiskEngine&&) = delete;

 private:
  // -------------------------------------------------------------------------
  // onSignal(event)
  // -------------------------------------------------------------------------
  // What: Converts a SignalEvent into a domain::Order with a new OrderId,
  // wraps it in OrderEvent, and publishes it.
  // Why: Encapsulates the mapping from "strategy intent" to "order to be
  // executed". Keeps order creation logic in the risk layer.
  // Thread model: Runs only on the risk_execution_loop thread.
  // -------------------------------------------------------------------------
  void onSignal(const SignalEvent& event);

  EventBus& bus_;                       // Reference to shared EventBus
  EventBus::SubscriptionId subscription_id_{0};
  std::uint64_t next_order_id_{1};      // Incremented on risk thread only
};

}  // namespace quant

