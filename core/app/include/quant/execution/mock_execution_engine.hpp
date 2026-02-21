#pragma once

#include "quant/execution/i_execution_engine.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/order_event.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/time/i_time_provider.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// MockExecutionEngine — deterministic fill simulator for backtesting
// -----------------------------------------------------------------------------
//
// @brief  Subscribes to OrderEvent and immediately publishes an
//         ExecutionReportEvent representing a perfect fill at the order
//         price with zero slippage.
//
// @details
// This is the backtesting counterpart to the Phase 1 ExecutionEngine. The
// key difference is the time source:
//
//   ExecutionEngine      → uses std::chrono::system_clock::now()
//   MockExecutionEngine  → uses ITimeProvider::now_ms()
//
// By injecting a SimulationTimeProvider, the fill timestamps track the
// historical data's timeline instead of wall-clock time, which is essential
// for deterministic backtesting.
//
// Fill model (Phase 2):
//   - Immediate fill: every order is filled instantly.
//   - Perfect fill:   fill_price = order.price, fill_quantity = order.quantity.
//   - Zero slippage:  no market impact or delay modeling.
//   This is intentionally simple. Phase 8 (Advanced Execution Models) will
//   introduce slippage, latency, and partial-fill simulation.
//
// Why a separate class instead of modifying ExecutionEngine:
//   - Keeps Phase 1 code untouched (no regressions, no refactoring risk).
//   - Clean separation: MockExecutionEngine is backtest-only; the original
//     ExecutionEngine remains available for live/paper trading.
//   - Both implement IExecutionEngine, so TradingEngine can hold either via
//     unique_ptr<IExecutionEngine> without conditional logic.
//
// Thread model:
//   Lives on the risk_execution_loop thread. The onOrder callback runs on
//   that thread whenever the loop publishes an OrderEvent. No internal
//   locking is needed because all access is single-threaded.
//
// Ownership:
//   Holds a reference to the EventBus (owned by EventLoopThread) and a
//   const reference to the ITimeProvider (owned by TradingEngine or the
//   backtest harness). Neither is owned by this class.
// -----------------------------------------------------------------------------
class MockExecutionEngine final : public IExecutionEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // @brief  Subscribes to OrderEvent on the given bus. For each order,
  //         publishes an ExecutionReportEvent with a deterministic timestamp.
  //
  // @param  bus            EventBus belonging to risk_execution_loop.
  // @param  time_provider  Time source for fill timestamps. In backtesting
  //                        this is a SimulationTimeProvider; in live mode it
  //                        could be a LiveTimeProvider.
  //
  // @details
  // The subscription callback captures [this] and forwards to onOrder().
  // The returned SubscriptionId is stored for RAII cleanup in the destructor.
  //
  // Thread-safety: Safe to construct from main() before events flow.
  //                subscribe() itself is thread-safe.
  // Side-effects:  Registers a callback on the EventBus.
  // -------------------------------------------------------------------------
  explicit MockExecutionEngine(EventBus& bus,
                               const ITimeProvider& time_provider);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // @brief  Unsubscribes from OrderEvent on the bus.
  //
  // @details
  // RAII cleanup. After destruction, no callbacks will fire into this object.
  //
  // Thread-safety: Safe to destroy from main() after the event loop is
  //                stopped.
  // -------------------------------------------------------------------------
  ~MockExecutionEngine() override;

  MockExecutionEngine(const MockExecutionEngine&) = delete;
  MockExecutionEngine& operator=(const MockExecutionEngine&) = delete;
  MockExecutionEngine(MockExecutionEngine&&) = delete;
  MockExecutionEngine& operator=(MockExecutionEngine&&) = delete;

 private:
  // -------------------------------------------------------------------------
  // onOrder(event)
  // -------------------------------------------------------------------------
  // @brief  Converts an OrderEvent into an ExecutionReportEvent with a
  //         perfect fill and a deterministic timestamp from the time provider.
  //
  // @param  event  The incoming OrderEvent from RiskEngine.
  //
  // @details
  // Fill logic:
  //   filled_quantity = order.quantity  (full fill)
  //   fill_price      = order.price    (zero slippage)
  //   status          = Filled
  //   timestamp       = ms_to_timestamp(time_provider_.now_ms())
  //
  // The ExecutionReportEvent is published back to the same bus so that
  // downstream subscribers (e.g. PositionEngine in Phase 3, or logging
  // subscribers) can react.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Publishes an ExecutionReportEvent to the EventBus.
  // -------------------------------------------------------------------------
  void onOrder(const OrderEvent& event);

  EventBus& bus_;
  const ITimeProvider& time_provider_;
  EventBus::SubscriptionId subscription_id_{0};
};

}  // namespace quant
