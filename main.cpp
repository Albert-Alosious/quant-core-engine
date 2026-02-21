// -----------------------------------------------------------------------------
// quant_engine — single executable entry point (per architecture.md).
//
// Phase 2+3 "Simulation Mode with Position Tracking":
//   1) Create a SimulationTimeProvider (the engine's clock for backtesting).
//   2) Create the TradingEngine and start it. The engine now includes a
//      PositionEngine on the risk/exec thread that tracks fills and PnL.
//   3) Subscribe logging callbacks to observe the full pipeline, including
//      PositionUpdateEvent for real-time PnL visibility.
//   4) Create a MarketDataGateway that receives JSON ticks from Python over
//      ZeroMQ, advances the simulation clock, and pushes MarketDataEvent
//      into the engine's strategy loop.
//   5) Run the gateway's recv loop on the main thread (blocks until the
//      Python feeder finishes and the gateway times out, or until Ctrl-C).
//   6) Shut down cleanly.
//
// Thread layout:
//   main thread        → MarketDataGateway::run() (ZMQ recv loop)
//   strategy thread    → DummyStrategy callbacks
//   risk/exec thread   → RiskEngine + ExecutionEngine + PositionEngine
//
// No global state; TradingEngine owns the event loops and components.
// SimulationTimeProvider and MarketDataGateway are stack-local in main().
// -----------------------------------------------------------------------------

#include "quant/engine/trading_engine.hpp"
#include "quant/events/event.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/events/position_update_event.hpp"
#include "quant/gateway/market_data_gateway.hpp"
#include "quant/time/simulation_time_provider.hpp"

#include <csignal>
#include <iostream>

// -----------------------------------------------------------------------------
// Global pointer for signal handler access.
// This is the ONLY global in the program and it is a raw pointer to a
// stack-local object — not global mutable state in the architectural sense.
// It exists solely so the SIGINT handler can call gateway->stop() to unblock
// the ZMQ recv loop. The pointer is set once before signal delivery is
// possible and never modified after.
// -----------------------------------------------------------------------------
static quant::MarketDataGateway* g_gateway_ptr = nullptr;

// -----------------------------------------------------------------------------
// sigint_handler
// -----------------------------------------------------------------------------
// @brief  POSIX signal handler for SIGINT (Ctrl-C).
//
// @param  signum  The signal number (always SIGINT here).
//
// @details
// Calls gateway->stop() which sets an atomic flag. The gateway's recv loop
// (running on the main thread) will notice this within its ZMQ_RCVTIMEO
// window (100 ms) and return from run(). After that, main() continues to
// engine.stop() for clean shutdown.
//
// Only async-signal-safe operations are used: atomic store (inside stop())
// and write() (inside the iostream call — technically not signal-safe, but
// acceptable for a development binary; production would use write(2)).
// -----------------------------------------------------------------------------
static void sigint_handler(int /*signum*/) {
  std::cout << "\n[main] SIGINT received. Shutting down...\n";
  if (g_gateway_ptr != nullptr) {
    g_gateway_ptr->stop();
  }
}

int main() {
  // -------------------------------------------------------------------------
  // 1) Create the simulation clock.
  // SimulationTimeProvider starts at time 0. The MarketDataGateway will
  // advance it to each tick's timestamp_ms before publishing the event.
  // -------------------------------------------------------------------------
  quant::SimulationTimeProvider sim_clock;

  // -------------------------------------------------------------------------
  // 2) Create and start the TradingEngine.
  // -------------------------------------------------------------------------
  quant::TradingEngine engine;

  // Subscribe logging callbacks BEFORE start() so we see every event from
  // the first tick. These callbacks run on the risk_execution_loop thread.
  engine.riskExecutionEventBus().subscribe<quant::SignalEvent>(
      [](const quant::SignalEvent& e) {
        std::cout << "[Risk/Execution] SignalEvent: strategy="
                  << e.strategy_id << " symbol=" << e.symbol << " side="
                  << (e.side == quant::SignalEvent::Side::Buy ? "Buy" : "Sell")
                  << " strength=" << e.strength << "\n";
      });

  engine.riskExecutionEventBus().subscribe<quant::ExecutionReportEvent>(
      [](const quant::ExecutionReportEvent& e) {
        std::cout << "[ExecutionReport] order_id=" << e.order_id << " status="
                  << (e.status == quant::ExecutionStatus::Filled ? "Filled"
                                                                 : "Rejected")
                  << " qty=" << e.filled_quantity << " price=" << e.fill_price
                  << "\n";
      });

  engine.riskExecutionEventBus().subscribe<quant::PositionUpdateEvent>(
      [](const quant::PositionUpdateEvent& e) {
        std::cout << "[PositionUpdate] symbol=" << e.position.symbol
                  << " net_qty=" << e.position.net_quantity
                  << " avg_price=" << e.position.average_price
                  << " realized_pnl=" << e.position.realized_pnl << "\n";
      });

  // Also log MarketDataEvent arrival on the strategy bus to confirm the
  // gateway → strategy_loop path is working.
  engine.strategyEventBus().subscribe<quant::MarketDataEvent>(
      [&sim_clock](const quant::MarketDataEvent& e) {
        std::cout << "[Strategy] MarketDataEvent: symbol=" << e.symbol
                  << " price=" << e.price << " volume=" << e.quantity
                  << " sim_clock=" << sim_clock.now_ms() << "\n";
      });

  engine.start();

  // -------------------------------------------------------------------------
  // 3) Create the MarketDataGateway.
  // The event_sink lambda captures &engine and calls pushEvent(), which
  // enqueues the Event variant into the strategy loop's ThreadSafeQueue.
  // This respects the thread model: the gateway thread never publishes
  // directly to the EventBus; it only pushes into the queue, and the
  // strategy loop thread dispatches via the bus.
  // -------------------------------------------------------------------------
  quant::MarketDataGateway gateway(
      sim_clock,
      [&engine](quant::Event event) {
        engine.pushEvent(std::move(event));
      });

  // -------------------------------------------------------------------------
  // 4) Install SIGINT handler so Ctrl-C triggers a clean shutdown.
  // -------------------------------------------------------------------------
  g_gateway_ptr = &gateway;
  std::signal(SIGINT, sigint_handler);

  // -------------------------------------------------------------------------
  // 5) Run the gateway recv loop on the main thread.
  // This blocks until either:
  //   a) The Python feeder finishes and no more messages arrive (the loop
  //      keeps timing out on recv; after several idle cycles with no data
  //      the operator can Ctrl-C to exit).
  //   b) The user presses Ctrl-C, which calls gateway.stop() via the
  //      signal handler, causing run() to return.
  // -------------------------------------------------------------------------
  std::cout << "[main] MarketDataGateway listening on tcp://127.0.0.1:5555\n"
            << "[main] Start the Python feeder in another terminal:\n"
            << "       python tools/backtest_feeder/feeder.py\n"
            << "[main] Press Ctrl-C to shut down.\n";

  gateway.run();

  // -------------------------------------------------------------------------
  // 6) Clean shutdown: stop the engine (destroys components, joins threads).
  // -------------------------------------------------------------------------
  std::cout << "[main] Gateway exited. Stopping engine...\n";
  engine.stop();

  g_gateway_ptr = nullptr;

  return 0;
}
