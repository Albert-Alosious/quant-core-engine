#pragma once

#include "quant/time/i_time_provider.hpp"

#include <atomic>
#include <cstdint>

namespace quant {

// -----------------------------------------------------------------------------
// SimulationTimeProvider — externally-driven clock for backtesting
// -----------------------------------------------------------------------------
//
// @brief  ITimeProvider implementation whose "current time" is set explicitly
//         by the data replay layer rather than read from the system clock.
//
// @details
// During a backtest the engine must believe that "now" is whatever timestamp
// the historical data says it is. The MarketDataGateway receives a tick with
// timestamp_ms = 1_700_000_000_000 and calls advance_time(1700000000000).
// From that point on, any component that calls now_ms() gets 1700000000000
// until the next tick advances the clock further.
//
// This is the key to deterministic backtesting:
//   - No look-ahead bias:  the engine only sees time that data has revealed.
//   - Reproducibility:     identical data → identical timestamps → identical
//                          signals and fills across runs.
//   - Isolation from wall-clock: tests run as fast as the CPU can process,
//                          not limited by real-time sleep/wait.
//
// Internal storage:
//   std::atomic<int64_t> current_time_ms_
//
// Why std::atomic instead of a mutex:
//   - The market data gateway thread (writer) calls advance_time() on every
//     tick. Strategy and risk threads (readers) call now_ms() on every event.
//   - A mutex would serialize these calls, creating contention on the hot
//     path. std::atomic<int64_t> is lock-free on all 64-bit platforms and
//     provides the necessary visibility guarantee (sequentially-consistent
//     by default) without blocking.
//
// Thread model:
//   - advance_time() is called by the MarketDataGateway thread (single writer).
//   - now_ms() may be called concurrently from strategy, risk, and execution
//     threads (multiple readers).
//   - Both operations are atomic; no additional synchronization is needed.
//
// Ownership:
//   Created by TradingEngine (or the backtest harness). Passed by reference
//   to MarketDataGateway (which calls advance_time) and to other components
//   (which call now_ms via the ITimeProvider interface).
// -----------------------------------------------------------------------------
class SimulationTimeProvider final : public ITimeProvider {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // @brief  Initializes the simulation clock to 0 ms (epoch start).
  //
  // @details
  // A zero initial value means "no data has been replayed yet." The first
  // call to advance_time() sets the clock to the first tick's timestamp.
  // Components should not rely on now_ms() returning a meaningful value
  // before data replay begins.
  // -------------------------------------------------------------------------
  SimulationTimeProvider() = default;

  // -------------------------------------------------------------------------
  // now_ms() override
  // -------------------------------------------------------------------------
  // @brief  Returns the last time set by advance_time().
  //
  // @return int64_t  Epoch milliseconds of the most recent simulated tick.
  //                  Returns 0 if advance_time() has never been called.
  //
  // @details
  // Atomic load with default memory ordering (seq_cst). The value returned
  // is the most recently stored value from any thread.
  //
  // Thread-safety: Safe to call from any thread. Lock-free on 64-bit
  //                platforms.
  // Side-effects:  None.
  // -------------------------------------------------------------------------
  std::int64_t now_ms() const override;

  // -------------------------------------------------------------------------
  // advance_time(new_time_ms)
  // -------------------------------------------------------------------------
  // @brief  Sets the simulation clock to the given timestamp.
  //
  // @param  new_time_ms  Epoch milliseconds of the current simulated tick.
  //                      Must be >= the previous value for correct
  //                      monotonic time progression (caller's responsibility;
  //                      not enforced here to avoid branching on the hot path).
  //
  // @details
  // Called by MarketDataGateway on every incoming tick before the tick is
  // published as a MarketDataEvent. After this call, all subsequent now_ms()
  // calls from any thread will return new_time_ms.
  //
  // Atomic store with default memory ordering (seq_cst). This ensures that
  // any thread reading now_ms() after the store sees the updated value.
  //
  // Thread-safety: Safe to call from any thread, but the intended usage is
  //                single-writer (MarketDataGateway thread).
  // Side-effects:  Changes the value returned by now_ms() globally.
  // -------------------------------------------------------------------------
  void advance_time(std::int64_t new_time_ms);

 private:
  // Atomic integer holding the current simulation time. Initialized to 0
  // (no data replayed). std::atomic ensures visibility across threads
  // without a mutex.
  std::atomic<std::int64_t> current_time_ms_{0};
};

}  // namespace quant
