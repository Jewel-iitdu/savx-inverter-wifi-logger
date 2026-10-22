// MonthToDate.cpp - see MonthToDate.h.
#include "agg/MonthToDate.h"

#include <math.h>

#include "model/TimeUtil.h"

int32_t MonthToDate::cycleStart(int32_t today, uint8_t startDay) {
  if (startDay < 1) startDay = 1;
  if (startDay > 28) startDay = 28;
  int y;
  unsigned m, d;
  tu::civilFromDays(today, y, m, d);
  if (d >= startDay) return tu::daysFromCivil(y, m, startDay);
  if (m == 1) return tu::daysFromCivil(y - 1, 12, startDay);
  return tu::daysFromCivil(y, m - 1, startDay);
}

uint32_t MonthToDate::yyyymmdd(int32_t day) {
  int y;
  unsigned m, d;
  tu::civilFromDays(day, y, m, d);
  return (uint32_t)y * 10000u + m * 100u + d;
}

bool MonthToDate::setStartDay(uint8_t startDay, int32_t today) {
  if (startDay < 1 || startDay > 28) startDay = 1;
  if (startDay == b_.startDay && b_.cycleDay) return roll(today);
  uint8_t keepVer = b_.ver;
  b_ = MtdBlob();
  b_.ver = keepVer ? keepVer : 2;
  b_.startDay = startDay;
  roll(today);
  return true;
}

bool MonthToDate::roll(int32_t today) {
  if (today <= 0) return false;
  int32_t cs = cycleStart(today, b_.startDay);
  if (cs == b_.cycleDay) return false;
  uint8_t sd = b_.startDay;
  b_ = MtdBlob();
  b_.startDay = sd;
  b_.cycleDay = cs;
  return true;
}

bool MonthToDate::folded(int32_t day) const {
  int32_t i = day - b_.cycleDay;
  if (!b_.cycleDay || i < 0 || i >= 31) return false;
  return (b_.folded >> i) & 1u;
}

bool MonthToDate::fold(const mrec::Daily& d) {
  int32_t i = d.day - b_.cycleDay;
  if (!b_.cycleDay || i < 0 || i >= 31) return false;
  if ((b_.folded >> i) & 1u) return false;
  // a day of the NEXT cycle must not be folded into this one
  if (cycleStart(d.day, b_.startDay) != b_.cycleDay) return false;
  b_.folded |= (1u << i);
  b_.eld += (float)d.eld;
  b_.egr += (float)d.egr;
  b_.epv += (float)d.epv;
  b_.edis += (float)d.edis;
  b_.ech += (float)d.ech;
  b_.gout += (uint32_t)(d.gout > 0 ? d.gout : 0);
  b_.nMin += (uint32_t)(d.n > 0 ? d.n : 0);
  b_.days++;
  return true;
}

int MonthToDate::missing(int32_t today, int32_t* out, int max) const {
  if (!b_.cycleDay) return 0;
  int n = 0;
  for (int32_t d = b_.cycleDay; d < today && d - b_.cycleDay < 31; d++)
    if (!folded(d)) {
      if (out && n < max) out[n] = d;
      n++;
    }
  return n;
}

double MonthToDate::sum(rules::Metric m) const {
  switch (m) {
    case rules::Metric::ELD: return b_.eld;
    case rules::Metric::EGR: return b_.egr;
    case rules::Metric::EPV: return b_.epv;
    case rules::Metric::EDIS: return b_.edis;
    case rules::Metric::ECH: return b_.ech;
    case rules::Metric::GOUT: return b_.gout;
    default: return NAN;
  }
}

uint8_t MonthToDate::coveragePct(int32_t today) const {
  if (!b_.cycleDay || today <= b_.cycleDay) return 100;
  double full = (double)(today - b_.cycleDay) * 1440.0;
  double c = 100.0 * b_.nMin / full;
  if (c > 100) c = 100;
  return (uint8_t)(c + 0.5);
}
