#pragma once

#include <stddef.h>
#include <stdint.h>

namespace acorn {

// Calendar time in UTC. The device never applies time zones or DST.
struct UtcDateTime {
  uint16_t year = 2000;  // 2000..2099 (RTC range)
  uint8_t month = 1;     // 1..12
  uint8_t day = 1;       // 1..31
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

namespace UtcTime {

bool isLeapYear(uint16_t year);
uint8_t daysInMonth(uint16_t year, uint8_t month);
bool isValid(const UtcDateTime& t);

// Seconds since 1970-01-01T00:00:00Z.
int64_t toEpoch(const UtcDateTime& t);
UtcDateTime fromEpoch(int64_t epoch);

// ISO weekday: 1 = Monday .. 7 = Sunday.
uint8_t weekday(const UtcDateTime& t);

// "2026-09-27T06:00:00Z" (buffer >= 21 bytes).
void formatIso(const UtcDateTime& t, char* out, size_t len);
// Accepts "YYYY-MM-DDTHH:MM:SS" with optional trailing "Z".
bool parseIso(const char* text, UtcDateTime& out);

// Epoch-second helpers. Local times are "local epoch" seconds (UTC epoch
// shifted by the fixed UTC offset), formatted without a zone suffix.
void formatEpochIso(int64_t epoch, char* out, size_t len);       // ...Z
void formatEpochLocalIso(int64_t local, char* out, size_t len);  // no Z
void formatEpochDate(int64_t epoch, char* out, size_t len);      // YYYY-MM-DD

}  // namespace UtcTime
}  // namespace acorn
