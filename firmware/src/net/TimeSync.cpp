// TimeSync.cpp - see TimeSync.h.
#include "net/TimeSync.h"

#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "model/TimeUtil.h"

TimeSync timeSync;

void TimeSync::begin() {
  configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);  // SNTP resync every 3 h (core default)
}

bool TimeSync::valid() { return tu::epochValid((uint32_t)time(nullptr)); }

uint32_t TimeSync::epochOrZero() {
  uint32_t e = (uint32_t)time(nullptr);
  if (!tu::epochValid(e)) return 0;
  if (!firstSeen_) {
    firstSeen_ = true;
    anchorUp_ = millis();
    anchorEpoch_ = e;
    if (strcmp(source_, "inverter") != 0) source_ = "ntp";
  }
  return e;
}

bool TimeSync::takeFirstValid(uint32_t& upMs, uint32_t& epoch) {
  epochOrZero();
  if (!firstSeen_ || firstTaken_) return false;
  firstTaken_ = true;
  upMs = anchorUp_;
  epoch = anchorEpoch_;
  return true;
}

void TimeSync::offerInverterClock(uint32_t epoch) {
  if (valid() || !tu::epochValid(epoch)) return;
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  source_ = "inverter";
}
