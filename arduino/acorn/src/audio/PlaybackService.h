#pragma once

#include <Arduino.h>

// How audio is played (init.md section 28). Streams a WAV or the built-in
// sine tone from a FreeRTOS task so the API stays responsive. Finished
// playbacks are queued and logged from the main loop via takeFinished().

namespace acorn {

enum class PlaybackSource : uint8_t { Scheduled, ManualUi, ManualTest };
const char* playbackSourceName(PlaybackSource s);  // "scheduled", "manual_ui", "manual_test"

struct PlaybackRequest {
  PlaybackSource source = PlaybackSource::ManualUi;
  bool tone = false;
  String path;            // "/audio/x.wav" (ignored for tone)
  uint8_t volume = 80;
  int64_t scheduledUtc = -1;  // scheduled playback only
  String eventId;             // "" if none
  String programId;           // "" if none
  String revision;
};

struct PlaybackSnapshot {
  bool playing = false;
  PlaybackRequest request;
  uint32_t durationMs = 0;
  uint32_t positionMs = 0;
  int64_t startedUtc = 0;
};

struct PlaybackResult {
  PlaybackRequest request;
  int64_t startedUtc = 0;
  uint32_t durationMs = 0;  // full length when ok, elapsed otherwise
  String result;            // ok | stopped | error
  String errorCode;
};

namespace PlaybackService {

void begin();
// Stops any current playback first. On failure fills errorCode.
bool start(const PlaybackRequest& request, const char*& errorCode);
void stop();                      // returns once the task is idle
bool isPlaying();
PlaybackSnapshot snapshot();
bool takeFinished(PlaybackResult& out);

}  // namespace PlaybackService
}  // namespace acorn
