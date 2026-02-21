#include "quant/time/live_time_provider.hpp"

#include <chrono>

namespace quant {

// -----------------------------------------------------------------------------
// now_ms(): delegate to system_clock and convert to epoch milliseconds
// -----------------------------------------------------------------------------
std::int64_t LiveTimeProvider::now_ms() const {
  // std::chrono::system_clock::now() returns a time_point representing the
  // current wall-clock time. time_since_epoch() gives us the duration from
  // the Unix epoch (1970-01-01 00:00:00 UTC). duration_cast truncates to
  // the target resolution (milliseconds). count() extracts the raw integer.
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}

}  // namespace quant
