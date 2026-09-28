#pragma once

#include <ArduinoJson.h>

#include <string>

#include "ErrorCodes.h"
#include "Program.h"

namespace acorn {

struct AudioMeta {
  bool valid = false;
  uint32_t durationMs = 0;
  std::string errorMessage;  // why it is not a supported WAV
};

// Audio lookup used by the validator. Firmware backs this with the SD audio
// library; host tests use an in-memory map.
class AudioCatalog {
 public:
  virtual ~AudioCatalog() = default;
  // False if `path` does not name an existing audio file.
  virtual bool find(const std::string& path, AudioMeta& out) const = 0;
};

// The single Program validator (init.md sections 17, 18, 49, 55). Used for
// API validate/save, activation and boot-time loading alike.
namespace ProgramValidator {

// Validates raw Program JSON. Missing schema_version/description/events take
// their defaults. `out` (optional) receives the parsed Program; it is only
// meaningful when the returned list is empty.
ValidationErrors validate(JsonVariantConst json, const AudioCatalog& audio, Program* out = nullptr);

// Parses text first; malformed JSON yields a single PROGRAM_JSON_INVALID.
ValidationErrors validateText(const char* text, size_t len, const AudioCatalog& audio, Program* out = nullptr);

bool isValidId(const char* id);

}  // namespace ProgramValidator
}  // namespace acorn
