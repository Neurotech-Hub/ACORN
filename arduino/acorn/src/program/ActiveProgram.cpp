#include "ActiveProgram.h"

#include <AcornHardware.h>

#include <memory>

#include "../config/DeviceConfig.h"
#include "../services/TimeService.h"
#include "AudioLibrary.h"
#include "ProgramStore.h"

namespace acorn {
namespace ActiveProgram {

namespace {

State current = State::None;
String activeId;
std::unique_ptr<Program> prog;
std::string rev;
ValidationErrors errs;
std::unique_ptr<Scheduler> sched;
int32_t schedOffset = 0;
uint32_t loadedAudioGen = 0;
uint32_t gen = 0;

void clear() {
  sched.reset();
  prog.reset();
  rev.clear();
  errs.clear();
}

void load() {
  clear();
  gen++;
  activeId = DeviceConfig::get().activeProgramId;
  loadedAudioGen = AudioLibrary::generation();
  if (activeId.isEmpty()) {
    current = State::None;
    return;
  }
  if (!SdHardware::isMounted()) {
    current = State::NoSd;
    return;
  }
  std::unique_ptr<Program> p(new Program());
  errs = ProgramStore::load(activeId, *p);
  if (!errs.empty() && errs.front().code == err::PROGRAM_NOT_FOUND) {
    current = State::Missing;
    return;
  }
  rev = ProgramJson::revision(*p);
  prog = std::move(p);
  current = errs.empty() ? State::Ok : State::Invalid;
}

// Validity depends on the audio library (missing/replaced WAVs).
void refreshIfStale() {
  if (loadedAudioGen != AudioLibrary::generation()) load();
}

}  // namespace

void reload() { load(); }

State state() {
  refreshIfStale();
  return current;
}

const String& id() { return activeId; }

const Program* program() {
  refreshIfStale();
  return prog.get();
}

const std::string& revision() {
  refreshIfStale();
  return rev;
}

const ValidationErrors& errors() {
  refreshIfStale();
  return errs;
}

bool valid() { return state() == State::Ok; }

const Scheduler* scheduler() {
  if (!valid()) return nullptr;
  const int32_t offset = TimeService::utcOffsetMinutes();
  if (!sched || schedOffset != offset) {
    sched.reset(new Scheduler(*prog, rev, offset));
    schedOffset = offset;
  }
  return sched.get();
}

uint32_t generation() {
  refreshIfStale();
  return gen;
}

ValidationErrors activate(const String& newId) {
  if (!SdHardware::isMounted()) return {{err::SD_NOT_PRESENT, "No SD card detected.", ""}};
  // Steps 1-9: load, parse, validate schema/events/overlaps/audio/durations/collisions, revision.
  Program candidate;
  ValidationErrors e = ProgramStore::load(newId, candidate);
  if (!e.empty()) return e;
  const std::string candidateRev = ProgramJson::revision(candidate);
  // Step 10: the schedule must be generable.
  {
    Scheduler s(candidate, candidateRev, TimeService::utcOffsetMinutes());
    ScheduledEvent next;
    if (!candidate.events.empty() && !s.nextEvent(TimeService::nowUtc(), next)) {
      return {{err::PROGRAM_JSON_INVALID, "Schedule preview produced no events.", "events"}};
    }
  }
  // Step 11: persist the selection; step 12 happens as the runtime observes generation().
  if (!DeviceConfig::setActiveProgram(newId)) {
    return {{err::INTERNAL, "Could not store the active Program ID.", ""}};
  }
  load();
  return {};
}

bool deactivate() {
  if (!DeviceConfig::setActiveProgram("")) return false;
  load();
  return true;
}

}  // namespace ActiveProgram
}  // namespace acorn
