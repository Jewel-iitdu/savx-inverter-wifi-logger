// SafeMode.cpp - see SafeMode.h.
#include "sys/SafeMode.h"

#include <Arduino.h>

#include "config.h"
#include "store/Settings.h"
#include "sys/Watchdog.h"

namespace safemode {

static bool cleared_ = false;
static uint8_t fails_ = 0;

bool evaluateAtBoot() {
  fails_ = settings.bootFails();
  if (watchdog::lastResetWasCrash()) {
    if (fails_ < 255) fails_++;
    settings.setBootFails(fails_);
  }
  return fails_ >= cfg::SAFE_MODE_BOOTFAILS || settings.safeFlag();
}

void loop(uint32_t nowMs) {
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
