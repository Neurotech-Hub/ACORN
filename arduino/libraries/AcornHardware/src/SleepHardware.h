#pragma once

#include <Arduino.h>

namespace acorn {

enum class WakeSource : uint8_t {
  PowerOn,   // cold boot / reset, not a deep-sleep wake
  Hall,      // EXT0: MAG_OUT HIGH (door opened)
  Button1,   // EXT1: BTN_01 LOW
  Button2,   // EXT1: BTN_02 LOW
  RtcAlarm,  // EXT1: RTC_MFP LOW
  Timer,
  Unknown,
};

const char* wakeSourceName(WakeSource source);

struct SleepConfig {
  bool wakeOnHall = true;
  // If the door is already open at sleep time, wake when it closes (EXT0 at
  // the inverse level) instead of skipping the Hall source.
  bool hallWakeOnClose = false;
  bool wakeOnButtons = true;
  bool wakeOnRtcAlarm = true;
  uint64_t timerUs = 0;  // 0 = no timer wake
};

// What enter() actually armed. Level sources already asserted at sleep time
// are skipped (they would re-wake immediately) and reported here.
struct SleepArmResult {
  bool hall = false;
  bool hallClose = false;  // EXT0 armed for door close (door was open)
  bool button1 = false;
  bool button2 = false;
  bool rtcAlarm = false;
  bool timer = false;
  bool skippedHall = false;
  bool skippedButton1 = false;
  bool skippedButton2 = false;
  bool skippedRtcAlarm = false;
  bool anyArmed() const { return hall || hallClose || button1 || button2 || rtcAlarm || timer; }
};

// ESP32-S3 deep sleep with mixed-polarity wake: EXT0 for MAG_OUT (HIGH) and
// EXT1 ANY_LOW for BTN_01, BTN_02 and RTC_MFP. Pulls are external; internal
// RTC pulls are disabled. Peripherals must be put to sleep by the caller
// (LedHardware/AudioHardware/SdHardware::prepareForSleep, RTC flag cleared).
namespace SleepHardware {

WakeSource wakeSource();  // cause of the current boot
SleepArmResult arm(const SleepConfig& config);
// Arms wake sources, latches held GPIOs and enters deep sleep. Returns only
// if nothing could be armed (the result is returned so callers can log it).
SleepArmResult enter(const SleepConfig& config);

}  // namespace SleepHardware
}  // namespace acorn
