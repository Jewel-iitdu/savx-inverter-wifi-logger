# R8: E-mail usage-threshold alerts — decision record

Date: 2026-10-21 (Asia/Dhaka). Status: **DECIDED** (Decide phase). This file supersedes [r8a](r8a-email-transports.md) and [r8b](r8b-email-rules-ux.md) wherever they differ. Every correction in [r8c](r8c-email-verification.md) is applied.

Inputs read: [d1](d1-architecture.md) §4 and §7, [v2](v2-platform-verification.md), r8a/r8b/r8c, and [user-constraints](user-constraints.md). I also read the current code before designing against it:
- firmware `src/agg/AlertEngine.*`, `src/net/Notify.h`, `src/net/Uplink.*` (still a stub), `src/store/Settings.*`, `src/net/WifiProv.*`, `src/web/LocalWeb.h`, `src/app/{App.h,Tasks.cpp}`, `include/config.h`, `platformio.ini`;
- web `src/data/{types,source,firebaseSource}.ts`, `src/pages/{Settings,Events}.tsx`, `src/app.tsx`;
- [`firebase/database.rules.json`](../firebase/database.rules.json) and [`shared/codes.json`](../shared/codes.json).

Tags: **[CONFIRMED-primary]** = read on an official page or in source today, or verified by my own test run today. **[SECONDARY]** = a non-official source. **[INFERRED]** = my design reasoning. **[TESTED]** = run today in the RTDB emulator or the mock harness (§11).

> ⚠️ **Nothing in this feature touches the inverter link.** Alerts read values the logger already has. The COM wiring (pins 1/2/8 only, never pin 4, never the BMS jack), the idle-level test, the explicit arm and `INV_ALLOW_WRITES=0` are unchanged. No new hardware. Every cloud part runs on free tiers.

---

## 0. Decisions at a glance

| # | Decision | Why (short) |
|---|---|---|
| E1 | **Primary transport: a Google Apps Script "mailer"** owned by a **dedicated Gmail sender account**. It runs on a time trigger every 5 min, reads a small RTDB outbox (`mailq`), sends with `MailApp`, and writes the result back. | Free and needs no domain. Mail comes from Gmail itself. The ESP needs no e-mail code or secret. Spark has no Functions. |
| E2 | **The HMAC payload push is dropped.** It is replaced by an optional **"nudge"** (v1.1): a body-less `GET <exec-url>?k=<key>` that only tells the mailer to poll now. The ESP never follows the 302. | The data is already in RTDB, so pushing it again adds nothing. Not following the redirect avoids the 3.3.12 `HTTPClient` 302→GET bug (r8c C1) and the `getString()` hang (r8c M1). There is no envelope to sign, no recipients in the request, and no second dedupe path (r8c C2). |
| E3 | **No fallback transport in v1.** Gmail SMTP from the ESP is specified as **v2, off by default** (§4.11). | Both Google paths share one account (r8c M7), so the fallback does not cover the likeliest failure. It would put an unencrypted mailbox credential in NVS. The library situation is weak (r8c M9). The independent channels stay: ntfy push (d1 §7.1) and Healthchecks.io's own e-mail. |
| E4 | **Apps Script authenticates to RTDB as a third Firebase Auth user, `mailer`** (email/password → ID token → `?auth=`). The rules pin `MAILER_UID` to the least it needs. | This is the least-privilege option, and it is verified [TESTED, 69 emulator cases]. `ScriptApp.getOAuthToken()` with `firebase.database` bypasses the rules and needs the sender to be a project member. Database secrets are legacy admin credentials. |
| E5 | **Rules are evaluated on the ESP32** in a new `RuleEngine` next to `AlertEngine`. The mailer evaluates only **logger offline** (which a dead logger cannot report) and builds the **daily summary**. | The ESP has 5 s samples and the hold and hysteresis semantics of d1 §7.3. It persists state across reboots and keeps working offline. A 5-min poller sees only snapshots, has a 90 min/day trigger budget, and would be a second engine that drifts out of step. |
| E6 | **The mailer renders the e-mails** (HTML + text), resolves recipients from `alerts/email/to` ∩ the `ALLOW_TO` script property, de-duplicates (ledger in PropertiesService + LockService), and enforces the **40 recipient-deliveries/day** cap (5 reserved for severity ≥ 4). | Templates can change without reflashing. The recipients cannot come from the device or from a request (r8c C2). With 40/day, 2 × 40 = 80 < 100 per rolling 24 h (r8c C2). CacheService alone may evict (r8c M3). |
| E7 | **Outbox node `mailq/{eventKey}`**, written by the device in the **same multi-path PATCH** as the event. Events get **deterministic keys** `<ts>-<rid|TYPE>-<o|c|r|i>`. `em` is a **string enum**. | No cursor and no ordering bug (r8c C2). The idle poll is one GET of `null`. Retries are idempotent. |
| E8 | Rule docs live in **`alerts/{email, rules/r01..r16}`** (owner-written). Change pickup uses **`cfg/alertsRev`** in the device's existing 60 s `cfg` GET. | Same approach as r8b A2/A3, which r8c did not dispute. |
| E9 | **One daily-summary e-mail**, sent by the mailer at `summary.hour` (default 07:00) for **yesterday**. The d1 ntfy `DAILY_SUMMARY` push (21:00) stays push-only. | Avoids double quota use (r8c C2). Yesterday's `daily` is final after 00:05. |
| E10 | **Test e-mail** = the owner writes `mailq/test`. The mailer answers in `mailer/test`. The existing **"Send test alert"** (`cfg.testAlert` → `TEST_ALERT`) is now also e-mailed, which tests the whole chain from the device. | The mailer can be tested even while the logger is offline. A test e-mail needs no firmware change. |

**New verification findings from this phase** (both affect r8b's snippets):
1. **`\s` and `\S` are rejected** in rules regular expressions, even though r8c #14 lists them. The database emulator v4.11.2 refuses to load the rules with `Illegal regular expression, 'whitespacechar' not found`. r8b's `matches(/^(\S| )+$/)` for `name` and `fromName` therefore fails in the emulator, and very likely on deploy too (production was not tried). Use `!newData.val().contains('\n') && !…contains('\r') && !…contains('\t')` instead. I tested it: it blocks LF, CR and TAB and accepts Bangla text. A regex class `[^\n\r\t]` does **not** work, because it let a newline through. [TESTED]
2. The emulator needs the unsigned test JWT as `?auth=<jwt>`. With `Authorization: Bearer <jwt>` every request was allowed. This only matters for writing tests. [TESTED]

---

## 1. Transport decision

### 1.1 Primary: Apps Script mailer, pull mode (v1)

```
 ESP32 (uplinkTask)                         Firebase RTDB (Spark)                       Apps Script (sender Gmail)
 ───────────────────                        ────────────────────                        ──────────────────────────
 RuleEngine/AlertEngine
   └─ AlertEvent{em:"q"} ──PATCH /devices/inv1.json──▶ events/<d>/<key>  {…,"em":"q"}
                           (one multi-path request)    mailq/<key>       {d,ts,sev[,rc]}
                                                                ▲      │
                                         every 5 min  ──GET mailq──────┘ (null when idle)
                                                       ──GET events/<d>/<key>, alerts/email, live
                                                                         MailApp.sendEmail ──▶ Gmail ──▶ owner/family inbox
                                                       ◀─PATCH {events/<d>/<key>/em:"sent", mailq/<key>:null}
                                                       ◀─PATCH mailer {hb, quota, sent, err, warn, last}
 PWA  ──set mailq/test {ts}────────────────────────▶ mailq/test  ──(next run)──▶ test mail ──▶ mailer/test {ok,msg}
 PWA  ◀─onValue mailer / alertState / events──────── (heartbeat, results, history)
```

- **Account:** a new Gmail account used only to send, for example `<yourname>.solar.alerts@gmail.com`. It owns the script and nothing else. The mail goes to your normal address. Mail a Gmail account sends to itself may not raise a phone notification [INFERRED, r8a].
- **Sending:** `MailApp.sendEmail({to, subject, body, htmlBody, name, replyTo})`. The scope is `script.send_mail`, which is narrower than GmailApp. `noReply` exists only on Workspace CONFIRMED-primary, [MailApp].
- **Trigger:** `everyMinutes(5)`. The allowed values are 1, 5, 10, 15 and 30 CONFIRMED-primary, [ClockTriggerBuilder]. Installable triggers run as their creator. A failing run e-mails the creator a "Summary of failures for Apps Script" [CONFIRMED-primary, r8c #9].
- **Latency:** from 0 to 5 min, plus trigger jitter, plus the upload of the device event (normally < 10 s). That is acceptable for usage thresholds. ntfy stays the instant channel. With the v1.1 nudge it drops to about 10–30 s [INFERRED].
- **Quotas** (consumer account) [CONFIRMED-primary, [quotas](https://developers.google.com/apps-script/guides/services/quotas)]:
  - 100 recipients/day, reset 24 h after the first request;
  - 90 min/day total trigger runtime;
  - 20,000 URL fetches/day;
  - 30 simultaneous executions;
  - 9 KB per property value and 500 KB per store.

  Google can change these "without notice", so the mailer publishes `getRemainingDailyQuota()` in `mailer/quota` (r8c M10).

**Budget for this design** [INFERRED; measure with the Executions page in week 1]:

| Resource | Idle run | Per day (288 runs) | Limit | Share |
|---|---|---|---|---|
| UrlFetch | 3: GET `mailq`, GET `live/ts`, PATCH `mailer`; plus GET `alerts/email` every 10 min (cached) and a token refresh hourly | ≈ 1,050 | 20,000 | 5 % |
| Trigger runtime | ≈ 1–2 s | ≈ 5–10 min | 90 min | ≤ 11 % |
| RTDB download | ≈ 4 KB (≈ 3.5 KB TLS handshake per request, V2) | ≈ 4 MB, about 120 MB/month | 10 GB/month | ≈ 1.2 % |
| E-mail recipients | – | ≤ 40 (hard cap in the code) | 100 | ≤ 40 % |

### 1.2 Speed-up (v1.1, optional): the nudge

This replaces r8a's HMAC push. After the device writes an event with `em:"q"`, it may call the mailer's web-app URL once. The URL is stored in NVS as `mail/nudge` and looks like `https://script.google.com/macros/s/<deploymentId>/exec?k=<NUDGE_KEY>`.

```
GET /macros/s/<deploymentId>/exec?k=<NUDGE_KEY> HTTP/1.1
Host: script.google.com
(no body; HTTPClient default headers)

<- HTTP/1.1 302 Found          Location: https://script.googleusercontent.com/macros/echo?user_content_key=...
   (doGet has already run; the ESP does NOT follow the redirect and never reads a body)
```

| Item | Rule |
|---|---|
| When | Only after a PATCH that created a `mailq` item. At least 60 s between nudges, at most 48 per day. |
| TLS | `NetworkClientSecure::setCACert(<GTS Root R1–R4 bundle>)`. Today's chain is WE2 → GTS Root R4 (measured, r8c). Pin the whole d1 §5.13 bundle, not R4 alone (r8c M8). |
| Redirects | `HTTPC_DISABLE_FOLLOW_REDIRECTS` (the default). `302` or `200` = accepted. Never follow: on 3.3.12 the redirected request carries a stale `Content-Length` (arduino-esp32 #8300, r8c C1). That bug only bites a POST, but not following also saves a second TLS handshake. |
| Body | Never read. Avoids the 3.3.12 `getString()` hang (fixed upstream in #12990 after 3.3.12, r8c M1). |
| Timeouts | connect 5 s, response 10 s, `esp_task_wdt_reset()` before and after (TWDT is 30 s, d1 §5.9). If the request times out, the script keeps running server-side [INFERRED, bench-test]. The 5-min trigger delivers the mail anyway. |
| Auth | `k` must equal the `NUDGE_KEY` script property (≥ 24 characters, constant-time compare). A wrong key returns `{"ok":false,"err":"auth"}`. A stolen key only lets someone trigger extra polls: at most 1 per 20 s (CacheService), serialised by LockService. It cannot choose the content or the recipients. |
| Deployment | Web app with `executeAs: USER_DEPLOYING` and `access: ANYONE_ANONYMOUS`; `/exec` only [CONFIRMED-primary, r8c #7]. `doGet` sees `e.parameter` [CONFIRMED-primary, [web apps](https://developers.google.com/apps-script/guides/web)]. |

### 1.3 Fallback

- **v1 has no second e-mail transport.** When the mailer is down:
  - ntfy still pushes every severity ≥ 3 alert (d1 §7.3);
  - Healthchecks.io still e-mails "logger offline" from a non-Google sender;
  - the PWA shows "Mailer not running" once `mailer/hb` is older than 15 min;
  - jobs wait in `mailq` and expire after 12 h (24 h for severity ≥ 4), so a recovered mailer does not flood the inbox.
- **v2 (later, opt-in): Gmail SMTP sent directly from the ESP.** Scope and dedupe protocol are in §4.11.
- **If a non-Google e-mail path is ever wanted:** Brevo's HTTPS API (300/day, no card, but the gmail.com sender is rewritten [SECONDARY], r8c C3). Not planned.

### 1.4 How Apps Script reads and writes RTDB (verified, least privilege)

| Option | Verdict |
|---|---|
| **(a) Firebase Auth user `mailer` + ID token, chosen** | Sign-in: `POST identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=<API_KEY>` with `{email,password,returnSecureToken:true}`. Refresh: `POST securetoken.googleapis.com/v1/token?key=…` with `grant_type=refresh_token`. These are the same endpoints the device uses (d1 §4.8, V2 F10 [CONFIRMED-primary]). The script caches the ID token for 50 min, keeps the refresh token in the `RTOK` property, and sends `?auth=<ID_TOKEN>` ([RTDB REST auth](https://firebase.google.com/docs/database/rest/auth) [CONFIRMED-primary]). On a 401 it retries once with a fresh token; a second 401 means the rules refused the request. The rules grant `MAILER_UID` **read**: `mailq`, `alerts`, `live`, `daily`, `events`, `mailer`. **Write**: `mailq`, `mailer`, `events/<d>/<k>/em` and `/emt` **only**. It cannot read `cfg`, `status`, `meta` or `hist`, cannot edit or create events, and cannot change rules [TESTED: N10–N13, N29, N36, N37]. The manifest scopes are the 3 below. |
| (b) `ScriptApp.getOAuthToken()` + `firebase.database` + `userinfo.email` scopes, Bearer | Rejected. It is admin-level and bypasses the rules ([REST auth](https://firebase.google.com/docs/database/rest/auth) [CONFIRMED-primary]). The sender account would have to be a member of the Firebase project. The "works for a project member" pattern is only [SECONDARY]. |
| (c) Legacy database secret | Rejected. It is an admin credential, deprecated, and bypasses the rules. |

Manifest `oauthScopes` (the full list):
- `script.send_mail` for MailApp [CONFIRMED-primary];
- `script.external_request` for UrlFetchApp CONFIRMED-primary, [UrlFetchApp];
- `script.scriptapp` for `newTrigger`, `getProjectTriggers` and `deleteTrigger` CONFIRMED-primary, [ScriptApp].

The PropertiesService and Lock reference pages list no scope [CONFIRMED-primary, by absence]. I assume the same for CacheService, Utilities and ContentService [INFERRED]. The first real `setup` run proves the scope set: a missing scope fails with an authorization error.

### 1.5 Rejected or deferred alternatives

All per r8a §1/§2.4 and r8c §2 #16–17:
- SMTP2GO: refuses Gmail sign-ups.
- Postmark and MailerSend: need a domain or a card.
- SendGrid: free plan retired.
- Cloudflare Email: needs a domain on Cloudflare.
- EmailJS: 200/month.
- Mailgun: sandbox only.
- Resend `resend.dev`: "only available for testing purposes".
- ntfy e-mail: 5/day.
- Firebase "Trigger Email" extension: Blaze plan.

---

## 2. Where rules are evaluated

| What | Where | Why |
|---|---|---|
| User rules (power, voltage, SOC, temperature, booleans, day/month energy, outage minutes) | **ESP32 `RuleEngine`** in `aggTask` | 5 s data, the d1 §7.3 hold/clear state machine, `millis()`-based holds unaffected by NTP steps, state that survives reboots, and it keeps running while the internet is down. Events are back-filled with their original `ts` and the mailer marks them "Delayed". |
| Built-in d1 §7.2 alerts by e-mail | **ESP32 `AlertEngine`** (existing), flagged by `EmailPolicy` | It already decides. It only needs `em:"q"` on the events it already makes. |
| Per-rule cooldown, repeat and `maxDay`, plus the device mail-job cap (40/day) | **ESP32** | Needs the rule state; persisted in NVS. |
| Recipients, allow-list, global recipient cap (40/day, reserve 5), Google quota floor, dedupe, digest, rendering | **Mailer** | The single point that actually sends. Owner-controlled. Survives logger bugs. |
| Logger offline / back online | **Mailer** | A dead or unplugged logger cannot report itself. Uses `live.ts`. |
| Daily summary (yesterday) | **Mailer** | Reads the final `daily/<yesterday>` and the cycle range. Also works as an outage signal ("No data for …"). |

---

## 3. Data contract (RTDB, under `/devices/inv1`)

These are additions to d1 §4.1–4.2. All key names are new, and none of them conflicts with the existing `live`, `status`, `meta`, `diag`, `hist`, `h15`, `daily`, `events` or `cfg`. Sizes are compact JSON.

### 3.1 Tree additions

```
/devices/inv1/
    alerts/email                 owner   e-mail settings + recipients (≈ 300 B)
    alerts/rules/{r01..r16}      owner   rule definitions (≈ 250–350 B each, ≤ 16)
    cfg/alertsRev                owner   serverTimestamp() (ms), bumped with every alerts/* change
    alertState/{r01..r16}        device  engine state per rule, written on transitions
    events/{date}/{key}          device  + rid, val, thr, em ; mailer: em, emt only
    mailq/{eventKey}             device  outbox job (pointer to the event); mailer deletes
    mailq/test                   owner   test e-mail request; mailer deletes
    mailer                       mailer  heartbeat, quota, counters, last result, test result
    live.gw                      device  NEW numeric key: grid import power estimate (W); `$num` already allows it
    status.{arv,acat,mjq,nrul}   device  NEW numeric keys (≤ 8 chars; `$k` already allows them)
```

### 3.2 `alerts/email`

```json
{"on":true,"to":"owner@gmail.com,family@gmail.com","fromName":"Solar logger","maxPerDay":40,"monthStart":1,
 "builtin":{"minSev":4,"clear":true},"summary":{"on":true,"hour":7},"offline":{"on":true,"min":30,"clear":true}}
```

| Field | Type / range | Default (absent) | Used by | Meaning |
|---|---|---|---|---|
| `on` | bool (required) | false | device + mailer | Master switch. When false the device queues nothing, and the mailer sends nothing except test e-mails. |
| `to` | string, 0–3 comma-separated addresses, ≤ 300 chars | `""` | mailer | Recipients. It is a **string, not an object**, which avoids the RTDB array/object ambiguity r8b warned about. Position `i` is bit `i` of a rule's `rc` mask. The mailer only sends to addresses that are **also** in the `ALLOW_TO` script property. |
| `fromName` | 1–40 chars, no CR/LF/TAB | `Solar logger` | mailer | Sender display name. |
| `maxPerDay` | int 1–40 | 40 | mailer | Recipient-deliveries per local day. The `MAX_PER_DAY` script property can only lower it. The last 5 are kept for severity ≥ 4. |
| `monthStart` | int 1–28 | 1 | device + mailer | First day of the billing cycle for `month` rules and the summary's "this cycle" column. |
| `builtin.minSev` | 0, 3, 4 or 5 | 4 | device | Which d1 built-in alerts are also e-mailed (0 = none). `TEST_ALERT` is always e-mailed while `on`. |
| `builtin.clear` | bool | true | device | Also e-mail the clear or restore of an e-mailed built-in, e.g. "Grid restored after 2h 14m". |
| `summary.{on,hour}` | bool, int 0–23 | `true`, `7` | mailer | Daily summary for yesterday, at or after this local hour, once per day. |
| `offline.{on,min,clear}` | bool, int 10–240, bool | `true`, `30`, `true` | mailer | "Logger offline" after `min` minutes without a `live` update, plus one "back online" mail. At most 4 such mails per day. Raise `min` if load-shedding makes it noisy. |

### 3.3 `alerts/rules/{rid}` (`rid` = `r01`…`r16`)

```json
{"name":"Load > 2500 W for 5 min","on":true,"metric":"w","scope":"inst","op":"gt","value":2500,"mode":"hold",
 "forSec":300,"clr":2200,"clrSec":120,"coolMin":60,"repeatMin":0,"maxDay":3,
 "win":{"from":"0600","to":"2300","days":127},"sev":3,"email":true,"ntfy":true,"rc":1,"clrMail":true,"upd":1792560000000}
```

| Field | Type / range | Default | Meaning |
|---|---|---|---|
| `name` | 1–40 chars, no CR/LF/TAB (required) | – | Shown in the UI, the event `msg` and the e-mail subject. |
| `on` | bool (required) | – | Enabled. |
| `metric` | catalogue id (§4.3) (required) | – | What is compared. |
| `scope` | `inst` \| `day` \| `month` (required) | – | `inst` for instantaneous metrics; `day`/`month` for energy and `gout`. |
| `op` | `gt` \| `ge` \| `lt` \| `le` (required) | – | Comparator. |
| `value` | number, range by metric (required) | – | Canonical units: W, Wh (not kWh), V, A, %, °C, Hz, min, 0/1 for booleans. |
| `mode` | `hold` \| `at` (required) | – | `hold`: true for `forSec`. `at`: checked once a day at `at`. |
| `at` | `hhmm` | – | Required when `mode = at`. |
| `forSec` | 0–86400 | inst 60, energy 0 | Hold time before the rule fires. |
| `clr`, `clrSec` | number on the safe side of `value`; 0–3600 | value ∓ default hysteresis (§4.3); 60 | Clear threshold and clear hold time. |
| `coolMin` | 5–10080 | 60 | Minimum gap between two e-mails of this rule (open/repeat). Clear e-mails are exempt. |
| `repeatMin` | 0 or 30–1440 | 0 | Re-mail "still active" while firing; spacing = max(repeatMin, coolMin). |
| `maxDay` | 1–10 | 3 | E-mails per local day for this rule. |
| `minCov` | 0–100 | 80 | Data coverage needed before an `at`-mode `lt`/`le` energy check may fire. |
| `win` | `{from, to: hhmm, days: 1–127}` | always | Active hours. `from > to` crosses midnight. `days` bit 0 = Sunday … bit 6 = Saturday, and applies to the day the window **starts**. |
| `sev` | 1–5 (required) | 3 | Severity: d1 meaning, `shared/codes.json` names (Info … Critical). |
| `email`, `ntfy` | bool (required, at least one true) | – | Channels. ntfy follows the d1 routing, quiet hours and push cap, and Telegram is added for sev ≥ 4. |
| `rc` | 1–7 bitmask over `alerts/email/to` | all | Recipient subset. |
| `clrMail` | bool | false | Send a "Cleared" e-mail, but only if the open was e-mailed. |
| `upd` | ms ≤ now (required) | – | `serverTimestamp()`. |

Constraints enforced by the database (§3.11):
- **Energy and `gout` rules:**
  - Scope must be `day` or `month`; every other metric must use `inst`.
  - `month` rules allow only `gt`/`ge` with `hold`.
  - `day` rules with `lt`/`le` must use `mode:"at"`, because energy only grows during the day.
- **Clear threshold:** `clr` must be on the safe side of `value`.
- **Value range:** `value` must fall within the range of its metric.
- **Count:** at most 16 rules, enforced by the key pattern, because the rules language has no `numChildren()` (r8c #13).

### 3.4 `cfg/alertsRev`

`{"alertsRev":1792560000000}`. A number in ms, ≤ `now`. It is always written in the **same** multi-path `update()` as the `alerts/*` change. The device's existing 60 s `GET cfg.json` sees `alertsRev ≠ loaded` and then GETs `alerts.json` (≤ ≈ 6 KB). The device also does a full refresh at boot and every 6 h. ETags are not used (r8c #11).

### 3.5 `alertState/{rid}` (device)

```json
{"r01":{"st":"fire","since":1792560123,"val":2710,"last":1792560125,"cnt":1},
 "r03":{"st":"latch","since":1792580003,"val":5010,"last":1792580003,"cnt":1,"pk":20261021}}
```

| Field | Meaning |
|---|---|
| `st` | `off`, `wait`, `idle`, `pend`, `fire`, `clr`, `latch`, `done` or `inv`. |
| `since` | Epoch when the state was entered. |
| `val` | The value at the transition. |
| `last` | The last e-mail queued. |
| `cnt` | E-mails today. |
| `pk` | Period key (`yyyymmdd`). |
| `err` | Why the rule is `inv` (≤ 40 characters). |

It is written only on transitions, piggy-backed on the minute PATCH.

### 3.6 `events/{date}/{key}`: additions

**Keys.** All device events, built-in and rule, use the deterministic key `<ts>-<src>-<kind>`:
- `ts` is the 10-digit event epoch;
- `src` is `r01`…`r16` or the built-in `type`;
- `kind` is `o` (open), `c` (clear), `r` (repeat / still active) or `i` (info).

Examples: `1792580003-r03-o`, `1792560123-GRID_LOSS-o`.

Retries overwrite the same key, so they never duplicate. The FW-2 uplink is still a stub, so moving away from d1's push IDs costs nothing. The PWA uses keys only for `ack`.

```json
{"ts":1792580003,"type":"RULE","sev":3,"st":"open","rid":"r03","val":5010,"thr":5000,
 "msg":"Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)","ch":"email","em":"q"}
```

| New field | Writer | Meaning |
|---|---|---|
| `rid` | device | Rule slot for `type:"RULE"` events. |
| `val`, `thr` | device | Value and threshold in canonical units (for the PWA chips). |
| `em` | device creates it, **mailer** updates it | E-mail status, one of these codes:<br>• `q` = queued for the mailer<br>• device gates: `cool` (cooldown), `rcap` (rule's `maxDay`), `cap` (device job cap), `skip` (coverage too low)<br>• mailer results: `sent`, `cap` (daily recipient cap), `quota` (Google quota floor), `off` (alerts switched off), `norc` (no allowed recipient), `exp` (too old), `fail` (5 send errors)<br>An event without `em` was not meant for e-mail. |
| `emt` | mailer | Epoch when the mailer set `em`. |

New event types for `shared/codes.json` → `eventTypes`:
- `RULE`: severity per rule, label "Rule alert";
- `RULE_INVALID`: severity 2, "Rule ignored by the logger".

### 3.7 `mailq` (outbox)

```json
{"1792580003-r03-o":{"d":"2026-10-21","ts":1792580003,"sev":3},
 "1792580090-r05-o":{"d":"2026-10-21","ts":1792580090,"sev":4,"rc":1,"n":2},
 "test":{"ts":1792580100,"rid":"r03"}}
```

- **Device jobs.** The key equals the event key. `d` is the event's date key. `rc` is copied from the rule. The mailer increments `n` after a send error and gives up at 5.
- **`test`.** Written by the owner from the PWA. `ts` must be within ±300 s of server time. The optional `rid` sends a "[TEST]" e-mail for that rule to that rule's recipients.
- **Lifecycle.** The mailer deletes each job when it finishes it, in the same PATCH that sets `em`. Idle state is `null`.

Writing to `mailq` grows with jobs (≤ 40/day from the device cap) and shrinks to zero.

### 3.8 `mailer` (mailer heartbeat)

```json
{"hb":1792580400,"ver":"1.0.0","day":"2026-10-21","sent":4,"quota":93,"off":0,"err":"","warn":"",
 "last":{"ts":1792580003,"ok":true,"msg":"[Solar] Daily grid usage > 5 kWh: now 5.01 kWh"},
 "test":{"ts":1792580100,"ok":true,"msg":"sent to 2 recipients"}}
```

| Field | Meaning |
|---|---|
| `hb` | Last run, epoch s. |
| `sent` | Recipient-deliveries today. |
| `quota` | `MailApp.getRemainingDailyQuota()`. |
| `off` | Last-seen `live.ts` while the logger is offline (0 = online). |
| `err` | The last run's error (≤ 120 characters). |
| `warn` | For example, "Not on ALLOW_TO: x@y". |
| `last` | The last e-mail sent. |
| `test` | The result of the last test request. |

### 3.9 `status` and `live` additions (device)

| Key | Meaning |
|---|---|
| `status.arv` | Loaded `alertsRev` (ms); the PWA shows "logger has your latest rules". |
| `status.acat` | Metric catalogue version (§4.3); the PWA hides metrics newer than it. |
| `status.mjq` | E-mail jobs queued today. |
| `status.nrul` | Enabled rules loaded. |
| `live.gw` | Grid import power estimate, W. This is the `egr` integrand `(gridPresent && md=='L') ? w + acChgW : 0` (d1 §4.4). It lets the PWA show the same value the device compares. |

### 3.10 Request flows

| Actor | When | Request |
|---|---|---|
| PWA | Save / toggle / delete a rule | `update(ref(db,'devices/inv1'), {'alerts/rules/r03': rule \| null, 'cfg/alertsRev': serverTimestamp()})` |
| PWA | Save e-mail settings | `update(..., {'alerts/email': cfg, 'alerts/rules/rNN/rc': remapped…, 'cfg/alertsRev': serverTimestamp()})` |
| PWA | Test e-mail | `set(ref(db,'devices/inv1/mailq/test'), {ts: floor((Date.now()+serverTimeOffset)/1000)[, rid]})`. The offset comes from `.info/serverTimeOffset` ([docs](https://firebase.google.com/docs/database/web/offline-capabilities) [CONFIRMED-primary]). |
| Device | 60 s | `GET /devices/inv1/cfg.json` (existing; now also reads `alertsRev`) |
| Device | Rev change, boot, 6 h | `GET /devices/inv1/alerts.json` |
| Device | Rule or built-in event | `PATCH /devices/inv1.json?auth=…&print=silent` `{"events/<d>/<key>":{…},"mailq/<key>":{…}}` (the `mailq` part only when `em:"q"`) |
| Device | Minute PATCH | adds `"alertState/rNN":{…}` for dirty rules and the `status` keys |
| Device | v1.1 | Nudge GET (§1.2) |
| Mailer | 5 min | `GET mailq` → per job `GET events/<d>/<k>` → `PATCH /devices/inv1.json {events/<d>/<k>/em, …/emt, mailq/<k>:null}` → `GET live/ts` → `PATCH mailer` |
| Mailer | Summary | `GET daily.json?orderBy="$key"&startAt="<min(cycleStart, d-2)>"&endAt="<yesterday>"` (≤ 32 × 183 B) + `GET events/<yesterday>.json` |

### 3.11 RTDB rules to merge into [`firebase/database.rules.json`](../firebase/database.rules.json)

There is a new placeholder, **`MAILER_UID`**, next to `OWNER_UID` and `DEVICE_UID`. The merge was tested: a merged copy loads in the database emulator and passes **69/69** allow/deny cases (§8.1) [TESTED].

**(a)** Add a `.read` grant for the mailer as the **first** key of `live`, `daily` and `events` (siblings of their existing `.write`):

```json
".read": "auth != null && auth.uid === 'MAILER_UID'",
```

**(b)** In `events/$date/$eid`, insert these keys **above** the existing `"$other": { ".validate": false }`:

```json
"rid": { ".validate": "newData.isString() && newData.val().matches(/^r(0[1-9]|1[0-6])$/)" },
"val": { ".validate": "newData.isNumber()" },
"thr": { ".validate": "newData.isNumber()" },
"em":  { ".write": "auth != null && auth.uid === 'MAILER_UID'", ".validate": "newData.isString() && newData.val().matches(/^(q|sent|cool|rcap|cap|quota|off|norc|exp|fail|skip)$/)" },
"emt": { ".write": "auth != null && auth.uid === 'MAILER_UID'", ".validate": "newData.isNumber()" },
```

**(c)** In `cfg`, insert this **above** its `"$other"`:

```json
"alertsRev": { ".validate": "newData.isNumber() && newData.val() <= now" },
```

**(d)** Add these four new children of `devices/inv1`, as siblings of `cfg`:

```json
"alerts": {
  ".read": "auth != null && auth.uid === 'MAILER_UID'",
  ".write": "auth != null && auth.uid === 'OWNER_UID'",
  "email": {
    ".validate": "newData.hasChildren(['on'])",
    "on":         { ".validate": "newData.isBoolean()" },
    "to":         { ".validate": "newData.isString() && newData.val().length <= 300 && (newData.val() === '' || newData.val().matches(/^[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,24}(,[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,24}){0,2}$/))" },
    "fromName":   { ".validate": "newData.isString() && newData.val().length >= 1 && newData.val().length <= 40 && !newData.val().contains('\\n') && !newData.val().contains('\\r') && !newData.val().contains('\\t')" },
    "maxPerDay":  { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 40" },
    "monthStart": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 28" },
    "builtin": {
      "minSev": { ".validate": "newData.isNumber() && (newData.val() === 0 || newData.val() === 3 || newData.val() === 4 || newData.val() === 5)" },
      "clear":  { ".validate": "newData.isBoolean()" },
      "$other": { ".validate": false }
    },
    "summary": {
      ".validate": "newData.hasChildren(['on', 'hour'])",
      "on":   { ".validate": "newData.isBoolean()" },
      "hour": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 23" },
      "$other": { ".validate": false }
    },
    "offline": {
      ".validate": "newData.hasChildren(['on', 'min'])",
      "on":    { ".validate": "newData.isBoolean()" },
      "min":   { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 10 && newData.val() <= 240" },
      "clear": { ".validate": "newData.isBoolean()" },
      "$other": { ".validate": false }
    },
    "$other": { ".validate": false }
  },
  "rules": {
    "$rid": {
      ".validate": "$rid.matches(/^r(0[1-9]|1[0-6])$/) && newData.hasChildren(['name', 'on', 'metric', 'scope', 'op', 'value', 'mode', 'sev', 'email', 'ntfy', 'upd']) && (newData.child('metric').val().matches(/^(eld|egr|epv|edis|ech|gout)$/) ? newData.child('scope').val() !== 'inst' : newData.child('scope').val() === 'inst') && (newData.child('scope').val() !== 'month' || (newData.child('op').val().matches(/^g[te]$/) && newData.child('mode').val() === 'hold')) && (!(newData.child('scope').val() === 'day' && newData.child('op').val().matches(/^l[te]$/)) || newData.child('mode').val() === 'at') && (newData.child('mode').val() !== 'at' || newData.hasChild('at')) && (!newData.hasChild('clr') || (newData.child('op').val().matches(/^g[te]$/) ? newData.child('clr').val() <= newData.child('value').val() : newData.child('clr').val() >= newData.child('value').val())) && (newData.child('email').val() === true || newData.child('ntfy').val() === true)",
      "name":   { ".validate": "newData.isString() && newData.val().length >= 1 && newData.val().length <= 40 && !newData.val().contains('\\n') && !newData.val().contains('\\r') && !newData.val().contains('\\t')" },
      "on":     { ".validate": "newData.isBoolean()" },
      "metric": { ".validate": "newData.isString() && newData.val().matches(/^(w|pvw|gw|bdw|bw|soc|ld|bv|gv|ov|pvv|bdi|tmp|gf|onbat|nogrid|eld|egr|epv|edis|ech|gout)$/)" },
      "scope":  { ".validate": "newData.isString() && newData.val().matches(/^(inst|day|month)$/)" },
      "op":     { ".validate": "newData.isString() && newData.val().matches(/^(gt|ge|lt|le)$/)" },
      "value":  { ".validate": "newData.isNumber() && (newData.parent().child('metric').val().matches(/^(w|pvw|gw|bdw|bw)$/) ? (newData.val() >= -10000 && newData.val() <= 20000) : newData.parent().child('metric').val().matches(/^(soc|ld)$/) ? (newData.val() >= 0 && newData.val() <= 250) : newData.parent().child('metric').val().matches(/^(bv|gv|ov|pvv)$/) ? (newData.val() >= 0 && newData.val() <= 600) : newData.parent().child('metric').val() === 'bdi' ? (newData.val() >= 0 && newData.val() <= 300) : newData.parent().child('metric').val() === 'tmp' ? (newData.val() >= -20 && newData.val() <= 150) : newData.parent().child('metric').val() === 'gf' ? (newData.val() >= 40 && newData.val() <= 70) : newData.parent().child('metric').val().matches(/^(onbat|nogrid)$/) ? (newData.val() === 0 || newData.val() === 1) : newData.parent().child('metric').val().matches(/^(eld|egr|epv|edis|ech)$/) ? (newData.val() >= 0 && newData.val() <= 5000000) : newData.parent().child('metric').val() === 'gout' ? (newData.val() >= 0 && newData.val() <= 44640) : false)" },
      "mode":      { ".validate": "newData.isString() && newData.val().matches(/^(hold|at)$/)" },
      "at":        { ".validate": "newData.isString() && newData.val().matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/)" },
      "forSec":    { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 86400" },
      "clr":       { ".validate": "newData.isNumber() && newData.val() >= -10000 && newData.val() <= 5000000" },
      "clrSec":    { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 3600" },
      "coolMin":   { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 5 && newData.val() <= 10080" },
      "repeatMin": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && (newData.val() === 0 || (newData.val() >= 30 && newData.val() <= 1440))" },
      "maxDay":    { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 10" },
      "minCov":    { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 100" },
      "win": {
        ".validate": "newData.hasChildren(['from', 'to', 'days']) && newData.child('from').val() !== newData.child('to').val()",
        "from": { ".validate": "newData.isString() && newData.val().matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/)" },
        "to":   { ".validate": "newData.isString() && newData.val().matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/)" },
        "days": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 127" },
        "$other": { ".validate": false }
      },
      "sev":      { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 5" },
      "email":    { ".validate": "newData.isBoolean()" },
      "ntfy":     { ".validate": "newData.isBoolean()" },
      "rc":       { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 7" },
      "clrMail":  { ".validate": "newData.isBoolean()" },
      "upd":      { ".validate": "newData.isNumber() && newData.val() <= now" },
      "$other":   { ".validate": false }
    }
  },
  "$other": { ".validate": false }
},
"alertState": {
  ".write": "auth != null && auth.uid === 'DEVICE_UID'",
  "$rid": {
    ".validate": "$rid.matches(/^r(0[1-9]|1[0-6])$/) && newData.hasChildren(['st'])",
    "st":    { ".validate": "newData.isString() && newData.val().matches(/^(off|wait|idle|pend|fire|clr|latch|done|inv)$/)" },
    "since": { ".validate": "newData.isNumber()" },
    "val":   { ".validate": "newData.isNumber()" },
    "last":  { ".validate": "newData.isNumber()" },
    "cnt":   { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 1000" },
    "pk":    { ".validate": "newData.isNumber()" },
    "err":   { ".validate": "newData.isString() && newData.val().length <= 40" },
    "$other": { ".validate": false }
  }
},
"mailq": {
  ".read": "auth != null && auth.uid === 'MAILER_UID'",
  ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'MAILER_UID')",
  "test": {
    ".write": "auth != null && auth.uid === 'OWNER_UID'",
    ".validate": "newData.hasChildren(['ts']) && newData.child('ts').isNumber() && newData.child('ts').val() > now / 1000 - 300 && newData.child('ts').val() < now / 1000 + 300",
    "ts":  { ".validate": "newData.isNumber()" },
    "rid": { ".validate": "newData.isString() && newData.val().matches(/^r(0[1-9]|1[0-6])$/)" },
    "$other": { ".validate": false }
  },
  "$k": {
    ".validate": "$k.matches(/^[0-9]{10}-[A-Za-z0-9_]{1,24}-[ocri]$/) && newData.hasChildren(['d', 'ts', 'sev'])",
    "d":   { ".validate": "newData.isString() && newData.val().matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)" },
    "ts":  { ".validate": "newData.isNumber()" },
    "sev": { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 5" },
    "rc":  { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 7" },
    "n":   { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 20" },
    "$other": { ".validate": false }
  }
},
"mailer": {
  ".read": "auth != null && auth.uid === 'MAILER_UID'",
  ".write": "auth != null && auth.uid === 'MAILER_UID'",
  "hb":    { ".validate": "newData.isNumber() && newData.val() < now / 1000 + 600" },
  "ver":   { ".validate": "newData.isString() && newData.val().length <= 16" },
  "day":   { ".validate": "newData.isString() && newData.val().length <= 10" },
  "sent":  { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 1000" },
  "quota": { ".validate": "newData.isNumber()" },
  "off":   { ".validate": "newData.isNumber()" },
  "err":   { ".validate": "newData.isString() && newData.val().length <= 120" },
  "warn":  { ".validate": "newData.isString() && newData.val().length <= 120" },
  "last": {
    "ts":  { ".validate": "newData.isNumber()" },
    "ok":  { ".validate": "newData.isBoolean()" },
    "msg": { ".validate": "newData.isString() && newData.val().length <= 120" },
    "$other": { ".validate": false }
  },
  "test": {
    "ts":  { ".validate": "newData.isNumber()" },
    "ok":  { ".validate": "newData.isBoolean()" },
    "msg": { ".validate": "newData.isString() && newData.val().length <= 120" },
    "$other": { ".validate": false }
  },
  "$other": { ".validate": false }
},
```

What these rules guarantee [TESTED unless noted]:

| Guarantee | Tests (§8.1) |
|---|---|
| Owner-only `alerts` and `cfg/alertsRev`; the device and the mailer can only read `alerts` | N1, N33, P1–P3 |
| Device-only `alertState` | P9, N26 |
| The mailer can write only `mailer`, `mailq` and `em`/`emt` of an **existing** event | P6, N11–N13, N21, N29, N36 |
| At most 16 rules and 3 recipients; valid addresses only | N2, N8, N9 |
| Metric/scope/op/mode consistency; `value` range by metric; `clr` on the safe side; at least one channel; no unknown fields; `upd` required | N3–N6, N17–N19, N23, N30–N32 |
| No CR/LF/TAB in `name` or `fromName` (header injection) | N7 |
| Test requests only from the owner and within ±300 s | P7, N14, N15, N38 |
| Device jobs only with well-formed keys; strangers read nothing | N16, N22 |
| Existing behaviour unchanged (owner cannot write `live`; device still writes `live`; legacy push-id events still accepted) | P21–P23, N35 |

> ⚠️ **Deploy order (r8c M6): rules first, then the mailer, then the PWA, then the firmware.** `events/$eid` and `cfg` have `$other:false`. A firmware that writes `rid`/`em`, or a PWA that writes `alertsRev`, before the new rules are live is refused with 401. Refused requests still count against the download quota (V2 F3).

> ⚠️ Rules still to verify on the real server: the emulator accepted every expression, but a per-expression size limit on production was not tested (r8c §3). If the console refuses the long `$rid` `.validate`, split its clauses onto the child keys (`scope` and `op` can reference `newData.parent()`).

---

## 4. Firmware changes (spec for the implementation phase)

### 4.1 Modules

| File | Kind | Content |
|---|---|---|
| `src/agg/RuleTypes.h` | new, host-portable | `rules::Metric`, `Scope`, `Op`, `Mode`, `St`, `Def`, `Set`, `EmailPolicy` |
| `src/agg/RuleParse.{h,cpp}` | new, host-portable (ArduinoJson 7.4.3) | `bool rules::parse(const char* json, size_t n, Set&, char* err, size_t)`. Mirrors §3.11 exactly. Unknown metric → `Def.invalid` with an `err` message, not a parse failure. Accepts the `to` string, but the device does not need recipients. |
| `src/agg/RuleEngine.{h,cpp}` | new, host-portable | State machine §4.4, gates §4.6, message formatting §4.5, output into the existing `AlertQueue` |
| `src/agg/MonthToDate.{h,cpp}` | new, host-portable | Billing-cycle sums §4.5 |
| `src/agg/NotifyGates.h` | new, host-portable | Quiet hours and the 40/day push cap extracted from `AlertEngine::emit`, plus the device mail-job cap. Shared by both engines, so there is one counter. |
| `src/agg/AlertEngine.{h,cpp}` | changed | Uses `NotifyGates`; sets `em`/`kind` per `EmailPolicy` (§4.7) |
| `include/alert_metrics.h` | new, **generated** | From `shared/alert-metrics.json` by `tools/gen_codes.py` (the same generator that writes `codes.h`) |
| `src/store/Settings.{h,cpp}` | changed | `getBlob/putBlob(ns,key,ptr,len)`; namespaces `rules` (`rs01`…`rs16`, `mj`, `rev`) and `mail` (`nudge`); `energy/mtd` |
| `src/app/App.{h,cpp}` | changed | `applyRules(const rules::Set&)` / `takeRules()` (uplink → aggTask), `ruleStates()` for `/api/status` |
| `src/app/Tasks.cpp` (aggTask) | changed | Load `/cfg/alerts.json` at boot; `ruleEngine.onSample()` after `alerts.evaluate()`; `ruleEngine.tick()` at 1 Hz; `mtd.fold(fin)` on `takeDayRolled`; gap fill at TIME_OK |
| `src/net/Uplink.cpp` (FW-2) | changed | §4.9 |
| `src/net/MailNudge.{h,cpp}` | new (v1.1) | §1.2 |
| `src/net/WifiProv.cpp` | changed (v1.1) | Portal field `{"mailurl","Mailer nudge URL incl. ?k= (optional; blank = keep)","mail","nudge",200,true}` |
| `src/web/LocalWeb.cpp` | changed | `/api/status` gains `rules:{n,rev,acat,st:[…]}`; `/api/config` accepts `mail/nudge`, write-only |
| `platformio.ini` `[env:native]` | changed | `lib_deps = bblanchon/ArduinoJson@7.4.3` (it is header-only and host-portable), so `RuleParse` is unit-tested |

### 4.2 Interfaces

```cpp
// src/agg/RuleTypes.h
namespace rules {
enum class Metric : uint8_t { W, PVW, GW, BDW, BW, SOC, LD, BV, GV, OV, PVV, BDI, TMP, GF, ONBAT, NOGRID,
                              ELD, EGR, EPV, EDIS, ECH, GOUT, COUNT };
enum class Scope : uint8_t { INST, DAY, MONTH };
enum class Op : uint8_t { GT, GE, LT, LE };
enum class Mode : uint8_t { HOLD, AT };
enum class St : uint8_t { OFF, WAIT, IDLE, PEND, FIRE, CLR, LATCH, DONE, INV };
struct Def {                         // ~100 B; 16 in internal RAM
  char rid[4], name[41];
  bool on, email, ntfy, clrMail, hasClr, hasWin, invalid;
  Metric metric; Scope scope; Op op; Mode mode;
  float value, clr;                  // canonical units
  uint32_t forSec, clrSec;
  uint16_t coolMin, repeatMin, atMin, winFrom, winTo;   // minutes of the local day
  uint8_t maxDay, minCov, sev, rc, days;
  uint32_t semHash;                  // FNV-1a over metric,scope,op,value,mode,at,forSec,clr,clrSec,win
  char err[40];
};
struct EmailPolicy { bool on = false; uint8_t builtinMinSev = 4; bool builtinClear = true; uint8_t monthStart = 1; };
struct Set { double rev = 0; uint8_t n = 0; Def d[cfg::MAX_RULES]; EmailPolicy email; };
}

// src/agg/AlertEngine.h - AlertEvent gains (all zero/empty = old behaviour):
//   char rid[4];   float val = NAN, thr = NAN;   char em[6];   // "" | "q" | "cool" | "rcap" | "cap" | "skip"
//   uint8_t rc = 0;  char kind = 'i';                            // key suffix o|c|r|i

// src/agg/RuleEngine.h
struct EnergyView {                  // filled by aggTask from Aggregator::todayEnergy()/daily() + MonthToDate
  int32_t day;                       // local day number of the "today" values (rollover guard, §4.5)
  double eld, egr, epv, edis, ech; int goutMin; uint8_t covPct;
  double mEld, mEgr, mEpv, mEdis, mEch; int mGoutMin; uint32_t mCycleKey; uint8_t mCovPct;
};
#pragma pack(push, 1)
struct RulePersist { uint32_t semHash, sinceEp, lastMailEp, openEp, pk; int32_t cntDay;
                     uint8_t st, cnt, evCnt, flags; };   // 28 B; flags: b0 openMailed, b1 deferredOpen
#pragma pack(pop)
class IRulePersist { public: virtual ~IRulePersist() {}
  virtual bool load(uint8_t slot, RulePersist& p) = 0; virtual void save(uint8_t slot, const RulePersist& p) = 0; };
struct AlertStateOut { char rid[4]; rules::St st; uint32_t since, last, pk; float val; uint8_t cnt; char err[40]; };

class RuleEngine {
 public:
  RuleEngine(AlertQueue& q, NotifyGates& g, IRulePersist* persist);
  void setRules(const rules::Set& s, uint32_t ep);   // keeps state where semHash is unchanged
  void onSample(const Sample& s, const EnergyView& e, uint32_t ms, uint32_t ep, bool timeOk);  // 5 s, inst rules
  void tick(const EnergyView& e, uint32_t ms, uint32_t ep, bool timeOk);  // 1 Hz: energy/at rules, NaN grace, rollover
  bool takeDirty(AlertStateOut& o);                  // for the uplink's alertState PATCH
  double loadedRev() const; uint8_t enabled() const;
};
```

### 4.3 Metric catalogue (`shared/alert-metrics.json`, `acat` = 1)

The generator emits `include/alert_metrics.h` and `web/src/alertMetrics.ts`. It must **assert that the metric regex in `database.rules.json` lists exactly these ids**.

| id | Label | Unit (stored / shown) | Scope | Source on the device | Default hysteresis (when `clr` is absent) |
|---|---|---|---|---|---|
| `w` | Load power | W | inst | `s.w` | max(50 W, 5 %) |
| `pvw` | Solar power | W | inst | `s.pvw` | max(50 W, 5 %) |
| `gw` | Grid import power (est.) | W | inst | `Aggregator::gridPresent(s) && s.md=='L' ? s.w + Aggregator::acChargeW(s) : 0` | max(50 W, 5 %) |
| `bdw` | Battery discharge power | W | inst | `max(0, −s.bw)` | max(50 W, 5 %) |
| `bw` | Battery net power (+ = charging) | W | inst | `s.bw` | max(50 W, 5 %) |
| `soc` | Battery SOC | % | inst | `s.soc` | 2 |
| `ld` | Load % of rating | % | inst | `s.ld` | 2 |
| `bv` | Battery voltage | V | inst | `s.bv` | 0.3 |
| `gv` | Grid voltage | V | inst | `s.gv` | 5 |
| `ov` | Output voltage | V | inst | `s.ov` | 5 |
| `pvv` | Solar voltage | V | inst | `s.pvv` | 10 |
| `bdi` | Battery discharge current | A | inst | `s.bdi` | 2 |
| `tmp` | Inverter temperature | °C | inst | `s.tmp`, NaN if `tmpRaw` | 3 |
| `gf` | Grid frequency | Hz | inst | `s.gf` | 0.3 |
| `onbat` | On battery | bool (0/1) | inst | `s.md == 'B'` | – |
| `nogrid` | Grid absent | bool (0/1) | inst | `!Aggregator::gridPresent(s)` | – |
| `eld` | Load energy | Wh / kWh | day, month | today running / MTD | latched |
| `egr` | Grid energy (est.) | Wh / kWh | day, month | today running / MTD | latched |
| `epv` | Solar energy | Wh / kWh | day, month | today running / MTD | latched |
| `edis` | Battery discharged | Wh / kWh | day, month | today running / MTD | latched |
| `ech` | Battery charged | Wh / kWh | day, month | today running / MTD | latched |
| `gout` | Grid outage time | min | day, month | `daily.gout` / MTD | latched |

`egr` and `gw` are labelled "est." everywhere. They count only loads on the inverter's AC OUT plus AC charging, so they are **not the utility meter** (d1 §4.4). `gw`, `egr` and `nogrid` share one grid predicate, `Aggregator::gridPresent()`.

### 4.4 Per-rule state machine

```
             on & loaded               in window & cond(v)          held ≥ forSec (0: at once)
   OFF ───────────────▶ IDLE ─────────────────────────▶ PEND ───────────────────────────▶ FIRE ──(day/month)──▶ LATCH
    ▲   needs local time  ▲ ◀── !cond / left window / NaN > 30 s ─┘                        │  ▲                  │
    │   & !TIME_OK        │                                                     clr(v)     │  │ !clr(v)          │ period key
   WAIT                   │                  held ≥ clrSec                                 ▼  │                  │ changes
                          └───────────────────────────────────────────────────────────── CLR ┘                  ▼
                                                                                                               IDLE
   mode "at":  IDLE ──(local hhmm ≥ at, once per day)──▶ check: cond → OPEN → LATCH ; else / low coverage → DONE
               LATCH/DONE ──(new local day)──▶ IDLE         INV: rule not understood by this firmware (RULE_INVALID once per rev)
```

| From | Trigger | To | Action |
|---|---|---|---|
| any | `on=false` | OFF | If it was FIRE/CLR: `info` event "<name>: rule turned off", no e-mail |
| any | semHash changed on reload | IDLE | If it was FIRE/CLR: `info` event "rule changed while active", no e-mail. Cosmetic edits (name, channels, `rc`, `sev`, `coolMin`, `maxDay`) keep the state. |
| any | unknown metric/mode | INV | One `RULE_INVALID` (sev 2) event per `alertsRev` |
| WAIT | `TIME_OK` | IDLE | Windows, `at`, `day` and `month` need local time. Inst rules without a window run on `millis()` before NTP; their events wait for a timestamp (§4.9). |
| IDLE | valid v, in window, cond(v) | PEND (t0 = ms) | If `forSec == 0`, go straight to OPEN |
| PEND | !cond(v), or left window, or no valid v for 30 s | IDLE | – |
| PEND | ms − t0 ≥ forSec·1000 | FIRE | **OPEN** (§4.6) |
| FIRE (`day`/`month` scope) | right after OPEN | LATCH (pk) | No clear or repeat until the period key changes |
| FIRE | clr(v) on valid data | CLR (t1) | – |
| CLR | !clr(v) | FIRE | No re-open |
| CLR | ms − t1 ≥ clrSec·1000 | IDLE | **CLEAR**: `clear` event with `dur`; e-mail if `clrMail` and `openMailed` |
| FIRE | repeatMin > 0 and now − lastMail ≥ max(repeatMin, coolMin)·60 | FIRE | **REPEAT** (kind `r`) "still <val> for <held>" |
| FIRE | the open was blocked by cooldown, cooldown has now passed, still FIRE | FIRE | One REPEAT, so a lasting condition is never silently lost |
| FIRE | window ended | IDLE | `info` event "window ended while active", no e-mail |
| LATCH/DONE | period key changes (local midnight, or the cycle start for `month`) | IDLE | Silent |
| IDLE (`mode:at`) | first moment with local hhmm ≥ `at`, not yet checked this period | LATCH or DONE | `lt`/`le` on energy with coverage < `minCov`: `info` event with `em:"skip"` "not checked: coverage 62 % < 80 %". A late check (device was down) is allowed until 23:59 and adds "checked late at hh:mm". |

Semantics:
- `cond(v)` is `v > value` (`gt`), `≥`, `<` or `≤`.
- `clr(v)` uses `clr` if set, else `value ∓ default hysteresis` (§4.3).
- Booleans: `cond = (v == value)`, `clr = (v != value)`.
- **Missing data:** a NaN inst value freezes PEND for up to 30 s (then IDLE) and freezes FIRE/CLR indefinitely. A rule never clears on missing data; LINK_LOST tells the user why.
- **Timing:** holds use `millis()`. Windows and period keys use local time (`tu::` helpers, UTC+6).
- **Day of week:** `dow = (localDayNumber + 4) % 7`, where 0 = Sunday (1970-01-01 was a Thursday).
- **Windows:** `from < to` means `from ≤ mm < to` with today's bit. `from > to` means `(mm ≥ from && bit(today)) || (mm < to && bit(yesterday))`.

### 4.5 Energy scopes, messages and month-to-date

**Day scope**
- The value is today's running total including the open minute (`Aggregator::todayEnergy()`), and `daily().gout` for `gout`.
- The period key is `yyyymmdd`.
- **Rollover guard:** `EnergyView.day` must equal `localDayNumber(ep)`. Otherwise the energy is treated as NaN until the aggregator has rolled. This stops yesterday's total from re-firing a fresh `gt` rule at 00:00:00.

**Month scope** (`MonthToDate`, NVS blob `energy/mtd`, about 48 B):

```cpp
struct MtdBlob { uint16_t ver; uint8_t startDay, flags; int32_t cycleDay, lastFolded;
                 float eld, egr, epv, edis, ech; uint32_t gout, nMin; uint16_t days; };
```

- `cycleStart(today, startDay)` is the local date `startDay` of this month if `today.day ≥ startDay`, else of the previous month. The period key is its `yyyymmdd`.
- `fold(finishedDay)` is idempotent: it adds a day only if `cycleDay ≤ day > lastFolded`. It is called from aggTask on `takeDayRolled(fin)`; LittleFS is the source of truth (d1 §4.4).
- **Gap fill** at TIME_OK and after a `monthStart` change: for each missing day, use `minuteQueue.recomputeDay(d)` if the file still exists (14 days are kept, d1 §5.5). Otherwise set `needBootstrap`.
- **Bootstrap** (uplink): one `GET daily.json?orderBy="$key"&startAt="<first missing>"&endAt="<yesterday>"` (≤ 5.7 KB), then fold each returned day.
- `month` rules may evaluate with an incomplete sum. `gt`/`ge` can then only fire **late, never falsely**. The message carries "(cycle data 87 %)" when coverage is below 95 %.
- **MTD value** = the cycle sums + today's running totals.

**Event `msg` formats** (≤ 200 chars; the mailer uses `msg` verbatim as the subject body):

| Kind | Format | Example |
|---|---|---|
| open | `<name>: now <val> (limit <thr><scope>)` | `Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)` |
| repeat | `<name>: still <val> for <held>` | `Load > 2500 W for 5 min: still 2,710 W for 1 h 10 min` |
| clear | `<name>: back to <val> (clear level <clr>)` | `SOC < 30 %: back to 36 % (clear level 35 %)` |
| at | `<name>: <val> at <hh:mm> (limit <thr>)[, checked late]` | `PV today < 3 kWh by 17:00: 2.41 kWh at 17:00 (limit 3.00 kWh)` |

Number formats:
- Wh ≥ 1000 is shown as kWh with 2 decimals.
- W: integer with thousands separators. V and Hz: 1 decimal. %, A and °C: integer.
- min: "h mm". Booleans: "yes"/"no".
- "est." is added for `egr` and `gw`.

### 4.6 Gates (device side) and the e-mail/day guard

Applied in this order at OPEN and REPEAT. The result goes into the event's `em`:
1. `rule.email && policy.on`. Otherwise there is no `em` field at all, and the event is still written if `ntfy` or for history.
2. **Cooldown**: `now − lastMailEp ≥ coolMin·60`, else `cool` (and a deferred REPEAT is armed).
3. **Rule cap**: `cnt < maxDay`, else `rcap`. `cnt` resets at local midnight.
4. **Device job cap** (`NotifyGates`): ≤ `MAIL_JOBS_PER_DAY` = 40 per local day. The last 5 are kept for sev ≥ 4. Over the cap gives `cap`. The counter is persisted (NVS `rules/mj`) and written once per job.
5. Pass: `em:"q"`, `cnt++`, `lastMailEp = now`, `openMailed = true`.

Other limits:
- **Clear e-mails** skip gates 2 and 3, but not 4.
- **Events per rule per day:** ≤ 20 (`evCnt`). Further transitions only update `alertState`.
- **ntfy for rules:** if `rule.ntfy`, use `NotifyGates::pushPrio(sev, clear, ep)`, which is the same quiet-hour and 40/day logic `AlertEngine::emit` has today.

The **mailer** then applies the authoritative recipient cap, quota floor, recipient allow-list and dedupe (§5).

### 4.7 Built-in alerts by e-mail

`AlertEngine::emit()` sets `em = "q"`, `kind`, and `rc = 0` (all recipients) when all of the following hold:
- `policy.on` and **one** of:
  - `st == "open"`, it is not a repeat, and `sev ≥ builtinMinSev`;
  - `st == "clear"`, `builtinClear` is set, and the matching open was e-mailed;
  - `type == "TEST_ALERT"`;
- **and** the job cap allows it.

`DAILY_SUMMARY` is never e-mailed, because the mailer sends its own summary.

### 4.8 Persistence across reboot

**Storage**
- Per slot, NVS key `rules/rsNN` (28 B `RulePersist`), written **on transitions only**: at most a few per rule per day, bounded by `maxDay` and `evCnt`.
- Rules: the raw JSON in LittleFS `/cfg/alerts.json` (≤ 8 KB), plus `rules/rev`. Both are loaded by aggTask **before** the network comes up.
- Device job counter: `rules/mj` `{day, cnt}`.

**Restore** at `setRules()` after boot:

| Saved state | Restored as |
|---|---|
| Same `semHash`, LATCH/DONE with the same `pk` | LATCH/DONE |
| `lastMailEp`, `cnt` (same day), `openMailed` | Restored |
| FIRE/CLR | FIRE **unconfirmed**: no new OPEN mail. It clears normally if the first valid samples show clr(v). |
| PEND | Not restored; the hold restarts |

**Result:** "Daily grid > 5 kWh" cannot e-mail twice in a day across reboots or boot loops, and cooldowns survive restarts. Even a duplicate event write is caught by the mailer's ledger (§5).

### 4.9 Uplink (FW-2) additions

1. **cfg poll:** parse `alertsRev`. If it differs from the loaded rev (or at boot, or after 6 h), `GET alerts.json` → `rules::parse` → `app.applyRules()` → save `/cfg/alerts.json` → `status.arv`. On a parse failure keep the old set and set `status` `ruleErr` (string ≤ 40).
2. **Events:**
   - Pop from `alertQueue`. If `ts == 0`, keep it until the time anchor gives `ts = anchorEp − (anchorUp − upMs)/1000` (d1 §5.6).
   - Build the key per §3.6 and write **one** `PATCH /devices/inv1.json?auth=…&print=silent` holding `events/<d>/<key>` and, if `em == "q"`, also `mailq/<key>` = `{d, ts, sev[, rc]}`.
   - Repeats with `em:"q"` **must** be written; other repeats may be skipped as today.
   - On a 400, or a 401 that persists after a token refresh: never loop (d1 back-off). The event is dropped after 3 tries and counted in `status.edrop`.
3. **ntfy/Telegram:** unchanged (`Notify.h`), driven by `pushPrio` and `telegram`.
4. **Minute PATCH:** add `alertState/<rid>` for `ruleEngine.takeDirty()` records, plus `status.{arv,acat,mjq,nrul}`. Add `live.gw` to the `live` PUT (`sampleToLiveJson`).
5. **MTD bootstrap GET** when `MonthToDate::needBootstrap()`.
6. **v1.1:** after a PATCH that wrote `mailq/…`, call `mailNudge()` (§1.2) if `mail/nudge` is set and the rate limits allow.

### 4.10 Nudge code (v1.1)

```cpp
// src/net/MailNudge.cpp - uplinkTask only. URL from NVS mail/nudge (secret: never logged, never echoed).
bool mailNudge(const String& url) {
  NetworkClientSecure tls;
  tls.setCACert(roots::GTS_R1_R4);                       // d1 §5.13 bundle; today WE2 -> GTS Root R4
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);                                // TWDT is 30 s
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // never follow (arduino-esp32 #8300)
  if (!http.begin(tls, url)) return false;
  esp_task_wdt_reset();
  int code = http.GET();                                 // 302 => doGet already ran
  http.end();                                            // body never read (3.3.12 getString() hang, #12990)
  esp_task_wdt_reset();
  return code == 302 || code == 200;
}
```

### 4.11 SMTP fallback (v2, later; off by default)

| Item | Spec |
|---|---|
| Enable | NVS `mail/smtpPass` = a 16-character App Password of the **dedicated sender** account (needs 2-Step Verification [CONFIRMED-primary, r8c #1]). Never the owner's main account. Write-only through the portal or `/api/config`. |
| When | Only if **all** of these hold: `mailer/hb` is older than 30 min (read by the 60 s loop with a 6-min cache); the job's event still has `em:"q"` after 30 min (re-read just before sending, r8c M2); and sev ≥ 4 or `TEST_ALERT`. |
| Transport | `smtp.gmail.com:465`, implicit TLS, `setCACert(GTS R1–R4)`. An in-house client of about 150 lines (EHLO, AUTH PLAIN, MAIL FROM, RCPT TO ≤ 3, DATA with dot-stuffing, QUIT), closing after each message. Not ESP-Mail-Client (deprecated) and not ReadyMail (single maintainer, ESP32 3.x hang notes, CC BY-NC licence) (r8c #18–19, M9). |
| Recipients | NVS `mail/smtpTo` (1–3), entered on the device. The device still cannot read the PWA's list by design. |
| Content | Plain text only: `msg`, `when`, a one-line snapshot. Subject `[Solar] <msg> (via logger)`. |
| After sending | `PATCH {events/<d>/<k>/em:"sent", mailq/<k>:null}`. No rules change is needed: the device may already write `em` through the `events` `.write` cascade, and it may delete `mailq` jobs [TESTED P4]. |
| Limits | 10/day, ≥ 60 s apart, plus a weekly self-test e-mail so that a broken fallback is noticed (r8c M9). |
| Risk accepted | NVS is unencrypted (d1 §5.13). Anyone with the box can dump an App Password that gives SMTP/IMAP access to the **sender** account, which holds no other data. Revoke it at myaccount.google.com/apppasswords. |

### 4.12 Constants and storage

```cpp
// include/config.h additions
constexpr uint8_t  MAX_RULES = 16;
constexpr uint8_t  RULE_EVENTS_PER_DAY = 20;
constexpr uint8_t  MAIL_JOBS_PER_DAY = 40, MAIL_RESERVE_SEV4 = 5;
constexpr uint32_t RULE_NAN_GRACE_MS = 30000;
constexpr uint32_t ALERTS_REFRESH_MS = 6UL * 3600UL * 1000UL;
constexpr uint16_t ALERT_CATALOG_VERSION = 1;          // status.acat
constexpr uint32_t NUDGE_MIN_GAP_MS = 60000; constexpr uint8_t NUDGE_MAX_PER_DAY = 48;   // v1.1
```

- **NVS (new):** `rules` = `rs01`…`rs16` (28 B each), `mj`, `rev`. `mail` = `nudge` (v1.1), `smtpPass`, `smtpTo` (v2). `energy` = `mtd`.
- **LittleFS:** `/cfg/alerts.json`.
- **RAM:** rules ≈ 1.6 KB, runtime state ≈ 0.6 KB. The `alerts.json` parse is a transient of ≈ 12 KB (put the ArduinoJson allocator in PSRAM). `AlertEvent` grows by about 20 B, × 24 queue slots.

### 4.13 Native tests (`pio test -e native`)

| Suite | Cases |
|---|---|
| `test_rules` | **Hold and clear:**<br>• forSec hold, clrSec clear, default hysteresis;<br>• `forSec = 0` goes straight to OPEN.<br>**Missing data:** NaN freezes PEND for ≤ 30 s, and FIRE never clears on NaN.<br>**Windows:**<br>• a window across midnight, with the days mask applied to the start day;<br>• Bangladesh Fri/Sat.<br>**Energy:**<br>• day latch, and reset at midnight;<br>• the rollover guard (yesterday's energy must not re-fire);<br>• month latch, and reset on the cycle start;<br>• `at` mode on time, late, and with low coverage (`skip`).<br>**Gates:**<br>• cooldown → `cool`, then exactly one deferred REPEAT;<br>• `repeatMin` spacing = max(repeat, cool);<br>• `maxDay` → `rcap`;<br>• job cap 40 with the sev ≥ 4 reserve;<br>• clear e-mail only when `clrMail && openMailed`;<br>• ≤ 20 events per rule per day.<br>**Edits:** semHash (cosmetic edit keeps the state; threshold edit resets it).<br>**Reboot restore:**<br>• LATCH: no second e-mail the same day;<br>• FIRE: no new OPEN;<br>• PEND: restarts.<br>**Formatting:** booleans; the exact `msg` strings of §4.5. |
| `test_rule_parse` | **Valid input:**<br>• all 9 presets (§6.3) parse;<br>• `to` as a string.<br>**Rejected or marked invalid** (each §3.11 negative case):<br>• r17;<br>• `w` 30000;<br>• `egr` day `lt` without `at`;<br>• month `lt`;<br>• `clr` on the wrong side;<br>• no channel;<br>• unknown metric → `invalid` with `err`. |
| `test_mtd` | • `cycleStart` for start days 1/5/28 across month and year ends;<br>• idempotent `fold`;<br>• gap fill;<br>• coverage %;<br>• a `monthStart` change resets the cycle. |
| `test_agg` (existing) | Extend it with: `gw` equals the `egr` integrand; `nogrid` equals `!gridPresent`. |
| `test_engine` (existing) | Built-in `em` flags per `EmailPolicy`, and `TEST_ALERT` always flagged. |

### 4.14 v1 vs later

| Item | v1 | v1.1 | v2 / later |
|---|---|---|---|
| RuleEngine (inst/day/month, hold/at, windows, gates, persistence), MTD | ✅ | | |
| Built-in alerts + TEST_ALERT by e-mail | ✅ | | |
| Deterministic event keys, `mailq`, `alertState`, `status`/`live.gw` additions | ✅ | | |
| Apps Script mailer (pull 5 min, digest, dedupe, caps, test, offline, summary) | ✅ | | |
| RTDB rules merge, PWA Alerts page | ✅ | | |
| Nudge GET from the ESP | | ✅ | |
| Gmail SMTP fallback from the ESP | | | ✅ (opt-in) |
| Quiet-hour deferral of e-mail, Bangla templates, device-health metrics (`rssi`, `q`, `rq`), Brevo path | | | later |

---

## 5. Apps Script mailer: complete source

It will live in the repo at `tools/apps-script/` (`appsscript.json`, `Code.gs`, `test/harness.js`, `test/e2e.js`). The tested copies are in the scratchpad (§11).

### 5.1 Script properties (Project Settings → Script properties)

| Property | Required | Example | Notes |
|---|---|---|---|
| `FB_API_KEY` | yes | `AIza…` | Web API key, not a secret (d1 §4.7) |
| `FB_DB_URL` | yes | `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app` | |
| `FB_EMAIL` / `FB_PASSWORD` | yes | `mailer@<anything>.invalid` / 24+ random chars | The **mailer** Auth user (§7 step 2) |
| `ALLOW_TO` | yes | `owner@gmail.com,family@gmail.com` | The allow-list. The PWA list is filtered by it. |
| `APP_URL` | no | the dashboard URL decided in [r7](r7-web-hosting.md): `https://savx-logger.<acct>.workers.dev/` (fallback host: `https://<project>.web.app/`) | Adds an "Open dashboard" button. Kept here, not in RTDB, so a hosting change needs no rules change. |
| `NUDGE_KEY` | no (v1.1) | 32 random chars | Empty = `doGet` refuses everything |
| `DEVICE` | no | `inv1` | |
| `SUBJECT_TAG` | no | `Solar` | Subject prefix `[Solar]`; keep it stable for the Gmail filter |
| `MAX_PER_DAY` | no | `40` | Can only lower the cap (it is clamped to ≤ 40) |
| `RTOK`, `STATE`, `SENT` | – | – | Written by the script: refresh token, day counters, de-dup ledger (≤ 8 KB, 72 h) |

### 5.2 `appsscript.json`

```json
{
  "timeZone": "Asia/Dhaka",
  "runtimeVersion": "V8",
  "exceptionLogging": "STACKDRIVER",
  "oauthScopes": [
    "https://www.googleapis.com/auth/script.send_mail",
    "https://www.googleapis.com/auth/script.external_request",
    "https://www.googleapis.com/auth/script.scriptapp"
  ],
  "webapp": {
    "executeAs": "USER_DEPLOYING",
    "access": "ANYONE_ANONYMOUS"
  }
}
```

### 5.3 `Code.gs`

```javascript
/**
 * Savx logger mailer (Google Apps Script, V8 runtime).
 * Owned by the DEDICATED sender Gmail account. Design: research/r8-email-alerts.md.
 *
 * Every 5 min (time trigger `poll`) and on an optional nudge from the logger (`doGet`):
 *   1. GET /devices/<DEVICE>/mailq            (usually null)
 *   2. for each queued job: GET the event, check gates, send with MailApp, then
 *      PATCH events/<d>/<k>/em = "sent" and delete mailq/<k> (one multi-path PATCH)
 *   3. logger-offline / back-online check on live/ts
 *   4. daily summary for yesterday at alerts/email/summary/hour
 *   5. PATCH /devices/<DEVICE>/mailer = heartbeat
 * RTDB access: Firebase Auth user "mailer" (email/password) -> ID token -> ?auth=<token>.
 * The rules pin MAILER_UID to: read mailq, alerts, live, daily, events, mailer; write mailq, mailer,
 * events/<d>/<k>/em|emt. Recipients come ONLY from alerts/email/to, filtered by ALLOW_TO.
 *
 * Script properties (Project Settings -> Script properties):
 *   FB_API_KEY  FB_DB_URL  FB_EMAIL  FB_PASSWORD  ALLOW_TO   (required)
 *   APP_URL  NUDGE_KEY  DEVICE (inv1)  SUBJECT_TAG (Solar)  MAX_PER_DAY (40)   (optional)
 * Written by the script: RTOK (refresh token), STATE (day counters), SENT (de-dup ledger).
 */
var VERSION = '1.0.0';
var TZ = 'Asia/Dhaka';
var DEF = { DEVICE: 'inv1', SUBJECT_TAG: 'Solar', MAX_PER_DAY: '40' };   // MAX_PER_DAY can only lower the cap
var RESERVE_SEV4 = 5;           // the last 5 recipient-deliveries of a day are kept for severity >= 4
var QUOTA_FLOOR = 10;           // MailApp quota below this: only severity >= 4
var MAX_AGE_S = 12 * 3600;      // queued alerts older than this expire (severity >= 4: twice as long)
var MAX_TRIES = 5;              // MailApp failures per job before em = "fail"
var TESTS_PER_DAY = 10;
var OFFLINE_MAILS_PER_DAY = 4;  // offline + back-online mails per local day (flapping guard)
var LEDGER_KEEP_S = 72 * 3600;
var CFG_CACHE_S = 600;          // alerts/email is cached 10 min (tests always read it fresh)
var SEVN = ['Debug', 'Info', 'Notice', 'Warning', 'Alert', 'Critical'];  // shared/codes.json "severity"
var MODES = { P: 'Power on', S: 'Standby', L: 'Line (grid)', B: 'Battery', F: 'Fault', H: 'Power saving',
              D: 'Shutdown', C: 'Charge', Y: 'Bypass', E: 'ECO' };       // shared/codes.json "qmod"

// =============================================================================================== entry points
/** Run once by hand after setting the script properties: signs in, checks access, installs the trigger. */
function setup() {
  ['FB_API_KEY', 'FB_DB_URL', 'FB_EMAIL', 'FB_PASSWORD', 'ALLOW_TO'].forEach(need_);
  ScriptApp.requireAllScopes(ScriptApp.AuthMode.FULL);   // consent for the 3 manifest scopes
  CacheService.getScriptCache().remove('idt');
  SP_().deleteProperty('RTOK');
  token_(true);                                   // throws if the mailer user cannot sign in
  db_('get', 'mailq');                            // throws "HTTP 401" if MAILER_UID is not in the rules yet
  ScriptApp.getProjectTriggers().forEach(function (t) {
    if (t.getHandlerFunction() === 'poll') ScriptApp.deleteTrigger(t);
  });
  ScriptApp.newTrigger('poll').timeBased().everyMinutes(5).create();
  var r = run_('setup');
  Logger.log('OK: signed in to RTDB, trigger "poll" installed (every 5 min), first run sent ' + r.sent +
             ' recipient-deliveries. MailApp quota left today: ' + MailApp.getRemainingDailyQuota());
}

/** Optional manual check of MailApp alone (no RTDB): one mail to the first ALLOW_TO address. */
function sendSelfTest() {
  var to = list_(need_('ALLOW_TO'))[0];
  MailApp.sendEmail({ to: to, subject: tag_() + 'Mailer self-test', name: 'Solar logger',
                      body: 'MailApp works from this account (' + VERSION + '). Quota left today: ' +
                            MailApp.getRemainingDailyQuota() });
  Logger.log('Sent to ' + to);
}

/** Removes the time trigger (stops the mailer). */
function uninstall() {
  ScriptApp.getProjectTriggers().forEach(function (t) { ScriptApp.deleteTrigger(t); });
  Logger.log('All triggers removed.');
}

/** Time trigger, every 5 min. */
function poll() {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(20000)) return;               // another run (nudge) is still busy
  try { run_('timer'); } finally { lock.releaseLock(); }
}

/** Optional nudge from the logger: GET <exec-url>?k=<NUDGE_KEY>. No payload; the data is already in RTDB. */
function doGet(e) {
  var key = SP_().getProperty('NUDGE_KEY') || '';
  var k = (e && e.parameter && e.parameter.k) || '';
  if (key.length < 24 || !safeEq_(String(k), key)) return out_({ ok: false, err: 'auth' });
  var cache = CacheService.getScriptCache();
  if (cache.get('nudge')) return out_({ ok: true, skipped: 'rate' });
  cache.put('nudge', '1', 20);                    // at most one nudge-run per 20 s
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(2000)) return out_({ ok: true, skipped: 'busy' });
  try {
    var r = run_('nudge');
    return out_({ ok: true, sent: r.sent });
  } catch (err) {
    return out_({ ok: false, err: clip_(err && err.message, 100) });
  } finally {
    lock.releaseLock();
  }
}

// =============================================================================================== one run
function run_(why) {
  var now = nowS_(), today = dkey_(now);
  var st = state_(today), ledger = ledger_(now);
  var ctx = { now: now, today: today, st: st, ledger: ledger, cfg: null, live: undefined, warn: '', last: null };
  var res = { sent: 0 }, errText = '', sent0 = st.sent;
  try {
    var q = db_('get', 'mailq') || {};
    if (q.test) doTest_(ctx, q.test);
    delete q.test;
    var keys = Object.keys(q).sort(function (a, b) { return ((q[a] || {}).ts || 0) - ((q[b] || {}).ts || 0); });
    var groups = {}, order = [];
    keys.forEach(function (k) {
      var it = q[k] || {};
      if (ledger[k]) { finish_(ctx, it.d, k, 'sent'); return; }       // device re-wrote an already sent job
      var ev = it.d ? db_('get', 'events/' + it.d + '/' + k) : null;
      if (!ev) { db_('delete', 'mailq/' + k); return; }               // event gone (pruned / never written)
      if (ev.em && ev.em !== 'q') { db_('delete', 'mailq/' + k); return; }
      var cf = cfg_(ctx);
      var sev = Number(ev.sev) || 0;
      if (!cf.on) { finish_(ctx, it.d, k, 'off'); return; }
      if (now - (Number(ev.ts) || now) > (sev >= 4 ? 2 : 1) * MAX_AGE_S) { finish_(ctx, it.d, k, 'exp'); return; }
      var r = recipients_(ctx, cf.to, it.rc);
      if (!r.length) { finish_(ctx, it.d, k, 'norc'); return; }
      var g = r.join(',');
      if (!groups[g]) { groups[g] = []; order.push(g); }
      groups[g].push({ k: k, d: it.d, ev: ev, sev: sev, n: Number(it.n) || 0 });
    });
    order.forEach(function (g) {
      var list = groups[g], to = g.split(','), cf = cfg_(ctx);
      var sevMax = Math.max.apply(null, list.map(function (x) { return x.sev; }));
      var gate = budget_(ctx, sevMax, to.length);
      if (gate) {
        list.forEach(function (x) { finish_(ctx, x.d, x.k, gate); });
        if (gate === 'cap') { try { capNotice_(ctx); } catch (err) { errText = 'MailApp: ' + clip_(err && err.message, 90); } }
        return;
      }
      var m = list.length === 1 ? eventMail_(ctx, list[0]) : digestMail_(ctx, list);
      try {
        send_(ctx, to, m);
      } catch (err) {
        errText = 'MailApp: ' + clip_(err && err.message, 90);
        list.forEach(function (x) { retry_(ctx, x); });
        return;
      }
      list.forEach(function (x) { ctx.ledger[x.k] = now; });
      saveLedger_(ctx.ledger);                                         // before the RTDB write: no re-send
      list.forEach(function (x) { finish_(ctx, x.d, x.k, 'sent'); });
    });
    [offline_, summary_].forEach(function (f) {
      try { f(ctx); } catch (err) { errText = clip_(err && err.message, 110); }   // keep the run going
    });
  } catch (err) {
    errText = clip_(err && err.message, 110) || 'error';
    throw err;                                    // Apps Script then e-mails the owner a failure summary
  } finally {
    res.sent = st.sent - sent0;
    saveState_(st);
    saveLedger_(ctx.ledger);
    try {
      var hb = { hb: now, ver: VERSION, day: today, sent: st.sent, quota: MailApp.getRemainingDailyQuota(),
                 off: st.offSince || 0, err: errText, warn: ctx.warn };
      if (ctx.last) hb.last = ctx.last;
      db_('patch', 'mailer', hb);
    } catch (e2) { /* RTDB unreachable: the PWA shows the heartbeat as stale */ }
  }
  return res;
}

// =============================================================================================== jobs
function finish_(ctx, d, k, em) {
  var p = {};
  p['mailq/' + k] = null;
  if (d) { p['events/' + d + '/' + k + '/em'] = em; p['events/' + d + '/' + k + '/emt'] = ctx.now; }
  try { db_('patch', '', p); }
  catch (err) { db_('delete', 'mailq/' + k); }    // e.g. the event no longer exists -> rules refuse em
}

function retry_(ctx, x) {
  var n = x.n + 1;
  if (n >= MAX_TRIES) finish_(ctx, x.d, x.k, 'fail');
  else db_('put', 'mailq/' + x.k + '/n', n);
}

/** '' = may send; else the em code. Daily cap counts recipient-deliveries per local day. */
function budget_(ctx, sev, n) {
  var cap = cfg_(ctx).maxPerDay;
  var limit = sev >= 4 ? cap : cap - RESERVE_SEV4;
  if (ctx.st.sent + n > limit) return 'cap';
  var quota = MailApp.getRemainingDailyQuota();
  if (quota < n || (sev < 4 && quota < QUOTA_FLOOR)) return 'quota';
  return '';
}

function capNotice_(ctx) {
  if (ctx.st.capNote) return;
  var cf = cfg_(ctx), to = recipients_(ctx, cf.to, 1);
  if (!to.length || budget_(ctx, 4, 1)) return;
  ctx.st.capNote = 1;
  send_(ctx, to, layout_(ctx, 'Daily e-mail limit reached',
    ['The logger reached today\'s limit of ' + cf.maxPerDay + ' alert e-mails.',
     'Until midnight only Alert/Critical (severity 4-5) e-mails are sent. Everything is still listed in the dashboard (Alerts > History).',
     'Raise or lower the limit in Alerts > E-mail; the hard ceiling is ' + Number(prop_('MAX_PER_DAY')) + ' (Apps Script allows 100 recipients per day).'],
    null, 'Daily e-mail limit reached'));
}

function send_(ctx, to, m) {
  var cf = cfg_(ctx);
  MailApp.sendEmail({ to: to.join(','), subject: m.subject, body: m.text, htmlBody: m.html,
                      name: cf.fromName, replyTo: to[0] });
  ctx.st.sent += to.length;
  ctx.last = { ts: ctx.now, ok: true, msg: clip_(m.subject, 100) };
}

// =============================================================================================== test, offline, summary
function doTest_(ctx, t) {
  var ok = false, msg = '', now = ctx.now, st = ctx.st;
  if (!t || !(Number(t.ts) > 0)) msg = 'bad request';
  else if (now - t.ts > 900) msg = 'expired: requested ' + dur_(now - t.ts) + ' ago';
  else if (st.lastTest && now - st.lastTest < 60) msg = 'rate limited: one test per minute';
  else if (st.tests >= TESTS_PER_DAY) msg = 'rate limited: ' + TESTS_PER_DAY + ' tests per day';
  else {
    ctx.cfg = null;                                // always fresh recipients for a test
    var cf = cfg_(ctx, true), rule = null;
    if (t.rid) rule = db_('get', 'alerts/rules/' + t.rid);
    var to = recipients_(ctx, cf.to, rule && rule.rc);
    var gate = to.length ? budget_(ctx, 3, to.length) : 'norc';
    if (gate === 'norc') msg = 'no recipients: add one in Alerts > E-mail and to ALLOW_TO in the mailer';
    else if (gate) msg = gate === 'cap' ? 'daily e-mail limit reached' : 'Google sending quota is low';
    else {
      send_(ctx, to, testMail_(ctx, rule, t));
      st.tests++; st.lastTest = now; ok = true;
      msg = 'sent to ' + to.length + ' recipient' + (to.length > 1 ? 's' : '') + (cf.on ? '' : ' (alert e-mails are switched OFF)');
    }
  }
  db_('patch', '', { 'mailq/test': null, 'mailer/test': { ts: Number(t && t.ts) || now, ok: ok, msg: clip_(msg, 100) } });
}

function offline_(ctx) {
  var cf = cfg_(ctx), st = ctx.st, now = ctx.now;
  if (!cf.on || !cf.offline.on) { st.offSince = 0; return; }
  var ts = ctx.live ? Number(ctx.live.ts) : Number(db_('get', 'live/ts'));
  if (!(ts > 0)) return;                                               // never seen: nothing to compare
  var age = now - ts;
  if (age > cf.offline.min * 60 && !st.offSince) {
    st.offSince = ts;
    var to = recipients_(ctx, cf.to, 0);
    if (to.length && st.offMails < OFFLINE_MAILS_PER_DAY && !budget_(ctx, 4, to.length)) {
      st.offMails++;
      send_(ctx, to, layout_(ctx, 'Logger offline since ' + hhmm_(ts),
        ['No data from the logger since ' + when_(ts) + ' (' + dur_(age) + ' ago).',
         'Likely causes: power cut with the router off (load-shedding), home internet down, Wi-Fi changed, or the logger crashed.',
         'Data is still recorded locally while the logger has power and is uploaded when it reconnects.',
         cf.offline.clear ? 'You will get one e-mail when it is back.' : ''],
        null, 'ALERT: Logger offline since ' + hhmm_(ts)));
    }
  } else if (st.offSince && age < 300) {
    var down = ts - st.offSince;
    st.offSince = 0;
    var to2 = recipients_(ctx, cf.to, 0);
    if (cf.offline.clear && to2.length && st.offMails < OFFLINE_MAILS_PER_DAY && !budget_(ctx, 4, to2.length)) {
      st.offMails++;
      send_(ctx, to2, layout_(ctx, 'Logger back online',
        ['The logger is sending data again (' + when_(ts) + '). It was silent for about ' + dur_(down) + '.',
         'Any alerts from that time arrive separately, marked "Delayed".'], null, 'Logger back online after ' + dur_(down)));
    }
  }
}

function summary_(ctx) {
  var cf = cfg_(ctx), st = ctx.st, now = ctx.now;
  if (!cf.on || !cf.summary.on || st.sumDay === ctx.today || localHour_(now) < cf.summary.hour) return;
  var y = addDays_(ctx.today, -1), y2 = addDays_(ctx.today, -2), cs = cycleStart_(y, cf.monthStart);
  var from = cs < y2 ? cs : y2;
  var days = db_('get', 'daily', undefined, '&orderBy=' + enc_('"$key"') + '&startAt=' + enc_('"' + from + '"') +
                 '&endAt=' + enc_('"' + y + '"')) || {};
  var evs = db_('get', 'events/' + y) || {};
  var to = recipients_(ctx, cf.to, 0);
  st.sumDay = ctx.today;                                               // once per day, even if refused below
  if (!to.length || budget_(ctx, 1, to.length)) return;
  send_(ctx, to, summaryMail_(ctx, y, days[y], days[y2], mtd_(days, cs, y), evs));
}

// =============================================================================================== e-mail content
function eventMail_(ctx, x) {
  var ev = x.ev, kind = x.k.slice(-1), msg = String(ev.msg || ev.type || 'Alert');
  var pre = kind === 'c' ? 'Cleared: ' : (kind === 'r' && msg.indexOf('Still active') !== 0) ? 'Still active: ' :
            x.sev >= 5 ? 'CRITICAL: ' : x.sev >= 4 ? 'ALERT: ' : '';
  var lines = ['When: ' + when_(ev.ts) + ' (Asia/Dhaka)',
               'Severity: ' + (SEVN[x.sev] || x.sev) + (ev.rid ? ' | rule ' + ev.rid : ' | ' + ev.type)];
  if (num_(ev.dur) && ev.dur > 0) lines.push('It lasted ' + dur_(ev.dur) + '.');
  var delay = ctx.now - (Number(ev.ts) || ctx.now);
  if (delay > 600) lines.push('Delayed ' + dur_(delay) + ': the logger or the internet was offline.');
  return layout_(ctx, msg, lines, live_(ctx), pre + msg);
}

function digestMail_(ctx, list) {
  var names = list.map(function (x) { return String(x.ev.msg || x.ev.type); });
  var lines = [];
  list.forEach(function (x) {
    var k = x.k.slice(-1);
    lines.push((k === 'c' ? 'Cleared: ' : k === 'r' ? 'Still active: ' : '') + (x.ev.msg || x.ev.type) +
               '  (' + hhmm_(x.ev.ts) + ', ' + (SEVN[x.sev] || x.sev) + ')');
  });
  var oldest = Math.min.apply(null, list.map(function (x) { return Number(x.ev.ts) || ctx.now; }));
  if (ctx.now - oldest > 600) lines.push('Some of these are delayed up to ' + dur_(ctx.now - oldest) + ' (logger or internet offline).');
  return layout_(ctx, list.length + ' alerts', lines, live_(ctx), list.length + ' alerts: ' + names.join('; '));
}

function testMail_(ctx, rule, t) {
  var cf = cfg_(ctx), to = recipients_(ctx, cf.to, rule && rule.rc);
  var lines = ['This is a test e-mail from your solar logger mailer, requested from the dashboard at ' + hhmm_(t.ts) + '.',
               'If you can read this, alert e-mails reach you.',
               'Recipients: ' + to.join(', ') + '.',
               'Alert e-mails are ' + (cf.on ? 'ON' : 'OFF') + '. E-mails sent today: ' + ctx.st.sent + ' of ' + cf.maxPerDay + '.'];
  var subj = 'Test e-mail - alerts are working';
  if (rule) {
    subj = '[TEST] ' + clip_(rule.name, 60);
    lines.unshift('Rule ' + t.rid + ': "' + rule.name + '" (' + (rule.on ? 'on' : 'off') + '). A real alert from this rule looks like this one.');
  }
  return layout_(ctx, rule ? 'Test of rule: ' + rule.name : 'Test e-mail', lines, live_(ctx), subj);
}

function summaryMail_(ctx, y, d, d0, mtd, evs) {
  var rows = [['', 'Yesterday', 'Day before', 'This cycle (' + mtd.days + ' d)']];
  function row(label, f) { rows.push([label, f(d), f(d0), f(mtd.sum)]); }
  function e(k) { return function (o) { return o && num_(o[k]) ? kwh_(o[k]) : '-'; }; }
  row('Solar (logger)', e('epv'));
  if (d && num_(d.epvi) && d.epvi >= 0) row('Solar (inverter)', e('epvi'));
  row('Load', e('eld'));
  row('Grid (est.)', e('egr'));
  row('Battery charged', e('ech'));
  row('Battery discharged', e('edis'));
  row('Grid outage', function (o) { return o && num_(o.gout) ? o.gout + ' min' : '-'; });
  var counts = {};
  Object.keys(evs || {}).forEach(function (k) {
    var v = evs[k];
    if (v && v.st === 'open') { var n = v.rid ? v.rid : v.type; counts[n] = (counts[n] || 0) + 1; }
  });
  var lines = [];
  if (!d) lines.push('No data for ' + y + ': the logger was offline all day, or it has not uploaded yet.');
  else {
    if (num_(d.eld) && d.eld > 0 && num_(d.egr)) lines.push('Solar share of load (est.): ' + Math.max(0, Math.round(100 * (1 - d.egr / d.eld))) + ' %');
    lines.push('Peak load / peak solar: ' + (num_(d.wx) ? n0_(d.wx) + ' W' : '-') + ' / ' + (num_(d.pvx) ? n0_(d.pvx) + ' W' : '-'));
    lines.push('Battery V min / max: ' + (num_(d.bvn) ? d.bvn : '-') + ' / ' + (num_(d.bvx) ? d.bvx : '-') + ' V; SOC min / max: ' +
               (num_(d.socn) ? d.socn : '-') + ' / ' + (num_(d.socx) ? d.socx : '-') + ' %');
    lines.push('Data coverage: ' + (num_(d.n) ? Math.round(d.n / 14.4) + ' % (' + d.n + ' of 1440 minutes)' : '-'));
  }
  var alerts = Object.keys(counts).map(function (n) { return n + ' x' + counts[n]; });
  lines.push('Alerts: ' + (alerts.length ? alerts.join(', ') : 'none'));
  lines.push('Cycle = since ' + mtd.from + ' (billing day ' + cfg_(ctx).monthStart + '), ' + mtd.days + ' day(s) with data.');
  var subj = fmtDay_(y) + ': PV ' + e('epv')(d) + ' | Load ' + e('eld')(d) + ' | Grid ' + e('egr')(d) + ' est.';
  return layout_(ctx, 'Your solar day - ' + fmtDay_(y), lines, null, subj, rows, '#/energy');
}

/** Builds {subject, text, html}. rows: [[label, value]] snapshot; table: [[c0, c1, ...]] (summary). */
function layout_(ctx, title, lines, rows, subject, table, route) {
  lines = (lines || []).filter(function (s) { return s; });
  var app = prop_('APP_URL'), link = /^https:\/\//.test(app) ? app.replace(/\/?$/, '/') + (route || '#/alerts') : '';
  var foot = 'Sent by the solar logger mailer (' + prop_('DEVICE') + '). You get this because the owner added you to the alert list. ' +
             'To stop, ask the owner to remove you (Alerts > E-mail) or to turn the rule off. Grid figures are estimates from the inverter\'s readings.';
  var t = [title, ''];
  var h = '<div style="max-width:560px;margin:0 auto;font:15px/1.45 -apple-system,Segoe UI,Roboto,Arial,sans-serif;color:#1a1a1a">' +
          '<p style="margin:0 0 4px;font-size:12px;color:#666">' + esc_(prop_('SUBJECT_TAG')) + ' &middot; ' + esc_(prop_('DEVICE')) + '</p>' +
          '<h1 style="margin:0 0 10px;font-size:20px">' + esc_(title) + '</h1>';
  if (table) {
    h += '<table role="presentation" style="border-collapse:collapse;font-size:14px;margin:8px 0 14px">';
    table.forEach(function (r, i) {
      t.push(pad_(r[0], 20) + r.slice(1).map(function (c) { return pad_(c, 14); }).join(''));
      h += '<tr>' + r.map(function (c, j) {
        return '<' + (i ? 'td' : 'th') + ' style="padding:3px 12px 3px 0;text-align:' + (j ? 'right' : 'left') +
               ';color:' + (j ? '#1a1a1a' : '#666') + '">' + esc_(c) + '</' + (i ? 'td' : 'th') + '>';
      }).join('') + '</tr>';
    });
    h += '</table>';
    t.push('');
  }
  t = t.concat(lines);
  h += lines.map(function (s) { return '<p style="margin:0 0 8px">' + esc_(s) + '</p>'; }).join('');
  if (rows) {
    t.push('', 'Right now:');
    h += '<h2 style="margin:16px 0 6px;font-size:15px">Right now</h2><table role="presentation" style="border-collapse:collapse;font-size:14px">';
    rows.forEach(function (r) {
      t.push('  ' + pad_(r[0], 10) + r[1]);
      h += '<tr><td style="padding:2px 12px 2px 0;color:#666">' + esc_(r[0]) + '</td><td>' + esc_(r[1]) + '</td></tr>';
    });
    h += '</table>';
  }
  if (link) {
    t.push('', 'Dashboard: ' + link);
    h += '<p style="margin:18px 0"><a href="' + esc_(link) + '" style="display:inline-block;padding:10px 16px;background:#0b6bcb;color:#fff;text-decoration:none;border-radius:8px">Open dashboard</a></p>';
  }
  t.push('', '--', foot);
  h += '<p style="margin:16px 0 0;font-size:12px;color:#666">' + esc_(foot) + '</p></div>';
  return { subject: clip_(tag_() + subject, 120), text: t.join('\n'), html: h };
}

function live_(ctx) {
  if (ctx.live === undefined) ctx.live = db_('get', 'live');
  var l = ctx.live;
  if (!l) return [['Logger', 'no live data']];
  var bw = l.bw, bwWord = !num_(bw) ? '-' : bw >= 0 ? 'charging ' + n0_(bw) + ' W' : 'discharging ' + n0_(-bw) + ' W';
  return [
    ['Battery', (num_(l.soc) ? l.soc + ' %' : '-') + ' / ' + (num_(l.bv) ? l.bv.toFixed(1) + ' V' : '-') + ' / ' + bwWord],
    ['Solar', (num_(l.pvw) ? n0_(l.pvw) + ' W' : '-') + ', today ' + (num_(l.epv) ? kwh_(l.epv) : '-')],
    ['Load', (num_(l.w) ? n0_(l.w) + ' W' : '-') + ', today ' + (num_(l.eld) ? kwh_(l.eld) : '-')],
    ['Grid', (num_(l.gv) ? Math.round(l.gv) + ' V' : '-') + ', today ' + (num_(l.egr) ? kwh_(l.egr) : '-') + ' (est.)'],
    ['Mode', MODES[l.md] || l.md || '-'],
    ['As of', num_(l.ts) ? when_(l.ts) : '-']];
}

// =============================================================================================== config, recipients, state
function cfg_(ctx, fresh) {
  if (ctx.cfg) return ctx.cfg;
  var cache = CacheService.getScriptCache(), raw = fresh ? null : cache.get('mcfg'), c;
  if (raw) c = JSON.parse(raw);
  else { c = db_('get', 'alerts/email') || {}; cache.put('mcfg', JSON.stringify(c), CFG_CACHE_S); }
  var s = c.summary || {}, o = c.offline || {};
  var hard = Math.min(Number(prop_('MAX_PER_DAY')) || 40, 40);     // 2 x 40 < 100 recipients per rolling 24 h
  ctx.cfg = {
    on: c.on === true,
    to: String(c.to || ''),
    fromName: clip_(String(c.fromName || 'Solar logger').replace(/[\r\n\t]/g, ' '), 40),
    maxPerDay: Math.min(hard, num_(c.maxPerDay) ? c.maxPerDay : hard),
    summary: { on: s.on === true, hour: num_(s.hour) ? s.hour : 7 },
    offline: { on: o.on !== false, min: num_(o.min) ? o.min : 30, clear: o.clear !== false },
    monthStart: num_(c.monthStart) ? c.monthStart : 1
  };
  return ctx.cfg;
}

/** alerts/email/to (max 3) filtered by bit mask rc (0/absent = all) and by the ALLOW_TO script property. */
function recipients_(ctx, toStr, rc) {
  var allow = list_(prop_('ALLOW_TO')).map(function (a) { return a.toLowerCase(); });
  var out = [], bad = [];
  list_(toStr).slice(0, 3).forEach(function (a, i) {
    if (rc && !(rc & (1 << i))) return;
    (allow.indexOf(a.toLowerCase()) >= 0 ? out : bad).push(a);
  });
  if (bad.length) ctx.warn = clip_('Not on ALLOW_TO: ' + bad.join(', '), 120);
  return out;
}

function state_(today) {
  var s = {};
  try { s = JSON.parse(SP_().getProperty('STATE') || '{}'); } catch (e) { s = {}; }
  if (s.day !== today) { s.day = today; s.sent = 0; s.capNote = 0; s.tests = 0; s.offMails = 0; }
  return s;
}
function saveState_(s) { SP_().setProperty('STATE', JSON.stringify(s)); }

function ledger_(now) {
  var l = {};
  try { l = JSON.parse(SP_().getProperty('SENT') || '{}'); } catch (e) { l = {}; }
  Object.keys(l).forEach(function (k) { if (now - l[k] > LEDGER_KEEP_S) delete l[k]; });
  return l;
}
function saveLedger_(l) {
  var keys = Object.keys(l).sort(function (a, b) { return l[a] - l[b]; });
  while (keys.length && JSON.stringify(l).length > 8000) delete l[keys.shift()];   // 9 KB per property
  SP_().setProperty('SENT', JSON.stringify(l));
}

function mtd_(days, cs, y) {
  var sum = { epv: 0, eld: 0, egr: 0, ech: 0, edis: 0, gout: 0 }, n = 0;
  Object.keys(days).forEach(function (k) {
    if (k < cs || k > y || !days[k]) return;
    n++;
    Object.keys(sum).forEach(function (f) { if (num_(days[k][f])) sum[f] += days[k][f]; });
  });
  return { sum: sum, days: n, from: cs };
}

// =============================================================================================== Firebase
function token_(force) {
  var cache = CacheService.getScriptCache();
  if (!force) { var c = cache.get('idt'); if (c) return c; }
  var key = enc_(need_('FB_API_KEY')), tok = null, rt = SP_().getProperty('RTOK');
  if (rt) {
    var r = UrlFetchApp.fetch('https://securetoken.googleapis.com/v1/token?key=' + key, {
      method: 'post', contentType: 'application/x-www-form-urlencoded', muteHttpExceptions: true,
      payload: 'grant_type=refresh_token&refresh_token=' + enc_(rt) });
    if (r.getResponseCode() === 200) {
      var j = JSON.parse(r.getContentText());
      tok = j.id_token;
      if (j.refresh_token && j.refresh_token !== rt) SP_().setProperty('RTOK', j.refresh_token);
    }
  }
  if (!tok) {
    var r2 = UrlFetchApp.fetch('https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=' + key, {
      method: 'post', contentType: 'application/json', muteHttpExceptions: true,
      payload: JSON.stringify({ email: need_('FB_EMAIL'), password: need_('FB_PASSWORD'), returnSecureToken: true }) });
    if (r2.getResponseCode() !== 200) throw new Error('Firebase sign-in failed: HTTP ' + r2.getResponseCode());
    var j2 = JSON.parse(r2.getContentText());
    tok = j2.idToken;
    SP_().setProperty('RTOK', j2.refreshToken);
  }
  cache.put('idt', tok, 3000);                    // ID tokens live 3600 s
  return tok;
}

/** RTDB REST under /devices/<DEVICE>. GET returns parsed JSON; writes use print=silent. */
function db_(method, path, body, query) {
  var base = need_('FB_DB_URL').replace(/\/+$/, '') + '/devices/' + prop_('DEVICE');
  for (var attempt = 0; attempt < 2; attempt++) {
    var url = base + (path ? '/' + path : '') + '.json?auth=' + enc_(token_(attempt > 0)) +
              (method === 'get' ? '' : '&print=silent') + (query || '');
    var opt = { method: method, muteHttpExceptions: true };
    if (body !== undefined) { opt.contentType = 'application/json'; opt.payload = JSON.stringify(body); }
    var r = UrlFetchApp.fetch(url, opt), c = r.getResponseCode();
    if (c === 200) return method === 'get' ? JSON.parse(r.getContentText()) : true;
    if (c === 204) return true;
    if (c === 401 && attempt === 0) continue;     // expired token: retry once; a second 401 = refused by the rules
    var e = new Error('RTDB ' + method.toUpperCase() + ' /' + (path || '') + ' -> HTTP ' + c + ' ' + clip_(r.getContentText(), 60));
    e.code = c;
    throw e;
  }
}

// =============================================================================================== helpers
function SP_() { return PropertiesService.getScriptProperties(); }
function prop_(k) { var v = SP_().getProperty(k); return v === null || v === '' ? (DEF[k] !== undefined ? DEF[k] : '') : String(v).trim(); }
function need_(k) { var v = prop_(k); if (!v) throw new Error('Script property ' + k + ' is not set'); return v; }
function tag_() { return '[' + prop_('SUBJECT_TAG') + '] '; }
function list_(s) { return String(s || '').split(',').map(function (a) { return a.trim(); }).filter(function (a) { return a; }); }
function enc_(s) { return encodeURIComponent(s); }
function num_(v) { return typeof v === 'number' && isFinite(v); }
function clip_(s, n) { s = String(s === undefined || s === null ? '' : s).replace(/[\r\n]+/g, ' '); return s.length > n ? s.slice(0, n - 1) + '…' : s; }
function pad_(s, n) { s = String(s); return s.length >= n ? s + ' ' : s + new Array(n - s.length + 1).join(' '); }
function n0_(v) { return String(Math.round(v)).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
function kwh_(wh) { return (wh / 1000).toFixed(2) + ' kWh'; }
function esc_(s) { return String(s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
function out_(o) { return ContentService.createTextOutput(JSON.stringify(o)).setMimeType(ContentService.MimeType.JSON); }
function safeEq_(a, b) { if (a.length !== b.length) return false; var d = 0; for (var i = 0; i < a.length; i++) d |= a.charCodeAt(i) ^ b.charCodeAt(i); return d === 0; }
function nowS_() { return Math.floor(Date.now() / 1000); }
function fmt_(s, p) { return Utilities.formatDate(new Date(s * 1000), TZ, p); }
function dkey_(s) { return fmt_(s, 'yyyy-MM-dd'); }
function hhmm_(s) { return fmt_(s, 'HH:mm'); }
function when_(s) { return fmt_(s, 'EEE d MMM yyyy, HH:mm'); }
function localHour_(s) { return Number(fmt_(s, 'H')); }
function fmtDay_(dkey) { var p = dkey.split('-').map(Number); return Utilities.formatDate(new Date(Date.UTC(p[0], p[1] - 1, p[2], 12)), 'UTC', 'EEE d MMM'); }
function addDays_(dkey, n) { var p = dkey.split('-').map(Number); return Utilities.formatDate(new Date(Date.UTC(p[0], p[1] - 1, p[2]) + n * 86400000), 'UTC', 'yyyy-MM-dd'); }
function cycleStart_(dkey, sd) {
  var p = dkey.split('-').map(Number), y = p[0], m = p[1];
  if (p[2] < sd) { m--; if (m < 1) { m = 12; y--; } }
  return y + '-' + (m < 10 ? '0' : '') + m + '-' + (sd < 10 ? '0' : '') + sd;
}
function dur_(s) {
  s = Math.max(0, Math.round(s));
  if (s >= 86400) return Math.floor(s / 86400) + ' d ' + Math.floor((s % 86400) / 3600) + ' h';
  if (s >= 3600) return Math.floor(s / 3600) + ' h ' + ('0' + Math.floor((s % 3600) / 60)).slice(-2) + ' min';
  if (s >= 60) return Math.floor(s / 60) + ' min';
  return s + ' s';
}
```

### 5.4 Behaviour summary

| Concern | Implementation |
|---|---|
| Concurrency | `LockService.getScriptLock()`. The trigger waits up to 20 s; a nudge waits 2 s, otherwise it answers `busy`. |
| De-dup | Ledger `SENT {key: ts}` in Script Properties, saved **before** the RTDB write. A re-queued key restores `em:"sent"` and drops the job [TESTED]. |
| Rate limits | • ≤ 40 recipient-deliveries per local day, with 5 reserved for sev ≥ 4.<br>• One cap-notice e-mail per day.<br>• `getRemainingDailyQuota()` < 10 means sev ≥ 4 only.<br>• Tests: ≥ 60 s apart, ≤ 10/day.<br>• Offline/online e-mails: ≤ 4/day.<br>• Nudge-runs: ≤ 1 per 20 s. |
| Digest | Jobs found in one run for the same recipient set become one e-mail ("N alerts: …") [TESTED]. |
| Expiry | Jobs older than 12 h (sev ≥ 4: 24 h) get `exp` and are not e-mailed. |
| Failures | A MailApp exception increments the job's `n`; at 5 it becomes `fail`. The run continues, and `mailer.err` is set. RTDB/auth errors re-throw, so Apps Script e-mails the sender account a failure summary. The heartbeat is always attempted. |
| Templates | HTML (inline CSS, escaped values) plus plain text, with a "Right now" snapshot from `live`, a "Delayed …" line, and a footer explaining why the recipient gets the mail and how to stop it. The subject has CR/LF stripped and is ≤ 120 chars. |

Sample rendered alert, plain text, printed by `mailer/sample.js` through the real `Code.gs` [TESTED]:

```
SUBJECT: [Solar] Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)
Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)

When: Wed 21 Oct 2026, 18:42 (Asia/Dhaka)
Severity: Warning | rule r03

Right now:
  Battery   78 % / 26.4 V / charging 317 W
  Solar     465 W, today 1.92 kWh
  Load      512 W, today 2.08 kWh
  Grid      231 V, today 0.33 kWh (est.)
  Mode      Line (grid)
  As of     Wed 21 Oct 2026, 18:42

Dashboard: https://savx.example.workers.dev/#/alerts

--
Sent by the solar logger mailer (inv1). You get this because the owner added you to the alert list. To stop, ask the owner to remove you (Alerts > E-mail) or to turn the rule off. Grid figures are estimates from the inverter's readings.

```

Sample daily summary, same source [TESTED]:

```
SUBJECT: [Solar] Tue 20 Oct: PV 12.84 kWh | Load 9.71 kWh | Grid 1.22 kWh est.
Your solar day - Tue 20 Oct

                    Yesterday     Day before    This cycle (2 d)
Solar (logger)      12.84 kWh     11.90 kWh     24.74 kWh
Solar (inverter)    12.70 kWh     -             -
Load                9.71 kWh      9.20 kWh      18.91 kWh
Grid (est.)         1.22 kWh      0.95 kWh      2.17 kWh
Battery charged     4.10 kWh      3.90 kWh      8.00 kWh
Battery discharged  3.05 kWh      2.80 kWh      5.85 kWh
Grid outage         42 min        0 min         42 min

Solar share of load (est.): 87 %
Peak load / peak solar: 1,840 W / 2,410 W
Battery V min / max: 24.9 / 28.6 V; SOC min / max: 31 / 100 %
Data coverage: 100 % (1440 of 1440 minutes)
Alerts: r03 x1, GRID_LOSS x1
Cycle = since 2026-10-01 (billing day 1), 2 day(s) with data.

Dashboard: https://savx.example.workers.dev/#/energy

--
Sent by the solar logger mailer (inv1). You get this because the owner added you to the alert list. To stop, ask the owner to remove you (Alerts > E-mail) or to turn the rule off. Grid figures are estimates from the inverter's readings.
```

---

## 6. Web changes (spec)

### 6.1 Navigation

- Rename the **Events** tab to **Alerts**. The route is `#/alerts`, and `#/events` stays as an alias.
- A segmented control at the top: **Rules | History | E-mail**. The current Events list becomes **History**, with its ack, filters and legends kept.
- The bottom bar keeps six items.

### 6.2 Types (`web/src/data/types.ts`) and DataSource (`source.ts`, `firebaseSource.ts`, `demoSource.ts`)

```ts
export type MetricId = 'w'|'pvw'|'gw'|'bdw'|'bw'|'soc'|'ld'|'bv'|'gv'|'ov'|'pvv'|'bdi'|'tmp'|'gf'|'onbat'|'nogrid'
                     | 'eld'|'egr'|'epv'|'edis'|'ech'|'gout';
export interface AlertRule { name: string; on: boolean; metric: MetricId; scope: 'inst'|'day'|'month';
  op: 'gt'|'ge'|'lt'|'le'; value: number; mode: 'hold'|'at'; at?: string; forSec?: number; clr?: number; clrSec?: number;
  coolMin?: number; repeatMin?: number; maxDay?: number; minCov?: number; win?: { from: string; to: string; days: number };
  sev: 1|2|3|4|5; email: boolean; ntfy: boolean; rc?: number; clrMail?: boolean; upd: number | object; }
export interface EmailCfg { on: boolean; to: string; fromName?: string; maxPerDay?: number; monthStart?: number;
  builtin?: { minSev: 0|3|4|5; clear: boolean }; summary?: { on: boolean; hour: number };
  offline?: { on: boolean; min: number; clear?: boolean }; }
export interface AlertsDoc { email?: EmailCfg; rules?: Record<string, AlertRule>; }
export interface AlertStateRec { st: 'off'|'wait'|'idle'|'pend'|'fire'|'clr'|'latch'|'done'|'inv';
  since?: number; val?: number; last?: number; cnt?: number; pk?: number; err?: string; }
export interface MailerStatus { hb?: number; ver?: string; day?: string; sent?: number; quota?: number; off?: number;
  err?: string; warn?: string; last?: { ts: number; ok: boolean; msg: string }; test?: { ts: number; ok: boolean; msg: string }; }
// EventRec += rid?: string; val?: number; thr?: number; em?: string; emt?: number;
// Status   += arv?: number; acat?: number; mjq?: number; nrul?: number;    Live += gw?: number;
// Cfg      += alertsRev?: number   (NOT in CFG_FIELDS; never sent by the Settings form)

// DataSource additions (date-bounded / small; listeners only while the Alerts page is visible)
onAlerts(cb: (v: AlertsDoc | null) => void, err?: ErrCb): Unsub;                  // alerts (≤ ~6 KB)
onAlertState(cb: (v: Record<string, AlertStateRec> | null) => void, err?: ErrCb): Unsub;
onMailer(cb: (v: MailerStatus | null) => void, err?: ErrCb): Unsub;                // ≈ 300 B, changes every 5 min
saveRule(rid: string, r: AlertRule): Promise<void>;     // update(inv1, {alerts/rules/rid, cfg/alertsRev: serverTimestamp()})
setRuleEnabled(rid: string, on: boolean): Promise<void>;
deleteRule(rid: string): Promise<void>;                 // {alerts/rules/rid: null, cfg/alertsRev}
saveEmailCfg(c: EmailCfg, rcRemap: Record<string, number>): Promise<void>;   // + remapped rule rc masks
requestTestEmail(rid?: string): Promise<number>;        // set mailq/test {ts: serverNow, rid}; resolves ts
```

`web/src/alertMetrics.ts` is generated (§4.3). `web/src/data/alerts.ts` exports:
- `PRESETS`;
- `defaultHysteresis()`;
- `validateRule(r): string[]`, which implements **exactly** the §3.11 constraints, so a `PERMISSION_DENIED` should be unreachable and is shown as "The database rejected this rule; please report";
- `fmtValue(metric, v)`, with the same formats as §4.5;
- `previewSubject(rule, v)`.

### 6.3 Rules segment

```
Alerts                         [ Rules | History | E-mail ]
───────────────────────────────────────────────────────────
✓ Logger has your latest rules (synced 14:03)      Mailer OK · 4/40 today
[ + New rule ]
───────────────────────────────────────────────────────────
Load > 2500 W for 5 min                                ( on)
Load 2,710 W · above for 3:40 of 5:00                PENDING
E-mail, ntfy · 06:00–23:00 · cooldown 1 h
───────────────────────────────────────────────────────────
Daily grid usage > 5 kWh                               ( on)
Grid today 4.62 kWh est.  [█████████░] 92 %             IDLE
E-mail · once per day
───────────────────────────────────────────────────────────
ⓘ "Logger offline" e-mails come from the mailer (E-mail tab) and from Healthchecks.io.
```

**Sync banner:**
- `status.arv === cfg.alertsRev` shows "synced hh:mm".
- Otherwise "Waiting for the logger (checks every 60 s)".
- If `live.ts` is more than 60 s old, "Logger offline since hh:mm; rules apply when it reconnects".

**Current values:**
- inst metrics: `live` (`gw` from `live.gw`);
- `day`: `daily/<today>`;
- `month`: `getDailyRange(cycleStart, yesterday)` + today.

**Badge and save behaviour:**
- The badge comes from `alertState/<rid>`. A progress bar is shown for `gt`/`ge` energy rules.
- Save is disabled while `.info/connected` is false.
- New-slot race: `runTransaction(alerts/rules/rNN, cur => cur === null ? rule : undefined)`, then the rev bump.
- Up to 16 slots; the UI picks the first free `rNN`.

**Presets** (the editor opens pre-filled and the user confirms):

| # | Preset | Stored rule (besides `name`, `on:true`, `upd`) |
|---|---|---|
| 1 | Daily grid usage > 5 kWh | `egr day gt 5000 hold forSec 0 coolMin 60 maxDay 1 sev 3 email` |
| 2 | Load > 2500 W for 5 min | `w inst gt 2500 hold forSec 300 clr 2200 clrSec 120 coolMin 60 maxDay 3 sev 3 email ntfy clrMail` |
| 3 | SOC < 30 % | `soc inst lt 30 hold forSec 60 clr 35 clrSec 300 coolMin 120 maxDay 3 sev 4 email clrMail` (hint: built-in BATT_LOW already pushes) |
| 4 | PV today < 3 kWh by 17:00 | `epv day lt 3000 at 1700 minCov 80 maxDay 1 sev 2 email` |
| 5 | Inverter temp > 70 °C | `tmp inst gt 70 hold forSec 120 clr 65 clrSec 300 coolMin 60 maxDay 3 sev 4 email ntfy clrMail` |
| 6 | Monthly grid usage > 150 kWh | `egr month gt 150000 hold maxDay 1 sev 3 email` |
| 7 | Grid outage today > 3 h | `gout day gt 180 hold maxDay 1 sev 3 email` |
| 8 | On battery for 2 h | `onbat inst ge 1 hold forSec 7200 clrSec 60 coolMin 120 maxDay 3 sev 3 email clrMail` |
| 9 | Evening peak load > 1500 W | `w inst gt 1500 hold forSec 60 clr 1300 clrSec 120 coolMin 60 maxDay 2 win 1800–2300 days 127 sev 3 email` |

**Editor** (full-screen sheet; inputs ≥ 16 px so iOS does not zoom; sticky Save bar above the safe area):
- **Name:** 40-char counter; no line breaks.
- **Metric:** a native `<select>` grouped Power / Battery / Grid / Energy. Each option shows its unit and current value. Metrics with `since > status.acat` are hidden.
- **Period** (energy only): Today | This billing month ("from 5 Oct").
- **Condition:** `>` `≥` `<` `≤` plus a value with a unit suffix.
  - `inputmode="decimal"`, accepting comma or dot; kWh is converted to Wh on save.
  - For energy, `<`/`≤` switches the mode to "Check once at".
- **Evaluate:**
  - "Continuously": a duration `<select>` with 0 s, 30 s, 1, 2, 5, 10, 15, 30, 60 min, 2, 4 or 8 h;
  - "Check once at": `<input type="time" step="60">`; "17:00" is stored as `"1700"`.
- **Recovers when:** "Auto (2,375 W)" or a custom value, plus a clear-hold `<select>`. Hidden for latched energy rules, which show "Fires at most once per day/month" instead.
- **E-mail frequency:**
  - Cooldown: 5, 15, 30 min, 1, 2, 6, 12, 24 h.
  - Repeat while active: off, 30 min, 1, 2, 6 h.
  - Max per day: 1–10.
- **Active hours:** an "All day" switch, or from/to times plus Sun–Sat chips (Fri and Sat marked as the weekend).
- **Channels:** E-mail, ntfy. At least one must be on.
- **Recipients:** "Everyone on the list", or chips for each address in `alerts/email/to` (stored as the `rc` mask).
- **Severity:** Info … Critical (1–5). Help text: "Alert/Critical (4–5) may use the last 5 e-mails of the day and also go to Telegram".
- **Also e-mail when it recovers:** toggle (`clrMail`).
- **Preview:** subject plus the first lines, from `previewSubject()` with live values.
- **Buttons:** Save, "Send test e-mail for this rule" (`requestTestEmail(rid)`), Delete (with a confirm).

### 6.4 E-mail segment

- **Send alert e-mails:** master switch (`on`).
- **Recipients:** up to 3, with add and remove (`type="email" autocomplete="email" autocapitalize="off" spellcheck="false"`). Saved as the comma string.
  - Removing an address **remaps every rule's `rc`** in the same update.
  - Note: "New addresses must also be added to `ALLOW_TO` in the mailer script, or they are skipped." The current `mailer.warn` is shown here.
- **Sender name**, **Daily limit** (1–40; "Google allows 100 recipients per day; 40 leaves a safety margin"), **Billing month starts on day** (1–28).
- **Built-in alerts by e-mail:** Off / Warning+ (3) / Alert+ (4) / Critical only (5), plus a "Recovery e-mails" switch.
- **Daily summary:** switch plus hour (default 07:00, "yesterday's totals").
- **Logger offline e-mail:** switch, minutes (10–240, default 30), "Back online" switch.
- **Mailer status card:**
  - `mailer.hb` age, `ver`, "sent today N/40", "Google quota left Q", and the last e-mail;
  - **red when `hb` is older than 15 min**: "Mailer not running. Open script.google.com as the sender account and check Triggers and Executions." `err` is shown verbatim.
- **Send test e-mail** (`requestTestEmail()`):
  - The button is disabled for 60 s.
  - It waits on `onMailer` until `mailer.test.ts === requested`, then shows "Sent to 2 recipients (14:03)" or the reason.
  - After 6 min it shows: "The mailer has not run yet; it runs every 5 min (last run hh:mm)."

### 6.5 History segment

- The last 7 days of `events/<date>` (existing date-bounded reads), filtered All / Rules / Built-in / E-mail.
- Each row: time, message, value vs threshold (`val`/`thr` with `fmtValue`), severity, and an **`em` chip**:

  | `em` | Chip |
  |---|---|
  | `q` | queued |
  | `sent` | e-mailed hh:mm |
  | `cool` | cooldown |
  | `rcap` | rule limit |
  | `cap` | daily limit |
  | `quota` | Google quota |
  | `off` | e-mail off |
  | `norc` | no recipient |
  | `exp` | expired |
  | `fail` | failed |
  | `skip` | not checked |

- Tapping a rule row opens its rule.

### 6.6 Settings page and demo mode

- **Settings page:** the `summaryHour` label becomes "Daily summary **push** at". The "Send test alert" note says "also e-mailed when alert e-mails are on".
- **Demo mode** (`?demo=1`): fake `alerts`, `alertState`, `mailer` and `requestTestEmail`. The fake resolves after 3 s with `ok:true`.
- **Bandwidth** (d1 §6.4 discipline): `onAlerts` (≤ 6 KB) and `onAlertState` (≈ 1.5 KB) only while Alerts is visible; `onMailer` ≈ 300 B per 5 min.
- **No CSP or hosting change:** the PWA never calls Apps Script. Test requests go through RTDB `mailq/test`, so the [r7](r7-web-hosting.md) `connect-src` list (Identity Toolkit, Secure Token and `*.asia-southeast1.firebasedatabase.app`) already covers everything.

---

## 7. User setup steps (one-time)

> ⚠️ **Order matters:** rules → mailer → PWA → firmware (r8c M6).

1. **Dedicated sender account.**
   - Create a new Gmail account, for example `<you>.solar.alerts@gmail.com`. Google may ask for phone verification.
   - Turn on 2-Step Verification to protect it. It is also required for the v2 SMTP fallback.
   - **Sign in to it at least once a year.** Google may delete personal accounts that have been inactive for 2 years, and script activity may not count (r8c M4).
2. **Firebase mailer user.**
   - Firebase console → Authentication → Users → **Add user** `mailer@<anything>.invalid`, with a 24+ character random password (`openssl rand -base64 24`).
   - Copy its **User UID**.
3. **Rules.**
   - Merge §3.11 into `firebase/database.rules.json` (the implementation phase will do this).
   - Replace the placeholders, extending the `firebase/README.md` §3 commands: `sed -i '' -e "s/OWNER_UID/$OWNER/g" -e "s/DEVICE_UID/$DEVICE/g" -e "s/MAILER_UID/$MAILER/g" firebase/database.rules.json`, then `grep -c "OWNER_UID\|DEVICE_UID\|MAILER_UID" firebase/database.rules.json` must print 0.
   - Publish: paste into the console's Realtime Database → Rules → **Publish**, or run `firebase deploy --only database` yourself.
4. **Apps Script project** (signed in as the **sender** account):
   1. Go to script.google.com → **New project** and name it "Savx mailer".
   2. Project Settings: tick **Show "appsscript.json" manifest file in editor**, and keep the default (script-managed) Cloud project. Do not switch to a standard GCP project, because that can bring the 7-day refresh-token expiry (r8c M5).
   3. Paste `appsscript.json` (§5.2) and `Code.gs` (§5.3), then save.
   4. Project Settings → **Script properties**: add `FB_API_KEY`, `FB_DB_URL`, `FB_EMAIL`, `FB_PASSWORD`, `ALLOW_TO` (your recipient addresses), and `APP_URL`.
   5. Choose the function `sendSelfTest` → **Run** and authorise.
      - Google shows "Google hasn't verified this app" for a personal script: **Advanced → Go to Savx mailer (unsafe)** [INFERRED, normal for your own script].
      - The three permissions are: send e-mail as you, connect to an external service, and run when you are not present.
      - Check that the self-test mail arrives.
   6. Run `setup`. The log must say `OK: signed in to RTDB, trigger "poll" installed…`.
      - "HTTP 401" means the rules or `MAILER_UID` are wrong.
      - "sign-in failed" means `FB_EMAIL`/`FB_PASSWORD` are wrong.
   7. Triggers (clock icon) → the `poll` trigger → **Failure notification settings: Notify me immediately**. These notices go to the sender account. Optionally forward them to your main address with a Gmail filter.
5. **PWA.**
   - Alerts → E-mail: add the same recipients, switch **Send alert e-mails** on, and press **Send test e-mail**.
   - Expect the e-mail within about 5 min, and "Sent to N recipients" in the app.
6. **Recipients' Gmail** (each recipient):
   - Create a filter `subject:"[Solar]"` → **Never send it to Spam**, **Always mark it as important**.
   - On the iPhone, use the **Gmail app** with notifications on. iOS Mail fetches Gmail on a schedule [INFERRED, r8a].
7. **Firmware** (after the implementation phase): flash; Settings → "Send test alert" should produce an ntfy push **and** an e-mail (`TEST_ALERT`).
8. **(v1.1, optional) Nudge.**
   - Apps Script → **Deploy → New deployment → Web app**: Execute as **Me**, Who has access **Anyone** → copy the `/exec` URL.
   - Set the `NUDGE_KEY` property (`openssl rand -hex 16`).
   - Enter `<exec URL>?k=<NUDGE_KEY>` in the logger's setup portal field "Mailer nudge URL".
9. **(Optional) Test the network paths from the home LAN**:
   - `curl -sI https://script.google.com | head -1`;
   - for v2, `openssl s_client -connect smtp.gmail.com:465 </dev/null | head -5`.

---

## 8. Test plan and acceptance criteria

### 8.1 Already run in this phase [TESTED]

| Suite | Result | What it covers |
|---|---|---|
| Mock harness (`harness.js`, Code.gs in a Node VM with fake RTDB, MailApp, Properties, Cache, Lock, Utilities, ContentService and ScriptApp) | **18/18 pass** | The tested behaviours:<br>• setup and trigger install;<br>• single mail, and `em` set to `sent`;<br>• duplicate re-write gives no second mail;<br>• digest and `rc` split;<br>• clear/repeat prefixes and the "Delayed" line;<br>• 45-job cap test: 35 + 1 notice, the rest `cap`, sev 4 from the reserve;<br>• allow-list filtering, `warn` and `norc`;<br>• `off`, `exp`, missing event;<br>• MailApp failure: retries, then `fail`;<br>• test e-mail rate limit and rule test;<br>• offline and back-online;<br>• summary at 07:xx with the cycle sum;<br>• expired-token refresh;<br>• `doGet` auth and rate limit;<br>• HTML escaping, CR/LF stripping, ledger ≤ 8 KB;<br>• quota floor;<br>• a failing offline mail does not abort the run. |
| RTDB emulator (firebase-tools 15.32.1, database emulator v4.11.2, merged rules) | **69/69 pass** | 27 allow cases (P1–P23) and 42 deny cases (N1–N39), as listed in §3.11, including the compatibility checks P21–P23 and N35 |
| End-to-end in the emulator (real `Code.gs` → real rules; UrlFetchApp shimmed with `curl`) | **pass** | Device multi-path PATCH, owner test request, `setup()`, 3 mails (test, digest of 2, summary), every `em` = `sent`, `mailq` empty, `mailer` heartbeat accepted by the rules, a re-queued job not re-sent, and a job without an event dropped |

### 8.2 To run in the implementation phase

1. `PLATFORMIO_CORE_DIR=<scratch>/pio-core <scratch>/pio-venv/bin/pio test -e native`: the 51 existing tests plus the §4.13 suites pass; `pio run -e savx` builds.
2. `python3 tools/gen_codes.py --check`, which now also checks `alert-metrics.json` against the rules metric regex.
3. `node tools/apps-script/test/harness.js` (18 cases) and the emulator suite: `HOME=<scratch> npx firebase-tools@15.32.1 emulators:exec --only database --project demo-savx "node tools/rules-test/rtest.mjs && node tools/apps-script/test/e2e.js"`. Keep HOME in scratch: the emulator JAR is cached under `$HOME/.cache/firebase/emulators`.
4. `cd web && npx tsc --noEmit && npm run build`; check the Alerts page in `?demo=1` at phone width in light and dark.
5. Add the new negative curl tests to `firebase/README.md` §5: N1, N2, N7, N8, N10a, N11, N14, N15, N22 and N25, as `code …` one-liners.

### 8.3 Acceptance criteria (bench and live, M7 in docs/07)

| # | Criterion | Pass if |
|---|---|---|
| A1 | Rules deployed | The 22 existing curl tests plus the new ones give the expected codes; the PWA and the device keep working (live, minute PATCH, cfg) |
| A2 | Mailer health | `setup` logs OK; `mailer.hb` updates every 5 ± 1 min for 24 h; Executions show ≤ 15 min/day of trigger runtime |
| A3 | Test e-mail | Pressing "Send test e-mail" delivers to every allowed recipient within **6 min** (v1.1 nudge: 1 min), and the app shows "Sent to N" |
| A4 | End-to-end built-in | "Send test alert" gives an ntfy push within 1 min **and** an e-mail within 6 min; the event shows the `e-mailed` chip |
| A5 | Day energy rule | Preset 1 with a 100 Wh threshold: **exactly one** e-mail that day. A reboot right after it gives no second e-mail; `alertState` is `latch` |
| A6 | Inst rule with hold/clear | A load rule with `> 100 W for 1 min`, `coolMin 5`, `clrMail`: one open mail when the load runs, one "Cleared" mail after the load stops and `clrSec` passes, and nothing in between |
| A7 | Offline delivery | Wi-Fi off for 40 min while a rule fires: after reconnect, **one** alert e-mail with "Delayed …", one "Logger offline" e-mail and one "back online" e-mail (offline.min = 30) |
| A8 | Caps | 45 forced jobs in a day: ≤ 40 recipient-deliveries (`mailer.sent`), exactly one "limit reached" mail, sev ≥ 4 still delivered, the rest `cap` |
| A9 | Allow-list | An address in `to` but not in `ALLOW_TO` gets nothing; the app shows the warning |
| A10 | Summary | Arrives between 07:00 and 07:10; yesterday's totals equal the Energy page ±1 % |
| A11 | Mailer down | Disabling the trigger shows "Mailer not running" in the app within 15 min; re-enabling it delivers queued jobs younger than 12 h and expires older ones |
| A12 | Quota | RTDB Usage shows ≤ 5 MB/day more download than before |
| A13 | Safety unchanged | `INV_ALLOW_WRITES=0`; no new inverter commands; the inverter link stats are unchanged during the tests |
| A14 | 7-day soak | No duplicate e-mails; no `fail`; no heartbeat gap > 15 min; the device's `mjq` matches the jobs sent |

---

## 9. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R-E1 | Google lowers the consumer MailApp quota ("can change without notice"), or the asterisk applies | Low / medium | `mailer.quota` is shown in the app; the quota floor of 10 gives sev ≥ 4 only; the cap can be lowered by property and in the app |
| R-E2 | The sender account is locked, deleted after inactivity, or its password is changed (which may revoke the script grant) | Low / high | Dedicated account with 2SV; sign in yearly; PWA "Mailer not running"; ntfy + Healthchecks stay independent; v2 SMTP does **not** help here (same account) |
| R-E3 | Alert mail lands in Spam | Medium / medium | Gmail-to-Gmail from a real account; recipient filter "Never send to spam"; a stable `[Solar]` tag |
| R-E4 | Trigger runtime grows (slow RTDB/TLS) and approaches 90 min/day | Low / medium | Idle run = 3 small requests; watch Executions; drop to `everyMinutes(10)` if needed |
| R-E5 | Duplicate e-mail after a lost reply or concurrent runs | Low / low | Deterministic keys, the PropertiesService ledger saved before the RTDB write, LockService [TESTED] |
| R-E6 | A rules typo blocks all writes for the device or the PWA (and denied requests are billed) | Medium / high | The 69 emulator cases; deploy order; README curl tests after every change |
| R-E7 | The production rules engine rejects an expression the emulator accepted (length or regex) | Low / medium | Publish in the console first (it validates); split the long `$rid` clause onto children if refused |
| R-E8 | A compromised device writes junk jobs | Low / low | The device cannot choose recipients or content beyond `msg`; ALLOW_TO; 40/day; jobs need an existing event |
| R-E9 | A compromised owner login or PWA bug sets odd recipients | Low / medium | ALLOW_TO in the sender's script properties (a second factor); rules allow ≤ 3 valid addresses |
| R-E10 | Load-shedding produces many offline e-mails | Medium / low | Default 30 min, ≤ 4/day, a switch in the app; raise `offline.min` to 120 if needed |
| R-E11 | The billing month in MTD differs from the utility bill, or `egr` is mistaken for the meter | Medium / low | `monthStart` setting; "est." everywhere; the footer says so |
| R-E12 | Unverified-app consent, or Google changes the consent flow for personal scripts | Low / low | It is your own script; re-run `setup` |
| R-E13 | Recipient privacy (family addresses in RTDB) | Low / low | Readable only by the owner, device and mailer UIDs; the footer explains why the person gets the mail and how to stop |
| R-E14 | The nudge URL or key leaks from NVS (unencrypted) | Low / low | It only triggers polls (rate-limited); rotate `NUDGE_KEY` |
| R-E15 | The 3.3.12 `HTTPClient` issues (#8300, `getString()` hang) also affect the FW-2 uplink | Medium / medium | Outside this feature, but FW-2 should read bodies with a Content-Length plus deadline guard (r8c M1) |

---

## 10. Sources (fetched or run 2026-10-21)

- **Apps Script:**
  - [Quotas](https://developers.google.com/apps-script/guides/services/quotas)
  - MailApp
  - UrlFetchApp
  - ScriptApp
  - ClockTriggerBuilder
  - Lock
  - PropertiesService
  - [Manifest](https://developers.google.com/apps-script/manifest)
  - [Scopes](https://developers.google.com/apps-script/concepts/scopes)
  - [Web apps](https://developers.google.com/apps-script/guides/web)
  - [Content service](https://developers.google.com/apps-script/guides/content)
- **Firebase:**
  - [RTDB REST auth](https://firebase.google.com/docs/database/rest/auth)
  - REST reference
  - Rules reference
  - [Offline capabilities / serverTimeOffset](https://firebase.google.com/docs/database/web/offline-capabilities)
  - firebase-tools 15.32.1 / database emulator v4.11.2, run locally
- **Gmail and account:**
  - [App passwords](https://support.google.com/accounts/answer/185833)
  - [Gmail sending limits](https://support.google.com/mail/answer/22839)
  - Inactive-account policy (via r8c M4)
- **arduino-esp32** (via r8c C1/M1):
  - `HTTPClient.cpp` at tag 3.3.12
  - [issue #8300](https://github.com/espressif/arduino-esp32/issues/8300)
  - [PR #12990](https://github.com/espressif/arduino-esp32/pull/12990)
- **Project:** [d1](d1-architecture.md), [v2](v2-platform-verification.md), [r8a](r8a-email-transports.md), [r8b](r8b-email-rules-ux.md), [r8c](r8c-email-verification.md), [user-constraints](user-constraints.md), [`firebase/database.rules.json`](../firebase/database.rules.json), [`shared/codes.json`](../shared/codes.json)

## 11. Decide-phase verification log

Working files are in `<scratch>`. No project files other than this one were changed.

| Item | Path (under `scratchpad/r8/`) |
|---|---|
| Mailer source and manifest | `mailer/Code.gs`, `mailer/appsscript.json` (identical to §5) |
| Mock harness | `mailer/harness.js`, run with `node mailer/harness.js` → `18 passed, 0 failed`; `node --check` OK |
| Rules snippets and merge | `rules/snippet-*.json`, `rules/merge.py` → `rules/database.rules.merged.json` (480 lines, valid JSON) |
| Emulator tests | `emu/rtest.mjs`, `emu/e2e.js`. Run with `HOME=<r8>/home CI=true <r8>/fbtools/node_modules/.bin/firebase emulators:exec --only database --project demo-savx "node rtest.mjs && node e2e.js"` → `69 passed, 0 failed` and `E2E OK`. firebase-tools was installed into `<r8>/fbtools` with HOME and the npm cache redirected to scratch (no login, no deploy). |
