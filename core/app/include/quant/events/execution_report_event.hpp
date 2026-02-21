#pragma once

#include "quant/domain/order.hpp"
#include "quant/events/event_types.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// ExecutionStatus
// -----------------------------------------------------------------------------
//
// @brief  Encodes the outcome reported by the execution layer for a specific
//         order.
//
// @details
// This is a wire-level enum describing what the execution layer observed.
// It is distinct from domain::OrderStatus, which tracks the full internal
// lifecycle. The OrderTracker maps ExecutionStatus → OrderStatus transitions.
//
// Values:
//   Accepted — the execution layer has acknowledged the order and will
//              attempt to fill it. Does not guarantee a fill.
//   Filled   — the order (or a tranche of it) was filled at the reported
//              price and quantity.
//   Rejected — the order was rejected by the execution layer (e.g. invalid
//              symbol, insufficient margin on the exchange side).
// -----------------------------------------------------------------------------
enum class ExecutionStatus {
  Accepted,
  Filled,
  Rejected,
};

// -----------------------------------------------------------------------------
// ExecutionReportEvent
// -----------------------------------------------------------------------------
//
// @brief  Immutable description of what happened to a specific order at the
//         execution layer.
//
// @details
// Orders describe *intent*; execution reports describe *outcome*. There may
// be multiple reports over an order's lifetime (e.g. Accepted then Filled).
//
// The default status is Accepted (ordinal 0), matching the natural
// zero-initialization of the enum and the first step in the order lifecycle.
// All execution engine implementations must explicitly set the status field
// for every report they publish.
//
// Thread model:
//   Created and published on the risk_execution_loop (ExecutionEngine).
//   Consumed by OrderTracker, PositionEngine, and logging subscribers on
//   that loop. Safe to copy between threads via Event since it is plain
//   data with value semantics.
// -----------------------------------------------------------------------------
struct ExecutionReportEvent {
  domain::OrderId order_id{};  // Which order this report refers to
  double filled_quantity{0.0}; // How much was filled (0 for Accepted reports)
  double fill_price{0.0};      // Price at which it was filled (0 for Accepted)
  ExecutionStatus status{ExecutionStatus::Accepted};

  // Optional event-level metadata following the same pattern as other
  // events. Timestamp helps correlate fills with market data.
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

}  // namespace quant

