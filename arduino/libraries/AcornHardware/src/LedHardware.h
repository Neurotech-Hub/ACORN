#pragma once

#include <Arduino.h>

namespace acorn {

enum class Led : uint8_t { Red, Green };

// LED_R (GPIO2, D3+D4) and LED_G (GPIO48, D2). Polarity lives in Pins.h.
namespace LedHardware {

void begin();
void set(Led led, bool on);
bool isOn(Led led);
void allOff();

// Non-blocking blink helper; call update() from loop().
void blink(Led led, uint16_t onMs, uint16_t offMs, uint8_t count);
void update();
bool isBlinking();

// Both LEDs OFF and latched for deep sleep.
void prepareForSleep();

}  // namespace LedHardware
}  // namespace acorn
