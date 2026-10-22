// Pi30Parse.h - parsers for PI30-family reply payloads (string after '(' without CRC). Host-portable.
// Field indices are 0-based (V1 V14, V16, V17; r2 §5). Non-numeric tokens become NaN.
#pragma once
#include <math.h>
#include <stdint.h>

namespace pi30p {

constexpr int QPIGS_MAX = 28;
struct Qpigs {
  int n = 0;                 // field count (17 / 21 / 24 ...)
  float v[QPIGS_MAX];        // numeric fields (index 16 and 20 are bit strings -> NaN here)
  char st[9] = "";           // field 16: b7..b0
  char st2[4] = "";          // field 20: b10 b9 b8 (21+ fields)
};
bool parseQpigs(const char* payload, Qpigs& q);

// REVO QALL: exactly 18 fields (V1 V17). Field 15 is the mode letter.
struct Qall {
  int n = 0;
  float v[18];
  char mode = 0;
  int warn = -1, fault = -1;
};
bool parseQall(const char* payload, Qall& a);

// REVO QLITH0: lithium BMS data relayed by the inverter, 12 fields (mpp-solar pi30revo):
// 0 BMS V, 1 charging A, 2 discharging A (0.1 A resolution, both >= 0), 3 temperature 0.1 °C, 4 capacity %,
// 5 wear-out %, 6 max charge A, 7 max discharge A, 8 max charge V, 9 min discharge V, 10 fault, 11 warning.
struct Qlith0 {
  int n = 0;
  float v[12];
};
bool parseQlith0(const char* payload, Qlith0& b);

constexpr int QPIRI_MAX = 32;
struct Qpiri {
  int n = 0;
  float v[QPIRI_MAX];
};
bool parseQpiri(const char* payload, Qpiri& r);

char parseQmod(const char* payload);  // 0 if not a single known mode letter

// QPIWS: a string of '0'/'1' (36/38-char dialects may embed fault-code digits at a31-a32). 32..40 chars.
bool parseQpiws(const char* payload, char* wf, int cap);
// Embedded 2-digit fault code at a31..a32 (24 V MAX/GK). Returns -1 if absent.
int qpiwsFaultCode(const char* wf);

// QFLAG "E<letters>D<letters>"; unknown characters are ignored (r2 §8 quirk 3).
bool parseQflag(const char* payload, char* enabled, int ecap, char* disabled, int dcap);

// "(VERFW:00014.04" -> "00014.04"; prefix "VERFW:" / "VERFW2:" / "VERFW3:"
bool parseVersion(const char* payload, const char* prefix, char* out, int cap);
// QID digits, QSID "NN<digits...>" (NN = valid length)
bool parseQid(const char* payload, char* out, int cap);
bool parseQsid(const char* payload, char* out, int cap);
// QET / QEDyyyymmdd / QLDyyyymmdd / QLT: "(NNNNNNNN" Wh
bool parseEnergyWh(const char* payload, uint32_t& wh);

// Q1 (undocumented extended status, r2 §5.9): 17+ fields; we use [6] inverter temperature, [16] charge stage
struct Q1 {
  int n = 0;
  float v[28];
};
bool parseQ1(const char* payload, Q1& q);

// REVO PQSE (doc §1.5): "BCDEFG HH.H II.I JJ.J KK.K LLL MMM NNNN OO PP QQ KK [SSTUV WW.W]"
struct Pqse {
  bool ok = false;
  int lang = -1, workMode = -1, inRange = -1, outVSet = -1, outFSet = -1, batType = -1;
  float bulkV = NAN, floatV = NAN, lowAlarmV = NAN, cutOffV = NAN;
  int sumChgA = -1, acChgA = -1;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0;
};
bool parsePqse(const char* payload, Pqse& p);

// Generic numeric token parse: NaN if not a number.
float num(const char* tok);

}  // namespace pi30p
