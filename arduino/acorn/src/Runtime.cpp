#include "Runtime.h"

#include <AcornCore.h>
#include <AcornHardware.h>
#include <ArduinoJson.h>

#include "AppConfig.h"
#include "api/ApiServer.h"
#include "audio/PlaybackService.h"
#include "config/DeviceConfig.h"
#include "logging/EnvironmentLogger.h"
#include "logging/PlaybackLogger.h"
#include "power/SleepManager.h"
#include "program/ActiveProgram.h"
#include "program/AudioLibrary.h"
#include "services/BatteryService.h"
#include "services/EnvironmentService.h"
#include "services/FirmwareService.h"
#include "services/ConfigSession.h"
#include "services/LedService.h"
#include "services/Telemetry.h"
#include "services/TimeService.h"
#include "services/WifiService.h"
#include "state/DeviceState.h"

namespace acorn {
namespace Runtime {

namespace {

constexpr uint32_t SCHEDULE_CHECK_MS = 250;
constexpr uint32_t POWER_ON_AWAKE_MS = 30000;  // console access after reset/flash
constexpr uint32_t SERIAL_AWAKE_MS = 30000;    // stay awake after console input

// Kept across deep sleep so a quick re-wake never repeats an event or sample.
RTC_DATA_ATTR int64_t lastExecutedUtc = 0;       // scheduled time of the last executed event
RTC_DATA_ATTR int64_t lastEnvBoundaryUtc = -1;   // boundary of the last env sample

bool inBackground = false;
uint32_t lastScheduleCheckMs = 0;
uint32_t awakeUntilMs = 0;
uint32_t seenTimeGen = 0;
uint32_t seenProgramGen = 0;
int64_t envNextUtc = -1;
uint16_t envInterval = 0;
uint32_t envTimeGen = 0;
String serialLine;

const char* stateName(ActiveProgram::State s) {
  switch (s) {
    case ActiveProgram::State::None: return "none";
    case ActiveProgram::State::NoSd: return "no SD";
    case ActiveProgram::State::Missing: return "missing";
    case ActiveProgram::State::Invalid: return "invalid";
    case ActiveProgram::State::Ok: return "ok";
  }
  return "?";
}

void printFaults() {
  const std::vector<Fault> faults = DeviceState::faults();
  if (faults.empty()) Serial.println("faults: none (armed)");
  for (const Fault& f : faults) {
    Serial.printf("fault %-26s %s  %s\n", f.code.c_str(), f.blocking ? "BLOCKING" : "        ", f.message.c_str());
  }
}

void printStatus() {
  JsonDocument doc;
  DeviceState::statusJson(doc.to<JsonObject>());
  serializeJsonPretty(doc, Serial);
  Serial.println();
}

// ---- playback logging -----------------------------------------------------------

void logFinished() {
  PlaybackResult r;
  while (PlaybackService::takeFinished(r)) {
    Serial.printf("playback %s: %s %s (%u ms)%s%s\n", playbackSourceName(r.request.source),
                  r.request.tone ? "builtin:sine" : r.request.path.c_str(), r.result.c_str(), r.durationMs,
                  r.errorCode.length() ? " " : "", r.errorCode.c_str());
    if (!DeviceState::sdReady()) continue;  // e.g. BTN_01 sine test without SD
    Csv::PlaybackRow row;
    row.actualUtc = TimeService::isoUtc(r.startedUtc).c_str();
    if (r.request.scheduledUtc >= 0) row.scheduledUtc = TimeService::isoUtc(r.request.scheduledUtc).c_str();
    row.programId = r.request.programId.c_str();
    row.revision = r.request.revision.c_str();
    row.eventId = r.request.eventId.c_str();
    row.source = playbackSourceName(r.request.source);
    row.audio = r.request.tone ? "builtin:sine" : r.request.path.c_str();
    row.volume = r.request.volume;
    row.durationMs = r.durationMs;
    row.result = r.result.c_str();
    row.errorCode = r.errorCode.c_str();
    // Telemetry is sampled after playback so the amplifier load is off.
    row.telemetry = makeTelemetry(BatteryService::read(), EnvironmentService::sample());
    PlaybackLogger::append(row);
  }
}

// ---- scheduled playback -----------------------------------------------------------

void startScheduled(const Scheduler& s, const ScheduledEvent& due) {
  const Event& ev = s.program().events[due.eventIndex];
  PlaybackRequest req;
  req.source = PlaybackSource::Scheduled;
  req.path = ev.audio.c_str();
  req.volume = static_cast<uint8_t>(ev.volume);
  req.scheduledUtc = due.utc;
  req.eventId = ev.eventId.c_str();
  req.programId = ActiveProgram::id();
  req.revision = ActiveProgram::revision().c_str();
  const char* code;
  if (PlaybackService::start(req, code)) return;  // also stops any manual playback
  Serial.printf("scheduled %s failed: %s\n", req.eventId.c_str(), code);
  Csv::PlaybackRow row;
  row.actualUtc = TimeService::isoUtc(TimeService::nowUtc()).c_str();
  row.scheduledUtc = TimeService::isoUtc(due.utc).c_str();
  row.programId = req.programId.c_str();
  row.revision = req.revision.c_str();
  row.eventId = req.eventId.c_str();
  row.source = "scheduled";
  row.audio = req.path.c_str();
  row.volume = req.volume;
  row.result = "error";
  row.errorCode = code;
  row.telemetry = makeTelemetry(BatteryService::read(), EnvironmentService::sample());
  PlaybackLogger::append(row);
}

void checkSchedule() {
  if (millis() - lastScheduleCheckMs < SCHEDULE_CHECK_MS) return;
  lastScheduleCheckMs = millis();
  if (!TimeService::isValid()) return;
  const int64_t now = TimeService::nowUtc();

  // Time, offset or Program changed: recompute from "now" and never replay
  // what the change skipped (init.md section 50). dueEvent() only looks
  // within the tolerance, so forward jumps cannot replay anything; after a
  // large backward jump the marker is pulled back so events play again.
  if (TimeService::generation() != seenTimeGen || ActiveProgram::generation() != seenProgramGen) {
    if (TimeService::generation() != seenTimeGen) SleepManager::onTimeChanged();
    seenTimeGen = TimeService::generation();
    seenProgramGen = ActiveProgram::generation();
    if (lastExecutedUtc > now + limits::EVENT_DUE_TOLERANCE_S) lastExecutedUtc = now - limits::EVENT_DUE_TOLERANCE_S - 1;
  }

  if (!DeviceState::armed()) return;
  const Scheduler* s = ActiveProgram::scheduler();
  if (!s) return;
  ScheduledEvent due;
  if (!s->dueEvent(now, lastExecutedUtc, limits::EVENT_DUE_TOLERANCE_S, due)) return;
  // An early (RTC lead) wake finds the event inside the tolerance; start it
  // at its scheduled second, never before.
  if (due.utc > now) return;
  lastExecutedUtc = due.utc;
  startScheduled(*s, due);
}

// ---- environmental sampling -----------------------------------------------------------

void checkEnvironment() {
  const DeviceSettings& cfg = DeviceConfig::get();
  if (!cfg.envEnabled || !TimeService::isValid()) {
    envNextUtc = -1;
    return;
  }
  const int64_t now = TimeService::nowUtc();
  if (envNextUtc < 0 || envInterval != cfg.envIntervalMinutes || envTimeGen != TimeService::generation()) {
    envInterval = cfg.envIntervalMinutes;
    envTimeGen = TimeService::generation();
    if (lastEnvBoundaryUtc > now) lastEnvBoundaryUtc = -1;  // clock moved backward
    // A wake that lands just after a boundary (slow boot) still takes it.
    const int64_t recent = EnvSchedule::sampleAtOrBefore(now, envInterval);
    envNextUtc = recent > lastEnvBoundaryUtc && now - recent <= limits::EVENT_DUE_TOLERANCE_S
                     ? recent
                     : EnvSchedule::nextSampleAfter(now, envInterval);
  }
  if (now < envNextUtc || PlaybackService::isPlaying()) return;
  const int64_t boundary = EnvSchedule::sampleAtOrBefore(now, envInterval);
  const Csv::Telemetry t = makeTelemetry(BatteryService::read(), EnvironmentService::sample());
  if (DeviceState::sdReady()) EnvironmentLogger::append(boundary, t);
  lastEnvBoundaryUtc = boundary;
  envNextUtc = EnvSchedule::nextSampleAfter(now, envInterval);
}

// ---- buttons ------------------------------------------------------------------------

// init.md section 30: next scheduled event's WAV -> /audio/playback.wav -> sine.
void onButton1() {
  if (PlaybackService::isPlaying()) return;
  PlaybackRequest req;
  req.source = PlaybackSource::ManualTest;
  req.volume = app::FALLBACK_VOLUME;
  req.tone = true;
  if (ActiveProgram::program()) {
    req.programId = ActiveProgram::id();
    req.revision = ActiveProgram::revision().c_str();
  }
  if (DeviceState::sdReady()) {
    const Scheduler* s = ActiveProgram::scheduler();
    ScheduledEvent next;
    if (s && TimeService::isValid() && s->nextEvent(TimeService::nowUtc(), next)) {
      const Event& ev = s->program().events[next.eventIndex];
      const AudioEntry* a = AudioLibrary::findPath(ev.audio.c_str());
      if (a && a->valid()) {
        req.tone = false;
        req.path = ev.audio.c_str();
        req.volume = static_cast<uint8_t>(ev.volume);
        req.eventId = ev.eventId.c_str();
      }
    }
    if (req.tone) {
      const AudioEntry* a = AudioLibrary::findPath(app::FALLBACK_AUDIO);
      if (a && a->valid()) {
        req.tone = false;
        req.path = app::FALLBACK_AUDIO;
      }
    }
  }
  const char* code;
  if (!PlaybackService::start(req, code) && !req.tone) {
    req.tone = true;
    req.path = "";
    req.eventId = "";
    req.volume = app::FALLBACK_VOLUME;
    PlaybackService::start(req, code);
  }
}

// init.md section 31.
void onButton2() {
  const BatteryState b = BatteryService::read();
  const uint8_t flashes = BatteryIndication::flashCount(b.valid, b.percent);
  if (flashes) {
    Serial.printf("battery %.2f V %d%% -> %u flashes\n", b.voltage, static_cast<int>(b.percent), flashes);
    LedService::flashBattery(flashes);
  } else {
    Serial.println("battery reading invalid -> error flash");
    LedService::flashBatteryError();
  }
}

// ---- sleep ------------------------------------------------------------------------

// PREPARE_SLEEP -> DEEP_SLEEP. Returns only if sleep was refused.
void enterSleep() {
  String reason;
  SleepManager::enter(reason);
  Serial.printf("sleep refused: %s\n", reason.c_str());
  if (reason != "next event is too close to sleep") ESP.restart();  // peripherals were shut down
}

// A scheduled event inside the due window that has not run yet. The next-wake
// planner only looks strictly after "now", so on the event's own second it
// would already point at the following event.
bool scheduledEventPending() {
  if (!TimeService::isValid() || !DeviceState::armed()) return false;
  const Scheduler* s = ActiveProgram::scheduler();
  ScheduledEvent due;
  return s && s->dueEvent(TimeService::nowUtc(), lastExecutedUtc, limits::EVENT_DUE_TOLERANCE_S, due);
}

// SERVICE mode: sleep once nothing is pending (init.md sections 40, 56).
void maybeSleep() {
#if !ACORN_STAY_AWAKE
  if (ConfigSession::active() || static_cast<int32_t>(awakeUntilMs - millis()) > 0) return;
  if (PlaybackService::isPlaying() || LedService::isFlashing()) return;
  if (scheduledEventPending()) return;
  logFinished();  // a playback that just ended is logged before sleeping
  if (!SleepManager::wakeTooClose(TimeService::nowUtc())) enterSleep();
#endif
}

// ---- serial console -----------------------------------------------------------------

void runCommand(const String& cmd) {
  if (cmd == "status") {
    printStatus();
  } else if (cmd == "faults") {
    printFaults();
  } else if (cmd == "wake") {
    const int64_t now = TimeService::nowUtc();
    const WakePlanner::Wake w = SleepManager::nextWake(now);
    if (w.reason == WakePlanner::Reason::None) Serial.println("next wake: none");
    else Serial.printf("next wake: %s (%s)\n", TimeService::isoUtc(w.utc).c_str(),
                       w.reason == WakePlanner::Reason::Playback ? "playback" : "env sample");
  } else if (cmd == "sleep") {
    ConfigSession::end();
    enterSleep();
  } else if (cmd == "session") {
    if (ConfigSession::active()) ConfigSession::end();
    else ConfigSession::start();
  } else if (cmd == "door") {
    const bool open = HallHardware::isDoorOpen();
    Serial.printf("door %s; sleep would arm the Hall wake for door %s\n", open ? "open" : "closed",
                  open ? "close" : "open");
  } else if (cmd == "btn1") {
    onButton1();
  } else if (cmd == "btn2") {
    onButton2();
  } else if (cmd.length()) {
    Serial.println("commands: status | faults | wake | sleep | session | door | btn1 | btn2");
  }
}

void pollSerial() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    awakeUntilMs = millis() + SERIAL_AWAKE_MS;
    if (c == '\n') {
      serialLine.trim();
      runCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 64) {
      serialLine += c;
    }
  }
}

void selfCheck(WakeSource wake) {
  const FirmwareInfo fw = FirmwareService::info();
  Serial.printf("\n%s \"%s\"  firmware %s (%s)\n", DeviceConfig::deviceId(), DeviceConfig::get().name.c_str(),
                fw.version.c_str(), fw.runningPartition.c_str());
  Serial.printf("wake=%s reset=%s\n", wakeSourceName(wake), SystemHardware::resetReason());
  const int64_t now = TimeService::nowUtc();
  Serial.printf("time %s (local %s, offset %+d min) valid=%d rtc=%d\n", TimeService::isoUtc(now).c_str(),
                TimeService::isoLocal(now).c_str(), static_cast<int>(TimeService::utcOffsetMinutes()),
                TimeService::isValid(), TimeService::rtcValid());
  const BatteryState& b = BatteryService::last();
  Serial.printf("battery %s", b.valid ? "" : "invalid");
  if (b.valid) Serial.printf("%.2f V (%d%%)", b.voltage, static_cast<int>(b.percent));
  const EnvState& e = EnvironmentService::last();
  Serial.printf("  env t=%s rh=%s lux=%s\n", e.envValid ? String(e.temperatureC, 1).c_str() : "-",
                e.envValid ? String(e.humidityPct, 1).c_str() : "-", e.lightValid ? String(e.lux, 1).c_str() : "-");
  const SdInfo& sd = DeviceState::sdInfo();
  Serial.printf("sd present=%d mounted=%d free=%llu MB  audio files=%u\n", DeviceState::sdPresent(),
                DeviceState::sdReady(), (sd.totalBytes - sd.usedBytes) / (1024ULL * 1024ULL),
                static_cast<unsigned>(DeviceState::sdReady() ? AudioLibrary::list().size() : 0));
  Serial.printf("program '%s' %s rev %s\n", ActiveProgram::id().c_str(), stateName(ActiveProgram::state()),
                ActiveProgram::revision().empty() ? "-" : ActiveProgram::revision().c_str());
  printFaults();
}

}  // namespace

void setup() {
  const WakeSource wake = SleepHardware::wakeSource();
  Serial.begin(115200);
  // Waiting for a USB host only makes sense after a reset; scheduled wakes
  // must not lose time here.
  if (wake == WakeSource::PowerOn) {
    const uint32_t start = millis();
    while (!Serial && millis() - start < 1500) delay(10);
  }

  // Minimum hardware, then section 56 order: NVS, RTC/time, battery, SD,
  // Program, logging.
  LedService::begin();
  ButtonHardware::begin();
  HallHardware::begin();
  AudioHardware::begin();
  SdHardware::begin();

  DeviceConfig::begin();
  RtcHardware::begin();
  TimeService::begin();
  BatteryService::begin();
  BatteryService::read();
  EnvironmentService::begin();
  EnvironmentService::sample();
  DeviceState::begin(wake);
  PlaybackService::begin();
  seenTimeGen = TimeService::generation();
  seenProgramGen = ActiveProgram::generation();

  selfCheck(wake);
  FirmwareService::markRunningValid();

  ApiServer::setBackgroundHook(background);
#if ACORN_STAY_AWAKE
  ConfigSession::start(true);
#else
  const bool doorOpen = HallHardware::isDoorOpen();
  // CONFIG only on a door-open wake, or a reset with the door open. A door
  // left open after a session ended does not restart the AP on other wakes.
  if (doorOpen && (wake == WakeSource::Hall || wake == WakeSource::PowerOn)) ConfigSession::start();
  if (wake == WakeSource::Hall && !doorOpen) Serial.println("door closed while asleep");
  if (wake == WakeSource::PowerOn) awakeUntilMs = millis() + POWER_ON_AWAKE_MS;
#endif

  if (wake == WakeSource::Button1) onButton1();
  if (wake == WakeSource::Button2) onButton2();
  Serial.println("type 'help' for console commands");
}

void background() {
  if (inBackground) return;
  inBackground = true;
  logFinished();
  checkSchedule();
  checkEnvironment();
  inBackground = false;
}

void loop() {
  ButtonHardware::update();
  HallHardware::update();
  LedService::update();
  DeviceState::update();
  TimeService::update();
  FirmwareService::update();
  ConfigSession::update();
  background();

  if (ButtonHardware::wasPressed(Button::Audio)) onButton1();
  if (ButtonHardware::wasPressed(Button::Battery)) onButton2();
  if (HallHardware::wasOpened()) {
    Serial.println("door opened");
    ConfigSession::start();
  }
  if (HallHardware::wasClosed()) {
    Serial.println("door closed");
    ConfigSession::onDoorClosed();
  }

  pollSerial();
  maybeSleep();
  delay(2);
}

}  // namespace Runtime
}  // namespace acorn
