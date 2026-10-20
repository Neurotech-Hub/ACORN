#pragma once

#include <Arduino.h>

// The only module that drives the LEDs (init.md section 32).
// LED_G = configuration AP: blinks while waiting for a station, solid once
// at least one Wi-Fi client is joined, off when the AP is down.
// setBroadcasting() / setBroadcastConnected() are called only from WifiService.
// LED_R = BTN_02 battery flashes and the invalid-battery pattern.

namespace acorn {
namespace LedService {

constexpr uint16_t FLASH_ON_MS = 100;
constexpr uint16_t FLASH_OFF_MS = 100;  // "approximately 100 ms between flashes"
constexpr uint16_t ERROR_ON_MS = 2000;  // one long flash = battery reading invalid
constexpr uint16_t WAIT_ON_MS = 400;    // LED_G while AP is up with no clients
constexpr uint16_t WAIT_OFF_MS = 400;

void begin();
void update();  // call from loop()

void setBroadcasting(bool on);           // AP up/down
void setBroadcastConnected(bool hasClient);  // station joined / left (while AP up)
bool isBroadcasting();

void flashBattery(uint8_t count);  // 1..10 short LED_R flashes
void flashBatteryError();          // single long LED_R flash
bool isFlashing();

void allOff();
void prepareForSleep();  // both OFF and latched (section 42)

}  // namespace LedService
}  // namespace acorn
