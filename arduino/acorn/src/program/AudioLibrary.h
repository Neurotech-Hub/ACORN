#pragma once

#include <Arduino.h>
#include <AcornCore.h>
#include <AcornHardware.h>

#include <vector>

// The /audio library (init.md sections 26, 27): cached WAV metadata, the
// AudioCatalog used by the Program validator, uploads and guarded deletes.

namespace acorn {

struct AudioEntry {
  String name;  // "playback.wav"
  uint32_t sizeBytes = 0;
  WavStatus status = WavStatus::Corrupt;
  WavInfo info;
  String errorMessage;  // "" when valid
  bool valid() const { return status == WavStatus::Ok; }
  String path() const { return String("/audio/") + name; }
};

struct UploadResult {
  bool ok = false;
  int httpStatus = 200;  // 201 created / 200 replaced on success
  const char* code = "";
  String message;
  AudioEntry entry;  // metadata (also for invalid WAVs)
};

namespace AudioLibrary {

void invalidate();  // rescan on next access (SD change)
const std::vector<AudioEntry>& list();  // sorted by name
const AudioEntry* find(const String& name);
const AudioEntry* findPath(const String& path);  // "/audio/<name>"
const AudioCatalog& catalog();
uint32_t generation();  // bumps whenever the library changes

// Mock-compatible upload name rules: basename, lowercased, [a-z0-9._-].
String sanitizeName(const String& filename);

bool remove(const String& name);

// Streaming upload into a temporary file, validated before it replaces
// any existing file of the same name.
bool uploadBegin();
bool uploadWrite(const uint8_t* data, size_t len);
UploadResult uploadFinish(const String& name);
void uploadAbort();

}  // namespace AudioLibrary
}  // namespace acorn
