#pragma once

#include <Arduino.h>

// Browser OTA (init.md section 51) on top of OtaHardware: streamed image
// checks (ESP32 magic byte, app descriptor version, slot size) and the
// reboot after a successful update. Available in SD fault mode.

namespace acorn {

struct FirmwareInfo {
  String version;
  String buildUtc;
  String runningPartition;
  String nextPartition;
  uint32_t slotSizeBytes = 0;
};

namespace FirmwareService {

FirmwareInfo info();
// Confirms the running image after a successful boot self-check.
void markRunningValid();

// Streaming update. Each call returns false once the upload has failed;
// errorCode/errorMessage/httpStatus then describe the failure.
bool begin(size_t declaredBytes);
bool write(const uint8_t* data, size_t len);
bool finish();
void abort();

size_t written();
const String& imageVersion();  // from the image's app descriptor, "" if absent
const String& targetPartition();
int httpStatus();
const char* errorCode();
const String& errorMessage();

void scheduleRestart(uint32_t delayMs);
void update();  // performs a scheduled restart; call from loop()

}  // namespace FirmwareService
}  // namespace acorn
