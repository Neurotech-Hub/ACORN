#pragma once

#include <string>
#include <vector>

// Machine-readable error codes (init.md section 48, plus the API-level codes
// documented in docs/web-ui.md section 4).

namespace acorn {
namespace err {

constexpr const char* SD_NOT_PRESENT = "SD_NOT_PRESENT";
constexpr const char* SD_MOUNT_FAILED = "SD_MOUNT_FAILED";
constexpr const char* SD_WRITE_FAILED = "SD_WRITE_FAILED";
constexpr const char* SD_FULL = "SD_FULL";

constexpr const char* PROGRAM_NOT_FOUND = "PROGRAM_NOT_FOUND";
constexpr const char* PROGRAM_JSON_INVALID = "PROGRAM_JSON_INVALID";
constexpr const char* PROGRAM_SCHEMA_UNSUPPORTED = "PROGRAM_SCHEMA_UNSUPPORTED";
constexpr const char* PROGRAM_RANGE_INVALID = "PROGRAM_RANGE_INVALID";
constexpr const char* PROGRAM_RANGE_OVERLAP = "PROGRAM_RANGE_OVERLAP";
constexpr const char* PROGRAM_EVENT_COLLISION = "PROGRAM_EVENT_COLLISION";
constexpr const char* PROGRAM_AUDIO_MISSING = "PROGRAM_AUDIO_MISSING";
constexpr const char* PROGRAM_AUDIO_INVALID = "PROGRAM_AUDIO_INVALID";
constexpr const char* PROGRAM_INTERVAL_TOO_SHORT = "PROGRAM_INTERVAL_TOO_SHORT";
constexpr const char* PROGRAM_RANDOM_RANGE_INVALID = "PROGRAM_RANDOM_RANGE_INVALID";
constexpr const char* PROGRAM_LIMIT_EXCEEDED = "PROGRAM_LIMIT_EXCEEDED";
constexpr const char* PROGRAM_ID_EXISTS = "PROGRAM_ID_EXISTS";
constexpr const char* PROGRAM_ACTIVE = "PROGRAM_ACTIVE";

constexpr const char* WAV_UNSUPPORTED = "WAV_UNSUPPORTED";
constexpr const char* WAV_CORRUPT = "WAV_CORRUPT";

constexpr const char* RTC_INVALID = "RTC_INVALID";
constexpr const char* TIME_NOT_SET = "TIME_NOT_SET";
constexpr const char* TIME_INVALID = "TIME_INVALID";
constexpr const char* TIME_OFFSET_INVALID = "TIME_OFFSET_INVALID";

constexpr const char* BATTERY_READING_INVALID = "BATTERY_READING_INVALID";
constexpr const char* BATTERY_MODEL_INVALID = "BATTERY_MODEL_INVALID";
constexpr const char* SENSOR_READ_FAILED = "SENSOR_READ_FAILED";

constexpr const char* PLAYBACK_START_FAILED = "PLAYBACK_START_FAILED";
constexpr const char* PLAYBACK_IO_ERROR = "PLAYBACK_IO_ERROR";
constexpr const char* VOLUME_INVALID = "VOLUME_INVALID";

constexpr const char* AUDIO_IN_USE = "AUDIO_IN_USE";
constexpr const char* AUDIO_NAME_TOO_LONG = "AUDIO_NAME_TOO_LONG";

constexpr const char* OTA_INVALID = "OTA_INVALID";
constexpr const char* OTA_FAILED = "OTA_FAILED";

constexpr const char* DEVICE_NAME_INVALID = "DEVICE_NAME_INVALID";
constexpr const char* ENV_CONFIG_INVALID = "ENV_CONFIG_INVALID";
constexpr const char* ENV_INTERVAL_INVALID = "ENV_INTERVAL_INVALID";
constexpr const char* UPLOAD_INVALID = "UPLOAD_INVALID";
constexpr const char* UPLOAD_TOO_LARGE = "UPLOAD_TOO_LARGE";
constexpr const char* JSON_INVALID = "JSON_INVALID";
constexpr const char* NOT_FOUND = "NOT_FOUND";
constexpr const char* INTERNAL = "INTERNAL";

}  // namespace err

struct ValidationError {
  std::string code;
  std::string message;
  std::string field;  // e.g. "events[1].recurrence.interval_seconds"
};

using ValidationErrors = std::vector<ValidationError>;

}  // namespace acorn
