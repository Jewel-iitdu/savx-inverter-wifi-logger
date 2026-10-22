// Decode.cpp - see Decode.h.
#include "model/Decode.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace decode {

static bool inRangeOrZero(float v, float lo, float hi) { return isnan(v) || v == 0.f || (v >= lo && v <= hi); }

static bool gates(float bv, float ov, float of, float soc, float pvv, float ld) {
  if (isnan(bv) || bv < cfg::GATE_BV_MIN || bv > cfg::GATE_BV_MAX) return false;
  if (!inRangeOrZero(ov, cfg::GATE_OV_MIN, cfg::GATE_OV_MAX)) return false;
  if (!inRangeOrZero(of, cfg::GATE_OF_MIN, cfg::GATE_OF_MAX)) return false;
  if (!isnan(soc) && (soc < 0.f || soc > 100.f)) return false;
  if (!isnan(pvv) && (pvv < 0.f || pvv > cfg::GATE_PVV_MAX)) return false;
  if (!isnan(ld) && (ld < 0.f || ld > cfg::GATE_LD_MAX)) return false;
  return true;
}

bool plausibleQpigs(const pi30p::Qpigs& q) {
  if (q.n < 17) return false;
  return gates(q.v[8], q.v[2], q.v[3], q.v[10], q.v[13], q.v[6]);
}

bool plausibleQall(const pi30p::Qall& a) {
  if (a.n != 18) return false;
  return gates(a.v[6], a.v[2], a.v[3], a.v[7], a.v[10], a.v[5]);
}

bool plausibleSample(const Sample& s) { return gates(s.bv, s.ov, s.of, s.soc, s.pvv, s.ld); }

char classifyPv12(const pi30p::Qpigs& q) {
  if (q.n < 14) return '?';
  float f12 = q.v[12], pvv = q.v[13];
  if (isnan(f12) || isnan(pvv) || pvv <= 30.f) return '?';
  if (f12 > cfg::PV_MAX_INPUT_A) return 'W';  // > 15 A is impossible on this unit -> power
  if (q.n >= 20 && !isnan(q.v[19]) && q.v[19] > 50.f && f12 > 0.f) {
    float p = f12 * pvv;
    if (fabsf(p - q.v[19]) <= 0.2f * q.v[19]) return 'A';
  }
  return '?';
}

void qpigs(const pi30p::Qpigs& q, Dialect d, char pv12, Sample& s) {
  if (q.n < 17) return;
  snprintf(s.st, sizeof s.st, "%s", q.st);
  if (q.st2[0]) snprintf(s.st2, sizeof s.st2, "%s", q.st2);
  s.bus = q.v[7];
  if (d == Dialect::REVO) return;  // QALL supplies the rest; QPIGS[15] is PV Wh on REVO (not discharge A)
  s.gv = q.v[0];
  s.gf = q.v[1];
  s.ov = q.v[2];
  s.of = q.v[3];
  s.va = q.v[4];
  s.w = q.v[5];
  s.ld = q.v[6];
  s.bv = q.v[8];
  s.bci = q.v[9];
  s.soc = q.v[10];
  float t = q.v[11];
  s.tmpRaw = !isnan(t) && t > cfg::TMP_RAW_ADC_ABOVE;
  s.tmp = s.tmpRaw ? NAN : t;
  s.pvv = q.v[13];
  s.sccv = q.v[14];
  s.bdi = q.v[15];
  char eff = pv12 != '?' ? pv12 : classifyPv12(q);
  if (eff == 'W') {
    s.pvw = q.v[12];
    s.pvi = (!isnan(s.pvv) && s.pvv > 1.f && !isnan(s.pvw)) ? roundf(s.pvw / s.pvv * 10.f) / 10.f : 0.f;
  } else {
    s.pvi = q.v[12];
    if (q.n >= 20 && !isnan(q.v[19])) s.pvw = q.v[19];  // "PV charging power" (charger output)
    else if (!isnan(s.pvv) && !isnan(s.pvi)) s.pvw = roundf(s.pvv * s.pvi);
  }
  s.finalizeDerived();
}

void qall(const pi30p::Qall& a, Sample& s) {
  if (a.n != 18) return;
  s.gv = a.v[0];
  s.gf = a.v[1];
  s.ov = a.v[2];
  s.of = a.v[3];
  s.w = a.v[4];
  s.ld = a.v[5];
  s.bv = a.v[6];
  s.soc = a.v[7];
  s.bci = a.v[8];
  s.bdi = a.v[9];
  s.pvv = a.v[10];
  s.pvi = a.v[11];
  s.pvw = a.v[12];
  s.epvi = a.v[13];
  s.etot = a.v[14];
  s.md = a.mode;
  s.wc = (int16_t)a.warn;
  s.fc = (int16_t)a.fault;
  s.fcSrc = codes::Src::REVO;
  s.finalizeDerived();
}

void qmod(char mode, Sample& s) {
  if (mode) s.md = mode;
}

void qpiws(const char* wf, Dialect d, Sample& s) {
  if (!wf) return;
  snprintf(s.wf, sizeof s.wf, "%s", wf);
  if (d != Dialect::REVO && strlen(wf) >= 36 && (d == Dialect::MAX24 || d == Dialect::GK)) {
    int fc = pi30p::qpiwsFaultCode(wf);
    if (fc >= 0) {
      s.fc = (int16_t)fc;
      s.fcSrc = codes::Src::QPIWS36;
    }
  }
  s.faultBits = codes::qpiwsHasFault(s.wf);
}

void q1(const pi30p::Q1& q, Sample& s) {
  if (q.n < 17) return;
  float t = q.v[6];  // inverter temperature °C (r2 §5.9, SECONDARY)
  if ((isnan(s.tmp) || s.tmpRaw) && !isnan(t) && t > -20.f && t < 120.f) {
    s.tmp = t;
    s.tmpRaw = false;
  }
}

void carrySlowFields(const Sample& from, Sample& to) {
  if (!to.md) to.md = from.md;
  if (!to.st[0]) memcpy(to.st, from.st, sizeof to.st);
  if (!to.st2[0]) memcpy(to.st2, from.st2, sizeof to.st2);
  if (!to.wf[0]) memcpy(to.wf, from.wf, sizeof to.wf);
  if (to.wc == INT16_MIN) to.wc = from.wc;
  if (to.fc == INT16_MIN) { to.fc = from.fc; to.fcSrc = from.fcSrc; }
  if (isnan(to.bus)) to.bus = from.bus;
  if (isnan(to.epvi)) to.epvi = from.epvi;
  if (isnan(to.etot)) to.etot = from.etot;
  if (isnan(to.tmp) && !to.tmpRaw) to.tmp = from.tmp;
}

}  // namespace decode
