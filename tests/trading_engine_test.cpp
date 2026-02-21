// =============================================================================
// trading_engine_test.cpp
// =============================================================================
// Unit tests for quant::TradingEngine.
//
// Validates:
//   - Lifecycle: start() / stop() / destructor
//   - pushMarketData() drives the full pipeline end-to-end
//   - Idempotent start() and stop()
//   - RAII: destructor stops threads even without explicit stop()
//   - EventBus accessors allow external subscribers
//
// Design: Each test creates its own TradingEngine. No global state.
// =============================================================================

#include "quant/engine/trading_engine.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/time/simulation_time_provider.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>

// Helper: build a MarketDataEvent.
static quant::MarketDataEvent makeMD(const std::string& symbol, double price,
                                      std::uint64_t seq = 1) {
  quant::MarketDataEvent md;
  md.symbol = symbol;
  md.price = price;
  md.quantity = 100.0;
  md.timestamp = std::chrono::system_clock::now();
  md.sequence_id = seq;
  return md;
}

// Shared simulation clock for all TradingEngine tests.
class TradingEngineTestFixture : public ::testing::Test {
 protected:
  quant::SimulationTimeProvider sim_clock;
};

// -----------------------------------------------------------------------------
// 1. Full end-to-end via TradingEngine: pushMarketData must produce an
//    ExecutionReportEvent on the risk_execution_loop.
// -----------------------------------------------------------------------------
TEST_F(TradingEngineTestFixture, FullPipelineEndToEnd) {
  quant::TradingEngine engine(sim_clock, "", "", "");

  std::promise<quant::ExecutionReportEvent> promise;
  auto future = promise.get_future();

  engine.riskExecutionEventBus().subscribe<quant::ExecutionReportEvent>(
      [&promise](const quant::ExecutionReportEvent& e) {
        if (e.status == quant::ExecutionStatus::Filled) {
          promise.set_value(e);
        }
      });

  engine.start();
  engine.pushMarketData(makeMD("AAPL", 150.25));

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready)
      << "Timed out — pipeline did not produce ExecutionReportEvent";

  auto report = future.get();
  EXPECT_EQ(report.status, quant::ExecutionStatus::Filled);
  EXPECT_EQ(report.order_id, 1u);
  EXPECT_DOUBLE_EQ(report.filled_quantity, 1.0);

  engine.stop();
}

// -----------------------------------------------------------------------------
// 2. Idempotent start: calling start() twice must not crash or spawn extra
//    threads.
// -----------------------------------------------------------------------------
TEST_F(TradingEngineTestFixture, IdempotentStart) {
  quant::TradingEngine engine(sim_clock, "", "", "");

  EXPECT_NO_FATAL_FAILURE(engine.start());
  EXPECT_NO_FATAL_FAILURE(engine.start());

  engine.stop();
}

// -----------------------------------------------------------------------------
// 3. Idempotent stop: calling stop() without start(), or calling stop() twice,
//    must not crash.
// -----------------------------------------------------------------------------
TEST_F(TradingEngineTestFixture, IdempotentStop) {
  quant::TradingEngine engine(sim_clock, "", "", "");
  EXPECT_NO_FATAL_FAILURE(engine.stop());

  engine.start();
  EXPECT_NO_FATAL_FAILURE(engine.stop());
  EXPECT_NO_FATAL_FAILURE(engine.stop());
}

// -----------------------------------------------------------------------------
// 4. RAII: TradingEngine destructor must stop threads even without explicit
//    stop(). If the destructor fails to join threads, this test may hang
//    or crash with a destroyed-while-running error.
// -----------------------------------------------------------------------------
TEST_F(TradingEngineTestFixture, DestructorStopsThreads) {
  {
    quant::TradingEngine engine(sim_clock, "", "", "");
    engine.start();
    engine.pushMarketData(makeMD("GOOG", 175.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  SUCCEED();
}

// -----------------------------------------------------------------------------
// 5. Multiple events: N market data events must produce N execution reports.
// -----------------------------------------------------------------------------
TEST_F(TradingEngineTestFixture, MultipleEvents) {
  constexpr int kCount = 5;

  quant::TradingEngine engine(sim_clock, "", "", "");

  std::atomic<int> report_count{0};
  std::mutex cv_mutex;
  std::condition_variable cv;

  engine.riskExecutionEventBus().subscribe<quant::ExecutionReportEvent>(
      [&report_count, &cv](const quant::ExecutionReportEvent& e) {
        if (e.status == quant::ExecutionStatus::Filled) {
          report_count.fetch_add(1);
          cv.notify_all();
        }
      });

  engine.start();

  for (int i = 1; i <= kCount; ++i) {
    engine.pushMarketData(makeMD("AAPL", 150.0 + i, static_cast<uint64_t>(i)));
  }

  {
    std::unique_lock lock(cv_mutex);
    bool done = cv.wait_for(lock, std::chrono::seconds(5),
                            [&] { return report_count.load() >= kCount; });
    ASSERT_TRUE(done) << "Only received " << report_count.load()
                      << " of " << kCount << " execution reports";
  }

  engine.stop();
}

// -----------------------------------------------------------------------------
// 6. EventBus accessors return valid references that can be subscribed to.
// -----------------------------------------------------------------------------
TEST_F(TradingEngineTestFixture, EventBusAccessorsWork) {
  quant::TradingEngine engine(sim_clock, "", "", "");

  int signal_count = 0;

  engine.strategyEventBus().subscribe<quant::SignalEvent>(
      [&signal_count](const quant::SignalEvent&) { ++signal_count; });

  std::promise<quant::SignalEvent> promise;
  auto future = promise.get_future();

  engine.riskExecutionEventBus().subscribe<quant::SignalEvent>(
      [&promise](const quant::SignalEvent& e) { promise.set_value(e); });

  engine.start();
  engine.pushMarketData(makeMD("TSLA", 237.5));

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready)
      << "SignalEvent did not arrive on risk_execution_loop";

  auto signal = future.get();
  EXPECT_EQ(signal.symbol, "TSLA");

  engine.stop();
}
