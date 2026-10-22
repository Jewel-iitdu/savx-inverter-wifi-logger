// MinuteRec.h - the 55-byte packed 1-minute record stored in LittleFS (d1 §5.5) and its JSON forms.
// Host-portable.
#pragma once
#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)
struct MinuteRec {           // 55 bytes
  uint32_t ts;               // UTC epoch of the minute start (provisional if flags b0)
  uint16_t bootId;
  uint32_t upMs;             // millis() at the minute start (for provisional-time fix-up)
  uint8_t n, flags;          // n valid samples; flags: b0 provisional time, b1 link gap, b2 grid absent (majority)
  int16_t w, wx, pvw, pvx, bw;   // W (avg, max, avg, max, avg battery W)
  uint16_t bv, bvn, bvx;     // cV (avg, min, max)
  int16_t bi;                // dA (avg, + = charging)
  uint16_t gv, gvn, ov, pvv; // dV (avg, min, avg, avg)
  uint8_t soc, ld;           // % (last, avg)
  int8_t tmp;                // °C (max)
  char md;                   // dominant mode letter
  uint8_t wn, fc;            // distinct QPIWS warning bits seen; last non-zero fault code
  uint16_t epv, eld, ech, edis, egr;  // dWh (0.1 Wh) in this minute
  uint8_t crc8;
};
#pragma pack(pop)
static_assert(sizeof(MinuteRec) == 55, "MinuteRec must be 55 bytes (d1 §5.5)");

namespace mrec {

constexpr int16_t I16_NA = INT16_MIN;
constexpr uint16_t U16_NA = 0xFFFF;
constexpr uint8_t U8_NA = 0xFF;
constexpr int8_t I8_NA = INT8_MIN;

constexpr uint8_t F_PROVISIONAL = 0x01, F_LINK_GAP = 0x02, F_NO_GRID = 0x04;

uint8_t crc8(const uint8_t* d, size_t n);  // CRC-8 poly 0x07, init 0
void seal(MinuteRec& r);                    // compute crc8
bool valid(const MinuteRec& r);
MinuteRec empty();                          // all fields "missing"

// The d1 §4.2 `hist/<date>/<hhmm>` document (exact keys). Adds "tq":0 for provisional timestamps.
size_t toHistJson(const MinuteRec& r, char* out, size_t cap);

// 15-minute aggregate (same schema as hist): n-weighted averages, min of minima, max of maxima,
// sum of energies, n = sum, soc = last, md = dominant by n, wn = max, fc = last non-zero.
struct H15 {
  uint32_t ts = 0;       // epoch of the quarter start
  MinuteRec agg;
  int count = 0;         // minute records folded
  int bestModeN = -1;    // samples behind the current dominant mode
};
void h15Begin(H15& h, uint32_t quarterTs);
void h15Add(H15& h, const MinuteRec& r);
size_t toH15Json(const H15& h, char* out, size_t cap);

// Day totals (d1 §4.2 `daily/<date>`). All energies Wh.
struct Daily {
  int32_t day = 0;        // local day number
  double epv = 0, eld = 0, egr = 0, ech = 0, edis = 0;
  float epvi = -1;        // inverter-reported PV Wh today (-1 unknown)
  int pvx = -1, wx = -1;
  float bvn = 0, bvx = 0; // 0 = unknown
  int socn = -1, socx = -1;
  int gout = 0;           // minutes without grid
  int nf = 0, nw = 0;     // fault / warning events
  int n = 0;              // minutes with data
};
void dailyReset(Daily& d, int32_t day);
void dailyAdd(Daily& d, const MinuteRec& r);
size_t toDailyJson(const Daily& d, char* out, size_t cap);

// Provisional-time fix-up (d1 §5.6): ts = anchorEpoch - (anchorUpMs - upMs)/1000 for the same boot.
bool fixupTs(MinuteRec& r, uint16_t anchorBootId, uint32_t anchorUpMs, uint32_t anchorEpoch);

}  // namespace mrec
