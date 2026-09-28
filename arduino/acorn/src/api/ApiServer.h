#pragma once

#include <Arduino.h>

// /api/v1 on the core WebServer. Routes, envelopes, status codes and error
// codes follow dev/mock/api.mjs (docs/web-ui.md section 4). Handlers call
// services only, never the HAL directly (init.md section 65). Other paths
// serve the embedded web UI and captive portal (WebUi).

namespace acorn {
namespace ApiServer {

void begin(uint16_t port = 80);  // may be called again after end()
void end();                      // aborts unfinished uploads
void handle();  // call from loop()

// Called periodically while long uploads block the loop so scheduled
// playback and sampling keep running.
void setBackgroundHook(void (*hook)());

}  // namespace ApiServer
}  // namespace acorn
