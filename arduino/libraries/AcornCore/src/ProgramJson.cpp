#include "ProgramJson.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "Hash.h"

namespace acorn {

int32_t parseTimeOfDay(const char* text) {
  if (!text) return -1;
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  const size_t n = strlen(text);
  if (n != 5 && n != 8) return -1;
  if (!digit(text[0]) || !digit(text[1]) || text[2] != ':' || !digit(text[3]) || !digit(text[4])) return -1;
  int h = (text[0] - '0') * 10 + (text[1] - '0');
  int m = (text[3] - '0') * 10 + (text[4] - '0');
  int s = 0;
  if (n == 8) {
    if (text[5] != ':' || !digit(text[6]) || !digit(text[7])) return -1;
    s = (text[6] - '0') * 10 + (text[7] - '0');
  }
  if (h > 23 || m > 59 || s > 59) return -1;
  return h * 3600 + m * 60 + s;
}

void formatTimeOfDay(int32_t seconds, char* out, size_t len) {
  if (seconds < 0) seconds = 0;
  snprintf(out, len, "%02d:%02d:%02d", static_cast<int>(seconds / 3600),
           static_cast<int>((seconds % 3600) / 60), static_cast<int>(seconds % 60));
}

namespace ProgramJson {

namespace {

void appendJsonString(std::string& out, const char* s) {
  out += '"';
  for (const char* p = s; *p; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void canonicalInto(JsonVariantConst v, std::string& out) {
  if (v.is<JsonArrayConst>()) {
    out += '[';
    bool first = true;
    for (JsonVariantConst item : v.as<JsonArrayConst>()) {
      if (!first) out += ',';
      first = false;
      canonicalInto(item, out);
    }
    out += ']';
  } else if (v.is<JsonObjectConst>()) {
    std::vector<const char*> keys;
    for (JsonPairConst kv : v.as<JsonObjectConst>()) keys.push_back(kv.key().c_str());
    std::sort(keys.begin(), keys.end(), [](const char* a, const char* b) { return strcmp(a, b) < 0; });
    out += '{';
    bool first = true;
    for (const char* k : keys) {
      if (!first) out += ',';
      first = false;
      appendJsonString(out, k);
      out += ':';
      canonicalInto(v[k], out);
    }
    out += '}';
  } else if (v.is<const char*>()) {
    appendJsonString(out, v.as<const char*>());
  } else if (v.is<bool>()) {
    out += v.as<bool>() ? "true" : "false";
  } else if (v.is<long long>()) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", v.as<long long>());
    out += buf;
  } else if (v.is<double>()) {
    std::string num;
    serializeJson(v, num);
    out += num;
  } else {
    out += "null";
  }
}

}  // namespace

void eventToJson(const Event& e, JsonObject o) {
  char t[12];
  o["event_id"] = e.eventId;
  if (e.type == EventType::Point) {
    o["type"] = "point";
    formatTimeOfDay(e.timeS, t, sizeof(t));
    o["time"] = t;
  } else {
    o["type"] = "range";
    formatTimeOfDay(e.startS, t, sizeof(t));
    o["start"] = t;
    formatTimeOfDay(e.endS, t, sizeof(t));
    o["end"] = t;
  }
  o["audio"] = e.audio;
  o["volume"] = e.volume;
  if (e.type == EventType::Range) {
    JsonObject r = o["recurrence"].to<JsonObject>();
    if (e.recurrence.type == RecurrenceType::Fixed) {
      r["type"] = "fixed";
      r["interval_seconds"] = e.recurrence.intervalS;
    } else {
      r["type"] = "random";
      r["min_interval_seconds"] = e.recurrence.minS;
      r["max_interval_seconds"] = e.recurrence.maxS;
    }
  }
}

void toJson(const Program& p, JsonObject o) {
  o["schema_version"] = p.schemaVersion;
  o["id"] = p.id;
  o["name"] = p.name;
  o["description"] = p.description;
  if (!p.modifiedUtc.empty()) o["modified_utc"] = p.modifiedUtc;
  JsonArray events = o["events"].to<JsonArray>();
  for (const Event& e : p.events) eventToJson(e, events.add<JsonObject>());
}

std::string toString(const Program& p, bool pretty) {
  JsonDocument doc;
  toJson(p, doc.to<JsonObject>());
  std::string out;
  if (pretty) serializeJsonPretty(doc, out);
  else serializeJson(doc, out);
  return out;
}

std::string canonical(JsonVariantConst v) {
  std::string out;
  canonicalInto(v, out);
  return out;
}

std::string revision(const Program& p) {
  JsonDocument doc;
  doc["schema_version"] = p.schemaVersion;
  JsonArray events = doc["events"].to<JsonArray>();
  for (const Event& e : p.events) eventToJson(e, events.add<JsonObject>());
  char hex[9];
  snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(fnv1a(canonical(doc.as<JsonVariantConst>()))));
  return hex;
}

std::string rawRevision(JsonVariantConst p) {
  JsonDocument doc;
  if (p["schema_version"].isNull()) doc["schema_version"] = 1;
  else doc["schema_version"] = p["schema_version"];
  if (p["events"].isNull()) doc["events"].to<JsonArray>();
  else doc["events"] = p["events"];
  char hex[9];
  snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(fnv1a(canonical(doc.as<JsonVariantConst>()))));
  return hex;
}

}  // namespace ProgramJson
}  // namespace acorn
