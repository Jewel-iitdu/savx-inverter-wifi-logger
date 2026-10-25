// OutageLog.cpp - see OutageLog.h.
#include "net/OutageLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "agg/AlertEngine.h"
#include "model/TimeUtil.h"

namespace outage {

const char* stepName(uint8_t s) {
  static const char* const N[S_N] = {"idle", "sign-in", "events", "live", "minute", "cfg", "alerts", "meta",
                                     "daily", "meter", "month", "diag", "prune", "update", "GitHub check", "push",
                                     "Telegram pairing", "mail", "healthcheck"};
  return s < S_N ? N[s] : "?";
}

static const char* const TASK_NAMES[T_N] = {"loop", "inv", "agg", "uplink"};

void crumbsReset(Crumbs& c) {
  memset(&c, 0, sizeof c);
  c.magic = CRUMBS_MAGIC;
}

bool crumbsValid(const Crumbs& c) { return c.magic == CRUMBS_MAGIC && c.step < S_N; }

void crumbsText(const Crumbs& c, char* out, size_t n) {
  if (!n) return;
  out[0] = 0;
  if (!crumbsValid(c)) return;
  uint32_t maxMs = 0;
  for (int t = 0; t < T_N; t++)
    if (c.ms[t] > maxMs) maxMs = c.ms[t];
  if (!maxMs) return;
  size_t l = 0;
  auto add = [&](const char* fmt, ...) {
    if (l >= n - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + l, n - l, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    l = (size_t)w >= n - l ? n - 1 : l + (size_t)w;
  };
  uint32_t upS = maxMs / 1000;
  add("up %luh%02lum", (unsigned long)(upS / 3600), (unsigned long)(upS / 60 % 60));
  bool inStep = c.step != S_IDLE && c.stepMs && c.stepMs <= maxMs;
  if (inStep) add("; uplink in %s %lus", stepName((uint8_t)c.step), (unsigned long)((maxMs - c.stepMs) / 1000));
  for (int t = 0; t < T_N; t++) {
    if (!c.ms[t] || (t == T_UPLINK && inStep)) continue;
    if (maxMs - c.ms[t] > STALL_MS) add("; %s stalled %lus", TASK_NAMES[t], (unsigned long)((maxMs - c.ms[t]) / 1000));
  }
}

static uint32_t fnv(const Rec& r) {
  const uint8_t* p = (const uint8_t*)&r;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < offsetof(Rec, sum); i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

bool recValid(const Rec& r) {
  return r.magic == REC_MAGIC && r.sum == fnv(r) && memchr(r.err, 0, sizeof r.err) && memchr(r.rr, 0, sizeof r.rr) &&
         memchr(r.crumb, 0, sizeof r.crumb);
}

void recSeal(Rec& r) { r.sum = fnv(r); }

const char* wifiReasonName(uint8_t reason) {
  switch (reason) {
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "INACTIVE";
    case 8: return "ASSOC_LEAVE";
    case 15: return "4WAY_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default: return nullptr;
  }
}

void OutageLog::restart(uint32_t nowMs, uint32_t nowEp) {
  memset(&r_, 0, sizeof r_);
  r_.magic = REC_MAGIC;
  r_.sinceEp = tu::epochValid(nowEp) ? nowEp : 0;
  r_.sinceMs = nowMs;
  r_.fw = fw_;
  if (wifiKnown_ && wifiUp_) r_.rssi = lastUpRssi_;  // the signal at the start of the gap, until a drop or a change
  recSeal(r_);
}

void OutageLog::boot(uint32_t nowMs, const char* resetReason, bool crashReset, bool poweron, uint32_t checkpointEp,
                     const char* crumb, uint32_t fwCode) {
  lastWifiMs_ = nowMs;
  wifiKnown_ = false;
  fw_ = fwCode;
  if (recValid(r_) && r_.fw == fwCode) {  // the gap that was open when the previous boot ended goes on
    r_.sinceMs = nowMs;  // millis() restarted; the duration now comes from the epochs
    if (r_.reboots < 255) r_.reboots++;
  } else {
    restart(nowMs, 0);
    // after a power cut (RTC memory lost) the gap starts at the NVS checkpoint; any other reset without a record of
    // this image (the first boot of an update or a rollback) starts at this boot, so it never reports a made-up gap
    if ((poweron || crashReset) && tu::epochValid(checkpointEp)) {
      r_.sinceEp = checkpointEp;
      r_.approx = 1;
      r_.reboots = 1;
    }
  }
  snprintf(r_.rr, sizeof r_.rr, "%s", resetReason ? resetReason : "");
  if (crashReset) r_.crash = 1;
  snprintf(r_.crumb, sizeof r_.crumb, "%s", crumb ? crumb : "");
  recSeal(r_);
}

void OutageLog::onWifi(uint32_t nowMs, bool up, uint8_t lastReason, int8_t rssi) {
  uint32_t dt = nowMs - lastWifiMs_;
  bool ch = false;
  if (wifiKnown_ && !wifiUp_ && !up) {  // down at both readings: a blocked loop or the drop's own second is not counted
    downAccMs_ += dt;
    if (downAccMs_ >= 1000) {
      r_.wifiDownS += downAccMs_ / 1000;
      downAccMs_ %= 1000;
      ch = true;
    }
    if (lastReason && r_.reason != lastReason) {
      r_.reason = lastReason;
      ch = true;
    }
  }
  if (wifiKnown_ && wifiUp_ && !up) {
    if (r_.drops < 65535) r_.drops++;
    r_.rssi = lastUpRssi_;
    ch = true;
  }
  if (up && rssi) {
    lastUpRssi_ = rssi;
    if (!r_.drops && r_.rssi != rssi) {
      r_.rssi = rssi;
      ch = true;
    }
  }
  wifiKnown_ = true;
  wifiUp_ = up;
  lastWifiMs_ = nowMs;
  if (ch) recSeal(r_);
}

void OutageLog::onHeap(uint32_t freeInternal, uint32_t largestBlock, uint32_t freeStack) {
  bool ch = false;
  if (freeStack && (!r_.minStack || freeStack < r_.minStack)) {
    r_.minStack = freeStack;
    ch = true;
  }
  if (freeInternal && (!r_.minHeap || freeInternal < r_.minHeap)) {
    r_.minHeap = freeInternal;
    ch = true;
  }
  if (largestBlock && (!r_.minBlock || largestBlock < r_.minBlock)) {
    r_.minBlock = largestBlock;
    ch = true;
  }
  if (ch) recSeal(r_);
}

void OutageLog::onFail(const char* err, bool oom) {
  if (r_.fails < 65535) r_.fails++;
  if (oom && r_.oom < 65535) r_.oom++;
  if (!r_.err[0] && err) snprintf(r_.err, sizeof r_.err, "%s", err);
  recSeal(r_);
}

void OutageLog::onTlsDrop() {
  if (r_.tlsDrops < 255) r_.tlsDrops++;
  recSeal(r_);
}

void OutageLog::onStep(uint8_t step, uint32_t ms) {
  if (ms <= r_.slowMs) return;
  r_.slowMs = ms;
  r_.slowStep = step;
  recSeal(r_);
}

bool OutageLog::onCloudOk(uint32_t nowMs, uint32_t nowEp, Report& out) {
  bool epOk = tu::epochValid(nowEp) && r_.sinceEp && nowEp >= r_.sinceEp;
  bool known = epOk || !r_.reboots;
  uint32_t durS = epOk ? nowEp - r_.sinceEp : known ? (nowMs - r_.sinceMs) / 1000 : 0;
  bool report = r_.crash || (known && (uint64_t)durS * 1000 >= OUTAGE_MIN_MS);
  if (report) {
    out.startEp = r_.sinceEp ? r_.sinceEp : (tu::epochValid(nowEp) && known && nowEp > durS ? nowEp - durS : 0);
    out.durS = durS;
    formatReport(r_, durS, known, out.msg, sizeof out.msg);
  }
  restart(nowMs, nowEp);
  downAccMs_ = 0;
  return report;
}

void toEvent(const Report& rep, uint32_t nowEp, uint32_t upMs, AlertEvent& e) {
  e = AlertEvent();
  e.ts = rep.startEp ? rep.startEp : tu::epochValid(nowEp) ? nowEp : 0;
  e.upMs = upMs;
  snprintf(e.type, sizeof e.type, "LOGGER_OFFLINE");
  e.sev = 1;
  snprintf(e.st, sizeof e.st, "info");
  snprintf(e.msg, sizeof e.msg, "%s", rep.msg);
  e.dur = rep.durS;
  e.kind = 'i';
}

static void fmtDur(uint32_t s, char* b, size_t n) {
  if (s < 90) snprintf(b, n, "%lu s", (unsigned long)s);
  else if (s < 90 * 60) snprintf(b, n, "%lu min", (unsigned long)((s + 30) / 60));
  else snprintf(b, n, "%lu h %02lu min", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
}

void formatReport(const Rec& r, uint32_t durS, bool durKnown, char* out, size_t n) {
  if (!n) return;
  out[0] = 0;
  size_t l = 0;
  // whole parts only: a part that does not fit is left out, later (shorter) ones may still fit
  auto part = [&](const char* s) {
    size_t k = strlen(s);
    if (l + k < n) {
      memcpy(out + l, s, k + 1);
      l += k;
    }
  };
  char p[160], d[24];
  const char* a = r.approx ? "~" : "";
  if (durKnown) {
    fmtDur(durS, d, sizeof d);
    if (r.sinceEp) {
      tu::Local t = tu::toLocal(r.sinceEp);
      snprintf(p, sizeof p, "Offline %s%s from %s%02u:%02u", a, d, a, t.hour, t.minute);
    } else {
      snprintf(p, sizeof p, "Offline %s", d);
    }
  } else {
    snprintf(p, sizeof p, "Offline (length unknown)");
  }
  part(p);
  if (r.approx && !r.crash) part(" (power cut?)");
  if (r.reboots) {
    if (r.reboots > 1) snprintf(p, sizeof p, ". Reboot %s x%u", r.rr[0] ? r.rr : "?", r.reboots);
    else snprintf(p, sizeof p, ". Reboot %s", r.rr[0] ? r.rr : "?");
    part(p);
    if (r.crumb[0]) {
      snprintf(p, sizeof p, " (%s)", r.crumb);
      part(p);
    }
  }
  if (r.drops || r.wifiDownS) {
    fmtDur(r.wifiDownS, d, sizeof d);
    snprintf(p, sizeof p, ". Wi-Fi down %s, %u drop%s", d, r.drops, r.drops == 1 ? "" : "s");
    part(p);
    if (r.reason) {
      const char* rn = wifiReasonName(r.reason);
      if (rn) snprintf(p, sizeof p, ", %s", rn);
      else snprintf(p, sizeof p, ", reason %u", r.reason);
      part(p);
    }
  } else {
    part(". Wi-Fi ok");
  }
  if (r.rssi) {
    snprintf(p, sizeof p, ", %d dBm", r.rssi);
    part(p);
  }
  if (r.fails) {
    snprintf(p, sizeof p, ". Cloud %u fail%s", r.fails, r.fails == 1 ? "" : "s");
    part(p);
    if (r.oom) {
      snprintf(p, sizeof p, " (%u no memory)", r.oom);
      part(p);
    }
    if (r.err[0]) {
      snprintf(p, sizeof p, ": %s", r.err);
      part(p);
    }
  }
  if (r.tlsDrops) {
    snprintf(p, sizeof p, ". TLS reset x%u", r.tlsDrops);
    part(p);
  }
  if (r.minHeap) {
    snprintf(p, sizeof p, ". Heap min %lu/%lu KB", (unsigned long)(r.minHeap / 1024), (unsigned long)(r.minBlock / 1024));
    part(p);
  }
  if (r.minStack) {
    snprintf(p, sizeof p, ", stack %lu B", (unsigned long)r.minStack);
    part(p);
  }
  if (r.slowMs >= 5000) {
    snprintf(p, sizeof p, ". Slowest %s %lu s", stepName(r.slowStep), (unsigned long)(r.slowMs / 1000));
    part(p);
  }
}

}  // namespace outage
