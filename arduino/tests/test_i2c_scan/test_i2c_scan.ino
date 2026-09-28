// Isolated test: scan the shared I2C bus (GPIO4 SDA / GPIO5 SCL) and confirm
// the three on-board devices respond. Extra devices (STEMMA QT) are listed.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

const char* nameFor(uint8_t addr) {
  switch (addr) {
    case i2c::ADDR_SHT40: return "SHT40";
    case i2c::ADDR_VEML6031: return "VEML6031X00";
    case i2c::ADDR_MCP7940N: return "MCP7940N";
    default: return "unknown";
  }
}

void scan() {
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    if (I2cHardware::probe(addr)) {
      test::info("0x%02X  %s", addr, nameFor(addr));
      found++;
    }
  }
  test::info("%u device(s) found", found);
}

void setup() {
  test::begin("test_i2c_scan");
  test::check("I2C bus started", I2cHardware::begin());
  scan();
  test::check("MCP7940N RTC at 0x6F", I2cHardware::probe(i2c::ADDR_MCP7940N));
  test::check("SHT40 at 0x44", I2cHardware::probe(i2c::ADDR_SHT40));
  test::check("VEML6031X00 at 0x29", I2cHardware::probe(i2c::ADDR_VEML6031));
  test::summary();
  test::info("Send any line to rescan");
}

void loop() {
  String line;
  if (test::readLine(line)) scan();
  delay(10);
}
