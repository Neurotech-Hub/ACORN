#pragma once

#include <AcornCore.h>

#include "BatteryService.h"
#include "EnvironmentService.h"

namespace acorn {

inline Csv::Telemetry makeTelemetry(const BatteryState& b, const EnvState& e) {
  Csv::Telemetry t;
  t.batteryValid = b.valid;
  t.batteryVoltage = b.voltage;
  t.batteryPercent = b.percent;
  t.envValid = e.envValid;
  t.temperatureC = e.temperatureC;
  t.humidityPct = e.humidityPct;
  t.lightValid = e.lightValid;
  t.lux = e.lux;
  return t;
}

}  // namespace acorn
