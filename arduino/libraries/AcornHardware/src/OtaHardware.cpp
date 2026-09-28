#include "OtaHardware.h"

#include <Update.h>
#include <esp_ota_ops.h>

namespace acorn {
namespace OtaHardware {

namespace {
String error;
}

OtaPartitions partitions() {
  OtaPartitions p;
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (running) p.running = running->label;
  if (next) {
    p.next = next->label;
    p.slotBytes = next->size;
  }
  return p;
}

void markRunningValid() { esp_ota_mark_app_valid_cancel_rollback(); }

bool begin(size_t imageBytes) {
  error = "";
  if (Update.isRunning()) Update.abort();
  if (!Update.begin(imageBytes ? imageBytes : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    error = Update.errorString();
    return false;
  }
  return true;
}

size_t write(const uint8_t* data, size_t len) {
  if (!Update.isRunning()) return 0;
  const size_t n = Update.write(const_cast<uint8_t*>(data), len);
  if (n != len) error = Update.errorString();
  return n;
}

bool end() {
  if (!Update.end(true)) {
    error = Update.errorString();
    return false;
  }
  return true;
}

void abort() {
  if (Update.isRunning()) Update.abort();
}

bool inProgress() { return Update.isRunning(); }
size_t written() { return Update.progress(); }
const char* lastError() { return error.c_str(); }

}  // namespace OtaHardware
}  // namespace acorn
