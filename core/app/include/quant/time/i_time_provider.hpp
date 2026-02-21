#pragma once

#include <cstdint>

namespace quant {

// -----------------------------------------------------------------------------
// ITimeProvider — abstract time source interface
// -----------------------------------------------------------------------------
//
// @brief  Pure virtual interface that abstracts the concept of "current time"
//         away from std::chrono::system_clock.
//
// @details
// In a live trading engine, time comes from the system clock. In a backtest,
// time is driven externally — each historical tick carries a timestamp, and
// the engine must believe *that* is "now." If components call
// std::chrono::system_clock::now() directly, the backtest loses determinism
// because timestamps drift with wall-clock time instead of following the
// replay data.
//
// ITimeProvider solves this with dependency injection:
//   - LiveTimeProvider   → delegates to std::chrono::system_clock.
//   - SimulationTimeProvider → returns a value set by the data replay layer.
//
// Components receive `const ITimeProvider&` and call now_ms() whenever they
// need a timestamp. The caller decides which implementation to inject, and
// the component does not know (or care) whether it is live or simulated.
//
// Why int64_t milliseconds instead of std::chrono::time_point:
//   - ZeroMQ messages from the Python data feeder carry integer timestamps.
//   - int64_t is language-agnostic: Python, C++, and JSON all handle it
//     natively without chrono conversion boilerplate.
//   - Millisecond resolution is sufficient for equity/crypto tick data. If
//     sub-millisecond precision is needed later, the interface can be
//     extended without breaking existing callers.
//
// Thread-safety contract:
//   Implementations MUST be safe for concurrent reads from multiple threads.
//   Writers (e.g. SimulationTimeProvider::advance_time) must synchronize
//   with readers internally (e.g. via std::atomic).
//
// Ownership:
//   Components hold a const reference; they do NOT own the provider. The
//   provider's lifetime must exceed that of all components that reference it.
// -----------------------------------------------------------------------------
class ITimeProvider {
 public:
  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // @brief  Virtual destructor for safe polymorphic deletion.
  //
  // @details
  // Required because we delete through base pointers (e.g. unique_ptr<
  // ITimeProvider>). Without a virtual destructor, deleting a derived object
  // through a base pointer is undefined behavior in C++.
  // -------------------------------------------------------------------------
  virtual ~ITimeProvider() = default;

  // -------------------------------------------------------------------------
  // now_ms()
  // -------------------------------------------------------------------------
  // @brief  Returns the current time as milliseconds since the Unix epoch
  //         (1970-01-01 00:00:00 UTC).
  //
  // @return int64_t  Epoch time in milliseconds. Always non-negative for
  //         real timestamps; may be 0 before the simulation clock is
  //         initialized.
  //
  // @details
  // - LiveTimeProvider:       delegates to std::chrono::system_clock::now()
  //                           and converts to milliseconds.
  // - SimulationTimeProvider: returns the last value written by
  //                           advance_time().
  //
  // Thread-safety: Safe to call concurrently from any thread.
  // Side-effects:  None. This is a pure read operation.
  // -------------------------------------------------------------------------
  virtual std::int64_t now_ms() const = 0;
};

}  // namespace quant
