// Uplink.h - cloud uplink (d1 §4.8, §5.5; r8 §4.9). Implemented in Uplink.cpp (FW-2).
//
// Threading: uplink::begin() is called once from setup() (never in safe mode); uplink::loop() is called every
// 1 s by uplinkTask (core 0, 16 KB stack, prio 3). loop() owns ALL TLS (RTDB, Auth, ntfy, Telegram, Healthchecks,
// mailer nudge), feeds the task watchdog between network steps and never blocks longer than ~15 s per step.
//
// Per tick, in priority order (d1 §5.5), at most 3 RTDB requests:
//   1 alerts from alertQueue -> ntfy / Telegram push + one multi-path PATCH {events/<d>/<key>, mailq/<key>}
//   2 live due (cfg.liveSec)  -> PUT live
//   3 minute record pending   -> multi-path PATCH hist + daily + status [+ h15] [+ alertState]
//   4 backlog                 -> PATCH up to 60 records (+ their h15 / daily) every >= 2 s, cursor after 2xx
//   5 cfg due (60 s)          -> GET cfg (thresholds, testAlert, reprobe, alertsRev) [-> GET alerts]
//   6 housekeeping            -> meta, final daily of yesterday, month-to-date bootstrap, diag/raw, prune 00:05
// Healthchecks ping every 5 min (independent of Firebase). Budgets, back-off and the network watchdog: Budget.h.
#pragma once
#include <stdint.h>

struct UplinkStatus {
  bool enabled = false;          // Firebase credentials present and valid-looking
  bool authOk = false;
  uint32_t lastCloudOkEpoch = 0; // `cok`
  bool cloudOkBoot = false, cfgOkBoot = false;  // 1.0.0: this boot wrote / read cfg, whatever the clock (probation)
  uint32_t handshakesToday = 0;  // `hs` (RTDB TLS sessions)
  uint32_t requestsToday = 0;    // `rq` (RTDB requests)
  bool quotaGuard = false;
  bool backfilling = false;
  char lastError[96] = "";
  // FW-2 additions
  char state[12] = "init";       // init | offline | no-cloud | auth | backoff | ready
  uint8_t quotaLevel = 0;        // `qg`
  uint32_t edrop = 0;            // events dropped after permanent RTDB errors
  uint8_t pendingEvents = 0;
  uint32_t backlog = 0;          // minute records not uploaded yet
  double cloudAlertsRev = 0;     // cfg/alertsRev last seen
  char ruleErr[41] = "";         // last alerts.json problem
  bool ntfyOn = false, telegramOn = false, healthcheckOn = false, nudgeOn = false;
  int tgChats = 0;          // paired Telegram chats (0.7.7)
  bool tgPairing = false;   // the logger is waiting for /start in the bot
  uint32_t pushesOk = 0, pushesFail = 0;
  bool emailOn = false;          // SMTP configured in the portal (NVS "smtp")
  uint32_t mailsOk = 0, mailsFail = 0;
  uint32_t tgOk = 0, tgFail = 0;               // 1.0.0: Telegram sends since boot (an alert to n chats counts once)
  char tgErr[41] = "", mailErr[41] = "";       // 1.0.0: last failure (alertErrText), kept after later successes
  uint32_t tgErrAt = 0, mailErrAt = 0;         // epoch of that failure
  int lastHcCode = 0;
};

namespace uplink {
void begin();
void loop();
UplinkStatus status();
void notifyMetaChanged();    // identity / link profile changed -> PUT meta soon
void notifyConfigChanged();  // portal / local API changed credentials or channels -> reload from NVS
void telegramAddPerson();    // open a 10-minute Telegram pairing window ("Add another person")
}  // namespace uplink
