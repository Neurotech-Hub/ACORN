#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "Program.h"

// The single authoritative interpreter of Program schedules (init.md
// sections 12-16, 19-21, 61-64). Everything that needs "when" asks this:
// RTC wake planning, status, BTN_01 audio choice, preview and tests.
// See docs/scheduling.md for the exact algorithm.

namespace acorn {

struct ScheduledEvent {
  size_t eventIndex = 0;  // into Program::events
  int64_t utc = 0;        // epoch seconds
  int64_t local = 0;      // local epoch seconds (utc + offset)
};

struct WindowState {
  enum class Kind : uint8_t { None, Active, Quiet, PointsOnly };
  Kind kind = Kind::None;
  size_t eventIndex = 0;  // range that is active / starts next
  int64_t untilUtc = 0;   // end of the active window / start of the next
  int64_t untilLocal = 0;
};

class Scheduler {
 public:
  // `program` must be valid (ProgramValidator) and outlive the Scheduler.
  Scheduler(const Program& program, std::string revision, int32_t utcOffsetMinutes);

  // Next `count` events strictly after `fromUtc`, ascending.
  std::vector<ScheduledEvent> preview(int64_t fromUtc, size_t count) const;
  bool nextEvent(int64_t nowUtc, ScheduledEvent& out) const;

  // Events with fromUtc <= t < toUtc, ascending.
  std::vector<ScheduledEvent> between(int64_t fromUtc, int64_t toUtc) const;

  // Earliest event within +/- toleranceS of nowUtc whose time is strictly
  // after `afterUtc` (the last executed scheduled time). Never returns
  // events outside the tolerance: missed events are not caught up.
  bool dueEvent(int64_t nowUtc, int64_t afterUtc, int32_t toleranceS, ScheduledEvent& out) const;

  WindowState windowState(int64_t nowUtc) const;

  const Program& program() const { return program_; }
  const std::string& revision() const { return revision_; }
  int32_t utcOffsetMinutes() const { return offsetS_ / 60; }

  // Occurrences (local epoch seconds) of one event for the schedule
  // instance starting on local day `dayStart`, restricted to lo <= t < hi,
  // at most `cap` values appended to `out`.
  void occurrences(size_t eventIndex, int64_t dayStart, int64_t lo, int64_t hi, size_t cap,
                   std::vector<int64_t>& out) const;

 private:
  const Program& program_;
  std::string revision_;
  int64_t offsetS_;
};

int64_t floorDiv(int64_t a, int64_t b);

}  // namespace acorn
