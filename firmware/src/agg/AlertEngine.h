// AlertEngine.h - alert rules, hysteresis and rate limits (d1 §7.2-7.3). Host-portable.
// The engine only DECIDES: it emits AlertEvents into an AlertQueue. Delivery (ntfy / Telegram / RTDB
// `events`) is the uplink's job (FW-2, see src/net/Notify.h and src/net/Uplink.h).
#pragma once
#include <math.h>
#include <stdint.h>

#include "agg/NotifyGates.h"
#include "agg/RuleTypes.h"
#include "config.h"
#include "model/Lock.h"
#include "model/Sample.h"
#include "store/MinuteRec.h"

// Owner-tunable thresholds = the cloud `cfg` node (d1 §4.2), cached in NVS namespace "alert".
struct AlertCfg {
  int liveSec = 10;
  float socLow = 30, bvLow = 22.5f, bvHigh = 30.0f, tempHigh = 75, loadHigh = 90;
  int notifyRepeatMin = 30;
  int keepHistDays = 400, keepEventDays = 730;
  int quietFrom = 22, quietTo = 7;
  int summaryHour = 21;  // -1 disables DAILY_SUMMARY
  uint32_t testAlert = 0, reprobe = 0;
  // 0.7.9 (appended: older NVS blobs load as a prefix): the owner's daily planned grid-off, local minutes 0..1439,
  // -1 = none. A grid loss at its start (+- GRID_OFF_GRACE_MIN) is labelled "planned" (log-only until 0.8.1, now
  // pushed once with its restore); still off after it -> a real alert.
  int gridOffFrom = -1, gridOffTo = -1;
  // 0.9.0 (appended): read the grid meter (1) or not (0); the app uses its values only when 1 (Settings -> Grid meter)
  int meter = 1;
  // 0.9.2 (appended): remote firmware update request from the app: version code (0.9.3 -> 903) and request time
  uint32_t ota = 0, otaAt = 0;
};

struct AlertEvent {
  uint32_t ts = 0;        // UTC epoch (0 if the clock was not set yet; transport may fix it)
  uint32_t upMs = 0;
  char type[24] = "";     // INVERTER_FAULT, GRID_LOSS, GRID_RESTORED, ...
  uint8_t sev = 0;        // 0..5
  char st[8] = "";        // "open" | "clear" | "info"
  char msg[200] = "";
  uint32_t dur = 0;       // seconds (clear events)
  uint8_t pushPrio = 0;   // 0 = log only (quiet hours / cap); else ntfy priority 1..5
  bool telegram = false;  // also send to Telegram (every pushed alert since 0.7.7)
  bool repeat = false;    // re-notification of a still-active alert (transport may skip the RTDB write)
  // r8 §3.6 / §4.2 additions (all empty = the old behaviour)
  char rid[4] = "";       // rule slot "r01".."r16" for type RULE / RULE_INVALID
  float val = NAN, thr = NAN;  // value and threshold in canonical units
  char em[6] = "";        // "" (not for e-mail) | "q" | "cool" | "rcap" | "cap" | "skip"
  uint8_t rc = 0;         // recipient mask 1..7 (0 = everyone on alerts/email/to)
  char kind = 'i';        // event-key suffix: o(pen) c(lear) r(epeat) i(nfo)
  bool planned = false;   // 0.8.1: the owner's planned grid-off (GRID_LOSS / GRID_RESTORED), marked in Telegram
};

// Bounded queue (24). When full, the oldest sev <= 2 entry is dropped first (d1 §7.3).
class AlertQueue {
 public:
  void setLock(ILock* l) { lock_ = l; }
  bool push(const AlertEvent& e);   // false if something had to be dropped
  bool pop(AlertEvent& out);        // oldest first
  bool peek(AlertEvent& out);
  int size();
  uint32_t dropped() const { return dropped_; }

 private:
  void L() { if (lock_) lock_->lock(); }
  void U() { if (lock_) lock_->unlock(); }
  AlertEvent q_[cfg::ALERT_QUEUE_DEPTH];
  int head_ = 0, count_ = 0;
  uint32_t dropped_ = 0;
  ILock* lock_ = nullptr;
};

class AlertEngine {
 public:
  enum class RState : uint8_t { IDLE, PENDING, ACTIVE, CLEARING };
  struct Rule {
    const char* type;
    const char* clearType;  // e.g. GRID_RESTORED; nullptr = same type
    uint8_t sev, clearSev;
    uint32_t holdOnMs, holdOffMs;
    uint8_t minConsecutive;  // >0: count of consecutive true evaluations instead of holdOnMs
    RState st = RState::IDLE;
    uint32_t since = 0, activeSince = 0, lastNotify = 0;
    uint8_t consec = 0;
    bool openMailed = false;  // the open of the current episode was queued for e-mail (r8 §4.7)
    bool planned = false;     // 0.7.9: this episode is the planned grid-off: labelled, alerted again if it overruns
    bool quiet = false;       // 0.8.1: its open is log-only (a boot inside the window: the cut started unseen)
    bool noRepeat = false;    // 0.7.9: never "Still active" re-notifications (GRID_LOSS)
    bool openPushed = false;  // 0.8.1: the open of the current episode was pushed (ntfy / Telegram)
    bool clearFollows = false;  // 0.8.1: the clear is pushed past quiet hours when its open was (GRID_RESTORED)
  };
  enum RuleId { R_FAULT = 0, R_BATT_CRIT, R_BATT_LOW, R_BATT_HIGH, R_GRID_LOSS, R_GRID_ABN, R_OVERLOAD, R_OVER_TEMP,
                R_PV_OVERVOLT, R_LINK_LOST, R_COUNT };

  explicit AlertEngine(AlertQueue& q, NotifyGates* gates = nullptr);
  void setCfg(const AlertCfg& c) {
    cfg_ = c;
    gates_->setQuiet(c.quietFrom, c.quietTo);
  }
  void setEmailPolicy(const rules::EmailPolicy& p) { policy_ = p; }
  // 0.7.9: an outage that was open (and notified) before a reboot: resume it silently instead of a second GRID_LOSS
  void resumeOutage(uint32_t openEpoch) { resumeEpoch_ = openEpoch; }
  bool plannedGridOff(uint32_t epoch) const;    // inside the owner's planned grid-off window (+- grace)
  bool nearPlannedStart(uint32_t epoch) const;  // within +- grace of the window start (the scheduled cut)
  const rules::EmailPolicy& emailPolicy() const { return policy_; }
  NotifyGates& gates() { return *gates_; }
  const AlertCfg& cfg() const { return cfg_; }
  void setInputRangeUps(bool ups) { ups_ = ups; }  // QPIRI field 15: 1 = UPS (170 V threshold)

  // Evaluate one valid sample (lk = 1).
  void evaluate(const Sample& s, uint32_t nowMs, uint32_t epoch);
  // Call ~1 Hz: LINK_LOST, PV_LOW_DAY window, DAILY_SUMMARY, day rollover of counters.
  void tick(uint32_t nowMs, uint32_t epoch, bool linkExpected, bool linkOk, const mrec::Daily* today);
  // One-off events (LOGGER_REBOOT, DATA_DROPPED, QUOTA_GUARD, BACKLOG_STUCK, TEST_ALERT ...)
  void raise(const char* type, uint8_t sev, const char* msg, uint32_t nowMs, uint32_t epoch, bool force = false);
  // 0.8.0: an info event that is only logged (RTDB events): no push, Telegram or e-mail (e.g. a planned restart)
  void raiseQuiet(const char* type, uint8_t sev, const char* msg, uint32_t nowMs, uint32_t epoch);

  const Rule& rule(int i) const { return rules_[i]; }
  int pushesToday() const { return gates_->pushesToday(); }
  // counters for daily nf / nw (taken by aggTask)
  int takeFaultOpens() { int v = faultOpens_; faultOpens_ = 0; return v; }
  int takeWarnOpens() { int v = warnOpens_; warnOpens_ = 0; return v; }
  bool anyActive() const;

 private:
  void update(Rule& r, bool cond, bool clearCond, uint32_t nowMs, uint32_t epoch, const char* openMsg,
              const char* clearMsg);
  void emit(const char* type, uint8_t sev, const char* st, const char* msg, uint32_t dur, uint32_t nowMs,
            uint32_t epoch, bool repeat, bool force, Rule* r = nullptr);
  void rollDay(uint32_t epoch);

  AlertQueue& q_;
  NotifyGates ownGates_;
  NotifyGates* gates_;
  rules::EmailPolicy policy_;
  AlertCfg cfg_;
  uint32_t resumeEpoch_ = 0;
  bool quietNext_ = false;  // raiseQuiet(): the next emit is log-only
  Rule rules_[R_COUNT];
  bool ups_ = false;
  int32_t day_ = 0;
  int faultOpens_ = 0, warnOpens_ = 0;
  // PV_LOW_DAY
  uint32_t pvLowSinceMs_ = 0;
  bool pvLowTracking_ = false, pvLowSentToday_ = false;
  bool summarySentToday_ = false;
  uint32_t lastValidMs_ = 0;
  bool hadValid_ = false;
};
