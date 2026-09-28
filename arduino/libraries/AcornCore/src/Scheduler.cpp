#include "Scheduler.h"

#include <math.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "Hash.h"
#include "AcornLimits.h"
#include "UtcTime.h"

namespace acorn {

namespace {

constexpr int64_t DAY = SECONDS_PER_DAY;

struct Pending {
  int64_t local;
  size_t eventIndex;
};

bool earlier(const Pending& a, const Pending& b) {
  return a.local != b.local ? a.local < b.local : a.eventIndex < b.eventIndex;
}

}  // namespace

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

Scheduler::Scheduler(const Program& program, std::string revision, int32_t utcOffsetMinutes)
    : program_(program), revision_(std::move(revision)), offsetS_(static_cast<int64_t>(utcOffsetMinutes) * 60) {}

void Scheduler::occurrences(size_t eventIndex, int64_t dayStart, int64_t lo, int64_t hi, size_t cap,
                            std::vector<int64_t>& out) const {
  if (cap == 0) return;
  const Event& ev = program_.events[eventIndex];
  if (ev.type == EventType::Point) {
    const int64_t t = dayStart + ev.timeS;
    if (t >= lo && t < hi) out.push_back(t);
    return;
  }
  const int64_t s = dayStart + ev.startS;
  int64_t e = dayStart + ev.endS;
  if (e <= s) e += DAY;
  const int64_t stop = std::min(e, hi);
  size_t added = 0;

  if (ev.recurrence.type == RecurrenceType::Fixed) {
    const int64_t iv = ev.recurrence.intervalS;
    if (iv <= 0) return;
    int64_t n = lo > s ? (lo - s + iv - 1) / iv : 0;  // anchor-based: t = s + n * iv
    for (int64_t t = s + n * iv; t < stop && added < cap; t = s + (++n) * iv) {
      out.push_back(t);
      added++;
    }
    return;
  }

  // Random: seeded by revision | event_id | local date of the range start,
  // so the day's sequence survives reboots and matches preview.
  char date[12];
  UtcTime::formatEpochDate(dayStart, date, sizeof(date));
  Mulberry32 rng(fnv1a(revision_ + "|" + ev.eventId + "|" + date));
  const int64_t mn = ev.recurrence.minS;
  const int64_t span = ev.recurrence.maxS - mn + 1;
  if (mn <= 0 || span <= 0) return;
  for (int64_t t = s; t < stop; t += mn + static_cast<int64_t>(floor(rng.next() * static_cast<double>(span)))) {
    if (t >= lo) {
      out.push_back(t);
      if (++added >= cap) return;
    }
  }
}

std::vector<ScheduledEvent> Scheduler::preview(int64_t fromUtc, size_t count) const {
  std::vector<ScheduledEvent> result;
  if (program_.events.empty() || count == 0) return result;
  const int64_t now = fromUtc + offsetS_;
  const int64_t firstDay = floorDiv(now, DAY) * DAY - DAY;
  std::vector<Pending> pending;
  std::vector<int64_t> times;

  for (int64_t day = firstDay; day < firstDay + limits::PREVIEW_HORIZON_DAYS * DAY && result.size() < count;
       day += DAY) {
    const size_t cap = count - result.size();
    for (size_t i = 0; i < program_.events.size(); i++) {
      times.clear();
      occurrences(i, day, now + 1, std::numeric_limits<int64_t>::max(), cap, times);
      for (int64_t t : times) pending.push_back({t, i});
    }
    std::sort(pending.begin(), pending.end(), earlier);
    const int64_t boundary = day + DAY;
    size_t taken = 0;
    while (taken < pending.size() && pending[taken].local < boundary && result.size() < count) {
      result.push_back({pending[taken].eventIndex, pending[taken].local - offsetS_, pending[taken].local});
      taken++;
    }
    pending.erase(pending.begin(), pending.begin() + taken);
  }
  return result;
}

bool Scheduler::nextEvent(int64_t nowUtc, ScheduledEvent& out) const {
  const std::vector<ScheduledEvent> next = preview(nowUtc, 1);
  if (next.empty()) return false;
  out = next.front();
  return true;
}

std::vector<ScheduledEvent> Scheduler::between(int64_t fromUtc, int64_t toUtc) const {
  std::vector<Pending> found;
  std::vector<int64_t> times;
  const int64_t from = fromUtc + offsetS_;
  const int64_t to = toUtc + offsetS_;
  for (int64_t day = floorDiv(from, DAY) * DAY - DAY; day < to; day += DAY) {
    for (size_t i = 0; i < program_.events.size(); i++) {
      times.clear();
      occurrences(i, day, from, to, std::numeric_limits<size_t>::max(), times);
      for (int64_t t : times) found.push_back({t, i});
    }
  }
  std::sort(found.begin(), found.end(), earlier);
  std::vector<ScheduledEvent> result;
  result.reserve(found.size());
  for (const Pending& p : found) result.push_back({p.eventIndex, p.local - offsetS_, p.local});
  return result;
}

bool Scheduler::dueEvent(int64_t nowUtc, int64_t afterUtc, int32_t toleranceS, ScheduledEvent& out) const {
  for (const ScheduledEvent& e : between(nowUtc - toleranceS, nowUtc + toleranceS + 1)) {
    if (e.utc > afterUtc) {
      out = e;
      return true;
    }
  }
  return false;
}

WindowState Scheduler::windowState(int64_t nowUtc) const {
  WindowState w;
  const int64_t now = nowUtc + offsetS_;
  const int64_t today = floorDiv(now, DAY) * DAY;
  bool anyRange = false;
  int64_t nextStart = std::numeric_limits<int64_t>::max();
  for (size_t i = 0; i < program_.events.size(); i++) {
    const Event& ev = program_.events[i];
    if (ev.type != EventType::Range) continue;
    anyRange = true;
    for (int64_t day : {today - DAY, today, today + DAY}) {
      const int64_t s = day + ev.startS;
      int64_t e = day + ev.endS;
      if (e <= s) e += DAY;
      if (s <= now && now < e) {
        w.kind = WindowState::Kind::Active;
        w.eventIndex = i;
        w.untilLocal = e;
        w.untilUtc = e - offsetS_;
        return w;
      }
      if (s > now && s < nextStart) {
        nextStart = s;
        w.eventIndex = i;
      }
    }
  }
  if (!anyRange) {
    w.kind = WindowState::Kind::PointsOnly;
    return w;
  }
  w.kind = WindowState::Kind::Quiet;
  w.untilLocal = nextStart;
  w.untilUtc = nextStart - offsetS_;
  return w;
}

}  // namespace acorn
