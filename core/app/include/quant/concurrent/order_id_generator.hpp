#pragma once

#include <atomic>
#include <cstdint>

namespace quant {

// -----------------------------------------------------------------------------
// OrderIdGenerator — thread-safe, monotonically increasing order ID source
// -----------------------------------------------------------------------------
//
// @brief  Produces unique, monotonically increasing order IDs via an atomic
//         counter. Each call to next_id() returns a value guaranteed to be
//         different from every other call, regardless of which thread invokes
//         it.
//
// @details
// The generator starts at 1 (ID 0 is reserved as an "unset" sentinel) and
// increments by 1 on each call. The underlying std::atomic<uint64_t>
// guarantees atomicity. std::memory_order_relaxed is used because the only
// requirement is that each call returns a unique value — no cross-variable
// ordering constraints exist.
//
// Why not a singleton:
//   architecture.md rule #6 prohibits global mutable state. The generator
//   is owned as a value member by TradingEngine and injected into RiskEngine
//   (and any future components that create orders) via reference. This makes
//   the dependency explicit, testable, and free of hidden coupling.
//
// Why atomic when RiskEngine runs single-threaded:
//   Today, only RiskEngine calls next_id(), and it runs exclusively on the
//   risk_execution_loop thread. The atomic is a design safety net for
//   architecture.md rule #10 ("support multiple strategies and multiple risk
//   modules"). If a future phase introduces a second RiskEngine on a
//   different thread, the generator remains correct without modification.
//
// Thread model:
//   next_id() is safe to call concurrently from any number of threads.
//   The returned IDs are unique and monotonically increasing per the
//   total order of fetch_add operations on the atomic.
//
// Ownership:
//   Owned by TradingEngine as a value member. Outlives all components that
//   hold a reference to it.
// -----------------------------------------------------------------------------
class OrderIdGenerator {
 public:
  OrderIdGenerator() = default;

  // Non-copyable, non-movable: atomics are not movable, and copying an ID
  // generator would create two sources producing duplicate IDs.
  OrderIdGenerator(const OrderIdGenerator&) = delete;
  OrderIdGenerator& operator=(const OrderIdGenerator&) = delete;
  OrderIdGenerator(OrderIdGenerator&&) = delete;
  OrderIdGenerator& operator=(OrderIdGenerator&&) = delete;

  // -------------------------------------------------------------------------
  // next_id()
  // -------------------------------------------------------------------------
  // @brief  Returns the next unique order ID.
  //
  // @return A uint64_t guaranteed to be unique across all calls to this
  //         generator instance. Values start at 1 and increase by 1.
  //
  // @details
  // Uses fetch_add(1, relaxed) for minimal overhead. The relaxed ordering
  // is sufficient because no other memory operations depend on the ordering
  // of this increment relative to other variables.
  //
  // Thread-safety: Safe to call concurrently from any thread.
  // Side-effects:  Atomically increments the internal counter.
  // -------------------------------------------------------------------------
  std::uint64_t next_id() {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  std::atomic<std::uint64_t> next_id_{1};
};

}  // namespace quant
