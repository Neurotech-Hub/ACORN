#include "WifiService.h"

#include <AcornHardware.h>
#include <DNSServer.h>

#include "../AppConfig.h"
#include "LedService.h"

namespace acorn {
namespace WifiService {

namespace {
DNSServer dns;
bool dnsRunning = false;
uint32_t lastActivityMs = 0;
}  // namespace

bool start() {
  WifiApHardware::setIdleTimeout(app::AP_IDLE_TIMEOUT_S * 1000UL);
  const bool ok = WifiApHardware::start();
  LedService::setBroadcasting(ok && WifiApHardware::isRunning());
  WifiApHardware::touch();
  lastActivityMs = millis();
  if (ok) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dnsRunning = dns.start(53, "*", WifiApHardware::ip());
  }
  return ok;
}

void stop() {
  if (dnsRunning) dns.stop();
  dnsRunning = false;
  WifiApHardware::stop();
  LedService::setBroadcasting(false);
}

void update() {
  if (dnsRunning) dns.processNextRequest();
}

bool isRunning() { return WifiApHardware::isRunning(); }
const char* ssid() { return WifiApHardware::ssid(); }
uint8_t clients() { return isRunning() ? WifiApHardware::stationCount() : 0; }

void touch() {
  lastActivityMs = millis();
  WifiApHardware::touch();
}
uint32_t idleTimeoutS() { return app::AP_IDLE_TIMEOUT_S; }

// Inactivity is measured from API requests only: a phone that stays joined
// with the page polling GET /status still times out (docs/web-ui.md section 4).
uint32_t idleRemainingS() {
  const uint32_t idleS = (millis() - lastActivityMs) / 1000;
  return idleS >= app::AP_IDLE_TIMEOUT_S ? 0 : app::AP_IDLE_TIMEOUT_S - idleS;
}

bool idleExpired() { return isRunning() && idleRemainingS() == 0; }

}  // namespace WifiService
}  // namespace acorn
