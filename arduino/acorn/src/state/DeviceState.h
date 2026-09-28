#pragma once

#include <Arduino.h>
#include <AcornCore.h>
#include <AcornHardware.h>
#include <ArduinoJson.h>

#include <vector>

// Device-level state (init.md sections 7, 39, 59): SD presence/mounting,
// fault aggregation (blocking vs non-blocking), "armed", and the /status
// model shared by the API and the serial console.

namespace acorn {

struct Fault {
  String code;
  String message;
  bool blocking = true;
  const char* area = "status";  // UI area: device | status | data | programs
  String programId;             // optional
  String field;                 // optional
};

namespace DeviceState {

void begin(WakeSource wake);
void update();  // SD insertion/removal polling; call from loop()

bool sdPresent();        // card detected
bool sdReady();          // present and mounted
const SdInfo& sdInfo();  // cached; refreshed periodically and on changes
void refreshSd();        // after uploads/deletes
bool sdFull();

void reportLogWrite(bool ok);  // loggers report every write

std::vector<Fault> faults();
bool armed();  // no blocking faults: scheduled playback may run
WakeSource wakeSource();

// Next scheduled playback (only when armed), false if none.
bool nextPlayback(int64_t nowUtc, ScheduledEvent& out);
// Next environmental sample boundary, or -1 if logging is off / time unset.
int64_t nextEnvSample(int64_t nowUtc);

void faultsJson(JsonArray out);
void statusJson(JsonObject out);

}  // namespace DeviceState
}  // namespace acorn
