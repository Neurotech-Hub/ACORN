#pragma once

#include <Arduino.h>

namespace acorn {

struct LightReading {
  bool valid = false;  // false on I2C error or saturation at the least sensitive range
  float lux = NAN;
  uint16_t rawAls = 0;
  uint16_t rawIr = 0;
  uint8_t range = 0;   // index into the auto-range table (0 = most sensitive)
};

// VEML6031X00 (0x29). Active-force (one-shot) measurements with auto-ranging;
// the sensor is shut down between readings.
namespace LightHardware {

bool begin();               // probe + ID check + calibration enable + shutdown
bool readId(uint16_t& id);  // expected 0x0001 for the 0x29 variant
LightReading read();

}  // namespace LightHardware
}  // namespace acorn
