// Combined bring-up: LEDs, buttons, Hall, battery, RTC, SHT40, light, SD,
// and a 5 s RTC deep-sleep wake.
// Cold boot runs the checks, then:
//   BTN_01 toggles LED_R and LED_G together
//   BTN_02 press, then release, sleeps until an RTC alarm 5 s out
//         (Hall and the buttons are not armed)
// Commands: "cal <volts>" and "set YYYY-MM-DDTHH:MM:SSZ" (UTC).

#include <AcornHardware.h>
#include <AcornTest.h>

#include <cstring>

using namespace acorn;

RTC_DATA_ATTR uint32_t bootCount = 0;

constexpr float PLAUSIBLE_MIN_V = 6.0f;
constexpr float PLAUSIBLE_MAX_V = 16.0f;
constexpr const char* SD_TEST_PATH = "/.acorn_sd_test.txt";

bool seenPress[2] = {false, false};
bool seenRelease[2] = {false, false};
bool buttonsReported = false;
bool doorOpened = false;
bool doorClosed = false;
bool doorReported = false;
bool ledsOn = false;
bool sleepOnRelease = false;
bool sdWatchReady = false;
bool sdLastPresent = false;

void showLedAlone(Led led, const char* name) {
  LedHardware::allOff();
  LedHardware::set(led, true);
  test::info("%s should be ON, the other OFF (2 s)", name);
  test::check("state reports ON", LedHardware::isOn(led));
  delay(2000);
  LedHardware::set(led, false);
  test::check("state reports OFF", !LedHardware::isOn(led));
  delay(500);
}

void runLedChecks() {
  test::check("both LEDs start OFF", !LedHardware::isOn(Led::Red) && !LedHardware::isOn(Led::Green));
  showLedAlone(Led::Red, "LED_R");
  showLedAlone(Led::Green, "LED_G");

  test::info("Non-blocking blink: LED_R x5, LED_G x3");
  LedHardware::blink(Led::Red, 150, 250, 5);
  LedHardware::blink(Led::Green, 300, 300, 3);
  const uint32_t start = millis();
  while (LedHardware::isBlinking() && millis() - start < 5000) LedHardware::update();
  test::check("blink sequence completes", !LedHardware::isBlinking());
  test::check("LEDs OFF after blink", !LedHardware::isOn(Led::Red) && !LedHardware::isOn(Led::Green));
}

void printBattery(const BatteryReading& r) {
  if (r.valid) {
    test::info("VBAT %.3f V (pin %u mV, cal %.4f)", r.voltage, r.adcMillivolts,
               BatteryHardware::calibration());
  } else {
    test::info("VBAT invalid (pin %u mV) -> BATTERY_READING_INVALID", r.adcMillivolts);
  }
}

void runBatteryCheck() {
  test::info("Divider ratio %.4f", VBAT_DIVIDER_RATIO);
  const BatteryReading r = BatteryHardware::read(64);
  printBattery(r);
  test::check("battery reading valid", r.valid);
  if (r.valid) {
    char detail[32];
    snprintf(detail, sizeof(detail), "%.2f V", r.voltage);
    test::check("voltage plausible for 12 V pack",
                r.voltage >= PLAUSIBLE_MIN_V && r.voltage <= PLAUSIBLE_MAX_V, detail);
  }
}

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

void runRtcSoftwareChecks() {
  checkEpoch("2000-01-01T00:00:00Z", 946684800LL, 6);
  checkEpoch("2024-02-29T12:34:56Z", 1709210096LL, 4);
  checkEpoch("2026-09-27T06:00:00Z", 1790488800LL, 7);
  checkEpoch("2099-12-31T23:59:59Z", 4102444799LL, 4);
  UtcDateTime t;
  test::check("rejects 2025-02-29", !UtcTime::parseIso("2025-02-29T00:00:00Z", t));
  test::check("rejects 24:00", !UtcTime::parseIso("2026-01-01T24:00:00Z", t));
  test::check("rejects local offset", !UtcTime::parseIso("2026-01-01T00:00:00+02:00", t));
}

void printRtcNow() {
  UtcDateTime t;
  if (RtcHardware::now(t)) {
    char iso[24];
    UtcTime::formatIso(t, iso, sizeof(iso));
    test::info("RTC now %s (epoch %lld)", iso, static_cast<long long>(UtcTime::toEpoch(t)));
  } else {
    test::info("RTC time invalid -> RTC_INVALID / TIME_NOT_SET");
  }
}

void runRtcAlarmCheck() {
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

void runRtcChecks() {
  runRtcSoftwareChecks();
  if (!test::check("MCP7940N present + configured", RtcHardware::begin())) return;

  const RtcStatus s = RtcHardware::status();
  test::info("oscillator=%d timeSet=%d powerFailed=%d valid=%d", s.oscillatorRunning, s.timeSet,
             s.powerFailed, s.valid);
  if (!s.valid) {
    test::info("Time not set. Send: set YYYY-MM-DDTHH:MM:SSZ (UTC)");
    return;
  }
  if (s.powerFailed) test::info("Main power was lost; backup kept time. Clearing PWRFAIL.");
  RtcHardware::clearPowerFail();

  int64_t a, b;
  RtcHardware::nowEpoch(a);
  printRtcNow();
  delay(2100);
  RtcHardware::nowEpoch(b);
  char detail[24];
  snprintf(detail, sizeof(detail), "delta %lld s", static_cast<long long>(b - a));
  test::check("RTC ticking", b - a >= 1 && b - a <= 3, detail);
  runRtcAlarmCheck();
}

void runSht40Checks() {
  const uint8_t sample[2] = {0xBE, 0xEF};
  test::check("CRC-8 datasheet vector (0xBEEF -> 0x92)", Sht40Hardware::crc8(sample, 2) == 0x92);
  if (!test::check("SHT40 present + soft reset", Sht40Hardware::begin())) return;

  uint32_t serial;
  if (test::check("serial number read (CRC ok)", Sht40Hardware::readSerial(serial))) {
    test::info("serial 0x%08lX", static_cast<unsigned long>(serial));
  }
  const EnvReading r = Sht40Hardware::read();
  if (r.valid) test::info("T %.2f C  RH %.1f %%", r.temperatureC, r.humidityPct);
  else test::info("reading invalid -> SENSOR_READ_FAILED");
  test::check("measurement valid (CRC ok)", r.valid);
  if (r.valid) {
    test::check("temperature plausible (-40..85 C)", r.temperatureC > -40 && r.temperatureC < 85);
    test::check("humidity in range (0..100 %)", r.humidityPct >= 0 && r.humidityPct <= 100);
  }
}

void runLightChecks() {
  uint16_t id = 0;
  const bool idOk = I2cHardware::begin() && LightHardware::readId(id);
  char detail[16];
  snprintf(detail, sizeof(detail), "0x%04X", id);
  test::check("device ID read", idOk, detail);
  if (!test::check("VEML6031X00 present + ID 0x0001", LightHardware::begin())) return;

  const LightReading r = LightHardware::read();
  if (r.valid) test::info("%.2f lx  (ALS %u, IR %u, range %u)", r.lux, r.rawAls, r.rawIr, r.range);
  else test::info("reading invalid -> SENSOR_READ_FAILED (ALS %u, range %u)", r.rawAls, r.range);
  test::check("light measurement valid", r.valid);
}

void listWavs(const char* dirPath) {
  File dir = SdHardware::fs().open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    String lower = name;
    lower.toLowerCase();
    if (!f.isDirectory() && lower.endsWith(".wav")) {
      WavInfo info;
      const WavStatus s = WavReader::open(f, info);
      if (s == WavStatus::Ok) {
        test::info("%s/%s  %lu Hz, %lu ms", dirPath, name.c_str(), static_cast<unsigned long>(info.sampleRate),
                   static_cast<unsigned long>(info.durationMs));
      } else {
        test::info("%s/%s  %s (fmt %u, ch %u, %lu Hz, %u bit)", dirPath, name.c_str(), wavStatusCode(s),
                   info.format, info.channels, static_cast<unsigned long>(info.sampleRate), info.bitsPerSample);
      }
    }
    f.close();
  }
}

void runCardTests() {
  const SdStatus m = SdHardware::mount();
  if (!test::check("mount", m == SdStatus::Ok, sdStatusCode(m))) return;

  const SdInfo i = SdHardware::info();
  const char* types[] = {"NONE", "MMC", "SD", "SDHC", "UNKNOWN"};
  test::info("type %s, %llu MB total, %llu MB used", types[min<uint8_t>(i.cardType, 4)],
             i.totalBytes / (1024ULL * 1024ULL), i.usedBytes / (1024ULL * 1024ULL));
  const SdStatus w = SdHardware::checkWritable();
  test::check("free space", w == SdStatus::Ok, sdStatusCode(w));

  const char* payload = "ACORN SD round trip 0123456789";
  File f = SdHardware::fs().open(SD_TEST_PATH, FILE_WRITE);
  const bool wrote = f && f.print(payload) == strlen(payload);
  if (f) f.close();
  test::check("write file", wrote, wrote ? nullptr : "SD_WRITE_FAILED");

  f = SdHardware::fs().open(SD_TEST_PATH, FILE_READ);
  String back = f ? f.readString() : String();
  if (f) f.close();
  test::check("read back matches", back == payload);
  test::check("delete file", SdHardware::fs().remove(SD_TEST_PATH));

  listWavs("/");
  listWavs("/audio");
}

void runSdChecks() {
  const bool present = SdHardware::isCardPresent();
  test::info("SD_DETECT: card %s", present ? "inserted" : "not inserted");
  if (!test::check("card present", present, present ? nullptr : "SD_NOT_PRESENT")) {
    test::check("mount reports SD_NOT_PRESENT", SdHardware::mount() == SdStatus::NotPresent);
  } else {
    runCardTests();
  }
}

void enterRtcSleep() {
  if (!RtcHardware::begin()) {
    test::info("RTC not found; not sleeping");
    return;
  }
  RtcHardware::clearAlarmFlag();
  int64_t now;
  if (!RtcHardware::nowEpoch(now) || !RtcHardware::setAlarm(UtcTime::fromEpoch(now + 5))) {
    test::info("RTC time invalid or alarm failed; not sleeping. Send: set YYYY-MM-DDTHH:MM:SSZ");
    return;
  }
  test::info("Entering deep sleep; RTC wake in 5 s (Hall and buttons not armed)");
  LedHardware::prepareForSleep();
  AudioHardware::prepareForSleep();
  SdHardware::prepareForSleep();

  SleepConfig cfg;
  cfg.wakeOnHall = false;
  cfg.wakeOnButtons = false;
  cfg.wakeOnRtcAlarm = true;
  cfg.timerUs = 0;
  const SleepArmResult armed = SleepHardware::enter(cfg);
  test::info("Nothing armed (rtc=%d skipped_rtc=%d); not sleeping", armed.rtcAlarm, armed.skippedRtcAlarm);
}

void reportButtons() {
  if (buttonsReported || !seenPress[0] || !seenRelease[0] || !seenPress[1] || !seenRelease[1]) return;
  buttonsReported = true;
  test::check("BTN_01 press + release detected", true);
  test::check("BTN_02 press + release detected", true);
}

void watchHall() {
  HallHardware::update();
  if (HallHardware::wasOpened()) {
    doorOpened = true;
    test::info("Door OPENED (MAG_OUT HIGH)");
  }
  if (HallHardware::wasClosed()) {
    doorClosed = true;
    test::info("Door CLOSED (MAG_OUT LOW)");
  }
  if (!doorReported && doorOpened && doorClosed) {
    doorReported = true;
    test::check("door open edge detected", true);
    test::check("door close edge detected", true);
  }
}

void watchSd() {
  const bool now = SdHardware::isCardPresent();
  if (!sdWatchReady) {
    sdWatchReady = true;
    sdLastPresent = now;
    return;
  }
  if (now == sdLastPresent) return;
  sdLastPresent = now;
  test::info("card %s", now ? "INSERTED" : "REMOVED");
  if (now) {
    delay(300);
    runCardTests();
  } else {
    SdHardware::unmount();
  }
}

void handleSerial() {
  String line;
  if (!test::readLine(line)) return;

  if (line.startsWith("cal ")) {
    const float measured = line.substring(4).toFloat();
    BatteryHardware::setCalibration(1.0f);
    const BatteryReading raw = BatteryHardware::read(64);
    if (raw.valid && measured > 0) {
      BatteryHardware::setCalibration(measured / raw.voltage);
      test::info("Calibration factor %.4f (store per unit in config)", BatteryHardware::calibration());
    } else {
      test::info("Cannot calibrate: reading invalid or bad value");
    }
    return;
  }

  if (line.startsWith("set ")) {
    UtcDateTime t;
    if (UtcTime::parseIso(line.substring(4).c_str(), t) && RtcHardware::setTime(t)) {
      test::info("RTC set");
      printRtcNow();
    } else {
      test::info("Bad time or write failed. Format: set 2026-09-27T06:00:00Z");
    }
  }
}

void printStatus() {
  static uint32_t last = 0;
  if (millis() - last < 2000) return;
  last = millis();

  const BatteryReading b = BatteryHardware::read();
  const EnvReading e = Sht40Hardware::read();
  const LightReading l = LightHardware::read();

  char vbat[16], temp[16], rh[16], lux[16], iso[24];
  if (b.valid) snprintf(vbat, sizeof(vbat), "%.2f V", b.voltage);
  else snprintf(vbat, sizeof(vbat), "invalid");
  if (e.valid) {
    snprintf(temp, sizeof(temp), "%.1f C", e.temperatureC);
    snprintf(rh, sizeof(rh), "%.0f %%", e.humidityPct);
  } else {
    snprintf(temp, sizeof(temp), "invalid");
    snprintf(rh, sizeof(rh), "invalid");
  }
  if (l.valid) snprintf(lux, sizeof(lux), "%.1f lx", l.lux);
  else snprintf(lux, sizeof(lux), "invalid");

  UtcDateTime t;
  if (RtcHardware::now(t)) UtcTime::formatIso(t, iso, sizeof(iso));
  else snprintf(iso, sizeof(iso), "invalid");

  test::info("VBAT %s  T %s  RH %s  %s  %s  door %s  SD %s", vbat, temp, rh, lux, iso,
             HallHardware::isDoorOpen() ? "OPEN" : "CLOSED", SdHardware::isCardPresent() ? "in" : "out");
}

void printOperatorHints() {
  test::info("BTN_01 toggles both LEDs. BTN_02: press, then release to sleep 5 s (RTC only).");
  test::info("Commands: cal <volts>    set YYYY-MM-DDTHH:MM:SSZ");
  test::info("Open and close the door. Insert or remove the SD card.");
}

void setup() {
  bootCount++;
  test::begin("test_hardware");
  LedHardware::begin();
  ButtonHardware::begin();
  HallHardware::begin();
  BatteryHardware::begin();
  AudioHardware::begin();
  SdHardware::begin();

  const WakeSource src = SleepHardware::wakeSource();
  if (src != WakeSource::PowerOn) {
    test::info("boot #%lu  wake=%s  reset=%s", static_cast<unsigned long>(bootCount), wakeSourceName(src),
               SystemHardware::resetReason());
    test::check("woke from RTC alarm (MFP LOW)", src == WakeSource::RtcAlarm);
    if (RtcHardware::begin()) RtcHardware::clearAlarmFlag();
    Sht40Hardware::begin();
    LightHardware::begin();
    printOperatorHints();
    return;
  }

  test::check("BTN_01 idle (not pressed at boot)", !ButtonHardware::readRaw(Button::Audio));
  test::check("BTN_02 idle (not pressed at boot)", !ButtonHardware::readRaw(Button::Battery));
  test::info("Door is %s at boot", HallHardware::isDoorOpen() ? "OPEN" : "CLOSED");

  runLedChecks();
  runBatteryCheck();
  runRtcChecks();
  runSht40Checks();
  runLightChecks();
  runSdChecks();
  test::summary();
  printOperatorHints();
}

void loop() {
  ButtonHardware::update();

  if (ButtonHardware::wasPressed(Button::Audio)) {
    seenPress[0] = true;
    ledsOn = !ledsOn;
    LedHardware::set(Led::Red, ledsOn);
    LedHardware::set(Led::Green, ledsOn);
    test::info("BTN_01 pressed; LEDs %s", ledsOn ? "ON" : "OFF");
  }
  if (ButtonHardware::wasReleased(Button::Audio)) {
    seenRelease[0] = true;
    test::info("BTN_01 released");
  }
  if (ButtonHardware::wasPressed(Button::Battery)) {
    seenPress[1] = true;
    sleepOnRelease = true;
    test::info("BTN_02 pressed; release to sleep (RTC +5 s)");
  }
  if (ButtonHardware::wasReleased(Button::Battery)) {
    seenRelease[1] = true;
    test::info("BTN_02 released");
    if (sleepOnRelease) {
      sleepOnRelease = false;
      reportButtons();
      enterRtcSleep();
    }
  }
  reportButtons();

  watchHall();
  watchSd();
  handleSerial();
  printStatus();
  delay(5);
}
