#include "RtcHardware.h"

#include "I2cHardware.h"
#include "Pins.h"

namespace acorn {
namespace RtcHardware {

namespace {

constexpr uint8_t ADDR = i2c::ADDR_MCP7940N;

constexpr uint8_t REG_RTCSEC = 0x00;
constexpr uint8_t REG_RTCWKDAY = 0x03;
constexpr uint8_t REG_CONTROL = 0x07;
constexpr uint8_t REG_ALM0SEC = 0x0A;
constexpr uint8_t REG_ALM0WKDAY = 0x0D;
constexpr uint8_t REG_SRAM = 0x20;

constexpr uint8_t BIT_ST = 0x80;       // RTCSEC: start oscillator
constexpr uint8_t BIT_12H = 0x40;      // RTCHOUR: 12-hour mode
constexpr uint8_t BIT_OSCRUN = 0x20;   // RTCWKDAY
constexpr uint8_t BIT_PWRFAIL = 0x10;  // RTCWKDAY
constexpr uint8_t BIT_VBATEN = 0x08;   // RTCWKDAY
constexpr uint8_t BIT_OUT = 0x80;      // CONTROL: MFP level when alarms off
constexpr uint8_t BIT_SQWEN = 0x40;
constexpr uint8_t BIT_ALM1EN = 0x20;
constexpr uint8_t BIT_ALM0EN = 0x10;
constexpr uint8_t BIT_EXTOSC = 0x08;
constexpr uint8_t BIT_ALMPOL = 0x80;   // ALMxWKDAY: 0 = MFP asserted LOW
constexpr uint8_t MASK_ALM_ALL = 0x70; // ALMxMSK = 111: full match
constexpr uint8_t BIT_ALMIF = 0x08;

constexpr uint8_t MAGIC[4] = {'A', 'C', 'R', 'N'};

uint8_t toBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
uint8_t fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

bool readTimeRegs(uint8_t r[7]) { return I2cHardware::readRegs(ADDR, REG_RTCSEC, r, 7); }

bool decode(const uint8_t r[7], UtcDateTime& t) {
  if (r[2] & BIT_12H) return false;
  t.second = fromBcd(r[0] & 0x7F);
  t.minute = fromBcd(r[1] & 0x7F);
  t.hour = fromBcd(r[2] & 0x3F);
  t.day = fromBcd(r[4] & 0x3F);
  t.month = fromBcd(r[5] & 0x1F);
  t.year = 2000 + fromBcd(r[6]);
  return UtcTime::isValid(t);
}

bool markerPresent() {
  uint8_t m[4];
  if (!I2cHardware::readRegs(ADDR, REG_SRAM, m, sizeof(m))) return false;
  return memcmp(m, MAGIC, sizeof(m)) == 0;
}

bool updateBits(uint8_t reg, uint8_t clear, uint8_t set) {
  uint8_t v;
  if (!I2cHardware::readReg(ADDR, reg, v)) return false;
  const uint8_t n = (v & ~clear) | set;
  return n == v || I2cHardware::writeReg(ADDR, reg, n);
}

}  // namespace

bool begin() {
  pinMode(pins::RTC_MFP, INPUT);
  if (!I2cHardware::begin() || !I2cHardware::probe(ADDR)) return false;
  // Battery backup on; crystal (not external clock); MFP idles HIGH
  // (released) whenever no alarm is enabled.
  return updateBits(REG_RTCWKDAY, 0, BIT_VBATEN) &&
         updateBits(REG_CONTROL, BIT_SQWEN | BIT_EXTOSC | BIT_ALM1EN, BIT_OUT);
}

RtcStatus status() {
  RtcStatus s;
  uint8_t r[7];
  if (!readTimeRegs(r)) return s;
  s.present = true;
  s.oscillatorRunning = (r[0] & BIT_ST) && (r[3] & BIT_OSCRUN);
  s.powerFailed = r[3] & BIT_PWRFAIL;
  s.timeSet = markerPresent();
  UtcDateTime t;
  s.valid = s.oscillatorRunning && s.timeSet && decode(r, t);
  return s;
}

bool now(UtcDateTime& out) {
  uint8_t r[7];
  if (!readTimeRegs(r)) return false;
  if (!(r[0] & BIT_ST) || !(r[3] & BIT_OSCRUN) || !markerPresent()) return false;
  UtcDateTime t;
  if (!decode(r, t)) return false;
  out = t;
  return true;
}

bool nowEpoch(int64_t& out) {
  UtcDateTime t;
  if (!now(t)) return false;
  out = UtcTime::toEpoch(t);
  return true;
}

bool setTime(const UtcDateTime& t) {
  if (!UtcTime::isValid(t)) return false;
  // Stop the oscillator and wait for OSCRUN to clear so the write cannot
  // straddle a rollover.
  if (!I2cHardware::writeReg(ADDR, REG_RTCSEC, 0)) return false;
  for (uint8_t i = 0; i < 20; i++) {
    uint8_t wk;
    if (!I2cHardware::readReg(ADDR, REG_RTCWKDAY, wk)) return false;
    if (!(wk & BIT_OSCRUN)) break;
    delay(1);
  }
  const uint8_t regs[6] = {
      toBcd(t.minute),
      toBcd(t.hour),  // 24-hour mode
      static_cast<uint8_t>(BIT_VBATEN | UtcTime::weekday(t)),  // clears PWRFAIL
      toBcd(t.day),
      toBcd(t.month),
      toBcd(static_cast<uint8_t>(t.year - 2000)),
  };
  if (!I2cHardware::writeRegs(ADDR, REG_RTCSEC + 1, regs, sizeof(regs))) return false;
  if (!I2cHardware::writeReg(ADDR, REG_RTCSEC, BIT_ST | toBcd(t.second))) return false;
  if (!I2cHardware::writeRegs(ADDR, REG_SRAM, MAGIC, sizeof(MAGIC))) return false;
  // OSCRUN lags the start bit by the crystal startup time. status() is not
  // valid until it sets, so wait here instead of reporting a failed set.
  for (uint8_t i = 0; i < 200; i++) {
    uint8_t wk;
    if (!I2cHardware::readReg(ADDR, REG_RTCWKDAY, wk)) return false;
    if (wk & BIT_OSCRUN) return true;
    delay(10);
  }
  return false;
}

bool setEpoch(int64_t epoch) { return setTime(UtcTime::fromEpoch(epoch)); }

bool clearPowerFail() { return updateBits(REG_RTCWKDAY, BIT_PWRFAIL, 0); }

bool setAlarm(const UtcDateTime& t) {
  if (!UtcTime::isValid(t)) return false;
  const uint8_t regs[6] = {
      toBcd(t.second),
      toBcd(t.minute),
      toBcd(t.hour),
      static_cast<uint8_t>(MASK_ALM_ALL | UtcTime::weekday(t)),  // ALMPOL=0, IF cleared
      toBcd(t.day),
      toBcd(t.month),
  };
  if (!I2cHardware::writeRegs(ADDR, REG_ALM0SEC, regs, sizeof(regs))) return false;
  return updateBits(REG_CONTROL, BIT_SQWEN, BIT_ALM0EN | BIT_OUT);
}

bool disableAlarm() {
  return updateBits(REG_CONTROL, BIT_ALM0EN, BIT_OUT) && clearAlarmFlag();
}

bool alarmFired() {
  uint8_t v;
  return I2cHardware::readReg(ADDR, REG_ALM0WKDAY, v) && (v & BIT_ALMIF);
}

bool clearAlarmFlag() { return updateBits(REG_ALM0WKDAY, BIT_ALMIF | BIT_ALMPOL, 0); }

bool isMfpAsserted() { return digitalRead(pins::RTC_MFP) == level::RTC_MFP_ASSERTED; }

}  // namespace RtcHardware
}  // namespace acorn
