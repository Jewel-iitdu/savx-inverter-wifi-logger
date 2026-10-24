// UplinkJson.cpp - see UplinkJson.h.
#include "net/UplinkJson.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "model/JsonAlloc.h"
#include "model/JsonOut.h"
#include "model/TimeUtil.h"

namespace upj {

// ============================================================================================ Patch
Patch::Patch(char* buf, size_t cap) : b_(buf), cap_(cap) {
  if (cap_ >= 2) {
    b_[0] = '{';
    b_[1] = 0;
    len_ = 1;
  } else {
    ok_ = false;
  }
}

void Patch::add(const char* path, const char* json) {
  if (!ok_ || closed_ || !path || !json) {
    ok_ = false;
    return;
  }
  size_t need = strlen(path) + strlen(json) + 4 + (n_ ? 1 : 0);
  if (len_ + need + 2 > cap_) {  // keep room for the closing brace
    ok_ = false;
    return;
  }
  len_ += (size_t)snprintf(b_ + len_, cap_ - len_, "%s\"%s\":%s", n_ ? "," : "", path, json);
  n_++;
}

size_t Patch::close() {
  if (!ok_) return 0;
  if (!closed_) {
    b_[len_++] = '}';
    b_[len_] = 0;
    closed_ = true;
  }
  return len_;
}

// ============================================================================================ events
static void sanitizeKey(const char* in, char* out, size_t n, size_t maxLen) {
  size_t o = 0;
  for (; in && *in && o + 1 < n && o < maxLen; in++) {
    char c = *in;
    out[o++] = (isalnum((unsigned char)c) || c == '_') ? c : '_';
  }
  if (o == 0 && n > 1) out[o++] = 'X';
  out[o] = 0;
}

size_t eventKey(const AlertEvent& e, char* out, size_t n) {
  char src[25];
  sanitizeKey(e.rid[0] ? e.rid : e.type, src, sizeof src, 24);
  char k = e.kind;
  if (k != 'o' && k != 'c' && k != 'r' && k != 'i') k = 'i';
  int w = snprintf(out, n, "%010lu-%s-%c", (unsigned long)e.ts, src, k);
  return w > 0 ? (size_t)w : 0;
}

bool wantsRtdbWrite(const AlertEvent& e) { return !e.repeat || !strcmp(e.em, "q"); }

void channels(const AlertEvent& e, bool ntfyOn, bool tgOn, char* out, size_t n) {
  out[0] = 0;
  size_t l = 0;
  auto add = [&](const char* s) { l += (size_t)snprintf(out + l, n > l ? n - l : 0, "%s%s", l ? "," : "", s); };
  if (e.pushPrio > 0 && ntfyOn) add("ntfy");
  if (e.telegram && tgOn) add("tg");
  if (!strcmp(e.em, "q")) add("email");
  if (!l) snprintf(out, n, "log");
}

size_t eventJson(const AlertEvent& e, const char* ch, char* out, size_t n) {
  JsonOut j(out, n);
  j.open();
  j.u("ts", e.ts);
  j.s("type", e.type);
  j.i("sev", e.sev);
  if (e.st[0]) j.s("st", e.st);
  if (e.rid[0]) j.s("rid", e.rid);
  j.num("val", e.val, 3);
  j.num("thr", e.thr, 3);
  j.s("msg", e.msg);
  if (ch && *ch) j.s("ch", ch);
  if (e.em[0]) j.s("em", e.em);
  if (!strcmp(e.st, "clear") || e.dur) j.u("dur", e.dur);
  j.close();
  return j.ok() ? j.len() : 0;
}

size_t mailqJson(const AlertEvent& e, const char* dateKey, char* out, size_t n) {
  JsonOut j(out, n);
  j.open();
  j.s("d", dateKey);
  j.u("ts", e.ts);
  j.i("sev", e.sev);
  if (e.rc >= 1 && e.rc <= 7) j.i("rc", e.rc);
  j.close();
  return j.ok() ? j.len() : 0;
}

size_t eventPatch(const AlertEvent& e, const char* ch, char* out, size_t n, bool* mailq) {
  if (mailq) *mailq = false;
  if (!tu::epochValid(e.ts)) return 0;
  char key[48], d[12], path[80], ev[520], mq[96];
  eventKey(e, key, sizeof key);
  tu::dateKey(e.ts, d, sizeof d);
  if (!eventJson(e, ch, ev, sizeof ev)) return 0;
  Patch p(out, n);
  snprintf(path, sizeof path, "events/%s/%s", d, key);
  p.add(path, ev);
  if (!strcmp(e.em, "q")) {
    if (!mailqJson(e, d, mq, sizeof mq)) return 0;
    snprintf(path, sizeof path, "mailq/%s", key);
    p.add(path, mq);
    if (mailq) *mailq = true;
  }
  return p.close();
}

// ============================================================================================ alertState / status
size_t alertStateJson(const AlertStateOut& o, char* out, size_t n) {
  if (o.removed) {
    snprintf(out, n, "null");
    return 4;
  }
  JsonOut j(out, n);
  j.open();
  j.s("st", rules::stName(o.st));
  if (o.since) j.u("since", o.since);
  j.num("val", o.val, 3);
  if (o.last) j.u("last", o.last);
  j.i("cnt", o.cnt);
  if (o.pk) j.u("pk", o.pk);
  if (o.err[0]) j.s("err", o.err);
  j.close();
  return j.ok() ? j.len() : 0;
}

size_t statusJson(const StatusDoc& s, char* out, size_t n) {
  JsonOut j(out, n);
  j.open();
  j.u("ts", s.ts);
  j.s("fw", s.fw);
  if (s.ip[0]) j.s("ip", s.ip);
  j.i("rssi", s.rssi);
  j.u("heap", s.heap);
  j.u("psram", s.psram);
  j.u("up", s.up);
  if (s.rr && *s.rr) j.s("rr", s.rr);
  if (s.q >= 0) j.i("q", s.q);
  if (s.proto[0]) j.s("proto", s.proto);
  if (s.dia[0]) j.s("dia", s.dia);
  j.u("hs", s.hs);
  j.u("rq", s.rq);
  if (s.lok) j.u("lok", s.lok);
  if (s.cok) j.u("cok", s.cok);
  j.num("arv", s.arv, 0);
  j.i("acat", s.acat);
  j.i("mjq", s.mjq);
  j.i("nrul", s.nrul);
  j.u("edrop", s.edrop);
  j.i("qg", s.qg);
  if (s.ruleErr[0]) j.s("ruleErr", s.ruleErr);
  if (s.meter[0]) j.s("meter", s.meter);
  if (s.ota[0]) j.s("ota", s.ota);
  if (s.repo[0]) j.s("repo", s.repo);
  if (s.otachk[0]) j.s("otachk", s.otachk);
  j.num("chipC", s.chipC, 1);
  if (s.tgOn) {
    j.u("tgOk", s.tgOk);
    j.u("tgFail", s.tgFail);
    if (s.tgErr[0]) j.s("tgErr", s.tgErr);
    if (s.tgErrAt) j.u("tgErrAt", s.tgErrAt);
  }
  if (s.mailOn) {
    j.u("mailOk", s.mailOk);
    j.u("mailFail", s.mailFail);
    if (s.mailErr[0]) j.s("mailErr", s.mailErr);
    if (s.mailErAt) j.u("mailErAt", s.mailErAt);
  }
  j.close();
  return j.ok() ? j.len() : 0;
}

// ============================================================================================ URLs
size_t rtdbPath(const char* path, const char* idToken, bool silent, const char* extraQuery, char* out, size_t n) {
  int w = snprintf(out, n, "/devices/%s%s%s.json?auth=%s%s%s%s", DEVICE_ID, (path && *path) ? "/" : "", path ? path : "",
                   idToken ? idToken : "", silent ? "&print=silent" : "", (extraQuery && *extraQuery) ? "&" : "",
                   extraQuery ? extraQuery : "");
  return (w > 0 && (size_t)w < n) ? (size_t)w : 0;
}

static bool endsWith(const char* s, const char* suf) {
  size_t a = strlen(s), b = strlen(suf);
  return a >= b && !strcmp(s + a - b, suf);
}

bool dbHostFromUrl(const char* url, char* host, size_t n) {
  if (!url || n < 8) return false;
  const char* p = url;
  while (*p == ' ') p++;
  if (strncmp(p, "https://", 8) != 0) return false;
  p += 8;
  size_t o = 0;
  for (; *p && *p != '/' && *p != '?' && *p != ':' && *p != ' '; p++) {
    if (o + 1 >= n) return false;
    host[o++] = (char)tolower((unsigned char)*p);
  }
  host[o] = 0;
  if (o < 5) return false;
  return endsWith(host, ".firebasedatabase.app") || endsWith(host, ".firebaseio.com");
}

size_t urlEncode(const char* s, char* out, size_t n) {
  static const char* H = "0123456789ABCDEF";
  size_t o = 0;
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      if (o + 1 >= n) return 0;
      out[o++] = (char)c;
    } else {
      if (o + 3 >= n) return 0;
      out[o++] = '%';
      out[o++] = H[c >> 4];
      out[o++] = H[c & 15];
    }
  }
  if (n) out[o] = 0;
  return o;
}

static bool isUuid(const char* s) {
  if (strlen(s) != 36) return false;
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    } else if (!isxdigit((unsigned char)s[i])) {
      return false;
    }
  }
  return true;
}

bool hcPath(const char* cfg, bool fail, char* path, size_t n) {
  if (!cfg || !*cfg) return false;
  char rest[160];
  const char* pre = "https://hc-ping.com/";
  if (!strncmp(cfg, pre, strlen(pre))) snprintf(rest, sizeof rest, "%s", cfg + strlen(pre));
  else if (isUuid(cfg)) snprintf(rest, sizeof rest, "%s", cfg);
  else return false;
  size_t l = strlen(rest);
  while (l && rest[l - 1] == '/') rest[--l] = 0;
  if (!l || strchr(rest, '?') || strchr(rest, ' ')) return false;
  int w = snprintf(path, n, "/%s%s", rest, fail ? "/fail" : "");
  return w > 0 && (size_t)w < n;
}

bool nudgePath(const char* url, char* path, size_t n) {
  const char* pre = "https://script.google.com";
  if (!url || strncmp(url, pre, strlen(pre)) != 0) return false;
  const char* p = url + strlen(pre);
  if (strncmp(p, "/macros/s/", 10) != 0 || !strstr(p, "/exec") || (!strstr(p, "?k=") && !strstr(p, "&k="))) return false;
  if (strchr(p, ' ') || strchr(p, '#')) return false;
  int w = snprintf(path, n, "%s", p);
  return w > 0 && (size_t)w < n;
}

// ============================================================================================ parsers
static bool numIn(JsonVariantConst v, double lo, double hi, double& out) {
  if (!v.is<double>()) return false;
  double d = v.as<double>();
  if (isnan(d) || d < lo || d > hi) return false;
  out = d;
  return true;
}

int tgPairChats(const char* json, size_t n, char (*out)[24], int maxOut, long long* maxUpdateId) {
  if (maxUpdateId) *maxUpdateId = -1;
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return 0;
  if (!doc["ok"].as<bool>()) return 0;
  int cnt = 0;
  for (JsonObjectConst u : doc["result"].as<JsonArrayConst>()) {
    long long id = u["update_id"] | -1LL;
    if (maxUpdateId && id > *maxUpdateId) *maxUpdateId = id;
    JsonObjectConst msg = u["message"];
    JsonObjectConst chat = msg["chat"];
    if (chat.isNull() || !chat["id"].is<long long>()) continue;
    const char* type = chat["type"] | "";
    const char* text = msg["text"] | "";
    bool ok = !strcmp(type, "private") ||
              ((!strcmp(type, "group") || !strcmp(type, "supergroup")) && !strncmp(text, "/start", 6));
    if (!ok) continue;  // never a channel, nor a group that did not ask
    char c[24];
    snprintf(c, sizeof c, "%lld", chat["id"].as<long long>());
    bool dup = false;
    for (int k = 0; k < cnt; k++) dup |= !strcmp(out[k], c);
    if (dup || cnt >= maxOut) continue;
    snprintf(out[cnt++], 24, "%s", c);
  }
  return cnt;
}

bool cfgEq(const AlertCfg& a, const AlertCfg& b) {
  return a.liveSec == b.liveSec && a.socLow == b.socLow && a.bvLow == b.bvLow && a.bvHigh == b.bvHigh &&
         a.tempHigh == b.tempHigh && a.loadHigh == b.loadHigh && a.notifyRepeatMin == b.notifyRepeatMin &&
         a.keepHistDays == b.keepHistDays && a.keepEventDays == b.keepEventDays && a.quietFrom == b.quietFrom &&
         a.quietTo == b.quietTo && a.summaryHour == b.summaryHour && a.testAlert == b.testAlert &&
         a.reprobe == b.reprobe && a.gridOffFrom == b.gridOffFrom && a.gridOffTo == b.gridOffTo && a.meter == b.meter &&
         a.ota == b.ota && a.otaAt == b.otaAt;
}

bool parseCfg(const char* json, size_t n, CfgDoc& out) {
  out = CfgDoc();
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return false;
  if (doc.isNull()) return true;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  if (o.isNull()) return false;
  AlertCfg& c = out.c;
  double v;
  // same ranges as the cfg rules (d1 §4.7); anything else keeps the default
  if (numIn(o["liveSec"], cfg::LIVE_SEC_MIN, cfg::LIVE_SEC_MAX, v)) c.liveSec = (int)v;
  if (numIn(o["socLow"], 5, 80, v)) c.socLow = (float)v;
  if (numIn(o["bvLow"], 20, 30, v)) c.bvLow = (float)v;
  if (numIn(o["bvHigh"], 26, 34, v)) c.bvHigh = (float)v;
  if (numIn(o["tempHigh"], 40, 100, v)) c.tempHigh = (float)v;
  if (numIn(o["loadHigh"], 50, 120, v)) c.loadHigh = (float)v;
  if (numIn(o["notifyRepeatMin"], 5, 1440, v)) c.notifyRepeatMin = (int)v;
  if (numIn(o["keepHistDays"], 30, 3650, v)) c.keepHistDays = (int)v;
  if (numIn(o["keepEventDays"], 30, 3650, v)) c.keepEventDays = (int)v;
  if (numIn(o["quietFrom"], 0, 23, v)) c.quietFrom = (int)v;
  if (numIn(o["gridOffFrom"], -1, 1439, v)) c.gridOffFrom = (int)v;  // 0.7.9 planned grid-off window (local minutes)
  if (numIn(o["gridOffTo"], -1, 1439, v)) c.gridOffTo = (int)v;
  if (numIn(o["quietTo"], 0, 23, v)) c.quietTo = (int)v;
  if (numIn(o["summaryHour"], -1, 23, v)) c.summaryHour = (int)v;
  if (numIn(o["meter"], 0, 1, v)) c.meter = (int)v;  // 0.9.0 grid meter on/off
  if (numIn(o["ota"], 0, 999999, v)) c.ota = (uint32_t)v;  // 0.9.2 remote update: version code
  if (numIn(o["otaAt"], 0, 1e15, v)) c.otaAt = (uint32_t)fmod(v, 4294967296.0);
  if (numIn(o["testAlert"], 0, 1e15, v)) c.testAlert = (uint32_t)fmod(v, 4294967296.0);
  if (numIn(o["reprobe"], 0, 1e15, v)) c.reprobe = (uint32_t)fmod(v, 4294967296.0);
  if (numIn(o["alertsRev"], 0, 1e16, v)) {
    out.alertsRev = v;
    out.hasRev = true;
  }
  return true;
}

static bool copyStr(JsonVariantConst v, char* out, size_t n) {
  if (!v.is<const char*>()) return false;
  const char* s = v.as<const char*>();
  if (strlen(s) >= n) return false;
  snprintf(out, n, "%s", s);
  return true;
}

static uint32_t asSeconds(JsonVariantConst v) {
  if (v.is<const char*>()) return (uint32_t)strtoul(v.as<const char*>(), nullptr, 10);
  if (v.is<double>()) return (uint32_t)v.as<double>();
  return 0;
}

bool parseSignIn(const char* json, size_t n, AuthResult& r) {
  r = AuthResult();
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return false;
  if (!copyStr(doc["idToken"], r.idToken, sizeof r.idToken)) return false;
  if (!copyStr(doc["refreshToken"], r.refreshToken, sizeof r.refreshToken)) return false;
  copyStr(doc["localId"], r.uid, sizeof r.uid);
  r.expiresIn = asSeconds(doc["expiresIn"]);
  if (!r.expiresIn) r.expiresIn = 3600;
  return true;
}

bool parseRefresh(const char* json, size_t n, AuthResult& r) {
  r = AuthResult();
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return false;
  if (!copyStr(doc["id_token"], r.idToken, sizeof r.idToken)) return false;
  if (!copyStr(doc["refresh_token"], r.refreshToken, sizeof r.refreshToken)) return false;
  copyStr(doc["user_id"], r.uid, sizeof r.uid);
  r.expiresIn = asSeconds(doc["expires_in"]);
  if (!r.expiresIn) r.expiresIn = 3600;
  return true;
}

bool parseApiError(const char* json, size_t n, char* msg, size_t mn) {
  if (mn) msg[0] = 0;
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return false;
  JsonVariantConst e = doc["error"];
  if (e.is<const char*>()) {
    snprintf(msg, mn, "%s", e.as<const char*>());
    return true;
  }
  if (e["message"].is<const char*>()) {
    snprintf(msg, mn, "%s", e["message"].as<const char*>());
    return true;
  }
  return false;
}

int parseShallowDates(const char* json, size_t n, int32_t* days, int max) {
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return -1;
  if (doc.isNull()) return 0;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  if (o.isNull()) return -1;
  int k = 0;
  for (JsonPairConst kv : o) {
    int32_t d;
    const char* key = kv.key().c_str();
    if (strlen(key) == 10 && tu::parseDateKey(key, d) && k < max) days[k++] = d;
  }
  return k;
}

int parseDailyRange(const char* json, size_t n, mrec::Daily* out, int max) {
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return -1;
  if (doc.isNull()) return 0;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  if (o.isNull()) return -1;
  int k = 0;
  for (JsonPairConst kv : o) {
    int32_t day;
    JsonObjectConst v = kv.value().as<JsonObjectConst>();
    if (k >= max || v.isNull() || !tu::parseDateKey(kv.key().c_str(), day)) continue;
    mrec::Daily& d = out[k++];
    mrec::dailyReset(d, day);
    d.epv = v["epv"] | 0.0;
    d.eld = v["eld"] | 0.0;
    d.egr = v["egr"] | 0.0;
    d.ech = v["ech"] | 0.0;
    d.edis = v["edis"] | 0.0;
    d.gout = v["gout"] | 0;
    d.n = v["n"] | 0;
  }
  return k;
}

}  // namespace upj
