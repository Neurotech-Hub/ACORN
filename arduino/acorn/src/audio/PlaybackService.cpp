#include "PlaybackService.h"

#include <AcornHardware.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <deque>

#include "../AppConfig.h"
#include "../services/TimeService.h"

namespace acorn {

const char* playbackSourceName(PlaybackSource s) {
  switch (s) {
    case PlaybackSource::Scheduled: return "scheduled";
    case PlaybackSource::ManualUi: return "manual_ui";
    case PlaybackSource::ManualTest: return "manual_test";
  }
  return "manual_ui";
}

namespace PlaybackService {

namespace {

constexpr size_t FRAMES = 512;
constexpr uint32_t FADE_MS = 10;
constexpr size_t MAX_FINISHED = 8;

SemaphoreHandle_t lock = nullptr;
TaskHandle_t task = nullptr;

// Shared state, guarded by `lock`.
bool busy = false;            // a request is queued or playing
volatile bool stopFlag = false;
PlaybackRequest active;
uint32_t activeDurationMs = 0;
int64_t activeStartedUtc = 0;
uint32_t activeStartedMs = 0;
std::deque<PlaybackResult> finished;

struct Lock {
  Lock() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(lock); }
};

void finish(const char* result, const char* code, uint32_t playedMs) {
  Lock l;
  PlaybackResult r;
  r.request = active;
  r.startedUtc = activeStartedUtc;
  r.durationMs = strcmp(result, "ok") == 0 ? activeDurationMs : playedMs;
  r.result = result;
  r.errorCode = code;
  if (finished.size() >= MAX_FINISHED) finished.pop_front();
  finished.push_back(r);
  busy = false;
}

void playTone(uint8_t volume) {
  const uint32_t rate = AudioHardware::DEFAULT_SAMPLE_RATE;
  if (!AudioHardware::powerUp(rate)) {
    finish("error", "PLAYBACK_START_FAILED", 0);
    return;
  }
  static int16_t buf[FRAMES];
  const uint32_t total = rate * app::TONE_MS / 1000;
  const uint32_t fade = rate * FADE_MS / 1000;
  const float step = 2.0f * PI * app::TONE_HZ / rate;
  uint32_t n = 0;
  while (n < total && !stopFlag) {
    const size_t count = std::min<uint32_t>(FRAMES, total - n);
    for (size_t i = 0; i < count; i++, n++) {
      float env = 1.0f;
      if (n < fade) env = static_cast<float>(n) / fade;
      else if (total - n < fade) env = static_cast<float>(total - n) / fade;
      buf[i] = static_cast<int16_t>(sinf(step * n) * 16000.0f * env);
    }
    AudioHardware::writeMono(buf, count, volume);
  }
  AudioHardware::writeSilence(20);
  AudioHardware::powerDown();
  const uint32_t played = static_cast<uint32_t>(static_cast<uint64_t>(n) * 1000 / rate);
  finish(stopFlag ? "stopped" : "ok", "", played);
}

void playFile(const String& path, uint8_t volume) {
  File f = SdHardware::fs().open(path, FILE_READ);
  WavInfo info;
  if (!f || WavReader::open(f, info) != WavStatus::Ok) {
    if (f) f.close();
    finish("error", "PLAYBACK_START_FAILED", 0);
    return;
  }
  if (!AudioHardware::powerUp(info.sampleRate)) {
    f.close();
    finish("error", "PLAYBACK_START_FAILED", 0);
    return;
  }
  static int16_t buf[FRAMES];
  uint32_t left = info.dataBytes / 2;
  uint32_t played = 0;
  bool ioError = false;
  while (left && !stopFlag) {
    const size_t want = std::min<uint32_t>(FRAMES, left);
    const size_t got = f.read(reinterpret_cast<uint8_t*>(buf), want * 2) / 2;
    if (!got) {
      ioError = true;
      break;
    }
    AudioHardware::writeMono(buf, got, volume);
    left -= got;
    played += got;
  }
  f.close();
  AudioHardware::writeSilence(20);
  AudioHardware::powerDown();
  const uint32_t playedMs = static_cast<uint32_t>(static_cast<uint64_t>(played) * 1000 / info.sampleRate);
  if (ioError) finish("error", "PLAYBACK_IO_ERROR", playedMs);
  else finish(stopFlag ? "stopped" : "ok", "", playedMs);
}

void taskMain(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    PlaybackRequest req;
    {
      Lock l;
      req = active;
    }
    if (req.tone) playTone(req.volume);
    else playFile(req.path, req.volume);
  }
}

void waitIdle() {
  for (int i = 0; i < 200; i++) {
    {
      Lock l;
      if (!busy) return;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

void begin() {
  if (lock) return;
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(taskMain, "acorn_play", 6144, nullptr, 3, &task, 1);
}

bool start(const PlaybackRequest& request, const char*& errorCode) {
  errorCode = "";
  stop();
  uint32_t durationMs = app::TONE_MS;
  if (!request.tone) {
    File f = SdHardware::isMounted() ? SdHardware::fs().open(request.path, FILE_READ) : File();
    WavInfo info;
    const bool ok = f && WavReader::open(f, info) == WavStatus::Ok;
    if (f) f.close();
    if (!ok) {
      errorCode = "PLAYBACK_START_FAILED";
      return false;
    }
    durationMs = info.durationMs;
  }
  {
    Lock l;
    active = request;
    activeDurationMs = durationMs;
    activeStartedUtc = TimeService::nowUtc();
    activeStartedMs = millis();
    stopFlag = false;
    busy = true;
  }
  xTaskNotifyGive(task);
  return true;
}

void stop() {
  bool wasBusy;
  {
    Lock l;
    wasBusy = busy;
    if (busy) stopFlag = true;
  }
  if (wasBusy) waitIdle();
}

bool isPlaying() {
  Lock l;
  return busy;
}

PlaybackSnapshot snapshot() {
  Lock l;
  PlaybackSnapshot s;
  s.playing = busy;
  if (!busy) return s;
  s.request = active;
  s.durationMs = activeDurationMs;
  s.positionMs = std::min<uint32_t>(millis() - activeStartedMs, activeDurationMs);
  s.startedUtc = activeStartedUtc;
  return s;
}

bool takeFinished(PlaybackResult& out) {
  Lock l;
  if (finished.empty()) return false;
  out = finished.front();
  finished.pop_front();
  return true;
}

}  // namespace PlaybackService
}  // namespace acorn
