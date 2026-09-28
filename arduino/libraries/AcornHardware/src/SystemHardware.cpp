#include "SystemHardware.h"

#include <esp_mac.h>
#include <esp_system.h>

namespace acorn {
namespace SystemHardware {

namespace {
char suffix[7] = {0};
char id[13] = {0};

void ensure() {
  if (suffix[0]) return;
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  snprintf(id, sizeof(id), "ACORN-%s", suffix);
}
}  // namespace

const char* deviceSuffix() {
  ensure();
  return suffix;
}

const char* deviceId() {
  ensure();
  return id;
}

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "UNKNOWN";
  }
}

}  // namespace SystemHardware
}  // namespace acorn
