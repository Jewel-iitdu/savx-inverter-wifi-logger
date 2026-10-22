// Aggregator.cpp - see Aggregator.h.
#include "agg/Aggregator.h"

#include <math.h>
#include <string.h>

#include "model/TimeUtil.h"

// ---------------------------------------------------------------------------------------------- ring
bool SampleRing::begin(uint32_t capacity, AllocFn alloc) {
  if (!alloc || capacity == 0) return false;
  buf_ = (RingRec*)alloc(sizeof(RingRec) * capacity);
  if (!buf_) return false;
  cap_ = capacity;
  head_ = count_ = 0;
  return true;
}

void SampleRing::push(const RingRec& r) {
  if (!buf_) return;
  buf_[head_] = r;
  head_ = (head_ + 1) % cap_;
  if (count_ < cap_) count_++;
}

const RingRec& SampleRing::at(uint32_t i) const {
  uint32_t start = (head_ + cap_ - count_) % cap_;
  return buf_[(start + i) % cap_];
}

int SampleRing::downsample(uint32_t nowMs, uint32_t spanS, uint32_t stepS, Bucket* out, int max) const {
  if (stepS == 0 || spanS == 0 || max <= 0) return 0;
  int nb = (int)(spanS / stepS);
  if (nb > max) nb = max;
  for (int i = 0; i < nb; i++) {
    out[i] = Bucket();
    out[i].tRel = -(int32_t)(spanS - (uint32_t)i * stepS);
    out[i].w = out[i].pvw = out[i].bw = out[i].bv = out[i].soc = out[i].gv = 0;
    out[i].n = 0;
    for (int j = 0; j < 6; j++) out[i].c[j] = 0;
  }
  static const int F = 6;
  for (uint32_t k = 0; k < count_; k++) {
    const RingRec& r = at(k);
    uint32_t age = (nowMs - r.upMs) / 1000;
    if (age >= spanS) continue;
    int idx = (int)((spanS - age - 1) / stepS);
    if (idx < 0 || idx >= nb) continue;
    Bucket& b = out[idx];
    float v[F] = {r.w == INT16_MIN ? NAN : (float)r.w, r.pvw == INT16_MIN ? NAN : (float)r.pvw,
                  r.bw == INT16_MIN ? NAN : (float)r.bw, r.bv == 0xFFFF ? NAN : r.bv / 100.f,
                  r.soc == 0xFF ? NAN : (float)r.soc, r.gv == 0xFFFF ? NAN : r.gv / 10.f};
    float* f[F] = {&b.w, &b.pvw, &b.bw, &b.bv, &b.soc, &b.gv};
    b.n++;
    for (int j = 0; j < F; j++)
      if (!isnan(v[j])) {
        b.c[j]++;
        *f[j] += (v[j] - *f[j]) / (float)b.c[j];  // running mean of present values
      }
    if (r.ts) b.ts = r.ts;
  }
  for (int i = 0; i < nb; i++) {
    Bucket& b = out[i];
    float* f[6] = {&b.w, &b.pvw, &b.bw, &b.bv, &b.soc, &b.gv};
    for (int j = 0; j < 6; j++)
      if (b.c[j] == 0) *f[j] = NAN;
  }
  return nb;
}

// ---------------------------------------------------------------------------------------------- aggregator
static const char MODES[] = "PSLBFHDCYE";

bool Aggregator::begin(uint16_t bootId, SampleRing::AllocFn alloc, uint32_t ringCap) {
  bootId_ = bootId;
  open_ = false;
  acc_ = Acc();
  return ring_.begin(ringCap, alloc);
}

double Aggregator::acChargeW(const Sample& s) {
  // Battery charging power not covered by PV, from MEASURED values only. The QPIGS status bits are not used: the
  // Sorotec REVO unit reports b1 "SCC charging" / b0 "AC off" while charging from the grid at 26 A with 0 W PV
  // (owner power-cut test 2026-10-22). Callers apply it only in line mode with the grid present.
  double pch;
  if (!isnan(s.bv) && !isnan(s.bci)) pch = (double)s.bv * s.bci;
  else if (!isnan(s.bw)) pch = s.bw > 0 ? s.bw : 0.0;
  else return 0;
  double p = pch - (!isnan(s.pvw) && s.pvw > 0 ? s.pvw : 0.0);
  return p > 0 ? p : 0;
}

double Aggregator::gridW(const Sample& s) {
  // Grid import from an energy balance of MEASURED values: load + battery (net, + = charging) - PV, only in line mode
  // with the grid present. 0.7.5: PV also feeds the load in line mode on this unit (2026-10-22 06:54: PV 174 W, battery
  // full and idle, load 527 VA, and `gw` showed the whole load). Inverter losses are ignored, so this is an estimate.
  // On a VA-only inverter the load term is VA; the web scales it (web/src/lib/power.ts).
  if (!(gridPresent(s) && s.md == 'L')) return 0;
  if (isnan(s.w)) return NAN;
  double bat = 0;
  if (!isnan(s.bw)) bat = s.bw;
  else if (!isnan(s.bv) && !isnan(s.bci)) bat = (double)s.bv * (s.bci - (isnan(s.bdi) ? 0.f : s.bdi));
  double pv = (!isnan(s.pvw) && s.pvw > 0) ? s.pvw : 0.0;
  double g = s.w + bat - pv;
  return g > 0 ? g : 0;
}

bool Aggregator::gridPresent(const Sample& s) {
  // A measured grid voltage wins over the QPIWS "line fail" bit (a5): the Sorotec REVO firmware keeps a5 set with the
  // grid at 236 V in line mode (first contact 2026-10-22). The bit only decides when no grid voltage is available.
  if (!isnan(s.gv)) return s.gv >= 90.f;
  return !(s.wf[0] && codes::qpiwsBit(s.wf, codes::QPIWS_LINE_FAIL));
}

void Aggregator::openMinute(uint32_t nowMs, uint32_t epoch) {
  acc_ = Acc();
  open_ = true;
  if (tu::epochValid(epoch)) {
    minStartEpoch_ = epoch - (epoch % 60);
    minStartMs_ = nowMs - (epoch % 60) * 1000;
  } else {
    minStartEpoch_ = 0;
    minStartMs_ = nowMs;
  }
}

void Aggregator::addSample(const Sample& s) {
  if (!open_) openMinute(s.upMs, s.ts);
  uint32_t ms = s.upMs;
  if (haveLastSample_ && (uint32_t)(ms - lastSampleMs_) > cfg::ENERGY_MAX_DT_MS) acc_.gap = true;
  haveLastSample_ = true;
  lastSampleMs_ = ms;

  Acc& a = acc_;
  a.n++;
  auto avg = [](double& sum, int& n, float v) { if (!isnan(v)) { sum += v; n++; } };
  auto mx = [](float& m, float v) { if (!isnan(v) && (isnan(m) || v > m)) m = v; };
  auto mn = [](float& m, float v) { if (!isnan(v) && (isnan(m) || v < m)) m = v; };
  avg(a.sw, a.nw, s.w);
  avg(a.spvw, a.npvw, s.pvw);
  avg(a.sbw, a.nbw, s.bw);
  avg(a.sbv, a.nbv, s.bv);
  avg(a.sbi, a.nbi, s.bi);
  avg(a.sgv, a.ngv, s.gv);
  avg(a.sov, a.nov, s.ov);
  avg(a.sld, a.nld, s.ld);
  avg(a.spvv, a.npvv, s.pvv);
  mx(a.wx, s.w);
  mx(a.pvx, s.pvw);
  mn(a.bvn, s.bv);
  mx(a.bvx, s.bv);
  mn(a.gvn, s.gv);
  mx(a.tmpx, s.tmp);
  if (!isnan(s.soc)) a.soc = s.soc;
  if (s.md) {
    const char* p = strchr(MODES, s.md);
    if (p) a.modeCount[p - MODES]++;
  }
  size_t wl = strlen(s.wf);
  size_t lim = wl >= 36 ? 31 : wl;
  for (size_t i = 0; i < lim && i < 64; i++)
    if (s.wf[i] == '1') a.warnBits |= (1ULL << i);
  if (s.fc != INT16_MIN && s.fc > 0) a.fc = (uint8_t)(s.fc > 255 ? 255 : s.fc);
  if (!gridPresent(s)) a.noGrid++;
  if (!isnan(s.epvi)) lastEpvi_ = s.epvi;

  // energy (trapezoid on consecutive valid samples, d1 §4.4)
  a.epv += iPv_.add(ms, s.pvw);
  a.eld += iLd_.add(ms, s.w);
  float bw = s.bw;
  a.ech += iCh_.add(ms, isnan(bw) ? NAN : (bw > 0 ? bw : 0.f));
  a.edis += iDis_.add(ms, isnan(bw) ? NAN : (bw < 0 ? -bw : 0.f));
  float pg = (float)gridW(s);
  a.egr += iGr_.add(ms, pg);

  RingRec r;
  r.ts = s.ts;
  r.upMs = s.upMs;
  r.w = isnan(s.w) ? INT16_MIN : (int16_t)lroundf(s.w);
  r.pvw = isnan(s.pvw) ? INT16_MIN : (int16_t)lroundf(s.pvw);
  r.bw = isnan(s.bw) ? INT16_MIN : (int16_t)lroundf(s.bw);
  r.bv = isnan(s.bv) ? 0xFFFF : (uint16_t)lroundf(s.bv * 100.f);
  r.gv = isnan(s.gv) ? 0xFFFF : (uint16_t)lroundf(s.gv * 10.f);
  r.soc = isnan(s.soc) ? 0xFF : (uint8_t)lroundf(s.soc);
  r.ld = isnan(s.ld) ? 0xFF : (uint8_t)lroundf(s.ld > 250 ? 250 : s.ld);
  r.md = s.md;
  r.flags = 0;
  ring_.push(r);
}

static int16_t encI(double v) {
  if (isnan(v)) return mrec::I16_NA;
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return (int16_t)lround(v);
}
static uint16_t encU(double v, double scale) {
  if (isnan(v)) return mrec::U16_NA;
  double x = v * scale;
  if (x < 0) x = 0;
  if (x > 65534) x = 65534;
  return (uint16_t)lround(x);
}
static uint16_t encE(double wh) {
  double x = wh * 10.0;
  if (x < 0) x = 0;
  if (x > 65535) x = 65535;
  return (uint16_t)lround(x);
}

MinuteRec Aggregator::closeMinute() {
  const Acc& a = acc_;
  MinuteRec r = mrec::empty();
  r.ts = minStartEpoch_;
  r.bootId = bootId_;
  r.upMs = minStartMs_;
  r.n = (uint8_t)(a.n > 255 ? 255 : a.n);
  r.flags = 0;
  if (!minStartEpoch_) r.flags |= mrec::F_PROVISIONAL;
  if (a.gap || a.n == 0) r.flags |= mrec::F_LINK_GAP;
  if (a.n > 0 && a.noGrid * 2 > a.n) r.flags |= mrec::F_NO_GRID;
  auto mean = [](double s, int n) { return n ? s / n : NAN; };
  r.w = encI(mean(a.sw, a.nw));
  r.wx = encI(a.wx);
  r.pvw = encI(mean(a.spvw, a.npvw));
  r.pvx = encI(a.pvx);
  r.bw = encI(mean(a.sbw, a.nbw));
  r.bv = encU(mean(a.sbv, a.nbv), 100);
  r.bvn = encU(a.bvn, 100);
  r.bvx = encU(a.bvx, 100);
  double bi = mean(a.sbi, a.nbi);
  r.bi = isnan(bi) ? mrec::I16_NA : encI(bi * 10.0);
  r.gv = encU(mean(a.sgv, a.ngv), 10);
  r.gvn = encU(a.gvn, 10);
  r.ov = encU(mean(a.sov, a.nov), 10);
  r.pvv = encU(mean(a.spvv, a.npvv), 10);
  r.soc = isnan(a.soc) ? mrec::U8_NA : (uint8_t)lroundf(a.soc);
  double ld = mean(a.sld, a.nld);
  r.ld = isnan(ld) ? mrec::U8_NA : (uint8_t)lround(ld > 250 ? 250 : ld);
  r.tmp = isnan(a.tmpx) ? mrec::I8_NA : (int8_t)(a.tmpx > 127 ? 127 : (a.tmpx < -127 ? -127 : lroundf(a.tmpx)));
  int best = -1, bestN = 0;
  for (int i = 0; i < 10; i++)
    if (a.modeCount[i] > bestN) { bestN = a.modeCount[i]; best = i; }
  r.md = best >= 0 ? MODES[best] : 0;
  r.wn = (uint8_t)__builtin_popcountll(a.warnBits);
  r.fc = a.fc;
  r.epv = encE(a.epv);
  r.eld = encE(a.eld);
  r.ech = encE(a.ech);
  r.edis = encE(a.edis);
  r.egr = encE(a.egr);
  mrec::seal(r);
  return r;
}

bool Aggregator::tick(uint32_t nowMs, uint32_t epoch, MinuteRec& rec) {
  if (!open_) {
    openMinute(nowMs, epoch);
    return false;
  }
  bool close = false;
  bool valid = tu::epochValid(epoch);
  if (minStartEpoch_ && valid) close = (epoch - (epoch % 60)) > minStartEpoch_;
  else close = (uint32_t)(nowMs - minStartMs_) >= cfg::MINUTE_MS;
  if (!close) return false;
  rec = closeMinute();

  // daily totals (source of truth on reboot = the day's file; this is the running copy)
  if (rec.ts) {
    int32_t day = tu::localDayNumber(rec.ts);
    if (daily_.day == 0) mrec::dailyReset(daily_, day);
    if (day != daily_.day) {
      finished_ = daily_;
      dayRolled_ = true;
      mrec::dailyReset(daily_, day);
      lastEpvi_ = NAN;
    }
  }
  if (daily_.day != 0) {
    mrec::dailyAdd(daily_, rec);
    if (!isnan(lastEpvi_)) daily_.epvi = lastEpvi_;
  }
  // 15-minute aggregate
  if (rec.ts) {
    uint32_t q = rec.ts - (rec.ts % 900);
    if (h15Open_ && h15_.ts != q) {
      h15Done_ = h15_;
      h15Ready_ = true;
      h15Open_ = false;
    }
    if (!h15Open_) {
      mrec::h15Begin(h15_, q);
      h15Open_ = true;
    }
    mrec::h15Add(h15_, rec);
    if (((rec.ts / 60) % 15) == 14) {
      h15Done_ = h15_;
      h15Ready_ = true;
      h15Open_ = false;
    }
  }
  openMinute(nowMs, epoch);
  return true;
}

void Aggregator::todayEnergy(float& epv, float& eld, float& ech, float& edis, float& egr) const {
  epv = (float)(daily_.epv + acc_.epv);
  eld = (float)(daily_.eld + acc_.eld);
  ech = (float)(daily_.ech + acc_.ech);
  edis = (float)(daily_.edis + acc_.edis);
  egr = (float)(daily_.egr + acc_.egr);
}

bool Aggregator::takeDayRolled(mrec::Daily& finished) {
  if (!dayRolled_) return false;
  dayRolled_ = false;
  finished = finished_;
  return true;
}

bool Aggregator::takeH15(mrec::H15& out) {
  if (!h15Ready_) return false;
  h15Ready_ = false;
  out = h15Done_;
  return true;
}
