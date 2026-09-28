// Isolated test: deep sleep with mixed-polarity wake sources.
//   EXT0: MAG_OUT HIGH (door opened), or LOW (door closed) if the door is
//         left open at sleep time
//   EXT1 ANY_LOW: BTN_01, BTN_02, RTC_MFP
//   Timer: 60 s fallback
// Each boot prints the wake source, blinks LED_G once per boot count, then
// sleeps after 10 s. Optionally arms an RTC alarm 20 s out (if RTC valid).
// Measure sleep current at the battery with the audio rail off.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

RTC_DATA_ATTR uint32_t bootCount = 0;

constexpr uint32_t AWAKE_MS = 10000;
constexpr uint64_t TIMER_US = 60ULL * 1000000ULL;

void setup() {
  bootCount++;
  test::begin("test_sleep_wake");
  LedHardware::begin();
  ButtonHardware::begin();
  HallHardware::begin();
  AudioHardware::begin();
  SdHardware::begin();

  const WakeSource src = SleepHardware::wakeSource();
  test::info("boot #%lu  wake=%s  reset=%s", static_cast<unsigned long>(bootCount), wakeSourceName(src),
             SystemHardware::resetReason());
  if (src == WakeSource::RtcAlarm) test::check("woke from RTC alarm (MFP LOW)", true);
  if (src == WakeSource::Hall) {
    // EXT0 fires on either edge depending on how it was armed; the level tells which.
    if (HallHardware::isDoorOpen()) test::check("woke from door open (MAG HIGH)", true);
    else test::check("woke from door close (MAG LOW, door had been left open)", true);
  }
  if (src == WakeSource::Button1 || src == WakeSource::Button2) test::check("woke from button (LOW)", true);
  if (src == WakeSource::Timer) test::check("woke from timer", true);

  LedHardware::blink(Led::Green, 120, 180, min<uint32_t>(bootCount, 10));

  const bool rtcOk = RtcHardware::begin();
  if (rtcOk) {
    RtcHardware::clearAlarmFlag();
    int64_t now;
    if (RtcHardware::nowEpoch(now) && RtcHardware::setAlarm(UtcTime::fromEpoch(now + 20))) {
      test::info("RTC alarm armed for +20 s");
    } else {
      test::info("RTC time invalid; RTC alarm wake not tested");
    }
  } else {
    test::info("RTC not found; RTC alarm wake not tested");
  }
  test::info("Sleeping in %lu s. Try: open door, press a button, or wait.", AWAKE_MS / 1000);
}

void loop() {
  LedHardware::update();
  if (millis() < AWAKE_MS) {
    delay(5);
    return;
  }

  LedHardware::prepareForSleep();
  AudioHardware::prepareForSleep();
  SdHardware::prepareForSleep();

  SleepConfig cfg;
  cfg.timerUs = TIMER_US;
  cfg.hallWakeOnClose = true;
  const SleepArmResult armed = SleepHardware::arm(cfg);
  test::info("armed: hall=%d hall_close=%d btn1=%d btn2=%d rtc=%d timer=%d", armed.hall, armed.hallClose,
             armed.button1, armed.button2, armed.rtcAlarm, armed.timer);
  if (armed.hallClose) test::info("door is open: HALL armed to wake on door close");
  if (armed.skippedHall) test::info("skipped HALL: door is open (would re-wake immediately)");
  if (armed.skippedButton1) test::info("skipped BTN_01: held");
  if (armed.skippedButton2) test::info("skipped BTN_02: held");
  if (armed.skippedRtcAlarm) test::info("skipped RTC: MFP asserted (alarm flag not cleared)");
  test::info("Entering deep sleep");
  SleepHardware::enter(cfg);
  test::info("Nothing armed; not sleeping");
  delay(5000);
}
