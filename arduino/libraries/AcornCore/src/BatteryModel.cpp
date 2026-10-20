#include "BatteryModel.h"

#include <math.h>

namespace acorn {

BatteryModel BatteryModel::defaultM12() {
  return {"M12 Li-ion (default)",
          {{12.0, 100}, {11.7, 90}, {11.5, 80}, {11.3, 70}, {11.1, 60}, {10.9, 50},
           {10.7, 40}, {10.5, 30}, {10.3, 20}, {10.0, 10}, {9.4, 0}}};
}

bool BatteryModel::isValid() const {
  if (points.size() < 2 || points.size() > 16) return false;
  for (size_t i = 0; i < points.size(); i++) {
    const BatteryPoint& p = points[i];
    if (!isfinite(p.volts) || p.percent < 0 || p.percent > 100) return false;
    if (i > 0 && (p.volts >= points[i - 1].volts || p.percent > points[i - 1].percent)) return false;
  }
  return true;
}

int32_t BatteryModel::percent(double v) const {
  if (points.empty()) return 0;
  if (v >= points.front().volts) return points.front().percent;
  for (size_t i = 1; i < points.size(); i++) {
    const BatteryPoint& hi = points[i - 1];
    const BatteryPoint& lo = points[i];
    if (v >= lo.volts) {
      const double f = (static_cast<double>(v) - lo.volts) / (static_cast<double>(hi.volts) - lo.volts);
      return static_cast<int32_t>(floor(lo.percent + f * (hi.percent - lo.percent) + 0.5));
    }
  }
  return points.back().percent;
}

namespace BatteryIndication {

uint8_t flashCount(bool valid, int32_t percent) {
  if (!valid) return 0;
  if (percent < 0) percent = 0;
  int32_t n = (percent + 9) / 10;
  if (n < 1) n = 1;
  if (n > MAX_FLASHES) n = MAX_FLASHES;
  return static_cast<uint8_t>(n);
}

}  // namespace BatteryIndication
}  // namespace acorn
