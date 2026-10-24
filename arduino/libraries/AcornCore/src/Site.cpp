#include "Site.h"

#include <math.h>
#include <string.h>

#include "Csv.h"
#include "UtcTime.h"

namespace acorn {
namespace SiteJson {

namespace {

bool number(JsonVariantConst v, double& out) {
  if (v.isNull() || v.is<bool>() || !(v.is<double>() || v.is<long long>())) return false;
  out = v.as<double>();
  return isfinite(out);
}

bool fail(std::string& field, std::string& message, const char* f, const char* m) {
  field = f;
  message = m;
  return false;
}

}  // namespace

bool parse(JsonVariantConst in, Site& out, std::string& field, std::string& message) {
  out = Site{};
  if (!in.is<JsonObjectConst>()) return fail(field, message, "", "Site must be a JSON object.");
  if (!number(in["latitude"], out.latitude) || out.latitude < -90 || out.latitude > 90) {
    return fail(field, message, "latitude", "Latitude must be a number from -90 to 90.");
  }
  if (!number(in["longitude"], out.longitude) || out.longitude < -180 || out.longitude > 180) {
    return fail(field, message, "longitude", "Longitude must be a number from -180 to 180.");
  }
  JsonVariantConst acc = in["accuracy_m"];
  if (!acc.isNull()) {
    if (!number(acc, out.accuracyM) || out.accuracyM < 0) {
      return fail(field, message, "accuracy_m", "Accuracy must be a number of metres, 0 or more.");
    }
    out.hasAccuracy = true;
  }
  JsonVariantConst src = in["source"];
  if (!src.isNull()) {
    const char* s = src.is<const char*>() ? src.as<const char*>() : nullptr;
    if (!s || (strcmp(s, "phone_gps") && strcmp(s, "manual") && strcmp(s, "prestage"))) {
      return fail(field, message, "source", "Source must be phone_gps, manual or prestage.");
    }
    out.source = s;
  }
  JsonVariantConst fixed = in["fixed_utc"];
  if (!fixed.isNull()) {
    UtcDateTime t;
    const char* s = fixed.is<const char*>() ? fixed.as<const char*>() : nullptr;
    if (!s || strlen(s) != 20 || s[19] != 'Z' || !UtcTime::parseIso(s, t)) {
      return fail(field, message, "fixed_utc", "fixed_utc must be an ISO time such as 2026-10-24T13:15:00Z.");
    }
    out.fixedUtc = s;
  }
  JsonVariantConst note = in["note"];
  if (!note.isNull()) {
    if (!note.is<const char*>() || strlen(note.as<const char*>()) > MAX_NOTE) {
      return fail(field, message, "note", "Note must be text of at most 120 characters.");
    }
    out.note = note.as<const char*>();
  }
  out.valid = true;
  return true;
}

void toJson(const Site& s, JsonObject out) {
  if (!s.valid) {
    out["latitude"] = nullptr;
    out["longitude"] = nullptr;
    out["accuracy_m"] = nullptr;
    out["source"] = nullptr;
    out["fixed_utc"] = nullptr;
    out["note"] = nullptr;
    return;
  }
  // Same 6-decimal text as the CSV logs, so the API never shows more precision than is stored.
  out["latitude"] = serialized(Csv::coordinate(s.latitude));
  out["longitude"] = serialized(Csv::coordinate(s.longitude));
  if (s.hasAccuracy) out["accuracy_m"] = s.accuracyM;
  else out["accuracy_m"] = nullptr;
  if (s.source.empty()) out["source"] = nullptr;
  else out["source"] = s.source;
  if (s.fixedUtc.empty()) out["fixed_utc"] = nullptr;
  else out["fixed_utc"] = s.fixedUtc;
  out["note"] = s.note;
}

}  // namespace SiteJson
}  // namespace acorn
