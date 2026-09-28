#pragma once

#include <Arduino.h>
#include <AcornCore.h>

// Device state that belongs in NVS rather than on the SD card (init.md 3.3):
// name, UTC offset, battery model/calibration, environmental logging and the
// active Program ID.

namespace acorn {

struct DeviceSettings {
  String name;
  int32_t utcOffsetMinutes = 0;
  BatteryModel batteryModel;
  float batteryCalibration = 1.0f;
  bool envEnabled = true;
  uint16_t envIntervalMinutes = EnvSchedule::DEFAULT_INTERVAL_MINUTES;
  String activeProgramId;  // "" = none
};

namespace DeviceConfig {

constexpr uint32_t SCHEMA_VERSION = 1;
constexpr const char* DEFAULT_ACTIVE_PROGRAM = "default";

void begin();
const DeviceSettings& get();
const char* deviceId();  // immutable "ACORN-xxxxxx" from the MAC (section 38)

bool isValidName(const char* name);          // ^[A-Za-z0-9_-]{1,32}$
bool isValidUtcOffset(long minutes);         // -720..840, multiple of 15

bool setName(const String& name);
bool setUtcOffset(int32_t minutes);
bool setBatteryModel(const BatteryModel& model);
bool setBatteryCalibration(float scale);
bool setEnvironment(bool enabled, uint16_t intervalMinutes);
bool setActiveProgram(const String& id);

}  // namespace DeviceConfig
}  // namespace acorn
