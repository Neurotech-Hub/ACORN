// Host unit tests for AcornCore (init.md section 67). Build and run with
// ./run.sh. `--write-golden` regenerates golden/*.json.

#include <AcornCore.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "check.h"

using namespace acorn;

namespace {

class MapCatalog : public AudioCatalog {
 public:
  std::map<std::string, AudioMeta> files;
  MapCatalog& add(const std::string& path, uint32_t ms, bool valid = true) {
    AudioMeta m;
    m.valid = valid;
    m.durationMs = ms;
    if (!valid) m.errorMessage = "Unsupported: stereo";
    files[path] = m;
    return *this;
  }
  bool find(const std::string& path, AudioMeta& out) const override {
    auto it = files.find(path);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
};

MapCatalog defaultCatalog() {
  MapCatalog c;
  c.add("/audio/playback.wav", 5032).add("/audio/squirrel.wav", 3410).add("/audio/hawk.wav", 6120);
  c.add("/audio/owl-hoot.wav", 2240).add("/audio/stereo.wav", 1000, false);
  return c;
}

int64_t utc(const char* iso) {
  UtcDateTime t;
  if (!UtcTime::parseIso(iso, t)) {
    printf("bad iso %s\n", iso);
    return 0;
  }
  return UtcTime::toEpoch(t);
}

std::string iso(int64_t epoch) {
  char buf[24];
  UtcTime::formatEpochIso(epoch, buf, sizeof buf);
  return buf;
}

std::string localIso(int64_t local) {
  char buf[24];
  UtcTime::formatEpochLocalIso(local, buf, sizeof buf);
  return buf;
}

std::string programJson(const std::string& id, const std::string& events) {
  return "{\"schema_version\":1,\"id\":\"" + id + "\",\"name\":\"" + id + "\",\"description\":\"\",\"events\":[" +
         events + "]}";
}

std::string rangeFixed(const char* id, const char* start, const char* end, int iv,
                       const char* audio = "/audio/playback.wav", int volume = 80) {
  char buf[400];
  snprintf(buf, sizeof buf,
           "{\"event_id\":\"%s\",\"type\":\"range\",\"start\":\"%s\",\"end\":\"%s\",\"audio\":\"%s\",\"volume\":%d,"
           "\"recurrence\":{\"type\":\"fixed\",\"interval_seconds\":%d}}",
           id, start, end, audio, volume, iv);
  return buf;
}

std::string rangeRandom(const char* id, const char* start, const char* end, int mn, int mx,
                        const char* audio = "/audio/squirrel.wav", int volume = 75) {
  char buf[400];
  snprintf(buf, sizeof buf,
           "{\"event_id\":\"%s\",\"type\":\"range\",\"start\":\"%s\",\"end\":\"%s\",\"audio\":\"%s\",\"volume\":%d,"
           "\"recurrence\":{\"type\":\"random\",\"min_interval_seconds\":%d,\"max_interval_seconds\":%d}}",
           id, start, end, audio, volume, mn, mx);
  return buf;
}

std::string point(const char* id, const char* time, const char* audio = "/audio/hawk.wav", int volume = 70) {
  char buf[300];
  snprintf(buf, sizeof buf,
           "{\"event_id\":\"%s\",\"type\":\"point\",\"time\":\"%s\",\"audio\":\"%s\",\"volume\":%d}", id, time,
           audio, volume);
  return buf;
}

ValidationErrors validate(const std::string& json, Program* out = nullptr) {
  MapCatalog c = defaultCatalog();
  return ProgramValidator::validateText(json.c_str(), json.size(), c, out);
}

Program mustParse(const std::string& json) {
  Program p;
  ValidationErrors errs = validate(json, &p);
  for (const auto& e : errs) printf("  unexpected: %s %s %s\n", e.code.c_str(), e.field.c_str(), e.message.c_str());
  CHECK(errs.empty());
  return p;
}

bool hasCode(const ValidationErrors& errs, const char* code) {
  for (const auto& e : errs)
    if (e.code == code) return true;
  return false;
}

// The three programs the mock ships with (dev/mock/state.mjs).
const std::string DEFAULT_PROGRAM = programJson("default", rangeFixed("daytime-playback", "06:00:00", "18:00:00", 600));
const std::string SQUIRREL_PROGRAM =
    programJson("squirrel-study", rangeRandom("random-morning", "06:00:00", "12:00:00", 480, 720) + "," +
                                      point("noon-call", "12:00:00"));
const std::string NIGHT_PROGRAM =
    programJson("night-owl", rangeFixed("overnight", "20:00:00", "04:00:00", 900, "/audio/owl-hoot.wav", 65));

std::vector<std::string> localTimes(const std::vector<ScheduledEvent>& evs) {
  std::vector<std::string> out;
  for (const auto& e : evs) out.push_back(localIso(e.local));
  return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

// ---- Time -------------------------------------------------------------------

TEST(utc_time_round_trip) {
  CHECK_EQ(utc("2026-09-27T06:00:00Z"), (int64_t)1790488800);
  CHECK_EQ(iso(1790488800), std::string("2026-09-27T06:00:00Z"));
  CHECK_EQ(iso(utc("2028-02-29T23:59:59Z")), std::string("2028-02-29T23:59:59Z"));
  UtcDateTime bad;
  CHECK(!UtcTime::parseIso("2026-02-30T00:00:00Z", bad));
  char d[12];
  UtcTime::formatEpochDate(utc("2026-09-27T23:59:59Z"), d, sizeof d);
  CHECK_EQ(std::string(d), std::string("2026-09-27"));
}

TEST(time_of_day_parse) {
  CHECK_EQ(parseTimeOfDay("06:00:00"), 21600);
  CHECK_EQ(parseTimeOfDay("06:00"), 21600);
  CHECK_EQ(parseTimeOfDay("23:59:59"), 86399);
  CHECK_EQ(parseTimeOfDay("24:00:00"), -1);
  CHECK_EQ(parseTimeOfDay("6:00"), -1);
  CHECK_EQ(parseTimeOfDay("06:60"), -1);
}

// ---- Scheduling vectors (section 67) ---------------------------------------

TEST(fixed_range_72_events_last_1750_no_1800) {
  Program p = mustParse(DEFAULT_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), 0);
  auto evs = s.between(utc("2026-09-27T00:00:00Z"), utc("2026-09-28T00:00:00Z"));
  CHECK_EQ(evs.size(), (size_t)72);
  auto t = localTimes(evs);
  CHECK_EQ(t.front(), std::string("2026-09-27T06:00:00"));
  CHECK_EQ(t.back(), std::string("2026-09-27T17:50:00"));
  CHECK(!contains(t, "2026-09-27T18:00:00"));
}

TEST(cross_midnight_range) {
  Program p = mustParse(NIGHT_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), 0);
  auto evs = s.between(utc("2026-09-27T00:00:00Z"), utc("2026-09-28T00:00:00Z"));
  auto t = localTimes(evs);
  CHECK_EQ(evs.size(), (size_t)32);  // 00:00-03:45 of the previous window + 20:00-23:45
  CHECK_EQ(t.front(), std::string("2026-09-27T00:00:00"));
  CHECK(contains(t, "2026-09-27T03:45:00"));
  CHECK(!contains(t, "2026-09-27T04:00:00"));
  CHECK(contains(t, "2026-09-27T20:00:00"));
  CHECK_EQ(t.back(), std::string("2026-09-27T23:45:00"));
  WindowState w = s.windowState(utc("2026-09-27T02:00:00Z"));
  CHECK(w.kind == WindowState::Kind::Active);
  CHECK_EQ(localIso(w.untilLocal), std::string("2026-09-27T04:00:00"));
}

TEST(missed_event_is_not_replayed) {
  Program p = mustParse(programJson("missed", rangeFixed("r", "08:00:00", "09:00:00", 600)));
  Scheduler s(p, ProgramJson::revision(p), 0);
  const int64_t now = utc("2026-09-27T08:17:00Z");
  ScheduledEvent next;
  CHECK(s.nextEvent(now, next));
  CHECK_EQ(iso(next.utc), std::string("2026-09-27T08:20:00Z"));
  ScheduledEvent due;
  CHECK(!s.dueEvent(now, utc("2026-09-27T08:00:00Z"), limits::EVENT_DUE_TOLERANCE_S, due));
}

TEST(due_event_tolerance_and_dedupe) {
  Program p = mustParse(DEFAULT_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), 0);
  ScheduledEvent due;
  CHECK(s.dueEvent(utc("2026-09-27T06:00:05Z"), 0, 10, due));
  CHECK_EQ(iso(due.utc), std::string("2026-09-27T06:00:00Z"));
  CHECK(s.dueEvent(utc("2026-09-27T05:59:55Z"), 0, 10, due));  // slightly early RTC wake
  CHECK(!s.dueEvent(utc("2026-09-27T06:00:05Z"), due.utc, 10, due));  // already executed
  CHECK(!s.dueEvent(utc("2026-09-27T06:00:30Z"), 0, 10, due));
}

TEST(random_reproducible_and_bounded) {
  Program p = mustParse(SQUIRREL_PROGRAM);
  const std::string rev = ProgramJson::revision(p);
  Scheduler a(p, rev, 0), b(p, rev, 0);
  auto pa = a.preview(utc("2026-09-27T00:00:00Z"), 60);
  auto pb = b.preview(utc("2026-09-27T00:00:00Z"), 60);
  CHECK_EQ(pa.size(), pb.size());
  CHECK(localTimes(pa) == localTimes(pb));

  const int64_t day = utc("2026-09-27T00:00:00Z");
  std::vector<int64_t> occ;
  a.occurrences(0, day, day, day + 2 * SECONDS_PER_DAY, 1000, occ);
  CHECK(!occ.empty());
  CHECK_EQ(occ.front(), day + 6 * 3600);
  for (size_t i = 1; i < occ.size(); i++) {
    const int64_t gap = occ[i] - occ[i - 1];
    CHECK(gap >= 480 && gap <= 720);
  }
  CHECK(occ.back() < day + 12 * 3600);
}

TEST(random_changes_with_date) {
  Program p = mustParse(SQUIRREL_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), 0);
  const int64_t d1 = utc("2026-09-27T00:00:00Z"), d2 = d1 + SECONDS_PER_DAY;
  std::vector<int64_t> o1, o2;
  s.occurrences(0, d1, d1, d1 + SECONDS_PER_DAY, 1000, o1);
  s.occurrences(0, d2, d2, d2 + SECONDS_PER_DAY, 1000, o2);
  for (auto& t : o2) t -= SECONDS_PER_DAY;
  CHECK(o1 != o2);
}

TEST(random_changes_with_revision) {
  Program p = mustParse(SQUIRREL_PROGRAM);
  Program q = p;
  q.events[0].volume = 76;  // any schedule-relevant edit changes the revision
  const std::string r1 = ProgramJson::revision(p), r2 = ProgramJson::revision(q);
  CHECK(r1 != r2);
  Program named = p;
  named.name = "Renamed";
  named.description = "different";
  CHECK_EQ(ProgramJson::revision(named), r1);
  Scheduler a(p, r1, 0), b(q, r2, 0);
  const int64_t d = utc("2026-09-27T00:00:00Z");
  std::vector<int64_t> o1, o2;
  a.occurrences(0, d, d, d + SECONDS_PER_DAY, 1000, o1);
  b.occurrences(0, d, d, d + SECONDS_PER_DAY, 1000, o2);
  CHECK(o1 != o2);
}

TEST(utc_offset_change) {
  Program p = mustParse(DEFAULT_PROGRAM);
  const std::string rev = ProgramJson::revision(p);
  const int64_t from = utc("2026-09-27T00:00:00Z");
  ScheduledEvent e;
  Scheduler west(p, rev, -300);
  CHECK(west.nextEvent(from, e));
  CHECK_EQ(iso(e.utc), std::string("2026-09-27T11:00:00Z"));
  CHECK_EQ(localIso(e.local), std::string("2026-09-27T06:00:00"));
  Scheduler east(p, rev, 570);
  CHECK(east.nextEvent(from, e));
  CHECK_EQ(iso(e.utc), std::string("2026-09-27T00:10:00Z"));  // 00:00Z is 09:30 local at +09:30
  CHECK_EQ(localIso(e.local), std::string("2026-09-27T09:40:00"));
}

TEST(rtc_moved_forward_and_backward) {
  Program p = mustParse(DEFAULT_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), 0);
  ScheduledEvent e;
  CHECK(s.nextEvent(utc("2026-09-27T10:03:00Z"), e));  // jumped forward from 08:17
  CHECK_EQ(iso(e.utc), std::string("2026-09-27T10:10:00Z"));
  CHECK(s.nextEvent(utc("2026-09-27T07:01:00Z"), e));  // jumped backward
  CHECK_EQ(iso(e.utc), std::string("2026-09-27T07:10:00Z"));
  // After a time change the runtime resets its "last executed" marker, so
  // the recomputed schedule plays normally from the new time.
  CHECK(s.dueEvent(utc("2026-09-27T07:10:02Z"), 0, 10, e));
}

TEST(preview_is_sorted_and_counted) {
  Program p = mustParse(SQUIRREL_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), -300);
  const int64_t from = utc("2026-09-27T12:00:00Z");
  auto evs = s.preview(from, 200);
  CHECK_EQ(evs.size(), (size_t)200);
  CHECK(evs.front().utc > from);
  for (size_t i = 1; i < evs.size(); i++) CHECK(evs[i].utc >= evs[i - 1].utc);
  Program empty = mustParse(programJson("empty", ""));
  Scheduler none(empty, ProgramJson::revision(empty), 0);
  CHECK(none.preview(from, 10).empty());
  ScheduledEvent e;
  CHECK(!none.nextEvent(from, e));
}

TEST(window_state_quiet_and_points_only) {
  Program p = mustParse(DEFAULT_PROGRAM);
  Scheduler s(p, ProgramJson::revision(p), 0);
  WindowState w = s.windowState(utc("2026-09-27T19:00:00Z"));
  CHECK(w.kind == WindowState::Kind::Quiet);
  CHECK_EQ(iso(w.untilUtc), std::string("2026-09-28T06:00:00Z"));
  w = s.windowState(utc("2026-09-27T10:00:00Z"));
  CHECK(w.kind == WindowState::Kind::Active);
  CHECK_EQ(iso(w.untilUtc), std::string("2026-09-27T18:00:00Z"));
  Program pts = mustParse(programJson("pts", point("a", "12:00:00")));
  Scheduler ps(pts, ProgramJson::revision(pts), 0);
  CHECK(ps.windowState(utc("2026-09-27T10:00:00Z")).kind == WindowState::Kind::PointsOnly);
}

// ---- Validation vectors ----------------------------------------------------

TEST(default_programs_validate) {
  CHECK(validate(DEFAULT_PROGRAM).empty());
  CHECK(validate(SQUIRREL_PROGRAM).empty());
  CHECK(validate(NIGHT_PROGRAM).empty());
}

TEST(overlapping_ranges_rejected) {
  auto errs = validate(programJson("o", rangeFixed("a", "06:00:00", "12:00:00", 600) + "," +
                                            rangeFixed("b", "11:00:00", "14:00:00", 600)));
  CHECK(hasCode(errs, err::PROGRAM_RANGE_OVERLAP));
  auto ok = validate(programJson("o", rangeFixed("a", "06:00:00", "12:00:00", 600) + "," +
                                          rangeFixed("b", "12:00:00", "14:00:00", 600)));
  CHECK(ok.empty());  // [start, end) ranges may touch
}

TEST(cross_midnight_overlap_rejected) {
  auto errs = validate(programJson("o", rangeFixed("a", "20:00:00", "04:00:00", 900, "/audio/owl-hoot.wav") +
                                            "," + rangeFixed("b", "03:00:00", "05:00:00", 900)));
  CHECK(hasCode(errs, err::PROGRAM_RANGE_OVERLAP));
}

TEST(interval_shorter_than_wav_rejected) {
  // playback.wav is 5032 ms; with the 2000 ms guard the interval must exceed 7032 ms.
  auto errs = validate(programJson("i", rangeFixed("a", "06:00:00", "18:00:00", 7)));
  CHECK(hasCode(errs, err::PROGRAM_INTERVAL_TOO_SHORT));
  CHECK(validate(programJson("i", rangeFixed("a", "06:00:00", "18:00:00", 8))).empty());
}

TEST(random_minimum_shorter_than_wav_rejected) {
  auto errs = validate(programJson("r", rangeRandom("a", "06:00:00", "12:00:00", 5, 60)));  // squirrel 3410 ms
  CHECK(hasCode(errs, err::PROGRAM_INTERVAL_TOO_SHORT));
  auto inverted = validate(programJson("r", rangeRandom("a", "06:00:00", "12:00:00", 600, 300)));
  CHECK(hasCode(inverted, err::PROGRAM_RANDOM_RANGE_INVALID));
}

TEST(missing_and_invalid_wav_rejected) {
  auto missing = validate(programJson("m", rangeFixed("a", "06:00:00", "18:00:00", 600, "/audio/nope.wav")));
  CHECK(hasCode(missing, err::PROGRAM_AUDIO_MISSING));
  auto bad = validate(programJson("m", rangeFixed("a", "06:00:00", "18:00:00", 600, "/audio/stereo.wav")));
  CHECK(hasCode(bad, err::PROGRAM_AUDIO_INVALID));
}

TEST(point_collisions_rejected) {
  auto inRange = validate(programJson("c", rangeFixed("a", "06:00:00", "18:00:00", 600) + "," +
                                               point("p", "12:00:00")));
  CHECK(hasCode(inRange, err::PROGRAM_EVENT_COLLISION));
  auto twoPoints = validate(programJson("c", point("p", "12:00:00") + "," + point("q", "12:00:03")));
  CHECK(hasCode(twoPoints, err::PROGRAM_EVENT_COLLISION));
}

TEST(schema_limits_and_ids) {
  std::string json = DEFAULT_PROGRAM;
  json.replace(json.find("\"schema_version\":1"), 18, "\"schema_version\":2");
  CHECK(hasCode(validate(json), err::PROGRAM_SCHEMA_UNSUPPORTED));
  CHECK(hasCode(validate("{not json"), err::PROGRAM_JSON_INVALID));
  std::string many;
  for (int i = 0; i < 33; i++) {
    char id[8], t[12];
    snprintf(id, sizeof id, "p%d", i);
    snprintf(t, sizeof t, "%02d:%02d:00", i / 2, (i % 2) * 30);
    if (i) many += ",";
    many += point(id, t);
  }
  CHECK(hasCode(validate(programJson("many", many)), err::PROGRAM_LIMIT_EXCEEDED));
  CHECK(ProgramValidator::isValidId("squirrel-study"));
  CHECK(!ProgramValidator::isValidId("Squirrel"));
  CHECK(!ProgramValidator::isValidId("-lead"));
  CHECK(!ProgramValidator::isValidId(""));
  auto dup = validate(programJson("d", point("x", "01:00:00") + "," + point("x", "02:00:00")));
  CHECK(!dup.empty());
  auto vol = validate(programJson("v", point("x", "01:00:00", "/audio/hawk.wav", 101)));
  CHECK(!vol.empty());
}

TEST(json_round_trip_normalizes) {
  Program p = mustParse(programJson("rt", rangeFixed("a", "06:00", "18:00", 600) + "," + point("b", "19:30")));
  const std::string text = ProgramJson::toString(p);
  CHECK(text.find("\"06:00:00\"") != std::string::npos);
  CHECK(text.find("\"19:30:00\"") != std::string::npos);
  Program q = mustParse(text);
  CHECK_EQ(ProgramJson::toString(q), text);
  CHECK_EQ(ProgramJson::revision(q), ProgramJson::revision(p));
  CHECK_EQ(ProgramJson::revision(p).size(), (size_t)8);
  JsonDocument doc;
  deserializeJson(doc, text);
  CHECK_EQ(ProgramJson::rawRevision(doc.as<JsonVariantConst>()), ProgramJson::revision(p));
}

// ---- Battery, environment, wake planning, CSV ------------------------------

TEST(battery_percent_and_btn02_flashes) {
  BatteryModel m = BatteryModel::defaultM12();
  CHECK(m.isValid());
  CHECK_EQ(m.percent(13.5), 100);
  CHECK_EQ(m.percent(8.0), 0);
  CHECK_EQ(BatteryIndication::flashCount(false, 50), 0);
  CHECK_EQ(BatteryIndication::flashCount(true, 0), 1);
  CHECK_EQ(BatteryIndication::flashCount(true, 1), 1);
  CHECK_EQ(BatteryIndication::flashCount(true, 10), 1);
  CHECK_EQ(BatteryIndication::flashCount(true, 11), 2);
  CHECK_EQ(BatteryIndication::flashCount(true, 22), 3);
  CHECK_EQ(BatteryIndication::flashCount(true, 51), 6);
  CHECK_EQ(BatteryIndication::flashCount(true, 74), 8);
  CHECK_EQ(BatteryIndication::flashCount(true, 93), 10);
  CHECK_EQ(BatteryIndication::flashCount(true, 100), 10);
  BatteryModel bad = m;
  std::swap(bad.points[0], bad.points[1]);
  CHECK(!bad.isValid());
}

TEST(battery_percent_monotonic) {
  BatteryModel m = BatteryModel::defaultM12();
  int32_t last = 101;
  for (double v = 13.0; v >= 9.0; v -= 0.01) {
    const int32_t pct = m.percent(v);
    CHECK(pct <= last);
    CHECK(pct >= 0 && pct <= 100);
    last = pct;
  }
}

TEST(env_sampling_epoch_aligned) {
  CHECK_EQ(iso(EnvSchedule::nextSampleAfter(utc("2026-09-27T12:03:10Z"), 10)), std::string("2026-09-27T12:10:00Z"));
  CHECK_EQ(iso(EnvSchedule::nextSampleAfter(utc("2026-09-27T12:10:00Z"), 10)), std::string("2026-09-27T12:20:00Z"));
  CHECK_EQ(iso(EnvSchedule::sampleAtOrBefore(utc("2026-09-27T12:10:00Z"), 10)), std::string("2026-09-27T12:10:00Z"));
  CHECK_EQ(iso(EnvSchedule::nextSampleAfter(utc("2026-09-27T23:59:00Z"), 1440)), std::string("2026-09-28T00:00:00Z"));
  CHECK(!EnvSchedule::isValidInterval(0));
  CHECK(EnvSchedule::isValidInterval(1440));
  CHECK(!EnvSchedule::isValidInterval(1441));
}

TEST(wake_planner_picks_earliest) {
  using WakePlanner::Reason;
  auto w = WakePlanner::earliest(1000, 900);
  CHECK(w.reason == Reason::EnvSample);
  CHECK_EQ(w.utc, (int64_t)900);
  w = WakePlanner::earliest(900, 900);
  CHECK(w.reason == Reason::Playback);
  w = WakePlanner::earliest(WakePlanner::NONE, 900);
  CHECK(w.reason == Reason::EnvSample);
  w = WakePlanner::earliest(WakePlanner::NONE, WakePlanner::NONE);
  CHECK(w.reason == Reason::None);
  CHECK(WakePlanner::isDue(995, 1000, 10));
  CHECK(!WakePlanner::isDue(980, 1000, 10));
}

TEST(csv_rows) {
  Csv::PlaybackRow r;
  r.actualUtc = "2026-09-27T06:00:01Z";
  r.scheduledUtc = "2026-09-27T06:00:00Z";
  r.programId = "default";
  r.revision = "0123abcd";
  r.eventId = "daytime-playback";
  r.source = "scheduled";
  r.audio = "/audio/playback.wav";
  r.volume = 80;
  r.durationMs = 5032;
  r.result = "ok";
  r.telemetry.batteryValid = true;
  r.telemetry.batteryVoltage = 12.614f;
  r.telemetry.batteryPercent = 92;
  CHECK_EQ(Csv::playbackRow(r), std::string("2026-09-27T06:00:01Z,2026-09-27T06:00:00Z,default,0123abcd,"
                                            "daytime-playback,scheduled,/audio/playback.wav,80,5032,ok,,12.61,92,,,"));
  Csv::Telemetry t;
  t.envValid = true;
  t.temperatureC = 21.26f;
  t.humidityPct = 48.04f;
  CHECK_EQ(Csv::environmentRow("2026-09-27T06:00:00Z", t), std::string("2026-09-27T06:00:00Z,21.3,48.0,,,"));
  CHECK_EQ(Csv::field("a,b"), std::string("\"a,b\""));
  CHECK_EQ(Csv::field("say \"hi\""), std::string("\"say \"\"hi\"\"\""));
}

// ---- Golden previews (compared against the mock by tools/check-mock-golden.mjs)

struct GoldenCase {
  const char* file;
  const std::string* program;
  int32_t offset;
  const char* from;
  size_t count;
};

static const GoldenCase GOLDEN[] = {
    {"default.json", &DEFAULT_PROGRAM, -300, "2026-09-27T12:00:00Z", 150},
    {"squirrel-study.json", &SQUIRREL_PROGRAM, -300, "2026-09-27T12:00:00Z", 150},
    {"night-owl.json", &NIGHT_PROGRAM, 330, "2026-09-27T12:00:00Z", 150},
    {"squirrel-utc.json", &SQUIRREL_PROGRAM, 0, "2027-03-01T00:00:00Z", 150},
};

static std::string goldenText(const GoldenCase& g) {
  Program p = mustParse(*g.program);
  const std::string rev = ProgramJson::revision(p);
  Scheduler s(p, rev, g.offset);
  JsonDocument doc;
  JsonObject prog = doc["program"].to<JsonObject>();
  ProgramJson::toJson(p, prog);
  doc["utc_offset_minutes"] = g.offset;
  doc["from_utc"] = g.from;
  doc["count"] = g.count;
  doc["revision"] = rev;
  JsonArray events = doc["events"].to<JsonArray>();
  for (const auto& e : s.preview(utc(g.from), g.count)) {
    JsonObject o = events.add<JsonObject>();
    o["event_id"] = p.events[e.eventIndex].eventId;
    o["scheduled_utc"] = iso(e.utc);
    o["scheduled_local"] = localIso(e.local);
  }
  std::string out;
  serializeJsonPretty(doc, out);
  return out + "\n";
}

static std::string goldenDir;
static bool writeGolden = false;

TEST(golden_previews) {
  for (const auto& g : GOLDEN) {
    const std::string path = goldenDir + "/" + g.file;
    const std::string text = goldenText(g);
    if (writeGolden) {
      std::ofstream(path) << text;
      continue;
    }
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    if (ss.str() != text) check::fail(__FILE__, __LINE__, "golden mismatch: " + path + " (run ./run.sh --write-golden)");
  }
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--write-golden")) writeGolden = true;
    if (!strcmp(argv[i], "--golden") && i + 1 < argc) goldenDir = argv[++i];
  }
  if (goldenDir.empty()) goldenDir = "golden";
  return check::runAll();
}
