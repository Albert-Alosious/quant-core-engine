// -----------------------------------------------------------------------------
// quant_engine — single executable entry point (per architecture.md).
//
// Phase 4 "Network Layer Threads":
//   1) Create a SimulationTimeProvider (the engine's clock for backtesting).
//   2) Create the TradingEngine (passing the clock).
//   3) Subscribe logging callbacks to observe the full pipeline.
//   4) Call engine.start() — spawns four threads:
//        strategy_loop     → DummyStrategy callbacks
//        risk_loop          → OrderTracker + PositionEngine + RiskEngine
//        order_routing      → ExecutionEngine
//        market_data        → MarketDataGateway (ZMQ recv loop)
//   5) Wait for SIGINT (Ctrl-C).
//   6) Call engine.stop() — joins all threads, destroys components.
//
// main() no longer creates or runs the MarketDataGateway directly.
// TradingEngine owns the MarketDataThread and manages its lifecycle.
//
// No global mutable state; TradingEngine owns everything.
// SimulationTimeProvider is stack-local in main().
// -----------------------------------------------------------------------------

#include "quant/domain/order_status.hpp"
#include "quant/engine/trading_engine.hpp"
#include "quant/events/event.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/events/order_update_event.hpp"
#include "quant/events/position_update_event.hpp"
#include "quant/time/simulation_time_provider.hpp"

#include <atomic>
#include <csignal>
#include <condition_variable>
#include <iostream>
#include <mutex>

// -----------------------------------------------------------------------------
// Shutdown signalling via condition variable (no global mutable state beyond
// the atomic flag). The signal handler sets the flag and notifies main() to
// wake from its wait.
// -----------------------------------------------------------------------------
static std::atomic<bool> g_shutdown_requested{false};
static std::mutex g_shutdown_mutex;
static std::condition_variable g_shutdown_cv;

static void sigint_handler(int /*signum*/) {
  g_shutdown_requested.store(true);
  g_shutdown_cv.notify_all();
}

int main() {
  // -------------------------------------------------------------------------
  // 1) Create the simulation clock.
  // -------------------------------------------------------------------------
  quant::SimulationTimeProvider sim_clock;

  // -------------------------------------------------------------------------
  // 2) Create the TradingEngine.
  // -------------------------------------------------------------------------
  quant::TradingEngine engine(sim_clock);

  // -------------------------------------------------------------------------
  // 3) Subscribe logging callbacks BEFORE start().
  // -------------------------------------------------------------------------
  engine.strategyEventBus().subscribe<quant::MarketDataEvent>(
      [&sim_clock](const quant::MarketDataEvent& e) {
        std::cout << "[Strategy] MarketDataEvent: symbol=" << e.symbol
                  << " price=" << e.price << " volume=" << e.quantity
                  << " sim_clock=" << sim_clock.now_ms() << "\n";
      });

  engine.riskExecutionEventBus().subscribe<quant::SignalEvent>(
      [](const quant::SignalEvent& e) {
        std::cout << "[Risk] SignalEvent: strategy="
                  << e.strategy_id << " symbol=" << e.symbol << " side="
                  << (e.side == quant::SignalEvent::Side::Buy ? "Buy" : "Sell")
                  << " strength=" << e.strength << "\n";
      });

  engine.riskExecutionEventBus().subscribe<quant::ExecutionReportEvent>(
      [](const quant::ExecutionReportEvent& e) {
        auto status_str = [](quant::ExecutionStatus s) -> const char* {
          switch (s) {
            case quant::ExecutionStatus::Accepted: return "Accepted";
            case quant::ExecutionStatus::Filled:   return "Filled";
            case quant::ExecutionStatus::Rejected:  return "Rejected";
          }
          return "Unknown";
        };
        std::cout << "[ExecutionReport] order_id=" << e.order_id
                  << " status=" << status_str(e.status)
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

  engine.riskExecutionEventBus().subscribe<quant::OrderUpdateEvent>(
      [](const quant::OrderUpdateEvent& e) {
        auto status_str = [](quant::domain::OrderStatus s) -> const char* {
          using S = quant::domain::OrderStatus;
          switch (s) {
            case S::New:              return "New";
            case S::PendingNew:       return "PendingNew";
            case S::Accepted:         return "Accepted";
            case S::PartiallyFilled:  return "PartiallyFilled";
            case S::Filled:           return "Filled";
            case S::Canceled:         return "Canceled";
            case S::Rejected:         return "Rejected";
            case S::Expired:          return "Expired";
          }
          return "Unknown";
        };
        std::cout << "[OrderUpdate] order_id=" << e.order.id
                  << " symbol=" << e.order.symbol
                  << " " << status_str(e.previous_status)
                  << " -> " << status_str(e.order.status) << "\n";
      });

  // -------------------------------------------------------------------------
  // 4) Start the engine (spawns all four threads).
  // -------------------------------------------------------------------------
  engine.start();

  std::cout << "[main] Engine started. 4 threads running.\n"
            << "[main] MarketDataThread listening on tcp://127.0.0.1:5555\n"
            << "[main] Start the Python feeder in another terminal:\n"
            << "       python tools/backtest_feeder/feeder.py\n"
            << "[main] Press Ctrl-C to shut down.\n";

  // -------------------------------------------------------------------------
  // 5) Wait for SIGINT.
  // -------------------------------------------------------------------------
  std::signal(SIGINT, sigint_handler);

  {
    std::unique_lock lock(g_shutdown_mutex);
    g_shutdown_cv.wait(lock, [] { return g_shutdown_requested.load(); });
  }

  // -------------------------------------------------------------------------
  // 6) Clean shutdown.
  // -------------------------------------------------------------------------
  std::cout << "\n[main] SIGINT received. Stopping engine...\n";
  engine.stop();

  return 0;
}
