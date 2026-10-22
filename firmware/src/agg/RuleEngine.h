// RuleEngine.h - owner-defined usage-threshold rules (research/r8-email-alerts.md §4.2-4.8). Host-portable.
//
// Runs in aggTask next to AlertEngine and writes into the same AlertQueue (type "RULE" / "RULE_INVALID").
// Per-rule state machine (r8 §4.4):
//   OFF -> (WAIT until local time when needed) -> IDLE -> PEND (held forSec) -> FIRE -> CLR (held clrSec) -> IDLE
//   day/month scope: FIRE -> LATCH until the period key changes;   mode "at": IDLE -> LATCH | DONE once per day
//   INV: the rule is not understood by this firmware (one RULE_INVALID event per alertsRev).
// Holds use millis(); windows, period keys and daily counters use local time (UTC+6). A rule never clears on missing
// data. Gates for e-mail at OPEN/REPEAT (r8 §4.6): channel+policy, cooldown -> "cool", maxDay -> "rcap", device job
// cap -> "cap", else "q". Clear e-mails need clrMail && the open was e-mailed and skip cooldown/maxDay.
// State that must survive reboots (cooldown, e-mails today, latches) is saved per slot through IRulePersist on
// transitions only (NVS rules/rsNN, 28 B).
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "agg/AlertEngine.h"
#include "agg/NotifyGates.h"
#include "agg/RuleTypes.h"
#include "model/Sample.h"

// Energy figures for day/month rules, filled by aggTask from Aggregator::todayEnergy()/daily() + MonthToDate.
struct EnergyView {
  int32_t day = 0;              // local day number of the "today" values (rollover guard, r8 §4.5)
  double eld = NAN, egr = NAN, epv = NAN, edis = NAN, ech = NAN;   // Wh today incl. the open minute
  int goutMin = -1;             // minutes without grid today
  uint8_t covPct = 0;           // minutes with data / minutes elapsed today
  double mEld = NAN, mEgr = NAN, mEpv = NAN, mEdis = NAN, mEch = NAN;  // billing cycle incl. today
  int mGoutMin = -1;
  uint32_t mCycleKey = 0;       // yyyymmdd of the cycle start (0 = unknown)
  uint8_t mCovPct = 0;
};

#pragma pack(push, 1)
struct RulePersist {
  uint32_t semHash, sinceEp, lastMailEp, openEp, pk;
  int32_t cntDay;
  uint8_t st, cnt, evCnt, flags;  // flags: b0 openMailed, b1 deferredOpen
};
#pragma pack(pop)
static_assert(sizeof(RulePersist) == 28, "RulePersist must be 28 bytes (r8 §4.2)");

class IRulePersist {
 public:
  virtual ~IRulePersist() {}
  virtual bool load(uint8_t slot, RulePersist& p) = 0;
  virtual void save(uint8_t slot, const RulePersist& p) = 0;
};

// one `alertState/<rid>` record for the uplink (r8 §3.5)
struct AlertStateOut {
  char rid[4] = "";
  rules::St st = rules::St::OFF;
  uint32_t since = 0, last = 0, pk = 0;
  float val = NAN;
  uint8_t cnt = 0;
  char err[40] = "";
  bool removed = false;         // slot deleted: write null
};

class RuleEngine {
 public:
  RuleEngine(AlertQueue& q, NotifyGates& g, IRulePersist* persist) : q_(q), gates_(g), persist_(persist) {}

  void setRules(const rules::Set& s, uint32_t ep);  // keeps state where semHash is unchanged
  void onSample(const Sample& s, const EnergyView& e, uint32_t ms, uint32_t ep, bool timeOk);  // 5 s, inst rules
  void tick(const EnergyView& e, uint32_t ms, uint32_t ep, bool timeOk);  // 1 Hz: energy/at rules, grace, periods
  bool takeDirty(AlertStateOut& o);

  double loadedRev() const { return set_.rev; }
  uint8_t enabled() const;
  const rules::EmailPolicy& policy() const { return set_.email; }
  const rules::Set& ruleSet() const { return set_; }
  rules::St state(int slot) const { return rs_[slot].st; }
  uint8_t mailsToday(int slot) const { return rs_[slot].cnt; }
  uint8_t eventsToday(int slot) const { return rs_[slot].evCnt; }

  // helpers (also used by the tests and the local API)
  static float instValue(rules::Metric m, const Sample& s);
  static size_t fmtValue(rules::Metric m, float v, char* out, size_t n);
  static void fmtDuration(uint32_t seconds, char* out, size_t n);
  static int dow(int32_t localDayNumber) { return (int)(((localDayNumber % 7) + 7 + 4) % 7); }  // 0 = Sunday
  static bool inWindow(const rules::Def& d, uint32_t ep);
  static bool cond(const rules::Def& d, float v);
  static bool clrCond(const rules::Def& d, float v);
  static float clrLevel(const rules::Def& d);

 private:
  struct RS {
    rules::St st = rules::St::OFF;
    uint32_t t0 = 0;                  // ms: PEND / CLR start
    uint32_t lastValidMs = 0;
    bool haveValid = false;
    uint32_t fireMs = 0, lastRepMs = 0;
    uint32_t sinceEp = 0, lastMailEp = 0, openEp = 0, pk = 0;
    int32_t cntDay = 0;
    uint8_t cnt = 0, evCnt = 0;
    bool openMailed = false, deferredOpen = false, unconfirmed = false;
    float val = NAN, lastV = NAN;
    double invRev = -1;               // alertsRev for which RULE_INVALID was raised
    bool restored = false;            // persistence consulted once per boot
    bool dirty = false, persistDirty = false, removed = false;
  };

  bool active(int i) const;
  bool needsTime(const rules::Def& d) const;
  bool prelude(int i, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e);
  void stepHold(int i, bool fresh, float v, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e);
  void stepAt(int i, float v, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e);
  void open(int i, float v, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e, bool atMode, bool late);
  void repeat(int i, uint32_t ms, uint32_t ep, bool timeOk);
  void clear(int i, float v, uint32_t ms, uint32_t ep, bool timeOk);
  void toIdle(int i, uint32_t ep, bool timeOk);
  void info(int i, const char* what, uint32_t ms, uint32_t ep, bool timeOk);
  const char* gateMail(int i, uint32_t ep, bool timeOk);
  bool emit(int i, const char* type, const char* st, char kind, uint8_t sev, const char* msg, const char* em,
            float val, float thr, uint32_t dur, uint32_t ms, uint32_t ep, bool timeOk, bool push);
  void dayRoll(int i, uint32_t ep);
  uint32_t periodKey(const rules::Def& d, uint32_t ep, const EnergyView& e) const;
  float energyValue(const rules::Def& d, const EnergyView& e, uint32_t ep, bool timeOk) const;
  void restore(int i, uint32_t ep);
  void flushPersist();
  void mark(int i, bool persist) {
    rs_[i].dirty = true;
    if (persist) rs_[i].persistDirty = true;
  }

  AlertQueue& q_;
  NotifyGates& gates_;
  IRulePersist* persist_;
  rules::Set set_;
  rules::Set prev_;  // previous set during setRules (a member, not a 3.4 KB stack copy)
  RS rs_[cfg::MAX_RULES];
  float inst_[(int)rules::Metric::COUNT];
  uint32_t instMs_ = 0;
  bool haveInst_ = false;
  int dirtyCursor_ = 0;
};
