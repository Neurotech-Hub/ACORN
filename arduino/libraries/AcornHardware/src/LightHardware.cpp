#include "LightHardware.h"

#include "I2cHardware.h"
#include "Pins.h"

namespace acorn {
namespace LightHardware {

namespace {

constexpr uint8_t ADDR = i2c::ADDR_VEML6031;
constexpr uint8_t REG_CONF0 = 0x00;  // ALS_CONF_0 (must be written with CONF_1)
constexpr uint8_t REG_ALS = 0x10;
constexpr uint8_t REG_IR = 0x12;
constexpr uint8_t REG_ID = 0x14;
constexpr uint8_t REG_INT_FLAG_H = 0x17;
constexpr uint8_t FLAG_AF_DATA_READY = 0x08;
constexpr uint16_t EXPECTED_ID = 0x0001;  // ID_H = 0x00 for the 0x29 variant

// CONF_0 bits
constexpr uint8_t C0_MODE_FORCE = 0x08;
constexpr uint8_t C0_TRIG = 0x04;
constexpr uint8_t C0_SD = 0x01;
// CONF_1 bits
constexpr uint8_t C1_SD = 0x80;
constexpr uint8_t C1_PDDIV_QUARTER = 0x40;
constexpr uint8_t C1_CAL = 0x01;  // must be 1 once powered

enum Gain : uint8_t { GAIN_X1 = 0, GAIN_X2 = 1, GAIN_X066 = 2, GAIN_X05 = 3 };

struct Range {
  uint8_t itCode;     // ALS_IT
  uint16_t itMs;
  Gain gain;
  bool quarterPd;
  float luxPerCount;  // datasheet tables 11/12
};

// Most → least sensitive; each step covers roughly 4x the previous max.
constexpr Range RANGES[] = {
    {0x5, 100, GAIN_X2, false, 0.0103f},   // ≤ 673 lx
    {0x4, 50, GAIN_X1, false, 0.0411f},    // ≤ 2 693 lx
    {0x4, 50, GAIN_X1, true, 0.1644f},     // ≤ 10 771 lx
    {0x3, 25, GAIN_X05, true, 0.6574f},    // ≤ 43 083 lx
    {0x1, 6, GAIN_X05, true, 2.6296f},     // ≤ 172 333 lx (6.25 ms)
};
constexpr uint8_t RANGE_COUNT = sizeof(RANGES) / sizeof(RANGES[0]);
constexpr uint16_t SATURATED = 60000;
constexpr uint16_t LOW_COUNTS = 1000;

uint8_t lastRange = 0;

bool writeConf(uint8_t c0, uint8_t c1) {
  const uint8_t d[2] = {c0, c1};
  return I2cHardware::writeRegs(ADDR, REG_CONF0, d, 2);
}

uint8_t conf1For(const Range& r) {
  return C1_CAL | (r.quarterPd ? C1_PDDIV_QUARTER : 0) | (static_cast<uint8_t>(r.gain) << 3);
}

bool shutdown() { return writeConf(C0_SD, C1_SD | C1_CAL); }

bool read16(uint8_t reg, uint16_t& out) {
  uint8_t d[2];
  if (!I2cHardware::readRegs(ADDR, reg, d, 2)) return false;
  out = d[0] | (d[1] << 8);
  return true;
}

bool measure(const Range& r, uint16_t& als, uint16_t& ir) {
  const uint8_t c0 = (r.itCode << 4) | C0_MODE_FORCE;
  const uint8_t c1 = conf1For(r);
  if (!writeConf(c0, c1)) return false;  // power on in force mode
  delay(4);
  if (!writeConf(c0 | C0_TRIG, c1)) return false;
  delay(r.itMs);
  const uint32_t deadline = millis() + r.itMs + 20;
  for (;;) {
    uint8_t flags;
    if (!I2cHardware::readReg(ADDR, REG_INT_FLAG_H, flags)) return false;
    if (flags & FLAG_AF_DATA_READY) break;
    if ((int32_t)(millis() - deadline) > 0) return false;
    delay(2);
  }
  return read16(REG_ALS, als) && read16(REG_IR, ir);
}

}  // namespace

bool readId(uint16_t& id) { return read16(REG_ID, id); }

bool begin() {
  if (!I2cHardware::begin() || !I2cHardware::probe(ADDR)) return false;
  uint16_t id;
  if (!readId(id) || id != EXPECTED_ID) return false;
  return shutdown();
}

LightReading read() {
  LightReading out;
  uint8_t idx = lastRange < RANGE_COUNT ? lastRange : 0;
  // Bounded search: at most one pass through the table.
  for (uint8_t attempt = 0; attempt < RANGE_COUNT + 1; attempt++) {
    uint16_t als, ir;
    if (!measure(RANGES[idx], als, ir)) {
      shutdown();
      return out;
    }
    if (als >= SATURATED && idx + 1 < RANGE_COUNT) {
      idx++;
      continue;
    }
    if (als < LOW_COUNTS && idx > 0 && attempt == 0) {
      idx--;
      continue;
    }
    out.rawAls = als;
    out.rawIr = ir;
    out.range = idx;
    out.valid = als < SATURATED;
    if (out.valid) out.lux = als * RANGES[idx].luxPerCount;
    break;
  }
  lastRange = idx;
  shutdown();
  return out;
}

}  // namespace LightHardware
}  // namespace acorn
