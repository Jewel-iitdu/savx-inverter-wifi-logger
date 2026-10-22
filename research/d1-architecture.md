# D1: Architecture and Decision Record for the Savx F4 3KW WiFi datalogger

Date: 2026-10-21. Role: solution architect.

**Inputs:**
- Research reports R1 (OEM), R2 (Voltronic PI30), R3 (Modbus families), R4 (apps), R5 (hardware), R6 (cloud/web).
- Verification reports V1 (protocol) and V2 (platform). Where a verification report contradicts a research report, the verification report wins.
- `user-constraints.md` (hard constraints from the user) and `manual-extract.md` (the Savx F4 paper manual).
- User photos 1–6.

**Confidence tags:**
- **[CONFIRMED-primary]**: read in an official document, datasheet, source code, the user's photo or the user's paper manual.
- **[SECONDARY]**: forum, README narrative, listing or search snippet.
- **[INFERRED]**: an architectural decision or reasoning by me.

Every design decision in this file is [INFERRED] unless it carries another tag. Facts carry the tag and URL of the report that verified them. Full URLs are collected in §11.

---

## 0. Corrections adopted from verification (traceability)

| # | Earlier claim | Adopted fact / decision | Source |
|---|---|---|---|
| K1 | R1/R2: the free inverter port is labelled "RS485" | The free port is **"COM" = RS232**. Pinout: 1 RXD, 2 TXD, 4 +VCC (dongle supply, voltage unknown), 8 GND. The **"BMS"** jack (1 485-B, 2 485-A, 4 CAN-H, 5 CAN-L) is occupied by the lithium battery cable. | V1 C1; photo `images/6.jpg`; paper manual §2.3 [CONFIRMED-primary] |
| K2 | R3: the most likely family is SMG Modbus (9600, slave 1) | The manual (§3.8) tells the user to install **WatchPower**, Voltronic's PC tool. So the protocol is the **Voltronic PI30 ASCII family at 2400 8N1**. Modbus families are last-resort probes only. | V1 C2 [INFERRED, high, from CONFIRMED-primary] |
| K3 | R1/R2/R3/R4/R5: "the RS485 module cannot talk to RS232, so buy a MAX3232" | The user can buy **nothing**. The module's differential receiver can *read* the inverter's RS232 TX line. The ESP32's inverted 0/3.3 V TX meets RS232 receiver thresholds (MAX3232: V_IT+ max 2.4 V, V_IT− min 0.6 V, input 3–7 kΩ). This is the "**RS232-lite**" link. A MAX3232 becomes an optional upgrade. | V1 C3, V2 C2; TI MAX3232 datasheet [CONFIRMED-primary values; INFERRED application] |
| K4 | R1 / seller listings: the unit has a USB port | **No USB port**: the bottom panel carries only COM, BMS, AC, battery, PV and a rocker switch. | V1 C4 [CONFIRMED-primary photo] |
| K5 | R1/R4: the vendor app is SmartESS/Eybond | The manual names an optional WiFi collector plus the **"SOLAR OF THINGS"** app (Shanghai Siseli New Energy). No collector was bought. | V1 C5 [CONFIRMED-primary] |
| K6 | R2 §9: use the RP6000 RS485 pinout (pin 4 = A) | **Dangerous.** COM pin 4 is +VCC. Only pins 1, 2 and 8 may ever be connected. | V1 C8 [CONFIRMED-primary manual] |
| K7 | R4: ESPHome `http_request` → Firebase | ESPHome has no Firebase Auth token handling. The cloud uplink lives in **custom Arduino firmware**. | V2 C5 [CONFIRMED-primary] |
| K8 | R6: async web server + WiFiManager + ElegantOTA in async mode | ElegantOTA v4: "You cannot run the synchronous `WebServer` and `ESPAsyncWebServer` in the same firmware". Decision: use the **sync stack** (core `WebServer`, WiFiManager 2.0.17, ElegantOTA 4.0.0 in sync mode). | V2 C4 [CONFIRMED-primary] |
| K9 | R6: `board = esp32-s3-devkitc-1` plus overrides | Use pioarduino **`board = esp32-s3-devkitc1-n16r8`**. It sets `qio_opi`, OPI PSRAM, 16 MB flash and `default_16MB.csv`. | V2 C11 [CONFIRMED-primary] |
| K10 | R4: RTDB Spark download is 360 MB/day | The Spark RTDB limit is **10 GB/month**. The 360 MB/day figure is the Spark **Hosting** transfer limit. | V2 C1 [CONFIRMED-primary] |
| K11 | R6: `sw.js` caches the cross-origin SDK from the CDN | A cross-origin SDK does not work offline. **Bundle and self-host** the Firebase SDK and uPlot. | V2 §3.11 [INFERRED] |
| K12 | R6: Telegram as a reliable backup | Telegram has been restricted in Bangladesh before. Use **ntfy primary, Telegram secondary, Healthchecks.io e-mail as a dead-man's switch**. | V2 §3.3–3.4 [SECONDARY + CONFIRMED-primary] |
| K13 | R1: QALL warning codes 00–08 | The Sorotec doc lists **00–07 and 21–26**. Codes 08 and 39 are mpp-solar additions. QALL fault codes are **not** the LCD fault codes. | V1 C12, C13 [CONFIRMED-primary] |
| K14 | Unsourced "gemini-claims" (QPIGS #15, DB9-style RJ45 pins, a "30001…" Modbus map) | All discarded. | V1 C9–C11 |
| K15 | R3: GPIO33–37 are used by PSRAM | **IO35–37** belong to the octal PSRAM on N16R8. GPIO16/17/18 are free. | V1 C18 [CONFIRMED-primary] |
| K16 | R1: compare the case with Sorotec's 335×225×100 mm | Compare with the Savx manual's own **344×255×105 mm, 5.5 kg**. | V1 C6 [CONFIRMED-primary] |

---

## 1. Executive decision

### 1.1 Existing app vs DIY

| Option | What it needs | Verdict | Evidence |
|---|---|---|---|
| Built-in WiFi on the inverter | Nothing | **Not present.** There is no WiFi/BT icon on the LCD, no WiFi jack, and the bottom panel has only COM and BMS. A zero-cost SSID scan in M0 confirms it. | photo 6, V1 E1 [CONFIRMED-primary] |
| Official route: optional "WiFi collector" on COM + "SOLAR OF THINGS" iOS app | **Buying** a collector. No price or availability was found, and IPS Bazar lists none. | **Rejected** under the no-purchase rule. Kept as fallback F-B. | manual §3.9 [CONFIRMED-primary]; App Store id6445806075 [CONFIRMED-primary]; R1 §2.2 (IPS Bazar has no WiFi accessory) [CONFIRMED-primary, by absence] |
| SmartESS / WatchPower / Energy-Mate (Eybond) dongles | Buying a dongle. Model-locked. Cloud-only, 5-minute data. | **Rejected.** It is a purchase, the ecosystem is probably wrong for this unit, and SmartESS is rated 2.69 (127 ratings) in the US store. | R4 §2, V2 I8 [CONFIRMED-primary] |
| WatchPower PC software over RS232 | A USB-RS232 adapter and an always-on PC | **Rejected.** Neither is owned. | R4, user constraints |
| Home Assistant / SolarAssistant | An always-on Pi or PC, and a purchase | **Rejected.** No host is available. | R4 §3.2–3.3 |
| **DIY: ESP32-S3 → Firebase → PWA** | Only parts already owned | **CHOSEN** | this document |

**Decision D1: build the DIY logger.** Reasons [INFERRED]:
1. It is the only option that needs no purchase.
2. The user owns all the data, with 1-minute history kept for years.
3. It works on the iPhone with no App Store dependency. Bangladesh is not an App Store storefront, so apps depend on the Apple ID's region (V2 I8).
4. It keeps logging through internet outages, with a local LittleFS buffer and back-fill.
5. The protocol is well understood. PI30 framing and every request frame were re-verified byte for byte (V1 §4.4).

### 1.2 Chosen stack

| Layer | Choice (pinned) | Why |
|---|---|---|
| Inverter link | COM RJ45 pins 1/2/8 only. "**RS232-lite**": HW-0519 receiver reads inverter TX; ESP32 GPIO17, inverted, drives inverter RX directly. UART1 at 2400 8N1, RX+TX inverted. | Uses only parts already owned. [INFERRED from CONFIRMED-primary thresholds, V1 §6.3–6.4] |
| Protocol | **Adapter framework, PI30 family first**: stock 17/21-field, MAX 24/28-field, PIP-GK, Sorotec REVO (QALL). Other families exist as detect-only probes. **Read-only whitelist** enforced at the link layer. | V1 §2.4: the PI30 family is high-confidence, but the REVO dialect is about 50/50 |
| MCU firmware | **Custom Arduino** firmware. Arduino-ESP32 **3.3.12** via **pioarduino 55.03.312-1**, `board = esp32-s3-devkitc1-n16r8`, `default_16MB.csv` partitions (2 × 6.25 MB OTA + 3.4 MB LittleFS). | V2 E5, E7, E8 [CONFIRMED-primary] |
| Device libs | ArduinoJson **7.4.3**, WiFiManager **2.0.17**, ElegantOTA **4.0.0** (sync), core `WebServer`, `HTTPClient`, `NetworkClientSecure`, `Preferences`, `LittleFS`, `ESPmDNS` | V2 §2.7 [CONFIRMED-primary versions] |
| Cloud client | **Hand-written REST client** (Identity Toolkit sign-in, securetoken refresh, RTDB `.json` REST with `print=silent`, keep-alive). Fallback: mobizt FirebaseClient **2.2.13**. | D6 below |
| Cloud DB | **Firebase Realtime Database, Spark (no-cost) plan, region `asia-southeast1`** (Singapore, the only Asian RTDB region) | V2 F1–F5 [CONFIRMED-primary] |
| Web app | **Static PWA** on Firebase Hosting (`<project>.web.app`). Vite build with TypeScript and Preact, **Firebase JS SDK 13.0.0** (modular, bundled), **uPlot 1.6.32**. Email/password sign-in. | V2 F13 [CONFIRMED-primary versions]; Vite/Preact versions pinned at implementation time |
| Local UI | ESP32 page at `http://solar.local` (mDNS) with a JSON API and OTA at `/update`. Works without internet. | R6 §8.3 |
| Alerts | ESP32 sends to **ntfy.sh** (primary) and the **Telegram bot API** (secondary). The ESP32 pings **Healthchecks.io** every 5 min, which e-mails the user if the logger goes silent. | V2 I3–I5, §3.3 [CONFIRMED-primary] |
| Power | The owned 5 V/2 A USB charger, plugged into an **inverter-backed socket** (AC OUT). The router/ONU must be on the same backed circuit. | R5 §5 Option A; V2 §3.13 |

### 1.3 Fallback stack

| ID | Trigger | Fallback |
|---|---|---|
| F-A (link) | The RS232-lite link fails (module damaged, or COM is not true RS232) | **Mini MAX3232 module** (৳80, a local shop [SECONDARY]) if a shop trip ever becomes possible. Same firmware with `rxInv=false, txInv=false`. |
| F-B (vendor) | The DIY link cannot be made to work at all | Ask IPS Bazar (01749 426 789) for the **Solar of Things WiFi collector**, which plugs into COM and uses the "SOLAR OF THINGS" app. Only one master is allowed on COM, so the ESP32 must then be removed or only listen. |
| F-C (cloud) | Firebase setup proves too complex, or Spark policy changes | **ThingSpeak** free (non-commercial): 4 channels × 8 fields at 1/min ≈ 2.1M of the 3M messages/year. Write with an HTTPS key. Charts in Safari. (V2 A3 [CONFIRMED-primary]) |
| F-D (local only) | No cloud at all | The ESP32 LAN UI (`http://solar.local`) with a 24 h PSRAM chart plus the LittleFS minute history (about 40 days). |
| Upgrade | Server logic, Web Push or cron aggregation wanted later | **Cloudflare Workers + D1** free tier: 100K requests/day and 100K rows written/day (V2 A4 [CONFIRMED-primary]) |

### 1.4 Decision log (ADR summary)

| ID | Decision | Rejected alternatives | Rationale |
|---|---|---|---|
| D1 | DIY logger | Vendor apps/dongles, HA, SolarAssistant | §1.1 |
| D2 | RS232-lite link on COM, pins 1/2/8 only | BMS port (occupied; inverter is master); buying a MAX3232 | No purchases. The BMS link must not be disturbed. |
| D3 | PI30-family adapter first. Other protocols are detect-only probes with raw capture. | Implementing every Modbus family | The manual and silkscreen point to PI30 over RS232. RS485 Modbus families cannot be on COM. |
| D4 | Custom Arduino firmware | ESPHome (`pipsolar`) | Firebase auth, back-fill, the probe tree and RS232-lite inversion handling all need code. ESPHome remains an optional cross-check tool only. |
| D5 | Firebase RTDB on Spark, asia-southeast1 | Firestore (20K writes/day cap, per-read billing), Supabase (7-day pause), Blynk (1-week retention), InfluxDB/Grafana (14–30 day retention) | RTDB has no per-operation caps, only bandwidth and storage. This design uses under 5% of them (§4.6). |
| D6 | Hand-written REST client | FirebaseClient 2.2.13 as primary | The device needs only 4 endpoints. Hand-written code gives explicit `print=silent`, keep-alive, back-off and quota guards, deterministic memory and one TLS client. It also avoids the library's async-mode echo (V2 L3). FirebaseClient stays the fallback. |
| D7 | Sync web stack | ESPAsyncWebServer | ElegantOTA and WiFiManager are sync. Mixing sync and async is forbidden (K8). |
| D8 | Aggregate on the device. **Never store raw 5–10 s samples in the cloud.** | Raw history in RTDB | Raw history at 10 s ≈ 1.19 GB/year, which breaks Spark (R6 §1.2). 1-minute aggregates ≈ 127 MB/year. |
| D9 | PWA with a bundled SDK, email/password auth | Google sign-in; CDN SDK | iOS standalone apps have redirect/popup problems, and the shell must load offline. |
| D10 | ntfy + Telegram + Healthchecks | iOS Web Push | Web Push needs a sender holding credentials, and Spark has no Functions. |
| D11 | Read-only whitelist, compile-time `INV_ALLOW_WRITES=0` | Expose setters | Setters include `PF` (factory reset) and battery voltages. One mistake could damage the battery. |
| D12 | Power from the charger on an inverter-backed socket | Battery buck converter; COM pin-4 +VCC | No parts to buy. Pin 4's voltage is unknown. |

---

## 2. System architecture

### 2.1 Block diagram

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
   LAN: http://solar.local                            | HTTPS (pinned roots: GTS R1-R4, ISRG X1/X2,
   local UI + /api/* + /update (OTA)                  |  Root YR/YE, Go Daddy G2, USERTrust ECC)
                  ^                                   +--> identitytoolkit / securetoken (device sign-in)
                  |                                   +--> Firebase RTDB asia-southeast1 /devices/inv1/...
      iPhone on home Wi-Fi (no internet needed)       +--> ntfy.sh --> Firebase --> APNs --> ntfy iOS app
                                                      +--> api.telegram.org --> Telegram (backup)
                                                      +--> hc-ping.com every 5 min --> e-mail if silent
                                                                         ^
   iPhone anywhere: Home Screen PWA https://<project>.web.app            | WebSocket (Firebase JS SDK 13)
   (Firebase Hosting; email/password owner sign-in) ---------------------+  reads live/hist/h15/daily/events
                                                                            writes cfg (owner only)
```

### 2.2 Data paths

| Path | Content | Cadence | Notes |
|---|---|---|---|
| Inverter → ESP | PI30 replies (`QPIGS`/`QALL`, `QMOD`, `QPIWS`, …) | 5 s fast cycle | One command at a time; CRC-checked |
| ESP RAM/PSRAM | Normalized `Sample`; 24 h ring of 5 s samples | 5 s | Local charts only. Lost on reboot (acceptable). |
| ESP LittleFS | 1-minute `MinuteRec` (about 56 B) | 60 s | The system of record until uploaded. Kept about 14 days after upload, up to about 40 days if not uploaded. |
| ESP → RTDB | `live` (10 s), minute PATCH (hist + daily + status), h15 (15 min), events (immediately), meta, diag | §4.3 | REST, `print=silent`, keep-alive |
| RTDB → PWA | `live` listener; date-bounded `get` of hist/h15/daily/events | On view | §6.4 |
| ESP → alerts | ntfy / Telegram POST; Healthchecks GET | On event; 5 min | §7 |
| PWA → RTDB → ESP | `cfg` (thresholds, `testAlert`, `reprobe`) | The device polls `cfg` every 60 s | Owner-only write |

### 2.3 Behaviour under failure

| Failure | Logging | Local UI | Cloud live | Cloud history | Alerts |
|---|---|---|---|---|---|
| Grid outage (inverter on battery) | Yes | Yes | Yes, if the router is on AC OUT | Yes | GRID_LOSS sent |
| ISP down, router up | Yes | Yes (LAN) | No | Back-filled later | Queued in RAM and sent on reconnect. Healthchecks e-mails after the grace period. |
| Router down | Yes | No | No | Back-filled later | As above |
| ESP loses power (inverter output off) | No (gap) | No | Stale. The PWA shows "offline". | Gap recorded | Healthchecks e-mail |
| Inverter link lost (wiring, firmware) | Status only | Yes, with diagnostics | `lk=0` | `n=0` minutes | LINK_LOST (ntfy) |
| Firebase Spark quota exceeded | Yes | Yes | No, for the rest of the month (V2 F9) | Back-filled next month if the queue still covers it | ntfy unaffected |
| ESP reboots with no internet | Yes, with provisional timestamps | Yes | No | Timestamps fixed after NTP (§5.6) | Queued |

---

## 3. Protocol strategy under uncertainty

### 3.1 Facts relied on

| Fact | Tag / source |
|---|---|
| COM = RS232; pinout 1 RXD, 2 TXD, 4 +VCC, 8 GND. The perspective of the RXD/TXD labels is **not stated**. | manual §2.3, photo 6 [CONFIRMED-primary] |
| Prior: RJ45 pin 1 = inverter TX. Voltronic's cable drawing maps RJ45 pin 1 to PC DB9 pin 2 ("TXD"). | V1 V3/V4 [CONFIRMED-primary drawing; INFERRED reading, medium-high] |
| PI30 serial format: 2400 baud, 8N1. CRC-16/XMODEM, high byte first. Any CRC byte equal to 0x28/0x0D/0x0A is incremented by one. Replies start with `(` and end with CR. | V1 V1, V8 [CONFIRMED-primary] |
| REVO dialect: `QALL` uses CHK = (sum of bytes + 1) & 0xFF. QPIGS field 12 is PV **power** (W). QPIGS field 15 is PV Wh today (seen as `00000` in a real trace). QPIGS fields 4 and 11 are reserved. | V1 V10, V16, V17 [CONFIRMED-primary doc] |
| Expected `QPIRI` identity for this unit: field 5 = `3200`, field 7 = `24.0`, field 4 ≈ `13.9` | V1 §6.4 D4 [INFERRED from nameplate + PI30 doc] |
| The LCD shows firmware versions "U1 xx xx" / "U2 xx xx". These should equal the `QVFW` / `QVFW2` replies. | manual §4.5 [CONFIRMED-primary] |
| Maximum PV input current is 15 A. A QPIGS field-12 value > 15 while PV V > 30 means the field is power, not current. | manual §11; V1 §6.5 [CONFIRMED-primary spec; INFERRED rule] |

### 3.2 Link layer (physical profiles)

`SerialLink` is the **only** code that can transmit to the inverter. It owns UART1, the pin assignment, the inversion flags, the whitelist check, echo suppression and a raw-frame ring buffer.

```cpp
enum class LinkMode : uint8_t { RS232_LITE, MAX3232, TTL_DIRECT, RS485_ON_COM };
struct LinkProfile {
  LinkMode mode = LinkMode::RS232_LITE;
  uint32_t baud = 2400;
  bool rxInvert = true, txInvert = true;     // RS232-lite: both inverted (V1 §6.4)
  uint8_t roPin = 18, diPin = 16, txPin = 17; // roPin/diPin swapped automatically if self-ID says so
  uint8_t invTxRjPin = 1;                     // 1 or 2, set by the idle-level test (M2); documentation only
};
// Serial1.begin(p.baud, SERIAL_8N1, p.roPin, p.txPin, /*invert=*/p.rxInvert && p.txInvert);
// then Serial1.setRxInvert(p.rxInvert); Serial1.setTxInvert(p.txInvert);   // both APIs exist in core 3.3.12 (V2 E4)
// diPin: pinMode(OUTPUT); digitalWrite(HIGH)  -> RS485 driver stays disabled forever in RS232-lite mode
```

Profiles tried by the prober, in order. Every profile is preceded by **3 s of silent listening**.

| # | baud | rxInv | txInv | Meaning |
|---|---|---|---|---|
| P1 | 2400 | true | true | Expected RS232-lite (V1 D1) |
| P2 | 2400 | false | true | Module polarity reversed in the wiring (V1 D1c) |
| P3 | 2400 | true | false | TX polarity probe (V1 F2) |
| P4 | 9600 | true | true | Some PI30 reports at 9600 (V1 D9) [SECONDARY] |
| P5 | 9600 | false | true | as P2 at 9600 |
| P6 | 2400 / 9600 | false | false | TTL-level or MAX3232 (only when `mode` = TTL_DIRECT / MAX3232) |

### 3.3 Adapter interface

```cpp
// src/proto/Adapter.h
struct ProbeResult { bool ok; uint8_t score; char family[12]; char dialect[10]; char detail[96]; };

class InverterAdapter {
public:
  virtual ~InverterAdapter() {}
  virtual const char* id() const = 0;                       // "pi30", "q6", "smg", "powmr", "raw"
  virtual ProbeResult probe(SerialLink& l, ProbeLog& log) = 0;   // read-only; < 15 s
  virtual void schedule(CommandScheduler& s) = 0;           // registers commands + periods (§3.7)
  virtual bool step(SerialLink& l, uint32_t nowMs, Sample& io) = 0; // runs ONE due command, merges into io
  virtual void identity(Identity& out) const = 0;           // QPI/QID/QVFW/QVFW2/QMN/QPIRI ratings
  virtual void onConfigChangedBit() {}                      // QPIGS status b6 -> re-read QPIRI/QFLAG
};
```

### 3.4 Adapters and their scope in v1

| Adapter | Status in v1 | Covers | Why |
|---|---|---|---|
| `Pi30Adapter` with dialect ∈ {`STOCK17`, `STOCK21`, `MAX24`, `GK`, `REVO`} | **Full decode** | QPI, QPIGS, QPIRI, QMOD, QPIWS, QFLAG, QVFW/2/3, QID/QSID, QMN/QGMN, Q1, QET/QT/QEDyyyymmdd/QLDyyyymmdd, and QALL/PQSE/PQSA for REVO | Expected family (K2) |
| `LegacyQ6Adapter` | Decode of `Q1\r`/`Q6\r` without CRC (field map from R2 §3.5) | Voltronic RP/RS-style firmware that answers `#-1` to PI30 | Cheap to add; seen on 24 V / 3 kW "Voltronic-look" units [SECONDARY] |
| `Pi18Detector` | **Detect + raw capture only** (`^P005PI` → `^D00518…`) | PI18 | No field map in the verified sources. Implement only if detected. |
| `ModbusSmgAdapter`, `ModbusPowMrAdapter` | **Detect + basic decode** (FC 03 only) | SMG 9600/slave 1; PowMr 2400/slave 5 (byte-swapped) | Last-resort on RS232 (V1 F3). PowMr exposes PI30 and Modbus on one 2400 UART [CONFIRMED-primary S2M code]. |
| SRNE / MUST / 8851 / G-ASCII | **Not implemented** | – | RS485 families cannot be on the RS232 COM (V1 §0.8) |
| `RawCaptureAdapter` | Always available | Listen-only. Logs every byte received with timestamps. | Used when nothing is detected, or a foreign master is present |

### 3.5 Boot and probe algorithm (read-only)

```
boot:
  load LinkProfile + adapterId + dialect + supported-command bitmap from NVS ("link")
  if profile exists:
      run adapter.probe() light: QPI (or QALL for REVO) + QPIGS x3 with plausibility
      success -> RUN
  else or fail -> FULL PROBE

FULL PROBE (never sends anything outside the whitelist):
  for profile in [P1..P5 (+P6 if mode != RS232_LITE)]:
      link.configure(profile); listen 3 s
      if unsolicited valid-looking frames ('(' / Modbus header): log, set FOREIGN_MASTER, STOP transmitting
      repeat 3x: QPI ('51 50 49 BE AC 0D'); wait 1.5 s first byte, 200 ms inter-byte; reject frames not starting '('
        '(PI30' / '(PI41' / '(PI17' / '(NAK' -> PI30 family -> classify dialect (§3.6) -> persist -> RUN
        '#-1\r'                              -> LegacyQ6 probe (Q1\r, Q6\r)
      '^P005PI' ('5E 50 30 30 35 50 49 71 8B 0D') -> '^D00518' => PI18 detected -> RawCapture + report
      'Q1\r', 'Q6\r' (no CRC)                     -> '(' + 8 / ~19 fields => LegacyQ6Adapter
  Modbus last resort (listen 3 s first):
      2400: '05 03 11 95 00 2D 91 43' (PowMr) ; 9600: '01 03 00 C9 00 01 54 34' (SMG; reject all-zero data)
  nothing -> RawCaptureAdapter (listen-only), UI shows "no protocol found", re-probe every 6 h

RUN:
  fast-cycle failure counter; 12 consecutive failed fast polls (~60 s) -> LINK_LOST event
  60 failed (~5 min) -> background re-probe with back-off 1 min, 5 min, 15 min, 1 h (read-only)
```

All frames are taken from V1 §4.4, where every one was recomputed [CONFIRMED-primary].

**Plausibility gates** applied before a QPIGS/QALL reply is accepted (V1 §6.4, manual §11 [INFERRED thresholds]):
- battery 18.0–34.0 V
- output V = 0 or 180–260
- output Hz = 0 or 45–65
- SOC 0–100
- PV V 0–520
- load 0–250 %
- QPIGS field count ∈ {17, 21, 24} (or ≥ 17)

**LCD cross-check during M3:** output V must match the LCD within ±2 V and battery V within ±0.3 V.

### 3.6 Dialect classification (V1 §6.5, in order)

1. **QALL** valid: 18 fields and a correct CHK. Try the CHK form `51 41 4C 4C 2B 0D` first, then the CRC form `51 41 4C 4C E8 FB 0D`. Result: **REVO**. QALL becomes the primary 5 s poll. QPIGS field 12 = PV W.
2. QPIRI ≥ 28 fields, or QFLAG contains `d`: **MAX24** (24-field QPIGS; QET/QED likely).
3. QPIWS is 36 characters, or QMOD returns `E`/`C`/`Y`/`D`: **GK**. The 24 V fault code sits at QPIWS a31–a32.
4. Otherwise: **STOCK17** or **STOCK21**, chosen by the QPIGS field count.
5. **Whatever the dialect,** if QPIGS field 12 > 15 while PV V > 30, treat field 12 as power. If field 12 × field 13 ≈ field 19 (±20 %), it is current. Persist the result as `pv12=W|A`.

Every optional command (`QPIWS`, `QFLAG`, `QET`, `QPIGS2`, `Q1`, `QMN`, `QLITH0`, `QT`) is tried once. A NAK or timeout marks it unsupported in an NVS bitmap. Unsupported commands are re-tried once a day.

### 3.7 Command schedule (2400 baud ≈ 4.17 ms/byte; a QPIGS reply takes about 0.46 s on the wire)

| Period | STOCK / MAX / GK | REVO |
|---|---|---|
| 5 s (fast) | `QPIGS` | `QALL` |
| 10 s | `QMOD`, then `QPIWS` (alternating, if supported) | `QMOD` (optional; QALL carries the mode), `QPIWS` if supported |
| 30 s | – | `QPIGS` (bus V, status bits) |
| 60 s | `Q1` (if supported: temperatures, charge stage) | same |
| 10 min, **or immediately when QPIGS status b6 = 1** | `QPIRI`, `QFLAG` | `QPIRI`, `PQSE` (read) |
| 5 min (if supported) | `QT`, `QET`, `QEDyyyymmdd`, `QLDyyyymmdd` | – (QALL gives Wh today / kWh total) |
| Boot + hourly | `QPI`, `QID`/`QSID`, `QVFW`, `QVFW2`, `QMN`, `QGMN` | same, plus `QLITH0` probe once |

Rules: one command in flight at a time. Flush RX before sending. 1.5 s first-byte timeout, 200 ms inter-byte gap, 150 ms gap between commands. Receive buffer 256 B. Accept CR or LF as the terminator (V1 C15). [CONFIRMED-primary timing constants from implementations; INFERRED choice]

### 3.8 Unified normalized data model (exact field list)

`Sample` fields use **SI units**. A missing value is NaN (floats) or `INT16_MIN` (ints), and is omitted from JSON. The same short keys are used in the firmware, the local API and the cloud `live` node.

**Core table: PI30 family** (indices are 0-based after stripping `(` and the CRC). Sources: V1 V14/V16/V17, R2 §5.2.

| Key | Unit | Meaning | PI30 STOCK17 / STOCK21 / MAX24 / GK | PI30 REVO |
|---|---|---|---|---|
| `ts` | s (UTC epoch) | Sample time | device clock (NTP) | device clock |
| `lk` | 0/1 | Valid inverter reply within 15 s | link layer | link layer |
| `gv` | V | Grid (AC input) voltage | QPIGS[0] | QALL[0] |
| `gf` | Hz | Grid frequency | QPIGS[1] | QALL[1] |
| `ov` | V | AC output voltage | QPIGS[2] | QALL[2] |
| `of` | Hz | AC output frequency | QPIGS[3] | QALL[3] |
| `va` | VA | Output apparent power | QPIGS[4] | – (QPIGS[4] reserved) |
| `w` | W | Output active power (load) | QPIGS[5] | QALL[4] |
| `ld` | % | Output load percent | QPIGS[6] | QALL[5] |
| `bus` | V | DC bus voltage | QPIGS[7] | QPIGS[7] (30 s) |
| `bv` | V | Battery voltage | QPIGS[8] | QALL[6] |
| `bci` | A | Battery charging current (integer A on PI30) | QPIGS[9] | QALL[8] |
| `bdi` | A | Battery discharge current | QPIGS[15] | QALL[9] (**not** QPIGS[15], which is PV Wh today on REVO) |
| `bi` | A | Net battery current, + = charging | derived: `bci − bdi` | derived |
| `bw` | W | Battery power, + = charging | derived: `bv × bi` | derived |
| `soc` | % | Battery capacity | QPIGS[10] (voltage-derived unless a BMS protocol is active [INFERRED]) | QALL[7] |
| `tmp` | °C | Inverter heat-sink temperature | QPIGS[11]. Flag as raw ADC if > 150 (P2014 note) | – (reserved) |
| `pvi` | A | PV input current | QPIGS[12] when classified as current (§3.6 rule 5) | QALL[11] |
| `pvv` | V | PV input voltage | QPIGS[13] | QALL[10] |
| `pvw` | W | PV power. On stock firmware this is "PV charging power", i.e. charger output. | QPIGS[19] (21/24-field); 17-field: `pvv × pvi` | QALL[12] (QPIGS[12] as cross-check) |
| `sccv` | V | Battery voltage measured by the SCC | QPIGS[14] | – |
| `md` | letter | Mode: P power-on, S standby, L line, B battery, F fault, H power-saving, D shutdown, C charge, Y bypass, E ECO | QMOD | QALL[15] |
| `st` | 8 chars | Status bits b7…b0 (b4 load on, b2 charging, b1 SCC charging, b0 AC charging, b6 config changed) | QPIGS[16] | QPIGS[16] |
| `st2` | 3 chars | Status bits b10 b9 b8 | QPIGS[20] | QPIGS tail |
| `wf` | 32/36/38 chars | QPIWS warning/fault bit string | QPIWS | QPIWS if supported |
| `wc` | code | Warning code (REVO table 00–07, 21–26) | – | QALL[16] |
| `fc` | code | Fault code. On GK/MAX 24 V: QPIWS a31–a32. **Tag the table source; LCD codes ≠ REVO codes.** | QPIWS a31–a32 (GK/MAX) | QALL[17] |
| `epv` | Wh | PV energy today, **integrated on the device** | §4.4 | §4.4 |
| `eld` | Wh | Load energy today, integrated | §4.4 | §4.4 |
| `ech` / `edis` | Wh | Battery charge / discharge energy today, integrated | §4.4 | §4.4 |
| `egr` | Wh | Grid-supplied energy today (**estimate**) | §4.4 | §4.4 |
| `epvi` | Wh | PV energy today **reported by the inverter** | `QEDyyyymmdd` (MAX) | QALL[13] |
| `etot` | kWh | Lifetime PV energy reported by the inverter | `QET`/1000 (MAX) | QALL[14] |
| `q` | count | Minute records waiting for upload | queue | queue |

Identity and ratings live in `meta` (§4.2), not in `Sample`:
- `QPI`, `QID`/`QSID`, `QVFW`, `QVFW2`, `QMN`.
- QPIRI: rating VA (field 5), W (6), battery rating V (7), battery type (12), max AC charge A (13), max charge A (14), input range (15), output priority (16), charger priority (17), and others.

**Secondary adapters** (decode only if detected). Sources: R3 §2.5 and §3 [CONFIRMED-primary maps]; R2 §3.5 [SECONDARY].

| Key | SMG Modbus (FC03, 9600, slave 1) | PowMr Modbus (2400, slave 5, **swap bytes in each word**) | Legacy Q6 (no CRC) |
|---|---|---|---|
| `gv` / `gf` | 202 ×0.1 V / 203 ×0.01 Hz | 4502 ×0.1 / 4503 ×0.1 | [0] / [1] |
| `ov` / `of` | 210 ×0.1 / 212 ×0.01 | 4510 ×0.1 / 4511 ×0.1 | [2] / [3] |
| `va` / `w` / `ld` | 214 / 213 / 225 | 4513 / 4512 / 4514 | [10]×10 / [11]×10 / [12] |
| `bv` | 215 ×0.1 | 4506 ×0.1 | [4] |
| `bci` / `bdi` | from 232 ×0.1 A signed (+ = charge) | 4508 / 4509 | [6] / – |
| `soc` | 229 | 4507 | [5] |
| `tmp` | 227 | 4557 (confirmed on HVM3.6M only) | [13] |
| `pvv` / `pvi` / `pvw` | 219 ×0.1 / 220 ×0.1 / 223 | 4504 ×0.1 / – / 4505 | – |
| `md` | 201 (0 power-on, 1 standby, 2 mains, 3 off-grid, 4 bypass, 5 charging, 6 fault) | flags 4553/4554 | [14] (B/L) |
| faults / warnings | 100–101 / 108–109 bitfields | 4530 error code (table not published) | – |
| `epvi` / `etot` | 443 ×0.01 kWh / 444–445 ×0.01 kWh (GM6200 only) | – | – |

### 3.9 Warning and fault normalization

- Store the **raw** value together with a **source tag**: `qpiws32`, `qpiws36`, `revo`, `smg`, `powmr` or `lcd`. Numbering differs between these tables (K13).
- One shared file `shared/codes.json` holds:
  - QPIWS P2014 bit meanings (a0–a31);
  - PGK/PMAX extra bits;
  - LCD fault codes 01–09 and 51–59 and warning codes 01, 02, 03, 04, 07, 08, 10, 15, 16, E9, bP, from the Savx manual §5/§6;
  - REVO warning 00–07 / 21–26 and fault 01–12.

  A script turns it into `firmware/include/codes.h` and `web/src/codes.ts`, so the device and the web app decode identically.
- "Fault-class" QPIWS bits are a1–a4, a7, a8, a18–a24, plus a9/a10/a11/a16 when a1 = 1 (P2014 rule "if a1=1 fault, else warning"). [CONFIRMED-primary R2 §5.6]

### 3.10 Read-only safety (defence in depth)

1. **Compile time:** `INV_ALLOW_WRITES=0` in `config.h`. Setter code is not compiled at all.
2. **Link layer:** `SerialLink::query()` refuses any command not in the exact-match whitelist (V1 §6.0):
   - `QPI QID QSID QVFW QVFW2 QVFW3 QMN QGMN QPIRI QPIGS QPIGS2 QMOD QPIWS QFLAG QDI Q1 QT QET QLT QLITH0 QALL PQSE PQSA QMCHGCR QMUCHGCR QBEQI`
   - `QEDyyyymmdd`, `QLDyyyymmdd`, `QEMyyyymm`, `QEYyyyy` (regex-checked)
   - `^P005PI`, legacy `Q1\r` / `Q6\r`
   - Modbus **FC 03 only**

   `PQSE`/`PQSA` are matched exactly, never by a `P` prefix. Everything else is refused, explicitly including `POP`, `PCP`, `PBT`, `PSDV`, `PCVV`, `PBFT`, `PF`, `PSET`, `PE`/`PD`, `PBATCD`, `MCHGC`, `MUCHGC`, `F50`/`F60`, `V230`, `RTEY`/`RTDL`, `BTA`, `DAT`, `~R`/`~S`, `^S…`, Modbus FC 06/10 and 8851 0x0010.
3. **Host unit test:** a test asserts that every refused command returns `PI_REFUSED` and that **zero bytes** are written (as in the R2 `sketch_core.h` test).

---

## 4. Cloud data model (Firebase RTDB, Spark, asia-southeast1)

### 4.1 Tree

Date keys use the local Asia/Dhaka date `YYYY-MM-DD`. Minute keys use local `hhmm`. Timestamps are UTC epoch seconds. All keys avoid `. $ # [ ] /` (V2 F4 [CONFIRMED-primary]).

```
/devices/inv1/
    live                          PUT every 10 s (354 B measured)
    status                        in the minute PATCH (≈208 B)
    meta                          PUT on boot and on change (identity, ratings, dialect, link profile)
    diag/raw/{cmd}                PUT every 10 min: last raw reply per command (≤300 chars each)
    hist/{YYYY-MM-DD}/{hhmm}      1-minute aggregate (≈242 B incl. key, measured)
    h15/{YYYY-MM-DD}/{hhmm}       15-minute aggregate (hhmm ∈ xx00, xx15, xx30, xx45)
    daily/{YYYY-MM-DD}            day totals (≈183 B incl. key, measured)
    events/{YYYY-MM-DD}/{pushId}  alerts and transitions (≈133 B, measured)
    cfg                           owner-written settings and commands
```

Sizes are compact JSON, measured with `json.dumps(separators=(',',':'))` on the examples below [CONFIRMED, measured].

### 4.2 Documents (exact JSON)

`live`:

```json
{"ts":1792560000,"lk":1,"gv":231.4,"gf":50.0,"ov":230.1,"of":50.0,"va":540,"w":512,"ld":16,"bus":381,
 "bv":26.42,"bci":12,"bdi":0,"bi":12.0,"bw":317,"soc":78,"tmp":38,"pvv":145.3,"pvi":3.2,"pvw":465,
 "sccv":26.44,"md":"L","st":"00010110","st2":"010","wf":"00000000000000000000000000000000","fc":0,
 "epv":1920,"eld":2080,"egr":330,"ech":900,"edis":410,"q":0}
```

`hist/2026-10-21/1435`, the 1-minute aggregate:

```json
{"w":498,"wx":620,"pvw":461,"pvx":480,"bv":26.4,"bvn":26.31,"bvx":26.48,"bi":11.6,"soc":78,"gv":231.2,
 "gvn":229.8,"ov":230.0,"ld":16,"tmp":38,"pvv":144.9,"md":"L","wn":0,"fc":0,"n":12,
 "epv":7.7,"eld":8.3,"ech":5.1,"edis":0,"egr":8.3}
```

Field meanings:
- Averages: `w`, `pvw`, `bv`, `bi`, `gv`, `ov`, `ld`, `pvv`.
- Maxima: `wx`, `pvx`, `tmp`. Minima: `bvn`, `gvn`. Max battery voltage: `bvx`.
- `soc`: last value. `md`: the dominant mode letter in the minute.
- `wn`: number of distinct warning bits seen. `fc`: last non-zero fault code. `n`: number of valid samples (12 when complete).
- `e*`: Wh in that minute, 0.1 Wh resolution.
- Optional `tq:0` marks a provisional timestamp (§5.6).

`h15/2026-10-21/1430` has the same schema, built from 15 minute records: n-weighted averages, minimum of minima, maximum of maxima, sum of energies, and `n` = sum.

`daily/2026-10-21`:

```json
{"epv":12840,"eld":9710,"egr":1220,"ech":4100,"edis":3050,"epvi":12700,"pvx":2410,"wx":1840,
 "bvn":24.9,"bvx":28.6,"socn":31,"socx":100,"gout":42,"nf":0,"nw":3,"n":1440}
```

All energies are in Wh. `gout` is minutes without grid. `nf`/`nw` count fault and warning events. `n` is minutes with data; coverage = n/1440.

`events/2026-10-21/-Nx…`:

```json
{"ts":1792560123,"type":"GRID_LOSS","sev":4,"st":"open","msg":"Grid lost, on battery (SOC 78%)","ch":"ntfy"}
```

`st` ∈ `open` | `clear` | `info`. A clear event adds `dur` (seconds). The owner may add `ack:true`.

`status`:

```json
{"ts":1792560000,"fw":"1.0.3","ip":"<logger-ip>","rssi":-61,"heap":182340,"psram":8123456,"up":86400,
 "rr":"POWERON","q":0,"proto":"pi30-2400","dia":"REVO","hs":12,"rq":1450,"lok":1792559995,"cok":1792560000}
```

`hs` = TLS handshakes today, `rq` = requests today, `lok` = last valid inverter reply, `cok` = last cloud OK. These feed the quota guard.

`meta` is kept flat so the rules can validate it:
- `qpi`, `qid`, `vfw`, `vfw2`, `qmn`
- `ratva`, `ratw`, `ratbv`, `btype`, `outpri`, `chgpri`, `inrng`
- `dia`, `pv12` (`W`|`A`), `sup` (comma list of supported commands)
- `link` (e.g. `rs232lite-2400-ii-tx1`), `fwver`, `boot`

`cfg` (owner-written; defaults shown):

```json
{"liveSec":10,"socLow":30,"bvLow":22.5,"bvHigh":30.0,"tempHigh":75,"loadHigh":90,"notifyRepeatMin":30,
 "keepHistDays":400,"keepEventDays":730,"quietFrom":22,"quietTo":7,"summaryHour":21,"testAlert":0,"reprobe":0}
```

### 4.3 Sample intervals

| Data | Interval | Where |
|---|---|---|
| Inverter fast poll | 5 s | device |
| Local 24 h ring | 5 s samples, about 17,280 × 48 B ≈ 0.8 MB of PSRAM | device RAM |
| `live` | 10 s (`cfg.liveSec` 5–300) | cloud |
| `hist` minute aggregate | 60 s, aligned to the wall-clock minute | LittleFS, then cloud |
| `h15` | 15 min, aligned | cloud |
| `daily` | Running totals in every minute PATCH; finalized at 00:05 local | cloud |
| `status` | 60 s | cloud |
| `cfg` poll | 60 s | device GET |
| `meta` | boot and on change | cloud |
| `diag/raw` | 10 min | cloud |
| Healthchecks ping | 5 min | hc-ping.com |

### 4.4 Aggregation on the device

- **Per-minute accumulators** (aggTask): for each numeric field, sum and count for the average, plus min and max where the schema needs them. Mode is tallied per letter. The warning-bit set is OR-ed.
- **Energy integration** uses the trapezoid rule on consecutive valid samples:
  `E += (P_prev + P_now)/2 × dt/3600`.
  - `dt` comes from `millis()` deltas, **only if 0 < dt ≤ 30 s**. A longer gap is counted as missing data and never interpolated.
  - Formulas:
    - `epv` from `pvw`
    - `eld` from `w`
    - `ech` from `max(0, bw)`
    - `edis` from `max(0, −bw)`
    - `egr` (estimate, labelled "est." everywhere) from `(w + acChgW)` while the grid is present and `md = L`
    - `acChgW = st.b0 ? max(0, bv×bci − (st.b1 ? pvw : 0)) : 0`
- **Precision note:** PI30 battery current has 1 A resolution, which is about 26 W at 24 V (R2). Battery energy figures are therefore coarse. `epv` and `eld` are good to a few percent.
- **Source of truth for daily totals:** the sum of the minute records in today's LittleFS file. On boot, recompute today's totals from that file. NVS holds a 5-minute checkpoint only as a fast-path cache. This keeps reboot and back-fill consistent.
- **Reconciliation:** if the inverter reports `epvi` (REVO QALL[13], MAX QED), store both values. The web app shows the difference. Do not override the integrated value.
- **Unit test:** a synthetic constant 1000 W for 3600 s at 5 s steps must integrate to 1000 Wh ±0.5 %.

### 4.5 Retention and pruning (no Cloud Functions on Spark, V2 F8)

| Node | Keep | Who prunes |
|---|---|---|
| `hist` | `cfg.keepHistDays` (default **400 days**, so this month can be compared with the same month last year) | Device, daily at 00:05 local. `GET hist.json?shallow=true` (≈6 KB), then `DELETE hist/{date}.json` for each date older than the cutoff (usually exactly one). |
| `events` | `cfg.keepEventDays` (default 730) | Device, same job, same date-key pattern |
| `h15`, `daily` | Forever | – |
| `live`, `status`, `meta`, `diag`, `cfg` | Overwritten | – |

`.validate` rules do not run on deletes, so the device needs only `.write` on these nodes (R6 §2.4 [CONFIRMED-primary]).

**Yearly export:** a `tools/rtdb-export.sh` script signs in as owner over REST and downloads `daily`, `h15` and one month of `hist` per request into dated JSON files. This counts against download quota: about 140 MB once a year is about 1.4 % of a month.

### 4.6 Quota math against the verified Spark limits

Spark RTDB limits: 1 GB stored, 10 GB/month download, 100 simultaneous connections. REST requests do not count as connections. Over quota, the app is shut off for the rest of the month. Billed download includes about 3.5 KB per TLS handshake, plus tens of bytes per message, plus rule-denied requests and console reads (V2 F1–F3, F9 [CONFIRMED-primary]). An RTDB REST response header measured 299 B (R6 [CONFIRMED, measured]).

**Storage** (JSON bytes, then ×2 for unknown internal overhead):

| Node | Per day | Steady state / growth | Year 1 | Year 10 |
|---|---|---|---|---|
| `hist` (242 B × 1440) | 348 KB | capped at 400 d ≈ 139 MB | 127 MB | 139 MB |
| `h15` (242 B × 96) | 23 KB | +8.5 MB/yr | 8.5 MB | 85 MB |
| `daily` | 0.18 KB | +0.07 MB/yr | 0.07 MB | 0.7 MB |
| `events` (≈20/day × 133 B) | 2.7 KB | capped at 730 d ≈ 2 MB | 1 MB | 2 MB |
| **Total ×2 overhead** | | | **≈273 MB (27 %)** | **≈454 MB (45 %)** |

Margin is at least 2.2× even at year 10. If `keepHistDays` is ever raised above about 1,000, re-check this table.

**Download per day:**

| Item | Nominal | Worst realistic |
|---|---|---|
| `live` PUT, 8,640/day × ~340 B (headers + TLS record) | 2.9 MB | – |
| Minute PATCH, 1,440 × 340 B | 0.5 MB | – |
| `cfg` GET, 1,440 × ~540 B | 0.8 MB | – |
| TLS handshakes | 144/day (reconnect every 10 min) × 3.5 KB = 0.5 MB | every request a new handshake: 11,820 × 3.5 KB = 41 MB |
| **Device subtotal** | **≈4.7 MB** | **≈44 MB** |
| PWA `live` listener, ~414 B per 10 s | 2 h/day open = 0.3 MB | 3 phones, 8 h each = 3.6 MB |
| PWA Today page (≤1440 × 242 B = 348 KB per open) | 10 opens = 3.5 MB | 90 opens = 31 MB |
| PWA History/Energy (h15 week 163 KB; daily month 6 KB) | 0.8 MB | 3 MB |
| **Total** | **≈9.3 MB/day ≈ 0.28 GB/month (2.8 %)** | **≈82 MB/day ≈ 2.5 GB/month (25 %)** |

**Runaway case:** a retry loop that opens a new TLS session every second costs about 300 MB/day, which reaches the 10 GB cap in about a month. Mandatory guards:
- (a) at least 5 s between retries, with exponential back-off up to 5 min;
- (b) a daily budget: more than 1,500 handshakes or more than 20,000 requests forces `liveSec = 60` and raises a `QUOTA_GUARD` event;
- (c) never leave the Firebase console data viewer open on `hist` (console reads are billed).

**Other limits:**

| Limit | Usage | Allowance |
|---|---|---|
| Hosting transfer (stricter figure, 360 MB/day) | Cold PWA load ≈ 220 KB (SDK ≈114 KB gzip measured for app+auth+database, uPlot 22 KB, app ~30 KB, icons ~50 KB), then cached by the service worker | ≈1,600 cold loads/day |
| Auth | 2 users | 50K MAU |
| Simultaneous connections | ≤ 3 dashboards | 100 |
| Firestore / Functions / Storage | not used | – |

### 4.7 Security rules (`firebase/database.rules.json`)

UIDs are pinned. Never write a rule of the form `auth != null` alone (V2 §3.16).

```json
{
  "rules": {
    ".read": false,
    ".write": false,
    "devices": {
      "inv1": {
        ".read": "auth != null && (auth.uid === 'OWNER_UID' || auth.uid === 'DEVICE_UID')",
        "live": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          ".validate": "newData.hasChildren(['ts', 'lk'])",
          "ts":  { ".validate": "newData.isNumber() && newData.val() > 1700000000 && newData.val() < now / 1000 + 600" },
          "md":  { ".validate": "newData.isString() && newData.val().length <= 2" },
          "st":  { ".validate": "newData.isString() && newData.val().length <= 12" },
          "st2": { ".validate": "newData.isString() && newData.val().length <= 4" },
          "wf":  { ".validate": "newData.isString() && newData.val().length <= 48" },
          "$num": { ".validate": "newData.isNumber() && $num.length <= 6" }
        },
        "status": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          "$k": { ".validate": "$k.length <= 8 && (newData.isNumber() || (newData.isString() && newData.val().length <= 40))" }
        },
        "meta": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          "$k": { ".validate": "$k.length <= 12 && (newData.isNumber() || newData.isBoolean() || (newData.isString() && newData.val().length <= 200))" }
        },
        "diag": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          "raw": { "$cmd": { ".validate": "$cmd.length <= 12 && newData.isString() && newData.val().length <= 300" } },
          "$other": { ".validate": false }
        },
        "hist": {
          ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'OWNER_UID')",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$hhmm": {
              ".validate": "$hhmm.matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/) && newData.hasChildren(['n'])",
              "md": { ".validate": "newData.isString() && newData.val().length <= 2" },
              "$f": { ".validate": "newData.isNumber() && $f.length <= 4" }
            }
          }
        },
        "h15": {
          ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'OWNER_UID')",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$hhmm": {
              ".validate": "$hhmm.matches(/^([01][0-9]|2[0-3])(00|15|30|45)$/) && newData.hasChildren(['n'])",
              "md": { ".validate": "newData.isString() && newData.val().length <= 2" },
              "$f": { ".validate": "newData.isNumber() && $f.length <= 4" }
            }
          }
        },
        "daily": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$f": { ".validate": "newData.isNumber() && $f.length <= 6" }
          }
        },
        "events": {
          ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'OWNER_UID')",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$eid": {
              ".validate": "newData.hasChildren(['ts', 'type', 'sev', 'msg'])",
              "ts":   { ".validate": "newData.isNumber()" },
              "type": { ".validate": "newData.isString() && newData.val().length <= 24" },
              "sev":  { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 5" },
              "st":   { ".validate": "newData.isString() && newData.val().length <= 8" },
              "msg":  { ".validate": "newData.isString() && newData.val().length <= 200" },
              "ch":   { ".validate": "newData.isString() && newData.val().length <= 16" },
              "dur":  { ".validate": "newData.isNumber()" },
              "ack":  { ".validate": "newData.isBoolean()" },
              "$other": { ".validate": false }
            }
          }
        },
        "cfg": {
          ".write": "auth != null && auth.uid === 'OWNER_UID'",
          "liveSec":  { ".validate": "newData.isNumber() && newData.val() >= 5 && newData.val() <= 300" },
          "socLow":   { ".validate": "newData.isNumber() && newData.val() >= 5 && newData.val() <= 80" },
          "bvLow":    { ".validate": "newData.isNumber() && newData.val() >= 20 && newData.val() <= 30" },
          "bvHigh":   { ".validate": "newData.isNumber() && newData.val() >= 26 && newData.val() <= 34" },
          "tempHigh": { ".validate": "newData.isNumber() && newData.val() >= 40 && newData.val() <= 100" },
          "loadHigh": { ".validate": "newData.isNumber() && newData.val() >= 50 && newData.val() <= 120" },
          "notifyRepeatMin": { ".validate": "newData.isNumber() && newData.val() >= 5 && newData.val() <= 1440" },
          "keepHistDays":    { ".validate": "newData.isNumber() && newData.val() >= 30 && newData.val() <= 3650" },
          "keepEventDays":   { ".validate": "newData.isNumber() && newData.val() >= 30 && newData.val() <= 3650" },
          "quietFrom":   { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 23" },
          "quietTo":     { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 23" },
          "summaryHour": { ".validate": "newData.isNumber() && newData.val() >= -1 && newData.val() <= 23" },
          "testAlert":   { ".validate": "newData.isNumber()" },
          "reprobe":     { ".validate": "newData.isNumber()" },
          "$other": { ".validate": false }
        }
      }
    }
  }
}
```

Notes:
- Rule semantics follow R6 §4 [CONFIRMED-primary]: `.validate` does not cascade, `$other` matches only unlisted keys, and each key of a multi-path PATCH is checked against its own path.
- The web API key is not a secret. Anyone can create Auth accounts (100/h/IP, V2 F12), but the UID pinning above makes such accounts useless.

### 4.8 Device request plan (REST)

The base is `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app`. Every request carries `?auth=<ID_TOKEN>&print=silent`, which returns 204 with no body (R6 §3.3 [CONFIRMED-primary]).

| When | Method and path | Body |
|---|---|---|
| 10 s | `PUT /devices/inv1/live.json` | live |
| 60 s | `PATCH /devices/inv1.json` | `{"hist/<d>/<hhmm>":{…},"daily/<d>":{…},"status":{…}}` (+ `"h15/<d>/<hhmm>"` every 15 min), ≈664 B |
| Back-fill | `PATCH /devices/inv1/hist.json` | up to 60 records `{"<d>/<hhmm>":{…},…}` ≈ 15 KB, then PATCH `daily/<d>` and `h15` for the affected days |
| 60 s | `GET /devices/inv1/cfg.json` (no `print=silent`) | – |
| Event | `POST /devices/inv1/events/<d>.json` | event |
| Boot / change | `PUT /devices/inv1/meta.json` | meta |
| 10 min | `PATCH /devices/inv1/diag/raw.json` | `{"QPIGS":"(…","QPIRI":"(…"}` |
| 00:05 | `GET /devices/inv1/hist.json?shallow=true`, then `DELETE /devices/inv1/hist/<old>.json` (same for events) | – |
| Auth | `POST identitytoolkit…/accounts:signInWithPassword?key=…`; `POST securetoken…/v1/token?key=…` | V2 F10 [CONFIRMED-primary] |

Keys are deterministic (`date/hhmm`), so retries are idempotent: a retry overwrites the same key and never duplicates a record.

---

## 5. Firmware design

### 5.1 Toolchain

```ini
; firmware/platformio.ini
[env:savx]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip
board = esp32-s3-devkitc1-n16r8        ; qio_opi, OPI PSRAM, 16MB, default_16MB.csv, -DBOARD_HAS_PSRAM (V2 C11)
framework = arduino
board_build.filesystem = littlefs       ; on the 3.4 MB "spiffs"-labelled partition of default_16MB.csv
monitor_speed = 115200
build_flags = -DARDUINO_USB_CDC_ON_BOOT=0 -DINV_ALLOW_WRITES=0 -DCORE_DEBUG_LEVEL=2
lib_deps =
  bblanchon/ArduinoJson@7.4.3
  tzapu/WiFiManager@2.0.17          ; verify the registry id at setup time
  ayushsharma82/ElegantOTA@4.0.0    ; sync mode: do NOT define ELEGANTOTA_USE_ASYNC_WEBSERVER

[env:native]                        ; host unit tests: CRC vectors, parsers on captured frames, aggregator, whitelist
platform = native
test_framework = unity
```

- Pin the release asset. Avoid the pre-releases Arduino 4.0.0-RC1 and pioarduino 61.04.00-RC1 (V2 §3.15 [CONFIRMED-primary]).
- Flash and read logs through the **"COM" USB-C port** (CH343 bridge to UART0), with CDC on boot disabled (R5 §1.4).

### 5.2 Repository and module layout

Final location: `<project>`.

```
Datalogger/
  docs/                      plan, wiring photos, site-survey sheet, probe reports, raw captures
  shared/codes.json          warning/fault code tables -> generates firmware/include/codes.h + web/src/codes.ts
  tools/                     crc_table.py (from r2-code), gen_codes.py, rtdb-export.sh, decode_capture.py
  firebase.json              hosting.public = web/dist ; database.rules = firebase/database.rules.json
  firebase/database.rules.json
  firmware/
    platformio.ini
    include/config.h         pins, defaults, INV_ALLOW_WRITES, intervals
    include/roots.h          PEM trust anchors (§5.13)
    include/codes.h          generated
    src/main.cpp             boot, safe-mode check, task creation
    src/link/SerialLink.*    UART1, LinkProfile, whitelist gate, echo/flush, raw ring
    src/link/PinDiag.*       module RO/DI self-ID, idle-level test (Stage C)
    src/proto/Adapter.h      interface (§3.3)
    src/proto/Prober.*       decision tree (§3.5–3.6)
    src/proto/Scheduler.*    per-command periods, supported bitmap
    src/proto/pi30/Pi30Frame.h   CRC/build/check (port of r2-code/pi30.h, host-tested)
    src/proto/pi30/RevoChk.h     sum+1 checksum
    src/proto/pi30/Pi30Adapter.* dialects STOCK17/STOCK21/MAX24/GK/REVO
    src/proto/legacy/Q6Adapter.*
    src/proto/modbus/ModbusRtu.*  CRC-16/MODBUS, FC03 only
    src/proto/modbus/SmgAdapter.*, PowMrAdapter.*
    src/proto/RawCapture.*
    src/model/Sample.h, Identity.h, Decode.*
    src/agg/Aggregator.*     energy integration, minute/h15/daily, PSRAM ring
    src/agg/AlertEngine.*    rules, hysteresis, rate limits (§7)
    src/store/MinuteQueue.*  LittleFS append-only files, cursors, time anchors
    src/store/Settings.*     NVS (Preferences) namespaces (§5.12)
    src/net/WifiProv.*       WiFiManager portal + custom params
    src/net/TimeSync.*       SNTP, TZ, provisional-time fix-up
    src/net/FirebaseRest.*   sign-in, refresh, RTDB PUT/PATCH/GET/POST/DELETE, keep-alive, budgets
    src/net/Uplink.*         uplink task state machine (§5.5)
    src/net/Notify.*         ntfy, Telegram, Healthchecks
    src/web/LocalWeb.*       WebServer routes, ElegantOTA, mDNS
    src/sys/Watchdog.*, StatusLed.*, SafeMode.*
    data/www/index.html.gz   local UI (uPlot inlined; works offline)
    test/                    unity tests (native)
  web/                       Vite + TypeScript + Preact PWA (§6)
```

### 5.3 FreeRTOS tasks

| Task | Core | Prio | Stack | Period | Owns |
|---|---|---|---|---|---|
| `invTask` | 1 | 5 | 8 KB | Scheduler tick 50 ms; 5 s fast cycle | UART1 (`SerialLink`), adapter, prober. Publishes `Sample` to `qSample` (depth 4, newest wins) and `gLatest` (mutex `mLatest`). |
| `aggTask` | 1 | 4 | 8 KB | On each sample; minute, 15-min and day roll-overs | Accumulators, energy integration, PSRAM ring, `MinuteQueue` append (mutex `mFs`), `AlertEngine` → `qAlert` (depth 24) |
| `uplinkTask` | 0 | 3 | **16 KB** | 1 s tick | **All TLS**: RTDB, Auth, ntfy, Telegram, Healthchecks. Back-fill, prune, cfg poll. Only one TLS client stays open; one-shot clients for notifications. |
| Arduino `loopTask` | 1 | 1 | 8 KB (core default) | continuous | `WebServer.handleClient()`, `ElegantOTA.loop()`, WiFiManager portal, BOOT-button long-press, status LED |

Event group bits: `WIFI_UP`, `TIME_OK`, `AUTH_OK`, `LINK_OK`, `SAFE_MODE`, `PORTAL`.

Rationale [INFERRED]:
- Keeping TLS objects in one task avoids sharing non-thread-safe clients.
- 16 KB of stack covers mbedTLS plus JSON work. Arduino builds use dynamic mbedTLS buffers, about 22–42 KB of heap per session (V2 E9, R6 §3.6 [CONFIRMED-primary]).

### 5.4 Timing budget

| Activity | Period / limit |
|---|---|
| Fast poll (QPIGS or QALL) | 5 s; ≤ 0.7 s on the wire |
| QMOD / QPIWS | 10 s, alternating |
| Command timeouts | first byte 1.5 s, inter-byte 200 ms, inter-command gap 150 ms |
| Live upload | 10 s |
| Minute record | at :00 of each wall-clock minute (or every 60 s of uptime if there is no time) |
| cfg poll / status | 60 s |
| HTTP connect / handshake / response timeouts | 5 s / 10 s / 10 s; TWDT reset between steps |
| Token refresh | 5 min before `expiresIn` (3600 s) |
| SNTP resync | 3 h (core default `CONFIG_LWIP_SNTP_UPDATE_DELAY=10800000`, V2 E9) |
| NVS energy checkpoint | 5 min |
| Healthchecks ping | 5 min |
| Prune | 00:05 local daily |

### 5.5 Offline buffering and back-fill

LittleFS layout:

```
/www/index.html.gz          local UI
/q/2026-10-21.bin           MinuteRec, append-only
/q/2026-10-21.up            uint16 cursor: records already uploaded
/q/anchors.bin              {bootId, uptimeMs, epoch} written at each first NTP sync per boot
/log/probe.json             last probe report;  /log/raw-*.txt (M3 capture, ≤256 KB, rotated)
```

```cpp
struct __attribute__((packed)) MinuteRec {   // 55 bytes
  uint32_t ts; uint16_t bootId; uint32_t upMs; uint8_t n, flags;   // flags: b0 provisional time, b1 link gap
  int16_t w, wx, pvw, pvx, bw;  uint16_t bv, bvn, bvx;  int16_t bi;          // W, cV, dA
  uint16_t gv, gvn, ov, pvv;  uint8_t soc, ld;  int8_t tmp;  char md;  uint8_t wn, fc;
  uint16_t epv, eld, ech, edis, egr;   uint8_t crc8;                           // dWh in this minute
};
```

At 1,440 × 55 B ≈ 79 KB/day, the 3.4 MB partition holds about **40 days**.
- Keep uploaded files for 14 days, for the local history page.
- When space runs out, drop the oldest *uploaded* file first, then the oldest unuploaded one. Dropping unuploaded data writes a `DATA_DROPPED` event.

Uplink state machine:

```
OFFLINE --wifi--> NEED_AUTH --signIn/refresh ok--> READY
READY (1 s tick), priority order:
  1 alerts in qAlert          -> ntfy (+Telegram for sev>=4); write event to RTDB
  2 live due                  -> PUT live
  3 minute record pending     -> multi-path PATCH (hist+daily+status[+h15])
  4 backlog (cursor < size)   -> PATCH up to 60 recs to /hist every >=2 s; then daily/h15 for those days
  5 cfg due                   -> GET cfg; act on testAlert/reprobe changes
  6 housekeeping              -> meta/diag/hc-ping/prune
errors: 401 -> refresh token (then full sign-in); 5xx/timeout -> back-off 5,10,20,...,300 s
budget: hs>1500 or rq>20000 per day -> liveSec=60 + QUOTA_GUARD event
network watchdog: Wi-Fi up but no cloud success 15 min -> drop TLS + re-auth; 60 min -> ESP.restart()
```

A 2-day outage back-fills in about 48 PATCH requests, roughly 2 minutes [INFERRED].

### 5.6 Time (NTP, no RTC)

- `configTzTime("<+06>-6", "pool.ntp.org", "time.google.com")`. Asia/Dhaka has no DST (R6 §3.5 [CONFIRMED-primary]).
- Time is valid when the year is ≥ 2026.
- **HTTPS works before NTP.** mbedTLS date checks are off by default (`CONFIG_MBEDTLS_HAVE_TIME_DATE=n`, V2 §3.7 [CONFIRMED-primary]). Only the data timestamps need NTP.
- Without time, records get `flags.b0 = 1` with `bootId`/`upMs` and are **held in the queue**. On the first sync in the same boot, a time anchor is written and `ts = epoch − (anchorUp − upMs)/1000`.
- If the ESP reboots before syncing, those records are uploaded using an estimate (`last NVS epoch + upMs`) and marked `tq:0`. This needs an ESP reboot *and* no internet together, which should be rare: the ESP RTC keeps time through Wi-Fi outages.
- If the inverter supports `QT` (MAX), its clock is a secondary time source (R2 §5.10 [CONFIRMED-primary]).

### 5.7 Wi-Fi provisioning

- **WiFiManager 2.0.17** captive portal, AP `SavxLogger-XXXX` with a password printed on the serial log and in the docs.
- Custom fields:
  - admin password;
  - Firebase Web API key, DB URL, device e-mail and device password;
  - ntfy topic (≥ 24 random characters);
  - Telegram bot token and chat id (optional);
  - Healthchecks UUID (optional).
- **Portal triggers:** no saved credentials, or **BOOT (GPIO0) held 3 s while running**. The portal never opens automatically because the Wi-Fi is down: during load-shedding the router is often off, and logging must continue.
- Use the portal in non-blocking mode so `invTask`/`aggTask` keep running. Verify `setConfigPortalBlocking(false)` + `process()` against the 2.0.17 API [INFERRED].
- 2.4 GHz only. If the router uses band steering on one SSID and joining fails, create a separate 2.4 GHz SSID. An iPhone hotspot needs "Maximize Compatibility" (R5 §1.6 [CONFIRMED-primary]).

### 5.8 OTA, recovery and safe mode

- **ElegantOTA 4.0.0 (sync)** at `http://solar.local/update` with basic auth (admin). The iPhone can upload the `.bin` from the Files app. **ArduinoOTA** with a password hash is used for development pushes (R6 §8.2 [CONFIRMED-primary]).
- Licence: ElegantOTA Lite is AGPL-3.0. That is fine for a personal device. Revisit if the firmware is ever published.
- **Safe mode:** an NVS `bootFails` counter increments at boot and clears after 120 s of healthy running. At ≥ 3, the device boots in safe mode:
  - no inverter TX and no cloud;
  - Wi-Fi + local UI + `/update` only;
  - LED solid red.

  USB re-flash through the COM port is always the last resort.

### 5.9 Watchdogs

- **TWDT:** 30 s, panic. Every long-running task calls `esp_task_wdt_add(NULL)` and resets in its loop (V2 E9, R6 §8.4 [CONFIRMED-primary API]).
- The network watchdog is described in §5.5.
- `esp_reset_reason()` goes into `status.rr`. A non-POWERON boot raises `LOGGER_REBOOT` (sev 2).
- Keep the default brownout detector. If brownouts appear, cap TX power with `WiFi.setTxPower(WIFI_POWER_15dBm)` (V2 §3.13 [CONFIRMED-primary API]).

### 5.10 Local web UI and JSON API (`http://solar.local`, core `WebServer`)

| Route | Auth | Returns |
|---|---|---|
| `GET /` | – | gzipped single page: Live tiles, 24 h chart (uPlot inlined), Raw, Diagnostics, Settings links |
| `GET /api/live` | – | Latest `Sample` JSON (same keys as `live`) plus `age_ms` |
| `GET /api/ring?min=1440&step=60` | – | Down-sampled PSRAM ring as arrays `{t:[],w:[],pvw:[],bv:[],soc:[]}` |
| `GET /api/minutes?date=YYYY-MM-DD` | – | That day's `MinuteRec`s from LittleFS as JSON arrays |
| `GET /api/status` | – | Link profile, adapter, dialect, supported commands, poll success %, CRC errors, RSSI, heap/PSRAM, queue depth, last cloud OK, reset reason |
| `GET /api/raw` | – | Last raw reply per command (ASCII + hex) |
| `GET /api/diag/pins` | admin | Module RO/DI self-ID result; idle-level test (`%LOW`, edges, per GPIO) |
| `POST /api/diag/idle?secs=10` | admin | Runs the Stage-C idle test (§8.4) |
| `POST /api/probe` | admin | Re-runs the read-only probe |
| `GET/POST /api/config` | admin | NVS settings (secrets write-only, never echoed) |
| `GET /update` | admin (basic) | ElegantOTA |

The HTTPS PWA cannot fetch these `http://` endpoints because of mixed-content rules (R4 §3.6). It only links to them.

### 5.11 Status LED

The onboard RGB LED is on GPIO48 for the v1.0 layout and clones, GPIO38 on Espressif v1.1. On clones it may need an open solder jumper bridged (R5 §1.3 [SECONDARY]). In the no-solder baseline it may stay dark, and `/api/status` is the authoritative indicator.

| Colour / pattern | Meaning |
|---|---|
| Blue, slow blink | Booting / provisioning portal open |
| Magenta blink | Probing the inverter link |
| Yellow | Wi-Fi down; logging locally |
| Short green blink every 5 s | Valid inverter reply |
| Cyan pulse | Back-fill in progress |
| Red blink | Inverter link lost |
| Solid red | Safe mode |

### 5.12 Config storage (NVS via `Preferences`)

| Namespace | Keys |
|---|---|
| `net` | (WiFiManager stores its own credentials); `host` = `solar` |
| `cloud` | `apiKey`, `dbUrl`, `email`, `pass`, `rtok` (refresh token), `uid` |
| `alert` | `ntfyTopic`, `tgToken`, `tgChat`, `hcUuid`, plus a cached copy of the cloud `cfg` thresholds |
| `link` | `mode`, `baud`, `rxInv`, `txInv`, `roPin`, `diPin`, `invTxRj`, `adapter`, `dialect`, `pv12`, `supMask`, `lastProbe` |
| `energy` | `date`, `epv`, `eld`, `ech`, `edis`, `egr` (5-min checkpoint), `lastEpoch` |
| `sys` | `bootId`, `bootFails`, `safe`, `adminHash`, `fwPrev` |

### 5.13 Security

- **Trust anchors in `roots.h`**, with no `setInsecure()` anywhere. Chains were measured by V2 I6 and §3.3 [CONFIRMED-primary]:

  | Host | Roots to embed |
  |---|---|
  | RTDB, identitytoolkit, securetoken | GTS Root R1 + R2 + R3 + R4 |
  | ntfy.sh | ISRG Root X1, X2, Root YR, Root YE (V2 §3.6) |
  | api.telegram.org | Go Daddy Root CA G2 |
  | hc-ping.com | USERTrust ECC |

  Expired roots are not detected while mbedTLS date checks are off, so re-check the roots once a year.
- **Firebase:** a dedicated *device* Auth user plus a separate *owner* user, with the UID-pinned rules in §4.7. The refresh token is persisted in NVS. It dies if the device user's password changes; recover with the BOOT long-press and re-enter credentials (V2 F11 [CONFIRMED-primary]).
- **Never log URLs containing `auth=`.** Secrets are write-only in `/api/config`.
- The local admin password is stored as a SHA-256 hash and protects `/update`, `/api/config`, `/api/probe` and `/api/diag/*`.
- **NVS is not encrypted** (no flash encryption). Physical access exposes the device credentials. Accepted risk: the rules limit the damage to writing this device's data.
- **ntfy:** the topic name is the password ("the topic is essentially a password", V2 I3). Use a 24+ character random topic.
- **Optional hardening:** restrict the Web API key in the Google Cloud console to the Identity Toolkit and Token Service APIs [INFERRED, unverified UI].
- **Licences:** do not copy Solar2MQTT code (CC BY-NC-ND 4.0, V1 V11). Our PI30 code is original (R2 `pi30.h`).

---

## 6. Web app design (PWA)

### 6.1 Stack

- Vite, TypeScript and Preact, with a hand-written hash router (`#/live`, `#/today`, …).
- `firebase@13.0.0` (modular: `app`, `auth`, `database`) and `uplot@1.6.32`. Both are **bundled and self-hosted** so the service worker can precache them (K11).
- Bar charts for daily energy are plain SVG, with no extra library.
- Pin Vite and Preact to their current stable versions in `package-lock.json` at implementation time.

### 6.2 Pages

| Page | Content | Data access (all date-bounded) |
|---|---|---|
| **Live** (default) | Power-flow card: PV → inverter → load, battery ↔, grid →, with W values. Mode badge (Line / Battery / Fault). SOC gauge. Battery V/A/W. Grid V/Hz. Output V/Hz. Load %. Heat-sink °C. Today's kWh tiles. "Updated N s ago". A red **"Logger offline"** banner when `live.ts` is more than 60 s old. Active warnings decoded with `codes.ts`. | `onValue(live)`, `onValue(status)`, `onValue(daily/<today>)` |
| **Today** | uPlot charts with a shared cursor: (1) PV W, Load W, Battery W; (2) SOC % and Battery V; (3) Grid V and Output V. Energy tiles: PV, Load, Grid (est.), Battery in/out, solar fraction, `epvi` vs `epv` reconciliation. | `get(hist/<today>)` once, then `onChildAdded(query(hist/<today>, orderByKey(), startAfter(lastKey)))` |
| **History** | Day picker (`hist/<date>`). Week view from `h15` (7 dates). Month and year views from `daily`. Overlay two days for comparison. | `get` per date node; `query(daily, orderByKey(), startAt(a), endAt(b))` |
| **Energy** | Daily kWh bars (PV / load / grid est.) for a month. Monthly totals for a year, computed client-side. **CSV export** of a daily range or of one day's minutes, as a `Blob` download that iOS saves to Files. | `daily` range queries; `hist/<date>` |
| **Events / Faults** | Open alerts first. Last 7 days of events, with "load more" and severity filter. Decoded text. Ack button (owner writes `ack:true`). Fault-code legend for LCD codes vs QPIWS vs REVO. | `get(events/<date>)` per day |
| **Settings** | `cfg` form with the same validation as the rules. Device info from `meta`/`status` (firmware, dialect, protocol, RSSI, heap, queue, last boot reason, U1/U2 versions). **Send test alert** (`cfg.testAlert = now`) and **Re-probe** (`cfg.reprobe = now`); the device acts within 60 s. Link to `http://solar.local`. Sign out. App version. | `onValue(cfg)`, `set(cfg/...)` |

### 6.3 Charts

- uPlot with the x-axis in Unix seconds and `tzDate` set to `Asia/Dhaka` (R6 §5.4/5.6 [CONFIRMED-primary API]).
- Missing minutes are `null` so gaps stay visible.
- On iPhone, use range buttons (6 h / 12 h / 24 h) instead of pinch-zoom.
- Colours are tokens with light and dark variants.
- uPlot is about 22 KB gzipped and draws 1,440–2,880 points comfortably on a phone (R6 §5.4).

### 6.4 Bandwidth discipline in the client

- Never subscribe to `hist` or `events` roots. Only read `…/<date>` or key ranges.
- Unsubscribe listeners on `visibilitychange` → hidden. Re-subscribe and re-render when visible again, because iOS suspends the WebSocket in the background.
- Cache today's minutes in memory per session. History views cache by date in IndexedDB with a "date < today" rule (immutable days).

### 6.5 PWA and iOS specifics

- Manifest: `name`, `short_name`, `start_url "/"`, `scope "/"`, `display "standalone"`, theme/background colours, 192/512/maskable icons. Also add `apple-touch-icon` 180×180.
- Viewport `viewport-fit=cover` with `env(safe-area-inset-*)` padding. Inputs at 16 px so iOS does not zoom.
- **iOS 26:** sites added to the Home Screen open as web apps by default (V2 C7 [CONFIRMED-primary]). On iOS 16.4–18, Add to Home Screen from Safari's Share menu.
- **Sign in inside the installed app.** A Home Screen app has its own storage, separate from Safari. Call `navigator.storage.persist()`. Auth persistence is the SDK default (IndexedDB) (V2 I2 [CONFIRMED-primary]).
- There is no background execution. Alerts come from ntfy/Telegram, not from the PWA. Web Push is out of scope because it needs a credentialed sender (V2 I1, F8).
- Service worker: precache the hashed `/assets/*` (cache-first). `index.html` is network-first with cache fallback. Never intercept Firebase traffic (`*.firebasedatabase.app`, `googleapis.com`).

### 6.6 Auth

- **Email/password only.** One `owner` user, plus the `device` user that only the ESP32 uses.
- No Google sign-in: iOS standalone redirect/popup flows are fragile (R6 §5.3).
- To add family viewers later, create more users and add their UIDs to the `.read` rule.

### 6.7 Hosting and deployment steps

1. On the Mac: install Node LTS, then `npm i -g firebase-tools` and `firebase login`.
2. In the Firebase console, create project `savx-logger` on **Spark** (no billing). Add a **Web app** and copy its config.
3. Create the **Realtime Database** in **Singapore (asia-southeast1)** in **locked mode**. The URL has the form `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app` (V2 F5).
4. **Authentication → Email/Password: enable.** Add the users `owner@…` and `inv1@…` with a long random password. Copy both UIDs.
5. In the repo:
   - `firebase use --add <project-id>`
   - put the UIDs into `firebase/database.rules.json`
   - `firebase deploy --only database`
6. Test the rules with `curl`:
   - unauthenticated GET returns 401/permission denied;
   - the owner cannot PUT `live`;
   - the device cannot PUT `cfg`.
7. `cd web && npm ci && npm run build`, then `firebase deploy --only hosting`.
8. On the iPhone, open `https://<project-id>.web.app` in Safari → Share → **Add to Home Screen** → open it → sign in as owner.
9. Watch **Realtime Database → Usage** daily for the first week. Expect about 5–15 MB/day download and about 0.4 MB/day storage growth.

---

## 7. Alerts design

### 7.1 Channels

| Channel | Role | Facts | Limits |
|---|---|---|---|
| **ntfy.sh** (free iOS app) | Primary | `POST https://ntfy.sh/<topic>` with headers `Title`, `Priority` (1–5), `Tags`, `Click` (set to the PWA URL). Delivered ntfy.sh → Firebase → APNs. (V2 I3/I4 [CONFIRMED-primary]) | 250 messages/day, burst 60 then 1 per 5 s, 4,096 B per message. Bangladeshi CGNAT may share the per-IP quota (V2 §3.5 [INFERRED]). |
| **Telegram bot** | Secondary, sev ≥ 4 | `POST https://api.telegram.org/bot<token>/sendMessage` with `chat_id` and `text`. ≤ 1 msg/s per chat. (V2 I5 [CONFIRMED-primary]) | May be blocked in Bangladesh (V2 §3.4 [SECONDARY]) |
| **Healthchecks.io** Hobbyist (free, 20 checks) | Dead-man's switch: "logger or internet is down" | `GET https://hc-ping.com/<uuid>` every 5 min; period 5 min, grace 15–30 min. E-mails when pings stop (Telegram integration optional). (V2 §3.3 [CONFIRMED-primary]) | – |
| RTDB `events` | Audit trail in the PWA | Every alert open/clear is written | – |

App availability: ntfy (iOS 15+) and Telegram install only if the Apple ID uses a storefront where they exist, such as US or IN. Bangladesh is not a storefront (V2 I8). The Healthchecks e-mail path needs no app.

### 7.2 Triggers

Thresholds come from the Savx manual and are owner-tunable via `cfg` [CONFIRMED-primary manual values; INFERRED rule design].

| Type | Sev | Trigger (hold) | Clear (hold) | Notes |
|---|---|---|---|---|
| `INVERTER_FAULT` | 5 | QMOD = `F`, or any fault-class QPIWS bit (§3.9), or `fc ≠ 0`, on **2 consecutive polls** | Condition false for 60 s | The message includes the decoded fault (LCD 01–09/51–59 wording where the table is known) |
| `BATT_CRITICAL` | 5 | SOC ≤ 20 %, or `bv` ≤ 21.5 V, or QPIWS a12 (battery low alarm) / a14 (under shutdown), for 30 s | SOC ≥ 25 % and `bv` ≥ 22.5 V for 5 min | Manual: low-DC warning 22.0/21.0 V, cut-off 20.5/20.0 V (24 V) |
| `BATT_LOW` | 4 | SOC ≤ `cfg.socLow` (30) or `bv` ≤ `cfg.bvLow` (22.5) while discharging, for 60 s | SOC ≥ socLow + 5 and `bv` ≥ bvLow + 0.5 for 5 min | Lithium program 29 default cut-off is SOC 15 %; program 12 back-to-utility is 40 % |
| `BATT_HIGH` | 4 | QPIWS a11, or `bv` ≥ `cfg.bvHigh` (30.0) for 30 s | `bv` < bvHigh − 0.5 for 5 min | Manual high-DC cut-off is 33.0 V. Set bvHigh from the battery's spec. |
| `GRID_LOSS` / `GRID_RESTORED` | 4 / 3 | Grid absent for 10 s: QPIWS a5 LINE_FAIL, or `gv` < 90 V (APL range), or < 170 V when QPIRI input range = UPS | Grid present for 30 s. The restore message includes the outage duration. | **Do not use QMOD = B alone.** With SBU priority (program 01) the unit runs on battery while the grid is present. |
| `GRID_ABNORMAL` | 2 | 90 ≤ `gv` < 180 V or `gv` > 270 V for 5 min | Normal for 5 min | Manual: low return 180 V, high return 270±7 V |
| `OVERLOAD` | 4 | QPIWS a16, or `ld` ≥ `cfg.loadHigh` (90 %) for 30 s | `ld` < 80 % for 60 s | Manual: overload trips after 10 s at 110–130 % |
| `OVER_TEMP` | 4 | QPIWS a9 (over-temperature) or a10 (fan locked), or `tmp` ≥ `cfg.tempHigh` (75) when `tmp` is plausible | Clear for 5 min | – |
| `PV_OVERVOLT` | 4 | QPIWS a26, or `pvv` ≥ 480 V for 10 s | `pvv` < 470 V for 5 min | Manual max Voc 500 V |
| `PV_LOW_DAY` | 2 | 10:00–15:00 local, `pvw` < 50 W for 60 min while SOC < 95 % | Next day | Once per day |
| `LINK_LOST` | 3 | No valid inverter reply for 60 s | First valid reply | Points to wiring or the probe |
| `LOGGER_REBOOT` | 2 | Boot reason ≠ POWERON | – | info |
| `QUOTA_GUARD`, `DATA_DROPPED`, `BACKLOG_STUCK` (queue > 6 h old with Wi-Fi up) | 2 | as named | – | Ops |
| `DAILY_SUMMARY` | 1 | At `cfg.summaryHour` (21:00) | – | PV / load / grid kWh. Optional; −1 disables. |
| *Logger offline* | – | Healthchecks missed pings | – | E-mail from healthchecks.io |

### 7.3 Debounce and rate limiting

- **Per-rule state machine:** `IDLE → PENDING` (condition true) `→ ACTIVE` (after hold) `→ CLEARING` (condition false) `→ IDLE` (after clear hold). The `ACTIVE` state notifies `open`; `IDLE` after `CLEARING` notifies `clear` with `dur`.
- **Re-notify** only for sev ≥ 4 while active, every `cfg.notifyRepeatMin` (30 min).
- **Quiet hours** (`quietFrom`–`quietTo`) suppress the push for sev ≤ 3. The event is still logged.
- **Global cap:** 40 pushes/day, well under ntfy's 250. Above the cap, only sev 5 is sent.
- **Offline:** alerts wait in `qAlert` (24 entries). On reconnect they are sent with their original time in the text, e.g. "(14:02, delayed 37 min)". If the queue is full, the oldest sev ≤ 2 entries are dropped first.
- **Routing:** sev 5 → ntfy priority 5 + Telegram; sev 4 → ntfy 4 + Telegram; sev 3 → ntfy 3; sev ≤ 2 → ntfy 2. Everything goes to `events`.

---

## 8. Hardware plan

### 8.1 Parts (all already owned)

| Part | Role |
|---|---|
| ESP32-S3-N16R8 DevKitC-1 clone | Controller |
| HW-0519-type auto-direction RS485↔TTL module (a local shop, ৳117 [CONFIRMED-primary photo]) | RS232-lite receiver: reads inverter TX |
| Spare LAN patch cable, cut | RJ45 plug into COM; conductors for pins 1, 2, 8 |
| 40 M-M jumpers | Cut and stripped for wrap connections; male pins clamp into the module's screw terminals |
| 5 V/2 A USB charger + USB cable | Power, on an inverter-backed socket |
| 100×68×50 mm ABS box, 25 mm foam tape | Enclosure and mounting |
| Paper manual | Pinout reference |

### 8.2 ESP32-S3 pin map

| GPIO | Use | Notes |
|---|---|---|
| 17 | UART1 TX → inverter **RX** pin (inverted) | U1TXD on Espressif's header J1-10 [CONFIRMED-primary]. Connected **only after M2**. |
| 18 | Module header "RXD" (expected **RO**) → UART1 RX (inverted) | J1-11 [CONFIRMED-primary] |
| 16 | Module header "TXD" (expected **DI**) → OUTPUT HIGH | Firmware swaps the roles of 16 and 18 if the self-ID test says the labels are the other way round [INFERRED] |
| 3V3 | Module VCC | Never 5 V (ESP inputs are not 5 V tolerant, R5 §1.3) |
| GND (×2) | Module GND; COM pin 8 via the B− terminal | Common ground is required for the direct TX link |
| 0 | BOOT button: 3 s hold opens the portal | Strapping pin; only read after boot |
| 48 (38) | RGB LED | May be unbridged on the clone |
| **Never use** | 0/3/45/46 (strapping), 19/20 (USB), 26–32 and 35–37 (flash/PSRAM), 43/44 (console) | V1 C18, R5 §1.3 [CONFIRMED-primary] |

### 8.3 Cable preparation (no tools beyond a knife or scissors)

1. Hold the LAN plug with the clip away from you, contacts facing you and the cable hanging down: pin 1 is on the **left**.
2. **Write down the actual colour order through the clear shell.** Do not assume T568A or T568B (R1 §5.4).
3. Cut the cable, leaving about 1–1.5 m on the plug side.
4. Strip only the conductors for **pins 1, 2 and 8**.
5. Cut the other five conductors short, fold each one back and **tape each individually**. **Pin 4 is +VCC (unknown voltage); never connect it** (K6).
6. Label the cable "INVERTER COM – NOT ETHERNET". Never plug it into a router, switch or the BMS jack.

### 8.4 Wiring for each possible port type

**A. COM = RS232 (expected): "RS232-lite" baseline**

```
 COM RJ45                       HW-0519 module                    ESP32-S3
 pin 8 GND ---(LAN cond.)-----> B-  screw ----(jumper, male pin    GND  (2nd GND pin)
                                     clamped in same screw)------>
 pin X = inverter TX ---------> A+  screw
 pin Y = inverter RX ------------------------------------------->  GPIO17 (connect ONLY after step 2 below)
 pins 3,4,5,6,7  X (taped)      接大地 (earth) X  (leave open)
                                R0 120R pad: leave OPEN
                                VCC <---------------------------   3V3
                                GND <---------------------------   GND
                                RXD ---------------------------->  GPIO18
                                TXD ---------------------------->  GPIO16
 X/Y = 1/2: decided by the idle-level test. Prior: X = pin 1 (V1 V3/V4).
```

Procedure (V1 §6.2–6.4, refined):

1. **Bench self-ID of the module** (inverter not connected).
   - Both module header signal pins go to GPIO16/18 set as `INPUT_PULLUP`.
   - With A/B open, RO should read HIGH (fail-safe bias).
   - Then jumper **A+ → ESP GND and B− → ESP 3V3**. The header pin that reads **LOW** is RO.
   - This polarity matches what the driver would output anyway, so it cannot cause contention [INFERRED].
   - From then on, DI is driven OUTPUT HIGH, which keeps the driver disabled.
2. **Idle-level test, receive-only** (no ESP GPIO on any RJ45 pin).
   - B− to COM pin 8, A+ to candidate pin 1. Sample RO every 10 ms for 10 s via `/api/diag/idle`, viewed on the iPhone.
   - **≥ 99 % LOW with about 0 edges:** this pin idles negative, so it is the **inverter TX**.
   - Otherwise move A+ to pin 2 and repeat.
   - Run the test twice to confirm.
   - Why it works:
     - An RS232 TX line idles negative, so A − B ≤ −200 mV and RO reads LOW.
     - An RS232 RX input has 3–7 kΩ to GND, so the module's A-side bias pulls A up and RO reads HIGH.
3. **Connect GPIO17 to the *other* pin** (the inverter RX), then run the probe (§3.5).

Stress note: the A input sits at about −5 to −12 V. That is outside the MAX3485-class −7.5 V abs-max specification [SECONDARY], but the module has TVS diodes and polyfuses, and RS232 drivers are current-limited. The worst case is a dead module (then fallback F-A); the ESP32 is not exposed (V1 §6.3, V2 §3.2). If a resistor becomes available, put ≤ 4.7 kΩ in series with the A lead (V2 §3.2c).

**B. COM = RS232 with a MAX3232 module (optional upgrade, F-A)**

| From | To |
|---|---|
| Inverter TX pin | MAX3232 RS232-RX (R1IN) |
| Inverter RX pin | MAX3232 RS232-TX (T1OUT) |
| Pin 8 | MAX3232 0V |
| MAX3232 VCC | 3V3 |
| MAX3232 TTL-out | GPIO18 |
| MAX3232 TTL-in | GPIO17 |

Set `mode = MAX3232`, `rxInv = txInv = false`. Run the loopback test first (bridge RS232-TX to RS232-RX; one TTL orientation must echo) (R5 §3.3).

**C. COM turns out to be TTL-level (fallback F1, unlikely)**

Symptom: neither pin idles negative in step 2.
1. Diagnostic: A+ → candidate, **B− → ESP 3V3**.
   - A 5 V TTL TX idles above 3.3 V, so RO reads HIGH, and drops to LOW on data.
   - An RX input pulled to 0 V reads LOW.
2. If the logic level cannot be established, **stop**. Do not connect any ESP GPIO to an unknown-voltage pin: ESP abs-max is 3.6 V. Escalate to F-A (a meter or MAX3232) or F-B [INFERRED].

**D. COM behaves as RS485 (contradicts the manual; last resort, V1 F4)**

- Move GPIO17 off the RJ45.
- Wire A+ → pin 2 and B− → pin 1 (the BMS-style convention), GND → pin 8.
- Firmware assigns UART TX to the module DI pin and RX to RO, non-inverted. Swap A/B once if needed.

**E. BMS jack:** **never touch or unplug it.** The inverter is master there; disconnecting it can raise battery-communication warnings and change lithium charging behaviour (V1 §6.0 [INFERRED]).

**F. USB / WiFi jack:** none exists on this unit (K4). The "WiFi collector" would use COM (pin 4 +VCC) and would compete with the ESP32 as master.

### 8.5 Joining without solder (baseline) and the solder upgrade

- **Wrap joint:** strip 8–10 mm, wrap 3–4 tight turns around the header pin close to the PCB, add a tape flag over it, and do a tug test. LAN conductors and jumper ends go into module screw terminals directly. A jumper's male pin clamps well.
- **Strain relief:** tie a loose knot in the LAN cable inside the box and tape the bundle to the box floor, so no pull reaches the wraps or the inverter's RJ45 jack.
- **Upgrade (if a soldering iron is borrowed):** solder the wraps on the header pins, solder or heat-shrink the LAN conductors, and keep the screw terminals.

### 8.6 Power

- Plug the charger into a socket fed by the inverter's **AC OUT**. Photo 6 shows a universal multi-socket at the installation; confirm it stays live when the grid breaker is off.
- **Put the router/ONU on AC OUT as well**, otherwise uploads stop during outages even though logging continues.
- The universal socket must grip the US plug firmly.
- Use a short, good USB cable. The module needs ≥ 0.5 A, and Wi-Fi TX peaks at 355 mA (V2 C6 [CONFIRMED-primary]). The 2 A charger has ample margin.
- Do **not** power the logger from COM pin 4 or from the battery: there is no buck converter and pin 4's voltage is unknown.
- **Debugging with a laptop:** keep the laptop **on battery** while the ESP is wired to the inverter, to avoid a ground loop through the laptop's mains earth (R2 §3.7 [SECONDARY]).

### 8.7 Enclosure layout (inside about 92 × 60 × 45 mm)

```
              wall B (top when mounted) - antenna end, keep >=15 mm clear
 +-----------------------------------------------------------------+
 |                         ~24 mm free  ^                           |
 |   +-------------------------------+  |      +-----------------+  |
 |   | ESP32-S3 DevKitC-1  62.7x25.4 |         | HW-0519  ~44x15 |  |
 |   | [WROOM antenna end ^ up]      |         | screw terms     |  |
 |   |                               |         | facing wall A   |  |
 |   | USB-C "COM" port v            |         +-----------------+  |
 |   +-------------------------------+   (3-4 layers foam tape      |
 |      ~5 mm                             under ESP: pins point down)|
 +--[hole ~13x9 mm: USB-C plug]---------[notch: LAN cable + knot]---+
              wall A (bottom when mounted) - cable entries, drain hole 2-3 mm
```

- The ESP board is 62.74 × 25.40 mm (Espressif DXF, R5 §6.1 [CONFIRMED-primary]).
- A USB-C plug inserted from inside does not fit: 62.7 + 20 + 15 mm > 92 mm. So **cut a ~13×9 mm hole in wall A in line with the "COM" USB-C port** and insert the plug from outside. Measure first [INFERRED].
- Cut holes with a hobby knife or a heated screwdriver. Seal with tape (indoor).
- The ESP's header pins point down, so stack 3–4 layers of foam tape under the module area. The HW-0519 sits flat on one layer.
- **Placement:**
  - Mount on the wall **beside** the inverter, ≥ 15 cm from its steel case, antenna end up.
  - Not in line with the bottom fans, not above the inverter, and out of direct sun (the clear lid acts as a greenhouse).
  - The N16R8 is rated for **65 °C ambient** (V2 E2 [CONFIRMED-primary]).
  - Log RSSI. Below −75 dBm, move the box or the router.

### 8.8 Missing parts

**Required: none.** Optional nice-to-haves, if a shop visit ever happens (never required):

| Priority | Item | Price | Why |
|---|---|---|---|
| 1 | Mini MAX3232 module | ৳80 [SECONDARY, a local shop] | Clean RS232 link (F-A) |
| 2 | 1 kΩ and 4.7 kΩ resistors | – | Series protection: ESP TX → inverter RX (1 kΩ); A lead (≤ 4.7 kΩ) |
| 3 | F-F Dupont jumpers | ৳75 [SECONDARY] | Replace wrap joints |
| 4 | Electrical tape or heat-shrink, cable ties | – | Insulation and strain relief |
| 5 | Soldering iron + solder | – | Permanent joints |
| 6 | Multimeter | ৳320 (DT830D) [SECONDARY] | Removes all pin ambiguity |

---

## 9. Milestones

Effort estimates are rough evenings [INFERRED].

| M | Name | Tasks | Deliverables | Acceptance criteria / tests |
|---|---|---|---|---|
| **M0** | Site survey and accounts (no wiring), 1 evening | (1) Photograph the bottom panel and both jacks. Confirm COM is free and BMS is connected. (2) Cycle the LCD with UP/DOWN and record every readout, including **U1/U2 versions** (manual §4.5). (3) Record programs 01, 02, 03, 05, 11, 12, 13, 16, 29. (4) Scan iPhone Wi-Fi next to the inverter for any collector SSID. (5) Identify the AC-OUT-backed sockets: switch the grid breaker off briefly and see which stay live. Check that the router/ONU is on one. (6) Measure the case: expect 344×255×105 mm. (7) Check the Apple ID storefront and install ntfy (and Telegram). (8) Create the Firebase project (§6.7 steps 2–4) and a Healthchecks account. (9) Optional: phone IPS Bazar about the collector and the protocol document. | `docs/site-survey.md` filled; photos | Every field filled; COM/BMS confirmed; at least one AC-OUT socket identified; ntfy test message received from `curl -d test ntfy.sh/<topic>` on the Mac |
| **M1** | Bench bring-up (ESP only), 1–2 evenings | Toolchain (§5.1). Blink and Wi-Fi join. PSRAM check. NTP. **UART loopback:** jumper GPIO17↔GPIO18, `Serial1.begin(2400,SERIAL_8N1,18,17,true)`, send the QPI frame, read it back; repeat non-inverted. Native unit tests of every CRC vector in V1 §4.4 and the whitelist refusal test. Module self-ID (§8.4 A step 1). | fw `v0.1-bench`; test log | `ESP.getPsramSize()` ≈ 8 MB. Loopback byte-exact in both modes. 100 % of the CRC vectors pass (e.g. `QPIGS` → `B7 A9 0D`, `QGMN` → `49 29 0D`). `POP02` → refused with 0 bytes sent. Module RO pin identified. |
| **M2** | Cable and idle-level test, 1 evening | Prepare the cable (§8.3). Install on COM pin 8 + A+ only. Run `/api/diag/idle` on pin 1, then pin 2. | Pin map recorded in NVS and `docs/wiring.md` with photos | Exactly one candidate shows ≥ 99 % LOW with < 3 edges in 10 s, repeated twice; the other shows HIGH. If neither, follow F1 (§8.4 C) and stop. |
| **M3** | Protocol probe and dialect, 1–2 evenings | Connect GPIO17 to the inverter-RX pin. Run the full probe (§3.5–3.6). Capture 200 cycles of every supported command to `/log/raw-*.txt` and copy them to `firmware/test/fixtures/`. Compare with the LCD. | `docs/probe-report.json`; raw fixtures; parser unit tests on the real frames | QPI is valid (`(PI30` or other). QPIGS CRC-valid in ≥ 99 % of 200 polls. Output V within ±2 V and battery V within ±0.3 V of the LCD. QPIRI fields 5/7 = `3200`/`24.0`. QVFW/QVFW2 = LCD U1/U2. Dialect and `pv12` decided. Unsupported commands listed. |
| **M4** | Firmware core, local-only logger, 3–5 evenings | Adapter framework, `Sample`, scheduler, aggregator and energy integration, MinuteQueue, NVS settings, WiFiManager, local UI/API, mDNS, ElegantOTA, watchdog, safe mode, LED, alert engine (logging only). | fw `v0.5` | 24 h on the inverter with ≥ 99 % valid fast polls. No unplanned resets. `http://solar.local` shows live data and a 24 h chart on the iPhone. Energy unit test passes. Power-cycling keeps today's totals within ±1 %. OTA from iPhone Safari works. 3 forced crash boots → safe mode. |
| **M5** | Cloud uplink, 2–3 evenings | Rules deploy and curl tests. FirebaseRest (sign-in, refresh, keep-alive). live / minute PATCH / h15 / daily / status / meta / diag. cfg poll. Prune. Back-fill. Budgets and back-off. | fw `v0.8`; `firebase/` committed | `live.ts` advances every 10 ± 2 s. After 48 h, RTDB Usage shows < 15 MB/day download. Router unplugged 2 h → 120 minute records back-filled with no gaps or duplicates. All 3 negative rule tests are denied. Token refresh survives > 3 h. |
| **M6** | Web PWA, 3–4 evenings | Vite project, six pages, `codes.ts`, service worker, manifest/icons, deploy. | `web/`; live URL | Installs to the Home Screen and opens standalone. Sign-in persists across restarts. Live updates within 15 s. "Logger offline" appears within 70 s of unplugging the ESP. A 1,440-point Today chart renders in < 1 s. CSV export opens in Files. Dark and light modes legible. Bandwidth rules (§6.4) code-reviewed. |
| **M7** | Alerts, 1–2 evenings | Notify module (ntfy, Telegram, Healthchecks), rule engine with debounce, Settings test alert. | fw `v0.9` | The test alert arrives in < 30 s. **Grid-breaker test** (inverter on battery for 2 min) → `GRID_LOSS`, then `GRID_RESTORED` with the duration, no duplicates. ESP unplugged → Healthchecks e-mail within period + grace. Quiet hours suppress sev ≤ 3. |
| **M8** | Enclosure, install, 7-day soak | Box holes, foam mounting, strain relief, placement. Soak 7 days. | Install photos; soak report | ≥ 99.5 % of minutes present in the cloud after back-fill. Max box temperature (logged via `temperatureRead()`) < 55 °C. RSSI > −75 dBm. Link errors < 1 %. Zero false sev-5 alerts. |
| **M9** | Polish and handover | `docs/README` (operation, recovery, re-provisioning, yearly export), maintenance checklist (roots, Google account sign-in, export), optional solder/MAX3232 upgrade guide, energy reconciliation (`epv` vs `epvi`). | v1.0 tagged | The user re-provisions Wi-Fi via BOOT long-press unaided. Export script produces dated JSON. Docs reviewed. |

---

## 10. Risk register and open questions

### 10.1 Risks

Likelihood (L) and impact (I) are rated H/M/L.

| # | Risk | L | I | Mitigation |
|---|---|---|---|---|
| R1 | ESP GPIO connected to the RS232 **TX** pin (negative voltage) by mistake | M | H | Idle-level test first (M2). GPIO17 is connected only afterwards. Checklist plus photo before power-on. |
| R2 | COM pin 4 (+VCC) touches anything | L | H | Only pins 1/2/8 are stripped. The rest are cut, folded and taped individually. |
| R3 | HW-0519 overstressed by RS232 negative levels | L–M | M | TVS/polyfuse on the module; ≤ 4.7 kΩ series resistor if available; F-A MAX3232 |
| R4 | RJ45 pin perspective wrong (pin 2 is actually TX) | M | L | The test finds it. No assumption is wired in. |
| R5 | COM is not true RS232 (TTL) | L | M | §8.4 C; stop rather than risk the ESP |
| R6 | Protocol is not PI30, or the dialect is unknown | L / M | M | Probe tree, raw capture, adapters, daily re-probe |
| R7 | Field semantics misread (REVO field 12, SOC source) | M | M | §3.6 rule 5, LCD cross-check, `diag/raw` kept for remote analysis |
| R8 | A setter command sent by accident | very L | H | Compile flag, link-layer whitelist, unit test |
| R9 | BMS cable disturbed | L | H | Documented "never touch". All work happens on COM only. |
| R10 | Electric shock (PV up to 500 VDC, 230 VAC, 145 A DC) | L | very H | Never open covers. The RJ45 is on the exposed bottom face (V1 C7). |
| R11 | Ground loop through a laptop burns the COM board | L–M | H | Laptop on battery; ESP on the isolated charger |
| R12 | Wrapped (no-solder) joints loosen, causing an intermittent link | M–H | M | Tape, strain relief, `LINK_LOST` alert, CRC error counters, solder upgrade |
| R13 | Router down in load-shedding | H | L | LittleFS queue (~40 days) and back-fill; router on AC OUT |
| R14 | Heat above 65 °C ambient | L–M | M | Placement rules, temperature logging, watchdog |
| R15 | Brownouts or reboots | M | L | Good cable, reset-reason logging, TX power cap |
| R16 | Spark quota exceeded → shut off for the month | L | M | Aggregates only, budgets, back-off, console discipline (§4.6) |
| R17 | Firebase/Google policy change (precedent: Storage moved to Blaze on 2026-02-03) | M (years) | M | Portable JSON model, yearly export, F-C ThingSpeak / Cloudflare D1 |
| R18 | Google account deleted after 2 years of inactivity | L | H | Sign in to the Google account occasionally; yearly export (V2 §3.10) |
| R19 | Telegram blocked in Bangladesh | M | L | ntfy primary; Healthchecks e-mail |
| R20 | ntfy / Telegram not installable (storefront) | M | M | Check storefront in M0; e-mail path; the PWA needs no store |
| R21 | Device credential leak via physical access to NVS | L | L | UID-pinned rules limit damage; rotate the device password |
| R22 | A WiFi collector added later fights the ESP as master | L | M | One master only. Remove the ESP or switch to listen-only. |
| R23 | Clone RGB LED not wired | M | L | Web status is authoritative |
| R24 | Licensing (ElegantOTA AGPL; Solar2MQTT NC-ND) if the code is published | L | L | No S2M code copied; AGPL compliance if published |

### 10.2 Open questions for the user

1. Which sockets are on the inverter's AC OUT? Is the Wi-Fi router/ONU powered from AC OUT?
2. LCD readings: U1/U2 firmware versions, and settings 01 (output priority), 03 (APL/UPS), 05 (battery type: LIb?), 12/13/29 (SOC or voltage thresholds), 16 (charger priority).
3. Does the battery report SOC to the inverter? Is the LCD's "Li" battery-communication icon lit?
4. Which App Store country is your Apple ID set to (for the ntfy/Telegram apps)? Do you want Telegram at all?
5. Does your router have a separate 2.4 GHz SSID? How far is it from the inverter?
6. Is a Mac or laptop available for flashing, and can it run on battery during the first inverter tests?
7. Can you borrow a soldering iron? Could a shop visit for an ৳80 MAX3232 module ever happen?
8. Should family members view the dashboard (extra Firebase users)?
9. Is the inverter in a hot spot or in sun? Which way do the bottom fans blow?
10. Did any WiFi collector or cable come in the inverter box?

---

## 11. Sources

**User / first-hand (primary)**
- Photos `images/1.jpg`, `2.jpg`, `3.jpg`, `4.webp`, `5.png`, `6.jpg`; `scratchpad/ports_label_zoom.jpg`
- Savx F4 paper manual scan, `the paper manual (PDF scan)` (via `research/manual-extract.md`)
- `research/user-constraints.md`

**Research and verification reports** (all in `scratchpad/research/`)
- `r1-oem-identification.md`
- `r2-voltronic-pi30.md`
- `r3-modbus-families.md`
- `r4-existing-apps.md`
- `r5-hardware.md`
- `r6-cloud-web.md`
- `v1-protocol-verification.md`
- `v2-platform-verification.md`
- `r2-code/pi30.h`, `r2-code/sketch_core.h`

**Protocol**
- PI30 (2014): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf
- PI30 (2015 Customer): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Communication-Protocol-20150924-Customer.pdf
- PI30MAX: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30MAX.Communication.Protocol20210217.pdf
- Sorotec REVO: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf
- mpp-solar `pi30revo.py` and `protocol_helpers.py`: https://github.com/jblance/mpp-solar
- ESPHome pipsolar: https://esphome.io/components/pipsolar/
- syssi esphome-pipsolar: https://github.com/syssi/esphome-pipsolar
- Solar2MQTT (reference only): https://github.com/softwarecrash/Solar2MQTT
- SMG V1.0.1 map: https://github.com/syssi/esphome-smg-ii
- PowMr maps: https://github.com/odya/esphome-powmr-hybrid-inverter and https://github.com/leodesigner/powmr_comm
- TI MAX3232: https://www.ti.com/lit/ds/symlink/max3232.pdf

**OEM**
- Sorotec REVO VM II PRO: https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/
- Savx F3 datasheet: https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf
- Solar of Things (App Store): https://apps.apple.com/us/app/solar-of-things/id6445806075
- IPS Bazar F4 listing: https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/

**ESP32 / toolchain**
- ESP32-S3-WROOM-1 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- DevKitC-1 v1.1 user guide: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
- arduino-esp32 3.3.12 (`HardwareSerial.h`, `boards.txt`, `tools/partitions`): https://github.com/espressif/arduino-esp32
- Lib-builder defconfigs: https://github.com/espressif/esp32-arduino-lib-builder/tree/master/configs
- pioarduino: https://github.com/pioarduino/platform-espressif32 (board `esp32-s3-devkitc1-n16r8.json`, release 55.03.312-1)
- ESP-IDF watchdogs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/wdts.html
- ESP-IDF mbedTLS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/mbedtls.html
- ElegantOTA: https://github.com/ayushsharma82/ElegantOTA
- WiFiManager: https://github.com/tzapu/WiFiManager
- ArduinoJson: https://github.com/bblanchon/ArduinoJson
- FirebaseClient (fallback): https://github.com/mobizt/FirebaseClient
- HW-0519-class module: https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/ [SECONDARY]

**Firebase**
- Pricing: https://firebase.google.com/pricing
- FAQ (over-quota behaviour): https://firebase.google.com/support/faq
- RTDB limits: https://firebase.google.com/docs/database/usage/limits
- RTDB billing: https://firebase.google.com/docs/database/usage/billing
- RTDB locations: https://firebase.google.com/docs/database/locations
- RTDB REST save data: https://firebase.google.com/docs/database/rest/save-data
- RTDB REST auth: https://firebase.google.com/docs/database/rest/auth
- RTDB rules core syntax: https://firebase.google.com/docs/database/security/core-syntax
- Auth REST: https://firebase.google.com/docs/reference/rest/auth
- Auth sessions: https://firebase.google.com/docs/auth/admin/manage-sessions
- Auth limits: https://firebase.google.com/docs/auth/limits
- Hosting quotas: https://firebase.google.com/docs/hosting/usage-quotas-pricing
- Storage changes FAQ: https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024
- JS SDK on npm: https://registry.npmjs.org/firebase
- uPlot: https://github.com/leeoniya/uPlot

**iOS / WebKit**
- Web Push on iOS: https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/
- Storage policy: https://webkit.org/blog/14403/updates-to-storage-policy/
- ITP / 7-day cap: https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/
- WWDC25 / Safari 26: https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/
- App Store availability: https://support.apple.com/en-us/118205

**Alerts**
- ntfy publish: https://docs.ntfy.sh/publish/
- ntfy config: https://docs.ntfy.sh/config/
- Telegram Bot API: https://core.telegram.org/bots/api
- Telegram Bot FAQ: https://core.telegram.org/bots/faq
- Healthchecks pricing: https://healthchecks.io/pricing/
- Healthchecks HTTP API: https://healthchecks.io/docs/http_api/
- Let's Encrypt certificates: https://letsencrypt.org/certificates/
- Telegram restrictions in Bangladesh: https://en.prothomalo.com/bangladesh/government/igr6isfgo4 [SECONDARY]

**Alternatives**
- ThingSpeak licence FAQ: https://thingspeak.mathworks.com/pages/license_faq
- Cloudflare D1 limits: https://developers.cloudflare.com/d1/platform/limits/
- Cloudflare Workers limits: https://developers.cloudflare.com/workers/platform/limits/
- Supabase project pausing: https://supabase.com/docs/guides/platform/free-project-pausing
- Google inactive-account policy: https://support.google.com/accounts/answer/12418290

**Shop prices [SECONDARY]**
- Mini MAX3232 (a local shop): (local shop listing)
- F-F jumpers (a local shop): (local shop listing)
- DT830D multimeter (a local shop): (local shop listing)
