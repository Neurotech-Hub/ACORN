#include "EnvironmentService.h"

#include <AcornHardware.h>

#include "TimeService.h"

namespace acorn {
namespace EnvironmentService {

namespace {
EnvState state;
bool shtReady = false;
bool lightReady = false;
}  // namespace

void begin() {
  shtReady = Sht40Hardware::begin();
  lightReady = LightHardware::begin();
}

const EnvState& sample() {
  if (!shtReady) shtReady = Sht40Hardware::begin();
  if (!lightReady) lightReady = LightHardware::begin();

  EnvState s;
  s.sampled = true;
  s.sampledUtc = TimeService::nowUtc();
  s.sampledAtMs = millis();
  if (shtReady) {
    const EnvReading r = Sht40Hardware::read();
    s.envValid = r.valid;
    if (r.valid) {
      s.temperatureC = r.temperatureC;
      s.humidityPct = r.humidityPct;
    }
  }
  if (lightReady) {
    const LightReading r = LightHardware::read();
    s.lightValid = r.valid;
    if (r.valid) s.lux = r.lux;
  }
  state = s;
  return state;
}

const EnvState& last() { return state; }

const EnvState& fresh(uint32_t maxAgeMs) {
  if (!state.sampled || millis() - state.sampledAtMs > maxAgeMs) sample();
  return state;
}

bool faulted() { return state.sampled && !state.ok(); }

}  // namespace EnvironmentService
}  // namespace acorn
