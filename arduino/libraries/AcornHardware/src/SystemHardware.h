#pragma once

#include <Arduino.h>

namespace acorn {

namespace SystemHardware {

// Last three bytes of the factory base MAC as uppercase hex, e.g. "A1B2C3".
const char* deviceSuffix();
// "ACORN-A1B2C3": device ID and Wi-Fi AP SSID.
const char* deviceId();

const char* resetReason();  // e.g. "POWER_ON", "DEEP_SLEEP", "PANIC"

}  // namespace SystemHardware
}  // namespace acorn
