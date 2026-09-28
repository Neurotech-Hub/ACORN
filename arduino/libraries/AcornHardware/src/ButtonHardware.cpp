#include "ButtonHardware.h"

#include "Pins.h"

namespace acorn {
namespace ButtonHardware {

namespace {

struct State {
  bool raw = false;
  bool stable = false;
  bool pressEdge = false;
  bool releaseEdge = false;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
};

State states[2];

uint8_t pinFor(Button b) { return b == Button::Audio ? pins::BTN_01 : pins::BTN_02; }
State& st(Button b) { return states[static_cast<uint8_t>(b)]; }

}  // namespace

void begin() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    const Button b = static_cast<Button>(i);
    pinMode(pinFor(b), INPUT);
    State& s = st(b);
    s = State{};
    s.raw = s.stable = readRaw(b);
    s.rawChangedAt = now;
    if (s.stable) s.pressedAt = now;
  }
}

bool readRaw(Button button) {
  return digitalRead(pinFor(button)) == level::BUTTON_PRESSED;
}

void update() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    const Button b = static_cast<Button>(i);
    State& s = st(b);
    const bool raw = readRaw(b);
    if (raw != s.raw) {
      s.raw = raw;
      s.rawChangedAt = now;
    }
    if (s.raw != s.stable && now - s.rawChangedAt >= DEBOUNCE_MS) {
      s.stable = s.raw;
      if (s.stable) {
        s.pressEdge = true;
        s.pressedAt = now;
      } else {
        s.releaseEdge = true;
      }
    }
  }
}

bool isPressed(Button button) { return st(button).stable; }

bool wasPressed(Button button) {
  State& s = st(button);
  const bool edge = s.pressEdge;
  s.pressEdge = false;
  return edge;
}

bool wasReleased(Button button) {
  State& s = st(button);
  const bool edge = s.releaseEdge;
  s.releaseEdge = false;
  return edge;
}

uint32_t heldMs(Button button) {
  const State& s = st(button);
  return s.stable ? millis() - s.pressedAt : 0;
}

}  // namespace ButtonHardware
}  // namespace acorn
