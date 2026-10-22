// Console.cpp - see Console.h. Commands: help status selftest idle [3v3] loopback arm arm-max3232 probe raw live
// reset-link capture portal reboot safe-exit cfg adminpw ls cloud rules test-alert meter
#include "app/Console.h"

#include <LittleFS.h>
#include <WiFi.h>

#include "app/App.h"
#include "config.h"
#include "model/LiveJson.h"
#include "model/TimeUtil.h"
#include "net/TimeSync.h"
#include "net/WifiProv.h"
#include "store/MinuteQueue.h"
#include "store/Settings.h"
#include "sys/SafeMode.h"

namespace console {

static char line_[160];
static size_t n_ = 0;

void begin() { Serial.println(F("Type 'help' for console commands.")); }

// "meter": classify the wire on GPIO4 (receive-only). The internal pull-down and pull-up (~45 kOhm) are applied in
// turn: a wire that follows them is open/floating (no connection, or a switched-off output); a wire that stays LOW is
// loaded to ground (e.g. an RS232/TTL receiver input) or driven low; one that stays HIGH is driven high (e.g. an idle
// 3.3 V TTL transmitter). Then one no-pull ADC reading. Only connect GPIO4 to a LAN wire that passed BOTH the idle
// test (not negative) and the upper check (below 3.3 V): ESP32-S3 pins tolerate only -0.3 .. 3.6 V.
static void meter() {
  const int pin = cfg::PIN_METER;
  int lo[5], hi[5];
  for (int i = 0; i < 5; i++) {
    pinMode(pin, INPUT_PULLDOWN);
    delay(15);
    lo[i] = digitalRead(pin);
    pinMode(pin, INPUT_PULLUP);
    delay(15);
    hi[i] = digitalRead(pin);
  }
  pinMode(pin, INPUT);
  delay(15);
  uint32_t mn = 0xFFFFFFFF, mx = 0, sum = 0;
  const int n = 50;
  for (int i = 0; i < n; i++) {
    uint32_t v = analogReadMilliVolts(pin);
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    sum += v;
    delay(2);
  }
  pinMode(pin, INPUT);  // leave it high-Z
  int loHigh = 0, hiHigh = 0;
  for (int i = 0; i < 5; i++) {
    loHigh += lo[i];
    hiHigh += hi[i];
  }
  const char* verdict;
  if (loHigh == 0 && hiHigh == 5) verdict = "FLOATING: follows the pull-up/pull-down (no connection, or a switched-off output)";
  else if (loHigh == 0 && hiHigh == 0) verdict = "HELD LOW: loaded to ground (a receiver input) or driven low";
  else if (loHigh == 5 && hiHigh == 5) verdict = "HELD HIGH: driven high (e.g. an idle 3.3 V TTL transmitter)";
  else verdict = "MIXED: weakly loaded or noisy - repeat";
  Serial.printf("[meter] GPIO%d  pull-down -> %d/5 HIGH, pull-up -> %d/5 HIGH, no pull: avg %u mV (min %u, max %u)\n"
                "[meter] %s\n",
                pin, loHigh, hiHigh, (unsigned)(sum / n), (unsigned)mn, (unsigned)mx, verdict);
}

static void help() {
  Serial.println(F(
      "Commands (receive-only unless noted):\n"
      "  status              link, Wi-Fi, time, memory, queue\n"
      "  live                latest sample as JSON\n"
      "  selftest            module RO/DI self-ID (bench: A/B open, then A+->GND, B-->3V3)\n"
      "  idle 1|2            10 s idle-level test with module A+ on RJ45 pin 1 or 2 (B- on pin 8)\n"
      "  idle 1|2 3v3        10 s UPPER check of the RX pin: A+ on it, B- on ESP 3V3, pin 8 + P5 out of B- (on GND)\n"
      "  meter               GPIO4 wire check: floating / held low / held high + mV (receive-only; see docs/03 9.1)\n"
      "  loopback yes        BENCH ONLY, before the module is wired: GPIO17 jumpered to GPIO18 (transmits QPI)\n"
      "  arm <pin> YES       after the idle tests: GPIO17 is wired to RJ45 <pin> (inverter RX) -> pre-arm check, TX\n"
      "  arm-max3232 YES     optional MAX3232 module that REPLACES the HW-0519 (needs reset-link all first)\n"
      "  probe               re-run the read-only protocol probe (armed only)\n"
      "  raw                 last raw reply per command\n"
      "  reset-link [all]    DISARM (GPIO17 high-Z) and forget probe/idle results (all: also self-ID)\n"
      "  capture on|off      append every exchange to /log/raw-0.txt\n"
      "  portal              open the Wi-Fi / credentials portal (10 min)\n"
      "  adminpw <new>       set the local admin password (>= 6 chars)\n"
      "  cfg                 alert thresholds\n"
      "  cloud               uplink state, quota counters, backlog, last error, notification channels\n"
      "  rules               usage-threshold rules and their states (r01..r16)\n"
      "  test-alert          raise TEST_ALERT (ntfy push + e-mail job when alert e-mails are on)\n"
      "  ls                  list LittleFS files\n"
      "  safe-exit           leave safe mode and reboot\n"
      "  reboot"));
}

void printStatus() {
  LinkSnapshot l = app.link();
  DiagState d = app.diag();
  uint32_t age;
  Sample s = app.latest(&age);
  uint32_t e = timeSync.epochOrZero();
  char date[12] = "-", hhmm[8] = "-";
  if (e) {
    tu::dateKey(e, date, sizeof date);
    tu::hhmmKey(e, hhmm, sizeof hhmm);
  }
  Serial.printf("fw %s  boot #%u  reset %s  up %lus  safe %s\n", FW_VERSION, app.bootId, app.resetReason,
                (unsigned long)(millis() / 1000), app.safeMode ? "YES" : "no");
  Serial.printf("wifi %s ip %s rssi %d  portal %s (AP %s)\n", wifiProv.wifiUp() ? "up" : "down",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), wifiProv.portalActive() ? "OPEN" : "closed",
                wifiProv.apName().c_str());
  Serial.printf("time %s %s (%s)\n", date, hhmm, timeSync.source());
  Serial.printf("link %s state=%s adapter=%s dialect=%s profile=%s ok=%d\n", l.armed ? "ARMED" : "NOT-ARMED",
                engineStateName(l.state), l.adapter, l.dialect, l.profile, l.linkOk);
  Serial.printf("     RO=GPIO%u DI=GPIO%u selfId=%s invTX=RJ45-%u GPIO17->RJ45-%u\n", l.roPin, l.diPin,
                l.diConfirmed ? "done" : "pending", l.invTxRj, l.armRx);
  Serial.printf("     tx=%lu ok=%lu nak=%lu timeout=%lu crc=%lu refused=%lu fastOk=%lu fastFail=%lu\n",
                (unsigned long)l.ls.txFrames, (unsigned long)l.ls.ok, (unsigned long)l.ls.nak,
                (unsigned long)l.ls.timeouts, (unsigned long)l.ls.badCrc, (unsigned long)l.ls.refused,
                (unsigned long)l.es.fastOk, (unsigned long)l.es.fastFail);
  for (int pin = 1; pin <= 2; pin++) {
    const IdleResult* r = d.idle.latest(pin);
    if (r) Serial.printf("     idle pin %d: %.1f%% LOW, %u edges -> %s\n", pin, r->pctLow, r->edges, idleVerdictName(r->verdict));
    const IdleResult* u = d.idle.latestUp(pin);
    if (u) Serial.printf("     upper check pin %d (B- on 3V3): %.1f%% LOW, %u edges -> %s\n", pin, u->pctLow, u->edges,
                         idleVerdictName(u->verdict));
  }
  if (d.arm.reason[0]) Serial.printf("     arm: %s\n", d.arm.reason);
  if (l.probeRan) Serial.printf("probe: %s\n", l.probeSummary);
  Serial.printf("sample age %lds bv=%.2f soc=%.0f w=%.0f pvw=%.0f md=%c\n", age == 0xFFFFFFFF ? -1L : (long)(age / 1000),
                s.bv, s.soc, s.w, s.pvw, s.md ? s.md : '-');
  Serial.printf("queue pending=%lu provisional=%lu fs %u/%u KB  heap %u  psram %u  alerts %d\n",
                (unsigned long)minuteQueue.pending(), (unsigned long)minuteQueue.provisional(),
                (unsigned)(minuteQueue.usedBytes() / 1024), (unsigned)(minuteQueue.totalBytes() / 1024),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), alertQueue.size());
  if (d.lastMsg[0]) Serial.printf("last diag: %s\n", d.lastMsg);
  if (settings.adminIsDefault()) Serial.println(F("admin password = the AP password (change it in the portal)"));
}

static void post(DiagKind k, uint8_t arg = 0, bool flag = false) {
  DiagRequest r;
  r.kind = k;
  r.arg = arg;
  r.flag = flag;
  if (!app.postDiag(r)) Serial.println(F("busy - try again"));
}

static void exec(char* c) {
  char* arg = strchr(c, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
  if (!strcmp(c, "help") || !strcmp(c, "?")) help();
  else if (!strcmp(c, "status")) printStatus();
  else if (!strcmp(c, "live")) {
    char buf[700];
    uint32_t age;
    Sample s = app.latest(&age);
    sampleToLiveJson(s, buf, sizeof buf, (int32_t)age);
    Serial.println(buf);
  } else if (!strcmp(c, "selftest")) post(DiagKind::SELF_ID);
  else if (!strcmp(c, "idle")) {
    int p = arg ? atoi(arg) : 0;
    bool up = arg && (strstr(arg, "3v3") || strstr(arg, "3V3"));
    if (p != 1 && p != 2) Serial.println(F("usage: idle 1|2 [3v3]"));
    else post(DiagKind::IDLE, (uint8_t)p, up);
  } else if (!strcmp(c, "loopback")) {
    bool yes = arg && !strcmp(arg, "yes");
    if (!yes) Serial.println(F("BENCH ONLY. Jumper GPIO17<->GPIO18, module and inverter disconnected, then: loopback yes"));
    else post(DiagKind::LOOPBACK, 0, true);
  } else if (!strcmp(c, "arm")) {
    int p = arg ? atoi(arg) : 0;
    bool yes = arg && strstr(arg, "YES");
    if (p != 1 && p != 2) Serial.println(F("usage: arm <1|2> YES   (pin = the RJ45 pin GPIO17 is wired to)"));
    else post(DiagKind::ARM, (uint8_t)p, yes);
  } else if (!strcmp(c, "arm-max3232")) post(DiagKind::ARM_MAX3232, 0, arg && !strcmp(arg, "YES"));
  else if (!strcmp(c, "probe")) post(DiagKind::PROBE);
  else if (!strcmp(c, "reset-link")) post(DiagKind::RESET_LINK, (arg && !strcmp(arg, "all")) ? 1 : 0);
  else if (!strcmp(c, "capture")) post(DiagKind::CAPTURE, 0, arg && !strcmp(arg, "on"));
  else if (!strcmp(c, "raw")) {
    LastReply* lr = (LastReply*)psramAlloc(sizeof(LastReply) * RawRing::NLAST);
    int n = lr ? app.copyLastReplies(lr, RawRing::NLAST) : 0;
    for (int i = 0; i < n; i++) Serial.printf("%-12s %-10s %s\n", lr[i].cmd, linkResultName((LinkResult)lr[i].result), lr[i].text);
    if (!n) Serial.println(F("(no replies yet)"));
    free(lr);
  } else if (!strcmp(c, "portal")) wifiProv.openPortal(cfg::PORTAL_TIMEOUT_S);
  else if (!strcmp(c, "adminpw")) {
    if (!arg || strlen(arg) < 6) Serial.println(F("usage: adminpw <at least 6 chars>"));
    else { settings.setAdminPassword(String(arg)); Serial.println(F("admin password updated")); }
  } else if (!strcmp(c, "cfg")) {
    AlertCfg a;
    settings.loadCfg(a);
    Serial.printf("socLow %.0f bvLow %.2f bvHigh %.2f tempHigh %.0f loadHigh %.0f repeat %dmin quiet %d-%d summary %d\n",
                  a.socLow, a.bvLow, a.bvHigh, a.tempHigh, a.loadHigh, a.notifyRepeatMin, a.quietFrom, a.quietTo,
                  a.summaryHour);
  } else if (!strcmp(c, "cloud")) {
    UplinkStatus u = app.uplinkStatus();
    Serial.printf("uplink %s  cloud %s  auth %s  cok %lu  hs %lu  rq %lu  quota level %u\n", u.state,
                  u.enabled ? "configured" : "NOT configured", u.authOk ? "ok" : "no", (unsigned long)u.lastCloudOkEpoch,
                  (unsigned long)u.handshakesToday, (unsigned long)u.requestsToday, u.quotaLevel);
    Serial.printf("backlog %lu records%s  events pending %u  dropped %lu  alertsRev cloud %.0f\n",
                  (unsigned long)u.backlog, u.backfilling ? " (back-filling)" : "", u.pendingEvents,
                  (unsigned long)u.edrop, u.cloudAlertsRev);
    Serial.printf("ntfy %s  telegram %s  healthchecks %s (last HTTP %d)  nudge %s  pushes ok %lu fail %lu\n",
                  u.ntfyOn ? "on" : "off", u.telegramOn ? "on" : "off", u.healthcheckOn ? "on" : "off", u.lastHcCode,
                  u.nudgeOn ? "on" : "off", (unsigned long)u.pushesOk, (unsigned long)u.pushesFail);
    Serial.printf("smtp e-mail %s  mails ok %lu fail %lu\n", u.emailOn ? "on" : "off", (unsigned long)u.mailsOk,
                  (unsigned long)u.mailsFail);
    if (u.lastError[0]) Serial.printf("last error: %s\n", u.lastError);
    if (u.ruleErr[0]) Serial.printf("rules: %s\n", u.ruleErr);
  } else if (!strcmp(c, "rules")) {
    RuleInfo ri = app.ruleInfo();
    Serial.printf("rules rev %.0f: %u present, %u enabled, %u invalid; e-mail %s, jobs today %u/%u, month starts day %u\n",
                  ri.rev, ri.present, ri.n, ri.invalid, ri.emailOn ? "on" : "off", ri.jobsToday,
                  (unsigned)cfg::MAIL_JOBS_PER_DAY, ri.monthStart);
    for (int i = 0; i < ri.nSlots; i++)
      Serial.printf("  %s  %-6s  e-mails today %u\n", ri.slot[i].rid, rules::stName((rules::St)ri.slot[i].st),
                    ri.slot[i].cnt);
    if (ri.err[0]) Serial.printf("  first problem: %s\n", ri.err);
  } else if (!strcmp(c, "test-alert")) {
    app.raise("TEST_ALERT", 3, "Test alert from the serial console", true);
    Serial.println(F("TEST_ALERT raised"));
  } else if (!strcmp(c, "ls")) {
    const char* dirs[] = {"/", "/www", "/q", "/log", "/cfg"};
    for (const char* dn : dirs) {
      File d = LittleFS.open(dn);
      if (!d || !d.isDirectory()) continue;
      for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        if (!f.isDirectory()) Serial.printf("%s/%s %u\n", strcmp(dn, "/") ? dn : "", f.name(), (unsigned)f.size());
        f.close();
      }
      d.close();
    }
  } else if (!strcmp(c, "safe-exit")) safemode::exitAndReboot();
  else if (!strcmp(c, "meter")) meter();
  else if (!strcmp(c, "reboot")) {
    settings.setStr("sys", "plannedRb", "console");
    Serial.println(F("rebooting"));
    delay(200);
    ESP.restart();
  }
  else if (*c) Serial.printf("unknown command '%s' (help)\n", c);
}

void loop() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (n_) {
        line_[n_] = 0;
        Serial.printf("> %s\n", line_);
        exec(line_);
        n_ = 0;
      }
    } else if (n_ < sizeof line_ - 1 && ch >= 0x20) {
      line_[n_++] = (char)ch;
    }
  }
}

}  // namespace console
