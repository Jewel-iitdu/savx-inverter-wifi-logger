// MeterDay.cpp - see MeterDay.h.
#include "meter/MeterDay.h"

#include "model/TimeUtil.h"

void MeterDay::restart(uint32_t epoch, int32_t localDay, uint32_t counter) {
  s_.day = localDay;
  s_.start = counter;
  s_.full = 0;
  s_.last = counter;
  s_.lastEpoch = epoch;
}

bool MeterDay::update(uint32_t epoch, int32_t localDay, uint32_t counter) {
  if (!s_.day || localDay < s_.day || epoch < s_.lastEpoch) {  // first reading, or the clock went backwards
    restart(epoch, localDay, counter);
    return false;
  }
  // plausibility against the last reading: never down, and not faster than the breaker can carry
  uint64_t maxUp = (uint64_t)(epoch - s_.lastEpoch) * MAX_KWH_PER_H * 100 / 3600 + 5;
  bool jump = counter < s_.last || (uint64_t)(counter - s_.last) > maxUp;
  if (localDay == s_.day) {
    if (jump) {
      resets_++;
      restart(epoch, localDay, counter);  // today is partial from here on
    } else {
      s_.last = counter;
      s_.lastEpoch = epoch;
    }
    return false;
  }
  // a new day: close the old one with its last reading, start the new one from it if it is recent enough
  s_.prevDay = s_.day;
  s_.prevWh = s_.full ? (int32_t)((s_.last - s_.start) * 10) : -1;
  bool recent = !jump && localDay == s_.day + 1 && s_.lastEpoch + GAP_S >= tu::epochOfLocalMidnight(localDay);
  if (jump) resets_++;
  if (recent) {
    s_.day = localDay;
    s_.start = s_.last;
    s_.full = 1;
    s_.last = counter;
    s_.lastEpoch = epoch;
  } else {
    restart(epoch, localDay, counter);
  }
  return true;
}

int32_t MeterDay::whFor(int32_t day) const {
  if (day && day == s_.day) return s_.full ? (int32_t)((s_.last - s_.start) * 10) : -1;
  if (day && day == s_.prevDay) return s_.prevWh;
  return -1;
}
