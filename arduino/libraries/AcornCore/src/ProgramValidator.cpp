#include "ProgramValidator.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "AcornLimits.h"

namespace acorn {
namespace ProgramValidator {

namespace {

constexpr int64_t DAY_MS = static_cast<int64_t>(SECONDS_PER_DAY) * 1000;

struct Span {
  size_t index;
  int64_t startMs;
  int64_t endMs;  // > start; may exceed one day for cross-midnight ranges
  std::string id;
};

struct PointAt {
  size_t index;
  int64_t atMs;
  uint32_t durationMs;
  std::string id;
};

std::string fieldPath(size_t i, const char* key) {
  char buf[64];
  if (key && *key) snprintf(buf, sizeof(buf), "events[%u].%s", static_cast<unsigned>(i), key);
  else snprintf(buf, sizeof(buf), "events[%u]", static_cast<unsigned>(i));
  return buf;
}

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof(buf), f, ap);
  va_end(ap);
  return buf;
}

bool isInteger(JsonVariantConst v) { return v.is<long long>() && !v.is<bool>(); }

bool intersects(int64_t a0, int64_t a1, int64_t b0, int64_t b1) { return a0 < b1 && b0 < a1; }

}  // namespace

bool isValidId(const char* id) {
  if (!id) return false;
  const size_t n = strlen(id);
  if (n < 1 || n > 32) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = id[i];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!(alnum || (i > 0 && c == '-'))) return false;
  }
  return true;
}

ValidationErrors validate(JsonVariantConst p, const AudioCatalog& audio, Program* out) {
  ValidationErrors errors;
  const auto err = [&](const char* code, std::string message, std::string field) {
    errors.push_back({code, std::move(message), std::move(field)});
  };

  if (!p.is<JsonObjectConst>()) {
    err(err::PROGRAM_JSON_INVALID, "Program must be a JSON object.", "");
    return errors;
  }

  Program prog;
  JsonVariantConst schema = p["schema_version"];
  if (schema.isNull()) {
    prog.schemaVersion = 1;
  } else if (!isInteger(schema) || schema.as<long long>() != 1) {
    std::string shown;
    serializeJson(schema, shown);
    err(err::PROGRAM_SCHEMA_UNSUPPORTED, fmt("schema_version %s is not supported (expected 1).", shown.c_str()),
        "schema_version");
  }

  const char* id = p["id"].is<const char*>() ? p["id"].as<const char*>() : nullptr;
  if (!isValidId(id)) {
    err(err::PROGRAM_JSON_INVALID, "ID must be 1–32 characters: lowercase letters, digits and hyphens.", "id");
  } else {
    prog.id = id;
  }

  const char* name = p["name"].is<const char*>() ? p["name"].as<const char*>() : nullptr;
  bool nameBlank = true;
  if (name) {
    for (const char* c = name; *c; c++) {
      if (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\r') nameBlank = false;
    }
  }
  if (!name || nameBlank) err(err::PROGRAM_JSON_INVALID, "Name is required.", "name");
  else if (strlen(name) > limits::MAX_NAME) {
    err(err::PROGRAM_LIMIT_EXCEEDED, fmt("Name exceeds %u characters.", limits::MAX_NAME), "name");
  } else {
    prog.name = name;
  }

  JsonVariantConst desc = p["description"];
  if (!desc.isNull() && !desc.is<const char*>()) {
    err(err::PROGRAM_JSON_INVALID, "Description must be text.", "description");
  } else if (desc.is<const char*>()) {
    if (strlen(desc.as<const char*>()) > limits::MAX_DESCRIPTION) {
      err(err::PROGRAM_LIMIT_EXCEEDED, fmt("Description exceeds %u characters.", limits::MAX_DESCRIPTION),
          "description");
    } else {
      prog.description = desc.as<const char*>();
    }
  }
  if (p["modified_utc"].is<const char*>()) prog.modifiedUtc = p["modified_utc"].as<const char*>();

  JsonVariantConst events = p["events"];
  if (!events.isNull() && !events.is<JsonArrayConst>()) {
    err(err::PROGRAM_JSON_INVALID, "events must be an array.", "events");
    return errors;
  }
  JsonArrayConst evs = events.as<JsonArrayConst>();
  if (evs.size() > limits::MAX_EVENTS) {
    err(err::PROGRAM_LIMIT_EXCEEDED, fmt("A program may contain at most %u events.", limits::MAX_EVENTS), "events");
  }

  std::vector<std::string> seen;
  std::vector<Span> ranges;
  std::vector<PointAt> points;

  size_t i = 0;
  for (JsonVariantConst ev : evs) {
    const size_t idx = i++;
    if (!ev.is<JsonObjectConst>()) {
      err(err::PROGRAM_JSON_INVALID, "Event must be an object.", fieldPath(idx, ""));
      continue;
    }
    Event e;
    const char* eid = ev["event_id"].is<const char*>() ? ev["event_id"].as<const char*>() : nullptr;
    if (!isValidId(eid)) {
      err(err::PROGRAM_JSON_INVALID, "Event ID must be 1–32 characters: lowercase letters, digits and hyphens.",
          fieldPath(idx, "event_id"));
    } else if (std::find(seen.begin(), seen.end(), eid) != seen.end()) {
      err(err::PROGRAM_JSON_INVALID, fmt("Event ID '%s' is used more than once.", eid), fieldPath(idx, "event_id"));
    } else {
      seen.push_back(eid);
      e.eventId = eid;
    }

    JsonVariantConst vol = ev["volume"];
    if (!isInteger(vol) || vol.as<long long>() < 0 || vol.as<long long>() > 100) {
      err(err::PROGRAM_JSON_INVALID, "Volume must be an integer from 0 to 100.", fieldPath(idx, "volume"));
    } else {
      e.volume = vol.as<int32_t>();
    }

    uint32_t durationMs = 0;
    const char* apath = ev["audio"].is<const char*>() ? ev["audio"].as<const char*>() : "";
    e.audio = apath;
    AudioMeta meta;
    if (!audio.find(e.audio, meta)) {
      err(err::PROGRAM_AUDIO_MISSING, fmt("Audio file '%s' was not found on the SD card.", apath),
          fieldPath(idx, "audio"));
    } else if (!meta.valid) {
      err(err::PROGRAM_AUDIO_INVALID,
          fmt("Audio file '%s' is not a supported WAV (%s).", apath, meta.errorMessage.c_str()),
          fieldPath(idx, "audio"));
    } else {
      durationMs = meta.durationMs;
    }

    const char* type = ev["type"].is<const char*>() ? ev["type"].as<const char*>() : "";
    if (strcmp(type, "point") == 0) {
      e.type = EventType::Point;
      const char* t = ev["time"].is<const char*>() ? ev["time"].as<const char*>() : nullptr;
      e.timeS = parseTimeOfDay(t);
      if (e.timeS < 0) err(err::PROGRAM_JSON_INVALID, "Time must be HH:MM or HH:MM:SS.", fieldPath(idx, "time"));
      else points.push_back({idx, e.timeS * 1000LL, durationMs, e.eventId});
    } else if (strcmp(type, "range") == 0) {
      e.type = EventType::Range;
      e.startS = parseTimeOfDay(ev["start"].is<const char*>() ? ev["start"].as<const char*>() : nullptr);
      e.endS = parseTimeOfDay(ev["end"].is<const char*>() ? ev["end"].as<const char*>() : nullptr);
      if (e.startS < 0) err(err::PROGRAM_RANGE_INVALID, "Start must be HH:MM or HH:MM:SS.", fieldPath(idx, "start"));
      if (e.endS < 0) err(err::PROGRAM_RANGE_INVALID, "End must be HH:MM or HH:MM:SS.", fieldPath(idx, "end"));
      if (e.startS >= 0 && e.endS >= 0 && e.startS == e.endS) {
        err(err::PROGRAM_RANGE_INVALID, "Start and end cannot be equal.", fieldPath(idx, "end"));
      }
      JsonVariantConst r = ev["recurrence"];
      const char* rtype = r["type"].is<const char*>() ? r["type"].as<const char*>() : "";
      if (strcmp(rtype, "fixed") == 0) {
        e.recurrence.type = RecurrenceType::Fixed;
        JsonVariantConst iv = r["interval_seconds"];
        if (!isInteger(iv) || iv.as<long long>() < 1) {
          err(err::PROGRAM_JSON_INVALID, "Interval must be a positive whole number of seconds.",
              fieldPath(idx, "recurrence.interval_seconds"));
        } else {
          e.recurrence.intervalS = iv.as<long long>();
          if (durationMs && e.recurrence.intervalS * 1000 <= durationMs + limits::SCHEDULE_GUARD_MS) {
            err(err::PROGRAM_INTERVAL_TOO_SHORT,
                fmt("Interval (%lld s) must exceed the audio duration (%.1f s) plus a %u s guard.",
                    static_cast<long long>(e.recurrence.intervalS), durationMs / 1000.0,
                    static_cast<unsigned>(limits::SCHEDULE_GUARD_MS / 1000)),
                fieldPath(idx, "recurrence.interval_seconds"));
          }
        }
      } else if (strcmp(rtype, "random") == 0) {
        e.recurrence.type = RecurrenceType::Random;
        JsonVariantConst mn = r["min_interval_seconds"];
        JsonVariantConst mx = r["max_interval_seconds"];
        if (!isInteger(mn) || !isInteger(mx) || mn.as<long long>() < 1 || mx.as<long long>() < mn.as<long long>()) {
          err(err::PROGRAM_RANDOM_RANGE_INVALID,
              "Random interval requires whole-second minimum ≥ 1 and maximum ≥ minimum.",
              fieldPath(idx, "recurrence.min_interval_seconds"));
        } else {
          e.recurrence.minS = mn.as<long long>();
          e.recurrence.maxS = mx.as<long long>();
          if (durationMs && e.recurrence.minS * 1000 <= durationMs + limits::SCHEDULE_GUARD_MS) {
            err(err::PROGRAM_INTERVAL_TOO_SHORT,
                fmt("Minimum interval (%lld s) must exceed the audio duration (%.1f s) plus a %u s guard.",
                    static_cast<long long>(e.recurrence.minS), durationMs / 1000.0,
                    static_cast<unsigned>(limits::SCHEDULE_GUARD_MS / 1000)),
                fieldPath(idx, "recurrence.min_interval_seconds"));
          }
        }
      } else {
        err(err::PROGRAM_JSON_INVALID, "Recurrence type must be 'fixed' or 'random'.", fieldPath(idx, "recurrence.type"));
      }
      if (e.startS >= 0 && e.endS >= 0 && e.startS != e.endS) {
        const int64_t s = e.startS * 1000LL;
        int64_t en = e.endS * 1000LL;
        if (en <= s) en += DAY_MS;
        ranges.push_back({idx, s, en, e.eventId});
      }
    } else {
      err(err::PROGRAM_JSON_INVALID, "Event type must be 'point' or 'range'.", fieldPath(idx, "type"));
    }
    prog.events.push_back(std::move(e));
  }

  for (size_t a = 0; a < ranges.size(); a++) {
    for (size_t b = a + 1; b < ranges.size(); b++) {
      const Span& A = ranges[a];
      const Span& B = ranges[b];
      bool hit = false;
      for (int64_t k : {-DAY_MS, 0LL, DAY_MS}) hit = hit || intersects(A.startMs, A.endMs, B.startMs + k, B.endMs + k);
      if (hit) {
        err(err::PROGRAM_RANGE_OVERLAP, fmt("Range '%s' overlaps range '%s'.", B.id.c_str(), A.id.c_str()),
            fieldPath(B.index, ""));
      }
    }
  }
  for (const PointAt& P : points) {
    for (const Span& R : ranges) {
      bool inside = false;
      for (int64_t k : {0LL, DAY_MS}) inside = inside || (P.atMs + k >= R.startMs && P.atMs + k < R.endMs);
      if (inside) {
        err(err::PROGRAM_EVENT_COLLISION, fmt("Point '%s' falls inside range '%s'.", P.id.c_str(), R.id.c_str()),
            fieldPath(P.index, "time"));
      }
    }
  }
  for (size_t a = 0; a < points.size(); a++) {
    for (size_t b = a + 1; b < points.size(); b++) {
      const PointAt& A = points[a];
      const PointAt& B = points[b];
      const int64_t ab = ((B.atMs - A.atMs) % DAY_MS + DAY_MS) % DAY_MS;
      const int64_t ba = ((A.atMs - B.atMs) % DAY_MS + DAY_MS) % DAY_MS;
      if (ab < A.durationMs + limits::SCHEDULE_GUARD_MS || ba < B.durationMs + limits::SCHEDULE_GUARD_MS) {
        err(err::PROGRAM_EVENT_COLLISION,
            fmt("Point '%s' would overlap playback of point '%s'.", B.id.c_str(), A.id.c_str()),
            fieldPath(B.index, "time"));
      }
    }
  }

  if (out) *out = std::move(prog);
  return errors;
}

ValidationErrors validateText(const char* text, size_t len, const AudioCatalog& audio, Program* out) {
  if (len > limits::MAX_PROGRAM_JSON_BYTES) {
    return {{err::PROGRAM_LIMIT_EXCEEDED,
             fmt("Program JSON exceeds %u bytes.", static_cast<unsigned>(limits::MAX_PROGRAM_JSON_BYTES)), ""}};
  }
  JsonDocument doc;
  if (deserializeJson(doc, text, len)) return {{err::PROGRAM_JSON_INVALID, "Body is not valid JSON.", ""}};
  return validate(doc.as<JsonVariantConst>(), audio, out);
}

}  // namespace ProgramValidator
}  // namespace acorn
