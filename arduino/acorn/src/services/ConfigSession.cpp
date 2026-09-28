#include "ConfigSession.h"

#include "../api/ApiServer.h"
#include "WifiService.h"

namespace acorn {
namespace ConfigSession {

namespace {
bool running = false;
bool keepAlive = false;
}  // namespace

bool start(bool persistent) {
  keepAlive = keepAlive || persistent;
  if (running) return true;
  if (!WifiService::start()) {
    Serial.println("config session: AP failed to start");
    WifiService::stop();
    return false;
  }
  ApiServer::begin();
  running = true;
  Serial.printf("config session: AP \"%s\" up, UI at http://192.168.4.1/\n", WifiService::ssid());
  return true;
}

void end() {
  if (!running) return;
  ApiServer::end();
  WifiService::stop();
  running = false;
  keepAlive = false;
  Serial.println("config session: ended");
}

bool active() { return running; }
bool persistent() { return running && keepAlive; }

void onDoorClosed() {
  if (running && !keepAlive) {
    Serial.println("config session: door closed");
    end();
  }
}

void update() {
  if (!running) return;
  WifiService::update();
  ApiServer::handle();
  if (!keepAlive && WifiService::idleExpired()) {
    Serial.println("config session: idle timeout");
    end();
  }
}

}  // namespace ConfigSession
}  // namespace acorn
