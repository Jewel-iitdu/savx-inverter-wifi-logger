// MeterLive.h - one grid meter reading and its JSON (0.9.0). Host-portable.
// Cloud keys (web/src/data/types.ts, all numeric so the existing `live` / `daily` rules accept them):
//   live:  mw W, mv V, mi A, mpf, mhz Hz, mrc residual mA, mtc °C, mkwh lifetime kWh, mon 1/0, egm Wh today,
//          mft the meter's alarm bitmap (0.9.1, labels in web/src/lib/meter.ts), mage s since the reading (0.9.1)
//   daily: egm Wh (fully metered days only)
// The estimate fields (gw, egr) are never touched: Settings -> Grid meter decides which one the app shows.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

class JsonOut;

struct MeterLive {
  float w = NAN, v = NAN, a = NAN, pf = NAN, hz = NAN, rc = NAN, tc = NAN;
  uint32_t counter = 0;   // lifetime forward energy, 0.01 kWh
  bool haveCounter = false;
  int8_t on = -1;         // breaker closed 1 / open 0 / unknown -1
  int32_t egm = -1;       // today's metered Wh, -1 = partial day
  int32_t fault = -1;     // 0.9.1: alarm bitmap (data point 9), -1 = unknown
  int32_t ageS = -1;      // 0.9.1: seconds since the reading (the app scales the 2 s inverter VA between readings)
};

// Merge the data points of one status payload (`{"dps":{...}}` or `{"data":{"dps":{...}}}`) into m.
// false: not JSON or no dps. Unknown data points are ignored.
bool meterDecodeDps(const char* json, size_t n, MeterLive& m);

void meterLiveFields(JsonOut& j, const MeterLive& m);

// Append ,"key":v before the closing brace of a JSON object in buf (len = strlen). false if it does not fit.
bool jsonAppendInt(char* buf, size_t cap, const char* key, long v);
