#include "AudioHardware.h"

#include <ESP_I2S.h>
#include <driver/gpio.h>
#include <math.h>

#include "GpioHold.h"
#include "Pins.h"

namespace acorn {
namespace AudioHardware {

namespace {

constexpr uint32_t RAIL_SETTLE_MS = 20;
constexpr uint32_t DAC_SETTLE_MS = 10;
constexpr uint32_t AMP_TURN_ON_MS = 60;  // TPA3138D2 tON = 50 ms
constexpr size_t CHUNK_FRAMES = 256;

I2SClass i2s;
bool powered = false;
bool ampOn = false;
uint32_t rate = DEFAULT_SAMPLE_RATE;
int16_t stereo[CHUNK_FRAMES * 2];

// OFF = high-Z so the 100k hardware pulldown holds SD/FAULT LOW (see Pins.h).
void setAmp(bool enable) {
  ampOn = enable;
  if (enable) {
    digitalWrite(pins::AMP_SHDN, level::AMP_ENABLED);
    pinMode(pins::AMP_SHDN, OUTPUT);
    digitalWrite(pins::AMP_SHDN, level::AMP_ENABLED);
  } else {
    pinMode(pins::AMP_SHDN, INPUT);
  }
}

bool startI2s(uint32_t sampleRate) {
  i2s.setPins(pins::I2S_BCK, pins::I2S_LRCK, pins::I2S_DOUT);
  if (!i2s.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    return false;
  }
  rate = sampleRate;
  return true;
}

}  // namespace

void begin() {
  gpio_hold_dis(static_cast<gpio_num_t>(pins::AMP_SHDN));  // older firmware latched it LOW in sleep
  setAmp(false);
  releaseOutput(pins::DAC_XSMT, !level::DAC_UNMUTED);
  releaseOutput(pins::SW_3V3_EN, !level::SW_3V3_ON);
  powered = false;
  ampOn = false;
}

bool powerUp(uint32_t sampleRate) {
  if (powered) {
    if (sampleRate == rate) return true;
    setAmp(false);
    digitalWrite(pins::DAC_XSMT, !level::DAC_UNMUTED);
    i2s.end();
    if (!startI2s(sampleRate)) {
      powerDown();
      return false;
    }
    writeSilence(5);
    digitalWrite(pins::DAC_XSMT, level::DAC_UNMUTED);
    delay(DAC_SETTLE_MS);
    setAmp(true);
    delay(AMP_TURN_ON_MS);
    return true;
  }

  digitalWrite(pins::SW_3V3_EN, level::SW_3V3_ON);
  delay(RAIL_SETTLE_MS);
  if (!startI2s(sampleRate)) {
    digitalWrite(pins::SW_3V3_EN, !level::SW_3V3_ON);
    return false;
  }
  powered = true;
  writeSilence(10);
  digitalWrite(pins::DAC_XSMT, level::DAC_UNMUTED);
  delay(DAC_SETTLE_MS);
  setAmp(true);
  delay(AMP_TURN_ON_MS);
  return true;
}

void powerDown() {
  setAmp(false);
  delay(2);
  digitalWrite(pins::DAC_XSMT, !level::DAC_UNMUTED);
  delay(DAC_SETTLE_MS);
  if (powered) i2s.end();
  // i2s.end() detaches the pins; keep the DAC inputs from floating.
  releaseOutput(pins::I2S_BCK, LOW);
  releaseOutput(pins::I2S_LRCK, LOW);
  releaseOutput(pins::I2S_DOUT, LOW);
  digitalWrite(pins::SW_3V3_EN, !level::SW_3V3_ON);
  powered = false;
}

bool isPoweredUp() { return powered; }
uint32_t sampleRate() { return rate; }

// While enabled the GPIO drives HIGH through the 5.1k series resistor, so it
// reads back its own level, not the SD/FAULT node: a fault cannot be seen here.
bool ampFaulted() { return false; }

int32_t volumeToGainQ15(uint8_t volume) {
  if (volume > 100) volume = 100;
  return (static_cast<int32_t>(volume) * volume * 32767) / 10000;
}

size_t writeMono(const int16_t* samples, size_t frames, uint8_t volume) {
  if (!powered) return 0;
  const int32_t gain = volumeToGainQ15(volume);
  size_t done = 0;
  while (done < frames) {
    const size_t n = min(CHUNK_FRAMES, frames - done);
    for (size_t i = 0; i < n; i++) {
      const int16_t s = static_cast<int16_t>((samples[done + i] * gain) >> 15);
      stereo[2 * i] = s;
      stereo[2 * i + 1] = s;
    }
    const size_t bytes = n * 2 * sizeof(int16_t);
    if (i2s.write(reinterpret_cast<const uint8_t*>(stereo), bytes) != bytes) break;
    done += n;
  }
  return done;
}

void writeSilence(uint32_t ms) {
  memset(stereo, 0, sizeof(stereo));
  uint32_t frames = rate * ms / 1000;
  while (frames) {
    const size_t n = min<uint32_t>(CHUNK_FRAMES, frames);
    i2s.write(reinterpret_cast<const uint8_t*>(stereo), n * 2 * sizeof(int16_t));
    frames -= n;
  }
}

bool playTone(uint16_t frequencyHz, uint32_t durationMs, uint8_t volume) {
  if (!powered || frequencyHz == 0) return false;
  const uint32_t total = rate * durationMs / 1000;
  const uint32_t fade = min<uint32_t>(rate / 100, total / 2);  // 10 ms
  const float step = 2.0f * PI * frequencyHz / rate;
  float phase = 0;
  int16_t mono[CHUNK_FRAMES];
  uint32_t i = 0;
  while (i < total) {
    const size_t n = min<uint32_t>(CHUNK_FRAMES, total - i);
    for (size_t k = 0; k < n; k++, i++) {
      float env = 1.0f;
      if (i < fade) env = static_cast<float>(i) / fade;
      else if (total - i <= fade) env = static_cast<float>(total - i) / fade;
      mono[k] = static_cast<int16_t>(sinf(phase) * env * 32000.0f);
      phase += step;
      if (phase > 2.0f * PI) phase -= 2.0f * PI;
    }
    if (writeMono(mono, n, volume) != n) return false;
  }
  return true;
}

void prepareForSleep() {
  powerDown();  // leaves AMP_SHDN high-Z: the 100k pulldown keeps the amp off in sleep
  holdOutput(pins::DAC_XSMT, !level::DAC_UNMUTED);
  holdOutput(pins::SW_3V3_EN, !level::SW_3V3_ON);
}

}  // namespace AudioHardware
}  // namespace acorn
