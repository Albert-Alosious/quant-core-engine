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
// onOrder: simulate immediate fill and publish ExecutionReportEvent
// -----------------------------------------------------------------------------
void ExecutionEngine::onOrder(const OrderEvent& event) {
  const domain::Order& order = event.order;

  ExecutionReportEvent report;
  report.order_id = order.id;
  report.filled_quantity = order.quantity;
  report.fill_price = order.price;
  report.status = ExecutionStatus::Filled;
  report.timestamp = std::chrono::system_clock::now();
  report.sequence_id = event.sequence_id;  // reuse for demo

  // In a real engine, this would be asynchronous and depend on broker
  // responses. Here we publish immediately to keep the example simple.
  bus_.publish(report);
}

}  // namespace quant

