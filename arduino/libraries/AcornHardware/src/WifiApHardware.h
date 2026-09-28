#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace acorn {

// Open (no password) soft AP named after the device ID ("ACORN-xxxxxx").
// When to start/stop it (Hall trigger, BTN hold) is policy, not HAL.
namespace WifiApHardware {

constexpr uint32_t DEFAULT_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;

bool start(uint8_t channel = 1);
void stop();
bool isRunning();
const char* ssid();
IPAddress ip();
uint8_t stationCount();

// Idle timeout: touch() on any client activity; expired() once no station
// is connected and nothing has touched the AP for `timeoutMs`.
void setIdleTimeout(uint32_t timeoutMs);
void touch();
bool idleExpired();
uint32_t idleMs();

}  // namespace WifiApHardware
}  // namespace acorn
