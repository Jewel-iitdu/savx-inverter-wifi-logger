// Watchdog.cpp - see Watchdog.h.
#include "sys/Watchdog.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

namespace watchdog {

void begin(uint32_t timeoutS) {
  esp_task_wdt_config_t c = {};
  c.timeout_ms = timeoutS * 1000;
  c.idle_core_mask = 0;  // watch our tasks, not the idle tasks
  c.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&c) != ESP_OK) esp_task_wdt_init(&c);
}

const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "UNKNOWN";
  }
}

bool lastResetWasCrash() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
    case ESP_RST_UNKNOWN:
      return true;
    default:
      return false;
  }
}

}  // namespace watchdog
