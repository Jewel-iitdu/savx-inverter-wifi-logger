# 08 – Risks, troubleshooting and maintenance

This document is for when something goes wrong, for the periodic upkeep that keeps a free-tier system alive for years, and for what to do next.

Related docs:
- Wiring and the idle-level test: [03 – Hardware and wiring](03-hardware-and-wiring.md)
- Build, flash, OTA and configuration: [05 – Firmware guide](05-firmware-guide.md)
- Firebase, GitHub Actions, Cloudflare hosting, the PWA and the e-mail mailer: [06 – Cloud and dashboard guide](06-cloud-and-dashboard-guide.md)
- Field meanings, endpoints, e-mail rules and the pipeline: [04 – Architecture and data model](04-architecture-and-data-model.md)
- The decisions behind hosting/CI and e-mail, with their own risk lists: [R7](../research/r7-web-hosting.md) §8 and [R8](../research/r8-email-alerts.md) §9

> ⚠️ **Standing safety rules apply to every step below.**
> - Never open the inverter's covers.
> - Never touch or unplug the **BMS** jack.
> - On **COM**, only pins 1, 2 and 8 are ever connected; pin 4 never.
> - When the Mac is connected to the ESP32 while the ESP32 is wired to the inverter, run the Mac **on battery**, not on its charger.

---

## Contents

1. [Risk register](#1-risk-register) (incl. [hosting, CI/CD and e-mail](#12-hosting-cicd-and-e-mail-risks))
2. [Troubleshooting decision trees](#2-troubleshooting-decision-trees) (incl. [e-mail](#211-e-mail-alerts-not-arriving) and [deploys](#212-deploys-ci-and-hosting))
3. [Maintenance](#3-maintenance)
4. [Recovery procedures](#4-recovery-procedures)
5. [Future upgrades](#5-future-upgrades)

---

## 1. Risk register

L is likelihood and I is impact, rated H/M/L.

### 1.1 Logger, link and platform risks

This is D1 §10.1 ([`research/d1-architecture.md`](../research/d1-architecture.md)).

| # | Risk | L | I | Mitigation | Where |
|---|---|---|---|---|---|
| R1 | An ESP GPIO is connected to the RS232 **TX** pin (negative voltage) by mistake | M | H | Idle-level test first (M2). GPIO17 is connected only afterwards. Checklist and photo before power-on | [03](03-hardware-and-wiring.md) |
| R2 | COM pin 4 (+VCC) touches anything | L | H | Only pins 1/2/8 are stripped. The rest are cut, folded and taped individually | [03](03-hardware-and-wiring.md) |
| R3 | The HW-0519 is overstressed by RS232 negative levels | L–M | M | TVS diodes and polyfuses on the module; a series resistor of ≤ 4.7 kΩ if one becomes available; fallback F-A (MAX3232) | [03](03-hardware-and-wiring.md), [§5.2](#52-max3232-module-fallback-f-a) |
| R4 | The RJ45 pin perspective is the other way round (pin 2 is actually TX) | M | L | The idle-level test finds out. No assumption is wired in | [03](03-hardware-and-wiring.md) |
| R5 | COM is not true RS232 (TTL levels) | L | M | Diagnostic in D1 §8.4 C. Stop rather than risk the ESP | [03](03-hardware-and-wiring.md) |
| R6 | The protocol is not PI30, or the dialect is unknown | L / M | M | Probe tree, raw capture, adapters, daily re-probe | [02](02-protocol-reference.md) |
| R7 | Field meanings misread (REVO field 12, SOC source) | M | M | Field-12 classification rule, LCD cross-check, `diag/raw` kept for remote analysis | [§2.4](#24-values-disagree-with-the-lcd) |
| R8 | A setter command is sent by accident | very L | H | Compile flag `INV_ALLOW_WRITES=0`, link-layer whitelist, unit test | [05](05-firmware-guide.md) |
| R9 | The BMS cable is disturbed | L | H | Documented "never touch". All work happens on COM only | [§5.6](#56-battery-bms-data) |
| R10 | Electric shock (PV up to 500 V DC, 230 V AC, 145 A DC) | L | very H | Never open covers. The RJ45 jacks are on the exposed bottom face | everywhere |
| R11 | A ground loop through a laptop burns the inverter's COM board | L–M | H | Laptop on battery; ESP powered from the isolated USB charger | [03](03-hardware-and-wiring.md) |
| R12 | Wrapped (no-solder) joints loosen, causing an intermittent link | M–H | M | Tape, strain relief, `LINK_LOST` alert, CRC error counters, solder upgrade | [§5.1](#51-soldered-joints) |
| R13 | The router is down during load-shedding | H | L | LittleFS queue (about 40 days) with back-fill; router on AC OUT | [04](04-architecture-and-data-model.md) |
| R14 | Heat above 65 °C ambient (the N16R8 rating) | L–M | M | Placement rules, temperature logging, watchdog | [03](03-hardware-and-wiring.md) |
| R15 | Brownouts or reboots | M | L | Good cable, reset-reason logging, TX power cap | [§2.5](#25-wifi-drops) |
| R16 | Spark quota exceeded, so the database is shut off for the month | L | M | Aggregates only, budgets, back-off, console discipline | [§2.7](#27-quota-warnings) |
| R17 | Firebase/Google policy change (precedent: Storage moved to Blaze on 2026-02-03) | M (over years) | M | Portable JSON model, yearly export, F-C ThingSpeak or Cloudflare D1 | [§3](#3-maintenance), [§5.3](#53-cloudflare-workers--d1) |
| R18 | Google deletes an account after 2 years of inactivity: the Firebase owner account **or the sender Gmail account** | L | H | Sign in to both occasionally (script runs may not count as activity); yearly export | [§3](#3-maintenance) |
| R19 | Telegram blocked in Bangladesh | M | L | ntfy primary; Healthchecks and mailer e-mail | [§2.8](#28-alerts-not-arriving) |
| R20 | ntfy or Telegram not installable (App Store storefront) | M | M | Check the storefront in M0; e-mail path; the PWA needs no store | site survey |
| R21 | Device credentials leak through physical access to NVS | L | L | UID-pinned rules limit the damage; rotate the device password | [§4.4](#44-rotate-the-device-credentials) |
| R22 | A WiFi collector added later fights the ESP as master on COM | L | M | One master only. Remove the ESP or switch it to listen-only | – |
| R23 | The clone's RGB LED is not wired | M | L | The web status page is authoritative | [04](04-architecture-and-data-model.md) |
| R24 | Licensing (ElegantOTA AGPL; Solar2MQTT NC-ND) if the code is ever published | L | L | No Solar2MQTT code copied; comply with AGPL if published | – |

### 1.2 Hosting, CI/CD and e-mail risks

These come from [R7](../research/r7-web-hosting.md) §8 (H-numbers) and [R8](../research/r8-email-alerts.md) §9 (R-E numbers). None of them can affect the inverter: the web app, the pipeline and the mailer have no path to the COM link.

| # | Risk | L | I | Mitigation | Where |
|---|---|---|---|---|---|
| R25 | **Apps Script / Gmail quotas** are lowered or hit: 100 recipients/day, 90 min/day of trigger runtime, 20,000 URL fetches/day for a consumer account. Google says they may change "without notice" (R-E1, R-E4) | L | M | The mailer caps itself at 40 recipient-deliveries/day (5 reserved for severity ≥ 4); `mailer/quota` is shown in the app; below 10 left, only severity ≥ 4 is sent; idle runs make 3 small requests; if runtime grows, change the trigger to every 10 min | [§2.11](#211-e-mail-alerts-not-arriving) |
| R26 | **Google account or policy change** for the sender: account locked, deleted after inactivity, password changed (which may revoke the script's grant), or a changed consent flow for personal scripts (R-E2, R-E12) | L | H | Dedicated sender account with 2-Step Verification; sign in yearly; the PWA shows "Mailer not running" after 15 min; Google e-mails the sender a failure summary; ntfy and Healthchecks stay independent of Google; re-run `sendSelfTest` + `setup` | [§4.9](#49-recover-the-e-mail-mailer) |
| R27 | Alert e-mails land in **Spam**, or iOS Mail fetches late (R-E3) | M | M | Gmail-to-Gmail from a real account, a stable `[Solar]` subject tag, a recipient filter "Never send it to Spam", the Gmail app with notifications | [§2.11](#211-e-mail-alerts-not-arriving) |
| R28 | Too many e-mails: load-shedding "logger offline" mails, a noisy rule, a duplicate after a lost reply (R-E5, R-E10) | M | L | Deterministic event keys, the mailer's de-dup ledger and lock; per-rule cooldown and `maxDay`; offline mails ≤ 4/day and the threshold adjustable (raise to 120 min) | [§2.11](#211-e-mail-alerts-not-arriving) |
| R29 | **Free-tier changes at the web host**: Cloudflare changes the free static-asset terms or suspends the account; or Firebase Hosting's quota changes (H6) | L | M | `dist/` is plain static files; Firebase Hosting is a configured cold standby (`firebase.json`); switching hosts changes the origin, so every phone re-installs | [§4.7](#47-web-rollback-and-host-failover) |
| R30 | **The dashboard origin changes** after phones installed the app: Worker or account-subdomain rename, a custom domain, a host switch (H1) | M | H | Choose `<acct>` once, before the first deploy; never rename `savx-logger`; write the origin in the site survey | [06](06-cloud-and-dashboard-guide.md) §5 |
| R31 | **CI secrets leak**: the Cloudflare API token can replace the code at your origin, which runs inside your signed-in owner session; a `GCP_SA_KEY` (fallback only) bypasses the database rules (H2, H3) | L | H | Secrets only in GitHub Secrets; token scoped to one account with an end date; keyless WIF preferred over a key; 2FA on GitHub, Cloudflare and Google; rotate on suspicion | [§4.8](#48-rotate-ci-and-cloud-secrets) |
| R32 | **Supply chain**: a compromised action, npm package or CLI (H16) | L | H | Actions pinned by commit SHA; CLIs as `npx <cli>@<exact>` at least two weeks old; `npm ci` from the lockfile; Dependabot PRs reviewed by hand, never auto-merged | [§3.4](#34-other-housekeeping) |
| R33 | GitHub Actions minutes run out (private repo, no card): CI and deploys stop until the 1st (H7) | L | L | About 350 of 2,000 min/month expected; deploy from the Mac meanwhile | [§2.12](#212-deploys-ci-and-hosting) |
| R34 | **Deploy order** broken: the web app or firmware writes new fields before the rules allow them; a rules typo blocks the device or the app (H12, R-E6, R-E7) | M | M | Rules first, in their own PR (rules → mailer → PWA → firmware); `ci` rules lint; the emulator suite (87 cases) and the `curl` tests after every rules change; rules stay backward-compatible | [§2.6](#26-no-cloud-updates) |
| R35 | Placeholder rules deployed, locking out the logger and you (H13) | L | H | `render-rules.sh` refuses placeholders and bad UIDs; no `database` block in the root `firebase.json`; a CI guard | [§2.12](#212-deploys-ci-and-hosting) |
| R36 | No branch protection on GitHub Free private repos, so a red `ci` does not block a merge (H14) | M | L | Each deploy workflow repeats its own checks; read the `ci` result before merging | [§2.12](#212-deploys-ci-and-hosting) |
| R37 | The browser-key referrer restriction blocks sign-in (wrong pattern, Safari quirk) (H9) | M | M | Restrict only after a working deploy; re-test on the iPhone; undo in the console in one click | [§2.9](#29-pwa-not-updating-on-the-iphone) |
| R38 | A stale app after a deploy or a rollback (service-worker cache, `immutable` assets) (H11) | L | L | "A new version… Reload" banner; force-quit and reopen; last resort delete and re-add the icon | [§2.9](#29-pwa-not-updating-on-the-iphone) |
| R39 | Recipient privacy: family addresses stored in RTDB (R-E13) | L | L | Readable only by the owner, device and mailer UIDs; every e-mail's footer says why the person gets it and how to stop | [06](06-cloud-and-dashboard-guide.md) §9 |
| R40 | Grid figures (`egr`, `gw`) mistaken for the utility meter; the billing month differs from the bill (R-E11) | M | L | "est." everywhere; the `monthStart` setting; the e-mail footer explains it | [04](04-architecture-and-data-model.md) §9.6 |

---

## 2. Troubleshooting decision trees

**Start every investigation at the local status page.** Open `http://solar.local` from the iPhone or Mac on the home WiFi. `GET /api/status` shows:
- the link profile, adapter and dialect;
- poll success % and CRC errors;
- RSSI, heap and queue depth;
- the last cloud OK and the reset reason.

`GET /api/raw` shows the last raw reply per command, in ASCII and hex. If `solar.local` doesn't resolve, use the IP address shown in `status.ip` in the PWA's Settings page, or in your router's client list.

### 2.1 No reply from the inverter

Symptoms: `lk = 0`, a `LINK_LOST` alert, the LED blinking red, and `/api/raw` empty or showing timeouts.

```
No reply
├─ Is the logger in safe mode (solid red, status says safe)?  ──yes──> §2.10
├─ Is the inverter ON (LCD lit)?  ──no──> switch it on; COM is dead when the unit is off
├─ /api/status: does the probe report "no protocol found"?
│    └─ Are ANY bytes received (raw ring / RX byte counter)?
│         ├─ NO bytes at all ───────────────> A. physical receive path
│         ├─ bytes = exactly the command we sent ──> B. receiver is on the wrong pin
│         └─ bytes, but not valid frames ──> §2.2 Garbage bytes
└─ It worked before and stopped ─────────> C. intermittent / mechanical
```

**A. Nothing is received**

1. ⚠️ Unplug the RJ45 from COM before touching any wiring. Check that the **ESP32 is unpowered** while you re-wire.
2. Check the plug: fully inserted, clip clicked, the cable not pulling on the jack.
3. Check each joint with a gentle tug: wraps on the header pins, conductors in the screw terminals.
4. Check the ground: **B−** and an ESP **GND** must both reach **COM pin 8**. The direct TX link needs this common ground.
5. **Disarm first** (`reset-link`; self-ID and the idle tests are refused while the link is armed). Then re-run the module self-ID on the bench (`/api/diag/pins`). The pin the firmware treats as RO must really be RO. See [03](03-hardware-and-wiring.md).
6. Re-run the **idle-level test** with **GPIO17 disconnected** (`POST /api/diag/idle?pin=1`, then `pin=2`): A+ on pin 1, then on pin 2. Exactly one pin should read ≥ 99 % LOW. That pin is the inverter TX and must be on **A+**. Then run the **upper check** on the other pin (`idle N 3v3`, [03](03-hardware-and-wiring.md) §8.4b). Only after two `below-3V3` results does that pin go on **GPIO17**. Arm again, which runs the pre-arm check.
7. Did anything else get connected to COM, such as a WiFi collector? Only one master is allowed. The 3 s listen before each probe raises `FOREIGN_MASTER` if it sees unsolicited frames.
8. Re-probe: `POST /api/probe`, or **Settings → Re-probe** in the PWA.
9. Still nothing with both pins verified:
   - If **neither** pin idled LOW, the port may be TTL-level. Follow D1 §8.4 C ([03](03-hardware-and-wiring.md)) and **stop** if the logic level can't be established. Never connect an ESP GPIO to an unknown-voltage pin.
   - Otherwise suspect a damaged module (R3). Escalate to fallback F-A (MAX3232, [§5.2](#52-max3232-module-fallback-f-a)) or F-B (vendor collector).

**B. The received bytes are exactly the command just sent** (e.g. `QPIGS` and its CRC, hex `51 50 49 47 53 B7 A9 0D`)

The module's A+ is on the same line that GPIO17 drives, so the receiver is reading our own TX instead of the inverter's TX [INFERRED]. Firmware 0.6.1 refuses to arm in that wiring (the pre-arm check needs RO LOW), so the wiring has been changed since arming. Pull the RJ45, power down, and **disarm** (`reset-link`). Put the conductors back as in your build record (B5), then redo the idle tests and the upper check **before** reconnecting GPIO17. If GPIO17 was on the inverter's transmit wire, it may have been damaged by the negative level: re-run the bench loopback before trusting it (module header wires off GPIO16/18 and `reset-link all` first, or the firmware refuses the loopback).

**C. It worked before and then stopped**

1. Look in the PWA's **Alerts → History** for `LOGGER_REBOOT` or `LINK_LOST` patterns.
   - Frequent short link losses point to a loose wrap joint (R12).
   - One long loss points to a disturbed cable or plug.
2. Do a physical inspection (§3) and re-tape the joints. Consider the solder upgrade ([§5.1](#51-soldered-joints)).
3. Did the inverter get a firmware update or a settings change? Re-probe, because the dialect may have changed.

### 2.2 Garbage bytes

Symptoms: bytes arrive, but frames don't start with `(` or end with CR, or they look like random characters.

```
Garbage
├─ All bytes look "inverted"/framing errors at the right rate ──> polarity: profile P1 vs P2
├─ Too few/too many bytes per reply (~4x off) ──────────────────> baud: 2400 vs 9600 (P4/P5)
├─ Mostly OK, occasional junk ─────────────────────────────────> noise / loose joint / ground (§2.3)
└─ Junk only while the Mac is plugged in ──────────────────────> ground loop: Mac on battery
```

1. **Let the prober do its job.** It tries P1 (2400, RX and TX inverted), then P2 (RX not inverted), P3 (TX not inverted), P4/P5 (9600). Check which profile it settled on in `/api/status`.
2. **RX polarity wrong (P2 needed):** the receiver's sense is reversed. That happens if the module's A/B roles are swapped in the wiring. P2 handles it in firmware, and no re-wiring is needed as long as **B− stays on pin 8 (GND)**. ⚠️ Don't move B− onto a signal pin.
3. **Wrong baud:** a PI30 `QPIGS` reply is about 110 bytes. Read at the wrong baud, it arrives as junk with clearly the wrong byte count (roughly 4× too many or too few). The prober covers 2400 and 9600.
4. **Noise:**
   - Keep the LAN cable short (about 1–1.5 m), and away from the inverter's AC and PV cables.
   - Make sure the ground conductor (pin 8) is solid.
   - Tape any loose joints.
5. **Ground loop:** if the junk appears only while the Mac is connected by USB, run the Mac on battery, or disconnect it and use OTA updates.

### 2.3 CRC errors

Symptoms: frames look right (`(` … CR) but the CRC check fails. `/api/status` shows a rising CRC error count.

| Observation | Likely cause | Action |
|---|---|---|
| Under 1 % of polls fail | Normal for a marginal RS232-lite link | Nothing. Failed polls are retried, and a minute only needs some valid samples (`n` < 12 is fine) |
| 1–10 % of polls fail, varying | Marginal levels, noise, loose joint | §2.2 step 4; re-seat joints; shorten the cable; solder upgrade; a ≤ 4.7 kΩ series resistor in the A lead (if available) reduces clamp stress, not noise |
| One specific command **always** fails, the others pass | A decoder or checksum-form issue for that command | REVO `QALL` uses a **sum+1 checksum**, not CRC. The prober tries both forms. Save the raw frame from `/api/raw` as a test case and check it against [02](02-protocol-reference.md) |
| The reply is `(NAKss` | The inverter rejected the request (unsupported command, or bad request CRC) | Normal for optional commands: they get marked unsupported. If `QPIGS` NAKs, the request frame or TX polarity (P3) is wrong |
| Every reply fails CRC, but the text is readable | CRC byte-escaping rule mishandled (a CRC byte equal to 0x28/0x0D/0x0A is incremented by one), or the wrong CRC variant | Firmware bug. Run the native unit tests (CRC vectors from V1 §4.4) and compare with the raw frame |

### 2.4 Values disagree with the LCD

Cross-check targets from M3: output V within ±2 V and battery V within ±0.3 V of the LCD.

| Symptom | Likely cause | Fix |
|---|---|---|
| "PV current" shows hundreds of amps | QPIGS field 12 is PV **power** (REVO dialect), not current | Check `meta.pv12`. It should be `W`. Re-probe; the classification rule is: field 12 > 15 while PV V > 30 means power (the manual's max PV current is 15 A) |
| Battery discharge current is always 0 while discharging | On REVO, QPIGS[15] is PV Wh today, not discharge current | The REVO dialect must take `bdi` from QALL[9]. Check `meta.dia` = `REVO` |
| SOC moves in coarse steps or differs from the battery's own display | The inverter's SOC is voltage-derived unless the BMS link is active | Check whether the LCD "Li" battery-communication icon is lit. The logger reports the inverter's figure as is |
| Temperature shows > 150 | Raw ADC value, not °C (a known PI30 quirk) | Ignored by the alert rules. Note it in an issue |
| Daily kWh differ from the inverter's own total | Integrated (`epv`) vs inverter-reported (`epvi`); 1 A battery-current resolution; gaps where `n` < 12 | Expected within a few percent for PV and load. Battery energy is coarse. The PWA shows both values |
| Times are 6 hours off | UTC vs local confusion | `ts` is UTC epoch. Date and `hhmm` keys are local Dhaka time. Check the device TZ string `<+06>-6` and the PWA's `tzDate` |
| Values frozen | Stale sample: the link is lost but the UI still shows the last value | Check `lk` and "updated N s ago". Then §2.1 |
| Fault code meaning doesn't match the LCD | Different code tables: LCD (manual §5/§6) vs REVO QALL vs QPIWS | Check the source tag; see [04 §5.4](04-architecture-and-data-model.md) and [`shared/codes.json`](../shared/codes.json) |
| Mode shows "B" (battery) while the grid is present | Normal with SBU output priority (program 01) | The `GRID_LOSS` alert deliberately does not use QMOD alone |

**For any unexplained value:**
1. Copy `/api/raw` for the command concerned.
2. Decode it by hand with the field map in [02](02-protocol-reference.md).
3. Photograph the LCD at the same moment.
4. Add the raw frame as a test case in `firmware/test/test_parsers/test_main.cpp` with the expected values ([05](05-firmware-guide.md) §16.3; there is no fixture folder), so the parser test captures the fix.

### 2.5 WiFi drops

| Check | Action |
|---|---|
| `status.rssi` below −75 dBm | Move the box: antenna end up, at least 15 cm from the inverter's steel case, not behind it. Or move the router |
| The router is off during outages | Expected unless the router or ONU is on AC OUT. Logging continues and back-fill catches up |
| The router uses one SSID for 2.4 + 5 GHz (band steering) and joins fail | Create a separate 2.4 GHz SSID. The ESP32-S3 is 2.4 GHz only |
| Using an iPhone hotspot for testing | Turn on *Personal Hotspot → Maximize Compatibility* (2.4 GHz) |
| `status.rr` = `BROWNOUT`, frequent reboots | Use a short, good USB cable and the 2 A charger. The firmware can cap TX power (`WiFi.setTxPower(WIFI_POWER_15dBm)`) |
| WiFi password changed | Re-provision: [§4.1](#41-re-provision-wifi-boot-long-press) |
| Router set to WPA3-only | Try WPA2/WPA3 mixed mode [INFERRED] |

The firmware never opens the setup portal on its own just because WiFi is down, because logging must continue during load-shedding. It reconnects by itself.

### 2.6 No cloud updates

Symptom: the local page is live, but the PWA shows an old "updated N s ago" or the "Logger offline" banner.

```
Local UI live, cloud stale
├─ /api/status "last cloud OK" recent? ──yes──> the problem is in the PWA: §2.9
├─ Internet up? (open any site from the same WiFi) ──no──> wait; queue grows (status.q), back-fill later
├─ HTTP 401/403 in the serial log or status ───────> credentials / rules  (A)
├─ TLS / handshake failure ─────────────────────────> trust anchors / DNS (B)
├─ Writes rejected for one node only ───────────────> validation rule (C)
└─ Firebase console: Usage shows quota exceeded ────> §2.7
```

**A. Credentials and rules**

- Was the **device user's password or e-mail changed**, or the user disabled? Then the refresh token is dead. Re-provision ([§4.1](#41-re-provision-wifi-boot-long-press)) and re-enter the device credentials.
- Do the GitHub Variables `FIREBASE_OWNER_UID` and `FIREBASE_DEVICE_UID` match the console's UIDs, and did the last **deploy-rules** run succeed? The committed `firebase/database.rules.json` keeps placeholders on purpose; only the rendered copy is deployed ([06](06-cloud-and-dashboard-guide.md) §3). `upErr` `GET cfg: HTTP 401` points here.
- Is the DB URL exactly `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app`? It must be the regional URL, not `firebaseio.com`.
- Is the portal's API key the **device key** (`savx-device`) of the **same project**? The browser key gives `sign-in HTTP 403` once it is referrer-restricted; another project's key gives `PROJECT_NUMBER_MISMATCH` on refresh.
- Did a rules change go out that the firmware does not match yet? The device drops refused `alertState` records and drops refused events after 3 tries (`status.edrop`, `cloud` on the console); minute uploads continue. Deploy order: rules → mailer → PWA → firmware.

**B. TLS failures**

- A host may have moved to a root that `roots.h` doesn't contain. Re-check the chains ([§3.2](#32-root-ca-check-and-rotation)), add the root, and OTA.
- Captive or filtered networks (hotel-style login pages) break TLS. Use the home router.

**C. One node rejected**

- `live` requires `ts` > 1700000000 and < now + 10 min. A wrong device clock makes the write fail. Check NTP: `status.ts` vs real time.
- Key names are length-limited, and `events`, `cfg`, `alerts` and `mailq` reject unknown keys. A firmware or web change that adds a field needs a matching rules change, deployed first. See [04 §8.2](04-architecture-and-data-model.md).

**If it stays broken:** the data is safe in the LittleFS queue (about 40 days). A `BACKLOG_STUCK` event fires when the queue is more than 6 h old while WiFi is up.

### 2.7 Quota warnings

Symptoms: a `QUOTA_GUARD` event (level 1: `live` slowed to at least 60 s; level 2 at twice the limits: `live` paused and `cfg` polled every 5 min; `status.qg`), or Firebase console → Realtime Database → Usage shows more than about 30 MB/day download.

1. **Device side:** check `status.hs` (TLS handshakes today) and `status.rq` (requests today).
   - `hs` in the hundreds or thousands means keep-alive is failing and the device reconnects for every request. Check the WiFi stability (§2.5) and the serial log.
   - `rq` far above about 12,000/day means a retry loop. Report it as a firmware bug, with the log.
2. **Console:** close the data viewer if it is open on `hist` or any big node. Console reads are billed.
3. **PWA:**
   - Several phones open all day multiply the `live` listener cost (about 3.6 MB/day for 3 phones × 8 h, which is still fine).
   - Repeatedly reloading "Today" costs about 0.35 MB per load.
   - A PWA bug that subscribes to a whole `hist` or `events` root is the expensive case. The code must only read date-bounded paths ([04 §7](04-architecture-and-data-model.md)).
4. **Mailer:** a healthy mailer adds about 4 MB/day (288 small runs). Much more means it is failing and retrying, or someone shortened the trigger; check script.google.com → Executions ([§2.11](#211-e-mail-alerts-not-arriving)).
5. **Profiling:** the Firebase CLI's `database:profile` command shows which paths are read most. Run it from the Mac only if needed.
6. **If the quota was exceeded:** the database is shut off until the next month. Logging continues locally, and the queue holds about 40 days. Back-fill runs automatically when access returns. ntfy, Telegram and Healthchecks keep working; the mailer cannot read jobs until then.

### 2.8 Alerts not arriving

Start with **Settings → Send test alert** in the PWA. The device acts within 60 s; it sends an ntfy push and, when alert e-mails are on, an e-mail job.

```
Test alert
├─ Event appears in Alerts → History?
│   ├─ NO ──> device side: is the logger online (status fresh)? cfg poll working? -> §2.6
│   └─ YES, ch shows ntfy, but no push on the iPhone ──> ntfy side (A)
├─ Real alerts missing but test works ──> rules side (B)
├─ Healthchecks e-mail never arrives ──> (C)
└─ Push works but no e-mail ──> §2.11
```

**A. ntfy**

1. From the Mac, run `curl -d "test" ntfy.sh/<your-topic>`. If that doesn't arrive either, the problem is the app or the subscription:
   - Is the topic spelled exactly the same in the ntfy app?
   - Is the app subscribed to the server `ntfy.sh`?
   - Are iOS notifications allowed for ntfy?
   - Is a Focus mode blocking them?
2. If curl works but the device's alerts don't, the ntfy topic in the logger's settings is wrong. Re-enter it through the portal or `/api/config`.
3. HTTP 429 in the device log: the per-IP quota is exhausted, possibly shared with other users behind CGNAT. Rare at this design's ≤ 40 pushes/day.

**B. The rules suppressed it** (all of this is by design):
- Quiet hours (default 22:00–07:00) suppress pushes for severity ≤ 3. The event is still logged.
- The daily cap of 40 pushes: above it, only severity 5 is sent.
- Hold times: e.g. `GRID_LOSS` needs 10 s, `BATT_LOW` 60 s.
- Thresholds in `cfg` may be set too loose.
- For **usage-threshold rules**: the rule may be outside its active hours, latched for today (energy rules fire once per day or billing month), in cooldown (`em` = `cool`), at its `maxDay` (`rcap`), or `invalid`. The badge in **Alerts → Rules** and `rules` on the console show the state.

**C. Telegram and Healthchecks**
- Telegram:
  - You must send `/start` to your bot once before it can message you.
  - Check the bot token and chat id.
  - Telegram may be blocked in Bangladesh. That is why it is secondary.
- Healthchecks:
  - Check that the UUID is entered on the device.
  - Look for pings in the check's log on healthchecks.io.
  - Period should be 5 min and grace 15–30 min.
  - Check your e-mail spam folder.

### 2.9 PWA not updating on the iPhone

| Symptom | Cause | Fix |
|---|---|---|
| "Updated N s ago" keeps growing | Logger or cloud problem | §2.6 |
| Data freezes after switching apps | iOS suspends the WebSocket in the background | Bring the app to the foreground. It re-subscribes on `visibilitychange`. If it doesn't, close and reopen it |
| Asked to sign in again, or shows no data | A Home Screen app has **its own storage**, separate from Safari | Sign in **inside the installed app**, not in Safari |
| A new version doesn't appear after a deploy (**stale service-worker cache**) | The service worker serves the app shell from its cache; `index.html` is network-first and the banner "A new version of the app is ready → Reload" appears only when the app is opened after the deploy | 1. Check that `deploy-web` actually finished (Actions tab). 2. Open the app, wait a few seconds for the banner, tap **Reload**. 3. Force-quit and reopen, perhaps twice. 4. After a **rollback**, the same applies; an old hashed asset can stay in the phone's HTTP cache for a while because `/assets/*` is `immutable` ([R7](../research/r7-web-hosting.md) H11): force-quit and reopen. 5. Last resort: delete the Home Screen icon, re-add it from Safari **at the same address** and sign in again (the history cache is downloaded again) |
| Charts show the wrong day around midnight | Time-zone handling | Date keys are Dhaka-local. Check the phone's time zone |
| The site doesn't load at all | A Cloudflare outage, a failed or rolled-back deploy, or the Worker was renamed or deleted | Installed apps keep working from the cache during short outages. Check the Cloudflare status page and **Workers & Pages → savx-logger → Deployments**; roll back if needed ([§4.7](#47-web-rollback-and-host-failover)). Use the local page `http://solar.local` meanwhile |
| Sign-in fails with an API-key or referrer error | The browser key's Websites list does not include the app's origin | [06](06-cloud-and-dashboard-guide.md) §5.6; changes take a few minutes. Undo the restriction in one click if you are locked out |
| Red "No access … UID …" banner | The signed-in UID is not the deployed `OWNER_UID` | Copy the UID shown into `FIREBASE_OWNER_UID`, run `deploy-rules` |
| The app asks you to install again / has no history after a change | The **origin changed** (new account subdomain, Worker name or host) | Expected after a host switch: add the new icon, sign in, delete the old icon. Avoid renaming ([R7](../research/r7-web-hosting.md) §2) |
| Offline banner appears although the local page is live | The device can't reach Firebase | §2.6 |

### 2.10 ESP in safe mode or a boot loop

**Safe mode:**
- How you recognise it: a solid red LED (if the LED works), or `/api/status` / `status` reports `safe YES`. It happens after **3 crash-type resets** (panic, watchdog, brownout) without 120 s of healthy running in between. Plain power cuts, `reboot`, OTA and the network-watchdog restart never count, so load-shedding cannot cause it ([05](05-firmware-guide.md) §13.1).
- In safe mode there is no inverter TX and no cloud. Only WiFi, the local UI, the console and `/update` work. Leave it with `safe-exit` or `POST /api/safemode/exit` once the cause is fixed.

```
Safe mode / boot loop
├─ Reaches WiFi (local UI opens)? ──yes──> read /api/status: rr (reset reason)
│    ├─ BROWNOUT ────────> power: cable, charger, socket (§2.5)
│    ├─ PANIC / TASK_WDT right after an OTA ──> roll back firmware (step 2)
│    └─ PANIC with old firmware ──> serial log (step 3); possibly corrupted settings (step 4)
└─ Never reaches WiFi ──> USB serial log (step 3), then USB re-flash (step 5)
```

1. **Power first.** Brownouts look like crashes. Use a short, good cable, the 2 A charger and a firm socket.
2. **Roll back:** upload the previous good `firmware.bin` at `http://solar.local/update`. Keep every released `.bin`. See [05](05-firmware-guide.md).
3. **Read the serial log:**
   1. ⚠️ Unplug the RJ45 from COM, or run the Mac on battery.
   2. Connect the Mac to the board's **"COM" USB-C port** (the CH343 bridge).
   3. Run `pio device monitor -b 115200`. The panic backtrace and reset reason print at boot.
4. **Corrupted settings:** do a settings reset ([§4.2](#42-factory-reset-of-logger-settings)).
5. **USB re-flash:** the last resort, always possible. On the bench, run `pio run -e savx -t upload`, then `-t uploadfs` for the local UI files. See [05](05-firmware-guide.md).

### 2.11 E-mail alerts not arriving

Start in the PWA: **Alerts → E-mail → Mailer** card, then **Send test e-mail**. The details of every `em` chip are in [`tools/apps-script/README.md`](../tools/apps-script/README.md) §10–§11.

```
No e-mail
├─ Mailer card red "Mailer not running" (heartbeat older than 15 min)? ──> (A) mailer stopped
├─ Test e-mail works, but an alert did not come:
│    look at the event in Alerts → History and its e-mail chip
│    ├─ no chip ─────────────> the logger did not queue it: rule e-mail off, built-in alert below
│    │                          "Built-in alerts by e-mail", or Send alert e-mails off when it fired
│    ├─ q (stays) ───────────> (A) mailer stopped
│    ├─ cool / rcap / cap ───> by design: cooldown, the rule's Max per day, the logger's 40 jobs a day
│    ├─ skip ────────────────> "check once" rule with too little data coverage that day
│    ├─ norc ────────────────> (B) recipients
│    ├─ off ─────────────────> Send alert e-mails was off
│    ├─ cap / quota ─────────> (C) quota
│    ├─ exp ─────────────────> the job waited more than 12 h (24 h for severity ≥ 4): the mailer was down
│    ├─ fail ────────────────> 5 send errors: mailer/err, then (C)
│    └─ sent ────────────────> (D) delivery: spam, filters, the phone
└─ Test e-mail does not work either ──> (A), then (B), then (D)
```

**A. Mailer stopped (heartbeat stale)**

1. Sign in to script.google.com **as the sender account** → the project → **Triggers**: is the `poll` trigger there? **Executions**: are the latest runs red?
2. Run `showStatus` (changes nothing) and read the log; then run `setup`.
3. Read `mailer/err` (shown on the card):
   - `RTDB … HTTP 401`: the rules no longer grant the mailer: `FIREBASE_MAILER_UID` wrong or missing, or a rules deploy without it. Fix the Variable and run `deploy-rules`.
   - `Firebase sign-in failed`: `FB_PASSWORD` changed, the mailer user was deleted, or `FB_API_KEY` is the (restricted) browser key. Use the device key.
   - `MailApp: …`: usually the daily quota, see (C).
4. If the sender account's password was changed, Google may have revoked the script's authorisation: run `sendSelfTest`, authorise again, then `setup` ([§4.9](#49-recover-the-e-mail-mailer)).
5. Meanwhile ntfy and Healthchecks still work, and queued jobs younger than 12 h are delivered once the mailer runs again.

**B. Recipients**

- An address must be **both** in Alerts → E-mail → Recipients **and** in the mailer's `ALLOW_TO` script property. `mailer/warn` (on the card) names the one that is missing.
- A rule's recipient checkboxes select a subset; removing an address in the app remaps the rules that used it.

**C. Quota and caps**

- The **Mailer** card shows "Sent today" and Google's remaining quota (`mailer/quota`). The mailer stops at your **Daily limit** (at most 40 recipient-deliveries; 3 recipients on one alert count as 3) and sends one "limit reached" e-mail; the last 5 are kept for Alert/Critical. Below 10 of Google's quota left, only severity ≥ 4 is sent.
- Google's consumer limit is 100 recipients a day, reset 24 h after the first send, and Google may change it without notice. If you hit the cap often: raise cooldowns, lower **Max per day** on noisy rules, turn off recovery e-mails, or raise the "Logger offline after" minutes during load-shedding.
- Trigger runtime is limited to 90 min a day. If Executions show runs near 30 s, RTDB or Google is slow: change `everyMinutes(5)` in `setup` to `10` and run `setup` again.

**D. Delivery**

1. **Check the Spam folder** of each recipient. Mark the mail "Not spam" once, and create the Gmail filter `subject:"[Solar]"` → **Never send it to Spam**, **Always mark it as important**.
2. Mail that a Gmail account sends to itself may not raise a notification: send to your normal address, not to the sender account.
3. On the iPhone, use the **Gmail app** with notifications on; iOS Mail fetches Gmail on a schedule.
4. The subject always starts with `[Solar]` (or your `SUBJECT_TAG`). Search for it in **All Mail**.

**Too many e-mails?** Each rule has a cooldown, a **Max per day** and a recovery level; energy rules fire at most once per day or billing month, also across reboots. "Logger offline" mails are at most 4 a day; raise the threshold to 120 min if load-shedding makes them noisy, or switch them off (Healthchecks still covers a dead logger). A true duplicate (the same event twice) should never happen; if it does, note both mails' times and the event key and check `mailer/err`.

### 2.12 Deploys, CI and hosting

| Symptom | Cause | Fix |
|---|---|---|
| A workflow fails with "repository variable … is not set" | A Variable is missing | Add it ([06](06-cloud-and-dashboard-guide.md) §4.3), then **Re-run** the workflow |
| `deploy-web` fails at the deploy step | The Cloudflare token expired or was revoked, or the account ID is wrong | New token from the **Edit Cloudflare Workers** template ([06](06-cloud-and-dashboard-guide.md) §5.3), update the secret |
| `deploy-web`: "must be the asia-southeast1 URL" | `VITE_FIREBASE_DATABASE_URL` points to another region | The CSP only allows asia-southeast1 |
| `deploy-rules`: the Google auth step fails | WIF values wrong, or WIF not possible on a Spark project | Re-check [06](06-cloud-and-dashboard-guide.md) §4.4 option A, or use the `GCP_SA_KEY` fallback (option B) |
| `ci`: "placeholders missing" or an "open rule" error | Real UIDs or a bare `auth != null` were committed to the rules | Restore the placeholders; UIDs belong only in Variables |
| `ci` red but the deploy went out | GitHub Free has no branch protection on private repos | Expected. Each deploy repeats its own checks; fix `ci` before the next merge |
| Workflows do not start at all | Docs-only change (`*.md`, `docs/`, `research/` are skipped on purpose), or the Actions minutes are used up | Settings → Billing → Actions; until the 1st, deploy from the Mac ([06](06-cloud-and-dashboard-guide.md) §3.3, §5.4) |
| `wrangler` refuses to run on the Mac | wrangler 4.146.0 needs Node ≥ 22; the Mac default is 20.17 | `source ~/.nvm/nvm.sh && nvm use 24.21.0` in that terminal, or in an emergency `npx --yes wrangler@4.86.0` on Node 20 |
| The app shows an old version after a deploy | Stale service-worker cache | [§2.9](#29-pwa-not-updating-on-the-iphone) |
| A bad version is live | – | Roll back ([§4.7](#47-web-rollback-and-host-failover)) |
| Dependabot opens many PRs | Monthly updates of actions and `web/` packages | Review and merge only what CI passes and you understand; Vite majors are excluded on purpose |

---

## 3. Maintenance

### 3.1 Schedule

| When | Task | How |
|---|---|---|
| Daily, first week | Check database usage | Firebase console → Realtime Database → Usage. Expect about 5–15 MB/day download (the mailer adds about 4 MB) and about 0.4 MB/day storage growth |
| Daily, first week | Check the mailer | Alerts → E-mail → **Mailer** card green; script.google.com (sender) → **Executions**: runs of a few seconds, far below 90 min/day |
| Weekly, first month | Glance at the health fields | PWA Settings: RSSI, heap, queue, last reset reason. Alerts → History: no unexpected `LOGGER_REBOOT` / `LINK_LOST`, no e-mail chip stuck on `q` |
| Monthly | Send a test alert | PWA → Settings → Send test alert. ntfy (and Telegram) should arrive within 30 s, and the e-mail within about 6 min |
| Monthly | Review Dependabot PRs | Merge only updates that pass CI and that you understand ([§3.4](#34-other-housekeeping)) |
| Monthly, first 3 months | GitHub Actions minutes | GitHub → Settings → Billing → Actions: about 350 of 2,000 minutes |
| Every 3 months | Physical inspection | Wrap joints and tape, cable strain relief, plug seated, box temperature, dust. **BMS cable untouched** |
| Before its end date | **Renew the Cloudflare API token** | New token, update the `CLOUDFLARE_API_TOKEN` secret, revoke the old one ([§4.8](#48-rotate-ci-and-cloud-secrets)). An expired token only makes `deploy-web` fail; the live site keeps working |
| Yearly | **Export the data** | The REST export in [06](06-cloud-and-dashboard-guide.md) §12 (`daily`, `h15`, one date of `hist` per request) or the PWA's CSV exports. About 140 MB of download, roughly 1.4 % of a month's quota. Store the files off the Mac too. (`tools/rtdb-export.sh` is planned but does not exist yet) |
| Yearly | **Root CA check** | §3.2 |
| Yearly (at most every 18 months) | **Use the Google accounts** | Sign in to the Google account that owns the Firebase project (e.g. open the Firebase console) **and to the sender Gmail account**. Google may delete accounts inactive for 2 years ([policy](https://support.google.com/accounts/answer/12418290)). PWA sign-ins don't count (they use Firebase Auth), and the mailer's script runs may not count either |
| Yearly | **Free-tier watch** | Firebase: the [pricing page](https://firebase.google.com/pricing) and the [FAQ](https://firebase.google.com/support/faq) for RTDB or Hosting changes on Spark. Cloudflare: [static assets billing](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/). GitHub: [Actions billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions). Apps Script: [quotas](https://developers.google.com/apps-script/guides/services/quotas). If a free tier shrinks, plan the move (F-C, D1, or the Firebase Hosting fallback) |
| Yearly | Rotate `GCP_SA_KEY` (only if you use the key fallback instead of WIF) | [§4.8](#48-rotate-ci-and-cloud-secrets) |
| Yearly | Review retention | Is `keepHistDays` (400) still right? Storage stays below half the quota up to about 1,000 days |
| When needed | Firmware updates | §3.3 |
| When needed | Web app and rules updates | Push to `main`; `deploy-web` / `deploy-rules` deploy them. Rules first when both change |
| When needed | Mailer updates | Run `npm test` in `tools/apps-script`, paste the changed files into script.google.com ([`tools/apps-script/README.md`](../tools/apps-script/README.md) §12) |

### 3.2 Root CA check and rotation

The device trusts only the roots compiled into `firmware/include/roots.h`. mbedTLS date checks are off, so an **expired** root keeps working. A **changed chain** (a server moving to a new root) breaks TLS for that host until the new root is added.

1. On the Mac, for each host, run:

   ```sh
   for h in identitytoolkit.googleapis.com securetoken.googleapis.com \
            <project>-default-rtdb.asia-southeast1.firebasedatabase.app \
            ntfy.sh api.telegram.org hc-ping.com script.google.com; do
     echo "== $h"; openssl s_client -connect "$h:443" -servername "$h" -showcerts </dev/null 2>/dev/null \
       | grep -E '^ *[0-9] s:|^ *i:'
   done
   ```

2. Compare the top issuer of each chain with the table in [04 §8.3](04-architecture-and-data-model.md):
   - GTS Root R1, R3 or R4 for Google (RTDB, sign-in, and `script.google.com` for the optional mailer nudge). GTS Root R2 is deliberately not embedded;
   - ISRG X1/X2, Root YR/YE for ntfy;
   - Go Daddy G2 for Telegram;
   - USERTrust ECC/RSA or Sectigo E46/R46 for Healthchecks.

   [`firmware/README.md`](../firmware/README.md) §7 has the exact `openssl verify` check against the embedded bundles.
3. If a new root appears, download its PEM from the official source (Google: <https://pki.goog/roots.pem>, Let's Encrypt: <https://letsencrypt.org/certificates/>). Add it to `roots.h`, keep the old one, build and OTA.
4. Expect Let's Encrypt changes. ntfy.sh is already on the new YR hierarchy, which is cross-signed by X1 (V2 §3.6).

### 3.3 Firmware OTA updates

1. Build on the Mac: `pio run -e savx`. The binary is `firmware/.pio/build/savx/firmware.bin`. Keep a copy named by version.
2. On the same WiFi, open `http://solar.local/update` and sign in with the admin password.
3. Upload the `.bin`. From the iPhone this works through the Files app.
4. After the reboot, check `/api/status` (or PWA Settings) for the new `fw` version, a valid link and a recent cloud OK.
5. If the new firmware crash-loops, it enters safe mode after 3 crash-type resets and `/update` stays reachable. Upload the previous `.bin`.
6. Changes to the local UI files (`firmware/data/www/`) need a filesystem image (`-t uploadfs` over USB). ⚠️ That replaces the **whole** LittleFS partition, including minute records not yet uploaded and the cached `alerts.json`: run it only when the upload backlog is 0, preferably just after midnight ([05](05-firmware-guide.md) §4 and §12). Firmware OTA never touches LittleFS.
7. ⚠️ Never build with `INV_ALLOW_WRITES=1`.

### 3.4 Other housekeeping

- **Dependencies:** don't chase library updates. Pinned versions (pioarduino 55.03.312-1, Arduino-ESP32 3.3.12, ArduinoJson 7.4.3, WiFiManager 2.0.17, ElegantOTA 4.0.0, Firebase JS SDK 13.0.0, uPlot 1.6.32, Vite 6.4.4; in CI and on the Mac: wrangler 4.146.0, firebase-tools 15.32.1, PlatformIO 6.2.0; GitHub Actions pinned by commit SHA) are a feature. Update only to fix a real problem, and avoid pre-releases.
- **Dependabot:** it opens update PRs monthly for the pinned actions and the `web/` packages. Read each one, let `ci` run, and merge only what passes and what you understand. Never enable auto-merge. Vite majors are excluded because the Mac has Node 20.17.
- **iOS updates:** after a major iOS release, check that the Home Screen app still opens, stays signed in, and receives ntfy pushes and Gmail notifications.
- **Passwords:** keep the owner, device and mailer passwords, the sender Gmail account and the admin password in a password manager. Also keep the ntfy topic, Telegram token, Healthchecks UUID, `NUDGE_KEY` (if used) and the Cloudflare token's end date there.

---

## 4. Recovery procedures

### 4.1 Re-provision WiFi (BOOT long-press)

Use this when the WiFi name or password changes, the device credentials change, or you move house.

1. While the logger is running, **hold the BOOT button (GPIO0) for 3 seconds**. The LED blinks slow blue if it is wired.
2. The ESP opens its setup access point **`SavxLogger-XXXX`**. Its password is printed on the serial log and given in [05](05-firmware-guide.md).
3. On the iPhone, join that WiFi network. The captive portal opens; if it doesn't, browse to `http://192.168.4.1`.
4. Choose the 2.4 GHz SSID and enter its password (or leave both empty to keep the Wi-Fi). Re-enter any other fields that changed; secret fields show empty, blank keeps the stored value and `-` clears it:
   - Firebase **device key**, DB URL, device e-mail and password;
   - ntfy topic, Telegram token and chat id, Healthchecks UUID;
   - dashboard URL for notification links, mailer nudge URL (both optional);
   - admin password.
5. Save. The logger joins the new network. **Logging to LittleFS continues the whole time**, because the portal is non-blocking.
6. Check `http://solar.local` and the PWA within a few minutes.

### 4.2 Factory reset of logger settings

Back up first:
- Make sure the queue is empty (`status.q` = 0), so every minute is in the cloud.
- Or save the local minutes from `/api/minutes?date=…`.

A settings reset does not erase LittleFS. A full erase does.

| Level | What it clears | How |
|---|---|---|
| **Re-probe only** | The link profile and dialect (re-detected automatically, read-only) | PWA Settings → Re-probe, or `POST /api/probe` |
| **Settings reset** | All NVS: WiFi credentials, Firebase and device credentials, alert channels, link state (self-ID, idle results, arm, probe profile), energy checkpoint and billing-month sums, rule states, boot counters. The LittleFS queue, history and the stored `alerts.json` stay | The firmware has no reset command. Erase only the NVS partition over USB: `nvs` is at `0x9000`, size `0x5000` (checked in the pinned `default_16MB.csv`); the exact `esptool … erase-region 0x9000 0x5000` command is in [05](05-firmware-guide.md) §13.5. Afterwards **redo the self-ID, idle test, upper check and arm** before the logger talks to the inverter again |
| **Full erase** | Everything: firmware, NVS, LittleFS (any unuploaded minutes are lost) | `pio run -e savx -t erase`, then `-t upload` and `-t uploadfs` |

After a settings reset or full erase:
1. The logger has no credentials, so it opens the portal by itself. Do §4.1 (the AP password is new; read it from the boot log).
2. The link state is gone, so the link stays **disarmed** (listen-only) until you redo the module self-ID, the idle test, the upper check and the arm ([05](05-firmware-guide.md) §7–§8). Then it runs the read-only probe.
3. Check `meta` and `status` in the PWA. The usage-threshold rules come back from `/cfg/alerts.json` (or from the cloud at the next `cfg` poll).

### 4.3 Roll back firmware

Upload the previous `.bin` at `/update` (§3.3), or re-flash over USB (§2.10 step 5).

### 4.4 Rotate the device credentials

Do this if the box is lost or opened by someone else, or periodically if you like.

1. Firebase console → Authentication: change the **device** user's password. Existing refresh tokens become invalid.
2. Re-provision the logger with the new password (§4.1).
3. If you create a new device user instead, put its UID into the GitHub Variable `FIREBASE_DEVICE_UID` and run **deploy-rules** (never into the committed rules file).

### 4.5 Lost owner password

Firebase console → Authentication → the owner user → reset password (an e-mail link), or set a new password directly. Sign in again **inside the Home Screen app**. (The reset link opens on `<project-id>.firebaseapp.com`, which is why that origin stays on the browser key's list.)

### 4.6 Restore from an export

The yearly export is plain JSON. The Firebase console's **Import JSON** on a node **replaces** that node's contents, so only import into the exact path the file came from, such as one `hist/<date>`. Do it with the logger stopped or the device's write access paused. Prefer importing into a scratch project first.

### 4.7 Web rollback and host failover

**Bad web deploy:** roll back on Cloudflare, then make git match ([06](06-cloud-and-dashboard-guide.md) §5.7):

```zsh
source ~/.nvm/nvm.sh && nvm use 24.21.0          # wrangler 4.146.0 needs Node >= 22
cd ~/Office/CodeBase/Personal/Datalogger/web
npx --yes wrangler@4.146.0 deployments list
npx --yes wrangler@4.146.0 rollback <VERSION_ID> --message "rollback: <why>"
```

Always name the `VERSION_ID`. Dashboard alternative: Workers & Pages → `savx-logger` → Deployments → **Rollback**. Then `git revert` the bad commit on `main`. Phones pick up the restored version on the next open ("Reload").

**Bad rules deploy:** `git revert` the rules commit (`deploy-rules` redeploys), or render the last good file and deploy from the Mac ([06](06-cloud-and-dashboard-guide.md) §3.3).

**Cloudflare unusable for days** (not for short outages: installed apps keep working from the cache): deploy the same build to the **Firebase Hosting fallback** by hand ([06](06-cloud-and-dashboard-guide.md) §5.9). ⚠️ `https://<project-id>.web.app` is a **new origin**: on every phone add the new icon, sign in, then delete the old one. Update `APP_URL` in the mailer and the dashboard URL in the logger portal.

### 4.8 Rotate CI and cloud secrets

| Secret | When | How |
|---|---|---|
| `CLOUDFLARE_API_TOKEN` | Before its end date, or at once if exposed | Cloudflare → API tokens → create a new **Edit Cloudflare Workers** token for this account only → update the GitHub secret → revoke the old token → re-run `deploy-web` |
| `GCP_SA_KEY` (fallback only) | Yearly, or at once if exposed (it bypasses the rules) | Google Cloud → IAM → Service Accounts → `gha-rtdb-rules` → Keys → add a new JSON key → update the secret → delete the old key. Better: switch to keyless WIF ([06](06-cloud-and-dashboard-guide.md) §4.4) |
| Mailer user password | If exposed | Firebase console → reset → update `FB_PASSWORD` → run `setup` |
| `NUDGE_KEY` | If exposed | New `openssl rand -hex 16` in the script property and in the logger's nudge URL |
| Device user password | If the box is lost | §4.4 |

After any suspicion of a leaked Cloudflare token, also check **Workers & Pages → savx-logger → Deployments** for versions you did not make, and roll back (§4.7).

### 4.9 Recover the e-mail mailer

1. Sign in to script.google.com as the **sender** account. If the account itself is locked or was deleted, create a new sender account and repeat [06](06-cloud-and-dashboard-guide.md) §9.1 and §9.3 (the Firebase `mailer` user and the rules stay as they are).
2. Run `sendSelfTest` (re-authorise if asked), then `setup`. Its log must end with `OK: signed in to RTDB, trigger "poll" installed …`.
3. Check the **Mailer** card turns green within 5 minutes, then press **Send test e-mail**.
4. Jobs younger than 12 h (24 h for severity ≥ 4) are delivered; older ones are marked `exp`, so a recovered mailer never floods the inbox.

---

## 5. Future upgrades

None of these are needed for v1. They are ordered roughly by value for effort.

> **Remote inverter settings (Phase 2)** is designed separately, with its own risk analysis, in [10 – Phase 2: remote settings](10-phase2-remote-settings.md). Do not enable writes outside that procedure.

### 5.1 Soldered joints

If a soldering iron becomes available:
- Solder the wrap joints on the header pins.
- Solder or heat-shrink the LAN-cable conductors. Use tape if there is no heat-shrink.
- Keep the module's screw terminals.

This removes the most likely long-term failure (R12). See [03](03-hardware-and-wiring.md).

### 5.2 MAX3232 module (fallback F-A)

A mini MAX3232 module (about ৳80 at local shops [SECONDARY listing]) gives a proper RS232 link. It replaces the RS232-lite workaround and removes R1/R3 stress.

| From | To |
|---|---|
| Inverter TX pin (found by the idle-level test) | MAX3232 RS232-RX (R1IN) |
| Inverter RX pin | MAX3232 RS232-TX (T1OUT) |
| COM pin 8 | MAX3232 0V |
| MAX3232 VCC | ESP 3V3 |
| MAX3232 TTL out | GPIO18 |
| MAX3232 TTL in | GPIO17 |

Set the link mode to `MAX3232` with `rxInv = txInv = false`: remove the HW-0519, run `reset-link all`, then `arm-max3232 YES` (refused while an HW-0519 self-ID is stored; the GPIO17 pre-arm check also refuses if GPIO17 sits on the MAX3232's R-OUT). Do a loopback test first: bridge RS232-TX to RS232-RX, and one TTL orientation must echo. Also useful if found: a **1 kΩ** series resistor in the ESP TX → inverter RX line and **≤ 4.7 kΩ** in the A lead for RS232-lite.

### 5.3 Cloudflare Workers + D1

Use this if you want server-side logic: cron aggregation, server-side alerts, or real **Web Push** to the PWA, which Spark can't do because it has no Functions. The PWA is **already** served by Cloudflare Workers static assets ([04 §12](04-architecture-and-data-model.md#12-web-hosting-and-cicd)), so the account and the pipeline exist.

- **Free tier:** Workers 100K requests/day; D1 500 MB per database, 100K rows written per day, and queries **fail** when over the limit ([D1 limits](https://developers.cloudflare.com/d1/platform/limits/), [Workers limits](https://developers.cloudflare.com/workers/platform/limits/)).
- **Path:**
  1. Write a small Worker that accepts the same JSON the device sends to RTDB.
  2. Point a second uplink target at it, or move the uplink entirely.
  3. Add a script to the existing `savx-logger` Worker, or a second Worker. ⚠️ A script makes requests count against the 100K/day script quota, and other Workers on the same account subdomain are same-site with the app ([R7](../research/r7-web-hosting.md) H15).
- The data model in [04](04-architecture-and-data-model.md) maps directly onto 1-minute rows (about 50 MB/year).

### 5.4 Home Assistant

This applies if an always-on Pi or PC ever becomes available.

- **Easiest, with no firmware change [INFERRED]:** HA's REST sensor polls `http://solar.local/api/live` on the LAN.
- **Don't** put a second reader on COM, such as ESPHome `pipsolar` on another ESP or a USB-RS232 cable. **Only one master is allowed** on the inverter's COM port.
- MQTT from the firmware would be a future code addition.

### 5.5 Other ideas

- **Family viewers:** create more Firebase users and add their UIDs to the `.read` rule and to `tools/render-rules.sh` ([`firebase/README.md`](../firebase/README.md) §7). Family members who only want e-mails need no account: add their address in Alerts → E-mail and to `ALLOW_TO`.
- **Faster e-mail (the nudge, v1.1, already built):** a web-app deployment of the mailer plus the logger's *Mailer nudge URL* cut the 0–5 min e-mail delay to seconds ([06](06-cloud-and-dashboard-guide.md) §9.7).
- **Gmail SMTP fallback from the ESP32 (R8 v2, not built):** it would only help when the mailer's trigger is broken, not when the sender account is (same account), and it would store a mailbox App Password in unencrypted NVS ([R8](../research/r8-email-alerts.md) §4.11).
- **CI addition:** run the rules emulator suite (`tools/rules-test/run.sh`, needs Java 21 and firebase-tools) in `ci.yml`. Today it runs only on the Mac. (The mailer tests already run in CI.)
- **PR previews:** off on purpose (Cloudflare preview URLs are public); one variable turns them on ([06](06-cloud-and-dashboard-guide.md) §5.8).
- **ESPHome cross-check:** a temporary ESPHome build with `pipsolar` (UART `inverted: true`) can confirm decoding during M3. Never run it alongside this firmware.
- **Higher temperature margin:** PSRAM ECC raises the module's limit from 65 °C to 85 °C, but it needs a custom sdkconfig (V2 §3.14). Do this only if the box runs hot.

### 5.6 Battery BMS data

It is tempting to read the lithium battery's BMS directly. **Don't disturb the BMS port.**

**Why not:**
- The inverter is the **master** on the BMS link: it polls the battery over RS485 or CAN and uses the answers to control lithium charging.
- Unplugging or disturbing that cable can raise battery-communication warnings, make the inverter fall back to voltage-based behaviour, or stop charging.
- "Sniffing" needs a Y-tap spliced into the live cable. That means unplugging it at least once and adding a stub to a live bus.
- CAN would need a CAN transceiver that isn't owned. The HW-0519 is RS485 only.
- The battery's protocol is unknown.
- **This is out of scope for this project.**

**Safe alternative, already in the plan:**
- Ask the inverter. The firmware probes the read-only `QLITH0` command (REVO) once and logs whatever BMS data the inverter relays.
- The inverter's own `soc` reflects BMS data when the battery link is active. Check whether the LCD "Li" icon is lit.

**If ever attempted anyway:** only with the inverter and battery shut down following the battery maker's instructions, with a proper RJ45 splitter, and as a listen-only receiver. Never transmit on that bus.
