#include "HallHardware.h"

#include "Pins.h"

namespace acorn {
namespace HallHardware {

namespace {
bool lastOpen = false;
bool openedEdge = false;
bool closedEdge = false;
}  // namespace

void begin() {
  pinMode(pins::MAG_OUT, INPUT);
  lastOpen = isDoorOpen();
  openedEdge = closedEdge = false;
}

bool isDoorOpen() { return digitalRead(pins::MAG_OUT) == level::DOOR_OPEN; }

void update() {
  const bool open = isDoorOpen();
  if (open != lastOpen) {
    if (open) openedEdge = true;
    else closedEdge = true;
    lastOpen = open;
  }
}

bool wasOpened() {
  const bool e = openedEdge;
  openedEdge = false;
  return e;
}

bool wasClosed() {
  const bool e = closedEdge;
  closedEdge = false;
  return e;
}

}  // namespace HallHardware
}  // namespace acorn
