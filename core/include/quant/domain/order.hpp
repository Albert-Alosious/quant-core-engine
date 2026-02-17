#pragma once

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
// Responsibility: Immutable description of an order intent: which strategy,
// which symbol, which side, how much, and at what price.
// Why a simple POD-like struct:
// - Plain data holder with no behavior keeps the domain model easy to reason
//   about, especially for a C background.
// - Value semantics: safe to copy between threads and store in events.
// - No internal locking: thread-safety is achieved by treating Order as
//   immutable once constructed and only passing it across threads via events.
// Ownership:
// - Orders are created and owned logically by the RiskEngine and then copied
//   into events. No shared mutable Order objects across threads.
// -----------------------------------------------------------------------------
struct Order {
  OrderId id{};                 // Unique identifier for this order
  std::string strategy_id;      // Which strategy generated this order
  std::string symbol;           // Instrument to trade (e.g. "AAPL")
  Side side{Side::Buy};         // Buy or Sell
  double quantity{0.0};         // Order size (units or contracts)
  double price{0.0};            // Limit price or last known price for testing
};

}  // namespace domain
}  // namespace quant

