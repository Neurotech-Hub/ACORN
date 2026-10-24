#pragma once

#include <Arduino.h>
#include <AcornCore.h>

// Site location in /config/site.json on the SD card (README "site lat/long").
// It travels with the card, not the device. A missing or invalid file means
// "no fix": logs get empty latitude/longitude cells and nothing is blocked.
// Saves are atomic: write site.json.tmp, read back and validate, then
// replace site.json.

namespace acorn {
namespace SiteStore {

void invalidate();  // SD change; the file is reread on next use
const Site& get();
bool hasFix();
Csv::Location location();  // for CSV rows

// `site` must already be valid (SiteJson::parse).
bool save(const Site& site, const char*& errorCode);
bool clear(const char*& errorCode);

}  // namespace SiteStore
}  // namespace acorn
