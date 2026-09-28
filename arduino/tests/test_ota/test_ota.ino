// Isolated test: two-slot OTA using the ACORN partition table.
// Starts the open AP, serves an upload form at http://192.168.4.1/.
// Upload any compiled .bin (e.g. this sketch rebuilt with a new BUILD_TAG);
// after reboot the running partition should switch ota_0 <-> ota_1.

#include <AcornHardware.h>
#include <AcornTest.h>
#include <WebServer.h>

using namespace acorn;

#ifndef BUILD_TAG
#define BUILD_TAG __DATE__ " " __TIME__
#endif

WebServer server(80);
bool uploadOk = false;

const char FORM[] PROGMEM =
    "<!doctype html><meta name=viewport content='width=device-width'>"
    "<h3>ACORN OTA test</h3><form method=POST action=/update enctype=multipart/form-data>"
    "<input type=file name=fw accept=.bin> <button>Upload</button></form>";

void handleUpload() {
  HTTPUpload& up = server.upload();
  WifiApHardware::touch();
  if (up.status == UPLOAD_FILE_START) {
    test::info("upload start: %s", up.filename.c_str());
    uploadOk = OtaHardware::begin(0);
    if (!uploadOk) test::info("begin failed: %s", OtaHardware::lastError());
  } else if (up.status == UPLOAD_FILE_WRITE && uploadOk) {
    uploadOk = OtaHardware::write(up.buf, up.currentSize) == up.currentSize;
    if (!uploadOk) test::info("write failed: %s", OtaHardware::lastError());
  } else if (up.status == UPLOAD_FILE_END && uploadOk) {
    uploadOk = OtaHardware::end();
    test::info("upload end: %u bytes, %s", up.totalSize, uploadOk ? "verified" : OtaHardware::lastError());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    OtaHardware::abort();
    uploadOk = false;
    test::info("upload aborted");
  }
}

void setup() {
  test::begin("test_ota");
  test::info("build %s", BUILD_TAG);
  const OtaPartitions p = OtaHardware::partitions();
  test::info("running=%s next=%s slot=%lu bytes", p.running, p.next, static_cast<unsigned long>(p.slotBytes));
  test::check("running from an OTA slot", strncmp(p.running, "app", 3) == 0 || strncmp(p.running, "ota", 3) == 0,
              p.running);
  test::check("second OTA slot available", p.next[0] != 0 && p.slotBytes > 0);
  test::check("slot fits 6 MB image", p.slotBytes >= 6UL * 1024UL * 1024UL);
  OtaHardware::markRunningValid();

  if (!test::check("AP started", WifiApHardware::start())) {
    test::summary();
    return;
  }
  test::summary();
  test::info("Join %s and open http://%s/", WifiApHardware::ssid(), WifiApHardware::ip().toString().c_str());

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", FORM); });
  server.on(
      "/update", HTTP_POST,
      [] {
        server.send(uploadOk ? 200 : 500, "text/plain", uploadOk ? "OK, rebooting\n" : "OTA_FAILED\n");
        if (uploadOk) {
          test::info("rebooting into new image");
          delay(500);
          ESP.restart();
        }
      },
      handleUpload);
  server.begin();
}

void loop() {
  server.handleClient();
  delay(2);
}
