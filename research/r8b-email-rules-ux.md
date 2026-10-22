# Usage-threshold e-mail alerts: rule engine, data contract and iPhone UX

Date: 2026-10-21. Scope: the rule engine, the data contract and the UX for user-defined threshold alerts that are delivered by e-mail. The e-mail transport (relay or SMTP) is only covered here as an interface plus quota guard (§8). The project's firmware and web files were read but not modified.

Tags: **[CONFIRMED-primary]** means I read it today on an official page or in source, or measured it today. **[SECONDARY]** means a non-official source. **[INFERRED]** means a design decision or reasoning of mine. Short references such as "d1 §4.2" and "V2 F8" point to `research/d1-architecture.md` and `research/v2-platform-verification.md`.

---

## 0. Decisions at a glance

| # | Decision | Why |
|---|---|---|
| A1 | Rules are evaluated **on the ESP32**, in a new `RuleEngine` inside `aggTask`, next to the built-in `AlertEngine` (d1 §7). | Spark has no Cloud Functions (V2 F8 [CONFIRMED-primary]). The Firebase "Trigger Email" extension needs Blaze, is Firestore-based and needs SMTP credentials ([extensions.dev](https://extensions.dev/extensions/firebase/firestore-send-email) [CONFIRMED-primary]). |
| A2 | Rules live in a new owner-written node, **`/devices/inv1/alerts/{email, rules/{rid}}`**, a sibling of `cfg`. `cfg` only gains `alertsRev`, `testEmail` and `testRule`. | It keeps the 60 s `cfg` GET small (+62 B, measured). The full `alerts` document, about 6.5 KB with 16 rules (measured), is fetched only when the revision changes. |
| A3 | Change pickup uses a **revision counter** (`cfg.alertsRev = serverTimestamp()`), written atomically with the rule edit in one multi-path `update()`. There is also a 6-hourly safety refresh. ETags are not used. | RTDB REST returns an ETag only when asked with `X-Firebase-ETag`. A GET with `if-match` returns `400 "...not supported.."`. If-None-Match/304 is not documented (REST reference [CONFIRMED-primary]). So an ETag GET still downloads the full body. |
| A4 | At most **16 rules**, enforced by the key pattern `r01`–`r16`. At most **3 recipients**, enforced by keys `0`–`2`. | The rules language has no `numChildren()`. RuleDataSnapshot offers `val, child, parent, hasChild, hasChildren, exists, getPriority, isNumber, isString, isBoolean` (rules reference [CONFIRMED-primary, by absence]). Restricting key patterns is the only server-side way to cap the count. |
| A5 | E-mail is **not** subject to quiet hours by default. A rule can set `quiet:"defer"` to hold mail until `cfg.quietTo`. | E-mail is silent unless the user's phone is set to alert on it. The existing ntfy quiet-hour logic is unchanged [INFERRED]. |
| A6 | Global guard: **40 recipient-deliveries per local day** (`alerts/email/maxPerDay` can only lower it). 5 of those are reserved for severity ≥ 4. Each rule also has its own `maxPerDay` (default 3). | Apps Script MailApp allows 100 recipients/day on consumer accounts, and the quota "reset[s] 24 hours after the first request" ([quotas](https://developers.google.com/apps-script/guides/services/quotas) [CONFIRMED-primary]). One rolling 24 h window can overlap two local days, so the worst case is 2 × 40 = 80 < 100 [INFERRED]. |
| A7 | **Templates are rendered on the ESP** (plain text plus simple HTML) from `shared/email-templates.json`. The same file feeds the web preview, following the existing `codes.json` → generator pattern. | The design works with any transport, the relay stays simple, and the PWA preview is identical to the real mail [INFERRED]. |
| A8 | Mail jobs are **spooled to LittleFS** and carry an idempotency key. Events use **deterministic keys** (`r03-<openTs>-o`) instead of POST push IDs. | Retries after a timeout cannot produce duplicate mails or duplicate events [INFERRED]. |
| A9 | Credentials (relay URL and secret, or an SMTP app password) live **only in device NVS** and are write-only. They never go into RTDB or the PWA bundle. | §7 |

---

## 1. Facts this design relies on

| Fact | Source | Tag |
|---|---|---|
| Spark: no Cloud Functions; RTDB 10 GB/month download; rule-denied requests are billed | V2 F1, F3, F8 | [CONFIRMED-primary] |
| `print=silent` → `204 No Content`; supported by GET, PUT, POST, PATCH | [save-data](https://firebase.google.com/docs/database/rest/save-data) | [CONFIRMED-primary] |
| ETag returned only with `X-Firebase-ETag: true`. GET + `if-match` → 400 "not supported". PATCH has no ETag. A failed conditional request is not retried. | REST reference, save-data | [CONFIRMED-primary] |
| `shallow` "cannot be mixed with any other query parameters" | REST reference | [CONFIRMED-primary] |
| Rules regex: `* + . ( ) [ ] { } \` work as normal; `^`/`$` only as first/last character; only the `i` flag; `\s \w \d \S \W \D` supported | rules regex | [CONFIRMED-primary] |
| Rules operators include `%` and the ternary `?:`. `now` is in milliseconds. `$` variables are always strings. `newData` merges existing and new data, so ancestor `.validate` rules apply to partial updates (the "fred" example). | rules reference | [CONFIRMED-primary] |
| Firebase JS SDK 13.0.0 (installed) exports `serverTimestamp()` and `increment()` | `web/node_modules/@firebase/database/dist/public.d.ts` L404, L1437 | [CONFIRMED-primary] |
| Apps Script consumer quotas: 100 e-mail recipients/day; 50 recipients/message; e-mail body 200 KB; per-user quotas reset 24 h after the first request; `MailApp.getRemainingDailyQuota()` | [quotas](https://developers.google.com/apps-script/guides/services/quotas) (updated 2026-09-03), MailApp | [CONFIRMED-primary] |
| Apps Script `CacheService`: expiry up to 21,600 s; keys ≤ 250 characters; `Utilities.computeHmacSha256Signature`; ContentService replies are redirected to a one-time URL on `script.googleusercontent.com`, so the client must follow redirects | Cache, Utilities, [Content service](https://developers.google.com/apps-script/guides/content) | [CONFIRMED-primary] |
| TLS: both `script.google.com` and `script.googleusercontent.com` chain WE2 → **GTS Root R4**, which is already embedded (d1 §5.13) | `openssl s_client`, today | [CONFIRMED, measured] |
| Gmail consumer: error above 500 e-mails/day or 500 recipients/message; sending works again "within 1 to 24 hours" | [Gmail limits](https://support.google.com/mail/answer/22839) | [CONFIRMED-primary] |
| App passwords: 16 digits, need 2-Step Verification, "aren't recommended", revoked when the Google password changes | [app passwords](https://support.google.com/accounts/answer/185833) | [CONFIRMED-primary] |
| ESP-Mail-Client is "DEPRECATED ... no longer maintained", with ReadyMail as the replacement. ReadyMail: SSL 465 / STARTTLS 587, plain-text and HTML bodies, CC BY-NC 4.0, notes a possible ESP32 v3.x hang in plain mode | [ESP-Mail-Client](https://github.com/mobizt/ESP-Mail-Client), [ReadyMail](https://github.com/mobizt/ReadyMail) | [CONFIRMED-primary] |
| Brevo Free: "up to 300 emails per day" once the account is approved. Resend Free: 100/day, 3,000/month. | [Brevo pricing](https://www.brevo.com/pricing/), [Resend pricing](https://resend.com/pricing) | [CONFIRMED-primary]. Resend's sender-domain requirement was not verified. |
| ntfy.sh can forward to e-mail, 5 e-mails/day | V2 I3 | [CONFIRMED-primary] |
| `egr` = ∫(w + acChgW) while the grid is present and md = L, with `acChgW = st.b0 ? max(0, bv·bci − (st.b1 ? pvw : 0)) : 0` | d1 §4.4 | design fact |
| Uploaded LittleFS minute files are kept for only **14 days** | d1 §5.5 | design fact. Month-to-date therefore **cannot** be rebuilt from LittleFS (§2.3). |

---

## 2. Metric catalogue (from d1 §3.8 / §4.2)

The catalogue lives in `shared/alert-metrics.json`. It is generated into `firmware/include/alert_metrics.h` and `web/src/alertMetrics.ts`, and carries a catalogue version (`acat`) that the device reports in `status`. Values are stored in **canonical units**: W, Wh, V, A, %, C, Hz, min, bool, dBm, n. The UI displays kWh and converts on save.

### 2.1 Instantaneous metrics (`scope:"inst"`, evaluated on every 5 s sample)

| id | Label (UI) | Unit | Source | Default hysteresis (used when `clearValue` is absent) | Notes |
|---|---|---|---|---|---|
| `w` | Load power | W | `live.w` (QPIGS[5] / QALL[4]) | max(50 W, 5 % of value) | |
| `ld` | Load % of rating | % | `ld` | 2 | Overlaps the built-in OVERLOAD alert |
| `pvw` | Solar power | W | `pvw` | max(50, 5 %) | Stock firmware reports "PV charging power" (d1) |
| `pvv` | Solar voltage | V | `pvv` | 10 | |
| `bv` | Battery voltage | V | `bv` | 0.3 | |
| `soc` | Battery SOC | % | `soc` | 2 | Voltage-derived unless a BMS protocol is active (d1) |
| `bdi` | Battery discharge current | A | `bdi` | 2 | 1 A resolution (d1 §4.4) |
| `bdw` | Battery discharge power | W | derived `max(0, −bw)` | max(50, 5 %) | |
| `bw` | Battery net power (+ = charging) | W | `bw` | max(50, 5 %) | Signed |
| `gv` | Grid voltage | V | `gv` | 5 | |
| `gf` | Grid frequency | Hz | `gf` | 0.3 | |
| `ov` | Output voltage | V | `ov` | 5 | |
| `tmp` | Inverter heat-sink temperature | C | `tmp` | 3 | NaN when > 150 (raw ADC, d1) |
| `gw` | **Grid import power (est.)** | W | derived: `(gridPresent && md=='L') ? w + acChgW : 0`, the same integrand as `egr` | max(50, 5 %) | **Recommend adding `gw` to `live`.** The existing `$num` rule already allows it. About +86 KB/day. The PWA then shows exactly the device's estimate. |
| `onbat` | On battery | bool | `md == 'B'` | – | The UI shows "is true for ≥ duration" (stored as `ge 1`) |
| `nogrid` | Grid absent | bool | the same predicate as GRID_LOSS (QPIWS a5, or `gv` < 90/170 V) | – | Load-shedding use case |

### 2.2 Energy and time metrics (`scope:"day"` or `"month"`)

| id | Label | Unit | Day value | Month value |
|---|---|---|---|---|
| `eld` | Load energy | Wh | running `eld` today (the d1 integrator) | MTD (§2.3) |
| `egr` | **Grid energy (est.)** | Wh | running `egr` | MTD |
| `epv` | Solar energy | Wh | running `epv` | MTD |
| `edis` | Battery discharged | Wh | running `edis` | MTD |
| `ech` | Battery charged | Wh | running `ech` | MTD |
| `gout` | Grid outage time | min | minutes without grid today (the `daily.gout` logic) | MTD |

Energy rules are **monotonic within a period**:
- `gt`/`ge` fire once, then **latch** until the period resets.
- `lt`/`le` only make sense as a check at a deadline, so they require `mode:"at"`. They are allowed only for `day` scope.

These constraints are enforced in the database rules (§3.4).

Wording for the UI and e-mails: `egr` is an estimate. It counts only loads behind the inverter's AC OUT plus AC charging. **It is not the utility meter reading** [INFERRED from d1 §4.4].

### 2.3 Month-to-date (on the device)

- **Cycle start:** `alerts/email/monthStart` (1–28, default 1), so the cycle can follow a billing date. The period key is the local date of the cycle start (for example `20261005`).
- **Store:** an NVS blob `energy/mtd`: `{u32 cycleKey; u8 startDay; u32 dayKey[31]; u32 eld[31], egr[31], epv[31], edis[31], ech[31]; u16 gout[31], n[31]}`, about 900 B.
- **Write:** once per day, at the local-midnight day rollover in `aggTask`, using yesterday's final totals.
  - Those totals come from the d1 LittleFS recomputation, which is the source of truth.
  - The 00:05 cloud finalize does not touch this store.
- **Value:** `MTD = Σ closed days in the current cycle + today's running total`. Month coverage = Σ n / elapsed minutes.
- **Bootstrap:** use this when the blob is missing (first install), the cycle start changed, or the blob looks inconsistent. Do one GET when online:
  `GET /devices/inv1/daily.json?orderBy="$key"&startAt="2026-10-05"&endAt="2026-10-20"&auth=…`
  - That is at most 31 × 183 B ≈ 5.7 KB.
  - Use it to fill the missing days.
  - Until it succeeds, month rules stay in `wait`.

  [INFERRED; REST `orderBy="$key"` range queries should be checked on the bench.]
- **Not uploaded.** The PWA computes MTD from `daily` ranges (d1 §6.2 already does this for the Energy page).

### 2.4 Device and cloud health (optional, `scope:"inst"`)

| id | Unit | Source | Note |
|---|---|---|---|
| `rssi` | dBm | `status.rssi` | Weak Wi-Fi |
| `q` | n | queue backlog (minutes) | Overlaps the built-in BACKLOG_STUCK |
| `rq` | n | RTDB requests today | Early warning before the d1 QUOTA_GUARD at 20,000 |
| *logger offline* | – | **not a rule** | A dead ESP cannot send e-mail. That stays the Healthchecks.io e-mail (d1 §7.1). The Alerts page shows an info card saying so. |

---

## 3. Data contract (RTDB)

### 3.1 Tree additions

```
/devices/inv1/
  cfg/alertsRev            owner: serverTimestamp() (ms), bumped with every alerts/* change
  cfg/testEmail            owner: epoch s; a test e-mail is requested
  cfg/testRule             owner: '' or 'r01'..'r16'; test with that rule's template and recipients
  alerts/email             owner: recipients and e-mail settings
  alerts/rules/{r01..r16}  owner: rule definitions
  alertState/{rid}         device: live engine state per rule (written on transitions)
  events/{date}/{key}      device: + fields rid, val, thr, em (deterministic keys for rule events)
  status                   device: + em, emf, emq, mq, arv, acat (all numbers; keys <= 8 chars)
```

### 3.2 Exact JSON

`alerts/email` (226 B compact, measured):

```json
{"on":true,"to":{"0":"owner@gmail.com","1":"family@gmail.com"},"fromName":"Solar logger","maxPerDay":40,
 "summary":{"on":true,"hour":7},"builtin":{"minSev":4,"clear":true},
 "appUrl":"https://savx-logger.web.app/","monthStart":1}
```

Notes on `alerts/email`:
- `builtin.minSev` sets which **existing** d1 alerts are also e-mailed: 0 = none, 4 = severity ≥ 4, 5 = severity 5 only. `builtin.clear` adds the clear or restore mail, such as GRID_RESTORED.
- `summary` is the daily e-mail for yesterday (§5.6). It is separate from `cfg.summaryHour`, which stays the ntfy summary.
- `to` is written as an object with keys `"0".."2"`. On read, Firebase may return it as a JSON array, so **both** the firmware and the PWA must accept array or object. The PWA always rewrites the whole `to` node in compacted form [INFERRED, known SDK behaviour].

The five preset rules, exactly as stored (each 323–407 B compact, measured):

```json
{
"r01":{"id":"r01","name":"Load > 2500 W for 5 min","on":true,"metric":"w","scope":"inst","op":"gt","value":2500,"unit":"W",
       "mode":"hold","forSec":300,"clearValue":2200,"clearSec":120,"cooldownMin":60,"repeatMin":0,"maxPerDay":3,
       "window":{"from":"0600","to":"2300","days":127},"sev":3,"ch":{"email":true,"ntfy":true,"tg":false},
       "notifyClear":true,"quiet":"send","createdAt":1792560000000,"updatedAt":1792560000000},
"r02":{"id":"r02","name":"SOC < 30 %","on":true,"metric":"soc","scope":"inst","op":"lt","value":30,"unit":"%",
       "mode":"hold","forSec":60,"clearValue":35,"clearSec":300,"cooldownMin":120,"maxPerDay":3,"sev":4,
       "ch":{"email":true,"ntfy":false,"tg":false},"notifyClear":true,"quiet":"send",
       "createdAt":1792560000000,"updatedAt":1792560000000},
"r03":{"id":"r03","name":"Daily grid usage > 5 kWh","on":true,"metric":"egr","scope":"day","op":"gt","value":5000,"unit":"Wh",
       "mode":"hold","forSec":0,"cooldownMin":60,"maxPerDay":1,"sev":3,"ch":{"email":true,"ntfy":false,"tg":false},
       "to":{"0":"owner@gmail.com"},"notifyClear":false,"quiet":"send",
       "createdAt":1792560000000,"updatedAt":1792560000000},
"r04":{"id":"r04","name":"PV today < 3 kWh by 17:00","on":true,"metric":"epv","scope":"day","op":"lt","value":3000,"unit":"Wh",
       "mode":"at","at":"1700","minCov":80,"cooldownMin":60,"maxPerDay":1,"sev":2,
       "ch":{"email":true,"ntfy":false,"tg":false},"notifyClear":false,"quiet":"send",
       "createdAt":1792560000000,"updatedAt":1792560000000},
"r05":{"id":"r05","name":"Inverter temp > 70 C","on":true,"metric":"tmp","scope":"inst","op":"gt","value":70,"unit":"C",
       "mode":"hold","forSec":120,"clearValue":65,"clearSec":300,"cooldownMin":60,"maxPerDay":3,"sev":4,
       "ch":{"email":true,"ntfy":true,"tg":false},"notifyClear":true,"quiet":"send",
       "createdAt":1792560000000,"updatedAt":1792560000000}
}
```

Field dictionary. Device defaults apply when a field is absent.

| Field | Type / range | Default | Meaning |
|---|---|---|---|
| `id` | = key | – | Slot id `r01`–`r16` |
| `name` | 1–40 chars, no tab/CR/LF | – | Shown in the UI, e-mail subject and events. HTML-escaped by the ESP. |
| `on` | bool | – | Enabled |
| `metric`, `unit` | §2 enums; the pair must match | – | `unit` is redundant on purpose, so rules can validate value ranges |
| `scope` | `inst` \| `day` \| `month` | – | Must be `inst` for §2.1 metrics and `day`/`month` for §2.2 metrics |
| `op` | `gt` \| `ge` \| `lt` \| `le` | – | |
| `value` | number, range by unit | – | Canonical units (Wh, not kWh) |
| `mode` | `hold` \| `at` | – | `hold`: true for `forSec`. `at`: checked once at `at` (local hhmm). |
| `at` | `hhmm` | – | Required when `mode:"at"` |
| `forSec` | 0–86400 | inst 60, energy 0 | Hold time before firing |
| `clearValue` | number on the "safe" side of `value` | value ∓ default hysteresis | Clear threshold |
| `clearSec` | 0–3600 | 60 | Clear hold time |
| `cooldownMin` | 5–10080 | 60 | Minimum gap between two e-mails of this rule (clear mails excluded) |
| `repeatMin` | 0 or 30–1440 | 0 | Re-mail while still firing |
| `maxPerDay` | 1–10 | 3 | Per-rule e-mail cap per local day |
| `minCov` | 0–100 % | 80 | Data coverage needed before an `lt`/`le` energy check may fire |
| `window` | `{from, to, days}` | absent = always | `from > to` crosses midnight. `days` bitmask: bit0 = Sunday … bit6 = Saturday. The mask applies to the day the window **starts**. |
| `ch` | `{email, ntfy, tg}` bools, at least one true | `{email:true}` | Channels. ntfy and Telegram follow the d1 routing and quiet hours. |
| `to` | up to 3 e-mails | absent = `alerts/email/to` | Recipient override |
| `sev` | 1–5 | 3 | Severity, using the d1 meaning |
| `notifyClear` | bool | false | Send a "cleared" mail, but only if the open was mailed |
| `quiet` | `send` \| `defer` | `send` | `defer` holds e-mail until `cfg.quietTo` |
| `createdAt`, `updatedAt` | ms ≤ `now` | – | Written with `serverTimestamp()` |

`cfg` additions (+62 B per 60 s cfg GET, measured):

```json
{"alertsRev":1792560000000,"testEmail":1792560100,"testRule":""}
```

`alertState` (device-written, 168 B for two rules, measured):

```json
{"r01":{"st":"fire","since":1792560123,"val":2710,"last":1792560125,"cnt":1},
 "r03":{"st":"latch","since":1792580003,"val":5010,"last":1792580003,"cnt":1,"pk":20261021}}
```

Fields:
- `st`: one of `idle | pend | fire | clr | latch | done | off | inv | wait`.
- `since`: when the state was entered.
- `last`: the last mail.
- `cnt`: mails today.
- `pk`: the period key.
- `err`: the reason for `inv`. For example, the firmware does not know the metric.

`events/2026-10-21/r03-1792580003-o` (179 B including the key, measured):

```json
{"ts":1792580003,"type":"RULE","sev":3,"st":"open","rid":"r03","val":5010,"thr":5000,
 "msg":"Daily grid usage > 5 kWh: 5.01 kWh (est.)","ch":"email","em":"queued"}
```

Event fields:
- `em` starts as `queued`. Once the transport answers it is PATCHed to one of: `sent | cool | rcap | cap | fail | exp | skip | off | dup`.
- Deterministic key: `<rid>-<openTs>-<o|c|r|i>` for open, clear, repeat and info.
- New `type` values: `RULE`, `RULE_INVALID` (sev 2), `EMAIL_TEST`, `EMAIL_SUMMARY`, `EMAIL_CAP` (sev 2, once a day), `EMAIL_FAIL` (sev 2, at most hourly).
- Test events use the key `et-<testEmail>`, so the PWA can wait on that exact path (§6.4).

`status` additions:
- `em`: recipient-deliveries today.
- `emf`: failures today.
- `emq`: relay-reported remaining quota, or −1.
- `mq`: spooled jobs.
- `arv`: the `alertsRev` the engine has loaded.
- `acat`: the catalogue version.

### 3.3 Change pickup (cheap and quick)

1. The PWA saves with **one multi-path `update()`** at `devices/inv1`: `{"alerts/rules/r03": {...}, "cfg/alertsRev": serverTimestamp()}`. Each key is checked against its own path's rules (d1 §4.7 note).
2. The device's existing 60 s `GET cfg.json` sees `alertsRev ≠ loadedRev` → `GET alerts.json` (≤ 6.5 KB) → validate against the catalogue → swap the RuleSet → store `/cfg/alerts.json` and the rev in LittleFS → report `status.arv`.
3. A full refresh also happens on boot and every 6 h, which covers edits made in the console without a rev bump.
4. Latency: at most about 60 s.
5. Options rejected:
   - ETag polling: the body is still downloaded (§1).
   - SSE stream: a second persistent TLS session in a 16 KB-stack task that d1 limits to one keep-alive client.
   - `cfg.json?shallow=true` with rules nested under `cfg`: shallow "cannot be mixed with other query parameters" and the device must send `auth=` [CONFIRMED-primary wording; whether `auth` counts was not tested].

### 3.4 Security rules (`.validate`)

The complete snippet below is JSON-checked and lives in `scratchpad/alerts-design/rules-snippet.json`. It was **not** emulator-tested; add the tests in §9. `OWNER_UID` and `DEVICE_UID` are the existing placeholders.

```json
"alerts": {
  ".write": "auth != null && auth.uid === 'OWNER_UID'",
  "email": {
    ".validate": "newData.hasChildren(['on'])",
    "on": { ".validate": "newData.isBoolean()" },
    "to": {
      ".validate": "newData.hasChildren()",
      "$i": { ".validate": "$i.matches(/^[0-2]$/) && newData.isString() && newData.val().length <= 254 && newData.val().matches(/^[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,24}$/i)" }
    },
    "fromName":   { ".validate": "newData.isString() && newData.val().length >= 1 && newData.val().length <= 40 && newData.val().matches(/^(\\S| )+$/)" },
    "maxPerDay":  { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 40" },
    "summary": {
      ".validate": "newData.hasChildren(['on', 'hour'])",
      "on":   { ".validate": "newData.isBoolean()" },
      "hour": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 23" },
      "$other": { ".validate": false }
    },
    "builtin": {
      "minSev": { ".validate": "newData.isNumber() && (newData.val() === 0 || newData.val() === 4 || newData.val() === 5)" },
      "clear":  { ".validate": "newData.isBoolean()" },
      "$other": { ".validate": false }
    },
    "appUrl":     { ".validate": "newData.isString() && newData.val().length <= 100 && newData.val().matches(/^https:\\/\\/[a-z0-9-]+\\.(web\\.app|firebaseapp\\.com)\\/?$/)" },
    "monthStart": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 28" },
    "$other": { ".validate": false }
  },
  "rules": {
    "$rid": {
      ".validate": "$rid.matches(/^r(0[1-9]|1[0-6])$/) && newData.hasChildren(['id', 'name', 'on', 'metric', 'op', 'value', 'unit', 'scope', 'mode', 'sev', 'ch', 'createdAt', 'updatedAt']) && ((newData.child('metric').val().matches(/^(w|pvw|gw|bw|bdw)$/) && newData.child('unit').val() === 'W') || (newData.child('metric').val().matches(/^(soc|ld)$/) && newData.child('unit').val() === '%') || (newData.child('metric').val().matches(/^(bv|gv|ov|pvv)$/) && newData.child('unit').val() === 'V') || (newData.child('metric').val() === 'bdi' && newData.child('unit').val() === 'A') || (newData.child('metric').val() === 'tmp' && newData.child('unit').val() === 'C') || (newData.child('metric').val() === 'gf' && newData.child('unit').val() === 'Hz') || (newData.child('metric').val().matches(/^(eld|egr|epv|edis|ech)$/) && newData.child('unit').val() === 'Wh') || (newData.child('metric').val() === 'gout' && newData.child('unit').val() === 'min') || (newData.child('metric').val().matches(/^(onbat|nogrid)$/) && newData.child('unit').val() === 'bool') || (newData.child('metric').val() === 'rssi' && newData.child('unit').val() === 'dBm') || (newData.child('metric').val().matches(/^(q|rq)$/) && newData.child('unit').val() === 'n')) && (newData.child('metric').val().matches(/^(eld|egr|epv|edis|ech|gout)$/) ? newData.child('scope').val() !== 'inst' : newData.child('scope').val() === 'inst') && (newData.child('scope').val() !== 'month' || (newData.child('op').val().matches(/^g[te]$/) && newData.child('mode').val() === 'hold')) && (!(newData.child('scope').val() === 'day' && newData.child('op').val().matches(/^l[te]$/)) || newData.child('mode').val() === 'at') && (newData.child('mode').val() !== 'at' || newData.hasChild('at')) && (!newData.hasChild('clearValue') || (newData.child('op').val().matches(/^g[te]$/) ? newData.child('clearValue').val() <= newData.child('value').val() : newData.child('clearValue').val() >= newData.child('value').val())) && (newData.child('ch/email').val() === true || newData.child('ch/ntfy').val() === true || newData.child('ch/tg').val() === true)",
      "id":     { ".validate": "newData.val() === $rid" },
      "name":   { ".validate": "newData.isString() && newData.val().length >= 1 && newData.val().length <= 40 && newData.val().matches(/^(\\S| )+$/)" },
      "on":     { ".validate": "newData.isBoolean()" },
      "metric": { ".validate": "newData.isString() && newData.val().matches(/^(w|pvw|gw|bw|bdw|soc|ld|bv|gv|ov|pvv|bdi|tmp|gf|eld|egr|epv|edis|ech|gout|onbat|nogrid|rssi|q|rq)$/)" },
      "op":     { ".validate": "newData.isString() && newData.val().matches(/^(gt|ge|lt|le)$/)" },
      "unit":   { ".validate": "newData.isString() && newData.val().matches(/^(W|%|V|A|C|Hz|Wh|min|bool|dBm|n)$/)" },
      "value":  { ".validate": "newData.isNumber() && ((newData.parent().child('unit').val() === 'W' && newData.val() >= -10000 && newData.val() <= 20000) || (newData.parent().child('unit').val() === '%' && newData.val() >= 0 && newData.val() <= 250) || (newData.parent().child('unit').val() === 'V' && newData.val() >= 0 && newData.val() <= 600) || (newData.parent().child('unit').val() === 'A' && newData.val() >= 0 && newData.val() <= 300) || (newData.parent().child('unit').val() === 'C' && newData.val() >= -20 && newData.val() <= 150) || (newData.parent().child('unit').val() === 'Hz' && newData.val() >= 40 && newData.val() <= 70) || (newData.parent().child('unit').val() === 'Wh' && newData.val() >= 0 && newData.val() <= 5000000) || (newData.parent().child('unit').val() === 'min' && newData.val() >= 0 && newData.val() <= 44640) || (newData.parent().child('unit').val() === 'bool' && (newData.val() === 0 || newData.val() === 1)) || (newData.parent().child('unit').val() === 'dBm' && newData.val() >= -100 && newData.val() <= 0) || (newData.parent().child('unit').val() === 'n' && newData.val() >= 0 && newData.val() <= 1000000))" },
      "scope":  { ".validate": "newData.isString() && newData.val().matches(/^(inst|day|month)$/)" },
      "mode":   { ".validate": "newData.isString() && newData.val().matches(/^(hold|at)$/)" },
      "at":     { ".validate": "newData.isString() && newData.val().matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/)" },
      "forSec":      { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 86400" },
      "clearValue":  { ".validate": "newData.isNumber() && newData.val() >= -10000 && newData.val() <= 5000000" },
      "clearSec":    { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 3600" },
      "cooldownMin": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 5 && newData.val() <= 10080" },
      "repeatMin":   { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && (newData.val() === 0 || (newData.val() >= 30 && newData.val() <= 1440))" },
      "maxPerDay":   { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 10" },
      "minCov":      { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 100" },
      "window": {
        ".validate": "newData.hasChildren(['from', 'to', 'days']) && newData.child('from').val() !== newData.child('to').val()",
        "from": { ".validate": "newData.isString() && newData.val().matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/)" },
        "to":   { ".validate": "newData.isString() && newData.val().matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/)" },
        "days": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 127" },
        "$other": { ".validate": false }
      },
      "ch": {
        ".validate": "newData.hasChildren()",
        "email": { ".validate": "newData.isBoolean()" },
        "ntfy":  { ".validate": "newData.isBoolean()" },
        "tg":    { ".validate": "newData.isBoolean()" },
        "$other": { ".validate": false }
      },
      "to": {
        ".validate": "newData.hasChildren()",
        "$i": { ".validate": "$i.matches(/^[0-2]$/) && newData.isString() && newData.val().length <= 254 && newData.val().matches(/^[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,24}$/i)" }
      },
      "sev":         { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 1 && newData.val() <= 5" },
      "notifyClear": { ".validate": "newData.isBoolean()" },
      "quiet":       { ".validate": "newData.isString() && newData.val().matches(/^(send|defer)$/)" },
      "createdAt":   { ".validate": "newData.isNumber() && newData.val() <= now" },
      "updatedAt":   { ".validate": "newData.isNumber() && newData.val() <= now" },
      "$other": { ".validate": false }
    }
  },
  "$other": { ".validate": false }
},
"alertState": {
  ".write": "auth != null && auth.uid === 'DEVICE_UID'",
  "$rid": {
    ".validate": "$rid.matches(/^r(0[1-9]|1[0-6])$/) && newData.hasChildren(['st'])",
    "st":    { ".validate": "newData.isString() && newData.val().matches(/^(idle|pend|fire|clr|latch|done|off|inv|wait)$/)" },
    "since": { ".validate": "newData.isNumber()" },
    "last":  { ".validate": "newData.isNumber()" },
    "val":   { ".validate": "newData.isNumber()" },
    "cnt":   { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 1000" },
    "pk":    { ".validate": "newData.isNumber()" },
    "err":   { ".validate": "newData.isString() && newData.val().length <= 40" },
    "$other": { ".validate": false }
  }
}
```

Merge into the existing `cfg` block, above its `$other`:

```json
"alertsRev": { ".validate": "newData.isNumber() && newData.val() <= now" },
"testEmail": { ".validate": "newData.isNumber() && (newData.val() === 0 || (newData.val() > now / 1000 - 300 && newData.val() < now / 1000 + 300 && (!data.exists() || newData.val() >= data.val() + 60)))" },
"testRule":  { ".validate": "newData.isString() && (newData.val() === '' || newData.val().matches(/^r(0[1-9]|1[0-6])$/))" }
```

Merge into `events/$date/$eid`, above its `$other`:

```json
"rid": { ".validate": "newData.isString() && newData.val().matches(/^r(0[1-9]|1[0-6])$/)" },
"val": { ".validate": "newData.isNumber()" },
"thr": { ".validate": "newData.isNumber()" },
"em":  { ".validate": "newData.isString() && newData.val().matches(/^(queued|sent|cool|rcap|cap|fail|exp|skip|off|dup)$/)" }
```

What these rules enforce:
- Owner-only writes to `alerts`, `cfg/alertsRev`, `cfg/testEmail` and `cfg/testRule`.
- The device can read everything, because it inherits `inv1/.read`.
- Device-only writes to `alertState`.
- At most 16 rules and at most 3 recipients, via the key patterns.
- Metric/unit consistency and value ranges by unit.
- The clear threshold sits on the correct side of the threshold.
- `lt`/`le` energy rules must be deadline checks. Month scope is `gt`/`ge` only.
- At least one channel per rule.
- No CR/LF in names, which blocks header injection.
- The PWA link is pinned to Firebase Hosting domains.
- **Test e-mails at most once per 60 s, at the database level.** The value must be within ±300 s of server time and at least 60 s after the previous value.

Caveats:
- The web app must **never** include `testEmail` in an ordinary settings patch. An unchanged value would fail the `+60` check and reject the whole update.
- Rule-denied writes are billed (V2 F3).
- Server-side `.validate` does not run on deletes, which is fine for removing rules.

---

## 4. Evaluation semantics (ESP32)

### 4.1 Placement

- `RuleEngine::onSample()` runs in `aggTask` after the energy integration, on every 5 s sample.
- It reads an immutable `RuleSet*`. The uplink task builds a new one and swaps it under mutex `mRules`.
- Output goes to the existing `qAlert` as `{kind=RULE, rid, st, val, thr, ts, sev, chMask, emailFlag}`.
- `uplinkTask` then:
  - writes the event (PUT with the deterministic key);
  - sends ntfy/Telegram per d1 routing;
  - puts the e-mail job into `MailQueue`, a LittleFS spool.
- Per-rule runtime state: about 40 B in RAM. Rule definitions: about 96 B each, internal RAM.

### 4.2 Per-rule state machine

```
            on&loaded                 cond(v) & inWin            held >= forSec
  OFF ------------------> IDLE -----------------------> PENDING -----------------> FIRING --(energy gt/ge)--> LATCHED
   ^   time not valid      ^  <-- !cond / left window /             (forSec=0: direct)  |  \                       |
   |   (window/at/day/     |      NaN > 30 s                                             |   \ repeatMin/cooldown   | period key
   |    month rules)       |                                                             |    re-mail (FIRING)      | changes
  WAIT                     |                   clear held >= clearSec                    v                          v
                           +------------------------------------------------------- CLEARING <-- clr(v)          IDLE
                                    (!clr(v) -> back to FIRING)
  mode "at": IDLE --(local time >= at, once per period)--> evaluate: cond -> FIRING->LATCHED (mail) | else DONE; DONE/LATCHED -> IDLE at period change
```

| From | Trigger | To | Action |
|---|---|---|---|
| any | `on=false`, rule deleted, or a semantic edit (hash change) | OFF / IDLE | If FIRING: event `st:"clear"`, msg "rule changed/disabled", no mail |
| WAIT | `TIME_OK` | IDLE | Window, `at`, day and month rules need local time |
| IDLE | valid v, in window, cond(v) | PENDING (`t0`) | – |
| PENDING | !cond(v) on valid data, or left window, or NaN for more than 30 s | IDLE | – |
| PENDING | `now − t0 ≥ forSec` | FIRING | **OPEN**: event, then notify through the gates (§4.6) |
| FIRING | clr(v) on valid data | CLEARING (`t1`) | – |
| CLEARING | !clr(v) | FIRING | – (no re-open) |
| CLEARING | `now − t1 ≥ clearSec` | IDLE | **CLEAR** event with `dur`. Mail only if `notifyClear` **and** the open was mailed. |
| FIRING | `repeatMin > 0` and `now − lastMail ≥ repeatMin` | FIRING | **REPEAT** mail ("still active N h") |
| FIRING | the open mail was blocked by cooldown, cooldown has now expired, and the rule is still firing | FIRING | One "still active" mail, so a lasting condition is never silently lost |
| FIRING (day/month gt/ge) | immediately after OPEN | LATCHED (`pk`) | No clear and no repeat until the period changes |
| LATCHED / DONE | period key changes (local midnight, or the cycle start) | IDLE | Silent. For month rules: event `st:"info"` "period reset". |
| IDLE (`mode:at`) | first valid sample with local hhmm ≥ `at` and not yet evaluated in this period | LATCHED or DONE | If cond → OPEN. For `lt`/`le` energy rules, coverage < `minCov` → event `em:"skip"` "coverage 62 %", no mail. Late check (device was down) allowed until 23:59, with "checked late at hh:mm" in the text. |
| FIRING | window ends | IDLE | Silent end event "window ended", no mail |

Further semantics:
- `cond(v)` is `v > value`, `v ≥ value`, and so on. `clr(v)` uses `clearValue`, or `value ∓ default hysteresis` (§2.1).
- Boolean metrics: `cond = (v == 1)`, `clr = (v == 0)`.
- Durations use the monotonic `millis()`. Windows and period keys use local time (TZ `<+06>-6`, d1 §5.6), so an NTP correction never stretches a hold [INFERRED].

### 4.3 Daily and monthly resets

- **Day scope:** at local midnight, during the `aggTask` day rollover. The period key is `yyyymmdd`. This hook also closes the day into the MTD blob (§2.3) and resets the per-rule `cnt`.
- **Month scope:** at 00:00 on `monthStart`. The period key is the cycle-start date.
- **E-mail counters** (`status.em`) reset at local midnight. The relay's own quota is a rolling 24 h window, which is why the cap is 40 (A6).

### 4.4 Reboots

- **Persist on transitions only**, plus at most once a minute for `cnt`. NVS blob `rule/st`: 16 × `{u8 slot; u32 semHash; u8 st; u32 since; u32 lastMail; u32 pk; u8 cnt; u32 cntDay; u8 openMailed}` ≈ 23 B each, about 370 B [INFERRED].
- `semHash` is FNV-1a over `metric, scope, op, value, unit, mode, at, forSec, clearValue, clearSec, window`. Editing the name, channels, recipients, severity or cooldown keeps the state, so there is no re-fire storm after a cosmetic edit.
- **On boot:**
  - Load the cached RuleSet from LittleFS `/cfg/alerts.json`, so rules work before the network is up.
  - Restore LATCHED, DONE (with the same `pk`), `lastMail`, `cnt` (if the same day) and `openMailed`.
  - Restore FIRING as **FIRING-unconfirmed**. If the first valid samples show clr(v), the rule goes to CLEARING normally. If they show cond(v), the rule stays FIRING **without** a new mail.
  - PENDING is not restored; the hold restarts.
- **Result:** "Daily grid > 5 kWh" cannot e-mail twice in one day across reboots, and cooldowns survive boot loops.
- The e-mail day counter (`mail/day`, `mail/cnt`) is also in NVS and is written once per sent mail. That is at most 40 writes a day, which is acceptable for NVS wear [INFERRED].

### 4.5 Missing data (NaN)

- **Instantaneous metric NaN** (no valid sample, `lk = 0`, a gated value, `tmp` > 150):
  - PENDING keeps its timer frozen for up to 30 s (6 samples), then returns to IDLE.
  - FIRING and CLEARING are frozen. A rule never clears on missing data.
  - LINK_LOST, a built-in alert, tells the user why.
- **Energy metrics** are never NaN, but they stop growing while the link is down. `gt` rules may fire late but never falsely. `lt`/`le` deadline checks need `minCov` (default 80 %), and the e-mail always shows coverage.
- **Unknown metric or unit** (firmware older than the PWA's catalogue): the rule becomes `inv` with `err:"metric gw unsupported (fw 0.9)"` and one `RULE_INVALID` event per rev. The PWA hides metrics newer than `status.acat`.
- **Time not valid:** window, `at`, day and month rules are in `wait`. Mails are queued until `TIME_OK`, because the relay checks timestamp freshness.

### 4.6 Notification gates

Gates apply in this order, and every outcome is logged in the event `em`:
1. `ch.email` and `alerts/email/on`, else `off`.
2. Per-rule cooldown since the last mail (clear mails exempt), else `cool`.
3. Per-rule `maxPerDay`, else `rcap`.
4. Quiet deferral (`quiet:"defer"` inside the `cfg.quietFrom`–`quietTo` window): the job gets `notBefore = quietTo:00`. Deferred jobs are merged into one digest.
5. Global cap at send time (MailQueue):
   - Normal jobs are allowed while `sent + nRcpt ≤ cap − 5`.
   - Severity ≥ 4 may use the last 5.
   - On the first refusal of the day, one `EMAIL_CAP` event plus a single "limit reached" mail to recipient 0 is sent from the reserve. After that: `cap`.
6. **Coalescing:** jobs created within 60 s for the same recipient set become one e-mail ("3 alerts: …"). Test and summary mails are never merged.
7. **Spacing:** at least 20 s between sends.
8. **Relay floor:** if the relay reports `rem < 10`, only severity ≥ 4 is sent.

### 4.7 De-duplication and delivery

- The state machine gives one OPEN per episode, and LATCHED gives one per period.
- Job key: `inv1:<rid>:<openTs>:<o|c|r>` (≤ 250 characters, the CacheService key limit [CONFIRMED-primary]). The relay remembers keys for 6 h (21,600 s maximum [CONFIRMED-primary]) and answers `dup`, which the device treats as sent.
- **Spool:** `/mail/<seq>.json` on LittleFS, at most 32 jobs and 256 KB, holding the rendered subject, text and HTML.
  - Expiry: rule and built-in jobs after 12 h (`em:"exp"`), summary after 18 h, test after 15 min.
  - When full: drop the oldest severity ≤ 2 job, then the oldest job.
- **Retry:** back-off of 1, 5, 15 and 60 minutes, at most 5 attempts, then `fail` plus `EMAIL_FAIL` (at most hourly).
- Delayed mails carry "Delayed 2 h 13 min (internet was down)".
- **Independence from Firebase:** rule e-mails do not depend on RTDB being reachable. If Spark is shut off (V2 F9), the cached rules plus the relay still work; only events and alertState stop [INFERRED].

### 4.8 Built-in alerts by e-mail

When `builtin.minSev > 0`:
- The d1 §7.2 alerts (INVERTER_FAULT, BATT_*, GRID_LOSS/RESTORED, OVERLOAD, …) at or above that severity also produce a mail job on open.
- With `builtin.clear`, they also mail on clear or restore, for example "Grid restored after 2 h 14 min".
- They keep their own debounce (d1 §7.3) and use the same caps and reserve.

### 4.9 Test e-mail

1. The PWA writes `cfg/testEmail = floor((Date.now() + serverTimeOffset)/1000)` and `cfg/testRule = '' | rid`. `serverTimeOffset` comes from `.info/serverTimeOffset` [INFERRED SDK feature], so the ±300 s rule tolerates a wrong phone clock.
2. Within 60 s the device sees `testEmail > lastTest` (in NVS) and `now − testEmail < 600 s`.
3. It renders:
   - the test template, sent to `alerts/email/to`; or
   - with `testRule` set, that rule's template using current values, subject prefixed `[TEST]`, sent to that rule's recipients.
4. Limits: at most 5 tests a day, counted in the cap.
5. The device writes event `events/<today>/et-<testEmail>` with `em:"sent"` or `"fail"` plus the reason in `msg`. The PWA listens on exactly that key (§6.4).

### 4.10 Daily summary mail

- At `summary.hour` (default 07:00 local), once per day (NVS `mail/sumDay`), for **yesterday**.
- If the device was offline at that hour, it sends on reconnect until 23:59, otherwise it skips the day.
- Data comes from the day-rollover snapshot (`yday` struct) plus the MTD blob plus per-rule `cnt` for yesterday. No RTDB read is needed.

### 4.11 Engine pseudo-code (load-bearing parts)

```cpp
void RuleEngine::onSample(const Sample& s, const EnergyView& e, uint32_t ms, time_t ep, bool timeOk) {
  for (const RuleDef& r : rs->rules) {
    RuleRt& t = rt[r.slot];
    if (!r.on || r.invalid)                   { endEpisode(t, r.invalid ? INV : OFF); continue; }
    if (r.needsLocalTime() && !timeOk)        { t.st = WAIT; continue; }
    uint32_t pk = periodKey(r, ep);           // yyyymmdd | cycle-start date | 0
    if (pk != t.pk) { if (t.st == LATCHED || t.st == DONE) t.st = IDLE; t.pk = pk; t.cnt = 0; persist(t); }
    float v = metric(r, s, e);                // NaN if unknown; energy from day running or MTD
    bool win = inWindow(r, ep);
    if (r.mode == AT) { if (t.st == IDLE && localHHMM(ep) >= r.at) checkAt(r, t, v, e.coverage(r.scope)); continue; }
    if (isnan(v)) { if (t.st == PENDING && ms - t.lastValidMs > 30000) t.st = IDLE; continue; }
    t.lastValidMs = ms;
    switch (t.st) {
      case IDLE:     if (win && cond(r, v)) { t.t0 = ms; t.st = PENDING; } else break;   // fallthrough when forSec==0
      case PENDING:  if (!win || !cond(r, v)) { t.st = IDLE; break; }
                     if (ms - t.t0 >= r.forSec * 1000UL) open(r, t, v, ep);              // -> FIRING or LATCHED
                     break;
      case FIRING:   if (!win) { endEpisode(t, IDLE, "window ended"); break; }
                     if (clr(r, v)) { t.t1 = ms; t.st = CLEARING; break; }
                     maybeRepeatOrDeferredOpen(r, t, v, ep);
                     break;
      case CLEARING: if (!clr(r, v)) { t.st = FIRING; break; }
                     if (ms - t.t1 >= r.clearSec * 1000UL) clear(r, t, v, ep);           // -> IDLE
                     break;
      default: break;                                                                     // LATCHED/DONE/WAIT
    }
  }
}
```

---

## 5. E-mail content

The templates live in `shared/email-templates.json`. They use `{{key}}` placeholders only and contain no logic: optional lines arrive pre-rendered or empty. The firmware renders them into PSRAM buffers of about 4 KB. Every value is HTML-escaped in the HTML part. The subject is ASCII-only (no RFC 2047 encoding needed for SMTP-direct), at most 120 characters, with CR and LF stripped [INFERRED].

Formatting rules:
- Energy at or above 1000 Wh is shown as kWh with 2 decimals.
- Power: W, integer, with thousands separator.
- V: 1 decimal. %: integer.
- Local time like "Tue 21 Oct 2026, 18:42" (Asia/Dhaka, 24 h).
- Durations like "1 h 05 min".
- Mode letters are decoded with the d1 §3.8 table: L = Line (grid), B = Battery, F = Fault, and so on.

### 5.1 Subjects

| Kind | Template | Example |
|---|---|---|
| open | `[{{tag}}] {{sevPrefix}}{{name}}: now {{valFmt}}` | `[Solar] Daily grid usage > 5 kWh: now 5.01 kWh` |
| open, severity ≥ 4 | `sevPrefix` = `WARNING: ` or `CRITICAL: ` | `[Solar] WARNING: SOC < 30 %: now 28 %` |
| repeat | `[{{tag}}] Still active: {{name}} ({{heldFmt}})` | `[Solar] Still active: Load > 2500 W for 5 min (1 h 10 min)` |
| clear | `[{{tag}}] Cleared: {{name}} after {{durFmt}}` | `[Solar] Cleared: SOC < 30 % after 2 h 05 min` |
| batch | `[{{tag}}] {{n}} alerts: {{names}}` (trimmed) | `[Solar] 2 alerts: Load > 2500 W for 5 min; Inverter temp > 70 C` |
| test | `[{{tag}}] Test e-mail - alerts are working` | |
| summary | `[{{tag}}] {{ydayShort}}: PV {{epv}} \| Load {{eld}} \| Grid {{egr}} est.` | `[Solar] Mon 20 Oct: PV 12.84 kWh \| Load 9.71 kWh \| Grid 1.22 kWh est.` |

`{{tag}}` = `alerts/email/fromName`, default "Solar". The same name is used as the sender display name.

### 5.2 Open: plain text (rendered example, r03)

```
Daily grid usage > 5 kWh

Grid energy today (est.): 5.01 kWh   (rule: above 5.00 kWh, today)
Crossed at: Tue 21 Oct 2026, 18:42 (Asia/Dhaka)
Severity: notice | Rule r03 | This rule e-mails at most once per day

Right now (18:42):
  Battery   78 %   26.4 V   charging 317 W
  Solar     465 W           today 1.92 kWh
  Load      512 W           today 2.08 kWh
  Grid      231 V           today 5.01 kWh (est.)
  Mode      Line (grid)

Open the dashboard: https://savx-logger.web.app/#/alerts?r=r03

--
Sent by your solar datalogger (inv1). Replies go to the owner.
You get this because the owner added you to the alert list for this logger.
To stop: ask the owner to remove you (Alerts > E-mail) or to turn this rule off.
Grid figures are estimates from the inverter's own readings and do not include loads that are not on the inverter output.
```

Optional lines, which are empty when they do not apply:
- `{{heldLine}}`: "Above 2,500 W for 5 min 00 s (since 18:37)"
- `{{delayLine}}`: "Delayed 37 min - the logger was offline."
- `{{covLine}}`: "Data coverage today: 62 %."
- `{{nextLine}}`: "Next e-mail for this rule no sooner than 19:42."

### 5.3 Open: HTML (template, about 2.5 KB rendered, well under the 200 KB Apps Script body limit)

```html
<!doctype html><html><head><meta charset="utf-8"><meta name="color-scheme" content="light dark"></head>
<body style="margin:0;padding:16px;font:15px/1.45 -apple-system,Segoe UI,Roboto,Arial,sans-serif;color:#1a1a1a">
<div style="max-width:560px;margin:0 auto">
 <p style="margin:0 0 4px;font-size:12px;color:#666">{{tag}} &middot; {{sevWord}} &middot; rule {{rid}}</p>
 <h1 style="margin:0 0 10px;font-size:20px">{{name}}</h1>
 <p style="margin:0 0 14px;font-size:17px"><b>{{metricLabel}}: {{valFmt}}</b> <span style="color:#666">(rule: {{opWord}} {{thrFmt}}{{scopeWord}})</span></p>
 <table role="presentation" style="border-collapse:collapse;font-size:14px">
  <tr><td style="padding:2px 12px 2px 0;color:#666">When</td><td>{{whenLocal}}</td></tr>
  {{heldRow}}{{delayRow}}{{covRow}}
 </table>
 <h2 style="margin:18px 0 6px;font-size:15px">Right now ({{nowHHMM}})</h2>
 <table role="presentation" style="border-collapse:collapse;font-size:14px">
  <tr><td style="padding:2px 12px 2px 0;color:#666">Battery</td><td>{{soc}} % &middot; {{bv}} V &middot; {{bwWord}}</td></tr>
  <tr><td style="padding:2px 12px 2px 0;color:#666">Solar</td><td>{{pvw}} W &middot; today {{epvK}} kWh</td></tr>
  <tr><td style="padding:2px 12px 2px 0;color:#666">Load</td><td>{{w}} W &middot; today {{eldK}} kWh</td></tr>
  <tr><td style="padding:2px 12px 2px 0;color:#666">Grid</td><td>{{gv}} V &middot; today {{egrK}} kWh (est.)</td></tr>
  <tr><td style="padding:2px 12px 2px 0;color:#666">Mode</td><td>{{modeWord}}</td></tr>
 </table>
 <p style="margin:20px 0"><a href="{{appUrl}}#/alerts?r={{rid}}" style="display:inline-block;padding:10px 16px;background:#0b6bcb;color:#fff;text-decoration:none;border-radius:8px">Open dashboard</a></p>
 <p style="margin:0;font-size:12px;color:#666">Sent by your solar datalogger ({{dev}}). Replies go to the owner. You get this because the owner added you to the alert list; to stop, ask the owner to remove you (Alerts &gt; E-mail) or turn this rule off. Grid values are estimates and exclude loads not on the inverter output.</p>
</div></body></html>
```

### 5.4 Clear, repeat and batch

- **Clear body:** "SOC is back to 36 % (clear level 35 %). The alert lasted 2 h 05 min (16:10–18:15)." Then the snapshot and footer.
- **Repeat body:** "Still above 2,500 W: now 2,710 W, for 1 h 10 min. Next reminder 20:52." Then the snapshot.
- **Batch body:** one header block per alert (name, value vs threshold, time), then **one** shared snapshot and footer.

### 5.5 Test e-mail

"This is a test from your solar datalogger (inv1), requested from the dashboard at 14:02. If you can read this, alert e-mails reach you. Current state: SOC 78 %, Solar 465 W, Load 512 W, Mode Line (grid). Recipients: owner@gmail.com, family@gmail.com. E-mails sent today: 4 of 40."

With `testRule` set, the subject is `[TEST]` followed by the rule's open subject, and the body is the rule template with live values.

### 5.6 Daily summary (yesterday)

Plain text. The HTML version is the same table with inline styles.

```
Your solar day - Mon 20 Oct 2026

                       Yesterday   Day before   Month to date (20 days)
Solar (logger)         12.84 kWh   11.90 kWh    231.50 kWh
Solar (inverter)       12.70 kWh   -            -          (only if the inverter reports it)
Load                    9.71 kWh    9.20 kWh    188.30 kWh
Grid (est.)             1.22 kWh    0.95 kWh     24.10 kWh
Battery charged         4.10 kWh
Battery discharged      3.05 kWh
Solar share of load    87 % (est.)          = 1 - grid/load
Grid outage            42 min
Peak load / peak PV    1,840 W / 2,410 W
Battery V min / max    24.9 / 28.6 V
SOC min / max          31 / 100 %
Data coverage          100 % (1440 of 1440 minutes)
Alerts                 Daily grid usage > 5 kWh x1, Load > 2500 W for 5 min x1, GRID_LOSS x1

Dashboard: https://savx-logger.web.app/#/energy
-- (same footer)
```

---

## 6. Web UX (iPhone PWA)

### 6.1 Placement

The bottom bar already has six destinations (Live, Today, History, Energy, Events, Settings). Rather than add a seventh, **rename Events to "Alerts"**, route `#/alerts`, with a segmented control at the top: **Rules | History | E-mail**. The existing Events list becomes the History segment [INFERRED].

### 6.2 Rules segment

```
Alerts                      [ Rules | History | E-mail ]
-------------------------------------------------------
(check) Logger has your latest rules - synced 14:03          E-mails today 4 / 40
[ + New rule ]
-------------------------------------------------------
Load > 2500 W for 5 min                          (on)
Load 2,710 W - above for 3:40 of 5:00            PENDING
E-mail, ntfy - 06:00-23:00 - cooldown 1 h
-------------------------------------------------------
Daily grid usage > 5 kWh                         (on)
Grid today 4.62 kWh est.  [#########-] 92 %      IDLE
E-mail - once per day
-------------------------------------------------------
PV today < 3 kWh by 17:00                        (on)
Checked 17:00 - 3.41 kWh - OK                    DONE
-------------------------------------------------------
Logger offline alerts come from Healthchecks.io e-mail (set up there).
```

How the list behaves:
- **Sync banner:**
  - `status.arv === cfg.alertsRev` → "synced hh:mm".
  - Otherwise, "Waiting for logger (checks every 60 s)".
  - If `live.ts` is older than 60 s, "Logger offline since hh:mm - rules apply when it reconnects".
- **Current value:**
  - Instantaneous metrics come from the `live` listener. `gw` comes from `live.gw` if added, otherwise the same formula computed client-side.
  - Day energy comes from `daily/<today>`.
  - Month values come from one `getDailyRange(cycleStart, yesterday)` plus today.
  - The state badge comes from `alertState/<rid>`, which is the device's view and authoritative.
  - The progress bar is shown for `gt` energy rules.
- **Toggle:** `update(devices/inv1, {'alerts/rules/r03/on': v, 'alerts/rules/r03/updatedAt': serverTimestamp(), 'cfg/alertsRev': serverTimestamp()})`.
- **Data cost:** `onValue(alerts)` (≤ 6.5 KB) and `onValue(alertState)` (≈ 1.5 KB), both only while the page is visible, following the d1 §6.4 discipline.

### 6.3 "New rule": presets first, then the editor

The preset sheet lists:
1. Daily grid usage > 5 kWh (`egr`, day, gt 5000)
2. Load > 2500 W for 5 min (`w`, gt 2500, hold 300 s, clear 2200)
3. SOC < 30 % (`soc`, lt 30, 60 s, clear 35, sev 4). The UI hints that the built-in BATT_LOW already pushes via ntfy.
4. PV today < 3 kWh by 17:00 (`epv`, day, lt 3000, at 1700)
5. Inverter temp > 70 °C (`tmp`, gt 70, 120 s, clear 65, sev 4)
6. Monthly grid usage > 150 kWh (`egr`, month, gt 150000)
7. Grid outage today > 3 h (`gout`, day, gt 180)
8. On battery for 2 h (`onbat` true, 7200 s)
9. Evening peak load > 1500 W, 18:00–23:00 (`w`, gt 1500, 60 s, window)
10. Custom…

A preset fills the editor. The user always confirms with Save.

**Editor** (full-screen sheet, sticky Save bar above the safe area, every input at least 16 px):
- **Name:** text, 40 characters, a counter, and a live-validated "no line breaks" check.
- **Metric:** a native `<select>` grouped Power / Battery / Grid / Energy / Device. Each option shows its unit and the current value, for example "Grid energy (est.) - today 4.62 kWh".
- **Period** (energy only): segmented Today | This month. "This month" shows the cycle start ("from 5 Oct").
- **Condition:** segmented `>` `≥` `<` `≤` plus a value field with a unit suffix.
  - `inputmode="decimal"`; both comma and dot are accepted.
  - kWh is converted to Wh on save.
  - For energy rules, `<` and `≤` automatically switch the evaluation mode to "Check at time".
- **Evaluate:** segmented "Continuously" | "Check once at".
  - Continuously: duration `<select>` with 0 s, 30 s, 1, 2, 5, 10, 15, 30, 60 min, 2, 4, 8 h.
  - Check once at: `<input type="time" step="60">`, which gives the native iOS wheel; "17:00" is stored as "1700".
- **Recovers when:** "Auto (2,375 W)" or a custom value, plus a clear-hold `<select>`. Hidden for latched energy rules. The note says: "Fires once per day/month".
- **E-mail frequency:**
  - Cooldown: 15 min, 30 min, 1, 2, 6, 12, 24 h.
  - Repeat while active: Off, 30 min, 1, 2, 6 h.
  - Max per day: 1–10.
- **Active hours:** "All day" switch. When off: from/to time inputs plus day chips Sun–Sat, with Fri and Sat marked as the weekend.
- **Channels:** E-mail (default on), ntfy, Telegram. At least one must be on.
- **Recipients:** "Default list (2)", or "Custom" with up to 3 `type="email" autocomplete="email" autocapitalize="off" spellcheck="false"` fields.
- **Severity:** Info / Notice / Warning / Critical / Emergency (1–5). The help text explains that this affects ntfy priority and the e-mail reserve.
- **Also e-mail when it clears:** toggle.
- **During quiet hours (22–07):** Send now | Hold until 07:00.
- **Preview e-mail:** renders the subject and plain text from the shared templates, using live values.
- **Buttons:** Save, Send test e-mail for this rule, Delete (with confirm).

Validation:
- `web/src/data/alerts.ts` exports `METRICS` (generated) and `validateRule(r): string[]`. These implement exactly the §3.4 constraints. Save stays disabled until there are no errors.
- A `PERMISSION_DENIED` reply is then shown as "The database rejected this rule - please report", because it should be unreachable.
- Save is disabled while `.info/connected` is false: RTDB web writes made offline are not persisted across app restarts [INFERRED].
- **New-slot race:** `runTransaction(alerts/rules/rNN, cur => cur === null ? rule : undefined)`, then the rev bump.

Save code:

```ts
const ts = serverTimestamp();
await update(ref(db, 'devices/inv1'), {
  [`alerts/rules/${r.id}`]: { ...r, createdAt: r.createdAt ?? ts, updatedAt: ts },
  'cfg/alertsRev': ts,
});
```

### 6.4 E-mail segment

- **Master switch:** "Send alert e-mails".
- **Recipients:** up to 3, with add and remove. The whole `to` node is rewritten compacted.
- **Sender name.**
- **Daily summary:** switch plus hour picker (default 07:00, "yesterday's totals").
- **Built-in alerts by e-mail:** Off / Warning and above / Critical only, plus a "Recovery e-mails" switch.
- **Billing month starts on day:** 1–28.
- **Daily e-mail limit:** 1–40, with the explanation: "the relay allows 100 recipients/day; 40 keeps a safety margin".
- **Status line:** "Sent today 4/40 - relay quota left 93 - last failure none", from `status.em`, `emq` and `emf`.
- **Send test e-mail:**
  - Writes `cfg/testEmail` and `testRule ''`.
  - The button is disabled for 60 s, matching the rule.
  - The spinner listens on `events/<today>/et-<ts>`: "Sent to 2 recipients (14:03)" or "Failed: relay auth".
  - After 150 s: "The logger did not answer - it checks every 60 s; last seen 13:58".

### 6.5 History segment

- Last 7 days of `events/<date>` (existing date-bounded reads), filter All / Rules / Built-in / E-mail.
- Each row shows: time, name, value vs threshold, and an `em` chip (sent, cooldown, limit, failed, skipped, delayed).
- Tapping a row opens the rule.
- Existing ack behaviour is kept.

### 6.6 DataSource additions (`web/src/data/source.ts`)

```ts
onAlerts(cb: (v: AlertsDoc | null) => void, err?: ErrCb): Unsub;          // alerts (<= 6.5 KB)
onAlertState(cb: (v: Record<string, AlertStateRec> | null) => void, err?: ErrCb): Unsub;
saveRule(r: AlertRule): Promise<void>;                                     // multi-path + rev bump
setRuleEnabled(rid: string, on: boolean): Promise<void>;
deleteRule(rid: string): Promise<void>;                                    // {'alerts/rules/rid': null, 'cfg/alertsRev': ts}
setEmailCfg(patch: Partial<EmailCfg>): Promise<void>;                      // + rev bump
requestTestEmail(rid?: string): Promise<number>;                           // returns testEmail ts
```

`demoSource.ts` needs matching fakes.

---

## 7. Security and abuse

| Concern | Measure | Tag |
|---|---|---|
| Transport credentials (relay URL and secret, or SMTP app password) | Stored only in device NVS namespace `mail`. Entered through the WiFiManager portal or the admin-protected `/api/config`. Write-only and never echoed or logged (the same rule as d1 §5.13 for `auth=` URLs). **Never** in RTDB, never in the PWA bundle or `.env`. | [INFERRED] |
| Prefer the relay over SMTP-direct | A Gmail app password "gives a less secure app or device permission to access your Google Account" and Google says app passwords "aren't recommended" [CONFIRMED-primary]. NVS is unencrypted (d1 §5.13), so physical theft of the logger would expose a full-mailbox credential. A relay secret only allows capped alert mail. | mixed |
| Relay authentication | Envelope `{"p":"<payload JSON string>","sig":"<hex HMAC-SHA256(secret, p)>"}`. The payload carries `ts` and `k`. The relay recomputes with `Utilities.computeHmacSha256Signature` [CONFIRMED-primary API], rejects `|now − ts| > 900 s`, and dedupes `k` in CacheService for 6 h. The signature goes in the body because Apps Script `doPost(e)` is not documented to expose request headers [INFERRED]. 32-byte random secret; rotate by updating the Script Property and NVS. | mixed |
| Who can change recipients | `alerts` is owner-write only (UID-pinned). The device can read but not write recipients. Other Auth accounts cannot read anything (d1 §4.7, V2 §3.16). | rules |
| A compromised device | It could ask the relay to send to arbitrary addresses. Mitigation: relay **allow-list** Script Property `ALLOW_TO`, holding the same 1–3 addresses entered once, plus per-message ≤ 3 recipients, a ≤ 40/day counter (PropertiesService) and a `getRemainingDailyQuota()` floor. The PWA reminds: "new recipients must also be added to the relay allow-list". Hardened alternative: the relay resolves recipients itself from RTDB with a third read-only `RELAY_UID`, so the device never names recipients. | [INFERRED] |
| Recipient PII | Readable only by the owner and device UIDs. The footer explains why the person receives the mail and how to stop. Replies go to the owner's mailbox. The owner should add only people who agreed. | [INFERRED] |
| Content injection | `name` and `fromName` cannot contain tab, CR or LF (rules regex `(\S| )+`). The ESP HTML-escapes all values and strips CR/LF from the subject. `appUrl` is pinned to `*.web.app` and `*.firebaseapp.com`, so mails cannot carry an arbitrary link. | rules + [INFERRED] |
| Rate limits, database layer | Test e-mails at most once per 60 s, timestamp within ±300 s. At most 16 rules and 3 recipients. Ranges and lengths as in §3.4. | rules |
| Rate limits, device layer | Cooldown ≥ 5 min. `maxPerDay` ≤ 10 per rule. Global ≤ 40 recipient-deliveries per day with a 5-mail severity ≥ 4 reserve. ≥ 20 s spacing. 60 s coalescing. At most 5 tests a day. At most 20 events per rule per day (after that, only `alertState.cnt`). | [INFERRED] |
| Rate limits, relay layer | The same day cap, dedupe, freshness window and quota floor. Body ≤ 20 KB. | [INFERRED] |
| Billing abuse | Rule-denied requests are billed (V2 F3). The PWA validates before writing, and the device never retries a 400 or 401 in a loop (d1 back-off rules). | [CONFIRMED-primary] + design |
| Spoofed "From" and deliverability | Through Apps Script, mail is sent from the owner's own Gmail, so no domain is needed. SMTP providers such as Resend may require a verified domain (not verified). | [INFERRED] |

---

## 8. Transport interface (transport choice decided elsewhere) and quotas

The interface in `src/notify/MailTransport.h`:

```cpp
struct MailJob { char key[64]; uint8_t nTo; char to[3][64]; char subject[128];
                 char* text; char* html; uint8_t sev; uint32_t createdEp, notBeforeEp; uint8_t kind; };
enum class MailResult { Sent, Duplicate, RetryLater, Rejected, QuotaLow };
class MailTransport { public: virtual MailResult send(const MailJob&, int& remainingQuota) = 0; };
// RelayTransport: HTTPS POST to script.google.com (GTS Root R4, already embedded), follow the 302 to
//   script.googleusercontent.com with GET to read {"ok":true,"dup":false,"rem":93} [CONFIRMED-primary redirect behaviour].
// SmtpTransport (fallback): ReadyMail over SSL 465; licence CC BY-NC 4.0 is fine for personal use.
```

| Transport | Free daily limit | Fits the 40/day guard? | Source |
|---|---|---|---|
| Apps Script MailApp (consumer Gmail) | 100 recipients/day; 50 recipients/message; reset 24 h after the first request | Yes; worst-case overlap 80 | [CONFIRMED-primary] |
| Gmail SMTP with an app password | 500 e-mails/day | Yes | [CONFIRMED-primary] |
| Brevo Free | 300/day after account approval | Yes | [CONFIRMED-primary] |
| Resend Free | 100/day, 3,000/month | Yes | [CONFIRMED-primary] |
| ntfy.sh e-mail forwarding | 5/day | Emergency only (for example severity 5 when the relay fails) | V2 I3 [CONFIRMED-primary] |

Resource notes:
- A send is one short-lived TLS session in `uplinkTask`, in the same pattern as ntfy. d1 cites 22–42 KB of heap per session (V2 E9).
- Rendered bodies live in PSRAM, not on the 16 KB stack [INFERRED].

---

## 9. Impact, implementation checklist and tests

**RTDB download** [INFERRED, from measured sizes]:

| Item | Cost |
|---|---|
| `cfg` GET | +62 B × 1440 ≈ 89 KB/day |
| `alerts` GET | 6.5 KB × (4 safety refreshes + edits + boots) ≈ 40–70 KB/day |
| Events | ≤ 20 × 180 B |
| `alertState` writes | Rare |
| Optional `live.gw` | +86 KB/day |
| **Total** | ≈ 0.25 MB/day ≈ 7.5 MB/month ≈ **0.08 %** of 10 GB |

E-mail traffic does not touch RTDB billing.

**Files to add or change.** This is a proposal; nothing was modified.

| Area | Files |
|---|---|
| Shared | `shared/alert-metrics.json` and `shared/email-templates.json`, plus an extension to `tools/gen_codes.py`. It should emit `firmware/include/alert_metrics.h`, `firmware/include/email_tpl.h`, `web/src/alertMetrics.ts` and `web/src/emailTemplates.ts`, and **assert that the metric regex in `database.rules.json` matches the catalogue**. |
| Firmware | `src/alert/RuleEngine.*`, `src/alert/RuleStore.*` (parse, validate, LittleFS cache), `src/alert/RuleStateNvs.*`, `src/agg/MonthToDate.*`, `src/notify/MailQueue.*` (spool, caps, coalescing), `src/notify/EmailRender.*`, `src/notify/RelayTransport.*` / `SmtpTransport.*`. In `config.h`: `EMAIL_CAP_DAY=40`, `EMAIL_RESERVE_SEV4=5`, `MAX_RULES=16`, `MAIL_SPOOL_MAX=32`. A new NVS namespace `mail`, and `rule/st`, `energy/mtd`. |
| Web | `src/data/alerts.ts` (types, `validateRule`), `src/pages/Alerts.tsx` (segments), `components/RuleCard.tsx`, `RuleEditor.tsx`, `EmailSettings.tsx`, `AlertHistory.tsx`. `source.ts` and `firebaseSource.ts`/`demoSource.ts` get the §6.6 additions. `types.ts` `Cfg` gains `alertsRev`, `testEmail`, `testRule`, and its comment requires the rules and firmware to change in the same commit. |
| Rules | Merge §3.4 into `firebase/database.rules.json`. |

**Tests:**
1. **Native Unity** (`env:native`): a table-driven state machine covering:
   - forSec, clearSec and hysteresis;
   - NaN freeze and the 30 s grace;
   - window crossing midnight, and the days mask;
   - day and month latch, and the reset;
   - `at` mode, including late checks and coverage skips;
   - reboot restore (no duplicate mail after FIRING or LATCHED);
   - cooldown "still active" mail, and the repeat timing;
   - cap reserve and coalescing;
   - semantic-hash edits;
   - MTD close-day and bootstrap.
2. **Rules:** `@firebase/rules-unit-testing` against the RTDB emulator (Java 21 is installed on this Mac; firebase-tools is not). Cases:
   - owner can create and toggle a rule;
   - device cannot write `alerts`;
   - `r17` is rejected;
   - recipient key `3` is rejected;
   - `w` with unit `Wh` is rejected;
   - `egr` day `lt` without `at` is rejected;
   - `egr` month `lt` is rejected;
   - `clearValue` on the wrong side is rejected;
   - a name containing a newline is rejected;
   - `testEmail` twice within 60 s is rejected;
   - `testEmail` from a skewed clock is rejected;
   - partial toggle of `on` passes;
   - deleting a rule passes.

   The rules are JSON-checked only; **not emulator-tested yet**.
3. **M7 acceptance** (with d1 §9):
   - A test e-mail arrives within 90 s.
   - The r03 preset with a 0.1 kWh threshold mails once, and a reboot right after it produces no second mail.
   - Pulling Wi-Fi while a rule fires gives one delayed mail with the "Delayed" line after reconnect.
   - 45 forced alerts in one day produce exactly one EMAIL_CAP event and no more than 40 recipient-deliveries.

**Open questions for the user:**
1. Does "usage" mean what the inverter sees (`eld`/`egr`, loads on AC OUT plus charging), or the utility meter? The meter is not measurable with the parts on hand.
2. Billing-cycle start day.
3. Recipient addresses (1–3) and the sender account. The Apps Script relay sends from the owner's Gmail.
4. Daily summary hour (default 07:00).
5. Bangla-language e-mails? Templates are data-driven, so a second template set is cheap.

---

## 10. Sources (fetched or measured 2026-10-21)

- Firebase RTDB REST, ETag and print=silent: https://firebase.google.com/docs/database/rest/save-data ; https://firebase.google.com/docs/reference/rest/database
- RTDB rules language, regex and limits: https://firebase.google.com/docs/reference/security/database ; https://firebase.google.com/docs/reference/security/database/regex ; https://firebase.google.com/docs/database/security/rules-conditions
- Trigger Email extension (Blaze, Firestore, SMTP): https://extensions.dev/extensions/firebase/firestore-send-email
- Firebase JS SDK 13.0.0 typings (`serverTimestamp`, `increment`): `web/node_modules/@firebase/database/dist/public.d.ts`
- Apps Script: https://developers.google.com/apps-script/guides/services/quotas ; https://developers.google.com/apps-script/reference/mail/mail-app ; https://developers.google.com/apps-script/reference/cache/cache ; https://developers.google.com/apps-script/reference/utilities/utilities ; https://developers.google.com/apps-script/guides/content ; https://developers.google.com/apps-script/guides/web
- Gmail limits and app passwords: https://support.google.com/mail/answer/22839 ; https://support.google.com/accounts/answer/185833
- Mail libraries: https://github.com/mobizt/ESP-Mail-Client ; https://github.com/mobizt/ReadyMail
- Providers: https://www.brevo.com/pricing/ ; https://resend.com/pricing
- Earlier project reports (Spark limits, ntfy, TLS roots, data model): `research/v2-platform-verification.md` (F1–F13, I3, I6) and `research/d1-architecture.md` (§3.8, §4.1–4.8, §5.3–5.13, §7)
- Measured today: `openssl s_client` for script.google.com and script.googleusercontent.com (WE2 → GTS Root R4); compact JSON sizes of every example above (python `json.dumps(separators=(',',':'))`)

Working files, not part of the repo:
- The complete rules snippet: `<scratch>`
- An example `alerts` document: `<scratch>`