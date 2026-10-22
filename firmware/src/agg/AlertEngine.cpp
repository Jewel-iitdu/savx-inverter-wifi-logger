// AlertEngine.cpp - see AlertEngine.h.
#include "agg/AlertEngine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "codes.h"
#include "model/TimeUtil.h"

// ---------------------------------------------------------------------------------------------- queue
bool AlertQueue::push(const AlertEvent& e) {
  L();
  bool clean = true;
  if (count_ == cfg::ALERT_QUEUE_DEPTH) {
    clean = false;
    dropped_++;
    // drop the oldest sev <= 2 entry; if none, the oldest entry
    int victim = 0;
    for (int i = 0; i < count_; i++) {
      if (q_[(head_ + i) % cfg::ALERT_QUEUE_DEPTH].sev <= 2) { victim = i; break; }
    }
    for (int i = victim; i < count_ - 1; i++)
      q_[(head_ + i) % cfg::ALERT_QUEUE_DEPTH] = q_[(head_ + i + 1) % cfg::ALERT_QUEUE_DEPTH];
    count_--;
  }
  q_[(head_ + count_) % cfg::ALERT_QUEUE_DEPTH] = e;
  count_++;
  U();
  return clean;
}

bool AlertQueue::pop(AlertEvent& out) {
  L();
  bool ok = count_ > 0;
  if (ok) {
    out = q_[head_];
    head_ = (head_ + 1) % cfg::ALERT_QUEUE_DEPTH;
    count_--;
  }
  U();
  return ok;
}

bool AlertQueue::peek(AlertEvent& out) {
  L();
  bool ok = count_ > 0;
  if (ok) out = q_[head_];
  U();
  return ok;
}

int AlertQueue::size() {
  L();
  int n = count_;
  U();
  return n;
}

// ---------------------------------------------------------------------------------------------- engine
static AlertEngine::Rule mk(const char* type, const char* clearType, uint8_t sev, uint8_t clearSev, uint32_t on,
                            uint32_t off, uint8_t consec) {
  AlertEngine::Rule r;
  r.type = type;
  r.clearType = clearType;
  r.sev = sev;
  r.clearSev = clearSev;
  r.holdOnMs = on;
  r.holdOffMs = off;
  r.minConsecutive = consec;
  return r;
}

AlertEngine::AlertEngine(AlertQueue& q, NotifyGates* gates) : q_(q), gates_(gates ? gates : &ownGates_) {
  gates_->setQuiet(cfg_.quietFrom, cfg_.quietTo);
  // d1 §7.2 trigger/clear holds
  rules_[R_FAULT] = mk("INVERTER_FAULT", nullptr, 5, 5, 0, 60000, 2);   // 2 consecutive polls / 60 s
  rules_[R_BATT_CRIT] = mk("BATT_CRITICAL", nullptr, 5, 5, 30000, 300000, 0);
  rules_[R_BATT_LOW] = mk("BATT_LOW", nullptr, 4, 4, 60000, 300000, 0);
  rules_[R_BATT_HIGH] = mk("BATT_HIGH", nullptr, 4, 4, 30000, 300000, 0);
  rules_[R_GRID_LOSS] = mk("GRID_LOSS", "GRID_RESTORED", 4, 3, 10000, 30000, 0);
  rules_[R_GRID_LOSS].noRepeat = true;  // 0.7.9: one message when it goes, one when it comes back
  rules_[R_GRID_ABN] = mk("GRID_ABNORMAL", nullptr, 2, 2, 300000, 300000, 0);
  rules_[R_OVERLOAD] = mk("OVERLOAD", nullptr, 4, 4, 30000, 60000, 0);
  rules_[R_OVER_TEMP] = mk("OVER_TEMP", nullptr, 4, 4, 30000, 300000, 0);
  rules_[R_PV_OVERVOLT] = mk("PV_OVERVOLT", nullptr, 4, 4, 10000, 300000, 0);
  rules_[R_LINK_LOST] = mk("LINK_LOST", nullptr, 3, 3, 0, 0, 0);
}

bool AlertEngine::anyActive() const {
  for (int i = 0; i < R_COUNT; i++)
    if (rules_[i].st == RState::ACTIVE || rules_[i].st == RState::CLEARING) return true;
  return false;
}

void AlertEngine::rollDay(uint32_t epoch) {
  if (!tu::epochValid(epoch)) return;
  int32_t d = tu::localDayNumber(epoch);
  if (d == day_) return;
  day_ = d;
  pvLowSentToday_ = false;
  summarySentToday_ = false;
}

void AlertEngine::emit(const char* type, uint8_t sev, const char* st, const char* msg, uint32_t dur, uint32_t nowMs,
                       uint32_t epoch, bool repeat, bool force, Rule* r) {
  AlertEvent e;
  e.ts = tu::epochValid(epoch) ? epoch : 0;
  e.upMs = nowMs;
  snprintf(e.type, sizeof e.type, "%s", type);
  e.sev = sev;
  snprintf(e.st, sizeof e.st, "%s", st);
  bool plannedOff = r && r->quiet;           // planned grid-off (0.7.9): log only
  bool quietEv = plannedOff || quietNext_;    // raiseQuiet() (0.8.0): log only
  snprintf(e.msg, sizeof e.msg, "%s%s%s", repeat ? "Still active: " : "", msg, plannedOff ? " (planned grid-off)" : "");
  e.dur = dur;
  e.repeat = repeat;
  bool isClear = !strcmp(st, "clear");
  e.kind = repeat ? 'r' : !strcmp(st, "open") ? 'o' : isClear ? 'c' : 'i';
  e.pushPrio = quietEv ? 0 : gates_->pushPrio(sev, isClear, epoch, force);
  e.telegram = e.pushPrio > 0;  // 0.7.7: Telegram is a primary channel (owner request), same gates as ntfy
  // e-mail flag for built-in alerts (r8 §4.7). DAILY_SUMMARY never: the mailer sends its own summary.
  bool mail = false;
  if (!quietEv && policy_.on && strcmp(type, "DAILY_SUMMARY") != 0) {
    if (!strcmp(type, "TEST_ALERT")) mail = true;
    else if (e.kind == 'o' && policy_.builtinMinSev > 0 && sev >= policy_.builtinMinSev) mail = true;
    else if (isClear && policy_.builtinClear && r && r->openMailed) mail = true;
  }
  if (mail) {
    bool ok = gates_->takeMailJob(sev, epoch);
    snprintf(e.em, sizeof e.em, "%s", ok ? "q" : "cap");
    if (r && e.kind == 'o') r->openMailed = ok;
  } else if (r && e.kind == 'o') {
    r->openMailed = false;
  }
  if (r && isClear) r->openMailed = false;
  q_.push(e);
}

void AlertEngine::update(Rule& r, bool cond, bool clearCond, uint32_t nowMs, uint32_t epoch, const char* openMsg,
                         const char* clearMsg) {
  if (r.st == RState::IDLE) {
    if (!cond) return;
    r.st = RState::PENDING;
    r.since = nowMs;
    r.consec = 0;
  }
  if (r.st == RState::PENDING) {
    if (!cond) {
      r.st = RState::IDLE;
      r.consec = 0;
      return;
    }
    if (r.consec < 255) r.consec++;
    bool fire = r.minConsecutive ? r.consec >= r.minConsecutive : (uint32_t)(nowMs - r.since) >= r.holdOnMs;
    if (!fire) return;
    r.st = RState::ACTIVE;
    r.activeSince = r.lastNotify = nowMs;
    emit(r.type, r.sev, "open", openMsg, 0, nowMs, epoch, false, false, &r);
    if (&r == &rules_[R_FAULT]) faultOpens_++;
    else warnOpens_++;
    return;
  }
  if (r.st == RState::ACTIVE) {
    if (clearCond) {
      r.st = RState::CLEARING;
      r.since = nowMs;
    } else {
      if (r.sev >= 4 && !r.noRepeat && !r.quiet && cfg_.notifyRepeatMin > 0 &&
          (uint32_t)(nowMs - r.lastNotify) >= (uint32_t)cfg_.notifyRepeatMin * 60000UL) {
        r.lastNotify = nowMs;
        emit(r.type, r.sev, "open", openMsg, 0, nowMs, epoch, true, false, &r);
      }
      return;
    }
  }
  if (r.st == RState::CLEARING) {
    if (!clearCond) {
      r.st = RState::ACTIVE;
      return;
    }
    if ((uint32_t)(nowMs - r.since) >= r.holdOffMs) {
      r.st = RState::IDLE;
      r.consec = 0;
      uint32_t dur = (nowMs - r.activeSince) / 1000;
      emit(r.clearType ? r.clearType : r.type, r.clearSev, "clear", clearMsg, dur, nowMs, epoch, false, false, &r);
    }
  }
}

bool AlertEngine::plannedGridOff(uint32_t epoch) const {
  if (cfg_.gridOffFrom < 0 || cfg_.gridOffTo < 0 || !tu::epochValid(epoch)) return false;
  tu::Local l = tu::toLocal(epoch);
  int m = (int)(l.hour * 60 + l.minute);
  int a = ((cfg_.gridOffFrom - cfg::GRID_OFF_GRACE_MIN) % 1440 + 1440) % 1440;
  int b = (cfg_.gridOffTo + cfg::GRID_OFF_GRACE_MIN) % 1440;
  return a <= b ? (m >= a && m < b) : (m >= a || m < b);
}

bool AlertEngine::nearPlannedStart(uint32_t epoch) const {
  if (cfg_.gridOffFrom < 0 || cfg_.gridOffTo < 0 || !tu::epochValid(epoch)) return false;
  tu::Local l = tu::toLocal(epoch);
  int d = (int)(l.hour * 60 + l.minute) - cfg_.gridOffFrom;
  d = ((d % 1440) + 1440 + 720) % 1440 - 720;  // circular difference in minutes, -720..719
  return d >= -cfg::GRID_OFF_GRACE_MIN && d <= cfg::GRID_OFF_GRACE_MIN;
}

static void fmtDur(uint32_t s, char* out, size_t n) {
  if (s >= 3600) snprintf(out, n, "%luh %02lum", (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
  else if (s >= 60) snprintf(out, n, "%lum", (unsigned long)(s / 60));
  else snprintf(out, n, "%lus", (unsigned long)s);
}

void AlertEngine::evaluate(const Sample& s, uint32_t nowMs, uint32_t epoch) {
  rollDay(epoch);
  lastValidMs_ = nowMs;
  hadValid_ = true;
  char msg[200], cmsg[120];
  auto bit = [&](int i) { return s.wf[0] && codes::qpiwsBit(s.wf, i); };
  bool socOk = !isnan(s.soc);
  bool bvOk = !isnan(s.bv);

  // INVERTER_FAULT
  {
    bool fcSet = s.fc != INT16_MIN && s.fc != 0;
    bool cond = s.md == 'F' || s.faultBits || fcSet;
    if (fcSet)
      snprintf(msg, sizeof msg, "Inverter fault %02d: %s [%s table]", s.fc, codes::faultText(s.fcSrc, s.fc),
               codes::srcTag(s.fcSrc));
    else if (s.faultBits) {
      int first = -1;
      bool a1 = bit(1);
      for (int i = 0; i < 31; i++)
        if (bit(i) && codes::qpiwsBitIsFault(i, a1)) { first = i; break; }
      snprintf(msg, sizeof msg, "Inverter fault: QPIWS a%d %s (mode %c)", first,
               codes::qpiwsBitName(first, (int)strlen(s.wf)), s.md ? s.md : '?');
    } else
      snprintf(msg, sizeof msg, "Inverter is in FAULT mode - check the LCD fault code");
    update(rules_[R_FAULT], cond, !cond, nowMs, epoch, msg, "Inverter fault cleared");
  }
  // BATT_CRITICAL
  {
    bool cond = (socOk && s.soc <= 20) || (bvOk && s.bv <= 21.5f) || bit(codes::QPIWS_BATT_LOW_ALARM) ||
                bit(codes::QPIWS_BATT_UNDER_SHUTDOWN);
    bool clr = (!socOk || s.soc >= 25) && bvOk && s.bv >= 22.5f && !bit(codes::QPIWS_BATT_LOW_ALARM) &&
               !bit(codes::QPIWS_BATT_UNDER_SHUTDOWN);
    snprintf(msg, sizeof msg, "Battery CRITICAL: %.2f V%s%.0f%%", s.bv, socOk ? ", SOC " : "", socOk ? s.soc : 0.0);
    if (!socOk) snprintf(msg, sizeof msg, "Battery CRITICAL: %.2f V", s.bv);
    update(rules_[R_BATT_CRIT], cond, clr, nowMs, epoch, msg, "Battery recovered from critical level");
  }
  // BATT_LOW (while discharging)
  {
    bool discharging = (!isnan(s.bdi) && s.bdi > 0) || (!isnan(s.bi) && s.bi < 0) || s.md == 'B';
    bool cond = discharging && ((socOk && s.soc <= cfg_.socLow) || (bvOk && s.bv <= cfg_.bvLow));
    bool clr = (!socOk || s.soc >= cfg_.socLow + 5) && bvOk && s.bv >= cfg_.bvLow + 0.5f;
    if (socOk) snprintf(msg, sizeof msg, "Battery low: SOC %.0f%%, %.2f V (discharging)", s.soc, s.bv);
    else snprintf(msg, sizeof msg, "Battery low: %.2f V (discharging)", s.bv);
    update(rules_[R_BATT_LOW], cond, clr, nowMs, epoch, msg, "Battery level OK again");
  }
  // BATT_HIGH
  {
    bool cond = bit(codes::QPIWS_BATT_HIGH) || (bvOk && s.bv >= cfg_.bvHigh);
    bool clr = bvOk && s.bv < cfg_.bvHigh - 0.5f && !bit(codes::QPIWS_BATT_HIGH);
    snprintf(msg, sizeof msg, "Battery voltage high: %.2f V", s.bv);
    update(rules_[R_BATT_HIGH], cond, clr, nowMs, epoch, msg, "Battery voltage back to normal");
  }
  // GRID_LOSS / GRID_RESTORED  (never from QMOD = B alone: SBU runs on battery with grid present)
  {
    float thr = ups_ ? 170.f : 90.f;
    // A measured grid voltage wins over the QPIWS "line fail" bit: the Sorotec REVO firmware on the user's unit keeps
    // a5 (and QALL warning 02 "mains low voltage") set with the grid at 236 V in line mode (first contact 2026-10-22).
    // The bit is only used when no grid voltage is available.
    bool absent = !isnan(s.gv) ? (s.gv < thr) : bit(codes::QPIWS_LINE_FAIL);
    if (socOk) snprintf(msg, sizeof msg, "Grid lost, on battery (SOC %.0f%%)", s.soc);
    else snprintf(msg, sizeof msg, "Grid lost, on battery (%.2f V)", s.bv);
    Rule& r = rules_[R_GRID_LOSS];
    IGateStore* st = gates_->store();
    bool planned = plannedGridOff(epoch);
    // 0.7.9: an outage notified before a reboot continues silently (its restore still comes, with the full duration)
    if (resumeEpoch_ && tu::epochValid(epoch)) {
      uint32_t ago = epoch > resumeEpoch_ ? epoch - resumeEpoch_ : 0;
      if (ago < cfg::OUTAGE_RESUME_MAX_S && r.st != RState::ACTIVE && r.st != RState::CLEARING) {
        r.st = RState::ACTIVE;
        r.activeSince = r.lastNotify = nowMs - ago * 1000UL;
        r.quiet = false;
      }
      resumeEpoch_ = 0;
    }
    // A new episode is planned only if it starts at the scheduled cut (window start +- grace). If the owner turns the
    // grid back on inside the window and it then fails, that is a real outage (owner request 2026-10-22). Right after
    // a boot inside the window with the grid off, assume the planned cut (the logger did not see it start).
    if (r.st == RState::IDLE || r.st == RState::PENDING)
      r.quiet = nearPlannedStart(epoch) || (planned && nowMs < cfg::GRID_BOOT_GRACE_MS + 120000UL);
    // planned, but the grid is still off after the window (+ grace): a real outage now
    if (r.st == RState::ACTIVE && r.quiet && !planned && absent && tu::epochValid(epoch)) {
      r.quiet = false;
      r.lastNotify = nowMs;
      char m2[200];
      snprintf(m2, sizeof m2, "Grid still off after the planned grid-off (%02d:%02d-%02d:%02d), %s", cfg_.gridOffFrom / 60,
               cfg_.gridOffFrom % 60, cfg_.gridOffTo / 60, cfg_.gridOffTo % 60, msg);
      emit(r.type, r.sev, "open", m2, 0, nowMs, epoch, false, false, &r);
      if (st) st->saveOutage(epoch);
    }
    char d[24];
    fmtDur(r.st == RState::CLEARING || r.st == RState::ACTIVE ? (nowMs - r.activeSince) / 1000 : 0, d, sizeof d);
    snprintf(cmsg, sizeof cmsg, "Grid restored after %s", d);
    RState before = r.st;
    bool openOk = nowMs >= cfg::GRID_BOOT_GRACE_MS;  // the first minute: cfg (planned window) and outage state load
    update(r, absent && openOk, !absent && !isnan(s.gv), nowMs, epoch, msg, cmsg);
    if (st) {
      if (before == RState::PENDING && r.st == RState::ACTIVE && !r.quiet) st->saveOutage(tu::epochValid(epoch) ? epoch : 0);
      if ((before == RState::ACTIVE || before == RState::CLEARING) && r.st == RState::IDLE) st->saveOutage(0);
    }
    // GRID_ABNORMAL
    bool abn = !absent && !isnan(s.gv) && ((s.gv >= 90.f && s.gv < 180.f) || s.gv > 270.f);
    snprintf(msg, sizeof msg, "Grid voltage abnormal: %.1f V", s.gv);
    update(rules_[R_GRID_ABN], abn, !abn, nowMs, epoch, msg, "Grid voltage normal again");
  }
  // OVERLOAD
  {
    bool cond = bit(codes::QPIWS_OVERLOAD) || (!isnan(s.ld) && s.ld >= cfg_.loadHigh);
    bool clr = !bit(codes::QPIWS_OVERLOAD) && (isnan(s.ld) || s.ld < 80.f);
    snprintf(msg, sizeof msg, "Overload: load %.0f%% (%.0f W)", s.ld, s.w);
    update(rules_[R_OVERLOAD], cond, clr, nowMs, epoch, msg, "Load back below 80%");
  }
  // OVER_TEMP
  {
    bool tOk = !isnan(s.tmp) && !s.tmpRaw;
    bool cond = bit(codes::QPIWS_OVER_TEMP) || bit(codes::QPIWS_FAN_LOCKED) || (tOk && s.tmp >= cfg_.tempHigh);
    if (bit(codes::QPIWS_FAN_LOCKED)) snprintf(msg, sizeof msg, "Inverter fan locked");
    else if (tOk) snprintf(msg, sizeof msg, "Inverter over temperature: %.0f C", s.tmp);
    else snprintf(msg, sizeof msg, "Inverter over temperature warning");
    update(rules_[R_OVER_TEMP], cond, !cond, nowMs, epoch, msg, "Inverter temperature normal again");
  }
  // PV_OVERVOLT
  {
    bool cond = bit(codes::QPIWS_PV_VOLT_HIGH) || (!isnan(s.pvv) && s.pvv >= 480.f);
    bool clr = !bit(codes::QPIWS_PV_VOLT_HIGH) && (isnan(s.pvv) || s.pvv < 470.f);
    snprintf(msg, sizeof msg, "PV voltage high: %.1f V (max Voc 500 V)", s.pvv);
    update(rules_[R_PV_OVERVOLT], cond, clr, nowMs, epoch, msg, "PV voltage normal again");
  }
  // PV_LOW_DAY (once per day, info)
  if (tu::epochValid(epoch)) {
    unsigned h = tu::localHour(epoch);
    bool c = h >= 10 && h < 15 && !isnan(s.pvw) && s.pvw < 50.f && (!socOk || s.soc < 95.f);
    if (!c) pvLowTracking_ = false;
    else if (!pvLowTracking_) {
      pvLowTracking_ = true;
      pvLowSinceMs_ = nowMs;
    } else if (!pvLowSentToday_ && (uint32_t)(nowMs - pvLowSinceMs_) >= 3600000UL) {
      pvLowSentToday_ = true;
      snprintf(msg, sizeof msg, "PV below 50 W for 60 min around midday (SOC %.0f%%): shade, dirt or PV breaker?",
               socOk ? s.soc : 0.0);
      emit("PV_LOW_DAY", 2, "info", msg, 0, nowMs, epoch, false, false);
      warnOpens_++;
    }
  }
}

void AlertEngine::tick(uint32_t nowMs, uint32_t epoch, bool linkExpected, bool linkOk, const mrec::Daily* today) {
  rollDay(epoch);
  // LINK_LOST: no valid inverter reply for 60 s while the link is expected to work
  if (linkOk) {
    lastValidMs_ = nowMs;
    hadValid_ = true;
  } else if (linkExpected && !hadValid_ && lastValidMs_ == 0) {
    lastValidMs_ = nowMs;  // start the 60 s timer when we start expecting replies
  }
  bool lost = linkExpected && lastValidMs_ != 0 && (uint32_t)(nowMs - lastValidMs_) >= 60000UL;
  update(rules_[R_LINK_LOST], lost, !lost, nowMs, epoch,
         "No valid inverter reply for 60 s - check the COM cable/wiring (logger keeps retrying)",
         "Inverter link restored");
  // DAILY_SUMMARY
  if (today && cfg_.summaryHour >= 0 && tu::epochValid(epoch) && !summarySentToday_ &&
      (int)tu::localHour(epoch) == cfg_.summaryHour) {
    summarySentToday_ = true;
    char msg[200];
    snprintf(msg, sizeof msg, "Today: PV %.1f kWh, load %.1f kWh, grid %.1f kWh (est.), battery in %.1f / out %.1f kWh",
             today->epv / 1000.0, today->eld / 1000.0, today->egr / 1000.0, today->ech / 1000.0, today->edis / 1000.0);
    emit("DAILY_SUMMARY", 1, "info", msg, 0, nowMs, epoch, false, false);
  }
}

void AlertEngine::raise(const char* type, uint8_t sev, const char* msg, uint32_t nowMs, uint32_t epoch, bool force) {
  rollDay(epoch);
  emit(type, sev, "info", msg, 0, nowMs, epoch, false, force);
}

void AlertEngine::raiseQuiet(const char* type, uint8_t sev, const char* msg, uint32_t nowMs, uint32_t epoch) {
  quietNext_ = true;
  raise(type, sev, msg, nowMs, epoch, false);
  quietNext_ = false;
}
