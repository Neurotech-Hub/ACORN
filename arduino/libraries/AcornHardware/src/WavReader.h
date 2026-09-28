#pragma once

#include <Arduino.h>
#include <FS.h>

namespace acorn {

enum class WavStatus : uint8_t { Ok, Unsupported, Corrupt };

const char* wavStatusCode(WavStatus status);  // "WAV_UNSUPPORTED", "WAV_CORRUPT"

struct WavInfo {
  uint16_t format = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
  uint32_t durationMs = 0;
};

// Validates the RIFF header against the v1 contract: PCM, 16-bit, mono,
// 44.1 or 48 kHz. On Ok the file is positioned at the first sample.
namespace WavReader {

WavStatus open(fs::File& file, WavInfo& info);
bool isSupported(const WavInfo& info);

}  // namespace WavReader
}  // namespace acorn
