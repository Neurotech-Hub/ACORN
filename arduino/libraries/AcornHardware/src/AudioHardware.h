#pragma once

#include <Arduino.h>

namespace acorn {

// Audio chain: 3V3_SW_EN rail -> PCM5102A (I2S, DAC_XSMT) -> TPA3138D2
// (AMP_SHDN = SD/FAULT). Callers only see powerUp/powerDown/write; the
// pop-free sequencing and pin polarities stay here.
namespace AudioHardware {

constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;

void begin();  // everything off (rail off, DAC muted, amp shut down)

// Rail on -> I2S (silence) -> DAC unmute -> amp enable. Idempotent; calling
// with a different rate while powered reconfigures I2S with the amp muted.
bool powerUp(uint32_t sampleRate = DEFAULT_SAMPLE_RATE);
// Amp shutdown -> DAC mute -> I2S stop -> rail off.
void powerDown();
bool isPoweredUp();
uint32_t sampleRate();

// True if the amp is holding SD/FAULT LOW while enabled (OT/OC/DC fault).
// Only meaningful with level::AMP_SHDN_OPEN_DRAIN.
bool ampFaulted();

// Blocking write of 16-bit mono PCM, duplicated to both DAC channels.
// volume 0..100 (perceptual curve; SPL calibration TBD).
size_t writeMono(const int16_t* samples, size_t frames, uint8_t volume);
void writeSilence(uint32_t ms);

// Built-in sine test tone (blocking) with short fades to avoid clicks.
// Requires powerUp().
bool playTone(uint16_t frequencyHz, uint32_t durationMs, uint8_t volume);

// Q15 gain for a 0..100 volume; exposed for tests/UI calibration.
int32_t volumeToGainQ15(uint8_t volume);

// Everything off and latched LOW for deep sleep.
void prepareForSleep();

}  // namespace AudioHardware
}  // namespace acorn
