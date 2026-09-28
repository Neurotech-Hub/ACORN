#pragma once

#include <Arduino.h>

// Temperature/humidity (SHT40) and ambient light (VEML6031). Failed reads
// are reported as invalid values and a non-blocking SENSOR_READ_FAILED
// fault, never as zeros (init.md sections 34, 59).

namespace acorn {

struct EnvState {
  bool sampled = false;
  int64_t sampledUtc = 0;
  uint32_t sampledAtMs = 0;
  bool envValid = false;  // temperature + humidity
  float temperatureC = NAN;
  float humidityPct = NAN;
  bool lightValid = false;
  float lux = NAN;
  bool ok() const { return envValid && lightValid; }
};

namespace EnvironmentService {

void begin();
const EnvState& sample();  // reads both sensors now
const EnvState& last();
// Cached reading, refreshed when older than maxAgeMs. The light sensor
// integrates for up to a few hundred ms, so status polling uses this.
const EnvState& fresh(uint32_t maxAgeMs = 30000);
bool faulted();  // last sample had a failed sensor

}  // namespace EnvironmentService
}  // namespace acorn
