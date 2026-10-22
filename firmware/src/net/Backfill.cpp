// Backfill.cpp - see Backfill.h.
#include "net/Backfill.h"

#include <stdio.h>
#include <string.h>

#include "model/TimeUtil.h"

namespace backfill {

bool next(IMinuteStore& st, int32_t today, Batch& b, uint16_t maxRecs, uint16_t liveMax) {
  b = Batch();
  if (maxRecs > 64) maxRecs = 64;  // addRecords never sees more than it can de-duplicate (no silent gaps)
  if (maxRecs < 1) maxRecs = 1;
  int32_t days[64];
  int nd = st.listDays(days, 64);
  bool found = false;
  for (int i = 0; i < nd; i++) {
    uint32_t c = st.count(days[i]), cur = st.cursor(days[i]);
    if (cur >= c) continue;
    b.pendingTotal += c - cur;
    if (found) continue;
    found = true;
    b.day = days[i];
    b.from = cur;
    uint32_t left = c - cur;
    b.live = today != 0 && days[i] == today && left <= liveMax;
    b.n = (uint16_t)(left > maxRecs ? maxRecs : left);
  }
  return found;
}

static uint32_t quarterOf(uint32_t ts) { return ts - (ts % 900); }

int addRecords(IMinuteStore& st, const Batch& b, const MinuteRec* recs, int32_t today, upj::Patch& p) {
  char dk[12];
  tu::dateKeyFromDay(b.day, dk, sizeof dk);
  // 1) hist entries, de-duplicated by hhmm (a provisional estimate never overrides a timed record)
  int keep[64];  // uplinkTask stack: batches are <= BACKFILL_MAX_RECS (60)
  int nk = 0;
  int n = b.n > 64 ? 64 : b.n;
  for (int i = 0; i < n; i++) {
    const MinuteRec& r = recs[i];
    if (!mrec::valid(r) || r.ts == 0 || tu::localDayNumber(r.ts) != b.day) continue;
    int dup = -1;
    for (int k = 0; k < nk; k++)
      if (recs[keep[k]].ts / 60 == r.ts / 60) { dup = k; break; }
    if (dup < 0) keep[nk++] = i;
    else if (!(r.flags & mrec::F_PROVISIONAL) || (recs[keep[dup]].flags & mrec::F_PROVISIONAL)) keep[dup] = i;
  }
  char path[40], json[300];
  for (int k = 0; k < nk; k++) {
    const MinuteRec& r = recs[keep[k]];
    char hm[6];
    tu::hhmmKey(r.ts, hm, sizeof hm);
    if (!mrec::toHistJson(r, json, sizeof json)) continue;
    snprintf(path, sizeof path, "hist/%s/%s", dk, hm);
    p.add(path, json);
  }
  // 2) quarters completed by this batch
  uint32_t maxTs = 0;
  for (int k = 0; k < nk; k++)
    if (recs[keep[k]].ts > maxTs) maxTs = recs[keep[k]].ts;
  if (!maxTs) return nk;
  uint32_t quarters[100];
  int nq = 0;
  auto addQ = [&](uint32_t q) {
    for (int i = 0; i < nq; i++)
      if (quarters[i] == q) return;
    if (nq < 100) quarters[nq++] = q;
  };
  for (int k = 0; k < nk; k++) addQ(quarterOf(recs[keep[k]].ts));
  if (b.from > 0) {  // the quarter of the last record uploaded before this batch may close only now
    MinuteRec prev;
    if (st.read(b.day, b.from - 1, &prev, 1) == 1 && mrec::valid(prev) && prev.ts) addQ(quarterOf(prev.ts));
  }
  bool dayOver = today == 0 || b.day < today;
  for (int i = 0; i < nq; i++) {
    uint32_t q = quarters[i];
    bool closed = dayOver || maxTs >= q + 840;  // minute 14 of the quarter, or anything later
    if (!closed) continue;
    // aggregate the quarter from the file: its records sit at most ~16 positions before the batch end
    mrec::H15 h;
    mrec::h15Begin(h, q);
    MinuteRec buf[32];
    uint32_t start = b.from > 20 ? b.from - 20 : 0;
    uint32_t end = b.from + b.n;
    for (uint32_t pos = start; pos < end;) {
      int got = st.read(b.day, pos, buf, 32);
      if (got <= 0) break;
      for (int j = 0; j < got && pos + (uint32_t)j < end; j++)
        if (mrec::valid(buf[j]) && buf[j].ts && quarterOf(buf[j].ts) == q) mrec::h15Add(h, buf[j]);
      pos += (uint32_t)got;
    }
    if (!h.count) continue;
    char hm[6];
    tu::hhmmKey(q, hm, sizeof hm);
    if (!mrec::toH15Json(h, json, sizeof json)) continue;
    snprintf(path, sizeof path, "h15/%s/%s", dk, hm);
    p.add(path, json);
  }
  return nk;
}

bool addDailyFields(IMinuteStore& st, int32_t day, upj::Patch& p) {
  mrec::Daily d;
  mrec::dailyReset(d, day);
  MinuteRec buf[32];
  uint32_t from = 0;
  bool any = false;
  for (;;) {
    int n = st.read(day, from, buf, 32);
    if (n <= 0) break;
    for (int i = 0; i < n; i++)
      if (mrec::valid(buf[i]) && buf[i].ts && tu::localDayNumber(buf[i].ts) == day) {
        mrec::dailyAdd(d, buf[i]);
        any = true;
      }
    from += (uint32_t)n;
  }
  if (!any) return false;
  char dk[12], path[40], v[24];
  tu::dateKeyFromDay(day, dk, sizeof dk);
  auto put = [&](const char* f, double x, int dec) {
    snprintf(path, sizeof path, "daily/%s/%s", dk, f);
    snprintf(v, sizeof v, "%.*f", dec, x);
    p.add(path, v);
  };
  put("epv", d.epv, 0);
  put("eld", d.eld, 0);
  put("egr", d.egr, 0);
  put("ech", d.ech, 0);
  put("edis", d.edis, 0);
  if (d.pvx >= 0) put("pvx", d.pvx, 0);
  if (d.wx >= 0) put("wx", d.wx, 0);
  if (d.bvn > 0) put("bvn", d.bvn, 2);
  if (d.bvx > 0) put("bvx", d.bvx, 2);
  if (d.socn >= 0) put("socn", d.socn, 0);
  if (d.socx >= 0) put("socx", d.socx, 0);
  put("gout", d.gout, 0);
  put("n", d.n, 0);
  return true;
}

bool commit(IMinuteStore& st, const Batch& b) {
  if (st.cursor(b.day) != b.from) return false;
  return st.setCursor(b.day, b.from + b.n);
}

uint32_t oldestPendingTs(IMinuteStore& st) {
  int32_t days[64];
  int nd = st.listDays(days, 64);
  for (int i = 0; i < nd; i++) {
    uint32_t c = st.count(days[i]), cur = st.cursor(days[i]);
    if (cur >= c) continue;
    MinuteRec r;
    if (st.read(days[i], cur, &r, 1) == 1 && r.ts) return r.ts;
  }
  return 0;
}

}  // namespace backfill
