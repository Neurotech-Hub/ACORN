#include "ApiServer.h"

#include <AcornCore.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#include <algorithm>

#include "../AppConfig.h"
#include "../audio/PlaybackService.h"
#include "../config/DeviceConfig.h"
#include "../logging/CsvFile.h"
#include "../program/ActiveProgram.h"
#include "../program/AudioLibrary.h"
#include "../program/ProgramStore.h"
#include "../services/BatteryService.h"
#include "../services/EnvironmentService.h"
#include "../services/FirmwareService.h"
#include "../services/TimeService.h"
#include "../services/WifiService.h"
#include "../state/DeviceState.h"
#include "../state/JsonUtil.h"
#include "WebUi.h"

namespace acorn {
namespace ApiServer {

namespace {

constexpr const char* PREFIX = "/api/v1";
constexpr const char* JSON_TYPE = "application/json; charset=utf-8";
constexpr const char* CSV_TYPE = "text/csv; charset=utf-8";

WebServer* server = nullptr;
bool running = false;
void (*backgroundHook)() = nullptr;
uint32_t lastHookMs = 0;

// ---- responses ------------------------------------------------------------

void send(int status, JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  server->sendHeader("Cache-Control", "no-store");
  server->send(status, JSON_TYPE, body);
}

JsonObject okData(JsonDocument& doc) {
  doc["ok"] = true;
  return doc["data"].to<JsonObject>();
}

// Returns error.details for callers that add to it before send().
JsonObject errorDoc(JsonDocument& doc, const char* code, const String& message, const char* field = nullptr) {
  doc["ok"] = false;
  JsonObject e = doc["error"].to<JsonObject>();
  e["code"] = code;
  e["message"] = message;
  if (field) e["field"] = field;
  return e["details"].to<JsonObject>();
}

void fail(int status, const char* code, const String& message, const char* field = nullptr) {
  JsonDocument doc;
  errorDoc(doc, code, message, field);
  send(status, doc);
}

void errorsJson(JsonArray out, const ValidationErrors& errors) {
  for (const ValidationError& e : errors) {
    JsonObject o = out.add<JsonObject>();
    o["code"] = e.code.c_str();
    o["message"] = e.message.c_str();
    o["field"] = e.field.c_str();
  }
}

void programInvalid(const ValidationErrors& errors) {
  JsonDocument doc;
  const ValidationError& first = errors.front();
  JsonObject details = errorDoc(doc, first.code.c_str(), first.message.c_str(), first.field.c_str());
  errorsJson(details["errors"].to<JsonArray>(), errors);
  send(422, doc);
}

bool requireSd() {
  if (DeviceState::sdReady()) return true;
  if (DeviceState::sdPresent()) fail(503, err::SD_MOUNT_FAILED, "The SD card could not be mounted.");
  else fail(503, err::SD_NOT_PRESENT, "No SD card detected.");
  return false;
}

const char* methodName(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_DELETE: return "DELETE";
    case HTTP_PATCH: return "PATCH";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "?";
  }
}

// ---- request helpers ------------------------------------------------------

String urlDecode(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '%' && i + 2 < s.length()) {
      char hex[3] = {s[i + 1], s[i + 2], 0};
      out += static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

String requestBody;  // JSON body of the current request, set by dispatch()
const String& body() { return requestBody; }

// Parses an optional JSON body. Empty bodies leave `doc` null and succeed.
bool readJson(JsonDocument& doc, const char* code = err::JSON_INVALID,
              const char* message = "Request body is not valid JSON.") {
  const String& b = body();
  if (b.isEmpty()) return true;
  if (deserializeJson(doc, b)) {
    fail(400, code, message);
    return false;
  }
  return true;
}

String nowIso() { return TimeService::isoUtc(TimeService::nowUtc()); }

// ---- resource JSON ----------------------------------------------------------

void deviceJson(JsonObject o) {
  const DeviceSettings& cfg = DeviceConfig::get();
  o["id"] = DeviceConfig::deviceId();
  o["name"] = cfg.name;
  o["firmware_version"] = ACORN_FIRMWARE_VERSION;
  json::batteryModel(o["battery_model"].to<JsonObject>(), cfg.batteryModel);
}

void audioEntryJson(JsonObject o, const AudioEntry& e) {
  o["name"] = e.name;
  o["path"] = e.path();
  if (e.status != WavStatus::Corrupt) {
    o["sample_rate"] = e.info.sampleRate;
    o["bits_per_sample"] = e.info.bitsPerSample;
    o["channels"] = e.info.channels;
    o["duration_ms"] = e.info.durationMs;
  }
  o["size_bytes"] = e.sizeBytes;
  o["valid"] = e.valid();
  if (!e.valid()) {
    JsonObject err = o["error"].to<JsonObject>();
    err["code"] = wavStatusCode(e.status);
    err["message"] = e.errorMessage;
  }
  JsonArray refs = o["referenced_by"].to<JsonArray>();
  bool inActive = false;
  const bool activeExists = ActiveProgram::program() != nullptr;
  const String path = e.path();
  for (const ProgramSummary& s : ProgramStore::list()) {
    if (std::find(s.audio.begin(), s.audio.end(), path) == s.audio.end()) continue;
    refs.add(s.id);
    if (activeExists && s.id == ActiveProgram::id()) inActive = true;
  }
  o["in_active_program"] = inActive;
}

void summaryJson(JsonObject o, const ProgramSummary& s) {
  o["id"] = s.id;
  o["name"] = s.name;
  o["description"] = s.description;
  if (s.modifiedUtc.length()) o["modified_utc"] = s.modifiedUtc;
  else o["modified_utc"] = nullptr;
  o["event_count"] = s.eventCount;
  if (s.revision.length()) o["revision"] = s.revision;
  else o["revision"] = nullptr;
  o["valid"] = s.valid;
  o["error_count"] = s.errorCount;
  o["active"] = s.id == DeviceConfig::get().activeProgramId && s.id.length() > 0;
}

void environmentJson(JsonObject o) {
  const DeviceSettings& cfg = DeviceConfig::get();
  o["enabled"] = cfg.envEnabled;
  o["interval_minutes"] = cfg.envIntervalMinutes;
  o["min_interval_minutes"] = EnvSchedule::MIN_INTERVAL_MINUTES;
  json::readings(o["readings"].to<JsonObject>(), EnvironmentService::fresh(10000));
  const int64_t next = DeviceState::nextEnvSample(TimeService::nowUtc());
  if (next >= 0) o["next_sample_utc"] = TimeService::isoUtc(next);
  else o["next_sample_utc"] = nullptr;
}

void logInfoJson(JsonObject o, const char* path) {
  const CsvFile::Info i = CsvFile::info(path);
  o["path"] = path;
  o["size_bytes"] = i.sizeBytes;
  o["rows"] = i.rows;
  if (i.lastTimestamp.length()) o["modified_utc"] = i.lastTimestamp;
  else o["modified_utc"] = nullptr;
}

// ---- routes: status / device / time ------------------------------------------

void getStatus() {
  JsonDocument doc;
  DeviceState::statusJson(okData(doc));
  send(200, doc);
}

void putDevice() {
  JsonDocument b;
  if (!readJson(b)) return;
  JsonVariantConst name = b["name"];
  if (!name.isNull() && !(name.is<const char*>() && DeviceConfig::isValidName(name.as<const char*>()))) {
    return fail(422, err::DEVICE_NAME_INVALID, "Name must be 1–32 characters: letters, digits, _ or -.", "name");
  }
  if (!b["battery_model"].isNull()) {
    return fail(422, err::BATTERY_MODEL_INVALID, "The battery model is fixed in this firmware and cannot be edited.",
                "battery_model");
  }
  if (!name.isNull()) DeviceConfig::setName(name.as<const char*>());
  JsonDocument doc;
  deviceJson(okData(doc));
  send(200, doc);
}

bool parseUtc(const char* text, int64_t& out) {
  if (!text) return false;
  const size_t n = strlen(text);
  if (n < 20 || text[n - 1] != 'Z') return false;
  char buf[32];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char* dot = strchr(buf, '.');  // browsers send milliseconds
  if (dot) {
    dot[0] = 'Z';
    dot[1] = 0;
  }
  UtcDateTime t;
  if (!UtcTime::parseIso(buf, t)) return false;
  out = UtcTime::toEpoch(t);
  return true;
}

void putTime() {
  JsonDocument b;
  if (!readJson(b)) return;
  JsonVariantConst utc = b["utc"];
  int64_t epoch = 0;
  if (!utc.isNull() && !(utc.is<const char*>() && parseUtc(utc.as<const char*>(), epoch))) {
    return fail(422, err::TIME_INVALID, "utc must be an ISO-8601 UTC timestamp ending in Z.", "utc");
  }
  JsonVariantConst off = b["utc_offset_minutes"];
  if (!off.isNull() && !(off.is<long>() && DeviceConfig::isValidUtcOffset(off.as<long>()))) {
    return fail(422, err::TIME_OFFSET_INVALID, "UTC offset must be a multiple of 15 minutes between −12:00 and +14:00.",
                "utc_offset_minutes");
  }
  if (!utc.isNull()) TimeService::setUtc(epoch);
  if (!off.isNull()) TimeService::setUtcOffset(off.as<int32_t>());
  JsonDocument doc;
  json::timeInfo(okData(doc));
  send(200, doc);
}

// ---- routes: audio --------------------------------------------------------------

struct AudioUpload {
  bool started = false;
  bool failed = false;
  int status = 0;
  const char* code = "";
  String message;
  String name;
  size_t bytes = 0;
  UploadResult result;
  bool finished = false;
};
AudioUpload audioUp;

void audioUploadFail(int status, const char* code, const String& message) {
  if (audioUp.failed) return;
  audioUp.failed = true;
  audioUp.status = status;
  audioUp.code = code;
  audioUp.message = message;
  AudioLibrary::uploadAbort();
}

void onAudioUpload(HTTPUpload& up) {
  if (up.status == UPLOAD_FILE_START) {
    if (audioUp.started) return;  // only the first file part is used
    audioUp = AudioUpload{};
    audioUp.started = true;
    if (!DeviceState::sdReady()) return audioUploadFail(503, err::SD_NOT_PRESENT, "No SD card detected.");
    audioUp.name = AudioLibrary::sanitizeName(up.filename);
    if (!audioUp.name.endsWith(".wav")) {
      return audioUploadFail(422, err::WAV_UNSUPPORTED, "Only .wav files are supported.");
    }
    if (audioUp.name.length() > limits::MAX_AUDIO_NAME) {
      return audioUploadFail(422, err::AUDIO_NAME_TOO_LONG,
                             String("File name exceeds ") + limits::MAX_AUDIO_NAME + " characters.");
    }
    const size_t declared = server->clientContentLength();
    if (declared > app::MAX_UPLOAD_BYTES) {
      return audioUploadFail(413, err::UPLOAD_TOO_LARGE, "Upload exceeds 16 MB.");
    }
    const SdInfo& sd = DeviceState::sdInfo();
    if (sd.totalBytes && declared > sd.totalBytes - sd.usedBytes) {
      return audioUploadFail(507, err::SD_FULL, "Not enough free space on the SD card.");
    }
    if (!AudioLibrary::uploadBegin()) {
      return audioUploadFail(507, err::SD_WRITE_FAILED, "Could not create the upload file on the SD card.");
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!audioUp.started || audioUp.failed || audioUp.finished) return;
    audioUp.bytes += up.currentSize;
    if (audioUp.bytes > app::MAX_UPLOAD_BYTES) {
      return audioUploadFail(413, err::UPLOAD_TOO_LARGE, "Upload exceeds 16 MB.");
    }
    if (!AudioLibrary::uploadWrite(up.buf, up.currentSize)) {
      return audioUploadFail(507, err::SD_WRITE_FAILED, "Writing to the SD card failed.");
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!audioUp.started || audioUp.failed || audioUp.finished) return;
    audioUp.finished = true;
    audioUp.result = AudioLibrary::uploadFinish(audioUp.name);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    audioUploadFail(400, err::UPLOAD_INVALID, "Upload aborted.");
  }
}

void postAudio() {
  AudioUpload up = audioUp;
  audioUp = AudioUpload{};
  if (!requireSd()) return;
  if (!up.started) return fail(400, err::UPLOAD_INVALID, "Expected a multipart upload with a file field.");
  if (up.failed) return fail(up.status, up.code, up.message, up.status == 422 ? "file" : nullptr);
  if (!up.finished) return fail(400, err::UPLOAD_INVALID, "Upload was incomplete.");
  DeviceState::refreshSd();
  const UploadResult& r = up.result;
  if (!r.ok) {
    JsonDocument doc;
    JsonObject details = errorDoc(doc, r.code, r.message, r.httpStatus == 422 ? "file" : nullptr);
    if (r.httpStatus == 422) {
      if (r.entry.status != WavStatus::Corrupt) {
        details["sample_rate"] = r.entry.info.sampleRate;
        details["bits_per_sample"] = r.entry.info.bitsPerSample;
        details["channels"] = r.entry.info.channels;
        details["duration_ms"] = r.entry.info.durationMs;
      }
      details["valid"] = false;
      JsonObject e = details["error"].to<JsonObject>();
      e["code"] = r.code;
      e["message"] = r.message;
    }
    return send(r.httpStatus, doc);
  }
  const AudioEntry* e = AudioLibrary::find(up.name);
  if (!e) return fail(500, err::INTERNAL, "Upload stored but not found in the library.");
  JsonDocument doc;
  audioEntryJson(okData(doc), *e);
  send(r.httpStatus, doc);
}

void getAudioList() {
  JsonDocument doc;
  JsonObject data = okData(doc);
  JsonArray files = data["files"].to<JsonArray>();
  for (const AudioEntry& e : AudioLibrary::list()) audioEntryJson(files.add<JsonObject>(), e);
  JsonObject rules = data["rules"].to<JsonObject>();
  rules["bits_per_sample"] = 16;
  rules["channels"] = 1;
  JsonArray rates = rules["sample_rates"].to<JsonArray>();
  rates.add(44100);
  rates.add(48000);
  send(200, doc);
}

void postAudioTest() {
  JsonDocument b;
  if (!readJson(b)) return;
  JsonVariantConst vol = b["volume"];
  if (!vol.is<long>() || vol.as<long>() < 0 || vol.as<long>() > 100) {
    return fail(422, err::VOLUME_INVALID, "Volume must be 0–100.", "volume");
  }
  const char* source = b["source"] | "";
  PlaybackRequest req;
  req.source = PlaybackSource::ManualUi;
  req.volume = vol.as<uint8_t>();
  if (strcmp(source, "file") == 0) {
    if (!requireSd()) return;
    const String file = b["file"] | "";
    const AudioEntry* e = AudioLibrary::find(file);
    if (!e) return fail(404, err::PROGRAM_AUDIO_MISSING, String("Audio file '") + file + "' not found.", "file");
    if (!e->valid()) return fail(422, wavStatusCode(e->status), e->errorMessage, "file");
    req.path = e->path();
  } else if (strcmp(source, "tone") == 0) {
    req.tone = true;
  } else {
    return fail(422, err::PLAYBACK_START_FAILED, "source must be 'file' or 'tone'.", "source");
  }
  if (ActiveProgram::program()) {
    req.programId = ActiveProgram::id();
    req.revision = ActiveProgram::revision().c_str();
  }
  const char* code;
  if (!PlaybackService::start(req, code)) return fail(500, code, "Playback could not start.");
  JsonDocument doc;
  json::playback(okData(doc), PlaybackService::snapshot());
  send(200, doc);
}

void postAudioStop() {
  PlaybackService::stop();
  JsonDocument doc;
  okData(doc)["state"] = "idle";
  send(200, doc);
}

void audioItem(HTTPMethod m, const String& name) {
  if (!requireSd()) return;
  const AudioEntry* e = AudioLibrary::find(name);
  if (!e) return fail(404, err::PROGRAM_AUDIO_MISSING, String("Audio file '") + name + "' not found.");
  if (m == HTTP_GET) {
    JsonDocument doc;
    audioEntryJson(okData(doc), *e);
    return send(200, doc);
  }
  JsonDocument probe;
  audioEntryJson(probe.to<JsonObject>(), *e);
  if (probe["in_active_program"].as<bool>()) {
    JsonDocument doc;
    JsonObject details = errorDoc(doc, err::AUDIO_IN_USE,
                                  String("'") + name + "' is used by the active program and cannot be deleted.");
    details["referenced_by"] = probe["referenced_by"];
    return send(409, doc);
  }
  if (!AudioLibrary::remove(name)) return fail(500, err::SD_WRITE_FAILED, "The file could not be deleted.");
  DeviceState::refreshSd();
  JsonDocument doc;
  okData(doc)["deleted"] = name;
  send(200, doc);
}

// ---- routes: programs -----------------------------------------------------------

void getPrograms() {
  JsonDocument doc;
  JsonObject data = okData(doc);
  const String& active = DeviceConfig::get().activeProgramId;
  if (active.length()) data["active_id"] = active;
  else data["active_id"] = nullptr;
  JsonObject lim = data["limits"].to<JsonObject>();
  lim["MAX_PROGRAMS"] = limits::MAX_PROGRAMS;
  lim["MAX_EVENTS"] = limits::MAX_EVENTS;
  lim["MAX_NAME"] = limits::MAX_NAME;
  lim["MAX_DESCRIPTION"] = limits::MAX_DESCRIPTION;
  lim["MAX_AUDIO_NAME"] = limits::MAX_AUDIO_NAME;
  lim["SCHEDULE_GUARD_MS"] = limits::SCHEDULE_GUARD_MS;
  lim["MAX_PROGRAM_JSON_BYTES"] = limits::MAX_PROGRAM_JSON_BYTES;
  JsonArray arr = data["programs"].to<JsonArray>();
  for (const ProgramSummary& s : ProgramStore::list()) summaryJson(arr.add<JsonObject>(), s);
  send(200, doc);
}

// Validates, stamps modified_utc and saves; sends the error response itself.
bool validateAndSave(Program& p) {
  const String& text = body();
  ValidationErrors errs = ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog(), &p);
  if (!errs.empty()) {
    programInvalid(errs);
    return false;
  }
  p.modifiedUtc = nowIso().c_str();
  const char* code;
  if (!ProgramStore::save(p, code)) {
    fail(strcmp(code, err::SD_FULL) == 0 ? 507 : 500, code, "The program could not be saved to the SD card.");
    return false;
  }
  return true;
}

void savedProgramJson(JsonObject data, const Program& p) {
  JsonObject prog = data["program"].to<JsonObject>();
  ProgramJson::toJson(p, prog);
  const ProgramSummary s = ProgramStore::summarize(p.id.c_str(), prog, {}, &p);
  summaryJson(data["summary"].to<JsonObject>(), s);
}

void postProgram() {
  if (!requireSd()) return;
  JsonDocument b;
  if (!readJson(b, err::PROGRAM_JSON_INVALID, "Body is not valid JSON.")) return;
  const String id = b["id"] | "";
  if (id.length() && ProgramStore::exists(id)) {
    return fail(409, err::PROGRAM_ID_EXISTS, String("A program with ID '") + id + "' already exists.", "id");
  }
  if (ProgramStore::count() >= limits::MAX_PROGRAMS) {
    return fail(422, err::PROGRAM_LIMIT_EXCEEDED, String("At most ") + limits::MAX_PROGRAMS + " programs can be stored.");
  }
  Program p;
  if (!validateAndSave(p)) return;
  JsonDocument doc;
  savedProgramJson(okData(doc), p);
  send(201, doc);
}

void programNotFound(const String& id) { fail(404, err::PROGRAM_NOT_FOUND, String("Program '") + id + "' not found."); }

// Draft from the body, else the stored file. Sends 404 itself.
bool draftOrStored(const String& id, String& text) {
  if (!body().isEmpty()) {
    JsonDocument probe;
    if (deserializeJson(probe, body())) {
      fail(400, err::JSON_INVALID, "Request body is not valid JSON.");
      return false;
    }
    text = body();
    return true;
  }
  if (!ProgramStore::readText(id, text)) {
    programNotFound(id);
    return false;
  }
  return true;
}

void programItem(HTTPMethod m, const String& id, const String& action) {
  if (!requireSd()) return;
  const bool stored = ProgramStore::exists(id);

  if (action == "validate" && m == HTTP_POST) {
    String text;
    if (!draftOrStored(id, text)) return;
    Program p;
    const ValidationErrors errs =
        ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog(), &p);
    JsonDocument doc;
    JsonObject data = okData(doc);
    data["valid"] = errs.empty();
    if (errs.empty()) {
      data["revision"] = ProgramJson::revision(p).c_str();
    } else {
      JsonDocument raw;
      if (!deserializeJson(raw, text) && raw.is<JsonObject>()) data["revision"] = ProgramJson::rawRevision(raw.as<JsonVariantConst>()).c_str();
      else data["revision"] = nullptr;
    }
    errorsJson(data["errors"].to<JsonArray>(), errs);
    return send(200, doc);
  }

  if (action == "preview" && (m == HTTP_GET || m == HTTP_POST)) {
    String text;
    if (m == HTTP_GET && !stored) return programNotFound(id);
    if (!draftOrStored(id, text)) return;
    if (!TimeService::isValid()) {
      return fail(409, err::TIME_NOT_SET, "Device time is not set; preview would be meaningless.");
    }
    Program p;
    const ValidationErrors errs =
        ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog(), &p);
    if (!errs.empty()) return programInvalid(errs);
    long count = server->hasArg("count") ? server->arg("count").toInt() : 100;
    count = std::max(1L, std::min(static_cast<long>(limits::MAX_PREVIEW_COUNT), count));
    const std::string rev = ProgramJson::revision(p);
    const int64_t now = TimeService::nowUtc();
    Scheduler s(p, rev, TimeService::utcOffsetMinutes());
    JsonDocument doc;
    JsonObject data = okData(doc);
    data["revision"] = rev.c_str();
    data["from_utc"] = TimeService::isoUtc(now);
    data["utc_offset_minutes"] = TimeService::utcOffsetMinutes();
    JsonArray events = data["events"].to<JsonArray>();
    for (const ScheduledEvent& e : s.preview(now, count)) json::scheduledEvent(events.add<JsonObject>(), p, e);
    return send(200, doc);
  }

  if (action == "activate" && m == HTTP_POST) {
    if (!stored) return programNotFound(id);
    const ValidationErrors errs = ActiveProgram::activate(id);
    if (!errs.empty()) {
      if (errs.front().code == err::PROGRAM_NOT_FOUND) return programNotFound(id);
      return programInvalid(errs);
    }
    return getStatus();
  }

  if (action == "deactivate" && m == HTTP_POST) {
    if (!stored) return programNotFound(id);
    if (id == DeviceConfig::get().activeProgramId && !ActiveProgram::deactivate()) {
      return fail(500, err::INTERNAL, "Could not clear the active Program ID.");
    }
    return getStatus();
  }

  if (action.length()) return fail(404, err::NOT_FOUND, String("No route for ") + server->uri());

  if (m == HTTP_GET) {
    if (!stored) return programNotFound(id);
    String text;
    ProgramStore::readText(id, text);
    Program p;
    const ValidationErrors errs =
        ProgramValidator::validateText(text.c_str(), text.length(), AudioLibrary::catalog(), &p);
    JsonDocument storedDoc;
    deserializeJson(storedDoc, text);
    JsonDocument doc;
    JsonObject data = okData(doc);
    data["program"] = storedDoc.as<JsonVariantConst>();
    const ProgramSummary* s = ProgramStore::summary(id);
    if (s) summaryJson(data["summary"].to<JsonObject>(), *s);
    errorsJson(data["errors"].to<JsonArray>(), errs);
    return send(200, doc);
  }

  if (m == HTTP_PUT) {
    if (!stored) return programNotFound(id);
    JsonDocument b;
    if (!readJson(b, err::PROGRAM_JSON_INVALID, "Body is not valid JSON.")) return;
    if (id != (b["id"] | "")) {
      return fail(422, err::PROGRAM_JSON_INVALID, "Program ID cannot be changed. Duplicate the program instead.", "id");
    }
    Program p;
    if (!validateAndSave(p)) return;
    const bool applied = id == DeviceConfig::get().activeProgramId;
    if (applied) ActiveProgram::reload();
    JsonDocument doc;
    JsonObject data = okData(doc);
    savedProgramJson(data, p);
    data["applied"] = applied;
    return send(200, doc);
  }

  if (m == HTTP_DELETE) {
    if (!stored) return programNotFound(id);
    if (id == DeviceConfig::get().activeProgramId) {
      return fail(409, err::PROGRAM_ACTIVE, "The active program cannot be deleted. Activate another program first.");
    }
    if (!ProgramStore::remove(id)) return fail(500, err::SD_WRITE_FAILED, "The program could not be deleted.");
    JsonDocument doc;
    okData(doc)["deleted"] = id;
    return send(200, doc);
  }

  fail(404, err::NOT_FOUND, String("No route for ") + server->uri());
}

// ---- routes: environment / logs / firmware ----------------------------------------

void putEnvironment() {
  JsonDocument b;
  if (!readJson(b)) return;
  JsonVariantConst en = b["enabled"];
  if (!en.isNull() && !en.is<bool>()) return fail(422, err::ENV_CONFIG_INVALID, "enabled must be true or false.", "enabled");
  JsonVariantConst iv = b["interval_minutes"];
  if (!iv.isNull() && !(iv.is<long>() && EnvSchedule::isValidInterval(iv.as<long>()))) {
    return fail(422, err::ENV_INTERVAL_INVALID, "Interval must be a whole number of minutes from 1 to 1440.",
                "interval_minutes");
  }
  const DeviceSettings& cfg = DeviceConfig::get();
  DeviceConfig::setEnvironment(en.isNull() ? cfg.envEnabled : en.as<bool>(),
                               iv.isNull() ? cfg.envIntervalMinutes : iv.as<uint16_t>());
  JsonDocument doc;
  environmentJson(okData(doc));
  send(200, doc);
}

void getLogs() {
  if (!requireSd()) return;
  JsonDocument doc;
  JsonObject data = okData(doc);
  logInfoJson(data["playback"].to<JsonObject>(), app::PLAYBACK_LOG);
  logInfoJson(data["environment"].to<JsonObject>(), app::ENVIRONMENT_LOG);
  send(200, doc);
}

void getLogCsv(const String& kind) {
  if (!requireSd()) return;
  const bool playback = kind == "playback";
  const char* path = playback ? app::PLAYBACK_LOG : app::ENVIRONMENT_LOG;
  const char* header = playback ? Csv::PLAYBACK_HEADER : Csv::ENVIRONMENT_HEADER;
  const long tail = server->hasArg("tail") ? server->arg("tail").toInt() : 0;
  server->sendHeader("Cache-Control", "no-store");
  if (tail <= 0) {
    server->sendHeader("Content-Disposition",
                       String("attachment; filename=\"") + DeviceConfig::deviceId() + "_" + kind + ".csv\"");
    File f = CsvFile::openRead(path);
    if (f && f.size()) {
      server->streamFile(f, CSV_TYPE);
      f.close();
      return;
    }
    if (f) f.close();
  }
  String out;
  CsvFile::tail(path, header, tail > 0 ? static_cast<uint32_t>(tail) : 0, out);
  server->send(200, CSV_TYPE, out);
}

void getFirmware() {
  const FirmwareInfo i = FirmwareService::info();
  JsonDocument doc;
  JsonObject data = okData(doc);
  data["version"] = i.version;
  data["build_utc"] = i.buildUtc;
  data["running_partition"] = i.runningPartition;
  data["next_partition"] = i.nextPartition;
  data["slot_size_bytes"] = i.slotSizeBytes;
  send(200, doc);
}

struct FirmwareUpload {
  bool started = false;
  bool finished = false;
  bool ok = false;
};
FirmwareUpload fwUp;

void onFirmwareUpload(HTTPUpload& up) {
  if (up.status == UPLOAD_FILE_START) {
    if (fwUp.started) return;
    fwUp = FirmwareUpload{};
    fwUp.started = true;
    FirmwareService::begin(server->clientContentLength());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (fwUp.started && !fwUp.finished) FirmwareService::write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!fwUp.started || fwUp.finished) return;
    fwUp.finished = true;
    fwUp.ok = FirmwareService::finish();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    FirmwareService::abort();
  }
}

void postFirmware() {
  FirmwareUpload up = fwUp;
  fwUp = FirmwareUpload{};
  if (!up.started) return fail(400, err::OTA_INVALID, "Expected a multipart upload with a firmware file.");
  if (!up.finished || !up.ok) {
    return fail(FirmwareService::httpStatus() == 200 ? 400 : FirmwareService::httpStatus(),
                FirmwareService::errorCode()[0] ? FirmwareService::errorCode() : err::OTA_INVALID,
                FirmwareService::errorMessage().length() ? FirmwareService::errorMessage() : String("Upload was incomplete."),
                FirmwareService::httpStatus() == 422 ? "file" : nullptr);
  }
  JsonDocument doc;
  JsonObject data = okData(doc);
  data["bytes"] = FirmwareService::written();
  data["partition"] = FirmwareService::targetPartition();
  if (FirmwareService::imageVersion().length()) data["version"] = FirmwareService::imageVersion();
  else data["version"] = nullptr;
  data["rebooting"] = true;
  send(200, doc);
  FirmwareService::scheduleRestart(1500);
}

// ---- dispatch ---------------------------------------------------------------------

void dispatch(HTTPMethod m, const String& uri) {
  const String route = uri.length() > strlen(PREFIX) ? uri.substring(strlen(PREFIX)) : String("/");
  if (!(m == HTTP_GET && route == "/status")) WifiService::touch();

  if (server->clientContentLength() > static_cast<int>(app::MAX_BODY_BYTES) &&
      !(m == HTTP_POST && (route == "/audio" || route == "/firmware"))) {
    return fail(413, err::UPLOAD_TOO_LARGE, "Request body too large.");
  }
  requestBody = server->arg("plain");

  if (m == HTTP_GET && route == "/status") return getStatus();
  if (m == HTTP_POST && route == "/session/extend") {
    WifiService::touch();
    JsonDocument doc;
    okData(doc)["idle_remaining_s"] = WifiService::idleTimeoutS();
    return send(200, doc);
  }

  if (route == "/device") {
    if (m == HTTP_GET) {
      JsonDocument doc;
      deviceJson(okData(doc));
      return send(200, doc);
    }
    if (m == HTTP_PUT) return putDevice();
  }

  if (route == "/time") {
    if (m == HTTP_GET) {
      JsonDocument doc;
      json::timeInfo(okData(doc));
      return send(200, doc);
    }
    if (m == HTTP_PUT) return putTime();
  }

  if (route == "/audio") {
    if (m == HTTP_GET) {
      if (!requireSd()) return;
      return getAudioList();
    }
    if (m == HTTP_POST) return postAudio();
  }
  if (m == HTTP_POST && route == "/audio/test") return postAudioTest();
  if (m == HTTP_POST && route == "/audio/stop") return postAudioStop();
  if (route.startsWith("/audio/") && route.indexOf('/', 7) < 0 && (m == HTTP_GET || m == HTTP_DELETE)) {
    return audioItem(m, urlDecode(route.substring(7)));
  }

  if (route == "/programs") {
    if (m == HTTP_GET) {
      if (!requireSd()) return;
      return getPrograms();
    }
    if (m == HTTP_POST) return postProgram();
  }
  if (route.startsWith("/programs/")) {
    const String rest = route.substring(10);
    const int slash = rest.indexOf('/');
    const String id = urlDecode(slash < 0 ? rest : rest.substring(0, slash));
    const String action = slash < 0 ? String() : rest.substring(slash + 1);
    if (id.length() && action.indexOf('/') < 0 &&
        (action.isEmpty() || action == "validate" || action == "preview" || action == "activate" ||
         action == "deactivate")) {
      return programItem(m, id, action);
    }
  }

  if (route == "/environment") {
    if (m == HTTP_GET) {
      JsonDocument doc;
      environmentJson(okData(doc));
      return send(200, doc);
    }
    if (m == HTTP_PUT) return putEnvironment();
  }

  if (m == HTTP_GET && route == "/logs") return getLogs();
  if (m == HTTP_GET && (route == "/logs/playback" || route == "/logs/environment")) {
    return getLogCsv(route.substring(6));
  }

  if (route == "/firmware") {
    if (m == HTTP_GET) return getFirmware();
    if (m == HTTP_POST) return postFirmware();
  }

  fail(404, err::NOT_FOUND, String("No route for ") + methodName(m) + " " + uri);
}

void runBackgroundHook() {
  if (!backgroundHook || millis() - lastHookMs < 100) return;
  lastHookMs = millis();
  backgroundHook();
}

class ApiHandler : public RequestHandler {
 public:
  // Called once per request before the body is parsed.
  bool canHandle(WebServer&, HTTPMethod, const String& uri) override {
    const bool mine = uri == PREFIX || uri.startsWith(String(PREFIX) + "/");
    if (mine) {
      if (audioUp.started && !audioUp.finished) AudioLibrary::uploadAbort();
      audioUp = AudioUpload{};
      fwUp = FirmwareUpload{};
    }
    return mine;
  }
  bool canUpload(WebServer& s, const String& uri) override {
    return s.method() == HTTP_POST && (uri == String(PREFIX) + "/audio" || uri == String(PREFIX) + "/firmware");
  }
  bool handle(WebServer&, HTTPMethod m, const String& uri) override {
    dispatch(m, uri);
    requestBody = String();
    return true;
  }
  void upload(WebServer&, const String& uri, HTTPUpload& up) override {
    if (uri.endsWith("/audio")) onAudioUpload(up);
    else onFirmwareUpload(up);
    WifiService::touch();
    runBackgroundHook();
  }
};

void notFound() {
  if (server->uri().startsWith(PREFIX)) {
    return fail(404, err::NOT_FOUND, String("No route for ") + server->uri());
  }
  if (WebUi::serve(*server)) return;
  server->send(404, "text/plain; charset=utf-8", String("No route for ") + server->uri() + "\n");
}

}  // namespace

void begin(uint16_t port) {
  if (running) return;
  if (!server) {
    server = new WebServer(port);
    server->addHandler(new ApiHandler());
    server->onNotFound(notFound);
    static const char* headers[] = {"If-None-Match"};
    server->collectHeaders(headers, 1);
  }
  server->begin();
  running = true;
}

void end() {
  if (!running) return;
  if (audioUp.started && !audioUp.finished) AudioLibrary::uploadAbort();
  audioUp = AudioUpload{};
  if (fwUp.started && !fwUp.finished) FirmwareService::abort();
  fwUp = FirmwareUpload{};
  server->stop();
  running = false;
}

void handle() {
  if (running) server->handleClient();
}

void setBackgroundHook(void (*hook)()) { backgroundHook = hook; }

}  // namespace ApiServer
}  // namespace acorn
