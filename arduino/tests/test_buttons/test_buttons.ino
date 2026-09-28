// Isolated test: BTN_01 (GPIO7) and BTN_02 (GPIO8), LOW = pressed.
// Press and release each button once; events are printed with hold time.
// LED_G mirrors BTN_01 and LED_R mirrors BTN_02.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

bool seenPress[2] = {false, false};
bool seenRelease[2] = {false, false};
bool reported = false;

void setup() {
  test::begin("test_buttons");
  LedHardware::begin();
  ButtonHardware::begin();
  test::check("BTN_01 idle (not pressed at boot)", !ButtonHardware::readRaw(Button::Audio));
  test::check("BTN_02 idle (not pressed at boot)", !ButtonHardware::readRaw(Button::Battery));
  test::info("Press and release BTN_01 and BTN_02");
}

void loop() {
  ButtonHardware::update();
  const Button buttons[2] = {Button::Audio, Button::Battery};
  const char* names[2] = {"BTN_01", "BTN_02"};
  for (uint8_t i = 0; i < 2; i++) {
    const Button b = buttons[i];
    if (ButtonHardware::wasPressed(b)) {
      seenPress[i] = true;
      test::info("%s pressed", names[i]);
    }
    if (ButtonHardware::wasReleased(b)) {
      seenRelease[i] = true;
      test::info("%s released", names[i]);
    }
  }
  LedHardware::set(Led::Green, ButtonHardware::isPressed(Button::Audio));
  LedHardware::set(Led::Red, ButtonHardware::isPressed(Button::Battery));

  static uint32_t lastHeld = 0;
  if (millis() - lastHeld > 500) {
    lastHeld = millis();
    for (uint8_t i = 0; i < 2; i++) {
      const uint32_t held = ButtonHardware::heldMs(buttons[i]);
      if (held > 1000) test::info("%s held %lu ms", names[i], static_cast<unsigned long>(held));
    }
  }

  if (!reported && seenPress[0] && seenRelease[0] && seenPress[1] && seenRelease[1]) {
    reported = true;
    test::check("BTN_01 press + release detected", true);
    test::check("BTN_02 press + release detected", true);
    test::summary();
    test::info("Continuing to print events");
  }
  delay(2);
}
