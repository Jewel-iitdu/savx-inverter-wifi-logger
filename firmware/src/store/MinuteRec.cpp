// MinuteRec.cpp - see MinuteRec.h.
#include "store/MinuteRec.h"

#include <string.h>

#include "model/JsonOut.h"

namespace mrec {

uint8_t crc8(const uint8_t* d, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

void seal(MinuteRec& r) { r.crc8 = crc8((const uint8_t*)&r, sizeof r - 1); }
bool valid(const MinuteRec& r) { return r.crc8 == crc8((const uint8_t*)&r, sizeof r - 1); }

MinuteRec empty() {
  MinuteRec r;
  memset(&r, 0, sizeof r);
  r.w = r.wx = r.pvw = r.pvx = r.bw = I16_NA;
  r.bv = r.bvn = r.bvx = U16_NA;
  r.bi = I16_NA;
  r.gv = r.gvn = r.ov = r.pvv = U16_NA;
  r.soc = r.ld = U8_NA;
  r.tmp = I8_NA;
  r.md = 0;
  return r;
}

static void putI(JsonOut& j, const char* k, int16_t v, double scale, int dec) {
  if (v != I16_NA) j.num(k, v * scale, dec);
}
static void putU(JsonOut& j, const char* k, uint16_t v, double scale, int dec) {
  if (v != U16_NA) j.num(k, v * scale, dec);
}
static void putU8(JsonOut& j, const char* k, uint8_t v) {
  if (v != U8_NA) j.i(k, v);
}

static void body(JsonOut& j, const MinuteRec& r) {
  putI(j, "w", r.w, 1, 0);
  putI(j, "wx", r.wx, 1, 0);
  putI(j, "pvw", r.pvw, 1, 0);
  putI(j, "pvx", r.pvx, 1, 0);
  putU(j, "bv", r.bv, 0.01, 2);
  putU(j, "bvn", r.bvn, 0.01, 2);
  putU(j, "bvx", r.bvx, 0.01, 2);
  putI(j, "bi", r.bi, 0.1, 1);
  putU8(j, "soc", r.soc);
  putU(j, "gv", r.gv, 0.1, 1);
  putU(j, "gvn", r.gvn, 0.1, 1);
  putU(j, "ov", r.ov, 0.1, 1);
  putU8(j, "ld", r.ld);
  if (r.tmp != I8_NA) j.i("tmp", r.tmp);
  putU(j, "pvv", r.pvv, 0.1, 1);
  j.ch("md", r.md);
  j.i("wn", r.wn);
  j.i("fc", r.fc);
  j.i("n", r.n);
  j.num("epv", r.epv * 0.1, 1);
  j.num("eld", r.eld * 0.1, 1);
  j.num("ech", r.ech * 0.1, 1);
  j.num("edis", r.edis * 0.1, 1);
  j.num("egr", r.egr * 0.1, 1);
  if (r.flags & F_PROVISIONAL) j.i("tq", 0);
}

size_t toHistJson(const MinuteRec& r, char* out, size_t cap) {
  JsonOut j(out, cap);
  j.open();
  body(j, r);
  j.close();
  return j.ok() ? j.len() : 0;
}

void h15Begin(H15& h, uint32_t quarterTs) {
  h.ts = quarterTs;
  h.agg = empty();
  h.agg.ts = quarterTs;
  h.count = 0;
  h.bestModeN = -1;
}

// n-weighted running average helper on encoded values
template <typename T>
static void wavg(T& acc, int nAcc, T v, int nv, T na) {
  if (v == na || nv <= 0) return;
  if (acc == na || nAcc <= 0) { acc = v; return; }
  double a = ((double)acc * nAcc + (double)v * nv) / (double)(nAcc + nv);
  acc = (T)(a < 0 ? a - 0.5 : a + 0.5);
}
template <typename T>
static void vmin(T& acc, T v, T na) { if (v != na && (acc == na || v < acc)) acc = v; }
template <typename T>
static void vmax(T& acc, T v, T na) { if (v != na && (acc == na || v > acc)) acc = v; }

void h15Add(H15& h, const MinuteRec& r) {
  MinuteRec& a = h.agg;
  int na = a.n, nr = r.n;
  if (nr > 0) {
    wavg<int16_t>(a.w, na, r.w, nr, I16_NA);
    wavg<int16_t>(a.pvw, na, r.pvw, nr, I16_NA);
    wavg<int16_t>(a.bw, na, r.bw, nr, I16_NA);
    wavg<uint16_t>(a.bv, na, r.bv, nr, U16_NA);
    wavg<int16_t>(a.bi, na, r.bi, nr, I16_NA);
    wavg<uint16_t>(a.gv, na, r.gv, nr, U16_NA);
    wavg<uint16_t>(a.ov, na, r.ov, nr, U16_NA);
    wavg<uint16_t>(a.pvv, na, r.pvv, nr, U16_NA);
    wavg<uint8_t>(a.ld, na, r.ld, nr, U8_NA);
    vmax<int16_t>(a.wx, r.wx, I16_NA);
    vmax<int16_t>(a.pvx, r.pvx, I16_NA);
    vmin<uint16_t>(a.bvn, r.bvn, U16_NA);
    vmax<uint16_t>(a.bvx, r.bvx, U16_NA);
    vmin<uint16_t>(a.gvn, r.gvn, U16_NA);
    vmax<int8_t>(a.tmp, r.tmp, I8_NA);
    if (r.soc != U8_NA) a.soc = r.soc;
    if (r.wn > a.wn) a.wn = r.wn;
    if (r.fc) a.fc = r.fc;
  }
  // dominant mode: the mode of the minute with the most samples wins (ties -> latest)
  if (r.md && nr >= h.bestModeN) { a.md = r.md; h.bestModeN = nr; }
  a.epv = (uint16_t)(a.epv + r.epv);
  a.eld = (uint16_t)(a.eld + r.eld);
  a.ech = (uint16_t)(a.ech + r.ech);
  a.edis = (uint16_t)(a.edis + r.edis);
  a.egr = (uint16_t)(a.egr + r.egr);
  a.flags |= (uint8_t)(r.flags & F_PROVISIONAL);
  a.n = (uint8_t)(na + nr > 255 ? 255 : na + nr);
  h.count++;
}

size_t toH15Json(const H15& h, char* out, size_t cap) {
  JsonOut j(out, cap);
  j.open();
  body(j, h.agg);
  j.close();
  return j.ok() ? j.len() : 0;
}

void dailyReset(Daily& d, int32_t day) {
  d = Daily();
  d.day = day;
}

void dailyAdd(Daily& d, const MinuteRec& r) {
  d.epv += r.epv * 0.1;
  d.eld += r.eld * 0.1;
  d.ech += r.ech * 0.1;
  d.edis += r.edis * 0.1;
  d.egr += r.egr * 0.1;
  if (r.n == 0) return;
  d.n++;
  if (r.pvx != I16_NA && r.pvx > d.pvx) d.pvx = r.pvx;
  if (r.wx != I16_NA && r.wx > d.wx) d.wx = r.wx;
  if (r.bvn != U16_NA && (d.bvn == 0 || r.bvn * 0.01f < d.bvn)) d.bvn = r.bvn * 0.01f;
  if (r.bvx != U16_NA && r.bvx * 0.01f > d.bvx) d.bvx = r.bvx * 0.01f;
  if (r.soc != U8_NA) {
    if (d.socn < 0 || r.soc < d.socn) d.socn = r.soc;
    if (r.soc > d.socx) d.socx = r.soc;
  }
  if (r.flags & F_NO_GRID) d.gout++;
}

size_t toDailyJson(const Daily& d, char* out, size_t cap) {
  JsonOut j(out, cap);
  j.open();
  j.num("epv", d.epv, 0);
  j.num("eld", d.eld, 0);
  j.num("egr", d.egr, 0);
  j.num("ech", d.ech, 0);
  j.num("edis", d.edis, 0);
  if (d.epvi >= 0) j.num("epvi", d.epvi, 0);
  if (d.pvx >= 0) j.i("pvx", d.pvx);
  if (d.wx >= 0) j.i("wx", d.wx);
  if (d.bvn > 0) j.num("bvn", d.bvn, 2);
  if (d.bvx > 0) j.num("bvx", d.bvx, 2);
  if (d.socn >= 0) j.i("socn", d.socn);
  if (d.socx >= 0) j.i("socx", d.socx);
  j.i("gout", d.gout);
  j.i("nf", d.nf);
  j.i("nw", d.nw);
  j.i("n", d.n);
  j.close();
  return j.ok() ? j.len() : 0;
}

bool fixupTs(MinuteRec& r, uint16_t anchorBootId, uint32_t anchorUpMs, uint32_t anchorEpoch) {
  if (!(r.flags & F_PROVISIONAL) || r.bootId != anchorBootId) return false;
  int64_t ts = (int64_t)anchorEpoch - ((int64_t)anchorUpMs - (int64_t)r.upMs) / 1000;
  if (ts <= 0) return false;
  r.ts = (uint32_t)ts;
  r.flags &= (uint8_t)~F_PROVISIONAL;
  seal(r);
  return true;
}

}  // namespace mrec
