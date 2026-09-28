#include "DeviceState.h"

#include "../AppConfig.h"
#include "../audio/PlaybackService.h"
#include "../config/DeviceConfig.h"
#include "../logging/EnvironmentLogger.h"
#include "../logging/PlaybackLogger.h"
#include "../program/ActiveProgram.h"
#include "../program/AudioLibrary.h"
#include "../program/ProgramStore.h"
#include "../services/BatteryService.h"
#include "../services/EnvironmentService.h"
#include "../services/TimeService.h"
#include "../services/WifiService.h"
#include "JsonUtil.h"

namespace acorn {
namespace DeviceState {

namespace {

constexpr uint32_t POLL_MS = 1000;
constexpr uint32_t MOUNT_RETRY_MS = 5000;
constexpr uint32_t SD_INFO_MS = 30000;

WakeSource wake = WakeSource::Unknown;
SdStatus mountStatus = SdStatus::NotPresent;
SdInfo info;
bool full = false;
bool logWriteFailed = false;
uint32_t lastPollMs = 0;
uint32_t lastMountAttemptMs = 0;
uint32_t lastInfoMs = 0;
bool everAttempted = false;

void onMounted() {
  AudioLibrary::invalidate();
  ProgramStore::invalidate();
  ActiveProgram::reload();
  refreshSd();
  reportLogWrite(PlaybackLogger::ensure() && EnvironmentLogger::ensure());
  PlaybackLogger::reloadLast();
}

void onUnmounted() {
  AudioLibrary::invalidate();
  ProgramStore::invalidate();
  ActiveProgram::reload();
  info = SdInfo{};
  full = false;
  logWriteFailed = false;
}

void tryMount() {
  lastMountAttemptMs = millis();
  everAttempted = true;
  mountStatus = SdHardware::mount();
  if (mountStatus == SdStatus::Ok) onMounted();
}

const char* windowName(WindowState::Kind k) {
  switch (k) {
    case WindowState::Kind::Active: return "active";
    case WindowState::Kind::Quiet: return "quiet";
    default: return "points_only";
  }
}

}  // namespace

void begin(WakeSource w) {
  wake = w;
  if (SdHardware::isCardPresent()) tryMount();
  else mountStatus = SdStatus::NotPresent;
  lastPollMs = millis();
}

void update() {
  const uint32_t now = millis();
  if (now - lastPollMs < POLL_MS) return;
  lastPollMs = now;
  const bool present = SdHardware::isCardPresent();
  if (present && !SdHardware::isMounted()) {
    if (!everAttempted || now - lastMountAttemptMs >= MOUNT_RETRY_MS) tryMount();
  } else if (!present && SdHardware::isMounted()) {
    PlaybackService::stop();
    SdHardware::unmount();
    mountStatus = SdStatus::NotPresent;
    onUnmounted();
  } else if (!present) {
    mountStatus = SdStatus::NotPresent;
    everAttempted = false;
  }
  if (SdHardware::isMounted() && now - lastInfoMs >= SD_INFO_MS) refreshSd();
}

bool sdPresent() { return SdHardware::isCardPresent(); }
bool sdReady() { return SdHardware::isMounted(); }
const SdInfo& sdInfo() { return info; }
bool sdFull() { return full; }

void refreshSd() {
  lastInfoMs = millis();
  if (!SdHardware::isMounted()) return;
  info = SdHardware::info();
  full = info.totalBytes > info.usedBytes ? (info.totalBytes - info.usedBytes) < SdHardware::MIN_FREE_BYTES : true;
}

void reportLogWrite(bool ok) { logWriteFailed = !ok; }

WakeSource wakeSource() { return wake; }

std::vector<Fault> faults() {
  std::vector<Fault> f;
  auto add = [&](const char* code, const String& message, bool blocking, const char* area) -> Fault& {
    Fault x;
    x.code = code;
    x.message = message;
    x.blocking = blocking;
    x.area = area;
    f.push_back(x);
    return f.back();
  };

  if (!TimeService::isValid()) {
    add(err::TIME_NOT_SET, "Device clock is not set. Synchronize time from this browser.", true, "device");
  } else if (!TimeService::rtcValid()) {
    add(err::RTC_INVALID, "The RTC is not holding time; it will be lost on power loss or sleep. Check the RTC and its backup cell.",
        true, "device");
  }

  const bool present = SdHardware::isCardPresent();
  if (!present) {
    add(err::SD_NOT_PRESENT, "No SD card detected. Insert a FAT32-formatted microSD card.", true, "status");
  } else if (!SdHardware::isMounted()) {
    add(err::SD_MOUNT_FAILED, "The SD card could not be mounted. Check that it is FAT32-formatted.", true, "status");
  } else {
    if (full) add(err::SD_FULL, "SD card is full. Logs cannot be written reliably.", true, "data");
    if (logWriteFailed) add(err::SD_WRITE_FAILED, "Writing a log to the SD card failed.", true, "data");
    switch (ActiveProgram::state()) {
      case ActiveProgram::State::None:
        add(err::PROGRAM_NOT_FOUND, "No active program. Create or activate a program.", true, "programs");
        break;
      case ActiveProgram::State::Missing:
        add(err::PROGRAM_NOT_FOUND,
            String("Active program '") + ActiveProgram::id() + "' was not found on the SD card.", true, "programs")
            .programId = ActiveProgram::id();
        break;
      case ActiveProgram::State::Invalid:
        for (const ValidationError& e : ActiveProgram::errors()) {
          Fault& x = add(e.code.c_str(), String("Active program '") + ActiveProgram::id() + "': " + e.message.c_str(),
                         true, "programs");
          x.programId = ActiveProgram::id();
          x.field = e.field.c_str();
        }
        break;
      default: break;
    }
  }

  if (BatteryService::faulted()) {
    add(err::BATTERY_READING_INVALID, "Battery voltage reading is invalid. Check the battery connection.", false,
        "device");
  }
  if (EnvironmentService::faulted()) {
    const EnvState& e = EnvironmentService::last();
    String which = !e.envValid && !e.lightValid ? "Temperature/humidity and light sensors"
                   : !e.envValid                ? "Temperature/humidity sensor"
                                                : "Light sensor";
    add(err::SENSOR_READ_FAILED, which + " did not respond. Environmental values will be blank.", false, "data");
  }
  return f;
}

bool armed() {
  for (const Fault& f : faults()) {
    if (f.blocking) return false;
  }
  return true;
}

bool nextPlayback(int64_t nowUtc, ScheduledEvent& out) {
  if (!armed()) return false;
  const Scheduler* s = ActiveProgram::scheduler();
  return s && s->nextEvent(nowUtc, out);
}

int64_t nextEnvSample(int64_t nowUtc) {
  const DeviceSettings& cfg = DeviceConfig::get();
  if (!cfg.envEnabled || !TimeService::isValid()) return -1;
  return EnvSchedule::nextSampleAfter(nowUtc, cfg.envIntervalMinutes);
}

void faultsJson(JsonArray out) {
  for (const Fault& f : faults()) {
    JsonObject o = out.add<JsonObject>();
    o["code"] = f.code;
    o["message"] = f.message;
    o["blocking"] = f.blocking;
    o["area"] = f.area;
    if (f.programId.length()) o["program_id"] = f.programId;
    if (f.field.length() || f.programId.length()) o["field"] = f.field;
  }
}

void statusJson(JsonObject out) {
  const int64_t now = TimeService::nowUtc();
  const DeviceSettings& cfg = DeviceConfig::get();

  JsonObject device = out["device"].to<JsonObject>();
  device["id"] = DeviceConfig::deviceId();
  device["name"] = cfg.name;
  device["firmware_version"] = ACORN_FIRMWARE_VERSION;

  out["mode"] = WifiService::isRunning() ? "config" : "normal";
  JsonArray faultArr = out["faults"].to<JsonArray>();
  faultsJson(faultArr);
  bool isArmed = true;
  for (JsonObject f : faultArr) isArmed = isArmed && !f["blocking"].as<bool>();
  out["armed"] = isArmed;

  JsonObject time = out["time"].to<JsonObject>();
  json::timeInfo(time);

  out["reset_reason"] = SystemHardware::resetReason();
  out["wake_reason"] = wakeSourceName(wake);
  out["uptime_s"] = static_cast<uint32_t>(millis() / 1000);

  const BatteryState& b = BatteryService::fresh();
  JsonObject battery = out["battery"].to<JsonObject>();
  json::battery(battery, b);

  JsonObject sd = out["sd"].to<JsonObject>();
  sd["present"] = SdHardware::isCardPresent();
  sd["mounted"] = SdHardware::isMounted();
  if (SdHardware::isMounted()) {
    sd["total_bytes"] = info.totalBytes;
    sd["used_bytes"] = info.usedBytes;
    sd["free_bytes"] = info.totalBytes > info.usedBytes ? info.totalBytes - info.usedBytes : 0;
  }

  const EnvState& e = EnvironmentService::fresh();
  JsonObject env = out["environment"].to<JsonObject>();
  json::readings(env, e);
  if (e.sampled) env["sampled_utc"] = TimeService::isoUtc(e.sampledUtc);
  else env["sampled_utc"] = nullptr;
  env["logging_enabled"] = cfg.envEnabled;
  env["interval_minutes"] = cfg.envIntervalMinutes;

  const ActiveProgram::State ps = ActiveProgram::state();
  if (ps == ActiveProgram::State::None) {
    out["program"] = nullptr;
  } else {
    JsonObject prog = out["program"].to<JsonObject>();
    prog["id"] = ActiveProgram::id();
    const Program* p = ActiveProgram::program();
    if (!p) {
      prog["name"] = nullptr;
      prog["revision"] = nullptr;
      prog["valid"] = false;
      prog["loaded"] = false;
      prog["window"] = nullptr;
    } else {
      prog["name"] = p->name.c_str();
      prog["revision"] = ActiveProgram::revision().c_str();
      prog["valid"] = ActiveProgram::valid();
      prog["loaded"] = true;
      const Scheduler* s = ActiveProgram::scheduler();
      if (s && TimeService::isValid()) {
        const WindowState w = s->windowState(now);
        JsonObject win = prog["window"].to<JsonObject>();
        win["state"] = windowName(w.kind);
        if (w.kind == WindowState::Kind::Active || w.kind == WindowState::Kind::Quiet) {
          win["event_id"] = p->events[w.eventIndex].eventId.c_str();
          win["until_local"] = TimeService::isoLocal(w.untilUtc);
          win["until_utc"] = TimeService::isoUtc(w.untilUtc);
        }
      } else {
        prog["window"] = nullptr;
      }
    }
  }

  JsonObject playback = out["playback"].to<JsonObject>();
  json::playback(playback, PlaybackService::snapshot());

  const LastPlayback& lp = PlaybackLogger::last();
  if (SdHardware::isMounted() && lp.present) {
    JsonObject last = out["last_playback"].to<JsonObject>();
    last["actual_utc"] = lp.actualUtc;
    if (lp.scheduledUtc.length()) last["scheduled_utc"] = lp.scheduledUtc;
    else last["scheduled_utc"] = nullptr;
    if (lp.eventId.length()) last["event_id"] = lp.eventId;
    else last["event_id"] = nullptr;
    last["source"] = lp.source;
    last["audio"] = lp.audio;
    last["volume"] = lp.volume;
    last["result"] = lp.result;
  } else {
    out["last_playback"] = nullptr;
  }

  ScheduledEvent next;
  const Scheduler* sched = isArmed ? ActiveProgram::scheduler() : nullptr;
  if (sched && sched->nextEvent(now, next)) {
    JsonObject n = out["next_playback"].to<JsonObject>();
    json::scheduledEvent(n, sched->program(), next);
  } else {
    out["next_playback"] = nullptr;
  }

  const int64_t envNext = nextEnvSample(now);
  if (envNext >= 0) out["next_env_sample_utc"] = TimeService::isoUtc(envNext);
  else out["next_env_sample_utc"] = nullptr;

  JsonObject wifi = out["wifi"].to<JsonObject>();
  wifi["mode"] = WifiService::isRunning() ? "ap" : "off";
  wifi["ssid"] = WifiService::ssid();
  wifi["security"] = "open";
  wifi["clients"] = WifiService::clients();
  wifi["idle_timeout_s"] = WifiService::idleTimeoutS();
  wifi["idle_remaining_s"] = WifiService::idleRemainingS();
}

}  // namespace DeviceState
}  // namespace acorn
