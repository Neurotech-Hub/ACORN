#include "LedService.h"

#include <AcornHardware.h>

namespace acorn {
namespace LedService {

namespace {
bool broadcasting = false;
}  // namespace

void begin() {
  LedHardware::begin();
  broadcasting = false;
}

void update() { LedHardware::update(); }

void setBroadcasting(bool on) {
  broadcasting = on;
  LedHardware::set(Led::Green, on);
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
  LedHardware::allOff();
}

void prepareForSleep() {
  broadcasting = false;
  LedHardware::prepareForSleep();
}

}  // namespace LedService
}  // namespace acorn
