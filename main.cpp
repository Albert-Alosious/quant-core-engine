// -----------------------------------------------------------------------------
// quant_engine — single executable entry point (per architecture.md).
//
// TradingEngine owns all threads and components. main() only needs to:
//   1) Create the engine.
//   2) Start it.
//   3) Subscribe for logging (optional).
//   4) Inject test market data.
//   5) Wait and shut down.
//
// No global state; TradingEngine owns everything.
// -----------------------------------------------------------------------------

#include "quant/engine/trading_engine.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/execution_report_event.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  quant::TradingEngine engine;

  // -------------------------------------------------------------------------
  // Subscribe for logging BEFORE start so we see all events from the first
  // tick. Subscribers registered on the risk_execution_loop bus will have
  // their callbacks run on that loop's worker thread.
  // -------------------------------------------------------------------------
  engine.riskExecutionEventBus().subscribe<quant::SignalEvent>(
      [](const quant::SignalEvent &e) {
        std::cout << "[Risk/Execution] received SignalEvent: strategy="
                  << e.strategy_id << " symbol=" << e.symbol << " side="
                  << (e.side == quant::SignalEvent::Side::Buy ? "Buy" : "Sell")
                  << " strength=" << e.strength << "\n";
      });

  engine.riskExecutionEventBus().subscribe<quant::ExecutionReportEvent>(
      [](const quant::ExecutionReportEvent &e) {
        std::cout << "[ExecutionReport] order_id=" << e.order_id << " status="
                  << (e.status == quant::ExecutionStatus::Filled ? "Filled"
                                                                 : "Rejected")
                  << " qty=" << e.filled_quantity << " price=" << e.fill_price
                  << "\n";
      });

  // -------------------------------------------------------------------------
  // Start the engine — spawns threads, wires components.
  // -------------------------------------------------------------------------
  engine.start();

  // -------------------------------------------------------------------------
  // Inject a single test MarketDataEvent.
  // Flow: MarketDataEvent → DummyStrategy → SignalEvent → (cross-thread)
  //       → RiskEngine → OrderEvent → ExecutionEngine → ExecutionReportEvent
  // -------------------------------------------------------------------------
  quant::MarketDataEvent md;
  md.symbol = "AAPL";
  md.price = 150.25;
  md.quantity = 100.0;
  md.timestamp = std::chrono::system_clock::now();
  md.sequence_id = 1;

  engine.pushMarketData(md);

  // Give the pipeline time to process across both threads.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // -------------------------------------------------------------------------
  // Shutdown — joins all threads.
  // -------------------------------------------------------------------------
  engine.stop();

  return 0;
}
