#include "quant/time/simulation_time_provider.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// now_ms(): atomic read of the simulated clock
// -----------------------------------------------------------------------------
std::int64_t SimulationTimeProvider::now_ms() const {
  // std::atomic::load() with default memory_order_seq_cst. This is the
  // strongest ordering and guarantees that the value we read is the most
  // recently written by any thread. On x86-64 this compiles to a plain
  // MOV (no fence needed for loads), so there is zero overhead compared to
  // a non-atomic read.
  return current_time_ms_.load();
}

// -----------------------------------------------------------------------------
// advance_time(): atomic write to the simulated clock
// -----------------------------------------------------------------------------
void SimulationTimeProvider::advance_time(std::int64_t new_time_ms) {
  // std::atomic::store() with default memory_order_seq_cst. After this
  // store, any thread calling now_ms() is guaranteed to see new_time_ms
  // (or a later value if another advance_time runs in between).
  //
  // We do NOT enforce monotonicity (new_time_ms >= current) because:
  //   1. It adds a branch on the hot path (every tick).
  //   2. The caller (MarketDataGateway) is responsible for feeding data
  //      in chronological order.
  //   3. In testing, being able to set arbitrary times is useful.
  current_time_ms_.store(new_time_ms);
}

}  // namespace quant
