#include "quant/risk/order_tracker.hpp"
#include "quant/events/order_update_event.hpp"

#include <iostream>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to OrderEvent and ExecutionReportEvent
// -----------------------------------------------------------------------------
OrderTracker::OrderTracker(EventBus& bus) : bus_(bus) {
  order_sub_id_ = bus_.subscribe<OrderEvent>(
      [this](const OrderEvent& e) { onOrder(e); });

  exec_sub_id_ = bus_.subscribe<ExecutionReportEvent>(
      [this](const ExecutionReportEvent& e) { onExecutionReport(e); });
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe from both event streams
// -----------------------------------------------------------------------------
OrderTracker::~OrderTracker() {
  bus_.unsubscribe(exec_sub_id_);
  bus_.unsubscribe(order_sub_id_);
}

// -----------------------------------------------------------------------------
// transitionStatus: validate state machine transitions
// -----------------------------------------------------------------------------
bool OrderTracker::transitionStatus(domain::OrderStatus current,
                                    domain::OrderStatus next) {
  using S = domain::OrderStatus;

  switch (current) {
    case S::New:
      return next == S::PendingNew ||
             next == S::Accepted ||
             next == S::Rejected;

    case S::PendingNew:
      return next == S::Accepted ||
             next == S::Rejected;

    case S::Accepted:
      return next == S::PartiallyFilled ||
             next == S::Filled ||
             next == S::Canceled ||
             next == S::Rejected;

    case S::PartiallyFilled:
      return next == S::PartiallyFilled ||
             next == S::Filled ||
             next == S::Canceled;

    case S::Filled:
    case S::Canceled:
    case S::Rejected:
    case S::Expired:
      return false;
  }

  return false;
}

// -----------------------------------------------------------------------------
// isTerminal: check if a status is a final (non-mutable) state
// -----------------------------------------------------------------------------
bool OrderTracker::isTerminal(domain::OrderStatus status) {
  using S = domain::OrderStatus;
  return status == S::Filled ||
         status == S::Canceled ||
         status == S::Rejected ||
         status == S::Expired;
}

// -----------------------------------------------------------------------------
// onOrder: register new order and publish initial OrderUpdateEvent
// -----------------------------------------------------------------------------
void OrderTracker::onOrder(const OrderEvent& event) {
  const domain::Order& order = event.order;

  active_orders_[order.id] = order;

  OrderUpdateEvent update;
  update.order = order;
  update.previous_status = domain::OrderStatus::New;
  update.timestamp = event.timestamp;
  update.sequence_id = event.sequence_id;

  bus_.publish(update);
}

// -----------------------------------------------------------------------------
// onExecutionReport: advance order status and publish OrderUpdateEvent
// -----------------------------------------------------------------------------
void OrderTracker::onExecutionReport(const ExecutionReportEvent& event) {
  auto it = active_orders_.find(event.order_id);
  if (it == active_orders_.end()) {
    std::cerr << "[OrderTracker] WARNING: execution report for unknown "
                 "order_id=" << event.order_id << ". Skipping.\n";
    return;
  }

  domain::Order& order = it->second;
  domain::OrderStatus previous = order.status;

  // Map wire-level ExecutionStatus to internal OrderStatus.
  domain::OrderStatus proposed;
  switch (event.status) {
    case ExecutionStatus::Accepted:
      proposed = domain::OrderStatus::Accepted;
      break;
    case ExecutionStatus::Filled:
      proposed = domain::OrderStatus::Filled;
      break;
    case ExecutionStatus::Rejected:
      proposed = domain::OrderStatus::Rejected;
      break;
  }

  if (!transitionStatus(previous, proposed)) {
    std::cerr << "[OrderTracker] WARNING: illegal transition for order_id="
              << event.order_id << " from "
              << static_cast<int>(previous) << " to "
              << static_cast<int>(proposed) << ". Skipping.\n";
    return;
  }

  order.status = proposed;

  if (proposed == domain::OrderStatus::Filled) {
    order.filled_quantity = event.filled_quantity;
  }

  OrderUpdateEvent update;
  update.order = order;
  update.previous_status = previous;
  update.timestamp = event.timestamp;
  update.sequence_id = event.sequence_id;

  bus_.publish(update);

  if (isTerminal(proposed)) {
    active_orders_.erase(it);
  }
}

}  // namespace quant
