#pragma once

#include <Arduino.h>
#include <AcornCore.h>

// The one active Program (init.md sections 8, 49): its parsed form, revision,
// validation state and the Scheduler built from it.

namespace acorn {
namespace ActiveProgram {

enum class State : uint8_t {
  None,     // no active Program ID configured
  NoSd,     // SD not mounted; the Program cannot be loaded
  Missing,  // configured ID has no file
  Invalid,  // loaded but fails validation
  Ok,
};

void reload();  // re-read the configured Program (boot, SD change, saves)
State state();
const String& id();
const Program* program();  // parsed Program (also when invalid), or nullptr
const std::string& revision();
const ValidationErrors& errors();
bool valid();
// Non-null only for a valid Program; rebuilt when the UTC offset changes.
const Scheduler* scheduler();
// Increments whenever the loaded Program or its validity changes.
uint32_t generation();

// Section 49 steps 1-12. On failure the previous Program stays active and
// the returned list explains why (PROGRAM_NOT_FOUND if absent).
ValidationErrors activate(const String& id);

}  // namespace ActiveProgram
}  // namespace acorn
