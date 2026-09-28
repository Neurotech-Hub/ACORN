#pragma once

// Single source of truth for ACORN pin numbers and electrical polarity.
// See docs/hardware.md. Application code should use the *Hardware helpers
// rather than these constants directly.

#include <Arduino.h>

namespace acorn {
namespace pins {

constexpr uint8_t VBAT_ADC = 1;
constexpr uint8_t LED_R = 2;
constexpr uint8_t I2C_SDA = 4;
constexpr uint8_t I2C_SCL = 5;
constexpr uint8_t RTC_MFP = 6;
constexpr uint8_t BTN_01 = 7;
constexpr uint8_t BTN_02 = 8;
constexpr uint8_t TRIG_IN = 9;  // unused in v1 (init.md section 69)
constexpr uint8_t SD_CS = 10;
constexpr uint8_t MAG_OUT = 11;
constexpr uint8_t SD_DETECT = 12;
constexpr uint8_t AUX_IO = 13;  // reserved
constexpr uint8_t SD_CLK = 14;
constexpr uint8_t SD_MOSI = 15;
constexpr uint8_t SD_MISO = 16;
constexpr uint8_t SW_3V3_EN = 17;
constexpr uint8_t DAC_XSMT = 18;
constexpr uint8_t I2S_BCK = 35;
constexpr uint8_t I2S_LRCK = 36;
constexpr uint8_t I2S_DOUT = 37;
constexpr uint8_t AMP_SHDN = 38;
constexpr uint8_t LED_G = 48;

}  // namespace pins

namespace level {

// Not stated in the netlist notes; verify with test_leds on first bring-up.
constexpr uint8_t LED_ON = HIGH;
constexpr uint8_t LED_OFF = !LED_ON;

constexpr uint8_t BUTTON_PRESSED = LOW;
constexpr uint8_t RTC_MFP_ASSERTED = LOW;
constexpr uint8_t DOOR_OPEN = HIGH;
constexpr uint8_t SD_INSERTED = LOW;
constexpr uint8_t SW_3V3_ON = HIGH;
constexpr uint8_t DAC_UNMUTED = HIGH;
constexpr uint8_t AMP_ENABLED = HIGH;

// AMP_SHDN reaches TPA3138D2 SD/FAULT through a 5.1k series resistor, and
// SD/FAULT has a 100k pulldown to GND. High-Z (INPUT) therefore means amp
// OFF, which is also the state during boot, reset and deep sleep; driving
// HIGH enables it. The series resistor limits contention (~0.65 mA) when the
// amp pulls SD/FAULT LOW on a fault while the GPIO drives HIGH.

}  // namespace level

namespace i2c {

constexpr uint8_t ADDR_SHT40 = 0x44;
constexpr uint8_t ADDR_VEML6031 = 0x29;
constexpr uint8_t ADDR_MCP7940N = 0x6F;
constexpr uint32_t CLOCK_HZ = 400000;

}  // namespace i2c

// Battery divider: 1M top, 249k bottom.
constexpr float VBAT_DIVIDER_RATIO = (1000.0f + 249.0f) / 249.0f;

}  // namespace acorn
