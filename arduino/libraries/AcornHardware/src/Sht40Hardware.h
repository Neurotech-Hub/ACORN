#pragma once

#include <Arduino.h>

namespace acorn {

struct EnvReading {
  bool valid = false;  // false on I2C error or CRC mismatch; never fake zeros
  float temperatureC = NAN;
  float humidityPct = NAN;
};

// SHT40-AD1F (0x44), high-precision single-shot measurements.
namespace Sht40Hardware {

bool begin();                    // probe + soft reset
bool readSerial(uint32_t& out);
EnvReading read();

// CRC-8 (poly 0x31, init 0xFF); exposed for tests.
uint8_t crc8(const uint8_t* data, size_t len);

}  // namespace Sht40Hardware
}  // namespace acorn
