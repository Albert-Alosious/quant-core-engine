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
// Responsibility: Immutable description of what happened to a specific order
// at the execution layer (filled or rejected).
// Why separate from Order:
// - Orders describe *intent*; execution reports describe *outcome*.
// - There may be multiple reports over an order's lifetime in a real engine;
//   here we keep it simple (single report) but the separation holds.
// Thread model:
// - Created and published on the risk_execution_loop (ExecutionEngine).
// - Consumed by any subscribers on that loop (e.g. logging, position manager
//   in the future). Safe to copy between threads via Event since it is plain
//   data with value semantics.
// -----------------------------------------------------------------------------
struct ExecutionReportEvent {
  domain::OrderId order_id{};  // Which order this report refers to
  double filled_quantity{0.0}; // How much was filled (full quantity here)
  double fill_price{0.0};      // Price at which it was filled
  ExecutionStatus status{ExecutionStatus::Filled};

  // Optional event-level metadata following the same pattern as other
  // events. Timestamp helps correlate fills with market data.
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

}  // namespace quant

