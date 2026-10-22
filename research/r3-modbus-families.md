# R3: Serial protocols of low-cost hybrid inverters that could be inside the Savx F4 3KW

*Research date: 2026-10-21. Scope: the Modbus RTU families (and their non-Modbus look-alikes) that a rebranded 3.2 kW / 24 V / 500 Voc "MPPT BASE" hybrid inverter could use. The goal is that an ESP32-S3 with an RS485 module can identify the family at runtime and log its data.*

Confidence tags:
- **[CONFIRMED-primary]**: read in an official protocol document or in project source code/configuration.
- **[SECONDARY]**: from a forum, an issue thread, a README narrative or a product listing.
- **[INFERRED]**: my own reasoning or calculation. Every CRC value shown below was computed with a CRC-16/MODBUS routine and checked against the examples printed in the vendor documents.

---

## 0. Summary of findings

1. **No public source names the OEM behind the "Savx F4 3KW".** The rest of this report treats it as unknown. On the inverter's own front panel, the LCD and controls look like the common Chinese "hybrid off-grid" layout: an INPUT/OUTPUT field, a BYPASS line, a load bar, an AC/INV–CHG–FAULT LED row, and ESC/UP/DOWN/ENTER keys **[INFERRED from photo 1]**. That layout is shared by the SMG-II, PI30/Voltronic and 8851 families, so it does not settle the protocol. Section 10 gives a runtime probe that does.
2. **The most likely family is the "SMG" Modbus map, also called "Sumry"** (EASUN/ISolar SMG-II, Anenji 3–6.2 kW, PowMr POW-HVM5.5K, Qoltec, GM6200, Aninerel). The evidence:
   - It is the most widely rebranded map in this power class. `syssi/esphome-smg-ii` lists **"Anenji 3kW-24Vdc-120Vac"** as supported **[CONFIRMED-primary]** ([README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md)).
   - SolarAssistant connects "ISolar SMG II 2kW–6.2kW 24V" and "Anenji ANJ-4000W-24V" through the same `sumry` driver **[SECONDARY]** ([SMG II 24V](https://solar-assistant.io/help/inverters/easun/ISOLAR-SMG-II-4kW-24V), [Anenji 24V](https://solar-assistant.io/help/inverters/anenji/ANJ-4000W-24V)).
   - Serial settings: 9600 8N1, slave 1, FC 0x03 to read and FC 0x10 to write. The data registers are 200–234 decimal.
3. Other families that a 3 kW / 24 V box could use:
   - **SRNE** (0x0100/0x0200/0xE000/0xF000 maps; 9600 8N1; slave 1, or 0xFF as a universal address). Rebranded as Y&H, Datouboss and Anenji 12 kW.
   - **MUST PV18/PH18** (19200 8N1, slave 4, registers 10000/15000/20000/25000).
   - **PowMr HVM "WIFI-VM"** (2400 8N1, slave 5, registers 4501+, bytes swapped inside each word).
   - **"8851" binary frames**, which are not Modbus but use the Modbus CRC (PowMr 4.5K/6.5K, Sumry HGX; 9600).
   - **Voltronic PI30 ASCII** (`QPIGS` and similar; usually 2400 baud). It is not Modbus but has to be in the probe list: an EASUN "SMH-II 3.2" and a Daxtromn AGH turned out to be PI30 **[SECONDARY]** ([#80](https://github.com/syssi/esphome-smg-ii/issues/80), [#66](https://github.com/syssi/esphome-smg-ii/issues/66)).
4. **Whether the purchased RS485 module fits depends on the hardware port:**
   - It works directly on the **SRNE** and **MUST** families.
   - It works on **SMG-II** only through its RS485/BMS RJ45 port. Pin 1 = B−, pin 2 = A+, pin 8 = GND, and it uses the same registers as RS232 **[SECONDARY]** ([#101](https://github.com/syssi/esphome-smg-ii/issues/101)).
   - It does **not** work on RS232-only ports (PowMr HVM, PI30, the SMG RS232 RJ45, 8851). Those need a MAX3232 RS232↔TTL module **[INFERRED]**.
5. **Do not connect the 12 V or 5 V supply pins on these ports to a 3.3 V part:**
   - Several RJ45 COM ports carry **+12 V on pin 4** (SMG-II, PIP/PI30, PowMr) **[CONFIRMED-primary]** ([smg-ii README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md), [powmr_comm](https://github.com/leodesigner/powmr_comm)).
   - The SRNE "WIFI" RJ45 carries **+5 V on pin 1** **[SECONDARY]** ([phinix](https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485)).
   - USB-A-shaped "WiFi" ports carry 5 V on VBUS. Their D+/D− pins carry RS232 on some SMG units and RS485 on MUST units, not USB data **[CONFIRMED-primary]** ([smg-ii README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md), [must esp32-c6 doc](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/esp32-c6-pv19-display.md)).

---

## 1. Family overview

| Family | Rebrands / models seen | Physical link | Serial | Default slave | Function codes | Identity registers | Fits the RS485 module? |
|---|---|---|---|---|---|---|---|
| **A. SMG / "Sumry Modbus"** | EASUN/ISolar SMG-II, SMG III, PowMr POW-HVM5.5K, Qoltec 53963, Anenji 3 kW-24V, 4.2/6.2 kW, GM6200, Aninerel ANL-4200T-24L, Sandisolar SD-HYM | RJ45 RS232 (pin 1 TX, 2 RX, 4 +12 V, 8 GND); RJ45 RS485/BMS (pin 1 B−, 2 A+, 8 GND); USB-A COM (RS232 on D+/D−); internal 5-pin TTL header | 9600 8N1 | 1 | 03 read, 10 write; 14 (file read) on GM6200 | 171 device type, 184 protocol no., 186×12 serial, 643 rated W | Yes, on the RS485 RJ45 |
| A′. Anenji protocol 3–10 | Anenji ANJ-11KW-48V-WIFI(-P), HHS-11kW, Sandisolar SD-11KP48V | Internal WiFi board header, RS485 RJ45 | 9600 | 1 | 03 | 171, 184 = 3/4/5/6 | Yes |
| **A2. PowMr HVM "WIFI-VM"** | POW-HVM1.5H-12V … 3.2H-24V … 10.2M | RJ45 RS232: pin 1 TX, 2 RX, 4 +12 V, 8 GND | **2400** 8N1 | **5** | 03 read, 06 write (ESPHome default) | Not documented | No (needs MAX3232) |
| **A3. "8851" binary** | PowMr POW-HVM4.5K-24V / 6.5K-48V, Sumry HGX 4 kW 24V; DTU WBS1-V001 | RS232 (same pinout as powmr_comm) | 9600 8N1 | n/a | Custom header `88 51` | Reply starts `88 51 00 03` | No |
| **C. SRNE** | SRNE HF/ASF/ASP/HESP, Y&H HSI, Datouboss (non-standard map), Anenji 12 kW split-phase | RJ45 "WIFI": pin 1 +5 V, 2 GND, 7 A, 8 B; RS485 RJ45; RS232 on ASF | 9600 8N1 | 1 (0xFF universal) | 03, 06, 10 | 0x000B product type, 0x0035×20 serial | Yes |
| **D. MUST PV18/PH18/PV19** | MUST PV1800/PH1800/EP1800/PV1900 | RJ45 RS485 (pin 1 A, 2 B, 8 GND); USB-A "WiFi" (1 +5 V, 2 B, 3 A, 4 GND); USB-B with on-board CH340 | **19200** 8N1 | **4** | 03 read, 06 write | 20000–20001 model "PV"+1800 | Yes |
| E1. Sumry/GES "0x7530" | Anenji GES48120M250-500P (white 12 kW split-phase) | RS485 | not stated in the sources I read | 1 (catalogue probe address) | 03 | 0xC738 model, 0xC739 class, 0xC768 protocol | Yes |
| E2. PI30 (Voltronic ASCII) | Axpert/MPP/EASUN SMH, Daxtromn AGH, PowMr 4.2 kW (pipsolar) | RJ45 RS232 (pin 4 or 7 = +12 V), USB-HID | 2400 8N1 (one report says 9600) | n/a | ASCII `QPI`, `QPIGS`… | `QPI` → `(PI30` | No |
| E3. EyeBond "G-ASCII" | Gootu GT-H2436M14P5 (3.6 kW 24V), LVYUAN TY-SIC-3.6KBE-W1 (3.6 kW 24V) | Via EyeBond collector | ? | 0 | ASCII `GPDAT0`, `GPV` + CR, no CRC | `F`/`SVFW` fingerprint | Unknown |

Sources for the table are cited in each family section below.

---

## 2. Family A: SMG / ISolar / EASUN / Anenji / "Sumry" Modbus

### 2.1 Primary documents and code
- *Modbus_RTU RS232 communication protocol, Nov 2021 V1.0* (the "classic SMG" map) **[CONFIRMED-primary]**: [SMG-RS232 Communication Protocol V1.0.1.pdf](https://github.com/syssi/esphome-smg-ii/blob/main/docs/SMG-RS232%2BCommunication%2BProtocol%2BV1.0.1.pdf)
- *2号通讯协议 / GM6200 Communication Protocol 2, July 2024 V1.0*: a Chinese-language superset of the same map, adding battery type, power flow, energy, a clock and file read **[CONFIRMED-primary]**: [GM6200 Communication Protocol 2_CN.html](https://github.com/syssi/esphome-smg-ii/blob/main/docs/GM6200%2BCommunication%2BProtocol%2B2_CN.html)
- *Anenji "Modbus_RTU RS232 communication protocol, Sept 2022 V1.0" (file RS232.Communication.Protocol.V1.0-220209.pdf)*: the same map plus a battery-type enum and an extended output-priority enum **[CONFIRMED-primary]**: [PDF](https://github.com/user-attachments/files/20907869/RS232.Communication.Protocol.V1.0-220209.pdf), referenced from [ha-solarman anenji_hybrid.yaml](https://github.com/davidrapan/ha-solarman/blob/main/custom_components/solarman/inverter_definitions/anenji_hybrid.yaml)
- Code:
  - [syssi/esphome-smg-ii esp32-example.yaml](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-example.yaml)
  - [esp32-gm6200-example.yaml](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-gm6200-example.yaml)
  - [esp32-s2-mini-usb-a-example.yaml](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-s2-mini-usb-a-example.yaml)
  - [groove-max/ha-eybond-local modbus_smg/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/modbus_smg/base.json)
  - [inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json)

### 2.2 Serial parameters, slave ID, function codes
- **9600 baud, 8 data bits, no parity, 1 stop bit; slave address 0x01** **[CONFIRMED-primary]**. Sources: `uart: baud_rate: 9600` and `modbus_controller: address: 0x01` in [esp32-example.yaml](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-example.yaml); `data_bits: 8, parity: NONE, stop_bits: 1` in [usb-a example](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-s2-mini-usb-a-example.yaml); `device_addr: 1`, `link_hints.baud: [9600]` for `modbus_smg` in [inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json).
- The protocol document itself does not state a baud rate. It gives the device address range as "1~247" **[CONFIRMED-primary]** (V1.0.1 PDF).
- **Function codes** **[CONFIRMED-primary]** (V1.0.1 PDF, GM6200 doc):
  - **0x03** reads registers ("R: … 03H command").
  - **0x10** writes registers ("W: … 10H command"). syssi's esp32-example.yaml sets `use_write_multiple: true` on its writable entities (32 occurrences) **[CONFIRMED-primary]**.
  - **0x14** (file read) on GM6200 only, for hourly, daily and monthly energy history.
- **Error replies** **[CONFIRMED-primary]**:
  - A failed **read** does *not* return a Modbus exception. The doc says the lower computer returns the normal FC 03 header followed by **N zero bytes**: "The First 0 … returns a total of Nth 0".
  - A failed **write** returns FC **0x90** with code 01 (read-only register), 03 (value out of range) or 07 (not modifiable in the current mode). GM6200 adds 08 (file data does not exist).
- **Block rule** **[CONFIRMED-primary]**: "The start and end address of the query must be a complete data block … the start address cannot be between 186 to 198". Never start or end a read inside a multi-register field such as the 32-bit codes or the ASCII serial.

### 2.3 Errata in the vendor PDFs (checked by CRC calculation) [INFERRED]
- Correct read example: `01 03 00 CA 00 03 25 F5`, reply `01 03 06 08 FC 13 88 04 B0 F7 F3` (202 = 230.0 V, 203 = 50.00 Hz, 204 = 1200 W). Both CRCs check out.
- The table header example `01 03 00 10 00 02 C0 CB` has a **wrong CRC**. The correct value is `C5 CE`.
- The tables label byte 6 "CRC high byte", but every valid example sends the **low byte first**, as standard Modbus does. 0xF525 is sent as `25 F5`.
- The V1.0.1 write example `01 10 01 40 00 01 08 98 BE 3A` is missing the byte-count byte. The Anenji PDF prints `… 08 98 C7 B2`, which is also wrong. The correct frame is **`01 10 01 40 00 01 02 08 98 BE 3A`**, and the reply is `01 10 01 40 00 01 01 E1`. The GM6200 doc prints this correct form.

### 2.4 Physical ports and pinouts
| Port | Pinout | Source | Tag |
|---|---|---|---|
| RJ45 "RS232/COM" | 1 = TX (inverter → ESP), 2 = RX, **4 = +12 V**, 8 = GND (T-568B colours: white-orange, orange, blue, brown) | [syssi README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md) | [CONFIRMED-primary] |
| RJ45 "RS485" (labelled BMS on newer units) | 1 = RS485 B−, 2 = RS485 A+, 8 = GND. Same registers as RS232, and both ports work in parallel. If BMS-over-RS485 is enabled, the logger function on RS485 switches off. | [issue #101](https://github.com/syssi/esphome-smg-ii/issues/101) (users test3210-d, syssi, cirolaferrara); [issue #130](https://github.com/syssi/esphome-smg-ii/issues/130) | [SECONDARY] |
| USB-A "COM" (6 kW firmware variant) | VBUS not used, D− = RX (inverter → ESP), D+ = TX (ESP → inverter), GND. RS232 levels, so a MAX3232 is needed. | [syssi README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md) | [CONFIRMED-primary] |
| Internal WiFi board header (new SMG-II WiFi, Anenji 6.2 kW) | 5-pin JST-XH connector. Users power a D1 mini from its VCC. On Anenji 4200 (2026) the 5-pin header is 1 TX, 2 RX (3.3 V TTL), 3 +5 V, 4 GND, 5 unknown, and the 4-pin header is RS485 (3.3 V TTL side), +5 V, GND. | [#130](https://github.com/syssi/esphome-smg-ii/issues/130), [psalkiewicz/esphome-2026-anenji4200](https://github.com/psalkiewicz/esphome-2026-anenji4200) | [SECONDARY] |

### 2.5 Register table (holding registers, FC 0x03; addresses are decimal in the docs)

Notes for the table:
- "Int" means signed 16-bit and "UInt" unsigned, per the doc legend ("Int: integer … UInt: unsigned integer").
- 32-bit values are sent **high word first** ("the high word comes first") **[CONFIRMED-primary]**.

| Dec | Hex | Name (doc) | Unit / scale | Type | Notes |
|---|---|---|---|---|---|
| 100–101 | 0x0064 | Fault code | bitfield | ULong | See 2.7 |
| 104–105 | 0x0068 | Warning code, unmasked (GM6200 only) | bitfield | ULong | |
| 108–109 | 0x006C | Warning code, masked | bitfield | ULong R/W | See 2.7 |
| 171 | 0x00AB | Device type (GM6200) / "model code" fingerprint (eybond) | — | UInt | Reserved in V1.0.1 |
| 172–183 | 0x00AC | Device name, ASCII (GM6200) | — | ASC×12 | |
| 184 | 0x00B8 | Protocol number (GM6200: "本协议的协议号为0x02", i.e. this protocol's number is 0x02); "Invalid data" in V1.0.1 | — | UInt | eybond layouts: 1, 2, 11 = classic SMG; 3/4/5/6 = Anenji 11 kW |
| 186–197 | 0x00BA | Serial number | ASCII | ASC×12 | Read as one complete block |
| 201 | 0x00C9 | Working mode | enum (2.6) | UInt | |
| 202 | 0x00CA | Mains (grid) voltage RMS | 0.1 V | Int | |
| 203 | 0x00CB | Mains frequency | 0.01 Hz | Int | |
| 204 | 0x00CC | Mains average power | 1 W | Int | Marked `inverse` in ha-solarman |
| 205 | 0x00CD | Inverter voltage | 0.1 V | Int | |
| 206 | 0x00CE | Inverter current | 0.1 A | Int | |
| 207 | 0x00CF | Inverter frequency | 0.01 Hz | Int | |
| 208 | 0x00D0 | Inverter average power | 1 W | Int | "Positive … inverter output, negative … inverter input" |
| 209 | 0x00D1 | Inverter charging power | 1 W | Int | |
| 210 | 0x00D2 | Output voltage | 0.1 V | Int | |
| 211 | 0x00D3 | Output current | 0.1 A | Int | |
| 212 | 0x00D4 | Output frequency | 0.01 Hz | Int | |
| 213 | 0x00D5 | Output active power | 1 W | Int | |
| 214 | 0x00D6 | Output apparent power | 1 VA | Int | |
| 215 | 0x00D7 | Battery voltage | 0.1 V | Int | |
| 216 | 0x00D8 | Battery current | 0.1 A | Int | |
| 217 | 0x00D9 | Battery power | 1 W | Int | syssi treats >0 as charging and <0 as discharging |
| 219 | 0x00DB | PV voltage | 0.1 V | Int | Single MPPT; no PV2 in this map |
| 220 | 0x00DC | PV current | 0.1 A | Int | |
| 223 | 0x00DF | PV power | 1 W | Int | |
| 224 | 0x00E0 | PV charging power | 1 W | Int | |
| 225 | 0x00E1 | Load percentage | 1 % | Int | |
| 226 | 0x00E2 | DC-DC temperature | 1 °C | Int | |
| 227 | 0x00E3 | Inverter temperature | 1 °C | Int | |
| 228 | 0x00E4 | PV temperature (GM6200) | 1 °C | Int | |
| 229 | 0x00E5 | Battery SOC | 1 % | UInt | |
| 231 | 0x00E7 | Power-flow status (GM6200) | bitfield (2.7) | UInt | |
| 232 | 0x00E8 | Battery current, filtered | 0.1 A | Int | "**Positive … charging, negative … discharging**" |
| 233 | 0x00E9 | Inverter (AC) charging current | 0.1 A | Int | |
| 234 | 0x00EA | PV charging current | 0.1 A | Int | |
| 235–236 | 0x00EB | External CT power (GM6200) | 1 W | Long | + = drawn from grid, − = fed into grid |
| 300 | 0x012C | Output mode | 0 single, 1 parallel, 2/3/4 three-phase P1/P2/P3; Anenji adds 5/6 split-phase P1/P2 | R/W | |
| 301 | 0x012D | **Output priority** | enum, see 2.6 (varies by variant) | R/W | |
| 302 | 0x012E | Input voltage range | V1.0.1: 0 wide, 1 narrow; GM6200: 0 APL, 1 UPS, 2 GNT | R/W | |
| 303 | 0x012F | Buzzer mode | 0–3 | R/W | |
| 305–310 | | LCD backlight, LCD auto-return, energy saving, overload restart, over-temperature restart, overload-to-bypass | 0/1 each | R/W | |
| 313 | 0x0139 | Battery equalisation enable | 0/1 | R/W | |
| 318 / 319 | 0x013E / 0x013F | Secondary output / charge priority (GM6200) | enum | R/W | |
| 320 | 0x0140 | Output voltage setting | 0.1 V | R/W | |
| 321 | 0x0141 | Output frequency setting | 0.01 Hz | R/W | |
| 322 | 0x0142 | **Battery type** (GM6200, Anenji) | enum, see 2.6 | R/W | Reserved in V1.0.1 |
| 323 | 0x0143 | Battery over-voltage protection | 0.1 V | R/W | |
| 324 | 0x0144 | Max (bulk) charging voltage | 0.1 V | R/W | |
| 325 | 0x0145 | Float voltage | 0.1 V | R/W | |
| 326 | 0x0146 | Battery discharge recovery point, mains mode | 0.1 V | R/W | |
| 327 | 0x0147 | Battery low-voltage point, mains mode | 0.1 V | R/W | |
| 329 | 0x0149 | Battery low-voltage point, off-grid mode | 0.1 V | R/W | |
| 331 | 0x014B | **Charger (battery charging) priority** | enum, see 2.6 | R/W | |
| 332 | 0x014C | Max charging current | 0.1 A | R/W | |
| 333 | 0x014D | Max mains charging current | 0.1 A | R/W | |
| 334–337 | | Equalisation voltage (0.1 V), equalisation time (min), equalisation timeout (min), equalisation interval (days) | | R/W | |
| 338 | 0x0152 | Auto mains output enable (GM6200) | 0/1 | R/W | |
| 341–343 | | SOC protection/recovery thresholds (GM6200) | 1 % | R/W | |
| 351 | 0x015F | Max discharge current protection (GM6200) | 1 A | R/W | |
| 402 / 403 | | Current-period output / charge priority (GM6200) | | R | |
| 406 | 0x0196 | Turn-on mode | 0 local or remote, 1 local only, 2 remote only | R/W | |
| 420 | 0x01A4 | Remote switch | 0 off, 1 on | R/W | |
| 426 | 0x01AA | Exit fault mode | write 1 | W | |
| 434–436 / 437–439 | 0x01B2 / 0x01B5 | Date (Y, M, D) / time (h, m, s) (GM6200) | | R/W | |
| 443 | 0x01BB | **PV energy today** (GM6200) | 0.01 kWh | UInt | |
| 444–445 | 0x01BC | **PV energy total** (GM6200) | 0.01 kWh | ULong | |
| 459 | 0x01CB | Clear energy data (GM6200) | write 0xAA | W | |
| 626–633 | 0x0272 | Program version, ASCII (GM6200) | | ASC×8 | |
| 643 | 0x0283 | Rated power | 1 W | UInt | |
| 644 | 0x0284 | Rated number of 12 V battery blocks "J" (GM6200) | pcs | UInt | GM6200 setting ranges are expressed in multiples of J |

Sources: the V1.0.1 PDF, the GM6200 doc and the Anenji PDF (all three **[CONFIRMED-primary]**), plus the `spec_sets` in [modbus_smg/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/modbus_smg/base.json) **[CONFIRMED-primary code]**.
- **Energy:** classic V1.0.1 units have **no energy counters**. syssi integrates PV power on the ESP with `total_daily_energy` **[CONFIRMED-primary]** ([esp32-example.yaml](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-example.yaml)).
- **Variant registers:** a 6 kW firmware with a USB-A port answers at a different map: 0x0050 grid V, 0x0052 grid Hz, 0x0058 output V, 0x005A output Hz, 0x0159 battery V, 0x015D battery current (signed). Registers 201+ "do not respond" on that firmware **[CONFIRMED-primary]** (empirical map in the [syssi README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md)).

### 2.6 Enumerations
- **Working mode (201):** 0 Power-on, 1 Standby, 2 Mains, 3 Off-grid, 4 Bypass, 5 Charging, 6 Fault **[CONFIRMED-primary]**.
- **Output priority (301)** **[CONFIRMED-primary]**; the meaning depends on the variant:
  - V1.0.1: 0 Utility-PV-Battery (UTI), 1 PV-Utility-Battery (SOL), 2 PV-Battery-Utility (SBU).
  - Anenji V1.0: adds 3 = SUB.
  - syssi notes that 4 = SUF exists on "Qoltec 53963, Anenji 3kW-24Vdc-120Vac".
  - GM6200 protocol 2 renumbers the list: 1 SUB, 2 SBU, 3 SUF, 4 ZEC.
- **Charger priority (331)** **[CONFIRMED-primary]**:
  - V1.0.1 and Anenji: 0 Utility, 1 PV, 2 PV = Utility, 3 PV only.
  - GM6200: 1 SOF (PV first), 2 SNU, 3 OSO (PV only), 4 SOR.
- **Battery type (322)** **[CONFIRMED-primary]**:
  - Anenji V1.0: 0 AGM, 1 FLD, 2 USER, 3 SMK1, 4 PYL, 5 FOX.
  - GM6200: 0 AGM, 1 FLD, 2 USER, 3 Li1, 4 Li2, 5 Li3, 6 Li4, 7 reserved (not displayed), 8 LIB.
- **Power flow (231, GM6200)** **[CONFIRMED-primary]**:

  | Bits | Meaning |
  |---|---|
  | 0–1 | PV connected |
  | 2–3 | Mains: 0 none, 1 connected, 2 grid-tied |
  | 4–5 | Battery: 0 idle, 1 charging, 2 discharging |
  | 6–7 | Load output |
  | 8 | Charging from AC |
  | 9 | Charging from PV |
  | 10–13 | LCD icon off flags (battery, PV, mains, load) |

### 2.7 Fault and warning tables
**Fault code (100–101).**
- V1.0.1 and Anenji: "fault code 1 corresponds to bit1", so bit N = code N and bit 0 is unused.
- GM6200: bit 0 is the first fault and the numbering differs in places (see the last column) **[CONFIRMED-primary]**.

| Code / bit (V1.0.1) | Meaning | GM6200 bit, where it differs |
|---|---|---|
| 1 | Inverter module over-temperature (Anenji: reserved) | bit 0 = inverter OT |
| 2 | DC-DC module over-temperature | bit 1 = PV OT, bit 2 = DC-DC OT |
| 3 | Battery over-voltage | bit 3 (same) |
| 4 | PV module over-temperature (Anenji: reserved) | bit 4 reserved |
| 5 | Output short-circuit | |
| 6 | Inverter over-voltage | |
| 7 | Output overload | |
| 8 | Bus over-voltage | |
| 9 | Bus soft-start timeout | |
| 10 | PV over-current | |
| 11 | PV over-voltage | |
| 12 | Battery over-current | bit 12 = DC-DC over-current |
| 13 | Inverter over-current | |
| 14 | Bus low voltage | |
| 16 | Inverter DC component too high | |
| 18–21 | Zero-bias too large: output current / inverter current / battery current / PV current | |
| 22 | Inverter low voltage | |
| 23 | Inverter negative-power protection | |
| 24 | Parallel: host lost | |
| 25 | Parallel: sync signal abnormal | |
| 26 | Battery type incompatible (Anenji: reserved) | |
| 27 | Parallel versions incompatible | |
| — | | bit 28 = external CT duplicate |

**Warning code (108–109)**, bit = meaning **[CONFIRMED-primary]**:

| Bit | Meaning |
|---|---|
| 0 | Mains zero-crossing lost (Anenji, GM6200; reserved in V1.0.1) |
| 1 | Mains waveform abnormal |
| 2 | Mains over-voltage (Anenji, GM6200) |
| 3 | Mains low voltage |
| 4 | Mains over-frequency |
| 5 | Mains low frequency |
| 6 | PV low voltage |
| 7 | Over-temperature |
| 8 | Battery low voltage |
| 9 | Battery not connected |
| 10 | Overload |
| 11 | Battery equalisation charging |
| 12 | Battery discharged low and not yet recharged to recovery point |
| 13 | Output power derating |
| 14 | Fan blocked |
| 15 | PV energy too low |
| 16 | Parallel communication lost |
| 17 | Single/parallel output-mode mismatch |
| 18 | Parallel battery voltage difference too large |
| 19 | Lithium BMS communication abnormal (GM6200) |
| 20 | Battery discharge current over the set value (GM6200) |
| 21 | Islanding (GM6200) |

### 2.8 Known SMG quirks
- **Wake-up frame** (USB-A 6 kW firmware) **[CONFIRMED-primary]** ([usb-a example](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-s2-mini-usb-a-example.yaml)):
  - The inverter "requires a proprietary wakeup frame (01 AA 06 DE A2) before the first Modbus read". One transmission at boot is enough "as long as polls arrive within ≤10 s".
  - `DE A2` is the valid CRC-16/MODBUS of `01 AA 06`, so the frame is a Modbus-style frame with function code 0xAA **[INFERRED]**.
- **Wrong reads look like zeros.** A read the inverter cannot serve comes back as zeros (per the doc). The SMG II 11 kW firmware answered the 6.2 kW map with "almost all data is zero" until the 11 kW map was used **[SECONDARY]** ([#88](https://github.com/syssi/esphome-smg-ii/issues/88)).
  - The eybond driver treats an all-zero identity block as "link down" and 0xFFFF as unavailable **[CONFIRMED-primary code]** ([smg.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/drivers/smg.py)).
  - **Consequence: a probe must reject all-zero replies.**
- **Block sizes in use:** syssi polls at `update_interval: 10s` with `send_wait_time: 200ms`. eybond reads blocks 100×10, 186×12, **201×34** and **300×44** **[CONFIRMED-primary code]** ([base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/modbus_smg/base.json)). 44 registers per read is known to work. No maximum is documented.
- **One master per bus.** "while this bridge is connected, no other device should poll the inverter on the same serial line" **[SECONDARY]** ([esp-eybond-collector](https://github.com/groove-max/esp-eybond-collector)). The factory WiFi module uses RS232; the RS485 port also answers unless BMS mode is enabled **[SECONDARY]** ([#101](https://github.com/syssi/esphome-smg-ii/issues/101)).

### 2.9 Known identity fingerprints (171 / 184 / 643)
From [inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json) **[CONFIRMED-primary data, capture-derived]**:

| Model | Reg 171 (model code) | Reg 184 (layout) | Reg 643 (rated W) |
|---|---|---|---|
| SMG 6200 / Sandisolar SD-HYM-4862HWP | 7680 (0x1E00) | 1 | 6200 |
| Anenji 4200 | 13569 (0x3501) | 1 | 4200 |
| SMG variant 4200 | 30721 | 11 | — |
| Anenji ANJ-5KW-48V-WIFI | 30977 | 11 | 5000 |
| Aninerel 6200 dual-output | 30979 | 11 | 6200 |
| Anenji 6200 | 14080 | 1 | 6200 |
| Aninerel ANL-4200T-24L-W-PRO (24 V) | 30723 | 11 | 4200 |
| Anenji ANJ-6200-48PL | 8960 | 2 | 6200 |
| Anenji HHS-11kW | 29440 | 3 | — |
| ANJ-11KW | 32768 / 33793 | 4 | — |

A Savx F4 on this family would most likely report 643 = 3200 or 3000 and 184 ∈ {1, 2, 11} **[INFERRED]**.

### 2.10 Variant A′: Anenji "Communication Protocol No. 3-10" (11 kW-class, protocol numbers 3/4/5/6)
Registers move compared with the classic map **[CONFIRMED-primary code]** ([communication_protocol_3_10.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/modbus_smg/protocols/communication_protocol_3_10.json)):

| Group | Registers |
|---|---|
| Faults and mode | 100–101 fault, 104–105 warning, 198 power flow, 201 mode |
| Output | 252 current (0.1 A), 253 frequency, 254 power (W), 255 VA, 256 load %, 346 voltage |
| Battery | 277 voltage (0.1 V), 278 current (0.1 A, signed), 279 power, **280 SOC**, 281 DC-DC temperature |
| PV | 302 total power, 303 charging power, 304 charging current, 305 PV temperature, 351/352/353 PV1 V/A/W, 389/390/391 PV2 V/A/W |
| Grid and inverter | 338 grid voltage, 340 grid power, 342/343/344 inverter V/A/W, 231 inverter temperature |
| Settings | 601 output priority, 630 battery type, 632 charge priority, 640/641 max charge currents (0.1 A), 606 output voltage |
| Energy | **702 PV energy today (0.01 kWh)**, 703–704 PV energy total (u32, high word first) |

The [PreK README](https://github.com/PreK/esphome-anenji-11kw-native-modbus) lists the same addresses (338 grid V, 277 battery V, 280 SOC, 351 PV1 V, 302 PV power, 606 output V) **[SECONDARY]**.

---

## 3. Family A2: PowMr HVM "WIFI-VM" Modbus (slave 5, 2400 baud)

Sources: [leodesigner/powmr_comm README](https://github.com/leodesigner/powmr_comm), [odya registers-map.md](https://github.com/odya/esphome-powmr-hybrid-inverter/blob/main/docs/registers-map.md), [odya inverter.yaml](https://github.com/odya/esphome-powmr-hybrid-inverter/blob/main/src/modules/inverter.yaml).
- **Models:** POW-HVM1.5H-12V, 2.0H-12V, 2.4H-24V, **3.2H-24V**, 3.6M-24V, 4.2M-24V, 5.5K-48V, 6.2M-48V, 8.2M, 10.2M **[SECONDARY]**. The HVM3.2H-24V is listed at "30-400V DC" input and 80 A, so it is not an exact spec match for the Savx F4 **[SECONDARY]** ([PowMr](https://powmr.com/products/all-in-one-inverter-charger-3000watt-220vac-24vdc)).
- **Serial:** "RS232 modbus protocol at **2400 baud** (1 stop bit, no parity control). PowMr device **slave id is 5**." **[SECONDARY reverse-engineered, matches code]**. The odya YAML uses `baud_rate: 2400`, `address: 0x05`, `send_wait_time: 250ms`, `command_throttle: 1s` **[CONFIRMED-primary code]**.
- **RJ45 (left side):** pin 8 Brown = GND, **pin 4 Blue = +12 V**, pin 2 Orange = RX (data to inverter), pin 1 White-Orange = TX (data from inverter) **[SECONDARY]**.
- **Original dongle traffic:** `05 03 11 95 00 2D 91 43` (read 45 registers from 4501) and `05 03 11 C2 00 10 E1 42` (read 16 registers from 4546) **[SECONDARY sniff]**. Both CRCs verified **[INFERRED]**.
- **Byte order:** "Read registers are using little-endian format", so swap the two bytes of each word (`swapBytes`) **[CONFIRMED-primary code]**. The odya selects write plain values through ESPHome's default single-register write (FC 06) **[INFERRED from code]**.

| Dec | Hex | Name | Scale (after byte swap) |
|---|---|---|---|
| 4501 | 0x1195 | Output source priority (index with offset; 4537 preferred) | — |
| 4502 | 0x1196 | Grid voltage | 0.1 V |
| 4503 | 0x1197 | Grid frequency | 0.1 Hz |
| 4504 | 0x1198 | PV voltage | 0.1 V |
| 4505 | 0x1199 | PV power (YAML) / "Charging (right now)" (map) | 1 W |
| 4506 | 0x119A | Battery voltage | 0.1 V |
| 4507 | 0x119B | Battery SOC | 1 % |
| 4508 | 0x119C | Battery charge current | 1 A (unsigned) |
| 4509 | 0x119D | Battery discharge current | 1 A (unsigned); net = 4508 − 4509 |
| 4510 / 4511 | 0x119E / 0x119F | Load voltage / frequency | 0.1 V / 0.1 Hz |
| 4512 / 4513 | 0x11A0 / 0x11A1 | Load power / VA | W / VA |
| 4514 | 0x11A2 | Load percent | % |
| 4516 | 0x11A4 | Flags (0x100 overload-related) | bits |
| 4530 | 0x11B2 | Error code | code table not published |
| 4535 | 0x11B7 | Settings flags (0x1 record fault, 0x2 equalisation, 0x100 alarm, 0x400 backlight, 0x800 restart on overload, 0x1000 restart on overheat, 0x2000 beep on source fail, 0x4000 return to default screen, 0x8000 overload bypass) | bits |
| 4536 | 0x11B8 | Charger priority: 0 utility first, 1 solar first, 2 solar + utility, 3 only solar | enum |
| 4537 | 0x11B9 | Output priority: 0 utility first, 1 solar first (SUB), 2 SBU | enum |
| 4538 | 0x11BA | AC input range: 0 appliance, 1 UPS | enum |
| 4540–4552 | | Output Hz (0 = 50, 1 = 60), max charge current, output V, max utility charge current, back-to-utility / back-to-battery V (0.1 V), bulk / float / low cut-off / equalisation V (0.1 V), equalisation time / timeout / interval | |
| 4553 / 4554 | 0x11C9 / 0x11CA | Status flags (on battery, AC active, load enabled …) | bits |
| 4555 | 0x11CB | Charger status: 0 off, 1 idle, 2 charging | enum |
| 4557 | 0x11CD | Temperature (confirmed on HVM3.6M only) | °C |
| 5002–5033 (writes) | 0x138A… | 5017 charger priority (0–3), 5018 output priority (0–2), **5020 battery type (0–2)**, 5022 max total charge current, 5024 utility charge current, … | |

Quirks:
- "one large batch causes data errors". odya splits reads into 0x1196×37 and 0x11BC×16, and reads selects singly because batching them "triggers `Modbus device set offline`" **[SECONDARY]**.
- "inverter produces a lot of RF noise … attach small capacitor to RX pin" **[SECONDARY]** ([powmr_comm](https://github.com/leodesigner/powmr_comm)).
- No energy counters are known.

---

## 4. Family A3: "8851" binary protocol (PowMr 4.5K/6.5K, Sumry HGX)

Sources: [leodesigner/powmr4500_comm README](https://github.com/leodesigner/powmr4500_comm), [include/inv8851.h](https://github.com/leodesigner/powmr4500_comm/blob/main/include/inv8851.h), [lufton/esphome-inv-8851](https://github.com/lufton/esphome-inv-8851).
- **Serial:** "9600 baud, mode 8N1"; frames "similar to simplified modbus"; "The last two bytes is a CRC16 … same as modbus CRC16" **[CONFIRMED-primary]**. The README says the port uses "RS232 … with pinout mentioned in" powmr_comm (RJ45 pin 1 TX, 2 RX, 4 +12 V, 8 GND) **[SECONDARY]**.
- **Requests:**
  - State: `88 51 00 03 00 00 00 00 4D 08`
  - Config: `88 51 00 03 02 00 00 00 4C B0`
  - Config write: command 0x0010. You must read the entire config and write it back whole.
  - All three are **[CONFIRMED-primary]**; both CRCs verified **[INFERRED]**.
- **Header struct:** `proto 0x8851, command 0x0003 (read) / 0x0010 (write), addr 0x0000 state / 0x0200 config, data_size`.
  - State packet length: 154 bytes (version 1, firmware "0005") or 158 bytes (version 2, firmware "8100").
  - Config packet length: 100 or 104 bytes **[CONFIRMED-primary]** (inv8851.h, lufton README).
- **Word order:** words are little-endian (the README dump is annotated "LSB MSB") **[CONFIRMED-primary]**.

**State words** (index after the 8-byte header; **byte offset = 8 + 2 × index**) **[CONFIRMED-primary]** (inv8851.h):

| Index | Field | Scale / type |
|---|---|---|
| 0 | run_mode: four 4-bit topology nibbles. Enum: 0 standby, 1 fault, 2 shutdown, 3 normal, 4 no battery, 5 discharge, 6 parallel discharge, 7 bypass, 8 charge, 9 grid discharge, 10 micro-grid discharge | — |
| 1 | Flags (charge finish, bus OK, no battery, PV excess, float, …) | bits |
| 14 | Software version | — |
| 21 | inv_voltage | 0.1 V |
| 22 | inv_current | 0.01 A |
| 23 | inv_freq | 0.01 Hz |
| 24 | inv_va | 1 VA |
| 25 | load_va | 1 VA |
| 27 | load_watt | 1 W |
| 28 / 29 | inverter VA % / W % | % |
| 30 | load_current | 0.01 A |
| 33 | grid_voltage | 0.1 V |
| 34 | grid_current | 0.01 A |
| 35 | grid_freq | 0.01 Hz |
| 39 | batt_voltage | **0.01 V** |
| 40 | batt_charge_current | 0.1 A, **signed: negative if the battery is discharging** |
| 43 | pv_voltage | 0.1 V |
| 44 | pv_current | 0.01 A |
| 45 | pv_power | 1 W |
| 46 | bus_voltage | 0.1 V |
| 50 | fan1 / fan2 speed % | u8, u8 |
| 51–52 | NTC2 / NTC3 / NTC4 / BTS temperatures | u8 |
| 53–55 | BMS SOC / voltage / current | — |
| 56–71 | BMS cell 1–16 voltages | — |

**Config bits** (word 0) **[CONFIRMED-primary]**:
- battery_type: 2 bits (0 AGM, 1 Flooded, 2 User, 3 Lib)
- output_energy_priority: 0 SUB, 1 SBU
- charge_energy_priority: 0 PV & Grid, 1 PV > Grid, 2 PV only
- grid_enable, frequency, and others

Caveat: on the Sumry HGX, battery_type, equalisation and grid_current do not match the panel **[SECONDARY]** (lufton README).

---

## 5. Family B: Anenji (summary across sub-families)

| Anenji model | Protocol | Evidence |
|---|---|---|
| **3 kW-24Vdc-120Vac**, 4.2 kW, 6.2 kW, 6200 W-WiFi | **Classic SMG** (section 2) | syssi README "Supported devices" **[CONFIRMED-primary]**; [jul-ienfr/esphome-anenji-4.2kw-wifi](https://github.com/jul-ienfr/esphome-anenji-4.2kw-wifi) **[SECONDARY]** |
| Anenji "Hybrid" (ha-solarman definition) | Classic SMG (202…229) per "RS232 Communication Protocol V1.0-220209" | [anenji_hybrid.yaml](https://github.com/davidrapan/ha-solarman/blob/main/custom_components/solarman/inverter_definitions/anenji_hybrid.yaml) **[CONFIRMED-primary]** |
| ANJ-4000W/4200W-24V | SolarAssistant "Connects as: sumry" (the same driver as EASUN SMG II 24V) | [SolarAssistant](https://solar-assistant.io/help/inverters/anenji/ANJ-4000W-24V) **[SECONDARY]** |
| ANJ-5KW / 6200-48PL / 6200 | SMG layouts 11 / 2 / 1 | eybond catalogue **[CONFIRMED-primary data]** |
| ANJ-11KW-WIFI(-P), HHS-11kW | Protocol No. 3-10 (A′, section 2.10) | eybond catalogue; PreK README |
| 12 kW split-phase 120/240 V (SRNE SN ANJ…) | **SRNE** map (section 6) | [RAR/esphome-srne-inverter](https://github.com/RAR/esphome-srne-inverter) **[SECONDARY]** |
| GES48120M250-500P (white 12 kW) | Sumry/GES 0x7530 map | eybond catalogue (section 8.1) |

Physical details:
- Battery type on SMG-family Anenji is register 322: 0 AGM, 1 FLD, 2 USER, 3 SMK1, 4 PYL, 5 FOX (Anenji PDF) **[CONFIRMED-primary]**.
- Anenji 4200 (2026) internal WiFi board: 5-pin RS232-side header (TX/RX at 3.3 V TTL, +5 V, GND) and a 4-pin RS485-side header. RJ45 RS485: pin 1 = B(+), pin 2 = A(−), pin 7 = A(−), pin 8 = B(+) **[SECONDARY]** ([psalkiewicz](https://github.com/psalkiewicz/esphome-2026-anenji4200)). The A/B labels conflict with the SMG-II report in section 2.4. Try swapping A and B if there is no reply **[INFERRED]**.
- Newer units have a built-in "EB-WFBLE-03" EyeBond WiFi/BLE module. Its supply "isn't beefy enough to power esp" **[SECONDARY]** ([#130](https://github.com/syssi/esphome-smg-ii/issues/130)).

---

## 6. Family C: SRNE (and rebrands such as Y&H, Datouboss, Anenji 12 kW)

### 6.1 Primary documents and code
- *MODBUS Protocol for Energy Storage Inverter V1.96 (Jan 11 2024)*, SRNE / 深圳硕日新能源 **[CONFIRMED-primary]**: [PDF](https://github.com/syssi/esphome-smg-ii/blob/main/docs/SRNE.Solar.Charge.Inverter.MODBUS.Protocol1.96.pdf). V1.7 hybrid PDF: [RAR repo](https://github.com/RAR/esphome-srne-inverter/blob/main/srne-hybrid-solar-inverter-modbus-protocol-v1-7.pdf). V2.08 PDF: [phinix repo](https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485).
- Code:
  - [syssi esp32-srne-asf-example.yaml](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-srne-asf-example.yaml)
  - [eybond srne.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/drivers/srne.py) and [srne_modbus/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/srne_modbus/base.json)
  - [HotNoob PythonProtocolGateway srne maps](https://github.com/HotNoob/PythonProtocolGateway/tree/main/protocols/srne)

### 6.2 Serial parameters, slave ID, function codes [CONFIRMED-primary] (V1.96 §1–3)
- "**9,600, n, 8, 1**".
- "The default address of the inverter is **1**, which can be set. It supports **255 universal address** … the address that the inverter responds to is the actual address." The slave range is 01H–FEH; 0 is broadcast; FFH is universal. The address is changed in **0xE200** (1–254).
- "supports **03, 06, and 10** function codes. The maximum number of read-write registers at a time is **32**."
- CRC bytes are sent low byte then high byte.
- Exception replies: 0x83 / 0x86 / 0x90 + code:

  | Code | Meaning |
  |---|---|
  | 01 | Illegal command |
  | 02 | Illegal data address |
  | 03 | Illegal data value |
  | 04 | Operation failure |
  | 05 | Password error |
  | 06 | Data frame / CRC error |
  | 07 | Read-only |
  | 08 | Not modifiable while running |
  | 09 | Password protection |
  | 0A | Length error (more than 32 registers) |
  | 0B | Permission denied |

- Doc example `01 03 02 00 00 20 45 AA` (read 32 registers from 0x0200): CRC verified **[INFERRED]**.
- **32-bit values are stored low word first** ("small-endian mode … 0x0001=0x5678 and 0x0002=0x1234"). This is the **opposite of SMG** **[CONFIRMED-primary]**.
- **Battery setting voltages are normalised to a 12 V battery:** "If the rated voltage … is 48 V and the actual set voltage is 57.6 V, the set value is 57.6 V/4=14.4 V". For a 24 V system, multiply by 2 **[CONFIRMED-primary]**.

### 6.3 Physical ports
| Port | Pinout | Source | Tag |
|---|---|---|---|
| RJ45 labelled **WIFI** (dongle port) | 1 = **+5 V**, 2 = GND, 3–6 = n/c, 7 = RS485-A, 8 = RS485-B | [phinix README](https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485); [RAR WIRING.md](https://github.com/RAR/esphome-srne-inverter/blob/main/WIRING.md) ("confirmed on real hardware": 2 GND, 7 A, 8 B) | [SECONDARY] |
| Generic RS485 RJ45 | 1 = A, 2 = B, 7–8 = GND ("verify against your specific inverter manual") | RAR WIRING.md | [SECONDARY] |
| Y&H HSI 5000U RJ45 | 7 = A, 8 = B, **1 = GND** (conflicts with SRNE pin 1 = +5 V; measure before wiring) | [duwke/Y-H-5000U](https://github.com/duwke/Y-H-5000U) | [SECONDARY] |
| Datouboss 4863U "WiFi dongle / COM" RJ45 | 1 = A, 2 = B, 8 = GND | [aogden24-byte/esphome-datouboss](https://github.com/aogden24-byte/esphome-datouboss) | [SECONDARY] |
| SRNE ASF | RS232 at 9600 (syssi example: "via RS232") | [syssi ASF example](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-srne-asf-example.yaml) | [CONFIRMED-primary] |

### 6.4 Register table (FC 0x03; addresses are **hex** in the doc) [CONFIRMED-primary] (V1.96)
| Hex | Dec | Name | Scale | Signed | Notes |
|---|---|---|---|---|---|
| 0x000B | 11 | Product type (MachType) | — | U | 0 domestic controller, 1 street-light controller, 3 grid-tie inverter, **4 all-in-one solar charger inverter**, 5 power-frequency off-grid |
| 0x0014–15 | 20 | Software version (APP, bootloader) | e.g. 100 = V1.00 | U | |
| 0x001A | 26 | RS485 address (read-only) | — | U | |
| 0x001C | 28 | RS485 protocol version | — | U | |
| 0x0035–0x0048 | 53 | **Product serial-number string**, 20 regs; "low bytes of each register valid" | ASCII | — | eybond's SRNE probe reads 53×20 and requires "SR" in the string |
| 0x0100 | 256 | **Battery SOC** | 1 % | U | |
| 0x0101 | 257 | **Battery voltage** | 0.1 V | U | |
| 0x0102 | 258 | **Battery current** | 0.1 A | **S** | Doc: ">0 indicates discharging; <0 indicates charging". See quirk 6.7 |
| 0x0103 | 259 | Battery temperature (V1.94+) | 0.1 °C | S | |
| 0x0107 / 0x0108 / 0x0109 | 263–265 | **PV1 voltage / current / power** | 0.1 V / 0.1 A / 1 W | U | |
| 0x010A | 266 | Total PV power | 1 W | U | |
| 0x010B | 267 | Charge state | enum | U | 0 off, 1 quick (bulk), 2 constant-voltage, 4 float, 5 reserved, 6 Li activation, 8 full |
| 0x010E | 270 | Total charge power (PV + AC) | 1 W | U | |
| 0x010F / 0x0110 / 0x0111 | 271–273 | **PV2 voltage / current / power** | 0.1 V / 0.1 A / 1 W | U | |
| 0x0200–0x0203 | 512 | Current fault bits (64-bit, internal) | — | U | |
| 0x0204–0x0207 | 516 | **Current fault codes** (up to 4 at once, 0 = none) | code | U | Table 6.6 |
| 0x020C–0x020E | 524 | RTC: year/month, day/hour, minute/second | — | U | RW |
| 0x0210 | 528 | **Machine state** | enum | U | 0 power-on delay, 1 standby, 2 init, 3 soft start, 4 AC (mains) operation, 5 inverter operation, 6 inverter→AC, 7 AC→inverter, 8 battery activation, 9 manual shutdown, 10 fault |
| 0x0212 | 530 | Bus voltage | 0.1 V | U | |
| 0x0213 | 531 | **Grid voltage (A)** | 0.1 V | U | |
| 0x0214 | 532 | Grid current (A) | 0.1 A | U | |
| 0x0215 | 533 | **Grid frequency** | 0.01 Hz | U | |
| 0x0216 | 534 | **Inverter output voltage (A)** | 0.1 V | U | |
| 0x0217 | 535 | Inverter current (A) | 0.1 A | U | |
| 0x0218 | 536 | **Inverter frequency** | 0.01 Hz | U | |
| 0x0219 | 537 | Load current (A) | 0.1 A | U | |
| 0x021B | 539 | **Load active power (A)** | 1 W | U | |
| 0x021C | 540 | **Load apparent power (A)** | 1 VA | U | |
| 0x021E | 542 | Mains charging current (battery side) | 0.1 A | U | |
| 0x021F | 543 | **Load ratio (A)** | 1 % | U | |
| 0x0220 | 544 | **Heatsink A (DC-DC) temperature** | 0.1 °C | S | |
| 0x0221 | 545 | **Heatsink B (DC-AC) temperature** | 0.1 °C | S | |
| 0x0222 / 0x0223 | 546 / 547 | Transformer / ambient temperature | 0.1 °C | S | |
| 0x0224 | 548 | PV charging current (battery side) | 0.1 A | U | |
| 0x023A–0x023C | 570 | Grid active power A/B/C | 1 W | S | + export / − import |
| 0xDF00 | 57088 | Power on/off command | 0 off / 1 on | W | |
| 0xE001 | 57345 | PV max charging current | 0.1 A | U | RW |
| 0xE003 | 57347 | Battery rated voltage | 12/24/36/48 | U | |
| 0xE004 | 57348 | **Battery type** | enum | U | 0 user, 1 SLD, 2 FLD, 3 GEL, 4/5/6 LFP×14/15/16, 7/8/9 LFP×7/8/9, 10/11/12/13 NMC×7/8/13/14 |
| 0xE005–0xE00E | | Battery voltage set-points | 0.1 V, 12 V-normalised | U | |
| 0xE00F | 57359 | Discharge cut-off SOC | % | U | |
| 0xE200 | 57856 | RS485 address | 1–254 | U | |
| 0xE204 | 57860 | **Output priority** | enum | U | 0 solar, 1 line (utility), 2 SBU |
| 0xE205 | 57861 | Mains charge current limit | 0.1 A | U | |
| 0xE208 / 0xE209 | | Output voltage / frequency setting | 0.1 V / 0.01 Hz | U | |
| 0xE20A | 57866 | Max charge current | 0.1 A | U | |
| 0xE20B | 57867 | AC input range | 0 APL, 1 UPS | U | |
| 0xE20F | 57871 | **Charger priority** | enum | U | 0 PV priority, 1 AC priority, 2 hybrid (both, PV first), 3 PV only |
| 0xE215 | 57877 | BMS enable | 0 off, 1 485-BMS, 2 CAN-BMS | U | |
| 0xF000–0xF006 | 61440 | PV energy, last 7 days | 0.1 kWh | U | |
| 0xF02D / 0xF02E | 61485 / 61486 | Battery Ah charged / discharged today | 1 Ah | U | |
| 0xF02F | 61487 | **PV energy today** | 0.1 kWh | U | |
| 0xF030 | 61488 | **Load energy today** | 0.1 kWh | U | |
| 0xF031 | 61489 | Total running days | d | U | |
| 0xF034–35 / 0xF036–37 | | Battery Ah charged / discharged, total | Ah, u32 low word first | U | |
| 0xF038–39 | 61496 | **PV energy total** | 0.1 kWh, u32 low word first | U | |
| 0xF03A–3B | 61498 | **Load energy total** | 0.1 kWh, u32 | U | |
| 0xF03C | 61500 | AC charging today | Ah | U | |
| 0xF03D | 61501 | Load energy from mains today | 0.1 kWh | U | |
| 0xF048–49 | 61512 | Load energy from mains, total | 0.1 kWh | U | |
| 0xF800 + 16·n | 63488 | Fault history records (32 × 16 words) | — | — | |

### 6.5 SRNE register areas [CONFIRMED-primary]
| Range | Area |
|---|---|
| 000A–00FF | Product info |
| 0100–02FF | Live data |
| DF00–DF1F | Control |
| E000–E0FF | Controller user settings |
| E200–E2FF | Inverter user settings |
| E400 | Grid-tie settings |
| F000–F7FF / F800–FFFF | History |

### 6.6 SRNE fault codes (0x0204–0x0207) [SECONDARY, transcribed from SRNE V2.08 by phinix]
Source: [Fault Codes Table.csv](https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485/blob/main/docs/MODBUS%20Protocol%20for%20Energy%20Storage%20Inverter%20-%20Fault%20Codes%20Table.csv).

| Code | Name | Meaning | Affects output |
|---|---|---|---|
| 01 | BatVoltLow | Battery undervoltage alarm | Y |
| 02 | BatOverCurrSw | Battery discharge average-current overcurrent (software) | Y |
| 03 | BatOpen | Battery not connected | Y |
| 04 | BatLowEod | Battery undervoltage, discharge stopped | Y |
| 05 | BatOverCurrHw | Battery overcurrent (hardware) | Y |
| 06 | BatOverVolt | Charging overvoltage | Y |
| 07 | BusOverVoltHw | Bus overvoltage (hardware) | Y |
| 08 | BusOverVoltSw | Bus overvoltage (software) | Y |
| 09 | PvVoltHigh | PV overvoltage | N |
| 10 | PvBuckOCSw | Buck overcurrent (software) | N |
| 11 | PvBuckOCHw | Buck overcurrent (hardware) | N |
| 12 | bLineLoss | Mains power down | N |
| 13 | OverloadBypass | Bypass overload | Y |
| 14 | OverloadInverter | Inverter overload | Y |
| 15 | AcOverCurrHw | Inverter overcurrent (hardware) | Y |
| 17 | InvShort | Inverter short circuit | Y |
| 19 | OverTemperMppt | Buck heatsink over-temperature | N |
| 20 | OverTemperInv | Inverter heatsink over-temperature | Y |
| 21 | FanFail | Fan failure | Y |
| 22 | EEPROM | Memory failure | Y |
| 23 | ModelNumErr | Model setting error | Y |
| 26 | RlyShort | Inverter AC output back-feeding to bypass AC input | Y |
| 29 | BusShort | Internal battery boost circuit failure | Y |

RAR adds code 59 = "BMSChargeDisabled" (undocumented; observed at 100 % BMS SOC) **[SECONDARY]**.

### 6.7 SRNE quirks
- **Battery current sign conflicts between sources:**
  - V1.96 doc: positive = discharging **[CONFIRMED-primary]**.
  - syssi's ASF config applies `multiply: -0.1` **[CONFIRMED-primary code]**.
  - RAR (Anenji 12 kW firmware): "signed; negative = discharging" **[SECONDARY]**.
  - Determine the sign at runtime by comparing with 0x010B (charge state) or PV/load power **[INFERRED]**.
- **Read size:** the doc allows 32 registers per read, but RAR found "max **20 registers per request** on this firmware (the V1.7 PDF claims 32 but the inverter silently times out beyond 20)" **[SECONDARY]**.
  - eybond falls back to short reads (0x0100×3, 0x0107×3, 0x010B×1, 0x010E×1, 0x010F×3) when 0x0100×18 is rejected, and "do[es] not cross … reserved 0x010D" **[CONFIRMED-primary code]** ([srne.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/drivers/srne.py)).
  - phinix: use `force_new_range: true` to stop ESPHome merging ranges **[SECONDARY]**.
- **Rebrands may not use the published map.** The Datouboss 4863U is "a rebrand of an SRNE design, but its register map does **not** match … (the standard 0x0100+ telemetry and 0xDF00 power registers are rejected with Modbus exception 2)". Its own map is at 0x0000–0x0043 / 0x005C–0x008A, with two settings packed into one register in several places, and the dongle polls 0x0000–0x0043 every 30–60 s **[SECONDARY]** ([Datouboss README](https://github.com/aogden24-byte/esphome-datouboss)).
- **Serial string is not a reliable brand test.** The Anenji 12 kW serial reads "ANJ2602260356-100132" at 0x0035 **[SECONDARY]** (RAR). eybond's "contains SR" check would miss rebrands like this one; probe 0x000B and 0x0100 as well **[INFERRED]**.

---

## 7. Family D: MUST PV18 / PH18 / EP18 / PV19

### 7.1 Primary documents and code
- *PH1800 PV1800 EP1800 PV3500 EP3500 RS485 Modbus RTU communication Protocol 1.4.15* (xlsx; sheets "Inverter modbusRTU (ID == 4)", "PV Charger ModbusRtu (ID == 4)", "Inverter Alarm", "PV Charger Alarm") **[CONFIRMED-primary]**: [xlsx](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/docs/PH1800%20PV1800%20EP1800%20PV3500%20EP3500%20RS485%20Modbud%20RTU%20communication%20Protocol%201.4.15.xlsx)
- Code:
  - [vladyspavlov esp32-pv18.yaml](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/esp32-pv18.yaml)
  - [mukaschultze/ha-must-inverter](https://github.com/mukaschultze/ha-must-inverter): [const.py](https://github.com/mukaschultze/ha-must-inverter/blob/master/custom_components/must_inverter/const.py), [__init__.py](https://github.com/mukaschultze/ha-must-inverter/blob/master/custom_components/must_inverter/__init__.py), [mapper.py](https://github.com/mukaschultze/ha-must-inverter/blob/master/custom_components/must_inverter/mapper.py)

### 7.2 Serial parameters, slave ID, function codes
- **19200 8N1, slave 4** **[CONFIRMED-primary]**:
  - ESPHome: `baud_rate: 19200`, `address: 0x04`, `command_throttle: 200ms`.
  - ha-must-inverter: `default=19200`, parity "N", stopbits 1, bytesize 8, `DEFAULT_DEVICE_ID = 0x04`.
  - The xlsx sheet names say "(ID == 4)".
- **FC 0x03** reads (`read_holding_registers`); **FC 0x06** writes (`write_register`) **[CONFIRMED-primary code]**.
- Large reads work: ha-must-inverter reads 10101–10124, 15201–15221, 20000–20016, **20101–20214 (114 registers)** and 25201–25279 (79 registers) in single requests. On PV19 it sleeps 0.1 s between reads "to not overload the inverter" **[CONFIRMED-primary code]**.

### 7.3 Physical ports
| Port | Pinout | Source | Tag |
|---|---|---|---|
| RJ45 RS485 / CAN | 1 = A (TR+), 2 = B (TR−), 8 = GND | [esp32-c6-pv19-display.md](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/esp32-c6-pv19-display.md) | [SECONDARY] |
| USB Type-A "Wi-Fi port" | "not a standard USB port … carries **+5V** power lines and the RS485 bus": 1 = +5 V, 2 (D−) = RS485-B, 3 (D+) = RS485-A, 4 = GND | Same | [SECONDARY] |
| USB-B | Built-in USB-serial (CH340, `usb-1a86_USB_Serial`), 19200 8N1, slave 4 | [xxx87/must-inverter-mon](https://github.com/xxx87/must-inverter-mon), [weby-homelab](https://github.com/weby-homelab/Must-Inverter-Dashboard) | [SECONDARY] |

### 7.4 Register table (decimal in the doc) [CONFIRMED-primary] (xlsx 1.4.15; signedness from mapper.py)
| Dec | Hex | Name | Unit / scale | Signed | Notes |
|---|---|---|---|---|---|
| 10101 | 0x2775 | Charger work enable | — | | |
| 10103 / 10104 | | Float / absorption voltage | 0.1 V | | |
| 10108 | | PV max charger current | 0.1 A | | |
| 10110 | 0x277E | **Battery type** | enum | | 0 none, 1 user-defined, 2 lithium, 3 sealed lead, 4 AGM, 5 GEL, 6 flooded |
| 10111 | | Battery Ah | 1 Ah | | |
| 15201 | 0x3B61 | Charger work state | enum | | 0 init, 1 self-test, 2 work, 3 stop |
| 15202 | 0x3B62 | MPPT state | enum | | 0 stop, 1 MPPT, 2 current limiting |
| 15203 | 0x3B63 | Charging state | enum | | 0 stop, 1 absorb, 2 float, 3 EQ |
| 15205 | 0x3B65 | **PV voltage** | 0.1 V | | |
| 15206 | 0x3B66 | Battery voltage (charger) | 0.1 V | | |
| 15207 | 0x3B67 | **PV charger current** | 0.1 A | | |
| 15208 | 0x3B68 | **PV charger power** | 1 W | | |
| 15209 / 15210 | 0x3B69 | Radiator / external temperature | 1 °C | S | |
| 15213 / 15214 | 0x3B6D | Charger error / warning bits | bits | | Table 7.6 |
| 15215 / 15216 | | Battery voltage grade (V) / rated current (0.1 A) | | | |
| 15217 / 15218 | 0x3B71 | **Accumulated PV energy** high / low | 1000 kWh / 0.1 kWh | | total = H×1000 + L×0.1 **[INFERRED]** |
| 16205–16208 | | PV2 data (PV19 only) | | | ha-must-inverter |
| 113 / 114 | 0x0071 | Battery SOC / SOH (PV19 only) | % | | ha-must-inverter |
| 20000 / 20001 | 0x4E20 | Machine type high ("PH/EP/PV") / low ("1800/3000") | ASCII / dec | | Probe target |
| 20102 / 20103 | | Output voltage / frequency setting | 0.1 V / 0.01 Hz | | |
| 20109 | 0x4E8D | **Energy use mode (output priority)** | enum | | 12 V/24 V units: 1 SBU, 3 UTI, 4 SOL (PV/PH); 48 V adds 2 SUB |
| 20111 | | Grid protect standard | enum | | 0 VDE4105, 1 UPS, 2 home, 3 GEN |
| 20125 | | Grid max charger current | 0.1 A | | |
| 20142 | | System setting bits | bits | | 0 OverLoadRestartForbid … 6 LcdLightEnable, 7 RecordFaultForbid |
| 20143 | 0x4EAF | **Charger source priority** | enum | | 0 solar first, 2 solar + utility, 3 only solar |
| 25201 | 0x6271 | **Work state** | enum | | 0 PowerOn, 1 SelfTest, 2 OffGrid, 3 Grid-Tie, 4 ByPass, 5 Stop, 6 Grid charging |
| 25205 | 0x6275 | **Battery voltage** | 0.1 V | S | |
| 25206 | 0x6276 | Inverter voltage | 0.1 V | S | |
| 25207 | 0x6277 | **Grid voltage** | 0.1 V | S | |
| 25208 | 0x6278 | Bus voltage | 0.1 V | S | |
| 25209–25212 | | Control / inverter / grid / load current | 0.1 A | S | |
| 25213 | 0x627D | P inverter | 1 W | S | |
| 25214 | 0x627E | **P grid** | 1 W | S | |
| 25215 | 0x627F | **P load** | 1 W | S | |
| 25216 | 0x6280 | **Load percent** | 1 % (the "0.01" in the xlsx unit column is inconsistent) | S | |
| 25217–25219 | | S inverter / grid / load | 1 VA | S | |
| 25221–25223 | | Q inverter / grid / load | 1 var | S | |
| 25225 / 25226 | 0x6289 / 0x628A | **Inverter / grid frequency** | 0.01 Hz | S | |
| 25233 / 25234 / 25235 | 0x6291–0x6293 | **AC radiator / transformer / DC radiator temperature** | 1 °C | S | |
| 25237–25242 | | Relay states (inverter, grid, load, N, DC, earth) | 0/1 | | |
| 25245–25260 | 0x629D… | Accumulated charger / discharger / buy / sell / load / self-use / PV-sell / grid-charger energy, each as (high 1000 kWh, low 0.1 kWh) | | | |
| 25261–25263 | 0x62AD | Error message 1–3 | bits | | Table 7.6 |
| 25265 / 25266 | 0x62B1 | Warning message 1–2 | bits | | |
| 25273 | 0x62B9 | **Battery power** | W | **S** | |
| 25274 | 0x62BA | **Battery current** | A | **S** | |
| 25275 | 0x62BB | Battery voltage grade | 12/24/48 | | |
| 25277 | 0x62BD | Rated power | W | | |
| 25279 | 0x62BF | Arrow flags (power flow) | bits | | 0 PV, 1 load, 2 battery, 3 grid present; 4 PV→machine; 5 machine→load; 6–7 battery arrow; 8–9 grid arrow |

- **Battery power/current sign** (25273/25274): the aquarat sample shows `"_battPower":"-146","_battCurrent":"-2"` while PV (`_chargerPower` 511 W) exceeds the load (`PLoad` 288 W). This suggests **negative = charging** **[INFERRED]** ([aquarat/must_inverter](https://github.com/aquarat/must_inverter)). Verify on the actual unit.
- **Battery SOC:** there is no SOC register in the 1.4.15 map for PV18. PV19 has 113. BMS values live at 15113/15115 in the PH1100 variant **[CONFIRMED-primary code]**.

### 7.5 MUST energy
Accumulated counters only (15217/18 and 25245–25260). There is no "today" counter in 1.4.15, so derive daily figures by differencing **[INFERRED]**.

### 7.6 MUST alarm bits [CONFIRMED-primary] (xlsx "Inverter Alarm" / "PV Charger Alarm")
**Error message 1 (25261):**

| Bit | Meaning |
|---|---|
| 0 | Fan locked while inverter off |
| 1 | Transformer over-temperature |
| 2 | Battery voltage too high |
| 3 | Battery voltage too low |
| 4 | Output short-circuited |
| 5 | Inverter output voltage high |
| 6 | Overload timeout |
| 7 | Bus voltage too high |
| 8 | Bus soft-start failed |
| 9 | Main relay failed |
| 10 | Output voltage sensor error |
| 11 | Grid voltage sensor error |
| 12 | Output current sensor error |
| 13 | Grid current sensor error |
| 14 | Load current sensor error |
| 15 | Grid over-current |

**Error message 2 (25262):**

| Bit | Meaning |
|---|---|
| 0 | Radiator over-temperature |
| 1 | Solar charger battery-voltage class error |
| 2 | Solar charger current sensor error |
| 3 | Solar charger current uncontrollable |
| 4 | Grid voltage low |
| 5 | Grid voltage high |
| 6 | Grid under-frequency |
| 7 | Grid over-frequency |
| 8 | Over-current protection |
| 9 | Bus voltage too low |
| 10 | Soft start failed |
| 11 | DC voltage in AC output |
| 12 | Battery connection open |
| 13 | Control current sensor error |
| 14 | Output voltage too low |

**Warning message 1 (25265):**

| Bit | Meaning |
|---|---|
| 0 | Fan locked |
| 1 | Fan 2 locked |
| 2 | Battery over-charged |
| 3 | Low battery |
| 4 | Overload |
| 5 | Output derating |
| 6 | Solar charger stopped: low battery |
| 7 | Solar charger stopped: high PV voltage |
| 8 | Solar charger stopped: overload |
| 9 | Solar charger over-temperature |
| 10 | PV charger communication error |

**Charger error (15213):**

| Bit | Meaning |
|---|---|
| 0 | Hardware protection |
| 1 | Over-current |
| 2 | Current sensor error |
| 3 | Over-temperature |
| 4 | PV voltage too high |
| 5 | PV voltage too low |
| 6 | Battery voltage too high |
| 7 | Battery voltage too low |
| 8 | Current uncontrollable |
| 9 | Parameter error |

**Charger warning (15214):** bit 0 = fan error.

---

## 8. Family E: other candidates

### 8.1 Sumry / GES "0x7530" map
Sources: [sumry_ges_7530/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/sumry_ges_7530/base.json), [community_ges_modbus_v263.json](https://github.com/groove-max/ha-eybond-local/blob/main/catalog/inverter_models/sources/community_ges_modbus_v263.json). **[CONFIRMED-primary code; protocol document not independently verified]**
- Identity: FC03 0xC738 (51000) model code, 0xC739 product class, 0xC768 (51048) protocol version. One device read model 45, class 10, protocol 220.
- Telemetry:

  | Register | Value |
  |---|---|
  | 30000 (0x7530) | Battery V (0.1 V) |
  | 30001 | Battery current (0.1 A, signed) |
  | 30002 | SOC |
  | 30024 / 30025 / 30026 | Output V / A / Hz |
  | 30058 / 30059 / 30060 | Mains V / A / Hz |

- "FC03 reads are limited to **32 registers**; 32-bit fields are **low-word-first**" (V2.6.3, 2025-08-08).

### 8.2 PI30 / Voltronic ASCII (not Modbus, but common in this class)
- RJ45 RS232: pin 1 TX, 2 RX, 8 GND. "The inverter provides **+12V on pin 4 or 7** depending on the model" **[CONFIRMED-primary]** ([esphome-pipsolar README](https://github.com/syssi/esphome-pipsolar)).
- Baud: 2400 in the pipsolar debug config and in eybond `link_hints` for `pi30` **[CONFIRMED-primary code]**. A Daxtromn AGH owner reported WatchPower at "COM port 9600" **[SECONDARY]** ([#66](https://github.com/syssi/esphome-smg-ii/issues/66)).
- Frame: ASCII command + 2-byte CRC (`crcPI`: a CRC-16/XMODEM-style table; any CRC byte equal to 0x28, 0x0D, 0x0A or 0x00 is incremented by one) + `\r` **[CONFIRMED-primary code]** ([mpp-solar protocol_helpers.py](https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/protocol_helpers.py)).
- Frames:
  - `QPI` = `51 50 49 BE AC 0D`
  - `QPIGS` = `51 50 49 47 53 B7 A9 0D`
  - `QPIRI` = `… F8 54 0D`
  - `QMOD` = `51 4D 4F 44 49 C1 0D`
  - `QPIWS` = `… B4 DA 0D`
  - These match the pipsolar test log in [#66](https://github.com/syssi/esphome-smg-ii/issues/66) (`"QPI\xBE\xAC\r"`, `"QPIGS\xB7\xA9\r"`) **[CONFIRMED by computation, INFERRED]**.
- `QPIGS` field order **[CONFIRMED-primary code]** ([mpp-solar pi30.py](https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/pi30.py)):

  | Index | Field |
  |---|---|
  | 0 | AC input V |
  | 1 | AC input Hz |
  | 2 | AC output V |
  | 3 | AC output Hz |
  | 4 | Output VA |
  | 5 | Output W |
  | 6 | Load % |
  | 7 | Bus V |
  | 8 | Battery V |
  | 9 | Battery charging A |
  | 10 | Battery capacity % |
  | 11 | Heatsink °C |
  | 12 | PV input current for battery (A) |
  | 13 | PV input V |
  | 14 | Battery V from SCC |
  | 15 | Battery discharge A |
  | 16 | Device status flags |
  | 17–18 | Reserved |
  | 19 | PV input power (W) |
  | 20 | Device status 2 |

- An EASUN "SMH-II 3.2" (a 3.2 kW unit) uses the pip protocol, not SMG Modbus **[SECONDARY]** ([#80](https://github.com/syssi/esphome-smg-ii/issues/80)).
- PI18 (`^P005PI`, `^P007PIRI` …) is a related ASCII dialect **[CONFIRMED-primary data]** ([inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json)).

### 8.3 EyeBond "G-ASCII" (24 V 3.6 kW units: Gootu GT-H2436M14P5, LVYUAN TY-SIC-3.6KBE-W1)
- "plain ASCII commands terminated by `\r` … without PI30 CRC bytes" **[CONFIRMED-primary code]** ([ascii_line.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/payload/ascii_line.py)).
- Probe commands: `GPDAT0`, `GPV`. The family also has `GBAT`, `GCHG`, `GOP`, `GLINE`, `GINV`, `GWS`, `GTMP`, `SVFW`, `GMOD`, `F`, `BL`. Unsupported commands answer `NAK`/`NOA`/`ERCRC`.
- Fingerprint, `F` rating: 230 V / 15 A / 24 V / 50 Hz and SVFW 4.007 for the Gootu; 220 V / 16 A / 24 V / 50 Hz and 4.003 for the LVYUAN **[CONFIRMED-primary data]** ([inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json), [documentation_jsdsolar_g_ascii_protocol.json](https://github.com/groove-max/ha-eybond-local/blob/main/catalog/inverter_models/sources/documentation_jsdsolar_g_ascii_protocol.json)).
- The baud rate and the physical port are not given in the sources I read. A probe should try 2400 and 9600 **[INFERRED]**.
- Relevance: these are 24 V-class Chinese all-in-ones of the same price class as the Savx. JsdSolar is named as the protocol author **[CONFIRMED-primary data]**.

### 8.4 Round-dial 3.2 kW / 24 V / 500 Voc candidates (Datouboss / Sumry / Y&H / Daxtromn)
I could not find a public listing matching all of the Savx nameplate values (3200 W, 24 V, 145 A DC, 30–450 V MPPT, 500 Voc, 3500 W PV, 100 A AC charging) **[SECONDARY, negative result]**.
- PowMr HVM3.2H-24V: "30-400V", 80 A.
- PowMr HVM3.5KW-24V: 500 Voc, 55–450 V, 100 A.
- Y&H 3200 VA: 55–450 V, 80 A.
- Search results: [PowMr HVM3.2H](https://powmr.com/products/all-in-one-inverter-charger-3000watt-220vac-24vdc), [eBay Y&H 3200VA](https://www.ebay.com/itm/226715729314).

What is known about each brand's protocol:
- **Daxtromn AGH** → PI30 **[SECONDARY]** ([#66](https://github.com/syssi/esphome-smg-ii/issues/66)).
- **Datouboss** → SRNE-derived, non-standard map **[SECONDARY]**.
- **Y&H** → SRNE **[SECONDARY]**.
- **Sumry** → the OEM name SolarAssistant uses for the SMG-II driver. Sumry HGX uses 8851 **[SECONDARY]** ([SolarAssistant HGX](https://solar-assistant.io/help/inverters/sumry/HGX), [lufton](https://github.com/lufton/esphome-inv-8851)).

Conclusion: the probe in section 10 must cover SMG, SRNE, MUST, PowMr HVM, 8851, PI30 and G-ASCII **[INFERRED]**.

---

## 9. SmartESS / EyeBond "WiFi-Plug" dongle: how it connects

### 9.1 Electrical side
- **Inside:** "EASUN remote monitoring WIFI module … has an esp32 chip with an RS232 converter" and is powered by **12 VDC** from the inverter through a DC/DC converter **[SECONDARY]** ([#88](https://github.com/syssi/esphome-smg-ii/issues/88)). The PowMr 4.5K/6.5K DTU **WBS1-V001** is an ESP module with +5V, GND1, BOOT1, RXD1 and TXD1 pads **[CONFIRMED-primary]** ([lufton README](https://github.com/lufton/esphome-inv-8851)).
- **Connector types and supply voltage:**

  | Connector | Signals and supply | Tag |
  |---|---|---|
  | RJ45 RS232 (SMG/PIP/PowMr) | +12 V on pin 4 (pin 7 on some PIP) | [CONFIRMED-primary] |
  | DB9 RS232 | SolarAssistant's "SunSynk USB RS232 cable (WiFi dongle route): Pin 2 = RX, Pin 3 = TX, Pin 5 = GND" | [SECONDARY] ([SolarAssistant](https://solar-assistant.io/help/inverters/easun/ISOLAR-SMG-II-4kW-24V/rs232)) |
  | RJ45 "WIFI" (SRNE) | RS485 on pins 7/8 and **+5 V on pin 1** | [SECONDARY] |
  | USB-A shaped "WiFi" (MUST) | RS485 on D+/D− and +5 V on VBUS | [SECONDARY] |
  | USB-A COM (SMG 6 kW) | RS232 on D+/D−; VBUS unused | [CONFIRMED-primary] |
  | Built-in (newer SMG-II / Anenji) | EB-WFBLE-03 module on a 5-pin header at 3.3 V TTL with +5 V | [SECONDARY] |

- syssi: "There is SmartESS dongle for PIP and a special version for Modbus communication" **[SECONDARY]** ([#66](https://github.com/syssi/esphome-smg-ii/issues/66)). A SmartESS dongle in the box therefore does not tell you the protocol.

### 9.2 Network side
- **Discovery and redirect:** UDP **58899** `set>server=<ip>:<port>;`. The dongle then connects to TCP **8899** (cloud ports seen: **18899**, 502).
- **Frame header:** `[tid:2][devcode:2][len:2][addr:1][fc:1]`, big-endian, where `len` = total length − 6. FC 1 = heartbeat (payload starts with a 14-char collector PN), FC 2/3 = query/set collector, **FC 4 = forward a raw serial payload to the inverter**, FC 17/18/19 = cloud triggers **[CONFIRMED-primary code]** ([collector/protocol.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/collector/protocol.py), [ha-smartess-local](https://github.com/ubombi/ha-smartess-local), [Local-Cloud-Bridge](https://github.com/samuelolteanu/Local-Cloud-Bridge-for-Anenji-Easun-MPP-Solar-Inverters)).
- **devcodes:** 1 = Modbus targets (SMG, SRNE, MUST); 2452 (0x0994) = PI30/PI18/G-ASCII; 258; 767 = short-ASCII **[CONFIRMED-primary data]** ([inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json)).
- **Serial settings the collector reported:** "SmartESS protocol asset 02FF … collector serial baudrate **19200-8-1-0**" for MUST PV18-3024 **[SECONDARY]**.
- **Replacing the dongle:** [esp-eybond-collector](https://github.com/groove-max/esp-eybond-collector) is ESPHome firmware that impersonates a collector so EyeBond Local can auto-detect the family. Its presets: ESP32 TX GPIO17, RX GPIO16, DE/RE GPIO4, default 9600. "PI30 / Voltronic-style inverters commonly use `2400`; SMG / Modbus inverters commonly use `9600`" **[CONFIRMED-primary]**.

---

## 10. Runtime probe (auto-detect) design

### 10.1 Rules
- **Only read** (FC 03 or ASCII queries). **Never write during probing.** Never send 8851 FC 0x0010, PI30 setters, or SMG/SRNE writes **[INFERRED]**.
- **Listen before talking.** Sniff the bus for 3–5 s at each baud rate. If you see frames you did not send (a dongle or BMS master), you have learned the family and must not transmit, or must use quiet slots as the Datouboss project does **[INFERRED; SECONDARY for the bus-sharing behaviour]**.
- **Gaps and timeouts:**
  - Modbus RTU needs at least 3.5 character times of silence between frames: about 3.6 ms at 9600 and 1.75 ms above 19200. ESPHome: "Above 19200 baud the specification fixes that gap at 1.75 ms … At 19200 baud and below the gap scales with the baud rate … 3.5 bytes wide" **[CONFIRMED-primary]** ([ESPHome modbus](https://esphome.io/components/modbus/)).
  - The inverters in these projects are slow. Use 1000 ms response timeouts and 200–600 ms between requests (syssi `send_wait_time: 200ms`; ESPHome defaults `send_wait_time` 2000 ms and `turnaround_time` 600 ms; odya `command_throttle: 1s`) **[CONFIRMED-primary]**.
- **Validate every reply:**
  - Correct address and function, `byte_count == 2*N`, CRC OK.
  - **Not all-zero:** SMG zero-fills unreadable ranges.
  - **Not all-0xFFFF.**
  - Values physically plausible for a 24 V / 230 V system: battery 20.0–32.0 V, grid/output 0 or 180–260 V, frequency 0 or 45–65 Hz, SOC 0–100 **[INFERRED]**.

### 10.2 Ordered probe table

All CRCs below are CRC-16/MODBUS, low byte first, and were verified **[INFERRED]**.

**9600 8N1** (SMG, SRNE, 8851, Sumry/GES):

| Step | Family test | TX (hex) | Valid reply looks like | Discriminator |
|---|---|---|---|---|
| 1 | SMG mode | `01 03 00 C9 00 01 54 34` | `01 03 02 00 0m CRC`, m ∈ 0..6 (e.g. Off-grid: `01 03 02 00 03 F8 45`) | On SRNE, 0x00C9 is undefined → exception `01 83 02 C0 F1` or silence |
| 2 | SMG live block | `01 03 00 C9 00 22 15 ED` (201×34) | 73-byte reply; 215 = battery 200–320 (×0.1 V); 202/210 ≈ 2300; 203/212 ≈ 5000 | Reject if all zero |
| 3 | SMG identity | `01 03 00 B8 00 01 04 2F` (184), `01 03 00 AB 00 01 F5 EA` (171), `01 03 02 83 00 01 74 5A` (643), `01 03 00 BA 00 0C 64 2A` (serial 186×12) | 184 ∈ {1, 2, 11} → classic/GM6200/variant; {3, 4, 5, 6} → Anenji 3-10 map; 643 ≈ 3000–3500 | Pick the register map (section 2.9) |
| 3b | SMG energy (GM6200 only) | `01 03 01 BB 00 03 74 12` (443–445) | Non-zero, increasing | Energy available? |
| 4 | SRNE product type | `01 03 00 0B 00 01 F5 C8` | `01 03 02 00 04 B9 87` (4 = all-in-one) | On SMG, 0x000B lies in the reserved region → zeros |
| 5 | SRNE DC data | `01 03 01 00 00 03 04 37` (SOC, V, I) | SOC 0–100; V 200–320 (24 V); I signed | On SMG, 256 is reserved → zeros |
| 6 | SRNE state + serial | `01 03 02 10 00 01 84 77` (0x0210); `01 03 00 35 00 14 55 CB` (20 ASCII low bytes) | State 0..10; serial printable | eybond test: "SR" in the serial |
| 6b | SRNE universal address | `FF 03 01 00 00 03 11 E9` | The reply carries the real address | Use only if address 1 is silent |
| 7 | 8851 state | `88 51 00 03 00 00 00 00 4D 08` | Starts `88 51 00 03 00 00`, 154 or 158 bytes, CRC OK | Non-Modbus header |
| 8 | Sumry/GES identity | `01 03 C7 38 00 02 78 B2` | 2 words (model, class) | — |

**19200 8N1** (MUST):

| Step | Family test | TX (hex) | Valid reply looks like | Discriminator |
|---|---|---|---|---|
| 9 | MUST model | `04 03 4E 20 00 02 D2 BC` | `04 03 04 'P' 'V' 07 08 …` (PV1800 → `04 03 04 50 56 07 08 5D D5`) [INFERRED] | ASCII PV / PH / EP |
| 10 | MUST live | `04 03 62 71 00 4F 4B C8` (25201×79), `04 03 3B 61 00 15 D8 AA` (15201×21) | 25201 ∈ 0..6; 25205 battery V | — |

**2400 8N1** (PowMr HVM, PI30):

| Step | Family test | TX (hex) | Valid reply looks like | Discriminator |
|---|---|---|---|---|
| 11 | PowMr HVM | `05 03 11 95 00 2D 91 43` | `05 03 5A` + 90 data bytes + CRC; byte-swap the words; 4506 battery V plausible | Slave 5 |
| 12 | PI30 | `51 50 49 BE AC 0D` ("QPI") | `(PI30` + CRC + CR, e.g. `28 50 49 33 30 9A 0B 0D` | ASCII "(" |
| 12b | PI30 status | `51 50 49 47 53 B7 A9 0D` ("QPIGS") | `(` + 21 space-separated fields + CRC + CR | — |

**2400 and 9600 (ASCII):**

| Step | Family test | TX | Valid reply looks like |
|---|---|---|---|
| 13 | G-ASCII | `GPDAT0\r`, `GPV\r` (no CRC) | Printable line, or `NAK` |
| 14 | PI30 at 9600 (Daxtromn report) | As step 12 | As step 12 |

Run the sequence over **both RS485 polarities** (swap A/B if nothing answers) and over each candidate physical port **[INFERRED]**.

### 10.3 CRC-16/MODBUS (C/C++ for ESP32) [CONFIRMED-primary algorithm]
This is the bitwise algorithm in SRNE V1.96 §4 "Method 1". The vendor docs' table-driven versions give identical results.

```cpp
// Poly 0xA001 (reflected 0x8005), init 0xFFFF; append low byte first.
uint16_t crc16_modbus(const uint8_t *p, size_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}
size_t build_read(uint8_t *f, uint8_t slave, uint16_t reg, uint16_t cnt) {
  f[0]=slave; f[1]=0x03; f[2]=reg>>8; f[3]=reg; f[4]=cnt>>8; f[5]=cnt;
  uint16_t c = crc16_modbus(f, 6); f[6]=c & 0xFF; f[7]=c >> 8; return 8;
}
bool valid_reply(const uint8_t *r, size_t n, uint8_t slave, uint16_t cnt) {
  if (n < 5 || r[0] != slave) return false;
  if (r[1] & 0x80) return false;                       // exception (0x83/0x86/0x90)
  if (r[1] != 0x03 || r[2] != cnt*2 || n != 5u + cnt*2) return false;
  uint16_t c = crc16_modbus(r, n-2);
  if (r[n-2] != (c & 0xFF) || r[n-1] != (c >> 8)) return false;
  bool allz = true, allf = true;                       // SMG zero-fill / 0xFFFF sentinel
  for (size_t i = 3; i < n-2; i += 2) {
    uint16_t w = (r[i] << 8) | r[i+1];
    allz &= (w == 0); allf &= (w == 0xFFFF);
  }
  return !allz && !allf;
}
```

PI30 CRC (`crcPI`) **[CONFIRMED-primary code]** (mpp-solar): a nibble table `{0x0000,0x1021,…,0xF1EF}` with init 0 and big-endian output (high byte first), followed by the 0x28/0x0D/0x0A/0x00 → +1 adjustment on each byte. See [protocol_helpers.py](https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/protocol_helpers.py).

### 10.4 Decision logic [INFERRED]
```
for baud in [9600, 19200, 2400]:
  sniff 3 s -> if foreign traffic: classify by header (01 03 / 04 03 / 05 03 / 88 51 / '(' / 'Q') and STOP transmitting
  9600:  SMG(1..3) -> SRNE(4..6, 6b) -> 8851(7) -> GES(8) -> G-ASCII/PI30@9600
  19200: MUST(9..10)
  2400:  PowMr(11) -> PI30(12) -> G-ASCII
choose the family with >=2 independent plausible checks; persist {baud, slave, family, map variant(184), sign convention}
```
- **SMG vs SRNE:** both answer at slave 1, 9600. SMG answers 201 with 0..6 and returns zeros at 0x0100/0x000B. SRNE answers 0x000B = 4 and 0x0100 with a plausible SOC/voltage, and raises exception 02 at 0x00C9.
- **Sign calibration:** once the family is known, read battery current together with charge state (SRNE 0x010B, MUST 15203, SMG 201 = Charging / 232 sign) to fix the charge/discharge sign.

---

## 11. Cross-family quirks

| Quirk | SMG | SRNE | MUST | PowMr HVM | 8851 |
|---|---|---|---|---|---|
| Wake-up needed | **Yes** on the USB-A 6 kW firmware: `01 AA 06 DE A2` once, then poll at intervals of 10 s or less [CONFIRMED-primary] | Not reported | Not reported | Not reported | Not reported |
| Recommended spacing | 200 ms send wait, 10 s update [CONFIRMED-primary] | Fast polls work (Datouboss 1–20 s) [SECONDARY] | 200 ms throttle; 0.1 s between blocks on PV19 [CONFIRMED-primary] | 1 s throttle, 250 ms wait [CONFIRMED-primary] | 5 s default update [CONFIRMED-primary] |
| Max registers per read | Not documented; 44 works | **32** (doc); **20** on some firmware | 114 works | 45 (dongle); splitting advised | Whole packet |
| 32-bit word order | **High word first** | **Low word first** | Hi/lo pairs in different units (1000 kWh / 0.1 kWh) | n/a | n/a |
| Byte order within a word | Big-endian | Big-endian | Big-endian | **Little-endian** | **Little-endian** |
| Battery current sign | 232: + charge / − discharge; 216/217 signed | 0x102: doc says + = **discharge** (firmware varies) | 25274/25273 signed (negative appears to mean charging [INFERRED]) | Separate unsigned charge (4508) and discharge (4509) | Signed, negative = discharging |
| Bad-read behaviour | **Zero-filled data**, no exception | Exception 02 / 0A | Exception | Unknown | No reply |
| Shares a port with the BMS | RS485 logger disabled when BMS-485 is enabled [SECONDARY] | BMS 485/CAN selected in 0xE215 | Separate RJ45 | — | — |

---

## 12. Implications for the purchased hardware [INFERRED unless cited]

- **RS485 module (auto-direction, VCC/TXD/RXD/GND, A+/B−):**
  - Correct for SMG (RS485/BMS RJ45: pin 1 B−, pin 2 A+, pin 8 GND), SRNE (WIFI RJ45 pins 7/8, GND pin 2), MUST (RJ45 pin 1 A, pin 2 B, pin 8 GND, or the USB-A pins) and Sumry/GES.
  - Power it from the ESP32-S3's **3.3 V** so its RXD output is at 3.3 V levels. The MUST ESPHome maintainer advised a user with a MAX485 board: "You should use 3.3V instead of the 5V, it affects the TTL voltage" **[SECONDARY]** ([MUST discussion #9](https://github.com/vladyspavlov/esphome-must-inverter/discussions/9)).
  - Auto-direction modules need no `flow_control_pin`. This is recommended over DE/RE modules **[SECONDARY]** ([#101](https://github.com/syssi/esphome-smg-ii/issues/101), [RAR WIRING.md](https://github.com/RAR/esphome-srne-inverter/blob/main/WIRING.md)).
- **If only an RS232 RJ45/DB9 port is present** (PI30, PowMr HVM, 8851, or SMG without an RS485 jack), buy a **MAX3232** RS232↔TTL module. Never wire RS232 (±12 V) to the ESP or to the RS485 module.
- **Before connecting anything, measure each RJ45/USB-A pin against GND with a multimeter:**
  - **+12 V** → pin 4 on the RS232 jack.
  - **+5 V** → pin 1 on the SRNE WIFI jack, or VBUS on USB-A.
  - **Negative idle voltage (about −5 to −12 V)** on a data pin → RS232 TX.
  - About **+2.5 V on two pins with a small difference between them** → RS485 A/B idle bias.
  - (Electrical rules of thumb.)
- **The inverter's 5 V or 12 V can power the logger** through a buck converter. Keep the logger's ground common with the inverter's comms ground only. The eybond docs warn: "Avoid powering the ESP from a laptop while it is connected to a mains-powered inverter" **[SECONDARY]** ([FLASHING.md](https://github.com/groove-max/esp-eybond-collector/blob/main/docs/FLASHING.md)).
- **ESP32-S3-N16R8 UART pins:** use UART1 on free GPIOs such as GPIO17 (TX) / GPIO18 (RX). Avoid GPIO43/44 (the UART0 console on the "COM" USB-C), strapping pins 0/3/45/46, and **GPIO33–37, which the octal PSRAM uses on R8 modules**. Treat this as a design note to check against the Espressif GPIO docs.
- **Next physical steps:**
  1. Open the bottom cover and photograph the ports. Look for labels such as "RS232/COM", "RS485/BMS", "WIFI", "USB", "CAN".
  2. Check the seller box for a SmartESS / WiFi-Plug dongle. Its connector reveals the link type.
  3. If the dongle is present, either sniff its traffic (listen-only) with the RS485 module on A/B, or read the "protocol asset id" in the SmartESS app/collector.
  4. Then run the section-10 probe.

---

## 13. Source index (all fetched 2026-10-21)

**Primary documents**
- SMG RS232 Modbus V1.0.1: https://github.com/syssi/esphome-smg-ii/blob/main/docs/SMG-RS232%2BCommunication%2BProtocol%2BV1.0.1.pdf
- GM6200 Protocol 2 (CN): https://github.com/syssi/esphome-smg-ii/blob/main/docs/GM6200%2BCommunication%2BProtocol%2B2_CN.html
- SRNE Modbus V1.96: https://github.com/syssi/esphome-smg-ii/blob/main/docs/SRNE.Solar.Charge.Inverter.MODBUS.Protocol1.96.pdf
- Anenji RS232 Protocol V1.0-220209: https://github.com/user-attachments/files/20907869/RS232.Communication.Protocol.V1.0-220209.pdf
- MUST PH/PV/EP1800 Modbus 1.4.15 (xlsx): https://github.com/vladyspavlov/esphome-must-inverter/tree/main/docs

**Code and configurations**
- syssi/esphome-smg-ii: https://github.com/syssi/esphome-smg-ii (README, esp32-example.yaml, esp32-gm6200-example.yaml, esp32-s2-mini-usb-a-example.yaml, esp32-srne-asf-example.yaml)
- syssi/esphome-pipsolar: https://github.com/syssi/esphome-pipsolar
- groove-max/ha-eybond-local: https://github.com/groove-max/ha-eybond-local (inverter_catalog.json, register_schemas/*, drivers/smg.py, drivers/srne.py, collector/protocol.py, payload/ascii_line.py)
- groove-max/esp-eybond-collector: https://github.com/groove-max/esp-eybond-collector
- odya/esphome-powmr-hybrid-inverter: https://github.com/odya/esphome-powmr-hybrid-inverter
- leodesigner/powmr_comm: https://github.com/leodesigner/powmr_comm
- leodesigner/powmr4500_comm: https://github.com/leodesigner/powmr4500_comm
- lufton/esphome-inv-8851: https://github.com/lufton/esphome-inv-8851
- vladyspavlov/esphome-must-inverter: https://github.com/vladyspavlov/esphome-must-inverter
- mukaschultze/ha-must-inverter: https://github.com/mukaschultze/ha-must-inverter
- davidrapan/ha-solarman (anenji_hybrid.yaml, srne_asf.yaml): https://github.com/davidrapan/ha-solarman
- jblance/mpp-solar (pi30.py, protocol_helpers.py): https://github.com/jblance/mpp-solar
- HotNoob/PythonProtocolGateway SRNE maps: https://github.com/HotNoob/PythonProtocolGateway/tree/main/protocols/srne

**Secondary (READMEs, issues, community)**
- RAR/esphome-srne-inverter: https://github.com/RAR/esphome-srne-inverter
- phinix-org/SRNE-inverters-by-modbus-rs485: https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485
- aogden24-byte/esphome-datouboss: https://github.com/aogden24-byte/esphome-datouboss
- duwke/Y-H-5000U: https://github.com/duwke/Y-H-5000U
- PreK/esphome-anenji-11kw-native-modbus: https://github.com/PreK/esphome-anenji-11kw-native-modbus
- psalkiewicz/esphome-2026-anenji4200: https://github.com/psalkiewicz/esphome-2026-anenji4200
- jul-ienfr/esphome-anenji-4.2kw-wifi: https://github.com/jul-ienfr/esphome-anenji-4.2kw-wifi
- ubombi/ha-smartess-local: https://github.com/ubombi/ha-smartess-local
- Paxy/SmartESS-proxy: https://github.com/Paxy/SmartESS-proxy
- samuelolteanu/Local-Cloud-Bridge: https://github.com/samuelolteanu/Local-Cloud-Bridge-for-Anenji-Easun-MPP-Solar-Inverters
- aquarat/must_inverter: https://github.com/aquarat/must_inverter
- xxx87/must-inverter-mon: https://github.com/xxx87/must-inverter-mon
- weby-homelab/Must-Inverter-Dashboard: https://github.com/weby-homelab/Must-Inverter-Dashboard
- syssi/esphome-smg-ii issues #66, #80, #88, #101, #130: https://github.com/syssi/esphome-smg-ii/issues
- SolarAssistant help pages:
  - https://solar-assistant.io/help/inverters
  - https://solar-assistant.io/help/inverters/easun/ISOLAR-SMG-II-4kW-24V (plus the /rs232 and /eybond-sumry sub-pages)
  - https://solar-assistant.io/help/inverters/anenji/ANJ-4000W-24V
  - https://solar-assistant.io/help/inverters/sumry/HGX
- ESPHome modbus docs: https://esphome.io/components/modbus/
- PowMr HVM3.2H product page: https://powmr.com/products/all-in-one-inverter-charger-3000watt-220vac-24vdc

**Pages that blocked fetching (HTTP 403)**
- powerforum.co.za topic 11341
- diysolarforum thread 119887
- daxtromn-power.com protocol blog
- eu.powmr.com (Cloudflare challenge)
