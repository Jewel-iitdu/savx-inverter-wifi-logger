// Pi30Parse.cpp - see Pi30Parse.h. Host-portable.
#include "proto/pi30/Pi30Parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto/pi30/Pi30Frame.h"

namespace pi30p {

float num(const char* tok) {
  if (!pi30::isNumberToken(tok)) return NAN;
  return (float)atof(tok);
}

static int tokenize(const char* payload, char* buf, size_t cap, char** tok, int maxTok) {
  if (!payload) return 0;
  size_t l = strlen(payload);
  if (l >= cap) l = cap - 1;
  memcpy(buf, payload, l);
  buf[l] = 0;
  return pi30::split(buf, tok, maxTok);
}

static bool isBits(const char* t, size_t n) {
  if (strlen(t) != n) return false;
  for (size_t i = 0; i < n; i++)
    if (t[i] != '0' && t[i] != '1') return false;
  return true;
}

bool parseQpigs(const char* payload, Qpigs& q) {
  char buf[256];
  char* t[QPIGS_MAX + 4];
  int n = tokenize(payload, buf, sizeof buf, t, QPIGS_MAX + 4);
  q = Qpigs();
  if (n < 17 || n > QPIGS_MAX) return false;
  if (!isBits(t[16], 8)) return false;  // status bits must be 8 x 0/1
  q.n = n;
  for (int i = 0; i < QPIGS_MAX; i++) q.v[i] = NAN;
  for (int i = 0; i < n; i++) q.v[i] = (i == 16 || i == 20) ? NAN : num(t[i]);
  snprintf(q.st, sizeof q.st, "%s", t[16]);
  if (n > 20 && strlen(t[20]) == 3) snprintf(q.st2, sizeof q.st2, "%s", t[20]);
  // the core fields must be numeric
  static const int core[] = {0, 1, 2, 3, 5, 6, 8, 9, 10, 13};
  for (int i : core)
    if (isnan(q.v[i])) return false;
  return true;
}

bool parseQlith0(const char* payload, Qlith0& b) {
  char buf[160];
  char* t[16];
  int n = tokenize(payload, buf, sizeof buf, t, 16);
  b = Qlith0();
  if (n != 12) return false;
  b.n = n;
  for (int i = 0; i < 12; i++) b.v[i] = num(t[i]);
  for (int i = 0; i < 5; i++)
    if (isnan(b.v[i])) return false;
  // plausibility: a 12-48 V lithium pack, currents within 0..500 A, capacity 0..100 %
  if (b.v[0] < 8.f || b.v[0] > 70.f || b.v[1] < 0.f || b.v[1] > 500.f || b.v[2] < 0.f || b.v[2] > 500.f ||
      b.v[4] < 0.f || b.v[4] > 100.f)
    return false;
  return true;
}

bool parseQall(const char* payload, Qall& a) {
  char buf[200];
  char* t[24];
  int n = tokenize(payload, buf, sizeof buf, t, 24);
  a = Qall();
  if (n != 18) return false;
  a.n = n;
  for (int i = 0; i < 18; i++) a.v[i] = i == 15 ? NAN : num(t[i]);
  if (strlen(t[15]) != 1) return false;
  a.mode = t[15][0];
  if (!strchr("PSLBFHDCYE", a.mode)) return false;
  a.warn = isnan(a.v[16]) ? -1 : (int)a.v[16];
  a.fault = isnan(a.v[17]) ? -1 : (int)a.v[17];
  static const int core[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  for (int i : core)
    if (isnan(a.v[i])) return false;
  return true;
}

bool parseQpiri(const char* payload, Qpiri& r) {
  char buf[256];
  char* t[QPIRI_MAX + 2];
  int n = tokenize(payload, buf, sizeof buf, t, QPIRI_MAX + 2);
  r = Qpiri();
  if (n < 20 || n > QPIRI_MAX) return false;
  r.n = n;
  for (int i = 0; i < QPIRI_MAX; i++) r.v[i] = i < n ? num(t[i]) : NAN;  // "-" (parallel max) -> NaN
  return !isnan(r.v[5]) && !isnan(r.v[7]);
}

char parseQmod(const char* payload) {
  if (!payload || strlen(payload) != 1) return 0;
  char c = payload[0];
  return strchr("PSLBFHDCYE", c) ? c : 0;
}

bool parseQpiws(const char* payload, char* wf, int cap) {
  if (!payload) return false;
  size_t n = strlen(payload);
  while (n > 0 && payload[n - 1] == ' ') n--;
  if (n < 32 || n > 40 || (int)n >= cap) return false;
  for (size_t i = 0; i < n; i++) {
    char c = payload[i];
    bool digitPos = n >= 36 && (i == 31 || i == 32);  // fault-code digits on 24 V MAX/GK
    if (!(c == '0' || c == '1' || (digitPos && c >= '0' && c <= '9'))) return false;
  }
  memcpy(wf, payload, n);
  wf[n] = 0;
  return true;
}

int qpiwsFaultCode(const char* wf) {
  if (!wf || strlen(wf) < 36) return -1;
  char a = wf[31], b = wf[32];
  if (a < '0' || a > '9' || b < '0' || b > '9') return -1;
  return (a - '0') * 10 + (b - '0');
}

bool parseQflag(const char* payload, char* en, int ecap, char* dis, int dcap) {
  if (!payload || payload[0] != 'E') return false;
  int ne = 0, nd = 0;
  bool inD = false;
  for (const char* p = payload + 1; *p; p++) {
    if (*p == 'D') { inD = true; continue; }
    if (*p < 'a' || *p > 'z') continue;  // ignore stray characters such as ';'
    if (inD) { if (nd < dcap - 1) dis[nd++] = *p; }
    else if (ne < ecap - 1) en[ne++] = *p;
  }
  en[ne] = 0;
  dis[nd] = 0;
  return inD;
}

bool parseVersion(const char* payload, const char* prefix, char* out, int cap) {
  size_t pl = strlen(prefix);
  if (!payload || strncmp(payload, prefix, pl) != 0) return false;
  const char* v = payload + pl;
  while (*v == ' ') v++;
  if (!*v || (int)strlen(v) >= cap) return false;
  for (const char* p = v; *p; p++)
    if (!((*p >= '0' && *p <= '9') || *p == '.')) return false;
  snprintf(out, cap, "%s", v);
  return true;
}

bool parseQid(const char* payload, char* out, int cap) {
  if (!payload || !*payload || (int)strlen(payload) >= cap) return false;
  for (const char* p = payload; *p; p++)
    if (*p < '0' || *p > '9') return false;
  snprintf(out, cap, "%s", payload);
  return true;
}

bool parseQsid(const char* payload, char* out, int cap) {
  if (!payload || strlen(payload) < 3) return false;
  for (const char* p = payload; *p; p++)
    if (*p < '0' || *p > '9') return false;
  int len = (payload[0] - '0') * 10 + (payload[1] - '0');
  int avail = (int)strlen(payload) - 2;
  if (len <= 0 || len > avail) len = avail;
  if (len >= cap) len = cap - 1;
  memcpy(out, payload + 2, (size_t)len);
  out[len] = 0;
  return true;
}

bool parseEnergyWh(const char* payload, uint32_t& wh) {
  if (!payload || !*payload || strlen(payload) > 10) return false;
  for (const char* p = payload; *p; p++)
    if (*p < '0' || *p > '9') return false;
  wh = (uint32_t)strtoul(payload, nullptr, 10);
  return true;
}

bool parseQ1(const char* payload, Q1& q) {
  char buf[256];
  char* t[30];
  int n = tokenize(payload, buf, sizeof buf, t, 30);
  q = Q1();
  if (n < 17 || n > 28) return false;
  q.n = n;
  for (int i = 0; i < 28; i++) q.v[i] = i < n ? num(t[i]) : NAN;
  return !isnan(q.v[6]);
}

bool parsePqse(const char* payload, Pqse& p) {
  char buf[160];
  char* t[24];
  int n = tokenize(payload, buf, sizeof buf, t, 24);
  p = Pqse();
  if (n < 12 || strlen(t[0]) != 6) return false;
  for (int i = 0; i < 6; i++)
    if (t[0][i] < '0' || t[0][i] > '9') return false;
  p.lang = t[0][0] - '0';
  p.workMode = t[0][1] - '0';
  p.inRange = t[0][2] - '0';
  p.outVSet = t[0][3] - '0';
  p.outFSet = t[0][4] - '0';
  p.batType = t[0][5] - '0';
  p.bulkV = num(t[1]);
  p.floatV = num(t[2]);
  p.lowAlarmV = num(t[3]);
  p.cutOffV = num(t[4]);
  p.sumChgA = (int)num(t[5]);
  p.acChgA = (int)num(t[6]);
  p.year = atoi(t[7]);
  p.month = atoi(t[8]);
  p.day = atoi(t[9]);
  p.hour = atoi(t[10]);
  p.minute = atoi(t[11]);
  p.ok = !isnan(p.bulkV) && !isnan(p.floatV);
  return p.ok;
}

}  // namespace pi30p
