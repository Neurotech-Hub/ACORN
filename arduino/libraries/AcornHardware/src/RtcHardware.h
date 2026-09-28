#pragma once

#include <Arduino.h>

#include <UtcTime.h>

namespace acorn {

struct RtcStatus {
  bool present = false;     // ACKs on I2C
  bool oscillatorRunning = false;
  bool timeSet = false;     // battery-backed SRAM marker written by setTime()
  bool powerFailed = false; // main power was lost since last setTime/clear
  bool valid = false;       // present && running && timeSet && sane registers
};

// MCP7940N (0x6F) running in UTC, 24-hour mode, external 32.768 kHz crystal,
// BR2032 backup. Alarm 0 drives MFP (GPIO6) LOW for deep-sleep wake.
namespace RtcHardware {

bool begin();
RtcStatus status();

// Returns false (and leaves `out` untouched) if the RTC is not valid.
bool now(UtcDateTime& out);
bool nowEpoch(int64_t& out);

bool setTime(const UtcDateTime& t);
bool setEpoch(int64_t epoch);
bool clearPowerFail();

// Alarm 0: full match on month/date/weekday/hour/minute/second.
bool setAlarm(const UtcDateTime& t);
bool disableAlarm();
bool alarmFired();       // ALM0IF flag
bool clearAlarmFlag();   // releases MFP; required before sleep
bool isMfpAsserted();    // GPIO6 level

}  // namespace RtcHardware
}  // namespace acorn
