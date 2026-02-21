#pragma once

#include "event_types.hpp"
#include "order_event.hpp"
#include "order_update_event.hpp"
#include "execution_report_event.hpp"
#include "position_update_event.hpp"
#include "risk_violation_event.hpp"
#include <variant>

namespace quant {

// -----------------------------------------------------------------------------
// Event (type alias)
// -----------------------------------------------------------------------------
// Responsibility: The single "envelope" type for all events in the engine.
// Why in architecture: All modules communicate via events (architecture.md).
// A single variant type allows one EventBus to carry every event kind without
// void* or inheritance.
//
// Why std::variant (modern C++):
// - Value semantics: no heap allocation, no raw pointers. Safer than
//   base-class pointers and dynamic_cast.
// - Type-safe: the compiler knows the set of possible types. Use std::visit
//   or std::get_if to dispatch; no undefined behavior from wrong casts.
// - Extensible: adding a new event type means adding it to the variant and
//   updating all visit/get_if sites; the compiler enforces completeness.
// -----------------------------------------------------------------------------
using Event = std::variant<
    MarketDataEvent,
    SignalEvent,
    OrderEvent,
    OrderUpdateEvent,
    RiskRejectEvent,
    FillEvent,
    HeartbeatEvent,
    ExecutionReportEvent,
    PositionUpdateEvent,
    RiskViolationEvent>;

}  // namespace quant

