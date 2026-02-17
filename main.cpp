// -----------------------------------------------------------------------------
// quant_engine — single executable entry point (per architecture.md).
//
// This program wires two EventLoopThread instances:
//   - strategy_loop        (Strategy Thread)
//   - risk_execution_loop  (Risk + Execution Thread)
//
// Event flow (end-to-end):
//   MarketDataEvent (strategy_loop)
//     -> DummyStrategy publishes SignalEvent (strategy_loop)
//     -> forwarder pushes SignalEvent into risk_execution_loop via push()
//     -> RiskEngine converts SignalEvent -> OrderEvent (risk_execution_loop)
//     -> ExecutionEngine converts OrderEvent -> ExecutionReportEvent
//     -> main() subscriber logs the ExecutionReportEvent.
//
// All cross-thread communication uses EventLoopThread::push(Event).
// All intra-thread communication uses EventBus::publish(Event).
// No global state; main() owns all objects and threads.
// -----------------------------------------------------------------------------

#include "quant/concurrent/event_loop_thread.hpp"
#include "quant/events/event_types.hpp"          // MarketDataEvent, SignalEvent
#include "quant/events/execution_report_event.hpp"  // ExecutionReportEvent
#include "quant/risk/risk_engine.hpp"
#include "quant/execution/execution_engine.hpp"
#include "quant/strategy/dummy_strategy.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  // -------------------------------------------------------------------------
  // Thread ownership and lifetime
  // -------------------------------------------------------------------------
  // main() owns both EventLoopThread instances. There is no global state.
  // - strategy_loop: hosts DummyStrategy and receives market data.
  // - risk_execution_loop: hosts RiskEngine and ExecutionEngine.
  // start()/stop() and push() are called from main() and from callbacks on
  // these threads only.
  // -------------------------------------------------------------------------
  quant::EventLoopThread strategy_loop;
  quant::EventLoopThread risk_execution_loop;

  strategy_loop.start();
  risk_execution_loop.start();

  // -------------------------------------------------------------------------
  // Forward SignalEvent from strategy_loop to risk_execution_loop.
  // -------------------------------------------------------------------------
  // Subscriber runs on the strategy_loop thread when DummyStrategy publishes
  // SignalEvent. push() is thread-safe, so we enqueue the event for the risk
  // loop. Strategy never calls execution directly—only events cross the
  // boundary.
  // -------------------------------------------------------------------------
  strategy_loop.eventBus().subscribe<quant::SignalEvent>(
      [&risk_execution_loop](const quant::SignalEvent& e) {
        risk_execution_loop.push(e);
      });

  // -------------------------------------------------------------------------
  // DummyStrategy: subscribes to MarketDataEvent on strategy_loop's bus,
  // emits SignalEvent when its simple condition is met. Runs on
  // strategy_loop thread.
  // -------------------------------------------------------------------------
  quant::DummyStrategy strategy(strategy_loop.eventBus());

  // -------------------------------------------------------------------------
  // RiskEngine and ExecutionEngine live on risk_execution_loop.
  // -------------------------------------------------------------------------
  // Both are constructed with risk_execution_loop.eventBus(), so their
  // callbacks (onSignal, onOrder) run on the risk/execution thread. They do
  // not know about threads directly; they only see events.
  // -------------------------------------------------------------------------
  quant::RiskEngine risk_engine(risk_execution_loop.eventBus());
  quant::ExecutionEngine execution_engine(risk_execution_loop.eventBus());

  // -------------------------------------------------------------------------
  // Risk/execution side: log when SignalEvent is received.
  // -------------------------------------------------------------------------
  // This callback runs on the risk_execution_loop thread, proving the signal
  // crossed threads (strategy_loop -> push -> risk_execution_loop -> publish).
  // -------------------------------------------------------------------------
  risk_execution_loop.eventBus().subscribe<quant::SignalEvent>(
      [](const quant::SignalEvent& e) {
        std::cout << "[Risk/Execution] received SignalEvent: strategy="
                  << e.strategy_id << " symbol=" << e.symbol
                  << " side="
                  << (e.side == quant::SignalEvent::Side::Buy ? "Buy" : "Sell")
                  << " strength=" << e.strength << "\n";
      });

  // -------------------------------------------------------------------------
  // Risk/execution side: log ExecutionReportEvent to prove full lifecycle.
  // -------------------------------------------------------------------------
  risk_execution_loop.eventBus().subscribe<quant::ExecutionReportEvent>(
      [](const quant::ExecutionReportEvent& e) {
        std::cout << "[ExecutionReport] order_id=" << e.order_id
                  << " status="
                  << (e.status == quant::ExecutionStatus::Filled ? "Filled"
                                                                  : "Rejected")
                  << " qty=" << e.filled_quantity
                  << " price=" << e.fill_price << "\n";
      });

  // -------------------------------------------------------------------------
  // Inject a single MarketDataEvent into strategy_loop.
  // -------------------------------------------------------------------------
  // Flow:
  //   1) strategy_loop pops and publishes MarketDataEvent.
  //   2) DummyStrategy publishes SignalEvent.
  //   3) Forwarder pushes SignalEvent into risk_execution_loop.
  //   4) risk_execution_loop pops and publishes SignalEvent.
  //   5) RiskEngine publishes OrderEvent.
  //   6) ExecutionEngine publishes ExecutionReportEvent.
  //   7) ExecutionReport subscriber logs the fill.
  // -------------------------------------------------------------------------
  quant::MarketDataEvent md;
  md.symbol = "AAPL";
  md.price = 150.25;
  md.quantity = 100.0;
  md.timestamp = std::chrono::system_clock::now();
  md.sequence_id = 1;

  strategy_loop.push(md);

  // Give the pipeline time to process all events across both threads.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  strategy_loop.stop();
  risk_execution_loop.stop();

  return 0;
}

