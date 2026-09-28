#pragma once

#include <Arduino.h>

namespace acorn {

enum class Button : uint8_t { Audio /* BTN_01 */, Battery /* BTN_02 */ };

// BTN_01 (GPIO7) and BTN_02 (GPIO8), LOW = pressed, hardware pull-ups.
// Polled with debounce; call update() from loop().
namespace ButtonHardware {

constexpr uint16_t DEBOUNCE_MS = 30;

void begin();
void update();
bool isPressed(Button button);       // debounced level
bool wasPressed(Button button);      // press edge since last call
bool wasReleased(Button button);     // release edge since last call
uint32_t heldMs(Button button);      // 0 when not pressed
bool readRaw(Button button);         // undebounced, true = pressed

}  // namespace ButtonHardware
}  // namespace acorn
