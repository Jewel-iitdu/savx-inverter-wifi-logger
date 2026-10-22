// test_rules - usage-threshold RuleEngine state machine (research/r8-email-alerts.md §4.4-4.8, §4.13).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "agg/Aggregator.h"
#include "agg/NotifyGates.h"
#include "agg/RuleEngine.h"
#include "agg/RuleParse.h"
#include "model/TimeUtil.h"

void setUp() {}
void tearDown() {}

// 2026-10-21 (Wednesday) 00:00 +06
static const uint32_t MIDNIGHT = 1792519200;
static uint32_t ep(int day, int hh, int mm, int ss = 0) {
  return MIDNIGHT + (uint32_t)day * 86400u + (uint32_t)(hh * 3600 + mm * 60 + ss);
}
static const int32_t DAY0 = 20747;  // local day number of 2026-10-21

struct FakePersist : IRulePersist {
  std::map<int, RulePersist> m;
  int saves = 0;
  bool load(uint8_t s, RulePersist& p) override {
    auto it = m.find(s);
    if (it == m.end()) return false;
    p = it->second;
    return true;
  }
  void save(uint8_t s, const RulePersist& p) override {
    m[s] = p;
    saves++;
  }
};

struct Rig {
  AlertQueue q;
  NotifyGates g;
  FakePersist* p;
  RuleEngine eng;
  std::vector<AlertEvent> ev;
  uint32_t ms = 10000, now = 0;
  EnergyView e;
  explicit Rig(FakePersist* fp = nullptr) : p(fp), eng(q, g, fp) {}
  void drain() {
    AlertEvent a;
    while (q.pop(a)) ev.push_back(a);
  }
  int count(const char* kindTypes = nullptr) {
    drain();
    (void)kindTypes;
    return (int)ev.size();
  }
};

static rules::Set mkSet(const std::string& rulesJson, bool emailOn = true, double rev = 1) {
  std::string j = std::string("{\"email\":{\"on\":") + (emailOn ? "true" : "false") +
                  ",\"to\":\"owner@gmail.com\"},\"rules\":{" + rulesJson + "}}";
  rules::Set s;
  s.rev = rev;
  char err[64];
  TEST_ASSERT_TRUE_MESSAGE(rules::parse(j.c_str(), j.size(), s, err, sizeof err), err);
  s.rev = rev;
  return s;
}

static Sample mkS(float w, float soc = 78, char md = 'L') {
  Sample s;
  s.lk = 1;
  s.gv = 231;
  s.gf = 50;
  s.ov = 230;
  s.w = w;
  s.ld = 16;
  s.bv = 26.4f;
  s.bci = 0;
  s.bdi = 0;
  s.soc = soc;
  s.pvw = 465;
  s.pvv = 145;
  s.md = md;
  s.tmp = 38;
  snprintf(s.st, sizeof s.st, "00010000");
  s.finalizeDerived();
  return s;
}

// advance `secs` seconds: tick at 1 Hz, a sample every 5 s
static void run(Rig& r, int secs, float w, float soc = 78, char md = 'L', bool samples = true) {
  for (int i = 0; i < secs; i++) {
    r.ms += 1000;
    r.now += 1;
    if (samples && (r.ms / 1000) % 5 == 0) r.eng.onSample(mkS(w, soc, md), r.e, r.ms, r.now, true);
    r.eng.tick(r.e, r.ms, r.now, true);
  }
  r.drain();
}

static const char* LOAD_RULE =
    "\"r01\":{\"name\":\"Load > 2500 W for 5 min\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\",\"op\":\"gt\","
    "\"value\":2500,\"mode\":\"hold\",\"forSec\":300,\"clr\":2200,\"clrSec\":120,\"coolMin\":60,\"maxDay\":3,"
    "\"sev\":3,\"email\":true,\"ntfy\":true,\"clrMail\":true,\"upd\":1}";

void test_hold_open_and_clear() {
  Rig r;
  r.now = ep(0, 14, 0);
  r.eng.setRules(mkSet(LOAD_RULE), r.now);
  TEST_ASSERT_EQUAL(1, r.eng.enabled());
  run(r, 60, 1000);
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(0));
  run(r, 200, 2600);
  TEST_ASSERT_EQUAL((int)rules::St::PEND, (int)r.eng.state(0));
  TEST_ASSERT_EQUAL(0, (int)r.ev.size());
  run(r, 110, 2600);  // > 300 s above the limit
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  const AlertEvent& o = r.ev[0];
  TEST_ASSERT_EQUAL_STRING("RULE", o.type);
  TEST_ASSERT_EQUAL_STRING("open", o.st);
  TEST_ASSERT_EQUAL_STRING("r01", o.rid);
  TEST_ASSERT_EQUAL_STRING("q", o.em);
  TEST_ASSERT_EQUAL('o', o.kind);
  TEST_ASSERT_EQUAL_FLOAT(2600, o.val);
  TEST_ASSERT_EQUAL_FLOAT(2500, o.thr);
  TEST_ASSERT_EQUAL_STRING("Load > 2500 W for 5 min: now 2,600 W (limit 2,500 W)", o.msg);
  TEST_ASSERT_EQUAL(3, o.pushPrio);  // ntfy, not quiet hours (14:00)
  // 2300 W is above the clear level (2200): stays FIRE
  run(r, 300, 2300);
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
  run(r, 60, 2100);
  TEST_ASSERT_EQUAL((int)rules::St::CLR, (int)r.eng.state(0));
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  run(r, 70, 2100);  // clrSec 120 passed
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(0));
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  const AlertEvent& c = r.ev[1];
  TEST_ASSERT_EQUAL_STRING("clear", c.st);
  TEST_ASSERT_EQUAL('c', c.kind);
  TEST_ASSERT_EQUAL_STRING("q", c.em);  // clrMail && the open was e-mailed
  TEST_ASSERT_EQUAL_STRING("Load > 2500 W for 5 min: back to 2,100 W (clear level 2,200 W)", c.msg);
  TEST_ASSERT_TRUE(c.dur >= 425 && c.dur <= 435);  // fire at +305 s, clear at +735 s
  TEST_ASSERT_EQUAL(1, r.eng.mailsToday(0));  // the clear does not count against maxDay
}

void test_default_hysteresis_and_for_zero() {
  Rig r;
  r.now = ep(0, 14, 0);
  r.eng.setRules(mkSet("\"r02\":{\"name\":\"Load > 2500\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\",\"op\":\"gt\","
                       "\"value\":2500,\"mode\":\"hold\",\"forSec\":0,\"clrSec\":10,\"sev\":3,\"email\":true,"
                       "\"ntfy\":false,\"upd\":1}"),
                 r.now);
  rules::Def d = r.eng.ruleSet().d[1];
  TEST_ASSERT_EQUAL_FLOAT(2375, RuleEngine::clrLevel(d));  // max(50 W, 5 %) = 125
  run(r, 5, 2600);  // forSec 0: straight to OPEN on the first sample
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(1));
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  TEST_ASSERT_EQUAL(0, r.ev[0].pushPrio);  // ntfy:false
  run(r, 30, 2400);  // 2400 > 2375: not cleared
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(1));
  run(r, 30, 2370);
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(1));
}

void test_nan_grace_and_never_clear_on_missing() {
  Rig r;
  r.now = ep(0, 14, 0);
  r.eng.setRules(mkSet(LOAD_RULE), r.now);
  run(r, 100, 2600);
  TEST_ASSERT_EQUAL((int)rules::St::PEND, (int)r.eng.state(0));
  run(r, 25, 0, 78, 'L', false);  // no samples for 25 s: PEND frozen
  TEST_ASSERT_EQUAL((int)rules::St::PEND, (int)r.eng.state(0));
  run(r, 10, 0, 78, 'L', false);  // > 30 s without valid data: back to IDLE
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(0));
  run(r, 310, 2600);
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
  run(r, 3600, 0, 78, 'L', false);  // an hour without data: FIRE stays (LINK_LOST explains why)
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
  run(r, 300, NAN);                 // samples with a missing value: still no clear
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
}

void test_window_across_midnight_and_weekend_days() {
  rules::Def d;
  d.hasWin = true;
  d.winFrom = 22 * 60;
  d.winTo = 6 * 60;
  d.days = 32;  // Friday only (bit 0 = Sunday)
  TEST_ASSERT_EQUAL(3, RuleEngine::dow(DAY0));       // 2026-10-21 is a Wednesday
  TEST_ASSERT_EQUAL(5, RuleEngine::dow(DAY0 + 2));   // Friday
  TEST_ASSERT_TRUE(RuleEngine::inWindow(d, ep(2, 23, 0)));    // Fri 23:00
  TEST_ASSERT_TRUE(RuleEngine::inWindow(d, ep(3, 1, 0)));     // Sat 01:00: the window started on Friday
  TEST_ASSERT_FALSE(RuleEngine::inWindow(d, ep(3, 23, 0)));   // Sat 23:00
  TEST_ASSERT_FALSE(RuleEngine::inWindow(d, ep(1, 23, 0)));   // Thu 23:00
  TEST_ASSERT_FALSE(RuleEngine::inWindow(d, ep(2, 1, 0)));    // Fri 01:00 belongs to Thursday's window
  // Bangladesh weekend (Fri + Sat), daytime window
  d.winFrom = 18 * 60;
  d.winTo = 23 * 60;
  d.days = 32 + 64;
  TEST_ASSERT_TRUE(RuleEngine::inWindow(d, ep(2, 18, 0)));
  TEST_ASSERT_TRUE(RuleEngine::inWindow(d, ep(3, 22, 59)));
  TEST_ASSERT_FALSE(RuleEngine::inWindow(d, ep(3, 23, 0)));
  TEST_ASSERT_FALSE(RuleEngine::inWindow(d, ep(4, 19, 0)));   // Sunday
  // a FIRE rule leaves its window: IDLE + info event, no e-mail
  Rig r;
  r.now = ep(0, 22, 58);
  r.eng.setRules(mkSet("\"r09\":{\"name\":\"Evening peak load > 1500 W\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\","
                       "\"op\":\"gt\",\"value\":1500,\"mode\":\"hold\",\"forSec\":60,\"clr\":1300,\"clrSec\":120,"
                       "\"coolMin\":60,\"maxDay\":2,\"win\":{\"from\":\"1800\",\"to\":\"2300\",\"days\":127},\"sev\":3,"
                       "\"email\":true,\"ntfy\":false,\"upd\":1}"),
                 r.now);
  run(r, 70, 1800);
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(8));
  run(r, 60, 1800);  // 23:00
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(8));
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("info", r.ev[1].st);
  TEST_ASSERT_EQUAL_STRING("", r.ev[1].em);
  TEST_ASSERT_EQUAL_STRING("Evening peak load > 1500 W: window ended while active", r.ev[1].msg);
}

static const char* GRID_DAY =
    "\"r03\":{\"name\":\"Daily grid usage > 5 kWh\",\"on\":true,\"metric\":\"egr\",\"scope\":\"day\",\"op\":\"gt\","
    "\"value\":5000,\"mode\":\"hold\",\"forSec\":0,\"coolMin\":60,\"maxDay\":1,\"sev\":3,\"email\":true,\"ntfy\":false,"
    "\"upd\":1}";

void test_day_energy_latch_rollover_guard_and_reset() {
  Rig r;
  r.now = ep(0, 18, 40);
  r.e.day = DAY0;
  r.e.egr = 4000;
  r.eng.setRules(mkSet(GRID_DAY), r.now);
  run(r, 30, 500);
  TEST_ASSERT_EQUAL(0, (int)r.ev.size());
  r.e.egr = 5010;
  run(r, 5, 500);
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)", r.ev[0].msg);
  TEST_ASSERT_EQUAL_STRING("q", r.ev[0].em);
  TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(2));
  r.e.egr = 7000;
  run(r, 3600, 500);  // latched: nothing more today
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  // 00:00:30 next day: the aggregator has not rolled yet (EnergyView.day is still yesterday) -> no re-fire
  r.now = ep(1, 0, 0, 30);
  run(r, 20, 500);
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(2));
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  r.e.day = DAY0 + 1;
  r.e.egr = 10;
  run(r, 60, 500);
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  r.e.egr = 5200;
  run(r, 2, 500);
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());  // a new day, a new e-mail
  TEST_ASSERT_EQUAL_STRING("q", r.ev[1].em);
}

void test_month_latch_and_cycle_reset() {
  Rig r;
  r.now = ep(0, 12, 0);
  r.e.day = DAY0;
  r.e.mEgr = 149000;
  r.e.mCycleKey = 20261001;
  r.e.mCovPct = 100;
  r.eng.setRules(mkSet("\"r06\":{\"name\":\"Monthly grid usage > 150 kWh\",\"on\":true,\"metric\":\"egr\",\"scope\":\"month\","
                       "\"op\":\"gt\",\"value\":150000,\"mode\":\"hold\",\"maxDay\":1,\"sev\":3,\"email\":true,"
                       "\"ntfy\":false,\"upd\":1}"),
                 r.now);
  run(r, 10, 500);
  TEST_ASSERT_EQUAL(0, (int)r.ev.size());
  r.e.mEgr = 151000;
  r.e.mCovPct = 87;
  run(r, 2, 500);
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING(
      "Monthly grid usage > 150 kWh: now 151.00 kWh (limit 150.00 kWh this cycle, est.) (cycle data 87 %)", r.ev[0].msg);
  TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(5));
  r.now = ep(5, 12, 0);  // later days of the same cycle: still latched
  r.e.day = DAY0 + 5;
  r.e.mEgr = 180000;
  run(r, 10, 500);
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(5));
  r.e.mCycleKey = 20261101;  // new billing cycle
  r.e.mEgr = 100;
  run(r, 5, 500);
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(5));
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
}

static const char* PV_AT =
    "\"r04\":{\"name\":\"PV today < 3 kWh by 17:00\",\"on\":true,\"metric\":\"epv\",\"scope\":\"day\",\"op\":\"lt\","
    "\"value\":3000,\"mode\":\"at\",\"at\":\"1700\",\"minCov\":80,\"maxDay\":1,\"sev\":2,\"email\":true,\"ntfy\":false,"
    "\"upd\":1}";

void test_at_mode_on_time_late_and_low_coverage() {
  {
    Rig r;
    r.now = ep(0, 16, 58);
    r.e.day = DAY0;
    r.e.epv = 2410;
    r.e.covPct = 100;
    r.eng.setRules(mkSet(PV_AT), r.now);
    run(r, 60, 500);
    TEST_ASSERT_EQUAL(0, (int)r.ev.size());
    run(r, 61, 500);  // 17:00:01
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("PV today < 3 kWh by 17:00: 2.41 kWh at 17:00 (limit 3.00 kWh)", r.ev[0].msg);
    TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(3));
    run(r, 600, 500);
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());  // once per day
  }
  {
    Rig r;  // the logger was down at 17:00 and boots at 18:42
    r.now = ep(0, 18, 42);
    r.e.day = DAY0;
    r.e.epv = 2410;
    r.e.covPct = 90;
    r.eng.setRules(mkSet(PV_AT), r.now);
    run(r, 2, 500);
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("PV today < 3 kWh by 17:00: 2.41 kWh at 18:42 (limit 3.00 kWh), checked late", r.ev[0].msg);
  }
  {
    Rig r;  // coverage below minCov: not checked
    r.now = ep(0, 17, 0);
    r.e.day = DAY0;
    r.e.epv = 2410;
    r.e.covPct = 62;
    r.eng.setRules(mkSet(PV_AT), r.now);
    run(r, 2, 500);
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("skip", r.ev[0].em);
    TEST_ASSERT_EQUAL_STRING("info", r.ev[0].st);
    TEST_ASSERT_EQUAL_STRING("PV today < 3 kWh by 17:00: not checked: coverage 62 % < 80 %", r.ev[0].msg);
    TEST_ASSERT_EQUAL((int)rules::St::DONE, (int)r.eng.state(3));
  }
  {
    Rig r;  // enough PV: DONE silently
    r.now = ep(0, 17, 0);
    r.e.day = DAY0;
    r.e.epv = 3500;
    r.e.covPct = 100;
    r.eng.setRules(mkSet(PV_AT), r.now);
    run(r, 2, 500);
    TEST_ASSERT_EQUAL(0, (int)r.ev.size());
    TEST_ASSERT_EQUAL((int)rules::St::DONE, (int)r.eng.state(3));
  }
}

static const char* FLAP_RULE =
    "\"r05\":{\"name\":\"Load > 1000 W\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\",\"op\":\"gt\",\"value\":1000,"
    "\"mode\":\"hold\",\"forSec\":0,\"clr\":900,\"clrSec\":0,\"coolMin\":5,\"maxDay\":3,\"sev\":3,\"email\":true,"
    "\"ntfy\":false,\"clrMail\":true,\"upd\":1}";

void test_cooldown_then_exactly_one_deferred_repeat() {
  Rig r;
  r.now = ep(0, 10, 0);
  r.eng.setRules(mkSet(FLAP_RULE), r.now);
  run(r, 5, 1200);
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("q", r.ev[0].em);
  run(r, 10, 800);  // clear (clrSec 0)
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("clear", r.ev[1].st);
  run(r, 10, 1200);  // re-open 25 s after the first mail: cooldown 5 min
  TEST_ASSERT_EQUAL(3, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("open", r.ev[2].st);
  TEST_ASSERT_EQUAL_STRING("cool", r.ev[2].em);
  run(r, 240, 1200);  // cooldown not over yet
  TEST_ASSERT_EQUAL(3, (int)r.ev.size());
  run(r, 60, 1200);  // cooldown passed while still FIRE: one deferred REPEAT
  TEST_ASSERT_EQUAL(4, (int)r.ev.size());
  TEST_ASSERT_EQUAL('r', r.ev[3].kind);
  TEST_ASSERT_EQUAL_STRING("q", r.ev[3].em);
  TEST_ASSERT_TRUE(strstr(r.ev[3].msg, "Load > 1000 W: still 1,200 W for 4 min") == r.ev[3].msg);
  run(r, 3600, 1200);  // repeatMin 0: nothing else
  TEST_ASSERT_EQUAL(4, (int)r.ev.size());
  TEST_ASSERT_EQUAL(2, r.eng.mailsToday(4));
}

void test_repeat_spacing_is_max_of_repeat_and_cooldown() {
  Rig r;
  r.now = ep(0, 9, 0);
  r.eng.setRules(mkSet("\"r02\":{\"name\":\"Load > 2500 W for 5 min\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\","
                       "\"op\":\"gt\",\"value\":2500,\"mode\":\"hold\",\"forSec\":0,\"coolMin\":60,\"repeatMin\":30,"
                       "\"maxDay\":10,\"sev\":3,\"email\":true,\"ntfy\":false,\"upd\":1}"),
                 r.now);
  run(r, 5, 2710);
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  run(r, 31 * 60, 2710);  // 30 min: no repeat yet (spacing = max(30, 60) = 60 min)
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  run(r, 30 * 60, 2710);
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  TEST_ASSERT_EQUAL('r', r.ev[1].kind);
  TEST_ASSERT_EQUAL_STRING("q", r.ev[1].em);
  run(r, 10 * 60, 2710);  // 1 h 10 min held in total
  run(r, 50 * 60, 2710);
  TEST_ASSERT_EQUAL(3, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("Load > 2500 W for 5 min: still 2,710 W for 2 h", r.ev[2].msg);
  char b[32];
  RuleEngine::fmtDuration(4200, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("1 h 10 min", b);
}

void test_maxday_rcap_and_daily_reset() {
  Rig r;
  r.now = ep(0, 10, 0);
  std::string rule = FLAP_RULE;
  rule.replace(rule.find("\"maxDay\":3"), 10, "\"maxDay\":1");
  r.eng.setRules(mkSet(rule), r.now);
  run(r, 5, 1200);
  run(r, 10, 800);
  run(r, 400, 800);  // > cooldown
  run(r, 5, 1200);
  TEST_ASSERT_EQUAL(3, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("rcap", r.ev[2].em);
  // next local day: the counter resets
  run(r, 10, 800);
  r.now = ep(1, 10, 0);
  run(r, 5, 1200);
  TEST_ASSERT_EQUAL_STRING("q", r.ev.back().em);
}

void test_job_cap_with_sev4_reserve() {
  NotifyGates g;
  uint32_t t = ep(0, 12, 0);
  int ok3 = 0;
  for (int i = 0; i < 50; i++) ok3 += g.takeMailJob(3, t) ? 1 : 0;
  TEST_ASSERT_EQUAL(35, ok3);
  int ok4 = 0;
  for (int i = 0; i < 10; i++) ok4 += g.takeMailJob(4, t) ? 1 : 0;
  TEST_ASSERT_EQUAL(5, ok4);
  TEST_ASSERT_FALSE(g.takeMailJob(5, t));
  TEST_ASSERT_TRUE(g.takeMailJob(3, ep(1, 0, 1)));  // new local day
  // a rule hitting the exhausted cap gets em "cap"
  Rig r;
  r.now = ep(0, 12, 0);
  for (int i = 0; i < 40; i++) r.g.takeMailJob(4, r.now);
  r.eng.setRules(mkSet(FLAP_RULE), r.now);
  run(r, 5, 1200);
  TEST_ASSERT_EQUAL_STRING("cap", r.ev[0].em);
  // persisted counter survives "reboots"
  struct Store : IGateStore {
    int32_t d = 0;
    uint16_t c = 0;
    void saveJobs(int32_t day, uint16_t cnt) override { d = day; c = cnt; }
  } st;
  NotifyGates g2;
  g2.setStore(&st);
  g2.takeMailJob(3, t);
  g2.takeMailJob(3, t);
  NotifyGates g3;
  g3.restoreJobs(st.d, st.c);
  TEST_ASSERT_EQUAL(2, g3.jobsToday());
  g3.takeMailJob(3, t);
  TEST_ASSERT_EQUAL(3, g3.jobsToday());
}

void test_clear_mail_needs_clrmail_and_mailed_open() {
  {
    Rig r;
    r.now = ep(0, 10, 0);
    std::string rule = FLAP_RULE;
    rule.replace(rule.find("\"clrMail\":true"), 14, "\"clrMail\":false");
    r.eng.setRules(mkSet(rule), r.now);
    run(r, 5, 1200);
    run(r, 10, 800);
    TEST_ASSERT_EQUAL(2, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("", r.ev[1].em);
  }
  {
    Rig r;  // open blocked by cooldown -> its clear is not e-mailed either
    r.now = ep(0, 10, 0);
    r.eng.setRules(mkSet(FLAP_RULE), r.now);
    run(r, 5, 1200);
    run(r, 10, 800);
    run(r, 10, 1200);  // "cool"
    run(r, 10, 800);
    TEST_ASSERT_EQUAL(4, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("cool", r.ev[2].em);
    TEST_ASSERT_EQUAL_STRING("clear", r.ev[3].st);
    TEST_ASSERT_EQUAL_STRING("", r.ev[3].em);
  }
}

void test_at_most_20_events_per_rule_per_day() {
  Rig r;
  r.now = ep(0, 8, 0);
  r.eng.setRules(mkSet(FLAP_RULE), r.now);
  for (int i = 0; i < 30; i++) {
    run(r, 5, 1200);
    run(r, 10, 800);
  }
  TEST_ASSERT_EQUAL(20, (int)r.ev.size());
  TEST_ASSERT_EQUAL(20, r.eng.eventsToday(4));
}

void test_semhash_cosmetic_edit_keeps_state_threshold_edit_resets() {
  Rig r;
  r.now = ep(0, 10, 0);
  r.eng.setRules(mkSet(FLAP_RULE, true, 1), r.now);
  run(r, 5, 1200);
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(4));
  std::string cosmetic = FLAP_RULE;
  cosmetic.replace(cosmetic.find("Load > 1000 W"), 13, "Big load");
  cosmetic.replace(cosmetic.find("\"sev\":3"), 7, "\"sev\":4");
  r.eng.setRules(mkSet(cosmetic, true, 2), r.now);
  run(r, 5, 1200);
  TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(4));
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  std::string thr = FLAP_RULE;
  thr.replace(thr.find("\"value\":1000"), 12, "\"value\":1100");
  r.eng.setRules(mkSet(thr, true, 3), r.now);
  r.drain();
  TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(4));
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("Load > 1000 W: rule changed while active", r.ev[1].msg);
  TEST_ASSERT_EQUAL_STRING("", r.ev[1].em);
  // turning it off while active -> info, OFF
  run(r, 5, 1200);
  std::string off = thr;
  off.replace(off.find("\"on\":true"), 9, "\"on\":false");
  r.eng.setRules(mkSet(off, true, 4), r.now);
  r.drain();
  TEST_ASSERT_EQUAL((int)rules::St::OFF, (int)r.eng.state(4));
  TEST_ASSERT_EQUAL_STRING("Load > 1000 W: rule turned off", r.ev.back().msg);
}

void test_reboot_restore_latch_fire_pend() {
  FakePersist fp;
  {
    Rig r(&fp);
    r.now = ep(0, 18, 40);
    r.e.day = DAY0;
    r.e.egr = 5010;
    r.eng.setRules(mkSet(GRID_DAY + std::string(",") + LOAD_RULE), r.now);
    run(r, 5, 2600);  // grid rule e-mails + latches, load rule PEND
    TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(2));
    TEST_ASSERT_EQUAL((int)rules::St::PEND, (int)r.eng.state(0));
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  }
  {
    Rig r(&fp);  // reboot the same day
    r.now = ep(0, 19, 0);
    r.e.day = DAY0;
    r.e.egr = 6000;
    r.eng.setRules(mkSet(GRID_DAY + std::string(",") + LOAD_RULE), r.now);
    TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(2));
    TEST_ASSERT_EQUAL((int)rules::St::IDLE, (int)r.eng.state(0));  // PEND is not restored: the hold restarts
    run(r, 120, 2600);
    TEST_ASSERT_EQUAL(0, (int)r.ev.size());  // no second "daily grid" e-mail
    TEST_ASSERT_EQUAL(1, r.eng.mailsToday(2));
    run(r, 300, 2600);  // load rule opens normally
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
    TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
  }
  {
    Rig r(&fp);  // reboot while FIRE: no new OPEN, clears normally
    r.now = ep(0, 19, 30);
    r.e.day = DAY0;
    r.e.egr = 6000;
    r.eng.setRules(mkSet(GRID_DAY + std::string(",") + LOAD_RULE), r.now);
    TEST_ASSERT_EQUAL((int)rules::St::FIRE, (int)r.eng.state(0));
    run(r, 600, 2600);
    TEST_ASSERT_EQUAL(0, (int)r.ev.size());
    run(r, 200, 2000);
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("clear", r.ev[0].st);
    TEST_ASSERT_EQUAL_STRING("q", r.ev[0].em);  // openMailed was restored
  }
  {
    Rig r(&fp);  // next day: the latch is released
    r.now = ep(1, 10, 0);
    r.e.day = DAY0 + 1;
    r.e.egr = 5500;
    r.eng.setRules(mkSet(GRID_DAY + std::string(",") + LOAD_RULE), r.now);
    run(r, 3, 100);
    TEST_ASSERT_EQUAL(1, (int)r.ev.size());
    TEST_ASSERT_EQUAL_STRING("q", r.ev[0].em);
  }
}

void test_formatting_booleans_and_units() {
  char b[48];
  RuleEngine::fmtValue(rules::Metric::ONBAT, 1, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("yes", b);
  RuleEngine::fmtValue(rules::Metric::NOGRID, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("no", b);
  RuleEngine::fmtValue(rules::Metric::W, 12345, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("12,345 W", b);
  RuleEngine::fmtValue(rules::Metric::W, -1200, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("-1,200 W", b);
  RuleEngine::fmtValue(rules::Metric::EPV, 830, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("830 Wh", b);
  RuleEngine::fmtValue(rules::Metric::BV, 26.44f, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("26.4 V", b);
  RuleEngine::fmtValue(rules::Metric::GF, 50, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("50.0 Hz", b);
  RuleEngine::fmtValue(rules::Metric::TMP, 70.4f, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("70 \xC2\xB0" "C", b);
  RuleEngine::fmtValue(rules::Metric::GOUT, 200, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("3 h 20 min", b);
  // the SOC example of r8 §4.5
  Rig r;
  r.now = ep(0, 12, 0);
  r.eng.setRules(mkSet("\"r03\":{\"name\":\"SOC < 30 %\",\"on\":true,\"metric\":\"soc\",\"scope\":\"inst\",\"op\":\"lt\","
                       "\"value\":30,\"mode\":\"hold\",\"forSec\":60,\"clr\":35,\"clrSec\":300,\"coolMin\":120,\"maxDay\":3,"
                       "\"sev\":4,\"email\":true,\"ntfy\":false,\"clrMail\":true,\"upd\":1}"),
                 r.now);
  run(r, 70, 500, 28);
  run(r, 310, 500, 36);
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("SOC < 30 %: now 28 % (limit 30 %)", r.ev[0].msg);
  TEST_ASSERT_EQUAL_STRING("SOC < 30 %: back to 36 % (clear level 35 %)", r.ev[1].msg);
  // boolean rule: on battery for 2 h
  Rig b2;
  b2.now = ep(0, 20, 0);
  b2.eng.setRules(mkSet("\"r08\":{\"name\":\"On battery for 2 h\",\"on\":true,\"metric\":\"onbat\",\"scope\":\"inst\","
                        "\"op\":\"ge\",\"value\":1,\"mode\":\"hold\",\"forSec\":7200,\"clrSec\":60,\"coolMin\":120,"
                        "\"maxDay\":3,\"sev\":3,\"email\":true,\"ntfy\":false,\"clrMail\":true,\"upd\":1}"),
                  b2.now);
  run(b2, 7210, 500, 70, 'B');
  TEST_ASSERT_EQUAL(1, (int)b2.ev.size());
  TEST_ASSERT_EQUAL_STRING("On battery for 2 h: now yes (limit yes)", b2.ev[0].msg);
  run(b2, 70, 500, 70, 'L');
  TEST_ASSERT_EQUAL(2, (int)b2.ev.size());
  TEST_ASSERT_EQUAL_STRING("On battery for 2 h: back to no (clear level no)", b2.ev[1].msg);
}

void test_invalid_rule_reported_once_per_rev_and_policy_off() {
  Rig r;
  r.now = ep(0, 12, 0);
  std::string bad = "\"r07\":{\"name\":\"Future\",\"on\":true,\"metric\":\"rssi\",\"scope\":\"inst\",\"op\":\"lt\",\"value\":-80,"
                    "\"mode\":\"hold\",\"sev\":2,\"email\":true,\"ntfy\":false,\"upd\":1}";
  r.eng.setRules(mkSet(bad, true, 5), r.now);
  r.eng.setRules(mkSet(bad, true, 5), r.now);
  r.drain();
  TEST_ASSERT_EQUAL(1, (int)r.ev.size());
  TEST_ASSERT_EQUAL_STRING("RULE_INVALID", r.ev[0].type);
  TEST_ASSERT_EQUAL(2, r.ev[0].sev);
  TEST_ASSERT_EQUAL_STRING("Rule r07 ignored by the logger: unknown metric 'rssi'", r.ev[0].msg);
  TEST_ASSERT_EQUAL((int)rules::St::INV, (int)r.eng.state(6));
  r.eng.setRules(mkSet(bad, true, 6), r.now);  // a new alertsRev reports it again
  r.drain();
  TEST_ASSERT_EQUAL(2, (int)r.ev.size());
  AlertStateOut o;
  bool found = false;
  while (r.eng.takeDirty(o))
    if (!strcmp(o.rid, "r07")) {
      found = true;
      TEST_ASSERT_EQUAL_STRING("unknown metric 'rssi'", o.err);
    }
  TEST_ASSERT_TRUE(found);
  // e-mail switched off: events carry no em
  Rig p;
  p.now = ep(0, 12, 0);
  p.eng.setRules(mkSet(FLAP_RULE, false), p.now);
  run(p, 5, 1200);
  TEST_ASSERT_EQUAL(1, (int)p.ev.size());
  TEST_ASSERT_EQUAL_STRING("", p.ev[0].em);
}

void test_wait_until_time_and_alertstate_output() {
  Rig r;
  r.now = 0;
  r.eng.setRules(mkSet(GRID_DAY), 0);
  r.e.day = DAY0;
  r.e.egr = 9000;
  for (int i = 0; i < 10; i++) r.eng.tick(r.e, r.ms += 1000, 0, false);
  r.drain();
  TEST_ASSERT_EQUAL((int)rules::St::WAIT, (int)r.eng.state(2));
  TEST_ASSERT_EQUAL(0, (int)r.ev.size());
  r.now = ep(0, 12, 0);
  run(r, 2, 0);
  TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)r.eng.state(2));
  AlertStateOut o;
  AlertStateOut last;
  while (r.eng.takeDirty(o)) last = o;
  TEST_ASSERT_EQUAL_STRING("r03", last.rid);
  TEST_ASSERT_EQUAL((int)rules::St::LATCH, (int)last.st);
  TEST_ASSERT_EQUAL_UINT32(20261021, last.pk);
  TEST_ASSERT_EQUAL(1, last.cnt);
  TEST_ASSERT_TRUE(last.last > 0);
  // deleting the rule: a removed record (null in RTDB)
  r.eng.setRules(mkSet(""), r.now);
  bool removed = false;
  while (r.eng.takeDirty(o))
    if (!strcmp(o.rid, "r03")) removed = o.removed;
  TEST_ASSERT_TRUE(removed);
}

void test_inst_metric_sources() {
  Sample s = mkS(500);
  s.bci = 10;  // AC charging: st b0 set below
  snprintf(s.st, sizeof s.st, "00010001");
  s.finalizeDerived();
  // 0.7.5 energy balance: load 500 + battery 26.4 V x 10 A - PV 465 = 299 from the grid
  TEST_ASSERT_FLOAT_WITHIN(0.5, 500 + 26.4f * 10 - 465, RuleEngine::instValue(rules::Metric::GW, s));
  TEST_ASSERT_FLOAT_WITHIN(0.5, Aggregator::gridW(s), RuleEngine::instValue(rules::Metric::GW, s));
  s.md = 'B';
  TEST_ASSERT_EQUAL_FLOAT(0, RuleEngine::instValue(rules::Metric::GW, s));
  TEST_ASSERT_EQUAL_FLOAT(1, RuleEngine::instValue(rules::Metric::ONBAT, s));
  s.gv = 0;
  TEST_ASSERT_EQUAL_FLOAT(1, RuleEngine::instValue(rules::Metric::NOGRID, s));
  TEST_ASSERT_EQUAL(!Aggregator::gridPresent(s), RuleEngine::instValue(rules::Metric::NOGRID, s) == 1);
  s.bi = -10;
  s.bw = -264;
  TEST_ASSERT_EQUAL_FLOAT(264, RuleEngine::instValue(rules::Metric::BDW, s));
  s.tmpRaw = true;
  TEST_ASSERT_TRUE(isnan(RuleEngine::instValue(rules::Metric::TMP, s)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hold_open_and_clear);
  RUN_TEST(test_default_hysteresis_and_for_zero);
  RUN_TEST(test_nan_grace_and_never_clear_on_missing);
  RUN_TEST(test_window_across_midnight_and_weekend_days);
  RUN_TEST(test_day_energy_latch_rollover_guard_and_reset);
  RUN_TEST(test_month_latch_and_cycle_reset);
  RUN_TEST(test_at_mode_on_time_late_and_low_coverage);
  RUN_TEST(test_cooldown_then_exactly_one_deferred_repeat);
  RUN_TEST(test_repeat_spacing_is_max_of_repeat_and_cooldown);
  RUN_TEST(test_maxday_rcap_and_daily_reset);
  RUN_TEST(test_job_cap_with_sev4_reserve);
  RUN_TEST(test_clear_mail_needs_clrmail_and_mailed_open);
  RUN_TEST(test_at_most_20_events_per_rule_per_day);
  RUN_TEST(test_semhash_cosmetic_edit_keeps_state_threshold_edit_resets);
  RUN_TEST(test_reboot_restore_latch_fire_pend);
  RUN_TEST(test_formatting_booleans_and_units);
  RUN_TEST(test_invalid_rule_reported_once_per_rev_and_policy_off);
  RUN_TEST(test_wait_until_time_and_alertstate_output);
  RUN_TEST(test_inst_metric_sources);
  return UNITY_END();
}
