// Sample.h - the unified normalized data model (d1 §3.8). SI units. Host-portable.
// Missing floats are NaN, missing ints are INT16_MIN; missing values are omitted from JSON.
// The same short keys are used in the firmware, the local API and the cloud `live` node.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "codes.h"

struct Sample {
  uint32_t ts = 0;     // UTC epoch seconds (0 = no valid time yet)
  uint32_t upMs = 0;   // millis() when the sample was taken (energy integration time base)
  uint8_t lk = 0;      // 1 = valid inverter reply within 15 s

  float gv = NAN, gf = NAN;          // grid V / Hz
  float ov = NAN, of = NAN;          // output V / Hz
  float va = NAN, w = NAN, ld = NAN; // output VA / W / load %
  float bus = NAN;                   // DC bus V
  float bv = NAN, bci = NAN, bdi = NAN;  // battery V, charge A, discharge A
  float bi = NAN, bw = NAN;          // derived: net battery A (+ = charging), battery W
  float soc = NAN;                   // %
  float tmp = NAN;                   // heat-sink °C (omitted if raw ADC)
  float pvi = NAN, pvv = NAN, pvw = NAN;  // PV A / V / W
  float sccv = NAN;                  // battery V measured by the SCC
  char md = 0;                       // mode letter (P S L B F H D C Y E), 0 = unknown
  char st[9] = "";                   // status bits b7..b0
  char st2[4] = "";                  // status bits b10 b9 b8
  char wf[40] = "";                  // QPIWS bit string
  int16_t wc = INT16_MIN;            // warning code (REVO table)
  int16_t fc = INT16_MIN;            // fault code (table given by fcSrc)
  codes::Src fcSrc = codes::Src::NONE;
  float epv = NAN, eld = NAN, ech = NAN, edis = NAN, egr = NAN;  // Wh today, integrated on the device
  float epvi = NAN;                  // Wh today reported by the inverter
  float etot = NAN;                  // kWh lifetime reported by the inverter
  int32_t q = -1;                    // minute records waiting for upload

  // internal (not serialized)
  bool tmpRaw = false;               // QPIGS[11] looked like a raw ADC value
  bool faultBits = false;            // any fault-class QPIWS bit set

  void finalizeDerived() {
    if (!isnan(bci) || !isnan(bdi)) {
      float c = isnan(bci) ? 0.f : bci, d = isnan(bdi) ? 0.f : bdi;
      bi = c - d;
      if (!isnan(bv)) bw = bv * bi;
    }
    faultBits = wf[0] ? codes::qpiwsHasFault(wf) : false;
  }
  bool stBit(int bit) const {
    int idx = 7 - bit;
    return (int)strlen(st) > idx && idx >= 0 && st[idx] == '1';
  }
};

inline bool hasv(float v) { return !isnan(v); }
