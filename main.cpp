// -----------------------------------------------------------------------------
// quant_engine — single executable entry point (per architecture.md).
// Wires strategy_loop and risk_execution_loop: DummyStrategy emits SignalEvent
// on market data; forwarder pushes SignalEvent to risk loop; risk loop logs
// receipt. Proves cross-thread event flow with no global state.
// -----------------------------------------------------------------------------

#include "quant/concurrent/event_loop_thread.hpp"
#include "quant/events/event_types.hpp"
#include "quant/strategy/dummy_strategy.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  // -------------------------------------------------------------------------
  // Thread ownership: main owns both loops. No global state—they are local
  // variables. start()/stop() and push() are called from main or from loop
  // thread callbacks; ownership stays clear.
  // -------------------------------------------------------------------------
  quant::EventLoopThread strategy_loop;
  quant::EventLoopThread risk_execution_loop;

  strategy_loop.start();
  risk_execution_loop.start();

  // -------------------------------------------------------------------------
  // Forward SignalEvent from strategy_loop to risk_execution_loop.
  // Subscriber runs on the strategy_loop thread when DummyStrategy publishes
  // SignalEvent. push() is thread-safe, so we enqueue the event for the
  // risk loop. Strategy never calls execution—only events cross the boundary.
  // -------------------------------------------------------------------------
  strategy_loop.eventBus().subscribe<quant::SignalEvent>(
      [&risk_execution_loop](const quant::SignalEvent& e) {
        risk_execution_loop.push(e);
      });

  // -------------------------------------------------------------------------
  // DummyStrategy: subscribes to MarketDataEvent on strategy_loop's bus,
  // emits SignalEvent when price > threshold. Runs on strategy_loop thread.
  // -------------------------------------------------------------------------
  quant::DummyStrategy strategy(strategy_loop.eventBus());

  // -------------------------------------------------------------------------
  // Risk/execution side: log when SignalEvent is received. This callback
  // runs on the risk_execution_loop thread, proving the event crossed
  // threads (strategy_loop -> push -> risk_execution_loop -> publish -> here).
  // -------------------------------------------------------------------------
  risk_execution_loop.eventBus().subscribe<quant::SignalEvent>(
      [](const quant::SignalEvent& e) {
        std::cout << "[Risk/Execution] received SignalEvent: strategy="
                  << e.strategy_id << " symbol=" << e.symbol
                  << " side=" << (e.side == quant::SignalEvent::Side::Buy ? "Buy" : "Sell")
                  << " strength=" << e.strength << "\n";
      });

  // -------------------------------------------------------------------------
  // Inject market data into strategy_loop. The loop will pop it, publish it,
  // DummyStrategy will see it and (if condition met) publish SignalEvent,
  // forwarder will push to risk_execution_loop, risk loop will pop and
  // publish and the logger will run.
  // -------------------------------------------------------------------------
  quant::MarketDataEvent md;
  md.symbol = "AAPL";
  md.price = 150.25;
  md.quantity = 100.0;
  md.timestamp = std::chrono::system_clock::now();
  md.sequence_id = 1;

  strategy_loop.push(md);

  // Give the pipeline time to process: strategy_loop pop -> publish ->
  // DummyStrategy -> SignalEvent -> forwarder push -> risk_loop pop ->
  // publish -> log. A short sleep is sufficient for this test.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  strategy_loop.stop();
  risk_execution_loop.stop();

  return 0;
}
