// Isolated test: audio chain 3V3_SW_EN -> PCM5102A (I2S, XSMT) -> TPA3138D2.
// Plays built-in tones (no SD needed), then the first valid WAV found in
// /audio or / if a card is present. After that, lists every valid WAV.
// Listen for pops at power up/down.
// BTN_01 replays the test tone. Keep volume low on first bring-up.
// Serial: "vol <0-100>", "tone <hz>", "wav <id>".

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

uint8_t volume = 10;

struct ListedWav {
  String path;
  uint32_t sampleRate = 0;
  uint32_t durationMs = 0;
};

constexpr uint8_t MAX_WAVS = 32;
ListedWav wavs[MAX_WAVS];
uint8_t wavCount = 0;

void collectWavs(const char* dirPath) {
  File dir = SdHardware::fs().open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    String lower = name;
    lower.toLowerCase();
    if (f.isDirectory() || name.startsWith(".") || !lower.endsWith(".wav")) {
      f.close();
      continue;
    }
    WavInfo info;
    const bool ok = WavReader::open(f, info) == WavStatus::Ok;
    f.close();
    if (!ok) continue;
    if (wavCount >= MAX_WAVS) {
      test::info("WAV list full (%u); further files omitted", MAX_WAVS);
      return;
    }
    ListedWav& w = wavs[wavCount++];
    w.path = String(dirPath) + (strcmp(dirPath, "/") ? "/" : "") + name;
    w.sampleRate = info.sampleRate;
    w.durationMs = info.durationMs;
  }
}

void scanWavs() {
  wavCount = 0;
  if (SdHardware::mount() != SdStatus::Ok) return;
  collectWavs("/audio");
  collectWavs("/");
}

void printWavList() {
  for (uint8_t i = 0; i < wavCount; i++) {
    test::info("%u  %s  %lu Hz, %lu ms", i + 1, wavs[i].path.c_str(),
               static_cast<unsigned long>(wavs[i].sampleRate),
               static_cast<unsigned long>(wavs[i].durationMs));
  }
}

bool playWav(const String& path) {
  File f = SdHardware::fs().open(path, FILE_READ);
  WavInfo info;
  const WavStatus s = WavReader::open(f, info);
  if (s != WavStatus::Ok) {
    test::info("%s: %s", path.c_str(), wavStatusCode(s));
    return false;
  }
  test::info("Playing %s (%lu Hz, %lu ms) at volume %u", path.c_str(),
             static_cast<unsigned long>(info.sampleRate), static_cast<unsigned long>(info.durationMs), volume);
  if (!AudioHardware::powerUp(info.sampleRate)) return false;
  static int16_t buf[512];
  uint32_t remaining = info.dataBytes;
  bool ok = true;
  while (remaining) {
    const size_t want = min<uint32_t>(sizeof(buf), remaining);
    const size_t got = f.read(reinterpret_cast<uint8_t*>(buf), want);
    if (got != want) {
      test::info("PLAYBACK_IO_ERROR after %lu bytes", static_cast<unsigned long>(info.dataBytes - remaining));
      ok = false;
      break;
    }
    AudioHardware::writeMono(buf, got / 2, volume);
    remaining -= got;
  }
  f.close();
  AudioHardware::writeSilence(20);
  return ok;
}

void toneTest(uint16_t hz, uint32_t ms) {
  test::info("Tone %u Hz, %lu ms, volume %u", hz, static_cast<unsigned long>(ms), volume);
  AudioHardware::playTone(hz, ms, volume);
}

void setup() {
  test::begin("test_audio");
  AudioHardware::begin();
  SdHardware::begin();
  ButtonHardware::begin();

  test::check("volume 0 -> gain 0", AudioHardware::volumeToGainQ15(0) == 0);
  test::check("volume 100 -> full scale", AudioHardware::volumeToGainQ15(100) == 32767);
  test::check("volume curve monotonic",
              AudioHardware::volumeToGainQ15(25) < AudioHardware::volumeToGainQ15(50) &&
                  AudioHardware::volumeToGainQ15(50) < AudioHardware::volumeToGainQ15(75));

  test::check("power up (rail, I2S, unmute, amp)", AudioHardware::powerUp());
  test::info("amp fault readback not available (AMP_SHDN drives SD/FAULT through 5.1k); listen instead");
  toneTest(1000, 1000);
  AudioHardware::writeSilence(200);
  toneTest(440, 500);
  toneTest(2000, 500);

  if (SdHardware::mount() == SdStatus::Ok) {
    scanWavs();
    if (wavCount) test::check("WAV playback", playWav(wavs[0].path), wavs[0].path.c_str());
    else test::info("No valid WAV on card (PCM 16-bit mono, 44.1/48 kHz); skipping");
  } else {
    test::info("No SD card; skipping WAV playback");
  }

  AudioHardware::powerDown();
  test::check("powered down", !AudioHardware::isPoweredUp());
  test::summary();
  if (wavCount) {
    test::info("Valid WAV files (bring-up played 1):");
    printWavList();
  }
  test::info("BTN_01 = tone. Serial: vol <0-100> | tone <hz> | wav <id>");
}

void loop() {
  ButtonHardware::update();
  String line;
  const bool haveLine = test::readLine(line);
  if (haveLine && line.startsWith("vol ")) {
    volume = constrain(line.substring(4).toInt(), 0, 100);
    test::info("volume %u", volume);
  }
  if (ButtonHardware::wasPressed(Button::Audio) || (haveLine && line.startsWith("tone"))) {
    const uint16_t hz = haveLine && line.length() > 5 ? line.substring(5).toInt() : 1000;
    if (AudioHardware::powerUp()) {
      toneTest(hz ? hz : 1000, 1000);
      AudioHardware::powerDown();
    }
  }
  if (haveLine && line.startsWith("wav")) {
    if (!wavCount) scanWavs();
    const int id = line.substring(3).toInt();
    if (id < 1 || id > wavCount) {
      if (!wavCount) test::info("No valid WAV on card");
      else test::info("Use wav <id>, 1 to %u", wavCount);
    } else {
      playWav(wavs[id - 1].path);
      AudioHardware::powerDown();
    }
  }
  delay(5);
}
