#include "SleepHardware.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "Pins.h"

namespace acorn {

const char* wakeSourceName(WakeSource source) {
  switch (source) {
    case WakeSource::PowerOn: return "POWER_ON";
    case WakeSource::Hall: return "HALL";
    case WakeSource::Button1: return "BTN_01";
    case WakeSource::Button2: return "BTN_02";
    case WakeSource::RtcAlarm: return "RTC_ALARM";
    case WakeSource::Timer: return "TIMER";
    case WakeSource::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

namespace SleepHardware {

namespace {

constexpr uint64_t pinMask(uint8_t pin) { return 1ULL << pin; }

void floatingRtcInput(uint8_t pin) {
  const gpio_num_t g = static_cast<gpio_num_t>(pin);
  rtc_gpio_pullup_dis(g);
  rtc_gpio_pulldown_dis(g);
}

}  // namespace

WakeSource wakeSource() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeSource::PowerOn;
    case ESP_SLEEP_WAKEUP_EXT0: return WakeSource::Hall;
    case ESP_SLEEP_WAKEUP_TIMER: return WakeSource::Timer;
    case ESP_SLEEP_WAKEUP_EXT1: {
      const uint64_t status = esp_sleep_get_ext1_wakeup_status();
      if (status & pinMask(pins::RTC_MFP)) return WakeSource::RtcAlarm;
      if (status & pinMask(pins::BTN_01)) return WakeSource::Button1;
      if (status & pinMask(pins::BTN_02)) return WakeSource::Button2;
      return WakeSource::Unknown;
    }
    default: return WakeSource::Unknown;
  }
}

SleepArmResult arm(const SleepConfig& config) {
  SleepArmResult r;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (uint8_t pin : {pins::MAG_OUT, pins::BTN_01, pins::BTN_02, pins::RTC_MFP}) pinMode(pin, INPUT);

  if (config.wakeOnHall) {
    if (digitalRead(pins::MAG_OUT) == level::DOOR_OPEN) {
      if (config.hallWakeOnClose) {
        floatingRtcInput(pins::MAG_OUT);
        r.hallClose = esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(pins::MAG_OUT),
                                                   !level::DOOR_OPEN) == ESP_OK;
      } else {
        r.skippedHall = true;
      }
    } else {
      floatingRtcInput(pins::MAG_OUT);
      r.hall = esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(pins::MAG_OUT),
                                            level::DOOR_OPEN) == ESP_OK;
    }
  }

  uint64_t mask = 0;
  auto addLow = [&](bool want, uint8_t pin, uint8_t asserted, bool& armed, bool& skipped) {
    if (!want) return;
    if (digitalRead(pin) == asserted) {
      skipped = true;
      return;
    }
    floatingRtcInput(pin);
    mask |= pinMask(pin);
    armed = true;
  };
  addLow(config.wakeOnButtons, pins::BTN_01, level::BUTTON_PRESSED, r.button1, r.skippedButton1);
  addLow(config.wakeOnButtons, pins::BTN_02, level::BUTTON_PRESSED, r.button2, r.skippedButton2);
  addLow(config.wakeOnRtcAlarm, pins::RTC_MFP, level::RTC_MFP_ASSERTED, r.rtcAlarm,
         r.skippedRtcAlarm);
  if (mask && esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) {
    r.button1 = r.button2 = r.rtcAlarm = false;
  }

  if (config.timerUs) {
    r.timer = esp_sleep_enable_timer_wakeup(config.timerUs) == ESP_OK;
  }
  return r;
}

SleepArmResult enter(const SleepConfig& config) {
  const SleepArmResult r = arm(config);
  if (!r.anyArmed()) return r;
  gpio_deep_sleep_hold_en();
  Serial.flush();
  esp_deep_sleep_start();
  return r;  // not reached
}

}  // namespace SleepHardware
}  // namespace acorn
