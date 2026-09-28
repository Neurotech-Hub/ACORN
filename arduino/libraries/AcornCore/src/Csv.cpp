#include "Csv.h"

#include <stdio.h>

namespace acorn {
namespace Csv {

namespace {

std::string num(float v, int decimals) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
  return buf;
}

std::string integer(long long v) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%lld", v);
  return buf;
}

void appendTelemetry(std::string& out, const Telemetry& t, bool batteryFirst) {
  const std::string battery = t.batteryValid ? num(t.batteryVoltage, 2) + "," + integer(t.batteryPercent) : ",";
  const std::string env = (t.envValid ? num(t.temperatureC, 1) + "," + num(t.humidityPct, 1) : ",") + "," +
                          (t.lightValid ? num(t.lux, 1) : "");
  out += batteryFirst ? battery + "," + env : env + "," + battery;
}

}  // namespace

std::string field(const std::string& s) {
  if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  out += '"';
  return out;
}

std::string playbackRow(const PlaybackRow& r) {
  std::string out;
  out += field(r.actualUtc) + "," + field(r.scheduledUtc) + "," + field(r.programId) + "," + field(r.revision) +
         "," + field(r.eventId) + "," + field(r.source) + "," + field(r.audio) + "," + integer(r.volume) + "," +
         integer(r.durationMs) + "," + field(r.result) + "," + field(r.errorCode) + ",";
  appendTelemetry(out, r.telemetry, true);
  return out;
}

std::string environmentRow(const std::string& timestampUtc, const Telemetry& t) {
  std::string out = field(timestampUtc) + ",";
  appendTelemetry(out, t, false);
  return out;
}

}  // namespace Csv
}  // namespace acorn
