#include "SleepManager.h"

#include <AcornHardware.h>

#include "../audio/PlaybackService.h"
#include "../services/LedService.h"
#include "../services/TimeService.h"
#include "../state/DeviceState.h"

namespace acorn {
namespace SleepManager {

WakePlanner::Wake nextWake(int64_t nowUtc) {
  ScheduledEvent next;
  const int64_t playback = DeviceState::nextPlayback(nowUtc, next) ? next.utc : WakePlanner::NONE;
  const int64_t env = DeviceState::nextEnvSample(nowUtc);
  return WakePlanner::earliest(playback, env < 0 ? WakePlanner::NONE : env);
}

bool wakeTooClose(int64_t nowUtc) {
  const WakePlanner::Wake wake = nextWake(nowUtc);
  return wake.reason != WakePlanner::Reason::None && wake.utc - WAKE_LEAD_S - nowUtc < MIN_SLEEP_S;
}

void onTimeChanged() {
  RtcHardware::disableAlarm();
  RtcHardware::clearAlarmFlag();
}

bool enter(String& reason) {
  const int64_t now = TimeService::nowUtc();
  const WakePlanner::Wake wake = nextWake(now);
  if (wakeTooClose(now)) {
    reason = "next event is too close to sleep";
    return false;
  }

  // Section 42 steps 1-9.
  PlaybackService::stop();
  AudioHardware::prepareForSleep();
  SdHardware::unmount();
  SdHardware::prepareForSleep();
  WifiApHardware::stop();
  LedService::prepareForSleep();

  // Steps 10-11.
  RtcHardware::clearAlarmFlag();
  SleepConfig cfg;
  cfg.hallWakeOnClose = true;
  if (wake.reason == WakePlanner::Reason::None || !TimeService::isValid()) {
    RtcHardware::disableAlarm();
    cfg.wakeOnRtcAlarm = false;
  } else if (!RtcHardware::setAlarm(UtcTime::fromEpoch(wake.utc - WAKE_LEAD_S))) {
    cfg.wakeOnRtcAlarm = false;
  }

  Serial.printf("sleep: next wake %s (%s)%s\n",
                wake.reason == WakePlanner::Reason::None ? "none" : TimeService::isoUtc(wake.utc).c_str(),
                wake.reason == WakePlanner::Reason::Playback    ? "playback"
                : wake.reason == WakePlanner::Reason::EnvSample ? "env sample"
                                                                : "buttons/door only",
                HallHardware::isDoorOpen() ? ", door open: wake on close" : "");
  Serial.flush();

  // Step 12. Returns only if no wake source could be armed.
  const SleepArmResult r = SleepHardware::enter(cfg);
  reason = r.anyArmed() ? "sleep entry failed" : "no wake source could be armed";
  return false;
}

}  // namespace SleepManager
}  // namespace acorn
