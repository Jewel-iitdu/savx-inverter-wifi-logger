// MeterDay.h - metered grid energy per local day from the meter's lifetime kWh counter (0.9.0). Host-portable.
// egm(day) = counter at the end of the day - counter at its start. A day counts as fully metered only if its start
// reading is the last one before midnight (at most METER_DAY_GAP_S earlier); otherwise, or after a counter jump or
// reset, the day is "partial" and the app falls back to the inverter estimate (`egr`). Energy between the last
// reading before midnight and midnight goes to the new day: with a 15 s poll that is seconds' worth.
#pragma once
#include <stdint.h>

#pragma pack(push, 1)
struct MeterDayState {  // NVS blob "meter"/"day" (layout fixed; append only)
  uint8_t ver = 1;
  int32_t day = 0;          // local day number of `start` (0 = no reading yet)
  uint32_t start = 0;       // counter at the start of `day`, 0.01 kWh
  uint8_t full = 0;         // 1 = `start` was taken at the beginning of `day`
  uint32_t last = 0;        // last accepted counter reading, 0.01 kWh
  uint32_t lastEpoch = 0;   // UTC epoch of `last`
  int32_t prevDay = 0;      // the finished day before `day`
  int32_t prevWh = -1;      // its metered energy, Wh (-1 = partial / unknown)
};
#pragma pack(pop)

class MeterDay {
 public:
  static constexpr uint32_t GAP_S = 2 * 3600;   // max age of the last reading before midnight for a full day
  static constexpr uint32_t MAX_KWH_PER_H = 16; // 63 A x 250 V: anything faster is a glitch or a meter swap

  void load(const MeterDayState& s) { s_ = s; }
  const MeterDayState& state() const { return s_; }

  // One counter reading (0.01 kWh) at UTC epoch / local day. Returns true when the day rolled over (the caller
  // persists the state and writes the finished day's egm).
  bool update(uint32_t epoch, int32_t localDay, uint32_t counter);

  int32_t whFor(int32_t day) const;  // metered Wh of `day` (today so far, or the finished day), -1 = unknown
  uint32_t resets() const { return resets_; }

 private:
  void restart(uint32_t epoch, int32_t localDay, uint32_t counter);
  MeterDayState s_;
  uint32_t resets_ = 0;
};
