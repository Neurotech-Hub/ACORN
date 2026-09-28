#pragma once

#include <Arduino.h>

// Configuration mode (init.md section 43, Hall behavior resolved in section
// 69): opening the enclosure door starts the AP, web UI, API and captive
// portal; closing the door or 10 minutes without API activity ends it.
// Scheduled playback and env sampling keep running during a session.

namespace acorn {
namespace ConfigSession {

// persistent = bench mode (ACORN_STAY_AWAKE): never ends on idle or door close.
bool start(bool persistent = false);
void end();
bool active();
bool persistent();

void onDoorClosed();  // ends a non-persistent session
void update();        // idle timeout; call from loop()

}  // namespace ConfigSession
}  // namespace acorn
