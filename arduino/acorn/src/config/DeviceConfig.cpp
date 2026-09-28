#include "DeviceConfig.h"

#include <AcornHardware.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace acorn {
namespace DeviceConfig {

namespace {

constexpr const char* NS = "acorn";
DeviceSettings settings;
Preferences prefs;

String modelToJson(const BatteryModel& m) {
  JsonDocument doc;
  doc["name"] = m.name.c_str();
  JsonArray pts = doc["points"].to<JsonArray>();
  for (const BatteryPoint& p : m.points) {
    JsonArray pt = pts.add<JsonArray>();
    pt.add(p.volts);
    pt.add(p.percent);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool modelFromJson(const String& text, BatteryModel& out) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  BatteryModel m;
  m.name = doc["name"] | "Custom";
  for (JsonArrayConst pt : doc["points"].as<JsonArrayConst>()) {
    m.points.push_back({pt[0].as<double>(), pt[1].as<int32_t>()});
  }
  if (!m.isValid()) return false;
  out = m;
  return true;
}

}  // namespace

void begin() {
  prefs.begin(NS, false);
  if (prefs.getUInt("schema", 0) != SCHEMA_VERSION) prefs.putUInt("schema", SCHEMA_VERSION);

  settings.name = prefs.getString("name", SystemHardware::deviceId());
  if (!isValidName(settings.name.c_str())) settings.name = SystemHardware::deviceId();
  settings.utcOffsetMinutes = prefs.getInt("utc_off", 0);
  if (!isValidUtcOffset(settings.utcOffsetMinutes)) settings.utcOffsetMinutes = 0;
  settings.batteryModel = BatteryModel::defaultM12();
  if (prefs.isKey("batt_model")) modelFromJson(prefs.getString("batt_model"), settings.batteryModel);
  settings.batteryCalibration = prefs.getFloat("batt_cal", 1.0f);
  if (!(settings.batteryCalibration > 0.5f && settings.batteryCalibration < 1.5f)) settings.batteryCalibration = 1.0f;
  settings.envEnabled = prefs.getBool("env_on", true);
  settings.envIntervalMinutes = prefs.getUShort("env_iv", EnvSchedule::DEFAULT_INTERVAL_MINUTES);
  if (!EnvSchedule::isValidInterval(settings.envIntervalMinutes)) {
    settings.envIntervalMinutes = EnvSchedule::DEFAULT_INTERVAL_MINUTES;
  }
  settings.activeProgramId = prefs.getString("active", DEFAULT_ACTIVE_PROGRAM);
}

const DeviceSettings& get() { return settings; }
const char* deviceId() { return SystemHardware::deviceId(); }

bool isValidName(const char* name) {
  if (!name) return false;
  const size_t n = strlen(name);
  if (n < 1 || n > 32) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = name[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
  }
  return true;
}

bool isValidUtcOffset(long minutes) { return minutes >= -720 && minutes <= 840 && minutes % 15 == 0; }

bool setName(const String& name) {
  if (!isValidName(name.c_str())) return false;
  settings.name = name;
  return prefs.putString("name", name) > 0;
}

bool setUtcOffset(int32_t minutes) {
  if (!isValidUtcOffset(minutes)) return false;
  settings.utcOffsetMinutes = minutes;
  return prefs.putInt("utc_off", minutes) > 0;
}

bool setBatteryModel(const BatteryModel& model) {
  if (!model.isValid()) return false;
  settings.batteryModel = model;
  return prefs.putString("batt_model", modelToJson(model)) > 0;
}

bool setBatteryCalibration(float scale) {
  if (!(scale > 0.5f && scale < 1.5f)) return false;
  settings.batteryCalibration = scale;
  return prefs.putFloat("batt_cal", scale) > 0;
}

bool setEnvironment(bool enabled, uint16_t intervalMinutes) {
  if (!EnvSchedule::isValidInterval(intervalMinutes)) return false;
  settings.envEnabled = enabled;
  settings.envIntervalMinutes = intervalMinutes;
  return prefs.putBool("env_on", enabled) > 0 && prefs.putUShort("env_iv", intervalMinutes) > 0;
}

bool setActiveProgram(const String& id) {
  settings.activeProgramId = id;
  if (id.isEmpty()) return prefs.putString("active", "") >= 0;
  return prefs.putString("active", id) > 0;
}

}  // namespace DeviceConfig
}  // namespace acorn
