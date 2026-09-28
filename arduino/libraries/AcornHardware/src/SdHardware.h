#pragma once

#include <Arduino.h>
#include <FS.h>

namespace acorn {

enum class SdStatus : uint8_t { Ok, NotPresent, MountFailed, WriteFailed, Full };

const char* sdStatusCode(SdStatus status);  // "SD_NOT_PRESENT", ... ("OK")

struct SdInfo {
  bool present = false;
  bool mounted = false;
  uint8_t cardType = 0;  // CARD_NONE / CARD_MMC / CARD_SD / CARD_SDHC
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
};

// microSD over SPI (CS 10, CLK 14, MOSI 15, MISO 16); SD_DETECT (GPIO12)
// LOW = inserted. Mounted at /sd via the core SD library.
namespace SdHardware {

constexpr uint32_t SPI_HZ = 20000000;
constexpr uint64_t MIN_FREE_BYTES = 1024 * 1024;  // below this = SD_FULL

void begin();                 // pins only; does not mount
bool isCardPresent();
SdStatus mount();             // idempotent
void unmount();
bool isMounted();
SdInfo info();
SdStatus checkWritable();     // free space check (mount first)
fs::FS& fs();

// Card CS HIGH and bus idle, latched for deep sleep.
void prepareForSleep();

}  // namespace SdHardware
}  // namespace acorn
