# 01 — Inverter profile: Savx F4 3KW (24 V)

This document describes the inverter the logger connects to: what it is, which ports it has, what its own paper manual says, and which facts the logger depends on. The protocol itself is in [02-protocol-reference.md](02-protocol-reference.md); wiring is in [03-hardware-and-wiring.md](03-hardware-and-wiring.md).

**Confidence tags** are used only where uncertainty matters:

| Tag | Meaning |
|---|---|
| **[confirmed]** | Read on the unit itself (photo, nameplate, LCD), in the unit's own paper manual, in an official document, or in source code. |
| **[secondary]** | Seller listing, forum post, README narrative. |
| **[inferred]** | Reasoning from the facts above. |
| **[unknown]** | Not known yet. The text says how it gets determined on site. |

Deep evidence lives in the research files; this page summarises them:
[r1 OEM identification](../research/r1-oem-identification.md) ·
[v1 protocol verification](../research/v1-protocol-verification.md) ·
[manual extract](../research/manual-extract.md) ·
[r4 existing apps](../research/r4-existing-apps.md) ·
[d1 architecture](../research/d1-architecture.md).

---

## 1. Identity at a glance

| Item | Value | Tag |
|---|---|---|
| Brand | **Savx**, the house brand of **IPS Bazar** (Bangladesh). Nameplate footer: "BRANDS OF IPS BAZAR". | [confirmed] nameplate |
| Model on nameplate | **SAVX F4 SERIES (2025)**, colour Red | [confirmed] |
| Marketing name | "Savx Solar Inverter F4 3KW-24V (MPPT BASE)"; front cover silkscreen "Savx SOLAR INVERTER", "MPPT BASE", "F4 / 3KW" | [confirmed] photos, listing |
| Rating | 3200 VA / 3200 W, 24 V battery, 230 V single phase | [confirmed] nameplate |
| Serial barcode | `250514-0315130023` (prefix looks like a YYMMDD date: 2025-05-14) | [confirmed] / date reading [inferred] |
| Second sticker | `CFAF09809` (meaning unknown; maybe a board or factory serial) | [confirmed] / meaning [unknown] |
| Paper manual | "USER MANUAL — 1.6KW/3.2KW SOLAR INVERTER/CHARGER", Version 3.0, back-cover part number **614.C0556-02**, no brand printed | [confirmed] |
| Probable platform | **Sorotec REVO VM II PRO 3.2KW/24V**, 2025 round-dial generation (or the factory that builds it for Sorotec) | **medium-high**, see §3 |
| Firmware lineage | Voltronic (Axpert)-derived | [inferred, high] |
| Monitoring protocol | Voltronic **PI30** ASCII family, 2400 baud 8N1, on the RS232 "COM" jack | [inferred, high] from manual §3.8 (WatchPower) |
| PI30 dialect | Stock PI30 vs Sorotec "PI30REVO" (adds `QALL`) | **[unknown] ~50/50** — the firmware probe decides in seconds ([02 §9](02-protocol-reference.md#9-probe-decision-tree)) |
| Communication jacks | **COM** (RS232, free) and **BMS** (RS485 + CAN, used by the lithium battery) | [confirmed] photo + manual |
| USB / WiFi / dry contact | **None** on this unit | [confirmed] photo |

### 1.1 Photos

| Photo | What it shows |
|---|---|
|  | Nameplate (transcribed in §2) |
|  | Red case, round black dial with orange ring, blue LCD, three LEDs, four touch keys |
|  | LCD close-up: "INPUT PV 1 W", "OUTPUT 231 V", BYPASS path lit, utility-charger icon, battery "CHARGING" with the **"Li"** tag lit, AC/INV LED solid green (= line mode). The lit "Li" tag suggests program 05 is set to LIb — **confirm during the site survey** [inferred]. |
|  | Bottom panel: two earth screws, AC IN L/N, AC OUT L/N, **COM** (empty) and **BMS** (grey battery cable) RJ45 jacks, two fans, BATTERY +/−, PV IN +/−, "ON – O OFF" rocker switch |
|  | Chassis silkscreen for the two RJ45 jacks (see §4.2) |

---

## 2. Nameplate transcription [confirmed]

```
SOLAR INVERTER
Model Name : SAVX F4 SERIES (2025)
Color : Red
Operating Temperature Range : -10°C - 50°C
[barcode] 250514-0315130023

Inverter Mode
  Rated Power : 3200VA/3200W
  DC Input    : 24VDC , 145A
  Power Factor: 1.0
  AC Output   : 230VAC , 50Hz/60Hz , 13.9A , 1Φ
AC Charge Mode :
  AC Input    : 230VAC, 50Hz/60Hz, 23.3A , 1Φ
  DC Output   : 27VDC
                Max.100A, Default 30A
  AC Output   : 230VAC , 50Hz/60Hz , 13.9A , 1Φ
Solar Charge Mode:
  Max. Power  : 3500W
  Nominal PV Voltage: 260VDC
  Operating Voltage Range: 30-450VDC
  Max.Solar Voltage(VOC): 500VDC
MADE IN CHINA   BRANDS OF IPS BAZAR   CE   [QR code]
[second barcode] CFAF09809
```

Sanity checks [inferred]: 3200 W / 230 V = 13.9 A (matches). 3200 W / 24 V / ~0.92 efficiency ≈ 145 A (matches). "Max.100A" is the **total** charge current (solar + AC); the manual caps AC charging alone at 80 A (§6.3). "Default 30A" matches program 11's default.

---

## 3. Who makes it: OEM identification

### 3.1 Seller side (IPS Bazar)

- Savx is IPS Bazar's house brand; the store's product API lists every Savx product. Hotline printed on IPS Bazar product images: **01749 426 789**. [confirmed] ([r1 §2](../research/r1-oem-identification.md))
- **F4 3000-24V listing** — https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/ [confirmed for what the seller *claims*]:

  | Listing says | Reality on this unit |
  |---|---|
  | Page title "Savx Solar Inverter **F3** 3000-24V (MPPT BASE)"; body "Savx \| F4 Series"; images show the red round-dial F4 | The listing contradicts itself (v2 C8) |
  | 3000 VA / 3000 W, surge 6000 VA | Nameplate: 3200 VA / 3200 W |
  | "Max PV Power: 3000W", max solar charge 80 A, max AC charge 60 A | Nameplate/manual: 3500 W PV, 100 A total, 80 A AC |
  | "MPPT Range: 30–450VDC", "Max PV Open Circuit Voltage: 500VDC" | Correct — these are the F4 values (the F3 is 60–450 V) (v1 C17) |
  | 348 × 270 × 95 mm, 5.6 kg | Manual: 344 × 255 × 105 mm, 5.5 kg net |
  | "USB / RS232 \| Optional: CAN / RS485" | **No USB port.** COM = RS232, BMS = RS485/CAN (photo) |
  | Price ৳35,000 (regular ৳50,000) | — |
  | Warranty: "2-Year Full Motherboard Warranty (Motherboard + Display + Control Card) \| 5-Year Service Warranty \| Lightning Protection Warranty (with proper earthing & safety tools)" | — |

  Treat listing specs as hints only; the nameplate and the paper manual win.
- **No WiFi module, monitoring app or dongle** is sold or mentioned anywhere in the IPS Bazar store (all ~220 products enumerated) [confirmed, by absence]. The store's media library holds no inverter manual.
- **Key supply-chain clue:** IPS Bazar's own Savx F3 datasheet (https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf) contains Sorotec's product-line text verbatim: "**REVO VM II PRO series** is suitable for off-grid applications" and "Standard:USB/RS232；Optional：CAN&RS485". "REVO" is Sorotec's line name. [confirmed]

### 3.2 Evidence for "Sorotec REVO VM II PRO 3.2KW/24V (2025 round-dial)"

Sorotec product page: https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/ (spec tables are images).

| # | Evidence | Strength |
|---|---|---|
| F1 | Spec match on many independent numbers with Sorotec's 3200 VA column: 3200 VA/W, 24 V, Voc 500 V, PV 3500 W, MPPT 30–450 V, PV input 15 A, 100 A max charge, 80 A AC charge, 2× surge, float 27 V, high DC cut-off 33 V, 93 % peak efficiency, −10…50 °C. Sorotec's 1.6 kW net weight (5 kg) also matches the manual. | Strong |
| F2 | Identical front (round black dial, orange ring, LEDs "AC/INV · CHG · FAULT", ESC/UP/DOWN/ENTER) and identical bottom layout (AC block left, two RJ45 side by side, two fans, battery block centre, PV block right, rocker top right). | Strong for the chassis |
| F3 | IPS Bazar's Savx F3 datasheet is a Sorotec "REVO VM II PRO" datasheet (§3.1). | Strong supply-chain link |
| F4 | The WatchPower screenshot in the Savx manual names the monitored device "COM1?_**Sorotec**" (blurry crop). | Moderate |
| F5 | The "614.Cxxxx" manual-number family also appears on a BestEn 3.5/5.5 kW manual with the same RS232 labelling, the same WatchPower section and a "RevoMonitor" Bluetooth app. | Moderate |
| F6 | Sorotec datasheet: "Standard: RS232, USB; CAN&RS485; Optional: WiFi, Bluetooth". The Savx has RS232 + RS485/CAN. | Weak–moderate |

### 3.3 What does not fit (still unproven)

| # | Discrepancy | Effect |
|---|---|---|
| A1 | Sorotec lists 335 × 225 × 100 mm, 6.1 kg; the Savx manual says **344 × 255 × 105 mm, 5.5 kg**. | Weakens "same SKU", not "same platform". If you measure the case, compare it with the manual's 344 × 255 × 105, not with Sorotec's table. |
| A2 | Sorotec WiFi variants report to **SmartESS/DessMonitor** (Eybond); the Savx manual names **"SOLAR OF THINGS"** (Shanghai Siseli New Energy). | Different dongle ecosystem; reason unknown. |
| A3 | Sorotec and the listings say "USB"; this unit has **no USB jack**. | Listings are generic; trust the photo. |
| A4 | No public protocol document, firmware dump or byte trace exists for the **2025 round-dial** generation. Every REVO protocol report is for older REVO II / VE II / VM II units. | The REVO dialect is **unproven** for this unit. |
| A5 | Rocker reads "ON – O OFF" on the unit vs "I O II" in Sorotec's render. | Cosmetic. |

Alternatives checked and ruled out by spec mismatch: Voltronic Axpert VM II Premium 3K-24V (450 V Voc), MUST PV1800 PRO, PowMr HVM3.2H-24V, Crown Xavier 3KW, SAKO Sunon Pro 3.5 kW, Hinvert HMK-plus ([r1 §3.2](../research/r1-oem-identification.md), [v1 §2.3](../research/v1-protocol-verification.md)).

### 3.4 What is actually known, ranked

| Rank | Statement | Confidence |
|---|---|---|
| 1 | The free port is **RS232 "COM"**; **BMS** is RS485 + CAN for the battery. | Certain (photo + manual) |
| 2 | Firmware is Voltronic-derived; PC software is WatchPower → **PI30 family at 2400 8N1**. | High |
| 3 | Platform is the **Sorotec REVO VM II PRO 1.6/3.2 kW round-dial** design, rebranded by IPS Bazar. | Medium-high |
| 4 | Firmware speaks Sorotec's **PI30REVO** dialect (`QALL`, QPIGS field 12 = PV watts). | Unknown, ~50/50 |
| 5 | The exact contract factory. | Unknown |

**Why the remaining uncertainty barely matters:** every candidate in ranks 2–4 speaks the same CRC-framed ASCII at 2400 8N1 on the same RJ45. The firmware's probe classifies the dialect in under 10 seconds ([02 §8–9](02-protocol-reference.md#8-dialect-classification)). If you want certainty anyway, you can ask IPS Bazar (01749 426 789) or Sorotec, quoting "1.6KW/3.2KW, manual 614.C0556-02" — optional.

---

## 4. What the paper manual says

The manual is generic (no brand) and covers the 1.6 kW/12 V and 3.2 kW/24 V models; **this unit is the "3.2K24V" column everywhere**. Scan: `reference/Savx-F4-user-manual.pdf` (each PDF page is a two-page spread). The printed page numbers do not match the table of contents.

| Manual section | Printed pages | PDF page |
|---|---|---|
| Cover, contents | – | 1–2 |
| §1–§2.2 Safety, features, system diagram | 1–2 | 3 |
| §2.3 Product overview + **communication port definition**; §3.1–3.3 | 3–4 | 4 |
| §3.4–3.6 Battery, AC, PV connection | 5–6 | 5 |
| §3.7 Final assembly | 7–8 | **not in the scan** |
| §3.8 **RS232 / WatchPower**, §3.9 **WiFi**, §4.1–4.2 panel | 9–10 | 6 |
| §4.3 LCD icons | 11–12 | 7 |
| §4.4 **Setting programs** | 13–20 | 8–11 |
| §4.5 Display pages | 21–23 | 12–13 |
| §4.6 Operating modes, §4.7 Equalization | 24–26 | 13–14 |
| §5 **Fault codes**, §6 **Warning codes** | 27–28 | 15 |
| §7 Troubleshooting | 29–30 | 16 |
| §8–§12 **Specifications** | 31–34 | 17–18 |
| §13 Dimension drawing; back cover "614.C0556-02" | 35 | 19–20 |

### 4.1 Features list (§2.1)

Off-grid pure sine wave inverter; output power factor 1.0; configurable AC/solar charger priority via the LCD; "smart battery charger"; compatible with mains or generator; overload / over-temperature / short-circuit / battery-low protection; **"External WIFI devices (option)"**; cold start; auto restart while AC is recovering.

### 4.2 Ports (§2.3) and the chassis silkscreen [confirmed]

Bottom-panel items in the manual: 1 AC input terminal · 2 AC output terminal · 3 Battery input · 4 PV input terminal · **5 COMM communication port** · **6 BMS communication port (Option)** · 7 Power on/off switch · 8 Dust cover.

Communication port definition (manual table):

| Jack | Interface | Pin assignment (manual) |
|---|---|---|
| **COMM** | RS232 | **1: RXD, 2: TXD, 4: +VCC, 8: GND** |
| **BMS** | RS485, CAN | 1: 485-B, 2: 485-A, 4: CAN-H, 5: CAN-L |

The silkscreen on this unit's bottom panel shows the same thing (pin 4 "+VCC" is not drawn on the chassis):



| Jack (left → right) | Silkscreen | State on this unit |
|---|---|---|
| **COM** | TXD → pin 2, RXD → pin 1, GND → pin 8 | **Free.** This is the logger's port. |
| **BMS** | 485-A → pin 2, 485-B → pin 1, CAN-H → pin 4, CAN-L → pin 5 | Grey cable to the lithium battery. **Never touch or unplug it.** |

Notes:

- The user first described the free port as "RS485". That was a misreading: the free jack is **COM = RS232** (manual + silkscreen). RS485 exists only on the BMS jack, where the inverter is the master polling the battery. ([v1 C1](../research/v1-protocol-verification.md))
- **"RXD"/"TXD" do not say whose perspective they use.** Voltronic's own cable drawing wires RJ45 pin 1 to the PC's receive pin, i.e. the inverter *transmits* on pin 1, and Voltronic-OEM Tripp Lite uses the same "1:RXD" labelling. So **pin 1 is probably the inverter's TX** — but this is proven electrically (idle-level test) before any ESP32 pin is connected. See [03-hardware-and-wiring.md](03-hardware-and-wiring.md). [inferred, medium-high]
- **Pin 4 "+VCC"** powers the optional WiFi collector. Its voltage is not stated (often +12 V on Voltronic-class units). **Never connect pin 4.** Only pins 1, 2 and 8 are ever used.
- There is no USB jack, no WiFi jack, no dry contact. The ports are on the exposed bottom face: **nothing needs to be opened.**

> ⚠️ **Do not open the lower wiring cover.** PV DC up to 500 V, 230 V AC and a 145 A battery circuit sit behind it. The COM jack is reachable from outside.

### 4.3 Monitoring options in the manual

**§3.8 "RS232 Communication Connection".** "Please download software 'Watch Power' from the official website." The screenshot is Voltronic's **WatchPower** PC tool (a reference 48 V / 6000 VA unit, "Main CPU version 00056.14", topology "transformerless"; the toolbar reads, blurred, "COM1?_Sorotec"). WatchPower is Voltronic's PC tool for the PI30 protocol family — this is the strongest single clue that the COM port speaks PI30 at 2400 8N1. [confirmed text; protocol inference high]

**§3.9 "Wi-Fi Connection (Optional)".** "The device has its own standard WIFI port … they must connect to the WIFI collector." Users download **"SOLAR OF THINGS"** from the app store. The same paragraph claims "factory-integrated Wi-Fi capability (Wi-Fi Dongle is Optional)". On this unit there is no WiFi jack, no WiFi icon on the LCD and no collector in the box; the "WIFI port" is in practice the COM jack (pin 4 supplies the collector). The *Solar of Things* iOS app is published by Shanghai Siseli New Energy (https://apps.apple.com/us/app/solar-of-things/id6445806075). [confirmed]

**Why the logger uses neither:**

| Option | Why not |
|---|---|
| WatchPower (PC over RS232) | Needs a USB-RS232 cable and an always-on Windows PC — neither is owned. Not an iPhone solution. |
| Solar of Things WiFi collector | Must be **bought**; no price or availability found; IPS Bazar lists none. Cloud-only. It would occupy COM, and **only one master may poll COM**, so it cannot run alongside the ESP32 logger. Kept as a fallback (d1 F-B). |

Full comparison: §10.

### 4.4 Front panel: LEDs and keys (§4.2)

| LED | Colour | Solid | Flashing |
|---|---|---|---|
| **AC/INV** | Green | Output powered by **utility** (line mode) | Output powered by **battery** (battery mode) |
| **CHG** | Green | Battery fully charged | Battery charging |
| **FAULT** | Red | **Fault** in the inverter | **Warning** condition |

| Key | Function |
|---|---|
| ESC | Exit setting mode |
| UP | Previous selection |
| DOWN | Next selection |
| ENTER | Confirm the selection in setting mode, or enter setting mode (hold 3 s) |

These LEDs are the first cross-check for the protocol: QMOD `L` should coincide with AC/INV solid, `B` with AC/INV flashing ([02 §7.8](02-protocol-reference.md#78-qmod--operating-mode)).

### 4.5 LCD icons (§4.3)

| Area | Shows |
|---|---|
| **INPUT** field (left digits) with AC / PV tags | Input voltage, input frequency, PV voltage, charger current, charging power, battery voltage |
| Centre two digits + wrench icon | Setting-program number |
| Centre two digits + ⚠ / ERROR | **Warning code** (flashing) or **fault code** (lit, with ERROR) |
| **OUTPUT** field (right digits) | Output voltage, output frequency, load %, load VA, load W, DC discharging current |
| Battery icon, "Li" tag, "CHARGING" | Battery level 0–24/25–49/50–74/75–100 % in battery mode; charging status in line mode |
| Load bulb + wedge bar, OVERLOAD | Load level 0–24/25–49/50–74/75–100 %; overload |
| Mode icons | Mains connected; PV connected; **BYPASS** (load supplied by utility); utility charger working; DC/AC inverter working; battery icon with a small dash = **communication with the battery**; **GRD** = "grid connection in progress" |
| Speaker icon | Alarm muted |

The battery-bar thresholds in the manual are given per cell (e.g. < 1.85 V/cell … > 2.058 V/cell), i.e. lead-acid logic. With a BMS-connected lithium battery the logic may differ [unknown].

### 4.6 Display pages (§4.5)

UP/DOWN cycles the normal (non-setting) display through these pages, in this order:

| # | Page | Left field | Right field |
|---|---|---|---|
| 1 | Input voltage / output voltage (**default screen**) | input V | output V |
| 2 | Input frequency | input Hz | output V |
| 3 | PV voltage | PV V | output V |
| 4 | Charging current | charge A | output V |
| 5 | Charging power (MPPT models) | PV charging W | output V |
| 6 | Battery voltage / output voltage | battery V | output V |
| 7 | Output frequency | battery V | output Hz |
| 8 | Load percentage | battery V | load % |
| 9 | Load in VA (`xxx VA` below 1 kVA, `x.xx kVA` above) | battery V | VA |
| 10 | Load in W (`xxx W` below 1 kW, `x.xx kW` above) | battery V | W |
| 11 | Battery voltage / DC discharging current | battery V | discharge A |
| 12 | **Main CPU version** — e.g. `U1 14 04` = 00014.04 | | |
| 13 | **Secondary CPU version** — e.g. `U2 03 03` = 00003.03 | | |

Pages 12–13 are what the protocol's `QVFW` / `QVFW2` replies should equal (`(VERFW:00014.04`, `(VERFW2:00003.03`); the manual's values are examples only. Program 19 decides whether the display returns to page 1 after one minute (ESP) or stays (HEP).

### 4.7 Setting programs (§4.4)

**How settings mode works (manual):** hold ENTER for 3 s to enter setting mode; UP/DOWN selects programs; ENTER confirms; ESC exits. Program 00 = exit.

> ⚠️ **During the site survey you only read settings, never change them.** Scroll with UP/DOWN and write down the value shown. If a value starts flashing (edit mode), press **ESC**, not ENTER. Do not "try" options. [inferred safe practice]

The logger never writes settings. It **reads** several of them through `QPIRI` and `QFLAG` (right-hand column; field numbers are 0-based, stock PI30 semantics — the mapping for this unit is confirmed in milestone M3 by comparing the LCD with the replies; REVO differences in [02 §7.7](02-protocol-reference.md#77-qpiri--ratings-and-current-settings)). Defaults are for the **24 V model**.

| Prog | Setting | Options (LCD text) | Default (24 V) | Range / notes | Logger reads via |
|---|---|---|---|---|---|
| 00 | Exit setting mode | `ESC` | – | – | – |
| **01** | Output source priority | `UtI` Utility first · `SOL` Solar first · `SbU` SBU (solar → battery → utility) | **UtI** | SbU: utility powers the load only when the battery falls to the low-level warning voltage or the program-12 point | QPIRI[16] (0 / 1 / 2) |
| **02** | Max charging current (utility + solar) | 10–100 A | **60 A** | step 10 A | QPIRI[14] |
| **03** | AC input voltage range | `APL` appliances 90–280 VAC · `UPS` 170–280 VAC | **APL** | | QPIRI[15] (0 = APL, 1 = UPS) |
| **05** | Battery type | `AGn` AGM · `FLd` Flooded · `USE` User-defined · `LIb` Lithium | **AGM** | USE unlocks programs 26, 27, 29 | QPIRI[12]: 0 AGM, 1 Flooded, 2 User; **LIb code [unknown]** (PI30MAX uses 8) |
| 06 | Auto restart after overload | `LtD` disable · `LtE` enable | LtD | | QFLAG `u` |
| 07 | Auto restart after over-temperature | `tFd` disable · `tFE` enable | tFd | | QFLAG `v` |
| 09 | Output frequency | 50 Hz · 60 Hz | 50 Hz | | QPIRI[3] |
| 10 | Output voltage | 220 · 230 · 240 V | 230 V | | QPIRI[2] |
| **11** | Max utility charging current | 2 A, 10–80 A | **30 A** | step 10 A. If program 02 is smaller than 11, the 02 value is used for the utility charger | QPIRI[13] |
| **12** | Back-to-utility point (when 01 = SbU or SOL) | voltage, or SOC for lithium | **23.0 V** (lithium: **SOC 40 %**) | 22.0–25.5 V step 0.5; lithium 10–80 % | QPIRI[8] ("battery re-charge voltage") — SOC encoding [unknown] |
| **13** | Back-to-battery point (when 01 = SbU or SOL) | voltage, `FUL` (battery fully charged), or SOC | **27.0 V** (lithium: **SOC 80 %**) | 24.0–29.0 V step 0.5; lithium 50–100 % step 5 | QPIRI[22] ("re-discharge voltage"; `00.0` = FUL) — SOC encoding [unknown] |
| **16** | Charger source priority | `CUt` Utility first · `CSO` Solar first · `SNU` Solar + Utility · `OSO` Only solar | **SNU** | applies in Line/Standby/Fault mode; in Battery or Power-saving mode only solar charges | QPIRI[17] (0 / 1 / 2 / 3) [inferred order] |
| 18 | Alarm | `bON` · `bOF` | bON | | QFLAG `a` |
| 19 | Auto return to default display screen | `ESP` return after 1 min · `HEP` stay | ESP | | QFLAG `k` |
| 20 | Backlight | `LON` · `LOF` | LON | | QFLAG `x` |
| 22 | Beep while primary source is interrupted | `AON` · `AOF` | AON | | QFLAG `y` |
| 23 | Overload bypass (transfer to line on overload in battery mode) | `byd` disable · `byE` enable | byd | | QFLAG `b` |
| 25 | Record fault code | `FEn` · `FdS` | FEn | | QFLAG `z` |
| 26 | Bulk (C.V.) charging voltage | `CU 26 xx.x` | 28.2 V | 25.0–31.5 V step 0.5 (User-defined only) | QPIRI[10] |
| 27 | Floating charging voltage | `FLU 27 xx.x` | 27.0 V | 25.0–31.5 V step 0.5 | QPIRI[11] |
| **29** | Low DC cut-off | voltage, or SOC for lithium | **21.0 V** (lithium: **SOC 15 %**) | 21.0–26.0 V step 0.5; lithium 5–50 % | QPIRI[9] ("battery under voltage") — SOC encoding [unknown] |
| 30 | Battery equalization | `EEn` · `EdS` | EdS | only with Flooded or User-defined | – |
| 31 | Equalization voltage | `Eu 31 xx.x` | 29.2 V | 25.0–31.5 V | – |
| 33 | Equalized time | | 60 min | 5–900 min step 5 | – |
| 34 | Equalized timeout | | 120 min | 5–900 min step 5 | – |
| 35 | Equalization interval | | 30 days | 0–90 days | – |
| 36 | Equalization activated immediately | `AEn` · `AdS` | AdS | LCD shows `E9` while active | – |
| 40 | Discharge limited current | `OFF` · 10–200 A | OFF | step 5 A. In SOL/SbU, above the limit the unit switches to utility; battery-only operation **shuts down immediately** | – |
| 42 | Adjustment parameter for "EARTH LED" | −30…30 | 0 | settable in line mode only | – |
| 43 | Solar energy feed to grid | `GTd` disable · `GTE` enable | GTd | | QFLAG `d` (PI30MAX; may be absent) |
| 44 | Reconnection delay time | 0–999 s | 00 | waiting time after utility returns | – |
| 45 | Zero-export to load | 0–500 step 10 | 100 (line mode) | settable in line mode only | – |
| 50 | Battery activation | `bdS` OFF · `bEn` ON | bdS | manual activation of an undetected battery | – |

Programs 04, 08, 14, 15, 17, 21, 24, 28, 32, 37–39, 41 and 46–49 do not appear in this manual.

### 4.8 Operating modes (§4.6) and how the protocol reports them

| Manual mode | Description | Charging possible from | QMOD letter |
|---|---|---|---|
| Power-on | Start-up | – | `P` |
| **Standby** | Inverter not switched on yet; no AC output, but it can still charge the battery | utility and/or PV | `S` |
| **Power saving** | If enabled, output turns off when the load is very low or absent | utility and/or PV | `H` (REVO doc calls it "Sleep") |
| **Line mode** | Output from the mains; also charges the battery | utility and PV | `L` |
| **Battery mode** | Output from battery and PV | (PV only) | `B` |
| **Fault mode** | Internal circuit error or external cause (over-temperature, output short, …) | PV and utility can still charge | `F` |

PIP-GK/MK firmware adds `D` shutdown, `C` charge, `Y` bypass, `E` ECO — not expected here.

§4.7 (equalization): enabled by program 30; triggered by the interval (35) or immediately (36); stays at the equalization voltage for program 33's time, extended up to program 34's timeout. Irrelevant for a lithium battery; the logger only shows warning `E9` if it happens.

### 4.9 Fault codes (§5) — shown lit on the LCD with ERROR

The wording matches Voltronic's own PI30 fault tables (P2014 / PI30MAX), another sign of Voltronic lineage. The "What to do" column summarises the manual's troubleshooting table (§7).

| Code | Fault event (manual) | Manual's advice (§7) |
|---|---|---|
| 01 | Fan is locked when inverter is off | Replace the fan |
| 02 | Over temperature | Internal temperature > 100 °C: check airflow and ambient temperature |
| 03 | Battery voltage is too high | Battery over-charged → repair centre; or check battery spec/quantity |
| 04 | Battery voltage is too low | (Unit shuts down at start-up if < 1.91 V/cell: re-charge or replace battery) |
| 05 | Output short-circuited, or over-temperature detected by internal converter components | Check wiring, remove abnormal load; converter > 120 °C → check airflow |
| 06 | Output voltage is too high | Output abnormal (< 190 or > 260 Vac): reduce load; repair centre |
| 07 | Overload time out | Overload 110 % for too long; or PV voltage above spec causing derating: reduce load / modules in series |
| 08 | Bus voltage is too high | Internal components failed → repair centre |
| 09 | Bus soft start failed | Internal components failed → repair centre |
| 51 | Over current or surge | Restart; if repeated, repair centre |
| 52 | Bus voltage is too low | Restart; if repeated, repair centre |
| 53 | Inverter soft start failed | Internal components failed → repair centre |
| 55 | Over DC voltage in AC output | Output voltage unbalanced: restart; if repeated, repair centre |
| 57 | Current sensor failed | Internal components failed → repair centre |
| 58 | Output voltage is too low | As 06 |
| 59 | PV voltage is over limitation | Reduce the number of PV modules in series |

### 4.10 Warning codes (§6) — shown flashing on the LCD

| Code | Warning event |
|---|---|
| 01 | Fan is locked when inverter is on |
| 02 | Over temperature |
| 03 | Battery is over-charged |
| 04 | Low battery |
| 07 | Overload |
| 08 | Discharge current limiting |
| 10 | Output power derating |
| 15 | PV energy is low |
| 16 | High AC input (> 280 VAC) during BUS soft start |
| E9 | Battery equalization |
| bP | Battery is not connected |

**These LCD codes are not the same numbering as the protocol's tables.** QPIWS is a bit string, and Sorotec's `QALL` uses its own warning (00–07, 21–26) and fault (01–12) codes — e.g. REVO fault 01 is "BUS overvoltage", while LCD fault 01 is "fan locked". The logger stores every code with its table source (`lcd`, `qpiws32`, `qpiws36`, `revo`), and `shared/codes.json` holds all tables. See [02 §7.9, §7.11](02-protocol-reference.md#79-qpiws--warning-and-fault-bits).

---

## 5. Full specifications (3.2K24V column)

From manual §8–§12 unless noted. [confirmed]

### 5.1 Line mode (§8)

| Parameter | Value |
|---|---|
| Input voltage waveform | Sinusoidal (utility or generator) |
| Nominal input voltage | 230 Vac |
| Low loss voltage | 170 ± 7 Vac (narrow range / UPS) · 90 ± 7 Vac (wide range / APL) |
| Low loss return voltage | 180 ± 7 Vac (narrow) · 100 ± 7 Vac (wide) |
| High loss voltage | 280 ± 7 Vac |
| High loss return voltage | 270 ± 7 Vac |
| Max AC input voltage | 300 Vac |
| Nominal input frequency | 50 Hz / 60 Hz (auto detection) |
| Low loss frequency / return | 40 ± 1 Hz / 42 ± 1 Hz |
| High loss frequency / return | 65 ± 1 Hz / 63 ± 1 Hz |
| Output short-circuit protection | Circuit breaker |
| Efficiency (line mode) | > 95 % (rated R load, battery fully charged) |
| Transfer time | 10 ms typical (narrow range), 20 ms typical (wide range) |
| Output power derating | Below 170 V AC input the output power is derated (graph: 50 % at 90 V, rated from 170 V to 280 V) |

### 5.2 Inverter mode (§9)

| Parameter | Value |
|---|---|
| Rated output power | **3200 VA / 3200 W** |
| Output waveform | Pure sine wave |
| Output voltage regulation | 230 Vac ± 5 % |
| Output frequency | 50 Hz / 60 Hz |
| Peak efficiency | 93 % |
| Overload protection | 10 s @ 110–130 % load; 5 s @ 130–200 %; 200 ms @ ≥ 200 % |
| Surge capacity | 2 × rated power for 5 s |
| Cold start voltage | 23.0 Vdc |
| Low DC warning voltage | 22.0 Vdc (load < 50 %) · 21.0 Vdc (load ≥ 50 %) |
| Low DC warning return voltage | 22.5 Vdc (load < 50 %) · 22.0 Vdc (load ≥ 50 %) |
| Low DC cut-off voltage | 20.5 Vdc (load < 50 %) · 20.0 Vdc (load ≥ 50 %) |
| High DC recovery voltage | 32.0 Vdc |
| High DC cut-off voltage | 33.0 Vdc |

### 5.3 Charge mode (§10)

| Parameter | Value |
|---|---|
| Charging algorithm | 3-step (bulk / absorption / float) |
| AC charging current | 2 / 10 / 20 / 30 / 40 / 50 / 60 / 70 / 80 A (@ 230 Vac input) |
| Bulk charging voltage | Flooded 29.2 Vdc · AGM/Gel 28.2 Vdc |
| Floating charging voltage | 27.0 Vdc |
| Max charging current (solar + AC) | **100 A** |

### 5.4 Solar mode (§11)

| Parameter | Value |
|---|---|
| Rated output voltage | 230 VAC (230 ± 5 %) |
| PV input max power | **3500 W** |
| PV array MPPT voltage range | **30–450 VDC** |
| Start-up voltage | 70 ± 5 VDC |
| Nominal PV voltage | 260 VDC |
| Max PV array open-circuit voltage | **500 VDC** |
| Max PV input current | **15 A** (the logger uses this to tell PV *current* from PV *power* in QPIGS field 12 — [02 §8](02-protocol-reference.md#8-dialect-classification)) |

### 5.5 General (§12, §13)

| Parameter | Value |
|---|---|
| Safety certification | CE |
| Operating temperature | −10 °C to 50 °C |
| Storage temperature | −15 °C to 60 °C |
| Humidity | 5–95 % RH, non-condensing |
| Dimensions (L × W × H) | 344 × 255 × 105 mm (drawing: 352 / 344 high, 254.4 / 268.6 wide incl. rails, 104.7 deep, 202 mm bracket) |
| Net / gross weight | 5.5 kg / 7 kg |

### 5.6 Installation figures (§3.3–§3.6)

| Item | Value |
|---|---|
| Battery cable | 1 × 3 AWG / 35 mm², max discharge current 145 A, recommended battery ≥ 200 Ah |
| AC input breaker | 30 A (3.2 kW model) |
| AC wiring | 12 AWG / 4 mm², torque 1.0–1.2 Nm |
| PV wiring | 12 AWG / 4 mm², torque 1.0–1.2 Nm; DC breaker + surge-protected PV junction box required |
| Mounting | Vertical, on concrete or another non-combustible surface; ≈ 20 cm clearance at the sides, ≈ 50 cm above and below; M4/M5 screws |
| Package contents | Inverter × 1, user manual × 1 (no cable, no dongle) |

---

## 6. Values the logger relies on

| Fact | Used for |
|---|---|
| Rated 3200 VA, 24.0 V battery, 13.9 A output | Identity check on QPIRI fields 4/5/7 (`13.9`, `3200`, `24.0`) during the probe |
| Max PV input current 15 A | QPIGS field-12 rule: a value > 15 while PV V > 30 must be watts, not amps |
| Battery window: cut-off 20.0 V, high DC cut-off 33.0 V | Plausibility gate 18.0–34.0 V on battery voltage |
| Output 230 V ± 5 %, 50/60 Hz | Plausibility gates (0 or 180–260 V; 0 or 45–65 Hz) |
| PV Voc 500 V | Plausibility gate PV V 0–520 |
| LCD U1/U2 versions | Must equal `QVFW` / `QVFW2` |
| Programs 01/02/03/05/11/12/13/16/29 | Must equal the decoded QPIRI fields |

---

## 7. Values to record from the LCD during the site survey

Record these **before** wiring anything (milestone M0). They become ground truth for the protocol checks in milestone M3. The survey form is site-survey.md.

**A. Firmware versions** (normal display, UP/DOWN to pages 12–13):

| Record | Example in manual | Must match |
|---|---|---|
| Main CPU `U1 xx xx` | `U1 14 04` | `QVFW` → `(VERFW:000xx.xx` |
| Secondary CPU `U2 xx xx` | `U2 03 03` | `QVFW2` → `(VERFW2:000xx.xx` |

**B. Setting programs** (read only — see the warning in §4.7):

| Prog | What to write down | Cross-check (stock PI30) |
|---|---|---|
| 01 | UtI / SOL / SbU | QPIRI[16] = 0 / 1 / 2 |
| 02 | max charging current, A | QPIRI[14] |
| 03 | APL / UPS | QPIRI[15] = 0 / 1 |
| 05 | AGn / FLd / USE / **LIb** | QPIRI[12] (tells us which code this firmware uses for LIb) |
| 11 | max utility charging current, A | QPIRI[13] |
| 12 | back-to-utility point: volts **or** SOC % | QPIRI[8] (also tells us how SOC thresholds are encoded) |
| 13 | back-to-battery point: volts, FUL **or** SOC % | QPIRI[22] |
| 16 | CUt / CSO / SNU / OSO | QPIRI[17] = 0 / 1 / 2 / 3 |
| 29 | low DC cut-off: volts **or** SOC % | QPIRI[9] |

Optional extras if time allows: 09, 10 (QPIRI[3], [2]); 26, 27 (QPIRI[10], [11]); 18/19/20/22/23/25 (QFLAG letters).

**C. Status at the moment of the first probe** (photograph the LCD): PV W, output V, battery V, load W, which LEDs are solid or flashing, whether the "Li" tag and the battery-communication icon are lit. A probe reply is trusted only if output V agrees within ±2 V and battery V within ±0.3 V.

**D. Labels:** the serial barcode (compare with `QID`/`QSID` — they may or may not match) and the second sticker.

---

## 8. Things that are still unknown, and how each gets resolved

| Unknown | How it is resolved | When |
|---|---|---|
| Which COM pin (1 or 2) is the inverter's TX | Receive-only idle-level test with the RS485 module ([03](03-hardware-and-wiring.md)) | M2 |
| True RS232 (± levels) or TTL on COM | Same test: a steadily LOW receiver output means a negative idle = RS232 | M2 |
| Pin-4 "+VCC" voltage | Not needed — never connected. Measure only if a multimeter ever appears. | – |
| Stock PI30 vs REVO dialect; which optional commands NAK | Firmware probe ([02 §8–9](02-protocol-reference.md#8-dialect-classification)) | M3 |
| QPIRI code for battery type LIb; SOC encoding of programs 12/13/29 | Compare the LCD (§7) with QPIRI | M3 |
| Whether the battery reports SOC to the inverter (vs voltage-derived SOC) | "Li"/battery-communication icon on the LCD; `QLITH0` probe (read-only) | M0 / M3 |
| Exact factory | Optional e-mail/phone to IPS Bazar or Sorotec | optional |
| Whether a Solar of Things collector exists for this unit and its price | Phone IPS Bazar (01749 426 789) | optional (fallback only) |

---

## 9. Safety summary for anyone touching the unit

> ⚠️ Work only on the **COM** jack. **Never touch or unplug the BMS cable** — the inverter is the master on that bus and polls the lithium battery; disconnecting it can raise battery-communication warnings and change lithium charging behaviour.
>
> ⚠️ Only RJ45 pins **1, 2 and 8** are ever connected. **Pin 4 (+VCC)** and pins 3, 5, 6, 7 are cut and insulated individually.
>
> ⚠️ Do not open any cover. Do not plug the cut LAN cable into a router, a switch or the BMS jack.
>
> ⚠️ Never change settings from the logger (read-only firmware) and never change them on the LCD "to test something".

---

## 10. Existing app vs DIY

Summary of [r4](../research/r4-existing-apps.md) and [d1 §1.1](../research/d1-architecture.md), under the hard constraints (no purchases, iPhone viewer, no always-on home server).

| Option | What it needs | Data / limits | Verdict |
|---|---|---|---|
| Built-in WiFi | Nothing | **Not present** on this unit: no WiFi icon on the LCD, no WiFi jack; only COM and BMS. A free SSID scan near the inverter in M0 double-checks. | Not available |
| **"SOLAR OF THINGS" app + optional WiFi collector** (the manual's own route) | **Buying** a collector that plugs into COM; no price or availability found, none listed by IPS Bazar | Cloud app by Shanghai Siseli New Energy; data ownership and retention unknown; would be the only master on COM | **Rejected** (purchase). Fallback F-B if the DIY link cannot be made to work. |
| SmartESS / WatchPower / Energy-Mate / SolarPower Wi-Fi (Eybond cloud, DessMonitor) | Buying an Eybond/Voltronic/PowMr WiFi dongle; model-locked ("only compatible with the inverters below") | All five iOS apps come from one Eybond developer account and one cloud; ratings ≈ 2.5–2.9 (SmartESS 2.69 from 127 ratings; Energy-Mate 4.2 from 13); dongle uploads every **5 minutes** by default; retention undocumented; cloud API unofficial (SHA-1 signed, no 2FA). Probably the wrong ecosystem for this unit (the manual names Solar of Things). | **Rejected** (purchase; wrong ecosystem likely) |
| WatchPower PC software over RS232 (the manual's §3.8) | A USB-RS232 cable and an always-on Windows PC | Local PC only; not an iPhone solution | **Rejected** (neither is owned) |
| Home Assistant + Companion app | An always-on Pi/PC host; remote access $6.50/month or self-managed | Excellent history and push | **Rejected** (no host; purchase) |
| SolarAssistant | Licence $59 + a Raspberry Pi (or $179–229 device) + $29 RS232 cable | Polished, local data, free remote access | **Rejected** (purchase; does not use the owned ESP32) |
| ESPHome `pipsolar` standalone | The owned ESP32 | Local page; graph history only 50 points in the browser; no Firebase auth, no back-fill | Useful only as an optional cross-check tool, not the logger (d1 D4) |
| **DIY: ESP32-S3 → Firebase RTDB → iPhone PWA** | Only parts already owned | 1-minute history kept for years, user-owned; local `http://solar.local` page works without internet; LittleFS buffer + back-fill during outages; alerts via ntfy / Telegram / Healthchecks | **CHOSEN** |

**Verdict.** Build the DIY logger. It is the only option that needs no purchase, it keeps the data under the user's control at 1-minute resolution, it works on the iPhone without depending on the App Store (Bangladesh is not an App Store storefront, so app availability depends on the Apple ID's region), and it keeps logging through internet outages. The protocol risk is low: every request frame has been verified byte for byte ([02 §6](02-protocol-reference.md#6-byte-exact-request-frames)). Architecture: [04-architecture-and-data-model.md](04-architecture-and-data-model.md).

> ⚠️ If a vendor collector is ever added to COM, the ESP32 must be removed or switched to listen-only. **Only one master may poll the COM port.**

---

## 11. Sources

First-hand: nameplate, front, LCD and bottom-panel photos in `images/`; the paper manual scan `reference/Savx-F4-user-manual.pdf`.

- Sorotec REVO VM II PRO 1.6–10 kW product page: https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/ (spec images `…/2025/01/0e9d9c96421b6fdc9130eb0ea6ad422.png`, `…/2025/01/3ce086e72ac3589270e34a76a8232df.png`)
- Savx F3 "VM II PRO" datasheet (IPS Bazar): https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf
- IPS Bazar Savx F4 3000-24V listing: https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/
- BestEn 3.5/5.5 kW manual ("614.C0572-00", "RevoMonitor"): https://deps.ua/downloads/Documentation/besten/autonomous-sm-1p-32kw-24v/BestEn_Autonomous-sm-1p-32kw-24v_EN.pdf
- Solar of Things (App Store): https://apps.apple.com/us/app/solar-of-things/id6445806075
- SmartESS (App Store): https://apps.apple.com/us/app/smartess/id1334656760
- dessmonitor-homeassistant README ("Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi"): https://github.com/SilverFire/dessmonitor-homeassistant
- Voltronic PI30 protocol (P2014, cable drawing): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf
- PI30MAX protocol (fault-code wording): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30MAX.Communication.Protocol20210217.pdf
- Tripp Lite APSWX manual ("1:RXD232, 2:TXD232, 8:GND"): https://tripplite.eaton.com/support/owners-manual/932564
- SolarAssistant shop: https://solar-assistant.io/shop · Home Assistant Cloud pricing: https://www.nabucasa.com/pricing/
- ESPHome pipsolar: https://esphome.io/components/pipsolar/

Full source list: [09-sources.md](09-sources.md).
