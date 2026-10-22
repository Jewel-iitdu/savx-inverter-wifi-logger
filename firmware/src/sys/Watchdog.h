// Watchdog.h - task watchdog 30 s, panic on expiry (d1 §5.9). Each long-running task subscribes itself
// with esp_task_wdt_add(NULL) and resets in its loop (ArduinoClock::sleepMs also feeds it during waits).
#pragma once
#include <stdint.h>

namespace watchdog {
void begin(uint32_t timeoutS);
const char* resetReasonName();  // POWERON, SW, PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT, DEEPSLEEP, EXT, ...
bool lastResetWasCrash();
}  // namespace watchdog
