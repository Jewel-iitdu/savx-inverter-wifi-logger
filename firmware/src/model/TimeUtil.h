// TimeUtil.h - fixed-offset local time for Asia/Dhaka (UTC+6, no DST; d1 §5.6). Host-portable and
// independent of the libc TZ database so date keys are deterministic in tests and on the device.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace tu {

inline bool epochValid(uint32_t e) { return e >= cfg::TIME_VALID_MIN_EPOCH; }

// Howard Hinnant's civil_from_days (public domain algorithm).
inline void civilFromDays(int32_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t yy = (int32_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = (int)(yy + (m <= 2));
}
inline int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

struct Local { int year; unsigned month, day, hour, minute, second; int32_t dayNumber; };

inline Local toLocal(uint32_t epoch) {
  int64_t t = (int64_t)epoch + cfg::TZ_OFFSET_S;
  int32_t days = (int32_t)(t / 86400);
  uint32_t sod = (uint32_t)(t % 86400);
  Local l;
  civilFromDays(days, l.year, l.month, l.day);
  l.hour = sod / 3600;
  l.minute = (sod / 60) % 60;
  l.second = sod % 60;
  l.dayNumber = days;
  return l;
}

// "YYYY-MM-DD" (local date). buf >= 11.
inline void dateKey(uint32_t epoch, char* buf, size_t n) {
  Local l = toLocal(epoch);
  snprintf(buf, n, "%04d-%02u-%02u", l.year, l.month, l.day);
}
// "hhmm" (local). buf >= 5.
inline void hhmmKey(uint32_t epoch, char* buf, size_t n) {
  Local l = toLocal(epoch);
  snprintf(buf, n, "%02u%02u", l.hour, l.minute);
}
// "yyyymmdd" (local) for QEDyyyymmdd.
inline void yyyymmdd(uint32_t epoch, char* buf, size_t n) {
  Local l = toLocal(epoch);
  snprintf(buf, n, "%04d%02u%02u", l.year, l.month, l.day);
}
inline int32_t localDayNumber(uint32_t epoch) { return toLocal(epoch).dayNumber; }
inline unsigned localHour(uint32_t epoch) { return toLocal(epoch).hour; }
inline unsigned localMinuteOfDay(uint32_t epoch) { Local l = toLocal(epoch); return l.hour * 60 + l.minute; }

// Parse "YYYY-MM-DD" -> local day number. Returns false on bad input.
inline bool parseDateKey(const char* s, int32_t& dayNumber) {
  int y;
  unsigned m, d;
  if (!s || sscanf(s, "%4d-%2u-%2u", &y, &m, &d) != 3) return false;
  if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  dayNumber = daysFromCivil(y, m, d);
  return true;
}
inline void dateKeyFromDay(int32_t dayNumber, char* buf, size_t n) {
  int y;
  unsigned m, d;
  civilFromDays(dayNumber, y, m, d);
  snprintf(buf, n, "%04d-%02u-%02u", y, m, d);
}
// UTC epoch of local midnight for a local day number.
inline uint32_t epochOfLocalMidnight(int32_t dayNumber) {
  return (uint32_t)((int64_t)dayNumber * 86400 - cfg::TZ_OFFSET_S);
}

// Parse the inverter clock "YYYYMMDDHHMMSS" (QT; local time) -> UTC epoch, 0 on failure.
inline uint32_t parseQt(const char* s) {
  int y;
  unsigned mo, d, h, mi, se;
  if (!s || strlen(s) != 14) return 0;
  for (int i = 0; i < 14; i++)
    if (s[i] < '0' || s[i] > '9') return 0;
  if (sscanf(s, "%4d%2u%2u%2u%2u%2u", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) return 0;
  int64_t e = (int64_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se - cfg::TZ_OFFSET_S;
  return e > 0 ? (uint32_t)e : 0;
}

}  // namespace tu
