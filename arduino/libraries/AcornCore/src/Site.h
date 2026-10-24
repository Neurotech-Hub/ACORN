#pragma once

#include <ArduinoJson.h>

#include <string>

// Current site location, stored on the SD card as /config/site.json. Only
// latitude/longitude are stamped into the CSV logs; accuracy, source, time
// and note stay in the file and the API.

namespace acorn {

struct Site {
  bool valid = false;  // latitude/longitude present and in range
  double latitude = 0;
  double longitude = 0;
  bool hasAccuracy = false;
  double accuracyM = 0;
  std::string source;    // "" | phone_gps | manual | prestage
  std::string fixedUtc;  // "" or YYYY-MM-DDTHH:MM:SSZ
  std::string note;
};

namespace SiteJson {

constexpr size_t MAX_NOTE = 120;

// Parses a site object. On failure returns false with the offending field
// ("latitude", "accuracy_m", ...) and a user-facing message.
bool parse(JsonVariantConst in, Site& out, std::string& field, std::string& message);

// Full object (all keys, null when absent); unset sites give null lat/lon.
void toJson(const Site& s, JsonObject out);

}  // namespace SiteJson
}  // namespace acorn
