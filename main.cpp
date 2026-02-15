#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event.hpp"
#include <chrono>
#include <iostream>

int main() {
  quant::EventBus bus;

  auto id = bus.subscribe<quant::MarketDataEvent>(
      [](const quant::MarketDataEvent& e) {
        std::cout << "MarketData: " << e.symbol << " @ " << e.price << "\n";
      });

  quant::MarketDataEvent md;
  md.symbol = "AAPL";
  md.price = 150.25;
  md.quantity = 100.0;
  md.timestamp = std::chrono::system_clock::now();

  bus.publish(md);

  bus.unsubscribe(id);
  return 0;
}
