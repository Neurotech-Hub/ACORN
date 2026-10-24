#pragma once

#include <stdint.h>

#include <string>

// CSV rows for the SD logs (init.md sections 35-37). Invalid measurements are
// written as empty cells, never as zeros.

namespace acorn {
namespace Csv {

constexpr const char* PLAYBACK_HEADER =
    "actual_utc,scheduled_utc,program_id,program_revision,event_id,source,audio,volume,duration_ms,"
    "result,error_code,battery_voltage,battery_percent,temperature_c,relative_humidity_pct,ambient_light_lux,"
    "latitude,longitude";
constexpr const char* ENVIRONMENT_HEADER =
    "timestamp_utc,temperature_c,relative_humidity_pct,ambient_light_lux,battery_voltage,battery_percent,"
    "latitude,longitude";

struct Telemetry {
  bool batteryValid = false;
  float batteryVoltage = 0;
  int32_t batteryPercent = 0;
  bool envValid = false;  // temperature + humidity
  float temperatureC = 0;
  float humidityPct = 0;
  bool lightValid = false;
  float lux = 0;
};

// Site fix current when the row is written (/config/site.json); empty cells when unset.
struct Location {
  bool valid = false;
  double latitude = 0;
  double longitude = 0;
};

struct PlaybackRow {
  std::string actualUtc;
  std::string scheduledUtc;  // empty for manual playback
  std::string programId;
  std::string revision;
  std::string eventId;
  std::string source;  // scheduled | manual_ui | manual_test
  std::string audio;   // path or builtin:sine
  int32_t volume = 0;
  uint32_t durationMs = 0;
  std::string result;  // ok | stopped | error
  std::string errorCode;
  Telemetry telemetry;
  Location location;
};

std::string field(const std::string& s);  // quotes when needed
std::string coordinate(double degrees);   // 6 decimal places (~0.1 m)
std::string playbackRow(const PlaybackRow& r);
std::string environmentRow(const std::string& timestampUtc, const Telemetry& t, const Location& location = {});

}  // namespace Csv
}  // namespace acorn
