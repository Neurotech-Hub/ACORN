#pragma once

#include <Arduino.h>

// The only module that drives the LEDs (init.md section 32).
// LED_G = "broadcasting" = Wi-Fi AP running (resolved TBD, section 69):
// setBroadcasting() is called only from the AP start/stop path.
// LED_R = BTN_02 battery flashes and the invalid-battery pattern.

namespace acorn {
namespace LedService {

constexpr uint16_t FLASH_ON_MS = 100;
constexpr uint16_t FLASH_OFF_MS = 100;  // "approximately 100 ms between flashes"
constexpr uint16_t ERROR_ON_MS = 2000;  // one long flash = battery reading invalid

void begin();
void update();  // call from loop()

void setBroadcasting(bool on);
bool isBroadcasting();

void flashBattery(uint8_t count);  // 1..10 short LED_R flashes
void flashBatteryError();          // single long LED_R flash
bool isFlashing();

void allOff();
void prepareForSleep();  // both OFF and latched (section 42)

}  // namespace LedService
}  // namespace acorn
