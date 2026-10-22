// TimeSync.h - SNTP with TZ "<+06>-6" (Asia/Dhaka, no DST), validity (year >= 2026) and the first-sync anchor
// used to fix provisional timestamps (d1 §5.6). HTTPS works before NTP (mbedTLS date checks are off).
#pragma once
#include <Arduino.h>

class TimeSync {
 public:
  void begin();
  bool valid();
  uint32_t epochOrZero();
  // true once per boot when the clock first becomes valid; gives the anchor (millis, epoch)
  bool takeFirstValid(uint32_t& upMs, uint32_t& epoch);
  // the anchor of this boot (millis, epoch) once the clock was valid; used to timestamp events raised before NTP
  bool anchor(uint32_t& upMs, uint32_t& epoch) const {
    if (!firstSeen_) return false;
    upMs = anchorUp_;
    epoch = anchorEpoch_;
    return true;
  }
  // Inverter clock (QT) as a secondary source: used only if NTP has not set the clock yet.
  void offerInverterClock(uint32_t epoch);
  const char* source() const { return source_; }

 private:
  bool firstSeen_ = false, firstTaken_ = false;
  uint32_t anchorUp_ = 0, anchorEpoch_ = 0;
  const char* source_ = "none";
};

extern TimeSync timeSync;
