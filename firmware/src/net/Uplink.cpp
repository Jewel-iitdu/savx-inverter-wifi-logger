// Uplink.cpp - see Uplink.h. FW-2: replaces the FW-1 stub.
#include "net/Uplink.h"

#include <WiFi.h>
#include <esp_task_wdt.h>

#include "agg/RuleParse.h"
#include "app/App.h"
#include "config.h"
#include "hal/ArduinoHal.h"
#include "model/JsonOut.h"
#include "meter/Meter.h"
#include "model/LiveJson.h"
#include "model/TimeUtil.h"
#include "net/Backfill.h"
#include "net/Budget.h"
#include "net/FirebaseRest.h"
#include "net/MailNudge.h"
#include "net/OtaPlan.h"
#include "net/RemoteOta.h"
#include "net/Notify.h"
#include "net/TimeSync.h"
#include "net/UplinkJson.h"
#include "store/MinuteQueue.h"
#include "store/Settings.h"

namespace uplink {
namespace {

struct QueueStore : IMinuteStore {
  int listDays(int32_t* d, int max) override { return minuteQueue.listDays(d, max); }
  uint32_t count(int32_t day) override { return minuteQueue.count(day); }
  uint32_t cursor(int32_t day) override { return minuteQueue.cursor(day); }
  bool setCursor(int32_t day, uint32_t n) override { return minuteQueue.setCursor(day, n); }
  int read(int32_t day, uint32_t from, MinuteRec* out, int max) override { return minuteQueue.read(day, from, out, max); }
};

struct Pend {
  AlertEvent e;
  bool rtdbDone = false, pushDone = false, tgDone = false, mailDone = false;
  uint8_t rtdbTries = 0, pushTries = 0, tgTries = 0, mailTries = 0;
  uint32_t nextPushMs = 0, nextTgMs = 0, nextMailMs = 0;
};

// E-mail recipients/sender name from alerts/email (owner's dashboard, Alerts > E-mail); set when alerts.json loads.
bool mailCfgLoaded_ = false;
char mailTo_[301] = "";
char mailFromName_[41] = "";

enum class Prune : uint8_t { IDLE, HIST_DEL, EV_LIST, EV_DEL };

UplinkStatus st_;
FirebaseRest fb;
INotifier* notifier = nullptr;
Budget budget;
Backoff dbBackoff;
NetWatchdog netwd;
QueueStore store;
volatile bool metaDirty = true, credsDirty = true;
volatile bool tgAddReq = false;  // set by the local web page, handled in the uplink task
volatile uint32_t metaChangedMs = 0;
char* body_ = nullptr;
char* resp_ = nullptr;
size_t bodyCap_ = 0, respCap_ = 0;
Pend* pend_ = nullptr;  // [ALERT_QUEUE_DEPTH] in PSRAM
int npend_ = 0;
rules::Set* rulesBuf_ = nullptr;  // PSRAM
MinuteRec* recsBuf_ = nullptr;    // [BACKFILL_MAX_RECS] PSRAM
mrec::Daily* mtdBuf_ = nullptr;   // [31] PSRAM
AlertCfg cloudCfg_;
bool haveCloudCfg_ = false, firstCfg_ = true;
double cloudRev_ = 0, loadedRev_ = -1;
bool alertsDue_ = true;
uint32_t tLive_ = 0, tStatus_ = 0, tCfg_ = 0, tDiag_ = 0, tHc_ = 0, tBackfill_ = 0, tAlerts_ = 0, tMtd_ = 0, tPrune_ = 0,
         tStuck_ = 0, lastCommitMs_ = 0, tScan_ = 0;
bool hcFirst_ = true;
uint32_t accHs_ = 0, accRq_ = 0;
mrec::Daily fin_;
bool finHave_ = false;
uint32_t otaReq_ = 0;       // 0.9.2: a remote update to run (version code)
bool otaProbed_ = false;    // 0.9.6: the GitHub download path was checked this boot
bool meterFinHave_ = false;  // 0.9.0 grid meter: a finished day's metered energy to write
int32_t meterFinDay_ = 0, meterFinWh_ = -1;
Prune prune_ = Prune::IDLE;
int32_t pruneDoneDay_ = 0, stuckDay_ = 0;
int32_t* pruneDays_ = nullptr;
int pruneN_ = 0, pruneI_ = 0;
bool wasOffline_ = true;
uint32_t lastNetwdRestart_ = 0;  // NVS sys/nwdEp (epoch of the last network-watchdog reboot)

void saveToken(const char* rt, const char* uid) {
  settings.setStr("cloud", "rtok", String(rt ? rt : ""));
  if (uid && *uid) settings.setStr("cloud", "uid", String(uid));
}

void reloadCreds() {
  credsDirty = false;
  String apiKey = settings.getStr("cloud", "apiKey", ""), db = settings.getStr("cloud", "dbUrl", "");
  String em = settings.getStr("cloud", "email", ""), pw = settings.getStr("cloud", "pass", "");
  String rt = settings.getStr("cloud", "rtok", "");
  apiKey.trim();
  db.trim();
  em.trim();
  bool ok = fb.configure(apiKey, db, em, pw, rt, saveToken);
  st_.enabled = ok;
  if (!ok && (apiKey.length() || db.length() || em.length())) snprintf(st_.lastError, sizeof st_.lastError, "%s", fb.lastError());
  if (notifier) notifier->reload();
  mailnudge::reload();
  st_.ntfyOn = notifier && notifier->ntfyOn();
  st_.telegramOn = notifier && notifier->telegramOn();
  st_.healthcheckOn = notifier && notifier->healthcheckOn();
  st_.nudgeOn = mailnudge::configured();
  st_.emailOn = notifier && notifier->emailOn();
  Serial.printf("[up] cloud %s, ntfy %s, telegram %s, healthchecks %s, mailer nudge %s, smtp e-mail %s\n",
                ok ? "configured" : "OFF", st_.ntfyOn ? "on" : "off", st_.telegramOn ? "on" : "off",
                st_.healthcheckOn ? "on" : "off", st_.nudgeOn ? "on" : "off", st_.emailOn ? "on" : "off");
}

void account() {
  uint32_t h = fb.dbHandshakes(), r = fb.dbRequests();
  while (accHs_ < h) {
    budget.onHandshake();
    accHs_++;
  }
  while (accRq_ < r) {
    budget.onRequest();
    accRq_++;
  }
}

// bookkeeping for every RTDB answer; true on 2xx
bool ok(const FirebaseRest::Res& r, const char* what) {
  account();
  uint32_t now = millis();
  if (fb.lastConnectOkMs()) netwd.onConnectOk(fb.lastConnectOkMs());
  if (r.http > 0) netwd.onCloudOk(now);  // any HTTP answer: the network path works
  if (r.kind == FirebaseRest::Kind::OK) {
    dbBackoff.ok();
    uint32_t ep = timeSync.epochOrZero();
    if (ep) st_.lastCloudOkEpoch = ep;
    return true;
  }
  snprintf(st_.lastError, sizeof st_.lastError, "%s: %s", what, fb.lastError());
  if (r.kind != FirebaseRest::Kind::NOAUTH) dbBackoff.fail(now);
  Serial.printf("[up] %s failed (%s); retry in %lus\n", what, fb.lastError(), (unsigned long)(dbBackoff.lastDelayMs() / 1000));
  return false;
}

bool canGo() { return dbBackoff.ready(millis()) && fb.authOk(); }

void ensureHeap() {  // a one-shot TLS session needs ~22-42 KB of internal heap (V2 E9)
  if (ESP.getMaxAllocHeap() < 50000) fb.drop();
}

int liveSec() { return haveCloudCfg_ ? cloudCfg_.liveSec : 10; }

// ------------------------------------------------------------------------------------------- events
void pullAlerts() {
  AlertEvent e;
  while (npend_ < cfg::ALERT_QUEUE_DEPTH && alertQueue.pop(e)) {
    Pend& p = pend_[npend_++];
    p = Pend();
    p.e = e;
    p.rtdbDone = !fb.configured() || !upj::wantsRtdbWrite(e);
    p.pushDone = !(e.pushPrio > 0 && notifier && notifier->ntfyOn());
    p.tgDone = !(e.telegram && notifier && notifier->telegramOn());
    p.mailDone = !(!strcmp(e.em, "q") && notifier && notifier->emailOn());
    Serial.printf("[alert] %s %s sev%u: %s\n", e.type, e.st, e.sev, e.msg);
  }
}

void fixTimestamps() {
  uint32_t up, ep0;
  if (!timeSync.anchor(up, ep0)) return;
  for (int i = 0; i < npend_; i++) {
    AlertEvent& e = pend_[i].e;
    if (e.ts) continue;
    e.ts = e.upMs <= up ? ep0 - (up - e.upMs) / 1000 : ep0 + (e.upMs - up) / 1000;  // d1 §5.6
  }
}

void compactPend() {
  int w = 0;
  for (int i = 0; i < npend_; i++) {
    Pend& p = pend_[i];
    if (p.rtdbDone && p.pushDone && p.tgDone && p.mailDone) continue;
    if (w != i) pend_[w] = p;
    w++;
  }
  npend_ = w;
  st_.pendingEvents = (uint8_t)npend_;
}

// recipients for one event: all addresses of alerts/email/to, or only those selected by the rule's rc mask
int mailRecipients(const AlertEvent& e, char* buf, size_t bn, const char** to, int maxTo) {
  snprintf(buf, bn, "%s", mailTo_);
  int n = 0, idx = 0;
  char* save = nullptr;
  for (char* t = strtok_r(buf, ",", &save); t && n < maxTo; t = strtok_r(nullptr, ",", &save), idx++) {
    while (*t == ' ') t++;
    if (!*t) continue;
    if (e.rc && !(e.rc & (1u << idx))) continue;
    to[n++] = t;
  }
  return n;
}

void processMail(uint32_t now) {
  for (int i = 0; i < npend_; i++) {
    Pend& p = pend_[i];
    if (p.mailDone || (int32_t)(now - p.nextMailMs) < 0) continue;
    if (!mailCfgLoaded_) return;  // wait until alerts.json (recipients) is loaded
    char buf[301];
    const char* to[3];
    int n = mailRecipients(p.e, buf, sizeof buf, to, 3);
    if (!n) {  // nobody to send to: the owner has not added recipients
      p.mailDone = true;
      snprintf(st_.lastError, sizeof st_.lastError, "e-mail: no recipients in Alerts > E-mail");
      continue;
    }
    ensureHeap();
    NotifyResult r = notifier->email(p.e, to, n, mailFromName_);
    if (r.rateLimited) return;
    if (r.ok) {
      p.mailDone = true;
      st_.mailsOk++;
      Serial.printf("[mail] sent %s to %d recipient(s)\n", p.e.type, n);
    } else {
      st_.mailsFail++;
      snprintf(st_.lastError, sizeof st_.lastError, "e-mail: %s", r.error);
      Serial.printf("[mail] %s failed: %s\n", p.e.type, r.error);
      if (++p.mailTries >= cfg::PUSH_MAX_TRIES) p.mailDone = true;
      p.nextMailMs = now + 60000UL * p.mailTries;
    }
    return;  // at most one SMTP session per tick
  }
}

void processPushes(uint32_t now) {
  int sends = 0;
  for (int i = 0; i < npend_ && sends < 2; i++) {
    Pend& p = pend_[i];
    if (!p.pushDone && (int32_t)(now - p.nextPushMs) >= 0) {
      ensureHeap();
      NotifyResult r = notifier->ntfy(p.e, notifier->clickUrl());
      if (r.rateLimited) break;
      sends++;
      if (r.ok) {
        p.pushDone = true;
        st_.pushesOk++;
      } else {
        st_.pushesFail++;
        snprintf(st_.lastError, sizeof st_.lastError, "ntfy: %s", r.error);
        if (++p.pushTries >= cfg::PUSH_MAX_TRIES) p.pushDone = true;
        p.nextPushMs = now + 30000UL * p.pushTries;
      }
    }
    if (!p.tgDone && sends < 2 && (int32_t)(now - p.nextTgMs) >= 0) {
      ensureHeap();
      NotifyResult r = notifier->telegram(p.e);
      if (r.rateLimited) break;
      sends++;
      if (r.ok) {
        p.tgDone = true;
      } else {
        snprintf(st_.lastError, sizeof st_.lastError, "telegram: %s", r.error);
        if (++p.tgTries >= cfg::PUSH_MAX_TRIES) p.tgDone = true;  // Telegram may be blocked in BD (V2 §3.4)
        p.nextTgMs = now + 30000UL * p.tgTries;
      }
    }
  }
}

bool doEvents(uint32_t now, int32_t day) {
  int writes = 0;
  for (int i = 0; i < npend_ && writes < 4 && canGo(); i++) {
    Pend& p = pend_[i];
    if (p.rtdbDone || p.e.ts == 0) continue;  // no clock yet: wait for the time anchor
    char ch[20];
    upj::channels(p.e, notifier && notifier->ntfyOn(), notifier && notifier->telegramOn(), ch, sizeof ch);
    bool mq = false;
    AlertEvent ev = p.e;
    if (!strcmp(ev.em, "q") && notifier && notifier->emailOn()) snprintf(ev.em, sizeof ev.em, "sent");
    size_t n = upj::eventPatch(ev, ch, body_, bodyCap_, &mq);
    if (!n) {
      p.rtdbDone = true;
      st_.edrop++;
      continue;
    }
    FirebaseRest::Res r = fb.rtdb("PATCH", "", body_, n, resp_, respCap_);
    writes++;
    if (ok(r, "event")) {
      p.rtdbDone = true;
      if (mq) mailnudge::maybeNudge(now, day);  // v1.1: no-op unless mail/nudge is set
      continue;
    }
    if (r.kind == FirebaseRest::Kind::DENIED || r.kind == FirebaseRest::Kind::CLIENT) {
      if (++p.rtdbTries >= cfg::EVENT_MAX_TRIES) {  // never loop on a refused write (r8 §4.9)
        p.rtdbDone = true;
        st_.edrop++;
      }
    }
    break;
  }
  return writes > 0;
}

// ------------------------------------------------------------------------------------------- live / status / minutes
bool doLive(uint32_t now, uint32_t ep) {
  uint32_t period = budget.liveMs(liveSec());
  if (!period || (uint32_t)(now - tLive_) < period || !canGo()) return false;
  tLive_ = now;
  uint32_t age;
  Sample s = app.latest(&age);
  MeterLive ml;
  bool haveMeter = meter::live(ml);
  char b[1000];
  size_t n;
  if (s.lk) {
    if (!tu::epochValid(s.ts)) s.ts = ep;
    n = sampleToLiveJson(s, b, sizeof b, -1, haveMeter ? &ml : nullptr);
  } else {  // no valid inverter reply within 15 s: only "the logger is alive, the link is not" (+ the grid meter)
    JsonOut j(b, sizeof b);
    j.open();
    j.u("ts", ep);
    j.i("lk", 0);
    j.i("q", (long long)minuteQueue.pending());
    if (haveMeter) meterLiveFields(j, ml);
    j.close();
    n = j.len();
  }
  if (!n) return false;
  ok(fb.rtdb("PUT", "live", b, n, resp_, respCap_), "live");
  return true;
}

size_t buildStatus(char* out, size_t cap, uint32_t ep) {
  upj::StatusDoc s;
  s.ts = ep;
  s.fw = FW_VERSION;
  snprintf(s.ip, sizeof s.ip, "%s", WiFi.localIP().toString().c_str());
  s.rssi = WiFi.RSSI();
  s.heap = ESP.getFreeHeap();
  s.psram = ESP.getFreePsram();
  s.up = millis() / 1000;
  s.rr = app.resetReason;
  s.q = (long)minuteQueue.pending();
  LinkSnapshot l = app.link();
  snprintf(s.proto, sizeof s.proto, "%s-%lu", l.adapter, (unsigned long)l.baud);
  snprintf(s.dia, sizeof s.dia, "%s", l.dialect);
  s.hs = budget.handshakes();
  s.rq = budget.requests();
  uint32_t lv = app.lastValidSampleMs;
  if (lv && ep) s.lok = ep - (millis() - lv) / 1000;
  s.cok = st_.lastCloudOkEpoch;
  RuleInfo ri = app.ruleInfo();
  s.arv = ri.rev;
  s.acat = cfg::ALERT_CATALOG_VERSION;
  s.mjq = ri.jobsToday;
  s.nrul = ri.n;
  s.edrop = st_.edrop;
  s.qg = budget.level();
  snprintf(s.ruleErr, sizeof s.ruleErr, "%s", st_.ruleErr[0] ? st_.ruleErr : ri.err);
  rota::status(s.ota, sizeof s.ota);
  rota::check(s.otachk, sizeof s.otachk);
  if (strlen(rota::repo()) < sizeof s.repo) snprintf(s.repo, sizeof s.repo, "%s", rota::repo());
  meter::Snap ms = meter::snap();
  if (ms.configured || !ms.enabled) {
    if (ms.fresh) snprintf(s.meter, sizeof s.meter, "ok %d.%d %lus", ms.proto / 10, ms.proto % 10, (unsigned long)ms.ageS);
    else snprintf(s.meter, sizeof s.meter, "%s", ms.state);
  }
  return upj::statusJson(s, out, cap);
}

bool doMinute(uint32_t now, uint32_t ep, int32_t today) {
  if (!canGo()) return false;
  bool statusDue = (uint32_t)(now - tStatus_) >= cfg::STATUS_MS;
  // cheap gate before scanning /q (the cached pending count also covers provisional records)
  uint32_t pending = minuteQueue.pending();
  if (!pending && !statusDue) {
    st_.backlog = 0;
    st_.backfilling = false;
    return false;
  }
  if (!statusDue && (uint32_t)(now - tScan_) < cfg::BACKFILL_GAP_MS) return false;
  tScan_ = now;
  Batch b;
  bool have = pending && backfill::next(store, today, b, cfg::BACKFILL_MAX_RECS, cfg::LIVE_BATCH_MAX);
  st_.backlog = have ? b.pendingTotal : 0;
  bool liveRecs = have && b.live;
  bool backRecs = have && !b.live && (uint32_t)(now - tBackfill_) >= cfg::BACKFILL_GAP_MS;
  if (!liveRecs && !backRecs && !statusDue) {
    if (!have) st_.backfilling = false;
    return false;
  }
  upj::Patch p(body_, bodyCap_);
  MinuteRec* recs = recsBuf_;
  bool withRecs = liveRecs || backRecs;
  if (withRecs) {
    int got = minuteQueue.read(b.day, b.from, recs, b.n);
    if (got <= 0) {
      withRecs = false;
    } else {
      b.n = (uint16_t)got;
      backfill::addRecords(store, b, recs, today, p);
      mrec::Daily ad = app.daily();
      char dk[12], path[24];
      tu::dateKeyFromDay(b.day, dk, sizeof dk);
      if (ad.day == b.day) {  // the aggregator's running day: full object incl. nf / nw / epvi
        char dj[340];
        if (mrec::toDailyJson(ad, dj, sizeof dj)) {
          int32_t egm = meter::egmFor(ad.day);  // this PUT replaces the day's object: keep the metered energy in it
          if (egm >= 0) jsonAppendInt(dj, sizeof dj, "egm", egm);
          snprintf(path, sizeof path, "daily/%s", dk);
          p.add(path, dj);
        }
      } else {
        backfill::addDailyFields(store, b.day, p);
      }
    }
  }
  bool withStatus = liveRecs || statusDue;
  if (withStatus) {
    char sj[720];
    if (buildStatus(sj, sizeof sj, ep)) p.add("status", sj);
  }
  AlertStateOut as[cfg::MAX_RULES];
  int nas = app.takeAlertStates(as, cfg::MAX_RULES);
  for (int i = 0; i < nas; i++) {
    char aj[160], path[24];
    if (!upj::alertStateJson(as[i], aj, sizeof aj)) continue;
    snprintf(path, sizeof path, "alertState/%s", as[i].rid);
    p.add(path, aj);
  }
  size_t n = p.close();
  if (!n) {
    app.returnAlertStates(as, nas);
    snprintf(st_.lastError, sizeof st_.lastError, "minute PATCH too large");
    tBackfill_ = now;
    return false;
  }
  const char* what = withRecs ? (liveRecs ? "minute" : "backfill") : "status";
  FirebaseRest::Res r = fb.rtdb("PATCH", "", body_, n, resp_, respCap_);
  if (ok(r, what)) {
    if (withRecs && backfill::commit(store, b)) lastCommitMs_ = now;
    if (withStatus) tStatus_ = now;
    if (backRecs) tBackfill_ = now;
    st_.backfilling = backRecs;
  } else if (r.kind == FirebaseRest::Kind::DENIED && nas > 0) {
    // A multi-path PATCH is atomic: if the rules refuse alertState (r8 rules not deployed yet, r8c M6) the minute
    // records must not stall behind it. Drop these alertState records; the next transition re-sends them.
    snprintf(st_.ruleErr, sizeof st_.ruleErr, "alertState refused: deploy the r8 RTDB rules");
  } else {
    app.returnAlertStates(as, nas);
  }
  return true;
}

// ------------------------------------------------------------------------------------------- cfg / alerts

bool doCfg(uint32_t now) {
  if ((!firstCfg_ && (uint32_t)(now - tCfg_) < budget.cfgPollMs()) || !canGo()) return false;
  tCfg_ = now;
  FirebaseRest::Res r = fb.rtdb("GET", "cfg", nullptr, 0, resp_, respCap_, false);
  if (!ok(r, "cfg")) return true;
  firstCfg_ = false;
  upj::CfgDoc c;
  if (!upj::parseCfg(resp_, r.len, c)) {
    snprintf(st_.lastError, sizeof st_.lastError, "cfg: not JSON");
    return true;
  }
  if (!haveCloudCfg_ || !upj::cfgEq(c.c, cloudCfg_)) {
    cloudCfg_ = c.c;
    app.applyCloudCfg(c.c);  // aggTask: thresholds, quiet hours, testAlert -> TEST_ALERT, reprobe
  }
  meter::setEnabled(c.c.meter != 0);  // acts on changes only (off -> on also re-arms after a crash stop)
  // 0.9.2 remote update: each request (version, time) is acted on once, whatever the outcome
  ota::Go go = ota::decide(c.c.ota, c.c.otaAt, ota::versionCode(FW_VERSION), rota::handledAt(), rota::repo()[0] != 0);
  if (go != ota::Go::NONE) {
    rota::setHandled(c.c.otaAt);
    char v[16];
    ota::versionStr(c.c.ota, v, sizeof v);
    if (go == ota::Go::START) otaReq_ = c.c.ota;
    else if (go == ota::Go::NOT_NEWER) rota::setStatus("%s is not newer than %s", v, FW_VERSION);
    else rota::setStatus("failed: no release repo on the logger");
    tStatus_ = millis() - cfg::STATUS_MS;  // report it with the next status write
  }
  haveCloudCfg_ = true;
  cloudRev_ = c.hasRev ? c.alertsRev : 0;
  st_.cloudAlertsRev = cloudRev_;
  if (cloudRev_ != loadedRev_) alertsDue_ = true;
  if ((uint32_t)(now - tAlerts_) >= cfg::ALERTS_REFRESH_MS) alertsDue_ = true;
  return true;
}

bool doAlerts(uint32_t now) {
  if (!alertsDue_ || firstCfg_ || !canGo()) return false;
  alertsDue_ = false;
  tAlerts_ = now;
  double rev = cloudRev_;
  FirebaseRest::Res r = fb.rtdb("GET", "alerts", nullptr, 0, resp_, respCap_, false);
  if (!ok(r, "alerts")) {
    alertsDue_ = true;
    return true;
  }
  if (r.truncated || r.len > cfg::ALERTS_JSON_MAX) {
    snprintf(st_.ruleErr, sizeof st_.ruleErr, "alerts.json over %u bytes", (unsigned)cfg::ALERTS_JSON_MAX);
    loadedRev_ = rev;  // do not refetch every minute; the next edit retries
    return true;
  }
  rules::Set& set = *rulesBuf_;
  char err[41];
  set.rev = rev;
  if (!rules::parse(resp_, r.len, set, err, sizeof err)) {  // keep the previous set
    snprintf(st_.ruleErr, sizeof st_.ruleErr, "%s", err);
    loadedRev_ = rev;
    return true;
  }
  set.rev = rev;
  st_.ruleErr[0] = 0;
  app.applyRules(set);
  snprintf(mailTo_, sizeof mailTo_, "%s", set.email.to);
  snprintf(mailFromName_, sizeof mailFromName_, "%s", set.email.fromName);
  mailCfgLoaded_ = true;
  loadedRev_ = rev;
  minuteQueue.writeFile("/cfg/alerts.json", resp_, r.len);  // loaded by aggTask at the next boot, before Wi-Fi
  settings.putBlob("rules", "rev", &rev, sizeof rev);
  Serial.printf("[up] alerts.json rev %.0f: %u rules (%u invalid), e-mail %s\n", rev, set.n, set.nInvalid,
                set.email.on ? "on" : "off");
  return true;
}

// ------------------------------------------------------------------------------------------- housekeeping
bool doMeta(uint32_t now) {
  if (!metaDirty || (uint32_t)(now - metaChangedMs) < 5000 || !canGo()) return false;
  metaDirty = false;
  char b[900];
  Identity id = app.identity();
  size_t n = identityToMetaJson(id, FW_VERSION, app.bootEpoch, b, sizeof b);
  if (!n) return false;
  if (!ok(fb.rtdb("PUT", "meta", b, n, resp_, respCap_), "meta")) metaDirty = true;
  return true;
}

bool doFinished() {
  mrec::Daily d;
  if (!finHave_ && app.takeFinishedDay(d)) {
    fin_ = d;
    finHave_ = true;
  }
  if (!finHave_ || !canGo()) return false;
  char dk[12], path[24], dj[340];
  tu::dateKeyFromDay(fin_.day, dk, sizeof dk);
  if (!mrec::toDailyJson(fin_, dj, sizeof dj)) {
    finHave_ = false;
    return false;
  }
  int32_t egm = meter::egmFor(fin_.day);
  if (egm >= 0) jsonAppendInt(dj, sizeof dj, "egm", egm);
  snprintf(path, sizeof path, "daily/%s", dk);
  upj::Patch p(body_, bodyCap_);
  p.add(path, dj);
  size_t n = p.close();
  if (ok(fb.rtdb("PATCH", "", body_, n, resp_, respCap_), "final daily")) finHave_ = false;
  return true;
}

bool doMeterDay() {
  if (!meterFinHave_ && meter::takeFinishedDay(meterFinDay_, meterFinWh_)) meterFinHave_ = true;
  if (!meterFinHave_ || !canGo()) return false;
  char dk[12], path[32], v[16];
  tu::dateKeyFromDay(meterFinDay_, dk, sizeof dk);
  snprintf(path, sizeof path, "daily/%s/egm", dk);
  snprintf(v, sizeof v, "%ld", (long)meterFinWh_);
  upj::Patch p(body_, bodyCap_);
  p.add(path, v);
  size_t n = p.close();
  if (ok(fb.rtdb("PATCH", "", body_, n, resp_, respCap_), "meter daily")) meterFinHave_ = false;
  return true;
}

// 0.9.2 remote update: one TLS session at a time (drop the cloud one), download / check / install, report, restart
void runOta() {
  uint32_t code = otaReq_;
  otaReq_ = 0;
  fb.drop();
  bool installed = rota::run(code);
  char sj[720];
  if (buildStatus(sj, sizeof sj, timeSync.epochOrZero())) {
    upj::Patch p(body_, bodyCap_);
    p.add("status", sj);
    size_t n = p.close();
    ok(fb.rtdb("PATCH", "", body_, n, resp_, respCap_), "ota status");
  }
  if (installed) {
    settings.setStr("sys", "plannedRb", "update");  // log-only restart (0.8.0)
    delay(500);
    ESP.restart();
  }
}

bool doMtd(uint32_t now) {
  int32_t a, b;
  if (!app.mtdNeed(a, b) || (tMtd_ && (uint32_t)(now - tMtd_) < 600000UL) || !canGo()) return false;
  tMtd_ = now;
  char sa[12], sb[12], q[160];
  tu::dateKeyFromDay(a, sa, sizeof sa);
  tu::dateKeyFromDay(b, sb, sizeof sb);
  snprintf(q, sizeof q, "orderBy=%%22%%24key%%22&startAt=%%22%s%%22&endAt=%%22%s%%22", sa, sb);
  FirebaseRest::Res r = fb.rtdb("GET", "daily", nullptr, 0, resp_, respCap_, false, q);
  if (!ok(r, "month bootstrap")) return true;
  int n = r.truncated ? -1 : upj::parseDailyRange(resp_, r.len, mtdBuf_, 31);
  app.postMtdDays(mtdBuf_, n < 0 ? 0 : n);
  return true;
}

bool doDiag(uint32_t now) {
  if (!budget.diagAllowed() || (uint32_t)(now - tDiag_) < cfg::DIAG_RAW_MS || !canGo()) return false;
  tDiag_ = now;
  LastReply* lr = (LastReply*)psramAlloc(sizeof(LastReply) * RawRing::NLAST);
  if (!lr) return false;
  int n = app.copyLastReplies(lr, RawRing::NLAST);
  JsonOut j(body_, bodyCap_);
  j.open();
  int k = 0;
  for (int i = 0; i < n; i++) {
    char key[13];
    size_t l = strlen(lr[i].cmd);
    if (!l || l > 12) continue;
    for (size_t c = 0; c <= l; c++) key[c] = (c < l && !isalnum((unsigned char)lr[i].cmd[c])) ? '_' : lr[i].cmd[c];
    j.s(key, lr[i].text);
    k++;
  }
  j.close();
  free(lr);
  if (!k || !j.ok()) return false;
  ok(fb.rtdb("PATCH", "diag/raw", body_, j.len(), resp_, respCap_), "diag/raw");
  return true;
}

bool doPrune(uint32_t now, uint32_t ep, int32_t today) {
  if (!ep || pruneDoneDay_ == today || !canGo()) return false;
  if (prune_ == Prune::IDLE && tu::localMinuteOfDay(ep) < cfg::PRUNE_AFTER_MIN) return false;
  if (tPrune_ && (uint32_t)(now - tPrune_) < 1000) return false;
  tPrune_ = now;
  if (!pruneDays_) pruneDays_ = (int32_t*)psramAlloc(sizeof(int32_t) * 800);
  if (!pruneDays_) return false;
  auto list = [&](const char* node, int keep) -> bool {
    FirebaseRest::Res r = fb.rtdb("GET", node, nullptr, 0, resp_, respCap_, false, "shallow=true");
    if (!ok(r, "prune list")) return false;
    int n = r.truncated ? -1 : upj::parseShallowDates(resp_, r.len, pruneDays_, 800);
    pruneN_ = 0;
    pruneI_ = 0;
    for (int i = 0; i < n; i++)
      if (pruneDays_[i] < today - keep) pruneDays_[pruneN_++] = pruneDays_[i];
    return true;
  };
  auto del = [&](const char* node) -> bool {  // one DELETE per call; true when the list is done
    if (pruneI_ >= pruneN_) return true;
    char dk[12], path[32];
    tu::dateKeyFromDay(pruneDays_[pruneI_], dk, sizeof dk);
    snprintf(path, sizeof path, "%s/%s", node, dk);
    if (ok(fb.rtdb("DELETE", path, nullptr, 0, resp_, respCap_), "prune")) {
      Serial.printf("[up] pruned %s\n", path);
      pruneI_++;
    }
    return false;
  };
  int keepHist = haveCloudCfg_ ? cloudCfg_.keepHistDays : 400, keepEv = haveCloudCfg_ ? cloudCfg_.keepEventDays : 730;
  switch (prune_) {
    case Prune::IDLE:
      if (list("hist", keepHist)) prune_ = Prune::HIST_DEL;
      break;
    case Prune::HIST_DEL:
      if (del("hist")) prune_ = Prune::EV_LIST;
      break;
    case Prune::EV_LIST:
      if (list("events", keepEv)) prune_ = Prune::EV_DEL;
      break;
    case Prune::EV_DEL:
      if (del("events")) {
        prune_ = Prune::IDLE;
        pruneDoneDay_ = today;
      }
      break;
  }
  return true;
}

void doHealthcheck(uint32_t now) {
  if (!notifier || !notifier->healthcheckOn()) return;
  if (!hcFirst_ && (uint32_t)(now - tHc_) < cfg::HC_PING_MS) return;
  hcFirst_ = false;
  tHc_ = now;
  LinkSnapshot l = app.link();
  bool failing = l.state == EngineState::RUN && !l.linkOk && (uint32_t)(millis() - app.lastValidSampleMs) > 300000UL;
  ensureHeap();
  NotifyResult r = notifier->healthcheck(failing);
  st_.lastHcCode = r.httpCode;
  if (!r.ok) snprintf(st_.lastError, sizeof st_.lastError, "healthchecks: %s", r.error);
}

void opsEvents(uint32_t now, uint32_t ep, int32_t today) {
  if (budget.takeGuardEvent()) {
    char m[120];
    snprintf(m, sizeof m, "Cloud quota guard: %lu TLS handshakes, %lu requests today - live updates slowed",
             (unsigned long)budget.handshakes(), (unsigned long)budget.requests());
    app.raise("QUOTA_GUARD", 2, m);
  }
  if (!ep || (uint32_t)(now - tStuck_) < 600000UL) return;
  tStuck_ = now;
  if (stuckDay_ == today || !fb.authOk()) return;
  uint32_t oldest = backfill::oldestPendingTs(store);
  bool noProgress = !lastCommitMs_ || (uint32_t)(now - lastCommitMs_) > 30UL * 60000UL;
  if (oldest && ep > oldest + cfg::BACKLOG_STUCK_S && noProgress) {
    stuckDay_ = today;
    char m[120], dk[12];
    tu::dateKey(oldest, dk, sizeof dk);
    snprintf(m, sizeof m, "Upload backlog stuck: %lu minute records waiting since %s (%s)",
             (unsigned long)minuteQueue.pending(), dk, st_.lastError);
    app.raise("BACKLOG_STUCK", 2, m);
  }
}

}  // namespace

// ============================================================================================ public
void begin() {
  st_ = UplinkStatus();
  bodyCap_ = 32768;
  respCap_ = 32768;
  body_ = (char*)psramAlloc(bodyCap_);
  resp_ = (char*)psramAlloc(respCap_);
  pend_ = psramNew<Pend>(cfg::ALERT_QUEUE_DEPTH);
  rulesBuf_ = psramNew<rules::Set>();
  recsBuf_ = psramNew<MinuteRec>(cfg::BACKFILL_MAX_RECS);
  mtdBuf_ = psramNew<mrec::Daily>(31);
  settings.loadCfg(cloudCfg_);  // last cloud cfg applied (NVS cache) until the first GET
  double rev;
  if (settings.getBlob("rules", "rev", &rev, sizeof rev)) loadedRev_ = rev;
  settings.getBlob("sys", "nwdEp", &lastNetwdRestart_, sizeof lastNetwdRestart_);
  notifier = createNotifier();
  reloadCreds();
  metaDirty = true;
  metaChangedMs = 0;
}

void loop() {
  esp_task_wdt_reset();
  if (!body_ || !resp_ || !pend_ || !rulesBuf_ || !recsBuf_ || !mtdBuf_) {
    snprintf(st_.state, sizeof st_.state, "no-memory");
    return;
  }
  if (credsDirty) reloadCreds();
  uint32_t now = millis();
  uint32_t ep = timeSync.epochOrZero();
  int32_t today = ep ? tu::localDayNumber(ep) : 0;
  budget.roll(ep ? today : (int32_t)(now / 86400000UL) - 100000);
  bool wifi = WiFi.status() == WL_CONNECTED;

  NetWatchdog::Action a = netwd.step(now, wifi, fb.configured(), ep, lastNetwdRestart_);
  if (a == NetWatchdog::Action::DROP_TLS) {
    Serial.println(F("[up] network watchdog: no cloud success for 15 min - dropping TLS, re-auth"));
    fb.drop();
    fb.forceReauth();
  } else if (a == NetWatchdog::Action::RESTART) {
    Serial.println(F("[up] network watchdog: no cloud success for 60 min - rebooting"));
    settings.putBlob("sys", "nwdEp", &ep, sizeof ep);
    delay(200);
    ESP.restart();
  }

  pullAlerts();
  fixTimestamps();
  if (!wifi) {
    snprintf(st_.state, sizeof st_.state, "offline");
    if (!wasOffline_) fb.drop();
    wasOffline_ = true;
    compactPend();
    return;
  }
  wasOffline_ = false;
  if (notifier) {
    processPushes(now);
    esp_task_wdt_reset();
    static uint32_t tTgPair = 0;  // Telegram auto-pairing: poll getUpdates every 15 s while pairing is open
    if (tgAddReq) {
      tgAddReq = false;
      notifier->openTelegramPairing(10UL * 60000UL);
      tTgPair = now - 15000;
    }
    if (notifier->telegramPairing() && (uint32_t)(now - tTgPair) >= 15000) {
      tTgPair = now;
      ensureHeap();
      if (notifier->pollTelegramPairing()) st_.telegramOn = notifier->telegramOn();
      esp_task_wdt_reset();
    }
    st_.tgChats = notifier->telegramChats();
    st_.tgPairing = notifier->telegramPairing();
    processMail(millis());
    esp_task_wdt_reset();
    doHealthcheck(millis());
  }
  esp_task_wdt_reset();
  if (!fb.configured()) {
    snprintf(st_.state, sizeof st_.state, "no-cloud");
    for (int i = 0; i < npend_; i++) pend_[i].rtdbDone = true;
    compactPend();
    return;
  }
  now = millis();
  if (!dbBackoff.ready(now)) {
    snprintf(st_.state, sizeof st_.state, "backoff");
    compactPend();
    return;
  }
  if (!fb.ensureAuth(now)) {
    st_.authOk = false;
    snprintf(st_.state, sizeof st_.state, "auth");
    snprintf(st_.lastError, sizeof st_.lastError, "%s", fb.lastError());
    netwd.onCloudOk(now);  // an auth problem is not a wedged network (avoid watchdog reboots)
    compactPend();
    return;
  }
  st_.authOk = true;
  snprintf(st_.state, sizeof st_.state, "ready");

  int ops = 0;
  auto step = [&](bool did) {
    if (did) ops++;
    esp_task_wdt_reset();
  };
  step(doEvents(millis(), today));
  if (ep) {
    if (ops < 3) step(doLive(millis(), ep));
    if (ops < 3) step(doMinute(millis(), ep, today));
  }
  if (ops < 3) step(doCfg(millis()));
  if (ops < 3) step(doAlerts(millis()));
  if (ops < 3) step(doMeta(millis()));
  if (ops < 3) step(doFinished());
  if (ops < 3) step(doMeterDay());
  if (ops < 3) step(doMtd(millis()));
  if (ops < 3) step(doDiag(millis()));
  if (ops < 3) step(doPrune(millis(), ep, today));
  if (otaReq_) {
    runOta();
  } else if (!otaProbed_ && st_.lastCloudOkEpoch && millis() > cfg::OTA_PROBE_AFTER_MS && rota::repo()[0]) {
    otaProbed_ = true;  // 0.9.6: once per boot, the same TLS path as an update (one session at a time)
    fb.drop();
    rota::probe();
    tStatus_ = millis() - cfg::STATUS_MS;
  }
  opsEvents(millis(), ep, today);
  compactPend();
}

UplinkStatus status() {
  account();
  st_.handshakesToday = budget.handshakes();
  st_.requestsToday = budget.requests();
  st_.quotaLevel = budget.level();
  st_.quotaGuard = st_.quotaLevel > 0;
  return st_;
}

void notifyMetaChanged() {
  metaDirty = true;
  metaChangedMs = millis();
}

void notifyConfigChanged() { credsDirty = true; }
void telegramAddPerson() { tgAddReq = true; }

}  // namespace uplink
