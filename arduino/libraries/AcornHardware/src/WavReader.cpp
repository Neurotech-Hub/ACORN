#include "WavReader.h"

namespace acorn {

const char* wavStatusCode(WavStatus status) {
  switch (status) {
    case WavStatus::Ok: return "OK";
    case WavStatus::Unsupported: return "WAV_UNSUPPORTED";
    case WavStatus::Corrupt: return "WAV_CORRUPT";
  }
  return "WAV_CORRUPT";
}

namespace WavReader {

namespace {

bool readExact(fs::File& f, void* buf, size_t len) {
  return f.read(static_cast<uint8_t*>(buf), len) == len;
}

uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool isSupported(const WavInfo& i) {
  return i.format == 1 && i.channels == 1 && i.bitsPerSample == 16 &&
         (i.sampleRate == 44100 || i.sampleRate == 48000);
}

WavStatus open(fs::File& file, WavInfo& info) {
  info = WavInfo{};
  if (!file || !file.seek(0)) return WavStatus::Corrupt;
  const size_t size = file.size();

  uint8_t riff[12];
  if (!readExact(file, riff, sizeof(riff))) return WavStatus::Corrupt;
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) return WavStatus::Corrupt;

  bool haveFmt = false;
  uint32_t pos = 12;
  while (pos + 8 <= size) {
    uint8_t hdr[8];
    if (!file.seek(pos) || !readExact(file, hdr, sizeof(hdr))) return WavStatus::Corrupt;
    const uint32_t chunkSize = le32(hdr + 4);
    const uint32_t body = pos + 8;

    if (memcmp(hdr, "fmt ", 4) == 0) {
      if (chunkSize < 16) return WavStatus::Corrupt;
      uint8_t fmt[16];
      if (!readExact(file, fmt, sizeof(fmt))) return WavStatus::Corrupt;
      info.format = le16(fmt);
      info.channels = le16(fmt + 2);
      info.sampleRate = le32(fmt + 4);
      info.bitsPerSample = le16(fmt + 14);
      haveFmt = true;
    } else if (memcmp(hdr, "data", 4) == 0) {
      if (!haveFmt) return WavStatus::Corrupt;
      info.dataOffset = body;
      info.dataBytes = min<uint32_t>(chunkSize, size - body) & ~1u;
      const uint32_t bytesPerSecond = info.sampleRate * info.channels * (info.bitsPerSample / 8);
      if (bytesPerSecond) {
        info.durationMs = static_cast<uint32_t>(static_cast<uint64_t>(info.dataBytes) * 1000 / bytesPerSecond);
      }
      if (!isSupported(info)) return WavStatus::Unsupported;
      if (chunkSize == 0 || body + chunkSize > size) return WavStatus::Corrupt;
      return file.seek(body) ? WavStatus::Ok : WavStatus::Corrupt;
    }
    pos = body + chunkSize + (chunkSize & 1);  // chunks are word aligned
  }
  return haveFmt && !isSupported(info) ? WavStatus::Unsupported : WavStatus::Corrupt;
}

}  // namespace WavReader
}  // namespace acorn
