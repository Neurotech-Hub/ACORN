// ACORN firmware entry point (event-driven sleep; door-driven config sessions).
//
// Layering (docs/init.md sections 65, 71-72):
//   src/api      /api/v1 on the core WebServer (calls services only)
//   src/...      services: config, program, audio, logging, state, power
//   AcornCore    pure scheduling/validation/time logic (host-tested)
//   AcornHardware HAL; application code never touches GPIOs directly
//
// Bench builds: -DACORN_STAY_AWAKE=1 keeps the AP, web UI and API always up.

#include "src/Runtime.h"

void setup() { acorn::Runtime::setup(); }

void loop() { acorn::Runtime::loop(); }
