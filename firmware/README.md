# Savx F4 logger firmware (ESP32-S3-N16R8)

Read-only datalogger for the Savx F4 3 kW / 24 V inverter: Voltronic PI30-family protocol at 2400 8N1 on the COM RJ45 port, logged to LittleFS, uploaded to Firebase Realtime Database (Spark, asia-southeast1), with ntfy / Telegram / Healthchecks alerts and owner-defined usage-threshold rules that queue e-mails for the Apps Script mailer.

Design sources: [d1 architecture](../research/d1-architecture.md) (§3–§5, §7), [r8 e-mail alerts](../research/r8-email-alerts.md) (§3–§4), [v2 platform verification](../research/v2-platform-verification.md), [user constraints](../research/user-constraints.md).

> ⚠️ **Safety.** The firmware is read-only towards the inverter: `INV_ALLOW_WRITES=0` (a build that sets anything else fails), an exact-match command whitelist, and GPIO17 stays a high-Z input until the idle-level test has found the inverter TX pin, the **upper check** has shown the other pin sits below 3.3 V, and you explicitly arm the link. Arming also needs the module self-ID and a passing **pre-arm check** (GPIO17 reads LOW with its pull-down, RO reads LOW); an armed logger repeats the GPIO17 check at every boot. Wire COM pins 1, 2 and 8 only. Never pin 4, never the BMS jack. The cloud, alert and e-mail code never talks to the inverter (the cloud can only ask for a re-probe of whitelisted reads). Details and residual risks: [docs/03 appendix A](../docs/03-hardware-and-wiring.md#appendix-a--safety-review-2026-10-21).

---

## 1. Build, flash, test

The toolchain is pinned in [`platformio.ini`](platformio.ini): pioarduino `55.03.312-1` = Arduino-ESP32 3.3.12 + ESP-IDF 5.5.5, board `esp32-s3-devkitc1-n16r8`, partitions `default_16MB.csv`, ArduinoJson 7.4.3, WiFiManager 2.0.17, ElegantOTA 4.0.0 (sync mode).

Run everything from this folder (`firmware/`). In this repo's dev setup, PlatformIO lives in a scratch folder; prefix each command with `PLATFORMIO_CORE_DIR=<scratch>/pio-core <scratch>/pio-venv/bin/` instead of a global install.

| # | What | Command |
|---|---|---|
| 1 | Build the firmware | `pio run -e savx` |
| 2 | Host unit tests (98 cases, 8 suites) | `pio test -e native` |
| 3 | Build the LittleFS image (local UI) | `pio run -e savx -t buildfs` |
| 4 | Flash the firmware (USB-C port labelled **COM**, CH343 bridge) | `pio run -e savx -t upload` |
| 5 | Flash the local UI into LittleFS | `pio run -e savx -t uploadfs` |
| 6 | Serial console, 115200 baud | `pio device monitor -e savx` |
| 7 | Erase the whole flash (last resort) | `pio run -e savx -t erase` |
| 8 | Reset only the settings (NVS at `0x9000`, size `0x5000`, from `default_16MB.csv`) | `~/.platformio/penv/bin/esptool --chip esp32s3 --port <port> erase-region 0x9000 0x5000` (esptool 5.4.0 of the pinned platform; with a custom `PLATFORMIO_CORE_DIR` use `$PLATFORMIO_CORE_DIR/penv/bin/esptool`; [docs/05 §13.5](../docs/05-firmware-guide.md#135-settings-reset-nvs-only)) |

Results on 2026-10-21 (firmware 0.6.1): build SUCCESS with no compiler warnings, RAM 30.6 % (100,300 of 327,680 B), flash 22.2 % (1,456,227 of 6,553,600 B); `pio test -e native` 98/98; `buildfs` SUCCESS. Large FW-2 buffers (64 KB of request/response scratch, the rule sets, the pending-event list) are allocated from the 8 MB PSRAM at start, so internal RAM grew by only ~5 KB.

OTA after the first USB flash: `http://solar.local/update` (user `admin`, the admin password) with `.pio/build/savx/firmware.bin`.

Build flags you may add in `platformio.ini`:

| Flag | Effect |
|---|---|
| `-DLED_PIN=38` | RGB LED on GPIO38 (DevKitC-1 v1.1) instead of 48 |
| `-DWIFI_TX_POWER_CAP` | Wi-Fi TX power 15 dBm (only if brownouts appear, d1 §5.9) |
| `-DAP_PASSWORD=\"...\"` | Fixed setup-AP password instead of a random one |
| `-DFW_TLS_INSECURE_DEBUG` | ⚠️ Bench only: disables certificate checks (prints a build warning). Never flash this to the installed logger. |

---

## 2. Module map

```
include/
  config.h          pins, timing, alert / rule / uplink constants, INV_ALLOW_WRITES gate
  codes.h           code tables, hand-written to match shared/codes.json (gen_codes.py only generates web/src/codes.ts)
  roots.h           PEM trust anchors per host (section 7)
src/
  main.cpp          boot, safe mode, PSRAM JSON allocator, task start
  app/              App (shared state between tasks), Tasks (invTask, aggTask, uplinkTask), Console
  link/             SerialLink (UART1, whitelist gate), PinDiag (self-ID, idle-level test), RawRing
  proto/            Prober, Scheduler, PI30 / REVO / Q6 / Modbus adapters (read-only)
  model/            Sample, Identity, LiveJson (live + meta JSON), JsonOut, TimeUtil (UTC+6), JsonAlloc
  agg/  Aggregator    energy integration, minute / h15 / daily records, 24 h PSRAM ring
        AlertEngine   built-in alerts of d1 §7.2 (+ e-mail flags per r8 §4.7)
        NotifyGates   quiet hours, 40 pushes/day, 40 mail jobs/day with 5 reserved for sev >= 4
        RuleTypes     rule definitions + metric catalogue (acat 1)
        RuleParse     alerts.json -> rule set, mirrors the RTDB .validate rules
        RuleEngine    usage-threshold state machine (r8 §4.4-4.8)
        MonthToDate   billing-cycle sums for `month` rules
  store/            MinuteQueue (LittleFS files, cursors, anchors), MinuteRec, Settings (NVS)
  net/  WifiProv      WiFiManager portal (non-blocking) + custom fields
        TimeSync      SNTP, TZ <+06>-6, first-sync anchor
        HttpParse     HTTP/1.1 response framing (host-tested)
        HttpsClient   TLS client with pinned roots, keep-alive, timeouts, watchdog feeding
        FirebaseRest  Identity Toolkit sign-in, Secure Token refresh, RTDB REST
        UplinkJson    RTDB paths, keys and bodies (host-tested data contract)
        Backfill      minute upload planning: cursors, h15 closure, day totals (host-tested)
        Budget        quota guard, back-off, network watchdog (host-tested)
        NotifyFormat  ntfy / Telegram bodies (host-tested)
        Notify        ntfy, Telegram, Healthchecks transports
        MailNudge     v1.1 optional mailer nudge (off unless configured)
        Uplink        the uplink state machine (section 4)
  web/LocalWeb      http://solar.local UI + JSON API + ElegantOTA
  sys/              Watchdog, SafeMode, StatusLed
test/               test_frames, test_parsers, test_engine, test_agg, test_rules, test_rule_parse, test_mtd, test_uplink
data/www/           local UI (packed by buildfs)
```

FreeRTOS tasks (d1 §5.3): `invTask` (core 1, prio 5, UART1), `aggTask` (core 1, prio 4: aggregator, AlertEngine, RuleEngine, LittleFS append), `uplinkTask` (core 0, prio 3, 16 KB: **all TLS**), Arduino `loopTask` (web server, portal, console, LED).

---

## 3. Setup portal fields

The portal opens on first boot (no Wi-Fi saved) or when you hold **BOOT (GPIO0) for 3 s** while running. It never opens just because the router is down. Join Wi-Fi `SavxLogger-XXXX` with the password printed on the serial log at boot, then open `http://192.168.4.1`.

Secret fields show empty. Leave a secret blank to keep the stored value; enter `-` to clear any field. Non-secret fields are re-read from NVS each time the portal opens. WiFiManager's own **Update** menu entry is removed and its `/update` and `/u` routes answer 403: firmware is only replaced through the admin-protected `http://solar.local/update`.

| Field | NVS (namespace/key) | Notes |
|---|---|---|
| Admin password | `sys/adminHash` (SHA-256) | ≥ 6 characters. Protects `/update`, `/api/config`, `/api/diag/*`, `/api/link/*`, `/api/probe` (POST). Defaults to the AP password. |
| Firebase Web API key | `cloud/apiKey` | Use the unrestricted "device key" of [r7 §6.2](../research/r7-web-hosting.md) (Identity Toolkit + Token Service APIs only, no referrer restriction). |
| Firebase RTDB URL | `cloud/dbUrl` | `https://<db>.asia-southeast1.firebasedatabase.app`. Only `*.firebasedatabase.app` / `*.firebaseio.com` are accepted (the roots are pinned for them). |
| Device user e-mail / password | `cloud/email`, `cloud/pass` | The dedicated **device** Firebase Auth user, whose UID is `DEVICE_UID` in the rules. Changing either forces a fresh sign-in (`cloud/rtok` is cleared). |
| ntfy topic | `alert/ntfyTopic` | ≥ 24 random characters; the topic is the password. |
| Telegram bot token / chat id | `alert/tgToken`, `alert/tgChat` | Optional; used for severity ≥ 4 only. |
| Healthchecks | `alert/hcUrl` | The check UUID or `https://hc-ping.com/<uuid>` (or `/<ping-key>/<slug>`). Other hosts are refused. |
| Dashboard URL | `alert/appUrl` | Optional `click` link in ntfy notifications, e.g. your PWA's `#/alerts` URL. |
| Mailer nudge URL | `mail/nudge` | Optional (r8 v1.1): `https://script.google.com/macros/s/<id>/exec?k=<NUDGE_KEY>`. Blank = pull mode only (the mailer polls every 5 min). |

The refresh token (`cloud/rtok`) and UID (`cloud/uid`) are written by the firmware. NVS is not encrypted (d1 §5.13): anyone with the board can read these values. The RTDB rules limit what the device user can do.

---

## 4. What the uplink does

```
OFFLINE --Wi-Fi--> (pushes + Healthchecks work without Firebase)
        --cloud configured--> AUTH (refresh token, else sign-in; back-off 5..300 s; bad credentials pause 30 min)
        --ID token--> READY: per 1 s tick, at most 3 RTDB requests, in this order
   1 events     alertQueue -> ntfy / Telegram + PATCH /devices/inv1.json {events/<d>/<key>, mailq/<key>}
   2 live       PUT live.json every cfg.liveSec (>= 60 s at quota level 1, paused at level 2)
   3 minute     PATCH /devices/inv1.json {hist/<d>/<hhmm>, daily/<d>, status, h15/..., alertState/...}
   4 back-fill  same PATCH with up to 60 records of the oldest pending day, >= 2 s apart; cursor after 2xx
   5 cfg        GET cfg.json every 60 s -> thresholds, testAlert, reprobe, alertsRev -> GET alerts.json
   6 house      PUT meta (boot/change), final daily/<yesterday>, month bootstrap GET, diag/raw (10 min),
                prune hist/events after 00:05 (GET ?shallow=true + DELETE <date>)
errors: 401 -> refresh once and retry; 401/403 again = rules refused; 429 / 5xx / network -> back-off 5,10,...,300 s
budget: > 1,500 handshakes or > 20,000 requests a day -> level 1 + QUOTA_GUARD; 2x that -> level 2
network watchdog: Wi-Fi up, no RTDB answer for 15 min -> drop TLS + re-auth; 60 min -> reboot (only if a TLS
                  connect worked in that hour, at most once per 6 h)
```

Every RTDB write uses `?auth=<ID token>&print=silent` (HTTP 204). One TLS connection to the database stays open (keep-alive). Token requests, ntfy, Telegram, Healthchecks and the nudge use short one-shot connections. The HTTP framing is the firmware's own (`HttpParse`), not `HTTPClient`, because of the arduino-esp32 3.3.12 issues documented in r8c (C1, M1).

**Offline behaviour.** Minute records stay in LittleFS (about 40 days of space) and are back-filled in order with no gaps or duplicates. Alerts wait in RAM (24 + 24 slots). Events raised before NTP get their timestamp from the first-sync anchor. Pushes delivered late are prefixed `(14:02, delayed 37 min)`.

**Usage-threshold rules (r8).** The rules are loaded from `/cfg/alerts.json` at boot, before Wi-Fi. They are refreshed when `cfg/alertsRev` changes, at boot, and every 6 h. A bad `alerts.json` keeps the previous set and shows up in `status.ruleErr`. A rule this firmware does not understand becomes state `inv` and raises one `RULE_INVALID` event per `alertsRev`. Each rule event that passes the gates (channel, cooldown, `maxDay`, 40 jobs/day) carries `em:"q"` and gets a `mailq/<key>` job in the same PATCH. The Apps Script mailer then sends it ([r8 §5](../research/r8-email-alerts.md)). Rule state survives reboots (NVS `rules/rsNN`), so "daily grid > 5 kWh" can e-mail at most once a day even through a boot loop.

> ⚠️ **Deploy order (r8c M6): RTDB rules first** ([r8 §3.11](../research/r8-email-alerts.md) merged into [`firebase/database.rules.json`](../firebase/database.rules.json)), then the mailer, then the PWA, then this firmware. Without the new rules, nothing that uses `rid`/`em`/`mailq`/`alertState` can be written. This firmware drops refused `alertState` records rather than stalling the minute uploads, and drops refused events after 3 tries (`status.edrop`).

---

## 5. Serial console (115200 baud)

| Command | Does |
|---|---|
| `help` | List the commands |
| `status` | Link, Wi-Fi, time, memory, queue |
| `live` | Latest sample as JSON |
| `selftest` | Module RO/DI self-ID (receive-only) |
| `idle 1` / `idle 2` | 10 s idle-level test with module A+ on RJ45 pin 1 or 2 (B− on pin 8) |
| `idle 1 3v3` / `idle 2 3v3` | 10 s **upper check** of the RX candidate: A+ on it, B− on ESP 3V3, pin 8 + P5 out of B− (still on GND). `below-3V3` twice is required before arming; `ABOVE-3V3` blocks arming until `reset-link` |
| `loopback yes` | **Bench only, before the module is wired**: GPIO17 jumpered to GPIO18 (transmits QPI). Refused once a self-ID or idle result is stored |
| `arm <pin> YES` | After the idle tests: GPIO17 is wired to RJ45 `<pin>` (inverter RX) → self-ID required, pre-arm check, then TX |
| `arm-max3232 YES` | Optional MAX3232 module that **replaces** the HW-0519 (non-inverted UART). Refused while an HW-0519 self-ID is stored (`reset-link all` first); runs the GPIO17 pre-arm check |
| `probe` | Re-run the read-only protocol probe (armed only) |
| `raw` | Last raw reply per command |
| `reset-link [all]` | Disarm (GPIO17 high-Z) and forget probe/idle results (`all`: also the self-ID) |
| `capture on` / `capture off` | Append every exchange to `/log/raw-0.txt` |
| `portal` | Open the Wi-Fi / credentials portal for 10 min |
| `adminpw <new>` | Set the local admin password (≥ 6 chars) |
| `cfg` | Alert thresholds |
| `cloud` | **FW-2**: uplink state, auth, handshakes/requests today, quota level, backlog, dropped events, channels, last error |
| `rules` | **FW-2**: rules rev, enabled/invalid counts, e-mail on/off, jobs today, per-slot state and e-mails today |
| `test-alert` | **FW-2**: raise `TEST_ALERT` (ntfy push + an e-mail job when alert e-mails are on) |
| `ls` | List LittleFS files (`/`, `/www`, `/q`, `/log`, `/cfg`) |
| `safe-exit` | Leave safe mode and reboot |
| `reboot` | Reboot |

---

## 6. Local web API (`http://solar.local`)

"admin" means HTTP Basic auth, user `admin`, with the admin password.

| Route | Auth | Returns / does |
|---|---|---|
| `GET /` | – | Local UI (from LittleFS; a fallback page if `uploadfs` was not run) |
| `GET /api/live` | – | Latest sample (the `live` keys incl. `gw`) + `age_ms` |
| `GET /api/status` | – | Link, engine, system, uplink (`upState`, `upErr`, `cok`, `hs`, `rq`, `qg`, `backlog`, `edrop`, `evPending`, `notify{ntfy,telegram,healthchecks,nudge,pushOk,pushFail,hcCode}`) and `rules{n,present,invalid,rev,cloudRev,acat,email,mjq,monthStart,err,st:[{rid,st,cnt}]}` |
| `GET /api/identity` | – | `meta` (QPI, QVFW, ratings, dialect …) |
| `GET /api/raw` | – | Last raw reply per command + recent exchanges |
| `GET /api/ring?min=1440&step=60` | – | 24 h chart data from the PSRAM ring |
| `GET /api/hist?date=YYYY-MM-DD` (alias `/api/minutes`) | – | That day's minute records |
| `GET /api/days` | – | Stored days with record counts and upload cursors |
| `GET /api/probe` / `POST /api/probe` | – / admin | Last probe report / re-run the probe |
| `GET /api/diag/pins` | admin | Self-ID, idle results, arm readiness, loopback |
| `GET\|POST /api/diag/selftest`, `/api/diag/idle?pin=1\|2[&ref=3v3]` | admin | Start the receive-only tests (`ref=3v3`: upper check). `/api/diag/pins` lists each result with `ref` and `arm.upperPin` |
| `POST /api/diag/loopback?bench=1` | admin | Bench loopback (refused once the module is wired) |
| `POST /api/link/arm` (`pin=1\|2&confirm=ARM`, or `mode=max3232&confirm=ARM-MAX3232`) | admin | Arm the link |
| `POST /api/link/reset[?all=1]` | admin | Disarm |
| `POST /api/capture?on=1\|0` | admin | Raw capture |
| `GET /api/config` | admin | Thresholds + which secrets are set (`cloudSet`, `ntfySet`, `telegramSet`, `hcSet`, `nudgeSet`) + `appUrl`. Secrets are never echoed. |
| `POST /api/config` | admin | Thresholds (same ranges as the RTDB rules), `adminpw`, and the write-only channel settings `nudge`, `appUrl`, `ntfyTopic`, `hcUrl` (`-` clears). Channel changes reload the uplink. |
| `POST /api/reboot`, `POST /api/safemode/exit` | admin | Reboot / leave safe mode |
| `GET /update` | admin | ElegantOTA |

The HTTPS PWA cannot call these `http://` routes because of mixed-content rules; it only links to them.

---

## 7. TLS roots (`include/roots.h`)

| Bundle | Hosts | Roots |
|---|---|---|
| `GOOGLE` | `*.firebasedatabase.app`, `*.firebaseio.com`, `identitytoolkit.googleapis.com`, `securetoken.googleapis.com`, `script.google.com`, `script.googleusercontent.com`, `smtp.gmail.com` | GTS Root R1, R3, R4 |
| `NTFY` | `ntfy.sh` | ISRG Root X1, X2, Root YR, Root YE |
| `TELEGRAM` | `api.telegram.org` | Go Daddy Root CA G2 |
| `HC` | `hc-ping.com` | USERTrust ECC, USERTrust RSA, Sectigo Public Server Authentication Root E46, R46 |

Every chain was measured on 2026-10-21 and verified against these bundles only. The header lists each root's SHA-256 fingerprint and source. Mozilla's store was cross-checked against Chrome's. The ESP does not check certificate dates (`CONFIG_MBEDTLS_HAVE_TIME_DATE=n`), so re-check the roots **once a year**:

```sh
for h in identitytoolkit.googleapis.com securetoken.googleapis.com <db>.asia-southeast1.firebasedatabase.app \
         ntfy.sh api.telegram.org hc-ping.com script.google.com; do
  echo "== $h"; openssl s_client -connect "$h:443" -servername "$h" -showcerts </dev/null 2>/dev/null | grep -E '^ *[0-9]+ s:|^ *i:'
done
```

If a chain ends at a root that is not in its bundle, add that root's PEM (from https://curl.se/ca/cacert.pem or the CA's own repository) and rebuild.

---

## 8. Data contract deviations

The RTDB keys and paths follow [d1 §4.1–4.2](../research/d1-architecture.md) and [r8 §3](../research/r8-email-alerts.md). The data-contract parts are pinned by `test/test_uplink`. These are the deliberate differences:

| # | d1 / r8 says | FW-2 does | Why |
|---|---|---|---|
| D1 | d1 §4.8: `POST events/<d>.json` (push IDs) | `PATCH /devices/inv1.json` with `events/<d>/<ts>-<rid\|TYPE>-<o\|c\|r\|i>` and, for `em:"q"`, `mailq/<same key>` in the same request | r8 §3.6/E7: deterministic keys make retries idempotent and keep the job and its event atomic |
| D2 | d1 §4.8: back-fill `PATCH hist.json`, then `daily` and `h15` | One multi-path `PATCH /devices/inv1.json` with `hist/…`, the completed `h15/…` and the day totals | Fewer requests and handshakes; same keys and values |
| D3 | d1: `daily/<d>` as one object in every minute PATCH | Full object for the aggregator's running day and for the finished day at the roll-over. For other days, per field (`daily/<d>/epv` …) recomputed from the day file, leaving `nf`/`nw`/`epvi` untouched | Those three are not stored in the minute file; back-fill must not reset them to 0 |
| D4 | d1: `h15` every 15 min from the aggregator | `h15/<d>/<hhmm>` recomputed from the day file when a batch completes the quarter: its minute 14 is present, a later record exists, or the day is over | Works the same for live and back-filled data; never writes a half quarter for an open one |
| D5 | d1 §4.2: `live` always the full sample | With `lk = 0` (no valid reply for 15 s), `live` = `{ts, lk:0, q}` | Stale values are not presented as current |
| D6 | – | `live.gw` added (r8 §3.9); `status` adds `arv`, `acat`, `mjq`, `nrul`, `ruleErr` (r8) plus FW-2 `edrop` (events dropped) and `qg` (quota level 0–2) | All within the existing `$num` / `$k` rules |
| D7 | r8 §3.5 | A deleted rule slot writes `alertState/<rid>: null` | Removes stale badges |
| D8 | d1 §4.6 (b) | Adds quota level 2 at 2× the thresholds (3,000 handshakes / 40,000 requests): live paused, `cfg` every 5 min | A second stop before the runaway case of d1 §4.6 |
| D9 | d1 §5.5: network watchdog reboots after 60 min | Reboots only if a TLS connect worked in that hour, at most once per 6 h (NVS `sys/nwdEp`). Otherwise it drops TLS again. | An ISP outage or load-shedding must not cause hourly reboots and a stream of `LOGGER_REBOOT` events |
| D10 | r8 §4.9: drop an event after 3 permanent errors | Same. Minute records are **never** dropped: refusals and errors back off (max 5 min) and retry | Data is kept in LittleFS; the error is visible in `upErr` / the `cloud` command |
| D11 | d1 §7.1: ntfy with `Title`/`Priority`/`Tags`/`Click` headers | ntfy JSON publish to `https://ntfy.sh/` with the same fields | UTF-8 safe for `°C` and Bangla rule names in titles and messages ([ntfy docs](https://docs.ntfy.sh/publish/#publish-as-json)) |
| D12 | d1 §5.13: GTS Root R1–R4 | GTS Root R1, R3, R4 | R2 is in neither the Mozilla extract of 2026-10-20 nor the Chrome Root Store; the measured chains end at R1 and R4 |
| D13 | r8 §4.1: `include/alert_metrics.h` generated from `shared/alert-metrics.json` | Catalogue in `src/agg/RuleTypes.cpp`; `test_rule_parse` asserts the id list equals the r8 §3.11 metric regex, and checks `firebase/database.rules.json` once the regex is merged there | Avoids editing the shared generator from the firmware task |
| D14 | r8 §4.5 `MtdBlob{…, lastFolded, …}` | `folded` bitmask over the cycle (NVS `energy/mtd`, version 2) | Idempotent and gap-tolerant when days arrive out of order (file gap fill, then RTDB bootstrap) |
| D15 | r8 §4.5 "at" message `<val> at <hh:mm>` | `hh:mm` is the time of the check; a late check adds `, checked late` | Follows both §4.4 and §4.5 |
| D16 | r8 §4.4 | Info events of rules (turned off / changed / deleted / window ended) use severity 1; booleans show `clear level no` / `yes` (the opposite value) | Not specified in r8 |
| D17 | Healthchecks "/fail on fault" | `/fail` while the link is armed and running but no valid inverter reply came for > 5 min | Healthchecks then e-mails "down" for a broken COM link as well |
| D18 | r8 §4.11 SMTP fallback | Not implemented (r8 marks it v2, off by default). The v1.1 nudge **is** implemented and is off unless `mail/nudge` is set | v1 = pull mode only |

---

## 9. NVS and flash layout

| Namespace | Keys |
|---|---|
| `sys` | `bootId`, `bootFails`, `safe`, `adminHash`, `adminDef`, `apPass`, `nwdEp` |
| `cloud` | `apiKey`, `dbUrl`, `email`, `pass`, `rtok`, `uid` |
| `alert` | `ntfyTopic`, `tgToken`, `tgChat`, `hcUrl`, `appUrl`, `cfg` (cached cloud thresholds) |
| `mail` | `nudge` |
| `rules` | `rs01`…`rs16` (28 B rule state), `mj` (mail jobs today), `rev` (loaded alertsRev) |
| `link` | probe/arm state, self-ID, idle history incl. upper checks (`idle` blob; a layout change discards it, which only means the tests must be redone) |
| `energy` | 5-min energy checkpoint, `mtd` (billing-cycle sums) |

LittleFS: `/q/<date>.bin` + `.up` (minute records and upload cursors), `/q/p<boot>.bin` (records still without a timestamp), `/q/anchors.bin`, `/log/probe.json`, `/log/raw-*.txt`, `/cfg/alerts.json`, `/www/index.html`.

No secret is compiled into the firmware, so there is no `secrets.h`. The root `.gitignore` still ignores `firmware/include/secrets.h` in case one is ever added.

---

## 10. Known limits and open items

- **First hardware run (2026-10-21, 0.6.2).** Board: ESP32-S3 rev 2, 16 MB flash, 8 MB PSRAM, CH343P on the "COM" port (`/dev/cu.usbmodem…`). Fixes found on the board:
  - 0.6.1 boot-looped: `WebServer::begin()` ran before the Wi-Fi driver had started lwIP and asserted in `sys_mutex_lock`. `setup()` now calls `WiFi.mode(WIFI_STA)` before the local web server starts.
  - The setup AP name used a placeholder MAC (`SavxLogger-A5A5`/`-0000`); it now reads the eFuse MAC (`esp_read_mac`), e.g. `SavxLogger-XXXX`.
  - `ArduinoClock::sleepMs` fed the task watchdog from unsubscribed tasks ("task not found" log spam); it now checks `esp_task_wdt_status()` first.
  - `Settings::begin()` creates all NVS namespaces, and reads/removes of missing keys are guarded with `isKey()`, so a fresh board no longer logs `nvs_open`/`nvs_get_*`/`nvs_erase_key ... NOT_FOUND`.
  - After the crash loop the board entered safe mode (bootFails 138); the console command `safe-exit` cleared it. Verified on the board: portal → Wi-Fi join → `http://solar.local` and `/api/status` OK, NTP time, link NOT armed, GPIO17 high-Z.
- **Cloud path not yet run on hardware.** The cloud path is verified with host tests, a clean build and the TLS chain checks above. The first bench run (M5/M7 in [docs/07](../docs/07-milestones-and-test-plan.md)) still has to confirm: keep-alive with RTDB, heap use with two TLS sessions open, the token endpoints' real responses, and the push channels from your ISP. Telegram may be blocked in Bangladesh (V2 §3.4).
- An estimated (`tq:0`) record from a boot that never got the time can overwrite a real record already uploaded for the same minute (d1 §5.6 edge case). Inside one batch, the real record always wins.
- The `.DS_Store` file in `data/` is packed into the LittleFS image. It is harmless; delete it if you like.
- **Safety review (0.6.1)**: upper check, pre-arm check (arm + every armed boot), self-ID required for arming, loopback and MAX3232 gates, early DI drive in `setup()`, NVS pin sanitising, and `QMCHGCR`/`QMUCHGCR`/`QBEQI`/`QLT`/`QEMyyyymm`/`QEYyyyy` dropped from the whitelist (it now equals the set of commands the firmware sends). See [docs/03 appendix A](../docs/03-hardware-and-wiring.md#appendix-a--safety-review-2026-10-21) for what was checked and the residual risks.
- **Setup-portal hardening (final consistency pass, still 0.6.1)**: WiFiManager's own unauthenticated firmware uploader is hidden and its `/update` and `/u` routes answer 403 while the portal runs (an on-demand portal is also reachable from the home LAN); the non-secret portal fields are re-read from NVS each time the portal opens, so saving the portal no longer writes back stale Healthchecks or dashboard URLs. Not yet tried on the board.
