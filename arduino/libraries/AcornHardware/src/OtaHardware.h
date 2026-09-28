#pragma once

#include <Arduino.h>

namespace acorn {

struct OtaPartitions {
  const char* running = "";
  const char* next = "";      // target of the next update
  uint32_t slotBytes = 0;     // size of the next slot
};

// Thin wrapper over the core Update library for two-slot (ota_0/ota_1)
// firmware updates. The new image boots only after end() verifies it.
namespace OtaHardware {

OtaPartitions partitions();
// Call once the new firmware has proven itself (no-op without rollback).
void markRunningValid();

bool begin(size_t imageBytes);  // 0/unknown size = whole slot
size_t write(const uint8_t* data, size_t len);
bool end();                     // verify + set boot partition
void abort();
bool inProgress();
size_t written();
const char* lastError();        // "" when no error

}  // namespace OtaHardware
}  // namespace acorn
