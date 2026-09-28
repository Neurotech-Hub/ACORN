// Isolated test: DRV5032FA Hall sensor on MAG_OUT (GPIO11), HIGH = door open.
// Open and close the enclosure door (or move a magnet) once. LED_G = open.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

bool opened = false;
bool closed = false;
bool reported = false;

void setup() {
  test::begin("test_hall");
  LedHardware::begin();
  HallHardware::begin();
  test::info("Door is %s at boot", HallHardware::isDoorOpen() ? "OPEN" : "CLOSED");
  test::info("Open and close the door (move the magnet away and back)");
}

void loop() {
  HallHardware::update();
  if (HallHardware::wasOpened()) {
    opened = true;
    test::info("Door OPENED (MAG_OUT HIGH)");
  }
  if (HallHardware::wasClosed()) {
    closed = true;
    test::info("Door CLOSED (MAG_OUT LOW)");
  }
  LedHardware::set(Led::Green, HallHardware::isDoorOpen());

  if (!reported && opened && closed) {
    reported = true;
    test::check("door open edge detected", true);
    test::check("door close edge detected", true);
    test::summary();
  }
  delay(5);
}
