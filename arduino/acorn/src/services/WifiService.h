#pragma once

#include <Arduino.h>

// Configuration AP policy (init.md section 43). This is the only AP
// start/stop path, and therefore the only caller of
// LedService::setBroadcasting(): LED_G is ON exactly while the AP is up.

namespace acorn {
namespace WifiService {

// start() also runs the captive-portal DNS (every name -> the AP address).
bool start();
void stop();
void update();  // DNS; call from loop() while the AP runs
bool isRunning();
const char* ssid();
uint8_t clients();

// Idle session: any API request except GET /status counts as activity;
// connected stations alone do not.
void touch();
uint32_t idleTimeoutS();
uint32_t idleRemainingS();
bool idleExpired();

}  // namespace WifiService
}  // namespace acorn
