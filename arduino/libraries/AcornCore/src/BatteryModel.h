#pragma once

#include <stdint.h>

#include <string>
#include <vector>

// Battery voltage -> estimated percent (init.md sections 31, 33). Voltage is
// authoritative; percent is a convenience estimate from a point table.

namespace acorn {

struct BatteryPoint {
  double volts;
  int32_t percent;
};

struct BatteryModel {
  std::string name;
  std::vector<BatteryPoint> points;  // volts strictly decreasing, percent non-increasing

  static BatteryModel defaultM12();

  // 2..16 points, volts strictly decreasing, percent 0..100 non-increasing.
  bool isValid() const;
  // Linear interpolation between points, rounded; clamped to the ends.
  int32_t percent(double volts) const;
};

namespace BatteryIndication {

constexpr uint8_t MAX_FLASHES = 10;

// BTN_02 flash count: ceil(percent / 10) clamped to 1..10, so a valid
// reading never shows zero flashes. Invalid readings return 0 and must use
// the separate error pattern.
uint8_t flashCount(bool valid, int32_t percent);

}  // namespace BatteryIndication
}  // namespace acorn
