#pragma once

#include <Arduino.h>
#include <AcornCore.h>

// /logs/playback.csv (init.md section 36).

namespace acorn {

struct LastPlayback {
  bool present = false;
  String actualUtc;
  String scheduledUtc;  // "" for manual playback
  String eventId;       // "" if none
  String source;
  String audio;
  int32_t volume = 0;
  String result;
};

namespace PlaybackLogger {

bool ensure();
// Appends a row; on failure the blocking SD_WRITE_FAILED fault is raised.
bool append(const Csv::PlaybackRow& row);
const LastPlayback& last();
void reloadLast();  // after the SD card (re)mounts

}  // namespace PlaybackLogger
}  // namespace acorn
