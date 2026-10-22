# R10: Can this REVO-family inverter's settings be changed remotely, and how safely?

- **Unit:** Savx F4 3KW (24 V), identified by the logger as a Sorotec REVO VM II PRO-family unit. It speaks Voltronic PI30 in the REVO dialect. Firmware `VERFW:00072.40`, `VERFW2:00000.00`, QPI `PI30`, QID `5535535553555`.
- **Date:** 2026-10-22.
- **Status:** research only. No firmware, web code or device setting was changed. **The inverter and the logger (<logger-ip>) were not contacted.** Every fact about this unit comes from replies captured earlier on 2026-10-22: docs/probe/2026-10-22-first-contact.txt and the session serial log (`scratchpad/serial-daemon.log`). All CRC/CHK values in this file were recomputed with `scratchpad/r10-scripts/frames.py` (PI30 CRC-16/XMODEM with the Voltronic adjust; REVO CHK = byte sum + 1).
- **Evidence tags:**

  | Tag | Meaning |
  |---|---|
  | **[CONFIRMED-doc]** | Stated in a primary protocol document or manual |
  | **[CONFIRMED-unit]** | Our own capture from this inverter (CRC/CHK verified) |
  | **[CONFIRMED-multi]** | Two or more independent first-hand reports with logs, or a vendor's own investigation |
  | **[SECONDARY]** | A single first-hand report, or a second-hand statement |
  | **[INFERRED]** | Our reasoning, not directly observed |

---

## 0. Executive summary

1. **Verdict: partially feasible at best, and unproven on this unit.** No source shows a REVO or VM II PRO unit on firmware 72.xx accepting any setting over RS232.
2. **Sorotec's REVO protocol has no per-setting commands, only the all-in-one `PSET`** [CONFIRMED-doc]. On REVO-E, REVO II and EASUN SMR II units, the stock PI30 setters (`PDx`, `POP00`, …) are answered with `(NAK` [CONFIRMED-multi, byte logs]. SolarAssistant states that the firmware on these units does not support setting changes through the ports [CONFIRMED-multi, vendor investigation].
3. **`PSET` does work on several REVO II and SMR II units**, but only with CHK = byte sum + 1 and **no trailing CR**. The replies are `ACK D0 0D` and `NAK DB 0D`, with no `(` [CONFIRMED-multi: 4 users, plus mpp-solar code].
4. **This unit sent no reply to `PQSE`/`PQSA`** (the read twin of PSET) during its probes [INFERRED from the reply table and the code]. So PSET is probably missing here. It is also high-risk: one frame rewrites the battery type, battery voltages, output V/Hz and the clock.
5. **The firmware is a hybrid.** It answers the stock PI30 `QFLAG` and `QDI` as well as the REVO `QALL` and `QLITH0` [CONFIRMED-unit]. So the stock flag setters (`PEx`/`PDx`) are not ruled out for 72.40 [INFERRED].
6. **On this firmware, the QPIRI fields 15–24, all of QDI and the QPIGS status bits are fixed values** [CONFIRMED-unit + doc + a second 72.40 unit]. QPIRI therefore cannot read back the output or charger priority. The values 11.0 / 10.5 / 14.2 / 14.2 look like per-12 V values of 22.0 / 21.0 / 28.4 / 28.4 V [INFERRED].
7. **Useful remote settings:** programs 01, 16, 11, 02, 18 and 20. **Never remote:** battery type, battery voltages and SOC points, output V/Hz, equalisation, `PF`, and `PSET` by default.
8. **Electrically, a write travels the same path that already answers 100 % of queries.** No hardware change is needed. The only unknown is whether the inverter checks the CRC of incoming frames [INFERRED].
9. **Recommended next step (zero writes, existing tools):**
   - run the logger's probe and confirm that `PQSE` and `PQSA` time out in `/api/probe`;
   - then toggle **LCD program 20 (backlight) LON → LOF → LON at the inverter**, running a probe after each change, to see whether QFLAG letter `x` follows the LCD.
10. **Only if QFLAG follows the LCD:** send one same-value `PEa` (`50 45 61 D0 70 0D`) from a one-shot test build, with the owner at the LCD.
    - `(NAK` ends the stock-setter path.
    - `(ACK` leads to the backlight toggle test (§9).

---

## Contents

1. [What this unit already told us (own captures)](#1-what-this-unit-already-told-us-own-captures)
2. [Q1: which SET commands the REVO dialect accepts](#2-q1-which-set-commands-the-revo-dialect-accepts)
3. [Q2: real-world reports](#3-q2-real-world-reports)
4. [Q3: OEM and PC software](#4-q3-oem-and-pc-software)
5. [Command table](#5-command-table)
6. [Q4: which settings are worth controlling remotely](#6-q4-which-settings-are-worth-controlling-remotely)
7. [Q5: safety design](#7-q5-safety-design)
8. [Q6: electrical check of the TX path](#8-q6-electrical-check-of-the-tx-path)
9. [Minimal on-site test plan](#9-minimal-on-site-test-plan)
10. [What must change in docs/10](#10-what-must-change-in-docs10)
11. [Open questions](#11-open-questions)
12. [Sources](#12-sources)

---

## 1. What this unit already told us (own captures)

All replies below are CRC/CHK-valid. The checks were recomputed today: QPIRI CRC `41 3E` ✓, QFLAG `2F 29` ✓, QID `D9 3E` ✓, QDI `6C 88` ✓, QALL CHK `54` ('T') = sum + 1 over `(` + payload ✓. [CONFIRMED-unit]

| Command | Reply payload (2026-10-22) | What it tells us |
|---|---|---|
| `QPI` | `PI30` | PI30 family |
| `QVFW` / `QVFW2` | `VERFW:00072.40` / `VERFW2:00000.00` | The **same main-CPU string** appears on a Yangtze **VMII5K48** (48 V, 5.5 kW VM II clone) in 2022. An experienced Voltronic firmware patcher read 72.40 as an off-grid VM II firmware line ([AEVA t=7440](http://web.archive.org/web/20220327033220/http://forums.aeva.asn.au/viewtopic.php?t=7440)) [SECONDARY] |
| `QFLAG` | `EakxyDbjuvz` | Answers, although the REVO doc does not define QFLAG [CONFIRMED-unit]. Enabled: a k x y. Disabled: b j u v z. This equals the manual's factory defaults **except `z`**: the manual default for program 25 is FEn, i.e. `z` enabled. **Whether QFLAG is live or a fixed string is unknown** (§9 Phase 0) |
| `QDI` | `230.0 50.0 0000 42.0 54.0 56.4 46.0 60 0 0 2 0 0 0 0 0 0 1 0 0 1 0 54 0 0 1` | **Fixed:** these are 48 V default voltages on a 24 V unit. Never use QDI for anything [CONFIRMED-unit] |
| `QPIRI` | `230.0 20.0 230.0 50.0 20.0 3600 3600 24.0 11.0 10.5 14.2 14.2 2 60 06P 1 0 1 6 01 0 0 52.0 0 1` | See the field analysis below |
| `QPIGS` | `… 26.50 000 100 0026 0000 000.0 26.50 00000 00010110 00 00 00000 010` | **The status bits `00010110` never change**, whether the battery is charging (26 A or 10 A), idle or discharging (5–6 A), across all our captures. The same string appears on a REVO II unit in 2021 ([mpp-solar #78](https://github.com/jblance/mpp-solar/discussions/78)). The charge-current field (`000`) and the temperature (`0026`, "reserved" in the doc) are also fixed [CONFIRMED-unit]. **Bit b6 ("configuration changed") cannot signal a settings change on this unit** |
| `QALL` | `241 49.8 241 49.8 0241 007 26.5 100 000 000 000 00.1 0000 000000 000000 L 02 00` | REVO live data, CHK framing. It carries no settings |
| `QLITH0` | `026.5 000.0 000.9 310 100 000 100.0 010.0 028.4 002.2 0 0` (PI30 CRC, not CHK) | The BMS reports a maximum charge voltage of **28.4 V** |
| `QMN`, `QGMN`, `QET`, `QT`, `Q1` | `(NAKss` = `28 4E 41 4B 73 73 0D` | **NAK format on this unit = the stock PI30 one** [CONFIRMED-unit]. `QT` NAK: the inverter has no clock readable over PI30 |
| `PQSE`, `PQSA` | **no entry in the last-reply table** | See below |
| `QPIWS` | answered, but **once with NAK** under 2 s polling (site survey, 0.7.2 run) | **NAKs can be transient on this unit** [CONFIRMED-unit] |

### 1.1 Why we believe PQSE and PQSA got no reply

- `Pi30Adapter::probe` sends `PQSE` and then `PQSA` (CHK framing) right after it classifies the unit as REVO (`Pi30Adapter.cpp` lines ≈ 300–303).
- `RawRing::add` stores a "last reply" only when at least one byte was received (`RawRing.h`, `setLast` only if `rxLen > 0`). NAKs therefore are stored, as QMN and QT show.
- The captured `raw` table lists 19 commands, including every NAK, but neither PQSE nor PQSA.
- **Conclusion:** both most likely timed out [INFERRED, high confidence]. The probe log (`GET /api/probe`, lines `PQSE -> …`) and `sup` in `/api/status` will confirm it (§9, step 0.2).
- PQSE has **not** been tried with the PI30 CRC framing, even though this firmware answers `QLITH0` with a CRC where Sorotec's doc writes CHK.

### 1.2 QPIRI field analysis

The REVO doc (V02 §1.1) lists the QPIRI fields with example or "Reserved" values [CONFIRMED-doc]. In the table below, "REVO doc value" is the doc's example or "Reserved" value, and the Yangtze column is the 48 V 72.40 unit from AEVA.

| # | Ours | Stock PI30 meaning | REVO doc meaning (doc value) | Yangtze 72.40 | Verdict |
|---|---|---|---|---|---|
| 8 | 11.0 | Re-charge (back-to-utility, prog 12) | Low-voltage **alarm** point (44.0 on 48 V) | 44.0 | Probably **per 12 V** (× 2 = 22.0 V = the manual's low-DC warning) [INFERRED] |
| 9 | 10.5 | Under-voltage (prog 29) | Low-voltage **shutdown** (40.0 on 48 V) | 42.0 | × 2 = 21.0 V = the prog 29 default [INFERRED] |
| 10 | 14.2 | Bulk (prog 26) | "Buck Vol" (bulk / equalisation) | 56.0 | × 2 = 28.4 V = the QLITH0 BMS maximum [INFERRED] |
| 11 | 14.2 | Float (prog 27) | Float | 54.0 | × 2 = 28.4 V [INFERRED] |
| 12 | 2 | Battery type (0 AGM, 1 FLD, 2 USE) | Same codes | 2 | Live or fixed: unknown. The LCD probably shows LIb |
| 13 | 60 | Max AC charging current (prog 11) | "Max charging current it can be configured" (60) | 60 | Possibly a capability constant, or prog 02 (default 60 A). **Unknown** |
| 14 | `06P` | Max charging current (prog 02) | "Reserved 06" | (parsed as 0) | **Fixed** (not even numeric) |
| 15–24 | `1 0 1 6 01 0 0 52.0 0 1` | Input range, **output priority**, **charger priority**, … , re-discharge V | "Reserved" `1 0 1 6 01 0 0 52.0 0 1` | identical | **Fixed** [CONFIRMED: doc + this unit + the Yangtze unit]. `52.0` is a 48 V value on a 24 V unit |

**Consequence:** on this firmware, **QPIRI cannot read back** program 01 (output priority), 16 (charger priority), 03 (input range) or 13 (back-to-battery). It may also be unable to read back 02 and 11. The only candidate read-backs are:

- **QFLAG**, for the flags (if it is live);
- **PQSE**, if it answers in some framing;
- **the LCD**, read by the owner.

---

## 2. Q1: which SET commands the REVO dialect accepts

### 2.1 What Sorotec documents

Source: "Revo RS232 Communication Protocol — V02" (file `PI30REVO.20Protocol.20V03--20201112.pdf`, revision notes to 2020-11-12).

- **Writes defined:**
  - `PSET` (§1.6, "Set the current machine configuration");
  - the internal calibration write `~S…` (§2, PV calibration).
- **No** `PE`/`PD`, `POP`, `PCP`, `MCHGC`, `MUCHGC`, `PBT`, `PCVV`, … and **no `QFLAG`**. [CONFIRMED-doc, by absence]
- **Reads defined:** QPIRI, QPIGS, QMOD, QALL, PQSE, PQSA and QLITH0.
- **Checksum:** "CHK = The accumulated value of sent data + 1, single byte" [CONFIRMED-doc].
- **Doc errata:**
  - The PSET example (§4.3) gives CHK `F6`; by the doc's own rule it is `F7`. This was found by us earlier (v1 C14) and independently on GitHub ([FMode](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-5897971)), and an experiment confirmed it (§2.3) [CONFIRMED-multi].
  - §1.6 says the PSET reply is `(ACK<CHK><cr>`, but the doc's own §4.3 example shows `41 43 4B D0 0D`, i.e. **no `(`**. Real units reply without `(` (§2.3) [CONFIRMED-multi].

### 2.2 Stock PI30 setters on REVO-family units

| Command (purpose) | Evidence | Grade |
|---|---|---|
| `PDx` (backlight off), REVO-E 5.5 kW | Byte log: sent `50 44 78 60 59 0D`, reply `(NAKss\r` after ≈ 100 ms (gdb0701's attachment [disable_backlight.pdf](https://github.com/jblance/mpp-solar/files/8044407/disable_backlight.pdf), [comment](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-2153169)) | CONFIRMED-multi (with the rows below) |
| "any setting", REVO-E | "whenever i want to change a parameter, it gives me back a NACss"; Sorotec confirmed that settings change only via the panel ([DSF 34404](http://web.archive.org/web/20260710203714/https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/)) | SECONDARY (vendor statement relayed) |
| "most settings", **REVO VM II PRO 5.5 kW** | "Same at me now with the new REVO VM II PRO 5,5kW", but "Strange that some parameters I can change (ie. float and bulk voltage)" (chladekj, DSF 34404). The tool and commands are not stated | SECONDARY (single report) |
| `POP00`, EASUN SMR II 5.5 kW (a REVO II twin) | NAK, or "Response to short" ([ols-hub](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8521530)) | SECONDARY (single log) |
| All setters, "Axpert Hybrid, **Revo VM II**, ISolar SMR II" | SolarAssistant: "there is a good chance that the inverter will reject most setting changes from SolarAssistant and also the official WatchPower or SolarPower app. When investigating one of these units, we confirmed that the firmware … does not support changing settings via the communication ports." (help page as archived [2023-11-08](http://web.archive.org/web/20231108231738/https://solar-assistant.io/help/voltronic/settings), still present on [2025-07-01](http://web.archive.org/web/20250701191501/https://solar-assistant.io/help/voltronic/settings); the current page moved and no longer carries the text) | CONFIRMED-multi (vendor investigation) |
| "Silently ignored" (ACK, but no change) | **Not observed.** mkowal1's "shows result as 'AK' but actually nothing happens" was a misparsed `NAK\xDB\r` ([log](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8572942)) | – |
| **This unit (72.40)** | **Untested.** QFLAG answers, so this firmware implements more of stock PI30 than the REVO doc. That raises the chance that `PE`/`PD` exist, but proves nothing | INFERRED |

### 2.3 `PSET`: the exact working format

**Frame (Rx = inverter):**

```
"PSET" + <body> + CHK          (NO 0x0D at the end)
CHK  = (sum of all bytes of "PSET"+<body>) + 1, mod 256
body = the PQSE reply payload in PQSE's own layout (14 space-separated tokens):
       BCDEFG HH.H II.I JJ.J KK.K LLL MMM NNNN OO PP QQ KK SSTUV WW.W
reply: 41 43 4B D0 0D  ("ACK"+CHK+CR, no "(")   or   4E 41 4B DB 0D  ("NAK"+CHK+CR)
```

**Evidence for the format** [CONFIRMED-multi]:

- **EASUN SMR II 5.5 kW** (ols-hub), working frame `PSET021103 56.0 56.7 47.5 47.0 030 000 2024 02 24 10 28 00001 55.0` + `6B` ('k'), no CR, reply `ACK\xD0\r`, and the change applied: "I can control my inverters now" ([comment](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575754)). Our recomputation: the sum + 1 of that body is `0x6B` ✓.
- **Same user, controls:**
  - doc-style sum-only CHK (`h`), no CR → `NAK\xDB\r` ([comment](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575669));
  - CR appended → NAK or no reply at all (mkowal1, [log](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8572942)).
- **A second SMR II 5.5 kW owner:** "Works for me as well !" ([mkowal1](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8577190)).
- **REVO-E / REVO-family**, "had to remove the 0D from the end" ([wissamch](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-2142720)); confirmed by [Martin-Kolaci](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-5209738).
- **REVO II 5.5k / iGrid VE II**, "You may NOT end a PSET command with <CR> … If you skip the <CR> and use the format of the parameters as described for the PQSE command it works" (Itschi, [DSF 48846](http://web.archive.org/web/20260710212342/https://diysolarforum.com/threads/cant-get-igrid-ve-ii-sorotec-revo-ii-to-use-battery-if-no-pv-power-available.48846/)). Itschi used it in a script for months to switch the AC charging current between 0 and 10 A ([DSF 48846](http://web.archive.org/web/20260710212342/https://diysolarforum.com/threads/cant-get-igrid-ve-ii-sorotec-revo-ii-to-use-battery-if-no-pv-power-available.48846/), [DSF 48573](http://web.archive.org/web/20260710194129/https://diysolarforum.com/threads/force-easun-smr-ii-5-5kw-to-change-between-utility-and-pul-modes.48573/)).
- **mpp-solar** implements PSET this way since 0.16.26: commit [`921f0a3`](https://github.com/jblance/mpp-solar/commit/921f0a3f44e8fcd7cb13721c488dbf561c9a3948) "remove _PSET implement PSET with no newline"; [`pi30revo.py`](https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/pi30revo.py) `get_full_command`.

**Counter-reports:**

- An EASUN owner "tried any combination, including yours with no 0D at the end. I always get NAK" ([Friskiefriskie](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3864726)).
- A REVO III 8 kW owner found the REVO II layout "not compatible" ([tanqt0728](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3485621)). [SECONDARY]

**PSET body fields** (doc §1.5/§1.6) [CONFIRMED-doc]:

| Token | Fields |
|---|---|
| `BCDEFG` | B language (0 EN, 1 CN); C work mode (0 utility, 1 solar, 2 battery priority, 3/4 PV+mains without/with CT); D input range (0 wide, 1 narrow); E output V (0 = 220, 1 = 230, 2 = 240); F output Hz (0 = 50, 1 = 60); G **battery type** (0 lead-acid, 1 lithium, 2 none, 3 custom) |
| `HH.H II.I JJ.J KK.K` | Bulk, float, low-alarm and cut-off voltages |
| `LLL` / `MMM` | Total charging current (10–90 A) / AC charging current (0–60 A) |
| `NNNN OO PP QQ KK` | Clock: year, month, day, hour, minute |
| `SSTUV` | S buzzer **disable** (0 = buzzer on, 1 = off), S grid enable, T single-phase parallel, U three-phase parallel, V phase ID |
| `WW.W` | Battery-full recovery point |

**How PSET relates to PQSE:**

- PSET's body **is** PQSE's payload: same tokens and widths, without `(` and CHK.
- Real PQSE replies carried zero dates (`0000 00 00 00 00`) on units without a clock. The working PSETs sent the real date ([PQSE dumps](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575548)).
- The doc's own examples predate the 2020-11-12 extension: the PSET example has 13 tokens and ends with a 2-digit `SS`; the PQSE example ends `00 00` instead of `SSTUV WW.W`. One user's PQSE ended in `… 52.0 P\r`, i.e. it had an extra space before the CHK ([wissamch](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-1678722)). **The layout varies between firmware versions** [CONFIRMED-multi].
- PSET has **no field** for charger priority (prog 16), backlight (20), return-to-default (19), beep-on-interrupt (22), the overload flags, or any SOC threshold [CONFIRMED-doc].

**Risk: high.**

- One frame rewrites about 20 parameters, including battery type, all battery voltages, output voltage and frequency, input range, clock and parallel flags.
- The layout varies by model and firmware.
- With no CR, the inverter must find the end of the frame by timing or length, so a following query could merge with it [INFERRED].
- The single-byte sum catches far fewer corruptions than a CRC: compensating errors and byte swaps pass [INFERRED].
- Whether a write is "all at once" in EEPROM is unknown.

### 2.4 Per-setting REVO commands

**None** are documented or reported anywhere we searched: the Sorotec doc, mpp-solar, Solar2MQTT, ESPHome pipsolar, DIY Solar Forum, AEVA, Power Forum and SolarAssistant [CONFIRMED-doc, by absence; INFERRED for the forums].

Solar2MQTT offers only a raw "Command Sender" passthrough with no REVO setter knowledge ([wiki](https://github.com/softwarecrash/Solar2MQTT/wiki/Set-parameters)).

---

## 3. Q2: real-world reports

| Unit (firmware) | Tool / command | Result | Source | Grade |
|---|---|---|---|---|
| REVO-E 5.5 kW | mpp-solar `PDx`; other setters | `(NAKss` every time; Sorotec: panel only | [#78 c-2153169](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-2153169), [#78 c-3500112](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3500112), [DSF 34404](http://web.archive.org/web/20260710203714/https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/) | first-hand |
| **REVO VM II PRO 5.5 kW** | unspecified | NAK for most; float and bulk changeable | DSF 34404 (chladekj) | first-hand, single |
| REVO-family (REVO-E?) | PSET | NAK with CR; **works without CR** | [#78 c-2142720](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-2142720), [#78 c-5209738](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-5209738) | first-hand ×2 |
| REVO II 5.5k / iGrid VE II | PSET (no CR), scripted mode and AC-current changes | Works; used for months | DSF 48846, 48573, [23525 p.15](http://web.archive.org/web/20260713015353/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-15) (Itschi) | first-hand |
| EASUN SMR II 5.5 kW ×2 | mpp-solar 0.16.26 PSET | `ACK D0`, settings changed | [#78 c-8575754](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575754), [#78 c-8577190](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8577190) | first-hand ×2 |
| EASUN (model n/a) | PSET, all variants | Always NAK | [#78 c-3864726](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3864726) | first-hand |
| EASUN SMR II ×6 and ×10 | WatchPower, SolarAssistant, ICC | "can not be programmed over RS232" | [DSF 48573](http://web.archive.org/web/20260710194129/https://diysolarforum.com/threads/force-easun-smr-ii-5-5kw-to-change-between-utility-and-pul-modes.48573/) (Sunexa, Watchout) | first-hand |
| iGrid VE II, graphical LCD | Own reverse engineering; SolarPower | "possible configure almost all settings programmatically via serial" (method undisclosed); the COM port goes to the display board, which "doesn't use configuration commands supported by SolarPower" | [DSF 23525 p.7](http://web.archive.org/web/20260718151326/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-7) (Dave21) | first-hand, single |
| iGrid VE II (Powland) | WatchPower / SolarPower | Parameter-set error | [DSF 23525 p.5](http://web.archive.org/web/20260713155126/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-5) (kemot_73) | first-hand |
| Revo VM II 8K | SolarPower from Sorotec's site | Asks for a ≥ 6-character password; outcome unknown | [DSF 35132](http://web.archive.org/web/20260710192612/https://diysolarforum.com/threads/sorotec-revo-ii-factory-password.35132/) (RotEvery) | first-hand, incomplete |
| REVO II | SmartESS WiFi app | "I cannot change settings on it" | DSF 35132 (baarcaya) | first-hand |
| Yangtze VMII5K48 (**72.40**) | mpp-solar, reads only | No write attempts | [AEVA t=7440](http://web.archive.org/web/20220327033220/http://forums.aeva.asn.au/viewtopic.php?t=7440) | first-hand |
| **Savx F4 / REVO VM II PRO 3.2 kW round-dial (72.40)** | – | **No report anywhere** | – | – |

**Failures, bricking, configuration loss, EEPROM wear:**

- No report of bricking or lost configuration from an RS232 write was found.
- One user who drove an iGrid VE II through scripted PSET writes saw fault 01 (bus over-voltage) roughly every 10 days. He did not attribute it to the writes ([DSF 23525 p.14](http://web.archive.org/web/20260713015444/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-14)) [SECONDARY, causality unknown].
- On EEPROM wear, the AEVA firmware expert is quoted as saying it is "not clear … whether sending such commands causes extra writes to the EEPROM chip", and that POP and PCP "override settings read from the EEPROM" ([Power Forum 623](http://web.archive.org/web/20251011080402/https://powerforum.co.za/topic/623-axpert-eeprom-chip-wearing-out-after-multiple-writes/)) [SECONDARY].

---

## 4. Q3: OEM and PC software

- **No sniffed trace of any OEM tool successfully writing to a REVO-family unit was found.**
- The only write path proven to work (PSET without CR) was found by the community from Sorotec's document, not copied from vendor software.
- **WatchPower / SolarPower** use the stock PI30 setters on Voltronic units. A user sniffed WatchPower's parameter-change frames on an EASUN ISolar SML, a non-REVO unit ([DSF 48573](http://web.archive.org/web/20260710194129/https://diysolarforum.com/threads/force-easun-smr-ii-5-5kw-to-change-between-utility-and-pul-modes.48573/), cyd) [SECONDARY].
- **Vendor and other tools on REVO-family units:**
  - SolarAssistant found that WatchPower and SolarPower are rejected on Revo VM II / SMR II [CONFIRMED-multi];
  - kemot_73 got an error from both on an iGrid VE II;
  - Dave21 says the display board on graphical-LCD units does not implement the SolarPower configuration commands.
- **The Savx manual (§3.8)** points to WatchPower for RS232 *monitoring* only [CONFIRMED-doc, manual-extract.md].
- **"SOLAR OF THINGS"**, the app for the optional WiFi collector (Shanghai Siseli, [Play Store](https://play.google.com/store/apps/details?id=com.ssli.sise_solar)), advertises local monitoring and debugging. Whether it can write settings to this unit is unknown. **The collector is not owned and cannot be bought**, so its traffic cannot be sniffed. [SECONDARY / not testable]
- **Conclusion:** vendor software gives no positive evidence that settings can be written on this platform. Its evidence is negative for the stock setter set.

---

## 5. Command table

Risk is to the house, battery or inverter if the command is wrong or misapplied. Status for 72.40 is "untested" unless stated.

| Command (bytes incl. CRC + CR) | Purpose (LCD prog) | REVO-family status (grade, source) | This unit's read-back | Risk | Recommended |
|---|---|---|---|---|---|
| `PEa` `50 45 61 D0 70 0D` / `PDa` `50 44 61 E3 41 0D` | Buzzer on / off (18) | Not in the REVO doc [CONFIRMED-doc]. REVO-E NAKs the sibling `PDx` [CONFIRMED-multi] | QFLAG `a` (live-ness unproven) | Low (`PDa` mutes alarms) | **Test-first:** same-value `PEa` is the first write ever. `PDa` local only |
| `PEx` `50 45 78 53 68 0D` / `PDx` `50 44 78 60 59 0D` | Backlight (20) | `PDx` → `(NAKss` on REVO-E (byte log) [CONFIRMED-multi with §2.2] | QFLAG `x` | None | **Test-first** (first visible toggle) |
| `PEk`/`PDk` (`71 3A` / `42 0B`), `PEy`/`PDy` (`43 49` / `70 78`) | Return to default (19); beep on interrupt (22) | As above | QFLAG `k`, `y` | Low | Test-first, after `x` |
| `PEb`/`PDb`, `PEu`/`PDu`, `PEv`/`PDv`, `PEz`, `PEj`/`PDj` | 23, 06, 07, 25, power saving | As above | QFLAG | Medium (overload behaviour; diagnostics) | **No remote.** LCD |
| `POP00` `…30 30 C2 48 0D`, `POP01` `…30 31 D2 69 0D`, `POP02` `…30 32 E2 0B 0D` (escape: raw `E2 0A`) | Output priority (01) | NAK / no response on SMR II [SECONDARY]; "most settings rejected" on Revo VM II [CONFIRMED-multi] | **None via QPIRI** (field 16 fixed). PQSE `C` if PQSE exists; otherwise LCD | Medium–high (SbU can black out the house) | Test-first, **only after the flags pass and with an independent read-back** |
| `PCP00…03` (`8D 7A`, `9D 5B`, `AD 38`, `BD 19`) | Charger priority (16) | As POP | **None** (QPIRI 17 fixed; not in PSET) | Medium (OSO = no grid charging) | Test-first, later; code order learnable only from the LCD |
| `MUCHGC0nn` (e.g. `030` → `C0 C0`) | Max AC charging current (11) | Not reported working; PSET `MMM` works on SMR II [CONFIRMED-multi] | PQSE `MMM`; QPIRI 13 doubtful | Medium | Test-first, after POP |
| `MCHGC0nn` (`090` needs escape `0E 8F`) | Max charging current (02) | Not reported | PQSE `LLL`; QPIRI 13 doubtful | Medium | Test-first, later |
| `PBCV`, `PBDV`, `PSDV` | Back-to-utility (12), back-to-battery (13), cut-off (29) | Not reported. **Lithium mode uses SOC %; no SOC command exists** [CONFIRMED-doc] | QPIRI 8/9 suspected per-12 V; 22 fixed | High | **No** (LCD only) |
| `PCVV`, `PBFT` | Bulk, float (26/27) | "Changeable" on VM II PRO 5.5k [SECONDARY]. BMS asks for 28.4 V | QPIRI 10/11 (suspected per-12 V) | High (lithium) | **No** |
| `PBT`, `F50`/`F60`, `V2xx`, `PGR`, `PBEQ*`, `PF`, `PSAVE`, `~S…` | Battery type, output Hz/V, input range, equalisation, factory reset, calibration | – | – | Severe | **Never** |
| `PSET<body><CHK>` (no CR) | Full configuration | Works on several REVO II / SMR II with sum + 1 and no CR [CONFIRMED-multi]; NAK on others | PQSE (same layout) | **High** (all at once) | **No by default.** Only if PQSE answers on this unit **and** the stock setters NAK, then a separate decision (§9 Phase 2-alt) |
| `PQSE` `50 51 53 45 3A 0D` (CHK) / `50 51 53 45 5F 48 0D` (CRC) | Read configuration | Answers on REVO II / SMR II [CONFIRMED-multi] | **CHK: no reply here** [INFERRED]; CRC: untested | None (read) | **Yes:** confirm in Phase 0; add the CRC variant as a read |
| `QFLAG` `51 46 4C 41 47 98 74 0D` | Read flags | Not in the REVO doc; answers here [CONFIRMED-unit] | – | None | Yes; prove live-ness (Phase 0) |
| `QMCHGCR` / `QMUCHGCR` | Allowed current values | Not reported | – | None (read; one dropped byte from a setter, `Whitelist.cpp`) | Optional, once, owner present |

---

## 6. Q4: which settings are worth controlling remotely

**Useful remotely** (owner's goals: shift load to solar or battery, limit grid charging, quiet nights):

| Prog | Setting | Why remotely | Known path on this unit |
|---|---|---|---|
| 01 | Output source priority (UtI / SOL / SbU) | Use solar and battery by day, grid at night or during outages | `POP` (untested); PSET `C` (if PSET exists) |
| 16 | Charger source priority | Avoid grid charging when sun is expected | `PCP` only (not in PSET) |
| 11 | Max utility charging current | Limit grid charging | `MUCHGC`; PSET `MMM` |
| 02 | Max total charging current | Seasonal or battery-health cap | `MCHGC`; PSET `LLL` |
| 18 | Buzzer | Quiet at night. **Enable remotely, disable locally only** (alarms matter) | `PEa`/`PDa`; PSET `S` |
| 20 | Backlight | Convenience; it is also the ideal harmless test | `PEx`/`PDx` |

**Never remotely:**

- **05 battery type:** a wrong type over- or under-charges the lithium battery. PSET carries it in every frame, which is why PSET is high-risk.
- **12 / 13 / 29 back-to-utility, back-to-battery and cut-off:** in LIb mode these are SOC %, and no command for SOC exists. The voltage scaling here is doubtful (§1.2).
- **26 / 27 bulk and float:** the battery's BMS governs these.
- **09 / 10 output frequency and voltage:** can damage appliances.
- **30–36 equalisation:** wrong for lithium.
- **03, 06, 07, 23:** transfer and overload behaviour.
- **25 fault recording.**
- **43 feed-in.**
- **`PF` factory reset:** restores AGM defaults on a lithium system.
- **Calibration `~S`.**

This matches docs/10 §3, with the read-back corrections in §10 below.

---

## 7. Q5: safety design

### 7.1 Read-back

Read-back decides the outcome; an ACK is only advisory (P2-D5, still right).

- **Flags:** QFLAG, only once Phase 0 shows it follows the LCD. The REVO schedule does **not** poll QFLAG today; only the probe sends it (`Pi30Adapter::schedule`). A write build must read QFLAG before and after each flag write.
- **Priorities and currents:** PQSE, if it answers in some framing. Otherwise **the LCD is the only oracle**. A remote change that cannot be read back fails docs/10 §7 guarantee 7 and must not be offered remotely.
- **Behaviour is a weak secondary check, never a substitute** [INFERRED]. Examples: QMOD `L` → `B` and the QALL discharge current after SOL/SbU; the QALL AC-charging current after `MUCHGC`.
- **Not usable on this unit:** QPIRI fields 13–24, QDI and the QPIGS status bits (§1). The docs/02 rule "re-read settings immediately when QPIGS b6 = 1" can never fire here.

### 7.2 One setting at a time, and rate limits

These are starting values [INFERRED].

| Rule | Value |
|---|---|
| Frames per change | One setter frame. **Never retry automatically**: a NAK can be transient here (QPIWS NAK under fast polling) and must be retried by a person on another occasion |
| Spacing | ≥ 60 s between writes; ≥ 2 s line silence before and after each write (polling paused) |
| Daily budget | ≤ 3 writes/day and ≤ 30/month to start |
| Same-value writes | **No** same-value or periodic writes, except the single acceptance test. Compare before writing |
| Link health | Write only after ≥ 30 min of clean link (0 timeouts, 0 CRC errors). This unit had an unexplained 8-minute TX silence on 2026-10-22 |

### 7.3 EEPROM and flash wear

- Where this firmware stores settings is unknown: an external EEPROM or the DSP's flash. Voltronic-family boards use TI C2000 DSPs (the AEVA thread shows C28x disassembly).
- Typical endurance quoted on Power Forum is "10,000 to a million" cycles [SECONDARY].
- With the budget above, even a 10,000-cycle cell lasts more than 9 years at 3 writes/day [INFERRED].
- A PSET most likely rewrites the whole configuration block on every write [INFERRED].

### 7.4 Power or link failure mid-write

| Case | Handling |
|---|---|
| PI30 setters (6–12 bytes, 25–50 ms at 2400 baud) | A truncated frame has no valid CRC/CR, and the next query's CR should end it with a NAK. **Unless the inverter ignores the input CRC** (unknown: run the docs/10 Test 0 CRC probe first) [INFERRED] |
| PSET (≈ 65–70 bytes, ≈ 0.3 s, no CR) | A truncated PSET plus the next query could be parsed together. The 8-bit sum then almost always fails (→ NAK), but not provably [INFERRED]. Hence ≥ 3 s of silence after any interrupted or uncertain PSET |
| ESP32 reboot between send and reply | State unknown: no writes for 24 h, read back, report (r9 §5.4.6, still right) |
| Inverter loses power during its own EEPROM write | Unknown behaviour; some firmwares fall back to defaults (battery type AGM). Write only in Line mode with grid and battery healthy. After every acceptance test the owner checks LCD prog 05 = LIb [INFERRED] |

### 7.5 ACK and NAK detection (exact bytes)

| Reply bytes | Meaning | Grade |
|---|---|---|
| `28 41 43 4B 39 20 0D` | PI30 setter ACK | CONFIRMED-doc (PI30 2015 §3) |
| `28 4E 41 4B 73 73 0D` | PI30 NAK | CONFIRMED-unit (queries) and REVO-E (`PDx`) |
| `41 43 4B D0 0D` | PSET ACK | CONFIRMED-multi |
| `4E 41 4B DB 0D` | PSET NAK | CONFIRMED-multi |
| `(ACK`+`F8`, `(NAK`+`03`, or anything else (echo, garbage, nothing within 3 s) | **Unknown**: read back, never retry | – |

### 7.6 Operating mode

- The PI30 docs give no mode condition for the flag, POP, PCP or current setters [CONFIRMED-doc, by absence].
- The manual restricts only programs 42 and 45 (Line mode).
- Do every test in **Line mode (QMOD `L`), grid present, no warning or fault, battery ≥ 80 %**.
- Never write in `F`, `P` or `H` mode [INFERRED].

---

## 8. Q6: electrical check of the TX path

- **Queries already work:** 14/14 OK and 0 CRC errors at first contact; 15 replies per 30 s at 2 s polling. The inverter's receiver therefore decodes our inverted 0/3.3 V signal reliably [CONFIRMED-unit].
- **A write is the same kind of frame on the same pin, at the same voltage and current.** PI30 setters are 6–12 bytes, as long as the queries already sent. **No new electrical concern** [INFERRED, high confidence].
- **The remaining unknowns are about data, not electricity:**
  1. Whether the inverter checks the CRC of incoming frames. Run the read-only CRC probe (docs/10 Test 0, r9 §4.4) before the first write.
  2. PSET's long, weakly protected frame (§7.4).
  3. The lines the firmware drives during resets. The existing arming check (GPIO17 must read LOW at boot) already covers idle-state glitches.
- **No purchase is needed.** A MAX3232 would add margin but is not required.

---

## 9. Minimal on-site test plan

**Global rules for every phase:**

- The owner stands at the inverter LCD, in daylight, with the grid present.
- Nobody touches the inverter's buttons while a command is in flight.
- The docs/07 stop rules apply: on any unexpected beep, new warning or fault, or heat, stop and pull the RJ45 from COM.
- Photograph the LCD program values first (site survey §8): programs 01, 02, 05, 11, 12, 13, 16, 18, 19, 20, 22, 23, 25, 26, 27, 29, plus the U1/U2 firmware pages.

### Phase 0: zero writes (existing firmware and tools; decides go/no-go)

| Step | Action (tool) | Record | Decision |
|---|---|---|---|
| 0.1 | Fill in site-survey §8 from the LCD (photos) | All program values | – |
| 0.2 | Run a full probe (`probe` on the serial console, or `POST /api/probe`). Read `GET /api/probe` (lines `PQSE -> …`, `PQSA -> …`), `/api/status` (`sup`) and `/api/raw` (QFLAG, QPIRI) | PQSE/PQSA result: OK / NAK / TIMEOUT; the exact QFLAG and QPIRI | PQSE answers → note its payload and compare with the LCD (`C` ↔ 01, `LLL` ↔ 02, `MMM` ↔ 11, `S` ↔ 18, `G` ↔ 05) |
| 0.3 | Compare QFLAG with the LCD: 18 ↔ `a`, 19 ↔ `k`, 20 ↔ `x`, 22 ↔ `y`, 23 ↔ `b`, 25 ↔ `z`, 06 ↔ `u`, 07 ↔ `v` | Matches / mismatches | Any mismatch (watch `z`) → QFLAG is suspect |
| 0.4 | **Live-ness test (the owner uses the LCD, the logger only reads):** set program **20 LON → LOF** at the LCD and run a probe; set **LOF → LON** and run a probe again | QFLAG before / after / restored | `x` moves to the D list and back → **QFLAG is live → Phase 1 allowed.** `x` does not move → QFLAG is fixed: no flag read-back, and the flag path stops |
| 0.5 (optional) | Same idea for prog 11 (30 → 20 → 30 A) and prog 01 (UtI → SOL → UtI, battery ≥ 80 %, low load), with a probe after each change | Which QPIRI / PQSE field moves | Expected: QPIRI 15–24 never move. If nothing moves anywhere → priorities and currents have no read-back |
| 0.6 (needs a read-only firmware change; not done here) | Add `PQSE` and `PQSA` with **PI30 CRC** framing (`50 51 53 45 5F 48 0D`) to the whitelist for one probe. Run the r9 §4.4 input-CRC probe (QPI with a corrupted CRC) | Replies | PQSE-CRC answers → PSET path conceivable (Phase 2-alt) |

**Phase 0 outcome:**

- **QFLAG live** → Phase 1.
- **QFLAG fixed and PQSE silent** → remote settings are **not feasible** without new hardware. Keep the read-only logger and use the LCD.

### Phase 1: first write (one-shot test build)

This needs a separate, temporary firmware build. The normal `savx` build cannot write: `INV_ALLOW_WRITES=0` and the exact-match whitelist. The test build should contain exactly these three hard-coded frames, each sent once, from the local page with a BOOT press.

| Step | Frame (hex) | Expected reply | The owner watches | Read-back | Undo |
|---|---|---|---|---|---|
| 1.1 Same-value buzzer ON (alarm already bON) | `50 45 61 D0 70 0D` (`PEa`) | `28 41 43 4B 39 20 0D` or `28 4E 41 4B 73 73 0D` | **Nothing changes:** no beep, no mute icon | QFLAG unchanged | – |
| 1.2 Backlight OFF (only if 1.1 = ACK) | `50 44 78 60 59 0D` (`PDx`) | ACK | Backlight goes dark (on Voltronic-style units LOF may only act after the idle timeout [INFERRED]). Program 20 reads **LOF** | QFLAG `x` in the D list | LCD prog 20 → LON |
| 1.3 Backlight ON | `50 45 78 53 68 0D` (`PEx`) | ACK | Program 20 reads **LON**; light on | QFLAG `x` in the E list | LCD prog 20 → LON |

**Decisions:**

- **1.1 NAK:** nothing changed (confirm by QFLAG and the LCD). Repeat once on another day, because NAKs can be transient here. A second NAK → **stock setters refused**, consistent with REVO-E / SolarAssistant. Go to Phase 2-alt only if Phase 0 showed PQSE answering; otherwise stop.
- **No reply:** record "unknown", read QFLAG, send nothing more that day.
- **ACK but QFLAG or the LCD did not change:** "dishonest ACK" → stop.
- **After 1.3, the owner also checks LCD prog 05 = LIb and prog 01 unchanged** (no side effects).

### Phase 2: one priority setting (only if Phase 1 passed AND an independent read-back exists for prog 01)

**Preconditions:**

- Line mode, grid on, battery ≥ 80 %, load < 30 % of rating, no warnings, between about 10:00 and 15:00.
- Program 01 = UtI on the LCD.

**Steps:**

1. Same-value `POP00` = `50 4F 50 30 30 C2 48 0D`. Expect ACK; nothing changes.
2. `POP01` (SOL) = `50 4F 50 30 31 D2 69 0D`. The LCD program 01 reads **SOL**. Watch the source icons and the AC/INV LED: solid = utility, flashing = battery. Observe for 10 min.
3. `POP00` → program 01 reads **UtI**.
4. **Undo at any time:** LCD program 01 → UtI.
5. **Read-back:** PQSE `C` if available, otherwise the LCD. **Never QPIRI field 16** (fixed).
6. **Do not test `POP02` (SbU)** until the battery's BMS ratings are recorded (docs/10 §2 item 6). Its CRC also needs the 0x0A escape (`E2 0A` → `E2 0B`).

### Phase 2-alt: PSET (only if PQSE answers on this unit and the stock setters NAK; needs its own decision record)

1. **Read PQSE twice**, at most 5 s apart. Both must be identical, have 14 tokens, and match the token widths `6 4 4 4 4 3 3 4 2 2 2 2 5 4`.
2. **Build the body** = that payload, unchanged except **one character**: `C` (work mode) or one digit of `MMM`. Byte-compare the body with PQSE: exactly one differing position, and never in `G`, `E`, `F`, `HH.H`, `II.I`, `JJ.J`, `KK.K` or `WW.W`.
3. **First a verbatim same-value PSET.** Frame = `PSET` + body + CHK(sum + 1), **no CR**, then ≥ 3 s of silence.
   - Expect `41 43 4B D0 0D`; then PQSE must be byte-identical.
   - **Clock fields:** if PQSE returns `0000 00 00 00 00`, whether the inverter accepts zeros is unknown (the working reports sent the real date). Treat a NAK here as "format", not "unsupported".
4. **Then the single-character change.** The LCD shows the new value. PQSE differs in exactly that position.
5. **Undo:** the LCD (program 01 or 11). Never undo by sending a second PSET built from stale data.

---

## 10. What must change in docs/10

Do not edit these here. These are the claims and sections to update when the owner decides.

1. **§1, last bullet of "Phase 2 is not"**:
   - Current claim: "probably a Sorotec REVO-platform unit … owners report that RS232 setting writes are refused on REVO units. If the logger identifies the unit as REVO, Phase 2 stops before a single setting command is sent."
   - What is true now:
     - the unit **was** identified as REVO on 2026-10-22;
     - "writes refused" is true for the **stock PI30 setters** on REVO-E, REVO II and SMR II (byte logs; SolarAssistant);
     - REVO **`PSET` works** on several units without CR (§2.3);
     - this unit answers the stock `QFLAG` and probably not `PQSE`.
   - Replace the REVO auto-stop with "Phase 2 continues only through r10 Phase 0 (zero writes) and stops if Phase 0 fails".
2. **§2 Preconditions:**
   - Item 3 ("three extra checks for REVO all came back negative") can no longer be met. Replace it with the r10 Phase 0 results: PQSE in CHK and CRC framing, QFLAG live-ness by the LCD backlight toggle, and the QPIRI/PQSE field map.
   - Item 5: state that QPIRI fields 15–24 are fixed on this firmware, so programs 01/03/16 have no QPIRI mapping.
   - Item 9 (CRC check): keep it, and move it before any write.
3. **§3 table:**
   - Rows 01, 02, 11, 16: "Remote" must become "Remote only if a live read-back exists (PQSE or other); QPIRI cannot read it on 72.40". Row 16: the code order can only be learned at the LCD (QPIRI 17 is fixed).
   - Row "Factory reset, full-configuration write (REVO `PSET`) … Never": keep as the default, but note that PSET is the only REVO write path known to work elsewhere, and that using it needs a separate decision because it carries battery type and voltages (it conflicts with §7 guarantee 2).
4. **§4 flow:**
   - "read all settings → QPIRI / QFLAG" must say "QFLAG (+ PQSE if available)" on REVO.
   - The ACK line must list the exact reply bytes, including the PSET form without `(` (§7.5).
5. **§7 guarantees:**
   - Guarantee 2 ("REVO `PSET` absent even from `savx-rw`") stays true unless the PSET decision is taken.
   - Guarantee 7 ("read back in full") must define "full" for REVO 72.40: QFLAG + PQSE, excluding the fixed QPIRI, QDI and QPIGS-status fields.
   - Guarantee 9: add "≥ 3 s silence after any uncertain write" (PSET has no CR).
6. **§8 milestones:**
   - P2-M0 "REVO → Phase 2 stops here": replace with the r10 §9 Phase 0 decision.
   - P2-M5: add the exact expected bytes and the "NAK twice on different days" rule.
   - P2-M6 and P2-M9: read-back via PQSE or LCD, not QPIRI.
7. **§9 test table:**
   - Test 1 stays the first write (same-value `PEa`).
   - Test 2 backlight: as in r10 Phase 1.
   - Test 4: remove any reliance on QPIRI field 16.
   - Test 7: "code order … confirmed from the LCD (site survey B6)" is impossible from QPIRI here; it needs a write plus the LCD.
   - Test 5/6: read-back via PQSE `MMM`/`LLL` only.
8. **§11 open questions:** add the r10 §11 items 1–5.
9. **Outside docs/10, same change:**
   - r9 §0 item 1, §4.2 (REVO → NO-GO tree) and §4.3 ("any QALL/PQSE answer → refuse all writes") must be revised the same way;
   - docs/02 §7.7 / §10: the QPIGS b6 "configuration changed" trigger never fires on this unit (status bits fixed), and QDI is fixed;
   - r9 §4.3 fingerprint: QID `5535535553555` looks like a placeholder (§11), so it adds little uniqueness.

---

## 11. Open questions

1. **PQSE / PQSA.** Do they answer at all on 72.40? Confirm the CHK timeout in `/api/probe`, and try PI30 CRC framing (needs a read-only whitelist addition).
2. **QFLAG.** Is it live (Phase 0.4)? Why does `z` (fault recording) read disabled when the manual default is FEn?
3. **QPIRI fields 8–14.** Are they live? Is the per-12 V hypothesis right (compare with LCD 26/27/29 and the warning point)? Which code shows LIb, and does field 13 = 60 mean prog 02, prog 11 or a constant?
4. **Input CRC.** Does the inverter check the CRC of incoming frames (r9 §4.4 probe)?
5. **Stock setters.** Do `PEa`/`PEx` ACK on 72.40, and does an ACK really apply (QFLAG + LCD)?
6. **Persistence.** Do accepted changes survive an inverter power cycle (docs/10 Test 9)?
7. **Storage.** Where are settings stored (EEPROM or flash), and what is the endurance? Is a same-value write a real write?
8. **PSET details** (only if PQSE answers): zero clock fields, the `SSTUV WW.W` tail on this firmware, and the meaning of the "Grid Enable" bit.
9. **QID.** It looks like a placeholder: `5535535553555` here, and a similar `553555355535552` on another Voltronic-style unit ([#78 c-5608001](https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-5608001)). Does the label serial differ?
10. **Vendor.** Would IPS Bazar or Sorotec provide a VM II PRO (2025 round-dial) protocol document that lists setters, or a firmware that accepts them? There is no cost, but the answer is uncertain.
11. **Other units.** Is the VM II PRO 5.5 kW report ("float and bulk changeable") reproducible? It hints that some stock setters exist in this family.

---

## 12. Sources

**Primary documents and code**
- Sorotec "Revo RS232 Communication Protocol V02" (file V03--20201112): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf (local text copy `scratchpad/research/dl-r2-pdf/pi30revo.txt`)
- Voltronic PI30 protocol 2015-09-24 (setters §3, ACK/NAK): https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Communication-Protocol-20150924-Customer.pdf
- mpp-solar `pi30revo.py` (PSET, no CR, sum + 1): https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/pi30revo.py
  - commit 921f0a3: https://github.com/jblance/mpp-solar/commit/921f0a3f44e8fcd7cb13721c488dbf561c9a3948
  - commit b71dcdd ("add pset"): https://github.com/jblance/mpp-solar/commit/b71dcdd00cecb00c3d65695be8b175634d9c01f9
- mpp-solar `pi30.py` (stock setter definitions and test replies): https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/pi30.py
- ESPHome pipsolar (`POP`, `PCP`, `MUCHGC` outputs): https://github.com/esphome/esphome/tree/dev/esphome/components/pipsolar
- Savx F4 manual: `research/manual-extract.md`, `reference/Savx-F4-user-manual.pdf`
- Own captures: `docs/probe/2026-10-22-first-contact.txt`, the session serial log; logger code `Pi30Adapter.cpp`, `RawRing.h`, `Whitelist.cpp`

**First-hand reports**
- mpp-solar Discussion #78 (REVO): https://github.com/jblance/mpp-solar/discussions/78. Key comments:
  - PDx NAK log: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-2153169 (attachment https://github.com/jblance/mpp-solar/files/8044407/disable_backlight.pdf)
  - "remove the 0D": https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-2142720
  - confirmation: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-5209738
  - F7 not F6: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-5897971
  - POP00 fails: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8521530
  - with CR → NAK / empty: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8572942
  - PQSE dumps: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575548
  - sum-only CHK → NAK: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575669
  - **ACK D0, works**: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8575754
  - second user works: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-8577190
  - never works: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3864726
  - Sorotec "panel only": https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3500112
  - REVO III incompatible: https://github.com/jblance/mpp-solar/discussions/78#discussioncomment-3485621
- mpp-solar issues #220 (REVO setters "not implemented"): https://github.com/jblance/mpp-solar/issues/220 and #403 (SMR II PSET): https://github.com/jblance/mpp-solar/issues/403
- DIY Solar Forum 34404 (REVO-E, VM II PRO 5.5k, PSET without CR): https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/ (archived http://web.archive.org/web/20260710203714/https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/)
- DIY Solar Forum 48846 (Itschi, PSET format): http://web.archive.org/web/20260710212342/https://diysolarforum.com/threads/cant-get-igrid-ve-ii-sorotec-revo-ii-to-use-battery-if-no-pv-power-available.48846/
- DIY Solar Forum 48573 (SMR II cannot be programmed; Itschi; WatchPower sniffing): http://web.archive.org/web/20260710194129/https://diysolarforum.com/threads/force-easun-smr-ii-5-5kw-to-change-between-utility-and-pul-modes.48573/
- DIY Solar Forum 23525 (REVO II / iGrid VE II), pages 5, 7, 14 and 15:
  - http://web.archive.org/web/20260713155126/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-5
  - http://web.archive.org/web/20260718151326/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-7
  - http://web.archive.org/web/20260713015444/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-14
  - http://web.archive.org/web/20260713015353/https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/page-15
- DIY Solar Forum 35132 (SolarPower password, SmartESS cannot set): http://web.archive.org/web/20260710192612/https://diysolarforum.com/threads/sorotec-revo-ii-factory-password.35132/
- AEVA forum t=7440 (Yangtze VMII5K48, VERFW 00072.40, identical QPIRI tail): http://web.archive.org/web/20220327033220/http://forums.aeva.asn.au/viewtopic.php?t=7440

**Vendor and secondary**
- SolarAssistant "Troubleshooting – Axpert Hybrid, Revo VM II, ISolar SMR II": http://web.archive.org/web/20231108231738/https://solar-assistant.io/help/voltronic/settings (also in the 2025-07-01 snapshot; current URL https://solar-assistant.io/help/voltronic/settings no longer carries it)
- Power Forum topic 623 (EEPROM wear; AEVA quote): http://web.archive.org/web/20251011080402/https://powerforum.co.za/topic/623-axpert-eeprom-chip-wearing-out-after-multiple-writes/
- Solar2MQTT "Set parameters" (raw passthrough only): https://github.com/softwarecrash/Solar2MQTT/wiki/Set-parameters
- "Solar of Things" app listing: https://play.google.com/store/apps/details?id=com.ssli.sise_solar
