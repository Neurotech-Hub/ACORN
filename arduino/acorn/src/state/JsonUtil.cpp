#include "JsonUtil.h"

#include "../config/DeviceConfig.h"
#include "../services/TimeService.h"

namespace acorn {
namespace json {

void number(JsonObject o, const char* key, bool valid, float value, uint8_t decimals) {
  if (!valid || isnan(value)) o[key] = nullptr;
  else o[key] = serialized(String(value, static_cast<unsigned int>(decimals)));
}

void timeInfo(JsonObject o) {
  const int64_t now = TimeService::nowUtc();
  o["utc"] = TimeService::isoUtc(now);
  o["local"] = TimeService::isoLocal(now);
  o["utc_offset_minutes"] = TimeService::utcOffsetMinutes();
  o["rtc_valid"] = TimeService::isValid() && TimeService::rtcValid();
}

void battery(JsonObject o, const BatteryState& b) {
  number(o, "voltage", b.valid, b.voltage, 2);
  if (b.valid) o["percent"] = b.percent;
  else o["percent"] = nullptr;
  o["valid"] = b.valid;
  o["model"] = DeviceConfig::get().batteryModel.name.c_str();
}

void readings(JsonObject o, const EnvState& e) {
  number(o, "temperature_c", e.envValid, e.temperatureC, 1);
  number(o, "relative_humidity_pct", e.envValid, e.humidityPct, 1);
  number(o, "ambient_light_lux", e.lightValid, e.lux, 1);
  o["ok"] = e.sampled && e.ok();
}

void playback(JsonObject o, const PlaybackSnapshot& s) {
  if (!s.playing) {
    o["state"] = "idle";
    return;
  }
  o["state"] = "playing";
  o["source"] = playbackSourceName(s.request.source);
  o["kind"] = s.request.tone ? "tone" : "file";
  if (s.request.tone) o["audio"] = nullptr;
  else o["audio"] = s.request.path;
  o["volume"] = s.request.volume;
  o["duration_ms"] = s.durationMs;
  o["started_utc"] = TimeService::isoUtc(s.startedUtc);
  o["position_ms"] = s.positionMs;
}

void scheduledEvent(JsonObject o, const Program& p, const ScheduledEvent& e) {
  const Event& ev = p.events[e.eventIndex];
  char buf[24];
  o["event_id"] = ev.eventId.c_str();
  o["type"] = ev.type == EventType::Point ? "point" : "range";
  UtcTime::formatEpochIso(e.utc, buf, sizeof(buf));
  o["scheduled_utc"] = buf;
  UtcTime::formatEpochLocalIso(e.local, buf, sizeof(buf));
  o["scheduled_local"] = buf;
  o["audio"] = ev.audio.c_str();
  o["volume"] = ev.volume;
}

void batteryModel(JsonObject o, const BatteryModel& m) {
  o["name"] = m.name.c_str();
  JsonArray pts = o["points"].to<JsonArray>();
  for (const BatteryPoint& p : m.points) {
    JsonArray pt = pts.add<JsonArray>();
    pt.add(p.volts);
    pt.add(p.percent);
  }
}

}  // namespace json
}  // namespace acorn
