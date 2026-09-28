#include "Sht40Hardware.h"

#include "I2cHardware.h"
#include "Pins.h"

namespace acorn {
namespace Sht40Hardware {

namespace {
constexpr uint8_t ADDR = i2c::ADDR_SHT40;
constexpr uint8_t CMD_MEASURE_HIGH = 0xFD;  // max 8.3 ms
constexpr uint8_t CMD_SERIAL = 0x89;
constexpr uint8_t CMD_SOFT_RESET = 0x94;    // max 1 ms

bool readWords(uint8_t cmd, uint16_t waitMs, uint16_t& a, uint16_t& b) {
  if (!I2cHardware::writeCommand(ADDR, cmd)) return false;
  delay(waitMs);
  uint8_t d[6];
  if (!I2cHardware::readRaw(ADDR, d, sizeof(d))) return false;
  if (crc8(d, 2) != d[2] || crc8(d + 3, 2) != d[5]) return false;
  a = (d[0] << 8) | d[1];
  b = (d[3] << 8) | d[4];
  return true;
}
}  // namespace

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return crc;
}

bool begin() {
  if (!I2cHardware::begin() || !I2cHardware::probe(ADDR)) return false;
  if (!I2cHardware::writeCommand(ADDR, CMD_SOFT_RESET)) return false;
  delay(2);
  return true;
}

bool readSerial(uint32_t& out) {
  uint16_t hi, lo;
  if (!readWords(CMD_SERIAL, 2, hi, lo)) return false;
  out = (static_cast<uint32_t>(hi) << 16) | lo;
  return true;
}

EnvReading read() {
  EnvReading r;
  uint16_t rawT, rawRh;
  if (!readWords(CMD_MEASURE_HIGH, 10, rawT, rawRh)) return r;
  r.temperatureC = -45.0f + 175.0f * rawT / 65535.0f;
  r.humidityPct = constrain(-6.0f + 125.0f * rawRh / 65535.0f, 0.0f, 100.0f);
  r.valid = true;
  return r;
}

}  // namespace Sht40Hardware
}  // namespace acorn
