#pragma once

#include <Arduino.h>
#include <AcornCore.h>

#include <vector>

// Program JSON files in /programs on the SD card (init.md sections 6, 45).
// Saves are atomic: write <id>.json.tmp, read back and validate, then
// replace <id>.json. A crash mid-save leaves either the old or the new file.

namespace acorn {

struct ProgramSummary {
  String id;
  String name;
  String description;
  String modifiedUtc;  // "" if absent
  size_t eventCount = 0;
  String revision;     // "" if the file is not valid JSON
  bool valid = false;
  size_t errorCount = 0;
  std::vector<String> audio;  // referenced audio paths
};

namespace ProgramStore {

void invalidate();  // SD change
const std::vector<ProgramSummary>& list();  // sorted by id
const ProgramSummary* summary(const String& id);
bool exists(const String& id);
size_t count();

bool readText(const String& id, String& out);
// Loads and validates a stored Program. PROGRAM_NOT_FOUND if absent.
ValidationErrors load(const String& id, Program& out, String* text = nullptr);

// `program` must already be valid. Writes normalized JSON atomically.
bool save(const Program& program, const char*& errorCode);
bool remove(const String& id);

// Summary for any Program JSON (stored or draft).
ProgramSummary summarize(const String& id, JsonVariantConst json, const ValidationErrors& errors,
                         const Program* parsed);

}  // namespace ProgramStore
}  // namespace acorn
