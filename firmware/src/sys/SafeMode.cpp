// SafeMode.cpp - see SafeMode.h.
#include "sys/SafeMode.h"

#include <Arduino.h>

#include "config.h"
#include "net/Budget.h"
#include "store/Settings.h"
#include "sys/Watchdog.h"

namespace safemode {

static bool cleared_ = false, crashSafe_ = false;
static uint8_t fails_ = 0;

bool evaluateAtBoot() {
  fails_ = settings.bootFails();
  if (watchdog::lastResetWasCrash()) {
    if (fails_ < 255) fails_++;
    settings.setBootFails(fails_);
  }
  bool manual = settings.safeFlag();
  crashSafe_ = fails_ >= cfg::SAFE_MODE_BOOTFAILS && !manual;
  return crashSafe_ || manual;
}

void loop(uint32_t nowMs) {
  if (safeModeAutoExit(crashSafe_, nowMs)) {  // 0.9.9: nobody may be home to end it; a real crash loop returns here
    char cause[64];
    snprintf(cause, sizeof cause, "left safe mode after 30 min (%u crashes in a row)", (unsigned)fails_);
    Serial.printf("[safe] %s, rebooting\n", cause);
    settings.setBootFails(0);
    settings.setStr("sys", "rbCause", cause);
    delay(200);
    ESP.restart();
  }
  if (cleared_ || nowMs < cfg::HEALTHY_AFTER_MS) return;
  cleared_ = true;
  if (fails_) settings.setBootFails(0);
}

void exitAndReboot() {
  settings.setBootFails(0);
  settings.setSafeFlag(false);
  settings.setStr("sys", "plannedRb", "safe-exit");
  Serial.println(F("[safe] leaving safe mode, rebooting"));
  delay(200);
  ESP.restart();
}

uint8_t bootFails() { return fails_; }

}  // namespace safemode
