# 02 — Protocol reference: Voltronic PI30 family on the Savx F4 COM port

This is the protocol the logger speaks to the inverter: physical layer, framing, checksums, every request frame byte for byte, every reply field, how the firmware discovers which dialect it is talking to, and what it must never send. The inverter itself is described in [01-inverter-profile.md](01-inverter-profile.md); the wiring in [03-hardware-and-wiring.md](03-hardware-and-wiring.md); the firmware that implements this in [05-firmware-guide.md](05-firmware-guide.md).

**Status of the facts on this page**

| Fact | Status |
|---|---|
| The free port is RS232 "COM" (RJ45 pins 1 RXD, 2 TXD, 4 +VCC, 8 GND) | **Confirmed** — photo + paper manual |
| The protocol is the Voltronic PI30 ASCII family at 2400 8N1 | **High confidence** — the manual tells users to install WatchPower; not yet seen on the wire |
| Stock PI30 vs Sorotec "PI30REVO" dialect | **Unknown (~50/50)** — the probe (§9) decides in seconds |
| Every request frame and CRC on this page | **Verified** — recomputed with the reference algorithm in §4; matches captured traffic and open-source implementations |
| Which RJ45 pin is the inverter's TX | **Unknown** until the idle-level test (prior: pin 1) |

Tags used below: **[confirmed]** (official document, source code, or the unit itself), **[secondary]** (forum / README narrative), **[inferred]** (our reasoning), **[unknown]**.

Conventions: field indices are **0-based** after stripping the leading `(` and the trailing checksum and splitting on single spaces. Hex bytes are written `51 50 49`. `<cr>` = 0x0D.

Deep evidence: [r2 Voltronic PI30](../research/r2-voltronic-pi30.md) · [v1 verification](../research/v1-protocol-verification.md) · [d1 architecture §3](../research/d1-architecture.md) · [r3 Modbus families](../research/r3-modbus-families.md).

---

## 1. The protocol family at a glance

Voltronic Power's "PI" protocols are used by Axpert / PIP and their many rebrands. The reply to `QPI` names the protocol.

| ID | Framing | Checksum | Relevance here |
|---|---|---|---|
| **PI30** (stock: HS/MS/MSX 2014, KS/MKS 2015) | `CMD<CRC><cr>` → `(…<CRC><cr>` | CRC-16/XMODEM + byte adjust | **Expected.** QPIGS has 17 or 21 fields. |
| **PI30 MAX** | same | same | Possible. 24-field QPIGS, 28-field QPIRI, energy counters. `QPI` still answers `PI30`. |
| **PI30 PIP-GK/MK** | same | same | Possible. 36-char QPIWS with embedded fault code, extra QMOD letters. |
| **PI30 REVO** (Sorotec) | same, plus `QALL<CHK><cr>` | CRC for most commands; **sum+1 CHK** for QALL/PQSE/PQSA | **Plausible** (the platform is probably Sorotec). QPIGS field 12 = PV **power**. |
| PI18 (`^P005PI` → `^D00518`) | `^Pnnn…<CRC><cr>` | CRC-16 | Detect-only probe |
| Legacy `Q1`/`Q6` (replies `#-1` to PI30) | no CRC | none | Detect-only probe (seen on RS485 ports of Voltronic RP/RS units) |

Sources: the Voltronic PDFs and the Sorotec PDF listed in §15. [confirmed]

---

## 2. Physical layer

### 2.1 Serial format

**2400 baud, 1 start bit, 8 data bits, no parity, 1 stop bit, no flow control.** Stated identically in the Voltronic PI30 2014/2015/MAX/GK documents ("Baud rate 2400 / Start bit 1 / Data bit 8 / Parity bit N / Stop bit 1") and in Sorotec's REVO document ("Baud rate: 2400"). [confirmed]

At 2400 8N1 one byte takes 10 bits / 2400 = **4.17 ms**. A QPIGS reply (≈ 106–116 bytes) therefore takes **≈ 0.45–0.5 s** on the wire.

9600 baud is only a fallback probe: some community reports mention PI30 at 9600 [secondary].

### 2.2 The COM jack (RJ45)

| RJ45 pin | Manual / silkscreen | Expected role | Logger connection |
|---|---|---|---|
| 1 | RXD | **Probably the inverter's TX** (Voltronic cable drawing: RJ45 pin 1 → PC DB9 pin 2 = PC receive) [inferred, medium-high] | Decided by the idle-level test: module **A+** if it is the TX, otherwise ESP **GPIO17** |
| 2 | TXD | Probably the inverter's RX | The other one of the two |
| 3 | – | unknown | cut, fold back, insulate |
| 4 | **+VCC** (manual only) | Supply for the optional WiFi collector; voltage **not stated** (+12 V on many Voltronic-class units) | ⚠️ **NEVER connect** |
| 5, 6, 7 | – | unknown (some brands put a supply on pin 7) | cut, fold back, insulate |
| 8 | GND | Signal ground | module **B−** + module GND + ESP GND |

**Why only pins 1, 2 and 8.** A UART link needs exactly three conductors: inverter TX, inverter RX and a common ground. Every additional conductor adds risk and no benefit:

- Pin 4 is a **power output** of unknown voltage. On it, a 3.3 V ESP32 GPIO (absolute maximum 3.6 V) or the RS485 module's A/B inputs could be destroyed, and the inverter's collector supply could be shorted.
- Unlabelled pins may carry supply rails on some models ("+12V on pin 4 or 7 depending on the model" — syssi/esphome-pipsolar README [secondary]).
- The labels RXD/TXD do not state a perspective, so the firmware never assumes which of pins 1/2 is TX; the idle-level test measures it (milestone M2).

The **BMS** jack (1 485-B, 2 485-A, 4 CAN-H, 5 CAN-L) is the battery link; the inverter is the master there. ⚠️ It is never touched, probed or unplugged.

### 2.3 Signal levels and the "RS232-lite" link

The user owns an RS485↔TTL module, not an RS232 transceiver (MAX3232). The approved design (d1 §3.2, §8.4) uses it in a non-standard but electrically sound way:

| Direction | How | Why it works |
|---|---|---|
| Inverter → ESP | Inverter TX → module **A+**, module **B−** → COM pin 8 (GND). Module **RO** → ESP GPIO18 (UART1 RX, **inverted**). Module **DI** held **HIGH** (driver off). | RS232 TX idles at a **negative** voltage (mark). An RS485 receiver outputs LOW when A − B ≤ −200 mV, so the idle line reads LOW and a start bit reads HIGH: an **inverted** UART signal. |
| ESP → Inverter | ESP **GPIO17** (UART1 TX, **inverted**, 0 / 3.3 V) → inverter RX pin directly | An RS232 receiver (MAX3232 class) switches between V_IT− ≥ 0.6 V and V_IT+ ≤ 2.4 V and has 3–7 kΩ input impedance (TI MAX3232 datasheet). Inverted TX idles at 0 V (= mark for the receiver) and drives +3.3 V for a start bit (= space). |

```
 Inverter COM (RJ45)                     HW-0519 module                    ESP32-S3
 pin X = inverter TX (idles negative) ──► A+        RO ("RXD") ───────────► GPIO18  UART1 RX (inverted)
 pin 8 = GND ──────────────────────────► B−        DI ("TXD") ◄── HIGH ─── GPIO16
                                    └──► GND       VCC ◄─────────────────── 3V3
 pin Y = inverter RX ◄───────────────────────────────────────────────────── GPIO17  UART1 TX (inverted)
                                                    (connected ONLY after the idle-level test)
 X / Y = 1 / 2 as measured by the idle-level test. Pins 3, 4, 5, 6, 7: never connected.
```

> ⚠️ **GPIO17 must never touch the inverter's TX pin.** That pin idles at about −5 to −12 V; the ESP32's input minimum is −0.3 V. This is why the idle-level test (receive-only, module A+ only) comes first. Full procedure: [03-hardware-and-wiring.md](03-hardware-and-wiring.md).

Firmware setting for this link (Arduino-ESP32 3.3.12, `HardwareSerial`): `Serial1.begin(2400, SERIAL_8N1, /*rx=*/18, /*tx=*/17, /*invert=*/true);` — or `setRxInvert()` / `setTxInvert()` to set each direction separately during the probe. [confirmed API]

Link profiles the prober tries, each preceded by 3 s of silent listening (d1 §3.2):

| Profile | Baud | RX inverted | TX inverted | Meaning |
|---|---|---|---|---|
| P1 | 2400 | yes | yes | Expected RS232-lite |
| P2 | 2400 | no | yes | Module polarity reversed in the wiring |
| P3 | 2400 | yes | no | TX polarity probe |
| P4 | 9600 | yes | yes | PI30 at 9600 (secondary reports) |
| P5 | 9600 | no | yes | P2 at 9600 |
| P6 | 2400 / 9600 | no | no | Only when the link mode is TTL_DIRECT or MAX3232 (optional MAX3232 upgrade) |

### 2.4 One master only

The COM port is point-to-point. **Only one device may poll it.** A WiFi collector, a PC with WatchPower and the ESP32 cannot all transmit on it. If anything else is ever attached, the ESP32 must be removed or switched to listen-only. During the probe the firmware listens 3 s before sending; if it hears unsolicited valid-looking frames it sets `FOREIGN_MASTER` and stops transmitting (d1 §3.5).

---

## 3. Framing

| Element | Format | Notes |
|---|---|---|
| **Request (PI30)** | `<ASCII command>` `<CRC hi>` `<CRC lo>` `0D` | Example `QPIGS` = `51 50 49 47 53 B7 A9 0D` |
| **Request (REVO CHK)** | `<ASCII command>` `<CHK>` `0D` | `QALL`, `PQSE`, `PQSA` (and `QLITH0` as the Sorotec doc writes it) |
| **Request (legacy)** | `Q1\r`, `Q6\r` | No checksum — detect-only probe |
| **Reply (PI30)** | `(` `<fields separated by single spaces>` `<CRC hi>` `<CRC lo>` `0D` | The CRC covers everything from `(` to the last data byte |
| **Reply (REVO CHK)** | `(` `<fields>` `<CHK>` `0D` | CHK covers everything from `(` to the last data byte |
| **NAK** | `(NAK` `73 73` `0D` | Some firmware sends `(NAK\r` **without** CRC — accept both |
| **ACK** | `(ACK` `39 20` `0D` | Only answers setters; the logger never sends setters, so it never expects one |
| **Legacy firmware** | `#-1\r` | The unit speaks the old `Q1`/`Q6` protocol instead of PI30 |

Receive rules [confirmed from implementations; inferred choices]:

1. **Reject echoes.** A valid PI30/REVO reply starts with `(`. Anything else (for example the echoed command `QPIGS…`, which carries a *valid* CRC) is discarded. Bytes received while the ESP is transmitting are ignored.
2. **Terminator.** Accept CR (0x0D) **or** LF (0x0A) as the end of a frame; mpp-solar's REVO test data ends in `\n`. Combine this with the **200 ms inter-byte silence** rule (§10), because a REVO CHK byte is not escaped and can itself be 0x0D or 0x0A [inferred].
3. **Parse by index.** Split on spaces; tolerate missing tail fields; a non-numeric token (e.g. `-` for "parallel max") becomes null; number formats differ between firmware (`0000` vs `00.0`, `50` vs `060`), so parse every token as a float or int independently.
4. **Always keep the raw reply string** (hex + ASCII) for diagnostics.

---

## 4. CRC-16/XMODEM with the Voltronic byte adjust

### 4.1 Algorithm

| Parameter | Value |
|---|---|
| Algorithm | CRC-16/XMODEM |
| Polynomial | 0x1021 |
| Initial value | 0x0000 |
| Input / output reflection | none |
| Final XOR | none |
| Byte order on the wire | **high byte first** |
| Voltronic adjust | after computing, if the high byte or the low byte equals **0x28 `(`, 0x0D CR or 0x0A LF**, add 1 to that byte, so a CRC can never look like a frame delimiter |
| mpp-solar variant | mpp-solar's `crcPI()` **also** turns 0x00 into 0x01. ESPHome, ned-kelly and Solar2MQTT do not. **Send with the 3-value rule; accept either on receive.** |

Verified in three independent implementations: ned-kelly `cal_crc_half()` (nibble table + adjust), ESPHome `pipsolar` (`crc16be` + adjust) and mpp-solar `crcPI()`. A real EASun QPIRI reply with raw CRC 0x0D3E was transmitted as `0E 3E`, proving the adjust on a device. [confirmed]

### 4.2 Reference C code

Plain C99, no Arduino dependencies; compiles on the host for unit tests. (The firmware's `pi30` module follows the same logic; a host-tested version exists in the research helper `r2-code/pi30.h`.)

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* CRC-16/XMODEM: poly 0x1021, init 0x0000, no reflection, no final XOR. */
static uint16_t crc16_xmodem(const uint8_t *p, size_t n) {
    uint16_t crc = 0x0000;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Voltronic adjust: a CRC byte must never equal '(' , CR or LF. */
static uint8_t pi30_adjust(uint8_t b) {
    return (b == 0x28 || b == 0x0D || b == 0x0A) ? (uint8_t)(b + 1) : b;
}

uint16_t pi30_crc(const uint8_t *p, size_t n) {
    uint16_t c = crc16_xmodem(p, n);
    return (uint16_t)((pi30_adjust((uint8_t)(c >> 8)) << 8) |
                       pi30_adjust((uint8_t)(c & 0xFF)));
}

/* mpp-solar variant (also bumps 0x00 -> 0x01). Accept on RECEIVE only. */
static uint16_t pi30_crc_mpp(const uint8_t *p, size_t n) {
    uint16_t c = pi30_crc(p, n);
    uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)(c & 0xFF);
    if (hi == 0x00) hi = 0x01;
    if (lo == 0x00) lo = 0x01;
    return (uint16_t)((hi << 8) | lo);
}

/* Build "<cmd><CRC hi><CRC lo>\r" into out (needs strlen(cmd)+3 bytes). */
size_t pi30_build(const char *cmd, uint8_t *out) {
    size_t n = strlen(cmd);
    memcpy(out, cmd, n);
    uint16_t c = pi30_crc((const uint8_t *)cmd, n);
    out[n]     = (uint8_t)(c >> 8);
    out[n + 1] = (uint8_t)(c & 0xFF);
    out[n + 2] = 0x0D;
    return n + 3;
}

typedef enum { PI_OK, PI_NAK, PI_BAD_START, PI_SHORT, PI_BAD_CRC } pi_result;

/* buf = complete reply including the terminator (CR or LF). */
pi_result pi30_check(const uint8_t *buf, size_t len) {
    if (len < 2 || (buf[len - 1] != 0x0D && buf[len - 1] != 0x0A)) return PI_SHORT;
    if (buf[0] != '(') return PI_BAD_START;                  /* echo or garbage */
    if (len >= 4 && memcmp(buf, "(NAK", 4) == 0) return PI_NAK; /* with or without CRC */
    if (len < 4) return PI_SHORT;
    size_t body = len - 3;                                   /* '(' .. last data byte */
    uint16_t rx = (uint16_t)((buf[len - 3] << 8) | buf[len - 2]);
    if (rx == pi30_crc(buf, body) || rx == pi30_crc_mpp(buf, body)) return PI_OK;
    return PI_BAD_CRC;
}

/* Sorotec REVO CHK: (sum of all bytes + 1) & 0xFF, one byte, not escaped. */
uint8_t revo_chk(const uint8_t *p, size_t n) {
    uint32_t s = 0;
    while (n--) s += *p++;
    return (uint8_t)((s + 1) & 0xFF);
}
```

### 4.3 Worked example 1 — `QPI`, step by step

Bytes: `Q` = 0x51, `P` = 0x50, `I` = 0x49.

First byte in detail: `crc = 0x0000 ^ (0x51 << 8) = 0x5100`, then 8 shift steps (XOR with 0x1021 whenever the top bit was 1 before the shift):

| Step | Top bit before shift | CRC after step |
|---|---|---|
| start | | 0x5100 |
| 1 | 0 | 0xA200 |
| 2 | 1 | 0x5421 |
| 3 | 0 | 0xA842 |
| 4 | 1 | 0x40A5 |
| 5 | 0 | 0x814A |
| 6 | 1 | 0x12B5 |
| 7 | 0 | 0x256A |
| 8 | 0 | **0x4AD4** |

Register after each byte: after `Q` 0x4AD4 → after `P` 0x677B → after `I` **0xBEAC**. Neither 0xBE nor 0xAC is 0x28/0x0D/0x0A, so no adjust.

Frame on the wire: **`51 50 49 BE AC 0D`**. This matches the frame in syssi/esphome-pipsolar's protocol-probe YAML. [confirmed]

### 4.4 Worked example 2 — the byte adjust

| Input | Raw CRC | Adjusted | Why | Frame tail |
|---|---|---|---|---|
| `QGMN` | 0x49**28** | 0x4929 | low byte 0x28 = `(` | `49 29 0D` |
| `QBOOT` | 0x**0A**88 | 0x0B88 | high byte 0x0A = LF | `0B 88 0D` |
| `QLY2026` | 0xBF**0D** | 0xBF0E | low byte 0x0D = CR | `BF 0E 0D` |
| `QPIGS` | 0xB7A9 | 0xB7A9 | nothing to adjust | `B7 A9 0D` |

### 4.5 Worked example 3 — checking replies

| Reply payload | CRC (= trailer) | Full reply on the wire |
|---|---|---|
| `(PI30` | 0x9A0B | `28 50 49 33 30 9A 0B 0D` |
| `(NAK` | 0x7373 ("ss") | `28 4E 41 4B 73 73 0D` |
| `(ACK` | 0x3920 | `28 41 43 4B 39 20 0D` |
| `(L` / `(B` / `(S` | 0x0607 / 0xE7C9 / 0xE5D9 | `28 4C 06 07 0D` … |
| `(VERFW:00014.04` (manual's example U1) | 0x60A8 | `… 60 A8 0D` |
| `(VERFW2:00003.03` (manual's example U2) | 0x1D64 | `… 1D 64 0D` |

Unit-test vectors for the firmware (milestone M1): all of the above plus every frame in §6.

---

## 5. Sorotec REVO "CHK" checksum

Sorotec's protocol document defines a second integrity scheme: "**CHK = The accumulated value of sent data + 1, single byte.**" [confirmed]

- **Request:** CHK = (sum of the command bytes + 1) & 0xFF.
- **Reply:** CHK = (sum of all reply bytes **including the leading `(`** + 1) & 0xFF.
- Used for: `QALL`, `PQSE`, `PQSA`, `PSET` (a setter — never sent). The doc also writes `QLITH0<CHK>`, but mpp-solar sends QLITH0 with the PI30 CRC → **try CRC first, then CHK**.
- `QPI`, `QPIGS`, `QPIRI`, `QMOD` use the normal PI30 CRC on REVO firmware.
- Solar2MQTT sends QALL **with the PI30 CRC** (`51 41 4C 4C E8 FB 0D`) and accepts a CHK reply → the firmware tries **CHK first, then the CRC form**. [confirmed code]
- The CHK byte is **not escaped**; it can be 0x0D, 0x0A or 0x28. End-of-frame detection therefore relies on the 200 ms silence rule as well as the terminator [inferred].

Worked examples (all recomputed):

| Command / reply | Byte sum | +1 → CHK | On the wire |
|---|---|---|---|
| `QALL` | 0x51 + 0x41 + 0x4C + 0x4C = 0x12A | 0x12B → **0x2B** | `51 41 4C 4C 2B 0D` (= the Sorotec doc's example) |
| `PQSE` | 0x139 | **0x3A** | `50 51 53 45 3A 0D` (= doc) |
| `PQSA` | 0x135 | **0x36** | `50 51 53 41 36 0D` |
| `QLITH0` (doc form) | 0x1B2 | **0xB3** | `51 4C 49 54 48 30 B3 0D` |
| Doc's QALL example reply `(000 00.0 000 00.0 0000 000 00.0 000 000 000 000 00.0 0000 000000 000000 S 00 00` | – | **0x04** | `… 30 30 04 0D` (= doc) |
| `ACK` (doc's PSET reply example) | 0xCF | **0xD0** | `41 43 4B D0 0D` (= doc) |

Errata to know about (irrelevant for a read-only logger): Sorotec's own PSET example shows CHK 0xF6; by its own rule it is 0xF7. mpp-solar's QALL test frame ends in `\x03\n`, but sum + 1 over that payload is 0x04 — treat library test frames as untrusted. [confirmed, v1 C14–C15]

---

## 6. Byte-exact request frames

Every frame below was recomputed for this document with the code in §4–§5; the ones also listed in [v1 §4.4](../research/v1-protocol-verification.md) were independently verified there and match. The firmware stores the command strings and computes the checksum at run time; these tables are the unit-test vectors.

### 6.1 PI30 (CRC) — identity and status

| Command | Purpose | Bytes on the wire |
|---|---|---|
| `QPI` | Protocol ID | `51 50 49 BE AC 0D` |
| `QID` | Serial number | `51 49 44 D6 EA 0D` |
| `QSID` | Serial number (length-prefixed) | `51 53 49 44 BB 05 0D` |
| `QVFW` | Main CPU firmware | `51 56 46 57 62 99 0D` |
| `QVFW2` | Second CPU firmware | `51 56 46 57 32 C3 F5 0D` |
| `QVFW3` | Third CPU / remote panel firmware | `51 56 46 57 33 D3 D4 0D` |
| `QMN` | Model name | `51 4D 4E BB 64 0D` |
| `QGMN` | General model number | `51 47 4D 4E 49 29 0D` (0x28 adjusted) |
| `QPIGS` | **General status** | `51 50 49 47 53 B7 A9 0D` |
| `QPIGS2` | Second status block (PV2) | `51 50 49 47 53 32 68 2D 0D` |
| `QMOD` | Operating mode | `51 4D 4F 44 49 C1 0D` |
| `QPIWS` | Warning/fault bits | `51 50 49 57 53 B4 DA 0D` |
| `Q1` | Undocumented extended status | `51 31 1B FC 0D` |

### 6.2 PI30 (CRC) — settings (read-only inquiries)

| Command | Purpose | Bytes on the wire |
|---|---|---|
| `QPIRI` | **Ratings and current settings** | `51 50 49 52 49 F8 54 0D` |
| `QFLAG` | Enable/disable flags | `51 46 4C 41 47 98 74 0D` |
| `QDI` | Factory defaults | `51 44 49 71 1B 0D` |
| `QMCHGCR` | Allowed max charging currents (reference only: **not** in the logger's whitelist) | `51 4D 43 48 47 43 52 D8 55 0D` |
| `QMUCHGCR` | Allowed max utility charging currents (reference only: **not** in the whitelist) | `51 4D 55 43 48 47 43 52 26 34 0D` |
| `QBEQI` | Equalization settings/status (reference only: **not** in the whitelist) | `51 42 45 51 49 2E A9 0D` |

### 6.3 PI30 (CRC) — clock and energy (PI30MAX / some clones; often NAK on basic 3.2 kW units)

The CRC covers the whole string including the date digits, so these frames change with the date. Examples for **2026-10-21**:

| Command | Purpose | Bytes on the wire |
|---|---|---|
| `QT` | Inverter clock | `51 54 27 FF 0D` |
| `QET` | Total PV energy | `51 45 54 81 B6 0D` |
| `QLT` | Total load energy (reference only: **not** in the whitelist) | `51 4C 54 3B 2E 0D` |
| `QED20261021` | PV energy on a day | `51 45 44 32 30 32 36 31 30 32 31 95 D0 0D` |
| `QLD20261021` | Load energy on a day | `51 4C 44 32 30 32 36 31 30 32 31 B0 DE 0D` |
| `QEM202610` | PV energy in a month (reference only: **not** in the whitelist) | `51 45 4D 32 30 32 36 31 30 70 DD 0D` |
| `QEY2026` | PV energy in a year (reference only: **not** in the whitelist) | `51 45 59 32 30 32 36 F7 EF 0D` |
| `QLY2026` | Load energy in a year | `51 4C 59 32 30 32 36 BF 0E 0D` (0x0D adjusted) |

Some raw date CRCs contain 0x00 (e.g. `QED20260119` → raw 0x000F). Send with the 3-value rule; if that date query gets NAK, one retry with the mpp-solar 0x00 rule is reasonable [inferred].

### 6.4 Sorotec REVO

| Command | Purpose | Bytes on the wire |
|---|---|---|
| `QALL` (CHK — try first) | All-in-one status | `51 41 4C 4C 2B 0D` |
| `QALL` (CRC — fallback, Solar2MQTT style) | same | `51 41 4C 4C E8 FB 0D` |
| `PQSE` (CHK) | **Read** configuration | `50 51 53 45 3A 0D` |
| `PQSA` (CHK) | **Read** power-flow state | `50 51 53 41 36 0D` |
| `QLITH0` (CRC — try first) | Lithium/BMS data relayed by the inverter | `51 4C 49 54 48 30 02 A4 0D` |
| `QLITH0` (CHK — as the doc writes it) | same | `51 4C 49 54 48 30 B3 0D` |

`PQSE` and `PQSA` start with `P` but are **reads**. The whitelist matches them exactly; it never allows "anything starting with P".

### 6.5 Detect-only probes

| Command | Protocol | Bytes on the wire | Valid reply starts with |
|---|---|---|---|
| `^P005PI` | PI18 | `5E 50 30 30 35 50 49 71 8B 0D` | `^D00518` |
| `Q1` (no CRC) | legacy UPS-style | `51 31 0D` | `(` + 8 fields |
| `Q6` (no CRC) | legacy UPS-style | `51 36 0D` | `(` + ≈ 19 fields |

Modbus probe frames: §13.

### 6.6 Expected reply trailers (for tests and for eyeballing raw logs)

| Reply | CRC bytes |
|---|---|
| `(PI30` | `9A 0B` |
| `(NAK` | `73 73` |
| `(ACK` | `39 20` |
| `(L` · `(B` · `(S` · `(F` · `(H` · `(P` | `06 07` · `E7 C9` · `E5 D9` · `A7 4D` · `46 83` · `D5 BA` |

---

## 7. Response formats, field by field

### 7.1 `QPI` — protocol ID

`(PI30` → PI30 family ("30 for HS series" in P2014, "30 for Axpert KS series" in PMAX). Alternatives `(PI41`, `(PI17`. `(NAK` → the unit is still PI30-capable, continue with QPIGS. `#-1` → legacy Q1/Q6 firmware. [confirmed]

### 7.2 `QID` / `QSID` — serial number

- `QID` → `(XXXXXXXXXXXXXX` (14 digits in P2014).
- `QSID` → `(NN` + 20 digits: NN = number of valid digits, zero-filled.
- Whether either equals the nameplate barcode `250514-0315130023` is **[unknown]** — record both.

### 7.3 `QVFW` / `QVFW2` / `QVFW3` — firmware versions

`(VERFW:NNNNN.NN` (main CPU), `(VERFW2:NNNNN.NN` (second CPU; on 2015 docs "SCC1 CPU"; `00000.00` = not reported), `(VERFW3:…`. The LCD shows the same numbers as `U1 xx xx` / `U2 xx xx` (manual §4.5: `U1 14 04` = 00014.04). **QVFW/QVFW2 must equal the LCD values recorded in the site survey.**

### 7.4 `QMN` / `QGMN` — model

`QMN` → `(MMMMM-NNNN` (e.g. `(MKS2-8000`; many clones report `VMII-NXPW5KW`); basic units often NAK. `QGMN` → `(NNN` (e.g. `(044`). Identity only; NAK is fine.

### 7.5 `QPIGS` — general status (the main logging command)

Template (P2014): `(BBB.B CC.C DDD.D EE.E FFFF GGGG HHH III JJ.JJ KKK OOO TTTT EEEE UUU.U WW.WW PPPPP b7b6b5b4b3b2b1b0`. P2015 / PIP-GK add `QQ VV MMMMM b10b9b8` (21 fields). PI30MAX adds `Y ZZ AAAA` (24 fields). Sorotec REVO has 21 fields with different meanings in places. [confirmed]

| Idx | Format | Stock PI30 meaning | Unit | 17 | 21 | 24 | **REVO** (Sorotec doc) |
|---|---|---|---|---|---|---|---|
| 0 | BBB.B | Grid (AC input) voltage | V | ✓ | ✓ | ✓ | same |
| 1 | CC.C | Grid frequency | Hz | ✓ | ✓ | ✓ | same |
| 2 | DDD.D | AC output voltage | V | ✓ | ✓ | ✓ | same |
| 3 | EE.E | AC output frequency | Hz | ✓ | ✓ | ✓ | same |
| 4 | FFFF | AC output apparent power | VA | ✓ | ✓ | ✓ | **Reserved** (`0000`) |
| 5 | GGGG | AC output active power | W | ✓ | ✓ | ✓ | same (load W) |
| 6 | HHH | Output load percent ("max of W% or VA%") | % | ✓ | ✓ | ✓ | same |
| 7 | III | DC bus voltage | V | ✓ | ✓ | ✓ | same |
| 8 | JJ.JJ | Battery voltage | V | ✓ | ✓ | ✓ | same |
| 9 | KKK | Battery charging current (integer amps) | A | ✓ | ✓ | ✓ | same |
| 10 | OOO | Battery capacity | % | ✓ | ✓ | ✓ | same |
| 11 | TTTT | Inverter heat-sink temperature ("NTC A/D value" on 1–3 kVA HS/MS/MSX) | °C or raw | ✓ | ✓ | ✓ | **Reserved** |
| 12 | EEEE / EE.E | PV input current (for battery); integer in P2014, `EE.E` on MAX/GK | A | ✓ | ✓ | ✓ | **PV power, W** ("0622 means 622W") |
| 13 | UUU.U | PV input voltage | V | ✓ | ✓ | ✓ | same |
| 14 | WW.WW | Battery voltage from SCC (often 0 on single-board units) | V | ✓ | ✓ | ✓ | same |
| 15 | PPPPP | **Battery discharge current** | A | ✓ | ✓ | ✓ | **PV energy today, Wh** (a real REVO trace showed `00000`) |
| 16 | b7…b0 | Device status (8 chars, first char = b7) | flags | ✓ | ✓ | ✓ | status (b2b1b0 documented) |
| 17 | QQ | Battery voltage offset for fans on | 10 mV | – | ✓ | ✓ | Reserved (`00`) |
| 18 | VV | EEPROM version | – | – | ✓ | ✓ | Reserved (`00`) |
| 19 | MMMMM | **PV charging power** (charger output, not strictly PV input) | W | – | ✓ | ✓ | "PV power, unit W" (a real trace showed `00000`) |
| 20 | b10b9b8 | Device status 2 | flags | – | ✓ | ✓ | Reserved (`010`) |
| 21 | Y | Solar feed-to-grid status | 0/1 | – | – | ✓ | – |
| 22 | ZZ | Country regulation (00 India, 01 Germany, 02 South America) | code | – | – | ✓ | – |
| 23 | AAAA | Solar feed-to-grid power | W | – | – | ✓ | – |

**Consequence for REVO:** there is **no battery discharge current in QPIGS** and field 12 is watts. On REVO the logger takes discharge current, PV current and PV power from `QALL` (§7.11) and polls QPIGS only every 30 s for bus voltage and status bits.

**Device status bits (field 16), PI30 HS/MS/MSX/KS/MAX/GK:**

| Char | Bit | Meaning | 1 = |
|---|---|---|---|
| 0 | b7 | "add SBU priority version" (PGK: "PV or AC feed the load") | yes |
| 1 | b6 | **configuration status** | **changed** → the logger re-reads QPIRI/QFLAG immediately |
| 2 | b5 | SCC firmware version | updated |
| 3 | b4 | **load status** | load on |
| 4 | b3 | battery voltage to steady while charging | yes |
| 5 | b2 | **charging status** (any source) | charging |
| 6 | b1 | **SCC (solar) charging** | on |
| 7 | b0 | **AC (utility) charging** | on |

`b2b1b0`: `000` not charging · `110` charging from solar · `101` charging from AC · `111` both. **Status 2 (field 20):** b10 charging to float; b9 switch on; b8 reserved ("dustproof installed" on PMAX/PGK). PI41 (split phase) uses a completely different bit meaning — not expected here. [confirmed]

**Reference replies** (not from this unit):

- 24 V / 3.2 kW EASun SP-3200, battery mode at night (21 fields; payload re-assembled from the per-field values of a Solar2MQTT capture tabulated in [r2 §5.2](../research/r2-voltronic-pi30.md), CRC not shown):
  `000.0 00.0 220.1 49.9 0198 0155 006 395 26.30 000 060 0028 0000 000.0 00.00 00004 00010000 00 00 00001 010`
  → grid 0 V, output 220.1 V, 155 W, battery 26.30 V, 60 %, discharge 4 A, load on, not charging.
- REVO VE II (mpp-solar discussion #78, [secondary]): QPIGS field 12 = `0248` at PV 355.0 V while field 19 = `00000` — the PV power sits in field 12.

### 7.6 `QPIGS2`

PI30MAX (48 V MAX only): `(BB.B CCC.C DDDDD` = PV2 current, PV2 voltage, PV2 charging power. Basic units (EASun SP-3200, Phocos PSW-H) answer NAK. Single-MPPT unit → expected NAK; tried once during the probe only.

### 7.7 `QPIRI` — ratings and current settings

25 fields (P2014 and the REVO doc), 26–27 (PIP-GK), 28 (PI30MAX). Read every 10 min, at boot, and immediately when QPIGS status b6 = 1. The "LCD program" column ties each field to the manual's setting programs ([01 §4.7](01-inverter-profile.md#47-setting-programs-44)); the mapping is confirmed on this unit in milestone M3 by comparing LCD and reply.

| Idx | Format | Stock PI30 meaning (P2014) | Codes / unit | LCD prog | REVO doc label |
|---|---|---|---|---|---|
| 0 | BBB.B | Grid rating voltage | V | – | same |
| 1 | CC.C | Grid rating current | A | – | same ("maximum input current") |
| 2 | DDD.D | AC output rating voltage | V | 10 | same |
| 3 | EE.E | AC output rating frequency | Hz | 09 | same |
| 4 | FF.F | AC output rating current | A — **expect ≈ 13.9** | – | same |
| 5 | HHHH | AC output rating apparent power | VA — **expect 3200** | – | same |
| 6 | IIII | AC output rating active power | W (3200 expected; an EASun 3.2 kVA reported 3000) | – | same |
| 7 | JJ.J | Battery rating voltage | V — **expect 24.0** | – | same |
| 8 | KK.K | Battery **re-charge** voltage (back to utility) | V | 12 | "**Bat Low Vol**" — low-voltage *alarm* point |
| 9 | JJ.J | Battery **under** voltage (cut-off) | V | 29 | "Bat Cut Vol" — shutdown point |
| 10 | KK.K | Battery bulk (C.V.) voltage | V | 26 | "Buck Vol" (bulk / equalizing) |
| 11 | LL.L | Battery float voltage | V | 27 | same |
| 12 | O | Battery type | 0 AGM, 1 Flooded, 2 User. PMAX adds 3 Pylon, 5 Weco, 6 Soltaro, **8 Lib**, 9 Lic. **LIb code on this unit [unknown]** | 05 | 0 AGM, 1 Flooded, 2 User Define |
| 13 | PP (PPP) | Max AC (utility) charging current | A | 11 | "Max charging current it can be configured" |
| 14 | QQ0 | Max (total) charging current | A | 02 | "Current max charging current" (marked Reserved) |
| 15 | O | Input voltage range | 0 Appliance (APL), 1 UPS | 03 | Reserved |
| 16 | P | Output source priority | 0 Utility first, 1 Solar first, 2 SBU | 01 | Reserved |
| 17 | Q | Charger source priority | 0 Utility first, 1 Solar first, 2 Solar + Utility, 3 Only solar (MS/MKS: 0, 1, 3; MAX: 1–3) | 16 | Reserved |
| 18 | R | Parallel max number | 0–9, `-` seen | – | Reserved |
| 19 | SS | Machine type | 00 grid-tie, 01 off-grid, 10 hybrid | – | Reserved |
| 20 | T | Topology | 0 transformerless, 1 transformer | – | Reserved |
| 21 | U | Output mode | 0 single, 1 parallel, 2–7 phase roles | – | Reserved |
| 22 | VV.V | Battery **re-discharge** voltage (back to battery) | V; `00.0` = "battery is full" (FUL) | 13 | Reserved |
| 23 | W | PV OK condition for parallel | 0/1 | – | Reserved |
| 24 | X | PV power balance | 0/1 | – | Reserved |
| 25 | YYY | Max charging time at C.V. stage (GK/MAX) | min | – | – |
| 26 | Z | Operation logic (GK/MAX) | 0 auto, 1 on-line, 2 ECO | – | – |
| 27 | CCC | Max discharging current (MAX) | A (maybe related to program 40 [unknown]) | – | – |

Notes:

- **REVO:** Sorotec's doc marks fields 15–24 "Reserved" and relabels 8/13/14. On REVO firmware the priorities and ranges should be read from **`PQSE`** (§7.12) instead, and QPIRI fields 8/13/14 must not be labelled with the stock meanings until checked against the LCD.
- **Lithium SOC mode:** with program 05 = LIb, programs 12/13/29 are set as SOC %. How QPIRI reports them (volts, percent, or unchanged) is **[unknown]** — the site survey records both views.
- Reference reply (24 V Axpert, CRC-valid mpp-solar test frame): `(230.0 13.0 230.0 50.0 13.0 3000 2400 24.0 23.0 21.0 28.2 27.0 0 30 50 0 2 1 - 01 1 0 26.0 0 0` — note the two-digit `50` and the `-`.

### 7.8 `QMOD` — operating mode

Reply `(X` + CRC.

| Letter | Mode | Documented in | Expected LED state (manual §4.2) |
|---|---|---|---|
| `P` | Power on | P2014, PMAX, PGK, REVO ("keep") | – |
| `S` | Standby | all | no output |
| `L` | **Line** (grid) | all | AC/INV solid green |
| `B` | **Battery** (battery or PV inverter) | all | AC/INV flashing green |
| `F` | **Fault** | all | FAULT solid red |
| `H` | Power saving (REVO: "Sleep") | P2014, P2015, PGK, REVO | – |
| `D` | Shutdown | PMAX, PGK | – |
| `C` | Charge | PGK | – |
| `Y` | Bypass | PGK ("for PIP-MK") | – |
| `E` | ECO | PGK ("for PIP-MK") | – |

A reply of `E`, `C`, `Y` or `D` is a dialect hint (PIP-GK/MK). On REVO, QALL field 15 carries the mode (B/L/S/F) so QMOD is optional.

### 7.9 `QPIWS` — warning and fault bits

Reply `(` + a string of `0`/`1` characters; character *k* = bit a*k*. Length: **32** (P2014), **36** (PI30MAX, PIP-GK/MK), 38 (P2015 Plus Duo). "If the warning happened, the relevant bit will set 1." The Sorotec REVO doc does not list QPIWS, so **NAK is acceptable** there. [confirmed]

Class: **F** = fault, **W** = warning, **F/W** = "fault if a1 = 1, otherwise warning".

| a*k* | P2014 (32 chars) | Class | Differences in PMAX / PGK (36 chars) |
|---|---|---|---|
| 0 | Reserved | – | PV loss (W) |
| 1 | Inverter fault | F | |
| 2 | Bus over | F | |
| 3 | Bus under | F | |
| 4 | Bus soft fail | F | |
| 5 | LINE_FAIL (grid lost) | W | |
| 6 | OPV short | W | F |
| 7 | Inverter voltage too low | F | |
| 8 | Inverter voltage too high | F | |
| 9 | Over temperature | F/W | |
| 10 | Fan locked | F/W | |
| 11 | Battery voltage high | F/W | |
| 12 | Battery low alarm | W | |
| 13 | Reserved | – | |
| 14 | Battery under shutdown | W | |
| 15 | Reserved | – | Battery derating (W) |
| 16 | Over load | F/W | |
| 17 | EEPROM fault | W | |
| 18 | Inverter over current | F | |
| 19 | Inverter soft fail | F | |
| 20 | Self test fail | F | |
| 21 | OP DC voltage over | F | |
| 22 | Battery open | F | |
| 23 | Current sensor fail | F | |
| 24 | Battery short | F | PMAX: reserved |
| 25 | Power limit | W | PMAX: reserved |
| 26 | PV voltage high | W | PMAX: reserved; PGK: W/F |
| 27 | MPPT overload fault | W | PMAX: reserved; PGK: F |
| 28 | MPPT overload warning | W | PMAX: reserved |
| 29 | Battery too low to charge | W | PMAX: reserved |
| 30 | Reserved | – | PGK: DC/DC over current (F) |
| 31 | Reserved | – | **24 V model: fault-code digit 1** (PMAX "a31, a32 is fault code"; PGK a31–a32) |
| 32 | – | – | **fault-code digit 2** |
| 33 | – | – | PGK: low PV energy (W) |
| 34 | – | – | PGK: high AC input during BUS soft start (W) |
| 35 | – | – | Battery equalization (W) |

Fault-class bits: a1–a4, a7, a8, a18–a24, plus a9/a10/a11/a16 when a1 = 1 (d1 §3.9). Real example: an EASun SP-3200 on battery replies `(00000100000000000000000000000000` → only a5 LINE_FAIL is set.

On 36-character firmware, the two fault-code digits use the **LCD fault-code table** (01–09, 51–59; [01 §4.9](01-inverter-profile.md#49-fault-codes-5--shown-lit-on-the-lcd-with-error)). Several bits obviously correspond to LCD warnings (a10 fan locked ↔ W01, a9 over temperature ↔ W02, a12 battery low ↔ W04, a16 overload ↔ W07, a22 battery open ↔ "bP") [inferred], but the logger never converts between tables: it stores the raw value with its source tag (`qpiws32`, `qpiws36`, `revo`, `lcd`), and `shared/codes.json` holds every table so the firmware and the web app decode identically.

### 7.10 `QFLAG` — enable/disable flags

Reply `(E<enabled letters>D<disabled letters>`, e.g. `(EakxyDbjuvz`. Ignore unknown characters (a PowMr unit sends a stray `;`). The REVO doc does not list QFLAG → may NAK.

| Letter | Setting | LCD program |
|---|---|---|
| a | Buzzer | 18 |
| b | Overload bypass | 23 |
| d | Solar feed to grid (PMAX, "reserved") | 43 |
| j | Power saving | – (no program in this manual) |
| k | LCD returns to the default page after 1 min | 19 |
| l | Data-log pop-up (P2015) | – |
| u | Overload restart | 06 |
| v | Over-temperature restart | 07 |
| x | Backlight | 20 |
| y | Alarm when the primary source is interrupted | 22 |
| z | Fault-code record | 25 |

### 7.11 `QALL` — Sorotec REVO all-in-one status

Doc template: `(BBB CC.C DDD EE.E FFFF GGG HH.H III JJJ KKK LLL MM.M NNNN OOOOOO PPPPPP Q KK SS<CHK><cr>` — **18 fields**. If it answers with a valid CHK, the unit is REVO and QALL becomes the primary 5 s poll. [confirmed doc]

| Idx | Format | Field | Unit | Logger key |
|---|---|---|---|---|
| 0 | BBB | Grid voltage | V | `gv` |
| 1 | CC.C | Grid frequency | Hz | `gf` |
| 2 | DDD | AC output voltage | V | `ov` |
| 3 | EE.E | AC output frequency | Hz | `of` |
| 4 | FFFF | AC output power (load) | W | `w` |
| 5 | GGG | Output load percent | % | `ld` |
| 6 | HH.H | Battery voltage | V | `bv` |
| 7 | III | Battery SOC | % | `soc` |
| 8 | JJJ | Battery charging current | A | `bci` |
| 9 | KKK | Battery discharge current | A | `bdi` |
| 10 | LLL | PV voltage | V | `pvv` |
| 11 | MM.M | PV current | A | `pvi` |
| 12 | NNNN | PV power | W | `pvw` |
| 13 | OOOOOO | PV energy today | Wh | `epvi` |
| 14 | PPPPPP | PV total energy | kWh | `etot` |
| 15 | Q | Mode: B inverter, L utility, S standby, F fault | letter | `md` |
| 16 | KK | Warning code (REVO table) | code | `wc` |
| 17 | SS | Fault code (REVO table) | code | `fc` |

Example from an older REVO II/VE II (mpp-solar discussion #78, [secondary]): `(223 49.9 223 49.9 1962 035 00.1 000 013 000 355 00.3 0106 018859 000056 L 00 00`.

**REVO warning codes** (Sorotec doc; **not** the LCD numbering):

| Code | Warning | Code | Warning |
|---|---|---|---|
| 00 | No warning | 21 | PV low voltage ("PV low pressure") |
| 01 | Low battery | 22 | PV overvoltage |
| 02 | Mains low voltage | 23 | PV overcurrent |
| 03 | Mains high voltage | 24 | PV over temperature |
| 04 | Overload | 25 | PV overload |
| 05 | Over temperature | 26 | PV boost failed |
| 06 | Fan lock | | |
| 07 | Battery overvoltage | | |

**REVO fault codes** (Sorotec doc):

| Code | Fault | Code | Fault |
|---|---|---|---|
| 01 | BUS overvoltage | 07 | Low battery voltage (too low at start-up; start-up not allowed) |
| 02 | Inverter overvoltage | 08 | Inverter failed |
| 03 | Inverter low voltage | 09 | BUS low voltage |
| 04 | BUS failure | 10 | Parallel failure |
| 05 | Overload fault | 11 | Over temperature fault |
| 06 | Output short circuit | 12 | Battery over-voltage fault (too high at start-up; cell count may be wrong) |

mpp-solar's `pi30revo.py` adds warning 08 "Discharge overcurrent", warning 39 "BMS Communication lost" and faults 13–24. Those are **not** in Sorotec's document; if they ever appear, store them raw with the `revo` tag. [confirmed, v1 C12–C13]

### 7.12 `PQSE` and `PQSA` — REVO configuration and power-flow reads

`PQSE` → `(BCDEFG HH.H II.I JJ.J KK.K LLL MMM NNNN OO PP QQ KK SSTUV WW.W<CHK>` (doc example `(000100 56.0 54.0 44.0 42.0 030 010 2018 06 01 20 00 00 00`). The first token packs single digits:

| Part | Field | Codes |
|---|---|---|
| B | Language | 0 English, 1 Chinese |
| C | Work mode (≈ program 01) | 0 utility priority, 1 solar priority, 2 battery priority, 3/4 PV+mains with/without external CT |
| D | Input range (≈ program 03) | 0 wide, 1 narrow |
| E | Output voltage | 0 = 220, 1 = 230, 2 = 240 V |
| F | Output frequency | 0 = 50, 1 = 60 Hz |
| G | Battery type | 0 lead-acid, 1 lithium, 2 no battery, 3 custom |
| HH.H / II.I | Bulk / float voltage | V |
| JJ.J / KK.K | Battery low-alarm / cut-off point | V |
| LLL / MMM | Total / AC charging current | A |
| NNNN OO PP QQ KK | Inverter clock (year, month, day, hour, minute) | – |
| S…V | Buzzer, grid, parallel flags, phase ID | – |
| WW.W | Battery full (recovery) voltage | V |

`PQSA` → `(BCDEFGHI<CHK>`: B = AC flow (0 none, 1 AC → converter, 2 converter → AC), C = PV (0 none, 1 PV → converter), D = battery (0 none, 1 charging, 2 discharging), E = load (0 no output, 1 output on), F–I reserved. Diagnostic only.

> ⚠️ `PSET` is the **write** twin of `PQSE` (same layout, including battery voltages and the clock). It is never sent.

### 7.13 `QLITH0` — lithium battery data (REVO)

`(BBB.B CCC.C DDD.D EEE FFF GGG HHH.H III.I JJJ.J KKK.K L M` = battery voltage, charge current, discharge current, temperature °C, SOC %, SOH, max charge current, max discharge current, max charge voltage, min discharge voltage, fault code, warning code (0 normal, 1 overvoltage, 2 low battery, 3 over temperature, 4 low temperature, 5 overcurrent, 6 output short circuit). Probed **once** at boot (CRC form first, then CHK). If it answers, the battery is reporting to the inverter over the BMS link, which also tells us whether `soc` is BMS-sourced. [confirmed doc; support on this unit unknown]

### 7.14 `Q1` — undocumented extended status

17 fields (27 on some firmware): time to end of absorb (s), time to end of float (s), SCC OK, AllowSccOn, charge average current, SCC PWM temperature, inverter temperature, battery temperature, transformer temperature, parallel mode, fan lock, unused, fan speed %, SCC charge power W, parallel warning, sync frequency, **charge stage** (10 none, 11 bulk, 12 absorb, 13 float). SCC power scaling differs between firmware versions. Polled every 60 s **only if supported**. [secondary — AEVA forum notes mirrored in mpp-solar]

### 7.15 `QDI` — factory defaults

Same style as QPIRI (output V/Hz, max AC charge A, under/float/bulk/re-charge V, max charge A, range, priorities, battery type, flag defaults, …). Read once during the probe for the diagnostic report; not scheduled.

### 7.16 Energy and clock queries

| Command | Reply | Unit |
|---|---|---|
| `QT` | `(YYYYMMDDHHMMSS` (the doc omits the CRC, but devices send one) | inverter clock |
| `QET` / `QLT` | `(NNNNNNNN` | total PV / load energy, **Wh** |
| `QEYyyyy` / `QEMyyyymm` / `QEDyyyymmdd` | `(NNNNNNNN` | PV energy per year / month / day, Wh |
| `QLYyyyy` / `QLMyyyymm` / `QLDyyyymmdd` | `(NNNNNNNN` | load energy per year / month / day, Wh |

Basic 24 V / 3.2 kW PI30 units usually NAK these (an EASun SP-3200 NAKs QET, QPIGS2, QALL and QMN [secondary]). Because of that the logger **integrates energy itself** (`epv`, `eld`, `ech`, `edis`, `egr`) and treats inverter-reported energy (`epvi`, `etot`) as an optional cross-check. If supported, `QT` tells which "day" the inverter uses.

---

## 8. Dialect classification

Applied in this order after QPI/QPIGS/QMOD succeed (d1 §3.6, V1 §6.5):

1. **`QALL` valid** — 18 fields and a correct CHK; try the CHK form `51 41 4C 4C 2B 0D` first, then the CRC form `51 41 4C 4C E8 FB 0D` → **`REVO`**. QALL becomes the 5 s poll; QPIGS field 12 = PV W; QPIGS 4 and 11 reserved; QPIGS 15 is "PV Wh today" (unreliable).
2. **QPIRI ≥ 28 fields, or QFLAG contains `d`** → **`MAX24`** (24-field QPIGS; QET/QED likely). (Solar2MQTT's heuristic.)
3. **QPIWS is 36 characters, or QMOD returned `E`/`C`/`Y`/`D`** → **`GK`**. The 24 V fault code sits in QPIWS a31–a32.
4. **Otherwise** → **`STOCK17`** or **`STOCK21`**, chosen by the QPIGS field count. Expect no energy counters.
5. **Field-12 rule, whatever the dialect.** The manual's max PV input current is 15 A, so:
   - if QPIGS[12] > 15 while PV V > 30 → field 12 is **power**;
   - if QPIGS[12] × QPIGS[13] ≈ QPIGS[19] (± 20 %) → it is **current**.
   Persist the result as `pv12 = W | A`.

Every optional command (`QPIWS`, `QFLAG`, `QET`, `QPIGS2`, `Q1`, `QMN`, `QLITH0`, `QT`) is tried once; NAK or timeout marks it unsupported in an NVS bitmap; unsupported commands are retried once a day.

A real Axpert MAX II 8 kW still returns 21 QPIGS fields, so the field count alone never decides MAX vs stock [confirmed capture]. Store the raw replies, the dialect tag, `pv12`, and the supported-command bitmap.

---

## 9. Probe decision tree

**Prerequisites (hardware, before the ESP32 ever transmits):** module self-ID on the bench (M1) and the receive-only idle-level test that identifies the inverter-TX pin (M2). See [03-hardware-and-wiring.md](03-hardware-and-wiring.md) and [07-milestones-and-test-plan.md](07-milestones-and-test-plan.md).

### 9.1 Flow

```
A  Inspect (no tools): labels, LCD U1/U2, programs 01/02/03/05/11/12/13/16/29
B  Bench: ESP UART loopback GPIO17<->GPIO18 (inverted and non-inverted), module RO/DI self-ID
C  Idle-level test (receive-only, A+ on candidate pin, B- on pin 8):
     pin 1 >=99% LOW, ~0 edges ? --yes--> inverter TX = pin 1, RX = pin 2
        |no
     pin 2 >=99% LOW ?           --yes--> inverter TX = pin 2, RX = pin 1
        |no
     -> F1 (TTL-level port or wiring fault): STOP, see 03-hardware-and-wiring
C2 Upper check of the RX candidate (receive-only, A+ on it, B- on ESP 3V3, pin 8 on ESP GND):
     >=99% LOW twice --> below ~3.1 V, safe for GPIO17     HIGH --> positive pin (pin 4?): STOP
   Connect GPIO17 -> inverter RX pin ONLY NOW; arming runs a pre-arm check (GPIO17 LOW with pull-down, RO LOW).
D  For profile P1..P5 (2400/9600, inversion variants), each after 3 s silent listening:
     unsolicited valid frames heard?  -> FOREIGN_MASTER: stop transmitting, raw capture
     QPI x3  -> "(PI30" / "(PI41" / "(PI17" / "(NAK" -> PI30 family -> E
             -> "#-1"                             -> legacy Q1/Q6 adapter
             -> silence/garbage                   -> next profile
     ^P005PI -> "^D00518"  -> PI18 detected: raw capture + report
     Q1 / Q6 (no CRC) -> "(" + 8 / ~19 fields -> legacy Q1/Q6 adapter
   Modbus last resort (FC 03 only, 3 s listen first): PowMr 2400 slave 5; SMG 9600 slave 1
   nothing -> RawCapture (listen-only), UI "no protocol found", re-probe every 6 h
E  PI30 family: QPIGS (x3, plausibility) -> QMOD -> QPIRI (identity) -> QVFW/QVFW2
   -> QPIWS -> QALL (CHK, then CRC) -> optional commands -> classify (section 8)
   -> persist {profile, TX pin, adapter, dialect, pv12, supported bitmap} in NVS -> RUN
```

### 9.2 Probe steps and acceptance checks

| Step | Send | Valid reply | Decision |
|---|---|---|---|
| D1 | `QPI` `51 50 49 BE AC 0D` | `28 50 49 33 30 9A 0B 0D` = `(PI30`; or `(PI41`/`(PI17`; or `(NAK` (with or without CRC) | PI30 family → D2. Silence → repeat 3× (the first command after idle can time out). Garbage → try the next inversion profile. |
| D2 | `QPIGS` `… B7 A9 0D` | `(` + 17 / 21 / 24 fields | Output V within **±2 V** and battery V within **±0.3 V** of the LCD |
| D3 | `QMOD` `… 49 C1 0D` | `(L` `06 07 0D`, `(B` `E7 C9 0D`, `(S`, `(F`, `(H`, `(P` | Must agree with the LEDs (AC/INV solid = L, flashing = B) |
| D4 | `QPIRI` `… F8 54 0D` | 25 / 26–27 / 28 fields | **Identity:** field 5 = `3200`, field 7 = `24.0`, field 4 ≈ `13.9` |
| D5 | `QVFW` `… 62 99 0D`, `QVFW2` `… C3 F5 0D` | `(VERFW:NNNNN.NN`, `(VERFW2:NNNNN.NN` | Must equal LCD U1 / U2 |
| D6 | `QPIWS` `… B4 DA 0D` | 32 / 36 / 38 chars of 0/1, or NAK | NAK allowed |
| D7 | `QALL` CHK `51 41 4C 4C 2B 0D`, then CRC `51 41 4C 4C E8 FB 0D` | `(` + 18 fields + CHK + CR | Valid → REVO. NAK → not REVO. |
| D8 | QFLAG, QDI, QID, QSID, QMN, QGMN, QET, QT, QPIGS2, Q1, QLITH0 (CRC then CHK) | data or NAK | Mark supported / unsupported |
| D9 | repeat D1–D3 at 9600 (profiles P4/P5) | as above | only if all 2400 profiles failed |

**Plausibility gates** applied to every QPIGS/QALL reply before it is accepted (from the manual's specs, d1 §3.5): battery 18.0–34.0 V; output V = 0 or 180–260; output Hz = 0 or 45–65; SOC 0–100; PV V 0–520; load 0–250 %; QPIGS field count ∈ {17, 21, 24} (or ≥ 17).

### 9.3 Fallback branches (condensed from v1 §6.6)

| Branch | Symptom | Action |
|---|---|---|
| F1 | Neither pin idles LOW in the idle test | Re-check the cable colour mapping; diagnostic with B− on 3V3; if the logic level cannot be established, **stop** (never connect a GPIO to an unknown-voltage pin) |
| F2 | TX pin found, but D1–D9 get silence | Check GPIO17 lands on the *other* pin; try TX non-inverted (P3); try `^P005PI`; try `Q1`/`Q6` without CRC |
| F3 | ASCII completely dead | Modbus last-resort probes, FC 03 only (§13) |
| F4 | COM behaves like RS485 (contradicts manual — extremely unlikely) | **Not supported by firmware 0.6.x** (no RS485 mode can be selected); do not wire it, escalate to F5 ([03](03-hardware-and-wiring.md) §9.3). Original plan, kept for a future firmware: A+ → pin 2, B− → pin 1, module DI = UART TX, non-inverted; swap A/B once |
| F5 | Everything fails | Stop transmitting; fall back to asking IPS Bazar for the Solar of Things collector or the protocol document, or a MAX3232 if purchases ever become possible |

### 9.4 After the probe (RUN)

12 consecutive failed fast polls (≈ 60 s) → `LINK_LOST` event. 60 failures (≈ 5 min) → background re-probe (read-only) with back-off 1 min, 5 min, 15 min, 1 h. On boot with a stored profile, a light probe (QPI or QALL + QPIGS × 3 with plausibility) skips the full tree.

---

## 10. Timing rules and command schedule

**Rules** (d1 §3.7; constants from ESPHome, Solar2MQTT, mpp-solar, ned-kelly) [confirmed constants, inferred choice]:

1. One command in flight at a time.
2. Flush RX before sending; ignore bytes received during your own transmission.
3. **1.5 s** timeout for the first reply byte.
4. **200 ms** of silence after the last byte = end of frame.
5. At least **150 ms** gap between commands.
6. Receive buffer 256 bytes.
7. Accept CR or LF as the terminator; require `(` as the first byte.
8. CRC/CHK-check every reply; a corrupted frame is counted and dropped, never parsed.

**Schedule:**

| Period | STOCK / MAX / GK | REVO |
|---|---|---|
| 5 s (fast) | `QPIGS` | `QALL` |
| 10 s | `QMOD`, then `QPIWS` (alternating, if supported) | `QMOD` (optional; QALL carries the mode), `QPIWS` if supported |
| 30 s | – | `QPIGS` (bus V, status bits) |
| 60 s | `Q1` (if supported) | same |
| 10 min, **or immediately when QPIGS b6 = 1** | `QPIRI`, `QFLAG` | `QPIRI`, `PQSE` |
| 5 min (if supported) | `QT`, `QET`, `QEDyyyymmdd`, `QLDyyyymmdd` | – (QALL has Wh today / kWh total) |
| Boot + hourly | `QPI`, `QID`/`QSID`, `QVFW`, `QVFW2`, `QMN`, `QGMN` | same, plus `QLITH0` once |

Wire-time budget: a 5 s cycle uses ≈ 0.5 s for QPIGS plus ≈ 0.2 s for QMOD/QPIWS — the link is idle > 80 % of the time.

---

## 11. Read-only safety: what we send and what we never send

The logger is **read-only towards the inverter**. Three independent layers (d1 §3.10):

1. **Compile time:** `INV_ALLOW_WRITES=0` in `config.h`; setter code is not compiled at all.
2. **Link layer:** `SerialLink::query()` is the only code that can transmit, and it refuses anything that is not an **exact match** in the whitelist. A refused command returns `LinkResult::REFUSED` and **zero bytes** are written.
3. **Host unit test:** asserts that every refused command (e.g. `POP02`) returns `REFUSED` with nothing transmitted.

### 11.1 Whitelist (the only commands that can ever leave the ESP32)

| Group | Commands |
|---|---|
| PI30 inquiries | `QPI` `QID` `QSID` `QVFW` `QVFW2` `QVFW3` `QMN` `QGMN` `QPIRI` `QPIGS` `QPIGS2` `QMOD` `QPIWS` `QFLAG` `QDI` `Q1` `QT` `QET` |
| Date queries (digits and ranges checked) | `QEDyyyymmdd` `QLDyyyymmdd` |
| Sorotec REVO reads (exact match) | `QALL` `PQSE` `PQSA` `QLITH0` |
| Detect-only probes | `^P005PI`; legacy `Q1\r` / `Q6\r` |
| Modbus | **FC 03 (read holding registers) only**, fixed probe frames (§13) |

Since the safety review (firmware 0.6.1, [03](03-hardware-and-wiring.md) appendix A) the whitelist is **exactly the set of commands the firmware sends**. `QMCHGCR`, `QMUCHGCR`, `QBEQI`, `QLT`, `QEMyyyymm` and `QEYyyyy` are read-only queries too, but they were unused. Some of them are one dropped byte or one `Q`→`P` bit away from setters (`MCHGC…`, `MUCHGC…`, `PBEQ…`, `PE…`), so they were removed.

### 11.2 Never sent (explicitly refused) — and why

| Command(s) | What it does | Why it is dangerous |
|---|---|---|
| `PF` | "All device parameters set to default value" | **Factory reset** — loses the lithium configuration |
| `PBT` | Set battery type | Wrong type can **over- or under-charge the lithium battery** |
| `PSDV`, `PCVV`, `PBFT`, `PBCV`, `PBDV` | Cut-off, bulk, float, back-to-grid, back-to-battery voltages | **Battery damage** |
| `POP` | Output source priority | Changes when the battery is cycled |
| `PCP` | Charger source priority | Can stop grid charging |
| `MCHGC`, `MNCHGC`, `MUCHGC`, `MSCHGC` | Max charging currents | Over-current on the battery |
| `F50` / `F60`, `V230` (etc.) | Output frequency / voltage | **Can damage connected loads** |
| `PGR` | Grid input range | Changes transfer behaviour |
| `PE…` / `PD…` | Enable/disable flags (buzzer, bypass, restarts, …) | Behaviour change |
| `RTEY`, `RTDL` | Erase PV/load energy counters / data log | **Data loss** |
| `PBATCD` | Enable/disable charger/discharger | Can **shut the unit down** |
| `PSET` (REVO) | Writes the full configuration (battery voltages, currents, clock) | All of the above at once |
| `~R`, `~S` (REVO) | Internal PV calibration read/**set** | Corrupts calibration |
| `DAT`, `BTA±nn`, `PSAVE`, `ATE1`/`ATE0`, `PLED…`, `PBATMAXDISC`, `PBEQ…`, `PCVT`, `PCST` | Clock, calibration, misc. setters | Not needed; refused |
| PI18 `^S…` | PI18 setters | Not needed; refused |
| Modbus FC 05/06/0F/10, "8851" command 0x0010 | Modbus writes | Not needed; refused |

`PQSE` and `PQSA` are allowed only because they are exact-match reads; anything else starting with `P` is refused. Sources: P2014 §3, PMAX §3, REVO doc §1.6 and §2, mpp-solar `SETTER_COMMANDS`; ESPHome notes that it does not expose all switches "as they lead to the possibility to make serious damage". [confirmed]

---

## 12. Normalized data model mapping

The firmware decodes every dialect into one `Sample` with short keys and SI units; the same keys are used in the local JSON API and the cloud `live` node (d1 §3.8; storage design in [04-architecture-and-data-model.md](04-architecture-and-data-model.md)). Missing values are omitted.

| Key | Unit | Meaning | STOCK17 | STOCK21 / GK | MAX24 | REVO |
|---|---|---|---|---|---|---|
| `ts` | s (UTC) | Sample time | device clock (NTP) | ← | ← | ← |
| `lk` | 0/1 | Valid inverter reply within 15 s | link layer | ← | ← | ← |
| `gv` | V | Grid voltage | QPIGS[0] | ← | ← | QALL[0] |
| `gf` | Hz | Grid frequency | QPIGS[1] | ← | ← | QALL[1] |
| `ov` | V | Output voltage | QPIGS[2] | ← | ← | QALL[2] |
| `of` | Hz | Output frequency | QPIGS[3] | ← | ← | QALL[3] |
| `va` | VA | Output apparent power | QPIGS[4] | ← | ← | – (reserved) |
| `w` | W | Output active power (load) | QPIGS[5] | ← | ← | QALL[4] |
| `ld` | % | Load percent | QPIGS[6] | ← | ← | QALL[5] |
| `bus` | V | DC bus voltage | QPIGS[7] | ← | ← | QPIGS[7] (30 s) |
| `bv` | V | Battery voltage | QPIGS[8] | ← | ← | QALL[6] |
| `bci` | A | Battery charging current | QPIGS[9] | ← | ← | QALL[8] |
| `bdi` | A | Battery discharge current | QPIGS[15] | ← | ← | QALL[9] (**not** QPIGS[15]) |
| `bi` | A | Net battery current (+ = charging) | `bci − bdi` | ← | ← | ← |
| `bw` | W | Battery power (+ = charging) | `bv × bi` | ← | ← | ← |
| `soc` | % | Battery capacity | QPIGS[10] (voltage-derived unless a BMS protocol is active) | ← | ← | QALL[7] |
| `tmp` | °C | Heat-sink temperature | QPIGS[11] (flag as raw ADC if > 150) | ← | ← | – (reserved) |
| `pvi` | A | PV current | QPIGS[12] if `pv12 = A` | ← | ← | QALL[11] |
| `pvv` | V | PV voltage | QPIGS[13] | ← | ← | QALL[10] |
| `pvw` | W | PV power (stock: "PV charging power" = charger output) | `pvv × pvi` | QPIGS[19] | QPIGS[19] | QALL[12] (QPIGS[12] as cross-check) |
| `sccv` | V | Battery voltage seen by the SCC | QPIGS[14] | ← | ← | – (d1 does not map it; REVO QPIGS[14] carries it at the 30 s poll) |
| `md` | letter | Mode (P S L B F H D C Y E) | QMOD | ← | ← | QALL[15] |
| `st` | 8 chars | Status bits b7…b0 | QPIGS[16] | ← | ← | QPIGS[16] |
| `st2` | 3 chars | Status bits b10 b9 b8 | – | QPIGS[20] | QPIGS[20] | QPIGS[20] (doc: reserved) |
| `wf` | 32/36/38 chars | QPIWS bit string | QPIWS | QPIWS (GK: 36) | QPIWS (36) | QPIWS if supported |
| `wc` | code | Warning code (REVO table) | – | – | – | QALL[16] |
| `fc` | code | Fault code (tagged with its table) | – (not in 32-char QPIWS) | GK: QPIWS a31–a32 | QPIWS a31–a32 (24 V) | QALL[17] |
| `epv` | Wh | PV energy today, **integrated on the device** | device | ← | ← | ← |
| `eld` | Wh | Load energy today, integrated | device | ← | ← | ← |
| `ech` / `edis` | Wh | Battery charge / discharge energy today, integrated | device | ← | ← | ← |
| `egr` | Wh | Grid energy today (**estimate**) | device | ← | ← | ← |
| `epvi` | Wh | PV energy today **reported by the inverter** | – | – | `QEDyyyymmdd` (if supported) | QALL[13] |
| `etot` | kWh | Lifetime PV energy reported by the inverter | – | – | `QET` / 1000 (if supported) | QALL[14] |
| `q` | count | Minute records waiting for upload | queue | ← | ← | ← |

"←" = same source as the column to its left. Identity and ratings (`QPI`, `QID`/`QSID`, `QVFW`, `QVFW2`, `QMN`, and QPIRI fields 4/5/6/7/12/13/14/15/16/17) go to the cloud `meta` node, not into `Sample`.

---

## 13. Modbus families (detect-only fallback)

The manual and silkscreen point to PI30 over RS232, and the only RS485 port (BMS) is occupied with the inverter as master. Modbus RTU families seen on look-alike Chinese all-in-ones are therefore **last-resort probes** on the same RS232-lite wiring, each preceded by 3 s of listening, **FC 03 only** (d1 §3.4, v1 §6.6 F3). Full register maps: [r3-modbus-families.md](../research/r3-modbus-families.md).

| Family | Serial | Slave | Probe frame (CRC low byte first) | Expected reply | Status in v1 firmware |
|---|---|---|---|---|---|
| PowMr HVM "WIFI-VM" (exposes Modbus **and** PI30 on one 2400 UART) | 2400 8N1 | 5 | `05 03 11 95 00 2D 91 43` | `05 03 5A` + 90 bytes + CRC (bytes swapped in each word) | Detect + basic decode |
| SMG / "Sumry" (EASUN SMG-II, Anenji, …) | 9600 8N1 | 1 | `01 03 00 C9 00 01 54 34` (register 201 = work mode) | `01 03 02 00 0m CRC`, m = 0–6; **reject all-zero data** | Detect + basic decode |
| SRNE | 9600 8N1 | 1 (0xFF universal) | `01 03 00 0B 00 01 F5 C8` | – | Not implemented (RS485 family) |
| MUST PV18/PH18 | 19200 8N1 | 4 | `04 03 4E 20 00 02 D2 BC` | – | Not implemented (RS485 family) |
| "8851" binary (PowMr 4.5K/6.5K, Sumry HGX) | 9600 | – | `88 51 00 03 00 00 00 00 4D 08` | – | Not implemented |

CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF, low byte first) reproduces every example frame in the SMG and SRNE documents (v1 V12). If none of the above answers, the firmware falls back to `RawCaptureAdapter` (listen-only) and reports "no protocol found".

---

## 14. Review of the Gemini-provided mapping

The user received the following from Gemini without sources ([gemini-claims.md](../research/gemini-claims.md)). Each claim is checked against primary sources (v1 C9–C11).

### 14.1 "RS232 port (standard RJ45): Pin 2 Inverter TX, Pin 3 Inverter RX, Pin 5 GND"

**Wrong for this unit.** TX on 2, RX on 3, GND on 5 is the **DB9** serial convention. This unit's COM jack is an RJ45 whose manual and chassis silkscreen say **1 RXD, 2 TXD, 4 +VCC, 8 GND**. Voltronic's own RJ45-to-DB9 cable drawing maps RJ45 pin 1 → DB9 pin 2, pin 2 → DB9 pin 3, pin 8 → DB9 pin 5 — Gemini apparently quoted the DB9 end of that cable. Following it would put a signal on RJ45 pin 3 (unassigned) and ground on pin 5 (unknown), and would miss pin 8. Correct procedure: only pins 1, 2, 8; which of 1/2 is the inverter's TX is measured by the idle-level test (prior: pin 1). Sources: photo `images/ports-label-zoom.jpg`; manual §2.3; P2014 cable drawing.

### 14.2 "RS485 interface (optional expansion card): Pin 1 A(+), Pin 2 B(−)"

**Wrong, and not usable anyway.** There is no expansion card. The only RS485 on this unit is the **BMS** jack, whose silkscreen and manual say **pin 1 = 485-B, pin 2 = 485-A** — the reverse of the claim. That jack is the lithium battery link with the inverter as the master; it is never used for logging. Sources: photo; manual §2.3.

### 14.3 QPIGS field list (1-based #1–#15)

Gemini's list numbers fields from 1; this table converts to the 0-based indices used everywhere else on this page.

| Gemini # | Gemini says | 0-based | Stock PI30 (P2014/P2015) | Sorotec REVO | Verdict |
|---|---|---|---|---|---|
| 1 | Grid voltage V | 0 | Grid voltage | same | ✔ |
| 2 | Grid frequency Hz | 1 | Grid frequency | same | ✔ |
| 3 | Output voltage V | 2 | AC output voltage | same | ✔ |
| 4 | Output frequency Hz | 3 | AC output frequency | same | ✔ |
| 5 | Output apparent power VA | 4 | Apparent power | **Reserved** | ✔ stock / ✘ REVO |
| 6 | Output active power W | 5 | Active power | same | ✔ |
| 7 | Output load % | 6 | Load percent | same | ✔ |
| 8 | BUS voltage V | 7 | DC bus voltage | same | ✔ |
| 9 | Battery voltage V | 8 | Battery voltage | same | ✔ |
| 10 | Battery charging current A | 9 | Charging current (integer A) | same | ✔ |
| 11 | Battery capacity % | 10 | Battery capacity (voltage-derived unless BMS data) | same | ✔ |
| 12 | Heat sink temp °C | 11 | Heat-sink temperature — **may be a raw NTC A/D value** on 1–3 kVA units | **Reserved** | ✔ with caveat / ✘ REVO |
| 13 | PV input current A | 12 | PV input current | **PV power, W** | ✔ stock / ✘ REVO |
| 14 | PV input voltage V | 13 | PV input voltage | same | ✔ |
| 15 | **Battery discharge current A** | 14 | **Battery voltage from SCC** (V) | same | **✘ wrong** |
| – | (missing) | 15 | **Battery discharge current** (A) | **PV energy today, Wh** | omitted |
| – | (missing) | 16 | Device status bits b7…b0 | same | omitted |
| – | (missing) | 17–20 | Fan offset, EEPROM version, **PV charging power W**, status bits b10–b8 (21-field firmware) | reserved / PV power | omitted |

**Verdict:** fields 1–14 are right for stock PI30; #15 is wrong for **stock PI30 as well** (not only for REVO), the list stops before the discharge current and status bits, and on REVO firmware three more fields change meaning. Source: P2014 §2.7, P2015 §2.9, Sorotec REVO doc §1.2.

### 14.4 Modbus RTU map ("30001 PV1 Power W ×1 … 30023 Battery discharge A ×0.1", FC 0x03 or 0x04)

**Discard entirely.** No source exists for this map, and it does not appear in any primary SMG, SRNE, MUST or PowMr register map (r3). The "3xxxx" numbering is the input-register convention (FC 04), which contradicts "FC 0x03 or 0x04". Most importantly, this unit's COM port is expected to speak **PI30 ASCII**, not Modbus; Modbus is only a detect-only last resort (§13). Sources: v1 C11; r3 primary maps.

### 14.5 "Serial: 2400 or 9600 baud, 8N1 — 2400 is standard for Axpert/F4 lineages"

**Mostly right.** 2400 8N1 is what Voltronic's PI30 documents and Sorotec's REVO document specify, and it is the logger's first profile. 9600 is only a fallback probe based on secondary reports. "F4 lineage" is not a known term — "F4" is IPS Bazar's model series name, not a protocol family. Sources: P2014 §1; REVO doc header.

---

## 15. Sources

**Primary protocol documents** (mirrored in the mpp-solar repository):

- Voltronic PI30 HS/MS/MSX RS232 protocol (2014-08-22): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf
- Voltronic PI30 communication protocol (2015-09-24, customer): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Communication-Protocol-20150924-Customer.pdf
- Voltronic PI30 PIP-GK/MK protocol: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_PIP-GK_MK-Protocol.pdf
- Voltronic PI30MAX communication protocol (2021-02-17): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30MAX.Communication.Protocol20210217.pdf
- Sorotec "Revo RS232 Communication Protocol" V02/V03 (2020-11-12): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf
- PI18 (InfiniSolar V) protocol: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI18_InfiniSolar-V-protocol-20170926.pdf
- PI30 Q1 field notes: https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Q1%20definition.md

**Source code:**

- mpp-solar `pi30.py`, `pi30revo.py`, `protocol_helpers.py` (`crcPI`, CHK): https://github.com/jblance/mpp-solar/tree/master/mppsolar/protocols
- ESPHome `pipsolar` (CRC, QPIGS parsing): https://github.com/esphome/esphome/tree/dev/esphome/components/pipsolar · docs https://esphome.io/components/pipsolar/
- syssi/esphome-pipsolar (probe YAML, captured PDUs, RJ45 pinout README): https://github.com/syssi/esphome-pipsolar
- ned-kelly `inverter.cpp` (`cal_crc_half`): https://github.com/ned-kelly/docker-voltronic-homeassistant
- Solar2MQTT (`PI_Serial.cpp`: QALL CRC/CHK handling, dialect heuristics; licence CC BY-NC-ND — reference only, nothing copied): https://github.com/softwarecrash/Solar2MQTT
- arduino-esp32 `HardwareSerial.h` (UART inversion API): https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/HardwareSerial.h

**Datasheets:**

- TI MAX3232 (RS232 receiver thresholds, input impedance): https://www.ti.com/lit/ds/symlink/max3232.pdf
- ESP32-S3-WROOM-1: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf

**Secondary:**

- mpp-solar discussion #78 (REVO VE II traces at 2400 baud): https://github.com/jblance/mpp-solar/discussions/78
- DIY Solar Forum thread 34404 (REVO VM II PRO reads over RS232; writes NAK): https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/

**Modbus maps:** see [r3-modbus-families.md](../research/r3-modbus-families.md) (syssi/esphome-smg-ii, SRNE V1.96, MUST 1.4.15, odya/esphome-powmr-hybrid-inverter, leodesigner/powmr_comm).

**This unit:** paper manual scan `reference/Savx-F4-user-manual.pdf` and photos in `images/`. Full project source list: [09-sources.md](09-sources.md).
