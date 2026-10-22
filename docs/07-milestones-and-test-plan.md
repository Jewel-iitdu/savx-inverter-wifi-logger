# 07 — Milestones and test plan

This is the step-by-step build plan, from the first look at the inverter (M0) to the finished, soak-tested logger (M9). Each milestone has:

- **Goal** and **time estimate**.
- **Before you start:** what must already be done.
- **Steps:** numbered, with the result you should see.
- **Acceptance:** tick-boxes. Do not start the next milestone until every box is ticked.
- **If it fails:** what each symptom means and where to go next.
- **Record:** what to write down, and where.

The milestones expand [`research/d1-architecture.md`](../research/d1-architecture.md) §9, with the web hosting and CI/CD steps of [R7](../research/r7-web-hosting.md) §7 (M5, M6) and the e-mail acceptance tests of [R8](../research/r8-email-alerts.md) §8.3 (M7, M8). Wiring details are in [03 — Hardware and wiring](03-hardware-and-wiring.md), firmware details in [05 — Firmware guide](05-firmware-guide.md), cloud, hosting, app and e-mail details in [06 — Cloud and dashboard guide](06-cloud-and-dashboard-guide.md), and fixes in [08 — Risks, troubleshooting and maintenance](08-risks-troubleshooting-maintenance.md).

> **About names.** Button labels, `/api/...` routes, serial commands (`selftest`, `loopback`, `idle`, `arm`, `probe`, `status`, `cloud`, `rules`, `test-alert`) and PlatformIO environment names (`savx`, `native`) follow the firmware in this repository. Where a name here differs from [05 — Firmware guide](05-firmware-guide.md), 05 wins. Cloud and web names follow [06](06-cloud-and-dashboard-guide.md).

> ⚠️ **Nothing in M5–M7 touches the inverter link.** The cloud, hosting, CI and e-mail steps only read data the logger already has. The COM wiring rules (pins 1/2/8, never pin 4, never the BMS jack) and `INV_ALLOW_WRITES=0` apply unchanged.

---

## Overview

| M | Name | Time (with the reference implementation) | Inverter connected? | Hard gate before the next milestone |
|---|---|---|---|---|
| **M0** | Site survey and accounts | 1 evening (2–3 h) | No | COM free, BMS connected, an AC-OUT socket found, ntfy test received |
| **M1** | Bench bring-up (ESP32 + module only) | 1–2 evenings | No | PSRAM 8 MB (optional Arduino IDE board check first), unit tests pass, loopback OK, module self-test identifies RO |
| **M2** | Cable, idle-level test and upper check (Stages C, C2) | 1 evening (1–2 h) | **Receive only** | Exactly one pin idles negative, twice; the other reads `below-3V3` twice |
| **M3** | Two-way link, probe and dialect (Stages D–E) | 1–2 evenings | Yes | Valid replies that match the LCD |
| **M4** | Local-only logger | 1 evening + 24 h run | Yes | 24 h with ≥ 99 % valid polls, OTA and safe mode work |
| **M5** | Cloud uplink, GitHub repository and CI | 1–2 evenings + 48 h run | Yes | `ci` green, rules deployed by `deploy-rules`, refusal tests denied, back-fill works, < 15 MB/day |
| **M6** | Web app (PWA) on Cloudflare and the iPhone | 1 evening | Yes | `deploy-web` green, header checks pass, browser key restricted, installs, signs in, live and history work |
| **M7** | Alerts: push (Part A) and e-mail with usage-threshold rules (Part B) | 2 evenings + 24 h | Yes | Test alert, grid-loss test, dead-man e-mail; mailer heartbeat, test e-mail, rule e-mails once per day (R8 A1–A13) |
| **M8** | Enclosure, install and 7-day soak | 1 evening + 7 days | Yes | ≥ 99.5 % of minutes present, no false critical alerts, no duplicate e-mails (R8 A14) |
| **M9** | Polish and handover | 1 evening | Yes | Go-live checklist complete, web rollback drill done |

The times assume you flash the reference firmware and web app from this repository. If you write that code yourself, use the development estimates in [d1](../research/d1-architecture.md) §9 instead (M4 3–5 evenings, M5 2–3, M6 3–4, M7 1–2).

**Global stop rules (any milestone).** Pull the RJ45 out of COM and stop if:

- the inverter beeps, shows a new warning or fault code, or its FAULT LED changes when the logger is connected;
- anything gets warm or smells hot;
- a result does not match any row of the "If it fails" tables.

Then read [08](08-risks-troubleshooting-maintenance.md) before continuing.

**Standard commands** (run in `firmware/`; full setup in [05](05-firmware-guide.md)):

```bash
pio test -e native                    # host unit tests: 98 cases in 8 suites (CRC vectors, parsers, whitelist, aggregator, rules, uplink)
pio run -e savx                       # build the ESP32 firmware
pio device list                       # find the board's /dev/cu.* port
pio run -e savx -t upload --upload-port /dev/cu.XXXX   # flash over the "COM" USB-C port
pio device monitor -b 115200          # serial log and serial commands
```

**Checks for the other parts** (from the repository root; CI runs the first three when their area changes):

```bash
python3 tools/gen_codes.py --check                   # web/src/codes.ts and alertMetrics.ts are up to date
(cd web && npm ci && npx tsc --noEmit && npm run build)
(cd tools/apps-script && npm test)                   # mailer: 44 tests
sh tools/rules-test/run.sh                           # RTDB rules emulator: 87 cases + parity fuzz (needs Java; not run by CI)
```

---

## M0 — Site survey and accounts

**Goal:** know the installation before touching anything, and have every account ready. **Nothing is wired.**

**Time:** 1 evening (2–3 h). Optionally wait for a natural grid outage to find the AC-OUT sockets.

**Before you start:** read [03](03-hardware-and-wiring.md) §1 (the NEVER list). Print or open the site survey.

**Steps** (each has a matching section in the site survey):

1. **Photos.** Take every photo in site-survey §2.
2. **Ports.** Confirm that COM is empty, the BMS cable is plugged in, and the silkscreen reads COM 1 RXD, 2 TXD, 8 GND (§4). Do not touch the BMS cable.
3. **Front panel.** Note the LED states, LCD icons (especially the battery "Li" and battery-communication icons) and any code shown (§5).
4. **LCD readings.** Press UP/DOWN to step through every screen and write down each value, including the **U1** and **U2** firmware versions (§6, §7).
5. **Settings programs (read-only).** Follow the safe procedure in site-survey §8. Record at least programs 01, 02, 03, 05, 11, 12, 13, 16 and 29.
6. **WiFi scan.** Stand next to the inverter, open iPhone Settings → Wi-Fi, and write down any network you don't recognise (§11). Expected: none, because no collector is installed.
7. **AC-OUT sockets.** Find which sockets stay live when the grid is off, and whether the router/ONU is on one (§13, method in [03](03-hardware-and-wiring.md) §10.1).
8. **Network.** Note the 2.4 GHz SSID (not the password), the router position and the walls between it and the inverter (§14). Optional: measure RSSI at the planned box spot with the `tools/board_check` sketch, on the Mac running on battery, not connected to the inverter.
9. **Mounting spot.** Choose the wall spot using the placement rules in [03](03-hardware-and-wiring.md) §11.5 (§15).
10. **Mac and cables.** Check that the board's cable fits the Mac, the Mac can run 2 h on battery, and PlatformIO and Node 20 are installed (§16). The Arduino IDE (2.3.10 is installed) is only needed for the optional board check in M1.
11. **Apple ID storefront and alert apps.** Check the storefront, install **ntfy**, create a random topic, and send a test (§17):
    ```bash
    TOPIC="savx-$(openssl rand -hex 12)"     # 29 characters; store it in your password manager
    curl -d "test from Mac" "ntfy.sh/$TOPIC"
    ```
    Subscribe to the topic in the ntfy app first. Expected: the notification appears on the iPhone within a few seconds. Telegram is optional.
12. **Cloud accounts.** Create the Firebase project, the Realtime Database in **asia-southeast1**, and the owner and device users, following [06](06-cloud-and-dashboard-guide.md) §2. Create a Healthchecks.io account and a check with a 5 min period and 15–30 min grace (§18). These can also wait until M5 and M7; [06](06-cloud-and-dashboard-guide.md) §1 gives the full order.
13. **Hosting and e-mail decisions (no deploy yet).**
    - **GitHub:** an account with two-factor authentication. The repository will be **private**, under your personal account (M5).
    - **Cloudflare account subdomain `<acct>`:** choose it now and write the final dashboard address `https://solar.<acct>.workers.dev` into the site survey. ⚠️ It becomes permanent once a phone installs the app, and it is public: no address, phone or family names ([06](06-cloud-and-dashboard-guide.md) §5.2).
    - **E-mail:** who receives alert e-mails (at most 3 addresses), whether you can create a dedicated Gmail sender account (M7), and the day your electricity billing month starts.
14. **Optional:** phone IPS Bazar (01749 426 789) about the Solar of Things WiFi collector and a protocol document (§19).
15. **Answer the open questions** in site-survey §20 and [00](00-plan-overview.md) §10.

**Acceptance**

- [ ] Every field in site-survey Part 1 is filled, or marked "n/a" with a reason.
- [ ] COM is confirmed empty and BMS confirmed connected (photo).
- [ ] At least one AC-OUT socket is identified within reach of the mounting spot.
- [ ] The ntfy test message from `curl` arrived on the iPhone.
- [ ] U1/U2 versions and the key settings programs are recorded.

**If it fails**

| Symptom | Meaning | Action |
|---|---|---|
| Something is already plugged into COM | Another device may be talking to the inverter | Stop. Find out what it is. Only one device may use COM. |
| No AC-OUT socket within reach | The logger would stop during outages | Use a normal socket for M1–M4 and decide before M8. An existing household extension lead from an AC-OUT socket is fine. Logging still back-fills when power returns, but data during the outage is lost. |
| Router/ONU not on AC OUT | Uploads stop during outages (logging continues and back-fills later) | Move its plug to an AC-OUT socket if possible; otherwise accept the gap. |
| ntfy not in the App Store | The Apple ID's storefront does not carry it (Bangladesh is not an App Store storefront) | Use e-mail (Healthchecks and the mailer, M7) and, optionally, Telegram. See [08](08-risks-troubleshooting-maintenance.md). |
| A settings value starts blinking | You are in edit mode | Press **ESC** at once. Re-read site-survey §8. |
| Unknown SSID appears only near the inverter | Possibly a collector or other device | Record it. Do not join it. Mention it when asking for help. |

**Record:** site survey Part 1; photos in `docs/images/site/`.

---

## M1 — Bench bring-up (ESP32 and module only)

**Goal:** prove the toolchain, the board, WiFi, the UART inversion and the module's pin roles, **without the inverter**.

**Time:** 1–2 evenings.

**Before you start:** M0 accepted. PlatformIO installed ([05](05-firmware-guide.md) §1). The Mac may be on mains power during M1, because nothing is connected to the inverter.

**Steps**

0. **Optional: Arduino IDE board check (2 minutes).** Before PlatformIO, prove the board is what it claims to be. With **only USB** connected, flash [`tools/board_check/board_check.ino`](../tools/board_check/board_check.ino) from the Arduino IDE with the settings in [05](05-firmware-guide.md) §2.2 (**ESP32S3 Dev Module**, Flash **QIO 80MHz**, **16MB**, PSRAM **OPI PSRAM**, USB CDC On Boot **Disabled** on the COM port). Expected in the Serial Monitor at 115200 baud: `Flash size: 16 MB`, `PSRAM size: 8192 KB`, and a Wi-Fi scan with RSSI every 20 s. `PSRAM size: 0 KB` means the PSRAM menu is wrong, not that the board is bad. The real firmware later overwrites the sketch.
1. **Identify the "COM" USB-C port** ([03](03-hardware-and-wiring.md) §3.2) and plug the board into the Mac. `pio device list` should show a new `/dev/cu.*` port.
2. **Run the unit tests:** `pio test -e native`.
   - Expected: all tests pass, including the CRC vectors (for example `QPIGS` → `B7 A9 0D`, `QGMN` → `49 29 0D`) and the whitelist test (`POP02` is refused and **0 bytes** are written).
3. **Build and flash:** `pio run -e savx`, then upload as shown in the overview.
4. **Check the boot log** (`pio device monitor -b 115200`). Expected:
   - flash 16 MB and PSRAM about 8 MB (`ESP.getPsramSize()` ≈ 8,388,608);
   - the firmware version;
   - writes disabled (`INV_ALLOW_WRITES=0`);
   - the reset reason;
   - the setup portal name `SavxLogger-XXXX` and its password.
5. **WiFi setup.**
   1. On the iPhone, join the `SavxLogger-XXXX` network. The setup page opens (or browse to `192.168.4.1`).
   2. Choose your **2.4 GHz** network, enter its password, and set a strong **admin password** for the logger.
   3. Leave the cloud and alert fields empty for now (they are filled in M5 and M7).
   4. The board restarts and joins your WiFi.
6. **Local page.** On the iPhone (on the same WiFi) open `http://solar.local`. If it does not open, use the IP address shown in the serial log. Expected: the page loads, the status shows WiFi RSSI and the correct local time (Asia/Dhaka, UTC+6). The live tiles say "no inverter link", which is correct.
7. **BOOT button check.** Hold **BOOT** for 3 s while the board is running. Expected: the setup portal opens again. Close it (or let it time out) without changing anything.
8. **UART loopback (Stage B1), before the module is wired.** Follow [03](03-hardware-and-wiring.md) §8.3 B1: one wire between GPIO17 and GPIO18, run `loopback yes`, then **remove the wire**. Expected: byte-exact echo of `51 50 49 BE AC 0D`, inverted and non-inverted. The firmware refuses the loopback once a self-test or idle result is stored.
9. **Wire the module header (Stage B2):** P1–P4 from [03](03-hardware-and-wiring.md) §5.2. Screw terminals stay empty.
10. **Module self-test (Stage B3):** step 1 with A/B open, then step 2 with the temporary jumpers T1 (A+ → G) and T2 (B− → 3V3). Use **Diagnostics → Step 1 → Run self-ID** on the iPhone, or `selftest` on serial. Interpret with the table in [03](03-hardware-and-wiring.md) §8.3.
11. **Remove T1 and T2 completely.** Tape the bare J3-1 and J1-2 pins.

**Acceptance**

- [ ] Optional board check: 16 MB flash and about 8192 KB PSRAM reported, and your 2.4 GHz network seen in the scan.
- [ ] `pio test -e native`: 100 % pass (98 cases on 2026-10-21), including every CRC vector and the "`POP02` refused, 0 bytes sent" test.
- [ ] PSRAM ≈ 8 MB and flash 16 MB in the boot log.
- [ ] The board joins the 2.4 GHz WiFi and `http://solar.local` (or its IP) opens on the iPhone.
- [ ] The time is correct for Asia/Dhaka.
- [ ] The loopback is byte-exact in both modes.
- [ ] The self-test identified the receiver output (RO) pin, and the result is stored (visible at `GET /api/diag/pins`).
- [ ] T1 and T2 are removed.

**If it fails**

| Symptom | Likely cause | Action |
|---|---|---|
| No `/dev/cu.*` port appears | Charge-only cable, wrong USB-C port, or (USB-C↔C cable) the board lacks the resistors that make a C-to-C cable power it | Try the other port, the supplied cable, or another data cable. See [05](05-firmware-guide.md) for drivers. |
| Upload fails to connect | The board did not enter its bootloader | Hold **BOOT**, tap **RST**, release BOOT, upload again. |
| PSRAM reads 0 | Wrong board setting | Use the `savx` environment (board `esp32-s3-devkitc1-n16r8`), not a generic S3 board. In the Arduino IDE: PSRAM **OPI PSRAM** ([05](05-firmware-guide.md) §2.2). |
| Cannot join WiFi | 5 GHz-only network, or one SSID for both bands that steers the board badly | Use a separate 2.4 GHz SSID. On an iPhone hotspot, enable **Maximize Compatibility**. |
| `solar.local` does not open | mDNS blocked or the phone is on mobile data | Use the IP address from the serial log or the router's client list. Check the iPhone is on the home WiFi. |
| Loopback fails | Wire on the wrong pins, loose wrap | Check the labels 17 and 18, re-wrap, repeat. |
| Self-test: both HIGH / both LOW | See the table in [03](03-hardware-and-wiring.md) §8.3 | Fix and repeat. **Do not continue to M2 without a clear RO result.** |

**Record:** build record B1 and B2 in the site survey; a copy of the boot log.

---

## M2 — Cable and idle-level test (Stage C)

**Goal:** find out which of COM pins 1 and 2 is the inverter's transmit wire, and prove that the other one is below 3.3 V. **Only the module's A+ input touches the candidate pins. No ESP32 pin is connected to the RJ45.**

**Time:** 1 evening (1–2 h).

**Before you start:** M1 accepted. Inverter on, LCD lit. The charger is in an AC-OUT socket near the inverter, **or** the Mac is **on battery** (laptop rule).

**Steps**

1. **Prepare the LAN cable** exactly as in [03](03-hardware-and-wiring.md) §7, and record the colours in build record B3. Cross-check by twist: the pin 1 and pin 2 wires must be twisted together; no solid blue wire (pin 4) is stripped.
2. **Wire Stage C** ([03](03-hardware-and-wiring.md) §8.4): P5 (B− → G), pin 8 → B−, **pin 1** → A+. Pin 2's wire is taped. GPIO17 is not wired.
3. **Pre-power checklist** ([03](03-hardware-and-wiring.md) §12) and photo.
4. Power the ESP32, wait for boot, then plug the RJ45 into **COM**. Watch the LCD for 10 s: nothing may change.
5. Run **Diagnostics → Step 2 → Test pin 1 (10 s)** (`POST /api/diag/idle?pin=1`, serial `idle 1`). Run it **twice**. Write down % LOW and edges each time.
6. Pull the RJ45, unplug the USB, move **pin 2** onto A+ (tape pin 1's end).
7. Reconnect and run **Idle test, A+ on pin 2** **twice**.
8. **Upper check (Stage C2)** on the pin that read HIGH ([03](03-hardware-and-wiring.md) §8.4b): RJ45 out, USB out, take pin 8 + P5 out of B− as one piece (they stay on ESP GND), T3 jumper B− → 3V3, the HIGH pin on A+. Power up, plug in, run **Diagnostics → Step 2 → 2b → Upper check pin N** (serial `idle N 3v3`) **twice**. Then undo it (T3 out, pin 8 + P5 back into B−, the transmit wire back on A+).
9. While the board sits near the inverter, note the WiFi RSSI from the status page.

**Acceptance**

- [ ] Exactly **one** candidate shows **≥ 99 % LOW with < 3 edges** in 10 s, in **both** runs.
- [ ] The other candidate shows **HIGH** (≤ 1 % LOW) in both runs.
- [ ] The upper check on that other candidate shows **`below-3V3`** in both runs, and `GET /api/diag/pins` shows `arm.ready: true`.
- [ ] T3 is removed and pin 8 + P5 are back in B− (photo).
- [ ] No change on the inverter's LCD or LEDs when the RJ45 was plugged in.
- [ ] The pin map (inverter TX = pin __, inverter RX = pin __) is recorded in build record B4/B5, and the firmware shows it in `GET /api/diag/pins`.

**If it fails**

| Result | Meaning | Action |
|---|---|---|
| Pin 1 LOW, pin 2 HIGH | Expected result: pin 1 is the inverter's TX | Accept, go to M3. |
| Pin 2 LOW, pin 1 HIGH | Pin 2 is the inverter's TX | Accept, go to M3 (GPIO17 will go to pin 1). |
| Both HIGH | No negative idle found | Check the basics in [03](03-hardware-and-wiring.md) §8.4 and repeat. If it stays HIGH/HIGH: fallback F1 ([03](03-hardware-and-wiring.md) §9.1). **Do not connect GPIO17.** |
| Both LOW | Wire mix-up (the B− wire is probably not pin 8) | Stop. Re-read the colours through the plug. Repeat. |
| Many edges or 2–98 % LOW | Something is transmitting, or the input floats | Make sure the inverter is on, nothing else is in COM, and the screws are tight. Record numbers and see [08](08-risks-troubleshooting-maintenance.md). |
| Results differ between run 1 and run 2 | Loose joint | Tug test, re-tighten, repeat both runs. |
| Upper check `ABOVE-3V3` | The "receive" wire carries a **positive** voltage: probably not pin 1/2 (pin 4?) | **Stop.** Never connect GPIO17 to it. Re-check colours and twists, `reset-link`, redo M2 from step 2. |
| Board does not boot in Stage C2 | B− touches 3V3 **and** ground (N9) | Unplug the USB at once. Take pin 8 + P5 out of B−, check, retry. |

**Record:** build record B3, B4, B5; photos of the Stage C and C2 wiring.

---

## M3 — Two-way link, probe and dialect (Stages D and E)

**Goal:** talk to the inverter (read-only), identify its protocol dialect, and prove the decoded values against the LCD.

**Time:** 1–2 evenings.

**Before you start:** M2 accepted with a clear result. Same power rules as M2.

**Steps**

1. **Wire Stage D** ([03](03-hardware-and-wiring.md) §8.5): the inverter-TX wire on A+, pin 8 on B− with P5 (T3 removed), and **P6: GPIO17 → the other wire** (the one that tested HIGH and `below-3V3`). Pre-power checklist and photo.
2. Power the ESP32, then plug the RJ45 into COM. Watch the LCD for 10 s.
3. **Arm the link:** **Diagnostics → Step 3 → Arm link (enable TX)** (`POST /api/link/arm`; serial `arm <pin> YES`), naming the RJ45 pin GPIO17 is on. Expected: the firmware accepts only the pin that tested HIGH and `below-3V3`. It then runs the 0.2 s pre-arm check (GPIO17 LOW with its pull-down, module RO LOW) before it enables TX.
4. **Run the probe:** **Diagnostics → Step 4 → Re-run probe** (`POST /api/probe`; serial `probe`). The probe lists each attempt. Expected first lines (wording may differ):
   ```
   P1 2400 8N1 rxInv=1 txInv=1  QPI -> "(PI30" CRC ok
   QPIGS -> 21 fields CRC ok ; QMOD -> "(L" ; QPIRI -> 25 fields
   QALL (CHK) -> ...   dialect = REVO | STOCK21 | ...
   ```
5. **LCD cross-check.** With the live values open on the iPhone, step through the LCD screens (UP/DOWN) at the same moment:
   - output voltage within **±2 V**;
   - battery voltage within **±0.3 V**;
   - mode agrees with the LEDs: AC/INV solid = line (`L`), flashing = battery (`B`).
6. **Identity check.** In the local UI's **Raw** tab, or `GET /api/raw`:
   - QPIRI (0-based) field 5 = `3200` and field 7 = `24.0`;
   - `QVFW` = `(VERFW:000xx.yy` matching LCD **U1 xx yy**;
   - `QVFW2` matching **U2**.
7. **Dialect and PV field.** Note the detected dialect and the `pv12` decision (`W` or `A`). Around midday with sun, check that the decoded PV power is plausible against the LCD's charging-power screen.
8. **Capture real frames.** Turn **Raw capture on** (Diagnostics → Step 4, or `capture on`) and let it run for **200 fast-poll cycles** (about 17 min at 5 s), then off. Save the recent exchanges with `curl -s http://solar.local/api/raw > raw-….json` and the probe report with `curl -s http://solar.local/api/probe > docs/probe-report.json` ([05](05-firmware-guide.md) §9). The capture file `/log/raw-0.txt` has no HTTP download; copy it later over USB on the bench (`-t download_fs`, [05](05-firmware-guide.md) §9.3) if you need it.
9. **Turn real frames into tests.** There is no fixture loader: paste one or two real `QPIGS`/`QALL` replies into `firmware/test/test_parsers/test_main.cpp` with the LCD values you read as expected values ([05](05-firmware-guide.md) §16.3), then run `pio test -e native` again.

**Acceptance**

- [ ] QPI is valid: `(PI30` (or `(PI41`, `(PI17`), or `(NAK` from a PI30-family unit.
- [ ] QPIGS is CRC-valid in **≥ 99 %** of 200 polls.
- [ ] Output V within ±2 V and battery V within ±0.3 V of the LCD.
- [ ] QPIRI fields 5 and 7 are `3200` and `24.0`.
- [ ] QVFW / QVFW2 equal the LCD's U1 / U2.
- [ ] The dialect and `pv12` are decided and stored; unsupported commands are listed.
- [ ] Real frames are saved (`/api/raw` JSON, probe report) and at least one is a passing native test.
- [ ] The inverter showed no new warning or fault during the whole session.

**If it fails**

| Symptom | Meaning | Action |
|---|---|---|
| Arm refused | No clear idle-test or upper-check result stored, the self-test is missing, or you named the TX pin | Re-run M1 step 10 or M2, or correct the pin number. Never force it. |
| Arm refused: "GPIO… reads HIGH with its pull-down" | The GPIO17 wrap touches the GPIO16 wrap, or GPIO17 is on a positive wire | Unplug, separate the wraps ([03](03-hardware-and-wiring.md) §6.3), re-check the colours. Do not retry until fixed. |
| Arm refused: "RO … is not steady LOW" | A+ is not on the transmit wire, the inverter is off, or the GPIO16/GPIO18 wraps touch | Unplug and check against the Stage D table. |
| Silence on every profile | TX path not working | Fallback F2 ([03](03-hardware-and-wiring.md) §9.2): check GPIO17 is on the HIGH pin, the P6 splice, and the P5 ground jumper. |
| Replies only with RX non-inverted (profile P2) | The module's polarity is reversed in the wiring | Acceptable; the firmware keeps that profile. Note it. |
| `(NAK` to everything except QPI | Unusual dialect | Keep the raw capture and see [02](02-protocol-reference.md). |
| CRC errors above 1 % | Loose joint or electrical noise | Tug test all joints, keep the cable away from PV and battery cables, re-run. |
| Values disagree with the LCD | Wrong field mapping or dialect | Do not trust the data yet. Save `diag/raw` frames and see [02](02-protocol-reference.md) (REVO field 12 rule). |
| Inverter warning appears | Unexpected interaction | Pull the RJ45 at once, note the code, stop. |

**Record:** build record B6; `docs/probe-report.json`; the saved `/api/raw` JSON.

---

## M4 — Local-only logger

**Goal:** the full firmware runs for 24 h on the inverter and is useful on the home WiFi without any cloud.

**Time:** 1 evening plus a 24 h run.

**Before you start:** M3 accepted.

**Steps**

1. Flash the full firmware (USB as before, or OTA as in step 5).
2. On the iPhone, check each local page: Live tiles, 24 h chart, Raw, Diagnostics, Status.
3. Leave it running for **24 h**. Do not touch the wiring.
4. **Power-cycle test.** Note today's energy totals, unplug the USB for 10 s, plug it back in. The totals must come back within ±1 %.
5. **OTA test.**
   1. Build on the Mac; the file is `firmware/.pio/build/savx/firmware.bin`.
   2. AirDrop it to the iPhone (it lands in Files).
   3. Open `http://solar.local/update`, sign in with the admin user, and upload it.
   4. Expected: the device restarts and the status page shows the new build.
6. **Safe-mode check.** Power-cycle the board 3 times in a row, each time **before** it has run 120 s. Expected: it boots **normally** every time (`status` shows `safe no`). Only crash-type resets (panic, watchdog, brownout) count towards safe mode, so power cuts and load-shedding can never trigger it, and there is no way to force it on purpose. Read the recovery steps in [05](05-firmware-guide.md) §13 so you know them if it ever happens.

**Acceptance**

- [ ] 24 h on the inverter with **≥ 99 %** valid fast polls (status page).
- [ ] No unplanned resets (reset reason stays POWERON).
- [ ] The local page shows live data and a 24 h chart on the iPhone.
- [ ] The energy-integration unit test passes (1000 W for 3600 s → 1000 Wh ±0.5 %).
- [ ] Today's totals survive a power cycle within ±1 %.
- [ ] OTA from iPhone Safari works.
- [ ] 3 quick power cycles → normal boots (no safe mode), and you know the safe-mode recovery steps ([05](05-firmware-guide.md) §13).

**If it fails**

| Symptom | Action |
|---|---|
| Valid polls below 99 % | Check CRC error counters and joints; see [08](08-risks-troubleshooting-maintenance.md). |
| Brownout or watchdog resets | Use a shorter or better USB cable or another socket; consider the TX-power cap ([05](05-firmware-guide.md)). |
| Totals jump after a reboot | Report it as a firmware bug with the minute files ([05](05-firmware-guide.md)). |
| OTA upload rejected | Check the admin password and that you uploaded `firmware.bin`, not another file. |

**Record:** note the firmware version and the 24 h figures in build record B7.

---

## M5 — Cloud uplink, GitHub repository and CI

**Goal:** the code is in your private GitHub repository with CI running, the database rules are deployed by the pipeline, and data reaches Firebase reliably, survives internet outages and stays well inside the free plan.

**Time:** 1–2 evenings plus 48 h of observation.

**Before you start:** M4 accepted. The Firebase project from M0 exists ([06](06-cloud-and-dashboard-guide.md) §2: Realtime Database in asia-southeast1, email/password sign-in, the owner and device users, and the **device key** `savx-device`).

**Steps**

1. **Private repository and first push** ([06](06-cloud-and-dashboard-guide.md) §4.1–§4.2). Create the repository (private, personal account, 2FA on), then run the "nothing secret staged" check before the first commit. Set the **Secrets and Variables first** (step 2), or the workflows start at once and fail harmlessly.
2. **Secrets and Variables** ([06](06-cloud-and-dashboard-guide.md) §4.3–§4.4). For this milestone you need the Variables `FIREBASE_PROJECT_ID`, `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`, `FIREBASE_MAILER_UID=none`, and the Google credential for `deploy-rules`: keyless Workload Identity Federation (`GCP_WIF_PROVIDER`, `GCP_RULES_SA`, set up in Cloud Shell), or, if that asks for billing, the `GCP_SA_KEY` secret. The Cloudflare values and `VITE_*` Variables can wait for M6.
3. **CI.** Open **Actions → ci**. Expected: the `checks` job (codes, rules lint), and the `web` and `firmware` jobs, green. A red `ci` does not block anything on GitHub Free, so read it before you merge.
4. **Rules.** Run **Actions → deploy-rules → Run workflow** on `main` (or push a rules change). Expected job summary: `RTDB rules: deploy OK for <project> at <sha>`. The committed `firebase/database.rules.json` keeps its placeholders; only the rendered copy is deployed. Mac alternative: `tools/render-rules.sh`, then `firebase-tools deploy --only database` from `.deploy/` ([06](06-cloud-and-dashboard-guide.md) §3.1, §3.3).
5. **Refusal tests** from the Mac, with the **device key** (the browser key will refuse requests without the app's `Referer` once it is restricted in M6). Passwords are read with `read -s`, so they never land in shell history:
   ```zsh
   DB="https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app"
   API_KEY="<device key>"
   token() {  # usage: token EMAIL  (prompts for the password, prints an ID token valid 1 h)
     printf "password for %s: " "$1" >&2; read -s PW; echo >&2   # works in zsh and bash
     curl -s "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=$API_KEY" \
       -H 'Content-Type: application/json' \
       -d "{\"email\":\"$1\",\"password\":\"$PW\",\"returnSecureToken\":true}" |
       python3 -c 'import sys,json; print(json.load(sys.stdin)["idToken"])'
   }
   NOW=$(date +%s)
   # a) no auth -> must be denied (401)
   curl -s -o /dev/null -w "%{http_code}\n" "$DB/devices/inv1/live.json"
   # b) owner must NOT be able to write live (401)
   OWNER=$(token owner@example.com)
   curl -s -o /dev/null -w "%{http_code}\n" -X PUT -d "{\"ts\":$NOW,\"lk\":1}" "$DB/devices/inv1/live.json?auth=$OWNER"
   # c) device must NOT be able to write cfg (401)
   DEV=$(token inv1@example.com)
   curl -s -o /dev/null -w "%{http_code}\n" -X PATCH -d '{"liveSec":10}' "$DB/devices/inv1/cfg.json?auth=$DEV"
   # d) device must NOT be able to write the alert rules (401)
   curl -s -o /dev/null -w "%{http_code}\n" -X PUT -d '{"on":false}' "$DB/devices/inv1/alerts/email.json?auth=$DEV"
   ```
   Use your real user e-mails. All four must print `401`. The full set of 35 tests is in [`firebase/README.md`](../firebase/README.md) §5. Never paste tokens into chats or files.
6. **Enter the cloud settings** on the device: hold BOOT 3 s to open the portal ([05](05-firmware-guide.md) §5.3, [06](06-cloud-and-dashboard-guide.md) §10). Enter the **device key**, the database URL and the device user's e-mail and password. Leave the alert channels for M7.
7. **Watch it start.**
   - `cloud` on the console (or `http://solar.local/api/status`) shows `upState` `ready` and `auth` true.
   - In the Firebase console, `devices/inv1/live` updates about every 10 s.
   - Only look at small nodes such as `live` and `status`. **Never leave the console's data viewer open on `hist`**: console reads are billed.
8. **Back-fill test.**
   1. Unplug the router for **2 h** (warn the household, or do it at night). The logger keeps logging; the queue count `q` rises to about 120.
   2. Plug the router back in.
   3. Within minutes `q` returns to 0, and that day's `hist` contains the 120 missing minutes with no gaps.
9. **Token refresh.** After more than 3 h of running, cloud writes still succeed (the ID token lasts 1 h and must refresh by itself).
10. **Usage.** After 48 h, open **Realtime Database → Usage** in the console.

**Acceptance**

- [ ] The repository is private, under your personal account, and no secret was committed (`.env`, `.firebaserc`, `.deploy/` absent from git).
- [ ] `ci` is green on `main`.
- [ ] `deploy-rules` deployed the rules; the committed file still contains `OWNER_UID`, `DEVICE_UID` and `MAILER_UID`.
- [ ] All four refusal tests return 401 (and the full [`firebase/README.md`](../firebase/README.md) §5 set gives the expected codes).
- [ ] `live.ts` advances every 10 ± 2 s.
- [ ] Router off for 2 h → 120 minute records back-filled, with no gaps and no duplicates.
- [ ] Writes keep working after more than 3 h (token refresh).
- [ ] After 48 h, RTDB download is **below 15 MB/day**.

**If it fails**

| Symptom | Action |
|---|---|
| A refusal test returns 200 | **Stop the device uploads.** Fix the rules (UIDs pinned, no bare `auth != null`), redeploy, retest. |
| `deploy-rules`: "repository variable … is not set", or the auth step fails | Add the Variable; re-check the WIF values, or use the `GCP_SA_KEY` fallback ([06](06-cloud-and-dashboard-guide.md) §4.4, §14). |
| `ci`: "placeholders missing" | Real UIDs were committed into the rules. Restore the placeholders; UIDs belong only in Variables. |
| `upErr` `sign-in HTTP 403` | The browser key was entered in the portal. Use the device key. |
| `upErr` `GET cfg: HTTP 401` | Rules not deployed, or the wrong device UID in `FIREBASE_DEVICE_UID`. |
| 401 from the device after it worked | The device user's password was changed (the stored refresh token dies). Re-enter the credentials via the portal. |
| Download above 15 MB/day | Check `cloud` (`hs` handshakes, `rq` requests, quota level). Look for a retry loop or an app tab left open. See [06](06-cloud-and-dashboard-guide.md) §11. |
| Back-fill incomplete | Check the queue count and the `BACKLOG_STUCK` event; see [08](08-risks-troubleshooting-maintenance.md). |

**Record:** usage numbers in build record B7; the repository URL in your password manager.

---

## M6 — Web app (PWA) on Cloudflare and the iPhone

**Goal:** the dashboard is built and deployed by the pipeline to its **permanent** address, passes the header checks, and works from anywhere as an app on the Home Screen.

**Time:** 1 evening.

**Before you start:** M5 accepted. The Cloudflare account subdomain `<acct>` chosen in M0.

> ⚠️ **The origin is permanent.** `https://solar.<acct>.workers.dev` owns the Home Screen icon, the sign-in, the service worker and the history cache. Renaming the Worker or the account subdomain, adding a custom domain or switching hosts later means re-installing the app on every phone. **Do not tap "Add to Home Screen" until step 5 passes** ([R7](../research/r7-web-hosting.md) §2, §7.1).

**Steps**

1. **Cloudflare account** ([06](06-cloud-and-dashboard-guide.md) §5.2–§5.3): free plan, 2FA, set the account subdomain `<acct>`, create the API token from the **Edit Cloudflare Workers** template (one account only), and copy the account ID. Store them as the GitHub **Secrets** `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID`.
2. **Web Variables** ([06](06-cloud-and-dashboard-guide.md) §4.3): `VITE_FIREBASE_API_KEY` (the **browser** key), `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_PROJECT_ID`, `VITE_FIREBASE_APP_ID`, `VITE_FIREBASE_DATABASE_URL` (the asia-southeast1 URL), `VITE_DEVICE_ID=inv1`.
3. **First deploy:** **Actions → deploy-web → Run workflow** (or push a change under `web/`). Expected job summary: `Production: https://solar.<acct>.workers.dev (commit …)`, matching the address you wrote down in M0 **exactly**. Mac alternative (Node 24 via nvm, `npx wrangler@4.146.0 deploy`): [06](06-cloud-and-dashboard-guide.md) §5.4.
4. **Header checks** from the Mac with the `curl` block of [06](06-cloud-and-dashboard-guide.md) §5.5: CSP, HSTS and `X-Robots-Tag` on `/`; `/index.html` → 307 to `/`; `sw.js` and the manifest `no-cache`; a hashed `/assets/…js` file `immutable`; `colo=` DAC or CGP ideally.
5. **Mac Safari with the Web Inspector:** no CSP errors in the Console; a `wss://…asia-southeast1.firebasedatabase.app/.ws` connection in the Network tab; sign in as the owner and the Live page updates. `?demo=1` must work too.
6. **Restrict the browser key** ([06](06-cloud-and-dashboard-guide.md) §5.6) to `solar.<acct>.workers.dev/*`, `<project-id>.web.app/*` and `<project-id>.firebaseapp.com/*`. Wait a few minutes, then sign in again in a private window.
7. **Install.** On the iPhone, open `https://solar.<acct>.workers.dev` in Safari → **Share → Add to Home Screen**.
8. Open it **from the Home Screen icon** and sign in as the owner. A Home Screen app has its own storage, separate from Safari.
9. Check each page: Live, Today, History, Energy, **Alerts** (Rules / History / E-mail), Settings. Settings shows your User UID.
10. Close the app fully, reopen it: you should still be signed in.
11. Unplug the ESP32's USB and watch the Live page.
12. In Energy, export a CSV and open it in Files.
13. Switch the iPhone between light and dark mode.
14. **Offline shell:** Airplane Mode on, force-quit, reopen: the app shell loads from the service-worker cache with the last data. Airplane Mode off.
15. **Update drill:** change a visible string in `web/`, merge to `main`, wait for `deploy-web`, open the app: "A new version of the app is ready → Reload" appears; tap it.
16. **Rollback drill:** `npx wrangler@4.146.0 deployments list`, then `rollback <VERSION_ID>` from the Mac (or the Cloudflare dashboard), check the old string is back after "Reload", then `git revert` the change on `main` ([06](06-cloud-and-dashboard-guide.md) §5.7).

**Acceptance**

- [ ] `deploy-web` is green and its summary shows the permanent address written down in M0.
- [ ] The header checks pass on the real edge, and Safari shows no CSP errors.
- [ ] The browser key is restricted, and sign-in still works on the Mac and the iPhone.
- [ ] Installs to the Home Screen and opens standalone (no Safari address bar).
- [ ] Sign-in persists after closing and reopening.
- [ ] Live values update within 15 s.
- [ ] The **"Logger offline"** banner appears within 70 s of unplugging the ESP32.
- [ ] Today's chart with 1,440 points renders in under 1 s.
- [ ] CSV export opens in Files.
- [ ] Light and dark modes are both legible.
- [ ] The app shell opens in Airplane Mode.
- [ ] The update drill and the rollback drill both worked.

**If it fails**

| Symptom | Action |
|---|---|
| `deploy-web`: "repository variable … is not set" / "must be the asia-southeast1 URL" | Add the Variable; the database must be in asia-southeast1 (the CSP only allows that region). |
| `deploy-web` fails at the deploy step | The token expired or was revoked, or the account ID is wrong: new token ([06](06-cloud-and-dashboard-guide.md) §5.3). |
| The summary shows a different address | **Do not install.** Fix the account subdomain or the Worker name first. |
| CSP errors in Safari | A new external call or another database region. Update `web/public/_headers` **and** `firebase.json` together. |
| Sign-in fails after restricting the key | The Websites list is wrong; changes take a few minutes ([06](06-cloud-and-dashboard-guide.md) §5.6). |
| Red "No access … UID …" banner | The signed-in UID is not the deployed `OWNER_UID`: copy it into `FIREBASE_OWNER_UID` and run `deploy-rules`. |
| "Signed out every time" | You signed in inside Safari instead of inside the installed app. |
| An old version keeps showing | See [08](08-risks-troubleshooting-maintenance.md) §2.9 (stale service-worker cache). |

More: [06](06-cloud-and-dashboard-guide.md) §14 and [08](08-risks-troubleshooting-maintenance.md).

**Record:** the final dashboard address (site survey), the Cloudflare token's end date (calendar).

---

## M7 — Alerts: push and e-mail

**Goal:** you are told about faults, low battery and grid loss by push, you are told when the logger itself goes silent, and your usage-threshold rules send e-mail reliably: once when they should, never twice.

**Time:** 2 evenings plus a 24 h check. Part A (push) needs only the logger; Part B (e-mail) adds the sender Gmail account, the mailer and the rules.

**Before you start:** M6 accepted. The ntfy topic (and, optionally, the Telegram bot) from M0, and the Healthchecks check UUID.

> ⚠️ **E-mail deploy order: database rules → mailer → PWA → firmware** ([R8](../research/r8-email-alerts.md) §7). The rules from M5 and the PWA from M6 already contain the e-mail parts, so in practice: give the mailer access in the rules (B2) **before** running its `setup` (B3).

### Part A — Push channels and the dead-man's switch

**Steps**

1. Enter the ntfy topic, the Healthchecks UUID and (optionally) the Telegram token and chat id in the device settings (portal, or `POST /api/config` for ntfy and Healthchecks: [05](05-firmware-guide.md) §6.5). Optional: *Dashboard URL for notification links* = `https://solar.<acct>.workers.dev/#/alerts`.
2. **Test alert.** In the app's Settings tap **Send test alert**. The device reacts within 60 s.
3. **Grid-loss test.** With battery SOC above 50 % and modest loads:
   1. Switch off the breaker that feeds the inverter's AC input. The inverter goes to battery (AC/INV LED flashing).
   2. Wait **2 minutes**.
   3. Switch it back on.
   Expected: one `GRID_LOSS` alert after about 10 s, and one `GRID_RESTORED` alert about 30 s after the grid returns, including the outage duration.
4. **Dead-man test.** Unplug the ESP32. Expected: Healthchecks e-mails you after the 5 min period plus the grace time. Plug it back in; Healthchecks reports it back up.
5. **Quiet hours.** Temporarily set quiet hours to include the current hour and wait for, or cause, a low-severity event (the test alert ignores quiet hours). It must be logged in **Alerts → History** but not pushed. Restore your quiet hours afterwards.

**Acceptance (Part A)**

- [ ] The test alert arrives in **under 30 s**.
- [ ] Grid-breaker test: exactly one `GRID_LOSS` and one `GRID_RESTORED` with the duration, no duplicates.
- [ ] ESP32 unplugged → Healthchecks e-mail within period + grace.
- [ ] Quiet hours suppress pushes of severity 3 or lower (the event is still logged).

### Part B — E-mail mailer and usage-threshold rules

Follow [06](06-cloud-and-dashboard-guide.md) §9 and [`tools/apps-script/README.md`](../tools/apps-script/README.md) step by step; the steps below are the order and the checks.

**Steps**

1. **Sender account (B1).** Create a dedicated Gmail account used only for sending, with 2-Step Verification. Put "sign in to the sender account" in your yearly calendar (Google may delete accounts inactive for 2 years).
2. **Mailer user and rules (B2).** Firebase console → Authentication → **Add user** `mailer@<anything>.invalid` (random 24+ character password), copy its UID into the GitHub Variable `FIREBASE_MAILER_UID`, and run **deploy-rules**. Never edit UIDs into the committed rules file.
3. **Apps Script (B3).** Signed in as the sender: new project "Savx mailer", paste `appsscript.json`, `Code.gs` and `Lib.gs`, set the script properties (`FB_API_KEY` = the **device key**, `FB_DB_URL`, `FB_EMAIL`, `FB_PASSWORD`, `ALLOW_TO`, `APP_URL`). Run `sendSelfTest` and authorise the three permissions; the self-test mail must arrive. Run `setup`; its log must end with `OK: signed in to RTDB, trigger "poll" installed (every 5 min), …`. Set the trigger's failure notifications to **Notify me immediately**.
4. **Mailer refusal tests (B4).** In a terminal with `DB`, `API_KEY` (device key) and the `token` function from M5 step 5, sign in as the mailer and check that it cannot read what it must not:
   ```zsh
   MAIL=$(token mailer@example.invalid)
   curl -s -o /dev/null -w "%{http_code}\n" "$DB/devices/inv1/cfg.json?auth=$MAIL"                          # 401
   curl -s -o /dev/null -w "%{http_code}\n" "$DB/devices/inv1/hist.json?shallow=true&auth=$MAIL"            # 401
   curl -s -o /dev/null -w "%{http_code}\n" "$DB/devices/inv1/mailq.json?auth=$MAIL"                        # 200
   ```
5. **E-mail settings in the app (B5).** PWA → **Alerts → E-mail**: switch **Send alert e-mails** on, add the recipients (each must also be in `ALLOW_TO`), check the sender name, daily limit, billing-month start day, built-in alerts level, daily summary and "logger offline" settings, **Save**.
6. **Recipients' Gmail (B6).** Each recipient: a Gmail filter `subject:"[Solar]"` → **Never send it to Spam**, **Always mark it as important**; on the iPhone, the **Gmail app** with notifications on.
7. **Test e-mail (A3).** **Alerts → E-mail → Mailer → Send test e-mail.**
8. **Whole chain from the logger (A4).** Settings → **Send test alert** (or `test-alert` on the console).
9. **Day-energy rule (A5).** **Alerts → Rules → + New rule →** preset "Daily grid usage > 5 kWh", change the limit to **0.1 kWh** (100 Wh), **Save**. Wait until the banner says "✓ Logger has your latest rules". When the grid has supplied 0.1 kWh today, the rule fires. Then `reboot` the logger.
10. **Instant rule with hold and recovery (A6).** A custom rule: Load power `> 100 W`, hold 1 min, cooldown 5 min, **Also e-mail when it recovers** on. Run a load above 100 W for a few minutes, then switch it off.
11. **Offline delivery (A7).** With a rule that will fire (for example SOC or load), switch the router's Wi-Fi off for **40 minutes** while the rule fires, then on again (offline e-mail after 30 min, the default).
12. **Caps (A8).** The daily cap is proven by the mailer's tests (the 45-job case in `cd tools/apps-script && npm test`, [`tools/apps-script/README.md`](../tools/apps-script/README.md) §13). A live test would need more than 40 real alerts in one day; skip it unless you are investigating a cap problem.
13. **Allow-list (A9).** Add an address to the app's recipients that is **not** in `ALLOW_TO`, then press **Send test e-mail**.
14. **Daily summary (A10).** The next morning, compare the summary with the Energy page for yesterday.
15. **Mailer down (A11).** In script.google.com → Triggers, disable (or delete) the `poll` trigger for 20 minutes, then run `setup` again.
16. **Quota (A12).** After 24 h, compare the RTDB download per day with the M5 figure.
17. **Clean up.** Set the test rules back to real limits (for example 5 kWh) or delete them; remove the test recipient.

**Acceptance (Part B)** — [R8](../research/r8-email-alerts.md) §8.3:

- [ ] **A1 Rules:** the M5 refusal tests and the B4 mailer tests give the expected codes; the PWA and the logger keep working (live, minute uploads, `cfg`).
- [ ] **A2 Mailer health:** `setup` logged OK; the **Mailer** card in Alerts → E-mail is green and `mailer.hb` updates every 5 ± 1 min for 24 h; script.google.com → Executions shows at most 15 min of trigger runtime a day.
- [ ] **A3 Test e-mail:** delivered to every allowed recipient within **6 min** (about 1 min with the optional nudge), and the app shows "Sent to N".
- [ ] **A4 End-to-end:** "Send test alert" gives an ntfy push within 1 min **and** an e-mail within 6 min; the event in Alerts → History shows the e-mailed chip.
- [ ] **A5 Day energy rule:** **exactly one** e-mail that day; the reboot right after it gives no second e-mail; the rule's badge (`alertState`) shows fired/latched.
- [ ] **A6 Instant rule:** one open e-mail when the load runs, one "Cleared" e-mail after the load stops and the recovery hold passes, nothing in between.
- [ ] **A7 Offline delivery:** after reconnecting, **one** alert e-mail marked "Delayed …", one "Logger offline" e-mail and one "back online" e-mail.
- [ ] **A8 Caps:** `npm test` in `tools/apps-script` passes (44/44), including the 45-job case: at most 40 recipient-deliveries, exactly one "limit reached" e-mail, severity ≥ 4 still delivered, the rest marked `cap`. On the real system, `mailer.sent` never exceeds your daily limit.
- [ ] **A9 Allow-list:** the address not in `ALLOW_TO` gets nothing, and the app shows the mailer's warning.
- [ ] **A10 Summary:** arrives between 07:00 and 07:10 (default hour); yesterday's totals equal the Energy page within ±1 %.
- [ ] **A11 Mailer down:** "Mailer not running" appears in the app within 15 min; after `setup`, queued jobs younger than 12 h are delivered and older ones are marked `exp`.
- [ ] **A12 Quota:** RTDB download is at most 5 MB/day above the M5 figure.
- [ ] **A13 Safety unchanged:** `INV_ALLOW_WRITES=0` in `status`; no new inverter commands; the link statistics are unchanged during the tests.

**If it fails**

| Symptom | Action |
|---|---|
| No ntfy message | Check the topic spelling, that the app is subscribed to it, and `cloud` (channel states, last error). Test with `curl -d test ntfy.sh/<topic>` from the Mac. |
| Telegram silent | It may be blocked by the ISP. ntfy and e-mail remain. |
| `GRID_LOSS` missing | Check the live `gv` value during the test. The rule ignores mode `B` alone (on purpose, because SBU priority also runs on battery). See [05](05-firmware-guide.md). |
| Duplicate alerts | Report with the Alerts → History list; the debounce settings are in [05](05-firmware-guide.md). |
| `setup`: `RTDB GET /mailq -> HTTP 401` | B2 is not live yet: `FIREBASE_MAILER_UID` missing or `deploy-rules` not run. |
| `setup`: `Firebase sign-in failed: HTTP 403` | `FB_API_KEY` is the browser key. Use the device key. |
| Mailer card red "Mailer not running" | script.google.com → Triggers / Executions; run `showStatus`, then `setup`. |
| Event chip stays `q` | The mailer is not running (as above). |
| Event chip `norc` | The address is not in both the app's recipients and `ALLOW_TO`. |
| E-mail in Spam | The B6 filter; mark it "Not spam" once. |
| "Waiting for the logger to load the new rules" for more than 2 min | The logger is offline, or `status.ruleErr` names a problem. |

More: [08](08-risks-troubleshooting-maintenance.md) §2.8 and §2.11, [06](06-cloud-and-dashboard-guide.md) §14, [`tools/apps-script/README.md`](../tools/apps-script/README.md) §11.

**Record:** the sender account and the mailer user in your password manager; the `ALLOW_TO` list; the A2/A12 figures in build record B7.

---

## M8 — Enclosure, install and 7-day soak

**Goal:** the logger is boxed, mounted and proven over a week of normal life, including load-shedding.

**Time:** 1 evening plus 7 days.

**Before you start:** M7 accepted.

**Steps**

1. **Box it** following [03](03-hardware-and-wiring.md) §11: foam stack, USB-C hole, LAN notch, drain hole, knot for strain relief.
2. **Run the pre-power checklist** ([03](03-hardware-and-wiring.md) §12) one more time, and take final photos with the lid open.
3. **Mount it** beside the inverter: at least 15 cm from the case, antenna end up, out of the fan airflow and out of the sun.
4. Power up, plug in the RJ45, and check the status page: link OK, RSSI, chip temperature.
5. **Start the soak** and fill in the daily table in [the soak checklist](#final-7-day-soak-checklist) below.

**Acceptance**

- [ ] **≥ 99.5 %** of the week's minutes are present in the cloud after back-fill (at least 10,030 of 10,080).
- [ ] Maximum chip temperature (logged by the firmware) stays **below 55 °C**. This is the chip's internal sensor, which reads above room temperature, so the limit is conservative.
- [ ] RSSI stays **better than −75 dBm**.
- [ ] Link errors stay **below 1 %**.
- [ ] **Zero false severity-5 alerts.**
- [ ] **E-mail soak ([R8](../research/r8-email-alerts.md) A14):** no duplicate e-mails; no event with `em` = `fail`; no mailer heartbeat gap longer than 15 min; the logger's e-mail jobs (`rules` on the console, `status.mjq`) match the mails received; one daily summary each morning.

**If it fails**

| Symptom | Action |
|---|---|
| Temperature too high | Move the box away from the inverter's airflow and out of the sun. |
| RSSI too low | Move the box (keep the 15 cm rule) or the router. |
| Gaps in minutes | Check the reset reasons and whether the charger socket is really on AC OUT. |
| Intermittent link errors | Loose wrap joints are the usual cause: re-wrap, or do the solder upgrade ([03](03-hardware-and-wiring.md) §6.7). |
| Too many "Logger offline" e-mails during load-shedding | Raise **Alerts → E-mail → Logger offline after** (for example 120 min) or switch it off; Healthchecks still covers a dead logger. |
| A rule e-mails too often | Raise its cooldown or lower **Max per day**; check that its recovery level is not too close to the limit. |

---

## M9 — Polish and handover

**Goal:** the system can be run, recovered and maintained by its owner without help.

**Time:** 1 evening.

**Steps**

1. Read [README](../README.md) and [08](08-risks-troubleshooting-maintenance.md) end to end and fix anything unclear.
2. **Re-provisioning drill.** Hold BOOT for 3 s, re-enter the WiFi settings, and confirm the logger comes back, without looking at notes.
3. **Yearly export.** Run the manual REST export in [06](06-cloud-and-dashboard-guide.md) §12 (the planned `tools/rtdb-export.sh` script does not exist yet), and try the PWA's CSV exports. It must produce dated JSON files.
4. **Energy reconciliation.** Compare the device-integrated PV energy (`epv`) with the inverter's own figure (`epvi`), if the dialect reports it. Note the typical difference.
5. **Maintenance reminders.** Put them in your calendar (see the go-live checklist).
6. Note the optional upgrades you may do later: solder joints ([03](03-hardware-and-wiring.md) §6.7), a MAX3232 ([03](03-hardware-and-wiring.md) §9.4) and the mailer nudge ([06](06-cloud-and-dashboard-guide.md) §9.7).
7. **Web rollback drill**, if it was skipped in M6: roll back to the previous Cloudflare version and forward again ([06](06-cloud-and-dashboard-guide.md) §5.7).
8. **Keep a copy of the firmware binary** you are running (`~/savx-releases/`, [05](05-firmware-guide.md) §12): CI builds the firmware but does not keep the `.bin`.
9. Tag the release (for example `git tag v1.0 && git push origin v1.0`).

**Acceptance**

- [ ] WiFi re-provisioned via the BOOT long-press, unaided.
- [ ] The manual export produces dated JSON.
- [ ] A web rollback was done at least once (M6 or here).
- [ ] Docs reviewed.
- [ ] The go-live checklist below is complete.

---

## After M9: Phase 2 milestones

The optional remote-settings milestones (P2-M0 onwards) and their per-command on-site tests are in [10 – Phase 2: remote settings](10-phase2-remote-settings.md#8-milestones). They start only after this plan's soak and go-live checklists are complete.

---

## Final 7-day soak checklist

Fill in one row per day, at about the same time. Read the values from the app's Settings/Status page and the History or Energy view. Read only small, date-bounded nodes in the Firebase console.

| Day | Date | Minutes with data (`daily.n`, of 1440) | Link errors % | Max chip °C | Worst RSSI dBm | Reboots and reason (`rr`) | RTDB download MB/day | Alerts received (real / false) | E-mails (sent / duplicates / `fail`) | Mailer card green? | Grid outages (count, total min) | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | | | | | | | | | | | | |
| 2 | | | | | | | | | | | | |
| 3 | | | | | | | | | | | | |
| 4 | | | | | | | | | | | | |
| 5 | | | | | | | | | | | | |
| 6 | | | | | | | | | | | | |
| 7 | | | | | | | | | | | | |

**Daily checks**

- [ ] The Live page shows "updated N s ago" with N below 20.
- [ ] Queue count `q` is 0, or falling after an outage.
- [ ] Each alert matched a real event (for example a grid outage you noticed).
- [ ] The inverter's LCD shows no warning or fault that the app missed.
- [ ] **Alerts → E-mail → Mailer** card is green (heartbeat under 15 min old), and no event in Alerts → History is stuck on `q`.
- [ ] The daily summary e-mail arrived (default 07:00).
- [ ] The box is not hot to the touch, and the cables are undisturbed.

**At least once during the week**

- [ ] A real or simulated grid outage was logged, alerted and back-filled correctly.
- [ ] The LCD values were spot-checked against the app (output V ±2 V, battery V ±0.3 V).
- [ ] The Healthchecks dashboard shows no missed periods except planned ones.
- [ ] The summary's totals for one day were compared with the Energy page (±1 %).
- [ ] script.google.com → **Executions** (sender account): runs take a few seconds and the daily total stays far below 90 min.

**End-of-soak criteria:** the M8 acceptance list above.

---

## Go-live checklist

**Safety and hardware**

- [ ] The wiring matches [03](03-hardware-and-wiring.md) §5.2. Photos are stored in `docs/images/site/`.
- [ ] Only COM pins 1, 2 and 8 are connected. Pins 3–7 are insulated.
- [ ] T1 and T2 are removed. The earth terminal is empty. The R0 pad is open.
- [ ] The cable is labelled "INVERTER COM – NOT ETHERNET". The BMS cable is untouched.
- [ ] The box is mounted per the placement rules, with strain relief and a drain hole.
- [ ] The charger is in an AC-OUT socket. The router/ONU is on AC OUT, or the upload gap is accepted.
- [ ] The household knows what the box is and not to unplug it.

**Firmware**

- [ ] The running build has writes disabled (`INV_ALLOW_WRITES=0` in the boot log or status).
- [ ] A strong admin password is set. OTA works.
- [ ] Link profile, dialect and supported commands are stored, and the probe is not re-running in a loop.
- [ ] The safe-mode recovery steps are known ([05](05-firmware-guide.md) §13; safe mode cannot be forced on purpose).

**Cloud**

- [ ] Rules are UID-pinned and deployed by `deploy-rules` (or from `.deploy/`); the committed file still has its placeholders. The M5 refusal tests and the mailer tests are denied.
- [ ] Download is below 15 MB/day.
- [ ] The device and mailer users' passwords are long and random and stored in a password manager.
- [ ] The browser key is restricted to the dashboard's origins; the device key only to the two sign-in APIs.

**Pipeline**

- [ ] The repository is private, under your personal account; 2FA is on for GitHub, Cloudflare and the Google accounts.
- [ ] `CLOUDFLARE_API_TOKEN` (and `GCP_SA_KEY`, if used) exist **only** in GitHub Secrets. The token's end date is in your calendar.
- [ ] `ci`, `deploy-web` and `deploy-rules` are green on `main`; PR previews are off (`CF_PREVIEWS` unset).
- [ ] The final dashboard address is written in the site survey and matches the `deploy-web` summary.

**Alerts**

- [ ] The ntfy topic has at least 24 random characters and is stored privately. The test alert works.
- [ ] The Healthchecks check is active and its e-mail works.
- [ ] The mailer: `setup` done, trigger failure notifications on, `ALLOW_TO` matches the app's recipients, the **Mailer** card is green, the test e-mail and the daily summary arrive, every recipient has the `[Solar]` Gmail filter.
- [ ] Your usage-threshold rules are real values, not the M7 test limits.
- [ ] Thresholds are reviewed in the app's Settings:
  - `bvHigh` against the battery's own specification;
  - `socLow` against programs 12 and 29;
  - quiet hours and the daily summary hour.

**App**

- [ ] The PWA is installed from the permanent address and signed in inside the installed app. The "Logger offline" banner works.

**Records and maintenance**

- [ ] Site survey Part 1 and build record Part 2 are complete.
- [ ] `docs/probe-report.json` and the real-frame test cases are saved.
- [ ] Calendar reminders are set:
  - monthly for the first 3 months: check Firebase usage and the mailer's Executions page;
  - monthly: review Dependabot's update PRs (do not merge blindly; [08](08-risks-troubleshooting-maintenance.md) §3.4);
  - every few months: sign in to the Google account that owns the Firebase project **and** to the sender Gmail account (Google can delete accounts inactive for 2 years);
  - before the Cloudflare token's end date: create a new token and update the GitHub secret;
  - yearly: run the export, re-check the TLS root certificates, rotate `GCP_SA_KEY` if you use it, and re-read the free-tier pages of Firebase, Cloudflare, GitHub and Apps Script ([08](08-risks-troubleshooting-maintenance.md) §3.1).
