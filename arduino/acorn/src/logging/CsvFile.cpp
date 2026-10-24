#include "CsvFile.h"

#include <AcornHardware.h>

#include <map>
#include <string>

#include "../AppConfig.h"

namespace acorn {
namespace CsvFile {

namespace {

struct RowCache {
  size_t size = 0;
  uint32_t lines = 0;
};
std::map<std::string, RowCache> rowCache;  // newline count keyed by file size

uint32_t countLines(fs::File& f) {
  uint8_t buf[512];
  uint32_t n = 0;
  f.seek(0);
  while (true) {
    const size_t got = f.read(buf, sizeof(buf));
    if (!got) break;
    for (size_t i = 0; i < got; i++) n += buf[i] == '\n';
  }
  return n;
}

// Offset of the start of the last `rows` lines (file ends with '\n').
size_t tailOffset(fs::File& f, uint32_t rows) {
  const size_t size = f.size();
  if (!size) return 0;
  uint8_t buf[256];
  size_t pos = size;
  uint32_t seen = 0;
  bool skippedFinal = false;
  while (pos > 0) {
    const size_t chunk = pos > sizeof(buf) ? sizeof(buf) : pos;
    pos -= chunk;
    f.seek(pos);
    f.read(buf, chunk);
    for (size_t i = chunk; i-- > 0;) {
      if (buf[i] != '\n') continue;
      if (!skippedFinal && pos + i == size - 1) {
        skippedFinal = true;
        continue;
      }
      if (++seen == rows) return pos + i + 1;
    }
  }
  return 0;
}

String firstLine(fs::FS& fs, const char* path) {
  File f = fs.open(path, FILE_READ);
  if (!f) return "";
  String line = f.readStringUntil('\n');
  f.close();
  line.trim();
  return line;
}

bool writeHeader(fs::FS& fs, const char* path, const char* header) {
  File f = fs.open(path, FILE_WRITE);
  if (!f) return false;
  const size_t len = strlen(header);
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(header), len) == len && f.write('\n') == 1;
  f.flush();
  f.close();
  rowCache.erase(path);
  return ok;
}

// Creates the file with its header if missing or empty.
bool ready(const char* path, const char* header) {
  fs::FS& fs = SdHardware::fs();
  if (!fs.exists(app::LOGS_DIR)) fs.mkdir(app::LOGS_DIR);
  if (fs.exists(path)) {
    File f = fs.open(path, FILE_READ);
    const bool nonEmpty = f && f.size() > 0;
    f.close();
    if (nonEmpty) return true;
  }
  return writeHeader(fs, path, header);
}

}  // namespace

bool ensure(const char* path, const char* header) {
  if (!ready(path, header)) return false;
  fs::FS& fs = SdHardware::fs();
  if (firstLine(fs, path) == header) return true;
  // Older column layout: keep it as <path>.bak and start a fresh file.
  const String bak = String(path) + ".bak";
  if (fs.exists(bak)) fs.remove(bak);
  if (!fs.rename(path, bak)) return false;
  rowCache.erase(path);
  return writeHeader(fs, path, header);
}

bool append(const char* path, const char* header, const String& row) {
  if (!ready(path, header)) return false;
  File f = SdHardware::fs().open(path, FILE_APPEND);
  if (!f) return false;
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(row.c_str()), row.length()) == row.length() &&
                  f.write('\n') == 1;
  f.flush();
  const size_t size = f.size();
  f.close();
  auto it = rowCache.find(path);
  if (ok && it != rowCache.end()) {
    it->second.lines++;
    it->second.size = size;
  }
  return ok;
}

Info info(const char* path) {
  Info out;
  File f = SdHardware::fs().open(path, FILE_READ);
  if (!f) return out;
  out.exists = true;
  out.sizeBytes = f.size();
  RowCache& c = rowCache[path];
  if (c.size != out.sizeBytes || (!c.lines && out.sizeBytes)) {
    c.lines = countLines(f);
    c.size = out.sizeBytes;
  }
  out.rows = c.lines > 0 ? c.lines - 1 : 0;
  f.close();
  if (out.rows) {
    const String last = lastRow(path);
    const int comma = last.indexOf(',');
    out.lastTimestamp = comma < 0 ? last : last.substring(0, comma);
  }
  return out;
}

bool tail(const char* path, const char* header, uint32_t rows, String& out) {
  out = header;
  out += '\n';
  File f = SdHardware::fs().open(path, FILE_READ);
  if (!f) return true;
  if (!f.size()) {
    f.close();
    return true;
  }
  // Skip the stored header line; the canonical header is emitted above.
  size_t start = 0;
  f.seek(0);
  while (f.available()) {
    start++;
    if (f.read() == '\n') break;
  }
  if (rows) {
    const size_t t = tailOffset(f, rows);
    if (t > start) start = t;
  }
  f.seek(start);
  const size_t len = f.size() - start;
  out.reserve(out.length() + len + 1);
  uint8_t buf[512];
  size_t left = len;
  while (left) {
    const size_t got = f.read(buf, left > sizeof(buf) ? sizeof(buf) : left);
    if (!got) break;
    out.concat(reinterpret_cast<const char*>(buf), got);
    left -= got;
  }
  f.close();
  return true;
}

File openRead(const char* path) { return SdHardware::fs().open(path, FILE_READ); }

String lastRow(const char* path) {
  File f = SdHardware::fs().open(path, FILE_READ);
  if (!f) return "";
  const size_t size = f.size();
  const size_t start = tailOffset(f, 1);
  String line;
  if (start > 0 && start < size) {
    f.seek(start);
    line = f.readStringUntil('\n');
  }
  f.close();
  return line;
}

}  // namespace CsvFile
}  // namespace acorn
