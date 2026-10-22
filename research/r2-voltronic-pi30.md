# R2: Voltronic "PI30" ASCII protocol and its relatives, for an ESP32-S3 datalogger

Research date: 2026-10-21. Scope: protocol, physical layer, framing/CRC, field tables, timing, clone quirks, ESP32 implementations, WiFi dongles, and what this means for the Savx F4 3KW (24 V).

Confidence tags: **[CONFIRMED-primary]** = read in an official protocol PDF or in source code / a captured byte trace that I re-checked. **[SECONDARY]** = forum, README, issue thread, listing. **[INFERRED]** = my reasoning.

---

## 0. Key takeaways for the plan writer

1. **The PI30 framing is simple and verified.** Send the ASCII command, then 2 CRC bytes, then CR (0x0D). The reply is `(` + space-separated ASCII fields + 2 CRC bytes + CR. The CRC is CRC-16/XMODEM (poly 0x1021, init 0). Any CRC byte equal to 0x28, 0x0D or 0x0A is incremented by one. Serial settings are 2400 baud 8N1. I re-computed the CRCs, and they match captured traffic and the open-source implementations:
   - `QPIGS` → `51 50 49 47 53 B7 A9 0D`
   - `QPI` → `BE AC`
   - `QMOD` → `49 C1`
   - `QPIRI` → `F8 54`
   - `QPIWS` → `B4 DA`

   [CONFIRMED-primary] Sources: §4.
2. **The physical port matters more than the protocol.** On classic Voltronic units the monitoring port is RS232 on an RJ45: pin 1 is the inverter's TX, pin 2 its RX and pin 8 is GND. Some models have +12 V on pin 4 or pin 7. **An RS485 module cannot talk to an RS232 port.** [CONFIRMED-primary pinout from the Voltronic PDF cable drawing; SECONDARY for the RS485 point, from esphome-pipsolar issue #111.]
3. **This user's inverter has an RJ45 port labelled "RS485"** (per the user-constraints note) and the user owns only an auto-direction RS485↔TTL module. Three kinds of evidence exist for "Voltronic-look" 24 V / 3 kW units with an RS485 port:
   - Some speak **PI30 over RS485**. The SUOER VMS-V-12K-48-HG was reported working with an *ESP32-S3-N16R8 plus an RS485-to-TTL module* and Solar2MQTT. [SECONDARY]
   - Some speak a **legacy UPS-style `Q1`/`Q6` protocol** at 2400 8N1. The Voltronic RP/RS series (RS3024, RP6000) answers `#-1\r` to every PI30 command. [SECONDARY]
   - Others speak **Modbus RTU** at 2400, 9600 or 19200 baud. [CONFIRMED-primary in Solar2MQTT source]

   **So the firmware must probe:** PI30 (`QPI`), then PI18 (`^P005PI`), then `Q1`/`Q6`, then Modbus at several baud rates. §9 gives a ready algorithm.
4. **Field layouts vary by firmware and model family.**
   - QPIGS has 17, 21 or 24 fields.
   - QPIRI has 25, 26/27 or 28 fields.
   - QPIWS has 32, 36 or 38 characters.
   - Number formats vary too. PV current comes as `0011` on some units and `02.7` on others. Max charge current comes as `50` or `060`. Parallel-max can be `-`.

   Parse by index, tolerate missing tail fields, and always store the raw reply string. [CONFIRMED-primary, from the docs and from CRC-valid sample frames]
5. **The logger should be read-only by default.** Use a whitelist of `Q*` inquiry commands. Setter commands include `PF` (factory defaults), `PBT`/`PSDV`/`PCVV` (battery limits) and `RTEY`/`RTDL` (erase energy and data logs). Never send them automatically. [CONFIRMED-primary: command meanings come from the PDFs]
6. **Timing.** 2400 baud is about 4.17 ms per byte, so a full QPIGS reply (about 110 bytes) takes about 0.46 s on the wire.
   - Implementations use a 1–5 s reply timeout, about 200 ms inter-byte timeout, and poll QPIGS every 1–10 s.
   - Recommended for this logger: one command at a time, ≥1.5 s reply timeout, QPIGS/QMOD/QPIWS every 5–10 s, QPIRI/QFLAG every 5–10 min.

   [CONFIRMED-primary for the implementations' constants; INFERRED for the recommendation]
7. **Basic 3.2 kW / 24 V PI30 units usually do NOT support energy counters** (`QET`/`QED`). An EASun SP-3200 answered NAK. The logger should integrate power × time itself. [SECONDARY]

---

## 1. Primary sources used

| ID | Document / code | URL |
|---|---|---|
| P2014 | Voltronic "HS_MS_MSX RS232 Protocol 20140822 after current upgrade" (PI30) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf (also https://forums.aeva.asn.au/uploads/293/HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf as cited by ned-kelly) |
| P2015 | "Inverter Communication Protocol 20150924 Customer" (PI30, KS/MKS/Plus Duo) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Communication-Protocol-20150924-Customer.pdf |
| PGK | PI30 "PIP-GK/MK Protocol" | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_PIP-GK_MK-Protocol.pdf |
| PMAX | "MAX Communication Protocol 20210217" (PI30MAX) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30MAX.Communication.Protocol20210217.pdf |
| PREVO | "Revo RS232 Communication Protocol V02/V03" | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf |
| P41 | "LV5048 5KW protocol 20190222" (PI41) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI41_LV5048.5KW.protocol-20190222.for.customer.pdf |
| P18 | "InfiniSolar V protocol 20170926" (PI18) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI18_InfiniSolar-V-protocol-20170926.pdf |
| P17 | "InfiniSolar 5KW protocol 20160309" (PI17) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI17_InfiniSolar-5KW-protocol-20160309.pdf |
| P16 | "Voltronic Power SUNNY protocol" (PI15/PI16, from NUT) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI16_VoltronicPowerSUNNYProtocol.md |
| Q1DOC | PI30 Q1 field notes (AEVA forum, mirrored) | https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Q1%20definition.md |
| MPP | jblance/mpp-solar protocol definitions (`pi30.py`, `pi30max.py`, `protocol_helpers.py`, `abstractprotocol.py`, `inout/hidrawio.py`, `inout/serialio.py`) | https://github.com/jblance/mpp-solar/tree/master/mppsolar (last push 2026-04-12) |
| ESPC | ESPHome core `pipsolar` component | https://github.com/esphome/esphome/tree/dev/esphome/components/pipsolar , docs https://esphome.io/components/pipsolar/ |
| SYS | syssi/esphome-pipsolar (pip8048, pip2424mse1, pi18 external components, PDUs, protocol-probe YAML) | https://github.com/syssi/esphome-pipsolar (last push 2026-10-14) |
| NED | ned-kelly/docker-voltronic-homeassistant (`inverter.cpp`, `inverter.conf`) | https://github.com/ned-kelly/docker-voltronic-homeassistant |
| S2M | softwarecrash/Solar2MQTT (ESP32 firmware; `PI_Serial.cpp`, `PiReplyReader.h`, `PROTOCOL_REFERENCE.md`, raw dumps, wiki) | https://github.com/softwarecrash/Solar2MQTT (last push 2026-10-19) |
| NUT | Network UPS Tools `nutdrv_qx.c` (Cypress USB-HID transport for 0665:5161) | https://github.com/networkupstools/nut/blob/master/drivers/nutdrv_qx.c |
| TRIPP | Tripp Lite APSWX 230 V owner's manual (Voltronic-style panel and RJ45) | https://tripplite.eaton.com/support/owners-manual/932564 |
| PMR | leodesigner/powmr_comm (PowMr RJ45 pinout, Modbus) | https://github.com/leodesigner/powmr_comm |
| RP | aguglie/esphome-rp6000 and syssi/esphome-config-examples `esp8266-voltronic-rp-rs.yaml` (RS485, Q6) | https://github.com/aguglie/esphome-rp6000 , https://github.com/syssi/esphome-config-examples/blob/main/esp8266-voltronic-rp-rs.yaml |
| I166 | esphome-pipsolar issue #166 "Voltronic 24V/3kW not showing any data" (RS485 port case study) | https://github.com/syssi/esphome-pipsolar/issues/166 |

---

## 2. The protocol family at a glance

| Protocol ID (reply to `QPI` or `^P005PI`) | Framing | Checksum | Typical devices | Notes |
|---|---|---|---|---|
| **PI30** | `CMD<CRC><cr>` → `(...<CRC><cr>` | CRC-16/XMODEM + byte bump | Axpert / PIP HS, MS, MSX, KS, MKS, VM II/III, PowMr HVM, EASun SP/SMH, Sumry, Anern, MPP Solar PIP | P2014, P2015. [CONFIRMED-primary] |
| **PI30 MAX** | same | same | Axpert MAX, LV6048MAX, Phocos Any-Grid PSW-H, "VMII-NXPW5KW" clones | Adds QPIGS2 (PV2), energy counters, 24-field QPIGS, 28-field QPIRI. `QPI` still answers `PI30`. [CONFIRMED-primary PMAX; S2M] |
| **PI30 PIP-GK/MK** | same | same | PIP-GK, PIP-MK, Axpert VM IV | Extra QMOD letters (C, Y, E, D), 36-character QPIWS with embedded fault code. [CONFIRMED-primary PGK] |
| **PI30 REVO** | `QALL<CHK><cr>` | 8-bit sum + 1 | Axpert Revo | `CHK` = sum of bytes + 1 (single byte). Example from PREVO: `QALL` → `51 41 4C 4C 2B 0D`. [CONFIRMED-primary PREVO] |
| **PI41** | same as PI30 | CRC-16 | LV5048 split phase | QPIGS2 = L2 phase plus PV2. QP2GSn. QPIGS status bits have *different meaning*. [CONFIRMED-primary P41] |
| **PI16 / PI15** | `QPIGS<cr>`, CRC optional | CRC-16 (optional in replies) | "SUNNY" grid-tie / hybrid (InfiniSolar older) | P16: "Optionally ... the device ... may put a 2-bytes non-reflected CRC (polynomial: 0x1021)". [CONFIRMED-primary P16] |
| **PI17** | `^P003PI<cr>` → `^D00517<CRC><cr>` | queries without CRC; replies with CRC | InfiniSolar 10 kW 3-phase, MPI | Comma-separated fields. [CONFIRMED-primary P17] |
| **PI18** | `^P005PI<CRC><cr>` → `^D00518<CRC><cr>` | CRC-16 over the whole `^P…` text | InfiniSolar V, LV5048 V2, EASun IGRID SV IV | `nnn` = length incl. CRC+CR. Data scaled ×0.1. [CONFIRMED-primary P18; SYS PDUs] |
| (legacy) **Q1/Q6 "UPS-style"** | `Q6\r` → `(225 50.1 229 …\r` (no CRC) | none | Voltronic RP/RS series (RS3024, RP6000) via **RS485** | Replies `#-1\r` to PI30 commands. [SECONDARY I166, RP] |

CRC verification for PI18: `^P005PI` → CRC 0x718B. This matches the bytes `…71 8B 0D` in SYS `tests/esp8266-test-protocols.yaml`. [CONFIRMED-primary]

---

## 3. Physical layer

### 3.1 Serial parameters
- **2400 baud, 1 start bit, 8 data bits, no parity, 1 stop bit.** This is stated identically in P2014, P2015, PMAX, PGK and P41 ("Baud rate 2400 / Start bit 1 / Data bit 8 / Parity bit N / Stop bit 1"). [CONFIRMED-primary]
- No flow control. ned-kelly opens the port raw with `B2400`, `~PARENB`, `~CSTOPB`, `CS8 | CLOCAL`. [CONFIRMED-primary NED inverter.cpp]

### 3.2 RJ45 pinouts seen in the wild (measure or read the manual first)

| Source | Device | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 | Pin 6 | Pin 7 | Pin 8 | Confidence |
|---|---|---|---|---|---|---|---|---|---|---|
| P2014 cable drawing ("RJ45 to RS232 cable", RJ45 P1 ↔ DB9 P2) | Voltronic HS/MS/MSX | → DB9 pin 2, labelled **TXD** (inverter transmits) | → DB9 pin 3, **RXD** | – | – | – | – | – | → DB9 pin 5, **GND** | [CONFIRMED-primary] (the table in the PDF image reads P1 1 "white and orange" → P2 2 TXD; 2 "blue" → 3 RXD; 8 "white and green" → 5 GND) |
| SYS README | PIP4048-compatible | TX (→ MAX3232 RIN1) | RX (← MAX3232 DOUT1) | | **VCC 12 V** | | | | GND | [SECONDARY] "The inverter provides +12V on pin 4 or 7 depending on the model." |
| TRIPP manual §5.2 | APSWX2K/3K24VMPPT (AC/INV-CHG-FAULT panel) | "RXD232" | "TXD232" | | | | | | "GND" | [CONFIRMED-primary] (labelled from the PC's side, so the same wiring as the Voltronic cable) [INFERRED] |
| PMR README | PowMr POW-HVM1.5H … 3.2H-24V, 10.2M | TX (data from inverter) | RX (data to inverter) | | **+12 V** | | | | GND | [SECONDARY] |
| S2M wiki report | ECGSOLAX VICTOR MAX 10.2 | | | | VCC ("Check voltage first") | | | | GND | [SECONDARY] |
| S2M wiki report | WERCHTAY GD3024EMH (24 V / 3 kW) | | **12 V VCC** | inverter TX | | GND | inverter RX | | | [SECONDARY] |
| SolarAssistant | PowMr HVM-KP, Easun ISolar-SMT | | | RX | GND (some units pin 5) | GND | TX | | | [SECONDARY] https://solar-assistant.io/help/inverters/powmr/POW-HVM-KP/rs232 |
| RP README (**RS485 port**) | Voltronic RP6000 | +5 V | GND | **B** | **A** | RXD (TTL) | TXD (TTL) | +5 V | GND | [SECONDARY] |
| I166 (**RS485 port**) | "RP series" RS3024 24 V / 3 kW | +5 V (measured) | GND | B (traced to SN75176B pin 7) | A (traced to SN75176B pin 6) | TTL RX (5 V) | TTL TX (5 V) | | | [SECONDARY] |

**Hazard summary:**
- RJ45 "RS232/COM/WiFi" ports often carry **+12 V (pin 4, sometimes 7 or 2)** or **+5 V (RS485 boards, pins 1/7)** to power WiFi dongles or remote panels.
- Never land an ESP GPIO or the RS485 module's A/B on an unidentified pin.
- Never plug an Ethernet switch or router into these ports.

[SECONDARY sources above; INFERRED advice]

TX/RX direction is disputed between documents: SolarAssistant's Axpert GT page labels pin 1 RX / pin 2 TX from the adapter side. If an RS232 link stays silent, swapping pins 1 and 2 is the standard first fix. [SECONDARY https://solar-assistant.io/help/inverters/growtech/Axpert-GT/rs232]

### 3.3 Level conversion
- RS232 ports need a **MAX3232-class transceiver**. The MAX3232 runs on 3–5.5 V and has 2 drivers and 2 receivers. [CONFIRMED-primary https://www.ti.com/product/MAX3232] This is what SYS, S2M and NED use.
- **A TTL↔RS485 module cannot talk to an RS232 port.** In SYS issue #111 a user with a MAX485 module on an RCT Axpert VM2 concluded: "I need a rs232 not rs485". [SECONDARY https://github.com/syssi/esphome-pipsolar/issues/111]
- If the inverter's port **is** RS485 (as in this user's case), the RS485 module is correct:
  - A goes to A, B goes to B.
  - GND is optional but recommended. syssi says "GND isn't important for RS485" [SECONDARY I166]; a common GND improves common-mode margin [INFERRED].
  - Power the module from 3.3 V so its TTL side matches the ESP32-S3. [INFERRED]
- Some RS485 boards also expose **5 V TTL UART** on pins 5/6 (RP6000, RS3024). These are *not* safe for the 3.3 V ESP32-S3 without a level shifter. [SECONDARY RP, I166]

### 3.4 USB variant (HID)
- Voltronic USB ports enumerate as **USB HID VID 0x0665 / PID 0x5161** ("Cypress … Voltronic Power UPSes"). Commands are sent as HID SET_REPORT control transfers (`bRequest 0x09`, `wValue 0x200`) in **8-byte chunks**. Replies are read from **interrupt endpoint 0x81 in 8-byte chunks** until CR.
- NUT uses a 1000 ms reply timeout (3000 ms for 0665:5161). It flushes stale reports first, because "Some USB devices with Cypress 0665:5161 can leave a reply from a previous command queued".

  [CONFIRMED-primary NUT nutdrv_qx.c lines ~753–925]
- mpp-solar `hidrawio.py`: sends commands longer than 8 bytes in 8-byte chunks padded with 0x00, sleeps 50 ms per chunk and 250 ms after sending, reads 256 bytes every 150 ms, and stops at the first CR (5 s overall timeout). [CONFIRMED-primary MPP]
- Behind the USB chip it is still the same 2400-baud UART, so USB is no faster. [SECONDARY powerforum via search snippet, https://powerforum.co.za/topic/32076-any-fix-for-flaky-rs232usb-comms-on-voltronic-inverters/]
- On ESP32-S3, USB-host HID is technically possible but needs an OTG adapter, which the user does not have. **Not recommended.** [INFERRED]

### 3.5 RS485 variant
- **PI30 over RS485 exists.** A SUOER VMS-V-12K-48-HG was reported working via RS485 with protocol PI30_MAX. The note reads: "Using an ESP32-S3-N16R8 WROOM CAM with an RS485-to-TTL module". [SECONDARY S2M wiki Reported-Working-Devices https://raw.githubusercontent.com/wiki/softwarecrash/Solar2MQTT/Reported-Working-Devices.md]
- **Voltronic RP/RS series RS485 = Q1/Q6 protocol at 2400 8N1.** These units return `#-1\r` to `QPI`, `QPIGS` and other PI30 commands. Captured replies:
  - `Q1\r` → `(000.0 000.0 229.6 000 50.0 12.7 09.0 10001001\r`
  - `Q6\r` → `(000 00.0 229 50.0 12.78 100 000.0 000 000.0 0 0000 0000 000 009.1 B 00 00 U1V0196 1 …\r`

  [SECONDARY I166, comment by syssi 2024-12-31]
- Q6 field map, from SYS config-examples and RP:

  | Index | Field |
  |---|---|
  | 0 | input V |
  | 1 | input Hz |
  | 2 | output V |
  | 3 | output Hz |
  | 4 | battery V |
  | 5 | SOC % |
  | 6 | battery charge A |
  | 7–9 | unknown |
  | 10 | apparent power ×10 VA |
  | 11 | active power ×10 W |
  | 12 | load % |
  | 13 | temperature °C |
  | 14 | mode (`B`/`L`) |
  | 17 | firmware string |

  RP6000 sends `"Q6\r\n"` with a 1500 ms response timeout, 200 ms quiet gap and 500 ms partial-frame timeout. Its comment: "At 2400 baud a Q6 frame takes ~380 ms on the wire". [SECONDARY RP rp6000.h; config-examples yaml]
- Modbus RTU families seen on similar Chinese all-in-ones (Solar2MQTT device classes), all [CONFIRMED-primary in S2M `src/modbus/device/*/…h`]:

  | Family | Baud | Slave address |
  |---|---|---|
  | SMG / SMG-II | 9600 | 1 |
  | Anenji | 9600 | 1 |
  | Deye | 9600 | 1 |
  | MUST PV/PH18 | 19200 | 4 |
  | PowMr | 2400 | 5 |

- **PowMr exposes both Modbus RTU and PI30 on the same 2400-baud UART.** S2M source comment: "PowMr/Victor exposes both Modbus RTU and a Voltronic-like PI30 command interface on the same 2400-baud UART". [CONFIRMED-primary S2M PI_Serial.cpp] PMR documents the PowMr Modbus side: "slave id is 5", the original dongle polls `05 03 11 95 00 2D 91 43` and `05 03 11 C2 00 10 E1 42`. [SECONDARY PMR]

### 3.6 Port sharing
- On Axperts, "the RJ-45 and USB ports … both use the same serial port on the DSP". Only one master at a time; a WiFi dongle plus an ESP32 on the same bus will collide. [SECONDARY powerforum search snippet https://powerforum.co.za/topic/14589-what-kind-of-data-is-available-on-the-different-ports-of-mppvoltronicclones/]
- A *passive* sniffer (RX-only on both lines) is safe. SYS ships `tests/esp8266-uart-sniffer.yaml` with two RX-only UARTs at 2400. [CONFIRMED-primary SYS]

### 3.7 Grounding, isolation, noise
- Some users report Axpert RS232 boards burning when a mains-earthed PC was plugged in. [SECONDARY https://powerforum.co.za/topic/8635-axpert-inverter-burning-rs232-card-when-ever-i-plug-serial-cable-to-computer/ (title/snippet only; site is Cloudflare-blocked)]
- Recommendation [INFERRED]: power the ESP32 from the isolated USB wall charger. Do not keep a mains-powered laptop attached over USB while the ESP is wired to the inverter.
- "During operation inverter produces a lot of RF noise … attach small capacitor to RX pin." [SECONDARY PMR README]
- A captured Easun trace contains a corrupted frame (cycle 1 QPIGS ends in a single `\xcf` byte where the CRC should be 0x4346). **Always CRC-check.** [CONFIRMED-primary: SYS `docs/pdus/pip2424mse1/easun-5500w-1ph.txt`, re-computed by me]

---

## 4. Framing and CRC

### 4.1 Frame format
- **Request:** `<ASCII command><CRC-hi><CRC-lo><0x0D>`. Example `QPIGS` = `51 50 49 47 53 B7 A9 0D`. [CONFIRMED-primary P2014 "Computer: QPIGS <CRC><cr>"; byte values verified]
- **Reply:** `( <fields separated by single spaces> <CRC-hi><CRC-lo> <0x0D>`. The CRC covers **everything from `(` up to the last data byte**.
  - Verified on a real EASun SP-3200: QPIWS reply `(00000100000000000000000000000000` → CRC 0xFE82, matching the device log `[C: QPIWS][CR: FE82]`.
  - QPIRI reply `(220.0 14.5 … 0 1` → 0xAA94, matching `[CR: AA94]`.

  [CONFIRMED-primary S2M `Protocol/raw data from devices/EASun sp-3200.txt`; my recomputation]
- **Negative reply:** `(NAK` + CRC `73 73` ("ss") + CR. **Positive set reply:** `(ACK` + `39 20` + CR. [CONFIRMED-primary MPP test_responses; recomputed: CRC("(NAK")=0x7373, CRC("(ACK")=0x3920]
- Some devices send `(NAK\r` without CRC, or echo the command. S2M explicitly handles a "NAK without crc" case and an "echo cmd … ignored" case. [CONFIRMED-primary S2M PI_Serial.cpp]
- Docs sometimes omit the CRC where devices still send it. PMAX shows `QT` replying `(YYYYMMDDHHMMSS<cr>` without CRC, but a real reply `(20210726122606JF\r` carries CRC `JF` = 0x4A46. [CONFIRMED-primary MPP pi30max test, recomputed]

### 4.2 CRC algorithm
- **CRC-16/XMODEM:** poly 0x1021, init 0x0000, no input/output reflection, no final XOR. Sent high byte first.
- **Voltronic byte-adjust:** after computing, if the low byte or the high byte equals **0x28 `(`, 0x0D CR or 0x0A LF**, increment it by 1, so the CRC can never contain a frame delimiter.
  - The nibble-table reference implementation (`crc_ta[16] = {0x0000,0x1021,…,0xf1ef}`) with exactly this adjust is in NED `cal_crc_half()` and in S2M `calculatePiCrc()`.
  - ESPHome uses `crc16be(msg,len)` plus the same adjust.
  - The official PDFs reference an embedded "CRC.c" that is not extractable (OLE icon only).

  [CONFIRMED-primary NED, S2M, SYS pip8048 `pipsolar_crc_()`, ESPC]
- **Discrepancy:** mpp-solar's `crcPI()` *also* bumps 0x00 bytes (`if crc_low == 0x28 or … or crc_low == 0x00`). ESPHome, ned-kelly and Solar2MQTT do not. [CONFIRMED-primary MPP protocol_helpers.py]
  - This affects commands whose raw CRC contains 0x00, for example `QED20260119` (raw 0x000F) and `QED20260407` (raw 0x3900).
  - Recommendation [INFERRED]: **send with the 3-byte rule** (majority, matches the reference C). **Accept either variant on receive.** If a date query gets NAK, retry once with the 0x00 rule.
- **Real-device proof of the adjust:** an Easun 5.5 kW QPIRI reply has raw CRC 0x0D3E and was transmitted as `0E 3E` (`\x0e>`). [CONFIRMED-primary SYS easun PDU, recomputed]

### 4.3 Verified CRC table (computed by me, cross-checked where noted)

| Command | Raw CRC | Sent bytes (CRC + CR) | Cross-check |
|---|---|---|---|
| `QPI` | BEAC | `BE AC 0D` | SYS test-protocols.yaml `[0x51,0x50,0x49,0xBE,0xAC,0x0D]` |
| `QID` | D6EA | `D6 EA 0D` | – |
| `QSID` | BB05 | `BB 05 0D` | – |
| `QVFW` | 6299 | `62 99 0D` | – |
| `QVFW2` | C3F5 | `C3 F5 0D` | – |
| `QVFW3` | D3D4 | `D3 D4 0D` | – |
| `QPIRI` | F854 | `F8 54 0D` | SYS yaml |
| `QFLAG` | 9874 | `98 74 0D` | SYS yaml |
| **`QPIGS`** | **B7A9** | **`B7 A9 0D`** | SYS yaml and captured traffic in SYS PDUs (`51:50:49:47:53:B7:A9:0D`) |
| `QPIGS2` | 682D | `68 2D 0D` | SYS yaml / PDU |
| `QMOD` | 49C1 | `49 C1 0D` | SYS yaml / PDU |
| `QPIWS` | B4DA | `B4 DA 0D` | SYS yaml |
| `QDI` | 711B | `71 1B 0D` | SYS yaml |
| `QMN` | BB64 | `BB 64 0D` | SYS yaml |
| `QGMN` | 4928 | **`49 29 0D`** (0x28 bumped) | – |
| `QBOOT` | 0A88 | **`0B 88 0D`** (0x0A bumped) | – |
| `QT` | 27FF | `27 FF 0D` | SYS yaml |
| `QET` | 81B6 | `81 B6 0D` | – |
| `QLT` | 3B2E | `3B 2E 0D` | – |
| `Q1` | 1BFC | `1B FC 0D` | – |
| `QBEQI` | 2EA9 | `2E A9 0D` | – |
| `QMCHGCR` | D855 | `D8 55 0D` | – |
| `QMUCHGCR` | 2634 | `26 34 0D` | – |
| `QOPM` | A5C5 | `A5 C5 0D` | – |
| `QPGS0` / `QPGS1` / `QPGS2` | 3FDA / 2FFB / 1F98 | | SYS yaml |
| `QED20261021` | 95D0 | `95 D0 0D` | – |
| `QEM202610` | 70DD | `70 DD 0D` | – |
| `QEY2026` | F7EF | `F7 EF 0D` | – |
| `QLD20261021` | B0DE | `B0 DE 0D` | – |
| `QLY2026` | BF0D | **`BF 0E 0D`** (0x0D bumped) | – |
| `^P005PI` (PI18) | 718B | `71 8B 0D` | SYS yaml |

**Date-argument energy queries:** the CRC is computed over the *whole* string including the digits, e.g. `QED20261021`. [CONFIRMED-primary PMAX "Computer: QEDyyyymmdd<CRC><cr>"; MPP builds full_command = command bytes + CRC + CR]

### 4.4 C/C++ implementation (host-compiled and unit-tested)
Tested with `c++ -std=c++11 -Wall -Wextra` on macOS. All vectors above pass, plus CRC-valid replies from MPP tests and SYS PDUs. Files: `scratchpad/research/r2-code/pi30.h`, `sketch_core.h`, `test.cpp`, `mock_test.cpp`.

```cpp
// pi30.h (excerpt) — Voltronic PI30 framing helpers, no Arduino deps
static inline uint16_t pi_crc16_xmodem(const uint8_t *d, size_t n) {
  uint16_t crc = 0;
  while (n--) {
    crc ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}
static inline uint8_t pi_adj(uint8_t b) {            // Voltronic quirk
  return (b == 0x28 || b == 0x0D || b == 0x0A) ? (uint8_t)(b + 1) : b;
}
static inline uint16_t pi_crc(const uint8_t *d, size_t n) {
  uint16_t c = pi_crc16_xmodem(d, n);
  return (uint16_t)(pi_adj(c >> 8) << 8) | pi_adj(c & 0xFF);
}
static inline uint16_t pi_crc_z(const uint8_t *d, size_t n) { // mpp-solar variant (also bumps 0x00) — RX only
  uint16_t c = pi_crc(d, n); uint8_t hi = c >> 8, lo = c & 0xFF;
  if (!hi) hi = 1; if (!lo) lo = 1; return (uint16_t)(hi << 8) | lo;
}
static inline size_t pi_build_frame(const char *cmd, uint8_t *out) {
  size_t n = strlen(cmd); memcpy(out, cmd, n);
  uint16_t c = pi_crc((const uint8_t *)cmd, n);
  out[n] = c >> 8; out[n + 1] = c & 0xFF; out[n + 2] = 0x0D; return n + 3;
}
enum PiResult { PI_OK, PI_NAK, PI_BAD_START, PI_SHORT, PI_BAD_CRC, PI_NO_CRC_OK, PI_REFUSED };
// buf includes trailing '\r'. Rejects echoes (must start with '('), detects (NAK, checks CRC.
static inline PiResult pi_check(uint8_t *buf, size_t len, const char **payload, bool allow_no_crc) {
  if (len < 2 || buf[len - 1] != 0x0D) return PI_SHORT;
  if (buf[0] != '(') return PI_BAD_START;                       // echo "QPIGS..\r" lands here
  if (len >= 4 && memcmp(buf, "(NAK", 4) == 0) return PI_NAK;
  if (len >= 4) {
    uint16_t rx = (uint16_t)(buf[len - 3] << 8) | buf[len - 2];
    if (rx == pi_crc(buf, len - 3) || rx == pi_crc_z(buf, len - 3)) {
      buf[len - 3] = 0; *payload = (const char *)buf + 1; return PI_OK;
    }
  }
  if (allow_no_crc) { /* printable check, then accept "(...\r" */ }
  return PI_BAD_CRC;
}
```
Test output (excerpt): `QPIGS -> B7 A9 0D OK`, `QGMN -> 49 29 0D OK`, `QBOOT -> 0B 88 0D OK`, `Easun QPIRI (0x0D->0x0E) result=0 OK`, `QPIGS parse ok=1 n=21 batt=57.50V pv=103.8V pvW=856 load_on=1 scc_chg=1 ac_chg=0`, `ALL PASS`.

### 4.5 Echo trap (important with RS485 modules)
syssi, on a looped-back line: "an echo of an request frame has a valid checksum and the decoder isn't able to identify the incorrect reply". [SECONDARY I166] Always require the first byte to be `(` for PI30 and Q1/Q6. For Modbus, require the slave address and function code. Discard any bytes received during your own transmission. [INFERRED] Half-duplex RS485 transceivers can echo TX onto RX depending on the module. [INFERRED]

---

## 5. Inquiry command reference (read-only)

Index convention: fields are 0-based after stripping `(` and the CRC, then splitting on spaces.

### 5.1 Identification

| Command | Reply format | Meaning | Source |
|---|---|---|---|
| `QPI` | `(PI30` | Protocol ID. "30 for HS series" (P2014); "30 for Axpert KS series" (PMAX). Real CRC-valid reply `(PI30\x9A\x0B\r`. | [CONFIRMED-primary P2014, PMAX, MPP test] |
| `QID` | `(XXXXXXXXXXXXXX` (14 digits) | Serial number. Example `(9293333010501`. | [CONFIRMED-primary P2014; MPP] |
| `QSID` | `(NNXXXXXXXXXXXXXXXXXXXX` | NN = valid length, then 20 digits, zero-filled. | [CONFIRMED-primary PMAX, P41, PGK] |
| `QVFW` | `(VERFW:NNNNN.NN` | Main CPU firmware. "00123: firmware series number; 01: version". Example `(VERFW:00072.70`. | [CONFIRMED-primary P2014; MPP] |
| `QVFW2` | `(VERFW2:NNNNN.NN` | "Another CPU" (P2014) / "SCC1 CPU" (P2015). `00000.00` = SCC did not report. | [CONFIRMED-primary] |
| `QVFW3`, `QVFW4` | `(VERFW3:…` | SCC2/SCC3 (P2015) or remote panel CPU (PMAX/PGK). | [CONFIRMED-primary] |
| `VERFW:` | `(VERFW: NNNNN.NN` | Bluetooth / BLE CPU version (PMAX, PGK). | [CONFIRMED-primary] |
| `QMN` | `(MMMMM-NNNN` | Model name + rated VA. Example `(MKS2-8000`; Phocos `MKS2-5000`. Many clones report `VMII-NXPW5KW`. EASun SP-3200 → NAK. | [CONFIRMED-primary PMAX; SECONDARY S2M wiki/dumps] |
| `QGMN` | `(NNN` | General model number. Example `(044`. | [CONFIRMED-primary PMAX; MPP] |

### 5.2 QPIGS — general status (the main logging command)
Template (P2014): `(BBB.B CC.C DDD.D EE.E FFFF GGGG HHH III JJ.JJ KKK OOO TTTT EEEE UUU.U WW.WW PPPPP b7b6b5b4b3b2b1b0`. P2015 / PGK / P41 add `QQ VV MMMMM b10b9b8`. PMAX adds `Y ZZ AAAA`. [CONFIRMED-primary]

| Idx | Format | Field | Unit | EASun SP-3200 24 V 3.2 kW (battery mode, night) | 48 V Axpert sample (MPP test) | Notes |
|---|---|---|---|---|---|---|
| 0 | BBB.B | Grid (AC input) voltage | V | `000.0` | `000.0` | |
| 1 | CC.C | Grid frequency | Hz | `00.0` | `00.0` | |
| 2 | DDD.D | AC output voltage | V | `220.1` | `230.0` | |
| 3 | EE.E | AC output frequency | Hz | `49.9` | `49.9` | |
| 4 | FFFF | AC output apparent power | VA | `0198` | `0161` | |
| 5 | GGGG | AC output active power | W | `0155` | `0119` | |
| 6 | HHH | Output load percent | % | `006` | `003` | "Maximum of W% or VA%" |
| 7 | III | DC BUS voltage | V | `395` | `460` | |
| 8 | JJ.JJ | Battery voltage | V | `26.30` | `57.50` | |
| 9 | KKK | Battery charging current | A (integer) | `000` | `012` | 1 A resolution ≈ 26 W on 24 V [INFERRED] |
| 10 | OOO | Battery capacity | % | `060` | `100` | Voltage-derived unless a BMS protocol is active [INFERRED] |
| 11 | TTTT | Inverter heat-sink temperature | °C | `0028` | `0069` | P2014/P2015: "(NTC A/D value for HS/MS/MSX 1~3K)". May be a raw ADC value on 1–3 kVA units. |
| 12 | EEEE or EE.E | PV input current (for battery) | A | `0000` | `0014` | P2014/P41: `EEEE` integer. PMAX/PGK: `EE.E`. Parse as float. |
| 13 | UUU.U | PV input voltage | V | `000.0` | `103.8` | |
| 14 | WW.WW | Battery voltage from SCC | V | `00.00` | `57.45` | Often 0 on single-board units |
| 15 | PPPPP | Battery discharge current | A | `00004` | `00000` | |
| 16 | b7…b0 | Device status (8 chars, **first char = b7**) | flags | `00010000` | `00110110` | See below |
| 17 | QQ | Battery voltage offset for fans on | 10 mV | `00` | `00` | Absent in P2014 (17-field firmware). PI41: "Reserved". |
| 18 | VV | EEPROM version | – | `00` | `00` | PI41: "Reserved" |
| 19 | MMMMM | **PV charging power** | W | `00001` | `00856` | P2015 "PV Charging power 1"; PMAX "PV1 Charging power". Charger output, *not* strictly PV input. mpp-solar names it "PV Input Power". |
| 20 | b10b9b8 | Device status 2 | flags | `010` | `010` | b10 charging to float; b9 switch on; b8 reserved / "dustproof installed" (PMAX, PGK) |
| 21 | Y | Solar feed-to-grid status | 0/1 | – | – | PMAX only ("reserved feature") |
| 22 | ZZ | Country regulation | code | – | – | PMAX: 00 India, 01 Germany, 02 South America |
| 23 | AAAA | Solar feed-to-grid power | W | – | – | PMAX only. Phocos sample ends `… 010 0 01 0000`. |

Sources: P2014 §2.7, P2015 §2.9, PMAX §2.9, PGK §2.10, P41 §2.7 [CONFIRMED-primary]. EASun SP-3200 dump from S2M [SECONDARY, but its QPIRI CRC matches the device log]. 48 V sample: MPP `pi30.py` test response with valid CRC 0x248C [CONFIRMED-primary].

**Device-status bits (field 16), PI30 HS/MS/MSX/KS/MAX/GK:**

| Char pos | Bit | Meaning | 1 = |
|---|---|---|---|
| 0 | b7 | "add SBU priority version" | yes. On PGK: "PV or AC feed the load". |
| 1 | b6 | configuration status | changed. Use this as a trigger to re-read QPIRI. |
| 2 | b5 | SCC firmware version | updated |
| 3 | b4 | load status | load on |
| 4 | b3 | battery voltage to steady while charging | (PGK: reserved) |
| 5 | b2 | charging status (any) | charging on |
| 6 | b1 | SCC (solar) charging | on |
| 7 | b0 | AC (utility) charging | on |

`b2b1b0`: `000` = do nothing, `110` = charging with SCC, `101` = charging with AC, `111` = SCC + AC. [CONFIRMED-primary P2014]

ESPHome maps char 0 → `add_sbu_priority_version` … char 7 → `ac_charging_status`. [CONFIRMED-primary SYS pip8048.cpp `handle_qpigs_`]

**PI41 is different:** b7 = SCC1 OK; b6 = L1 AC charging; b5 = L1 SCC charging; b4b3 = 2 battery open / 1 under / 0 normal; b2 = L1 line loss; b1 = load on; b0 = configuration status. [CONFIRMED-primary P41]

**Field-count variants:** 17 (P2014), 21 (P2015, PGK, P41, most clones including EASun SP-3200 and PowMr), 24 (PMAX, Phocos). S2M's parser has explicit 24/17/21-field tables. [CONFIRMED-primary S2M QPIGS.h] A real Axpert MAX II 8 kW still returns 21 fields (see Appendix B), so the field count alone does not identify the variant. [CONFIRMED-primary SYS PDU]

### 5.3 QPIGS2 — second status block (only on some models)
- **PMAX (48 V MAX only):** `(BB.B CCC.C DDDDD` → PV2 input current A, PV2 input voltage V, PV2 charging power W. Real reply: `(03.1 327.3 01026 `, with a trailing space before the CRC. [CONFIRMED-primary PMAX; MPP test CRC-valid]
- **P2015 (Plus Duo, 3 trackers):** 13 items:
  - PV2 current, PV2 voltage, battery V from SCC2, PV2 charging power
  - status bits (b24 SCC2 charging, b25 SCC3 charging)
  - AC charging current, AC charging power
  - PV3 current, PV3 voltage, battery V from SCC3, PV3 charging power
  - PV total charging power

  [CONFIRMED-primary P2015 §2.10]
- **P41 (split phase):** L2 input V/Hz, L2 output V/Hz/VA/W/%, PV2 current, PV2 voltage, battery V (L2), status bits. [CONFIRMED-primary P41 §2.8]
- Basic units (EASun SP-3200, Phocos PSW-H 5 kW) answer **NAK**. [SECONDARY S2M dumps]

### 5.4 QPIRI — rating and current settings

| Idx | Format | Field | Codes / unit | EASun SP-3200 value |
|---|---|---|---|---|
| 0 | BBB.B | Grid rating voltage | V | 220.0 |
| 1 | CC.C | Grid rating current | A | 14.5 |
| 2 | DDD.D | AC output rating voltage | V | 220.0 |
| 3 | EE.E | AC output rating frequency | Hz | 50.0 |
| 4 | FF.F | AC output rating current | A | **13.9** |
| 5 | HHHH | AC output rating apparent power | VA | **3200** |
| 6 | IIII | AC output rating active power | W | 3000 |
| 7 | JJ.J | Battery rating voltage | V | **24.0** |
| 8 | KK.K | Battery re-charge voltage | V | 22.5 |
| 9 | JJ.J | Battery under voltage (cut-off) | V | 23.2 |
| 10 | KK.K | Battery bulk (C.V.) voltage | V | 27.6 |
| 11 | LL.L | Battery float voltage | V | 27.0 |
| 12 | O | Battery type | see below | 2 (User) |
| 13 | PP (PPP if >99 A on MAX) | Max AC (utility) charging current | A | 40 |
| 14 | QQ0 | Max (total) charging current | A | 060 |
| 15 | O | Input voltage range | 0 Appliance, 1 UPS | 0 |
| 16 | P | Output source priority | see below | 0 |
| 17 | Q | Charger source priority | see below | 2 |
| 18 | R | Parallel max number | 0–9 (seen `-`) | 6 |
| 19 | SS | Machine type | 00 grid-tie, 01 off-grid, 10 hybrid, 11 off-grid 2 trackers, 20 off-grid 3 trackers | 01 |
| 20 | T | Topology | 0 transformerless, 1 transformer | 0 |
| 21 | U | Output mode | 0 single, 1 parallel, 2–4 phase 1/2/3 of 3, 5 phase 1 of 2, 6 phase 2 of 2 (120°), 7 phase 2 of 2 (180°) | 0 |
| 22 | VV.V | Battery re-discharge voltage | V. "00.0V means battery is full (charging in float mode)" | 27.5 |
| 23 | W | PV OK condition for parallel | 0/1 | 0 |
| 24 | X | PV power balance | 0 = PV max current is max charged current; 1 = PV max power = charge + load | 1 |
| 25 | YYY | Max charging time at C.V. stage | min (PMAX/PGK/MPP; 48 V only on MAX) | – |
| 26 | Z | Operation logic | 0 auto, 1 on-line, 2 ECO (PMAX/PGK) | – |
| 27 | CCC | Max discharging current | A (PMAX 48 V) | – |

Sources: P2014 §2.5, P2015 §2.7, PMAX §2.7, PGK §2.8 [CONFIRMED-primary]. EASun SP-3200 values come from a dump whose CRC 0xAA94 matches the device log [CONFIRMED-primary capture, via S2M].

**Code lists:**
- **Battery type:** 0 AGM, 1 Flooded, 2 User (P2014). PMAX adds 3 Pylon, 5 Weco, 6 Soltaro, 8 Lib, 9 Lic. The PMAX `PBT` setter lists "04 for Shinheung … 07 for BAK". mpp-solar maps 3 Pylontech, 4 Shinheung, 5 WECO, 6 Soltaro, 7 TBD, 8 "LIb-protocol compatible", 9 "3rd party Lithium". [CONFIRMED-primary]
- **Output source priority:** 0 Utility first, 1 Solar first, 2 SBU (P2014). PMAX names them 0 UtilitySolarBat, 1 SolarUtilityBat, 2 SolarBatUtility. [CONFIRMED-primary]
- **Charger source priority:**
  - HS/KS: 0 Utility first, 1 Solar first, 2 Solar+Utility, 3 Only solar.
  - MS/MKS (`PCP` setter): 0, 1, 3 only.
  - PMAX: 1 Solar first, 2 Solar+Utility, 3 Only solar (no 0).

  [CONFIRMED-primary]
- **Real-world format quirks** (all from CRC-valid frames in MPP tests) [CONFIRMED-primary]:
  - 24 V Axpert `(230.0 13.0 230.0 50.0 13.0 3000 2400 24.0 23.0 21.0 28.2 27.0 0 30 50 0 2 1 - 01 1 0 26.0 0 0`: max charge current is two digits (`50`) and parallel-max is `-`.
  - 8 kW MAX `… 2 010 030 1 2 2 9 01 0 0 50.0 0 1 480 0 070`: 28 fields.
  - S2M classifies 21 / 25 / 26 / 28-field layouts. [CONFIRMED-primary S2M PROTOCOL_REFERENCE.md]

### 5.5 QMOD — operating mode (`(X`)

| Letter | Mode | Where documented |
|---|---|---|
| P | Power on | P2014, PMAX, PGK |
| S | Standby | all |
| L | Line (grid) | all ("Means: the current DEVICE mode is Grid mode") |
| B | Battery | all |
| F | Fault | all |
| H | Power saving | P2014, P2015, PGK |
| D | Shutdown | PMAX, PGK |
| C | Charge | PGK |
| Y | Bypass | PGK ("For PIP-MK"); also in MPP pi30 |
| E | ECO | PGK ("For PIP-MK") |

[CONFIRMED-primary] Real replies: `(B\xE7\xC9\r`, `(S\xE5\xD9\r`. mpp-solar's pi30 test frame `(S\x64\x39` has a wrong CRC (computed E5D9), so do not copy test frames blindly. [CONFIRMED-primary, recomputed]

### 5.6 QPIWS — warnings and faults (bit string, char k = a_k)
P2014 uses `a0…a31` (32 chars). P2015 numbers them `b1…b38` (b1 = a0; 38 chars). PMAX and PGK use `a0…a35` (36 chars). "If the warning is happened, the relevant bit will set 1". Rows marked "if a1=1 fault, else warning" mean "Compile with a1, if a1=1, fault, otherwise warning". [CONFIRMED-primary]

| Idx | P2014 HS/MS/MSX | P2015 (Plus Duo) | PMAX | PGK/MK |
|---|---|---|---|---|
| 0 | Reserved | Reserved | **PV loss** (W) | PV loss (W) |
| 1 | Inverter fault (F) | same | same | same |
| 2 | Bus over (F) | same | same | same |
| 3 | Bus under (F) | same | same | same |
| 4 | Bus soft fail (F) | same | same | same |
| 5 | LINE_FAIL (W) | same | same | same |
| 6 | OPV short (W) | (W) | (F) | (F) |
| 7 | Inverter voltage too low (F) | same | same | same |
| 8 | Inverter voltage too high (F) | same | same | same |
| 9 | Over temperature (F if a1=1, else W) | same | same | same |
| 10 | Fan locked (F if a1=1, else W) | same | same | same |
| 11 | Battery voltage high (F if a1=1, else W) | same | same | same |
| 12 | Battery low alarm (W) | same | same | same |
| 13 | Reserved | Reserved (overcharge) | Reserved | Reserved |
| 14 | Battery under shutdown (W) | same | same | same |
| 15 | Reserved | Reserved (battery derating) (W) | Battery derating (W) | Battery derating (W) |
| 16 | Over load (F if a1=1, else W) | same | same | same |
| 17 | EEPROM fault (W) | same | same | same |
| 18 | Inverter over current (F) | same | same | same |
| 19 | Inverter soft fail (F) | same | same | same |
| 20 | Self test fail (F) | same | same | same |
| 21 | OP DC voltage over (F) | same | same | same |
| 22 | Battery open (F) | same | Bat open | Battery open (F) |
| 23 | Current sensor fail (F) | same | same | same |
| 24 | Battery short (F) | same | Reserved | Battery short (F) |
| 25 | Power limit (W) | same | Reserved | Power limit (W) |
| 26 | PV voltage high (W) | PV voltage high 1 | Reserved | PV voltage high (W/F) |
| 27 | MPPT overload fault (W) | … 1 | Reserved | MPPT overload fault (F) |
| 28 | MPPT overload warning (W) | … 1 | Reserved | MPPT overload warning (W) |
| 29 | Battery too low to charge (W) | … 1 | Reserved | Battery too low to charge (W) |
| 30 | Reserved | PV voltage high 2 | Reserved | DC/DC over current (F) |
| 31 | Reserved | MPPT overload fault 2 | Battery weak (48 V) / **fault-code digit (24 V)** | **fault-code digit** |
| 32 | – | MPPT overload warning 2 | Reserved / **fault-code digit** | **fault-code digit** |
| 33 | – | Battery too low to charge 2 | Reserved / fault-code digit (48 V) | Low PV energy (W) |
| 34 | – | PV voltage high 3 | Reserved | High AC input during BUS soft start (W) |
| 35 | – | MPPT overload fault 3 | Battery equalization (W) | Battery equalization (W) |
| 36–37 | – | MPPT overload warning 3; Battery too low to charge 3 | – | – |

**Fault-code location varies:** PMAX says "24V model: a31, a32 is fault code; 48V model: a32, a33 is fault code". PGK puts it at a31–a32. ESPHome pip8048 reads positions 32–33. [CONFIRMED-primary PMAX, PGK, SYS]

Real example: EASun SP-3200 in battery mode replies `(00000100000000000000000000000000`, i.e. only a5 LINE_FAIL is set. [CONFIRMED-primary capture via S2M]

**Fault codes** (from QPGSn tables and fault-code digits; 2-digit, also shown on the LCD [INFERRED]):

| Code | P2014 / P2015 | PMAX | PGK |
|---|---|---|---|
| 01 | Fan locked | Fan locked when inverter off | Fan locked |
| 02 | Over temperature | same | same |
| 03 | Battery voltage too high | same | same |
| 04 | Battery voltage too low | same | same |
| 05 | Output short circuited or over temperature | Output short circuited | same as P2014 |
| 06 | Output voltage too high | same | same |
| 07 | Overload time out | same | same |
| 08 | Bus voltage too high | same | same |
| 09 | Bus soft start failed | same | same |
| 10 | – | PV over current | PV charger current over |
| 11 | Main relay failed | PV over voltage | – |
| 12 | – | DCDC over current | – |
| 13 | – | Battery discharge over current | – |
| 51 | Over current inverter | Over current | same |
| 52 | Bus soft start failed | Bus voltage too low | – |
| 53 | Inverter soft start failed | same | same |
| 54 | Self-test failed | – | – |
| 55 | Over DC voltage on output | same | same |
| 56 | Battery connection open | – | – |
| 57 | Current sensor failed | same | same |
| 58 | Output voltage too low | same | same |
| 59 | – | – | PV voltage high |
| 60 | Inverter negative power | Power feedback protection | same as P2014 |
| 71–86 | parallel faults (version different, output circuit failed, CAN, host lost, sync lost, battery/line/current/setting mismatch) | equivalent wording | same |

[CONFIRMED-primary P2014 §2.15 table, PMAX §2.11, PGK §2.10]

### 5.7 QFLAG — enable/disable flags
Reply format: `(E<enabled letters>D<disabled letters>`.

| Letter | Setting |
|---|---|
| a | buzzer |
| b | overload bypass |
| d | solar feed to grid (PMAX, reserved) |
| j | power saving |
| k | LCD returns to default page after 1 min |
| l | data-log pop-up (P2015) |
| u | overload restart |
| v | over-temperature restart |
| x | backlight |
| y | alarm on primary-source interrupt |
| z | fault-code record |

[CONFIRMED-primary P2014, P2015, PMAX]

Examples:
- `(EakxyDbjuvz` (MPP, CRC OK)
- EASun SP-3200 `(EazDbjkuvxy`
- PowMr `(EauxyzDbjkv;`: contains a stray `;` ("unrecognized flag character … silently ignored")

[SECONDARY SYS `tests/components/pip8048/frames_powmr_4200w.h`]

### 5.8 QDI — factory default settings

| Idx | Field |
|---|---|
| 0 | AC output V |
| 1 | AC output Hz |
| 2 | max AC charging A (`00DD`) |
| 3 | battery under V |
| 4 | float V |
| 5 | bulk V |
| 6 | re-charge V |
| 7 | max charging A |
| 8 | input range |
| 9 | output priority |
| 10 | charger priority |
| 11 | battery type |
| 12 | buzzer |
| 13 | power saving |
| 14 | overload restart |
| 15 | over-temp restart |
| 16 | backlight |
| 17 | alarm on source interrupt |
| 18 | fault record |
| 19 | overload bypass |
| 20 | LCD escape |
| 21 | output mode |
| 22 | re-discharge V |
| 23 | PV OK condition |
| 24 | PV power balance |
| 25+ (PMAX) | max CV time, max discharging current |
| 25+ (P2015) | charging stage, data-log pop-up, max solar charging A, CV time |

Example: `(230.0 50.0 0030 42.0 54.0 56.4 46.0 60 0 0 2 0 0 0 0 0 1 1 0 0 1 0 54.0 0 1 000`. [CONFIRMED-primary P2014 §2.10, P2015 §2.15, PMAX §2.14; MPP test CRC OK] Useful only once, at commissioning.

### 5.9 Q1 — undocumented extended status (works on many PI30 units)
Reply has 17 fields (27 on firmware 74.40):

| Idx | Field |
|---|---|
| 0 | time until end of absorb (s) |
| 1 | time until end of float (s) |
| 2 | SCC OK flag |
| 3 | AllowSccOn flag |
| 4 | ChargeAverageCurrent |
| 5 | SCC PWM temperature °C |
| 6 | inverter temperature °C |
| 7 | battery temperature °C |
| 8 | transformer temperature °C |
| 9 | parallel mode |
| 10 | fan lock |
| 11 | unused |
| 12 | fan speed % |
| 13 | SCC charge power W |
| 14 | parallel warning |
| 15 | sync frequency |
| 16 | charge stage: 10 none, 11 bulk, 12 absorb, 13 float |

[SECONDARY Q1DOC (AEVA forum), implemented in MPP pi30.py]

Q1DOC notes that SCC charge power scaling changed between firmware 52.30 and 72.40 ("divided by 10"). EASun SP-3200 answers Q1: `(07288 00001 00 00 00 000 028 028 000 00 00 000 0030 0001 0000 00.00 10`. [SECONDARY S2M dump]

### 5.10 Energy counters (PMAX, PGK, some clones)

| Command | Reply | Unit | Notes |
|---|---|---|---|
| `QET` | `(NNNNNNNN` | Wh | Total PV generated energy |
| `QEYyyyy` | `(NNNNNNNN` | Wh | PV energy for the year |
| `QEMyyyymm` | `(NNNNNNNN` | Wh | PV energy for the month |
| `QEDyyyymmdd` | `(NNNNNNNN` | Wh | PV energy for the day |
| `QLT` / `QLYyyyy` / `QLMyyyymm` / `QLDyyyymmdd` | `(NNNNNNNN` | Wh | Output load energy |
| `QT` | `(YYYYMMDDHHMMSS` | – | Inverter clock (needed to know which "day" the inverter uses) |

[CONFIRMED-primary PMAX §2.19, 2.23–2.30]

- Phocos PSW-H answers all of them, e.g. QET `10636700`, QED `00003899`. Its `QLT` returned `20241022`, which looks like a firmware bug or dump error. [SECONDARY S2M dump]
- **EASun SP-3200 (24 V / 3.2 kW) answers NAK to `QET`** and to QPIGS2, QALL and QMN. [SECONDARY S2M dump]
- PI18 equivalents are `^P005ET` / `^P009EYyyyy` / `^P011EMyyyymm` / `^P013EDyyyymmdd`. These report **kWh** for ET/EY/EM and **Wh** for ED. [CONFIRMED-primary P18]

### 5.11 Other inquiries

| Command | Reply | Notes |
|---|---|---|
| `QMCHGCR` | `(010 020 030 …` | Allowed max charge currents. PowMr includes `002`. |
| `QMUCHGCR` | `(002 010 020 …` | Allowed max utility charge currents. PowMr uses 3-digit `MUCHGC%03` while modern Voltronic uses 4 digits. [SECONDARY SYS frames] |
| `QMSCHGCR` | – | Allowed max solar charge currents (P2015) |
| `QOPM` | `(nn` | Output mode |
| `QBOOT` | `(1`/`(0` | DSP has bootstrap |
| `QPGSn` | 27+ fields | Parallel unit n: serial, mode, fault code, …, status bits. LV5048 users report `(NAKss` for QPGS0. [SECONDARY https://github.com/jblance/mpp-solar/issues/11] |
| `QBEQI` | – | Equalization settings and status |
| `QOPPT`, `QCHPT` | – | Time-of-day priority tables (PMAX) |
| `QBMS` | – | Sources disagree. PMAX documents it as a "BMS message" answered `(ACK`. mpp-solar `pi30.py` and ESPHome pip8048 treat it as a read of BMS values (connect status, SOC, charge/discharge flags, CV/float/cut-off voltage, current limits). Keep it out of the default whitelist; probe it manually only. |
| `QLED` | – | RGB LED settings |

[CONFIRMED-primary P2014, P2015, PMAX unless marked]

### 5.12 What changed across firmware and documents (summary)

| Item | Older | Newer | Source |
|---|---|---|---|
| QPIGS field count | 17 (P2014) | 21 (P2015/PGK/P41/clones) → 24 (PMAX) | [CONFIRMED-primary] |
| QPIGS idx 12 PV current | `EEEE` integer A | `EE.E` float A | P2014 vs PMAX/PGK |
| QPIGS idx 19 PV charging power | absent | `MMMMM` W ("PV Charging power 1" / "PV1 Charging power") | P2015/PMAX |
| QPIGS idx 11 temperature | NTC A/D value on 1–3 kVA | °C | P2014/P2015 note |
| QPIGS status bit b7 | "SBU priority version added" | PGK: "PV or AC feed the load" | PGK |
| QPIRI | 25 fields | 26–27 (CV time, operation logic) → 28 (max discharge current) | P2014 / PGK / PMAX |
| QPIWS | 32 chars | 36 (PMAX/PGK, fault code embedded) / 38 (P2015) | [CONFIRMED-primary] |
| QMOD | P S L B F H | + D, C, Y, E | PMAX/PGK |
| Charger priority codes | 0–3 | 1–3 only (MAX) | PMAX |
| Q1 | 17 fields; SCC power scaling differs between fw 52.30 and 72.40 | 27 fields (fw 74.40) | Q1DOC [SECONDARY] |
| Output-power field width | – | ANERN user: solar wattage read as `0200` instead of `10200` (field overflow) | [SECONDARY S2M wiki] |

---

## 6. Setting commands (never sent by the logger by default)
Every setter returns `(ACK<CRC><cr>` or `(NAK<CRC><cr>`. [CONFIRMED-primary P2014 §3]

| Command | Effect | Risk |
|---|---|---|
| `POP00/01/02` | Output source priority: Utility / Solar / SBU | Changes when the battery is cycled |
| `PCP00..03` | Charger priority: Utility / Solar / Solar+Utility / Solar only (MS: no 02) | Can stop grid charging |
| `PBT00..` | Battery type | **Wrong type can over- or under-charge lithium** |
| `PSDV nn.n` | Battery cut-off voltage | **Battery damage** |
| `PCVV nn.n` / `PBFT nn.n` | Bulk (C.V.) / float voltage | **Battery damage** |
| `PBCV nn.n` / `PBDV nn.n` | Back-to-grid / back-to-battery voltages | – |
| `MCHGC` / `MNCHGC` / `MUCHGC` / `MSCHGC` | Max charge currents | – |
| `F50/F60`, `V230` | Output frequency / voltage | **Can damage loads** |
| `PGR00/01` | Grid range: Appliance / UPS | – |
| `PEx` / `PDx` | Enable/disable flags (buzzer, bypass, …) | – |
| **`PF`** | "All Device parameters set to default value" | **Factory reset** |
| `RTEY` / `RTDL` | "Reset all stored data for PV/load energy" / "Erase all data log" | **Data loss** |
| `PBATCD abc` | Enable/disable charger/discharger; can "shut down unit completely" | **Outage** |
| `PBATMAXDISC`, `PBEQ*`, `PCVT`, `PCST`, `DAT`, `BTA±nn` (calibration), `PSAVE`, `ATE1`/`ATE0`, `PLED*` | Various | – |

Sources: P2014 §3, P2015 §3, PMAX §3, MPP pi30.py SETTER_COMMANDS [CONFIRMED-primary]. ESPHome docs: "Not all possible switches are exposed as they lead to the possibility to make serious damage." [CONFIRMED-primary https://esphome.io/components/pipsolar/]

**Recommendation** [INFERRED]: hard-code a whitelist of read commands in firmware:
- `QPI QID QSID QVFW QVFW2 QVFW3 QMN QGMN QPIRI QFLAG QDI QPIGS QPIGS2 QMOD QPIWS Q1 QMCHGCR QMUCHGCR QT QET QLT QBEQI`
- `QEY…`/`QEM…`/`QED…`/`QLY…`/`QLM…`/`QLD…`
- `Q6` for the RS485 UPS-style variant

Refuse everything else unless a compile-time flag enables writes. This is implemented and tested in `sketch_core.h`, where `POP02` → `PI_REFUSED` and nothing is transmitted.

---

## 7. Timing, rates and buffers

| Parameter | Value | Source |
|---|---|---|
| Byte time at 2400 8N1 | 10 bits / 2400 = **4.17 ms/byte** | [INFERRED arithmetic] |
| QPIGS reply length | 106 bytes on EASun (S2M log `L:106`); ~110 (NED default `qpigs=110`); 116 on PMAX/Phocos (`L:116`) → **~0.45–0.5 s on the wire** | [CONFIRMED-primary S2M dumps, NED conf] |
| QPIRI reply length | 94 (EASun) – 109 (Phocos) bytes | S2M dumps |
| QPIWS / QMOD | 32–36 / 1 payload bytes | S2M dumps |
| ESPHome core pipsolar | default `update_interval` **1 s** (`cv.polling_component_schema("1s")`); one command in flight; `COMMAND_TIMEOUT = 5000` ms; read buffer **128** bytes; command queue 10; ≤15 polling commands | [CONFIRMED-primary ESPC `__init__.py`, `pipsolar.h`] |
| SYS pip8048 | buffer 130, timeout 5000 ms, 16 polling commands | [CONFIRMED-primary SYS pipsolar.h] |
| Solar2MQTT | after write: `flush()` + `delay(20)`. First-byte timeout = 120 bytes × 10 × 1000 / baud + 350 ms ≈ **850 ms**. Inter-byte timeout **200 ms**. Frame timeout ≈ **1.9 s**. Max reply **256** bytes. Default intervals: QPIGS **5 s**, Q1 2 s, QFLAG 60 s, QPIRI 300 s (PowMr scheduler). | [CONFIRMED-primary S2M PiReplyReader.h, PI_Serial.cpp, PowMrPiSupplementScheduler.h] |
| ned-kelly | reads fixed sizes (qpiri=98, qpiws=36, qmod=5, qpigs=110); 2 s read timeout; sleeps 5 s per cycle | [CONFIRMED-primary NED] |
| mpp-solar serial | 1 s read timeout; 0.1 s sleep after write; `read_until(b"\r")` | [CONFIRMED-primary MPP serialio.py] |
| SYS protocol probe | 1 s between test commands | [CONFIRMED-primary SYS test-protocols.yaml] |
| RP6000 (Q6, RS485) | response timeout 1500 ms; quiet gap 200 ms; partial timeout 500 ms; recovery 2000 ms | [SECONDARY RP rp6000.h] |
| USB HID (NUT) | 8-byte reports; reply timeout 1000 ms (3000 ms for 0665:5161); flush stale reports first | [CONFIRMED-primary NUT] |
| First command after open may time out (USB/Python) | "If I run QPIGS again straight after, then it runs quick" | [SECONDARY powerforum topic 3950 snippet] |

**Recommended logger schedule** [INFERRED, consistent with the above]:
- Strictly one command at a time. Wait for CR or timeout (1.5 s first byte, 200 ms inter-byte, 2 s total), then leave a ≥100 ms gap before the next command.
- **Every 5–10 s:** QPIGS, QMOD, QPIWS (~1 s total wire time).
- **Every 30–60 s:** Q1 (if supported).
- **Every 5–10 min, or immediately when QPIGS status b6 = 1:** QPIRI, QFLAG.
- **At boot and hourly:** QPI, QID/QSID, QVFW, QVFW2, QMN, QGMN.
- **Every 5 min, if the probe succeeded:** QET, QT, QEDyyyymmdd, QLDyyyymmdd.
- Mark NAK'd commands unsupported and re-probe daily.
- Buffer at least 256 bytes.

---

## 8. Clone quirks catalogue

| # | Quirk | Mitigation | Evidence |
|---|---|---|---|
| 1 | Field counts differ (QPIGS 17/21/24, QPIRI 25–28, QPIWS 32/36/38) | Index-based parse, tolerate short replies, keep raw | [CONFIRMED-primary docs + dumps] |
| 2 | Number formats differ (`0000` vs `00.0` PV A; `50` vs `060`; `-` parallel) | `atof`/`atoi` per token; treat non-numeric as null | [CONFIRMED-primary MPP CRC-valid frames] |
| 3 | Unknown characters in QFLAG (`;`) | Ignore unknown letters | [SECONDARY SYS frames_powmr] |
| 4 | NAK without CRC; replies without CRC (PI16 optional CRC; "QPI … no checksums" reports) | Accept `(NAK` with or without CRC; optional no-CRC mode | [CONFIRMED-primary P16, S2M code; SECONDARY powerforum topic 1251 snippet] |
| 5 | CRC 0x00 bump differs between implementations | Send the 3-byte rule; accept both | [CONFIRMED-primary MPP vs NED/ESPC/S2M] |
| 6 | Echo of the request (loopback, RS485 transceiver, wrong wiring) | Require `(`; flush RX before TX; S2M ignores echoes | [SECONDARY I166; CONFIRMED-primary S2M `isEchoedCommand`] |
| 7 | Line noise / corrupted frames | CRC check, retry, RX capacitor | [CONFIRMED-primary SYS easun PDU; SECONDARY PMR] |
| 8 | Docs list commands the unit NAKs (QPGSn on LV5048; QET/QPIGS2/QMN on EASun SP-3200) | Probe once, cache support | [SECONDARY mpp-solar #11; S2M dump] |
| 9 | Same UART speaks two protocols (PowMr: PI30 + Modbus slave 5 @2400) | Allows choosing either | [CONFIRMED-primary S2M comment; SECONDARY PMR] |
| 10 | RS485 port on Voltronic-look units speaks Q1/Q6 (no CRC) and replies `#-1` to PI30 | Include Q1/Q6 in the probe | [SECONDARY I166, RP] |
| 11 | Non-2400 baud families on look-alike hardware (9600 SMG/Anenji/Deye; 19200 MUST); WiFi Plug Pro dongle UART "usually 9600" | Probe 2400 → 9600 → 19200 | [CONFIRMED-primary S2M device headers; SECONDARY powerforum profile 39373-sac snippet] |
| 12 | Heat-sink "temperature" may be a raw ADC value on 1–3 kVA units | Sanity-check range, label as "raw" if >150 | [CONFIRMED-primary P2014/P2015] |
| 13 | Docs omit CRC (QT) but the device sends one | Generic CRC check on every reply | [CONFIRMED-primary MPP QT test frame `JF`] |
| 14 | Some test frames in mpp-solar have wrong CRCs (pi30 QMOD) | Don't hard-code test frames as truth | [CONFIRMED-primary recomputation] |
| 15 | Only one master on the port (dongle vs logger) | Unplug the dongle or sniff passively | [SECONDARY powerforum] |

---

## 9. Probe algorithm for an unknown "Voltronic-look" inverter (fits the user's RS485-only hardware)
Assumes the ESP32-S3 drives the auto-direction RS485 module on UART1. All steps are read-only. [INFERRED design, built from the evidence above]

1. **Wiring check (no inverter needed):** a module-to-module test is impossible with only one RS485 module. Instead do an S2M-style loopback on the ESP side: temporarily jumper the ESP's UART TX GPIO to its RX GPIO, send a pattern (S2M uses `S2MQT`) and confirm it reads back. Then remove the jumper. [CONFIRMED-primary that S2M has `loopbackTest()`]
2. **Connect A/B per the paper manual.** Known RS485-port pinouts are pin 3 = B, pin 4 = A, pin 2 = GND, with +5 V on pins 1/7 (RP6000, RS3024). These must be confirmed against the manual. If no reply comes, swap A/B; this is harmless. [SECONDARY RP, I166; INFERRED]
3. **PI30 @ 2400 8N1:** send `QPI` (`51 50 49 BE AC 0D`). Wait 1.5 s. Discard any echo (frames not starting with `(`).
   - `(PI30`/`(PI41`/… → PI30 family. Continue with QMN, QPIRI, QPIGS, QPIWS, QFLAG, Q1, QET.
   - `(NAK` → PI30-capable but `QPI` is unsupported. Still try QPIGS.
   - `#-1\r` → legacy firmware (see step 5).
4. **PI18 @ 2400:** send `^P005PI` + `71 8B 0D`. A reply `^D00518…` means PI18.
5. **Legacy UPS-style @ 2400:** send `Q1\r`, then `Q6\r` (no CRC). A reply starting `(` with 8 (Q1) or about 19 (Q6) space-separated fields means RP/RS-series protocol.
6. **Modbus RTU** (detail belongs to the Modbus researcher):
   - slave 5 @ 2400 (PowMr, read 4501+)
   - slave 1 @ 9600 (SMG-II/Anenji/Deye)
   - slave 4 @ 19200 (MUST)
7. **If nothing replies:** repeat steps 3–5 at 9600 and 4800. SYS suggested "4800, 9600, 19200, 38400, 56700, 115200" when stuck. [SECONDARY I166] If still nothing, the RS485 port may be a remote-panel bus only (syssi's hypothesis in I166). [SECONDARY]
8. **Persist the detected protocol and baud** in NVS. Re-probe only on repeated failures.

---

## 10. ESP32 implementations and how to reuse them

| Project | What it is | Protocols | Notes for ESP32-S3 |
|---|---|---|---|
| **ESPHome core `pipsolar`** | Built-in component (codeowner @andreashergert1984) | PI30 | Polls only the commands needed by configured entities (macro `add_polling_command_` in each `set_*`). Commands: QPIRI, QPIGS, QMOD, QFLAG, QPIWS, QT, QMN, QET, QLT. Entities: sensors (e.g. `battery_voltage`, `pv_input_voltage`, `pv_charging_power`, `ac_output_active_power`, `total_pv_generated_energy`), binary sensors (`load_status`, `faults_present`, `warnings_present`, `fault_*`, `warning_*`), text sensors (`device_mode`, `last_qpigs`…). [CONFIRMED-primary ESPC] |
| **syssi/esphome-pipsolar** | External components `pip8048`, `pip2424mse1`, `pi18` | PI30 extended, PI18 | pip8048 adds QPIGS2, QBATCD, QPGS0, Q1, QBMS, QMCHGCR, QMUCHGCR. Requires "ESPHome 2026.3.0 or higher". Includes the protocol-probe YAML and UART sniffer. [CONFIRMED-primary SYS] |
| **softwarecrash/Solar2MQTT** | Complete ESP32 firmware: web UI, MQTT, JSON `/livejson`, OTA | PI15/16/18/30/30MAX/30REVO/PIP-GK/41 + Modbus MUST/SMG/Anenji/Deye/PowMr | Auto-detects protocol. ESP32-S3 SuperMini default pins RX GPIO16 / TX GPIO15 / RS485 DE GPIO7. Reported working on ESP32-S3-N16R8 + RS485. **License CC BY-NC-ND 4.0** (README badge), so its code cannot be modified and redistributed. [CONFIRMED-primary S2M README/wiki] |
| aguglie/esphome-rp6000 | ESPHome component | Q6 over RS485 | DE pin hard-wired to GPIO5 (the user's auto-direction module doesn't need it). [SECONDARY RP] |
| syssi/esphome-config-examples `esp8266-voltronic-rp-rs.yaml` | UART-debug-lambda parser | Q6 | Lists RS3000/RS3024/RS2000/RS4000/RP5000/RP6000/RP6024E/RP6048E. [SECONDARY] |
| leodesigner/powmr_comm | ESP8266 MQTT bridge | PowMr Modbus @2400 | [SECONDARY PMR] |
| jblance/mpp-solar | Python CLI/daemon | all PI | Has an `esp32io.py` (MicroPython UART) path. Best protocol reference. [CONFIRMED-primary MPP] |
| ned-kelly/docker-voltronic-homeassistant | Linux C++ poller + MQTT | PI30 | Fixed reply lengths (fragile). [CONFIRMED-primary NED] |

**ESP32-S3-N16R8 pin guidance** [CONFIRMED-primary Espressif docs]:
- **Avoid these pins:**
  - GPIO0, 3, 45, 46 are strapping pins: "GPIO0, GPIO3, GPIO45 and GPIO46 are strapping pins".
  - GPIO19/20 are USB D-/D+ ("used by USB-JTAG by default").
  - GPIO26–37: flash/PSRAM. GPIO35–37 are "not available for external use" on Octal-PSRAM modules (N16R8 is Octal).
  - GPIO43/44 are UART0 (console).
  - GPIO38 or GPIO48 drives the RGB LED (board v1.1 vs v1.0).
- **Use instead:** UART1 on e.g. **TX = GPIO17, RX = GPIO18** [INFERRED], or S2M's S3 defaults RX 16 / TX 15.
- Sources: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html , https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html

**Minimal ESPHome config (PI30 over the RS485 module)** [INFERRED: not compiled here. Keys checked against ESPC sources.]
```yaml
esphome:
  name: savx-logger
esp32:
  board: esp32-s3-devkitc-1
  flash_size: 16MB
  framework: { type: esp-idf }
psram: { mode: octal, speed: 80MHz }
wifi: { ssid: !secret wifi_ssid, password: !secret wifi_password }
api:
ota: { platform: esphome }
logger: { level: DEBUG }
uart:
  id: inv_uart
  baud_rate: 2400
  tx_pin: GPIO17      # -> RS485 module TXD/DI
  rx_pin: GPIO18      # <- RS485 module RXD/RO
  # debug: { direction: BOTH, dummy_receiver: false, after: { delimiter: "\r" },
  #          sequence: [ lambda: UARTDebug::log_string(direction, bytes); ] }
pipsolar:
  id: inverter0
  uart_id: inv_uart
  update_interval: 10s
sensor:
  - platform: pipsolar
    pipsolar_id: inverter0
    grid_voltage: { name: "Grid V" }
    ac_output_voltage: { name: "Out V" }
    ac_output_active_power: { name: "Load W" }
    output_load_percent: { name: "Load %" }
    battery_voltage: { name: "Battery V" }
    battery_charging_current: { name: "Charge A" }
    battery_discharge_current: { name: "Discharge A" }
    battery_capacity_percent: { name: "SOC" }
    pv_input_voltage: { name: "PV V" }
    pv_input_current_for_battery: { name: "PV A" }
    pv_charging_power: { name: "PV charge W" }
    inverter_heat_sink_temperature: { name: "Heatsink" }
text_sensor:
  - platform: pipsolar
    pipsolar_id: inverter0
    device_mode: { name: "Mode" }
    last_qpigs: { name: "Raw QPIGS" }
    last_qpiws: { name: "Raw QPIWS" }
binary_sensor:
  - platform: pipsolar
    pipsolar_id: inverter0
    warnings_present: { name: "Warnings" }
    faults_present: { name: "Faults" }
```
Caveat: the ESPHome components have no protocol auto-probe and will not handle Q6 or Modbus. Use SYS `tests/esp8266-test-protocols.yaml` (adapted to S3 pins) as step 1 to identify the protocol. [CONFIRMED-primary SYS README "If you don't know the protocol of your inverter please use this configuration …"]

**Arduino-ESP32 read-only poller core:** host-tested with mocks for OK, timeout, NAK, no-CRC and refused-setter cases. Not yet run on hardware.
```cpp
#include <Arduino.h>
#include "sketch_core.h"           // includes pi30.h; whitelist + pi_query<Port,Clock>()
struct EspClock { uint32_t ms() { return millis(); } void yield_ms(uint32_t d) { delay(d); } };
struct EspPort  { HardwareSerial &s; int available(){return s.available();} int read(){return s.read();}
                  void write(const uint8_t*b,size_t n){s.write(b,n);} void flush(){s.flush();} };
HardwareSerial Inv(1); EspPort port{Inv}; EspClock clk;
void setup() { Serial.begin(115200); Inv.begin(2400, SERIAL_8N1, /*RX*/18, /*TX*/17); }
void loop() {
  static const char *cycle[] = {"QPIGS", "QMOD", "QPIWS"};
  for (const char *cmd : cycle) {
    uint8_t buf[256]; const char *pl = nullptr;
    PiResult r = pi_query(port, clk, cmd, buf, sizeof buf, &pl, 1500, 200, false);
    Serial.printf("%s -> %d %s\n", cmd, r, (r == PI_OK) ? pl : "");
    if (r == PI_OK && !strcmp(cmd, "QPIGS")) { Qpigs q; if (pi_parse_qpigs((char*)pl, &q)) {
        Serial.printf("bat=%.2fV pv=%.1fV pvW=%d loadW=%d mode-bits=%s\n", q.batt_v, q.pv_v, q.pv_w, q.out_w, q.status1); } }
    delay(150);                    // inter-command gap
  }
  delay(10000);                    // 10 s cycle
}
```

---

## 11. WatchPower / SmartESS WiFi dongles and coexistence
- **Voltronic WatchPower** is the vendor PC software. SolarAssistant advises testing with "the WatchPower or SolarPower app on a laptop. If it works there, it should work with SolarAssistant". It speaks the same PI30 protocol. [SECONDARY https://solar-assistant.io/help/inverters/powmr/POW-HVM-KP/rs232] Solar2MQTT targets "Most devices that use the watchpower PC Software". [CONFIRMED-primary S2M README]
- **PowMr WIFI-VM / WiFi Plug Pro (SmartESS / DESS Monitor cloud)** plugs into the RJ45 or COM port and is powered from it (PowMr RJ45 pin 4 = +12 V). The dongle polls Modbus slave 5: "read 45 registers … starting from address 4501" and "read 16 registers … from address 4546" "every few seconds". [SECONDARY PMR]
- One WiFi Plug Pro teardown reports an inverter-side UART "usually 9600 baud" and a debug UART at 115200 that leaks WiFi credentials. [SECONDARY powerforum profile 39373-sac, search snippet only; the site blocks automated fetches]
- **Coexistence:** the dongle and the ESP32 are both masters. Use one or the other, or sniff passively with RX-only taps. [SECONDARY + INFERRED]
- **iOS:** these cloud apps (WatchPower, SmartESS) need the vendor dongle, which the user did not buy. The DIY path is ESP32 → WiFi → own backend or web dashboard. [INFERRED]

---

## 12. Applicability to the Savx F4 3KW (24 V)
Evidence that it is in the Voltronic-style family:
- **The front panel matches.** The photo shows LEDs labelled `AC/INV`, `CHG`, `FAULT` and keys `ESC`, `UP`, `DOWN`, `ENTER` (read from 1.jpg). The Tripp Lite APSWX manual (a Voltronic-style MPPT inverter/charger) documents exactly this set:
  - "AC/INV Green — Solid On: AC Output is by AC Mains in Line Mode; Flashing: AC Output is powered by Battery / PV"
  - "CHG Green — Solid On: Battery is fully charged; Flashing: Battery is charging"
  - "FAULT Red — Solid On: A fault condition has occurred; Flashing: A warning condition is present"
  - "ESC/UP/DOWN/ENTER" keys

  [CONFIRMED-primary TRIPP §2.4.1–2.4.2]
- **The ratings match known PI30 units.** Savx nameplate: 3200 VA, 24 VDC, AC output 13.9 A. Matches:
  - EASun SP-3200 QPIRI `220.0 14.5 220.0 50.0 13.9 3200 3000 24.0 …` (PI30, 21-field QPIGS, no energy counters) [CONFIRMED-primary capture via S2M]
  - Sumry SP-3200 (PI30, RS232; reported QPIRI `230.0 13.9 230.0 50.0 13.9 3200 3000 24.0 23.0 …`) [SECONDARY S2M wiki]
  - PowMr POW-HVM3.2H-24V (PI30 via RS232 per S2M wiki; Modbus slave 5 per PMR) [SECONDARY]

  [INFERRED: the Savx F4 is very likely an OEM of this 3.2 kW / 24 V class.]
- **But** the user reports the ports are "RS485" and "BMS/CAN" (no RS232 port). This matches the RP/RS-series case (I166: 24 V / 3 kW, Voltronic-look panel, RS485 port, Q1/Q6 protocol, `#-1` to PI30). It also matches units that run PI30 over RS485 (SUOER). **The protocol cannot be confirmed without the §9 probe.** [INFERRED]
- **"Li" battery tag and BMS/CAN port:** the inverter is likely set to a lithium battery type (QPIRI idx 12 may read 8 or 9, or a vendor code). The BMS port carries battery comms, not monitoring. Leave it untouched. [INFERRED]
- **Expected data if PI30 works:** grid/output V and Hz, load W/VA/%, battery V, charge/discharge A, SOC %, PV V/A, PV charging W, heat-sink temperature, mode letter, warning/fault bits, settings. Energy counters are probably not available, so integrate on the ESP or the server. [INFERRED from the EASun SP-3200 dump]

---

## 13. Open questions and risks
- Exact RJ45 pinout of the Savx "RS485" port, and whether pins carry +5 V or +12 V: **take it from the paper manual.** No multimeter is available. [INFERRED]
- Whether the Savx speaks PI30, Q1/Q6 or Modbus on that port, and at which baud. The probe is mandatory.
- Auto-direction RS485 modules derive DE from the TX line. At 2400 baud this usually works but is unverified for this module; validate it with the PI30 probe's echo/NAK behaviour. [INFERRED]
- The mpp-solar 0x00 CRC rule could matter for some date-based energy queries. The tolerant receive plus NAK-retry described in §4.2 mitigates it.
- powerforum.co.za threads are Cloudflare-blocked for automated fetches. Claims from them come from search-result snippets and are tagged [SECONDARY].

---

## Appendix A: Files produced
- `<scratch>`: CRC, frame builder, validator (tolerant CRC, NAK, echo rejection), QPIGS parser, status-bit helper.
- `.../r2-code/sketch_core.h`: read-only whitelist and a transport-agnostic `pi_query()` with first-byte and inter-byte timeouts.
- `.../r2-code/test.cpp`, `.../r2-code/mock_test.cpp`: host unit tests, all passing.
- `.../r2-code/crc_table.py`: regenerates the CRC table in §4.3.

## Appendix B: Raw evidence snippets (verbatim device traffic)
- Voltronic Axpert MAX II 8 kW (SYS `docs/pdus/pip8048/voltronic-axpert-max-ii-8kw.txt`):
  - `>>> 51:50:49:47:53:B7:A9:0D` / `<<< (222.6 49.9 230.1 50.0 0551 0499 006 398 54.30 000 061 0039 01.0 214.4 00.00 00000 00010110 00 00 00226 010` + `48 D1 0D`. This is 21 fields even on a MAX, with PV current in `01.0` format.
  - `>>> QPIGS2` → `(01.0 202.0 00205 ` + `27 4A 0D`
  - `>>> QMOD` → `(B` + `E7 C9 0D`
  - `>>> QFLAG` → `(EbkuzDajvxy` + `B4 EE 0D`
  - `>>> QPIRI` → `(230.0 34.7 230.0 50.0 34.7 8000 8000 48.0 51.0 48.0 55.2 55.0 2 010 040 0 2 3 9 01 0 0 52.0 1 1 480 0 140` + `C0 31 0D` (28 fields)
- EASun 5.5 kW (SYS PDU): `QPIRI` → `(230.0 23.9 230.0 50.0 23.9 5500 5500 48.0 51.0 48.0 55.3 55.3 2 02 100 0 2 3 1 01 0 0 53.0 0 1` + `0E 3E` (raw CRC 0x0D3E bumped).
- EASun SP-3200 (S2M dump):
  - `QPI = PI30`
  - `QMOD = B`
  - `QPIGS = 000.0 00.0 220.1 49.9 0198 0155 006 395 26.30 000 060 0028 0000 000.0 00.00 00004 00010000 00 00 00001 010`
  - `QPIGS2 = NAK`, `QET = NAK`, `QMN = NAK`
  - `QFLAG = EazDbjkuvxy`
- Voltronic RS3024 over RS485 (I166):
  - `>>> QPI\xBE\xAC\r` / `<<< #-1\r`
  - `>>> Q1\r` / `<<< (000.0 000.0 229.6 000 50.0 12.7 09.0 10001001\r`
