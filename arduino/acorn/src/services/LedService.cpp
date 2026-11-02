#include "LedService.h"

#include <AcornHardware.h>

namespace acorn {
namespace LedService {

namespace {
bool broadcasting = false;
bool hasClient = false;
bool waitPhaseOn = false;
uint32_t waitChangedAt = 0;

// LED_R is solid while the SD card is not mounted. A battery flash takes the
// LED over: it starts from "off" after FLASH_LEAD_MS so the first flash is
// distinct, and the solid indicator returns SD_RESUME_MS after the last one.
bool sdMissing = false;
bool flashPending = false;
uint16_t pendingOnMs = 0;
uint16_t pendingOffMs = 0;
uint8_t pendingCount = 0;
uint32_t flashAt = 0;     // when a deferred flash starts
uint32_t resumeAt = 0;    // earliest time the solid indicator may return

bool reached(uint32_t t) { return static_cast<int32_t>(millis() - t) >= 0; }

void startWaitBlink() {
  hasClient = false;
  waitPhaseOn = true;
  waitChangedAt = millis();
  LedHardware::set(Led::Green, true);
}

void startRedFlash(uint16_t onMs, uint16_t offMs, uint8_t count) {
  if (!sdMissing) {
    LedHardware::blink(Led::Red, onMs, offMs, count);
    return;
  }
  LedHardware::set(Led::Red, false);
  flashPending = true;
  pendingOnMs = onMs;
  pendingOffMs = offMs;
  pendingCount = count;
  flashAt = millis() + FLASH_LEAD_MS;
}

void updateRed() {
  if (flashPending && reached(flashAt)) {
    flashPending = false;
    LedHardware::blink(Led::Red, pendingOnMs, pendingOffMs, pendingCount);
  }
  if (flashPending || LedHardware::isBlinking()) {
    resumeAt = millis() + SD_RESUME_MS;
    return;
  }
  if (reached(resumeAt) && LedHardware::isOn(Led::Red) != sdMissing) LedHardware::set(Led::Red, sdMissing);
}
}  // namespace

void begin() {
  LedHardware::begin();
  broadcasting = false;
  hasClient = false;
  sdMissing = false;
  flashPending = false;
  resumeAt = 0;
}

void update() {
  LedHardware::update();
  updateRed();

  if (!broadcasting || hasClient) return;
  const uint16_t interval = waitPhaseOn ? WAIT_ON_MS : WAIT_OFF_MS;
  if (millis() - waitChangedAt < interval) return;
  waitPhaseOn = !waitPhaseOn;
  waitChangedAt = millis();
  LedHardware::set(Led::Green, waitPhaseOn);
}

void setBroadcasting(bool on) {
  broadcasting = on;
  if (!on) {
    hasClient = false;
    LedHardware::set(Led::Green, false);
    return;
  }
  startWaitBlink();
}

void setBroadcastConnected(bool connected) {
  if (!broadcasting || hasClient == connected) return;
  if (connected) {
    hasClient = true;
    LedHardware::set(Led::Green, true);
  } else {
    startWaitBlink();
  }
}

bool isBroadcasting() { return broadcasting; }

void setSdMissing(bool missing) { sdMissing = missing; }

void flashBattery(uint8_t count) {
  if (!count) return;
  startRedFlash(FLASH_ON_MS, FLASH_OFF_MS, count);
}

void flashBatteryError() { startRedFlash(ERROR_ON_MS, 0, 1); }

void flashCritical() { startRedFlash(CRITICAL_ON_MS, CRITICAL_OFF_MS, CRITICAL_COUNT); }

bool isFlashing() { return flashPending || LedHardware::isBlinking(); }

void allOff() {
  broadcasting = false;
  hasClient = false;
  sdMissing = false;
  flashPending = false;
  LedHardware::allOff();
}

void prepareForSleep() {
  broadcasting = false;
  hasClient = false;
  sdMissing = false;
  flashPending = false;
  LedHardware::prepareForSleep();
}

}  // namespace LedService
}  // namespace acorn
