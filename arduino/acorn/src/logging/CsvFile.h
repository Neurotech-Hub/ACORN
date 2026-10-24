#pragma once

#include <Arduino.h>
#include <FS.h>

// Append-only CSV files on the SD card (init.md section 37): every row is
// open -> append -> flush -> close, the header is written only when the file
// is new or empty, and failures are reported to the caller.

namespace acorn {
namespace CsvFile {

struct Info {
  bool exists = false;
  size_t sizeBytes = 0;
  uint32_t rows = 0;      // data rows (header excluded)
  String lastTimestamp;   // first field of the last row, "" if none
};

// mkdir + header. A file whose first line differs from `header` (an older
// column layout) is renamed to <path>.bak and a fresh file is started.
bool ensure(const char* path, const char* header);
bool append(const char* path, const char* header, const String& row);
Info info(const char* path);
// Header plus the last `rows` data rows (all rows if rows == 0).
bool tail(const char* path, const char* header, uint32_t rows, String& out);
// The last data row, "" if none.
String lastRow(const char* path);
File openRead(const char* path);  // for streaming downloads

}  // namespace CsvFile
}  // namespace acorn
