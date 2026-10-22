// MonthToDate.h - billing-cycle energy sums for `month` rules (r8 §4.5). Host-portable.
//
// The cycle starts on local day `startDay` (alerts/email/monthStart, 1..28): cycleStart(today) is that date of this
// month if today.day >= startDay, else of the previous month. Finished days are folded in exactly once (a 31-bit
// mask over the cycle, so folding is idempotent and gap-tolerant - a refinement of r8's `lastFolded` field).
// Days that are missing locally are filled from the day files (gap fill) or from RTDB `daily` (bootstrap, by the
// uplink). A month rule may therefore see an incomplete sum: gt/ge can only fire late, never falsely.
// The blob is persisted in NVS energy/mtd on every change.
#pragma once
#include <stdint.h>

#include "agg/RuleTypes.h"
#include "store/MinuteRec.h"

#pragma pack(push, 1)
struct MtdBlob {
  uint16_t ver = 2;
  uint8_t startDay = 1, flags = 0;   // flags b0: RTDB bootstrap done for this cycle
  int32_t cycleDay = 0;              // local day number of the cycle start (0 = not started)
  uint32_t folded = 0;               // bit i = day cycleDay + i folded
  float eld = 0, egr = 0, epv = 0, edis = 0, ech = 0;  // Wh
  uint32_t gout = 0, nMin = 0;       // outage minutes, minutes with data
  uint16_t days = 0;                 // days folded
};
#pragma pack(pop)

class MonthToDate {
 public:
  static int32_t cycleStart(int32_t today, uint8_t startDay);
  static uint32_t yyyymmdd(int32_t day);

  void load(const MtdBlob& b) { b_ = b; }
  const MtdBlob& blob() const { return b_; }

  // Both return true when the blob changed (persist it).
  bool setStartDay(uint8_t startDay, int32_t today);  // a change restarts the cycle
  bool roll(int32_t today);                           // new cycle -> reset sums
  bool fold(const mrec::Daily& d);                    // idempotent; ignores days outside [cycleDay, ...)

  bool folded(int32_t day) const;
  // days in [cycleDay, today-1] not folded yet (ascending); returns the count
  int missing(int32_t today, int32_t* out, int max) const;
  bool needBootstrap(int32_t today) const { return !(b_.flags & 1) && missing(today, nullptr, 0) > 0; }
  void markBootstrapped() { b_.flags |= 1; }

  double sum(rules::Metric m) const;                  // cycle sum excluding today (Wh or minutes)
  uint8_t coveragePct(int32_t today) const;           // minutes with data / minutes of the finished cycle days
  uint32_t cycleKey() const { return b_.cycleDay ? yyyymmdd(b_.cycleDay) : 0; }
  int32_t cycleDay() const { return b_.cycleDay; }

 private:
  MtdBlob b_;
};
