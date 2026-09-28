// Isolated test: SHT40-AD1F temperature/humidity (0x44).
// Breathe on the sensor to see humidity rise.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

void printReading(const EnvReading& r) {
  if (r.valid) test::info("T %.2f C  RH %.1f %%", r.temperatureC, r.humidityPct);
  else test::info("reading invalid -> SENSOR_READ_FAILED");
}

void setup() {
  test::begin("test_sht40");
  const uint8_t sample[2] = {0xBE, 0xEF};
  test::check("CRC-8 datasheet vector (0xBEEF -> 0x92)", Sht40Hardware::crc8(sample, 2) == 0x92);

  if (!test::check("SHT40 present + soft reset", Sht40Hardware::begin())) {
    test::summary();
    return;
  }
  uint32_t serial;
  if (test::check("serial number read (CRC ok)", Sht40Hardware::readSerial(serial))) {
    test::info("serial 0x%08lX", static_cast<unsigned long>(serial));
  }
  const EnvReading r = Sht40Hardware::read();
  printReading(r);
  test::check("measurement valid (CRC ok)", r.valid);
  if (r.valid) {
    test::check("temperature plausible (-40..85 C)", r.temperatureC > -40 && r.temperatureC < 85);
    test::check("humidity in range (0..100 %)", r.humidityPct >= 0 && r.humidityPct <= 100);
  }
  test::summary();
  test::info("Printing every 2 s");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    printReading(Sht40Hardware::read());
  }
}
