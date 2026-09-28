#include "I2cHardware.h"

#include "Pins.h"

namespace acorn {
namespace I2cHardware {

namespace {
bool started = false;
}

bool begin(uint32_t clockHz) {
  if (started) return true;
  started = Wire.begin(pins::I2C_SDA, pins::I2C_SCL, clockHz ? clockHz : i2c::CLOCK_HZ);
  if (started) Wire.setTimeOut(50);
  return started;
}

void end() {
  if (!started) return;
  Wire.end();
  started = false;
}

bool probe(uint8_t address) {
  if (!begin()) return false;
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writeRegs(uint8_t address, uint8_t reg, const uint8_t* data, size_t len) {
  if (!begin()) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (len) Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t address, uint8_t reg, uint8_t* data, size_t len) {
  if (!begin()) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  return readRaw(address, data, len);
}

bool writeReg(uint8_t address, uint8_t reg, uint8_t value) {
  return writeRegs(address, reg, &value, 1);
}

bool readReg(uint8_t address, uint8_t reg, uint8_t& value) {
  return readRegs(address, reg, &value, 1);
}

bool writeCommand(uint8_t address, uint8_t command) {
  if (!begin()) return false;
  Wire.beginTransmission(address);
  Wire.write(command);
  return Wire.endTransmission() == 0;
}

bool readRaw(uint8_t address, uint8_t* data, size_t len) {
  if (!begin()) return false;
  size_t got = Wire.requestFrom(static_cast<uint16_t>(address), len, true);
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; i++) data[i] = Wire.read();
  return true;
}

}  // namespace I2cHardware
}  // namespace acorn
