#include "SdHardware.h"

#include <SD.h>
#include <SPI.h>

#include "GpioHold.h"
#include "Pins.h"

namespace acorn {

const char* sdStatusCode(SdStatus status) {
  switch (status) {
    case SdStatus::Ok: return "OK";
    case SdStatus::NotPresent: return "SD_NOT_PRESENT";
    case SdStatus::MountFailed: return "SD_MOUNT_FAILED";
    case SdStatus::WriteFailed: return "SD_WRITE_FAILED";
    case SdStatus::Full: return "SD_FULL";
  }
  return "SD_MOUNT_FAILED";
}

namespace SdHardware {

namespace {
SPIClass spi(FSPI);
bool mounted = false;
bool spiStarted = false;
}  // namespace

void begin() {
  pinMode(pins::SD_DETECT, INPUT);
  releaseOutput(pins::SD_CS, HIGH);
}

bool isCardPresent() { return digitalRead(pins::SD_DETECT) == level::SD_INSERTED; }

SdStatus mount() {
  if (!isCardPresent()) {
    if (mounted) unmount();
    return SdStatus::NotPresent;
  }
  if (mounted) return SdStatus::Ok;
  if (!spiStarted) {
    spi.begin(pins::SD_CLK, pins::SD_MISO, pins::SD_MOSI, pins::SD_CS);
    spiStarted = true;
  }
  mounted = SD.begin(pins::SD_CS, spi, SPI_HZ, "/sd", 5, false);
  return mounted ? SdStatus::Ok : SdStatus::MountFailed;
}

void unmount() {
  if (mounted) SD.end();
  mounted = false;
  if (spiStarted) {
    spi.end();
    spiStarted = false;
  }
  releaseOutput(pins::SD_CS, HIGH);
}

bool isMounted() { return mounted; }

SdInfo info() {
  SdInfo i;
  i.present = isCardPresent();
  i.mounted = mounted;
  if (!mounted) return i;
  i.cardType = SD.cardType();
  i.totalBytes = SD.totalBytes();
  i.usedBytes = SD.usedBytes();
  return i;
}

SdStatus checkWritable() {
  if (!isCardPresent()) return SdStatus::NotPresent;
  if (!mounted) return SdStatus::MountFailed;
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  if (total <= used || total - used < MIN_FREE_BYTES) return SdStatus::Full;
  return SdStatus::Ok;
}

fs::FS& fs() { return SD; }

void prepareForSleep() {
  unmount();
  holdOutput(pins::SD_CS, HIGH);
}

}  // namespace SdHardware
}  // namespace acorn
