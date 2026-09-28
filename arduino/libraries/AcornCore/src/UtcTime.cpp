#include "UtcTime.h"

#include <stdio.h>

namespace acorn {
namespace UtcTime {

namespace {

// Howard Hinnant's days_from_civil / civil_from_days.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

}  // namespace

bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : days[month - 1];
}

bool isValid(const UtcDateTime& t) {
  return t.year >= 2000 && t.year <= 2099 && t.month >= 1 && t.month <= 12 && t.day >= 1 &&
         t.day <= daysInMonth(t.year, t.month) && t.hour < 24 && t.minute < 60 && t.second < 60;
}

int64_t toEpoch(const UtcDateTime& t) {
  return daysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

UtcDateTime fromEpoch(int64_t epoch) {
  int64_t days = epoch / 86400;
  int64_t rem = epoch % 86400;
  if (rem < 0) {
    rem += 86400;
    days--;
  }
  int64_t y;
  unsigned m, d;
  civilFromDays(days, y, m, d);
  UtcDateTime t;
  t.year = static_cast<uint16_t>(y);
  t.month = m;
  t.day = d;
  t.hour = rem / 3600;
  t.minute = (rem % 3600) / 60;
  t.second = rem % 60;
  return t;
}

uint8_t weekday(const UtcDateTime& t) {
  // 1970-01-01 was a Thursday (ISO 4).
  const int64_t days = daysFromCivil(t.year, t.month, t.day);
  int64_t w = (days + 3) % 7;
  if (w < 0) w += 7;
  return static_cast<uint8_t>(w + 1);
}

void formatIso(const UtcDateTime& t, char* out, size_t len) {
  snprintf(out, len, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.year, t.month, t.day, t.hour, t.minute,
           t.second);
}

bool parseIso(const char* text, UtcDateTime& out) {
  unsigned y, mo, d, h, mi, s;
  char tail = 0;
  const int n = sscanf(text, "%4u-%2u-%2uT%2u:%2u:%2u%c", &y, &mo, &d, &h, &mi, &s, &tail);
  if (n < 6 || (n == 7 && tail != 'Z')) return false;
  UtcDateTime t;
  t.year = y;
  t.month = mo;
  t.day = d;
  t.hour = h;
  t.minute = mi;
  t.second = s;
  if (!isValid(t)) return false;
  out = t;
  return true;
}

void formatEpochIso(int64_t epoch, char* out, size_t len) { formatIso(fromEpoch(epoch), out, len); }

void formatEpochLocalIso(int64_t local, char* out, size_t len) {
  const UtcDateTime t = fromEpoch(local);
  snprintf(out, len, "%04u-%02u-%02uT%02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute,
           t.second);
}

void formatEpochDate(int64_t epoch, char* out, size_t len) {
  const UtcDateTime t = fromEpoch(epoch);
  snprintf(out, len, "%04u-%02u-%02u", t.year, t.month, t.day);
}

}  // namespace UtcTime
}  // namespace acorn
