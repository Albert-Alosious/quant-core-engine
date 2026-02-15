// -----------------------------------------------------------------------------
// quant_engine — single executable entry point (per architecture.md).
// This file demonstrates EventBus and base event types: subscribe, publish,
// and unsubscribe. No strategy, risk, or execution logic here.
// -----------------------------------------------------------------------------

#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event.hpp"
#include <chrono>
#include <iostream>

int main() {
  quant::EventBus bus;

  // Typed subscription: only MarketDataEvent will trigger this callback.
  // Lambda receives const quant::MarketDataEvent&; no need for std::get_if.
  auto id = bus.subscribe<quant::MarketDataEvent>(
      [](const quant::MarketDataEvent &e) {
        std::cout << "MarketData: " << e.symbol << " @ " << e.price
                  << " time: " << e.timestamp.time_since_epoch().count()
                  << " quantity: " << e.quantity << "\n";
      });

  // Build a market data event and publish it. The subscriber above will run
  // on this thread before publish() returns (synchronous dispatch).
  quant::MarketDataEvent md;
  md.symbol = "AAPL";
  md.price = 150.25;
  md.timestamp = std::chrono::system_clock::now();

  bus.publish(md);

  // Clean up: remove the subscription so the callback is no longer invoked.
  bus.unsubscribe(id);

  return 0;
}
