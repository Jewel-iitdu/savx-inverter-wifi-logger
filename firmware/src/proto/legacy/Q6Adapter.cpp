// Q6Adapter.cpp - see Q6Adapter.h.
#include "proto/legacy/Q6Adapter.h"

#include <stdio.h>
#include <string.h>

#include "model/Decode.h"
#include "proto/pi30/Pi30Frame.h"
#include "proto/pi30/Pi30Parse.h"

static int tok(const char* payload, char* buf, size_t cap, char** t, int maxT) {
  size_t l = strlen(payload);
  if (l >= cap) l = cap - 1;
  memcpy(buf, payload, l);
  buf[l] = 0;
  return pi30::split(buf, t, maxT);
}

bool Q6Adapter::decodeQ6(const char* payload, Sample& s) {
  char buf[200];
  char* t[32];
  int n = tok(payload, buf, sizeof buf, t, 32);
  if (n < 15) return false;
  s.gv = pi30p::num(t[0]);
  s.gf = pi30p::num(t[1]);
  s.ov = pi30p::num(t[2]);
  s.of = pi30p::num(t[3]);
  s.bv = pi30p::num(t[4]);
  s.soc = pi30p::num(t[5]);
  s.bci = pi30p::num(t[6]);
  float va = pi30p::num(t[10]), w = pi30p::num(t[11]);
  s.va = isnan(va) ? NAN : va * 10.f;
  s.w = isnan(w) ? NAN : w * 10.f;
  s.ld = pi30p::num(t[12]);
  s.tmp = pi30p::num(t[13]);
  if (strlen(t[14]) == 1 && strchr("BL", t[14][0])) s.md = t[14][0];
  s.finalizeDerived();
  return !isnan(s.ov) && !isnan(s.bv);
}

bool Q6Adapter::decodeQ1(const char* payload, Sample& s) {
  char buf[160];
  char* t[12];
  int n = tok(payload, buf, sizeof buf, t, 12);
  if (n != 8) return false;
  s.gv = pi30p::num(t[0]);
  s.ov = pi30p::num(t[2]);
  s.ld = pi30p::num(t[3]);
  s.gf = pi30p::num(t[4]);
  s.tmp = pi30p::num(t[6]);
  return !isnan(s.ov);
}

ProbeResult Q6Adapter::probe(SerialLink& l, ProbeLog& log) {
  ProbeResult pr;
  snprintf(pr.family, sizeof pr.family, "q6");
  Sample s;
  LinkResult r = l.query("Q6", Framing::NONE, reply_);
  log.add(l.clock().ms(), "Q6 (no CRC) -> %s %.50s", linkResultName(r), reply_.payload);
  if ((r == LinkResult::OK || r == LinkResult::NO_CRC_OK) && decodeQ6(reply_.payload, s)) {
    q6_ = true;
  } else {
    r = l.query("Q1", Framing::NONE, reply_);
    log.add(l.clock().ms(), "Q1 (no CRC) -> %s %.50s", linkResultName(r), reply_.payload);
    if (!((r == LinkResult::OK || r == LinkResult::NO_CRC_OK) && decodeQ1(reply_.payload, s))) {
      pr.code = r == LinkResult::TIMEOUT ? ProbeCode::SILENCE : ProbeCode::GARBAGE;
      snprintf(pr.detail, sizeof pr.detail, "legacy Q1/Q6: %s", linkResultName(r));
      return pr;
    }
    q6_ = false;
  }
  pr.ok = true;
  pr.code = ProbeCode::OK;
  pr.score = 60;
  snprintf(pr.dialect, sizeof pr.dialect, "%s", q6_ ? "Q6" : "Q1");
  snprintf(pr.detail, sizeof pr.detail, "legacy RP/RS protocol (%s), partial decode", q6_ ? "Q6" : "Q1");
  ident_ = Identity();
  snprintf(ident_.family, sizeof ident_.family, "q6");
  snprintf(ident_.dia, sizeof ident_.dia, "%s", pr.dialect);
  return pr;
}

bool Q6Adapter::lightProbe(SerialLink& l, ProbeLog& log) {
  Sample s;
  LinkResult r = l.query(q6_ ? "Q6" : "Q1", Framing::NONE, reply_);
  bool ok = (r == LinkResult::OK || r == LinkResult::NO_CRC_OK) &&
            (q6_ ? decodeQ6(reply_.payload, s) : decodeQ1(reply_.payload, s));
  log.add(l.clock().ms(), "light %s -> %s", q6_ ? "Q6" : "Q1", linkResultName(r));
  return ok;
}

void Q6Adapter::schedule(uint32_t nowMs) {
  sched_.clear();
  sched_.add(Cmd::Q1, cfg::P_FAST_MS, 0, true, nowMs);  // Cmd::Q1 slot stands for the legacy fast poll
}

bool Q6Adapter::step(SerialLink& l, uint32_t nowMs, Sample& io) {
  last_ = StepInfo();
  int idx = sched_.due(nowMs);
  if (idx < 0) return false;
  LinkResult r = l.query(q6_ ? "Q6" : "Q1", Framing::NONE, reply_);
  bool ok = (r == LinkResult::OK || r == LinkResult::NO_CRC_OK) &&
            (q6_ ? decodeQ6(reply_.payload, io) : decodeQ1(reply_.payload, io)) && (!q6_ || decode::plausibleSample(io));
  sched_.done(idx, l.clock().ms());
  last_.ran = true;
  last_.fast = true;
  last_.ok = ok;
  last_.cmd = Cmd::Q1;
  last_.res = r;
  return ok;
}

void Q6Adapter::exportState(AdapterState& st) const {
  st = AdapterState();
  snprintf(st.adapter, sizeof st.adapter, "q6");
  snprintf(st.dialect, sizeof st.dialect, "%s", q6_ ? "Q6" : "Q1");
  st.flags = q6_ ? AF_Q6 : 0;
}

void Q6Adapter::importState(const AdapterState& st) {
  q6_ = st.flags & AF_Q6;
  ident_ = Identity();
  snprintf(ident_.family, sizeof ident_.family, "q6");
  snprintf(ident_.dia, sizeof ident_.dia, "%s", q6_ ? "Q6" : "Q1");
}
