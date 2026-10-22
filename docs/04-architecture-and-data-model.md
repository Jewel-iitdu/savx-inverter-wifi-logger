# 04 – Architecture and data model

This document covers how the logger is put together, what it stores where, how much of the free tier it uses, who can read and write what, how alerts and the e-mail usage-threshold rules work, and how the dashboard is hosted and deployed.

Sources:
- The approved design is [`research/d1-architecture.md`](../research/d1-architecture.md) (§2, §4, §5, §7). It is restated here as the reference for implementation.
- Platform facts come from [`research/v2-platform-verification.md`](../research/v2-platform-verification.md).
- The cloud research behind the data model is in [`research/r6-cloud-web.md`](../research/r6-cloud-web.md).
- Web hosting and CI/CD: [`research/r7-web-hosting.md`](../research/r7-web-hosting.md) (the decision; verified by [r7c](../research/r7c-hosting-verification.md)).
- E-mail usage-threshold alerts: [`research/r8-email-alerts.md`](../research/r8-email-alerts.md) (the decision; verified by [r8c](../research/r8c-email-verification.md)). Where R7 or R8 differ from D1, they win.
- The code as built: [`firmware/README.md`](../firmware/README.md) §8 lists the 18 deliberate data-contract differences from D1 and R8; this document follows the code.

Related docs:
- Firmware internals: [05 – Firmware guide](05-firmware-guide.md)
- Setting up Firebase, GitHub, Cloudflare, the PWA and the mailer: [06 – Cloud and dashboard guide](06-cloud-and-dashboard-guide.md)
- Protocol fields: [02 – Protocol reference](02-protocol-reference.md)

---

## Contents

1. [System overview](#1-system-overview)
2. [Data paths](#2-data-paths)
3. [Behaviour under failure](#3-behaviour-under-failure)
4. [Firmware architecture (overview)](#4-firmware-architecture-overview)
5. [Cloud data model](#5-cloud-data-model)
6. [Sample intervals, aggregation and energy](#6-sample-intervals-aggregation-and-energy)
7. [Retention, pruning and quota math](#7-retention-pruning-and-quota-math)
8. [Security model](#8-security-model)
9. [Alerts design](#9-alerts-design) (incl. [e-mail](#95-e-mail-alerts-the-apps-script-mailer) and [usage-threshold rules](#96-usage-threshold-rules))
10. [Local LAN UI](#10-local-lan-ui)
11. [Why Firebase RTDB](#11-why-firebase-rtdb)
12. [Web hosting and CI/CD](#12-web-hosting-and-cicd)

---

## 1. System overview

```
 +---------------------+   RS232 (COM pins 1/2/8)    +-------------------------------+
 |  Savx F4 inverter   |<--------------------------->|  HW-0519 receiver + ESP32-S3  |
 |  PI30 @ 2400 8N1    |   read-only queries          |  invTask  aggTask  uplinkTask |
 +---------------------+                              |  LittleFS queue, NVS, PSRAM   |
                                                      +---+-----------------------+---+
                                         LAN (HTTP)       |                       | HTTPS (TLS, pinned roots)
                       iPhone at home <-------------------+                       |
                       http://solar.local                                         v
                                                  +----------------------------------------------+
                                                  | Firebase (Spark plan, no cost)                |
                                                  |  Auth: owner, device and mailer users         |
                                                  |  RTDB asia-southeast1: /devices/inv1/...      |
                                                  |  (Hosting: cold fallback only)                |
                                                  +-----+--------------+-------------------+-----+
                                     WebSocket (JS SDK) |              ^ REST, every 5 min |
                                                        v              |                   |
   Cloudflare Workers static assets --PWA shell--> iPhone anywhere:    |    Google Apps Script "mailer"
   https://solar.<acct>.workers.dev          Home Screen PWA     |    (dedicated Gmail sender)
        ^                                          (owner sign-in)     |         |  MailApp
        | deploy-web                                                   |         v
   GitHub Actions (private repo) ----deploy-rules (rules only)---------+   recipients' inboxes
         ESP32 --> ntfy.sh --> APNs --> ntfy app          (primary push)
         ESP32 --> api.telegram.org --> Telegram          (secondary, severity >= 4)
         ESP32 --> hc-ping.com every 5 min; silence --> Healthchecks e-mail (dead-man's switch)
         ESP32 --> script.google.com nudge (optional, no data; makes the mailer poll at once)
```

| Component | Runs on | Responsibility |
|---|---|---|
| Inverter link | ESP32 UART1 + HW-0519 receiver | The only code that may transmit to the inverter. Enforces the whitelist |
| Protocol adapters | ESP32 | Probe, classify the dialect, poll, decode into a normalized `Sample` |
| Aggregator | ESP32 | 1-minute, 15-minute and daily aggregates; energy integration; built-in alerts (`AlertEngine`) |
| Rule engine | ESP32 | Up to 16 owner-defined usage-threshold rules, every 5 s, with hold, recovery, cooldown and daily caps; billing-month sums ([§9.6](#96-usage-threshold-rules)) |
| Minute queue | ESP32 LittleFS | System of record until a record is uploaded; about 40 days of capacity |
| Uplink | ESP32 | Auth, RTDB writes (events with their e-mail jobs, `live`, minute records, back-fill), `cfg` and `alerts` polls, pruning, pushes, optional mailer nudge |
| Local UI | ESP32 `WebServer` | Live tiles, 24 h chart, diagnostics, settings, OTA. Works without internet |
| Firebase RTDB | Google, Singapore | Live value, history, events, configuration |
| PWA | iPhone Safari / Home Screen, served by Cloudflare | Live, Today, History, Energy, **Alerts** (Rules / History / E-mail) and Settings pages |
| Mailer | Google Apps Script in a dedicated Gmail account | Every 5 min: sends queued alert e-mails, the daily summary and "logger offline"; writes a heartbeat ([§9.5](#95-e-mail-alerts-the-apps-script-mailer)) |
| ntfy / Telegram / Healthchecks | Third-party free tiers | Push alerts and the dead-man's switch |
| GitHub Actions | GitHub Free, private repo | CI checks; deploys the PWA to Cloudflare and the rules to Firebase ([§12](#12-web-hosting-and-cicd)) |

---

## 2. Data paths

| Path | Content | Cadence | Notes |
|---|---|---|---|
| Inverter → ESP | PI30 replies (`QPIGS`/`QALL`, `QMOD`, `QPIWS`, …) | 5 s fast cycle | One command in flight at a time; CRC-checked |
| ESP RAM/PSRAM | Normalized `Sample`; 24 h ring of 5 s samples | 5 s | Local charts only. Lost on reboot, which is acceptable |
| ESP LittleFS | 1-minute `MinuteRec` (55 B) | 60 s | System of record until uploaded. Kept 14 days after upload, about 40 days if never uploaded |
| ESP → RTDB | `live` (10 s); minute PATCH (`hist` + `daily` + `status` + `h15` + `alertState`); events with their `mailq` job (immediately); `meta`; `diag` | See [§6.1](#61-sample-intervals) | REST with `print=silent` and keep-alive |
| RTDB → PWA | `live` listener; date-bounded reads of `hist`/`h15`/`daily`/`events`; `alerts`, `alertState`, `mailer` on the Alerts page | When a page is viewed | Never subscribes to a whole history root |
| ESP → alerts | ntfy (JSON publish) / Telegram POST; Healthchecks GET; optional mailer nudge GET | On event; every 5 min | [§9](#9-alerts-design) |
| PWA → RTDB → ESP | `cfg` (thresholds, `testAlert`, `reprobe`, `alertsRev`); `alerts/rules`, `alerts/email` | The device polls `cfg` every 60 s and fetches `alerts` when `alertsRev` changes (and at boot, every 6 h) | Only the owner may write |
| RTDB ↔ mailer | Reads `mailq`, the queued events, `alerts/email`, `live`, `daily`; writes `em`/`emt` on events, deletes `mailq` jobs, writes `mailer` | Every 5 min (idle run = 3 small requests) | [§9.5](#95-e-mail-alerts-the-apps-script-mailer) |
| PWA → mailer | `mailq/test` ("Send test e-mail") | Next mailer run | Owner only; result in `mailer/test` |
| GitHub → Cloudflare / Firebase | The built PWA (`web/dist`); the rendered rules | On a push to `main` that touches them | [§12](#12-web-hosting-and-cicd) |

---

## 3. Behaviour under failure

| Failure | Logging | Local UI | Cloud live | Cloud history | Alerts |
|---|---|---|---|---|---|
| Grid outage (inverter on battery) | Yes | Yes | Yes, if the router is on AC OUT | Yes | `GRID_LOSS` sent |
| ISP down, router up | Yes | Yes (LAN) | No | Back-filled later | Queued in RAM and sent on reconnect. Healthchecks e-mails after the grace period |
| Router down | Yes | No | No | Back-filled later | As above |
| ESP loses power (inverter output off) | No (gap) | No | Stale. The PWA shows "Logger offline" | Gap recorded | Healthchecks e-mail |
| Inverter link lost (wiring, firmware) | Status only | Yes, with diagnostics | `lk = 0` | Minutes with `n = 0` | `LINK_LOST` |
| Firebase Spark quota exceeded | Yes | Yes | No, for the rest of the month | Back-filled next month if the queue still covers it | ntfy unaffected |
| ESP reboots with no internet | Yes, with provisional timestamps | Yes | No | Timestamps fixed after NTP ([§4.4](#44-time)) | Queued |
| Mailer stopped (trigger off, sender account locked, Google quota) | Yes | Yes | Yes | Yes | Pushes unaffected. E-mail jobs wait in `mailq` (expire after 12 h, 24 h for severity ≥ 4). The PWA shows "Mailer not running" after 15 min. Healthchecks still e-mails "logger offline" |
| Logger offline for longer than `offline.min` (default 30 min) | as above | – | Stale | Gap or back-fill | One "Logger offline" e-mail from the mailer and one "back online" e-mail; rule events from the outage arrive later marked "Delayed" |
| Cloudflare outage | Yes | Yes | Yes | Yes | Installed apps keep running from the service-worker cache with live Firebase data; only first installs and updates are blocked |
| GitHub Actions unavailable or out of minutes | Yes | Yes | Yes | Yes | Nothing changes at runtime; deploys wait, or go from the Mac ([06](06-cloud-and-dashboard-guide.md) §3.3, §5.4) |

---

## 4. Firmware architecture (overview)

The full guide is in [05 – Firmware guide](05-firmware-guide.md). The design source is D1 §5.

### 4.1 FreeRTOS tasks

| Task | Core | Prio | Stack | Period | Owns |
|---|---|---|---|---|---|
| `invTask` | 1 | 5 | 12 KB | 50 ms scheduler tick; 5 s fast cycle | UART1 (`SerialLink`), adapter, prober. Publishes `Sample` to a queue (newest wins) and to a mutex-protected "latest" copy |
| `aggTask` | 1 | 4 | 10 KB | On each sample; minute, 15-minute and day roll-overs | Accumulators, energy integration, PSRAM ring, appends to `MinuteQueue`, `AlertEngine` and `RuleEngine` → alert queue (depth 24), month-to-date sums |
| `uplinkTask` | 0 | 3 | 16 KB | 1 s tick | **All TLS**: RTDB, Auth, ntfy, Telegram, Healthchecks, the optional mailer nudge. Back-fill, prune, `cfg`/`alerts` poll. One kept-alive TLS client to the database; one-shot clients for tokens and notifications |
| Arduino `loopTask` | 1 | 1 | 8 KB | Continuous | `WebServer`, ElegantOTA, WiFiManager portal, BOOT long-press, status LED |

Event-group bits: `WIFI_UP`, `TIME_OK`, `AUTH_OK`, `LINK_OK`, `SAFE_MODE`, `PORTAL`. All TLS objects stay inside `uplinkTask`, so non-thread-safe clients are never shared. Stack sizes are from `include/config.h` (D1 had 8/8/16 KB; the firmware adds headroom for the probe and alert texts).

### 4.2 Module map

| Area | Modules | Purpose |
|---|---|---|
| `link/` | `SerialLink`, `Whitelist`, `PinDiag`, `LinkProfile` | UART1, link profile and inversion flags, whitelist gate, raw-frame ring; module RO/DI self-ID, the idle-level test, the upper check and the pre-arm check |
| `proto/` | `Engine`, `Prober`, `Scheduler`, `pi30/*`, `legacy/Q6Adapter`, `modbus/*`, `RawCapture` | Probe tree, dialect classification, per-command periods, decoding |
| `model/` | `Sample`, `Identity`, `Decode`, `LiveJson`, `JsonAlloc` | Normalized data model ([§5.3](#53-field-dictionary)); the `live` JSON (incl. `gw`); a PSRAM allocator for big JSON documents |
| `agg/` | `Aggregator`, `AlertEngine`, `RuleTypes`, `RuleParse`, `RuleEngine`, `NotifyGates`, `MonthToDate` | Minute, h15 and daily aggregates; energy; built-in alert state machines; the rule metric catalogue, `alerts.json` parser, per-rule state machine, push/e-mail gates and billing-cycle sums |
| `store/` | `MinuteQueue`, `MinuteRec`, `Settings` | LittleFS append-only queue; NVS namespaces |
| `net/` | `WifiProv`, `TimeSync`, `HttpParse`, `HttpsClient`, `FirebaseRest`, `UplinkJson`, `Backfill`, `Budget`, `Uplink`, `Notify`, `NotifyFormat`, `MailNudge` | Provisioning, NTP, the firmware's own HTTP framing (not `HTTPClient`, because of arduino-esp32 3.3.12 issues), REST client, request bodies, back-fill planning, quota guard, uplink state machine, ntfy/Telegram/Healthchecks, the optional nudge |
| `web/` | `LocalWeb` | Local UI, JSON API, OTA, mDNS |
| `sys/` | `Watchdog`, `StatusLed`, `SafeMode` | TWDT, LED patterns, boot-failure counter |
| `app/` | `App`, `Tasks`, `Console` | Wiring of the tasks, the serial console (incl. `cloud`, `rules`, `test-alert`) |

### 4.3 Offline buffer and back-fill

- **LittleFS layout:**
  - `/q/<YYYY-MM-DD>.bin`: append-only, 55 B per minute record
  - `/q/<date>.up`: upload cursor
  - `/q/anchors.bin`: time anchors
  - `/log/probe.json`, `/log/raw-*.txt`
- **Capacity:** 1,440 × 55 B ≈ 79 KB/day. The 3.4 MB partition therefore holds about **40 days**.
- **Housekeeping:** uploaded files are kept 14 days for the local history page. When space runs out, the oldest *uploaded* file is dropped first. Dropping unuploaded data raises a `DATA_DROPPED` event.
- **Uplink priorities** (at most 3 database requests per 1 s tick, in this order):
  1. Events, each with its e-mail job (`mailq`) in the same PATCH
  2. `live` when due
  3. The pending minute record (multi-path PATCH with `daily`, `status`, completed `h15` and dirty `alertState`)
  4. Backlog: up to 60 records per PATCH, at least 2 s apart, in **one** combined PATCH on `/devices/inv1.json` (with the completed `h15` and the day totals)
  5. `cfg` poll every 60 s, and `GET alerts.json` when `cfg/alertsRev` changed
  6. Housekeeping: `meta`, final day totals, the month-to-date bootstrap, `diag`, Healthchecks ping, prune
- **On errors:**
  - 401 → refresh the token once and retry; a second 401/403 means the rules refused the request.
  - 429, 5xx or timeout → back off 5, 10, 20 … 300 s.
  - Wrong credentials pause sign-in for 30 min (saving the portal again ends the pause).
  - An event refused 3 times is dropped and counted in `status.edrop`. Minute records are **never** dropped: they back off and retry.
  - WiFi up but no database answer for 15 min → drop TLS and re-authenticate. After 60 min → reboot, but only if a TLS connection worked in that hour, and at most once per 6 h. A plain ISP outage or load-shedding therefore never causes reboot loops ([`firmware/README.md`](../firmware/README.md) §8 D9).
- **Idempotent keys:** records are written under `date/hhmm` and events under deterministic keys, so a retry overwrites instead of duplicating. A 2-day outage back-fills in about 48 PATCH requests, roughly 2 minutes.
- **Past days' totals** are written field by field (`daily/<d>/epv` …) recomputed from the day file, so back-fill never resets `nf`, `nw` or `epvi` to 0 ([`firmware/README.md`](../firmware/README.md) §8 D3).

### 4.4 Time

- `configTzTime("<+06>-6", "pool.ntp.org", "time.google.com")`. Asia/Dhaka has no DST. The clock counts as valid when the year is ≥ 2026.
- **HTTPS works before NTP.** mbedTLS date checks are off in the Arduino build (V2 §3.7). Only data timestamps need NTP.
- **Before NTP:** records are stored with `bootId` and `upMs` and held in the queue. On the first sync in the same boot, a time anchor fixes their timestamps. If the ESP reboots before it ever syncs, those records go up with an estimated time and `tq: 0`.

---

## 5. Cloud data model

### 5.1 Tree

Conventions:
- Date keys are the **local Asia/Dhaka** date `YYYY-MM-DD`; minute keys are local `hhmm`.
- Timestamps (`ts`) are **UTC epoch seconds**.
- No key contains `. $ # [ ] /` (V2 F4).

```
/devices/inv1/
    live                          PUT every 10 s (~354 B)
    status                        in the minute PATCH (~208 B)
    meta                          PUT on boot and on change (identity, ratings, dialect, link profile)
    diag/raw/{cmd}                PATCH every 10 min: last raw reply per command (<= 300 chars each)
    hist/{YYYY-MM-DD}/{hhmm}      1-minute aggregate (~242 B incl. key)
    h15/{YYYY-MM-DD}/{hhmm}       15-minute aggregate (hhmm in xx00, xx15, xx30, xx45)
    daily/{YYYY-MM-DD}            day totals (~183 B incl. key)
    events/{YYYY-MM-DD}/{key}     alerts and transitions (~133 B; rule events ~200 B). key = <ts>-<src>-<o|c|r|i>
    cfg                           owner-written settings and commands (+ alertsRev)
    alerts/email                  owner: e-mail settings and up to 3 recipients (~300 B)          [R8]
    alerts/rules/{r01..r16}       owner: usage-threshold rules (~250-350 B each)                 [R8]
    alertState/{r01..r16}         device: rule engine state, written on transitions              [R8]
    mailq/{eventKey}              device: e-mail job for an event; the mailer deletes it         [R8]
    mailq/test                    owner: "Send test e-mail" request; the mailer deletes it       [R8]
    mailer                        mailer: heartbeat, quota, counters, last result               [R8]
```

Sizes are compact JSON, measured in D1 (and R8 for the `[R8]` nodes) on the example documents below.

### 5.2 Documents (exact JSON)

**`live`**

```json
{"ts":1792560000,"lk":1,"gv":231.4,"gf":50.0,"ov":230.1,"of":50.0,"va":540,"w":512,"ld":16,"bus":381,
 "bv":26.42,"bci":12,"bdi":0,"bi":12.0,"bw":317,"soc":78,"tmp":38,"pvv":145.3,"pvi":3.2,"pvw":465,
 "sccv":26.44,"md":"L","st":"00010110","st2":"010","wf":"00000000000000000000000000000000","fc":0,
 "epv":1920,"eld":2080,"egr":330,"ech":900,"edis":410,"q":0}
```

**`hist/2026-10-21/1435`**: the 1-minute aggregate.

```json
{"w":498,"wx":620,"pvw":461,"pvx":480,"bv":26.4,"bvn":26.31,"bvx":26.48,"bi":11.6,"soc":78,"gv":231.2,
 "gvn":229.8,"ov":230.0,"ld":16,"tmp":38,"pvv":144.9,"md":"L","wn":0,"fc":0,"n":12,
 "epv":7.7,"eld":8.3,"ech":5.1,"edis":0,"egr":8.3}
```

**`h15/2026-10-21/1430`** has the same schema as `hist`. It is built from 15 minute records:
- averages are weighted by `n`;
- minima take the minimum of the minima, and maxima the maximum of the maxima;
- energies are summed;
- `n` is the sum of the minutes' `n`.

**`daily/2026-10-21`**

```json
{"epv":12840,"eld":9710,"egr":1220,"ech":4100,"edis":3050,"epvi":12700,"pvx":2410,"wx":1840,
 "bvn":24.9,"bvx":28.6,"socn":31,"socx":100,"gout":42,"nf":0,"nw":3,"n":1440}
```

**`events/2026-10-21/1792560123-GRID_LOSS-o`**

```json
{"ts":1792560123,"type":"GRID_LOSS","sev":4,"st":"open","msg":"Grid lost, on battery (SOC 78%)","ch":"ntfy","em":"q"}
```

The key is deterministic: `<10-digit ts>-<src>-<kind>`, where `src` is the built-in `type` or the rule slot `r01`…`r16`, and `kind` is `o` (open), `c` (clear), `r` (repeat / still active) or `i` (info). A retry overwrites the same key. D1 used push IDs (`-Nx…`); the rules still accept them ([R8](../research/r8-email-alerts.md) §3.6, [`firmware/README.md`](../firmware/README.md) §8 D1).

**`events/2026-10-21/1792580003-r03-o`**: a rule event.

```json
{"ts":1792580003,"type":"RULE","sev":3,"st":"open","rid":"r03","val":5010,"thr":5000,
 "msg":"Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)","ch":"email","em":"q"}
```

**`status`**

```json
{"ts":1792560000,"fw":"0.6.1","ip":"<logger-ip>","rssi":-61,"heap":182340,"psram":8123456,"up":86400,
 "rr":"POWERON","q":0,"proto":"pi30-2400","dia":"REVO","hs":12,"rq":1450,"lok":1792559995,"cok":1792560000,
 "qg":0,"edrop":0,"arv":1792560000000,"acat":1,"mjq":2,"nrul":3}
```

**`meta`**: a flat map so the rules can validate each key. Example values are illustrative; real values come from the M3 probe.

```json
{"qpi":"PI30","qid":"<serial>","vfw":"00014.04","vfw2":"00003.03","qmn":"","ratva":3200,"ratw":3200,
 "ratbv":24.0,"btype":2,"outpri":0,"chgpri":2,"inrng":0,"dia":"REVO","pv12":"W",
 "sup":"QPI,QPIGS,QPIRI,QMOD,QALL","link":"rs232lite-2400-ii-tx1","fwver":"0.6.1","boot":1792473600}
```

**`diag/raw`**: the reply strings below are illustrative, not a real capture. Real replies are recorded in M3.

```json
{"QPIGS":"(231.4 50.0 230.1 50.0 0540 0512 016 381 26.42 012 078 0038 03.2 145.3 26.44 00000 00010110 00 00 00465 010","QPIRI":"(…"}
```

**`cfg`**: owner-written. The defaults are shown.

```json
{"liveSec":10,"socLow":30,"bvLow":22.5,"bvHigh":30.0,"tempHigh":75,"loadHigh":90,"notifyRepeatMin":30,
 "keepHistDays":400,"keepEventDays":730,"quietFrom":22,"quietTo":7,"summaryHour":21,"testAlert":0,"reprobe":0,
 "alertsRev":1792560000000}
```

**`alerts/email`** ([R8](../research/r8-email-alerts.md) §3.2): owner-written. `to` is one comma-separated string, not an array.

```json
{"on":true,"to":"owner@gmail.com,family@gmail.com","fromName":"Solar logger","maxPerDay":40,"monthStart":1,
 "builtin":{"minSev":4,"clear":true},"summary":{"on":true,"hour":7},"offline":{"on":true,"min":30,"clear":true}}
```

**`alerts/rules/r01`** ([R8](../research/r8-email-alerts.md) §3.3): owner-written; at most 16 (`r01`…`r16`).

```json
{"name":"Load > 2500 W for 5 min","on":true,"metric":"w","scope":"inst","op":"gt","value":2500,"mode":"hold",
 "forSec":300,"clr":2200,"clrSec":120,"coolMin":60,"repeatMin":0,"maxDay":3,
 "win":{"from":"0600","to":"2300","days":127},"sev":3,"email":true,"ntfy":true,"rc":1,"clrMail":true,"upd":1792560000000}
```

**`alertState`** (device), **`mailq`** (device + owner, emptied by the mailer) and **`mailer`** (mailer heartbeat):

```json
{"alertState":{"r01":{"st":"fire","since":1792560123,"val":2710,"last":1792560125,"cnt":1}},
 "mailq":{"1792580003-r03-o":{"d":"2026-10-21","ts":1792580003,"sev":3},"test":{"ts":1792580100}},
 "mailer":{"hb":1792580400,"ver":"1.0.0","day":"2026-10-21","sent":4,"quota":93,"off":0,"err":"","warn":"",
           "last":{"ts":1792580003,"ok":true,"msg":"[Solar] Daily grid usage > 5 kWh: now 5.01 kWh"},
           "test":{"ts":1792580100,"ok":true,"msg":"sent to 2 recipients"}}}
```

Idle state: `mailq` is `null`, so the mailer's poll downloads almost nothing.

### 5.3 Field dictionary

Conventions:
- **SI units.** Power in W, energy in Wh unless stated, voltage in V, current in A, temperature in °C.
- **Missing values are omitted** from the JSON, never sent as `null` or `0`. A field that a dialect does not provide simply never appears.
- **Short keys.** The same keys are used in the firmware `Sample`, the local `/api/live` and the cloud `live` node.
- **Field indices** are 0-based after stripping `(` and the CRC. See [02](02-protocol-reference.md) for the full maps.

#### `live` (and the local `/api/live`)

| Key | Unit | Type | Meaning | Source: stock PI30 (17/21/24-field) | Source: REVO dialect |
|---|---|---|---|---|---|
| `ts` | s (UTC epoch) | int | Sample time | Device clock (NTP) | Device clock |
| `lk` | 0/1 | int | 1 = a valid inverter reply within the last 15 s | Link layer | Link layer |
| `gv` | V | num | Grid (AC input) voltage | QPIGS[0] | QALL[0] |
| `gf` | Hz | num | Grid frequency | QPIGS[1] | QALL[1] |
| `ov` | V | num | AC output voltage | QPIGS[2] | QALL[2] |
| `of` | Hz | num | AC output frequency | QPIGS[3] | QALL[3] |
| `va` | VA | int | Output apparent power | QPIGS[4] | – (reserved on REVO) |
| `w` | W | int | Output active power (load) | QPIGS[5] | QALL[4] |
| `ld` | % | int | Output load percent | QPIGS[6] | QALL[5] |
| `bus` | V | int | DC bus voltage | QPIGS[7] | QPIGS[7] (polled every 30 s) |
| `bv` | V | num | Battery voltage | QPIGS[8] | QALL[6] |
| `bci` | A | int | Battery charging current (1 A resolution) | QPIGS[9] | QALL[8] |
| `bdi` | A | int | Battery discharge current | QPIGS[15] | QALL[9] (**not** QPIGS[15], which is PV Wh today on REVO) |
| `bi` | A | num | Net battery current, + = charging | Derived: `bci − bdi` | Derived |
| `bw` | W | int | Battery power, + = charging | Derived: `bv × bi` | Derived |
| `soc` | % | int | Battery capacity as the inverter reports it | QPIGS[10] | QALL[7] |
| `tmp` | °C | int | Heat-sink temperature. Flagged as raw ADC if > 150 | QPIGS[11] | – (reserved) |
| `pvi` | A | num | PV input current | QPIGS[12] if classified as current (`pv12 = A`) | QALL[11] |
| `pvv` | V | num | PV input voltage | QPIGS[13] | QALL[10] |
| `pvw` | W | int | PV power. On stock firmware this is "PV charging power" (charger output) | QPIGS[19] (21/24-field); 17-field: `pvv × pvi` | QALL[12] |
| `sccv` | V | num | Battery voltage as measured by the solar charger | QPIGS[14] | – |
| `md` | letter | str | Mode: P power-on, S standby, L line, B battery, F fault, H power-saving, D shutdown, C charge, Y bypass, E ECO | QMOD | QALL[15] |
| `st` | 8 chars | str | Status bits b7…b0: b6 config changed, b4 load on, b2 charging, b1 solar charging, b0 AC charging | QPIGS[16] | QPIGS[16] |
| `st2` | 3 chars | str | Status bits b10 b9 b8 | QPIGS[20] | QPIGS tail |
| `wf` | 32/36/38 chars | str | QPIWS warning/fault bit string | QPIWS | QPIWS if supported |
| `wc` | code | int | Warning code (REVO table 00–07, 21–26) | – | QALL[16] |
| `fc` | code | int | Fault code. ⚠️ LCD codes and REVO codes are **different tables** ([§5.4](#54-warning-and-fault-codes)) | QPIWS a31–a32 (GK/MAX 24 V) | QALL[17] |
| `epv` | Wh | int | PV energy today, **integrated on the device** | [§6.2](#62-on-device-aggregation-and-energy-integration) | same |
| `eld` | Wh | int | Load energy today, integrated | same | same |
| `ech` | Wh | int | Battery charge energy today, integrated | same | same |
| `edis` | Wh | int | Battery discharge energy today, integrated | same | same |
| `egr` | Wh | int | Grid-supplied energy today (**estimate**; labelled "est." in the UI) | same | same |
| `epvi` | Wh | int | PV energy today **as reported by the inverter** (only if supported) | `QEDyyyymmdd` (MAX) | QALL[13] |
| `etot` | kWh | num | Lifetime PV energy reported by the inverter (only if supported) | `QET` / 1000 (MAX) | QALL[14] |
| `q` | count | int | Minute records waiting for upload | Queue | Queue |
| `gw` | W | int | Grid import power **estimate**: `w + acChgW` while the grid is present and `md = L`, else 0. The integrand of `egr`, so the PWA shows the value the rules compare ([R8](../research/r8-email-alerts.md) §3.9) | Derived | Derived |

With `lk = 0` (no valid reply for 15 s) the device writes only `{ts, lk:0, q}`, so stale values are never shown as current ([`firmware/README.md`](../firmware/README.md) §8 D5).

#### `hist/{date}/{hhmm}` and `h15/{date}/{hhmm}`

| Key | Unit | Aggregate | Meaning |
|---|---|---|---|
| `w` | W | average | Load power |
| `wx` | W | maximum | Peak load power |
| `pvw` | W | average | PV power |
| `pvx` | W | maximum | Peak PV power |
| `bv` | V | average | Battery voltage |
| `bvn` | V | minimum | Lowest battery voltage |
| `bvx` | V | maximum | Highest battery voltage |
| `bi` | A | average | Net battery current, + = charging |
| `soc` | % | last | SOC at the end of the interval |
| `gv` | V | average | Grid voltage |
| `gvn` | V | minimum | Lowest grid voltage |
| `ov` | V | average | Output voltage |
| `ld` | % | average | Load percent |
| `tmp` | °C | maximum | Peak heat-sink temperature |
| `pvv` | V | average | PV voltage |
| `md` | letter | dominant | The most frequent mode letter in the interval |
| `wn` | count | – | Number of distinct warning bits seen |
| `fc` | code | last non-zero | Fault code seen in the interval (0 = none) |
| `n` | count | sum | Valid samples (12 = a complete minute at 5 s). **Required** by the rules. 0 means the link was down for that minute |
| `epv`, `eld`, `ech`, `edis`, `egr` | Wh | sum | Energy in this interval, 0.1 Wh resolution |
| `tq` | flag | – | Present (`0`) only when the timestamp is provisional ([§4.4](#44-time)) |

Only `md` may be a string. Every other key must have a numeric value and a name of at most 4 characters (rule-enforced).

#### `daily/{date}`

| Key | Unit | Meaning |
|---|---|---|
| `epv` | Wh | PV energy (integrated) |
| `eld` | Wh | Load energy |
| `egr` | Wh | Grid energy (estimate) |
| `ech` | Wh | Battery charge energy |
| `edis` | Wh | Battery discharge energy |
| `epvi` | Wh | PV energy as reported by the inverter (if supported), for reconciliation |
| `pvx` | W | Peak PV power |
| `wx` | W | Peak load power |
| `bvn` / `bvx` | V | Lowest / highest battery voltage |
| `socn` / `socx` | % | Lowest / highest SOC |
| `gout` | min | Minutes without grid |
| `nf` / `nw` | count | Fault / warning events opened that day |
| `n` | min | Minutes with data. Coverage = `n / 1440` |

Running totals are rewritten in every minute PATCH and finalized at 00:05 local.

#### `events/{date}/{key}`

| Key | Unit | Meaning |
|---|---|---|
| `ts` | s (UTC) | When the condition opened, cleared or happened |
| `type` | string ≤ 24 | Alert type ([§9.2](#92-triggers)), `RULE` or `RULE_INVALID` for rule events |
| `sev` | 0–5 | Severity: 5 critical … 1 info |
| `st` | string | `open`, `clear` or `info` |
| `msg` | string ≤ 200 | Human-readable text, e.g. "Grid lost, on battery (SOC 78%)". The mailer uses it as the e-mail subject |
| `ch` | string ≤ 16 | Channels, e.g. `ntfy` or `email` |
| `dur` | s | On `clear` only: how long the condition lasted |
| `ack` | bool | Set by the owner from the PWA |
| `rid` | `r01`…`r16` | Rule slot (rule events only) |
| `val`, `thr` | canonical units | Value and threshold at the transition (W, Wh, V, A, %, °C, Hz, min) |
| `em` | string | E-mail status. Set by the device: `q` (queued), or not queued because of `cool` (cooldown), `rcap` (the rule's daily limit), `cap` (the device's 40-job cap), `skip` (data coverage too low). Set by the mailer: `sent`, `cap` (daily recipient cap), `quota` (Google quota floor), `off` (e-mail switched off), `norc` (no allowed recipient), `exp` (too old), `fail` (5 send errors). No `em` = not meant for e-mail |
| `emt` | s (UTC) | When the mailer set `em` |

No other keys are allowed. The mailer may write only `em` and `emt`, and only on an existing event.

#### `status`

| Key | Unit | Meaning |
|---|---|---|
| `ts` | s (UTC) | Time of this status |
| `fw` | string | Logger firmware version |
| `ip` | string | LAN IP address |
| `rssi` | dBm | WiFi signal strength (below −75 means move the box or router) |
| `heap` | B | Free internal heap |
| `psram` | B | Free PSRAM |
| `up` | s | Uptime |
| `rr` | string | Reset reason from `esp_reset_reason()`, e.g. `POWERON` |
| `q` | count | Minute records waiting for upload |
| `proto` | string | Active protocol and baud, e.g. `pi30-2400` |
| `dia` | string | PI30 dialect: `STOCK17`, `STOCK21`, `MAX24`, `GK` or `REVO` |
| `hs` | count | TLS handshakes today (quota guard) |
| `rq` | count | HTTPS requests today (quota guard) |
| `lok` | s (UTC) | Last valid inverter reply |
| `cok` | s (UTC) | Last successful cloud write |
| `qg` | 0–2 | Quota-guard level ([§7.5](#75-runaway-protection-mandatory)) |
| `edrop` | count | Events dropped after 3 refused attempts |
| `arv` | ms | The `alertsRev` the device has loaded. The PWA shows "Logger has your latest rules" when it equals `cfg/alertsRev` |
| `acat` | int | Rule metric catalogue version (1). The PWA hides metrics newer than this |
| `mjq` | count | E-mail jobs queued today (cap 40) |
| `nrul` | count | Enabled rules loaded |
| `ruleErr` | string ≤ 40 | Why the latest `alerts.json` could not be used (the previous set stays active) |

#### `meta`

| Key | Meaning | Source |
|---|---|---|
| `qpi` | Protocol ID, e.g. `PI30` | `QPI` |
| `qid` | Inverter serial number | `QID` / `QSID` |
| `vfw` / `vfw2` | Main / secondary CPU firmware. Should equal the LCD's "U1" / "U2" readouts | `QVFW` / `QVFW2` |
| `qmn` | Model name (if supported) | `QMN` |
| `ratva` / `ratw` | Rated VA / W (expect 3200 / 3200) | QPIRI[5] / QPIRI[6] |
| `ratbv` | Rated battery voltage (expect 24.0) | QPIRI[7] |
| `btype` | Battery type code | QPIRI[12] |
| `outpri` / `chgpri` | Output-source / charger-source priority codes | QPIRI[16] / QPIRI[17] |
| `inrng` | AC input range (0 = appliance/APL, 1 = UPS) | QPIRI[15] |
| `dia` | Dialect | Prober |
| `pv12` | `W` or `A`: how QPIGS field 12 was classified | Prober ([02](02-protocol-reference.md)) |
| `sup` | Comma list of supported optional commands | Prober |
| `link` | Link profile, e.g. `rs232lite-2400-ii-tx1`: mode, baud, RX/TX inversion, which RJ45 pin is inverter TX | NVS |
| `fwver` | Logger firmware version | Build |
| `boot` | Boot time (UTC epoch) | Device |

#### `diag/raw/{cmd}`

The last raw reply for each command, as ASCII, at most 300 characters. It is used for remote debugging and re-decoding (for example after a dialect correction). It is written every 10 minutes.

#### `cfg` (owner-written; the device polls it every 60 s)

| Key | Unit | Range (rules) | Default | Effect |
|---|---|---|---|---|
| `liveSec` | s | 2–300 (0.7.2; was 5–300) | 10 | `live` upload period |
| `socLow` | % | 5–80 | 30 | `BATT_LOW` threshold |
| `bvLow` | V | 20–30 | 22.5 | `BATT_LOW` voltage threshold |
| `bvHigh` | V | 26–34 | 30.0 | `BATT_HIGH` threshold. Set it from the battery's spec |
| `tempHigh` | °C | 40–100 | 75 | `OVER_TEMP` threshold |
| `loadHigh` | % | 50–120 | 90 | `OVERLOAD` threshold |
| `notifyRepeatMin` | min | 5–1440 | 30 | Re-notify interval for active alerts of severity ≥ 4 |
| `keepHistDays` | days | 30–3650 | 400 | Retention of `hist` |
| `keepEventDays` | days | 30–3650 | 730 | Retention of `events` |
| `quietFrom` / `quietTo` | hour | 0–23 | 22 / 7 | Quiet hours (local) for severity ≤ 3 pushes |
| `summaryHour` | hour | −1…23 | 21 | Daily summary push time. −1 disables it |
| `testAlert` | epoch | number | 0 | Changing it makes the device send a test alert (`TEST_ALERT`: ntfy push, and an e-mail job when alert e-mails are on) |
| `reprobe` | epoch | number | 0 | Changing it makes the device re-run the read-only probe |
| `alertsRev` | ms | ≤ server time | – | Bumped (`serverTimestamp()`) by the PWA in the same update as every `alerts/*` change; the device then fetches `alerts.json` |

#### `alerts/email`, `alerts/rules/{rid}`, `alertState/{rid}`, `mailq`, `mailer`

These are the e-mail-alert nodes from [R8](../research/r8-email-alerts.md) §3. Field-by-field tables (types, ranges, defaults) are in R8 §3.2–§3.9; the rules that enforce them are summarised in [`firebase/README.md`](../firebase/README.md) §1. The main points:

| Node | Field highlights |
|---|---|
| `alerts/email` | `on` (master switch), `to` (0–3 addresses, ≤ 300 chars; position *i* = bit *i* of a rule's `rc`), `fromName` (1–40 chars), `maxPerDay` (1–40, default 40), `monthStart` (1–28, the billing-cycle start), `builtin.minSev` (0/3/4/5: which built-in alerts are e-mailed, default 4), `builtin.clear`, `summary {on, hour}` (default on, 07:00), `offline {on, min 10–240, clear}` (default on, 30 min) |
| `alerts/rules/{rid}` | `name`, `on`, `metric` ([§9.6](#96-usage-threshold-rules)), `scope` (`inst`/`day`/`month`), `op` (`gt`/`ge`/`lt`/`le`), `value` (canonical units; energy in **Wh**), `mode` (`hold` or `at` + `at:"hhmm"`), `forSec`, `clr`/`clrSec` (recovery), `coolMin` (5–10080, default 60), `repeatMin` (0 or 30–1440), `maxDay` (1–10, default 3), `minCov` (0–100, default 80), `win {from, to, days}`, `sev` (1–5), `email`, `ntfy` (at least one true), `rc` (1–7 recipient mask), `clrMail`, `upd` |
| `alertState/{rid}` | `st` (`off`, `wait`, `idle`, `pend`, `fire`, `clr`, `latch`, `done`, `inv`), `since`, `val`, `last`, `cnt`, `pk` (period key), `err`. A deleted rule writes `null` |
| `mailq/{eventKey}` | `d` (date key of the event), `ts`, `sev`, optional `rc`, retry counter `n` (the mailer gives up at 5) |
| `mailq/test` | `ts` (within ±300 s of server time), optional `rid` (send a "[TEST]" mail in that rule's wording) |
| `mailer` | `hb` (last run), `ver`, `day`, `sent` (recipient-deliveries today), `quota` (`MailApp.getRemainingDailyQuota()`), `off` (last `live.ts` while the logger is offline), `err`, `warn`, `last`, `test` |

### 5.4 Warning and fault codes

- Codes are stored **raw**, together with a **source tag**: `qpiws32`, `qpiws36`, `revo`, `smg`, `powmr` or `lcd`. The numbering differs between these tables. For example, REVO fault 01 is "BUS overvoltage", while LCD fault 01 is "Fan locked when inverter off" (V1 C13).
- One file, [`shared/codes.json`](../shared/codes.json), holds every table:
  - the QPIWS bit meanings;
  - the LCD fault codes 01–09 and 51–59 and warning codes 01, 02, 03, 04, 07, 08, 10, 15, 16, E9 and bP (Savx manual §5/§6);
  - the REVO warning codes 00–07 and 21–26 and fault codes 01–12.
- [`tools/gen_codes.py`](../tools/gen_codes.py) generates `web/src/codes.ts` from that file, and `web/src/alertMetrics.ts` from [`shared/alert-metrics.json`](../shared/alert-metrics.json) (the rule metric catalogue). `--check` fails if either is stale or if the metric list disagrees with the database rules; CI runs it.
- The firmware's `include/codes.h` is hand-written to match, and its metric catalogue lives in `src/agg/RuleTypes.cpp`; the host tests check the metric list against the rules ([`firmware/README.md`](../firmware/README.md) §8 D13). The event types `TEST_ALERT`, `RULE` and `RULE_INVALID` are in `codes.json`.

---

## 6. Sample intervals, aggregation and energy

### 6.1 Sample intervals

| Data | Interval | Where |
|---|---|---|
| Inverter fast poll (`QPIGS` or `QALL`) | 5 s | Device |
| `QMOD` / `QPIWS` | 10 s, alternating | Device |
| `QPIRI` (+ `QFLAG` / `PQSE`) | 10 min, or immediately when QPIGS status bit b6 = 1 | Device |
| Identity (`QPI`, `QID`, `QVFW`, `QVFW2`, `QMN`) | Boot and hourly | Device |
| Local 24 h ring | 5 s samples, about 17,280 × 48 B ≈ 0.8 MB of PSRAM | Device RAM |
| `live` | 10 s (`cfg.liveSec`, 2–300) | Cloud |
| `hist` | 60 s, aligned to the wall-clock minute | LittleFS, then cloud |
| `h15` | 15 min, aligned | Cloud |
| `daily` | Running totals in every minute PATCH; finalized at 00:05 local | Cloud |
| `status` | 60 s | Cloud |
| `cfg` poll | 60 s | Device GET |
| `alerts.json` fetch | When `cfg/alertsRev` changes, at boot, every 6 h | Device GET (≤ about 6 KB) |
| Usage-threshold rules | Every 5 s sample (also offline) | Device (`RuleEngine`) |
| `alertState` | On rule transitions, in the minute PATCH | Cloud |
| Mailer poll | Every 5 min (time trigger) | Apps Script → RTDB |
| Daily summary e-mail | Once, at or after `alerts/email/summary.hour` (default 07:00), for yesterday | Mailer |
| `meta` | Boot and on change | Cloud |
| `diag/raw` | 10 min | Cloud |
| Healthchecks ping | 5 min | hc-ping.com |
| NVS energy checkpoint | 5 min | Device |
| Prune | Daily at 00:05 local | Device → cloud |

### 6.2 On-device aggregation and energy integration

**Per-minute accumulators** (in `aggTask`):
- For each numeric field: sum and count for the average, plus min and max where the schema needs them.
- Mode letters are tallied, and the most frequent one wins.
- Warning bits are OR-ed together.

**Energy** is integrated with the trapezoid rule on consecutive valid samples:

```
E += (P_prev + P_now) / 2 × dt / 3600        [Wh]
```

- `dt` comes from `millis()` deltas and is used **only if 0 < dt ≤ 30 s**. A longer gap counts as missing data and is never interpolated.
- Energy formulas:

| Energy | Integrated from |
|---|---|
| `epv` | `pvw` |
| `eld` | `w` |
| `ech` | `max(0, bw)` |
| `edis` | `max(0, −bw)` |
| `egr` (estimate) | `w + acChgW`, while the grid is present and `md = L` |

- `acChgW = st.b0 ? max(0, bv × bci − (st.b1 ? pvw : 0)) : 0`. In words: when AC charging is active, the charger's power is battery power minus whatever solar is contributing.

**Precision:**
- PI30 battery current has **1 A resolution**, which is about 26 W at 24 V. Battery energy figures are therefore coarse.
- `epv` and `eld` are good to a few percent.
- `egr` is always labelled "est.".

**Source of truth for daily totals:**
- The sum of today's minute records in LittleFS. On boot, today's totals are recomputed from that file.
- NVS keeps a 5-minute checkpoint only as a fast-path cache. This keeps reboots and back-fill consistent.

**Reconciliation:** if the inverter reports its own PV energy (`epvi`, from REVO QALL[13] or MAX `QED`), both values are stored. The PWA shows the difference. The integrated value is never overwritten.

**Unit test:** a synthetic constant 1000 W for 3600 s at 5 s steps must integrate to 1000 Wh ± 0.5 %.

---

## 7. Retention, pruning and quota math

### 7.1 Retention and pruning

Spark has no Cloud Functions (V2 F8), so the device does the pruning.

| Node | Keep | Who prunes and how |
|---|---|---|
| `hist` | `cfg.keepHistDays` (default **400 days**, so you can compare a month with the same month last year) | Device, daily at 00:05 local: `GET hist.json?shallow=true` (about 6 KB), then `DELETE hist/{date}.json` for each date older than the cutoff (usually exactly one) |
| `events` | `cfg.keepEventDays` (default 730) | Device, same job, same date-key pattern |
| `h15`, `daily` | Forever | – |
| `live`, `status`, `meta`, `diag`, `cfg` | Overwritten in place | – |

`.validate` rules do not run on deletes, so `.write` permission is enough for pruning (R6 §2.4).

**Yearly export:** sign in as the owner over REST and download `daily`, `h15` and `hist` (one date per request) into dated JSON files, or use the PWA's CSV exports. The commands are in [06](06-cloud-and-dashboard-guide.md) §12. A full year is about 140 MB of download, roughly 1.4 % of a month's allowance. ⚠️ The `tools/rtdb-export.sh` script that D1 planned does **not** exist yet; 06 §12 is its manual equivalent.

The e-mail nodes need no pruning: `mailq` empties itself (the mailer deletes each job, and jobs older than 12 h, or 24 h for severity ≥ 4, expire), `alerts` holds at most 17 small documents, and `alertState` and `mailer` are overwritten in place.

### 7.2 Verified Spark limits

These were checked on 2026-10-21 (V2 §2.1).

| Product | Spark limit | Source |
|---|---|---|
| RTDB storage | 1 GB | [pricing](https://firebase.google.com/pricing) |
| RTDB download | **10 GB/month** | [pricing](https://firebase.google.com/pricing) |
| RTDB simultaneous connections | 100. REST requests **do not** count | [limits](https://firebase.google.com/docs/database/usage/limits), [billing](https://firebase.google.com/docs/database/usage/billing) |
| What download includes | Every byte: protocol and TLS overhead (about **3.5 KB per new TLS connection**), rule-denied requests, console reads | [billing](https://firebase.google.com/docs/database/usage/billing) |
| Over quota | "your app will be shut off for the remainder of that month" | [FAQ](https://firebase.google.com/support/faq) |
| Hosting | 10 GB stored. Transfer is **360 MB/day** on the pricing page but 10 GB/month in the Hosting doc; the stricter figure is used here | [pricing](https://firebase.google.com/pricing), [hosting quotas](https://firebase.google.com/docs/hosting/usage-quotas-pricing) |
| Auth | 50K MAU; 100 account creations per hour per IP | [auth limits](https://firebase.google.com/docs/auth/limits) |
| Cloud Functions / Cloud Storage | Not available on Spark (Storage removed for Spark from 2026-02-03) | [pricing](https://firebase.google.com/pricing), [storage FAQ](https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024) |

### 7.3 Storage

JSON bytes are doubled to allow for unknown internal overhead.

| Node | Per day | Steady state / growth | Year 1 | Year 10 |
|---|---|---|---|---|
| `hist` (242 B × 1440) | 348 KB | Capped at 400 days ≈ 139 MB | 127 MB | 139 MB |
| `h15` (242 B × 96) | 23 KB | +8.5 MB/year | 8.5 MB | 85 MB |
| `daily` | 0.18 KB | +0.07 MB/year | 0.07 MB | 0.7 MB |
| `events` (≈ 20/day × 133 B) | 2.7 KB | Capped at 730 days ≈ 2 MB | 1 MB | 2 MB |
| **Total × 2 overhead** | | | **≈ 273 MB (27 %)** | **≈ 454 MB (45 %)** |

The margin is at least 2.2× even at year 10. If `keepHistDays` is ever raised above about 1,000, re-check this table.

### 7.4 Download per day

| Item | Nominal | Worst realistic |
|---|---|---|
| `live` PUT: 8,640/day × ~340 B (headers + TLS record) | 2.9 MB | – |
| Minute PATCH: 1,440 × 340 B | 0.5 MB | – |
| `cfg` GET: 1,440 × ~540 B | 0.8 MB | – |
| TLS handshakes | 144/day (reconnect every 10 min) × 3.5 KB = 0.5 MB | A new handshake for every request: 11,820 × 3.5 KB = 41 MB |
| **Device subtotal** | **≈ 4.7 MB** | **≈ 44 MB** |
| PWA `live` listener, ~414 B per 10 s | Open 2 h/day: 0.3 MB | 3 phones × 8 h: 3.6 MB |
| PWA Today page (≤ 1440 × 242 B = 348 KB per open) | 10 opens: 3.5 MB | 90 opens: 31 MB |
| PWA History/Energy (a week of `h15` ≈ 163 KB; a month of `daily` ≈ 6 KB) | 0.8 MB | 3 MB |
| Mailer: 288 runs/day, idle run ≈ 4 KB (3 small requests, each ≈ 3.5 KB TLS handshake) | ≈ 4 MB | ≈ 5 MB |
| Device: `alerts.json` (≤ 6 KB) on each rules change and every 6 h; e-mail jobs ride on the event PATCH | < 0.1 MB | < 0.5 MB |
| **Total** | **≈ 13 MB/day ≈ 0.4 GB/month (4 %)** | **≈ 88 MB/day ≈ 2.6 GB/month (26 %)** |

The mailer figure is R8's estimate (§1.1) [INFERRED]; check it in the RTDB Usage tab during the first week (acceptance A12: at most 5 MB/day more than before).

### 7.5 Runaway protection (mandatory)

A retry loop that opens a new TLS session every second costs about 300 MB/day, which reaches the 10 GB cap in about a month. The firmware therefore enforces these guards:

1. **Back-off:** at least 5 s between retries, growing exponentially up to 5 min.
2. **Daily budget, level 1:** more than 1,500 handshakes or more than 20,000 requests in a day forces `live` to at least 60 s and raises a `QUOTA_GUARD` event (`status.qg = 1`).
3. **Daily budget, level 2:** twice those numbers (3,000 handshakes or 40,000 requests) pauses `live` and slows the `cfg` poll to every 5 min (`status.qg = 2`). This second stop is a firmware addition to D1 ([`firmware/README.md`](../firmware/README.md) §8 D8).
4. **Never loop on a refusal:** a 400, or a 401 that persists after a token refresh, backs off; events are dropped after 3 tries (`status.edrop`). Rule-denied requests are billed too.
5. **Console discipline (yours):** never leave the Firebase console's data viewer open on `hist`. Console reads are billed.

### 7.6 Other limits

| Limit | Usage | Allowance |
|---|---|---|
| Cloudflare Workers static assets | Cold PWA load ≈ 147 KB gzip (measured build: Firebase SDK 69 KB, app 41 KB, uPlot 23 KB, Preact 6 KB, CSS 5 KB), then cached by the service worker | Static-asset requests are free and unmetered; the Worker has no script, so the 100,000/day script cap never applies ([R7](../research/r7-web-hosting.md) §1.1) |
| Firebase Hosting (fallback only, not deployed) | as above | 10 GB stored, 360 MB/day transfer |
| Auth | 3 users (owner, device, mailer) | 50K MAU; 100 sign-ups/hour/IP |
| Simultaneous connections | ≤ 3 dashboards (REST writes from the device and the mailer don't count) | 100 |
| Apps Script (consumer Gmail) | ≤ 40 recipient-deliveries/day (hard cap in the mailer); ≈ 1,050 URL fetches/day; ≈ 5–10 min/day trigger runtime | 100 recipients/day; 20,000 URL fetches/day; 90 min/day triggers. Google may change these "without notice" ([R8](../research/r8-email-alerts.md) §1.1) |
| GitHub Actions (private repo) | ≈ 350 min in a busy month | 2,000 min/month on GitHub Free; blocked (not billed) when used up without a card ([R7](../research/r7-web-hosting.md) §4.7) |
| Firestore / Functions / Storage | Not used | – |

### 7.7 Device request plan (REST)

The base URL is `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app`. Every request carries `?auth=<ID_TOKEN>`. Writes also carry `print=silent`, which makes the server reply 204 with no body.

| When | Method and path | Body |
|---|---|---|
| 10 s | `PUT /devices/inv1/live.json` | `live` |
| 60 s | `PATCH /devices/inv1.json` | `{"hist/<d>/<hhmm>":{…},"daily/<d>":{…},"status":{…}}`, plus `"h15/<d>/<hhmm>"` when a quarter completes and `"alertState/<rid>":{…}` for rules that changed state (≈ 664 B) |
| Back-fill | `PATCH /devices/inv1.json` | Up to 60 records `{"hist/<d>/<hhmm>":{…},…}` (≈ 15 KB) plus the completed `h15` and that day's totals, in one request |
| 60 s | `GET /devices/inv1/cfg.json` | – |
| `alertsRev` changed, boot, 6 h | `GET /devices/inv1/alerts.json` | – |
| Event | `PATCH /devices/inv1.json` | `{"events/<d>/<key>":{…}}`, plus `"mailq/<key>":{"d","ts","sev"[,"rc"]}` when `em = "q"` (D1 used `POST events/<d>.json`) |
| Billing-month gap | `GET /devices/inv1/daily.json?orderBy="$key"&startAt="<first missing>"&endAt="<yesterday>"` | – (≤ 5.7 KB) |
| Boot / change | `PUT /devices/inv1/meta.json` | `meta` |
| 10 min | `PATCH /devices/inv1/diag/raw.json` | `{"QPIGS":"(…","QPIRI":"(…"}` |
| 00:05 | `GET /devices/inv1/hist.json?shallow=true`, then `DELETE /devices/inv1/hist/<old>.json` (same for `events`) | – |
| Auth | `POST https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=…`; `POST https://securetoken.googleapis.com/v1/token?key=…` (the **device key**) | Email and password / refresh token |
| After an event with `em = "q"` (optional) | `GET https://script.google.com/macros/s/<id>/exec?k=<NUDGE_KEY>`, redirect not followed, body not read; ≥ 60 s apart, ≤ 48/day | – |

The mailer's own requests (every 5 min, as the `mailer` user): `GET mailq` → per job `GET events/<d>/<key>` → one `PATCH /devices/inv1.json` with `events/<d>/<key>/em`, `…/emt` and `mailq/<key>: null` → `GET live/ts` → `PATCH mailer` ([R8](../research/r8-email-alerts.md) §3.10).

---

## 8. Security model

The rules file is [`firebase/database.rules.json`](../firebase/database.rules.json). How to render the UIDs and deploy it: [`firebase/README.md`](../firebase/README.md) and [06](06-cloud-and-dashboard-guide.md) §3. The design sources are D1 §4.7 and §5.13, [R7](../research/r7-web-hosting.md) §4.6 and §6, and [R8](../research/r8-email-alerts.md) §1.4 and §3.11.

### 8.1 Identities

| Identity | What it is | Where its secret lives |
|---|---|---|
| **Owner** | A Firebase email/password user that you sign in with on the iPhone PWA | Your memory or password manager; the iPhone app's own storage |
| **Device** | A separate Firebase email/password user that only the ESP32 uses | ESP32 NVS (entered through the WiFiManager portal), together with its refresh token |
| **Mailer** | A third Firebase email/password user that only the Apps Script uses (`mailer@<anything>.invalid`) | The Apps Script's script properties (`FB_EMAIL`, `FB_PASSWORD`); its refresh token in the `RTOK` property |
| **Browser key** | The Firebase web `apiKey`. Identifies the project. **Not a secret** (it ships in the PWA). Restricted to the dashboard's origins (`solar.<acct>.workers.dev/*`, `<project-id>.web.app/*`, `<project-id>.firebaseapp.com/*`) once the first deploy works | GitHub Variable `VITE_FIREBASE_API_KEY`; `web/.env` |
| **Device key** `savx-device` | A second API key with no referrer restriction, limited to Identity Toolkit and Token Service. Not a secret either | ESP32 NVS; the mailer's `FB_API_KEY`; `web/.env.local` for development; `curl` tests |
| **Sender Gmail account** | A dedicated Gmail account that owns the Apps Script and sends the alert e-mails. Nothing else lives in it | Your password manager; 2-Step Verification on |
| `ALLOW_TO` | The mailer's recipient allow-list: an address must be in it **and** in `alerts/email/to` to receive mail. A second factor against a compromised owner login | Script property (sender account) |
| `NUDGE_KEY` (optional) | Shared key for the mailer's web-app URL. It can only trigger an extra poll (rate-limited); it carries no data | Script property; ESP32 NVS `mail/nudge` |
| `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` | Let `deploy-web` replace the code at the dashboard's origin. **Secret**: code there runs inside your signed-in owner session | GitHub **Secrets** only |
| Google credential for `deploy-rules` | Keyless Workload Identity Federation (preferred), or a service-account key `GCP_SA_KEY` that **bypasses the rules** | WIF: nothing stored. Key: GitHub **Secrets** only; rotate yearly |
| Local admin | Password for the ESP32's `/update`, `/api/config`, `/api/probe` and `/api/diag/*` | ESP32 NVS, stored as a SHA-256 hash |
| ntfy topic | **The topic name is the password** ("the topic is essentially a password", V2 I3). Use 24 or more random characters | ESP32 NVS; the ntfy app subscription |
| Telegram bot token, Healthchecks UUID | Optional channel secrets | ESP32 NVS |

### 8.2 Access matrix (rule-enforced)

UIDs are pinned in the rules. **Never** write a rule of the form `auth != null` alone (V2 §3.16).

| Node | Read | Write | Validation highlights |
|---|---|---|---|
| `/` (everything else) | nobody | nobody | Default deny |
| `devices/inv1` (all children) | owner, device | – | – |
| `live`, `daily`, `events`, `alerts`, `mailq`, `mailer` | also **mailer** | – | The mailer cannot read `cfg`, `status`, `meta`, `diag` or `hist` |
| `live` | (inherited) | device | Must have `ts`, `lk`. `ts` is a number > 1700000000 and < now + 10 min. String keys `md`/`st`/`st2`/`wf` are length-limited. Every other key is numeric with a name ≤ 6 characters |
| `status` | (inherited) | device | Key ≤ 8 characters; number or string ≤ 40 |
| `meta` | (inherited) | device | Key ≤ 12 characters; number, boolean or string ≤ 200 |
| `diag/raw/{cmd}` | (inherited) | device | `cmd` ≤ 12 characters; string ≤ 300; no other children under `diag` |
| `hist`, `h15` | (inherited) | device, owner | Date key matches `YYYY-MM-DD`. `hhmm` is valid (`h15`: only 00/15/30/45). `n` required. Only `md` may be a string; other keys numeric, ≤ 4 characters |
| `daily` | (inherited) | device | Date key format; numeric keys ≤ 6 characters |
| `events` | (inherited) | device, owner; **mailer**: only `em` and `emt` of an existing event | Date key; requires `ts`, `type`, `sev` (0–5), `msg`. Typed optional `st`, `ch`, `dur`, `ack`, `rid`, `val`, `thr`, `em` (fixed list), `emt`; **no other keys**. New keys `<ts>-<src>-<o\|c\|r\|i>`; older push IDs stay valid |
| `cfg` | (inherited) | **owner only** | Each key range-checked ([§5.3](#53-field-dictionary)), incl. `alertsRev` ≤ server time; unknown keys rejected |
| `alerts/email`, `alerts/rules/{r01…r16}` | (inherited) | **owner only** | Required fields; metric/scope/comparator/mode consistency; value range by metric; recovery level on the safe side; at least one channel; no CR/LF/TAB in names; ≤ 3 valid addresses; at most 16 rules (key pattern). The PWA validates exactly the same (a parity fuzz agreed on 5,414 cases) |
| `alertState/{rid}` | (inherited) | device | `st` from a fixed list; numeric fields; `err` ≤ 40 chars |
| `mailq/{eventKey}` | (inherited) | device creates; mailer deletes | Key `<10-digit ts>-<src>-<kind>`; `d`, `ts`, `sev` 0–5, optional `rc` 1–7 and `n` 0–20 |
| `mailq/test` | (inherited) | **owner only** | `ts` within ±300 s of server time; optional `rid` |
| `mailer` | (inherited) | **mailer only** | Fixed keys and lengths |

Notes:
- `.validate` rules don't cascade. `$other` matches only unlisted keys. Each key of a multi-path PATCH is checked against its own path (R6 §4).
- **Open sign-up is harmless.** Anyone holding the Web API key can create Auth accounts (100/hour/IP, V2 F12), but the UID pinning makes such accounts useless.
- The device cannot write `cfg` or the rules, the owner cannot overwrite `live`, and the mailer cannot create or edit events. These are checked with `curl` during M5 and M7 (see [07](07-milestones-and-test-plan.md) and the 35 tests in [`firebase/README.md`](../firebase/README.md) §5), and by the emulator suite `tools/rules-test/run.sh` (87 cases).
- **The committed rules keep the placeholders** `OWNER_UID`, `DEVICE_UID`, `MAILER_UID`, so as committed nobody can read or write anything. Only a copy rendered by `tools/render-rules.sh` into the git-ignored `.deploy/` folder is ever deployed. `MAILER_UID=none` works until the mailer exists. CI fails if real UIDs or a bare `auth != null` rule are committed.
- ⚠️ **Deploy order for the e-mail features: rules → mailer → PWA → firmware** ([R8](../research/r8-email-alerts.md) §7). Anything that writes the new nodes or fields before the current rules are live is refused (and refused requests still cost download quota).

### 8.3 Device-side security

- **TLS everywhere, with pinned trust anchors** (`firmware/include/roots.h`). There is no `setInsecure()` anywhere. The chains were measured by V2:

  | Host | Roots embedded (`include/roots.h`, measured 2026-10-21) |
  |---|---|
  | RTDB, identitytoolkit, securetoken, script.google.com (nudge) | GTS Root R1, R3, R4. GTS Root R2 is left out: it is in neither Mozilla's nor Chrome's current root store, and the measured chains end at R1 and R4 |
  | ntfy.sh | ISRG Root X1, X2, Root YR, Root YE |
  | api.telegram.org | Go Daddy Root CA G2 |
  | hc-ping.com | USERTrust ECC and RSA, Sectigo Public Server Authentication Root E46 and R46 |

  mbedTLS date checks are off, so an expired root is **not detected**. Re-check the roots once a year ([08](08-risks-troubleshooting-maintenance.md) §3.2).
- **Tokens:**
  - ID tokens last 1 hour and are refreshed 5 minutes before they expire.
  - The refresh token is persisted in NVS. It dies if the device user's password or email changes, or if the user is disabled or deleted. Recover by holding BOOT for 3 s and re-entering the credentials (V2 F11).
- **Never log a URL that contains `auth=`.** Secrets are write-only in `/api/config` and never echoed back.
- **NVS is not encrypted** (no flash encryption). Physical access to the ESP32 exposes the device credentials. This is an accepted risk, because the rules limit the damage to writing this device's own data. Rotate the device password if the box is ever lost.
- **Read-only inverter link:**
  - Compile-time `INV_ALLOW_WRITES=0`.
  - An exact-match command whitelist in `SerialLink`.
  - A host unit test proving that refused commands write zero bytes. See [02](02-protocol-reference.md) and [05](05-firmware-guide.md).
- **API keys:** the device uses the separate **device key** (sign-in APIs only, no referrer restriction), so the browser key can be restricted to the dashboard's web origins without breaking the ESP32 or the mailer ([R7](../research/r7-web-hosting.md) §6.2; set-up in [06](06-cloud-and-dashboard-guide.md) §2.6 and §5.6). App Check and reCAPTCHA stay off: they would reject the device's plain REST sign-in.
- **No e-mail secret on the device.** The ESP32 never holds a mailbox password: it only writes jobs to RTDB. The optional nudge URL is the only mailer-related value in NVS, and it cannot choose content or recipients.

### 8.4 Web, CI and mailer security

- **Dashboard headers** (`web/public/_headers`, mirrored in `firebase.json` for the fallback): a Content-Security-Policy that allows only the app itself, Identity Toolkit, Secure Token and `*.asia-southeast1.firebasedatabase.app` (incl. `wss`); HSTS; `nosniff`; framing denied; `X-Robots-Tag: noindex, nofollow`; `Referrer-Policy: strict-origin-when-cross-origin` (needed for the browser key's referrer restriction).
- **The origin is permanent.** `solar.<acct>.workers.dev` owns the Home Screen icon, the sign-in, the service worker and the history cache. Renaming the Worker or the account subdomain, adding a custom domain or switching hosts means re-installing the app on every phone ([R7](../research/r7-web-hosting.md) §2).
- **PR previews are off**: Cloudflare preview URLs are public, and protecting them needs a card.
- **Supply chain:** GitHub Actions pinned by commit SHA; CLIs run as `npx <cli>@<exact version>`; `npm ci` from the lockfile; Dependabot proposes updates monthly and you review them.
- **Mailer:** least-privilege rules for the `mailer` user (above); recipients come only from `alerts/email/to` ∩ `ALLOW_TO`, never from the device; a de-dup ledger in Script Properties is saved before the RTDB write, and LockService serialises runs, so a retried or concurrent run never mails twice ([R8](../research/r8-email-alerts.md) §5.4).

---

## 9. Alerts design

The sources are D1 §7 (built-in alerts and push channels) and [R8](../research/r8-email-alerts.md) (e-mail and usage-threshold rules). Built-in thresholds come from the Savx manual and can be tuned through `cfg`; usage-threshold rules are yours, set in the PWA's **Alerts** page.

### 9.1 Channels

| Channel | Role | How | Limits and caveats |
|---|---|---|---|
| **ntfy.sh** (free iOS app) | Primary instant push | JSON publish: `POST https://ntfy.sh/` with `topic`, `title`, `message`, `priority` (1–5), `tags`, `click` (the PWA URL). D1 used headers; JSON keeps `°C` and Bangla rule names safe. Delivery path: ntfy.sh → Firebase → APNs → iPhone | 250 messages/day, bursts of 60 then 1 per 5 s, 4,096 B per message. Bangladeshi CGNAT may share the per-IP quota (V2 §3.5). The topic name is the password |
| **Telegram bot** | Secondary, severity ≥ 4 | `POST https://api.telegram.org/bot<token>/sendMessage` with `chat_id` and `text` | ≤ 1 msg/s per chat. Telegram has been restricted in Bangladesh before (V2 §3.4) |
| **Healthchecks.io** (Hobbyist, free, 20 checks) | Dead-man's switch: "logger or internet is down" | `GET https://hc-ping.com/<uuid>` every 5 min; `/fail` while the link is armed and running but no valid inverter reply came for 5 min. Check period 5 min, grace 15–30 min. E-mails you when the pings stop | Do not ping one check more than 5 times per minute |
| **E-mail** (Apps Script mailer) | Usage-threshold rules, built-in alerts of severity ≥ `builtin.minSev` (default 4), `TEST_ALERT`, the daily summary, "logger offline" | The device queues a job in `mailq`; the mailer sends it within 0–5 min ([§9.5](#95-e-mail-alerts-the-apps-script-mailer)) | 40 recipient-deliveries a day (Google allows 100). Needs no app, so it also works where ntfy cannot be installed |
| RTDB `events` | Audit trail in the PWA (**Alerts → History**) | Every open and clear is written, with its e-mail status chip | – |

**App availability:** Bangladesh is not an App Store storefront (V2 I8). ntfy (iOS 15+) and Telegram can only be installed with an Apple ID from a storefront such as US or IN. The Healthchecks e-mail and the mailer's e-mails need no app (the Gmail app with notifications on is recommended).

### 9.2 Triggers

| Type | Sev | Trigger (hold time) | Clear (hold time) | Notes |
|---|---|---|---|---|
| `INVERTER_FAULT` | 5 | QMOD = `F`, any fault-class QPIWS bit, or `fc ≠ 0`, on **2 consecutive polls** | Condition false for 60 s | The message includes the decoded fault, using LCD wording where the table is known |
| `BATT_CRITICAL` | 5 | SOC ≤ 20 %, or `bv` ≤ 21.5 V, or QPIWS a12 (battery low alarm) / a14 (under shutdown), for 30 s | SOC ≥ 25 % and `bv` ≥ 22.5 V for 5 min | Manual: low-DC warning 22.0/21.0 V, cut-off 20.5/20.0 V (24 V model) |
| `BATT_LOW` | 4 | SOC ≤ `socLow` (30) or `bv` ≤ `bvLow` (22.5) while discharging, for 60 s | SOC ≥ socLow + 5 and `bv` ≥ bvLow + 0.5 for 5 min | Lithium program 29 default cut-off is SOC 15 %; program 12 back-to-utility is 40 % |
| `BATT_HIGH` | 4 | QPIWS a11, or `bv` ≥ `bvHigh` (30.0) for 30 s | `bv` < bvHigh − 0.5 for 5 min | The manual's high-DC cut-off is 33.0 V |
| `GRID_LOSS` / `GRID_RESTORED` | 4 / 3 | Grid absent for 10 s: QPIWS a5 (line fail), or `gv` < 90 V (APL range), or `gv` < 170 V when the QPIRI input range is UPS | Grid present for 30 s. The restore message includes the outage duration | ⚠️ **Do not use QMOD = B alone.** With SBU priority (program 01) the unit runs on battery while the grid is present |
| `GRID_ABNORMAL` | 2 | 90 ≤ `gv` < 180 V, or `gv` > 270 V, for 5 min | Normal for 5 min | Manual: low return 180 V, high return 270 ± 7 V |
| `OVERLOAD` | 4 | QPIWS a16, or `ld` ≥ `loadHigh` (90 %) for 30 s | `ld` < 80 % for 60 s | Manual: trips after 10 s at 110–130 % |
| `OVER_TEMP` | 4 | QPIWS a9 (over-temperature) or a10 (fan locked), or `tmp` ≥ `tempHigh` (75) when `tmp` is plausible | Clear for 5 min | – |
| `PV_OVERVOLT` | 4 | QPIWS a26, or `pvv` ≥ 480 V for 10 s | `pvv` < 470 V for 5 min | Manual max Voc 500 V |
| `PV_LOW_DAY` | 2 | 10:00–15:00 local, `pvw` < 50 W for 60 min while SOC < 95 % | Next day | Once per day |
| `LINK_LOST` | 3 | No valid inverter reply for 60 s | First valid reply | Points to the wiring or the probe |
| `LOGGER_REBOOT` | 2 | Reset reason ≠ POWERON | – | Info |
| `QUOTA_GUARD`, `DATA_DROPPED`, `BACKLOG_STUCK` (queue > 6 h old while WiFi is up) | 2 | As named | – | Operations |
| `DAILY_SUMMARY` | 1 | At `summaryHour` (21:00) | – | ntfy **push** only: PV, load and grid kWh. −1 disables it. Never e-mailed; the mailer sends its own summary of yesterday at 07:00 |
| `TEST_ALERT` | 3 | `cfg.testAlert` changed (PWA **Send test alert**) or `test-alert` on the console | – | Ignores quiet hours. Also e-mailed whenever alert e-mails are on, which tests the whole chain |
| `RULE` | per rule (1–5) | A usage-threshold rule opens, repeats or recovers ([§9.6](#96-usage-threshold-rules)) | per rule | Carries `rid`, `val`, `thr` |
| `RULE_INVALID` | 2 | The firmware cannot use a rule (unknown metric or mode) | – | Once per `alertsRev`; the rule's state becomes `inv` |
| *Logger offline* | – | Healthchecks missed pings; the mailer sees `live.ts` older than `offline.min` | – | E-mail from healthchecks.io **and** from the mailer (one "offline" + one "back online", at most 4 a day). The device cannot send this itself |

### 9.3 Debounce, repetition and rate limits

- **Per-rule state machine:**

  ```
  IDLE --condition true--> PENDING --held for trigger time--> ACTIVE  (notify "open")
  ACTIVE --condition false--> CLEARING --held for clear time--> IDLE  (notify "clear" with dur)
  PENDING/CLEARING fall back if the condition flips before the hold time.
  ```

- **Re-notify** only for severity ≥ 4 while the alert stays active, every `notifyRepeatMin` (30 min).
- **Quiet hours** (`quietFrom`–`quietTo`, default 22:00–07:00 local) suppress the **push** for severity ≤ 3. The event is still written to `events`.
- **Global cap:** 40 pushes per day, well under ntfy's 250. Above the cap, only severity 5 is pushed.
- **Offline behaviour:** alerts wait in a 24-entry RAM queue. On reconnect they are sent with their original time in the text, e.g. "(14:02, delayed 37 min)". If the queue fills up, the oldest severity ≤ 2 entries are dropped first.
- **Routing:**

  | Severity | ntfy priority | Telegram | `events` |
  |---|---|---|---|
  | 5 | 5 | yes | yes |
  | 4 | 4 | yes | yes |
  | 3 | 3 | no | yes |
  | ≤ 2 | 2 | no | yes |

### 9.4 Healthchecks dead-man's switch

1. Create a free Hobbyist account at healthchecks.io and add one check.
2. Set the period to **5 minutes** and the grace to **15–30 minutes**.
3. Enter the check's UUID in the logger's portal or `/api/config`.
4. The device pings `https://hc-ping.com/<uuid>` every 5 minutes from `uplinkTask`.
5. If the ESP loses power, the router dies or the ISP is down for longer than period + grace, Healthchecks e-mails you. It is fully independent of Google: it keeps working when the mailer or the sender account is down. The mailer's own "logger offline" e-mail is the second, Google-side path.

### 9.5 E-mail alerts: the Apps Script mailer

Firebase Spark has no Cloud Functions and the project has no server, so e-mail is sent by a **Google Apps Script** project owned by a **dedicated Gmail sender account** ([R8](../research/r8-email-alerts.md) E1). The code is [`tools/apps-script/`](../tools/apps-script/) (`Code.gs`, `Lib.gs`, `appsscript.json`; 44 Node tests). Set-up: [06](06-cloud-and-dashboard-guide.md) §9 and [`tools/apps-script/README.md`](../tools/apps-script/README.md).

```
 ESP32 (uplinkTask)                      Firebase RTDB (Spark)                    Apps Script (sender Gmail)
   RuleEngine / AlertEngine
     event {…, em:"q"} --one PATCH--->  events/<d>/<key> {…, em:"q"}
                                        mailq/<key> {d, ts, sev[, rc]}  <--GET every 5 min (null when idle)
                                                                         --GET event, alerts/email, live
                                                                           MailApp.sendEmail --> inboxes
                                        events/<d>/<key>/em:"sent", emt  <--PATCH (job deleted in the same request)
                                        mailer {hb, sent, quota, err…}   <--PATCH heartbeat every run
 PWA "Send test e-mail" --set-------->  mailq/test {ts}                  --next run--> test mail --> mailer/test
 (optional) ESP32 --GET nudge (no data)----------------------------------> doGet: poll now (≤ 1 per 20 s)
```

| Concern | How it works |
|---|---|
| Sign-in | As the third Firebase user `mailer` (email/password → ID token → `?auth=`), with the **device key**. The rules grant it only what it needs (§8.2). `ScriptApp.getOAuthToken()` and database secrets were rejected: both bypass the rules |
| Manifest scopes | `script.send_mail` (MailApp), `script.external_request` (UrlFetchApp), `script.scriptapp` (the trigger). Time zone Asia/Dhaka |
| Recipients | `alerts/email/to` ∩ the `ALLOW_TO` script property; a rule's `rc` mask picks a subset. Never from the device or a request |
| Caps | ≤ 40 recipient-deliveries per local day (`maxPerDay`, and `MAX_PER_DAY` can only lower it), with the last 5 reserved for severity ≥ 4 (`min(5, cap/4)` for small caps); one "limit reached" notice a day; Google quota < 10 → severity ≥ 4 only; tests ≥ 60 s apart and ≤ 10 a day; offline/online mails ≤ 4 a day |
| De-dup and concurrency | A `SENT` ledger in Script Properties, saved **before** the RTDB write; LockService. A re-queued key restores `em:"sent"` without a second mail |
| Digest and expiry | Jobs for the same recipients found in one run become one e-mail ("N alerts: …"). Jobs older than 12 h (24 h for severity ≥ 4) become `exp` |
| Failures | A MailApp error increments the job's `n`; at 5 it becomes `fail`. RTDB or auth errors re-throw, so Google e-mails the sender account a failure summary. The heartbeat is always attempted |
| Content | HTML + plain text: subject `[Solar] <msg>` (`ALERT:` / `CRITICAL:` for severity 4 / 5), local time, "Value / limit" for rule alerts, a "Right now" snapshot from `live`, a "Delayed …" line for late events, an **Open dashboard** link (`APP_URL`), and a footer saying why the recipient gets it and how to stop. Grid figures say "est." |
| Daily summary | At or after `summary.hour` (default 07:00), for **yesterday**: PV (logger and inverter), load, grid (est.), battery charged/discharged, outage minutes, the day before, the billing cycle so far, coverage, peaks, alerts |
| Logger offline | `live.ts` older than `offline.min` (default 30 min) → one "Logger offline" e-mail, then one "back online" e-mail |
| Latency | 0–5 min plus trigger jitter; about 10–30 s with the optional nudge (v1.1, built: `src/net/MailNudge.cpp`, ≥ 60 s apart, ≤ 48 a day, redirect never followed, body never read) |
| Fallback | None in v1. ntfy and Healthchecks stay independent; Gmail SMTP from the ESP32 is an opt-in v2 that is **not built** (it would share the same Google account and put a mailbox password in unencrypted NVS) |

**Budget** ([R8](../research/r8-email-alerts.md) §1.1, [INFERRED], to be measured in week 1): about 1,050 of 20,000 URL fetches a day (5 %), 5–10 of 90 trigger-minutes a day, about 4 MB/day of RTDB download, and at most 40 of Google's 100 recipients a day. Google says these Apps Script quotas may change "without notice"; the mailer therefore publishes `mailer/quota`.

### 9.6 Usage-threshold rules

Up to **16 rules** (`alerts/rules/r01`…`r16`) are written by the owner from the PWA (**Alerts → Rules**, with 9 presets) and evaluated **on the ESP32** by `RuleEngine` in `aggTask`, on every 5 s sample, also while the internet is down. The device keeps the last rules in `/cfg/alerts.json` and loads them before Wi-Fi comes up. Why on the device: it has 5 s data, hold and hysteresis semantics, and state that survives reboots; a 5-minute poller would only see snapshots ([R8](../research/r8-email-alerts.md) §2).

**Metric catalogue** (`shared/alert-metrics.json`, catalogue version `acat` = 1):

| Scope | Metrics (id: meaning, unit) |
|---|---|
| `inst` (instantaneous) | `w` load power W; `pvw` solar power W; `gw` grid import power (est.) W; `bdw` battery discharge power W; `bw` battery net power W (+ = charging); `soc` %; `ld` load % of rating; `bv` battery V; `gv` grid V; `ov` output V; `pvv` solar V; `bdi` battery discharge A; `tmp` inverter °C; `gf` grid Hz; `onbat` on battery (0/1); `nogrid` grid absent (0/1) |
| `day` or `month` (energy totals) | `eld` load; `egr` grid (est.); `epv` solar; `edis` battery discharged; `ech` battery charged (all stored in Wh, typed in kWh); `gout` grid outage minutes |

`egr` and `gw` count only loads on the inverter's AC OUT plus AC charging, so they are **not the utility meter**; they say "est." everywhere. `month` means the billing cycle that starts on `alerts/email/monthStart` (1–28), summed on the device from finished days (`MonthToDate`, NVS `energy/mtd`) plus today; missing days are refilled from flash or with one `daily` range GET.

**Rule semantics** (per rule; [R8](../research/r8-email-alerts.md) §4.4):

```
   OFF --on & loaded--> IDLE --in window & cond(v)--> PEND --held >= forSec--> FIRE --(day/month)--> LATCH
                         ^  <-- !cond / left window / no valid value 30 s --'     |  ^                 |
                         |                                              clr(v)    v  | !clr(v)          | new period
                         '---------------------- held >= clrSec ------------------ CLR                  v
                                                                                                       IDLE
   mode "at":  IDLE --(local hh:mm >= at, once per day)--> check: cond -> OPEN -> LATCH ; else / low coverage -> DONE
```

| Rule | Meaning |
|---|---|
| Condition | `cond(v)` is `v > value` (`gt`), `≥`, `<` or `≤`; booleans compare equal |
| Hold (`mode: hold`) | The condition must hold for `forSec` (`inst` default 60 s; energy 0) before the rule fires |
| Check once (`mode: at`) | Checked once a day at `at` (local). Required for `day` rules with `lt`/`le` ("PV today < 3 kWh by 17:00"), because energy only grows during the day. Needs data coverage ≥ `minCov` (default 80 %), else an info event with `em:"skip"`. A late check (device was down) is allowed until 23:59 and says "checked late" |
| Energy rules | `day` and `month` rules **latch** after firing: at most one open per day or billing cycle, no clear. `month` allows only `gt`/`ge` with `hold`. At midnight a rollover guard stops yesterday's total re-firing a fresh rule |
| Recovery | `clr` and `clrSec` (default 60 s); without `clr`, the default hysteresis of the metric applies (e.g. max(50 W, 5 %) for power, 2 percentage points for SOC, 0.3 V for battery V). A rule never clears on missing data |
| Active hours | `win {from, to, days}`; `from > to` crosses midnight; the `days` mask (bit 0 = Sunday) applies to the day the window starts |
| Gates (device, in order) | e-mail on for the rule and globally → cooldown `coolMin` (else `cool`, plus one deferred repeat) → the rule's `maxDay` (else `rcap`) → the device's 40 jobs a day with 5 reserved for severity ≥ 4 (else `cap`). Recovery mails (`clrMail`, only if the open was e-mailed) skip the cooldown and `maxDay` gates, not the job cap. At most 20 events per rule per day |
| Repeats | `repeatMin` > 0 re-mails "still …" while firing, spaced by max(`repeatMin`, `coolMin`) |
| Push | If `ntfy` is set, the d1 routing applies (quiet hours, 40 pushes a day); severity ≥ 4 also goes to Telegram |
| Reboots | State is saved on transitions (NVS `rules/rsNN`); a latched rule stays latched and a firing rule is restored without a new open mail. So "daily grid > 5 kWh" cannot e-mail twice in a day, even through a boot loop |
| Edits | Cosmetic edits (name, channels, recipients, severity, cooldown, `maxDay`) keep the state; a threshold or metric change resets it |

**Event text** (≤ 200 chars; the mailer uses it as the subject): open `<name>: now <val> (limit <thr><scope>)`, repeat `<name>: still <val> for <held>`, clear `<name>: back to <val> (clear level <clr>)`, check `<name>: <val> at <hh:mm> (limit <thr>)`. Example: `Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)`.

**Built-in alerts by e-mail:** `AlertEngine` flags an event `em:"q"` (all recipients) when alert e-mails are on and it is an open of severity ≥ `builtin.minSev`, a clear of an e-mailed open (if `builtin.clear`), or a `TEST_ALERT`, and the device's job cap allows it.

**Change pickup:** the PWA writes the rule and `cfg/alertsRev` in one update; the device's 60 s `cfg` poll sees the new revision, fetches `alerts.json`, applies it and reports `status.arv`. The PWA then shows "✓ Logger has your latest rules". A broken `alerts.json` keeps the previous set (`status.ruleErr`).

---

## 10. Local LAN UI

The ESP32 serves `http://solar.local` (mDNS) on the home WiFi using the core synchronous `WebServer`. It works without internet. The HTTPS PWA cannot fetch these `http://` endpoints because of mixed-content rules; it only links to them.

| Route | Auth | Returns |
|---|---|---|
| `GET /` | – | Gzipped single page: live tiles, 24 h chart (uPlot inlined), raw replies, diagnostics, settings links |
| `GET /api/live` | – | Latest `Sample` JSON (the same keys as `live`) plus `age_ms` |
| `GET /api/ring?min=1440&step=60` | – | Down-sampled PSRAM ring as arrays `{t:[],w:[],pvw:[],bv:[],soc:[]}` |
| `GET /api/hist?date=YYYY-MM-DD` (alias `/api/minutes`) | – | That day's minute records from LittleFS |
| `GET /api/days` | – | Stored days, record counts, upload cursors |
| `GET /api/status` | – | Link profile, adapter, dialect, supported commands, poll success %, CRC errors, RSSI, heap/PSRAM, queue depth, last cloud OK, reset reason; the uplink (`upState`, `upErr`, `hs`, `rq`, `qg`, `edrop`, channel states) and the rules (`rules{…}`) |
| `GET /api/identity` | – | `meta` |
| `GET /api/raw` | – | Last raw reply per command (ASCII and hex) |
| `GET /api/diag/pins` | admin | Module RO/DI self-ID result; idle-level test results |
| `POST /api/diag/idle?pin=1\|2[&ref=3v3]` | admin | Runs the 10 s idle-level test, or with `ref=3v3` the upper check ([03](03-hardware-and-wiring.md) §8.4–8.4b) |
| `GET\|POST /api/diag/selftest`, `POST /api/diag/loopback?bench=1` | admin | Module self-ID; bench UART loopback |
| `POST /api/link/arm`, `POST /api/link/reset` | admin | Arm the link (after the idle test, upper check and pre-arm check) / disarm |
| `POST /api/probe` | admin | Re-runs the read-only probe |
| `POST /api/capture` | admin | Raw capture on/off |
| `GET/POST /api/config` | admin | NVS settings (thresholds, ntfy topic, Healthchecks URL, dashboard URL, mailer nudge URL). Secrets are write-only and never echoed |
| `POST /api/reboot`, `POST /api/safemode/exit` | admin | Reboot / leave safe mode |
| `GET /update` | admin (basic auth) | ElegantOTA firmware upload. The iPhone can upload a `.bin` from the Files app. The only firmware-upload path: while the setup portal runs, WiFiManager's own `/update` and `/u` routes answer 403 |

The exact parameters are in [05](05-firmware-guide.md) §6.4, which follows the code.

**Status LED** (onboard RGB on GPIO48, or GPIO38 on Espressif v1.1 boards). On clones it may be unwired, so `/api/status` is the authoritative indicator.

| Colour / pattern | Meaning |
|---|---|
| Blue, slow blink | Booting, or provisioning portal open |
| Magenta blink | Probing the inverter link |
| Yellow | WiFi down; logging locally |
| Short green blink every 5 s | Valid inverter reply |
| Cyan pulse | Back-fill in progress |
| Red blink | Inverter link lost |
| Solid red | Safe mode |

---

## 11. Why Firebase RTDB

The requirements: the ESP32 writes directly with no server of our own, multi-year history, viewing on an iPhone, and zero cost. The free tiers below were verified on 2026-10-21 (V2 §2.1, §2.6) unless marked otherwise.

| Option | Free-tier limits | ESP32 writes directly | Multi-year history free | iPhone viewing | Verdict | Sources |
|---|---|---|---|---|---|---|
| **Firebase RTDB (Spark)** | 1 GB stored; 10 GB/month download; 100 connections (REST excluded); **no per-operation caps**; shut off for the rest of the month if exceeded | Yes (REST) | **Yes**: this design uses ≈ 27 % of storage in year 1 and ≈ 45 % by year 10 | Own PWA, live via WebSocket | **Chosen** | [pricing](https://firebase.google.com/pricing), [limits](https://firebase.google.com/docs/database/usage/limits), [FAQ](https://firebase.google.com/support/faq) |
| Firestore (Spark) | 1 GiB; **50K reads, 20K writes, 20K deletes per day**; 10 GiB/month egress; every field auto-indexed (index entries count as storage) | Yes (verbose typed REST) | Only with index exemptions | Own PWA; **1 read per listener update** | Poor for live data: a 10 s `live` alone is 8,640 writes/day | [quotas](https://firebase.google.com/docs/firestore/quotas), [pricing](https://firebase.google.com/docs/firestore/pricing) |
| Supabase Free | 500 MB DB; 5 GB egress; 50K MAU; 2 projects; **paused after 7 days of low activity** (restorable for up to 1 year) | Yes (PostgREST + RLS) | About 3–5 years at 1 min [INFERRED] | Own PWA | A strong #2 for SQL, but a week-long outage risks a pause | [pricing](https://supabase.com/pricing), [pausing](https://supabase.com/docs/guides/platform/free-project-pausing) |
| ThingSpeak Free | 3M messages/year; 15 s minimum interval; 4 channels × 8 fields; 10M-message storage cap; **non-commercial only** | Yes (HTTP API key) | Yes (4 channels at 1/min ≈ 2.1M msg/year) | Web charts in Safari; clunky | **Fallback F-C** | [licence FAQ](https://thingspeak.mathworks.com/pages/license_faq), [channels](https://www.mathworks.com/help/thingspeak/channel-settings.html) |
| Cloudflare Workers + D1 Free | Workers 100K requests/day, 10 ms CPU, 5 cron triggers; D1 500 MB/DB, 5 GB total, 5M rows read/day, **100K rows written/day**; queries fail over the limit | Via your own Worker (~100 lines of JS) | Yes (≈ 50 MB/year at 1-min rows) [INFERRED] | Own PWA on Workers static assets (**already the PWA host**, [§12](#12-web-hosting-and-cicd)) | **Upgrade path** if server logic or Web Push is wanted | [Workers limits](https://developers.cloudflare.com/workers/platform/limits/), [D1 limits](https://developers.cloudflare.com/d1/platform/limits/), [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/) |
| Blynk Free | 5 devices; 1 user; 100K messages/month; **1-week retention** | Yes (library) | **No** | Native app (nice) | Live only | [pricing](https://blynk.io/pricing) |
| InfluxDB Cloud Serverless Free | **30-day retention**; writes 5 MB / 5 min | Yes (line protocol) | **No** | Web UI | Rejected (not re-verified by V2) | [pricing](https://www.influxdata.com/influxdb-cloud-pricing/) |
| Grafana Cloud Free | 10k series; **14-day retention** | Indirectly [INFERRED] | **No** | Grafana web | Rejected (not re-verified by V2) | [pricing](https://grafana.com/pricing/) |
| Adafruit IO Free | 30 data points/min; 10 feeds; **30 days** storage | Yes | **No** | Web dashboards | Rejected: about 30 fields at 1/min hits the cap (not re-verified by V2) | [IO+ guide](https://learn.adafruit.com/welcome-to-adafruit-io/io-plus) |
| Home Assistant (self-hosted) | Free software, but **needs an always-on host** | Via ESPHome/MQTT | Yes (local) | Excellent native app and push | Rejected: no host available, and buying one isn't allowed | [modbus integration](https://www.home-assistant.io/integrations/modbus/), [Nabu Casa](https://www.nabucasa.com/pricing/) |

**RTDB vs Firestore** for this particular time series (R6 §2.1):

| Criterion | RTDB (Spark) | Firestore (Spark) |
|---|---|---|
| Live value every 5–10 s | No operation quota; only bandwidth | 8,640–17,280 writes/day against a 20K/day cap |
| Dashboard listener | Billed by bytes | 1 read per update; a dashboard open all day at 10 s uses 8,640 of 50K reads/day |
| Storage efficiency | Compact JSON, no automatic indexes | Every field auto-indexed ascending and descending |
| ESP32 REST | `PUT`/`PATCH`/`POST`/`DELETE` on `.json`; multi-path `PATCH`; `print=silent` | Verbose typed JSON (`{"doubleValue":…}`); `updateMask` patches |
| SDK weight in the PWA (gzip, measured) | database 49.0 KB | firestore 186.5 KB |

**Conclusion:** RTDB on Spark fits this workload best. It has no operation caps, REST writes don't use connection slots, multi-path PATCH makes one minute's writes a single request, and the design stays under 5 % of the download allowance and under half the storage even after 10 years.

The risks of depending on Firebase are a policy change (Spark lost Cloud Storage in 2026) and Google deleting an inactive account. The mitigations are a portable JSON model, a yearly export, and ready fallbacks (ThingSpeak, Cloudflare D1). See [08](08-risks-troubleshooting-maintenance.md).

---

## 12. Web hosting and CI/CD

The decision record is [R7](../research/r7-web-hosting.md); set-up steps are in [06](06-cloud-and-dashboard-guide.md) §4–§5.

### 12.1 Hosting

| | Choice |
|---|---|
| **Primary** | **Cloudflare Workers static assets**: an assets-only Worker `savx-logger` ([`web/wrangler.jsonc`](../web/wrangler.jsonc): `./dist`, SPA fallback, `workers_dev: true`, `preview_urls: false`) at **`https://solar.<acct>.workers.dev`**. Static-asset requests are free and unmetered; it is served from Cloudflare's Dhaka and Chittagong PoPs (49–58 ms TTFB measured from Dhaka) |
| **Fallback** | **Firebase Hosting** on Spark (`firebase.json`, same headers, no `database` block), `https://<project-id>.web.app`. Deployed by hand only, if Cloudflare is unusable for days. It is a new origin, so every phone re-installs the app |
| **Not chosen** | Vercel Hobby (close second: Mumbai routing at 177–196 ms, a 30-day lockout at limits, "non-commercial personal use only", previews on the shared `*.vercel.app`); Netlify Free (credits, ≈ 19 production deploys a month); GitHub Pages (needs a public repo, no custom headers); Cloudflare Pages (works, but Cloudflare now recommends Workers for new projects) |
| **Caching** | `/assets/*` immutable for a year; `/`, `/index.html`, `/sw.js` and the manifest `no-cache`. The hand-written service worker precaches the shell (never `_headers`, dotfiles or `/index.html`) and shows "A new version of the app is ready → Reload" after a deploy |
| **Short outages** | Need no action: installed apps keep running from the service-worker cache with live Firebase data |

### 12.2 Pipeline

```
 Mac --git push--> GitHub (private repo, personal account; 2FA)
                     |
     +---------------+-------------------------+-------------------------------+
     v               v                         v                               v
  ci.yml          deploy-web.yml            deploy-rules.yml               (manual, Mac: npx only)
  checks          gate: VITE_* vars,        render UIDs -> .deploy/         wrangler deploy / rollback,
  (codes, rules   codes, typecheck,         Google auth (WIF, or            firebase-tools deploy,
  lint) -> web    build, dist check         GCP_SA_KEY fallback)            fallback hosting
  -> firmware     npx wrangler@4.146.0      firebase deploy --only
  -> mailer       deploy (main only)        database (PR: --dry-run)
                     |                         |
                     v                         v
     Cloudflare: solar.<acct>.workers.dev    Firebase RTDB rules (asia-southeast1)

 Not in CI: firmware flashing (USB/OTA by you), the Apps Script mailer (pasted by hand),
            the RTDB emulator suite (needs Java; run it on the Mac).
```

| Workflow | Trigger | Does | Credentials |
|---|---|---|---|
| [`ci.yml`](../.github/workflows/ci.yml) | Every PR and push to `main`, except docs-only changes; manual | `checks`: change detection, `gen_codes.py --check`, rules lint (valid JSON, placeholders kept, no open rule, no `database` block in `firebase.json`, test render). Then `web` (`npm ci`, typecheck, build, dist checks) `firmware` (PlatformIO 6.2.0, `pio run -e savx`, `pio test -e native`) and `mailer` (Node 24, `npm run check`, `npm test` in `tools/apps-script`), each only if its area changed | none |
| [`deploy-web.yml`](../.github/workflows/deploy-web.yml) | Push to `main` touching `web/`, `shared/` or `tools/gen_codes.py`; manual | Repeats its own gate (the six `VITE_*` variables, asia-southeast1 URL check, codes, typecheck, build, dist check), then `npx wrangler@4.146.0 deploy --tag <sha>`; PR previews only if the `CF_PREVIEWS` variable is `true` (off) | `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` (Secrets) |
| [`deploy-rules.yml`](../.github/workflows/deploy-rules.yml) | Rules or render-script changes; manual | `tools/render-rules.sh` with the Variables `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`, `FIREBASE_MAILER_UID` (`none` until the mailer exists), then `firebase-tools@15.32.1 deploy --only database` (`--dry-run` on PRs) | Workload Identity Federation (no stored key), or `GCP_SA_KEY` |
| [`dependabot.yml`](../.github/dependabot.yml) | Monthly | Update PRs for the pinned actions and the `web/` npm packages (Vite majors excluded: the Mac has Node 20.17) | – |

Ground rules: actions pinned by commit SHA (all Node 24); CLIs pinned to exact versions at least two weeks old; no GitHub `environment:` (Free private repos have none) and no branch protection (Free private repos have none), so **each deploy repeats its own gate**; about 350 of the free 2,000 Actions minutes a month. Everything that is not a secret is a repository **Variable** (the `VITE_*` values ship in the PWA anyway).

### 12.3 Deploy order and rollback

- **Order for the e-mail features:** database rules → mailer → PWA → firmware (§8.2). Rules changes go in their own PR and merge first, and stay backward-compatible (expand → migrate → contract).
- **Web rollback:** `npx wrangler@4.146.0 rollback <VERSION_ID>` from the Mac (Node 24 via nvm) or the Cloudflare dashboard, then `git revert` on `main` ([06](06-cloud-and-dashboard-guide.md) §5.7).
- **Rules rollback:** `git revert` the rules commit (`deploy-rules` redeploys), or render the last good file and deploy from the Mac.
- **Firmware** is never deployed by CI; it goes over USB or OTA ([05](05-firmware-guide.md) §12).
