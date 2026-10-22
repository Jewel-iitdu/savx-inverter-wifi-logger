// RuleParse.cpp - see RuleParse.h. Every check below has a twin in r8 §3.11 (firebase/database.rules.json).
#include "agg/RuleParse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "model/JsonAlloc.h"

namespace rules {

size_t utf16Len(const char* s) {
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)s; p && *p; p++) {
    if ((*p & 0xC0) == 0x80) continue;  // continuation byte
    n += (*p >= 0xF0) ? 2 : 1;          // 4-byte sequences are surrogate pairs in UTF-16
  }
  return n;
}

void utf8Trim(char* s) {
  size_t l = strlen(s);
  if (!l) return;
  // walk back over continuation bytes to the lead byte of the last sequence
  size_t i = l;
  int cont = 0;
  while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80 && cont < 4) {
    i--;
    cont++;
  }
  if (i == 0) {
    s[0] = 0;
    return;
  }
  unsigned char lead = (unsigned char)s[i - 1];
  int need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
  if (lead >= 0x80 && cont < need) s[i - 1] = 0;  // incomplete sequence: drop it
}

void copyUtf8(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  snprintf(dst, cap, "%s", src ? src : "");
  if (src && strlen(src) >= cap) utf8Trim(dst);
}

bool parseHhmm(const char* s, uint16_t& minutes) {
  if (!s || strlen(s) != 4) return false;
  for (int i = 0; i < 4; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[2] - '0') * 10 + (s[3] - '0');
  if (h > 23 || m > 59) return false;
  minutes = (uint16_t)(h * 60 + m);
  return true;
}

namespace {

bool num(JsonVariantConst v, double& out) {
  if (!v.is<double>()) return false;
  out = v.as<double>();
  return !isnan(out) && !isinf(out);
}

// integer in [lo, hi] (the rules use `% 1 === 0`)
bool intIn(JsonVariantConst v, long lo, long hi, long& out) {
  double d;
  if (!num(v, d) || floor(d) != d || d < lo || d > hi) return false;
  out = (long)d;
  return true;
}

bool noCtl(const char* s) { return !strchr(s, '\n') && !strchr(s, '\r') && !strchr(s, '\t'); }

struct Ctx {
  Def& d;
  bool fail(const char* fmt, const char* a = "") {
    char m[80];
    snprintf(m, sizeof m, fmt, a);
    copyUtf8(d.err, sizeof d.err, m);
    d.invalid = true;
    return false;
  }
};

bool parseRule(JsonObjectConst o, const char* rid, Def& d) {
  d = Def();
  d.present = true;
  snprintf(d.rid, sizeof d.rid, "%s", rid);
  Ctx c{d};
  static const char* const REQ[] = {"name", "on", "metric", "scope", "op", "value", "mode", "sev", "email", "ntfy", "upd"};
  for (const char* k : REQ)
    if (o[k].isNull()) return c.fail("missing %s", k);

  // name: 1..40 UTF-16 units, no CR/LF/TAB (header injection guard)
  const char* name = o["name"].is<const char*>() ? o["name"].as<const char*>() : nullptr;
  if (!name || utf16Len(name) < 1 || utf16Len(name) > 40 || !noCtl(name)) return c.fail("bad name");
  copyUtf8(d.name, sizeof d.name, name);
  if (!o["on"].is<bool>()) return c.fail("bad on");
  d.on = o["on"].as<bool>();

  const char* metric = o["metric"].is<const char*>() ? o["metric"].as<const char*>() : "";
  if (!metricFromId(metric, d.metric)) {
    char m[24];
    snprintf(m, sizeof m, "%.16s", metric);
    return c.fail("unknown metric '%s'", m);
  }
  const MetricInfo& mi = info(d.metric);

  const char* scope = o["scope"].is<const char*>() ? o["scope"].as<const char*>() : "";
  if (!strcmp(scope, "inst")) d.scope = Scope::INST;
  else if (!strcmp(scope, "day")) d.scope = Scope::DAY;
  else if (!strcmp(scope, "month")) d.scope = Scope::MONTH;
  else return c.fail("bad scope");

  const char* op = o["op"].is<const char*>() ? o["op"].as<const char*>() : "";
  if (!strcmp(op, "gt")) d.op = Op::GT;
  else if (!strcmp(op, "ge")) d.op = Op::GE;
  else if (!strcmp(op, "lt")) d.op = Op::LT;
  else if (!strcmp(op, "le")) d.op = Op::LE;
  else return c.fail("bad op");

  double v;
  if (!num(o["value"], v) || v < mi.vmin || v > mi.vmax) return c.fail("value out of range");
  if (mi.unit == Unit::BOOL && v != 0 && v != 1) return c.fail("value must be 0 or 1");
  d.value = (float)v;

  const char* mode = o["mode"].is<const char*>() ? o["mode"].as<const char*>() : "";
  if (!strcmp(mode, "hold")) d.mode = Mode::HOLD;
  else if (!strcmp(mode, "at")) d.mode = Mode::AT;
  else return c.fail("bad mode");

  long l;
  if (!intIn(o["sev"], 1, 5, l)) return c.fail("bad sev");
  d.sev = (uint8_t)l;
  if (!o["email"].is<bool>() || !o["ntfy"].is<bool>()) return c.fail("bad channel flags");
  d.email = o["email"].as<bool>();
  d.ntfy = o["ntfy"].as<bool>();
  double upd;
  if (!num(o["upd"], upd)) return c.fail("bad upd");

  // optional fields (defaults per r8 §3.3)
  d.forSec = mi.energy ? 0 : 60;
  if (!o["at"].isNull()) {
    const char* at = o["at"].is<const char*>() ? o["at"].as<const char*>() : "";
    if (!parseHhmm(at, d.atMin)) return c.fail("bad at");
  }
  if (!o["forSec"].isNull()) {
    if (!intIn(o["forSec"], 0, 86400, l)) return c.fail("bad forSec");
    d.forSec = (uint32_t)l;
  }
  if (!o["clr"].isNull()) {
    double cv;
    if (!num(o["clr"], cv) || cv < -10000 || cv > 5000000) return c.fail("bad clr");
    d.clr = (float)cv;
    d.hasClr = true;
  }
  if (!o["clrSec"].isNull()) {
    if (!intIn(o["clrSec"], 0, 3600, l)) return c.fail("bad clrSec");
    d.clrSec = (uint32_t)l;
  }
  if (!o["coolMin"].isNull()) {
    if (!intIn(o["coolMin"], 5, 10080, l)) return c.fail("bad coolMin");
    d.coolMin = (uint16_t)l;
  }
  if (!o["repeatMin"].isNull()) {
    if (!intIn(o["repeatMin"], 0, 1440, l) || (l != 0 && l < 30)) return c.fail("bad repeatMin");
    d.repeatMin = (uint16_t)l;
  }
  if (!o["maxDay"].isNull()) {
    if (!intIn(o["maxDay"], 1, 10, l)) return c.fail("bad maxDay");
    d.maxDay = (uint8_t)l;
  }
  if (!o["minCov"].isNull()) {
    if (!intIn(o["minCov"], 0, 100, l)) return c.fail("bad minCov");
    d.minCov = (uint8_t)l;
  }
  if (!o["win"].isNull()) {
    JsonObjectConst w = o["win"].as<JsonObjectConst>();
    if (w.isNull()) return c.fail("bad win");
    const char* f = w["from"].is<const char*>() ? w["from"].as<const char*>() : "";
    const char* t = w["to"].is<const char*>() ? w["to"].as<const char*>() : "";
    if (!parseHhmm(f, d.winFrom) || !parseHhmm(t, d.winTo) || d.winFrom == d.winTo) return c.fail("bad win");
    if (!intIn(w["days"], 1, 127, l)) return c.fail("bad win.days");
    d.days = (uint8_t)l;
    d.hasWin = true;
  }
  if (!o["rc"].isNull()) {
    if (!intIn(o["rc"], 1, 7, l)) return c.fail("bad rc");
    d.rc = (uint8_t)l;
  }
  if (!o["clrMail"].isNull()) {
    if (!o["clrMail"].is<bool>()) return c.fail("bad clrMail");
    d.clrMail = o["clrMail"].as<bool>();
  }

  // cross-field constraints (the $rid .validate of r8 §3.11)
  if (mi.energy ? d.scope == Scope::INST : d.scope != Scope::INST) return c.fail("scope does not fit metric");
  bool gtge = d.op == Op::GT || d.op == Op::GE;
  if (d.scope == Scope::MONTH && (!gtge || d.mode != Mode::HOLD)) return c.fail("month needs gt/ge + hold");
  if (d.scope == Scope::DAY && !gtge && d.mode != Mode::AT) return c.fail("day lt/le needs mode at");
  if (d.mode == Mode::AT && o["at"].isNull()) return c.fail("mode at needs at");
  if (d.hasClr && (gtge ? d.clr > d.value : d.clr < d.value)) return c.fail("clr on the wrong side");
  if (!d.email && !d.ntfy) return c.fail("no channel");

  d.semHash = semanticHash(d);
  return true;
}

void parseEmail(JsonObjectConst e, EmailPolicy& p) {
  p = EmailPolicy();
  if (e.isNull()) return;
  p.on = e["on"].is<bool>() && e["on"].as<bool>();
  long l;
  if (intIn(e["monthStart"], 1, 28, l)) p.monthStart = (uint8_t)l;
  if (e["to"].is<const char*>()) snprintf(p.to, sizeof p.to, "%s", e["to"].as<const char*>());
  if (e["fromName"].is<const char*>()) snprintf(p.fromName, sizeof p.fromName, "%s", e["fromName"].as<const char*>());
  if (intIn(e["maxPerDay"], 1, 40, l)) p.maxPerDay = (uint8_t)l;
  JsonObjectConst b = e["builtin"].as<JsonObjectConst>();
  if (!b.isNull()) {
    if (intIn(b["minSev"], 0, 5, l) && (l == 0 || l == 3 || l == 4 || l == 5)) p.builtinMinSev = (uint8_t)l;
    if (b["clear"].is<bool>()) p.builtinClear = b["clear"].as<bool>();
  }
}

}  // namespace

bool parse(const char* json, size_t n, Set& out, char* err, size_t errn) {
  if (errn) err[0] = 0;
  JsonDocument doc(jsonalloc::get());
  DeserializationError e = deserializeJson(doc, json, n);
  if (e) {
    if (errn) snprintf(err, errn, "alerts.json: %s", e.c_str());
    return false;
  }
  // reset in place: a `Set()` temporary would put ~3.4 KB on the caller's stack (aggTask has 10 KB)
  for (int i = 0; i < cfg::MAX_RULES; i++) out.d[i] = Def();
  out.n = out.nInvalid = 0;
  out.email = EmailPolicy();
  out.err[0] = 0;
  if (doc.isNull()) return true;  // nothing configured
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) {
    if (errn) snprintf(err, errn, "alerts.json: not an object");
    return false;
  }
  parseEmail(root["email"].as<JsonObjectConst>(), out.email);
  JsonObjectConst rs = root["rules"].as<JsonObjectConst>();
  for (JsonPairConst kv : rs) {
    const char* rid = kv.key().c_str();
    int slot = slotFromRid(rid);
    if (slot < 0) {  // the database cannot hold such a key (r8 §3.11); ignore defensively
      if (!out.err[0]) snprintf(out.err, sizeof out.err, "ignored rule key '%.8s'", rid);
      continue;
    }
    Def& d = out.d[slot];
    JsonObjectConst o = kv.value().as<JsonObjectConst>();
    if (o.isNull()) {
      d = Def();
      d.present = true;
      snprintf(d.rid, sizeof d.rid, "%s", rid);
      d.invalid = true;
      snprintf(d.err, sizeof d.err, "not an object");
    } else {
      parseRule(o, rid, d);
    }
    out.n++;
    if (d.invalid) {
      out.nInvalid++;
      if (!out.err[0]) {
        char m[64];
        snprintf(m, sizeof m, "%s: %s", d.rid, d.err);
        copyUtf8(out.err, sizeof out.err, m);
      }
    }
  }
  return true;
}

}  // namespace rules
