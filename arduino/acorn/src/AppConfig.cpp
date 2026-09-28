#include "AppConfig.h"

#include <stdio.h>
#include <string.h>

namespace acorn {
namespace app {

const char* buildTimestamp() {
  static char out[20] = "";
  if (out[0]) return out;
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* d = __DATE__;  // "Sep 28 2026"
  char mon[4] = {d[0], d[1], d[2], 0};
  const char* hit = strstr(months, mon);
  const int m = hit ? static_cast<int>(hit - months) / 3 + 1 : 1;
  const int day = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
  snprintf(out, sizeof(out), "%s-%02d-%02dT%s", d + 7, m, day, __TIME__);
  return out;
}

}  // namespace app
}  // namespace acorn
