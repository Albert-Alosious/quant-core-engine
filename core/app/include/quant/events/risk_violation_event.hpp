#pragma once

#include "quant/events/event_types.hpp"

#include <cstdint>
#include <string>

namespace quant {

// -----------------------------------------------------------------------------
// RiskViolationEvent — notification that a hard risk limit has been breached
// -----------------------------------------------------------------------------
//
// @brief  Published by PositionEngine when a post-trade risk check fails
//         (e.g., realized PnL breaches the max drawdown floor).
//
// @details
// Subscribers (primarily RiskEngine) use this event to activate the kill
// switch, halting all further signal-to-order conversion. The event carries
// enough context for logging, alerting, and future automated recovery:
//
//   - symbol:        The instrument that triggered the violation.
//   - reason:        Human-readable description (e.g., "Max Drawdown Exceeded").
//   - current_value: The actual value that breached the limit (e.g., realized
//                    PnL of -510.0).
//   - limit_value:   The threshold that was exceeded (e.g., -500.0).
//
// Thread model:
//   Created and published on the risk_execution_loop thread by
//   PositionEngine::onFill(). Consumed by RiskEngine::onRiskViolation()
//   on the same thread. Safe to copy between threads via Event since it
//   is plain data with value semantics.
//
// Ownership:
//   Value type. No references to internal state of any component.
// -----------------------------------------------------------------------------
struct RiskViolationEvent {
  std::string symbol;
  std::string reason;
  double current_value{0.0};
  double limit_value{0.0};
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

}  // namespace quant
