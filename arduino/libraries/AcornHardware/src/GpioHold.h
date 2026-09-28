#pragma once

#include <Arduino.h>

namespace acorn {

// Drive an output and latch it so it survives deep sleep.
// SleepHardware::enter() enables the global deep-sleep hold.
void holdOutput(uint8_t pin, uint8_t level);

// Configure an output at `level` and release any hold left from deep sleep.
// The level is applied before releasing so the pin never glitches.
void releaseOutput(uint8_t pin, uint8_t level);

}  // namespace acorn
