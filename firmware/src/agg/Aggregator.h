// Aggregator.h - energy integration, 1-minute records, h15/daily folding and the 24 h PSRAM ring (d1 §4.2-4.4).
// Host-portable: memory for the ring comes from an injected allocator (ps_malloc on the device).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "model/Sample.h"
#include "store/MinuteRec.h"

// Compact 5 s sample for local charts (≈24 B; 17,280 of them ≈ 0.4 MB of PSRAM).
struct RingRec {
  uint32_t ts;      // epoch (0 if unknown)
  uint32_t upMs;
  int16_t w, pvw, bw;
  uint16_t bv;      // cV
  uint16_t gv;      // dV
  uint8_t soc, ld;  // 0xFF = missing
  char md;
  uint8_t flags;
};

class SampleRing {
 public:
  typedef void* (*AllocFn)(size_t);
  bool begin(uint32_t capacity, AllocFn alloc);
  void push(const RingRec& r);
  uint32_t size() const { return count_; }
  uint32_t capacity() const { return cap_; }
  const RingRec& at(uint32_t i) const;  // 0 = oldest
  bool ok() const { return buf_ != nullptr; }

  struct Bucket { int32_t tRel; uint32_t ts; float w, pvw, bw, bv, soc, gv; uint16_t n; uint16_t c[6]; };
  // Average the samples of the last spanS seconds into stepS-second buckets (oldest first). Returns count.
  int downsample(uint32_t nowMs, uint32_t spanS, uint32_t stepS, Bucket* out, int max) const;

 private:
  RingRec* buf_ = nullptr;
  uint32_t cap_ = 0, head_ = 0, count_ = 0;
};

// Trapezoid integration of one power channel: E += (P_prev + P_now)/2 * dt, only for 0 < dt <= 30 s.
struct EnergyIntegrator {
  bool havePrev = false;
  uint32_t prevMs = 0;
  float prevP = 0;
  // returns Wh added
  double add(uint32_t ms, float p) {
    double e = 0;
    if (isnan(p)) { havePrev = false; return 0; }
    if (havePrev) {
      uint32_t dt = ms - prevMs;
      if (dt > 0 && dt <= cfg::ENERGY_MAX_DT_MS) e = ((double)prevP + (double)p) / 2.0 * (double)dt / 3600000.0;
    }
    havePrev = true;
    prevMs = ms;
    prevP = p;
    return e;
  }
};

class Aggregator {
 public:
  bool begin(uint16_t bootId, SampleRing::AllocFn alloc, uint32_t ringCap = cfg::RING_SAMPLES);

  // Feed one VALID sample (lk = 1). Energy uses s.upMs deltas.
  void addSample(const Sample& s);
  // Call at >= 1 Hz. When a minute closes, fills rec and returns true (also when n == 0: link gap).
  bool tick(uint32_t nowMs, uint32_t epoch, MinuteRec& rec);

  // Wh today including the open minute (for `live`)
  void todayEnergy(float& epv, float& eld, float& ech, float& edis, float& egr) const;
  const mrec::Daily& daily() const { return daily_; }
  void setDaily(const mrec::Daily& d) { daily_ = d; }   // boot recompute from today's file
  void noteEvent(bool fault) { if (fault) daily_.nf++; else daily_.nw++; }
  bool takeDayRolled(mrec::Daily& finished);            // true once after local midnight
  bool takeH15(mrec::H15& out);                          // true once when a quarter closed
  SampleRing& ring() { return ring_; }
  const SampleRing& ring() const { return ring_; }
  uint16_t bootId() const { return bootId_; }

  // exposed for tests
  static double acChargeW(const Sample& s);
  static double gridW(const Sample& s);  // grid import estimate (gw, egr, rule GW); 0 off line mode, NAN if no load
  static bool gridPresent(const Sample& s);

 private:
  struct Acc {
    int n = 0;
    double sw = 0, spvw = 0, sbw = 0, sbv = 0, sbi = 0, sgv = 0, sov = 0, sld = 0, spvv = 0;
    int nw = 0, npvw = 0, nbw = 0, nbv = 0, nbi = 0, ngv = 0, nov = 0, nld = 0, npvv = 0;
    float wx = NAN, pvx = NAN, bvn = NAN, bvx = NAN, gvn = NAN, tmpx = NAN, soc = NAN;
    int modeCount[10] = {};
    uint64_t warnBits = 0;
    uint8_t fc = 0;
    int noGrid = 0;
    double epv = 0, eld = 0, ech = 0, edis = 0, egr = 0;
    bool gap = false;
  };
  void openMinute(uint32_t nowMs, uint32_t epoch);
  MinuteRec closeMinute();

  uint16_t bootId_ = 0;
  SampleRing ring_;
  Acc acc_;
  bool open_ = false;
  uint32_t minStartMs_ = 0, minStartEpoch_ = 0;
  uint32_t lastSampleMs_ = 0;
  bool haveLastSample_ = false;
  EnergyIntegrator iPv_, iLd_, iCh_, iDis_, iGr_;
  mrec::Daily daily_;
  mrec::Daily finished_;
  bool dayRolled_ = false;
  mrec::H15 h15_, h15Done_;
  bool h15Open_ = false, h15Ready_ = false;
  float lastEpvi_ = NAN;
};
