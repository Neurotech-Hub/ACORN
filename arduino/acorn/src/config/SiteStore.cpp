#include "SiteStore.h"

#include <AcornHardware.h>

#include "../AppConfig.h"

namespace acorn {
namespace SiteStore {

namespace {

constexpr size_t MAX_FILE_BYTES = 1024;

Site site;
bool cached = false;

bool readFile(const char* path, String& out) {
  File f = SdHardware::fs().open(path, FILE_READ);
  if (!f) return false;
  const size_t len = f.size();
  if (len > MAX_FILE_BYTES) {
    f.close();
    return false;
  }
  out = f.readString();
  f.close();
  return out.length() == len;
}

bool parseText(const String& text, Site& out) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  std::string field, message;
  return SiteJson::parse(doc.as<JsonVariantConst>(), out, field, message);
}

String toText(const Site& s) {
  JsonDocument doc;
  SiteJson::toJson(s, doc.to<JsonObject>());
  String text;
  serializeJsonPretty(doc, text);
  return text + "\n";
}

void load() {
  cached = true;
  site = Site{};
  if (!SdHardware::isMounted()) return;
  fs::FS& fs = SdHardware::fs();
  const String tmp = String(app::SITE_PATH) + ".tmp";
  String text;
  // Finish or discard a save interrupted by power loss.
  if (fs.exists(tmp)) {
    Site pending;
    if (!fs.exists(app::SITE_PATH) && readFile(tmp.c_str(), text) && parseText(text, pending)) {
      fs.rename(tmp, app::SITE_PATH);
    } else {
      fs.remove(tmp);
    }
  }
  if (!fs.exists(app::SITE_PATH)) return;
  Site parsed;
  if (readFile(app::SITE_PATH, text) && parseText(text, parsed)) {
    site = parsed;
  } else {
    Serial.println("[site] /config/site.json is invalid; logging without a location");
  }
}

}  // namespace

void invalidate() { cached = false; }

const Site& get() {
  if (!cached) load();
  return site;
}

bool hasFix() { return get().valid; }

Csv::Location location() {
  const Site& s = get();
  return {s.valid, s.latitude, s.longitude};
}

bool save(const Site& next, const char*& errorCode) {
  errorCode = "";
  if (!SdHardware::isMounted()) {
    errorCode = err::SD_NOT_PRESENT;
    return false;
  }
  fs::FS& fs = SdHardware::fs();
  if (!fs.exists(app::CONFIG_DIR)) fs.mkdir(app::CONFIG_DIR);
  const String tmp = String(app::SITE_PATH) + ".tmp";
  const String text = toText(next);
  Site back;
  String readBack;
  File f = fs.open(tmp, FILE_WRITE);
  const bool wrote = f && f.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()) == text.length();
  if (f) {
    f.flush();
    f.close();
  }
  if (!wrote || !readFile(tmp.c_str(), readBack) || readBack != text || !parseText(readBack, back)) {
    fs.remove(tmp);
    errorCode = SdHardware::checkWritable() == SdStatus::Full ? err::SD_FULL : err::SD_WRITE_FAILED;
    return false;
  }
  if (fs.exists(app::SITE_PATH) && !fs.remove(app::SITE_PATH)) {
    fs.remove(tmp);
    errorCode = err::SD_WRITE_FAILED;
    return false;
  }
  if (!fs.rename(tmp, app::SITE_PATH)) {
    errorCode = err::SD_WRITE_FAILED;  // load() completes it on the next read
    invalidate();
    return false;
  }
  site = back;
  cached = true;
  return true;
}

bool clear(const char*& errorCode) {
  errorCode = "";
  if (!SdHardware::isMounted()) {
    errorCode = err::SD_NOT_PRESENT;
    return false;
  }
  fs::FS& fs = SdHardware::fs();
  if (fs.exists(app::SITE_PATH) && !fs.remove(app::SITE_PATH)) {
    errorCode = err::SD_WRITE_FAILED;
    return false;
  }
  site = Site{};
  cached = true;
  return true;
}

}  // namespace SiteStore
}  // namespace acorn
