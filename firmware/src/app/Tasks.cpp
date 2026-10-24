// Tasks.cpp - invTask, aggTask and uplinkTask (d1 §5.3).
//   invTask    core 1, prio 5: UART1 / SerialLink / engine / pin diagnostics -> qSample + app.latest
//   aggTask    core 1, prio 4: aggregator, energy, minute records -> LittleFS, alert engine -> alertQueue
//   uplinkTask core 0, prio 3: FW-2 cloud uplink (RTDB, Auth, ntfy, Telegram, Healthchecks)
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "agg/MonthToDate.h"
#include "agg/NotifyGates.h"
#include "agg/RuleEngine.h"
#include "agg/RuleParse.h"
#include "app/App.h"
#include "model/JsonOut.h"
#include "model/TimeUtil.h"
#include "net/TimeSync.h"
#include "net/Uplink.h"
#include "store/MinuteQueue.h"
#include "store/Settings.h"

static QueueHandle_t qSample = nullptr;

// ============================================================================================ invTask
static ArduinoUart uart(Serial1);
static ArduinoClock clk;
static ArduinoGpio gpio;
static RtosLock rawLock;
static SerialLink invLink(uart, clk);
static InverterEngine engine(invLink);
static PinDiag pinDiag(gpio, clk);
static LinkSettings ls;

static void setDiagMsg(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  app.editDiag([&](DiagState& d) {
    snprintf(d.lastMsg, sizeof d.lastMsg, "%s", buf);
    d.seq++;
  });
  Serial.printf("[diag] %s\n", buf);
}

static void diagBusy(const char* what, uint32_t expectMs) {
  app.editDiag([&](DiagState& d) {
    d.busy = what != nullptr;
    snprintf(d.running, sizeof d.running, "%s", what ? what : "");
    d.startedMs = millis();
    d.expectMs = expectMs;
  });
}

static void saveProbeReport() {
  const ProbeOutcome& o = engine.lastProbe();
  const ProbeLog& lg = engine.probeLog();
  size_t cap = lg.size() * 2 + 1024;
  char* buf = (char*)psramAlloc(cap);
  if (!buf) return;
  char prof[40];
  o.profile.describe(prof, sizeof prof);
  JsonOut j(buf, cap);
  j.open();
  j.u("ts", timeSync.epochOrZero());
  j.b("ok", o.ok);
  j.s("kind", adapterKindName(o.kind));
  j.s("family", o.result.family);
  j.s("dialect", o.state.dialect);
  j.s("code", probeCodeName(o.result.code));
  j.s("profile", prof);
  j.b("foreignMaster", o.foreignMaster);
  j.b("pi18", o.pi18);
  j.u("durationMs", o.durationMs);
  j.s("summary", o.summary);
  j.s("detail", o.result.detail);
  j.s("log", lg.text());
  j.close();
  minuteQueue.writeFile("/log/probe.json", buf, j.len());
  app.setProbeReport(String(buf));
  free(buf);
}

static void publishLink() {
  LinkSnapshot s;
  s.state = engine.state();
  InverterAdapter* a = engine.adapter();
  snprintf(s.adapter, sizeof s.adapter, "%s", a ? a->id() : "none");
  AdapterState st;
  if (a) a->exportState(st);
  snprintf(s.dialect, sizeof s.dialect, "%s", st.dialect);
  invLink.profile().describe(s.profile, sizeof s.profile);
  s.baud = invLink.profile().baud;
  s.armed = invLink.armed();
  s.safe = app.safeMode;
  s.linkOk = invLink.linkOk(millis());
  s.mode = ls.prof.mode;
  s.roPin = ls.prof.roPin;
  s.diPin = ls.prof.diPin;
  s.diConfirmed = ls.prof.diConfirmed;
  s.invTxRj = ls.prof.invTxRjPin;
  s.armRx = ls.armRx;
  s.ls = invLink.stats();
  s.es = engine.stats();
  const ProbeOutcome& o = engine.lastProbe();
  snprintf(s.probeSummary, sizeof s.probeSummary, "%s", o.summary);
  s.probeAtMs = o.startedMs;
  s.probeRan = o.ran;
  s.probeOk = o.ok;
  app.setLink(s);
  Identity id;
  engine.identity(id);
  app.setIdentity(id);
  if (id.qtEpoch && !timeSync.valid()) timeSync.offerInverterClock(id.qtEpoch + (millis() - id.qtAtMs) / 1000);
}

static void handleDiag(const DiagRequest& rq) {
  switch (rq.kind) {
    case DiagKind::SELF_ID: {
      if (ls.armed) { setDiagMsg("Self-ID refused: the link is armed (inverter wired). Run reset-link first."); return; }
      diagBusy("self-id", 700);
      invLink.release();
      SelfIdResult r = pinDiag.selfId(cfg::PIN_MOD_HDR_RXD, cfg::PIN_MOD_HDR_TXD);
      if (r.verdict == SelfIdVerdict::RO_FOUND) {
        ls.prof.roPin = r.roPin;
        ls.prof.diPin = r.diPin;
        ls.prof.diConfirmed = true;
        settings.saveSelfId(r.roPin, r.diPin);
      }
      engine.setBaseProfile(ls.prof);  // re-attaches the UART (RX only while not armed)
      app.editDiag([&](DiagState& d) { d.selfId = r; });
      diagBusy(nullptr, 0);
      setDiagMsg("Self-ID: %s. %s", selfIdName(r.verdict), r.msg);
      return;
    }
    case DiagKind::IDLE: {
      if (ls.armed) { setDiagMsg("Idle test refused: the link is armed. Run reset-link first."); return; }
      if (rq.arg != 1 && rq.arg != 2) { setDiagMsg("Idle test: pin must be 1 or 2"); return; }
      IdleRef ref = rq.flag ? IdleRef::V3V3 : IdleRef::GND;  // flag: upper check, module B- on ESP 3V3
      diagBusy(ref == IdleRef::V3V3 ? (rq.arg == 1 ? "upper-pin1" : "upper-pin2") : (rq.arg == 1 ? "idle-pin1" : "idle-pin2"),
               cfg::IDLE_DURATION_MS + 200);
      invLink.release();
      IdleResult r = pinDiag.idleTest(rq.arg, ls.prof.roPin, ref);
      ls.idle.add(r);
      settings.saveIdle(ls.idle);
      engine.setBaseProfile(ls.prof);
      ArmCheck ac = PinDiag::armCheck(ls.idle, 0);
      app.editDiag([&](DiagState& d) {
        d.idle = ls.idle;
        d.arm = ac;
      });
      diagBusy(nullptr, 0);
      const char* hint = r.verdict == IdleVerdict::TX_IDLES_NEGATIVE ? "this pin idles NEGATIVE = inverter TX"
                         : r.verdict == IdleVerdict::HIGH_NOT_TX     ? "HIGH = not the inverter TX (RX input, open wire, or a positive pin: run the upper check)"
                         : r.verdict == IdleVerdict::BELOW_3V3       ? "upper check: below 3.3 V = safe for GPIO17"
                         : r.verdict == IdleVerdict::ABOVE_3V3       ? "upper check: ABOVE 3.3 V = a POSITIVE pin. NEVER connect GPIO17 to it - STOP"
                         : r.verdict == IdleVerdict::ACTIVE_TRAFFIC  ? "edges: something is transmitting - is a dongle on COM?"
                                                                     : "undecided - check the wrap joints and repeat";
      setDiagMsg("%s RJ45 pin %u (B- on %s): %.1f%% LOW, %u edges in %u samples -> %s",
                 ref == IdleRef::V3V3 ? "Upper check" : "Idle test", r.rjPin, ref == IdleRef::V3V3 ? "3V3" : "pin 8/GND",
                 r.pctLow, r.edges, r.samples, hint);
      return;
    }
    case DiagKind::LOOPBACK: {
      // B1 is a bench test BEFORE the module is wired (B2). Once self-ID or an idle test has run, GPIO18 carries the
      // module's RO output (and maybe the RJ45): a GPIO17-GPIO18 jumper would make GPIO17 fight RO (safety review).
      if (ls.prof.diConfirmed || ls.idle.any()) {
        setDiagMsg("Loopback refused: the module (and maybe the RJ45) is already wired - loopback is the bench test "
                   "before B2. Remove the module wires from GPIO16/18, run reset-link all, then retry.");
        return;
      }
      diagBusy("loopback", 1500);
      SerialLink::LoopbackResult r = invLink.loopbackTest(rq.flag);
      app.editDiag([&](DiagState& d) { d.loop = r; });
      diagBusy(nullptr, 0);
      setDiagMsg("Loopback: %s", r.detail);
      return;
    }
    case DiagKind::ARM: {
      if (app.safeMode) { setDiagMsg("Arm refused: safe mode (no inverter TX)."); return; }
      if (!ls.prof.diConfirmed) {
        setDiagMsg("Arm refused: module self-ID (Stage B3) not done - the module's DI pin is not held HIGH yet.");
        return;
      }
      ArmCheck ac = PinDiag::armCheck(ls.idle, rq.arg);
      app.editDiag([&](DiagState& d) { d.arm = ac; });
      if (!ac.ok) { setDiagMsg("Arm refused: %s", ac.reason); return; }
      if (!rq.flag) {
        setDiagMsg("Arm needs confirmation: GPIO17 must be wired to RJ45 pin %u (inverter RX) and nothing else. "
                   "Repeat with confirm.", ac.rxRj);
        return;
      }
      // Pre-arm check (receive-only): GPIO17 must read LOW with its pull-down, RO must read LOW (A+ on the TX pin).
      diagBusy("pre-arm", cfg::PREARM_SAMPLE_MS + 100);
      invLink.release();
      PreArmResult pa = pinDiag.preArmCheck(cfg::PIN_INV_TX, ls.prof.roPin, true);
      engine.setBaseProfile(ls.prof);  // RX re-attached, GPIO17 high-Z (still not armed)
      diagBusy(nullptr, 0);
      if (!pa.ok) { setDiagMsg("Arm refused: %s", pa.reason); return; }
      ls.armed = true;
      ls.armRx = ac.rxRj;
      ls.prof.invTxRjPin = ac.txRj;
      ls.prof.mode = LinkMode::RS232_LITE;
      settings.saveArm(true, ac.rxRj, ac.txRj, LinkMode::RS232_LITE);
      engine.setBaseProfile(ls.prof);
      engine.setArmed(true, millis());
      setDiagMsg("ARMED: GPIO17 -> RJ45 pin %u (inverter RX). Read-only probe starts now (about 1-2 min).", ac.rxRj);
      return;
    }
    case DiagKind::ARM_MAX3232: {
      if (app.safeMode || !rq.flag) { setDiagMsg("MAX3232 arm refused (safe mode or not confirmed)."); return; }
      // This path has no idle-test gate, so it must never be usable with the HW-0519 RS232-lite wiring: a MAX3232
      // module REPLACES the HW-0519 (GPIO17 then goes to the MAX3232's TTL input, never to an RJ45 pin).
      if (ls.prof.diConfirmed) {
        setDiagMsg("MAX3232 arm refused: an HW-0519 self-ID is stored (RS232-lite wiring). Fit the MAX3232 module in its "
                   "place, run reset-link all, then arm-max3232 again.");
        return;
      }
      diagBusy("pre-arm", cfg::PREARM_SAMPLE_MS + 100);
      invLink.release();
      PreArmResult pa = pinDiag.preArmCheck(cfg::PIN_INV_TX, ls.prof.roPin, false);
      engine.setBaseProfile(ls.prof);
      diagBusy(nullptr, 0);
      if (!pa.ok) {
        setDiagMsg("MAX3232 arm refused: %s (with a MAX3232: is GPIO17 on its R-OUT instead of T-IN?)", pa.reason);
        return;
      }
      ls.armed = true;
      ls.armRx = 0;
      ls.prof.mode = LinkMode::MAX3232;
      ls.prof.rxInvert = ls.prof.txInvert = false;
      settings.saveArm(true, 0, 0, LinkMode::MAX3232);
      engine.setBaseProfile(ls.prof);
      engine.setArmed(true, millis());
      setDiagMsg("ARMED in MAX3232 mode (non-inverted UART on GPIO18/GPIO17). Probe starts now.");
      return;
    }
    case DiagKind::RESET_LINK: {
      engine.setArmed(false, millis());
      settings.clearLink(rq.arg == 0);
      settings.loadLink(ls);
      engine.setBaseProfile(ls.prof);
      app.editDiag([&](DiagState& d) {
        d.idle = ls.idle;
        d.arm = ArmCheck();
      });
      setDiagMsg("Link reset: DISARMED (GPIO17 is high-Z again), idle results%s cleared.",
                 rq.arg ? ", self-ID" : "");
      return;
    }
    case DiagKind::PROBE:
      if (!invLink.armed()) { setDiagMsg("Probe refused: the link is not armed (idle test + arm first)."); return; }
      engine.requestProbe();
      setDiagMsg("Read-only probe requested (runs for about 1-2 minutes).");
      return;
    case DiagKind::CAPTURE:
      app.captureOn = rq.flag;
      setDiagMsg("Raw capture to /log/raw-0.txt %s", rq.flag ? "ON" : "OFF");
      return;
  }
}

static void captureNew() {
  static uint32_t lastUp = 0;
  if (!app.captureOn) return;
  RawExchange ex[4];
  int n = app.copyExchanges(ex, 4);
  for (int i = n - 1; i >= 0; i--) {
    if (ex[i].upMs <= lastUp) continue;
    lastUp = ex[i].upMs;
    char txt[260], line[320];
    RawRing::render(ex[i].rx, ex[i].rxLen, txt, sizeof txt);
    snprintf(line, sizeof line, "%lu %s %s %s", (unsigned long)ex[i].upMs, ex[i].cmd,
             linkResultName((LinkResult)ex[i].result), txt);
    minuteQueue.appendLine("/log/raw-0.txt", line, 256 * 1024);
  }
}

static void invTask(void*) {
  esp_task_wdt_add(nullptr);
  invLink.ring().setLock(&rawLock);
  app.setRaw(&invLink.ring());
  settings.loadLink(ls);
  app.editDiag([&](DiagState& d) { d.idle = ls.idle; });
  AdapterState st = ls.st;
  if (!ls.armed) st = AdapterState();
  bool armNow = ls.armed;
  if (armNow && !app.safeMode) {
    // Boot-time pre-arm check (safety review): a GPIO16/GPIO17 wrap short that appeared after arming would make
    // two ESP outputs fight forever (DI held HIGH, TX idling LOW). TX stays detached for this boot if GPIO17 reads
    // HIGH. RO is not checked here: the inverter may legitimately be off at boot.
    PreArmResult pa = pinDiag.preArmCheck(cfg::PIN_INV_TX, ls.prof.roPin, false);
    if (!pa.ok) {
      armNow = false;
      st = AdapterState();
      setDiagMsg("Boot: TX NOT attached this boot. %s", pa.reason);
      app.raise("LINK_LOST", 4, "Logger did not enable inverter TX: GPIO17 pre-arm check failed (wiring short or "
                "positive pin). Unplug the COM cable and inspect the GPIO16/17 wraps.", false);
    }
  }
  engine.begin(ls.prof, st, armNow, app.safeMode, millis());
  Serial.printf("[inv] link %s, mode %s, RO=GPIO%u DI=GPIO%u%s\n", ls.armed ? "ARMED" : "NOT armed (GPIO17 high-Z)",
                linkModeName(ls.prof.mode), ls.prof.roPin, ls.prof.diPin, ls.prof.diConfirmed ? "" : " (self-ID pending)");
  uint32_t lastProbeStart = 0, lastPublish = 0;
  for (;;) {
    esp_task_wdt_reset();
    DiagRequest rq;
    while (app.takeDiag(rq)) handleDiag(rq);
    uint32_t now = millis();
    uint32_t epoch = timeSync.epochOrZero();
    Sample s;
    if (engine.tick(now, epoch, s)) {
      app.setLatest(s);
      if (xQueueSend(qSample, &s, 0) != pdTRUE) {  // depth 4, newest wins
        Sample drop;
        xQueueReceive(qSample, &drop, 0);
        xQueueSend(qSample, &s, 0);
      }
      xEventGroupSetBits(app.events, EV_LINK_OK);
    } else if (!invLink.linkOk(millis())) {
      xEventGroupClearBits(app.events, EV_LINK_OK);
    }
    const ProbeOutcome& o = engine.lastProbe();
    if (o.ran && o.startedMs != lastProbeStart) {
      lastProbeStart = o.startedMs;
      saveProbeReport();
      Serial.printf("[inv] probe: %s\n", o.summary);
      uplink::notifyMetaChanged();
    }
    LinkProfile p;
    AdapterState as;
    if (engine.takePersist(p, as)) {
      settings.saveLinkState(p, as, epoch);
      ls.prof.baud = p.baud;
      ls.prof.rxInvert = p.rxInvert;
      ls.prof.txInvert = p.txInvert;
      uplink::notifyMetaChanged();
    }
    if ((uint32_t)(millis() - lastPublish) >= 1000) {
      lastPublish = millis();
      publishLink();
    }
    captureNew();
    vTaskDelay(pdMS_TO_TICKS(cfg::SCHED_TICK_MS));
  }
}

void startInvTask() {
  qSample = xQueueCreate(4, sizeof(Sample));
  xTaskCreatePinnedToCore(invTask, "invTask", cfg::STACK_INV, nullptr, cfg::PRIO_INV, nullptr, cfg::CORE_INV);
}

// ============================================================================================ aggTask
// NVS-backed persistence of the rule engine (r8 §4.8: rules/rsNN, 28 B, written on transitions only) and of the
// device e-mail job counter (r8 §4.6: rules/mj, written once per job).
class NvsRulePersist : public IRulePersist {
 public:
  bool load(uint8_t slot, RulePersist& p) override {
    char k[6];
    snprintf(k, sizeof k, "rs%02u", (unsigned)(slot + 1));
    return settings.getBlob("rules", k, &p, sizeof p);
  }
  void save(uint8_t slot, const RulePersist& p) override {
    char k[6];
    snprintf(k, sizeof k, "rs%02u", (unsigned)(slot + 1));
    settings.putBlob("rules", k, &p, sizeof p);
  }
};
struct JobsBlob {
  int32_t day;
  uint16_t cnt, pad;
};
class NvsGateStore : public IGateStore {
 public:
  void saveJobs(int32_t day, uint16_t cnt) override {
    JobsBlob b{day, cnt, 0};
    settings.putBlob("rules", "mj", &b, sizeof b);
  }
  void saveOutage(uint32_t openEpoch) override { settings.putBlob("alert", "outage", &openEpoch, sizeof openEpoch); }
  void saveSummaryDay(int32_t day) override { settings.putBlob("alert", "sumDay", &day, sizeof day); }
};

static Aggregator agg;
static NotifyGates gates;  // one quiet-hours / push-cap / mail-job counter for both engines (r8 §4.1)
static AlertEngine alerts(alertQueue, &gates);
static NvsRulePersist rulePersist;
static NvsGateStore gateStore;
static RuleEngine* ruleEngine = nullptr;  // ~8 KB, constructed in PSRAM by aggTask
static MonthToDate mtd;
static rules::Set* ruleSetP = nullptr;    // ~3.4 KB in PSRAM, never on the 10 KB aggTask stack
static const rules::Set kNoRules;
static mrec::Daily* mtdIn = nullptr;      // [31] PSRAM: month bootstrap days from the uplink
static EnergyView lastEv;
static RtosLock aggLock;

static void saveMtd() { settings.putBlob("energy", "mtd", &mtd.blob(), sizeof(MtdBlob)); }

// fold finished days of this billing cycle that are still in LittleFS; ask the uplink for the rest (r8 §4.5)
static void mtdGapFill(int32_t today) {
  int32_t miss[31];
  int n = mtd.missing(today, miss, 31);
  bool changed = false;
  for (int i = 0; i < n && i < 31; i++) {
    mrec::Daily d;
    if (minuteQueue.recomputeDay(miss[i], d)) changed |= mtd.fold(d);
    esp_task_wdt_reset();
  }
  if (changed) saveMtd();
  if (mtd.needBootstrap(today)) {
    n = mtd.missing(today, miss, 31);
    if (n > 0) app.setMtdNeed(miss[0], today - 1);
  }
}

static void loadBootRules(uint32_t epoch) {
  rules::Set& ruleSet = *ruleSetP;
  char* buf = (char*)psramAlloc(cfg::ALERTS_JSON_MAX + 1);
  size_t n = buf ? minuteQueue.readFile("/cfg/alerts.json", buf, cfg::ALERTS_JSON_MAX + 1) : 0;
  double rev = 0;
  settings.getBlob("rules", "rev", &rev, sizeof rev);
  char err[41];
  ruleSet.rev = rev;
  if (!n || !rules::parse(buf, n, ruleSet, err, sizeof err)) ruleSet = kNoRules;
  ruleSet.rev = n ? rev : 0;
  free(buf);
  ruleEngine->setRules(ruleSet, epoch);
  alerts.setEmailPolicy(ruleSet.email);
  Serial.printf("[agg] rules: %u loaded from flash (%u enabled), e-mail %s\n", ruleSet.n, ruleEngine->enabled(),
                ruleSet.email.on ? "on" : "off");
}

static void publishRuleInfo() {
  static RuleInfo ri;
  ri = RuleInfo();
  const rules::Set& s = ruleEngine->ruleSet();
  ri.rev = s.rev;
  ri.n = ruleEngine->enabled();
  ri.present = s.n;
  ri.invalid = s.nInvalid;
  ri.jobsToday = (uint16_t)gates.jobsToday();
  ri.emailOn = s.email.on;
  ri.monthStart = s.email.monthStart;
  snprintf(ri.err, sizeof ri.err, "%s", s.err);
  for (int i = 0; i < cfg::MAX_RULES; i++) {
    if (!s.d[i].present) continue;
    RuleInfo::Slot& sl = ri.slot[ri.nSlots++];
    snprintf(sl.rid, sizeof sl.rid, "%s", s.d[i].rid);
    sl.st = (uint8_t)ruleEngine->state(i);
    sl.cnt = ruleEngine->mailsToday(i);
  }
  app.setRuleInfo(ri);
}

int aggDownsample(uint32_t nowMs, uint32_t spanS, uint32_t stepS, SampleRing::Bucket* out, int max) {
  LockGuard g(&aggLock);
  return agg.ring().downsample(nowMs, spanS, stepS, out, max);
}

static void aggTask(void*) {
  esp_task_wdt_add(nullptr);
  {
    LockGuard g(&aggLock);
    if (!agg.begin(app.bootId, psramAlloc)) Serial.println(F("[agg] PSRAM ring allocation failed"));
  }
  AlertCfg c;
  settings.loadCfg(c);
  alerts.setCfg(c);
  uint32_t lastTestAlert = c.testAlert, lastReprobe = c.reprobe;
  // usage-threshold rules: restore the job counter, the billing-cycle sums and the last alerts.json BEFORE the
  // network comes up, so rules keep working through an internet outage (r8 §4.8)
  ruleEngine = new (psramAlloc(sizeof(RuleEngine))) RuleEngine(alertQueue, gates, &rulePersist);
  ruleSetP = psramNew<rules::Set>();
  mtdIn = psramNew<mrec::Daily>(31);
  {
    JobsBlob jb;
    if (settings.getBlob("rules", "mj", &jb, sizeof jb)) gates.restoreJobs(jb.day, jb.cnt);
    gates.setStore(&gateStore);
    uint32_t outage = 0;  // 0.7.9: a grid outage already notified before this boot -> no second GRID_LOSS
    if (settings.getBlob("alert", "outage", &outage, sizeof outage) && outage) alerts.resumeOutage(outage);
    int32_t sumDay = -1;  // 1.0.1: no second DAILY_SUMMARY after a restart in the summary hour
    if (settings.getBlob("alert", "sumDay", &sumDay, sizeof sumDay)) alerts.resumeSummaryDay(sumDay);
    MtdBlob mb;
    if (settings.getBlob("energy", "mtd", &mb, sizeof mb) && mb.ver == 2) mtd.load(mb);
    loadBootRules(timeSync.epochOrZero());
  }
  uint32_t lastRuleInfo = 0;
  // 0.8.0 (owner request): restarts we planned are only logged, never pushed: a firmware update (the stored version
  // changed; on the first boot that stores it, a SW reset is the OTA restart) and a requested reboot (web/console/
  // safe-exit set sys/plannedRb). Crashes, watchdogs, brown-outs and network-watchdog reboots still notify.
  {
    String lastFw = settings.getStr("sys", "fw", "");
    String planned = settings.getStr("sys", "plannedRb", "");
    String cause = settings.getStr("sys", "rbCause", "");  // 0.9.9: why a watchdog restarted the logger
    if (cause.length()) settings.setStr("sys", "rbCause", "");
    bool sw = !strcmp(app.resetReason, "SW");
    bool updated = lastFw.length() ? lastFw != FW_VERSION : sw;
    if (lastFw != FW_VERSION) settings.setStr("sys", "fw", FW_VERSION);
    if (planned.length()) settings.setStr("sys", "plannedRb", "");
    char msg[96];
    if (updated) {
      snprintf(msg, sizeof msg, "Logger updated to %s%s%s", FW_VERSION, lastFw.length() ? " (was " : "",
               lastFw.length() ? (lastFw + ")").c_str() : "");
      alerts.raiseQuiet("LOGGER_REBOOT", 2, msg, millis(), timeSync.epochOrZero());
    } else if (strcmp(app.resetReason, "POWERON") != 0) {
      if (planned.length() && sw) {
        snprintf(msg, sizeof msg, "Logger restarted on request (%s)", planned.c_str());
        alerts.raiseQuiet("LOGGER_REBOOT", 2, msg, millis(), timeSync.epochOrZero());
      } else {
        snprintf(msg, sizeof msg, "Logger rebooted (reason %s%s%s)", app.resetReason, cause.length() && sw ? ": " : "",
                 cause.length() && sw ? cause.c_str() : "");
        alerts.raise("LOGGER_REBOOT", 2, msg, millis(), timeSync.epochOrZero());
      }
    }
  }
  uint32_t lastTick = 0, lastCheckpoint = millis(), lastHouse = 0;
  for (;;) {
    esp_task_wdt_reset();
    Sample s;
    uint32_t epoch = timeSync.epochOrZero();
    if (xQueueReceive(qSample, &s, pdMS_TO_TICKS(200)) == pdTRUE) {
      {
        LockGuard g(&aggLock);
        agg.addSample(s);
      }
      alerts.evaluate(s, s.upMs, epoch);
      ruleEngine->onSample(s, lastEv, s.upMs, epoch, tu::epochValid(epoch));
    }
    uint32_t now = millis();
    if ((uint32_t)(now - lastTick) < 1000) continue;
    lastTick = now;
    epoch = timeSync.epochOrZero();

    // first valid time of this boot: anchor, fix provisional records, rebuild today's totals from the file
    uint32_t aUp, aEp;
    if (timeSync.takeFirstValid(aUp, aEp)) {
      xEventGroupSetBits(app.events, EV_TIME_OK);
      if (!app.bootEpoch) app.bootEpoch = aEp - (aUp / 1000);
      minuteQueue.onTimeValid(aUp, aEp);
      mrec::Daily d, ck;
      int32_t today = tu::localDayNumber(aEp);
      minuteQueue.recomputeDay(today, d);
      uint32_t le;
      if (settings.loadEnergy(ck, le) && ck.day == today) {
        d.nf = ck.nf;
        d.nw = ck.nw;
        d.epvi = ck.epvi;
      }
      {
        LockGuard g(&aggLock);
        agg.setDaily(d);
      }
      // month-to-date: (re)start the billing cycle, fold finished days still in flash, maybe ask RTDB
      if (mtd.setStartDay(ruleEngine->policy().monthStart, today)) saveMtd();
      mtdGapFill(today);
    }

    MinuteRec rec;
    bool closed;
    {
      LockGuard g(&aggLock);
      closed = agg.tick(now, epoch, rec);
    }
    if (closed && !minuteQueue.append(rec)) Serial.println(F("[agg] minute append failed"));
    mrec::H15 h;
    if (agg.takeH15(h)) app.pushH15(h);
    mrec::Daily fin;
    bool rolled = agg.takeDayRolled(fin);
    for (int i = alerts.takeFaultOpens(); i > 0; i--) agg.noteEvent(true);
    for (int i = alerts.takeWarnOpens(); i > 0; i--) agg.noteEvent(false);

    LinkSnapshot lk = app.link();
    Identity id = app.identity();
    alerts.setInputRangeUps(id.inrng == 1);
    mrec::Daily today;
    {
      LockGuard g(&aggLock);
      today = agg.daily();
    }
    alerts.tick(now, epoch, lk.state == EngineState::RUN, lk.linkOk, &today);
    float e1, e2, e3, e4, e5;
    {
      LockGuard g(&aggLock);
      agg.todayEnergy(e1, e2, e3, e4, e5);
    }
    app.setEnergy(e1, e2, e3, e4, e5, (int32_t)minuteQueue.pending());
    app.setDaily(today);

    // ---- usage-threshold rules (r8 §4.1-4.8)
    bool timeOk = tu::epochValid(epoch);
    int32_t localDay = timeOk ? tu::localDayNumber(epoch) : 0;
    if (app.takeRules(*ruleSetP)) {
      ruleEngine->setRules(*ruleSetP, epoch);
      alerts.setEmailPolicy(ruleSetP->email);
      if (timeOk && mtd.setStartDay(ruleSetP->email.monthStart, localDay)) {
        saveMtd();
        mtdGapFill(localDay);
      }
    }
    if (rolled) {
      if (mtd.fold(fin)) saveMtd();
      app.pushFinishedDay(fin);  // the uplink writes the final daily/<d> incl. nf / nw / epvi
    }
    if (timeOk && mtd.roll(localDay)) {  // a new billing cycle started
      saveMtd();
      mtdGapFill(localDay);
    }
    {
      bool answered = false;
      int nb = app.takeMtdDays(mtdIn, 31, &answered);
      if (answered) {
        for (int i = 0; i < nb; i++) mtd.fold(mtdIn[i]);
        mtd.markBootstrapped();
        saveMtd();
      }
    }
    EnergyView ev;
    ev.day = today.day;
    ev.epv = e1;
    ev.eld = e2;
    ev.ech = e3;
    ev.edis = e4;
    ev.egr = e5;
    ev.goutMin = today.day ? today.gout : -1;
    if (timeOk) {
      unsigned elapsed = tu::localMinuteOfDay(epoch);
      if (elapsed < 1) elapsed = 1;
      unsigned cov = (unsigned)today.n * 100u / elapsed;
      ev.covPct = (uint8_t)(cov > 100 ? 100 : cov);
      if (mtd.cycleDay() && today.day == localDay) {
        ev.mEld = mtd.sum(rules::Metric::ELD) + e2;
        ev.mEgr = mtd.sum(rules::Metric::EGR) + e5;
        ev.mEpv = mtd.sum(rules::Metric::EPV) + e1;
        ev.mEdis = mtd.sum(rules::Metric::EDIS) + e4;
        ev.mEch = mtd.sum(rules::Metric::ECH) + e3;
        ev.mGoutMin = (int)mtd.sum(rules::Metric::GOUT) + today.gout;
        ev.mCycleKey = mtd.cycleKey();
        ev.mCovPct = mtd.coveragePct(localDay);
      }
    }
    lastEv = ev;
    ruleEngine->tick(ev, now, epoch, timeOk);
    AlertStateOut aso;
    while (ruleEngine->takeDirty(aso)) app.putAlertState(aso);
    if ((uint32_t)(now - lastRuleInfo) >= 2000) {
      lastRuleInfo = now;
      publishRuleInfo();
    }

    AlertCfg nc;
    if (app.takeCfg(nc)) {
      alerts.setCfg(nc);
      settings.saveCfg(nc);
      if (nc.testAlert && nc.testAlert != lastTestAlert)
        alerts.raise("TEST_ALERT", 3, "Test alert from the dashboard", now, epoch, true);
      if (nc.reprobe && nc.reprobe != lastReprobe) {
        DiagRequest rq;
        rq.kind = DiagKind::PROBE;
        app.postDiag(rq);
      }
      lastTestAlert = nc.testAlert;
      lastReprobe = nc.reprobe;
    }
    RaiseReq rr;
    while (app.takeRaise(rr)) alerts.raise(rr.type, rr.sev, rr.msg, now, epoch, rr.force);

    if ((uint32_t)(now - lastCheckpoint) >= cfg::NVS_CHECKPOINT_MS) {
      lastCheckpoint = now;
      settings.saveEnergy(today, epoch);
    }
    if (rolled || lastHouse == 0 || (uint32_t)(now - lastHouse) >= 600000UL) {
      lastHouse = now;
      if (tu::epochValid(epoch)) minuteQueue.housekeeping(tu::localDayNumber(epoch), &alerts, now, epoch);
    }
  }
}

void startAggTask() {
  xTaskCreatePinnedToCore(aggTask, "aggTask", cfg::STACK_AGG, nullptr, cfg::PRIO_AGG, nullptr, cfg::CORE_AGG);
}

// ============================================================================================ uplinkTask
static void uplinkTask(void*) {
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    uplink::loop();
    app.setUplinkStatus(uplink::status());
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void startUplinkTask() {
  xTaskCreatePinnedToCore(uplinkTask, "uplinkTask", cfg::STACK_UPLINK, nullptr, cfg::PRIO_UPLINK, nullptr,
                          cfg::CORE_UPLINK);
}
