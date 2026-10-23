// test_agg - energy integration (d1 §4.4 acceptance: 1000 W for 3600 s at 5 s -> 1000 Wh +-0.5 %),
// minute / h15 / daily aggregation, MinuteRec format, local time keys and the alert engine (d1 §7).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "agg/AlertEngine.h"
#include "agg/Aggregator.h"
#include "agg/RuleEngine.h"
#include "model/LiveJson.h"
#include "model/TimeUtil.h"
#include "store/MinuteRec.h"

void setUp() {}
void tearDown() {}

static void* hostAlloc(size_t n) { return calloc(1, n); }

static const uint32_t T_1435 = 1792571700;  // 2026-10-21 14:35:00 +06
static const uint32_t T_2359 = 1792605540;  // 2026-10-21 23:59:00 +06
static const uint32_t T_2100 = 1792594805;  // 2026-10-21 21:00:05 +06
static const uint32_t T_2300 = 1792602000;  // 2026-10-21 23:00:00 +06
static const uint32_t T_1200 = 1792562400;  // 2026-10-21 12:00:00 +06

static Sample mkSample(uint32_t upMs, uint32_t ts, float w, float pvw) {
  Sample s;
  s.upMs = upMs;
  s.ts = ts;
  s.lk = 1;
  s.gv = 231;
  s.gf = 50;
  s.ov = 230;
  s.of = 50;
  s.w = w;
  s.ld = 16;
  s.bv = 26.4f;
  s.bci = 0;
  s.bdi = 0;
  s.soc = 78;
  s.pvv = 145;
  s.pvw = pvw;
  s.md = 'L';
  snprintf(s.st, sizeof s.st, "00010000");
  s.finalizeDerived();
  return s;
}

void test_time_keys() {
  char b[16];
  tu::dateKey(T_1435, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("2026-10-21", b);
  tu::hhmmKey(T_1435, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("1435", b);
  tu::dateKey(T_2359 + 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("2026-10-22", b);
  tu::hhmmKey(T_2359 + 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("0000", b);
  tu::yyyymmdd(T_1435, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("20261021", b);
  int32_t day;
  TEST_ASSERT_TRUE(tu::parseDateKey("2026-10-21", day));
  TEST_ASSERT_EQUAL_UINT32(1792519200, tu::epochOfLocalMidnight(day));  // 2026-10-21 00:00 +06
  TEST_ASSERT_EQUAL_UINT32(T_1435, tu::parseQt("20261021143500"));
  TEST_ASSERT_EQUAL_UINT32(0, tu::parseQt("2026102114350"));
  TEST_ASSERT_FALSE(tu::epochValid(1735689600));  // 2025
  TEST_ASSERT_TRUE(tu::epochValid(T_1435));
}

void test_minuterec_format() {
  TEST_ASSERT_EQUAL(55, (int)sizeof(MinuteRec));
  MinuteRec r = mrec::empty();
  r.ts = T_1435;
  r.n = 12;
  r.w = 498;
  r.wx = 620;
  r.pvw = 461;
  r.pvx = 480;
  r.bv = 2640;
  r.bvn = 2631;
  r.bvx = 2648;
  r.bi = 116;
  r.soc = 78;
  r.gv = 2312;
  r.gvn = 2298;
  r.ov = 2300;
  r.ld = 16;
  r.tmp = 38;
  r.pvv = 1449;
  r.md = 'L';
  r.epv = 77;
  r.eld = 83;
  r.ech = 51;
  r.edis = 0;
  r.egr = 83;
  mrec::seal(r);
  TEST_ASSERT_TRUE(mrec::valid(r));
  char j[400];
  TEST_ASSERT_TRUE(mrec::toHistJson(r, j, sizeof j) > 0);
  // exactly the d1 §4.2 hist example
  TEST_ASSERT_EQUAL_STRING(
      "{\"w\":498,\"wx\":620,\"pvw\":461,\"pvx\":480,\"bv\":26.4,\"bvn\":26.31,\"bvx\":26.48,\"bi\":11.6,\"soc\":78,"
      "\"gv\":231.2,\"gvn\":229.8,\"ov\":230,\"ld\":16,\"tmp\":38,\"pvv\":144.9,\"md\":\"L\",\"wn\":0,\"fc\":0,\"n\":12,"
      "\"epv\":7.7,\"eld\":8.3,\"ech\":5.1,\"edis\":0,\"egr\":8.3}",
      j);
  r.flags |= mrec::F_PROVISIONAL;
  mrec::toHistJson(r, j, sizeof j);
  TEST_ASSERT_NOT_NULL(strstr(j, "\"tq\":0"));
  r.n ^= 1;  // corrupt
  TEST_ASSERT_FALSE(mrec::valid(r));
}

void test_energy_integration_1000w_1h() {
  Aggregator a;
  TEST_ASSERT_TRUE(a.begin(1, hostAlloc, 2000));
  uint32_t ms = 100000, ts = T_1200;
  double epvMinutes = 0;
  MinuteRec rec;
  a.tick(ms, ts, rec);  // opens the first minute
  for (int i = 0; i <= 720; i++) {  // 0..3600 s at 5 s
    Sample s = mkSample(ms, ts, 1000, 1000);
    a.addSample(s);
    for (int k = 0; k < 5; k++) {
      ms += 1000;
      ts += 1;
      if (a.tick(ms, ts, rec)) epvMinutes += rec.epv * 0.1;
    }
  }
  float epv, eld, ech, edis, egr;
  a.todayEnergy(epv, eld, ech, edis, egr);
  TEST_ASSERT_FLOAT_WITHIN(5.0, 1000.0, epv);  // +-0.5 %
  TEST_ASSERT_FLOAT_WITHIN(5.0, 1000.0, eld);
  TEST_ASSERT_FLOAT_WITHIN(5.0, 1000.0, epvMinutes);
  TEST_ASSERT_FLOAT_WITHIN(5.0, 1000.0, a.daily().epv);
  // grid estimate (0.7.5 energy balance): 1000 W of PV covers the 1000 W load in line mode -> ~0 Wh from the grid
  // (egr integration accuracy: test_gw_and_nogrid_share_the_grid_predicate)
  TEST_ASSERT_FLOAT_WITHIN(5.0, 0.0, egr);
}

void test_energy_gap_not_interpolated() {
  EnergyIntegrator e;
  TEST_ASSERT_EQUAL_DOUBLE(0, e.add(0, 1000));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1000.0 * 5 / 3600, e.add(5000, 1000));
  TEST_ASSERT_EQUAL_DOUBLE(0, e.add(5000 + 31000, 1000));  // 31 s gap -> missing data
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1000.0 * 30 / 3600, e.add(5000 + 31000 + 30000, 1000));  // exactly 30 s ok
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, (1000.0 + 0.0) / 2 * 10 / 3600, e.add(76000, 0));  // trapezoid
  TEST_ASSERT_EQUAL_DOUBLE(0, e.add(80000, NAN));
  TEST_ASSERT_EQUAL_DOUBLE(0, e.add(85000, 500));  // NaN broke the chain
}

void test_minute_aggregation_and_alignment() {
  Aggregator a;
  a.begin(7, hostAlloc, 100);
  uint32_t ms = 50000, ts = T_1435 - 30;  // 14:34:30
  MinuteRec rec;
  a.tick(ms, ts, rec);
  bool closed = false;
  // first (partial) minute closes at 14:35:00 exactly
  for (int i = 0; i < 30 && !closed; i++) {
    ms += 1000;
    ts += 1;
    closed = a.tick(ms, ts, rec);
  }
  TEST_ASSERT_TRUE(closed);
  TEST_ASSERT_EQUAL_UINT32(T_1435 - 60, rec.ts);
  TEST_ASSERT_EQUAL(0, rec.n);
  TEST_ASSERT_TRUE(rec.flags & mrec::F_LINK_GAP);
  // a full minute 14:35 with 12 samples, loads 400..620, one battery-mode sample
  closed = false;
  for (int i = 0; i < 12; i++) {
    Sample s = mkSample(ms, ts, 400 + 20 * i, 460);
    s.bv = 26.30f + 0.01f * i;
    if (i == 5) s.md = 'B';
    a.addSample(s);
    for (int k = 0; k < 5; k++) {
      ms += 1000;
      ts += 1;
      if (a.tick(ms, ts, rec)) closed = true;
    }
  }
  TEST_ASSERT_TRUE(closed);
  TEST_ASSERT_EQUAL_UINT32(T_1435, rec.ts);
  TEST_ASSERT_EQUAL(12, rec.n);
  TEST_ASSERT_EQUAL(510, rec.w);   // mean of 400..620
  TEST_ASSERT_EQUAL(620, rec.wx);
  TEST_ASSERT_EQUAL(460, rec.pvw);
  TEST_ASSERT_EQUAL(2630, rec.bvn);
  TEST_ASSERT_EQUAL(2641, rec.bvx);
  TEST_ASSERT_EQUAL('L', rec.md);
  TEST_ASSERT_EQUAL(78, rec.soc);
  TEST_ASSERT_EQUAL(7, rec.bootId);
  TEST_ASSERT_TRUE(mrec::valid(rec));
  TEST_ASSERT_FALSE(rec.flags & mrec::F_PROVISIONAL);
  char hh[8];
  tu::hhmmKey(rec.ts, hh, sizeof hh);
  TEST_ASSERT_EQUAL_STRING("1435", hh);
  // ring holds the samples
  TEST_ASSERT_EQUAL(12, (int)a.ring().size());
  SampleRing::Bucket b[10];
  int nb = a.ring().downsample(ms, 600, 60, b, 10);
  TEST_ASSERT_EQUAL(10, nb);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 520, b[9].w);  // 11 newest samples (420..620)
  TEST_ASSERT_FLOAT_WITHIN(0.5, 400, b[8].w);  // the 60 s old one
  TEST_ASSERT_TRUE(isnan(b[0].w));  // no data 10 min ago -> gap
}

void test_provisional_minutes_and_fixup() {
  Aggregator a;
  a.begin(3, hostAlloc, 100);
  uint32_t ms = 10000;
  MinuteRec rec;
  a.tick(ms, 0, rec);
  bool closed = false;
  for (int i = 0; i < 12; i++) {
    a.addSample(mkSample(ms, 0, 300, 0));
    for (int k = 0; k < 5; k++) {
      ms += 1000;
      if (a.tick(ms, 0, rec)) closed = true;
    }
  }
  TEST_ASSERT_TRUE(closed);
  TEST_ASSERT_EQUAL_UINT32(0, rec.ts);
  TEST_ASSERT_TRUE(rec.flags & mrec::F_PROVISIONAL);
  TEST_ASSERT_EQUAL_UINT32(10000, rec.upMs);
  // NTP arrives at upMs 130000 = epoch T_1435 -> the record started 120 s earlier
  TEST_ASSERT_TRUE(mrec::fixupTs(rec, 3, 130000, T_1435));
  TEST_ASSERT_EQUAL_UINT32(T_1435 - 120, rec.ts);
  TEST_ASSERT_FALSE(rec.flags & mrec::F_PROVISIONAL);
  TEST_ASSERT_TRUE(mrec::valid(rec));
  MinuteRec other = rec;
  other.flags |= mrec::F_PROVISIONAL;
  other.bootId = 2;  // a different boot cannot be fixed with this anchor
  TEST_ASSERT_FALSE(mrec::fixupTs(other, 3, 130000, T_1435));
}

void test_h15_and_daily_rollover() {
  Aggregator a;
  a.begin(1, hostAlloc, 3000);
  uint32_t ms = 1000, ts = T_2359 - 60 * 29;  // 23:30:00
  MinuteRec rec;
  a.tick(ms, ts, rec);
  int minutes = 0, h15s = 0;
  mrec::H15 h;
  mrec::Daily fin;
  bool rolled = false;
  for (int i = 0; i < 12 * 31; i++) {  // 31 minutes: 23:30 .. 00:00
    a.addSample(mkSample(ms, ts, 600, 300));
    for (int k = 0; k < 5; k++) {
      ms += 1000;
      ts += 1;
      if (a.tick(ms, ts, rec)) minutes++;
      if (a.takeH15(h)) {
        h15s++;
        TEST_ASSERT_EQUAL(0, (int)(h.ts % 900));
        if (h15s == 1) {
          TEST_ASSERT_EQUAL(15, h.count);
          TEST_ASSERT_EQUAL(180, h.agg.n);
          TEST_ASSERT_EQUAL(600, h.agg.w);
          char hh[8];
          tu::hhmmKey(h.ts, hh, sizeof hh);
          TEST_ASSERT_EQUAL_STRING("2330", hh);
          TEST_ASSERT_FLOAT_WITHIN(1.5, 75.0, h.agg.epv * 0.1);  // PV 300 W x 0.25 h
        }
      }
      if (a.takeDayRolled(fin)) rolled = true;
    }
  }
  TEST_ASSERT_TRUE(minutes >= 30);
  TEST_ASSERT_EQUAL(2, h15s);  // 23:30 and 23:45 quarters
  TEST_ASSERT_TRUE(rolled);
  TEST_ASSERT_EQUAL(30, fin.n);
  TEST_ASSERT_FLOAT_WITHIN(2.0, 300.0, fin.eld);  // 600 W for 30 min
  char j[300];
  TEST_ASSERT_TRUE(mrec::toDailyJson(fin, j, sizeof j) > 0);
  TEST_ASSERT_NOT_NULL(strstr(j, "\"n\":30"));
  TEST_ASSERT_NOT_NULL(strstr(j, "\"gout\":0"));
}

// Field case (2026-10-22 06:54, line mode): PV 174 W, battery full and idle, load 527 VA -> the grid supplies 353,
// not the whole load. Grid charging adds, PV subtracts, never below 0, and 0 off line mode.
void test_grid_power_energy_balance() {
  Sample s = mkSample(0, 0, 527, 174);
  s.bv = 26.5f;
  s.bci = 0;
  s.bdi = 0;
  s.finalizeDerived();
  TEST_ASSERT_DOUBLE_WITHIN(1, 527 - 174, Aggregator::gridW(s));
  s.pvw = 0;
  s.bci = 26;  // grid charging at night
  s.finalizeDerived();
  TEST_ASSERT_DOUBLE_WITHIN(1, 527 + 26.5 * 26, Aggregator::gridW(s));
  s.pvw = 900;  // PV covers load and charging -> nothing from the grid
  s.bci = 10;
  s.finalizeDerived();
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0, Aggregator::gridW(s));
  s.md = 'B';
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0, Aggregator::gridW(s));
}

void test_ac_charge_and_grid_present() {
  Sample s = mkSample(0, 0, 500, 200);
  snprintf(s.st, sizeof s.st, "00010101");  // b2 charging, b0 AC charging
  s.bci = 20;
  // measured values only: charge power minus PV, whatever the status bits say
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 26.4 * 20 - 200, Aggregator::acChargeW(s));
  snprintf(s.st, sizeof s.st, "00010111");
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 26.4 * 20 - 200, Aggregator::acChargeW(s));
  // REVO after a power cut: b1 "SCC charging", b0 clear, 0 W PV, 26 A into the battery -> all from the grid
  Sample r = mkSample(0, 0, 216, 0);
  snprintf(r.st, sizeof r.st, "00010110");
  r.bv = 26.7f;
  r.bci = 26;
  TEST_ASSERT_DOUBLE_WITHIN(0.5, 26.7 * 26, Aggregator::acChargeW(r));
  r.pvw = 900;  // PV covers the charging -> no AC charge
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0, Aggregator::acChargeW(r));
  TEST_ASSERT_TRUE(Aggregator::gridPresent(s));
  s.gv = 0;
  TEST_ASSERT_FALSE(Aggregator::gridPresent(s));
  s.gv = 231;
  snprintf(s.wf, sizeof s.wf, "00000100000000000000000000000000");  // a5 LINE_FAIL
  TEST_ASSERT_TRUE(Aggregator::gridPresent(s));   // measured 231 V wins (REVO keeps a5 set on grid)
  s.gv = NAN;
  TEST_ASSERT_FALSE(Aggregator::gridPresent(s));  // no voltage reading: the bit decides
}


// ---------------------------------------------------------------------------------------------- alerts
static int countType(AlertQueue& q, const char* type, std::vector<AlertEvent>* out = nullptr) {
  int n = 0;
  AlertEvent e;
  std::vector<AlertEvent> keep;
  while (q.pop(e)) {
    if (!strcmp(e.type, type)) n++;
    keep.push_back(e);
  }
  if (out) *out = keep;
  return n;
}

// Real REVO behaviour (first contact 2026-10-22): line mode, grid 236 V, QPIWS a5 set -> no GRID_LOSS
void test_alert_revo_line_fail_bit_with_grid_present() {
  AlertQueue q;
  AlertEngine eng(q);
  uint32_t ms = 1000, ts = T_1200;
  for (int i = 0; i < 600; i += 5) {
    Sample s = mkSample(ms, ts, 354, 0);
    s.gv = 236;
    s.md = 'L';
    snprintf(s.wf, sizeof s.wf, "00000100000000000000000000000000");
    eng.evaluate(s, ms, ts);
    ms += 5000;
    ts += 5;
  }
  TEST_ASSERT_EQUAL(0, countType(q, "GRID_LOSS"));
}

void test_alert_grid_loss_and_restore() {
  AlertQueue q;
  AlertEngine eng(q);
  uint32_t ms = 1000, ts = T_1200;
  auto run = [&](float gv, int seconds, char md = 'L') {
    for (int i = 0; i < seconds; i += 5) {
      Sample s = mkSample(ms, ts, 300, 0);
      s.gv = gv;
      s.md = md;
      eng.evaluate(s, ms, ts);
      ms += 5000;
      ts += 5;
    }
  };
  run(231, 60, 'B');  // SBU: battery mode with grid present -> no GRID_LOSS (d1 §7.2 note)
  TEST_ASSERT_EQUAL(0, q.size());
  run(0, 5);
  TEST_ASSERT_EQUAL(0, q.size());  // < 10 s hold
  run(0, 600);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(q, "GRID_LOSS", &ev));
  TEST_ASSERT_EQUAL(4, ev[0].sev);
  TEST_ASSERT_EQUAL_STRING("open", ev[0].st);
  TEST_ASSERT_EQUAL_STRING("Grid lost, on battery (SOC 78%)", ev[0].msg);
  TEST_ASSERT_EQUAL(4, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].telegram);
  run(231, 20);
  TEST_ASSERT_EQUAL(0, q.size());  // < 30 s present
  run(231, 60);
  TEST_ASSERT_EQUAL(1, countType(q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].sev);
  TEST_ASSERT_TRUE(ev[0].telegram);  // 0.7.7: every pushed alert goes to Telegram (was sev >= 4 only)
  TEST_ASSERT_EQUAL_STRING("clear", ev[0].st);
  TEST_ASSERT_TRUE(ev[0].dur >= 600 && ev[0].dur <= 650);
  TEST_ASSERT_NOT_NULL(strstr(ev[0].msg, "Grid restored after 10m"));
  run(231, 300);
  TEST_ASSERT_EQUAL(0, q.size());  // no duplicates
}


// ---------------------------------------------------------------------------------------------- 0.7.9 grid-off
// Owner's daily scheduled grid cut 08:00-15:30 (2026-10-22): the cut and its end are labelled "planned" (log-only in
// 0.7.9-0.8.0, pushed once each since 0.8.1); the grid failing after the owner turned it back on inside the window, or
// still off after the window, are real alerts; no "Still active".
struct OutageStore : IGateStore {
  uint32_t outage = 0;
  void saveJobs(int32_t, uint16_t) override {}
  void saveOutage(uint32_t e) override { outage = e; }
};
static const uint32_t T_0800 = T_1200 - 4 * 3600;

struct GridRig {
  AlertQueue q;
  NotifyGates g;
  AlertEngine eng{q, &g};
  OutageStore store;
  uint32_t ms = 200000, ts = T_0800;
  GridRig(bool window = true, bool quietHours = false) {
    g.setStore(&store);
    AlertCfg c;
    c.quietFrom = quietHours ? 22 : 0;  // most tests run without quiet hours (the default is 22-7)
    c.quietTo = quietHours ? 7 : 0;
    if (window) {
      c.gridOffFrom = 8 * 60;
      c.gridOffTo = 15 * 60 + 30;
    }
    eng.setCfg(c);
  }
  void run(float gv, uint32_t seconds) {
    for (uint32_t i = 0; i < seconds; i += 5) {
      Sample s = mkSample(ms, ts, 300, 0);
      s.gv = gv;
      s.md = gv > 90 ? 'L' : 'B';
      eng.evaluate(s, ms, ts);
      ms += 5000;
      ts += 5;
    }
  }
};

// 0.8.1 (owner request 2026-10-23): one alert when the planned cut starts, one when it ends (was log-only)
void test_planned_grid_off_alerts_start_and_end_once() {
  GridRig r;
  rules::EmailPolicy pol;
  pol.on = true;
  r.eng.setEmailPolicy(pol);
  r.run(0, 7 * 3600 + 25 * 60);  // 08:00 -> 15:25
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_LOSS", &ev));  // one: no "Still active", no overrun alert
  TEST_ASSERT_EQUAL(4, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].telegram);
  TEST_ASSERT_TRUE(ev[0].planned);
  TEST_ASSERT_EQUAL_STRING("q", ev[0].em);
  TEST_ASSERT_NOT_NULL(strstr(ev[0].msg, "(planned grid-off)"));
  r.run(231, 120);  // 15:25 the owner switches the grid back on
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].telegram);
  TEST_ASSERT_TRUE(ev[0].planned);
  TEST_ASSERT_EQUAL_STRING("q", ev[0].em);
  TEST_ASSERT_NOT_NULL(strstr(ev[0].msg, "Grid restored after 7h 25m (planned grid-off)"));
}

void test_planned_grid_off_manual_on_then_fail_alerts() {
  GridRig r;
  r.run(0, 600);  // 08:00 the scheduled cut
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_LOSS", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);  // 0.8.1: pushed (log-only before)
  TEST_ASSERT_TRUE(ev[0].planned);
  TEST_ASSERT_TRUE(r.store.outage > 0);  // 0.8.1: persisted, so a reboot does not repeat it
  r.run(0, 2 * 3600);                    // until 10:10: no "Still active"
  TEST_ASSERT_EQUAL(0, r.q.size());
  r.run(231, 120);  // the owner turns the grid on inside the window: the planned cut's end
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(ev[0].planned);
  r.run(0, 600);  // ~10:12 the grid really fails: a real alert
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_LOSS", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(ev[0].telegram);
  TEST_ASSERT_FALSE(ev[0].planned);
  TEST_ASSERT_NULL(strstr(ev[0].msg, "planned"));
  TEST_ASSERT_TRUE(r.store.outage > 0);
  r.run(231, 120);
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_EQUAL(0u, r.store.outage);
}

void test_planned_grid_off_overrun_alerts_once() {
  GridRig r;
  r.run(0, 7 * 3600 + 45 * 60);  // 08:00 -> 15:45, still off
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(2, countType(r.q, "GRID_LOSS", &ev));  // the planned cut + one real alert after 15:40
  std::vector<AlertEvent> gl;
  for (const AlertEvent& e : ev)
    if (!strcmp(e.type, "GRID_LOSS")) gl.push_back(e);
  TEST_ASSERT_TRUE(gl[0].pushPrio > 0);  // 0.8.1: the planned start is pushed too
  TEST_ASSERT_TRUE(gl[0].planned);
  TEST_ASSERT_TRUE(gl[1].pushPrio > 0);
  TEST_ASSERT_FALSE(gl[1].planned);
  TEST_ASSERT_NOT_NULL(strstr(gl[1].msg, "still off after the planned grid-off (08:00-15:30)"));
  r.run(231, 120);
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_FALSE(ev[0].planned);  // an overrun ends as a real outage
  TEST_ASSERT_TRUE(ev[0].dur > 7 * 3600);
}

void test_real_outage_no_repeats_and_boot_inside_window() {
  GridRig r;
  r.ts = T_1200 + 6 * 3600;  // 18:00, outside the window
  r.run(0, 3 * 3600);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_LOSS", &ev));  // no "Still active" every 30 min
  TEST_ASSERT_EQUAL(0, r.q.size());
  // a fresh boot at 11:00 with the grid off (inside the window): assumed to be the planned cut, whose start the logger
  // did not see: log only. 0.8.1: its end is still pushed.
  GridRig b;
  b.ms = 1000;
  b.ts = T_1200 - 3600;
  b.run(0, 300);
  TEST_ASSERT_EQUAL(1, countType(b.q, "GRID_LOSS", &ev));
  TEST_ASSERT_EQUAL(0, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].planned);
  TEST_ASSERT_EQUAL(0u, b.store.outage);
  b.run(231, 120);
  TEST_ASSERT_EQUAL(1, countType(b.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(ev[0].planned);
}

// 0.8.1: a reboot during the planned cut (e.g. a firmware update) resumes it silently; its end is still pushed
void test_planned_grid_off_resumes_after_reboot() {
  GridRig a;
  a.run(0, 600);  // 08:00 the scheduled cut, pushed and persisted
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(a.q, "GRID_LOSS", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(a.store.outage > 0);
  GridRig b;  // the logger restarts at 08:11, grid still off
  b.ms = 1000;
  b.ts = a.ts + 60;
  b.eng.resumeOutage(a.store.outage);
  b.run(0, 3 * 3600);
  TEST_ASSERT_EQUAL(0, countType(b.q, "GRID_LOSS"));  // no second "grid lost"
  b.run(231, 120);
  TEST_ASSERT_EQUAL(1, countType(b.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(ev[0].planned);
  TEST_ASSERT_TRUE(ev[0].dur > 3 * 3600);
}

void test_outage_resumes_after_reboot_without_duplicate() {
  GridRig a(false);
  a.ts = T_1200 + 6 * 3600;  // 18:00 real outage
  a.run(0, 600);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(a.q, "GRID_LOSS", &ev));
  uint32_t opened = a.store.outage;
  TEST_ASSERT_TRUE(opened > 0);
  GridRig b(false);  // the logger reboots 10 min later, grid still off
  b.ms = 1000;
  b.ts = a.ts + 60;
  b.eng.resumeOutage(opened);
  b.run(0, 600);
  TEST_ASSERT_EQUAL(0, countType(b.q, "GRID_LOSS"));  // no second "grid lost"
  b.run(231, 120);
  TEST_ASSERT_EQUAL(1, countType(b.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(ev[0].dur >= 1200);  // counts the time before the reboot too
}

// 0.8.1: the owner's 06:10-06:17 cut on 2026-10-23. "Grid lost" (sev 4) went to Telegram, but "grid restored" (a sev-3
// clear) was muted by quiet hours while its e-mail still went out. A restore now follows a pushed loss past quiet hours.
static const uint32_t T_0610 = T_1200 - 5 * 3600 - 50 * 60;

void test_grid_restore_follows_pushed_loss_in_quiet_hours() {
  GridRig r(false, true);
  r.ts = T_0610;
  r.run(0, 420);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_LOSS", &ev));
  TEST_ASSERT_EQUAL(4, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].telegram);
  r.run(236, 60);
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].telegram);
  TEST_ASSERT_NOT_NULL(strstr(ev[0].msg, "Grid restored after 7m"));
}

void test_grid_restore_stays_quiet_when_loss_was_not_pushed() {
  GridRig r(false, true);
  r.ts = T_2300 + 50 * 60;  // 23:50, the day's push cap already used up: the loss is log-only
  for (int i = 0; i < cfg::PUSH_CAP_PER_DAY; i++) r.g.pushPrio(5, false, r.ts, false);
  r.run(0, 1800);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_LOSS", &ev));
  TEST_ASSERT_EQUAL(0, ev[0].pushPrio);
  r.run(236, 60);  // 00:20 the next day: a fresh cap, but quiet hours and nobody heard about the loss
  TEST_ASSERT_EQUAL(1, countType(r.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL(0, ev[0].pushPrio);
  TEST_ASSERT_FALSE(ev[0].telegram);
}

void test_grid_restore_after_reboot_follows_resumed_outage_in_quiet_hours() {
  GridRig a(false, true);
  a.ts = T_0610 - 3600;  // 05:10, pushed (sev 4)
  a.run(0, 600);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(a.q, "GRID_LOSS", &ev));
  TEST_ASSERT_TRUE(ev[0].telegram);
  GridRig b(false, true);  // the logger reboots, grid still off
  b.ms = 1000;
  b.ts = a.ts + 60;
  b.eng.resumeOutage(a.store.outage);
  b.run(0, 300);
  b.run(236, 60);
  TEST_ASSERT_EQUAL(1, countType(b.q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].pushPrio);
  TEST_ASSERT_TRUE(ev[0].telegram);
}

// 0.8.0: a planned restart (firmware update, requested reboot) is logged but never pushed or e-mailed
void test_raise_quiet_is_log_only() {
  AlertQueue q;
  NotifyGates g;
  AlertEngine eng(q, &g);
  rules::EmailPolicy pol;
  pol.on = true;
  pol.builtinMinSev = 1;
  eng.setEmailPolicy(pol);
  eng.raiseQuiet("LOGGER_REBOOT", 2, "Logger updated to 0.8.0 (was 0.7.9)", 100000, T_1200);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(q, "LOGGER_REBOOT", &ev));
  TEST_ASSERT_EQUAL(0, ev[0].pushPrio);
  TEST_ASSERT_FALSE(ev[0].telegram);
  TEST_ASSERT_EQUAL_STRING("", ev[0].em);
  TEST_ASSERT_EQUAL_STRING("Logger updated to 0.8.0 (was 0.7.9)", ev[0].msg);
  eng.raise("LOGGER_REBOOT", 2, "Logger rebooted (reason PANIC)", 100000, T_1200);  // unexpected: pushed
  TEST_ASSERT_EQUAL(1, countType(q, "LOGGER_REBOOT", &ev));
  TEST_ASSERT_TRUE(ev[0].pushPrio > 0);
  TEST_ASSERT_TRUE(ev[0].telegram);
}

void test_alert_fault_consecutive_and_repeat() {
  AlertQueue q;
  AlertEngine eng(q);
  uint32_t ms = 1000, ts = T_1200;
  Sample s = mkSample(ms, ts, 300, 0);
  s.md = 'F';
  eng.evaluate(s, ms, ts);
  TEST_ASSERT_EQUAL(0, q.size());  // one poll is not enough
  s.md = 'L';
  eng.evaluate(s, ms += 5000, ts += 5);
  s.md = 'F';
  eng.evaluate(s, ms += 5000, ts += 5);
  TEST_ASSERT_EQUAL(0, q.size());
  eng.evaluate(s, ms += 5000, ts += 5);  // 2 consecutive
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(q, "INVERTER_FAULT", &ev));
  TEST_ASSERT_EQUAL(5, ev[0].sev);
  TEST_ASSERT_EQUAL(5, ev[0].pushPrio);
  TEST_ASSERT_EQUAL(1, eng.takeFaultOpens());
  // still active after 30 min -> one re-notification
  for (int i = 0; i < 12 * 31; i++) eng.evaluate(s, ms += 5000, ts += 5);
  TEST_ASSERT_EQUAL(1, countType(q, "INVERTER_FAULT", &ev));
  TEST_ASSERT_TRUE(ev[0].repeat);
  // REVO fault code with its own table
  s.md = 'L';
  s.fc = 1;
  s.fcSrc = codes::Src::REVO;
  for (int i = 0; i < 3; i++) eng.evaluate(s, ms += 5000, ts += 5);
  // condition still true (fc != 0) -> stays active, no clear
  TEST_ASSERT_EQUAL(0, countType(q, "INVERTER_FAULT"));
  s.fc = 0;
  for (int i = 0; i < 13; i++) eng.evaluate(s, ms += 5000, ts += 5);
  TEST_ASSERT_EQUAL(1, countType(q, "INVERTER_FAULT", &ev));
  TEST_ASSERT_EQUAL_STRING("clear", ev[0].st);
}

void test_alert_batt_low_hysteresis_and_quiet_hours() {
  AlertQueue q;
  AlertEngine eng(q);
  uint32_t ms = 1000, ts = T_2300;  // 23:00 = quiet hours (22-7)
  Sample s = mkSample(ms, ts, 300, 0);
  s.md = 'B';
  s.bdi = 12;
  s.soc = 29;
  s.finalizeDerived();
  for (int i = 0; i < 14; i++) eng.evaluate(s, ms += 5000, ts += 5);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(q, "BATT_LOW", &ev));
  TEST_ASSERT_EQUAL(4, ev[0].sev);
  TEST_ASSERT_EQUAL(4, ev[0].pushPrio);  // sev 4 is not silenced by quiet hours
  // SOC 33 % is not enough to clear (needs >= socLow + 5)
  s.soc = 33;
  for (int i = 0; i < 100; i++) eng.evaluate(s, ms += 5000, ts += 5);
  TEST_ASSERT_EQUAL(0, countType(q, "BATT_LOW"));
  s.soc = 36;
  for (int i = 0; i < 61; i++) eng.evaluate(s, ms += 5000, ts += 5);
  TEST_ASSERT_EQUAL(1, countType(q, "BATT_LOW", &ev));
  TEST_ASSERT_EQUAL_STRING("clear", ev[0].st);
  TEST_ASSERT_EQUAL(0, ev[0].pushPrio);  // clear is sev 4 -> prio 3 -> silenced at night
  // a sev 2 event at night is logged but not pushed
  eng.raise("QUOTA_GUARD", 2, "test", ms, ts);
  TEST_ASSERT_EQUAL(1, countType(q, "QUOTA_GUARD", &ev));
  TEST_ASSERT_EQUAL(0, ev[0].pushPrio);
  // a forced test alert is always pushed
  eng.raise("TEST_ALERT", 3, "test alert", ms, ts, true);
  TEST_ASSERT_EQUAL(1, countType(q, "TEST_ALERT", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].pushPrio);
}

void test_alert_daily_push_cap() {
  AlertQueue q;
  AlertEngine eng(q);
  uint32_t ms = 1000, ts = T_1200;
  std::vector<AlertEvent> ev;
  int pushed = 0;
  for (int i = 0; i < 45; i++) {
    eng.raise("X", 3, "x", ms, ts);
    AlertEvent e;
    while (q.pop(e)) pushed += e.pushPrio ? 1 : 0;
  }
  TEST_ASSERT_EQUAL(cfg::PUSH_CAP_PER_DAY, pushed);
  eng.raise("Y", 5, "y", ms, ts);
  TEST_ASSERT_EQUAL(1, countType(q, "Y", &ev));
  TEST_ASSERT_EQUAL(5, ev[0].pushPrio);  // sev 5 always goes through
  eng.raise("Z", 3, "z", ms, T_1200 + 86400);  // next day: counter reset
  TEST_ASSERT_EQUAL(1, countType(q, "Z", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].pushPrio);
}

void test_alert_link_lost_and_summary() {
  AlertQueue q;
  AlertEngine eng(q);
  uint32_t ms = 1000;
  mrec::Daily d;
  d.epv = 12840;
  d.eld = 9710;
  d.egr = 1220;
  d.ech = 4100;
  d.edis = 3050;
  for (int i = 0; i < 60; i++) eng.tick(ms += 1000, T_1200 + i, true, false, &d);
  TEST_ASSERT_EQUAL(0, countType(q, "LINK_LOST"));
  eng.tick(ms += 1000, T_1200 + 60, true, false, &d);  // 60 s without a valid reply
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(q, "LINK_LOST", &ev));
  TEST_ASSERT_EQUAL(3, ev[0].sev);
  eng.tick(ms += 1000, T_1200 + 61, true, true, &d);
  TEST_ASSERT_EQUAL(1, countType(q, "LINK_LOST", &ev));
  TEST_ASSERT_EQUAL_STRING("clear", ev[0].st);
  // not expected (not armed / probing) -> never LINK_LOST
  AlertQueue q2;
  AlertEngine e2(q2);
  for (int i = 0; i < 300; i++) e2.tick(ms += 1000, T_1200 + i, false, false, &d);
  TEST_ASSERT_EQUAL(0, q2.size());
  // DAILY_SUMMARY at 21:00
  eng.tick(ms += 1000, T_2100, true, true, &d);
  TEST_ASSERT_EQUAL(1, countType(q, "DAILY_SUMMARY", &ev));
  TEST_ASSERT_EQUAL_STRING("Today: PV 12.8 kWh, load 9.7 kWh, grid 1.2 kWh (est.), battery in 4.1 / out 3.0 kWh",
                           ev[0].msg);
  eng.tick(ms += 1000, T_2100 + 60, true, true, &d);
  TEST_ASSERT_EQUAL(0, countType(q, "DAILY_SUMMARY"));  // once per day
}

void test_alert_queue_drop_policy() {
  AlertQueue q;
  AlertEvent e;
  for (int i = 0; i < cfg::ALERT_QUEUE_DEPTH; i++) {
    snprintf(e.type, sizeof e.type, "E%d", i);
    e.sev = (i == 3) ? 2 : 4;
    TEST_ASSERT_TRUE(q.push(e));
  }
  snprintf(e.type, sizeof e.type, "NEW");
  e.sev = 5;
  TEST_ASSERT_FALSE(q.push(e));  // full: the oldest sev <= 2 (E3) is dropped
  TEST_ASSERT_EQUAL(cfg::ALERT_QUEUE_DEPTH, q.size());
  bool sawE3 = false, sawNew = false;
  AlertEvent o;
  while (q.pop(o)) {
    if (!strcmp(o.type, "E3")) sawE3 = true;
    if (!strcmp(o.type, "NEW")) sawNew = true;
  }
  TEST_ASSERT_FALSE(sawE3);
  TEST_ASSERT_TRUE(sawNew);
  TEST_ASSERT_EQUAL(1, (int)q.dropped());
}


// r8 §4.13: `gw` equals the `egr` integrand and `nogrid` equals !gridPresent (one grid predicate everywhere)
void test_gw_and_nogrid_share_the_grid_predicate() {
  Aggregator agg;
  TEST_ASSERT_TRUE(agg.begin(1, hostAlloc, 64));
  uint32_t ms = 1000, ts = T_1200;
  Sample s = mkSample(ms, ts, 800, 0);
  snprintf(s.st, sizeof s.st, "00010101");  // AC charging
  s.bci = 10;
  s.finalizeDerived();
  double integrand = Aggregator::gridW(s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, (float)integrand, RuleEngine::instValue(rules::Metric::GW, s));
  // 1 h at that power integrates to the same Wh in egr
  for (int i = 0; i <= 720; i++) {
    Sample x = s;
    x.upMs = ms + (uint32_t)i * 5000;
    x.ts = ts + (uint32_t)i * 5;
    agg.addSample(x);
  }
  float e1, e2, e3, e4, egr;
  agg.todayEnergy(e1, e2, e3, e4, egr);
  TEST_ASSERT_FLOAT_WITHIN((float)integrand * 0.005f, (float)integrand, egr);
  char b[700];
  sampleToLiveJson(s, b, sizeof b);
  char want[32];
  snprintf(want, sizeof want, "\"gw\":%.0f", integrand);
  TEST_ASSERT_NOT_NULL(strstr(b, want));
  s.gv = 0;  // grid gone
  TEST_ASSERT_EQUAL_FLOAT(Aggregator::gridPresent(s) ? 0.f : 1.f, RuleEngine::instValue(rules::Metric::NOGRID, s));
  TEST_ASSERT_EQUAL_FLOAT(0, RuleEngine::instValue(rules::Metric::GW, s));
  sampleToLiveJson(s, b, sizeof b);
  TEST_ASSERT_NOT_NULL(strstr(b, "\"gw\":0"));
}

// r8 §4.7: built-in alerts get em:"q" per EmailPolicy; TEST_ALERT always (while on); DAILY_SUMMARY never
void test_builtin_email_flags() {
  AlertQueue q;
  NotifyGates g;
  AlertEngine eng(q, &g);
  rules::EmailPolicy pol;
  pol.on = true;
  pol.builtinMinSev = 4;
  pol.builtinClear = true;
  eng.setEmailPolicy(pol);
  uint32_t ms = 100000, ts = T_1200;  // past the 0.7.9 GRID_LOSS boot grace
  auto run = [&](float gv, int seconds) {
    for (int i = 0; i < seconds; i += 5) {
      Sample s = mkSample(ms, ts, 300, 0);
      s.gv = gv;
      eng.evaluate(s, ms, ts);
      ms += 5000;
      ts += 5;
    }
  };
  run(0, 30);
  std::vector<AlertEvent> ev;
  TEST_ASSERT_EQUAL(1, countType(q, "GRID_LOSS", &ev));
  TEST_ASSERT_EQUAL_STRING("q", ev[0].em);
  TEST_ASSERT_EQUAL('o', ev[0].kind);
  TEST_ASSERT_EQUAL(0, ev[0].rc);
  run(231, 60);
  TEST_ASSERT_EQUAL(1, countType(q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL_STRING("q", ev[0].em);  // builtin.clear and the open was e-mailed
  TEST_ASSERT_EQUAL('c', ev[0].kind);
  TEST_ASSERT_EQUAL(2, g.jobsToday());
  eng.raise("TEST_ALERT", 3, "Test alert from the dashboard", ms, ts, true);
  TEST_ASSERT_EQUAL(1, countType(q, "TEST_ALERT", &ev));
  TEST_ASSERT_EQUAL_STRING("q", ev[0].em);
  TEST_ASSERT_EQUAL('i', ev[0].kind);
  // minSev 5: a sev-4 open is not e-mailed, so neither is its clear
  pol.builtinMinSev = 5;
  eng.setEmailPolicy(pol);
  run(0, 30);
  TEST_ASSERT_EQUAL(1, countType(q, "GRID_LOSS", &ev));
  TEST_ASSERT_EQUAL_STRING("", ev[0].em);
  run(231, 60);
  TEST_ASSERT_EQUAL(1, countType(q, "GRID_RESTORED", &ev));
  TEST_ASSERT_EQUAL_STRING("", ev[0].em);
  // minSev 0: none, but TEST_ALERT still; DAILY_SUMMARY never
  pol.builtinMinSev = 0;
  eng.setEmailPolicy(pol);
  eng.raise("TEST_ALERT", 3, "again", ms, ts, true);
  TEST_ASSERT_EQUAL(1, countType(q, "TEST_ALERT", &ev));
  TEST_ASSERT_EQUAL_STRING("q", ev[0].em);
  mrec::Daily d;
  eng.tick(ms, T_2100, true, true, &d);
  TEST_ASSERT_EQUAL(1, countType(q, "DAILY_SUMMARY", &ev));
  TEST_ASSERT_EQUAL_STRING("", ev[0].em);
  // master switch off: nothing at all
  pol.on = false;
  pol.builtinMinSev = 3;
  eng.setEmailPolicy(pol);
  eng.raise("TEST_ALERT", 3, "off", ms, ts, true);
  TEST_ASSERT_EQUAL(1, countType(q, "TEST_ALERT", &ev));
  TEST_ASSERT_EQUAL_STRING("", ev[0].em);
  // device job cap reached -> "cap"
  pol.on = true;
  eng.setEmailPolicy(pol);
  while (g.takeMailJob(5, ts)) {
  }
  eng.raise("TEST_ALERT", 3, "capped", ms, ts, true);
  TEST_ASSERT_EQUAL(1, countType(q, "TEST_ALERT", &ev));
  TEST_ASSERT_EQUAL_STRING("cap", ev[0].em);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_raise_quiet_is_log_only);
  RUN_TEST(test_planned_grid_off_alerts_start_and_end_once);
  RUN_TEST(test_planned_grid_off_manual_on_then_fail_alerts);
  RUN_TEST(test_planned_grid_off_resumes_after_reboot);
  RUN_TEST(test_planned_grid_off_overrun_alerts_once);
  RUN_TEST(test_real_outage_no_repeats_and_boot_inside_window);
  RUN_TEST(test_outage_resumes_after_reboot_without_duplicate);
  RUN_TEST(test_grid_restore_follows_pushed_loss_in_quiet_hours);
  RUN_TEST(test_grid_restore_stays_quiet_when_loss_was_not_pushed);
  RUN_TEST(test_grid_restore_after_reboot_follows_resumed_outage_in_quiet_hours);
  RUN_TEST(test_time_keys);
  RUN_TEST(test_minuterec_format);
  RUN_TEST(test_energy_integration_1000w_1h);
  RUN_TEST(test_energy_gap_not_interpolated);
  RUN_TEST(test_minute_aggregation_and_alignment);
  RUN_TEST(test_provisional_minutes_and_fixup);
  RUN_TEST(test_h15_and_daily_rollover);
  RUN_TEST(test_ac_charge_and_grid_present);
  RUN_TEST(test_grid_power_energy_balance);
  RUN_TEST(test_alert_grid_loss_and_restore);
  RUN_TEST(test_alert_revo_line_fail_bit_with_grid_present);
  RUN_TEST(test_alert_fault_consecutive_and_repeat);
  RUN_TEST(test_alert_batt_low_hysteresis_and_quiet_hours);
  RUN_TEST(test_alert_daily_push_cap);
  RUN_TEST(test_alert_link_lost_and_summary);
  RUN_TEST(test_alert_queue_drop_policy);
  RUN_TEST(test_gw_and_nogrid_share_the_grid_predicate);
  RUN_TEST(test_builtin_email_flags);
  return UNITY_END();
}
