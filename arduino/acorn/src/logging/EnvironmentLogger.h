#pragma once

#include <Arduino.h>
#include <AcornCore.h>

// /logs/environment.csv (init.md section 35).

namespace acorn {
namespace EnvironmentLogger {

bool ensure();
// Appends a row stamped with the sample boundary; on failure the blocking
// SD_WRITE_FAILED fault is raised.
bool append(int64_t boundaryUtc, const Csv::Telemetry& t);

}  // namespace EnvironmentLogger
}  // namespace acorn
