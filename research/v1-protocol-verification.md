# V1: Fact-check of R1/R2/R3 (OEM identity, physical port, protocol) for the Savx F4 3KW

Date: 2026-10-21. Role: skeptical verifier. I tried to refute the OEM identification and the port/protocol claims in R1 (OEM), R2 (Voltronic PI30) and R3 (Modbus families), and re-checked the load-bearing technical claims against primary sources.

**Confidence tags**
- **[CONFIRMED-primary]**: I read it myself in an official document, in source code, or in the user's own photo or paper manual. Or I computed it and it matches a primary example.
- **[SECONDARY]**: forum, README narrative, listing or search snippet.
- **[INFERRED]**: my own reasoning.

**Evidence base used by this check (all re-read or re-fetched by me)**
- User photos `images/1.jpg`, `2.jpg`, `3.jpg` and **`images/6.jpg` (bottom panel)**. R1 to R3 did not use the bottom-panel photo.
- **The Savx F4 paper manual, scanned:** `the paper manual (PDF scan)` (20 scanned pages; I viewed the pages directly). R1 to R3 did not use it either.
- Voltronic PI30 protocol PDFs, re-downloaded from `raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/` and text-extracted. Local copies are in `scratchpad/v1/dl/`.
- Source code: mpp-solar `protocol_helpers.py`, `pi30revo.py`, `abstractprotocol.py`; ned-kelly `inverter.cpp`; ESPHome core `pipsolar.cpp`; syssi `esphome-pipsolar` README; arduino-esp32 `esp32-hal-uart.c`.
- Sorotec spec images and renders (R1's downloads, re-read by me).
- The Savx F3 datasheet PDF (re-downloaded).
- A BestEn manual found during refutation (new).
- The "Solar of Things" App Store page (new).
- **CRC checks:** every CRC and checksum in this file was recomputed by my own script, `scratchpad/v1/scripts/crc.py`. It implements bitwise CRC-16/XMODEM, the ned-kelly nibble table (asserted identical), and CRC-16/MODBUS.

---

## 0. Verdict in brief

1. **The free port is "COM" and it is RS232, not RS485.**
   - The bottom panel (photo 6) shows two RJ45 jacks silk-screened **"COM"** (free) and **"BMS"** (grey battery cable plugged in).
   - The silkscreen diagram reads COM pin 1 = RXD, pin 2 = TXD, pin 8 = GND.
   - It also reads BMS pin 1 = 485-B, pin 2 = 485-A, pin 4 = CAN-H, pin 5 = CAN-L.
   - The paper manual (§2.3) says the same and adds **COMM pin 4 = "+VCC"**: *"COMM: RS232 — 1:RXD, 2:TXD, 4:+VCC, 8:GND"* and *"BMS: RS485, CAN — 1:485-B, 2:485-A, 4:CAN-H, 5:CAN-L"*.
   - R1's and R2's premise that the free port is labelled "RS485" is **wrong**. R3's "most likely SMG Modbus over RS485" is **refuted for this unit**. [CONFIRMED-primary]
2. **The protocol is almost certainly the Voltronic PI30 ASCII family at 2400 8N1.**
   - Manual §3.8 "RS232 Communication Connection" tells the user to install **"Watch Power"**, Voltronic's PC tool for the PI30 family.
   - The LCD fault codes (01–09, 51–59) and the "U1/U2" CPU-version display are Voltronic conventions; the fault-code wording matches Voltronic's own PI30MAX document (manual §5 vs PMAX §2.11).
   - Protocol: [INFERRED, high] from [CONFIRMED-primary] manual and photo. Baud and framing: [CONFIRMED-primary] from Voltronic's protocol PDFs, but not yet seen on this unit.
3. **Stock PI30 versus Sorotec's "PI30REVO" dialect is unresolved.** Both answer QPI/QPIGS/QPIRI/QMOD with the same CRC framing.
   - REVO adds `QALL` (sum+1 checksum).
   - REVO **re-purposes QPIGS field 12 (0-based) from PV current to PV power**.
   - The firmware must test for `QALL` and sanity-check field 12 (§6.5). [CONFIRMED-primary for the dialect definitions; unknown for this unit]
4. **OEM identity is moderately well supported but not proven.**
   - The best-supported statement is: *a Sorotec "REVO VM II PRO 1.6/3.2 kW" (2025 round-dial) platform, built by Sorotec or by the factory that builds it for Sorotec, with a Voltronic-derived firmware lineage.*
   - Six independent clues support it. Three facts do not fit cleanly: different case dimensions and weight in the manual, a different WiFi app ("SOLAR OF THINGS", not SmartESS), and no USB port.
   - None of this changes the protocol procedure. See §2.
5. **All PI30, REVO-CHK and Modbus frame bytes quoted by R1/R2/R3 that I checked are correct.** This covers more than 40 frames (§4.4). I found two small documentation errata: the PSET example checksum in Sorotec's own doc, and mpp-solar's QALL test frame.
6. **The RJ45 TX/RX labels are perspective-ambiguous.** In Voltronic's cable drawing, RJ45 pin 1 → PC DB9 pin 2, labelled "TXD": the device transmits on pin 1. Savx (and Voltronic-OEM Tripp Lite) silkscreen pin 1 as "RXD", which most likely means "PC receives here".
   - **Pin 1 is probably the inverter's TX**, but this must be proven electrically before the ESP32's TX pin touches anything (§6.3). [INFERRED, medium-high]
7. **The user's RS485 module can still be used on the RS232 port, receive-side, in a non-standard way.** Its differential receiver can read the RS232 TX line (A = TX, B = GND → inverted UART). The ESP32's TX, inverted at 0/3.3 V, meets RS232 receiver thresholds (MAX3232: V_IT+ ≤ 2.4 V, input 3–7 kΩ).
   - R1/R2's "the RS485 module cannot talk to an RS232 port" is too absolute.
   - This is a workaround with stated risks, not a MAX3232-equivalent design. [INFERRED from CONFIRMED-primary datasheet values]
8. **Modbus families (SMG, SRNE, MUST, PowMr, 8851) drop to last-resort probes.** Their RS485 variants cannot apply, because the only RS485 port (BMS) is occupied and the inverter is master on it.

---

## 1. New first-hand evidence the three reports did not have

| # | Evidence | What it shows | Tag |
|---|---|---|---|
| E1 | Photo `images/6.jpg` (bottom panel) | RJ45 "COM" (free, left) and "BMS" (grey cable, right). Silkscreen pinout: COM 1 RXD, 2 TXD, 8 GND; BMS 1 485-B, 2 485-A, 4 CAN-H, 5 CAN-L. Also: AC IN L/N, AC OUT L/N, BATTERY +/−, PV IN +/−, two earth screws, an **"ON – O OFF"** rocker, two fans. **No USB jack, no WiFi jack, no dry contact.** The ports are on the exposed bottom face, so no cover has to be opened. | [CONFIRMED-primary] |
| E2 | Manual cover | "USER MANUAL, 1.6KW/3.2KW SOLAR INVERTER/CHARGER, Version:3.0". Generic, no brand. The cover drawing is the same round dial (white body, red-orange ring). | [CONFIRMED-primary] |
| E3 | Manual §2.3 (page 3) | "Communication port definition: COMM: RS232 — 1:RXD, 2:TXD, 4:+VCC, 8:GND; BMS: RS485, CAN — 1:485-B, 2:485-A, 4:CAN-H, 5:CAN-L". Items: "5. COMM communication port", "6. BMS communication port (Option)". The **+VCC voltage is not stated.** | [CONFIRMED-primary] |
| E4 | Manual §3.8 (page 6) | "Please download software 'Watch Power' from the official website." The screenshot is WatchPower (Voltronic). The toolbar reads, blurred, "Monitored device **COM1?_Sorotec**". The screenshot is of a 48 V / 6000 VA unit, "Main CPU 00056.14". | Text: [CONFIRMED-primary]. "Sorotec" reading: [INFERRED, blurry crop `scratchpad/v1/ws_name.jpg`] |
| E5 | Manual §3.9 (page 6) | "The device has its own standard WIFI port … must connect to the WIFI collector. Users can download **'SOLAR OF THINGS'** WIFI monitoring software … (Wi-Fi Dongle is Optional)". | [CONFIRMED-primary] |
| E6 | "Solar of Things" iOS app | Seller: **Shanghai Siseli New Energy** (上海四色礼新能源), id6445806075, v3.1.12, iOS 15+. It mentions "Bluetooth distribution network" and "local monitoring". https://apps.apple.com/us/app/solar-of-things/id6445806075 | [CONFIRMED-primary, App Store] |
| E7 | Manual §4.5 (page 13) | The LCD can show "Main CPU version 00014.04 → U1 14 04" and "Secondary CPU version 00003.03 → U2 03 03". These are example values. The real values are readable on the user's LCD and should equal the `QVFW`/`QVFW2` replies. | [CONFIRMED-primary] |
| E8 | Manual §5 / §6 (page 15) | Fault codes 01–09 and 51, 52, 53, 55, 57, 58, 59. The wording matches Voltronic PMAX §2.11 ("Fan is locked when inverter is off", "Bus voltage is too low", "Over DC voltage in AC output"). Warning codes: 01, 02, 03, 04, 07, 08, 10, 15, 16, E9, bP. | [CONFIRMED-primary] (comparison against `PI30MAX…20210217.pdf`) |
| E9 | Manual §11 / §12 (page 18) | 3.2K24V: PV 3500 W, MPPT 30–450 VDC, start-up 70 ± 5 VDC, nominal 260 VDC, Voc 500 VDC, **max PV input current 15 A**, max charge (solar + AC) 100 A, AC charge 2–80 A. **Dimensions (L×W×H) 344 × 255 × 105 mm; net 5.5 kg** (gross 7). The drawing shows 352/344 high, 254.4 wide (268.6 with rails), 104.7 deep. | [CONFIRMED-primary] |
| E10 | Manual back cover (page 20) | Part number **"614.C0556-02"**. | [CONFIRMED-primary] |

---

## 2. OEM identification: refutation attempt

### 2.1 Evidence for "Sorotec REVO VM II PRO 1.6/3.2 kW (2025 round-dial) platform"

| # | Evidence | Strength | Source / tag |
|---|---|---|---|
| F1 | The spec table matches the 3200 VA column on many independent numbers: 3200 VA/W, 24 V, Voc 500 V, PV 3500 W, MPPT 30–450 V, PV input 15 A, max charge 100 A, AC charge 80 A, surge 6400 VA (manual: "2* rated power for 5 seconds"), float 27 V, overcharge 33 V (manual: high DC cut-off 33.0 V), peak efficiency 93 %, −10…50 °C. The **1.6 kW net weight (5 kg) also matches**. | Strong | Sorotec spec images `wp-content/uploads/2025/01/0e9d9c96421b6fdc9130eb0ea6ad422.png` and `…3ce086e72ac3589270e34a76a8232df.png` (I read both) vs manual §8–§12. [CONFIRMED-primary] |
| F2 | The front design is the same: round black dial, orange ring, LEDs "AC/INV · CHG · FAULT", ESC/UP/DOWN/ENTER. The bottom layout is also the same: 4-pole AC block on the left, **two RJ45 side by side**, two fans, battery block in the centre, PV block on the right, rocker at top right. | Strong for the chassis; it does not identify who makes it | Sorotec render `…/2025/01/03-底部-768x768.webp` vs photo 6. [CONFIRMED-primary] |
| F3 | IPS Bazar's own Savx F3 datasheet contains Sorotec's product-line text: "F3 VM II PRO … **REVO VM II PRO series** is suitable for…" and "Standard:USB/RS232；Optional：CAN&RS485". | Strong: the supply chain links to the REVO line | https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf (re-downloaded; `pdftotext` lines 1, 9, 94). [CONFIRMED-primary] |
| F4 | The WatchPower screenshot in the Savx manual names the monitored device "COM1?_**Sorotec**". | Moderate; the crop is blurry | E4. [INFERRED] |
| F5 | The manual-numbering family "614.Cxxxx-xx" also appears on a BestEn "3.5KW/5.5KW" manual (back cover "614.C0572-00"). That manual has the same RS232 pin labelling ("1:RXD, 2:TXD, 8:GND"), the same WatchPower §, and a Bluetooth app called **"RevoMonitor"**. "REVO" is Sorotec's line name, and the BestEn spec (120–450 V, 5500 W PV) matches Sorotec's older REVO VM II PRO 3.5/5.5 kW. | Moderate: same document factory as a REVO rebrand | https://deps.ua/downloads/Documentation/besten/autonomous-sm-1p-32kw-24v/BestEn_Autonomous-sm-1p-32kw-24v_EN.pdf (downloaded; text lines 158–164, 382, 391–393, last line). [CONFIRMED-primary document; INFERRED link] |
| F6 | Sorotec's spec sheet lists "Standard: RS232, USB; CAN&RS485; Optional: WiFi, Bluetooth". The Savx has RS232 (COM) and RS485/CAN (BMS). | Weak to moderate (USB is missing; see A3) | Sorotec spec image part 2. [CONFIRMED-primary] |

### 2.2 Evidence against, or unexplained

| # | Discrepancy | Effect on the conclusion | Source / tag |
|---|---|---|---|
| A1 | **Dimensions and weight differ.** Sorotec 3.2 kW: "Dimensions D×W×H 335\*225\*100", 6.1 kg. Savx manual: 344 × 255 × 105 mm, 5.5 kg net (drawing 254.4 wide). | Either Sorotec's table is wrong, or the Savx is a related but different chassis revision or factory. It **weakens "same SKU"** and does not weaken "same platform". **R1's advice to "measure the case and compare with 335 × 225 × 100" is a poor test.** Compare with the manual's own 344 × 255 × 105 instead. | Sorotec spec image part 2 vs manual §12. [CONFIRMED-primary] |
| A2 | **WiFi app differs.** Sorotec WiFi variants are reported on SmartESS/DESS (Eybond). The Savx manual names "SOLAR OF THINGS" (Siseli). | The collector ecosystem differs. Sorotec probably did not write this manual verbatim, or newer Sorotec units changed dongle vendor. Unknown. | dessmonitor README ("Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi") https://raw.githubusercontent.com/SilverFire/dessmonitor-homeassistant/main/README.md vs E5. [CONFIRMED-primary both] |
| A3 | **No USB port** on the unit (photo 6; manual items 1–8 list none). Sorotec and the Savx F3/F4 listings say "USB". | The listings and the datasheet are generic. Trust the photo. | E1. [CONFIRMED-primary] |
| A4 | No public document, firmware dump or trace exists for the **2025 round-dial** generation (Sorotec or rebrand). Every REVO protocol report is for older REVO II/VE II/VM II units. | The REVO dialect is **unproven** for this unit. | mpp-solar #78 (REVO II / iGrid VE II); DIY Solar thread 34404 (older 120–450 V "REVO VM II PRO 5.5kW"). [SECONDARY] |
| A5 | The rocker is labelled "ON – O OFF" on the unit, not "I O II" as in Sorotec's render. | Cosmetic only. | Photo 6 vs render. [CONFIRMED-primary] |

### 2.3 Alternatives checked
- **Voltronic Axpert VM II Premium 3K-24V**, **MUST PV1800 PRO**, **PowMr HVM3.2H-24V**, **Crown Xavier**, **SAKO Sunon Pro**, **Hinvert HMK-plus**: R1's spec mismatches (Voc 450 V, 30–400 V MPPT, 60–400 V and so on) stand.
  - I re-viewed the SAKO image (`r1dl/small_img_SUNONPro…jpg`). It is a rectangular-LCD design, not round-dial. [CONFIRMED-primary image]
- **Web searches** for "1.6KW/3.2KW" + 30–450 V + COMM/BMS, for "614.C0556", and for Sorotec + "Solar of Things" found **no other brand** documenting this exact chassis. That is a negative result, not proof of uniqueness. [SECONDARY]

### 2.4 What is actually known, ranked

| Rank | Statement | Confidence |
|---|---|---|
| 1 | The free port is **RS232 "COM"** (1/2 = RXD/TXD in some perspective, 8 = GND, 4 = +VCC). **BMS** is RS485 + CAN (battery link, occupied). | **Certain**: photo + manual [CONFIRMED-primary] |
| 2 | The firmware is **Voltronic-derived** and the PC software is WatchPower, so the protocol is the **PI30 family at 2400 8N1**. | **High** [INFERRED from CONFIRMED-primary] |
| 3 | The platform is the **Sorotec REVO VM II PRO 1.6/3.2 kW round-dial design** (Sorotec or its contract factory), rebranded by IPS Bazar. | **Medium-high** (F1–F6 vs A1–A2) |
| 4 | The firmware speaks the **Sorotec PI30REVO dialect** (QALL, QPIGS field 12 = PV W). | **Unknown, around 50/50**. It must be probed (§6.5). |
| 5 | Any specific contract factory beyond Sorotec. | **Unknown** |

**Why the remaining uncertainty barely matters:** every candidate in ranks 2–4 speaks the same CRC-framed ASCII at 2400 8N1 on the same RJ45. The probe sequence in §6 detects the dialect in under 10 seconds.

---

## 3. Corrections

| # | Report and claim | Correct value | Source / tag |
|---|---|---|---|
| C1 | R1 §0.3, §4.1; R2 §0.3, §12: the user's free port is labelled **"RS485"** (and "BMS/CAN"). | The labels are **"COM"** (RS232) and **"BMS"**. COM: 1 RXD, 2 TXD, 4 +VCC, 8 GND. BMS: 1 485-B, 2 485-A, 4 CAN-H, 5 CAN-L. | Photo 6; manual §2.3. [CONFIRMED-primary] |
| C2 | R3 §0.2: the most likely family is **SMG Modbus** (9600, slave 1). | Not for this unit. The port is RS232 and the manual specifies **WatchPower**, so PI30 ASCII at 2400. SMG becomes a last-resort probe. | Manual §2.3, §3.8. [CONFIRMED-primary] → [INFERRED] |
| C3 | R1 §0.5, R2 §3.3, R3 §12: the RS485 module **cannot** talk to an RS232 port; a MAX3232 is needed. | It cannot *drive* RS232, but its receiver can *read* an RS232 TX line differentially (A = TX, B = GND gives inverted UART). The ESP32 TX, inverted at 0/3.3 V, satisfies RS232 receiver thresholds: TI MAX3232 receiver V_IT+ typ 1.5 V, **max 2.4 V**; V_IT− min 0.6 V; input 3–7 kΩ; input range ±25 V. It is workable with risks (§6.3). A MAX3232 remains the robust option. | TI MAX3232 datasheet SLLS410O §6.7, https://www.ti.com/lit/ds/symlink/max3232.pdf. [CONFIRMED-primary values; INFERRED application] |
| C4 | R1 §4.1, §5.3; Savx/Sorotec listings: there is a **USB** port carrying the same protocol. | **No USB port** on this unit. | Photo 6; manual §2.3 item list. [CONFIRMED-primary] |
| C5 | R1 §0.4, §4.4: no Savx WiFi accessory or app exists; Sorotec WiFi uses **SmartESS**, so test for a "W0…" Eybond SSID. | The **manual documents an optional "WIFI collector" with the "SOLAR OF THINGS" app** (Shanghai Siseli New Energy). The Eybond SSID test is harmless but may not be the relevant ecosystem. Ask IPS Bazar for the *Solar of Things* collector. | Manual §3.9; App Store id6445806075. [CONFIRMED-primary] |
| C6 | R1 §5.6.2: measure the case and compare with Sorotec's **335 × 225 × 100 mm / 6.1 kg**. | The Savx's own manual says **344 × 255 × 105 mm, 5.5 kg** net. Compare with that. | Manual §12 / §13. [CONFIRMED-primary] |
| C7 | R1 §5.2 and §5.3 (rocker "I O II"); R3 §12 ("Open the bottom cover and photograph the ports"). | The ports are on the **exposed bottom face**; nothing needs opening. The rocker reads "ON – O OFF". **Do not open the lower cover**: PV DC up to 500 V sits behind it. | Photo 6; nameplate. [CONFIRMED-primary] |
| C8 | R2 §9 step 2: use the RS485-port pinout "pin 3 = B, pin 4 = A, pin 2 = GND, +5 V on 1/7" (RP6000/RS3024). | **Dangerous for this unit.** Savx COM **pin 4 is +VCC** (dongle supply). Never put A/B or a GPIO on pin 4. Use only pins 1, 2 and 8. | Manual §2.3. [CONFIRMED-primary] |
| C9 | gemini-claims.md: QPIGS #15 = battery discharge current (and R1 called the list wrong **only for REVO**). | The list is wrong for **stock PI30 too**. 1-based #15 is "Battery voltage from SCC" (WW.WW) and #16 is "Battery discharge current" (PPPPP). Fields #1–#14 are right. | `PI30_HS_MS_MSX_RS232_Protocol_20140822…pdf` §2.7. [CONFIRMED-primary] |
| C10 | gemini-claims.md: RS232 "Pin 2 TX, Pin 3 RX, Pin 5 GND". | That is a **DB9** convention. On the Savx RJ45 the signals are on 1/2 with GND on 8. | Photo 6; Voltronic cable drawing. [CONFIRMED-primary] |
| C11 | gemini-claims.md: Modbus map "30001 PV1 Power …". | No source exists. Discard it (R1 agreed). Not seen in any SMG/SRNE/MUST/PowMr primary map either. | R3 primary maps. [CONFIRMED-primary absence] |
| C12 | R1 §4.2: QALL warning codes "00–08 and 21–26". | The Sorotec doc lists **00–07** and 21–26. "08 Discharge overcurrent" and "39 BMS Communication lost" are **mpp-solar additions**, not in the doc. | `PI30REVO.20Protocol.20V03--20201112.pdf` p.4; `pi30revo.py` lines 140–158. [CONFIRMED-primary] |
| C13 | R1 §4.2 (implicit): QALL fault codes equal the LCD fault codes. | They **do not**. QALL faults: 01 BUS overvoltage, 02 Inverter overvoltage, … 12 Battery over-voltage. The LCD and manual use Voltronic codes (01 fan locked, 02 over-temperature, 51 over-current, …). Store the raw code with a "source" tag. | REVO doc p.4 vs manual §5. [CONFIRMED-primary] |
| C14 | Sorotec REVO doc §4.3: the PSET example checksum is **F6**. | By the doc's own rule (sum + 1) it is **F7**. The doc's QALL (0x04) and PQSE (0x3C) reply examples and the `ACK`→**D0** example are consistent. This is an internal erratum, irrelevant to a read-only logger. | My computation (`scratchpad/v1/scripts/crc.py`). [CONFIRMED-primary] |
| C15 | Implicit in R1 (mpp-solar test data treated as truth). | mpp-solar's QALL test frame ends `\x03\n`, but sum + 1 over that payload is **0x04** (the Sorotec doc example sends `04 0D`). R2 already flagged a bad QMOD test frame. Treat library test frames as untrusted. **Accept `\r` or `\n` as terminator.** | `pi30revo.py` test_responses vs REVO doc §4.1. [CONFIRMED-primary] |
| C16 | R2 §3.2: SolarAssistant's Axpert GT page labels pin 1 RX / pin 2 TX "from the adapter side". | The page gives "Pin 1 RX, Pin 2 TX, Pin 8 GND" but **does not state a perspective**. | https://solar-assistant.io/help/inverters/growtech/Axpert-GT/rs232 [SECONDARY] |
| C17 | R1 §2.2: the F4 3000-24V listing's numeric block was "copied from the F3 2500". | Partly. Its MPPT range "30–450VDC" and Voc 500 V are the **F4** values (F3 is 60–450). Its 3000 W PV, 80 A solar / 60 A AC and 348 × 270 × 95 mm conflict with the nameplate and manual. | `r1dl/f4_3000.html` (re-parsed). [CONFIRMED-primary] |
| C18 | R3 §12: GPIO33–37 are used by octal PSRAM on R8 modules. | On the ESP32-S3-WROOM-1 the datasheet says "**IO35, IO36, and IO37** are connected to the Octal SPI PSRAM and are not available for other uses". IO33/34 are not brought out on the module. N16R8 = 16 MB **Quad** flash + 8 MB **Octal** PSRAM. The UART1 choice TX GPIO17 / RX GPIO18 is fine. | ESP32-S3-WROOM-1 datasheet (local `pdfs/wroom.txt` l.124–126, 691–692). [CONFIRMED-primary] |

---

## 4. Confirmed (load-bearing claims re-verified)

### 4.1 Physical layer and serial framing

| # | Claim | How verified | Source / tag |
|---|---|---|---|
| V1 | Voltronic PI30 serial format: **2400 baud, 1 start, 8 data, no parity, 1 stop**. | Read the "Communication format" table. | `PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf` §1, https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf (also present in P2015 and PMAX). [CONFIRMED-primary] |
| V2 | Sorotec REVO protocol: "**Baud rate: 2400**"; "CHK=The accumulated value of sent data+1, single byte". | Read the doc header. | `PI30REVO.20Protocol.20V03--20201112.pdf` p.1, https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf [CONFIRMED-primary] |
| V3 | Voltronic RJ45-to-DB9 cable: **RJ45 pin 1 → DB9 pin 2 "TXD"; pin 2 → DB9 pin 3 "RXD"; pin 8 → DB9 pin 5 "GND"**. The PC's DB9 pin 2 is the PC's receive, so **RJ45 pin 1 = device transmit**. The wire colours in that cable are white-orange, blue and white-green (not T568B). | Extracted the embedded drawing (`scratchpad/v1/dl/img/p14-000.png`). | Same PDF, p.2. [CONFIRMED-primary]. The PC-side reasoning is [INFERRED]. |
| V4 | Voltronic-OEM Tripp Lite APSWX labels the RJ45 "**1:RXD232, 2:TXD232, 8:GND**", the same labelling style as the Savx. This supports reading Savx "1:RXD" as "the PC receives on pin 1". | grep of the manual text. | https://tripplite.eaton.com/support/owners-manual/932564 (R2's copy). [CONFIRMED-primary text]; inference [INFERRED] |
| V5 | Community pinout for PIP-class RJ45: 1 TX, 2 RX, 4 "VCC 12V", 8 GND; "+12V on pin 4 or 7 depending on the model". | Read the README. | https://raw.githubusercontent.com/syssi/esphome-pipsolar/main/README.md [SECONDARY] (community; cites the Voltronic PDF) |
| V6 | ESP32 Arduino core supports UART line inversion: `begin(baud, config, rx, tx, bool invert)` plus `setRxInvert()` / `setTxInvert()`, implemented via `UART_SIGNAL_RXD_INV`/`TXD_INV`. | Read the header and HAL source. | https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/HardwareSerial.h and `esp32-hal-uart.c`. [CONFIRMED-primary] |
| V7 | ESP32-S3 strapping pins are GPIO0, 3, 45, 46; GPIO19/20 are USB D−/D+; IO35–37 belong to octal PSRAM on R8 modules. | Datasheet grep. | ESP32-S3-WROOM-1 datasheet. [CONFIRMED-primary] |

### 4.2 CRC / checksum algorithms

| # | Claim | How verified | Source / tag |
|---|---|---|---|
| V8 | PI30 CRC = CRC-16/XMODEM: poly 0x1021, init 0x0000, no reflection, no final XOR, **high byte first**. Then each CRC byte equal to **0x28, 0x0D or 0x0A** is incremented by 1. | Read three implementations: the ned-kelly `cal_crc_half()` nibble table `{0x0000,0x1021,…,0xf1ef}` with the 0x28/0x0d/0x0a bump; ESPHome `pipsolar_crc_()` = `crc16be()` + the same bump; mpp-solar `crcPI()`. My bitwise XMODEM equals the nibble table on all test vectors. | https://raw.githubusercontent.com/ned-kelly/docker-voltronic-homeassistant/master/sources/inverter-cli/inverter.cpp l.215–245; https://raw.githubusercontent.com/esphome/esphome/dev/esphome/components/pipsolar/pipsolar.cpp l.808–818. [CONFIRMED-primary] |
| V9 | mpp-solar's `crcPI()` **also bumps 0x00**; ESPHome and ned-kelly do not. Send with the 3-value rule; accept both on receive. | Read the source. | https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/protocol_helpers.py l.303–356. [CONFIRMED-primary] |
| V10 | REVO CHK = (sum of bytes + 1) & 0xFF, one byte, then CR, for **QALL / PQSE / PQSA / PSET**. QPI/QPIGS/QPIRI/QMOD/QLITH0 use the PI30 CRC. mpp-solar sends **PSET without CR**. | Read `pi30revo.py` `get_chk()` (= `crc8()` + 1) and `get_full_command()`; the doc examples recomputed (QALL request `2B` ✓; QALL reply `04` ✓; PQSE request `3A` ✓; PQSE reply `3C` ✓; "ACK" → `D0` ✓). | REVO doc §4; https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/pi30revo.py l.492–520. [CONFIRMED-primary] |
| V11 | Solar2MQTT sends **QALL with the PI30 CRC** (`QALL E8 FB 0D`) and accepts the reply with CHK ("crude fix for the qall chk thing"). It detects REVO when QALL has ≥ 18 fields, QPIGS ≥ 21 and QPIRI ≤ 21. So the firmware should try **both** QALL framings. | Read the source (R2's copy). | softwarecrash/Solar2MQTT `PI_Serial.cpp` l.154–160, 1134–1144, 1611–1614, 1670–1680, https://github.com/softwarecrash/Solar2MQTT [CONFIRMED-primary code]. Note: licence CC BY-NC-ND 4.0, README badge. |
| V12 | CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF, low byte first) reproduces every Modbus example in the SMG and SRNE docs, including R3's errata (`01 03 00 10 00 02` → **C5 CE**, not C0 CB; write frame needs byte count `02` → `BE 3A`). | My script. | §4.4. [CONFIRMED-primary] |

### 4.3 Message content

| # | Claim | How verified | Source / tag |
|---|---|---|---|
| V13 | `QPI` reply `(PI<NN>`, "30 for HS series". | Doc §2.1. | PI30 2014 PDF. [CONFIRMED-primary] |
| V14 | QPIGS field order (0-based): 0 grid V; 1 grid Hz; 2 out V; 3 out Hz; 4 out VA; 5 out W; 6 load %; 7 bus V; 8 batt V; 9 charge A; 10 capacity %; 11 heatsink (NTC A/D on 1–3 kVA HS/MS/MSX); 12 PV current; 13 PV V; 14 batt V from SCC; 15 discharge A; 16 status b7…b0. That is **17 fields** in P2014. P2015 adds 17 QQ (fan offset, 10 mV), 18 VV (EEPROM), 19 MMMMM "PV Charging power 1" (W), 20 b10b9b8, giving **21**. PMAX adds Y ZZ AAAA, giving **24**. | Read all three templates. | P2014 §2.7; P2015 §2.9; PMAX §2.9. [CONFIRMED-primary] |
| V15 | Status bits: b4 load on; b2 charging; b1 SCC charging; b0 AC charging. `110` = SCC, `101` = AC, `111` = both. | Doc text. | P2014 §2.7. [CONFIRMED-primary] |
| V16 | **REVO QPIGS** differs. Field 12 `EEEE`: "PV current generating power 0622 means 622W". Field 15 `PPPPP`: "changed to the amount of power generated on the day. The unit is WH". Field 4 (VA) and field 11 (heatsink) "Reserved"; tail `KK DD ddddd aaa` with `ddddd` = "PV power unit W". A real REVO VE II trace shows field 12 = `0248` at PV 355.0 V with field 19 = `00000`: the PV power sits in field 12. | Doc §1.2; mpp-solar discussion #78 trace. | REVO doc §1.2 [CONFIRMED-primary]; https://github.com/jblance/mpp-solar/discussions/78 [SECONDARY] |
| V17 | **REVO QALL** reply: 18 fields. In order: grid V, grid Hz, out V, out Hz, out W, load %, batt V, SOC %, charge A, discharge A, PV V, PV A, PV W, PV Wh today, PV kWh total, mode (B/L/S/F), warning code, fault code. Then CHK + CR. | Doc §1.4; example recomputed. | REVO doc §1.4, §4.1. [CONFIRMED-primary] |
| V18 | QMOD letters: P Power-on, S Standby, L Line, B Battery, F Fault, H Power-saving (P2014). REVO calls H "Sleep". | Doc tables. | P2014 §2.8; REVO §1.3. [CONFIRMED-primary] |
| V19 | QPIWS (P2014) is 32 characters a0…a31. Examples: a1 inverter fault, a5 LINE_FAIL, a9/a10/a11/a16 are "fault if a1=1, else warning", a12 battery low alarm, a22 battery open. In PMAX the **24 V model's fault code is in a31–a32**. | Doc tables. | P2014 §2.9; PMAX l.611–614. [CONFIRMED-primary] |
| V20 | LCD fault codes (Savx manual) = Voltronic fault-code table (P2014/PMAX: 01 fan locked, 02 over-temperature, 05 output short, 09 bus soft start, 51 over-current, 52, 53, 55, 57, 58; PGK 59 PV voltage high). | Compared the tables. | P2014 l.632–662; PMAX l.510–533; manual §5. [CONFIRMED-primary] |
| V21 | Modbus family serial constants as used in code: **SMG 9600 / slave 1**; **MUST PV18 19200 / slave 4**; **PowMr HVM 2400 / slave 5**; **SRNE "9,600, n, 8, 1", default address 1, "255 universal address", FC 03/06/10, ≤ 32 registers per request**. | Read the yaml/headers and the SRNE PDF text. | syssi `esp32-example.yaml` (`baud_rate: 9600`, `address: 0x01`); vladyspavlov `esp32-pv18.yaml` (`19200`, `0x04`); odya `inverter.yaml` (`2400`, `0x05`); Solar2MQTT `smg.h`/`must_pv_ph18.h`/`powmr.h`; SRNE V1.96 l.117–123. [CONFIRMED-primary] |
| V22 | Register spot-checks: SMG 201 = working mode, 215 = battery average voltage 0.1 V (Int); SRNE 0x0100 = SOC %, 0x0101 = battery V 0.1 V; MUST 25201 = work state, 25205 = battery V 0.1 V, 20000 = machine type high, 15205 = PV V 0.1 V. | Doc grep. | SMG V1.0.1 l.243/280; SRNE V1.96 l.519–520; MUST 1.4.15 l.62/100/104/286. [CONFIRMED-primary] |
| V23 | Sorotec 3.2 kW datasheet facts: surge 6400 VA, PV input 15 A, 1 MPPT, max AC charge 80 A, "Smart load management NO", "Standard:RS232,USB; CAN&RS485; Optional:WiFi,Bluetooth". | Read the spec images. | Sorotec product page images (R1 URLs). [CONFIRMED-primary] |
| V24 | dessmonitor README lists "Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi" as confirmed working. This needs **Sorotec's** WiFi dongle. | Raw README. | https://raw.githubusercontent.com/SilverFire/dessmonitor-homeassistant/main/README.md l.35–37 [SECONDARY, community list] |
| V25 | Eybond Wi-Fi Plug Pro: PN like "W0016250020617", "Initial Password:12345678". | PDF text. | https://www.inverter.com/images/uploaded/solar-inverter-charger-wifi-plug-pro.pdf [CONFIRMED-primary] |

### 4.4 Byte-exact frames, recomputed by me

All recomputed with `scratchpad/v1/scripts/crc.py`. ✓ means it matches the value R1/R2/R3 quoted, or the doc example where noted.

**PI30 requests** (ASCII + CRC-hi + CRC-lo + `0D`):

| Command | Bytes on the wire | Note |
|---|---|---|
| QPI | `51 50 49 BE AC 0D` | ✓ |
| QPIGS | `51 50 49 47 53 B7 A9 0D` | ✓ |
| QPIRI | `51 50 49 52 49 F8 54 0D` | ✓ |
| QMOD | `51 4D 4F 44 49 C1 0D` | ✓ |
| QPIWS | `51 50 49 57 53 B4 DA 0D` | ✓ |
| QFLAG | `51 46 4C 41 47 98 74 0D` | ✓ |
| QID | `51 49 44 D6 EA 0D` | ✓ |
| QSID | `51 53 49 44 BB 05 0D` | ✓ |
| QVFW | `51 56 46 57 62 99 0D` | ✓ |
| QVFW2 | `51 56 46 57 32 C3 F5 0D` | ✓ |
| QDI | `51 44 49 71 1B 0D` | ✓ |
| QMN | `51 4D 4E BB 64 0D` | ✓ |
| QGMN | `51 47 4D 4E 49 29 0D` | raw 0x4928, low byte 0x28 bumped ✓ |
| QBOOT | `51 42 4F 4F 54 0B 88 0D` | raw 0x0A88 bumped ✓ |
| QT | `51 54 27 FF 0D` | ✓ |
| QET | `51 45 54 81 B6 0D` | ✓ |
| Q1 | `51 31 1B FC 0D` | ✓ |
| QPIGS2 | `51 50 49 47 53 32 68 2D 0D` | ✓ |
| QPGS0 | `51 50 47 53 30 3F DA 0D` | ✓ |
| QLITH0 (CRC) | `51 4C 49 54 48 30 02 A4 0D` | ✓ |
| QLITH0 (CHK, as the doc writes it) | `51 4C 49 54 48 30 B3 0D` | |
| QED20261021 | `… 95 D0 0D` | ✓ |
| QLY2026 | `… BF 0E 0D` | raw 0xBF0D bumped ✓ |

**PI18:**

| Command | Bytes on the wire | Note |
|---|---|---|
| `^P005PI` | `5E 50 30 30 35 50 49 71 8B 0D` | ✓ |
| `^P007PIRI` | `… EE 38 0D` | |
| `^P005GS` | `… 58 14 0D` | |

**Expected PI30 reply trailers** (payload + CRC + `0D`):

| Reply | CRC bytes | Note |
|---|---|---|
| `(PI30` | `9A 0B` | ✓ |
| `(NAK` | `73 73` | ✓ |
| `(ACK` | `39 20` | ✓ |
| `(B` | `E7 C9` | ✓ |
| `(S` | `E5 D9` | ✓ |
| `(L` | `06 07` | |
| `(F` | `A7 4D` | |
| `(H` | `46 83` | |
| `(P` | `D5 BA` | |
| `(VERFW:00014.04` | `60 A8` | Manual's example version |
| `(VERFW2:00003.03` | `1D 64` | Manual's example version |

**REVO CHK requests:**

| Command | Bytes on the wire | Note |
|---|---|---|
| QALL | `51 41 4C 4C 2B 0D` | Matches the doc ✓ |
| PQSE | `50 51 53 45 3A 0D` | Matches the doc ✓ |
| PQSA | `50 51 53 41 36 0D` | ✓ |
| QALL (CRC variant, Solar2MQTT) | `51 41 4C 4C E8 FB 0D` | |

**Modbus RTU** (low CRC byte first). All match R3 ✓:

| Purpose | Frame |
|---|---|
| SMG, register 201 | `01 03 00 C9 00 01 54 34` |
| SMG, 201 × 34 | `01 03 00 C9 00 22 15 ED` |
| SMG, register 184 | `01 03 00 B8 00 01 04 2F` |
| SMG, register 171 | `01 03 00 AB 00 01 F5 EA` |
| SMG, register 643 | `01 03 02 83 00 01 74 5A` |
| SMG, serial 186 × 12 | `01 03 00 BA 00 0C 64 2A` |
| SMG, energy 443 × 3 | `01 03 01 BB 00 03 74 12` |
| SRNE, product type 0x000B | `01 03 00 0B 00 01 F5 C8` |
| SRNE, DC data 0x0100 × 3 | `01 03 01 00 00 03 04 37` |
| SRNE, state 0x0210 | `01 03 02 10 00 01 84 77` |
| SRNE, serial 0x0035 × 20 | `01 03 00 35 00 14 55 CB` |
| SRNE, universal address | `FF 03 01 00 00 03 11 E9` |
| Sumry/GES | `01 03 C7 38 00 02 78 B2` |
| MUST, model 20000 | `04 03 4E 20 00 02 D2 BC` |
| MUST, 25201 × 79 | `04 03 62 71 00 4F 4B C8` |
| MUST, 15201 × 21 | `04 03 3B 61 00 15 D8 AA` |
| PowMr, 4501 × 45 | `05 03 11 95 00 2D 91 43` |
| PowMr, 4546 × 16 | `05 03 11 C2 00 10 E1 42` |
| 8851 state | `88 51 00 03 00 00 00 00 4D 08` |
| 8851 config | `88 51 00 03 02 00 00 00 4C B0` |
| SMG wake-up | `01 AA 06 DE A2` |

---

## 5. Still unknown (only the user can determine these on site)

| # | Unknown | How to resolve it | Cost |
|---|---|---|---|
| U1 | **Which COM pin (1 or 2) is the inverter's TX.** The labels are perspective-ambiguous; the prior is pin 1 = inverter TX (V3, V4). | Receive-only idle-level test (§6.3). | No tools |
| U2 | Whether COM is **true RS232 (± levels)** or TTL-level. The manual says RS232. | §6.3: a steady LOW on the TX candidate (A = pin, B = GND) means a negative idle, so true RS232. | No tools |
| U3 | **COM pin-4 "+VCC" voltage** (5 V? 12 V?). | Not needed. **Never connect it.** Measure only if a multimeter appears. | Multimeter |
| U4 | PI30 **dialect**: stock (17/21-field), REVO (QALL), MAX (24/28-field), GK; and which optional commands NAK (QPIWS, QET, QPIGS2, Q1, QMN, QFLAG). | §6.4–§6.5 probe. | ESP32 |
| U5 | **Firmware versions** (LCD "U1 xx xx" / "U2 xx xx"). Useful as a cross-check against the QVFW/QVFW2 replies, and for future firmware-specific quirks. | Press UP/DOWN in the normal display (manual §4.5); photograph each screen. | No tools |
| U6 | Battery-type setting and code (program 05 shows AGM/FLd/USE/**LIb**) and the **QPIRI code** it maps to (P2014 knows only 0–2; PMAX uses 8 = "Lib"; Sorotec doc only 0–2). | Read program 05 on the LCD, then compare with QPIRI field 12. | No tools |
| U7 | Whether the battery link on BMS uses **RS485 or CAN**, and whether the inverter relays BMS data (REVO `QLITH0`, PI30 `QBMS`). | Probe `QLITH0` (read-only) once. Leave the BMS cable alone. | ESP32 |
| U8 | Whether a **Solar of Things (Siseli) WiFi collector** for this model exists and its price (an alternative to DIY). | Phone IPS Bazar, 01749 426 789 (R1). | Call |
| U9 | The exact factory (Sorotec or a supplier). | Optional: ask IPS Bazar / Sorotec, quoting "1.6KW/3.2KW, manual 614.C0556-02". | Email |
| U10 | Whether the RS485 module's **R0 120 Ω termination pad is open** (R5 reports open by default; shorting enables it). With it closed, the RS232 driver would idle into 120 Ω. | Look at the board. Leave R0 open. | Eyes |

---

## 6. Protocol discovery procedure (definitive decision tree for this unit)

### 6.0 Ground rules

**Port and wiring rules**
- **Only COM.** Leave the BMS cable plugged in at all times. The inverter is master on that bus. Unplugging it can raise "battery communication" warnings (REVO warning 39 per mpp-solar) and change lithium charging behaviour. [INFERRED]
- **Only RJ45 pins 1, 2 and 8 are ever connected.** Insulate pin 4 (+VCC) and pins 3, 5, 6 and 7 individually.
- On the cut LAN cable, read the colour order through the clear plug; do not assume T568A or T568B.
- **Never send setters.** Hard-code a whitelist of read commands; everything else is refused in firmware:
  - Allowed: `QPI QID QSID QVFW QVFW2 QVFW3 QMN QGMN QPIRI QPIGS QPIGS2 QMOD QPIWS QFLAG QDI Q1 QT QET QEDyyyymmdd QLT QLITH0 QALL PQSE PQSA ^P005PI`, and Modbus **FC 03 only**.
  - Explicitly refused: `P*` setters except the REVO **reads** `PQSE`/`PQSA`, so match by whitelist and not by prefix. This covers POP, PCP, PBT, PSDV, PCVV, PBFT, **PF**, **PSET**, PE/PD and PBATCD.
  - Also refused: `MCHGC`, `MUCHGC`, `F50`/`F60`, `V230`, `RTEY`/`RTDL`, `BTA`, `DAT`, Sorotec `~R`/`~S` calibration, PI18 `^S…`, Modbus FC 06/10, and 8851 command 0x0010.
  - Sources: P2014 §3; REVO doc §2 (`~R`/`~S`). [CONFIRMED-primary]

**Power and safety**
- Power the ESP32 **only from the USB wall charger**. Do not keep a laptop on the ESP's USB while it is wired to the inverter, unless the laptop runs on battery. Some users report Axpert RS232 boards burning when a mains-earthed PC was connected. [SECONDARY, R2 §3.7]
- **Do not open the lower cover.** PV DC up to 500 V is behind it. [CONFIRMED-primary nameplate]

### 6.1 Stage A: inspect (no tools)

1. Confirm the COM jack is empty and the silkscreen reads COM 1 RXD, 2 TXD, 8 GND. Already confirmed in photo 6. ✔
2. Record the LCD readouts by cycling UP/DOWN: input V/Hz, PV V, charging current and power, battery V, output V/Hz, load %, VA, W, discharge current, **U1 and U2 versions**.
3. Record the setting programs 01, 02, 05, 11, 16. These become ground truth for checking QPIRI and QPIGS.
4. Note what the LCD shows **at the moment of each probe**: PV W, output V, battery V. A probe reply must agree with the LCD to within rounding before it is trusted.
5. Make sure nothing else is plugged into COM: no dongle, no PC. Only one master is allowed on the line.

### 6.2 Stage B: bench self-test (ESP32 alone, inverter not connected)

1. **ESP UART loopback.** Jumper GPIO17 to GPIO18 and run `Serial1.begin(2400, SERIAL_8N1, 18, 17, true)` (both lines inverted). Send `QPI` + CRC and check it reads back byte-exact. Repeat with `invert=false`. This proves the build and the inversion settings.
2. **Module receive path.**
   - Wire the module: VCC ← 3V3, GND ← GND, **module TXD (DI) ← 3V3**, which holds the auto-direction driver off. Module RXD (RO) → GPIO18.
   - With A/B open, RO should read **HIGH**: fail-safe bias, idle. Record it.
   - Then short A to B with a jumper. RO is now undefined (HIGH, LOW or oscillating). This is just to see that the GPIO read works.
   - Module pin naming per R5 [SECONDARY]. Check R0 is open (U10).

### 6.3 Stage C: find the inverter-TX pin, receive-only, without a multimeter

Wiring for this stage (the ESP TX is **not** connected to the inverter):

| Module terminal | Goes to |
|---|---|
| B− | COM pin 8 (GND) |
| Module GND terminal (if present) | COM pin 8 |
| A+ | **Candidate pin**: first pin 1, then pin 2 |
| Module VCC / GND / RXD / TXD | ESP 3V3 / GND / GPIO18 / 3V3, as in 6.2 |

Firmware: sample GPIO18 every 10 ms for 10 s. Report the % of time LOW and the number of edges, on the USB log or a small web page.

```
Candidate pin 1 (A+ = pin1, B- = pin8):
 ├─ RO LOW ≥ 99 % of samples, ~0 edges  → pin 1 idles NEGATIVE → pin 1 = inverter TX (true RS232).
 │                                         Inverter RX = pin 2. → go to 6.4 with RX-invert = TX-invert = true.
 └─ RO HIGH (or noisy) → move A+ to pin 2 and repeat:
      ├─ RO LOW ≥ 99 % → pin 2 = inverter TX; inverter RX = pin 1. → 6.4 (both inverted).
      └─ neither pin idles LOW → the port does not idle negative → Branch F1 (TTL-level or wiring fault).
Any steady edges while nothing is being sent → something is transmitting unsolicited
  (unexpected on COM) → capture raw bytes at 2400/9600/19200 before sending anything.
```

Why this works [INFERRED from CONFIRMED-primary specs]:
- **RS232 polarity.** RS232 TX idles at mark = negative. A MAX3232 driver gives −5 to −5.4 V at 3 kΩ; a 5 V MAX232 gives more negative. An RS485 receiver outputs LOW when A − B ≤ −200 mV. So a TX pin gives a steady LOW.
- **An RX pin reads differently.** An RS232 receiver input is 3–7 kΩ to GND. The module's A-side fail-safe bias lifts it above B = GND, so RO reads HIGH (or undefined if the module has no bias).
- **Stress on the module.**
  - Typical RS485 transceivers tolerate about −7 to −8 V on A/B. The HW-0519 class has TVS diodes and polyfuses on A/B [SECONDARY, R5].
  - The inverter's RS232 driver is short-circuit-limited (MAX3232 I_OS ±35 mA typ).
  - Residual risk: a cheap transceiver could be over-stressed if the inverter's driver swings below about −8 V. Worst case is a dead RS485 module; the ESP32 is not at risk.
  - A **1 kΩ series resistor** in the A lead, if available locally, reduces the stress. It does not change the logic result.

### 6.4 Stage D: two-way link and probe sequence

**Wiring change from 6.3**
- Keep A+ on the inverter-TX pin and B−/GND on pin 8.
- Add **ESP GPIO17 → the other signal pin (inverter RX)**, directly. A 1 kΩ series resistor is optional.
- Never connect GPIO17 to the pin that tested as TX, and never to pin 4.

**UART setting**
- `Serial1.begin(2400, SERIAL_8N1, 18, 17, /*invert=*/true)`.
- RX is inverted because A = TX, B = GND gives an inverted RO. TX is inverted because an RS232 receiver needs idle ≈ 0 V and a start bit ≈ +3.3 V, which is above V_IT+ max 2.4 V (MAX3232 datasheet). [CONFIRMED-primary thresholds; INFERRED design]

**Rules for every probe**
- Flush RX before sending.
- Ignore bytes received during your own transmission (echo).
- Wait up to **1.5 s for the first byte** (2400 baud ≈ 4.17 ms per byte; a QPIGS reply of about 110 bytes takes about 0.46 s).
- Treat **200 ms of silence** after the last byte as the end of the frame. Leave at least 150 ms between commands.
- Log the raw hex of every reply.

| Step | Baud / mode | Send (hex) | Expected valid reply | Decision |
|---|---|---|---|---|
| D1 | 2400, inv/inv | QPI `51 50 49 BE AC 0D` | `28 50 49 33 30 9A 0B 0D` = `(PI30`. Alternatives: `(PI41`, `(PI17` + CRC; or `(NAK` = `28 4E 41 4B 73 73 0D` (also accept NAK without CRC). | `(PI30` → PI30 family, go to D2. `(NAK` → PI30-capable, go to D2. Silence → D1b. Garbage bytes → D1c. |
| D1b | 2400 | repeat QPI 3× (first command after idle can time out) | as D1 | Still silence → **Branch F2** (swap check) |
| D1c | 2400, RX non-inverted, TX inverted | QPI | as D1 | Valid now → the module polarity is reversed in the wiring; keep this setting |
| D2 | 2400 | QPIGS `51 50 49 47 53 B7 A9 0D` | `(` + **17 / 21 / 24** space-separated fields + CRC + CR (about 110 bytes for 21 fields) | Check field 2 (output V) against the LCD to within ±2 V, and field 8 (battery V) to within ±0.3 V |
| D3 | 2400 | QMOD `51 4D 4F 44 49 C1 0D` | `(L` `06 07 0D` / `(B` `E7 C9 0D` / `(S` `E5 D9 0D` / `(F` `A7 4D 0D` / `(H` `46 83 0D` / `(P` `D5 BA 0D` | Must agree with the LED/LCD state (AC/INV solid = L, flashing = B) |
| D4 | 2400 | QPIRI `51 50 49 52 49 F8 54 0D` | 25 (P2014) / 26–27 (GK) / 28 (MAX) fields; REVO samples have 25 | **Identity check:** field 5 = `3200`, field 7 = `24.0`, field 4 ≈ `13.9` |
| D5 | 2400 | QVFW `51 56 46 57 62 99 0D`, QVFW2 `51 56 46 57 32 C3 F5 0D` | `(VERFW:NNNNN.NN`, `(VERFW2:NNNNN.NN` | Must equal LCD U1/U2 (U5) |
| D6 | 2400 | QPIWS `51 50 49 57 53 B4 DA 0D` | `(` + 32 / 36 / 38 chars of 0/1, or NAK | NAK is allowed (REVO doc does not list QPIWS) |
| D7 | 2400 | QALL **CHK** `51 41 4C 4C 2B 0D`; if no reply or NAK, QALL **CRC** `51 41 4C 4C E8 FB 0D` | `(` + **18 fields** + 1 CHK byte (sum + 1, including `(`) + `0D` | Valid → **REVO dialect** (6.5). NAK → not REVO. |
| D8 | 2400 | QFLAG, QDI, QID, QMN, QET `51 45 54 81 B6 0D`, QPIGS2, Q1, QLITH0 (CRC, then CHK) | Data or NAK | Mark each command supported or unsupported; persist in NVS |
| D9 | 9600 | repeat D1–D3 (only if D1/D1b/D1c all failed) | as above | Some reports put PI30 at 9600 (R3: Daxtromn, WatchPower "COM port 9600") [SECONDARY] |

Go to 6.5 when D1–D3 succeed.

### 6.5 Stage E: classify the PI30 dialect (rules, in order)

1. **QALL valid** (18 fields with a good CHK) → **PI30REVO**.
   - QPIGS field 12 is **PV power (W)**, not current. Field 15 is the doc's "PV Wh today", which a real REVO trace showed as `00000`, so do not rely on it. Fields 4 and 11 are reserved.
   - Use **QALL as the primary poll**. It has SOC, charge and discharge A, PV V/A/W, **daily and total PV energy**, mode, and warning/fault codes from REVO's own tables.
2. **Sanity rule for QPIGS field 12, whatever the dialect.** The manual's max PV input current is **15 A**.
   - If field 12 > 15 while PV V > 30, then field 12 is power.
   - If field 12 × field 13 ≈ field 19 (± 20 %), it is current. [INFERRED rule from manual §11 + V16]
3. QPIRI ≥ 28 fields, or QFLAG contains `d` → **PI30MAX** (24-field QPIGS; energy QET/QED likely). This is Solar2MQTT's heuristic. [CONFIRMED-primary code]
4. QPIWS 36 chars, or QMOD returns `E`/`C`/`Y`/`D` → **PIP-GK/MK** variant. Fault code sits in QPIWS a31–a32. [CONFIRMED-primary PGK/PMAX]
5. Otherwise → **stock PI30**.
   - QPIGS has 17 or 21 fields (P2014/P2015 semantics).
   - Expect no energy counters, so integrate PV W and load W over time on the ESP or the server. R2: an EASun SP-3200 NAKs QET [SECONDARY].

**Store regardless of dialect:** the raw reply strings, the dialect tag, and the warning/fault codes **with their table source**. LCD codes, QPIWS bits and QALL codes use different numbering (C13).

### 6.6 Fallback branches

- **F1: no pin idles negative in 6.3 (possibly a TTL-level port, or a wiring fault).**
  1. Re-check the cut-cable colour mapping for pins 1, 2 and 8.
  2. Swap orientation (A+ → pin 8 GND, B− → candidate). A steady LOW now means the candidate idles **positive**, which looks like a TTL TX.
  3. Then run D1–D3 with **RX non-inverted** through the module, keeping its A/B orientation, and **TX non-inverted**.
  4. If the inverter side is 5 V logic, a 3.3 V TX may be marginal. Try anyway; it cannot damage a TTL input.
- **F2: the TX pin is identified but D1–D9 get silence.**
  1. Recheck that GPIO17 lands on the *other* pin.
  2. Try TX non-inverted while keeping RX inverted.
  3. Try the PI18 probe `^P005PI` = `5E 50 30 30 35 50 49 71 8B 0D` at 2400. A valid reply starts `^D00518`.
  4. Try the legacy `Q1\r` / `Q6\r` without CRC at 2400 (R2's RP/RS-series case) [SECONDARY].
- **F3: ASCII completely dead.** Last-resort Modbus probes on the same RS232 wiring, each preceded by 3 s of listening:
  1. PowMr style: 2400, `05 03 11 95 00 2D 91 43`. Expect `05 03 5A` + 90 bytes + CRC, then byte-swap the words. PowMr exposes Modbus and PI30 on one 2400 UART (Solar2MQTT comment) [CONFIRMED-primary code].
  2. SMG style: 9600, `01 03 00 C9 00 01 54 34`. Expect `01 03 02 00 0m CRC` with m 0–6. **Reject all-zero data.**
  3. SRNE: `01 03 00 0B 00 01 F5 C8` at 9600.
  4. MUST: `04 03 4E 20 00 02 D2 BC` at 19200.
  5. Steps 3–4 are RS485 families and very unlikely on an RS232 COM [INFERRED].
- **F4: COM behaves like RS485.** This contradicts the manual and silkscreen, so it is extremely unlikely. Only after F1–F3 fail:
  1. Connect A+ → pin 2 and B− → pin 1, per the BMS-style "1 = B, 2 = A" convention. Make the module's TXD the ESP TX (non-inverted).
  2. Repeat D1–D3 and F3 non-inverted.
  3. Swap A/B once.
- **F5: everything fails.** Stop transmitting. Fall back to (a) asking IPS Bazar for the Solar of Things collector or the protocol document, or (b) a MAX3232 later if purchases become possible.

### 6.7 Flow summary

```
A inspect (labels, LCD U1/U2, settings) ─► B ESP loopback + module self-test
   ─► C receive-only idle test: pin1 LOW? ─yes─► TX=pin1 ┐
                                 └no─► pin2 LOW? ─yes─► TX=pin2 ┤
                                              └no─► F1 (TTL?) ──┤
   ─► D 2400 8N1 inverted: QPI ─(PI30/NAK)─► QPIGS/QMOD/QPIRI/QVFW/QPIWS ─► QALL(CHK→CRC)
            │silence/garbage► D1b/D1c ─► D9 @9600 ─► F2 (TX polarity, ^P005PI, Q1/Q6) ─► F3 Modbus (FC03 only) ─► F4 ─► F5
   ─► E dialect: QALL ok → REVO | QPIRI≥28 or QFLAG 'd' → MAX | QPIWS36/QMOD E → GK | else stock PI30
   ─► persist {baud, inversion flags, TX pin, dialect, supported-command set} in NVS; re-probe only after N consecutive failures
```

---

## 7. Sources

**First-hand (user)**
- Photos: `images/1.jpg`, `images/2.jpg`, `images/3.jpg`, `images/6.jpg`.
- Savx F4 paper manual scan: `the paper manual (PDF scan)` (pages 1, 3, 6, 13–15, 16–20 read). Crops in `scratchpad/v1/`.

**Primary protocol documents and code**
- Voltronic PI30 HS/MS/MSX (2014): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf
- PI30 2015 Customer: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Communication-Protocol-20150924-Customer.pdf
- PI30MAX 2021: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30MAX.Communication.Protocol20210217.pdf
- Sorotec Revo RS232 protocol V02/V03: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf
- mpp-solar source:
  - https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/protocol_helpers.py
  - https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/pi30revo.py
  - https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/abstractprotocol.py
- ned-kelly CRC: https://raw.githubusercontent.com/ned-kelly/docker-voltronic-homeassistant/master/sources/inverter-cli/inverter.cpp
- ESPHome pipsolar: https://raw.githubusercontent.com/esphome/esphome/dev/esphome/components/pipsolar/pipsolar.cpp
- Solar2MQTT (`PI_Serial.cpp`, `smg.h`, `powmr.h`, `must_pv_ph18.h`): https://github.com/softwarecrash/Solar2MQTT
- arduino-esp32 UART inversion: https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/HardwareSerial.h and https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-uart.c
- TI MAX3232 datasheet (SLLS410O): https://www.ti.com/lit/ds/symlink/max3232.pdf
- ESP32-S3-WROOM-1 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- Modbus maps and configurations (via R3's links):
  - SMG V1.0.1 PDF and `esp32-example.yaml`: https://github.com/syssi/esphome-smg-ii
  - SRNE V1.96: https://github.com/syssi/esphome-smg-ii/blob/main/docs/SRNE.Solar.Charge.Inverter.MODBUS.Protocol1.96.pdf
  - MUST 1.4.15 and `esp32-pv18.yaml`: https://github.com/vladyspavlov/esphome-must-inverter
  - PowMr `inverter.yaml`: https://github.com/odya/esphome-powmr-hybrid-inverter

**OEM evidence**
- Sorotec REVO VM II PRO 1.6/3.2/4/6/8/10 kW: https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/
  - Spec images: https://www.soropower.com/wp-content/uploads/2025/01/0e9d9c96421b6fdc9130eb0ea6ad422.png and https://www.soropower.com/wp-content/uploads/2025/01/3ce086e72ac3589270e34a76a8232df.png
  - Bottom render: https://www.soropower.com/wp-content/uploads/2025/01/03-底部-768x768.webp
- Savx F3 "VM II PRO" datasheet: https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf
- IPS Bazar F4 3000-24V listing: https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/
- BestEn 3.5/5.5 kW manual ("614.C0572-00", "RevoMonitor"): https://deps.ua/downloads/Documentation/besten/autonomous-sm-1p-32kw-24v/BestEn_Autonomous-sm-1p-32kw-24v_EN.pdf
- Solar of Things (Shanghai Siseli New Energy), App Store: https://apps.apple.com/us/app/solar-of-things/id6445806075
- dessmonitor-homeassistant README: https://raw.githubusercontent.com/SilverFire/dessmonitor-homeassistant/main/README.md
- Eybond Wi-Fi Plug Pro guide: https://www.inverter.com/images/uploaded/solar-inverter-charger-wifi-plug-pro.pdf
- Tripp Lite APSWX manual (RJ45 "1:RXD232, 2:TXD232, 8:GND"): https://tripplite.eaton.com/support/owners-manual/932564

**Secondary**
- mpp-solar discussion #78 (REVO VE II traces, 2400 baud): https://github.com/jblance/mpp-solar/discussions/78
- DIY Solar Forum thread 34404 (REVO / VM II PRO 5.5 kW, PSET, "built on the base of all Voltronic or Axpert"): https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/
- syssi esphome-pipsolar README (RJ45 1 TX / 2 RX / 4 12 V / 8 GND): https://raw.githubusercontent.com/syssi/esphome-pipsolar/main/README.md
- SolarAssistant Axpert GT RS232 page: https://solar-assistant.io/help/inverters/growtech/Axpert-GT/rs232
