#pragma once

#include <stdint.h>

#include <string>
#include <vector>

// In-memory Program model (init.md section 8-14). Times are seconds of the
// local day (0..86399); -1 marks a field that failed validation.

namespace acorn {

enum class EventType : uint8_t { Point, Range };
enum class RecurrenceType : uint8_t { Fixed, Random };

struct Recurrence {
  RecurrenceType type = RecurrenceType::Fixed;
  int64_t intervalS = 0;  // fixed
  int64_t minS = 0;       // random
  int64_t maxS = 0;       // random
};

struct Event {
  std::string eventId;
  EventType type = EventType::Point;
  int32_t timeS = -1;   // point
  int32_t startS = -1;  // range, inclusive
  int32_t endS = -1;    // range, exclusive; <= start means it crosses midnight
  std::string audio;
  int32_t volume = 0;
  Recurrence recurrence;
};

struct Program {
  int32_t schemaVersion = 1;
  std::string id;
  std::string name;
  std::string description;
  std::string modifiedUtc;
  std::vector<Event> events;
};

constexpr int32_t SECONDS_PER_DAY = 86400;

// "HH:MM" or "HH:MM:SS" (two-digit fields) -> seconds of day, or -1.
int32_t parseTimeOfDay(const char* text);
// seconds of day -> "HH:MM:SS" (buffer >= 9 bytes).
void formatTimeOfDay(int32_t seconds, char* out, size_t len);

}  // namespace acorn
