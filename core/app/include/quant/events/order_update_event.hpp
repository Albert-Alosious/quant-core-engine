#pragma once

#include "quant/domain/order.hpp"
#include "quant/domain/order_status.hpp"
#include "quant/events/event_types.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// OrderUpdateEvent
// -----------------------------------------------------------------------------
//
// @brief  Published by the OrderTracker whenever an order's lifecycle state
//         changes. Carries a snapshot of the updated order and the status
//         it transitioned from.
//
// @details
// This event enables downstream subscribers (logging, monitoring, future
// PortfolioEngine, Python IPC) to observe the full order lifecycle without
// accessing the OrderTracker's internal state.
//
// The order field is a full copy (pass-by-value) of the order after the
// transition has been applied. The previous_status field records the state
// the order was in before this transition, enabling subscribers to log or
// react to specific transitions (e.g. Accepted → Filled).
//
// Thread model:
//   Created and published on the risk_execution_loop thread by
//   OrderTracker. Subscribers on that bus receive it on the same thread.
//   Safe to copy across threads via Event (std::variant) since it is
//   plain data with value semantics.
//
// Ownership:
//   Self-contained. No references to external mutable state.
// -----------------------------------------------------------------------------
struct OrderUpdateEvent {
  domain::Order order;                                // Snapshot after transition
  domain::OrderStatus previous_status{domain::OrderStatus::New};  // State before
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

}  // namespace quant
