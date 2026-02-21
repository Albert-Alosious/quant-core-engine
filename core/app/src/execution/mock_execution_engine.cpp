#include "quant/execution/mock_execution_engine.hpp"
#include "quant/time/time_utils.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to OrderEvent
// -----------------------------------------------------------------------------
MockExecutionEngine::MockExecutionEngine(EventBus& bus,
                                         const ITimeProvider& time_provider)
    : bus_(bus), time_provider_(time_provider) {
  // Register a typed callback for OrderEvent. The lambda captures [this] so
  // the callback forwards to the private onOrder() method. The subscription
  // id is saved for unsubscribe in the destructor.
  subscription_id_ = bus_.subscribe<OrderEvent>(
      [this](const OrderEvent& e) { onOrder(e); });
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe from OrderEvent
// -----------------------------------------------------------------------------
MockExecutionEngine::~MockExecutionEngine() {
  bus_.unsubscribe(subscription_id_);
}

// -----------------------------------------------------------------------------
// onOrder: simulate perfect fill and publish ExecutionReportEvent
// -----------------------------------------------------------------------------
void MockExecutionEngine::onOrder(const OrderEvent& event) {
  const domain::Order& order = event.order;

  // Build the execution report. In backtesting we simulate a perfect fill:
  // the entire order quantity is filled at exactly the order price, with no
  // slippage or partial fills. This keeps the simulation simple and
  // reproducible for Phase 2.
  ExecutionReportEvent report;
  report.order_id = order.id;
  report.filled_quantity = order.quantity;
  report.fill_price = order.price;
  report.status = ExecutionStatus::Filled;

  // Use the injected time provider instead of std::chrono::system_clock::now().
  // In backtesting, time_provider_ is a SimulationTimeProvider whose clock
  // tracks the historical data. ms_to_timestamp() converts the int64_t
  // milliseconds back to the Timestamp (chrono time_point) that
  // ExecutionReportEvent expects.
  report.timestamp = ms_to_timestamp(time_provider_.now_ms());

  // Reuse the sequence_id from the incoming OrderEvent for traceability.
  report.sequence_id = event.sequence_id;

  // Publish the fill report back onto the same bus. Downstream subscribers
  // (logging, future PositionEngine) will receive it on this thread.
  bus_.publish(report);
}

}  // namespace quant
