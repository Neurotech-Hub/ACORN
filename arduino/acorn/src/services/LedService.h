#pragma once

#include <Arduino.h>

// The only module that drives the LEDs (init.md section 32).
// LED_G = configuration AP: blinks while waiting for a station, solid once
// at least one Wi-Fi client is joined, off when the AP is down.
// setBroadcasting() / setBroadcastConnected() are called only from WifiService.
// LED_R = solid while the SD card is not mounted (awake only; both LEDs are
// off during sleep), plus BTN_02 battery flashes and the invalid-battery
// pattern, and a fast blink when the battery is below the critical gate.
// A flash briefly takes the LED over from the solid indicator.

namespace acorn {
namespace LedService {

constexpr uint16_t FLASH_ON_MS = 100;
constexpr uint16_t FLASH_OFF_MS = 100;  // "approximately 100 ms between flashes"
constexpr uint16_t ERROR_ON_MS = 2000;  // one long flash = battery reading invalid
constexpr uint16_t WAIT_ON_MS = 400;    // LED_G while AP is up with no clients
constexpr uint16_t WAIT_OFF_MS = 400;
constexpr uint16_t CRITICAL_ON_MS = 60;   // critical battery: fast blink, about 5 s
constexpr uint16_t CRITICAL_OFF_MS = 60;
constexpr uint8_t CRITICAL_COUNT = 40;
constexpr uint16_t FLASH_LEAD_MS = 300;  // LED_R dark before a flash while the SD indicator is on
constexpr uint16_t SD_RESUME_MS = 600;   // LED_R dark after a flash before the SD indicator returns

void begin();
void update();  // call from loop()

void setBroadcasting(bool on);           // AP up/down
void setBroadcastConnected(bool hasClient);  // station joined / left (while AP up)
bool isBroadcasting();

void setSdMissing(bool missing);  // LED_R solid while true (call every loop)

void flashBattery(uint8_t count);  // 1..10 short LED_R flashes
void flashBatteryError();          // single long LED_R flash
void flashCritical();              // fast LED_R blink: battery below the critical gate
bool isFlashing();

void allOff();
void prepareForSleep();  // both OFF and latched (section 42)

}  // namespace LedService
}  // namespace acorn
