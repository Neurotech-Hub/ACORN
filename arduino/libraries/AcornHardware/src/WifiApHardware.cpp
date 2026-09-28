#include "WifiApHardware.h"

#include <WiFi.h>

#include "SystemHardware.h"

namespace acorn {
namespace WifiApHardware {

namespace {
bool running = false;
uint32_t timeout = DEFAULT_IDLE_TIMEOUT_MS;
uint32_t lastActivity = 0;
}  // namespace

bool start(uint8_t channel) {
  if (running) return true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  running = WiFi.softAP(SystemHardware::deviceId(), nullptr, channel);
  if (!running) WiFi.mode(WIFI_OFF);
  lastActivity = millis();
  return running;
}

void stop() {
  if (!running) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
}

bool isRunning() { return running; }
const char* ssid() { return SystemHardware::deviceId(); }
IPAddress ip() { return running ? WiFi.softAPIP() : IPAddress(); }
uint8_t stationCount() { return running ? WiFi.softAPgetStationNum() : 0; }

void setIdleTimeout(uint32_t timeoutMs) { timeout = timeoutMs; }
void touch() { lastActivity = millis(); }

uint32_t idleMs() {
  if (stationCount() > 0) lastActivity = millis();
  return millis() - lastActivity;
}

bool idleExpired() { return running && idleMs() >= timeout; }

}  // namespace WifiApHardware
}  // namespace acorn
