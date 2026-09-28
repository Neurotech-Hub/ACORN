// Isolated test: MCP7940N RTC (0x6F) in UTC, alarm 0 -> MFP (GPIO6, LOW).
// 1. Pure UTC conversion checks (no hardware).
// 2. RTC presence, oscillator, validity, ticking.
// 3. Alarm: MFP asserts ~5 s after arming, then releases when cleared.
// If the time is not set, send "set YYYY-MM-DDTHH:MM:SSZ" (UTC) and reset.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

void checkEpoch(const char* iso, int64_t expected, uint8_t isoWeekday) {
  UtcDateTime t;
  const bool parsed = UtcTime::parseIso(iso, t);
  char detail[48];
  snprintf(detail, sizeof(detail), "%s", iso);
  test::check("parse ISO", parsed, detail);
  if (!parsed) return;
  test::check("toEpoch", UtcTime::toEpoch(t) == expected, detail);
  const UtcDateTime back = UtcTime::fromEpoch(expected);
  char out[24];
  UtcTime::formatIso(back, out, sizeof(out));
  test::check("fromEpoch round trip", strcmp(out, iso) == 0, out);
  test::check("ISO weekday", UtcTime::weekday(t) == isoWeekday, detail);
}

void softwareTests() {
  checkEpoch("2000-01-01T00:00:00Z", 946684800LL, 6);
  checkEpoch("2024-02-29T12:34:56Z", 1709210096LL, 4);
  checkEpoch("2026-09-27T06:00:00Z", 1790488800LL, 7);
  checkEpoch("2099-12-31T23:59:59Z", 4102444799LL, 4);
  UtcDateTime t;
  test::check("rejects 2025-02-29", !UtcTime::parseIso("2025-02-29T00:00:00Z", t));
  test::check("rejects 24:00", !UtcTime::parseIso("2026-01-01T24:00:00Z", t));
  test::check("rejects local offset", !UtcTime::parseIso("2026-01-01T00:00:00+02:00", t));
}

void printNow() {
  UtcDateTime t;
  if (RtcHardware::now(t)) {
    char iso[24];
    UtcTime::formatIso(t, iso, sizeof(iso));
    test::info("RTC now %s (epoch %lld)", iso, static_cast<long long>(UtcTime::toEpoch(t)));
  } else {
    test::info("RTC time invalid -> RTC_INVALID / TIME_NOT_SET");
  }
}

void alarmTest() {
  int64_t now;
  if (!RtcHardware::nowEpoch(now)) return;
  RtcHardware::clearAlarmFlag();
  test::check("MFP released before arming", !RtcHardware::isMfpAsserted());
  test::check("alarm set (now + 5 s)", RtcHardware::setAlarm(UtcTime::fromEpoch(now + 5)));
  const uint32_t start = millis();
  bool asserted = false;
  while (millis() - start < 8000) {
    if (RtcHardware::isMfpAsserted()) {
      asserted = true;
      break;
    }
    delay(20);
  }
  char detail[24];
  snprintf(detail, sizeof(detail), "%lu ms", static_cast<unsigned long>(millis() - start));
  test::check("MFP asserted LOW by alarm", asserted, detail);
  test::check("ALM0IF flag set", RtcHardware::alarmFired());
  RtcHardware::disableAlarm();
  delay(5);
  test::check("MFP released after clear", !RtcHardware::isMfpAsserted());
  test::check("ALM0IF cleared", !RtcHardware::alarmFired());
}

void setup() {
  test::begin("test_rtc");
  softwareTests();

  if (!test::check("MCP7940N present + configured", RtcHardware::begin())) {
    test::summary();
    return;
  }
  const RtcStatus s = RtcHardware::status();
  test::info("oscillator=%d timeSet=%d powerFailed=%d valid=%d", s.oscillatorRunning, s.timeSet,
             s.powerFailed, s.valid);
  if (!s.valid) {
    test::info("Time not set. Send: set YYYY-MM-DDTHH:MM:SSZ (UTC), then reset.");
    test::summary();
    return;
  }
  if (s.powerFailed) test::info("Main power was lost; backup kept time. Clearing PWRFAIL.");
  RtcHardware::clearPowerFail();

  int64_t a, b;
  RtcHardware::nowEpoch(a);
  printNow();
  delay(2100);
  RtcHardware::nowEpoch(b);
  char detail[24];
  snprintf(detail, sizeof(detail), "delta %lld s", static_cast<long long>(b - a));
  test::check("RTC ticking", b - a >= 1 && b - a <= 3, detail);

  alarmTest();
  test::summary();
  test::info("Printing time every 5 s. Send 'set <iso>' to set UTC.");
}

void loop() {
  String line;
  if (test::readLine(line) && line.startsWith("set ")) {
    UtcDateTime t;
    if (UtcTime::parseIso(line.substring(4).c_str(), t) && RtcHardware::setTime(t)) {
      test::info("RTC set");
      printNow();
    } else {
      test::info("Bad time or write failed. Format: set 2026-09-27T06:00:00Z");
    }
  }
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    printNow();
  }
}
