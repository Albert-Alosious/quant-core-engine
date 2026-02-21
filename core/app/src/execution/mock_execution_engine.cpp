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
// onOrder: simulate realistic two-step execution (Accepted → Filled)
// -----------------------------------------------------------------------------
void MockExecutionEngine::onOrder(const OrderEvent& event) {
  const domain::Order& order = event.order;
  auto ts = ms_to_timestamp(time_provider_.now_ms());

  // --- Report 1: Accepted ---------------------------------------------------
  // The execution layer acknowledges the order before filling it. This
  // allows the OrderTracker to advance the order from New → Accepted.
  ExecutionReportEvent ack;
  ack.order_id = order.id;
  ack.filled_quantity = 0.0;
  ack.fill_price = 0.0;
  ack.status = ExecutionStatus::Accepted;
  ack.timestamp = ts;
  ack.sequence_id = event.sequence_id;

  bus_.publish(ack);

  // --- Report 2: Filled -----------------------------------------------------
  // Perfect fill at the order price with zero slippage. The OrderTracker
  // advances Accepted → Filled, and PositionEngine processes the fill.
  ExecutionReportEvent fill;
  fill.order_id = order.id;
  fill.filled_quantity = order.quantity;
  fill.fill_price = order.price;
  fill.status = ExecutionStatus::Filled;
  fill.timestamp = ts;
  fill.sequence_id = event.sequence_id;

  bus_.publish(fill);
}

}  // namespace quant
