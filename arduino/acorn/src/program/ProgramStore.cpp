#include "ProgramStore.h"

#include <AcornHardware.h>

#include <algorithm>

#include "../AppConfig.h"
#include "AudioLibrary.h"

namespace acorn {
namespace ProgramStore {

namespace {

std::vector<ProgramSummary> summaries;
bool cached = false;
uint32_t cachedAudioGen = 0;

String pathFor(const String& id) { return String(app::PROGRAMS_DIR) + "/" + id + ".json"; }

bool readFile(const String& path, String& out) {
  File f = SdHardware::fs().open(path, FILE_READ);
  if (!f) return false;
  // Read one byte past the limit so the validator can report the overflow.
  const size_t len = std::min<size_t>(f.size(), limits::MAX_PROGRAM_JSON_BYTES + 1);
  out = "";
  out.reserve(len);
  uint8_t buf[256];
  size_t left = len;
  while (left) {
    const size_t got = f.read(buf, std::min(left, sizeof(buf)));
    if (!got) break;
    out.concat(reinterpret_cast<const char*>(buf), got);
    left -= got;
  }
  f.close();
  return out.length() == len;
}

bool writeFile(const String& path, const String& text) {
  File f = SdHardware::fs().open(path, FILE_WRITE);
  if (!f) return false;
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()) == text.length();
  f.flush();
  f.close();
  return ok;
}

// Finish or discard saves interrupted by power loss.
void recoverTemps(fs::FS& fs) {
  File dir = fs.open(app::PROGRAMS_DIR);
  if (!dir) return;
  std::vector<String> temps;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    f.close();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.endsWith(".json.tmp")) temps.push_back(name);
  }
  dir.close();
  for (const String& t : temps) {
    const String tmpPath = String(app::PROGRAMS_DIR) + "/" + t;
    const String target = tmpPath.substring(0, tmpPath.length() - 4);
    String text;
    if (!fs.exists(target) && readFile(tmpPath, text) &&
        ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog()).empty()) {
      fs.rename(tmpPath, target);
    } else {
      fs.remove(tmpPath);
    }
  }
}

void rebuild() {
  summaries.clear();
  cached = true;
  cachedAudioGen = AudioLibrary::generation();
  if (!SdHardware::isMounted()) return;
  fs::FS& fs = SdHardware::fs();
  if (!fs.exists(app::PROGRAMS_DIR)) fs.mkdir(app::PROGRAMS_DIR);
  recoverTemps(fs);
  File dir = fs.open(app::PROGRAMS_DIR);
  if (!dir || !dir.isDirectory()) return;
  std::vector<String> ids;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    const bool isDir = f.isDirectory();
    f.close();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (isDir || !name.endsWith(".json")) continue;
    const String id = name.substring(0, name.length() - 5);
    if (ProgramValidator::isValidId(id.c_str())) ids.push_back(id);
  }
  dir.close();
  std::sort(ids.begin(), ids.end(), [](const String& a, const String& b) { return a < b; });
  for (const String& id : ids) {
    String text;
    JsonDocument doc;
    Program p;
    ValidationErrors errs;
    if (!readFile(pathFor(id), text)) {
      errs.push_back({err::PROGRAM_JSON_INVALID, "Program file could not be read.", ""});
    } else {
      errs = ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog(), &p);
      deserializeJson(doc, text);
    }
    summaries.push_back(summarize(id, doc.as<JsonVariantConst>(), errs, &p));
  }
}

void ensureCache() {
  if (!cached || cachedAudioGen != AudioLibrary::generation()) rebuild();
}

}  // namespace

void invalidate() { cached = false; }

const std::vector<ProgramSummary>& list() {
  ensureCache();
  return summaries;
}

const ProgramSummary* summary(const String& id) {
  for (const ProgramSummary& s : list()) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

bool exists(const String& id) { return summary(id) != nullptr; }
size_t count() { return list().size(); }

bool readText(const String& id, String& out) {
  if (!ProgramValidator::isValidId(id.c_str()) || !SdHardware::isMounted()) return false;
  return readFile(pathFor(id), out);
}

ValidationErrors load(const String& id, Program& out, String* textOut) {
  String text;
  if (!readText(id, text)) {
    return {{err::PROGRAM_NOT_FOUND, std::string("Program '") + id.c_str() + "' not found.", ""}};
  }
  ValidationErrors errs = ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog(), &out);
  if (textOut) *textOut = text;
  return errs;
}

bool save(const Program& program, const char*& errorCode) {
  errorCode = "";
  if (!SdHardware::isMounted()) {
    errorCode = err::SD_NOT_PRESENT;
    return false;
  }
  fs::FS& fs = SdHardware::fs();
  if (!fs.exists(app::PROGRAMS_DIR)) fs.mkdir(app::PROGRAMS_DIR);
  const String id = program.id.c_str();
  const String target = pathFor(id);
  const String tmp = target + ".tmp";
  const String text = String(ProgramJson::toString(program, true).c_str()) + "\n";

  String back;
  if (!writeFile(tmp, text) || !readFile(tmp, back) || back != text ||
      !ProgramValidator::validateText(back.c_str(), back.length(), AudioLibrary::catalog()).empty()) {
    fs.remove(tmp);
    errorCode = SdHardware::checkWritable() == SdStatus::Full ? err::SD_FULL : err::SD_WRITE_FAILED;
    return false;
  }
  if (fs.exists(target) && !fs.remove(target)) {
    fs.remove(tmp);
    errorCode = err::SD_WRITE_FAILED;
    return false;
  }
  if (!fs.rename(tmp, target)) {
    errorCode = err::SD_WRITE_FAILED;  // recoverTemps() completes it on the next scan
    invalidate();
    return false;
  }
  invalidate();
  return true;
}

bool remove(const String& id) {
  const bool ok = SdHardware::isMounted() && SdHardware::fs().remove(pathFor(id));
  invalidate();
  return ok;
}

ProgramSummary summarize(const String& id, JsonVariantConst json, const ValidationErrors& errors,
                         const Program* parsed) {
  ProgramSummary s;
  s.id = id;
  s.name = json["name"].is<const char*>() ? json["name"].as<const char*>() : id.c_str();
  s.description = json["description"] | "";
  s.modifiedUtc = json["modified_utc"] | "";
  s.eventCount = json["events"].is<JsonArrayConst>() ? json["events"].size() : 0;
  s.valid = errors.empty();
  s.errorCount = errors.size();
  if (s.valid && parsed) s.revision = ProgramJson::revision(*parsed).c_str();
  else if (json.is<JsonObjectConst>()) s.revision = ProgramJson::rawRevision(json).c_str();
  for (JsonVariantConst ev : json["events"].as<JsonArrayConst>()) {
    if (!ev["audio"].is<const char*>()) continue;
    const String a = ev["audio"].as<const char*>();
    if (std::find(s.audio.begin(), s.audio.end(), a) == s.audio.end()) s.audio.push_back(a);
  }
  return s;
}

}  // namespace ProgramStore
}  // namespace acorn
