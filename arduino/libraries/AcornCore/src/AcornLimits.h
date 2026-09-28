#pragma once

#include <stdint.h>

// Centralized implementation limits (init.md section 55). Exposed to clients
// through GET /api/v1/programs "limits"; exceeding one is a validation error.

namespace acorn {
namespace limits {

constexpr uint16_t MAX_PROGRAMS = 16;
constexpr uint16_t MAX_EVENTS = 32;
constexpr uint16_t MAX_NAME = 48;
constexpr uint16_t MAX_DESCRIPTION = 200;
constexpr uint16_t MAX_AUDIO_NAME = 32;         // file name within /audio
constexpr uint16_t MAX_AUDIO_FILES = 64;        // indexed by the audio library
constexpr uint32_t SCHEDULE_GUARD_MS = 2000;    // audio duration guard between events
constexpr uint32_t MAX_PROGRAM_JSON_BYTES = 16384;
constexpr uint16_t MAX_PREVIEW_COUNT = 500;
constexpr uint16_t PREVIEW_HORIZON_DAYS = 400;

// RTC wake tolerance (section 57): an event is executed only if the device
// is awake within this many seconds of its scheduled time.
constexpr int32_t EVENT_DUE_TOLERANCE_S = 10;

}  // namespace limits
}  // namespace acorn
