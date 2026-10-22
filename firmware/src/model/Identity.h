// Identity.h - identity and ratings kept in `meta` (d1 §3.8, §4.2), not in Sample. Host-portable.
#pragma once
#include <math.h>
#include <stdint.h>

struct Identity {
  char qpi[12] = "";     // e.g. "PI30"
  char qid[32] = "";     // serial (QID or QSID)
  char vfw[20] = "";     // QVFW  "00014.04" (= LCD U1)
  char vfw2[20] = "";    // QVFW2 "00003.03" (= LCD U2)
  char vfw3[20] = "";
  char qmn[32] = "";     // QMN model name
  char qgmn[12] = "";
  // QPIRI ratings
  int qpiriFields = 0;
  float ratGridV = NAN, ratGridA = NAN, ratOutV = NAN, ratOutHz = NAN, ratOutA = NAN;
  float ratva = NAN, ratw = NAN, ratbv = NAN;
  float rechgV = NAN, cutV = NAN, bulkV = NAN, floatV = NAN, redischgV = NAN;
  int btype = -1, maxAcChg = -1, maxChg = -1, inrng = -1, outpri = -1, chgpri = -1, machine = -1;
  char qflag[40] = "";   // raw QFLAG payload
  char pqse[96] = "";    // raw REVO PQSE payload
  // classification
  char family[12] = "";  // "pi30", "q6", "smg", "powmr", "pi18", "raw"
  char dia[10] = "";     // STOCK17 / STOCK21 / MAX24 / GK / REVO
  char pv12 = '?';       // 'W' | 'A' | '?' (QPIGS field 12 semantics, d1 §3.6 rule 5)
  char sup[200] = "";    // comma list of supported commands
  char link[40] = "";    // e.g. "rs232lite-2400-ii-tx1"
  uint32_t qtEpoch = 0;  // inverter clock (QT, MAX) as UTC epoch; secondary time source (d1 §5.6)
  uint32_t qtAtMs = 0;   // millis() when QT was read
};
