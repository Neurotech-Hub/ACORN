#pragma once

// Critical-battery gate. Near the bottom of the pack, a current pulse (amp,
// radio, SD write) can brown the MCU out in an undefined way, so below the
// gate the firmware shuts the heavy loads off and sleeps instead.
//
// The two thresholds give hysteresis: the pack sags under load and recovers
// at rest, so the gate trips at ENTER and only releases above EXIT.

namespace acorn {
namespace BatteryGate {

constexpr float ENTER_CRITICAL_V = 8.0f;
constexpr float EXIT_CRITICAL_V = 8.5f;

// Next gate state. An invalid reading (disconnected or implausible sense
// voltage) is treated as critical: the pack state is unknown.
inline bool critical(bool wasCritical, bool valid, float volts) {
  if (!valid) return true;
  return wasCritical ? volts < EXIT_CRITICAL_V : volts < ENTER_CRITICAL_V;
}

}  // namespace BatteryGate
}  // namespace acorn
