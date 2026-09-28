#include "LedHardware.h"

#include "GpioHold.h"
#include "Pins.h"

namespace acorn {
namespace LedHardware {

namespace {

struct Blink {
  uint16_t onMs = 0;
  uint16_t offMs = 0;
  uint8_t remaining = 0;
  bool phaseOn = false;
  uint32_t changedAt = 0;
};

bool state[2] = {false, false};
Blink blinks[2];

uint8_t pinFor(Led led) { return led == Led::Red ? pins::LED_R : pins::LED_G; }
uint8_t idx(Led led) { return static_cast<uint8_t>(led); }

void write(Led led, bool on) {
  state[idx(led)] = on;
  digitalWrite(pinFor(led), on ? level::LED_ON : level::LED_OFF);
}

}  // namespace

void begin() {
  releaseOutput(pins::LED_R, level::LED_OFF);
  releaseOutput(pins::LED_G, level::LED_OFF);
  state[0] = state[1] = false;
  blinks[0] = blinks[1] = Blink{};
}

void set(Led led, bool on) {
  blinks[idx(led)].remaining = 0;
  write(led, on);
}

bool isOn(Led led) { return state[idx(led)]; }

void allOff() {
  set(Led::Red, false);
  set(Led::Green, false);
}

void blink(Led led, uint16_t onMs, uint16_t offMs, uint8_t count) {
  Blink& b = blinks[idx(led)];
  b.onMs = onMs;
  b.offMs = offMs;
  b.remaining = count;
  b.phaseOn = count > 0;
  b.changedAt = millis();
  write(led, b.phaseOn);
}

void update() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    Blink& b = blinks[i];
    if (!b.remaining) continue;
    const Led led = static_cast<Led>(i);
    if (b.phaseOn && now - b.changedAt >= b.onMs) {
      b.phaseOn = false;
      b.changedAt = now;
      write(led, false);
      if (--b.remaining == 0) continue;
    } else if (!b.phaseOn && now - b.changedAt >= b.offMs) {
      b.phaseOn = true;
      b.changedAt = now;
      write(led, true);
    }
  }
}

bool isBlinking() { return blinks[0].remaining || blinks[1].remaining; }

void prepareForSleep() {
  blinks[0].remaining = blinks[1].remaining = 0;
  state[0] = state[1] = false;
  holdOutput(pins::LED_R, level::LED_OFF);
  holdOutput(pins::LED_G, level::LED_OFF);
}

}  // namespace LedHardware
}  // namespace acorn
