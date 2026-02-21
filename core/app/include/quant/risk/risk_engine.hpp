#pragma once

#include "quant/concurrent/order_id_generator.hpp"
#include "quant/domain/order.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/order_event.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// RiskEngine
// -----------------------------------------------------------------------------
//
// @brief  Converts strategy signals into executable orders.
//
// @details
// Listens for SignalEvent on the risk_execution_loop's EventBus, converts
// them into domain::Order objects, wraps them in OrderEvent, and publishes
// them back to the same bus.
//
// Why in architecture:
//   Enforces the rule that strategy never calls execution directly.
//   Serves as the boundary between strategy intent (SignalEvent) and actual
//   orders sent toward execution.
//   Lives on the Risk + Execution Thread (risk_execution_loop) so all
//   risk and order creation logic runs in a single, well-defined thread.
//
// Thread model:
//   RiskEngine is constructed on main() but its callbacks (onSignal) run
//   on the risk_execution_loop thread, because that loop publishes
//   SignalEvent via its EventBus.
//   Order IDs are obtained from an externally-owned OrderIdGenerator
//   (std::atomic), which is thread-safe even though today only this
//   thread calls it.
//
// Ownership:
//   RiskEngine does not own the EventBus or the OrderIdGenerator; it holds
//   references to both. The EventBus is owned by EventLoopThread and the
//   OrderIdGenerator is owned by TradingEngine. Both outlive RiskEngine.
// -----------------------------------------------------------------------------
class RiskEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  //
  // @brief  Subscribes to SignalEvent on the provided EventBus.
  //
  // @param  bus     EventBus belonging to risk_execution_loop.
  // @param  id_gen  Thread-safe ID generator owned by TradingEngine.
  //
  // @details
  // When a signal arrives, onSignal() will be invoked on the
  // risk_execution_loop thread. The id_gen reference must remain valid
  // for the entire lifetime of this RiskEngine instance.
  //
  // Thread-safety: Safe to construct from main() before events start
  // flowing. subscribe() itself is thread-safe.
  // -------------------------------------------------------------------------
  RiskEngine(EventBus& bus, OrderIdGenerator& id_gen);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  //
  // @brief  Unsubscribes from the SignalEvent stream.
  //
  // @details
  // RAII cleanup; ensures no callbacks run after RiskEngine is destroyed.
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
  //
  // @brief  Converts a SignalEvent into a domain::Order with a new OrderId,
  //         wraps it in OrderEvent, and publishes it.
  //
  // @param  event  The incoming signal from a strategy.
  //
  // @details
  // Encapsulates the mapping from "strategy intent" to "order to be
  // executed". Keeps order creation logic in the risk layer. The unique
  // order ID is obtained from the injected OrderIdGenerator.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Publishes an OrderEvent to the EventBus.
  // -------------------------------------------------------------------------
  void onSignal(const SignalEvent& event);

  EventBus& bus_;
  OrderIdGenerator& id_gen_;
  EventBus::SubscriptionId subscription_id_{0};
};

}  // namespace quant

