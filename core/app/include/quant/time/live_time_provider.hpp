#pragma once

#include "quant/time/i_time_provider.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// LiveTimeProvider — wall-clock time implementation of ITimeProvider
// -----------------------------------------------------------------------------
//
// @brief  Returns real wall-clock time via std::chrono::system_clock.
//
// @details
// Used in live trading mode where events must carry the actual system
// timestamp. Converts std::chrono::system_clock::now() to milliseconds
// since the Unix epoch.
//
// Why a separate class instead of inlining chrono calls everywhere:
//   - Keeps all time-source logic behind the ITimeProvider interface so
//     components are testable with SimulationTimeProvider.
//   - Single point of change if the clock source or resolution changes
//     (e.g. switching to steady_clock or nanoseconds).
//
// Thread model:
//   std::chrono::system_clock::now() is safe to call from any thread on all
//   major platforms (it reads a monotonically adjusted hardware clock).
//   No internal mutex is needed.
//
// Ownership:
//   Typically created by TradingEngine and passed by const reference to
//   components. TradingEngine owns the instance; components borrow it.
// -----------------------------------------------------------------------------
class LiveTimeProvider final : public ITimeProvider {
 public:
  // -------------------------------------------------------------------------
  // now_ms() override
  // -------------------------------------------------------------------------
  // @brief  Returns the current wall-clock time in milliseconds since epoch.
  //
  // @return int64_t  Milliseconds since 1970-01-01 00:00:00 UTC.
  //
  // @details
  // Delegates to std::chrono::system_clock::now(), then converts the
  // resulting time_point to a duration in milliseconds via
  // duration_cast<milliseconds>(time_since_epoch()).count().
  //
  // Thread-safety: Safe to call from any thread. No shared mutable state.
  // Side-effects:  None.
  // -------------------------------------------------------------------------
  std::int64_t now_ms() const override;
};

}  // namespace quant
