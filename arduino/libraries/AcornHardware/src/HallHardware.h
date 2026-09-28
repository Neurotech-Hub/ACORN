#pragma once

#include <Arduino.h>

namespace acorn {

// DRV5032FA on MAG_OUT (GPIO11), push-pull, HIGH = enclosure door open.
// Configuration-AP policy lives outside the HAL (acorn/src ConfigSession).
namespace HallHardware {

void begin();
bool isDoorOpen();

// Edge detection; call update() from loop().
void update();
bool wasOpened();
bool wasClosed();

}  // namespace HallHardware
}  // namespace acorn
