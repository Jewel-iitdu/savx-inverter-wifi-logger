# R1: Savx F4 3KW OEM identification and communication interface

Research date: 2026-10-21. Scope: identify who actually builds the "Savx F4 3KW (MPPT BASE)" inverter, which ports and protocols it has, and what the user should check physically before wiring the ESP32-S3.

Confidence tags:
- **[CONFIRMED-primary]**: read in an official document, datasheet or source code.
- **[SECONDARY]**: from a forum, blog, marketplace listing or search-engine summary.
- **[INFERRED]**: my own reasoning from the evidence.

Local copies of every downloaded artefact are in `scratchpad/research/r1dl/`. These include the PDFs, the Sorotec images and the forum HTML.

---

## 0. Summary

1. **The OEM is very probably SOROTEC (Shenzhen SORO Electronics Co., Ltd.).** The Savx F4 3KW is a red re-skin of the 2025-generation **SOROTEC REVO VM II PRO 3.2KW / 24V**. The model with the round black dial and orange ring is the one at https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/.
   - Eight nameplate numbers match Sorotec's 3200VA/3200W column exactly: 3200VA/3200W, 24VDC, 500VDC Voc, 3500W PV, 30~450VDC MPPT range, 100A max charge, -10 to 50 °C, 230VAC.
   - The front panel is identical: round dial, orange ring, LEDs labelled "AC/INV", "CHG" and "FAULT", and ESC/UP/DOWN/ENTER touch keys.
   - IPS Bazar's own Savx F3 datasheet PDF calls the product "**REVO VM II PRO** series", which is Sorotec's product-line name.
   - Overall: [INFERRED, very high confidence, built on CONFIRMED-primary matches].
2. **Sorotec's REVO family is Voltronic/Axpert-derived.** It speaks a Sorotec dialect of the Voltronic **PI30** ASCII protocol: the *"Revo RS232 Communication Protocol V02/V03"*. The baud rate is **2400**. Commands are `QPIGS`, `QPIRI`, `QMOD` and `QPI` with the PI30 CRC, plus Sorotec-specific `QALL`, `PQSE`, `PSET`, `PQSA` and `QLITH0`, some with a sum+1 "CHK" byte.
   - The protocol is [CONFIRMED-primary], from Sorotec's protocol document and the mpp-solar `pi30revo.py` source.
   - It is proven on older REVO II, VE II and VM II units [SECONDARY]. It is **not yet proven on the 2025 round-dial generation** [INFERRED].
3. **Ports per Sorotec's 2025 datasheet:** *"Standard: RS232, USB; CAN&RS485; Optional: WiFi, Bluetooth"* [CONFIRMED-primary]. The BMS port is *"Reserved communication port (RS485, CAN) for BMS"*.
   - Sorotec's bottom-view render shows **two RJ45 jacks side by side** next to a rocker switch [CONFIRMED-primary image].
   - The orchestrator's user-constraints note reports the user's unit has two RJ45 ports, labelled "**RS485**" (free) and "**BMS/CAN**" (used by the battery). That fits the render [SECONDARY: relayed, not verified by me].
4. **Monitoring app:** Sorotec's WiFi variants report to the Eybond **SmartESS** app (iOS App Store id1334656760; web portal dessmonitor.com).
   - The open-source dessmonitor-homeassistant project lists "*Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi*" as confirmed working [SECONDARY, from a primary README].
   - The Savx F4 front panel has **no "WiFi" or "BMS" logo**, while the Sorotec original has both. **No Savx or IPS Bazar WiFi accessory or app is listed anywhere** on the IPS Bazar store, its WooCommerce API, its media library or BME [CONFIRMED-primary, by absence].
5. **Hardware implication for the plan:** the user bought an **RS485** to TTL module. If the free port labelled "RS485" is the monitoring port, that module is the right tool.
   - The protocol on that RS485 port is unknown. It could be Sorotec PI30REVO ASCII at 2400 baud, Modbus RTU, or the inverter acting as a BMS master that polls a battery. The firmware must probe for it [INFERRED].
   - If the only monitoring port turned out to be true RS232 (±12 V levels), the RS485 module could not talk to it. A MAX3232 would be needed, and the user cannot buy one.

---

## 1. What the user's photos show (first-hand reading)

### 1.1 Nameplate (photo 2.jpg), transcribed [CONFIRMED-primary, photo]

| Field | Value on label |
|---|---|
| Title | SOLAR INVERTER |
| Model Name | SAVX F4 SERIES (2025) |
| Colour | Red |
| Operating temperature | -10 °C to 50 °C |
| Serial barcode | 250514-0315130023 (prefix looks like a YYMMDD date code, 2025-05-14 [INFERRED]) |
| Second sticker | CFAF09809 (meaning unknown; possibly a factory or PCB serial [INFERRED]) |
| **Inverter Mode** | Rated Power 3200VA/3200W; DC Input 24VDC, 145A; Power Factor 1.0; AC Output 230VAC, 50Hz/60Hz, 13.9A, 1Φ |
| **AC Charge Mode** | AC Input 230VAC, 50Hz/60Hz, 23.3A, 1Φ; DC Output 27VDC, Max.100A, Default 30A; AC Output 230VAC, 50Hz/60Hz, 13.9A, 1Φ |
| **Solar Charge Mode** | Max. Power 3500W; Nominal PV Voltage 260VDC; Operating Voltage Range 30-450VDC; Max. Solar Voltage (VOC) 500VDC |
| Footer | MADE IN CHINA, BRANDS OF IPS BAZAR, CE mark, QR code |

Sanity checks [INFERRED]:
- 3200 W / 230 V = 13.9 A, which matches the label.
- 3200 W / 24 V / about 0.92 efficiency ≈ 145 A, which matches the label.
- "Max.100A" in AC-charge mode most likely means the *total* maximum charge current (Sorotec lists 100 A max charge but **80 A** max AC charge for this size; see section 3).

### 1.2 Front panel (photos 1.jpg and 3.jpg) [CONFIRMED-primary, photo]
- Red powder-coated case with a large round black dial, an orange outer ring and a blue monochrome segment LCD in the centre.
- Silk-screened "Savx SOLAR INVERTER", "MPPT BASE", and "F4 / 3KW" on the lower wiring cover.
- Three LEDs labelled **"AC/INV"** (green, lit), **"CHG"** and **"FAULT"** (with a triangle symbol).
- Four capacitive keys: **ESC** (with a curved-arrow icon), **UP**, **DOWN** and **ENTER**.
- LCD layout:
  - "INPUT" field top-left, showing the "PV" tag and "1 W".
  - "OUTPUT" field top-right, showing "231 V".
  - A "BYPASS" tag over a dashed AC-to-load path.
  - An AC sine icon, then the inverter icon, then a battery icon with "CHARGING" and an "Li" tag.
  - A bulb/load icon with a wedge bar marked 25% to 100%.
- No WiFi or Bluetooth icon is visible on the LCD in the photo.
- The ports are not visible in any photo; they sit on the bottom face.

---

## 2. Seller-side research (Savx / IPS Bazar / BME / YouTube)

### 2.1 Savx is IPS Bazar's house brand
- The nameplate says "BRANDS OF IPS BAZAR" [CONFIRMED-primary, photo].
- IPS Bazar's WooCommerce Store API lists every Savx product. The query was `https://ipsbazarbd.com/wp-json/wc/store/products?search=savx&per_page=50` [CONFIRMED-primary].
- Hotline printed on IPS Bazar's product images: **01749 426 789** [CONFIRMED-primary, image `wp-content/uploads/2026/02/Savx-f4-Solar-Inverter.jpg`].

### 2.2 Savx inverter listings found

| Listing (URL) | Name as listed | Key specs as listed | Comms as listed | Price (BDT) |
|---|---|---|---|---|
| https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/ | API title "Savx Solar Inverter F3 3000-24V( MPPT BASE)"; short description says "**Savx \| F4 Series**"; images show the red round-dial "F4 3KW-24V (MPPT BASE)" | 3000VA/3000W; surge 6000VA; 24VDC; "Built-in 100A MPPT \| Max PV Power: 3000W \| MPPT Range: 30–450VDC \| Max PV Open Circuit Voltage: 500VDC"; max solar charge 80A; max AC charge 60A; 348 × 270 × 95 mm; 5.6 kg; "Smart LCD Touch Display" | "**USB / RS232 \| Optional: CAN / RS485**" | 35,000 (regular 50,000) |
| https://ipsbazarbd.com/product/savx-f%e0%a7%aa-1500-12v-solar-inverter-mppt-base/ (the slug uses the Bengali digit ৪) | Savx F4 1500-12V Solar Inverter (MPPT BASE); image shows the same round-dial red design | 1500VA/1500W; 12VDC; 80A MPPT; 2000W PV; 60–450VDC; 500VDC | "USB / RS232 (Optional: CAN & RS485)" | 30,400 |
| https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/ | Savx F3 3500-24V | 3500W; 4500W PV; 60–450VDC; 500VDC; 100A solar; 80A AC; 400 × 300 × 115 mm; 8.2 kg | "USB and RS232 (standard); CAN and RS485 (optional)" | 38,000 |
| https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/ | Savx F3 2500-24V | 2500W; 3000W PV; 60–450VDC; 500VDC | same | 33,000 |
| https://bme.com.bd/savx-f3-3500-24v-mppt-base-solar-inverter (also F3 2500 and F3 5500) | Savx F3 only; BME lists no F4 | generic bullets only | no ports listed | 41,950 |

All rows are [CONFIRMED-primary] for what the seller *claims*. Points to note:
- The F4 3000-24V listing's numeric block is copied from the F3 2500 (same dimensions and charge currents). It **contradicts the user's nameplate** (3200 W, 3500 W PV). Treat the listing specs as unreliable [INFERRED].
- **Warranty** (all Savx inverter listings): "2-Year Full Motherboard Warranty (Motherboard + Display + Control Card) | 5-Year Service Warranty | Lightning Protection Warranty (with proper earthing & safety tools)" [CONFIRMED-primary, F4 3000 listing].
- **WiFi or monitoring:** no Savx listing mentions a WiFi module, a monitoring app or a dongle. No WiFi accessory product exists in the store API (all roughly 220 products were enumerated) [CONFIRMED-primary, by absence].
- **Manuals:** the WordPress media library (`/wp-json/wp/v2/media?search=savx`) holds **no inverter manual**. It does hold one inverter datasheet, `https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf`, covered in 2.3. The Savx battery PDF `Savx-F2-100Ah-24V-Lithium-Battery.pdf` lists "BMS: Smart BMS and Display" but no communication port [CONFIRMED-primary].

### 2.3 Key find: the Savx F3 datasheet is a Sorotec "REVO VM II PRO" datasheet
`SAVX-2025-01-08.pdf` is 2 pages, made in Illustrator and dated 2025-01-08 +06. It is titled **"F3 VM II PRO, Hybrid Energy Storage Inverter, series 1.5/2.5/3.5/5.5kW"**. Its feature text says verbatim:
- "**REVO VM II PRO series is suitable for off-grid applications.**"
- "Reserved communication port(RS485,CAN)for BMS"
- "With touch buttons"
- Spec row: "DISPLAY AND COMMUNICATION … Standard:USB/RS232；Optional：CAN&RS485"

[CONFIRMED-primary] The page layout is the same template as Sorotec's own datasheets: house-at-night hero photo, four feature icons, the "With battery connected" diagram and the "Schematic diagram". Compare `https://www.soropower.com/wp-content/uploads/2026/06/REVO-VM-II-PRO-US-2.2KW.pdf` [CONFIRMED-primary]. "REVO" is Sorotec's product-line trademark (section 3). This links Savx's supplier to Sorotec [INFERRED, high].

### 2.4 YouTube
- The video at https://www.youtube.com/watch?v=tdCSpWiejBQ is titled "Savx সোলার ইনভার্টারে রয়েছে ১০% ছাড় । Savx Solar Inverter Price in BD || Solar inverter bd", from the channel **"IPS Bazar - Bangladesh"** (@IpsBazar). Source: YouTube oEmbed API [CONFIRMED-primary].
- The description could not be read: youtube.com redirected to a Google "sorry" CAPTCHA page, which I did not attempt to bypass. It is a price-promotion video, not a technical one.
- No Facebook, Daraz or review pages for "Savx F4" were found by search [SECONDARY, negative result].

---

## 3. OEM identification

### 3.1 Primary candidate: SOROTEC REVO VM II PRO 3.2KW-24V (2025 round-dial generation)

Source: https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/. The spec table is published as images:
- `…/uploads/2025/01/0e9d9c96421b6fdc9130eb0ea6ad422.png` (part 1)
- `…/uploads/2025/01/3ce086e72ac3589270e34a76a8232df.png` (part 2)

Product photos:
- Front: `…/uploads/2025/01/01-正面-1-768x768.webp`
- Bottom: `…/uploads/2025/01/03-底部-768x768.webp`

All are [CONFIRMED-primary]. I read the images directly.

| Parameter | Savx F4 nameplate | Sorotec REVO VM II PRO, **3200VA/3200W** column | Match |
|---|---|---|---|
| Rated power | 3200VA/3200W | 3200VA/3200W | exact |
| Battery voltage | 24VDC | 24VDC | exact |
| Max PV open-circuit voltage | 500VDC | "Maximum PV Array Open Voltage(V) 500VDC" | exact |
| Max PV power | 3500W | "Maximum PV Array Power 3500W" | exact |
| MPPT / operating range | 30-450VDC | "MPPT voltage range(V) 30~450VDC" (1.6 and 3.2 kW; 60~450 for 4 and 6 kW) | exact |
| Max charge current | "Max.100A" | "Maximum charge current 100A"; "Maximum solar charge current 100A"; "Maximum AC charge current 80A" | consistent (see 1.1) |
| AC output | 230VAC 50/60Hz | 230VAC; 50/60Hz; regulation 230VAC ± 5% | exact |
| Operating temperature | -10 to 50 °C | "-10°C ~ 50°C" | exact |
| PF | 1.0 | VA = W rating | consistent |
| Front design | round black dial, orange ring, LCD centre, LEDs AC/INV, CHG, FAULT, keys ESC/UP/DOWN/ENTER | identical; Sorotec adds a white body, an orange stripe and "BMS"/"WiFi" logos | identical layout |
| LCD layout | INPUT/OUTPUT digit fields, BYPASS path, load wedge | same, from Sorotec renders | same |
| Communication (datasheet) | (seller: USB/RS232, optional CAN/RS485) | "**Standard:RS232,USB; CAN&RS485; Optional:WiFi,Bluetooth**" | consistent |
| Size / weight | not on label | 335 × 225 × 100 mm (listed as "DxWxH"), 6.1 kg | **user can verify by measuring** |

Further Sorotec facts for the 3.2 kW model [CONFIRMED-primary, same tables]:
- Surge 6400VA.
- Max PV input current 15A, one MPPT tracker.
- Battery type: "Lithium/Lead-acid".
- Smart load management: "NO" for 1.6 and 3.2 kW.
- On-grid: "Yes (optional)".
- Safety: EN/IEC62109-1 and -2.
- The diagram says "Without battery connected (4-6kw)", so battery-less operation is advertised only for the 4–6 kW models.

Range mapping [INFERRED]:
- Savx F4 1500-12V corresponds to Sorotec 1600VA/12V (same round-dial design in IPS Bazar's F4 1500 image).
- Savx F4 3KW-24V corresponds to Sorotec 3200VA/24V.
- Savx F3, an older design, is described with Sorotec "REVO VM II PRO" text, so IPS Bazar appears to source both generations through the Sorotec ecosystem.

Who Sorotec is [CONFIRMED-primary, site footer]:
- "SOROTEC Company specializes in the research, development, manufacture and sale of solar energy products."
- Office: Unit 4N, Building 2, Quanzhi Science and Technology Innovation Park, Song'an Road, Shajing Street, Bao'an District, Shenzhen.
- Factory: Building 7, No. 26 Keyuan Road, Tangxia Town, Dongguan.
- Contact: ella@soroups.com, +86-13510865777.
- The page also links `mailto:sa@mustpower.com`, which suggests a commercial link to MUST [INFERRED, weak].

Forum evidence on the REVO family [SECONDARY, DIY Solar Forum, read via Wayback]:
- User "Frank in Thailand" says "Sorotec is the manufacturer of Revo II" and "EASun might be OEM products from Sorotec".
  - https://diysolarforum.com/threads/sorotec-revo-ii-factory-password.35132/
  - https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/
- User "chladekj", owner of a "new REVO VM II PRO 5,5kW", says "it is built on the base of all Voltronic or Axpert inverters": https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/

### 3.2 Ranked OEM candidates

| Rank | Candidate | Evidence | Confidence |
|---|---|---|---|
| 1 | **SOROTEC REVO VM II PRO 3.2KW-24V (2025 round-dial)**, Shenzhen/Dongguan | All 8 nameplate numbers match the 3.2 kW column; identical round-dial front; same LED and key labels; IPS Bazar's Savx datasheet literally says "REVO VM II PRO"; same datasheet template; Sorotec WiFi variant works with SmartESS | **Very high** [INFERRED from CONFIRMED-primary matches] |
| 2 | Another Guangdong contract factory that builds this chassis for Sorotec and sells it to others | Not excluded: Chinese white-label chains are common, and Sorotec states it manufactures but resells widely. No second brand using this exact round-dial chassis was found. | Low [INFERRED] |
| 3 | Voltronic Power (Axpert) as the *architecture* origin | Forum says REVO is "built on the base of all Voltronic or Axpert inverters"; protocol is a PI30 dialect | Medium for protocol lineage; not the maker [SECONDARY] |
| Ruled out | **Voltronic Axpert VM II Premium 3K-24V** | Datasheet: max PV Voc 450 VDC for the 3K-24V column, 3000 W PV, "RS232/RS485, optional WiFi" (bimblesolar PDF, `https://www.bimblesolar.com/docs/Axpert_VM II Premium datasheet.pdf`) vs 500 V / 3500 W on the label | [CONFIRMED-primary mismatch] |
| Ruled out | **MUST PV1800 PRO (2–6.2 kW)** | "up to 450VDC PV Voc"; "Support RS485 and CAN port for lithium battery communication" (https://www.mustpower.com/product/pv1800-pro-series-26-2kw/) vs 500 V | [CONFIRMED-primary mismatch] |
| Ruled out | **PowMr POW-HVM3.2H-24V-N** | Listing summary: 3000 W rated, "30-400V DC input range", 3000 W PV (powmr.com listing via search; the direct fetch returned 403) | [SECONDARY mismatch] |
| Ruled out | Crown Xavier 3KW 24V (sold by IPS Bazar) | "Operating VDC 60~400", 50A MPPT; black body with a red crown graphic (https://ipsbazarbd.com/product/crown-xavier-3kw-24v-solar-inverter/) | [CONFIRMED-primary mismatch] |
| Ruled out | SAKO Sunon Pro 3.5kW (IPS Bazar) | "MPPT Volt Range: 120~450VDC", "MAX PV Array Open Circuit Voltage: 450V", "DC Input: 24VDC, 162A"; white body (https://ipsbazarbd.com/product/sunonpro-3-5kw-off-grid-solar-inverter/) | [CONFIRMED-primary mismatch] |
| Ruled out | Hinvert HMK-plus 3.5kW | 3500W / 4000W PV / 120A; dark-blue body (IPS Bazar listing) | [CONFIRMED-primary mismatch] |
| Not individually checked | Anenji, EASUN ISolar, SRNE HF/HESP/ASP, Datouboss, SUMRY, Y&H, Daxtromn, Felicity, Growatt SPF, Kstar, and Pakistani rebrands (Inverex, Fronus, Tesla, Crown, Knox) | Not needed: the Sorotec match is exact on design and all numbers. Search found no other 3200 W / 500 V / 3500 W / 30–450 V product. Anenji 4000W-24V lists "60-500VDC" (eBay.de, SECONDARY), which does not match. | n/a |

Note on the label format [INFERRED]: the "Inverter Mode / AC Charge Mode / Solar Charge Mode" layout is a generic Chinese factory template. SAKO's Sunon Pro label uses the same structure ("Inverter Mode … DC Input : 24VDC, 162A"), so the layout alone does not identify the OEM. The numbers and the front design do.

---

## 4. Ports and protocols (most likely)

### 4.1 What the 2025 Sorotec chassis exposes

| Port (expected) | Evidence | Likely protocol / purpose | Confidence |
|---|---|---|---|
| RJ45 #1, "RS232" in Sorotec datasheets, possibly silk-screened "RS485" on the user's unit (per the constraints note) | Datasheet "Standard:RS232,USB; CAN&RS485"; bottom render shows 2 RJ45 jacks | PC or WiFi-dongle monitoring port. On REVO units this has been the PI30REVO ASCII protocol at 2400 baud. If physically RS485 on this generation, the framing could still be PI30REVO or something else. | RS232 vs RS485: **unknown for this unit**. Protocol PI30REVO: medium [INFERRED] |
| RJ45 #2, "BMS" / "BMS/CAN" | "Reserved communication port(RS485,CAN)for BMS" (Sorotec and Savx datasheets) | Inverter-to-lithium-battery link. The inverter is the master and polls the battery BMS. Not a monitoring port. | [CONFIRMED-primary] purpose |
| USB | Datasheet "Standard: … USB" | Probably a USB-serial bridge carrying the same protocol as the RS232 port. Not visible in the render. | [INFERRED], low |
| WiFi (built in or optional) | "Built-in Wi-Fi for mobile monitoring" (Sorotec product page); "Optional:WiFi,Bluetooth" (datasheet). The Sorotec front shows a WiFi logo; the **Savx front does not**. | Eybond datalogger reporting to the SmartESS / DessMonitor cloud | WiFi present on the Savx: **unknown, probably absent** [INFERRED] |
| Rocker switch "I O II" | Bottom render | Power switch | [CONFIRMED-primary image] |
| Dry contact / parallel | Not mentioned for the 3.2 kW model; "Smart load management: NO" for 3.2 kW | Probably none | [INFERRED] |

### 4.2 Sorotec "Revo RS232 Communication Protocol" (V02, revision 2020-11-12)
- Source PDF: `https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf`.
- Its header reads "SOROTEC: Revo RS232 communication Protocol, Version:V02 – Google Translated". The revision notes end with "2020-11-12 PQSE PSET adds parallel and high-voltage recovery point information".
- Everything in this subsection is [CONFIRMED-primary] unless tagged otherwise.
- **"Baud rate: 2400"**. The 8N1 framing comes from the parent Voltronic PI30 spec: "Baud rate 2400, Start bit 1, Data bit 8, Parity N, Stop bit 1" (`PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf`, same repo) [CONFIRMED-primary for Voltronic; INFERRED for Sorotec].
- **Two integrity schemes:**
  - **CRC** (Voltronic PI30 CRC-16/XMODEM-style, with byte escaping of 0x28, 0x0D, 0x0A and 0x00) for `QPIRI`, `QPIGS`, `QMOD`, `QPI` and `QLITH0`. The doc writes `QLITH0<CHK>`, but mpp-solar uses CRC for it, so try both.
  - **CHK**, defined as "CHK=The accumulated value of sent data+1, single byte", for `QALL`, `PQSE`, `PSET` and `PQSA`.
- Commands documented:
  - `QPIRI`: rating information.
  - `QPIGS`: general status.
  - `QMOD`: mode (P, S, L, B, F, H).
  - `QALL`: Sorotec all-in-one status.
  - `PQSE` / `PSET`: read and write configuration.
  - `PQSA`: power-flow state.
  - `QLITH0`: BMS data relayed by the inverter.
  - `~R`, `~S`: calibration commands. **Never send these.**

Exact command bytes. I computed them with mpp-solar's `crcPI()` and `crc8P1()` (`mppsolar/protocols/protocol_helpers.py`). The QALL and PQSE results match the worked examples in Sorotec's doc byte for byte [CONFIRMED-primary].

| Command | Bytes on the wire (hex) |
|---|---|
| QPI | `51 50 49 BE AC 0D` |
| QPIGS | `51 50 49 47 53 B7 A9 0D` |
| QPIRI | `51 50 49 52 49 F8 54 0D` |
| QMOD | `51 4D 4F 44 49 C1 0D` |
| QPIWS (Voltronic, may NAK) | `51 50 49 57 53 B4 DA 0D` |
| QID (Voltronic, may NAK) | `51 49 44 D6 EA 0D` |
| QVFW (Voltronic, may NAK) | `51 56 46 57 62 99 0D` |
| QLITH0 (CRC variant) | `51 4C 49 54 48 30 02 A4 0D` |
| **QALL** (CHK) | `51 41 4C 4C 2B 0D` (doc: "51 41 4C 4C 2B 0D") |
| **PQSE** (CHK) | `50 51 53 45 3A 0D` (doc: "50 51 53 45 3A 0D") |
| PQSA (CHK) | `50 51 53 41 36 0D` |

**QALL response**, the best single poll for a logger. Doc text: "(BBB CC.C DDD EE.E FFFF GGG HH.H III JJJ KKK LLL MM.M NNNN OOOOOO PPPPPP Q KK SS<CHK><cr>". Fields in order:

1. Grid voltage (V)
2. Grid frequency (Hz)
3. AC output voltage (V)
4. AC output frequency (Hz)
5. AC output power (W)
6. Output load (%)
7. Battery voltage (V)
8. Battery SOC (%)
9. Battery charging current (A)
10. Battery discharge current (A)
11. PV voltage (V)
12. PV current (A)
13. PV power (W)
14. PV energy today (Wh)
15. PV total energy (kWh)
16. Mode (B/L/S/F)
17. Warning code
18. Fault code

The doc's example reply is `(000 00.0 000 00.0 0000 000 00.0 000 000 000 000 00.0 0000 000000 000000 S 00 00`. Warning codes 00–08 and 21–26 and fault codes 01–12 are tabulated in the doc. mpp-solar adds warning 39 "BMS Communication lost" and faults 13–24 (`pi30revo.py`).

**QPIGS on REVO differs from stock Voltronic** [CONFIRMED-primary, doc]:
- Field 13 `EEEE` was "changed from 'PV current' to 'PV power'".
- Field 16 `PPPPP` is "the amount of power generated on the day. The unit is WH".
- Heat-sink temperature is "reserved".
- Two more fields follow the status bits; mpp-solar names them `RSV1`/`RSV2`. The doc's 19th field (`ddddd`) is labelled "PV power unit W", which mpp-solar names "PV Input Power".

The unsourced "Gemini" QPIGS field list in `scratchpad/research/gemini-claims.md` (field 13 = PV current, field 15 = battery discharge current) is **wrong for REVO firmware** [INFERRED from the primary doc].

Field reports [SECONDARY]:
- jblance/mpp-solar Discussion #78 (https://github.com/jblance/mpp-solar/discussions/78): on a Sorotec REVO II / iGrid VE II at 2400 baud, plain PI30 mislabels PV fields. Protocol `PI30REVO`, added in v0.7.47, works with `QALL`, `QPIGS`, `QPIRI`, `QMOD`, `PQSE` and `QPI`. Sample QALL reply: `(223 49.9 223 49.9 1962 035 00.1 000 013 000 355 00.3 0106 018859 000056 L 00 00`.
- DIY Solar Forum (thread 34404): reading works over RS232 on Revo-E and on a "new REVO VM II PRO 5,5kW". **Setting writes return NAK.** Sorotec confirmed that firmware only allows changes from the panel, while "PSET works if you leave <CR>/0Dhex at the end". For a read-only logger, this is irrelevant.
- syssi/esphome-pipsolar issue #169: a Sorotec 8 kW unit is read by an ESP32 at "Baud Rate: 2400 baud, Data Bits: 8, Parity: NONE, Stop bits: 1" using the PI30 component (some fields wrong; a two-MPPT branch was suggested).
- GitHub community discussion #49529: another user refers to a "Sorotec REVO VM … Protocol V02" as "similar to Modbus ASCII". The V02 doc read above is actually PI30-style ASCII with CRC/CHK. Read that comment as loose wording [INFERRED].
- **No source documents the protocol for the 2025 round-dial REVO VM II PRO specifically.** Assume PI30REVO first and verify on the wire [INFERRED].

### 4.3 RJ45 pinout conventions (verify against the paper manual)
- Voltronic / PIP RS232-on-RJ45 convention (syssi/esphome-pipsolar README, which cites the Voltronic HS_MS_MSX RS232 protocol PDF) [CONFIRMED-primary for Voltronic; applicability to Sorotec unknown]:
  - Pin 1 = TX (from the inverter).
  - Pin 2 = RX.
  - Pin 8 = GND.
  - "+12V on pin 4 or 7 depending on the model".
  - T568B colours: pin 1 white-orange, pin 2 orange, pin 4 blue, pin 8 brown.
- The "Gemini" claim (RS232 TX pin 2, RX pin 3, GND pin 5) is the **DB9** convention, not RJ45. Discard it [INFERRED].
- **No source gives the Sorotec RS485 or BMS RJ45 pinout.** RJ45 RS485 pin assignments vary by vendor (pairs such as 1/2, 3/6 or 7/8). Do not guess: take them from the paper manual or ask IPS Bazar or Sorotec [INFERRED].

### 4.4 WiFi, app and cloud (the "existing app" option)
- **SmartESS** (Shenzhen Eybond) for iOS: App Store id1334656760, free, "Requires iOS 13.0 or later", version 3.56.0 dated 24 Aug [CONFIRMED-primary, App Store page].
- dessmonitor-homeassistant README (https://github.com/SilverFire/dessmonitor-homeassistant), "Inverters confirmed working" [SECONDARY, community-confirmed list]:
  - "Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi"
  - "Sorotec REVO VM II PRO 3.5kW/24V Wi-Fi"
  - Also PowMr, EG4, Anenji, EASUN and MuscleGrid units.
- The same README shows that data is available at `https://web.dessmonitor.com/public/?…&action=querySPDeviceLastData&…`, with a long-lived token and fields such as `gd_grid_voltage`, `pv_voltage1`, `bt_battery_capacity` and `bc_load_active_power`. This is useful for an app-plus-cloud fallback path.
- Eybond **Wi-Fi Plug Pro** quick guide (https://www.inverter.com/images/uploaded/solar-inverter-charger-wifi-plug-pro.pdf) [CONFIRMED-primary]:
  - The dongle plugs into the inverter's serial port.
  - LEDs: PWR, COM, NET and SRV.
  - Its hotspot SSID is the datalogger PN (example "W0016250020617") with "Initial Password:12345678".
  - Registration uses the PN.
- Sorotec-branded "Wi-Fi-Plug-Pro-03" for "SOROTEC AND REVO" is sold in Qatar (alannabi.qa, search snippet; page 403) [SECONDARY].
- REVO II owners report using SmartESS (forum thread 35132) [SECONDARY]. A "Sorotec Revo VM II 8K" owner used **SolarPower** PC software "downloaded from Sorotec website" [SECONDARY]. Other users mention WatchPower [SECONDARY].
- **Savx / IPS Bazar:** no WiFi module, app or monitoring service is offered or documented [CONFIRMED-primary, absence]. Whether IPS Bazar can supply the Sorotec/Eybond WiFi plug for the F4 must be asked by phone (01749 426 789).

---

## 5. Physical-inspection checklist for the user

Per the orchestrator's `user-constraints.md`, the user has **no multimeter**, has the **paper manual**, a spare LAN cable, and can buy nothing. The checklist below is ordered accordingly. Steps marked (optional) need tools the user does not have.

### 5.1 Safety rules (read first)
1. Do **not** remove the lower "F4 3KW" wiring cover. Behind it are 230 VAC and **PV DC up to 500 V**, which stays live whenever the sun is on the panels, plus battery DC at up to 145 A [CONFIRMED-primary, nameplate].
2. Inspect the communication ports from **below the unit**, using the iPhone camera (selfie camera or a small mirror) and a torch. Nothing needs to be opened. In Sorotec's render the RJ45 jacks are on the exposed bottom face, outside the wiring cover [CONFIRMED-primary image].
3. Leave the **BMS/CAN** cable plugged in. Unplugging it can make the inverter raise a BMS-communication warning (mpp-solar warning 39, "BMS Communication lost") and may change charging behaviour [INFERRED].
4. Never connect any RJ45 pin to an ESP32 GPIO directly. RS232 lines swing ±3 to ±15 V, and some RJ45 pins can carry +12 V dongle power. Only the RS485 module's A/B screw terminals may connect to the port, and only on the pins the manual assigns [INFERRED].

### 5.2 Photograph or record these (send to the plan writer)
1. The **whole bottom face**, straight on, then a close-up of each jack with its silk-screen label readable. Expected: two RJ45 jacks side by side (reported as "RS485" and "BMS/CAN"), maybe a USB-B or micro-USB jack, a rocker switch marked "I O II", fan grilles and cable entries.
2. Any **extra label**: inside the wiring-cover lip (do not open it), the sides, the top bracket, and the back if visible. Look for words such as SOROTEC, REVO, VM II PRO, SORO, a factory model string, firmware or QC stickers, or a WiFi PN starting with "W0".
3. **Paper manual pages:** cover and title (does it say REVO VM II PRO or SOROTEC anywhere?), "Product overview" (the bottom-panel drawing with numbered callouts), any "Communication", "RS485", "BMS communication", "WiFi" or "Dry contact" sections, the RJ45 pin table, and the full LCD settings-program list. Note the programs for battery type, BMS protocol and RS485 address.
4. The **battery side:** which battery port the cable leaves from (CAN? RS485?) and its labels.
5. **LCD screens:** press UP/DOWN in the normal display to cycle through all readouts, and photograph each, including any firmware-version screen. This tells the plan writer which values the inverter can measure.

### 5.3 How to read the labels

| Silk-screen text | Meaning | Use for the datalogger |
|---|---|---|
| RS232 / COM / PC | Serial monitoring port at ±12 V levels (Voltronic/PI30-type) | Needs an RS232 to TTL (MAX3232) converter. The user's RS485 module will **not** work. |
| RS485 (alone) | Differential bus: monitoring or WiFi port, or RS485 BMS port | **Primary target** for the RS485 module (A+/B- from the manual's pin table) |
| BMS / BMS/CAN / CAN / LI-COM | Battery link; inverter is master | Leave connected. Do not use for logging. |
| USB | USB-serial or USB-HID with the same protocol | Unusable (no OTG adapter or USB-host cable); ignore |
| WIFI / WLAN | Dongle port (serial levels plus dongle power) | Would mean an Eybond plug is expected. Ask IPS Bazar. |
| DRY / RELAY / GEN | Dry contact for generator start | Not a data port |
| PARALLEL / SHARE / CURRENT SHARE | Parallel-kit bus | Do not touch |

### 5.4 Counting RJ45 pins (no tools)
- Use the spare LAN cable as a **colour key**. Standard patch cables are usually T568B:
  - Pin 1 white-orange, pin 2 orange
  - Pin 3 white-green, pin 4 blue
  - Pin 5 white-blue, pin 6 green
  - Pin 7 white-brown, pin 8 brown
- Hold the **plug** with the clip facing away from you (down), the gold contacts facing you and the cable hanging toward you. Pin 1 is on the **left**.
- Before cutting, look through the clear plug shell and **write down the actual colour order**. Do not assume T568A or T568B.
- Cut the cable leaving about 30 cm on the plug side. Strip only the two (or three) conductors the manual names for RS485 A, B (and GND). Fold back and tape the unused conductors individually so no bare copper is exposed.

### 5.5 Electrical identification (optional, needs a multimeter the user does not have)
If a multimeter becomes available, measure in DC volts with the inverter on, the black probe on the manual's GND pin and the red probe on each other pin, through the cut LAN cable [INFERRED, standard interface levels]:
- An RS232 TX line idles at about -5 to -12 V.
- An RS485 A or B line sits at about +1 to +4 V each.
- CAN-H and CAN-L sit at about +2.5 V each.
- A dongle supply pin reads about +5 or +12 V.

A negative reading means the port is RS232, and the RS485 module must not be used on it.

### 5.6 Zero-tool checks that can short-circuit the whole project
1. **Built-in Eybond WiFi test.** Stand next to the inverter, open iPhone Settings, then Wi-Fi, and look for an SSID that looks like a PN starting with "W0…" (the Eybond format, for example "W0016250020617"). If one appears, the inverter has an Eybond datalogger. Install **SmartESS**, connect with the guide's initial password "12345678", and follow the Wi-Fi Plug Pro guide. Monitoring then exists today, and dessmonitor.com data can be pulled into Firebase by a cloud function or Home Assistant (section 4.4) [INFERRED test, CONFIRMED-primary format].
2. **Measure the case** with any tape or ruler. Sorotec's 3.2 kW unit is listed as 335 × 225 × 100 mm, 6.1 kg. The 4–6 kW chassis is 400 × 300 × 120 mm [CONFIRMED-primary]. A match further confirms the OEM.
3. **Call IPS Bazar (01749 426 789)** and ask:
   - Is the F4 3KW a Sorotec REVO VM II PRO 3.2KW?
   - Is there a WiFi module for it, and does it use SmartESS? What does it cost?
   - Can they send the PDF manual and the RS485/RS232 protocol document?
   - Which RJ45 pins are RS485 A and B?

   If needed, email Sorotec (ella@soroups.com) with the same questions, quoting "REVO VM II PRO 3.2KW 24V, 2025".

### 5.7 First wire test (for the firmware plan), RS485 module on the "RS485" port
The firmware should auto-probe as follows [INFERRED]:
1. **Listen first.** At each baud rate (2400, 9600, 19200, 115200; 8N1), listen silently for about 3 s. Periodic unsolicited frames mean the port is a BMS master port (the inverter is polling a battery), not a monitoring port.
2. **Try the Sorotec PI30REVO dialect.** At 2400, then 9600: send `QPI` (`51 50 49 BE AC 0D`). Expect an ASCII reply starting with `(` and ending in `0D` (for example "(PI30…"). Then send `QALL` (`51 41 4C 4C 2B 0D`) and `QPIGS` (`51 50 49 47 53 B7 A9 0D`).
3. **Try Modbus RTU.** At 9600 (then 19200 and 2400), send function 03 and 04 reads to slave addresses 1 and 2. No Sorotec register map was found. The unsourced "30001…" map in `gemini-claims.md` has **no source** and must not be trusted.
4. **Keep the BMS cable untouched** throughout. Log raw hex of everything received.

---

## 6. Open questions and risks for the plan writer
1. **RS232 vs RS485 on the free port.** Sorotec's datasheet says "Standard: RS232". The user's unit is reported as labelled "RS485". Only the paper manual or the silk-screen settles this. If it is truly RS232, the purchased RS485 module cannot be used on it, and with no new purchases allowed, only the SmartESS/WiFi route remains.
2. **Protocol on the 2025 round-dial generation** is unverified: PI30REVO is likely, but not proven.
3. **Built-in WiFi** on the Savx unit is unknown. The SSID scan in 5.6 settles it at no cost.
4. **The RS485 port's role** may be battery (BMS RS485) rather than monitoring. The listen-first probe detects this.
5. **Settings writes are blocked** over serial on REVO firmware (forum). That does not matter for a read-only logger.

---

## 7. Sources

Primary (official, datasheets, source code):
- IPS Bazar WooCommerce Store API: https://ipsbazarbd.com/wp-json/wc/store/products?search=savx&per_page=50 (all Savx listings, specs, prices, images)
- IPS Bazar F4 3000-24V listing: https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/
- IPS Bazar F4 1500-12V listing: https://ipsbazarbd.com/product/savx-f%e0%a7%aa-1500-12v-solar-inverter-mppt-base/
- IPS Bazar F3 listings: https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/ , https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/
- **Savx "F3 VM II PRO" datasheet** (contains "REVO VM II PRO series"): https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf
- Savx F2 24V battery sheet: https://ipsbazarbd.com/wp-content/uploads/2024/11/Savx-F2-100Ah-24V-Lithium-Battery.pdf
- IPS Bazar media library API: https://ipsbazarbd.com/wp-json/wp/v2/media?search=savx&per_page=100
- IPS Bazar F4 images: https://ipsbazarbd.com/wp-content/uploads/2026/04/gthdeee-1.jpg , https://ipsbazarbd.com/wp-content/uploads/2026/02/Savx-f4-Solar-Inverter.jpg
- BME Savx pages: https://bme.com.bd/electrical-equipment/inverter/savx , https://bme.com.bd/savx-f3-3500-24v-mppt-base-solar-inverter
- **SOROTEC REVO VM II PRO 1.6/3.2/4/6/8/10KW** product page: https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/
  - Spec images: https://www.soropower.com/wp-content/uploads/2025/01/0e9d9c96421b6fdc9130eb0ea6ad422.png and https://www.soropower.com/wp-content/uploads/2025/01/3ce086e72ac3589270e34a76a8232df.png
  - Bottom view: https://www.soropower.com/wp-content/uploads/2025/01/03-底部-768x768.webp
- Sorotec REVO VM II PRO US 2.2KW datasheet: https://www.soropower.com/wp-content/uploads/2026/06/REVO-VM-II-PRO-US-2.2KW.pdf
- Sorotec REVO VP/VM datasheet: https://www.soropower.com/wp-content/uploads/2024/08/REVO-VP-VM.pdf
- **Sorotec Revo RS232 Communication Protocol V02/V03**: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf
- Voltronic PI30 HS/MS/MSX RS232 protocol: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf
- mpp-solar `pi30revo.py`: https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/pi30revo.py
- mpp-solar `protocol_helpers.py` (crcPI, crc8P1): https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/protocol_helpers.py
- esphome-pipsolar README (RJ45 pinout, 2400 baud): https://raw.githubusercontent.com/syssi/esphome-pipsolar/main/README.md
- Eybond Wi-Fi Plug Pro quick guide: https://www.inverter.com/images/uploaded/solar-inverter-charger-wifi-plug-pro.pdf
- SmartESS on the App Store: https://apps.apple.com/us/app/smartess/id1334656760
- Voltronic Axpert VM II Premium datasheet: https://www.bimblesolar.com/docs/Axpert_VM%20II%20Premium%20datasheet.pdf
- MUST PV1800 PRO: https://www.mustpower.com/product/pv1800-pro-series-26-2kw/
- YouTube oEmbed for tdCSpWiejBQ: https://www.youtube.com/oembed?url=https://www.youtube.com/watch?v=tdCSpWiejBQ&format=json

Secondary (community, forums, listings):
- dessmonitor-homeassistant README (Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi confirmed): https://github.com/SilverFire/dessmonitor-homeassistant
- mpp-solar Discussion #78 (Sorotec REVO VE II, PI30REVO): https://github.com/jblance/mpp-solar/discussions/78
- GitHub community discussion #49529 (PS3KVA, Sorotec V02 protocol mention): https://github.com/orgs/community/discussions/49529
- esphome-pipsolar issue #169 (Sorotec 8 kW at 2400 baud): https://github.com/syssi/esphome-pipsolar/issues/169
- DIY Solar Forum, Revo-E / REVO VM II PRO 5.5 kW RS232 writes: https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/
- DIY Solar Forum, Sorotec REVO II password, SmartESS, SolarPower: https://diysolarforum.com/threads/sorotec-revo-ii-factory-password.35132/
- DIY Solar Forum, Sorotec REVO II vs EASUN iGrid VE II: https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/
- Sorotec older REVO VM II Pro 3.5/5.5 kW (120–450 VDC): https://www.sorosolar.com/wide-range-120-450vdc-new-style-sorotec-revo-vm-ii-pro-3-5kw-5-5kw-off-grid-solar-inverter-product/
- Sorotec WiFi Plug Pro-03 listing (Qatar, page 403, search snippet only): https://alannabi.qa/product/wifi-module-for-solar-inverters-sorotec-brand-wi-fi-plug-pro-03/
- PowMr POW-HVM3.2H-24V-N listing (search snippet): https://powmr.com/products/all-in-one-inverter-charger-3000watt-220vac-24vdc
