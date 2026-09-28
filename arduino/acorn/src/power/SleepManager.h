#pragma once

#include <Arduino.h>
#include <AcornCore.h>

// Deep sleep entry (init.md sections 41, 42): stop playback, put audio, SD,
// Wi-Fi and LEDs into their safe states, program the RTC alarm for the
// earliest required wake (WakePlanner) and enter deep sleep.

namespace acorn {
namespace SleepManager {

constexpr int64_t MIN_SLEEP_S = 3;  // closer wakes are handled awake
// The RTC alarm fires this much before the planned time so boot, SD mount
// and audio power-up are done by then; the runtime waits for the exact second.
constexpr int64_t WAKE_LEAD_S = 2;

WakePlanner::Wake nextWake(int64_t nowUtc);

// True if the next planned wake is too close to be worth sleeping.
bool wakeTooClose(int64_t nowUtc);

// Disables and clears the RTC alarm after the clock or offset changed while
// awake; the next alarm is always recomputed at sleep time.
void onTimeChanged();

// Returns only if sleep was refused or nothing could be armed; `reason`
// explains why. If the door is open, the Hall source is armed to wake on
// door close.
bool enter(String& reason);

}  // namespace SleepManager
}  // namespace acorn
