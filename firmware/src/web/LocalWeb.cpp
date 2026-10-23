// LocalWeb.cpp - local UI + JSON API on http://solar.local (core WebServer, sync stack; d1 §5.10, K8).
#include "web/LocalWeb.h"

#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app/App.h"
#include "codes.h"
#include "config.h"
#include "meter/Meter.h"
#include "net/RemoteOta.h"
#include "model/JsonOut.h"
#include "model/LiveJson.h"
#include "model/TimeUtil.h"
#include "net/TimeSync.h"
#include "net/Uplink.h"
#include "net/WifiProv.h"
#include "store/MinuteQueue.h"
#include "store/Settings.h"
#include "sys/SafeMode.h"

LocalWeb localWeb;
static WebServer server(80);

// ------------------------------------------------------------------------------------------- auth
static bool needsAdmin(const String& u, HTTPMethod m) {
  if (u.startsWith("/update") || u.startsWith("/ota/")) return true;
  if (u.startsWith("/api/diag") || u.startsWith("/api/link") || u.startsWith("/api/capture")) return true;
  if (u == "/api/reboot" || u.startsWith("/api/safemode") || u == "/api/config") return true;
  if (u == "/telegram" || u == "/api/test") return true;
  if (u == "/meter" || u == "/api/meter") return true;  // 0.9.0 grid meter set-up (local key)
  if (u == "/api/probe" && m == HTTP_POST) return true;
  return false;
}

static bool adminOk() {
  return server.authenticate([](HTTPAuthMethod mode, String user, String params[]) -> String* {
    if (mode != BASIC_AUTH || user != ADMIN_USER) return nullptr;
    if (!settings.adminCheck(params[0])) return nullptr;
    return new String(params[0]);  // WebServer compares it (constant time) with the entered password
  });
}

class AdminMiddleware : public Middleware {
 public:
  bool run(WebServer& s, Middleware::Callback next) override {
    if (!needsAdmin(s.uri(), s.method())) return next();
    if (adminOk()) return next();
    if (s.uri().startsWith("/api/")) {
      // no WWW-Authenticate for fetch() calls: the page sends the Authorization header itself
      s.send(401, "application/json", "{\"error\":\"admin password required\"}");
      return true;
    }
    s.requestAuthentication(BASIC_AUTH, "solar", "admin password required");
    return true;
  }
};
static AdminMiddleware adminMw;

// ------------------------------------------------------------------------------------------- helpers
static void sendJson(const char* buf, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", buf);
}

static void sendMsg(int code, const char* msg) {
  char b[260];
  JsonOut j(b, sizeof b);
  j.open();
  j.b("ok", code < 300);
  j.s("msg", msg);
  j.close();
  sendJson(b, code);
}

static void post(DiagKind k, uint8_t arg, bool flag, const char* okMsg) {
  DiagRequest r;
  r.kind = k;
  r.arg = arg;
  r.flag = flag;
  if (app.postDiag(r)) sendMsg(202, okMsg);
  else sendMsg(503, "diagnostics busy - try again");
}

// stream a large JSON document in chunks
class Chunked {
 public:
  explicit Chunked(const char* type = "application/json") {
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, type, "");
  }
  void add(const char* s) {
    size_t l = strlen(s);
    if (n_ + l >= sizeof b_) flush();
    if (l >= sizeof b_) { server.sendContent(s, l); return; }
    memcpy(b_ + n_, s, l);
    n_ += l;
  }
  void addf(const char* fmt, ...) {
    char t[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    add(t);
  }
  void num(float v, int dec) {
    if (isnan(v)) add("null");
    else addf("%.*f", dec, v);
  }
  void flush() {
    if (n_) server.sendContent(b_, n_);
    n_ = 0;
  }
  void end() {
    flush();
    server.sendContent("");
  }

 private:
  char b_[1024];
  size_t n_ = 0;
};

// ------------------------------------------------------------------------------------------- handlers
static void hRoot() {
  const char* gz = "/www/index.html.gz";
  const char* pl = "/www/index.html";
  if (minuteQueue.mounted() && LittleFS.exists(gz)) {
    File f = LittleFS.open(gz, FILE_READ);
    server.sendHeader("Content-Encoding", "gzip");
    server.streamFile(f, "text/html");
    f.close();
    return;
  }
  if (minuteQueue.mounted() && LittleFS.exists(pl)) {
    File f = LittleFS.open(pl, FILE_READ);
    server.streamFile(f, "text/html");
    f.close();
    return;
  }
  server.send(200, "text/html",
              "<!doctype html><meta name=viewport content='width=device-width'><title>Savx logger</title>"
              "<body style='font-family:sans-serif;padding:16px'><h1>Savx logger</h1>"
              "<p>The local UI is not in flash yet: run <code>pio run -e savx -t uploadfs</code>.</p>"
              "<p><a href='/api/live'>/api/live</a> &middot; <a href='/api/status'>/api/status</a> &middot; "
              "<a href='/api/raw'>/api/raw</a> &middot; <a href='/telegram'>Telegram alerts</a> &middot; <a href='/meter'>Grid meter</a> &middot; <a href='/update'>/update</a></p>");
}

static void hLive() {
  char b[1000];
  uint32_t age;
  Sample s = app.latest(&age);
  MeterLive ml;
  bool m = meter::live(ml);
  sampleToLiveJson(s, b, sizeof b, age == 0xFFFFFFFF ? -1 : (int32_t)age, m ? &ml : nullptr);
  sendJson(b);
}

static void hIdentity() {
  char b[700];
  Identity id = app.identity();
  identityToMetaJson(id, FW_VERSION, app.bootEpoch, b, sizeof b);
  sendJson(b);
}

static void hStatus() {
  static char* b = nullptr;  // PSRAM
  static const size_t B_CAP = 4096;
  if (!b) b = (char*)psramAlloc(B_CAP);
  if (!b) { sendMsg(500, "out of memory"); return; }
  LinkSnapshot l = app.link();
  UplinkStatus up = app.uplinkStatus();
  uint32_t age;
  Sample s = app.latest(&age);
  Identity id = app.identity();
  JsonOut j(b, B_CAP);
  j.open();
  j.u("ts", timeSync.epochOrZero());
  j.s("fw", FW_VERSION);
  j.s("ip", WiFi.localIP().toString().c_str());
  j.i("rssi", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  j.u("heap", ESP.getFreeHeap());
  j.u("psram", ESP.getFreePsram());
  j.u("up", millis() / 1000);
  j.s("rr", app.resetReason);
  j.u("boot", app.bootId);
  j.b("safe", app.safeMode);
  j.u("bootFails", safemode::bootFails());
  j.i("q", (long long)minuteQueue.pending());
  j.i("qprov", (long long)minuteQueue.provisional());
  j.u("fsUsed", minuteQueue.usedBytes());
  j.u("fsTotal", minuteQueue.totalBytes());
  j.s("timeSrc", timeSync.source());
  j.b("wifi", WiFi.status() == WL_CONNECTED);
  j.b("portal", wifiProv.portalActive());
  char proto[24];
  snprintf(proto, sizeof proto, "%s-%lu", l.adapter, (unsigned long)l.baud);  // e.g. "pi30-2400"
  j.s("proto", proto);
  j.s("dia", l.dialect);
  j.s("sup", id.sup);
  j.s("link", l.profile);
  j.b("armed", l.armed);
  j.s("engine", engineStateName(l.state));
  j.b("lk", l.linkOk);
  j.s("mode", linkModeName(l.mode));
  j.i("roPin", l.roPin);
  j.i("diPin", l.diPin);
  j.b("selfId", l.diConfirmed);
  j.i("invTxRj", l.invTxRj);
  j.i("armRx", l.armRx);
  j.u("tx", l.ls.txFrames);
  j.u("rxOk", l.ls.ok);
  j.u("nak", l.ls.nak);
  j.u("timeouts", l.ls.timeouts);
  j.u("crcErr", l.ls.badCrc);
  j.u("badStart", l.ls.badStart);
  j.u("refused", l.ls.refused);
  j.u("fastOk", l.es.fastOk);
  j.u("fastFail", l.es.fastFail);
  uint32_t tot = l.es.fastOk + l.es.fastFail;
  j.num("pollPct", tot ? 100.0 * l.es.fastOk / tot : NAN, 1);
  j.u("probes", l.es.probes);
  j.u("reprobes", l.es.reprobes);
  j.s("probe", l.probeSummary);
  j.i("ageMs", age == 0xFFFFFFFF ? -1 : (long long)age);
  j.i("alertsQueued", alertQueue.size());
  j.b("cloud", up.enabled);
  j.b("auth", up.authOk);
  j.s("upState", up.state);
  j.s("upErr", up.lastError);
  j.u("cok", up.lastCloudOkEpoch);
  j.u("hs", up.handshakesToday);
  j.u("rq", up.requestsToday);
  j.i("qg", up.quotaLevel);
  j.b("backfill", up.backfilling);
  j.u("backlog", up.backlog);
  j.u("edrop", up.edrop);
  j.i("evPending", up.pendingEvents);
  j.openObj("notify");
  j.b("ntfy", up.ntfyOn);
  j.b("telegram", up.telegramOn);
  j.b("healthchecks", up.healthcheckOn);
  j.b("nudge", up.nudgeOn);
  j.u("pushOk", up.pushesOk);
  j.u("pushFail", up.pushesFail);
  j.b("email", up.emailOn);
  j.u("mailOk", up.mailsOk);
  j.u("mailFail", up.mailsFail);
  j.i("hcCode", up.lastHcCode);
  j.close();
  {
    meter::Snap ms = meter::snap();
    j.openObj("meter");
    j.b("configured", ms.configured);
    j.b("enabled", ms.enabled);
    j.b("running", ms.running);
    j.b("fresh", ms.fresh);
    j.s("state", ms.state);
    j.s("ip", ms.ip);
    j.i("proto", ms.proto);
    j.u("ageS", ms.ageS);
    j.u("polls", ms.polls);
    j.u("fails", ms.fails);
    j.u("scans", ms.scans);
    j.u("resets", ms.resets);
    if (ms.fresh) meterLiveFields(j, ms.m);
    j.close();
  }
  {
    char os[48];
    rota::status(os, sizeof os);
    j.openObj("ota");
    j.s("repo", rota::repo());
    j.s("state", os);
    rota::check(os, sizeof os);
    j.s("check", os);
    j.b("probation", rota::inProbation());
    j.close();
  }
  {  // usage-threshold rules (r8 §4.1): rules:{n,rev,acat,st:[...]}
    RuleInfo ri = app.ruleInfo();
    j.openObj("rules");
    j.i("n", ri.n);
    j.i("present", ri.present);
    j.i("invalid", ri.invalid);
    j.num("rev", ri.rev, 0);
    j.num("cloudRev", up.cloudAlertsRev, 0);
    j.i("acat", cfg::ALERT_CATALOG_VERSION);
    j.b("email", ri.emailOn);
    j.i("mjq", ri.jobsToday);
    j.i("monthStart", ri.monthStart);
    j.s("err", up.ruleErr[0] ? up.ruleErr : ri.err);
    j.openArr("st");
    for (int i = 0; i < ri.nSlots; i++) {
      j.elOpen();
      j.s("rid", ri.slot[i].rid);
      j.s("st", rules::stName((rules::St)ri.slot[i].st));
      j.i("cnt", ri.slot[i].cnt);
      j.close();
    }
    j.closeArr();
    j.close();
  }
  j.num("chipTemp", temperatureRead(), 1);
  j.b("adminDefault", settings.adminIsDefault());
  j.close();
  sendJson(b);
}

static void hRaw() {
  LastReply* lr = (LastReply*)psramAlloc(sizeof(LastReply) * RawRing::NLAST);
  RawExchange* ex = (RawExchange*)psramAlloc(sizeof(RawExchange) * RawRing::N);
  if (!lr || !ex) { free(lr); free(ex); sendMsg(500, "out of memory"); return; }
  int n = app.copyLastReplies(lr, RawRing::NLAST);
  int m = app.copyExchanges(ex, RawRing::N);
  Chunked c;
  c.add("{\"last\":[");
  for (int i = 0; i < n; i++) {
    c.addf("%s{\"cmd\":\"%s\",\"res\":\"%s\",\"ageS\":%lu,\"text\":\"", i ? "," : "", lr[i].cmd,
           linkResultName((LinkResult)lr[i].result), (unsigned long)((millis() - lr[i].upMs) / 1000));
    c.add(lr[i].text);  // already escaped (\xHH for non-printables, no quotes/backslashes)
    c.add("\"}");
  }
  c.add("],\"recent\":[");
  for (int i = 0; i < m; i++) {
    char tx[80], rxh[620], rxa[300];
    RawRing::hex(ex[i].tx, ex[i].txLen, tx, sizeof tx);
    RawRing::hex(ex[i].rx, ex[i].rxLen, rxh, sizeof rxh);
    RawRing::render(ex[i].rx, ex[i].rxLen, rxa, sizeof rxa);
    c.addf("%s{\"cmd\":\"%s\",\"res\":\"%s\",\"ageS\":%lu,\"tx\":\"", i ? "," : "", ex[i].cmd,
           linkResultName((LinkResult)ex[i].result), (unsigned long)((millis() - ex[i].upMs) / 1000));
    c.add(tx);
    c.add("\",\"rxHex\":\"");
    c.add(rxh);
    c.add("\",\"rx\":\"");
    c.add(rxa);
    c.add("\"}");
  }
  c.add("]}");
  c.end();
  free(lr);
  free(ex);
}

static void hRing() {
  int mins = server.hasArg("min") ? server.arg("min").toInt() : 1440;
  int step = server.hasArg("step") ? server.arg("step").toInt() : 60;
  if (mins < 5) mins = 5;
  if (mins > 1440) mins = 1440;
  if (step < 5) step = 5;
  if (step > 3600) step = 3600;
  int maxB = mins * 60 / step;
  if (maxB > 2880) maxB = 2880;
  SampleRing::Bucket* b = (SampleRing::Bucket*)psramAlloc(sizeof(SampleRing::Bucket) * (size_t)maxB);
  if (!b) { sendMsg(500, "out of memory"); return; }
  uint32_t now = millis();
  int n = aggDownsample(now, (uint32_t)mins * 60, (uint32_t)step, b, maxB);
  uint32_t ep = timeSync.epochOrZero();
  Chunked c;
  c.addf("{\"now\":%lu,\"rel\":%d,\"step\":%d,\"t\":[", (unsigned long)ep, ep ? 0 : 1, step);
  for (int i = 0; i < n; i++) c.addf("%s%ld", i ? "," : "", ep ? (long)(ep + b[i].tRel) : (long)b[i].tRel);
  const char* names[] = {"w", "pvw", "bw", "bv", "soc", "gv"};
  for (int k = 0; k < 6; k++) {
    c.addf("],\"%s\":[", names[k]);
    for (int i = 0; i < n; i++) {
      if (i) c.add(",");
      float v = k == 0 ? b[i].w : k == 1 ? b[i].pvw : k == 2 ? b[i].bw : k == 3 ? b[i].bv : k == 4 ? b[i].soc : b[i].gv;
      c.num(v, k == 3 ? 2 : 0);
    }
  }
  c.add("]}");
  c.end();
  free(b);
}

static void hHist() {
  String date = server.arg("date");
  int32_t day;
  if (!tu::parseDateKey(date.c_str(), day)) {
    uint32_t e = timeSync.epochOrZero();
    if (!e) { sendMsg(400, "date=YYYY-MM-DD required (no clock yet)"); return; }
    day = tu::localDayNumber(e);
  }
  uint32_t n = minuteQueue.count(day);
  if (n > 1500) n = 1500;
  MinuteRec* r = (MinuteRec*)psramAlloc(sizeof(MinuteRec) * (n ? n : 1));
  if (!r) { sendMsg(500, "out of memory"); return; }
  int got = n ? minuteQueue.read(day, 0, r, (int)n) : 0;
  char dk[12];
  tu::dateKeyFromDay(day, dk, sizeof dk);
  Chunked c;
  c.addf("{\"date\":\"%s\",\"n\":%d,\"uploaded\":%lu,\"t\":[", dk, got, (unsigned long)minuteQueue.cursor(day));
  for (int i = 0; i < got; i++) c.addf("%s%lu", i ? "," : "", (unsigned long)r[i].ts);
  struct F { const char* k; int kind; } fs[] = {{"w", 0}, {"wx", 1}, {"pvw", 2}, {"bv", 3}, {"bvn", 4}, {"soc", 5},
                                                {"gv", 6}, {"ov", 7}, {"ld", 8}, {"epv", 9}, {"eld", 10}, {"egr", 11},
                                                {"ech", 12}, {"edis", 13}, {"n", 14}};
  for (const F& f : fs) {
    c.addf("],\"%s\":[", f.k);
    for (int i = 0; i < got; i++) {
      const MinuteRec& m = r[i];
      if (i) c.add(",");
      switch (f.kind) {
        case 0: c.num(m.w == mrec::I16_NA ? NAN : m.w, 0); break;
        case 1: c.num(m.wx == mrec::I16_NA ? NAN : m.wx, 0); break;
        case 2: c.num(m.pvw == mrec::I16_NA ? NAN : m.pvw, 0); break;
        case 3: c.num(m.bv == mrec::U16_NA ? NAN : m.bv / 100.f, 2); break;
        case 4: c.num(m.bvn == mrec::U16_NA ? NAN : m.bvn / 100.f, 2); break;
        case 5: c.num(m.soc == mrec::U8_NA ? NAN : m.soc, 0); break;
        case 6: c.num(m.gv == mrec::U16_NA ? NAN : m.gv / 10.f, 1); break;
        case 7: c.num(m.ov == mrec::U16_NA ? NAN : m.ov / 10.f, 1); break;
        case 8: c.num(m.ld == mrec::U8_NA ? NAN : m.ld, 0); break;
        case 9: c.num(m.epv / 10.f, 1); break;
        case 10: c.num(m.eld / 10.f, 1); break;
        case 11: c.num(m.egr / 10.f, 1); break;
        case 12: c.num(m.ech / 10.f, 1); break;
        case 13: c.num(m.edis / 10.f, 1); break;
        default: c.addf("%u", m.n); break;
      }
    }
  }
  c.add("],\"md\":\"");
  for (int i = 0; i < got; i++) {
    char t[2] = {r[i].md ? r[i].md : '-', 0};
    c.add(t);
  }
  c.add("\"}");
  c.end();
  free(r);
}

static void hDays() {
  int32_t days[64];
  int n = minuteQueue.listDays(days, 64);
  Chunked c;
  c.add("{\"days\":[");
  for (int i = 0; i < n; i++) {
    char dk[12];
    tu::dateKeyFromDay(days[i], dk, sizeof dk);
    c.addf("%s{\"date\":\"%s\",\"n\":%lu,\"up\":%lu}", i ? "," : "", dk, (unsigned long)minuteQueue.count(days[i]),
           (unsigned long)minuteQueue.cursor(days[i]));
  }
  c.add("]}");
  c.end();
}

static void hProbeGet() {
  String r = app.probeReport();
  if (!r.length()) {
    char* b = (char*)psramAlloc(8192);
    size_t n = b ? minuteQueue.readFile("/log/probe.json", b, 8192) : 0;
    if (n) { sendJson(b); free(b); return; }
    free(b);
    sendJson("{\"ok\":false,\"summary\":\"no probe has run yet\"}");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", r);
}

static void hDiagPins() {
  static char b[4096];  // up to 12 idle results + messages
  DiagState d = app.diag();
  LinkSnapshot l = app.link();
  ArmCheck ac = PinDiag::armCheck(d.idle, l.armRx ? l.armRx : 0);
  JsonOut j(b, sizeof b);
  j.open();
  j.b("busy", d.busy);
  j.s("running", d.running);
  if (d.busy) j.u("elapsedMs", millis() - d.startedMs);
  j.u("expectMs", d.expectMs);
  j.u("seq", d.seq);
  j.s("lastMsg", d.lastMsg);
  j.b("armed", l.armed);
  j.s("mode", linkModeName(l.mode));
  j.i("roPin", l.roPin);
  j.i("diPin", l.diPin);
  j.b("selfIdDone", l.diConfirmed);
  j.i("txGpio", cfg::PIN_INV_TX);
  j.s("txGpioState", l.armed ? "UART TX (armed)" : "high-Z input (not armed)");
  j.openObj("selfId");
  j.s("verdict", selfIdName(d.selfId.verdict));
  j.i("lowA", d.selfId.lowA);
  j.i("lowB", d.selfId.lowB);
  j.i("samples", d.selfId.samples);
  j.i("roPin", d.selfId.roPin);
  j.i("diPin", d.selfId.diPin);
  j.s("msg", d.selfId.msg);
  j.close();
  j.openArr("idle");
  auto idleEl = [&](const IdleResult& r) {
    if (r.verdict == IdleVerdict::NONE) return;
    j.elOpen();
    j.i("rj", r.rjPin);
    j.i("gpio", r.gpio);
    j.s("ref", idleRefName(r.ref));
    j.num("pctLow", r.pctLow, 1);
    j.i("edges", r.edges);
    j.i("samples", r.samples);
    j.s("verdict", idleVerdictName(r.verdict));
    j.u("seq", r.seq);
    j.close();
  };
  for (int pin = 1; pin <= 2; pin++) {
    for (int k = 0; k < IdleHistory::N; k++) idleEl(d.idle.r[pin - 1][k]);
    for (int k = 0; k < IdleHistory::NUP; k++) idleEl(d.idle.up[pin - 1][k]);
  }
  j.closeArr();
  j.openObj("arm");
  j.b("ready", ac.txRj != 0);
  j.i("txRj", ac.txRj);
  j.i("rxRj", ac.rxRj);
  j.i("upperPin", ac.upperPin);
  j.s("reason", ac.reason);
  j.close();
  j.openObj("loopback");
  j.b("ran", d.loop.ran);
  j.b("inverted", d.loop.invertedOk);
  j.b("normal", d.loop.normalOk);
  j.s("detail", d.loop.detail);
  j.close();
  j.close();
  sendJson(b);
}

static void hSelfTest() { post(DiagKind::SELF_ID, 0, false, "self-ID running (0.5 s) - poll /api/diag/pins"); }

static void hIdle() {
  int pin = server.arg("pin").toInt();
  if (pin != 1 && pin != 2) { sendMsg(400, "pin=1|2 required"); return; }
  bool up = server.arg("ref") == "3v3";  // upper check: module B- on ESP 3V3 (safety review)
  post(DiagKind::IDLE, (uint8_t)pin, up,
       up ? "upper check running (10 s) - poll /api/diag/pins" : "idle-level test running (10 s) - poll /api/diag/pins");
}

static void hLoopback() {
  bool bench = server.arg("bench") == "1";
  if (!bench) { sendMsg(400, "bench=1 required: GPIO17<->GPIO18 jumper, module and inverter DISCONNECTED"); return; }
  post(DiagKind::LOOPBACK, 0, true, "loopback running - poll /api/diag/pins");
}

static void hArm() {
  if (server.arg("mode") == "max3232") {
    post(DiagKind::ARM_MAX3232, 0, server.arg("confirm") == "ARM-MAX3232", "arm (MAX3232) requested - see /api/diag/pins");
    return;
  }
  int pin = server.arg("pin").toInt();
  if (pin != 1 && pin != 2) { sendMsg(400, "pin=1|2 (the RJ45 pin GPIO17 is wired to) required"); return; }
  post(DiagKind::ARM, (uint8_t)pin, server.arg("confirm") == "ARM", "arm requested - see /api/diag/pins");
}

static void hReset() { post(DiagKind::RESET_LINK, server.arg("all") == "1" ? 1 : 0, false, "link reset (disarmed)"); }
static void hProbePost() { post(DiagKind::PROBE, 0, false, "probe requested"); }
static void hCapture() { post(DiagKind::CAPTURE, 0, server.arg("on") == "1", "capture setting changed"); }

static void hConfigGet() {
  char b[700];
  AlertCfg c;
  settings.loadCfg(c);
  JsonOut j(b, sizeof b);
  j.open();
  j.i("liveSec", c.liveSec);
  j.num("socLow", c.socLow, 0);
  j.num("bvLow", c.bvLow, 2);
  j.num("bvHigh", c.bvHigh, 2);
  j.num("tempHigh", c.tempHigh, 0);
  j.num("loadHigh", c.loadHigh, 0);
  j.i("notifyRepeatMin", c.notifyRepeatMin);
  j.i("quietFrom", c.quietFrom);
  j.i("quietTo", c.quietTo);
  j.i("summaryHour", c.summaryHour);
  j.i("gridOffFrom", c.gridOffFrom);
  j.i("gridOffTo", c.gridOffTo);
  j.i("meter", c.meter);
  j.s("otaRepo", rota::repo());  // 0.9.2 remote update source (local admin setting)
  // secrets are write-only: only report whether they are set
  j.b("cloudSet", settings.hasStr("cloud", "apiKey") && settings.hasStr("cloud", "email"));
  j.b("ntfySet", settings.hasStr("alert", "ntfyTopic"));
  j.b("telegramSet", settings.hasStr("alert", "tgToken"));
  j.b("telegramChat", settings.hasStr("alert", "tgChat"));
  {
    UplinkStatus up = app.uplinkStatus();
    j.i("tgChats", up.tgChats);
    j.b("tgPairing", up.tgPairing);
  }
  j.s("tgLang", settings.getStr("alert", "tgLang", "bn").c_str());
  j.b("hcSet", settings.hasStr("alert", "hcUrl"));
  j.b("nudgeSet", settings.hasStr("mail", "nudge"));
  j.s("appUrl", settings.getStr("alert", "appUrl", "").c_str());
  j.b("adminDefault", settings.adminIsDefault());
  j.close();
  sendJson(b);
}

static void hConfigPost() {
  AlertCfg c;
  settings.loadCfg(c);
  auto f = [](const char* k, float& v, float lo, float hi) {
    if (server.hasArg(k)) { float x = server.arg(k).toFloat(); if (x >= lo && x <= hi) v = x; }
  };
  auto i = [](const char* k, int& v, int lo, int hi) {
    if (server.hasArg(k)) { int x = server.arg(k).toInt(); if (x >= lo && x <= hi) v = x; }
  };
  // same ranges as the RTDB rules (d1 §4.7)
  f("socLow", c.socLow, 5, 80);
  f("bvLow", c.bvLow, 20, 30);
  f("bvHigh", c.bvHigh, 26, 34);
  f("tempHigh", c.tempHigh, 40, 100);
  f("loadHigh", c.loadHigh, 50, 120);
  i("notifyRepeatMin", c.notifyRepeatMin, 5, 1440);
  i("quietFrom", c.quietFrom, 0, 23);
  i("quietTo", c.quietTo, 0, 23);
  i("summaryHour", c.summaryHour, -1, 23);
  i("gridOffFrom", c.gridOffFrom, -1, 1439);
  i("gridOffTo", c.gridOffTo, -1, 1439);
  i("meter", c.meter, 0, 1);
  app.applyCloudCfg(c);
  meter::setEnabled(c.meter != 0);
  if (server.hasArg("otaRepo")) {  // 0.9.2: "owner/repo" of the releases, "-" turns remote updates off
    String r = server.arg("otaRepo");
    r.trim();
    if (!rota::setRepo(r == "-" ? "" : r.c_str())) {
      sendMsg(400, "otaRepo: owner/repo");
      return;
    }
  }
  if (server.hasArg("adminpw") && server.arg("adminpw").length() >= 6) settings.setAdminPassword(server.arg("adminpw"));
  // channel settings: write-only secrets ("-" clears), never echoed back
  bool chg = false;
  auto str = [&](const char* arg, const char* ns, const char* key) {
    if (!server.hasArg(arg)) return;
    String v = server.arg(arg);
    v.trim();
    if (v == "-") v = "";
    settings.setStr(ns, key, v);
    chg = true;
  };
  str("nudge", "mail", "nudge");      // v1.1 mailer nudge URL (r8 §1.2)
  str("appUrl", "alert", "appUrl");   // dashboard link in ntfy notifications
  str("ntfyTopic", "alert", "ntfyTopic");
  str("hcUrl", "alert", "hcUrl");
  str("tgToken", "alert", "tgToken");  // Telegram (0.7.7): token from @BotFather; the chat pairs itself (/start)
  str("tgChat", "alert", "tgChat");    // "-" forgets every paired chat (pairing starts again)
  if (server.hasArg("tgAdd")) uplink::telegramAddPerson();  // 10-minute window for one more person or group
  if (server.hasArg("tgLang")) {
    String l = server.arg("tgLang");
    settings.setStr("alert", "tgLang", l == "en" ? "en" : "bn");
    chg = true;
  }
  if (chg) uplink::notifyConfigChanged();
  sendMsg(200, "saved");
}

// Telegram set-up page (0.7.7, admin): paste the @BotFather token, pick the language, then press Start in the bot.
static const char TELEGRAM_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=en><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Telegram alerts</title>
<style>body{font-family:system-ui,sans-serif;max-width:520px;margin:0 auto;padding:16px;background:#0e1116;color:#e6edf3;line-height:1.45}
input,select,button{font:inherit;width:100%;padding:10px;margin:6px 0 14px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:inherit;box-sizing:border-box}
button{background:#2563eb;border:0;color:#fff;cursor:pointer}button.s{background:#334155}#st{padding:12px;border-radius:8px;background:#161b22;margin:12px 0}
code{background:#161b22;padding:1px 5px;border-radius:4px}a{color:#7aa2f7}</style>
<h1>Telegram alerts</h1><div id=st>Loading…</div>
<ol><li>In Telegram open <b>@BotFather</b>, send <code>/newbot</code>, choose a name and a username ending in <code>bot</code>. Copy the token it gives you.</li>
<li>Paste the token below and press <b>Save</b>.</li><li>Open your new bot in Telegram and press <b>Start</b>. Within about 15 s the logger pairs and the bot greets you.</li></ol>
<label>Bot token<input id=tok type=password autocomplete=off placeholder="123456789:AA…"></label>
<label>Message language<select id=lang><option value=bn>বাংলা (Bangla)</option><option value=en>English</option></select></label>
<button onclick=save()>Save</button><button class=s onclick=test()>Send a test alert</button>
<button class=s onclick=add()>Add another person (opens pairing for 10 min)</button><button class=s onclick=forget()>Forget all paired chats</button>
<p style="font-size:14px;opacity:.8">More people: press <b>Add another person</b>, then each of them opens the bot and presses Start within 10 minutes. A family group works too: add the bot to the group and send <code>/start</code> there during the window. Up to 5 chats.</p>
<p><a href="/">Logger home</a></p>
<script>
const $=id=>document.getElementById(id);let t=null;
async function load(){try{const c=await (await fetch('/api/config')).json();$('lang').value=c.tgLang||'bn';
const n=c.tgChats||0;$('st').textContent=!c.telegramSet?'Not set up yet: add a bot token.':
(c.tgPairing?(n?n+' of 5 paired. Waiting: the new person opens the bot and presses Start (window closes after 10 min)…':'Token saved. Now open your bot in Telegram and press Start…')
:(n?'Paired ✓ Alerts go to '+n+' Telegram chat'+(n>1?'s':'')+' (max 5).':'Token saved. Waiting for Start…'));
clearTimeout(t);if(c.telegramSet&&(c.tgPairing||!n))t=setTimeout(load,5000);}catch(e){$('st').textContent='Could not read the logger settings.'}}
async function post(b){const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams(b)});return r.ok}
async function save(){const b={tgLang:$('lang').value};const k=$('tok').value.trim();if(k)b.tgToken=k;
$('st').textContent=(await post(b))?'Saved.':'Save failed.';$('tok').value='';setTimeout(load,1500)}
async function test(){const r=await fetch('/api/test',{method:'POST'});$('st').textContent=r.ok?'Test alert raised: it should reach Telegram (and e-mail) within a few seconds.':'Test failed.'}
async function forget(){await post({tgChat:'-'});setTimeout(load,1500)}
async function add(){await post({tgAdd:'1'});$('st').textContent='Pairing open for 10 minutes…';setTimeout(load,2000)}
load();
</script></html>)HTML";

// Grid meter set-up page (0.9.0, admin): device id and local key from tools/meter/get-key.sh (the owner types them).
static const char METER_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=en><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Grid meter</title>
<style>body{font-family:system-ui,sans-serif;max-width:520px;margin:0 auto;padding:16px;background:#0e1116;color:#e6edf3;line-height:1.45}
input,select,button{font:inherit;width:100%;padding:10px;margin:6px 0 14px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:inherit;box-sizing:border-box}
button{background:#2563eb;border:0;color:#fff;cursor:pointer}button.s{background:#334155}#st{padding:12px;border-radius:8px;background:#161b22;margin:12px 0;white-space:pre-line}
code{background:#161b22;padding:1px 5px;border-radius:4px}a{color:#7aa2f7}</style>
<h1>Grid meter (Smart Life)</h1><div id=st>Loading…</div>
<p>Read-only: the logger only asks the meter for its readings. Smart Life keeps all control (on/off, schedule).</p>
<p>Run <code>tools/meter/get-key.sh</code> on the Mac, then copy the values from <code>.meter/secret.json</code>.</p>
<label>Device ID<input id=id autocomplete=off spellcheck=false></label>
<label>Local key (16 characters)<input id=key type=password autocomplete=off></label>
<label>Protocol<select id=proto><option value=0>Auto</option><option value=35>3.5</option><option value=34>3.4</option><option value=33>3.3</option></select></label>
<label>IP address (optional: the logger finds the meter on its own)<input id=ip autocomplete=off placeholder="e.g. 192.168.1.50"></label>
<button onclick=save()>Save</button><button class=s onclick=clr()>Forget the meter</button>
<p><a href="/">Logger home</a></p>
<script>
const $=id=>document.getElementById(id);let t=null;
async function load(){try{const s=await (await fetch('/api/status')).json();const m=s.meter||{};
let x=!m.configured?'Not set up yet.':!m.enabled?'Switched off in the app (Settings > Grid meter).':m.state;
if(m.fresh)x='Reading ✓  '+m.mw+' W, '+m.mv+' V, '+(m.mkwh??'?')+' kWh total\nIP '+m.ip+', protocol '+(m.proto/10).toFixed(1)+', '+m.ageS+' s ago';
x+='\npolls '+m.polls+', failures '+m.fails+', LAN searches '+m.scans;$('st').textContent=x;}catch(e){$('st').textContent='Could not read the logger status.'}
clearTimeout(t);t=setTimeout(load,5000)}
async function post(b){const r=await fetch('/api/meter',{method:'POST',body:new URLSearchParams(b)});const j=await r.json().catch(()=>({}));return [r.ok,j.msg||'']}
async function save(){const [ok,msg]=await post({id:$('id').value.trim(),key:$('key').value,proto:$('proto').value,ip:$('ip').value.trim()});
$('st').textContent=ok?'Saved. The first reading comes within about 15 s…':'Not saved: '+msg;if(ok)$('key').value='';setTimeout(load,4000)}
async function clr(){if(!confirm('Forget the meter key?'))return;await post({clear:'1'});setTimeout(load,1000)}
load();
</script></html>)HTML";

static void hMeterPage() { server.send_P(200, "text/html; charset=utf-8", METER_PAGE); }

static void hMeterPost() {
  if (server.arg("clear") == "1") {
    meter::clearConfig();
    sendMsg(200, "meter forgotten");
    return;
  }
  char err[80];
  String id = server.arg("id"), key = server.arg("key"), ip = server.arg("ip");
  id.trim();
  ip.trim();
  if (!meter::saveConfig(id.c_str(), key.c_str(), server.arg("proto").toInt(), ip.c_str(), err, sizeof err)) {
    sendMsg(400, err);
    return;
  }
  sendMsg(200, "saved");
}

static void hTelegram() {
  server.send_P(200, "text/html; charset=utf-8", TELEGRAM_PAGE);
}

static void hTest() {
  app.raise("TEST_ALERT", 3, "Test alert from the logger page", true);
  sendMsg(200, "test alert raised");
}

static void hReboot() {
  settings.setStr("sys", "plannedRb", "web");  // 0.8.0: a requested restart is log-only after boot
  sendMsg(200, "rebooting");
  delay(300);
  ESP.restart();
}

static void hSafeExit() {
  sendMsg(200, "leaving safe mode");
  delay(300);
  safemode::exitAndReboot();
}

// ------------------------------------------------------------------------------------------- lifecycle
void LocalWeb::begin() {
  if (!routes_) {
    server.addMiddleware(&adminMw);
    server.on("/", HTTP_GET, hRoot);
    server.on("/api/live", HTTP_GET, hLive);
    server.on("/api/status", HTTP_GET, hStatus);
    server.on("/api/identity", HTTP_GET, hIdentity);
    server.on("/api/raw", HTTP_GET, hRaw);
    server.on("/api/ring", HTTP_GET, hRing);
    server.on("/api/hist", HTTP_GET, hHist);
    server.on("/api/minutes", HTTP_GET, hHist);
    server.on("/api/days", HTTP_GET, hDays);
    server.on("/api/probe", HTTP_GET, hProbeGet);
    server.on("/api/probe", HTTP_POST, hProbePost);
    server.on("/api/diag/pins", HTTP_GET, hDiagPins);
    server.on("/api/diag/selftest", HTTP_ANY, hSelfTest);
    server.on("/api/diag/idle", HTTP_ANY, hIdle);
    server.on("/api/diag/loopback", HTTP_POST, hLoopback);
    server.on("/api/link/arm", HTTP_POST, hArm);
    server.on("/api/link/reset", HTTP_POST, hReset);
    server.on("/api/capture", HTTP_POST, hCapture);
    server.on("/api/config", HTTP_GET, hConfigGet);
    server.on("/api/config", HTTP_POST, hConfigPost);
    server.on("/api/reboot", HTTP_POST, hReboot);
    server.on("/telegram", HTTP_GET, hTelegram);
    server.on("/meter", HTTP_GET, hMeterPage);
    server.on("/api/meter", HTTP_POST, hMeterPost);
    server.on("/api/test", HTTP_POST, hTest);
    server.on("/api/safemode/exit", HTTP_POST, hSafeExit);
    server.onNotFound([]() { sendMsg(404, "not found"); });
    ElegantOTA.begin(&server);  // sync mode; /update and /ota/* are protected by AdminMiddleware
    ElegantOTA.onStart([]() { Serial.println(F("[ota] update started")); });
    ElegantOTA.onEnd([](bool ok) {
      if (ok) settings.setStr("sys", "plannedRb", "update");  // the version check also catches it (0.8.0)
      Serial.printf("[ota] update %s\n", ok ? "OK - rebooting" : "FAILED");
    });
    routes_ = true;
  }
  server.begin();
  running_ = true;
}

void LocalWeb::stop() {
  if (!running_) return;
  server.stop();
  running_ = false;
}

void LocalWeb::loop() {
  if (!running_) return;
  server.handleClient();
  ElegantOTA.loop();
  if (!mdns_ && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdns_ = true;
      Serial.printf("[web] http://%s.local  (http://%s)\n", MDNS_HOST, WiFi.localIP().toString().c_str());
    }
  }
}
