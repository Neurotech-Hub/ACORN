#pragma once

#include <stdint.h>

// Firmware-wide constants. Program limits live in AcornCore (AcornLimits.h).

#define ACORN_FIRMWARE_VERSION "0.4.0-dev"

// 0 (default): event-driven wake/work/sleep; the door starts config sessions.
// 1: bench mode, stays awake with the AP, web UI and API always up.
#ifndef ACORN_STAY_AWAKE
#define ACORN_STAY_AWAKE 0
#endif

namespace acorn {
namespace app {

constexpr const char* AUDIO_DIR = "/audio";
constexpr const char* PROGRAMS_DIR = "/programs";
constexpr const char* LOGS_DIR = "/logs";
constexpr const char* PLAYBACK_LOG = "/logs/playback.csv";
constexpr const char* ENVIRONMENT_LOG = "/logs/environment.csv";
constexpr const char* CONFIG_DIR = "/config";
constexpr const char* SITE_PATH = "/config/site.json";
constexpr const char* FALLBACK_AUDIO = "/audio/playback.wav";

constexpr uint8_t FALLBACK_VOLUME = 80;   // BTN_01 fallback WAV / sine
constexpr uint16_t TONE_HZ = 1000;
constexpr uint32_t TONE_MS = 2000;        // matches the mock's tone test

constexpr uint32_t MAX_UPLOAD_BYTES = 16UL * 1024UL * 1024UL;
constexpr uint32_t MAX_BODY_BYTES = 20UL * 1024UL;  // JSON request bodies
constexpr uint32_t AP_IDLE_TIMEOUT_S = 600;

const char* buildTimestamp();  // "YYYY-MM-DDTHH:MM:SS" of the build host clock

}  // namespace app
}  // namespace acorn
