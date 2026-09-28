#pragma once

#include <Arduino.h>

// The device clock (init.md sections 3.2, 50, 64). The RTC is read once at
// boot and then extrapolated with the monotonic esp_timer, re-synced every
// few minutes. All logic uses UTC epoch seconds; local time only exists for
// display and schedule interpretation via the fixed UTC offset.

namespace acorn {
namespace TimeService {

void begin();
void update();  // periodic RTC re-sync; call from loop()

// True once the clock holds real time: the RTC was valid at boot or the
// time was set this session. Without it, nowUtc() counts from 2000-01-01.
bool isValid();
// RTC hardware present, running and holding a set time.
bool rtcValid();

int64_t nowUtc();
int32_t utcOffsetMinutes();
int64_t toLocal(int64_t utc);

String isoUtc(int64_t utc);
String isoLocal(int64_t utc);  // local wall-clock time of `utc`, no zone suffix

// Writes the RTC. The software clock follows even if the RTC write fails
// (reported as RTC_INVALID) so bench boards without an RTC stay usable.
bool setUtc(int64_t utc);
bool setUtcOffset(int32_t minutes);

// Incremented on every time or offset change; consumers recompute
// schedules and never replay events skipped by a change (section 50).
uint32_t generation();

}  // namespace TimeService
}  // namespace acorn
