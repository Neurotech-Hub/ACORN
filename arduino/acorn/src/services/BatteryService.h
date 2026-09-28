#pragma once

#include <Arduino.h>

// Battery voltage (authoritative) and the estimated percent from the NVS
// battery model (init.md section 33).

namespace acorn {

struct BatteryState {
  bool valid = false;
  float voltage = NAN;
  int32_t percent = 0;
  uint32_t readAtMs = 0;
};

namespace BatteryService {

// Readings outside this window are treated as an ADC/wiring fault
// (BATTERY_READING_INVALID) instead of being shown as 0 %.
constexpr float MIN_PLAUSIBLE_V = 6.0f;
constexpr float MAX_PLAUSIBLE_V = 15.0f;

void begin();
void applyCalibration();  // after DeviceConfig::setBatteryCalibration
BatteryState read();
const BatteryState& last();
// Cached reading, refreshed when older than maxAgeMs.
const BatteryState& fresh(uint32_t maxAgeMs = 10000);
bool faulted();  // last reading invalid

}  // namespace BatteryService
}  // namespace acorn
