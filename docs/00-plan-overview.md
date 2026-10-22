# 00 – Plan overview

This is the executive summary of the plan for the Savx F4 WiFi datalogger. The full decision record is [`research/d1-architecture.md`](../research/d1-architecture.md). Where this page and D1 differ, D1 wins. Where D1 and the verification reports ([V1](../research/v1-protocol-verification.md), [V2](../research/v2-platform-verification.md)) differ, the verification reports win.

Two later decisions extend D1 and win over it where they differ: [R7 – web hosting and CI/CD](../research/r7-web-hosting.md) (the dashboard moves from Firebase Hosting to Cloudflare, deployed by GitHub Actions) and [R8 – e-mail usage-threshold alerts](../research/r8-email-alerts.md) (rules set in the web app, e-mail sent by a Google Apps Script mailer).

---

## 1. Executive summary

- **What:** an ESP32-S3 reads the Savx F4 inverter's free **COM** port (RS232) every 5 seconds. It aggregates the readings into 1-minute records, keeps about 40 days of them in flash, and uploads them to a free **Firebase Realtime Database**. An **iPhone Home Screen web app**, hosted free on **Cloudflare**, shows live power flow, today's charts, history and energy totals. Faults, low battery and grid loss trigger push alerts through **ntfy** (with Telegram as backup), and **Healthchecks.io** e-mails you if the logger itself goes silent.
- **Usage-threshold e-mails:** in the web app's **Alerts** page you set up to 16 rules such as "daily grid usage > 5 kWh", "load > 2500 W for 5 min" or "SOC < 30 %". The logger evaluates them every 5 s, also while the internet is down. A free **Google Apps Script mailer**, running every 5 minutes in a dedicated Gmail account, sends the e-mails, a daily summary and a "logger offline" warning. Firebase Spark has no Cloud Functions, so this is the only free way to send e-mail without a server of our own ([R8](../research/r8-email-alerts.md)).
- **Pipeline:** the code lives in a private GitHub repository. GitHub Actions checks every change, deploys the dashboard to Cloudflare and deploys the database rules to Firebase ([R7](../research/r7-web-hosting.md)).
- **Why DIY:** the inverter has no WiFi, USB or Bluetooth. The official WiFi collector would have to be bought, and no source or price for it was found. A DIY logger built from parts you already own is the only route that needs no purchase.
- **The key trick ("RS232-lite"):** you have an RS485 module, not an RS232 converter. Its differential receiver can still *read* the inverter's RS232 transmit line. In the other direction, the ESP32's inverted 3.3 V output is enough to drive the inverter's RS232 *receive* input directly. A one-time **idle-level test** finds which RJ45 pin is the inverter's transmit line before any ESP32 pin is connected to it. A MAX3232 module is the clean upgrade if you ever get one.
- **The protocol:** the manual points to Voltronic's WatchPower software, so the inverter almost certainly speaks the **Voltronic PI30 ASCII protocol at 2400 8N1** (`QPIGS`, `QPIRI`, `QMOD`, …). Whether it uses Sorotec's "REVO" variant (`QALL`) is about 50/50. The firmware detects the variant automatically.
- **Safety posture:** read-only by construction. On the COM jack only pins 1, 2 and 8 are ever connected, and the BMS jack is never touched.
- **Cost:** zero. All cloud services are free tiers, and the design uses less than 5 % of Firebase Spark's download allowance in normal use.

---

## 2. Goals and non-goals

| # | Goal | Measure of success |
|---|---|---|
| G1 | See the inverter's live state on the iPhone, from anywhere | `live` updates every 10 s; a "Logger offline" banner appears within 70 s of an outage |
| G2 | Keep long-term history | 1-minute data for 400 days and 15-minute data and daily totals forever, all within the Spark free tier |
| G3 | Don't lose data during load-shedding or ISP outages | A LittleFS queue holds about 40 days. Back-fill leaves no gaps and no duplicates |
| G4 | Get alerts for faults, low battery, grid loss and a silent logger | ntfy push within 30 s; Healthchecks e-mail when pings stop |
| G5 | Never harm the inverter, battery or person | Read-only whitelist; pins 1/2/8 only; BMS untouched; no covers opened |
| G6 | Use only owned parts and free services | No purchases; no paid plan |
| G7 | Usage-threshold alerts by e-mail, configured from the web app | A rule saved in the app reaches the logger within 60 s; its e-mail arrives within about 6 min ([R8](../research/r8-email-alerts.md) §8.3 A3–A5) |
| G8 | Repeatable, checked deploys of the dashboard and the rules | Every push to `main` runs CI; the PWA and the rules deploy from GitHub Actions, never by hand-editing live config ([R7](../research/r7-web-hosting.md) §4) |

**Non-goals for v1:**
- Changing inverter settings remotely. This is deliberately impossible: `INV_ALLOW_WRITES=0`.
- Reading the battery BMS directly. The BMS cable must not be disturbed.
- Web Push, because it needs a server holding credentials and Spark has no Cloud Functions. E-mail (through the Apps Script mailer) and ntfy cover the phone instead.
- Sending e-mail directly from the ESP32 (Gmail SMTP). R8 specifies it as an opt-in v2 fallback; it is not built.
- A custom domain for the dashboard. It would need a bought domain, which breaks the zero-cost rule.
- Home Assistant integration.
- Multiple inverters.

---

## 3. Hard constraints

These come from the user and override everything else. The source is [`research/user-constraints.md`](../research/user-constraints.md).

| # | Constraint | Consequence for the design |
|---|---|---|
| 1 | **No new purchases.** Owned parts: ESP32-S3-N16R8 DevKitC-1 clone with its USB cable, an HW-0519-type RS485↔TTL module, 40 M-M jumpers, 25 mm foam tape, a 5 V/2 A USB charger (US plug), and a 100×68×50 mm ABS box. At home: a spare LAN cable, the paper manual, a Mac and an iPhone. | The "RS232-lite" link. Power comes from the USB charger. Joints are wrapped and taped, plus the module's screw terminals |
| 2 | **No multimeter.** A soldering iron *might* be available from a local shop. | Pins are identified with the ESP32 itself (idle-level test). The baseline needs no solder; soldering is an optional upgrade |
| 3 | The inverter link is the free **COM** RJ45: pin 1 RXD, 2 TXD, 4 +VCC, 8 GND. Which device the RXD/TXD labels refer to is unknown. Only pins 1, 2 and 8 may be connected; pin 4 never. **BMS** is used by the battery and is never touched. | Cable preparation cuts and tapes every other conductor |
| 4 | RS232-lite: the module receiver reads inverter TX on A+ (B− to pin 8). ESP32 GPIO17 (inverted) drives inverter RX, connected **only after** the idle-level test. The module's RO/DI pins are identified on the bench first. | Milestones M1 (bench) → M2 (idle test) → M3 (probe) |
| 5 | Read-only towards the inverter | Compile flag `INV_ALLOW_WRITES=0`, a link-layer whitelist and a unit test |
| 6 | iPhone viewer; 2.4 GHz WiFi; free tiers only | Firebase Spark RTDB (asia-southeast1); Cloudflare Workers static assets (free) for the PWA, with Firebase Hosting as the cold fallback; GitHub Free (private repo, 2,000 Actions minutes a month); Google Apps Script + a free Gmail account for e-mail; ntfy, Telegram and Healthchecks. No card on file anywhere |

---

## 4. Key findings from research

### 4.1 What the inverter is

| Finding | Confidence | Evidence |
|---|---|---|
| Savx F4 3KW is a re-skin of the **Sorotec REVO VM II PRO 1.6/3.2 kW (2025 round-dial)** platform, rebranded by IPS Bazar | Medium-high | Spec numbers, front panel and bottom layout all match. IPS Bazar's own F3 datasheet says "REVO VM II PRO series". Two details don't fit: the case size (manual 344×255×105 mm vs Sorotec's 335×225×100 mm) and the WiFi app ([V1 §2](../research/v1-protocol-verification.md), [R1](../research/r1-oem-identification.md)) |
| Firmware lineage is Voltronic-derived | High | Manual §3.8 says to install WatchPower. The LCD fault codes and the U1/U2 CPU-version display match Voltronic conventions |
| No WiFi, USB or Bluetooth on the unit | Certain | Photo of the bottom panel; the manual's item list ([V1 C4](../research/v1-protocol-verification.md)) |
| Official monitoring = an optional "WiFi collector" + the **SOLAR OF THINGS** app (Shanghai Siseli). It is not SmartESS | Certain (manual) | Manual §3.9; App Store id6445806075 |

### 4.2 Ports

| Jack | Manual and silkscreen | Use in this project |
|---|---|---|
| **COM** (left, free) | RS232: 1 RXD, 2 TXD, 4 +VCC, 8 GND | **Logger link, pins 1/2/8 only** |
| **BMS** (right, grey battery cable) | RS485 + CAN: 1 485-B, 2 485-A, 4 CAN-H, 5 CAN-L | ⚠️ **Never touch** |

The RXD/TXD labels don't say whose point of view they use. Voltronic's own cable drawing suggests **pin 1 is the inverter's transmit line** (medium-high confidence), but the idle-level test proves it before anything is wired. See [03](03-hardware-and-wiring.md).

### 4.3 Protocol

- Voltronic **PI30** ASCII, 2400 baud 8N1, CRC-16/XMODEM. Replies start with `(` and end with CR. Every request frame was recomputed byte for byte in [V1 §4.4](../research/v1-protocol-verification.md).
- Possible dialects: stock 17/21-field, MAX (24-field), PIP-GK, or **Sorotec REVO**. REVO adds `QALL` with a sum+1 checksum and makes QPIGS field 12 PV *power* instead of current. The firmware probes for the dialect in under 15 s. See [02](02-protocol-reference.md).
- Modbus families (SMG, PowMr, SRNE, MUST) are last-resort probes only: their RS485 variants cannot be on an RS232 port.

### 4.4 Corrections to the Gemini claims

The claims are in [`research/gemini-claims.md`](../research/gemini-claims.md). They are unsourced and wrong in places that matter.

| Gemini claim | Reality | Source |
|---|---|---|
| RS232 on "pin 2 TX, pin 3 RX, pin 5 GND" | That is a **DB9** convention. On this RJ45 the signals are on pins 1/2 and GND is pin 8 | Photo; manual §2.3 ([V1 C10](../research/v1-protocol-verification.md)) |
| QPIGS field 15 = battery discharge current | Wrong even for stock PI30: field 15 (1-based) is "battery voltage from SCC" and **field 16** is discharge current. On REVO, field 15 (0-based) is PV Wh today | PI30 2014 doc §2.7 ([V1 C9](../research/v1-protocol-verification.md)) |
| A Modbus map "30001 PV1 power …" | No source exists anywhere. Discarded | [V1 C11](../research/v1-protocol-verification.md) |
| "2400 or 9600 baud" | 2400 is expected. 9600 is only a fallback probe | PI30 docs |
| "RS485 optional expansion card, pin 1 A, pin 2 B" | The only RS485 on this unit is the occupied BMS jack, where pin 1 is **B** and pin 2 is **A** | Silkscreen |

Other corrections adopted from verification (V1 and V2): the RTDB Spark download limit is **10 GB/month**, not 360 MB/day (360 MB/day is the Hosting limit). ESPHome cannot do Firebase Auth. The async web server cannot be mixed with the sync WiFiManager and ElegantOTA. PSRAM uses GPIO35–37, so GPIO16/17/18 are free. The full list is in [D1 §0](../research/d1-architecture.md).

---

## 5. Decisions (ADR summary)

These are from [D1 §1.4](../research/d1-architecture.md).

| ID | Decision | Rejected alternatives | Rationale |
|---|---|---|---|
| D1 | Build a DIY logger | Vendor apps and dongles, Home Assistant, SolarAssistant | The only no-purchase option. You own the data, it works offline, and it needs no App Store |
| D2 | RS232-lite link on COM, pins 1/2/8 only | The BMS port (occupied; the inverter is master there); buying a MAX3232 | No purchases. The BMS link must not be disturbed |
| D3 | PI30-family adapter first. Other protocols are detect-only probes with raw capture | Implementing every Modbus family | The manual and silkscreen point to PI30 over RS232 |
| D4 | Custom Arduino firmware | ESPHome (`pipsolar`) | Firebase auth, back-fill, the probe tree and the inversion handling all need code. ESPHome stays an optional cross-check |
| D5 | Firebase RTDB on Spark, asia-southeast1 | Firestore, Supabase, Blynk, InfluxDB/Grafana | No per-operation caps, only bandwidth and storage. This design uses under 5 % of them. See [04 §10](04-architecture-and-data-model.md) |
| D6 | Hand-written REST client | FirebaseClient 2.2.13 as the primary client | Only 4 endpoints are needed. Hand-written code gives explicit `print=silent`, keep-alive, back-off and quota guards, with deterministic memory. FirebaseClient stays the fallback |
| D7 | Sync web stack (core `WebServer`, WiFiManager, ElegantOTA sync) | ESPAsyncWebServer | ElegantOTA and WiFiManager are sync, and sync and async cannot be mixed |
| D8 | Aggregate on the device. Never store raw 5–10 s samples in the cloud | Raw history in RTDB | Raw history at 10 s would be about 1.19 GB/year. 1-minute aggregates are about 127 MB/year |
| D9 | PWA with a bundled SDK and email/password auth | Google sign-in; SDK loaded from a CDN | iOS standalone apps have trouble with redirect and popup sign-in, and the app shell must load offline |
| D10 | ntfy + Telegram + Healthchecks | iOS Web Push | Web Push needs a sender holding credentials, and Spark has no Functions |
| D11 | Read-only whitelist and compile-time `INV_ALLOW_WRITES=0` | Exposing setters | Setters include `PF` (factory reset) and battery voltages. One mistake could damage the battery |
| D12 | Power from the charger on an inverter-backed socket | A battery buck converter; COM pin 4 | No parts to buy. Pin 4's voltage is unknown |

Decisions added on 2026-10-21 for the two new requirements (hosting with CI/CD, and e-mail usage-threshold alerts):

| ID | Decision | Rejected alternatives | Rationale |
|---|---|---|---|
| D13 | Host the PWA on **Cloudflare Workers static assets** (an assets-only Worker `savx-logger` at `https://solar.<acct>.workers.dev`). **Firebase Hosting** is a cold fallback, deployed by hand only | Vercel Hobby (runner-up), Netlify Free, GitHub Pages, Cloudflare Pages | Static-asset requests are free and unmetered, with no lockout at a limit. It answers from Cloudflare's Dhaka PoP (49–58 ms measured vs 177–196 ms for Vercel via Mumbai). There is no non-commercial clause, and previews would stay under your own subdomain. GitHub Pages needs a public repo and has no custom headers ([R7](../research/r7-web-hosting.md) §1) |
| D14 | **GitHub Actions** in a private repository is the single CI/CD control plane: `ci.yml`, `deploy-web.yml`, `deploy-rules.yml`. Actions pinned by SHA, CLIs pinned to exact versions | Cloudflare Workers Builds; `wrangler-action`; Vercel's Git integration | One place runs the checks for web, rules and firmware. Each deploy repeats its own gate, because GitHub Free has no branch protection on private repos. About 350 of the free 2,000 minutes a month ([R7](../research/r7-web-hosting.md) §4) |
| D15 | Rules deploy only from a **rendered copy**: the committed rules keep the `OWNER_UID` / `DEVICE_UID` / `MAILER_UID` placeholders, and `tools/render-rules.sh` fills them into the git-ignored `.deploy/` | Editing real UIDs into the committed file; a `database` block in `firebase.json` | UIDs stay out of git, and a stray `firebase deploy` can never upload placeholder rules that lock everyone out ([R7](../research/r7-web-hosting.md) §3.6, §6.4) |
| D16 | Two API keys: a **browser key** restricted to the dashboard's origins, and a **device key** (no referrer restriction, sign-in APIs only) for the ESP32, the mailer and `curl` tests | One unrestricted key | The browser key restriction adds abuse friction; the UID-pinned rules remain the real security ([R7](../research/r7-web-hosting.md) §6.2) |
| D17 | E-mail through a **Google Apps Script mailer** in a dedicated Gmail account, polling an RTDB outbox (`mailq`) every 5 min and signing in as a third Firebase user, `mailer`, with least-privilege rules | Brevo, Resend, SendGrid, Mailgun, EmailJS, SMTP2GO, Cloudflare Email, the Firebase "Trigger Email" extension, SMTP from the ESP32 | Free, needs no domain and no card; mail comes from Gmail itself; the ESP32 holds no mailbox secret. The others need a domain, a card or a paid plan, cap too low, or (Trigger Email) need Blaze ([R8](../research/r8-email-alerts.md) §1) |
| D18 | **Usage-threshold rules are evaluated on the ESP32** (`RuleEngine`, next to `AlertEngine`). The mailer only renders, de-duplicates, enforces the caps and recipients, and sends the "logger offline" mail and the daily summary | Evaluating rules in the mailer from 5-min snapshots | The ESP32 has 5 s data, hold and hysteresis semantics, state that survives reboots, and keeps evaluating while offline ([R8](../research/r8-email-alerts.md) §2) |
| D19 | Events get **deterministic keys** `<ts>-<rule or type>-<o\|c\|r\|i>`, written in the same multi-path PATCH as their `mailq` job | Push IDs (D1 §4.8) | Retries overwrite instead of duplicating, and an event and its e-mail job are atomic ([R8](../research/r8-email-alerts.md) §3.6) |

---

## 6. Chosen stack and fallbacks

### 6.1 Chosen stack

From [D1 §1.2](../research/d1-architecture.md).

| Layer | Choice (pinned versions) |
|---|---|
| Inverter link | COM RJ45 pins 1/2/8. The HW-0519 receiver reads inverter TX; ESP32 GPIO17 (inverted) drives inverter RX. UART1 at 2400 8N1 with RX and TX inverted |
| Protocol | Adapter framework, PI30 family first (STOCK17/21, MAX24, GK, REVO). Other families are detect-only. Read-only whitelist at the link layer |
| MCU firmware | Custom Arduino firmware on Arduino-ESP32 **3.3.12** via pioarduino **55.03.312-1**, `board = esp32-s3-devkitc1-n16r8`, `default_16MB.csv` (2 × 6.25 MB OTA + 3.4 MB LittleFS) |
| Device libraries | ArduinoJson **7.4.3**, WiFiManager **2.0.17**, ElegantOTA **4.0.0** (sync), plus core `WebServer`, `HTTPClient`, `NetworkClientSecure`, `Preferences`, `LittleFS`, `ESPmDNS` |
| Cloud client | Hand-written REST: Identity Toolkit sign-in, securetoken refresh, RTDB `.json` with `print=silent`, keep-alive |
| Cloud database | Firebase **Realtime Database**, Spark plan, region **asia-southeast1** (Singapore) |
| Web app | Static PWA built with **Vite 6.4.4** + TypeScript + Preact. **Firebase JS SDK 13.0.0** (bundled) and **uPlot 1.6.32**. Email/password sign-in. Pages: Live, Today, History, Energy, Alerts, Settings |
| Web hosting | **Cloudflare Workers static assets** (`web/wrangler.jsonc`, `web/public/_headers`), deployed with `npx wrangler@4.146.0`. Fallback: Firebase Hosting (`firebase.json`) |
| CI/CD | **GitHub Actions** (private repo): `ci.yml`, `deploy-web.yml`, `deploy-rules.yml` (firebase-tools 15.32.1, keyless Workload Identity Federation or a service-account key), Dependabot monthly |
| Local UI | `http://solar.local` with a JSON API and OTA at `/update`. Works without internet |
| Alerts | **ntfy.sh** primary push, **Telegram** secondary, **Healthchecks.io** dead-man's switch (e-mail), and **e-mail through the Apps Script mailer** for built-in alerts, usage-threshold rules, a daily summary and "logger offline" |
| E-mail mailer | Google Apps Script (`tools/apps-script/`) in a dedicated Gmail sender account: 5-min trigger, `MailApp`, 40 recipient-deliveries a day, recipient allow-list `ALLOW_TO` |
| Power | The 5 V/2 A charger in an inverter-backed **AC OUT** socket. The router or ONU must be on AC OUT too |

### 6.2 Fallbacks

| ID | Trigger | Fallback |
|---|---|---|
| F-A (link) | RS232-lite fails: the module is damaged, or COM is not true RS232 | A mini MAX3232 module (about ৳80, [SECONDARY] listing) if a shop trip ever happens. The firmware is the same with inversion off |
| F-B (vendor) | The DIY link cannot be made to work at all | Ask IPS Bazar for the "Solar of Things" WiFi collector. Only one master is allowed on COM, so the ESP32 would then be removed or switched to listen-only |
| F-C (cloud) | Firebase is too complex, or the Spark policy changes | **ThingSpeak** free (non-commercial): 4 channels × 8 fields at 1/min ≈ 2.1M of the 3M messages/year |
| F-D (local only) | No cloud at all | The ESP32 LAN page with a 24 h chart plus the LittleFS minute history (about 40 days) |
| F-E (web host) | Cloudflare unusable for days, or its free terms change | **Firebase Hosting** (`firebase.json`), deployed by hand. It is a new origin, so every phone re-installs the app ([R7](../research/r7-web-hosting.md) §2.5) |
| F-F (e-mail) | The mailer or the sender account is down | No second e-mail path in v1: ntfy still pushes, Healthchecks still e-mails "logger offline" from its own sender, and the PWA shows "Mailer not running". Gmail SMTP from the ESP32 is an opt-in v2 ([R8](../research/r8-email-alerts.md) §1.3) |
| F-G (CI) | GitHub Actions minutes run out, or GitHub is down | Deploy from the Mac with `npx` ([06](06-cloud-and-dashboard-guide.md) §3.3, §5.4) |
| Upgrade | Server logic, Web Push or cron jobs wanted later | **Cloudflare Workers + D1** free tier |

---

## 7. Architecture

This is the diagram from [D1 §2.1](../research/d1-architecture.md), updated for R7 (hosting) and R8 (e-mail). The data model and failure behaviour are in [04](04-architecture-and-data-model.md).

```
  SAVX F4 3KW  (Sorotec REVO VM II PRO-class platform, PI30-family firmware, 2400 8N1)
  +--------------------------------------------------------------------------+
  | COM RJ45 (RS232):  pin1 RXD    pin2 TXD    pin4 +VCC     pin8 GND         |   BMS RJ45 ==> Li battery BMS
  +-----------|-----------|-----------x(insulated)---|-----------------------+   (RS485/CAN; inverter is
              | inv-TX *  | inv-RX *                 | GND                         master. NEVER unplug)
              |           |                          |
  * which of pin1/pin2 is the INVERTER's TX is found by the idle-level test (M2) before any GPIO touches them
              v           |                          |
   +--------------------+ |                          |
   | HW-0519 RS485<->TTL| |                          |
   |  A+ <--------------+ |                          |
   |  B- <--------------------------------------------+------+
   |  RO  ----> GPIO18 or GPIO16 (firmware detects which)     |
   |  DI  <---- other GPIO held HIGH (driver disabled)        |
   |  VCC <- 3V3, GND <- GND                                  |
   +---------+----------+ |                                   |
             |            |                                   |
   +---------v------------^-----------------------------------v-----------+
   | ESP32-S3-N16R8  UART1 2400 8N1, RX inverted + TX inverted             |
   |   GPIO17 (TX, idle 0 V / start bit 3.3 V) ------------> inv-RX direct |
   |   tasks: inv | agg | uplink | web/loop                                |
   |   LittleFS minute queue (~40 d) | NVS settings | PSRAM 24 h ring      |
   |   power: 5 V USB charger in an inverter-backed (AC OUT) socket        |
   +--------------------------------+--------------------------------------+
                                    | 2.4 GHz Wi-Fi
                                    v
                Home router/ONU (must also be on inverter AC OUT)
                  |                                   |
   LAN: http://solar.local                            | HTTPS (pinned roots: GTS R1/R3/R4, ISRG X1/X2,
   local UI + /api/* + /update (OTA)                  |  Root YR/YE, Go Daddy G2, USERTrust/Sectigo)
                  ^                                   +--> identitytoolkit / securetoken (device sign-in)
                  |                                   +--> Firebase RTDB asia-southeast1 /devices/inv1/...
      iPhone on home Wi-Fi (no internet needed)       +--> ntfy.sh --> Firebase --> APNs --> ntfy iOS app
                                                      +--> api.telegram.org --> Telegram (backup)
                                                      +--> hc-ping.com every 5 min --> e-mail if silent
                                                      +--> (optional) script.google.com nudge, no data
                                                                         ^
   iPhone anywhere: Home Screen PWA https://solar.<acct>.workers.dev | WebSocket (Firebase JS SDK 13)
   (Cloudflare Workers static assets; owner sign-in) --------------------+  reads live/hist/h15/daily/events,
                                                                         |  alerts, alertState, mailer
                                                                         |  writes cfg, alerts/*, mailq/test (owner)
   Google Apps Script mailer (dedicated Gmail) ---- every 5 min ---------+  reads mailq/events/alerts/live/daily
        '--> MailApp --> recipients' inboxes                                writes em/emt, mailq, mailer (heartbeat)

   GitHub (private) --Actions--> Cloudflare (PWA)  and  --> Firebase RTDB rules
```

---

## 8. Milestone roadmap

Acceptance tests for every milestone are in [07 – Milestones and test plan](07-milestones-and-test-plan.md). Effort is counted in evenings of roughly 2–3 hours.

| M | Name | Wiring to the inverter? | Effort, from scratch (D1) | Effort with this repo [INFERRED] | Exit gate (short) |
|---|---|---|---|---|---|
| M0 | Site survey and accounts | None | 1 | 1 | Site survey complete; AC-OUT socket identified; ntfy test received |
| M1 | Bench bring-up (ESP only) | None | 1–2 | 0.5–1 | Optional Arduino IDE board check (16 MB flash, 8 MB PSRAM, RSSI); PSRAM ≈ 8 MB; UART loopback OK; CRC and whitelist tests pass; module RO pin identified |
| M2 | Cable, idle-level test and upper check | Receiver only (A+, B−) | 1 | 1 | Exactly one pin idles LOW (inverter TX), confirmed twice; the other reads below 3.3 V twice (B− on 3V3) |
| M3 | Protocol probe and dialect | Full (GPIO17 → inverter RX) | 1–2 | 1 | QPI valid; QPIGS CRC OK on ≥ 99 % of polls; values match the LCD |
| M4 | Local-only logger | Full | 3–5 | 1 | 24 h with ≥ 99 % valid polls; `http://solar.local` works on the iPhone |
| M5 | Cloud uplink, GitHub repo and CI | Full | 2–3 | 1–2 | Code in the private repo with `ci` green; rules deployed by `deploy-rules`; `live` every 10 s; back-fill after 2 h offline; negative rule tests denied |
| M6 | iPhone PWA on Cloudflare | – | 3–4 | 1 | `deploy-web` publishes the permanent origin; header checks pass; browser key restricted; installs to the Home Screen; live within 15 s; offline banner works |
| M7 | Alerts: push and e-mail | Full | 1–2 for push (D1 has no estimate for the e-mail part) | 2 + 24 h | Test alert in < 30 s; grid-breaker test; Healthchecks e-mail; mailer heartbeat every 5 min; test e-mail within 6 min; one rule e-mail per day, none after a reboot (R8 A1–A13) |
| M8 | Enclosure, install, 7-day soak | Full | 1 + 7 days | 1 + 7 days | ≥ 99.5 % minutes present; box < 55 °C; RSSI > −75 dBm; no duplicate e-mails and no mailer gap > 15 min (R8 A14) |
| M9 | Polish and handover | – | 1 | 0.5 | You can re-provision WiFi unaided; yearly export works; web rollback drill done |

### Effort estimate

- **From scratch** (D1's estimate, writing all the code yourself): about **15–22 evenings**, plus a 7-day soak. D1 gives no figure for M8; one evening is assumed here.
- **With this repo's reference implementation:** about **10–12 evenings**, plus the soak [INFERRED]. The GitHub/Cloudflare set-up and the e-mail mailer with its acceptance tests add about two evenings to the earlier 8–10. Most of the remaining work is physical (survey, cable, idle test, install) and verification (comparing values against the LCD, back-fill and alert tests). Firmware and web work shrinks to configuring, building, flashing and fixing whatever the real inverter's replies reveal.
- **The biggest schedule risk is M2–M3:** the physical link and the protocol dialect. Everything after M3 is ordinary software.

### After v1: Phase 2 (design only)

Remote control of a few inverter settings from the web app (output and charger priority, bounded charging currents, the buzzer) is designed in [10 – Phase 2: remote settings](10-phase2-remote-settings.md). It starts only after M8's 7-day soak, writes nothing if M3 identifies the Sorotec REVO dialect, needs a separate firmware build, and needs a BOOT-button press at the logger to open a short change window. Estimated 10.5–14.5 evenings to the on-site go/no-go and 21.5–29 evenings plus a soak for the full feature. v1 stays strictly read-only (D11).

---

## 9. What success looks like

After the 7-day soak (M8):

1. Open the Home Screen app on the iPhone, anywhere. It shows PV, load, battery and grid power, SOC and the mode. The data is less than 15 s old.
2. Today's chart and the daily kWh bars match the inverter's LCD totals within a few percent. Battery energy is coarser, because battery current comes in 1 A steps.
3. **≥ 99.5 %** of minutes are present in the cloud, after back-fill, across load-shedding and ISP outages.
4. A grid outage produces one `GRID_LOSS` push and one `GRID_RESTORED` push with the outage duration, and no duplicates. Unplugging the logger produces a Healthchecks e-mail and, after the configured minutes, one "Logger offline" e-mail from the mailer.
5. A usage-threshold rule such as "daily grid usage > 5 kWh" e-mails **exactly once** on the day it is crossed, even across reboots. The mailer's heartbeat never goes stale for more than 15 minutes, and the daily summary arrives each morning.
6. Every change you push goes through CI, and the dashboard and the rules on the internet match what is in `main`.
7. No false severity-5 alerts. Link error rate below 1 %. The box stays under 55 °C. WiFi RSSI is better than −75 dBm.
8. Firebase usage stays around **5–15 MB/day** download (the mailer adds about 4 MB/day) and about **0.4 MB/day** storage growth, far inside Spark.
9. Not one byte outside the read-only whitelist was ever sent to the inverter.

---

## 10. Open questions for you

This is D1 §10.2, updated with what is now known.

**Already answered**

| Question | Answer |
|---|---|
| Is COM free and BMS occupied? | ✅ Yes, confirmed by photo: COM is free, and BMS carries the battery's grey cable |
| Is the manual available? | ✅ Yes, the paper manual, scanned to `reference/Savx-F4-user-manual.pdf`. It gives the COM pinout |
| Is a computer available for flashing? | ✅ A Mac |
| Soldering? | ⚪ Maybe, from a local shop. The baseline does not need it |

**Still open.** Most of these are answered during the site survey.

1. **Which sockets are on the inverter's AC OUT?** Is the WiFi router or ONU powered from AC OUT? If not, uploads stop during outages, although logging continues.
2. **LCD readings:** the U1/U2 firmware versions, and settings 01 (output priority), 03 (APL/UPS), 05 (battery type, probably `LIb`), 12/13/29 (SOC or voltage thresholds) and 16 (charger priority).
3. **Does the battery report SOC to the inverter?** Is the LCD's "Li" battery-communication icon lit?
4. **Which App Store country is your Apple ID set to?** ntfy and Telegram can only be installed from a storefront where they exist (for example US or IN). Do you want Telegram at all, given it has been restricted in Bangladesh before?
5. **Does your router have a separate 2.4 GHz SSID?** How far is it from the inverter?
6. **Is the Mac a laptop that can run on battery** during the first tests while it is connected to the wired ESP32? This avoids a ground loop. If it is a desktop Mac, use OTA updates over WiFi once the ESP32 is wired to the inverter.
7. **If you get a soldering iron,** could you also pick up a few 1 kΩ / 4.7 kΩ resistors or a ৳80 MAX3232 module? These are optional and never required.
8. **Should family members see the dashboard?** That means extra Firebase users, each added to the rules.
9. **Is the inverter in a hot spot or in sun?** Which way do the bottom fans blow? This decides where the box goes.
10. **Did a WiFi collector or a communication cable come in the inverter box?**
11. **Which Cloudflare account subdomain do you want?** It becomes part of the permanent dashboard address `https://solar.<acct>.workers.dev`, which is public. Short and neutral, with no address, phone or family names ([R7](../research/r7-web-hosting.md) §2.1). Decide before the first deploy.
12. **Who should receive the alert e-mails?** At most 3 addresses. Each must be in both the app's recipient list and the mailer's `ALLOW_TO` property ([06](06-cloud-and-dashboard-guide.md) §9).
13. **Can you create a dedicated Gmail account for sending?** Google may ask for phone verification. You must sign in to it at least once a year.
14. **Which day does your electricity billing month start?** It sets the "This billing month" rules and the summary (1–28). Remember the grid figures are estimates from the inverter, not your meter.
15. **Do you already have a GitHub account with two-factor authentication?** The repository must be private and under your personal account.
