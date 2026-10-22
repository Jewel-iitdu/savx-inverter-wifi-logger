// RuleEngine.cpp - see RuleEngine.h and research/r8-email-alerts.md §4.4-4.8.
#include "agg/RuleEngine.h"

#include <stdio.h>
#include <string.h>

#include "agg/Aggregator.h"
#include "agg/RuleParse.h"
#include "model/TimeUtil.h"

using rules::Def;
using rules::Metric;
using rules::Mode;
using rules::Op;
using rules::Scope;
using rules::St;

// ============================================================================================ static helpers
float RuleEngine::instValue(Metric m, const Sample& s) {
  switch (m) {
    case Metric::W: return s.w;
    case Metric::PVW: return s.pvw;
    case Metric::GW: {  // the `egr` integrand (d1 §4.4): load + battery - PV while the grid feeds the output
      if (isnan(s.gv) && !s.wf[0]) return NAN;
      return (float)Aggregator::gridW(s);
    }
    case Metric::BDW: return isnan(s.bw) ? NAN : (s.bw < 0 ? -s.bw : 0.f);
    case Metric::BW: return s.bw;
    case Metric::SOC: return s.soc;
    case Metric::LD: return s.ld;
    case Metric::BV: return s.bv;
    case Metric::GV: return s.gv;
    case Metric::OV: return s.ov;
    case Metric::PVV: return s.pvv;
    case Metric::BDI: return s.bdi;
    case Metric::TMP: return s.tmpRaw ? NAN : s.tmp;
    case Metric::GF: return s.gf;
    case Metric::ONBAT: return s.md ? (s.md == 'B' ? 1.f : 0.f) : NAN;
    case Metric::NOGRID:
      if (isnan(s.gv) && !s.wf[0]) return NAN;
      return Aggregator::gridPresent(s) ? 0.f : 1.f;
    default: return NAN;
  }
}

static void thousands(long v, char* out, size_t n) {
  char t[24];
  snprintf(t, sizeof t, "%ld", v < 0 ? -v : v);
  size_t l = strlen(t), o = 0;
  char b[32];
  if (v < 0) b[o++] = '-';
  for (size_t i = 0; i < l; i++) {
    b[o++] = t[i];
    size_t rest = l - 1 - i;
    if (rest && rest % 3 == 0) b[o++] = ',';
  }
  b[o] = 0;
  snprintf(out, n, "%s", b);
}

void RuleEngine::fmtDuration(uint32_t s, char* out, size_t n) {
  if (s < 60) snprintf(out, n, "%lu s", (unsigned long)s);
  else if (s < 3600) snprintf(out, n, "%lu min", (unsigned long)(s / 60));
  else if ((s % 3600) / 60) snprintf(out, n, "%lu h %lu min", (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
  else snprintf(out, n, "%lu h", (unsigned long)(s / 3600));
}

size_t RuleEngine::fmtValue(Metric m, float v, char* out, size_t n) {
  if (isnan(v)) {
    snprintf(out, n, "-");
    return strlen(out);
  }
  char t[32];
  switch (rules::info(m).unit) {
    case rules::Unit::W: thousands(lroundf(v), t, sizeof t); snprintf(out, n, "%s W", t); break;
    case rules::Unit::WH:
      if (fabsf(v) >= 1000.f) snprintf(out, n, "%.2f kWh", v / 1000.0);
      else snprintf(out, n, "%ld Wh", lroundf(v));
      break;
    case rules::Unit::V: snprintf(out, n, "%.1f V", v); break;
    case rules::Unit::HZ: snprintf(out, n, "%.1f Hz", v); break;
    case rules::Unit::PCT: snprintf(out, n, "%ld %%", lroundf(v)); break;
    case rules::Unit::A: snprintf(out, n, "%ld A", lroundf(v)); break;
    case rules::Unit::DEGC: snprintf(out, n, "%ld \xC2\xB0" "C", lroundf(v)); break;
    case rules::Unit::MIN: fmtDuration((uint32_t)(v < 0 ? 0 : lroundf(v)) * 60u, out, n); break;
    case rules::Unit::BOOL: snprintf(out, n, "%s", v >= 0.5f ? "yes" : "no"); break;
  }
  return strlen(out);
}

bool RuleEngine::inWindow(const Def& d, uint32_t ep) {
  if (!d.hasWin) return true;
  if (!tu::epochValid(ep)) return false;
  int32_t day = tu::localDayNumber(ep);
  unsigned mm = tu::localMinuteOfDay(ep);
  auto bit = [&](int32_t dn) { return ((d.days >> dow(dn)) & 1u) != 0; };
  if (d.winFrom < d.winTo) return mm >= d.winFrom && mm < d.winTo && bit(day);
  return (mm >= d.winFrom && bit(day)) || (mm < d.winTo && bit(day - 1));  // crosses midnight: start day's bit
}

bool RuleEngine::cond(const Def& d, float v) {
  if (isnan(v)) return false;
  if (rules::info(d.metric).unit == rules::Unit::BOOL) return (v >= 0.5f) == (d.value >= 0.5f);
  switch (d.op) {
    case Op::GT: return v > d.value;
    case Op::GE: return v >= d.value;
    case Op::LT: return v < d.value;
    case Op::LE: return v <= d.value;
  }
  return false;
}

float RuleEngine::clrLevel(const Def& d) {
  const rules::MetricInfo& mi = rules::info(d.metric);
  if (mi.unit == rules::Unit::BOOL) return d.value >= 0.5f ? 0.f : 1.f;  // booleans clear on the opposite value
  if (d.hasClr) return d.clr;
  float h = mi.hystAbs;
  float p = mi.hystPct * fabsf(d.value);
  if (p > h) h = p;
  return (d.op == Op::GT || d.op == Op::GE) ? d.value - h : d.value + h;
}

bool RuleEngine::clrCond(const Def& d, float v) {
  if (isnan(v)) return false;  // never clear on missing data
  if (rules::info(d.metric).unit == rules::Unit::BOOL) return (v >= 0.5f) != (d.value >= 0.5f);
  float c = clrLevel(d);
  return (d.op == Op::GT || d.op == Op::GE) ? v <= c : v >= c;
}

// ============================================================================================ setup
uint8_t RuleEngine::enabled() const {
  uint8_t n = 0;
  for (int i = 0; i < cfg::MAX_RULES; i++)
    if (set_.d[i].present && set_.d[i].on && !set_.d[i].invalid) n++;
  return n;
}

bool RuleEngine::active(int i) const {
  const Def& d = set_.d[i];
  return d.present && d.on && !d.invalid;
}

bool RuleEngine::needsTime(const Def& d) const { return d.hasWin || d.mode == Mode::AT || d.scope != Scope::INST; }

void RuleEngine::restore(int i, uint32_t ep) {
  RS& r = rs_[i];
  r.restored = true;
  RulePersist p;
  if (!persist_ || !persist_->load((uint8_t)i, p)) return;
  // the e-mail/day guard and the cooldown survive even a rule edit (they protect the inbox, not the semantics)
  r.lastMailEp = p.lastMailEp;
  r.cnt = p.cnt;
  r.evCnt = p.evCnt;
  r.cntDay = p.cntDay;
  if (p.semHash != set_.d[i].semHash) return;
  r.openMailed = p.flags & 1;
  r.deferredOpen = (p.flags & 2) != 0;
  r.pk = p.pk;
  r.sinceEp = p.sinceEp;
  r.openEp = p.openEp;
  switch ((St)p.st) {
    case St::LATCH:
    case St::DONE: r.st = (St)p.st; break;  // the period check in prelude() releases it when the key changed
    case St::FIRE:
    case St::CLR:
      r.st = St::FIRE;      // unconfirmed: no new OPEN e-mail; clears normally on valid data
      r.unconfirmed = true;
      r.fireMs = 0;
      break;
    default: break;  // PEND is not restored: the hold restarts
  }
  (void)ep;
}

void RuleEngine::setRules(const rules::Set& s, uint32_t ep) {
  bool timeOk = tu::epochValid(ep);
  prev_ = set_;
  const rules::Set& old = prev_;
  set_ = s;
  for (int i = 0; i < cfg::MAX_RULES; i++) {
    const Def& nd = s.d[i];
    const Def& od = old.d[i];
    RS& r = rs_[i];
    bool wasActive = r.st == St::FIRE || r.st == St::CLR;
    if (!nd.present) {
      if (od.present) {
        if (wasActive && !od.invalid) {
          set_.d[i] = od;  // format the message with the old definition
          info(i, "rule deleted while active", 0, ep, timeOk);
          set_.d[i] = nd;
        }
        double inv = r.invRev;
        r = RS();
        r.invRev = inv;
        r.removed = true;
        r.restored = true;      // a rule added to this slot later starts clean ...
        r.dirty = true;
        r.persistDirty = true;  // ... and so does the next boot (the NVS record is overwritten)
      }
      continue;
    }
    if (nd.invalid) {
      if (r.invRev != s.rev) {
        r.invRev = s.rev;
        char msg[200];
        snprintf(msg, sizeof msg, "Rule %s ignored by the logger: %s", nd.rid, nd.err);
        emit(i, "RULE_INVALID", "info", 'i', 2, msg, "", NAN, NAN, 0, 0, ep, timeOk, false);
      }
      r.st = St::INV;
      r.removed = false;
      mark(i, false);
      continue;
    }
    r.removed = false;
    bool firstSeen = !r.restored;
    if (firstSeen) restore(i, ep);  // once per boot and slot: may put the slot back into LATCH/DONE/FIRE
    bool sameSem = od.present && !od.invalid && od.semHash == nd.semHash;
    if (!nd.on) {
      if (wasActive) info(i, "rule turned off", 0, ep, timeOk);
      if (r.st != St::OFF) {
        r.st = St::OFF;
        r.openMailed = r.deferredOpen = r.unconfirmed = false;
        mark(i, true);
      }
      continue;
    }
    if (firstSeen || r.st == St::OFF || r.st == St::INV) {  // newly loaded, enabled or valid
      if (r.st == St::OFF || r.st == St::INV) r.st = St::IDLE;
      mark(i, false);
      continue;
    }
    if (!sameSem) {
      if (wasActive) info(i, "rule changed while active", 0, ep, timeOk);
      r.st = St::IDLE;
      r.openMailed = r.deferredOpen = r.unconfirmed = false;
      r.pk = 0;
      mark(i, true);
    }
    // cosmetic edit (name, channels, rc, sev, coolMin, maxDay...): keep the state
  }
  flushPersist();
}

// ============================================================================================ evaluation
void RuleEngine::dayRoll(int i, uint32_t ep) {
  RS& r = rs_[i];
  int32_t d = tu::localDayNumber(ep);
  if (r.cntDay != d) {
    r.cntDay = d;
    r.cnt = 0;
    r.evCnt = 0;
  }
}

uint32_t RuleEngine::periodKey(const Def& d, uint32_t ep, const EnergyView& e) const {
  if (d.scope == Scope::MONTH) return e.mCycleKey;
  tu::Local l = tu::toLocal(ep);
  return (uint32_t)l.year * 10000u + l.month * 100u + l.day;
}

float RuleEngine::energyValue(const Def& d, const EnergyView& e, uint32_t ep, bool timeOk) const {
  if (!timeOk || e.day != tu::localDayNumber(ep)) return NAN;  // rollover guard: yesterday must not re-fire
  bool m = d.scope == Scope::MONTH;
  switch (d.metric) {
    case Metric::ELD: return (float)(m ? e.mEld : e.eld);
    case Metric::EGR: return (float)(m ? e.mEgr : e.egr);
    case Metric::EPV: return (float)(m ? e.mEpv : e.epv);
    case Metric::EDIS: return (float)(m ? e.mEdis : e.edis);
    case Metric::ECH: return (float)(m ? e.mEch : e.ech);
    case Metric::GOUT: {
      int g = m ? e.mGoutMin : e.goutMin;
      return g < 0 ? NAN : (float)g;
    }
    default: return NAN;
  }
}

bool RuleEngine::prelude(int i, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e) {
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  (void)ms;
  if (needsTime(d) && !timeOk) {
    if (r.st == St::IDLE || r.st == St::PEND) {
      r.st = St::WAIT;
      r.sinceEp = 0;
      mark(i, false);
    }
    return false;
  }
  if (r.st == St::WAIT) {
    r.st = St::IDLE;
    r.sinceEp = timeOk ? ep : 0;
    mark(i, false);
  }
  if (timeOk) dayRoll(i, ep);
  if (r.st == St::LATCH || r.st == St::DONE) {
    if (!timeOk) return false;
    uint32_t pk = periodKey(d, ep, e);
    if (!pk || pk == r.pk) return false;
    r.st = St::IDLE;  // silent: a new day / billing cycle
    r.sinceEp = ep;
    r.openMailed = r.deferredOpen = false;
    mark(i, true);
  }
  return true;
}

void RuleEngine::onSample(const Sample& s, const EnergyView& e, uint32_t ms, uint32_t ep, bool timeOk) {
  for (int m = 0; m < (int)Metric::COUNT; m++) inst_[m] = rules::info((Metric)m).energy ? NAN : instValue((Metric)m, s);
  instMs_ = ms;
  haveInst_ = true;
  for (int i = 0; i < cfg::MAX_RULES; i++) {
    if (!active(i)) continue;
    const Def& d = set_.d[i];
    if (d.scope == Scope::INST && d.mode == Mode::HOLD) stepHold(i, true, inst_[(int)d.metric], ms, ep, timeOk, e);
  }
  flushPersist();
}

void RuleEngine::tick(const EnergyView& e, uint32_t ms, uint32_t ep, bool timeOk) {
  for (int i = 0; i < cfg::MAX_RULES; i++) {
    if (!active(i)) continue;
    const Def& d = set_.d[i];
    if (d.mode == Mode::AT) {
      float v;
      if (rules::info(d.metric).energy) v = energyValue(d, e, ep, timeOk);
      else v = (haveInst_ && (uint32_t)(ms - instMs_) <= cfg::RULE_NAN_GRACE_MS) ? inst_[(int)d.metric] : NAN;
      stepAt(i, v, ms, ep, timeOk, e);
    } else if (d.scope != Scope::INST) {
      stepHold(i, true, energyValue(d, e, ep, timeOk), ms, ep, timeOk, e);
    } else {
      stepHold(i, false, NAN, ms, ep, timeOk, e);  // no new data: grace, window end, repeats
    }
  }
  flushPersist();
}

void RuleEngine::stepHold(int i, bool fresh, float v, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e) {
  if (!prelude(i, ms, ep, timeOk, e)) return;
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  bool inWin = !d.hasWin || inWindow(d, ep);
  bool valid = fresh && !isnan(v);
  if (valid) {
    r.lastValidMs = ms;
    r.haveValid = true;
    r.lastV = v;
  }
  switch (r.st) {
    case St::IDLE:
      if (valid && inWin && cond(d, v)) {
        if (d.forSec == 0) {
          open(i, v, ms, ep, timeOk, e, false, false);
        } else {
          r.st = St::PEND;
          r.t0 = ms;
          r.sinceEp = timeOk ? ep : 0;
          r.val = v;
          mark(i, false);
        }
      }
      break;
    case St::PEND:
      if (!inWin || (valid && !cond(d, v))) {
        toIdle(i, ep, timeOk);
        break;
      }
      if (!valid) {  // missing data freezes PEND for up to 30 s
        if (!r.haveValid || (uint32_t)(ms - r.lastValidMs) > cfg::RULE_NAN_GRACE_MS) toIdle(i, ep, timeOk);
        break;
      }
      if ((uint32_t)(ms - r.t0) >= d.forSec * 1000UL) open(i, v, ms, ep, timeOk, e, false, false);
      break;
    case St::FIRE:
    case St::CLR:
      if (d.hasWin && !inWin) {
        info(i, "window ended while active", ms, ep, timeOk);
        toIdle(i, ep, timeOk);
        break;
      }
      if (r.st == St::FIRE) {
        if (r.fireMs == 0) {  // restored FIRE: "held" and the repeat spacing count from now
          r.fireMs = ms ? ms : 1;
          r.lastRepMs = ms;
        }
        if (valid && clrCond(d, v)) {
          r.st = St::CLR;
          r.t0 = ms;
          mark(i, false);
          break;
        }
        // repeat while active: spacing max(repeatMin, coolMin); one deferred REPEAT after a cooldown-blocked open
        uint32_t spacing = (uint32_t)(d.repeatMin > d.coolMin ? d.repeatMin : d.coolMin) * 60000UL;
        bool rep = d.repeatMin > 0 && (uint32_t)(ms - r.lastRepMs) >= spacing;
        bool deferred = r.deferredOpen && timeOk && r.lastMailEp && ep >= r.lastMailEp + (uint32_t)d.coolMin * 60u;
        if (rep || deferred) repeat(i, ms, ep, timeOk);
      } else {  // CLR
        if (valid && !clrCond(d, v)) {
          r.st = St::FIRE;  // back above the clear level: no re-open
          mark(i, false);
          break;
        }
        if (valid && (uint32_t)(ms - r.t0) >= d.clrSec * 1000UL) clear(i, v, ms, ep, timeOk);
      }
      break;
    default:
      break;
  }
}

void RuleEngine::stepAt(int i, float v, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e) {
  if (!prelude(i, ms, ep, timeOk, e)) return;
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  if (r.st != St::IDLE) return;
  unsigned mm = tu::localMinuteOfDay(ep);
  if (mm < d.atMin) return;
  if (d.hasWin && !inWindow(d, ep)) {  // not an active day/time for this rule: checked = done, silently
    r.st = St::DONE;
    r.pk = periodKey(d, ep, e);
    r.sinceEp = ep;
    mark(i, true);
    return;
  }
  if (isnan(v)) return;  // wait for data (until the day ends)
  bool late = mm > (unsigned)d.atMin + 2;
  bool gtge = d.op == Op::GT || d.op == Op::GE;
  if (rules::info(d.metric).energy && !gtge && e.covPct < d.minCov) {
    char msg[200];
    snprintf(msg, sizeof msg, "%s: not checked: coverage %u %% < %u %%", d.name, (unsigned)e.covPct, (unsigned)d.minCov);
    rules::utf8Trim(msg);
    emit(i, "RULE", "info", 'i', 1, msg, (d.email && set_.email.on) ? "skip" : "", v, d.value, 0, ms, ep, timeOk, false);
    r.st = St::DONE;
    r.pk = periodKey(d, ep, e);
    r.sinceEp = ep;
    r.val = v;
    mark(i, true);
    return;
  }
  if (cond(d, v)) {
    open(i, v, ms, ep, timeOk, e, true, late);
  } else {
    r.st = St::DONE;
    r.pk = periodKey(d, ep, e);
    r.sinceEp = ep;
    r.val = v;
    mark(i, true);
  }
}

// ============================================================================================ transitions
const char* RuleEngine::gateMail(int i, uint32_t ep, bool timeOk) {
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  if (!d.email || !set_.email.on) return "";
  if (r.lastMailEp) {  // cooldown (without a clock we cannot prove it passed: be conservative)
    if (!timeOk || ep < r.lastMailEp + (uint32_t)d.coolMin * 60u) return "cool";
  }
  if (r.cnt >= d.maxDay) return "rcap";
  if (!gates_.takeMailJob(d.sev, ep)) return "cap";
  r.cnt++;
  r.lastMailEp = timeOk ? ep : 0;
  return "q";
}

bool RuleEngine::emit(int i, const char* type, const char* st, char kind, uint8_t sev, const char* msg, const char* em,
                      float val, float thr, uint32_t dur, uint32_t ms, uint32_t ep, bool timeOk, bool push) {
  const Def& d = set_.d[i];
  AlertEvent e;
  e.ts = timeOk ? ep : 0;
  e.upMs = ms;
  snprintf(e.type, sizeof e.type, "%s", type);
  e.sev = sev;
  snprintf(e.st, sizeof e.st, "%s", st);
  rules::copyUtf8(e.msg, sizeof e.msg, msg);
  e.dur = dur;
  snprintf(e.rid, sizeof e.rid, "%s", d.rid[0] ? d.rid : "");
  e.val = val;
  e.thr = thr;
  snprintf(e.em, sizeof e.em, "%s", em ? em : "");
  e.rc = d.rc;
  e.kind = kind;
  e.repeat = kind == 'r';
  if (push && d.ntfy) {
    e.pushPrio = gates_.pushPrio(sev, kind == 'c', ep, false);
    e.telegram = e.pushPrio > 0;  // 0.7.7: every pushed rule alert
  }
  q_.push(e);
  return true;
}

void RuleEngine::open(int i, float v, uint32_t ms, uint32_t ep, bool timeOk, const EnergyView& e, bool atMode,
                      bool late) {
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  r.st = St::FIRE;
  r.fireMs = ms ? ms : 1;
  r.lastRepMs = ms;
  r.sinceEp = r.openEp = timeOk ? ep : 0;
  r.val = v;
  r.unconfirmed = r.deferredOpen = r.openMailed = false;
  const rules::MetricInfo& mi = rules::info(d.metric);
  char vs[32], ts[32], msg[200];
  fmtValue(d.metric, v, vs, sizeof vs);
  fmtValue(d.metric, d.value, ts, sizeof ts);
  if (atMode) {
    tu::Local l = tu::toLocal(ep);
    snprintf(msg, sizeof msg, "%s: %s at %02u:%02u (limit %s%s)%s", d.name, vs, l.hour, l.minute, ts,
             mi.est ? ", est." : "", late ? ", checked late" : "");
  } else {
    const char* sc = d.scope == Scope::DAY ? " today" : d.scope == Scope::MONTH ? " this cycle" : "";
    snprintf(msg, sizeof msg, "%s: now %s (limit %s%s%s)", d.name, vs, ts, sc, mi.est ? ", est." : "");
    if (d.scope == Scope::MONTH && e.mCovPct < 95) {
      size_t l = strlen(msg);
      snprintf(msg + l, sizeof msg - l, " (cycle data %u %%)", (unsigned)e.mCovPct);
    }
  }
  rules::utf8Trim(msg);
  if (r.evCnt < cfg::RULE_EVENTS_PER_DAY) {
    r.evCnt++;
    const char* em = gateMail(i, ep, timeOk);
    if (!strcmp(em, "cool")) r.deferredOpen = true;
    if (!strcmp(em, "q")) r.openMailed = true;
    emit(i, "RULE", "open", 'o', d.sev, msg, em, v, d.value, 0, ms, ep, timeOk, true);
  }
  if (d.scope != Scope::INST || d.mode == Mode::AT) {
    r.st = St::LATCH;  // energy / once-a-day rules: no clear or repeat until the period key changes
    r.pk = periodKey(d, ep, e);
  }
  mark(i, true);
}

void RuleEngine::repeat(int i, uint32_t ms, uint32_t ep, bool timeOk) {
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  char vs[32], held[32], msg[200];
  fmtValue(d.metric, r.lastV, vs, sizeof vs);
  fmtDuration((ms - r.fireMs) / 1000, held, sizeof held);
  snprintf(msg, sizeof msg, "%s: still %s for %s", d.name, vs, held);
  rules::utf8Trim(msg);
  r.lastRepMs = ms;
  r.deferredOpen = false;
  if (r.evCnt < cfg::RULE_EVENTS_PER_DAY) {
    r.evCnt++;
    const char* em = gateMail(i, ep, timeOk);
    if (!strcmp(em, "q")) r.openMailed = true;
    emit(i, "RULE", "open", 'r', d.sev, msg, em, r.lastV, d.value, 0, ms, ep, timeOk, true);
  }
  mark(i, true);
}

void RuleEngine::clear(int i, float v, uint32_t ms, uint32_t ep, bool timeOk) {
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  char vs[32], cs[32], msg[200];
  fmtValue(d.metric, v, vs, sizeof vs);
  fmtValue(d.metric, clrLevel(d), cs, sizeof cs);
  snprintf(msg, sizeof msg, "%s: back to %s (clear level %s)", d.name, vs, cs);
  rules::utf8Trim(msg);
  uint32_t dur = r.fireMs ? (ms - r.fireMs) / 1000 : 0;
  if (r.evCnt < cfg::RULE_EVENTS_PER_DAY) {
    r.evCnt++;
    const char* em = "";
    if (d.email && set_.email.on && d.clrMail && r.openMailed)  // clear mails skip cooldown and maxDay, not the cap
      em = gates_.takeMailJob(d.sev, ep) ? "q" : "cap";
    emit(i, "RULE", "clear", 'c', d.sev, msg, em, v, clrLevel(d), dur, ms, ep, timeOk, true);
  }
  r.st = St::IDLE;
  r.sinceEp = timeOk ? ep : 0;
  r.val = v;
  r.openMailed = r.deferredOpen = r.unconfirmed = false;
  mark(i, true);
}

void RuleEngine::toIdle(int i, uint32_t ep, bool timeOk) {
  RS& r = rs_[i];
  bool wasActive = r.st == St::FIRE || r.st == St::CLR;
  r.st = St::IDLE;
  r.sinceEp = timeOk ? ep : 0;
  if (wasActive) r.openMailed = r.deferredOpen = r.unconfirmed = false;
  mark(i, wasActive);
}

void RuleEngine::info(int i, const char* what, uint32_t ms, uint32_t ep, bool timeOk) {
  const Def& d = set_.d[i];
  RS& r = rs_[i];
  if (r.evCnt >= cfg::RULE_EVENTS_PER_DAY) return;
  r.evCnt++;
  char msg[200];
  snprintf(msg, sizeof msg, "%s: %s", d.name, what);
  rules::utf8Trim(msg);
  emit(i, "RULE", "info", 'i', 1, msg, "", r.lastV, NAN, 0, ms, ep, timeOk, false);
}

// ============================================================================================ output
void RuleEngine::flushPersist() {
  if (!persist_) {
    for (int i = 0; i < cfg::MAX_RULES; i++) rs_[i].persistDirty = false;
    return;
  }
  for (int i = 0; i < cfg::MAX_RULES; i++) {
    RS& r = rs_[i];
    if (!r.persistDirty) continue;
    r.persistDirty = false;
    RulePersist p;
    memset(&p, 0, sizeof p);
    p.semHash = set_.d[i].present ? set_.d[i].semHash : 0;
    p.sinceEp = r.sinceEp;
    p.lastMailEp = r.lastMailEp;
    p.openEp = r.openEp;
    p.pk = r.pk;
    p.cntDay = r.cntDay;
    p.st = (uint8_t)r.st;
    p.cnt = r.cnt;
    p.evCnt = r.evCnt;
    p.flags = (uint8_t)((r.openMailed ? 1 : 0) | (r.deferredOpen ? 2 : 0));
    persist_->save((uint8_t)i, p);
  }
}

bool RuleEngine::takeDirty(AlertStateOut& o) {
  for (int k = 0; k < cfg::MAX_RULES; k++) {
    int i = (dirtyCursor_ + k) % cfg::MAX_RULES;
    RS& r = rs_[i];
    if (!r.dirty) continue;
    r.dirty = false;
    dirtyCursor_ = (i + 1) % cfg::MAX_RULES;
    o = AlertStateOut();
    snprintf(o.rid, sizeof o.rid, "r%02d", i + 1);
    o.removed = r.removed;
    o.st = r.st;
    o.since = r.sinceEp;
    o.last = r.lastMailEp;
    o.pk = (r.st == St::LATCH || r.st == St::DONE) ? r.pk : 0;
    o.val = r.val;
    o.cnt = r.cnt;
    if (r.st == St::INV) rules::copyUtf8(o.err, sizeof o.err, set_.d[i].err);
    return true;
  }
  return false;
}
