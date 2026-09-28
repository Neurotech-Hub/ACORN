#include "AudioLibrary.h"

#include <algorithm>

#include "../AppConfig.h"

namespace acorn {
namespace AudioLibrary {

namespace {

constexpr const char* UPLOAD_TMP = "/audio/.upload.tmp";

std::vector<AudioEntry> entries;
bool scanned = false;
uint32_t gen = 1;
File uploadFile;
bool uploadFailed = false;

String describeUnsupported(const WavInfo& i) {
  String s = "Unsupported: ";
  bool first = true;
  auto add = [&](const String& part) {
    if (!first) s += ", ";
    s += part;
    first = false;
  };
  if (i.format != 1) add("not linear PCM");
  if (i.bitsPerSample != 16) add(String(i.bitsPerSample) + "-bit");
  if (i.channels != 1) add(i.channels == 2 ? String("stereo") : String(i.channels) + " channels");
  if (i.sampleRate != 44100 && i.sampleRate != 48000) add(String(i.sampleRate) + " Hz");
  return s;
}

AudioEntry inspect(const String& name, const char* path) {
  AudioEntry e;
  e.name = name;
  File f = SdHardware::fs().open(path, FILE_READ);
  if (!f) {
    e.errorMessage = "File could not be opened";
    return e;
  }
  e.sizeBytes = f.size();
  e.status = WavReader::open(f, e.info);
  f.close();
  if (e.status == WavStatus::Unsupported) e.errorMessage = describeUnsupported(e.info);
  else if (e.status == WavStatus::Corrupt) e.errorMessage = "Not a valid RIFF/WAVE file";
  return e;
}

void scan() {
  entries.clear();
  scanned = true;
  if (!SdHardware::isMounted()) return;
  fs::FS& fs = SdHardware::fs();
  if (!fs.exists(app::AUDIO_DIR)) fs.mkdir(app::AUDIO_DIR);
  File dir = fs.open(app::AUDIO_DIR);
  if (!dir || !dir.isDirectory()) return;
  std::vector<String> names;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    const bool isDir = f.isDirectory();
    f.close();
    if (isDir || name.startsWith(".")) continue;
    String lower = name;
    lower.toLowerCase();
    if (!lower.endsWith(".wav")) continue;
    names.push_back(name);
  }
  dir.close();
  std::sort(names.begin(), names.end(), [](const String& a, const String& b) { return a < b; });
  for (const String& n : names) entries.push_back(inspect(n, (String(app::AUDIO_DIR) + "/" + n).c_str()));
}

class Catalog : public AudioCatalog {
 public:
  bool find(const std::string& path, AudioMeta& out) const override {
    const AudioEntry* e = findPath(String(path.c_str()));
    if (!e) return false;
    out.valid = e->valid();
    out.durationMs = e->info.durationMs;
    out.errorMessage = e->errorMessage.c_str();
    return true;
  }
};
Catalog catalogInstance;

}  // namespace

void invalidate() {
  scanned = false;
  gen++;
}

const std::vector<AudioEntry>& list() {
  if (!scanned) scan();
  return entries;
}

const AudioEntry* find(const String& name) {
  for (const AudioEntry& e : list()) {
    if (e.name == name) return &e;
  }
  return nullptr;
}

const AudioEntry* findPath(const String& path) {
  static const String prefix = "/audio/";
  if (!path.startsWith(prefix)) return nullptr;
  const String name = path.substring(prefix.length());
  if (name.isEmpty() || name.indexOf('/') >= 0) return nullptr;
  return find(name);
}

const AudioCatalog& catalog() { return catalogInstance; }
uint32_t generation() { return gen; }

String sanitizeName(const String& filename) {
  String name = filename;
  const int slash = std::max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (slash >= 0) name = name.substring(slash + 1);
  name.toLowerCase();
  for (size_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) name[i] = '-';
  }
  return name;
}

bool remove(const String& name) {
  const bool ok = SdHardware::fs().remove(String(app::AUDIO_DIR) + "/" + name);
  invalidate();
  return ok;
}

bool uploadBegin() {
  uploadAbort();
  fs::FS& fs = SdHardware::fs();
  if (!fs.exists(app::AUDIO_DIR)) fs.mkdir(app::AUDIO_DIR);
  uploadFile = fs.open(UPLOAD_TMP, FILE_WRITE);
  uploadFailed = !uploadFile;
  return !uploadFailed;
}

bool uploadWrite(const uint8_t* data, size_t len) {
  if (uploadFailed || !uploadFile) return false;
  if (uploadFile.write(data, len) != len) uploadFailed = true;
  return !uploadFailed;
}

UploadResult uploadFinish(const String& name) {
  UploadResult r;
  if (uploadFile) {
    uploadFile.flush();
    uploadFile.close();
  }
  fs::FS& fs = SdHardware::fs();
  if (uploadFailed) {
    fs.remove(UPLOAD_TMP);
    r.httpStatus = 507;
    r.code = SdHardware::checkWritable() == SdStatus::Full ? "SD_FULL" : "SD_WRITE_FAILED";
    r.message = strcmp(r.code, "SD_FULL") == 0 ? "Not enough free space on the SD card." : "Writing to the SD card failed.";
    return r;
  }
  r.entry = inspect(name, UPLOAD_TMP);
  if (!r.entry.valid()) {
    fs.remove(UPLOAD_TMP);
    r.httpStatus = 422;
    r.code = wavStatusCode(r.entry.status);
    r.message = r.entry.errorMessage;
    return r;
  }
  const String dest = String(app::AUDIO_DIR) + "/" + name;
  const bool existed = fs.exists(dest);
  if (existed) fs.remove(dest);
  if (!fs.rename(UPLOAD_TMP, dest)) {
    fs.remove(UPLOAD_TMP);
    r.httpStatus = 507;
    r.code = "SD_WRITE_FAILED";
    r.message = "Could not move the upload into place.";
    invalidate();
    return r;
  }
  invalidate();
  r.ok = true;
  r.httpStatus = existed ? 200 : 201;
  return r;
}

void uploadAbort() {
  if (uploadFile) uploadFile.close();
  if (SdHardware::isMounted() && SdHardware::fs().exists(UPLOAD_TMP)) SdHardware::fs().remove(UPLOAD_TMP);
  uploadFailed = false;
}

}  // namespace AudioLibrary
}  // namespace acorn
