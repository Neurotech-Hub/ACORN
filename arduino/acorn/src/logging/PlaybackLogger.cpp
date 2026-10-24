#include "PlaybackLogger.h"

#include <vector>

#include "../AppConfig.h"
#include "../config/SiteStore.h"
#include "../state/DeviceState.h"
#include "CsvFile.h"

namespace acorn {
namespace PlaybackLogger {

namespace {

LastPlayback lastPlayback;

// Minimal CSV split that honours double-quoted fields.
std::vector<String> split(const String& line) {
  std::vector<String> out;
  String cur;
  bool quoted = false;
  for (size_t i = 0; i < line.length(); i++) {
    const char c = line[i];
    if (quoted) {
      if (c == '"' && i + 1 < line.length() && line[i + 1] == '"') {
        cur += '"';
        i++;
      } else if (c == '"') {
        quoted = false;
      } else {
        cur += c;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      out.push_back(cur);
      cur = "";
    } else if (c != '\r') {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

void remember(const std::vector<String>& c) {
  if (c.size() < 10) return;
  lastPlayback.present = true;
  lastPlayback.actualUtc = c[0];
  lastPlayback.scheduledUtc = c[1];
  lastPlayback.eventId = c[4];
  lastPlayback.source = c[5];
  lastPlayback.audio = c[6];
  lastPlayback.volume = c[7].toInt();
  lastPlayback.result = c[9];
}

}  // namespace

bool ensure() { return CsvFile::ensure(app::PLAYBACK_LOG, Csv::PLAYBACK_HEADER); }

bool append(const Csv::PlaybackRow& row) {
  Csv::PlaybackRow stamped = row;
  stamped.location = SiteStore::location();
  const std::string line = Csv::playbackRow(stamped);
  const bool ok = CsvFile::append(app::PLAYBACK_LOG, Csv::PLAYBACK_HEADER, String(line.c_str()));
  DeviceState::reportLogWrite(ok);
  if (ok) remember(split(String(line.c_str())));
  return ok;
}

const LastPlayback& last() { return lastPlayback; }

void reloadLast() {
  lastPlayback = LastPlayback{};
  const String row = CsvFile::lastRow(app::PLAYBACK_LOG);
  if (row.length()) remember(split(row));
}

}  // namespace PlaybackLogger
}  // namespace acorn
