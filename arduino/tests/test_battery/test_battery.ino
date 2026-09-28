// Isolated test: battery voltage on VBAT_ADC (GPIO1), 1M/249k divider.
// Compare the printed voltage with a multimeter at the battery terminals.
// Send "cal <measured_volts>" to compute a calibration factor for this unit.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

constexpr float PLAUSIBLE_MIN_V = 6.0f;
constexpr float PLAUSIBLE_MAX_V = 16.0f;

void printReading(const BatteryReading& r) {
  if (r.valid) {
    test::info("VBAT %.3f V (pin %u mV, cal %.4f)", r.voltage, r.adcMillivolts,
               BatteryHardware::calibration());
  } else {
    test::info("VBAT invalid (pin %u mV) -> BATTERY_READING_INVALID", r.adcMillivolts);
  }
}

void setup() {
  test::begin("test_battery");
  BatteryHardware::begin();
  test::info("Divider ratio %.4f", VBAT_DIVIDER_RATIO);

  const BatteryReading r = BatteryHardware::read(64);
  printReading(r);
  test::check("reading valid", r.valid);
  if (r.valid) {
    char detail[32];
    snprintf(detail, sizeof(detail), "%.2f V", r.voltage);
    test::check("voltage plausible for 12 V pack", r.voltage >= PLAUSIBLE_MIN_V && r.voltage <= PLAUSIBLE_MAX_V, detail);
  }
  test::summary();
  test::info("Printing every 2 s. Send 'cal <volts>' to calibrate.");
}

void loop() {
  String line;
  if (test::readLine(line) && line.startsWith("cal ")) {
    const float measured = line.substring(4).toFloat();
    BatteryHardware::setCalibration(1.0f);
    const BatteryReading raw = BatteryHardware::read(64);
    if (raw.valid && measured > 0) {
      BatteryHardware::setCalibration(measured / raw.voltage);
      test::info("Calibration factor %.4f (store per unit in config)", BatteryHardware::calibration());
    } else {
      test::info("Cannot calibrate: reading invalid or bad value");
    }
  }

  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    printReading(BatteryHardware::read());
  }
}
