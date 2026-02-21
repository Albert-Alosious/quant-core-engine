#include "quant/strategy/dummy_strategy.hpp"
#include "quant/events/event.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to MarketDataEvent
// -----------------------------------------------------------------------------
DummyStrategy::DummyStrategy(EventBus& bus) : bus_(bus) {
  // Subscribe with a lambda that forwards to our member. The lambda captures
  // [this]; when the bus publishes a MarketDataEvent, this callback runs on
  // the loop thread. We store the returned id so the destructor can
  // unsubscribe.
  subscription_id_ = bus_.subscribe<MarketDataEvent>(
      [this](const MarketDataEvent& e) { onMarketData(e); });
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe so no more callbacks
// -----------------------------------------------------------------------------
DummyStrategy::~DummyStrategy() {
  // Unsubscribe even if id is 0 (should not happen). EventBus tolerates
  // unsubscribe of non-existent id by removing nothing.
  bus_.unsubscribe(subscription_id_);
}

// -----------------------------------------------------------------------------
// onMarketData: condition check and SignalEvent publish
// -----------------------------------------------------------------------------
void DummyStrategy::onMarketData(const MarketDataEvent& event) {
  // Simple condition: emit a signal when price is above threshold.
  // For a minimal test, kPriceThreshold is 0.0 so any positive price
  // triggers a signal. Keeps the pipeline easy to demonstrate.
  if (event.price <= kPriceThreshold) {
    return;
  }

  // Build the signal from the market data. Strategy never calls execution—
  // it only publishes an event. The bus and forwarder will deliver it to
  // risk_execution_loop.
  SignalEvent signal;
  signal.strategy_id = kStrategyId;
  signal.symbol = event.symbol;
  signal.side = SignalEvent::Side::Buy;
  signal.strength = 1.0;
  signal.timestamp = std::chrono::system_clock::now();
  signal.sequence_id = event.sequence_id;

  // Publish on the same bus (strategy_loop's bus). Subscribers on this bus
  // (e.g. the forwarder that pushes to risk_execution_loop) will run next.
  bus_.publish(signal);
}

}  // namespace quant
