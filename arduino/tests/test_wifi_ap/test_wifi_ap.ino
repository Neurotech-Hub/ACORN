// Isolated test: open soft AP "ACORN-xxxxxx" and the idle-timeout helper.
// Join the network from a phone/laptop; http://192.168.4.1/ returns a short
// page. The AP stops after 2 minutes idle in this test (10 min default).

#include <AcornHardware.h>
#include <AcornTest.h>
#include <WebServer.h>

using namespace acorn;

constexpr uint32_t TEST_IDLE_TIMEOUT_MS = 2UL * 60UL * 1000UL;

WebServer server(80);
uint8_t lastStations = 0;
bool sawClient = false;
bool reported = false;

void setup() {
  test::begin("test_wifi_ap");
  test::info("Device ID %s", SystemHardware::deviceId());
  test::check("SSID format ACORN-XXXXXX", strlen(WifiApHardware::ssid()) == 12 &&
                                              strncmp(WifiApHardware::ssid(), "ACORN-", 6) == 0);

  if (!test::check("AP started (open)", WifiApHardware::start())) {
    test::summary();
    return;
  }
  WifiApHardware::setIdleTimeout(TEST_IDLE_TIMEOUT_MS);
  test::info("SSID %s  IP %s", WifiApHardware::ssid(), WifiApHardware::ip().toString().c_str());

  server.on("/", [] {
    WifiApHardware::touch();
    sawClient = true;
    server.send(200, "text/plain", String("ACORN AP test OK\n") + SystemHardware::deviceId() + "\n");
  });
  server.begin();
  test::info("Join the AP and open http://%s/", WifiApHardware::ip().toString().c_str());
}

void loop() {
  if (!WifiApHardware::isRunning()) {
    delay(100);
    return;
  }
  server.handleClient();

  const uint8_t n = WifiApHardware::stationCount();
  if (n != lastStations) {
    test::info("stations: %u", n);
    lastStations = n;
  }
  if (!reported && sawClient) {
    reported = true;
    test::check("client connected and fetched /", true);
    test::summary();
  }

  static uint32_t lastIdle = 0;
  if (millis() - lastIdle >= 15000) {
    lastIdle = millis();
    test::info("idle %lu s", static_cast<unsigned long>(WifiApHardware::idleMs() / 1000));
  }

  if (WifiApHardware::idleExpired()) {
    server.stop();
    WifiApHardware::stop();
    test::check("idle timeout stopped AP", !WifiApHardware::isRunning());
    if (!reported) test::summary();
    test::info("Reset to run again");
  }
  delay(2);
}
