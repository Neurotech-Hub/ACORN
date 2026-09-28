#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace acorn {

// Shared I2C bus (GPIO4 SDA / GPIO5 SCL) with small register helpers used by
// the first-party RTC and sensor drivers.
namespace I2cHardware {

bool begin(uint32_t clockHz = 0);
void end();
bool probe(uint8_t address);

// 8-bit register address, N data bytes (auto-increment).
bool writeRegs(uint8_t address, uint8_t reg, const uint8_t* data, size_t len);
bool readRegs(uint8_t address, uint8_t reg, uint8_t* data, size_t len);
bool writeReg(uint8_t address, uint8_t reg, uint8_t value);
bool readReg(uint8_t address, uint8_t reg, uint8_t& value);

// Raw command write / raw read (no register pointer), used by SHT40.
bool writeCommand(uint8_t address, uint8_t command);
bool readRaw(uint8_t address, uint8_t* data, size_t len);

}  // namespace I2cHardware
}  // namespace acorn
