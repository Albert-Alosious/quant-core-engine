#pragma once

#include "quant/domain/order_status.hpp"

#include <cstdint>
#include <string>

namespace quant {
namespace domain {

// -----------------------------------------------------------------------------
// OrderId
// -----------------------------------------------------------------------------
// Responsibility: Represents the unique identifier for an order within the
// trading engine.
// Why a strong alias (using OrderId = std::uint64_t):
// - Makes function signatures self-documenting (OrderId vs plain std::uint64_t).
// - Prevents accidental mixing with other counters or ids.
// - Still behaves like an integer (cheap to copy, comparable, hashable).
// Thread-safety: OrderId itself is just a value type; it is thread-safe to
// copy and pass by value. Incrementing counters that produce OrderId must
// be synchronized by the owning component (e.g. RiskEngine).
// -----------------------------------------------------------------------------
using OrderId = std::uint64_t;

// -----------------------------------------------------------------------------
// Side
// -----------------------------------------------------------------------------
// Responsibility: Encodes the trading side (buy or sell) for an order.
// Why enum class instead of plain enum:
// - Strongly typed: Side::Buy and Side::Sell live in their own scope.
// - Prevents implicit conversion to int, avoiding many bugs.
// - Clear at call sites (Side::Buy vs 0/1).
// -----------------------------------------------------------------------------
enum class Side {
  Buy,
  Sell,
};

// -----------------------------------------------------------------------------
// Order
// -----------------------------------------------------------------------------
// Responsibility: Describes an order's full state: the original intent (symbol,
// side, quantity, price) plus its current lifecycle status and cumulative fill.
//
// @details
// When first created by RiskEngine, status is New and filled_quantity is 0.
// The OrderTracker mutates its internal copy as execution reports arrive,
// advancing the status through the state machine and accumulating fills.
// Copies distributed via OrderEvent and OrderUpdateEvent are snapshots —
// recipients must not mutate them.
//
// Why a simple POD-like struct:
// - Plain data holder with no behavior keeps the domain model easy to reason
//   about, especially for a C background.
// - Value semantics: safe to copy between threads and store in events.
// - No internal locking: thread-safety is achieved by treating Order as
//   immutable once copied into an event. Only the authoritative copy inside
//   the OrderTracker is mutated, and only on the risk_execution_loop thread.
//
// Ownership:
// - Orders are created by RiskEngine, tracked by OrderTracker, and
//   distributed as read-only snapshots via events.
// -----------------------------------------------------------------------------
struct Order {
  OrderId id{};                 // Unique identifier for this order
  std::string strategy_id;      // Which strategy generated this order
  std::string symbol;           // Instrument to trade (e.g. "AAPL")
  Side side{Side::Buy};         // Buy or Sell
  double quantity{0.0};         // Order size (units or contracts)
  double price{0.0};            // Limit price or last known price for testing
  OrderStatus status{OrderStatus::New};  // Current lifecycle state
  double filled_quantity{0.0};  // Cumulative filled quantity (partial fills)
};

}  // namespace domain
}  // namespace quant

