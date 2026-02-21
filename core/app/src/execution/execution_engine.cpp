#include "quant/execution/execution_engine.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to OrderEvent
// -----------------------------------------------------------------------------
ExecutionEngine::ExecutionEngine(EventBus& bus) : bus_(bus) {
  subscription_id_ = bus_.subscribe<OrderEvent>(
      [this](const OrderEvent& e) { onOrder(e); });
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe
// -----------------------------------------------------------------------------
ExecutionEngine::~ExecutionEngine() { bus_.unsubscribe(subscription_id_); }

// -----------------------------------------------------------------------------
// onOrder: simulate two-step execution (Accepted → Filled)
// -----------------------------------------------------------------------------
void ExecutionEngine::onOrder(const OrderEvent& event) {
  const domain::Order& order = event.order;
  auto ts = std::chrono::system_clock::now();

  // --- Report 1: Accepted ---------------------------------------------------
  ExecutionReportEvent ack;
  ack.order_id = order.id;
  ack.filled_quantity = 0.0;
  ack.fill_price = 0.0;
  ack.status = ExecutionStatus::Accepted;
  ack.timestamp = ts;
  ack.sequence_id = event.sequence_id;

  bus_.publish(ack);

  // --- Report 2: Filled -----------------------------------------------------
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

