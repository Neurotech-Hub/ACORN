#pragma once

#include <Arduino.h>

namespace acorn {

struct BatteryReading {
  bool valid = false;
  uint16_t adcMillivolts = 0;  // calibrated pin voltage (averaged)
  float voltage = NAN;         // battery volts after divider + calibration
};

// VBAT_ADC (GPIO1) through a 1M/249k divider. Voltage only: the
// voltage-to-percent (M12) model belongs to BatteryService.
namespace BatteryHardware {

void begin();
BatteryReading read(uint8_t samples = 16);

// Multiplicative correction for divider tolerance (default 1.0).
void setCalibration(float scale);
float calibration();

}  // namespace BatteryHardware
}  // namespace acorn
