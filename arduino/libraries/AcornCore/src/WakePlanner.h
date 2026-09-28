#pragma once

#include <stdint.h>

// Unified next device event (init.md sections 41, 57). The sleep layer asks
// this for the earliest required wake; it contains no scheduling policy of
// its own, only the merge and the due-tolerance rule.

namespace acorn {
namespace WakePlanner {

constexpr int64_t NONE = -1;

enum class Reason : uint8_t { None, Playback, EnvSample };

struct Wake {
  int64_t utc = NONE;
  Reason reason = Reason::None;
};

// Either input may be NONE. On a tie, playback wins (the env sample runs
// in the same wake).
inline Wake earliest(int64_t nextPlaybackUtc, int64_t nextEnvSampleUtc) {
  Wake w;
  if (nextPlaybackUtc != NONE) w = {nextPlaybackUtc, Reason::Playback};
  if (nextEnvSampleUtc != NONE && (w.utc == NONE || nextEnvSampleUtc < w.utc)) {
    w = {nextEnvSampleUtc, Reason::EnvSample};
  }
  return w;
}

inline bool isDue(int64_t nowUtc, int64_t scheduledUtc, int32_t toleranceS) {
  const int64_t d = nowUtc - scheduledUtc;
  return d >= -toleranceS && d <= toleranceS;
}

}  // namespace WakePlanner
}  // namespace acorn
