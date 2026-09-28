#pragma once

#include <Arduino.h>
#include <AcornCore.h>
#include <ArduinoJson.h>

#include "../audio/PlaybackService.h"
#include "../services/BatteryService.h"
#include "../services/EnvironmentService.h"

// JSON fragments shared by /status and the other API resources, matching
// the shapes in dev/mock (docs/web-ui.md section 4).

namespace acorn {
namespace json {

// Fixed-precision number, or null when !valid.
void number(JsonObject o, const char* key, bool valid, float value, uint8_t decimals);

void timeInfo(JsonObject o);  // {utc, local, utc_offset_minutes, rtc_valid}
void battery(JsonObject o, const BatteryState& b);  // {voltage, percent, valid, model}
void readings(JsonObject o, const EnvState& e);     // {temperature_c, relative_humidity_pct, ambient_light_lux, ok}
void playback(JsonObject o, const PlaybackSnapshot& s);
void scheduledEvent(JsonObject o, const Program& p, const ScheduledEvent& e);
void batteryModel(JsonObject o, const BatteryModel& m);  // {name, points: [[v, pct], ...]}

}  // namespace json
}  // namespace acorn
