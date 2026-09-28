#include "BatteryHardware.h"

#include "Pins.h"

namespace acorn {
namespace BatteryHardware {

namespace {

// Outside this pin-voltage window the reading is treated as invalid rather
// than reported as 0% / 100%: below = open/shorted divider, above = ADC
// saturation at 11 dB attenuation.
constexpr uint16_t MIN_VALID_MV = 100;
constexpr uint16_t MAX_VALID_MV = 3050;

float scale = 1.0f;

}  // namespace

void begin() {
  pinMode(pins::VBAT_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(pins::VBAT_ADC, ADC_11db);
}

BatteryReading read(uint8_t samples) {
  BatteryReading r;
  if (samples == 0) samples = 1;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogReadMilliVolts(pins::VBAT_ADC);
    delayMicroseconds(200);
  }
  r.adcMillivolts = sum / samples;
  if (r.adcMillivolts < MIN_VALID_MV || r.adcMillivolts > MAX_VALID_MV) return r;
  r.voltage = (r.adcMillivolts / 1000.0f) * VBAT_DIVIDER_RATIO * scale;
  r.valid = true;
  return r;
}

void setCalibration(float s) {
  if (s > 0.5f && s < 1.5f) scale = s;
}

float calibration() { return scale; }

}  // namespace BatteryHardware
}  // namespace acorn
