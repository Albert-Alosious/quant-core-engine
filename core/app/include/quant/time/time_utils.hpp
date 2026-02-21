#pragma once

#include "quant/events/event_types.hpp"

#include <chrono>
#include <cstdint>

namespace quant {

// -----------------------------------------------------------------------------
// Time conversion utilities
// -----------------------------------------------------------------------------
//
// @brief  Free functions that convert between the engine's Timestamp type
//         (std::chrono::system_clock::time_point) and int64_t milliseconds
//         since epoch.
//
// @details
// The ITimeProvider interface returns int64_t milliseconds, but existing
// event structs (MarketDataEvent, ExecutionReportEvent, etc.) carry a
// Timestamp (chrono time_point). These utilities bridge the two
// representations so new components (MockExecutionEngine, MarketDataGateway)
// can use ITimeProvider::now_ms() and still populate Timestamp fields.
//
// Both functions are inline because they are trivial one-liners. Placing
// them in a header avoids a .cpp file and link-time dependency for what is
// purely a type conversion.
//
// Thread-safety: Stateless — safe to call from any thread.
// -----------------------------------------------------------------------------

// -------------------------------------------------------------------------
// ms_to_timestamp
// -------------------------------------------------------------------------
// @brief  Converts epoch milliseconds to a Timestamp (time_point).
//
// @param  ms  Milliseconds since 1970-01-01 00:00:00 UTC.
// @return Timestamp  The equivalent std::chrono::system_clock::time_point.
//
// @details
// Constructs a time_point from a duration of the given milliseconds.
// std::chrono::milliseconds{ms} creates a duration; Timestamp{duration}
// interprets it as an offset from the epoch. This is the inverse of
// timestamp_to_ms().
// -------------------------------------------------------------------------
inline Timestamp ms_to_timestamp(std::int64_t ms) {
  return Timestamp{std::chrono::milliseconds{ms}};
}

// -------------------------------------------------------------------------
// timestamp_to_ms
// -------------------------------------------------------------------------
// @brief  Converts a Timestamp (time_point) to epoch milliseconds.
//
// @param  tp  A std::chrono::system_clock::time_point.
// @return int64_t  Milliseconds since 1970-01-01 00:00:00 UTC.
//
// @details
// Extracts the duration since epoch via time_since_epoch(), then truncates
// to millisecond resolution with duration_cast. count() returns the raw
// integer. This is the inverse of ms_to_timestamp().
// -------------------------------------------------------------------------
inline std::int64_t timestamp_to_ms(Timestamp tp) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             tp.time_since_epoch())
      .count();
}

}  // namespace quant
