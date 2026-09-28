#pragma once

// Application runtime (init.md sections 40, 56): boot self-check, then
// SERVICE (due playback, env sampling, BTN_01 / BTN_02, console) until
// nothing is pending and the device sleeps, or CONFIG while a door-driven
// configuration session is up. ACORN_STAY_AWAKE=1 keeps a permanent session.

namespace acorn {
namespace Runtime {

void setup();
void loop();

// Scheduled playback, env sampling and playback logging. Safe to call from
// inside long API uploads (re-entrancy guarded).
void background();

}  // namespace Runtime
}  // namespace acorn
