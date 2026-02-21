#include "quant/risk/risk_engine.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to SignalEvent
// -----------------------------------------------------------------------------
RiskEngine::RiskEngine(EventBus& bus) : bus_(bus) {
  // Subscribe to SignalEvent. The callback runs on the
  // risk_execution_loop thread whenever that loop publishes a SignalEvent.
  subscription_id_ = bus_.subscribe<SignalEvent>(
      [this](const SignalEvent& e) { onSignal(e); });
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe
// -----------------------------------------------------------------------------
RiskEngine::~RiskEngine() { bus_.unsubscribe(subscription_id_); }

// -----------------------------------------------------------------------------
// onSignal: build Order and publish OrderEvent
// -----------------------------------------------------------------------------
void RiskEngine::onSignal(const SignalEvent& event) {
  // Generate a new unique order id. This is safe without atomics because all
  // callbacks run on the risk_execution_loop thread—no concurrent writers.
  domain::OrderId id = next_order_id_++;

  // Build the domain Order. Maps signal fields to order fields:
  // - strategy_id and symbol directly from the signal
  // - side: SignalEvent::Side -> domain::Side
  // - quantity: 1.0 (dummy size; a real engine derives from signal.strength)
  // - price: propagated from the signal's market price so downstream
  //   execution reports and the PositionEngine have the correct fill price
  domain::Order order;
  order.id = id;
  order.strategy_id = event.strategy_id;
  order.symbol = event.symbol;
  order.side = (event.side == SignalEvent::Side::Buy)
                   ? domain::Side::Buy
                   : domain::Side::Sell;
  order.quantity = 1.0;
  order.price = event.price;

  // Wrap in OrderEvent so it flows through the EventBus as part of the
  // Event variant, carrying optional event-level metadata.
  OrderEvent order_event;
  order_event.order = order;
  order_event.timestamp = std::chrono::system_clock::now();
  order_event.sequence_id = event.sequence_id;  // simple reuse for demo

  // Publish back onto the same bus. ExecutionEngine (also on the
  // risk_execution_loop) will subscribe to OrderEvent and react.
  bus_.publish(order_event);
}

}  // namespace quant

