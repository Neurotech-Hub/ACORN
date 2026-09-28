// Isolated test: LED_R (GPIO2, D3+D4) and LED_G (GPIO48, D2).
// Visual check: each LED lights alone, then both blink via the non-blocking
// helper. If an LED is lit when reported OFF, flip level::LED_ON in Pins.h.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

void showAlone(Led led, const char* name) {
  LedHardware::allOff();
  LedHardware::set(led, true);
  test::info("%s should be ON, the other OFF (2 s)", name);
  test::check("state reports ON", LedHardware::isOn(led));
  delay(2000);
  LedHardware::set(led, false);
  test::check("state reports OFF", !LedHardware::isOn(led));
  delay(500);
}

void setup() {
  test::begin("test_leds");
  LedHardware::begin();
  test::check("both LEDs start OFF", !LedHardware::isOn(Led::Red) && !LedHardware::isOn(Led::Green));

  showAlone(Led::Red, "LED_R");
  showAlone(Led::Green, "LED_G");

  test::info("Non-blocking blink: LED_R x5, LED_G x3");
  LedHardware::blink(Led::Red, 150, 250, 5);
  LedHardware::blink(Led::Green, 300, 300, 3);
  const uint32_t start = millis();
  while (LedHardware::isBlinking() && millis() - start < 5000) LedHardware::update();
  test::check("blink sequence completes", !LedHardware::isBlinking());
  test::check("LEDs OFF after blink", !LedHardware::isOn(Led::Red) && !LedHardware::isOn(Led::Green));

  test::summary();
  test::info("Looping: alternating LEDs every 1 s");
}

void loop() {
  static bool red = false;
  red = !red;
  LedHardware::set(Led::Red, red);
  LedHardware::set(Led::Green, !red);
  delay(1000);
}
