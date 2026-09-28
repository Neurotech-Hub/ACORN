// Isolated test: microSD over SPI with SD_DETECT (GPIO12, LOW = inserted).
// Mounts, reports capacity, does a write/read/delete round trip and lists
// WAV files with header validation. Insert/remove the card to see detect.

#include <AcornHardware.h>
#include <AcornTest.h>

using namespace acorn;

constexpr const char* TEST_PATH = "/.acorn_sd_test.txt";

void listWavs(const char* dirPath) {
  File dir = SdHardware::fs().open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    String lower = name;
    lower.toLowerCase();
    if (!f.isDirectory() && lower.endsWith(".wav")) {
      WavInfo info;
      const WavStatus s = WavReader::open(f, info);
      if (s == WavStatus::Ok) {
        test::info("%s/%s  %lu Hz, %lu ms", dirPath, name.c_str(), static_cast<unsigned long>(info.sampleRate),
                   static_cast<unsigned long>(info.durationMs));
      } else {
        test::info("%s/%s  %s (fmt %u, ch %u, %lu Hz, %u bit)", dirPath, name.c_str(), wavStatusCode(s),
                   info.format, info.channels, static_cast<unsigned long>(info.sampleRate), info.bitsPerSample);
      }
    }
    f.close();
  }
}

void runCardTests() {
  const SdStatus m = SdHardware::mount();
  if (!test::check("mount", m == SdStatus::Ok, sdStatusCode(m))) return;

  const SdInfo i = SdHardware::info();
  const char* types[] = {"NONE", "MMC", "SD", "SDHC", "UNKNOWN"};
  test::info("type %s, %llu MB total, %llu MB used", types[min<uint8_t>(i.cardType, 4)],
             i.totalBytes / (1024ULL * 1024ULL), i.usedBytes / (1024ULL * 1024ULL));
  const SdStatus w = SdHardware::checkWritable();
  test::check("free space", w == SdStatus::Ok, sdStatusCode(w));

  const char* payload = "ACORN SD round trip 0123456789";
  File f = SdHardware::fs().open(TEST_PATH, FILE_WRITE);
  const bool wrote = f && f.print(payload) == strlen(payload);
  if (f) f.close();
  test::check("write file", wrote, wrote ? nullptr : "SD_WRITE_FAILED");

  f = SdHardware::fs().open(TEST_PATH, FILE_READ);
  String back = f ? f.readString() : String();
  if (f) f.close();
  test::check("read back matches", back == payload);
  test::check("delete file", SdHardware::fs().remove(TEST_PATH));

  listWavs("/");
  listWavs("/audio");
}

void setup() {
  test::begin("test_sd");
  SdHardware::begin();
  const bool present = SdHardware::isCardPresent();
  test::info("SD_DETECT: card %s", present ? "inserted" : "not inserted");
  if (!test::check("card present", present, present ? nullptr : "SD_NOT_PRESENT")) {
    test::check("mount reports SD_NOT_PRESENT", SdHardware::mount() == SdStatus::NotPresent);
  } else {
    runCardTests();
  }
  test::summary();
  test::info("Watching SD_DETECT; re-runs tests on insert");
}

void loop() {
  static bool last = SdHardware::isCardPresent();
  const bool now = SdHardware::isCardPresent();
  if (now != last) {
    last = now;
    test::info("card %s", now ? "INSERTED" : "REMOVED");
    if (now) {
      delay(300);
      runCardTests();
    } else {
      SdHardware::unmount();
    }
  }
  delay(20);
}
