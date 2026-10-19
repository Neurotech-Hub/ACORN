#include "TimeService.h"

#include <AcornHardware.h>
#include <esp_timer.h>

#include "../config/DeviceConfig.h"

namespace acorn {
namespace TimeService {

namespace {

constexpr int64_t UNSET_EPOCH = 946684800;  // 2000-01-01T00:00:00Z
constexpr uint32_t RESYNC_MS = 5UL * 60UL * 1000UL;

int64_t baseEpoch = UNSET_EPOCH;
int64_t baseUs = 0;
bool valid = false;
bool rtcOk = false;
uint32_t gen = 0;
uint32_t lastSyncMs = 0;

void anchor(int64_t epoch) {
  baseEpoch = epoch;
  baseUs = esp_timer_get_time();
}

bool syncFromRtc() {
  rtcOk = RtcHardware::status().valid;
  int64_t epoch;
  if (rtcOk && RtcHardware::nowEpoch(epoch)) {
    anchor(epoch);
    return true;
  }
  return false;
}

}  // namespace

void begin() {
  anchor(UNSET_EPOCH);
  valid = syncFromRtc();
  lastSyncMs = millis();
}

void update() {
  // A set that just missed OSCRUN must be rechecked promptly. The 5-minute
  // cadence would otherwise leave rtc_valid false while the software clock runs.
  const uint32_t interval = (valid && !rtcOk) ? 1000 : RESYNC_MS;
  if (millis() - lastSyncMs < interval) return;
  lastSyncMs = millis();
  const int64_t before = nowUtc();
  if (syncFromRtc()) {
    if (!valid) {
      valid = true;
      gen++;
    } else if (llabs(nowUtc() - before) > 2) {
      gen++;  // the software clock drifted or the RTC was changed externally
    }
  }
}

bool isValid() { return valid; }
bool rtcValid() { return rtcOk; }

int64_t nowUtc() { return baseEpoch + (esp_timer_get_time() - baseUs) / 1000000; }
int32_t utcOffsetMinutes() { return DeviceConfig::get().utcOffsetMinutes; }
int64_t toLocal(int64_t utc) { return utc + static_cast<int64_t>(utcOffsetMinutes()) * 60; }

String isoUtc(int64_t utc) {
  char buf[24];
  UtcTime::formatEpochIso(utc, buf, sizeof(buf));
  return buf;
}

String isoLocal(int64_t utc) {
  char buf[24];
  UtcTime::formatEpochLocalIso(toLocal(utc), buf, sizeof(buf));
  return buf;
}

bool setUtc(int64_t utc) {
  const bool wrote = RtcHardware::setEpoch(utc);
  anchor(utc);
  valid = true;
  rtcOk = wrote && RtcHardware::status().valid;
  lastSyncMs = millis();
  gen++;
  return rtcOk;
}

bool setUtcOffset(int32_t minutes) {
  if (!DeviceConfig::setUtcOffset(minutes)) return false;
  gen++;
  return true;
}

uint32_t generation() { return gen; }

}  // namespace TimeService
}  // namespace acorn
