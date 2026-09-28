#pragma once

#include <stdint.h>

// Environmental sampling boundaries (init.md section 58). Samples fall on
// multiples of the interval counted from the UTC epoch, so a 10-minute
// interval samples at :00, :10, :20 ... UTC and never drifts with how long a
// wake takes. Intervals that do not divide a day stay epoch-aligned.

namespace acorn {
namespace EnvSchedule {

constexpr uint16_t MIN_INTERVAL_MINUTES = 1;
constexpr uint16_t MAX_INTERVAL_MINUTES = 1440;
constexpr uint16_t DEFAULT_INTERVAL_MINUTES = 10;

inline bool isValidInterval(long minutes) {
  return minutes >= MIN_INTERVAL_MINUTES && minutes <= MAX_INTERVAL_MINUTES;
}

// First boundary strictly after nowUtc.
inline int64_t nextSampleAfter(int64_t nowUtc, uint16_t intervalMinutes) {
  const int64_t iv = static_cast<int64_t>(intervalMinutes ? intervalMinutes : 1) * 60;
  int64_t q = nowUtc / iv;
  if (nowUtc % iv != 0 && nowUtc < 0) q--;
  return q * iv + iv;
}

// Most recent boundary at or before nowUtc.
inline int64_t sampleAtOrBefore(int64_t nowUtc, uint16_t intervalMinutes) {
  const int64_t iv = static_cast<int64_t>(intervalMinutes ? intervalMinutes : 1) * 60;
  return nextSampleAfter(nowUtc, intervalMinutes) - iv;
}

}  // namespace EnvSchedule
}  // namespace acorn
