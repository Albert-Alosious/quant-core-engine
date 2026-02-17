#pragma once

#include "quant/domain/order.hpp"
#include "quant/events/event_types.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// OrderEvent
// -----------------------------------------------------------------------------
// Responsibility: Wraps a domain::Order in an event so it can be transported
// through the EventBus and across threads.
// Why wrap the domain object:
// - Separates *what* the order is (domain::Order) from *how* it moves through
//   the system (events).
// - Allows us to attach additional metadata (timestamps, sequence numbers)
//   specific to event flow without polluting the core domain model.
// Thread model:
// - OrderEvent instances are created on the risk/execution thread by
//   RiskEngine and then published on that same thread via EventBus.
// - They may be copied between threads by value via Event (std::variant), but
//   the data itself is immutable after creation.
// -----------------------------------------------------------------------------
struct OrderEvent {
  domain::Order order;   // The underlying domain order

  // Optional event-level metadata. For now we re-use the global Timestamp
  // alias and sequence id pattern used by other events so we can order and
  // audit events if needed.
  Timestamp timestamp{};         // When this event was created
  std::uint64_t sequence_id{0}; // Monotonic id if the engine chooses to set it
};

}  // namespace quant

