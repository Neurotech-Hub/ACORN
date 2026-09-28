#include "EnvironmentLogger.h"

#include "../AppConfig.h"
#include "../services/TimeService.h"
#include "../state/DeviceState.h"
#include "CsvFile.h"

namespace acorn {
namespace EnvironmentLogger {

bool ensure() { return CsvFile::ensure(app::ENVIRONMENT_LOG, Csv::ENVIRONMENT_HEADER); }

bool append(int64_t boundaryUtc, const Csv::Telemetry& t) {
  const std::string line = Csv::environmentRow(TimeService::isoUtc(boundaryUtc).c_str(), t);
  const bool ok = CsvFile::append(app::ENVIRONMENT_LOG, Csv::ENVIRONMENT_HEADER, String(line.c_str()));
  DeviceState::reportLogWrite(ok);
  return ok;
}

}  // namespace EnvironmentLogger
}  // namespace acorn
