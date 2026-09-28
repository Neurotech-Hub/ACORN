#include "FirmwareService.h"

#include <AcornHardware.h>

#include "../AppConfig.h"

namespace acorn {
namespace FirmwareService {

namespace {

constexpr uint8_t IMAGE_MAGIC = 0xE9;
constexpr uint32_t APP_DESC_MAGIC = 0xABCD5432;
constexpr size_t APP_DESC_OFFSET = 24 + 8;  // image header + first segment header
constexpr size_t MIN_IMAGE_BYTES = 256;

bool failed = false;
bool first = true;
size_t bytes = 0;
String version;
String target;
int status = 200;
const char* code = "";
String message;
uint32_t restartAtMs = 0;
bool restartPending = false;

bool fail(int httpStatus, const char* errCode, const String& msg) {
  if (!failed) {
    failed = true;
    status = httpStatus;
    code = errCode;
    message = msg;
    OtaHardware::abort();
  }
  return false;
}

uint32_t le32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

FirmwareInfo info() {
  const OtaPartitions p = OtaHardware::partitions();
  FirmwareInfo i;
  i.version = ACORN_FIRMWARE_VERSION;
  i.buildUtc = app::buildTimestamp();
  i.runningPartition = p.running;
  i.nextPartition = p.next;
  i.slotSizeBytes = p.slotBytes;
  return i;
}

void markRunningValid() { OtaHardware::markRunningValid(); }

bool begin(size_t declaredBytes) {
  failed = false;
  first = true;
  bytes = 0;
  version = "";
  status = 200;
  code = "";
  message = "";
  if (OtaHardware::inProgress()) OtaHardware::abort();  // a previous upload was cut off
  const OtaPartitions p = OtaHardware::partitions();
  target = p.next;
  // Multipart framing adds a few hundred bytes around the image.
  if (declaredBytes > p.slotBytes + 4096) {
    return fail(413, "OTA_INVALID", "Firmware image exceeds the application slot.");
  }
  if (!OtaHardware::begin(0)) return fail(500, "OTA_FAILED", OtaHardware::lastError());
  return true;
}

bool write(const uint8_t* data, size_t len) {
  if (failed) return false;
  if (first && len) {
    first = false;
    if (data[0] != IMAGE_MAGIC) {
      return fail(422, "OTA_INVALID", "Not an ESP32 application image (bad magic byte).");
    }
    if (len >= APP_DESC_OFFSET + 48 && le32(data + APP_DESC_OFFSET) == APP_DESC_MAGIC) {
      char v[33];
      memcpy(v, data + APP_DESC_OFFSET + 16, 32);
      v[32] = 0;
      version = v;
    }
  }
  if (bytes + len > OtaHardware::partitions().slotBytes) {
    return fail(413, "OTA_INVALID", "Firmware image exceeds the application slot.");
  }
  if (OtaHardware::write(data, len) != len) return fail(500, "OTA_FAILED", OtaHardware::lastError());
  bytes += len;
  return true;
}

bool finish() {
  if (failed) return false;
  if (bytes < MIN_IMAGE_BYTES) return fail(422, "OTA_INVALID", "Not an ESP32 application image (too small).");
  if (!OtaHardware::end()) {
    failed = true;
    status = 422;
    code = "OTA_INVALID";
    message = String("Image verification failed: ") + OtaHardware::lastError();
    return false;
  }
  return true;
}

void abort() {
  if (!failed) fail(400, "OTA_INVALID", "Upload aborted.");
}

size_t written() { return bytes; }
const String& imageVersion() { return version; }
const String& targetPartition() { return target; }
int httpStatus() { return status; }
const char* errorCode() { return code; }
const String& errorMessage() { return message; }

void scheduleRestart(uint32_t delayMs) {
  restartPending = true;
  restartAtMs = millis() + delayMs;
}

void update() {
  if (restartPending && static_cast<int32_t>(millis() - restartAtMs) >= 0) ESP.restart();
}

}  // namespace FirmwareService
}  // namespace acorn
