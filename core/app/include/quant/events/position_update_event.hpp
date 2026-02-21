#pragma once

#include "quant/domain/position.hpp"
#include "quant/events/event_types.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// PositionUpdateEvent
// -----------------------------------------------------------------------------
//
// @brief  Carries an immutable snapshot of a Position after a fill has been
//         applied by the PositionEngine.
//
// @details
// Published to the EventBus every time PositionEngine processes a
// ExecutionReportEvent with Filled status. Downstream subscribers (logging,
// monitoring, future PortfolioEngine, Python IPC) can observe position
// changes without accessing PositionEngine's internal state.
//
// The position field is a full copy (pass-by-value), not a reference. This
// ensures the event remains valid and immutable after publication,
// regardless of what happens to the PositionEngine's internal map.
//
// Thread model:
//   Created and published on the risk_execution_loop thread by
//   PositionEngine. Subscribers on that bus receive it on the same thread.
//   Safe to copy across threads via Event (std::variant) since it is
//   plain data with value semantics.
//
// Ownership:
//   Self-contained. No references to external mutable state.
// -----------------------------------------------------------------------------
struct PositionUpdateEvent {
  domain::Position position;   // Snapshot of the updated position
  Timestamp timestamp{};       // When this update was produced
  std::uint64_t sequence_id{0};
};

}  // namespace quant
