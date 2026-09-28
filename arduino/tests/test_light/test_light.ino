// Isolated test: VEML6031X00 ambient light (0x29), auto-ranged one-shot reads.
// Cover the sensor, then shine a light on it; lux and range should follow.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

void printReading(const LightReading& r) {
  if (r.valid) {
    test::info("%.2f lx  (ALS %u, IR %u, range %u)", r.lux, r.rawAls, r.rawIr, r.range);
  } else {
    test::info("reading invalid -> SENSOR_READ_FAILED (ALS %u, range %u)", r.rawAls, r.range);
  }
}

void setup() {
  test::begin("test_light");
  uint16_t id = 0;
  const bool idOk = I2cHardware::begin() && LightHardware::readId(id);
  char detail[16];
  snprintf(detail, sizeof(detail), "0x%04X", id);
  test::check("device ID read", idOk, detail);
  if (!test::check("VEML6031X00 present + ID 0x0001", LightHardware::begin())) {
    test::summary();
    return;
  }
  const LightReading r = LightHardware::read();
  printReading(r);
  test::check("measurement valid", r.valid);
  test::summary();
  test::info("Printing every 1 s");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    printReading(LightHardware::read());
  }
}
