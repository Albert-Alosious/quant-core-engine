#pragma once

namespace quant {
namespace domain {

// -----------------------------------------------------------------------------
// OrderStatus — order lifecycle state machine
// -----------------------------------------------------------------------------
//
// @brief  Enumerates every possible state an order can occupy during its
//         lifetime within the trading engine.
//
// @details
// The order lifecycle follows a strict state machine. Not every transition
// is legal — the OrderTracker enforces the valid transition graph:
//
//   New  ──────────> PendingNew ───> Accepted ───> PartiallyFilled ──> Filled
//    │                   │               │              │     ▲           ▲
//    │                   │               │              │     │           │
//    │                   ▼               ▼              ▼     │           │
//    └──> Accepted       Rejected     Canceled      Canceled  └───────────┘
//    └──> Rejected                    Rejected
//
// Terminal states: Filled, Canceled, Rejected, Expired.
// Once an order reaches a terminal state, no further transitions are
// permitted and the OrderTracker removes it from its active order map.
//
// Why a separate enum from ExecutionStatus:
//   ExecutionStatus (Accepted, Filled, Rejected) describes the wire-level
//   outcome reported by the execution layer. OrderStatus describes the
//   full internal lifecycle tracked by the OrderTracker. The OrderTracker
//   maps incoming ExecutionStatus values to OrderStatus transitions.
//
// Thread model:
//   OrderStatus is a plain enum — a value type with no mutable state.
//   Thread-safe to copy and compare from any thread.
// -----------------------------------------------------------------------------
enum class OrderStatus {
  New,              // Order created by RiskEngine, not yet sent to execution
  PendingNew,       // Submitted to execution, awaiting acknowledgment
  Accepted,         // Acknowledged by the execution layer
  PartiallyFilled,  // Some quantity filled, remainder still open
  Filled,           // Fully filled — terminal state
  Canceled,         // Canceled by request — terminal state
  Rejected,         // Rejected by execution or risk layer — terminal state
  Expired,          // Expired due to time-in-force — terminal state
};

}  // namespace domain
}  // namespace quant
