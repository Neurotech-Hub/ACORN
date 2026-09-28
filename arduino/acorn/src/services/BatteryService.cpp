#include "BatteryService.h"

#include <AcornHardware.h>

#include "../config/DeviceConfig.h"

namespace acorn {
namespace BatteryService {

namespace {
BatteryState state;
bool haveReading = false;
}  // namespace

void begin() {
  BatteryHardware::begin();
  applyCalibration();
}

void applyCalibration() { BatteryHardware::setCalibration(DeviceConfig::get().batteryCalibration); }

BatteryState read() {
  const BatteryReading r = BatteryHardware::read();
  BatteryState s;
  s.readAtMs = millis();
  s.valid = r.valid && r.voltage >= MIN_PLAUSIBLE_V && r.voltage <= MAX_PLAUSIBLE_V;
  if (s.valid) {
    s.voltage = r.voltage;
    s.percent = DeviceConfig::get().batteryModel.percent(r.voltage);
  }
  state = s;
  haveReading = true;
  return s;
}

const BatteryState& last() { return state; }

const BatteryState& fresh(uint32_t maxAgeMs) {
  if (!haveReading || millis() - state.readAtMs > maxAgeMs) read();
  return state;
}

bool faulted() { return haveReading && !state.valid; }

}  // namespace BatteryService
}  // namespace acorn
