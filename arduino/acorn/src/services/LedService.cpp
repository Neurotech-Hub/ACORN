#include "LedService.h"

#include <AcornHardware.h>

namespace acorn {
namespace LedService {

namespace {
bool broadcasting = false;
bool hasClient = false;
bool waitPhaseOn = false;
uint32_t waitChangedAt = 0;

void startWaitBlink() {
  hasClient = false;
  waitPhaseOn = true;
  waitChangedAt = millis();
  LedHardware::set(Led::Green, true);
}
}  // namespace

void begin() {
  LedHardware::begin();
  broadcasting = false;
  hasClient = false;
}

void update() {
  LedHardware::update();
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

void flashBattery(uint8_t count) {
  if (!count) return;
  LedHardware::blink(Led::Red, FLASH_ON_MS, FLASH_OFF_MS, count);
}

void flashBatteryError() { LedHardware::blink(Led::Red, ERROR_ON_MS, 0, 1); }

bool isFlashing() { return LedHardware::isBlinking(); }

void allOff() {
  broadcasting = false;
  hasClient = false;
  LedHardware::allOff();
}

void prepareForSleep() {
  broadcasting = false;
  hasClient = false;
  LedHardware::prepareForSleep();
}

}  // namespace LedService
}  // namespace acorn
