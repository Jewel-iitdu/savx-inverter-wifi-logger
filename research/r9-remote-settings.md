# R9: Phase 2 design: opt-in remote control of selected inverter settings

**Status:** final design, 2026-10-21. **Design only. Nothing here is implemented.** v1 ships strictly read-only (`docs/00` D11; `INV_ALLOW_WRITES=0`; exact-match read whitelist in `firmware/src/link/Whitelist.*`). The plan chapter for the owner is [`docs/10-phase2-remote-settings.md`](../docs/10-phase2-remote-settings.md). It supersedes the review draft `research/r9-remote-settings-draft.md`, which is left unchanged.

**Scope.** After read-only logging has been proven, let the owner change a small, fixed set of settings values from the PWA. Every change needs:
- a window opened **physically** at the logger;
- a value that was verified on the LCD during on-site acceptance;
- range, battery and inverter-state checks;
- exactly one setter frame;
- a whole-snapshot read-back;
- follow-up reads;
- an audit record and a notice.

Everything else stays on the inverter LCD.

**Inputs:**
- the Phase 2 setter research (**P2SET §n**, relayed with the task);
- the R9 draft;
- the adversarial safety review of that draft (**SR Fn**);
- the firmware, web and rules code as read today;
- the project docs listed in §16.

**Evidence tags:**
- **[CONFIRMED-primary]**: read today in a primary protocol document, the paper manual, or this repository's code ("(code)").
- **[SECONDARY]**: a non-official source (forum, ESPHome, mpp-solar used as a reference implementation).
- **[INFERRED]**: design reasoning or an unverified platform behaviour. Every [INFERRED] item that matters for safety has a test in §11.

**Source keys** (P2SET's):
- **MAN**: the paper manual §4.4 (`reference/Savx-F4-user-manual.pdf` pp. 13–20).
- **P14 / P15 / PMAX / PGK / REVO**: local text copies `pi30_2014.txt`, `pi30_2015.txt`, `pi30max.txt`, `pi30_gkmk.txt`, `pi30revo.txt` in the scratchpad `research/dl-r2-pdf/`.
- **MPP**: mpp-solar protocol sources.
- **ESPH**: ESPHome `pipsolar`.
- **FORUM**: DIY Solar thread 34404, via `research/r1-oem-identification.md` §4.2.

---

## Contents

0. [Summary](#0-summary)
1. [Safety-review disposition](#1-safety-review-disposition)
2. [Master command table (programs 00–50)](#2-master-command-table-programs-0050)
3. [Settings catalog](#3-settings-catalog)
4. [Rollout gates and the go/no-go decision](#4-rollout-gates-and-the-gono-go-decision)
5. [Firmware](#5-firmware)
6. [Local enable window and physical confirmation](#6-local-enable-window-and-physical-confirmation)
7. [Data contract (RTDB)](#7-data-contract-rtdb)
8. [Web UI: "Inverter settings"](#8-web-ui-inverter-settings)
9. [Notifications](#9-notifications)
10. [Invariants](#10-invariants)
11. [Tests](#11-tests)
12. [Milestones, effort and acceptance](#12-milestones-effort-and-acceptance)
13. [Rollback](#13-rollback)
14. [Risks](#14-risks)
15. [Open questions](#15-open-questions)
16. [Plan and doc changes when adopted; sources](#16-plan-and-doc-changes-when-adopted-sources)

---

## 0. Summary

1. **The protocol may not allow any of this on this unit.** The inverter is most likely on the Sorotec REVO platform.
   - Sorotec's protocol document defines no individual setters, only the all-in-one `PSET`. [CONFIRMED-primary, by absence, REVO §1.6]
   - The field report says RS232 setting writes NAK on REVO units. [SECONDARY FORUM]
   - **On REVO, Phase 2 is NO-GO with zero writes** (SR F13). The go/no-go decision is made first, from v1 data that already exists (§4.2).
2. **Remote control is defined per value, not per program** (SR F2, F4, F14). The remote-allowed set for this battery is:

   | Program | Remote-allowed after on-site acceptance | Local only (logger page + BOOT press) |
   |---|---|---|
   | 01 output priority `POP` | UtI always (also in Battery mode). SOL/SbU only if the load, SOC and grid checks pass and the battery data are recorded | – |
   | 02 max charging current `MCHGC`/`MNCHGC` | floor…cap from the battery's Ah and BMS rating (§3.3) | other manual values |
   | 11 max utility charging current `MUCHGC` | 10 A…cap | 2 A, values above the cap |
   | 16 charger priority `PCP` | CUt (HS map only) / CSO / SNU | OSO |
   | 18 alarm `PEa`/`PDa` | **on only** (`PEa`) | off (`PDa`) |
   | 19, 20, 22 flags `k x y` | both directions | – |

   - Programs 03, 06, 07, 23 and 25 are Tier B: local only, behind a second compile flag.
   - Programs 12 and 13 are Tier C on this lithium installation (SR §3).
   - Everything else is LCD only. **Tier C command strings are absent from every binary.**
3. **A window can only be opened physically**, with three BOOT presses (or an optional capacitive pad through the lid). The LAN page can only *stage* an action, which a press then confirms. This closes the replayed-Basic-auth path (SR F1).
   - The default window is **10 min and single use**. The longest is 30 min and allows up to 5 changes (the agreed "about 30 min").
4. **"Applied" means a whole-snapshot read-back changed exactly the expected field** (SR F16), confirmed again by follow-up reads at +60 s, +5 min and +10 min (SR F17, F19).
   - A write is never retried.
   - One change is in flight at a time, including its 10-min watch.
5. **Cloud compromise cannot write on its own, and cannot hide a write:**
   - the cloud can never open a window, set acceptance, raise a cap or switch the feature on;
   - remote changes need a fresh re-authentication;
   - the alert e-mail settings are pinned and checked before every remote execution;
   - notices go through channels configured on the logger (SR F5, F7, F9).
6. **Effort:**
   - **10.5–14.5 evenings** to the on-site go/no-go (P2-M5);
   - **21.5–29 evenings plus a 7-day soak** for the full remote feature (P2-M0…M10);
   - optional Tier B adds 1.5–2.
   
   The safety review added about 7–8 evenings over the draft (§12).

### 0.1 Decision records

| # | Decision | Rejected alternative | Why |
|---|---|---|---|
| P2-D1 | Separate build env `savx-rw`. `savx` stays default and read-only, with no setter code at all | One binary with a runtime flag | Keeps D11's guarantee for the default build (`docs/00` D11, d1 §3.10) |
| P2-D2 | Setter whitelist = a **generated table of complete command strings**, matched with `strcmp` | Regex or prefix rules (`MUCHGC\d{3}`) | Same exact-match philosophy as `Whitelist.cpp` (code). Tier C strings cannot exist in the binary |
| P2-D3 | **One request slot, keyed by id**: `ctl/in/req/{id}`, created only when `ctl/in/req` is empty | A fixed-name slot (draft) | The rules can enforce "empty parent" with `exists()`, and the device then deletes only the id it took (SR F22, errata 5) |
| P2-D4 | The window is RAM only, closed at boot, and **opened only by a physical gesture** | A LAN-openable or persisted window | A reboot can never leave control open. A replayed admin header cannot open it (SR F1) |
| P2-D5 | **Whole-snapshot read-back decides the result.** ACK is advisory | Trust the ACK, or check only the one field | Some ACKs may be dishonest; CRC checking on input is unknown (`docs/03` R5); a wrong index can hide a side effect (SR F16) |
| P2-D6 | **Never retry a write**; only reads are retried. The owner resubmits with a new id | Automatic retry | A lost ACK plus a retry is a double write (EEPROM wear, U17) |
| P2-D7 | **Battery-derived floors and caps** (§3.3). Remote values never exceed the value accepted on site | Manual range plus an "accepted value" envelope (draft) | The factory 60 A may already exceed 0.5C on a 100 Ah pack; low values are an outage attack (SR §1, F4) |
| P2-D8 | REVO: **zero writes**. `PSET` is never used | One same-value `PEx` probe (draft); read-modify-write PSET | The REVO document defines neither PE/PD nor QFLAG (SR F13); P2SET §3 gives six reasons against PSET |
| P2-D9 | Safe-direction actions are always available remotely: close, kill, revoke, and (inside a window) `pop`→UtI | Local only | Remote can make things safer, never less safe |
| P2-D10 | The read-only settings snapshot and LCD-change detection ship in **both** builds (v1.x), sourced from PQSE on REVO | rw build only | Useful without Phase 2, and the go/no-go needs it (SR F13, F16) |
| P2-D11 | **Per-(key, value) acceptance**, bound to an inverter fingerprint | Per-key acceptance (draft) | The read-back uses the encoder's own map, so only the LCD is an independent oracle (SR F2, F6) |
| P2-D12 | Remote notices go through **locally configured** channels (ntfy/Telegram in NVS); e-mail only while its settings match a pinned hash | Trust the cloud `alerts/email` | The same stolen account could silence the e-mail (SR F5) |

### 0.2 Corrections to earlier documents (found today; not fixed in those files)

1. **`QMCHGCR` / `QMUCHGCR` / `QBEQI` are not on the v1 whitelist**, contrary to P2SET §1.
   - The code removed them on purpose (`Whitelist.cpp`; `docs/02` §11.1 text after the table).
   - `research/d1-architecture.md` §3.10 still lists them.
   - Handling here: §3.6.
2. **CRC escape cases among the remote strings are `POP02` and `MCHGC090`**, not `MCHGC090` alone (draft §0.2; SR F11).
   - `POP02`: raw CRC 0xE20A → `E2 0B`.
   - `MCHGC090`: raw 0x0D8F → `0E 8F`.
   - `PBDV27.5` has a 0x00 byte (`00 C6`); it is Tier C here anyway.
   - P2SET §4's note that `PGR00` was adjusted is wrong: the raw CRC is already 0x29EB.
   - Recomputed today; the script reproduces the `docs/02` §4.3–4.4 vectors (QPI, QGMN, QBOOT, QLY2026, QPIGS). [CONFIRMED, own computation, scratchpad `p2final/crc.py`]
3. **`config.h` stops any write build** with `#error` when `INV_ALLOW_WRITES != 0`, and `[env:native]` hard-codes `-DINV_ALLOW_WRITES=0` (`platformio.ini` l.50). [CONFIRMED-primary (code)]
4. **Holding BOOT for 3 s opens the Wi-Fi portal** (`main.cpp` l.127, `cfg::BOOT_HOLD_MS` = 3000), and the button is inside the screwed clear lid (`docs/03` §11). [CONFIRMED-primary (code, docs)]
5. **`struct Pqse` does not parse `SSTUV` / `WW.W`**, and QFLAG is kept only as a raw string (`Pi30Parse.h`, `Identity::qflag`). [CONFIRMED-primary (code)]
6. **The dialect is a heuristic.** REVO is chosen only if QALL answered (`Pi30Adapter::classify` rule 1). MAX24 is chosen if QPIRI has ≥ 28 fields or QFLAG contains `d`; GK by a 36-character QPIWS or QMOD `E/C/Y/D`. [CONFIRMED-primary (code)]
7. **`RawRing` holds 16 entries** (`RawRing.h` `N = 16`). **`TimeSync` falls back to the inverter clock (QT)** before NTP (`TimeSync.h`). **Notification caps:** 40 pushes a day, above which only severity 5 is pushed; 40 mail jobs a day, the last 5 reserved for severity ≥ 4 (`NotifyGates.h`). [CONFIRMED-primary (code)]
8. **`events` is owner-writable at the `events` node** (`database.rules.json`), and `alerts/email/{on,to}` is owner-writable. [CONFIRMED-primary]

---

## 1. Safety-review disposition

Every finding was accepted, except where a row says otherwise.

| SR | Sev | Disposition | Where |
|---|---|---|---|
| F1 LAN admin replay = presence | High | **Accepted.** Opening a window, switching the feature on, setting acceptance, raising a cap, entering battery data, any local-only or Tier B write, and OTA all need a BOOT press within 60 s. The LAN page can only stage. OTA-with-press is a v1.x change and a gate (G9) before `savx-rw` is installed. The capacitive pad is accepted as an option, to be bench-tested first | §6, §4.1 G9 |
| F2 Per-key acceptance hides mapping errors | High | **Accepted.** Per-value `in`/`out` LCD bits; the PWA greys out unverified values | §3.5 |
| F3 `pop` → battery can black out the house | High | **Accepted.** Load, SOC, grid and warning checks; QFLAG `u/b/v` shown; `pop`→UtI allowed in Battery mode | §5.4.2 #14, §3.2 |
| F4 Low charging is an outage attack; OSO | High | **Accepted.** Floors, OSO and 2 A local only, a cross-key guard, ≤ 2 changes per key per window, no remote reversal within 15 min. **Refinement:** restoring a key to its accepted baseline is exempt from the reversal spacing (safe direction, P2-D9) | §3.3, §5.5 |
| F5 Account can blind the alerts | High | **Accepted.** A local channel is required; the e-mail settings hash is pinned and checked at window open and before each remote execution | §9.2 |
| F6 Single-shot dialect gate | High | **Accepted.** Fingerprint binding, explicit QALL/PQSE attempts, soak consistency. **Refinement:** a fingerprint change or a QALL/PQSE answer disables *control* (revoke all, close the window, sev-4 notice). It does not change v1's *logging* dialect logic, which Phase 2 must not touch | §4.3 |
| F7 Stolen phone | Medium | **Accepted.** Client step-up re-authentication; an `auth_time` rules term (verified in the emulator, else client-only); default window 10 min, single use; maximum 30 min | §6.1, §7.4, §8 |
| F8 Compromised PWA shows the wrong value | Low | **Accepted** as invariant I6 | §10 |
| F9 "Cloud cannot open the window" untested | Medium | **Accepted** as invariant I1, with CI grep and a native test | §10, §11.1 |
| F10 Pre-planted stale request | Medium | **Accepted.** On window open, read `ctl/in` (honour `kill`), delete `ctl/in/req`, then PUT the window | §6.1 |
| F11 POP02 escape; PBDV27.5 zero byte | Low–Med | **Accepted and recomputed today.** The QLD/QED escape dates were also recomputed (§4.4) | §3.7, §4.4 |
| F12 Input-CRC checking unknown | Medium | **Accepted.** One-time read-only CRC probe before the first write | §4.4 |
| F13 REVO branch sends a write | Medium | **Accepted.** REVO is NO-GO with zero writes | §4.2 |
| F14 Remote mute | Medium | **Accepted.** `PDa` is local only | §3.2 |
| F15 NAK/ACK misread | Low | **Accepted.** Exact reply bytes; a late-reply test | §5.4.4, §11.1 |
| F16 Wrong read-back field | Medium | **Accepted.** Whole-snapshot diff with a catalog coupling list; `failed/sidefx`; REVO values come from PQSE | §5.4.5 |
| F17 LCD race, stale view | Medium | **Accepted.** CAS on `exp`, watch at +60 s/+5/+10 min, `SETTING_REVERTED`. **Addition:** one change in flight including its watch | §5.4 |
| F18 Truncated frame on reset | Medium | **Accepted.** Full-snapshot `inflt`, 24 h lockout after brown-out/panic/WDT, COMMIT-or-reject, audit moved to LittleFS | §5.4.6, §5.6 |
| F19 Late apply reported as failed | Medium | **Accepted.** Interim `unverified`, final `late` or `failed` | §5.4.5 |
| F20 Narrow mode/warning checks | Medium | **Accepted**, implemented as an **allow-list of tolerated QPIWS bits**, which is stricter than the review's deny-list. Battery mode needs SOC ≥ 50 % | §5.4.2 #13 |
| F21 Inverter clock accepted | Low | **Accepted.** NTP only, synced < 24 h; `cts` + server `sts`; the window carries server `opened` + `dur`. **Refinement:** the window document is written only at open and at close, so a later patch cannot refresh the server timestamp | §5.4.2 #3, §7 |
| F22 Take race, stuck pending | Low–Med | **Accepted.** Slot keyed by id; "take unknown" handling; the PWA times out at 60 s. **Addition:** more than one request present → reject all (closes the multi-path loophole the rules cannot count) | §7.3 |
| F23 Editable cloud audit | Low | **Partly accepted.** The authoritative history is `ctl/res` (device-only) plus the device audit ring; the PWA history reads only `ctl/res`. **Deferred:** narrowing the owner's write on `events` to `ack`. The owner grant sits on the `events` node, and RTDB grants cannot be narrowed below a granting node, so it needs a v1 rules restructure that must first be checked against the PWA's ack writes. Listed as a v1.x item (§16) | §7.5, §16 |
| F24 Mutable slot | Low | **Accepted.** Create-only (`!data.exists()`), freshness in `.write`, id prefix = `cts`, child-path and merged-file emulator cases | §7.4, §11.2 |
| F25 Kill latency 60 s | Low | **Accepted.** `ctl/in/{kill,close}` is read every 5 s while a window is open. `cfg/ctlOff` is kept for the closed-window case; the PWA writes both in one update | §7.3 |
| F26 ctl notices use up the alert budget | Medium | **Accepted.** Separate ctl budget of 15 mails a day; 3 of the 40 v1 slots kept for severity 5 only; forced ctl pushes do not count toward the 40 | §9.3 |
| F27 Midnight reset of limits | Low | **Accepted.** Rolling 24 h ring | §5.5 |
| F28 Unmeasurable M8 check | Low | **Accepted.** Persisted frame counter `status.sfr` (the status rule allows keys ≤ 8 characters) | §5.6, §12 |
| F29 UI ambiguity | Medium | **Accepted.** All items | §8 |
| SR §1 Safe ranges | – | **Accepted.** Formulas; gated on site-survey §9 plus a new BMS row | §3.3 |
| SR §3 Tier moves | – | **Accepted**, except `fz` → A, which is **rejected**: restoring fault recording has no remote use, and every remote value costs an on-site acceptance. Tier A stays minimal | §3.2 |
| SR §2 H1–H9 tests | – | **Accepted.** H1's power-cycle check is mandatory for `pop`, `pcp`, `mchg` and `muchg`, once per key | §11 |
| SR §5 errata 1–8 | – | **Accepted** | §0.2 and throughout |

---

## 2. Master command table (programs 00–50)

Condensed from P2SET §2, with the final tier **for this installation** (24 V lithium battery, probable REVO platform). Dialect key: STOCK = P14 + P15 (STOCK17/STOCK21 in `Dialect.h`); MAX = PMAX (MAX24); GK = PGK; REVO = Sorotec. Value encodings and read-back fields are [CONFIRMED-primary] in the cited sections unless tagged.

**Tier codes:**
- **R**: remote-allowed values after acceptance.
- **L**: local only (logger page + BOOT press).
- **B**: Tier B key, local only, compiled only with `INV_WRITE_TIER_B=1`.
- **C**: never via the logger.
- **LCD**: no command known.

| Prog | LCD options (24 V) [MAN] | Command, by dialect | Read-back | Final tier here | Notes |
|---|---|---|---|---|---|
| 00 | ESC | – | – | LCD | |
| **01** Output source priority | UtI / SOL / SbU | `POP00` / `POP01` / `POP02` (P14 §3.4, P15 §3.8, PMAX §3.9, PGK). REVO: PSET only | QPIRI[16]; REVO PQSE C | **R** (UtI any mode; SOL/SbU after the F3 checks) | Never `POP03` (ESPH "hybrid", undocumented) [SECONDARY] |
| **02** Max charging current | 10–100 A step 10, default 60 | STOCK: `MCHGC0nn` (P14 `<mnn>` with m = 0 equals P15 `<nnn>`). MAX: `MNCHGC0nnn`. GK: `MNCHGCnnn` (blocked until U5) | QPIRI[14]; QMCHGCR list | **R** floor…cap; **L** otherwise | STOCK capped at 90 A: P14 reads `MCHGC100` as "unit 1, 00 A" [INFERRED] |
| **03** AC input range | APL / UPS | `PGR00` / `PGR01` | QPIRI[15] | **B** | Transfer thresholds and transfer time change |
| **05** Battery type | AGn / FLd / USE / LIb | `PBT<NN>` (code sets differ; LIb code unknown) | QPIRI[12] | **C** | Over- or under-charge risk |
| **06** Restart after overload | LtD / LtE | `PDu` / `PEu` | QFLAG `u` | **B**; **C on MAX24** | PMAX: `u` also means "battery over discharge restart" (SR §3) |
| **07** Restart after over-temperature | tFd / tFE | `PDv` / `PEv` | QFLAG `v` | **B** | |
| **09** Output frequency | 50 / 60 Hz | `F50` / `F60` | QPIRI[3] | **C** | Can damage loads |
| **10** Output voltage | 220 / 230 / 240 | `V220`… (PMAX §3.8, PGK only) | QPIRI[2] | **C** | |
| **11** Max utility charging current | 2, 10–80 A, default 30 | `MUCHGC002`, `MUCHGC010`…`MUCHGC080` (same bytes in P14/P15/PGK/PMAX); LV6548 4-digit form not used [SECONDARY] | QPIRI[13]; QMUCHGCR | **R** 10…cap; **L** 2 A and above the cap | If 02 < 11, 02 is used [MAN p.14] |
| **12** Back to utility | 22.0–25.5 V, or SOC 10–80 % (lithium) | `PBCV<nn.n>`; **no SOC command** | QPIRI[8] (REVO relabels it) | **C here** | Lithium → SOC; QPIRI encoding unknown (U9) (SR §3) |
| **13** Back to battery | 24.0–29.0 V / FUL, or SOC 50–100 % | `PBDV<nn.n>`, `PBDV00.0` = FUL; **no SOC command** | QPIRI[22] | **C here** | As 12 |
| **16** Charger source priority | CUt / CSO / SNU / OSO | `PCP00`…`PCP03`. Code set per dialect: HS 00–03; MS 00/01/03; MAX/GK 01–03 | QPIRI[17] | **R** CUt (HS map only) / CSO / SNU; **L** OSO | The wire↔LCD order is unproven until U7 (`docs/01` §4.7). Never `PPCP` |
| **18** Alarm | bON / bOF | `PEa` / `PDa` | QFLAG `a`; REVO PQSE S | **R** on (`PEa`); **L** off (`PDa`) | Polarity proven on site via the mute icon [MAN p.12] (SR F14, H8) |
| **19** Return to default screen | ESP / HEP | `PEk` / `PDk` | QFLAG `k` | **R** | Display only |
| **20** Backlight | LON / LOF | `PEx` / `PDx` | QFLAG `x` | **R** | Visible test |
| **22** Beep on source interruption | AON / AOF | `PEy` / `PDy` | QFLAG `y` | **R** (with a notice) | |
| **23** Overload bypass | byd / byE | `PEb` / `PDb` | QFLAG `b` | **B** | |
| **25** Record fault code | FEn / FdS | `PEz` only (`enableOnly`) | QFLAG `z` | **B** (enable only) | SR's "may be A" rejected (§1) |
| **26 / 27** Bulk / float | 25.0–31.5 V | `PCVV<nn.n>` / `PBFT<nn.n>` (no space) | QPIRI[10] / [11] | **C** | Battery damage |
| **29** Low DC cut-off | 21.0–26.0 V, or SOC 5–50 % | `PSDV<nn.n>`; no SOC command | QPIRI[9] | **C** | |
| **30–36** Equalization | – | `PBEQE/V/T/OT/P/A` (PMAX, PGK only) | QBEQI (not whitelisted) | **C** | Wrong for lithium |
| **40** Discharge current limit | OFF / 10–200 A | none for this unit (PMAX 48 V `PBATMAXDISC` only) | – | **LCD** | |
| **42** EARTH LED adjust | −30…30 | none known | – | **LCD** | |
| **43** Solar feed to grid | GTd / GTE | PMAX `PEd`/`PDd` ("reserved feature") | QFLAG `d` | **C** | |
| **44** Reconnection delay; **45** zero export; **50** battery activation | – | none known | – | **LCD** | |
| 04, 08, 14, 15, 17, 21, 24, 28, 32, 37–39, 41, 46–49 | not in the manual | – | – | – | |

**Never sent, no LCD program** (all **C**; P2SET §2; `docs/02` §11.2):
- `PF`, `PSET`, `~S`, `PSAVE`;
- `PEj`/`PDj`, `PCVT`, `PCST`, `MSCHGC`;
- `POPM`, `PPCP`, `POPLG`, `PPVOKC`, `PSPB`;
- `DAT`, `RTEY`, `RTDL`, `PBATCD`, `PBATMAXDISC`;
- `BTA*`, `ATE0`/`ATE1`, `PLED*`, `PBMS`.

**Rules that apply to every setter** (P2SET §1):
- Frame = ASCII + CRC-16/XMODEM with the Voltronic adjust (high byte first) + CR (`docs/02` §4).
- Voltages are `nn.n` with no space.
- Reply is `(ACK<CRC><cr>` or `(NAK[<CRC>]<cr>`; `(ACK` CRC = `39 20`, `(NAK` CRC = `73 73`.
- One lowercase flag letter per frame (U3).
- No document states a mode condition for any Tier A setter; the manual limits only 42 and 45 to Line mode. [CONFIRMED-primary, by absence; MAN pp. 14–20]

---

## 3. Settings catalog

### 3.1 Single source and generator

- **Source:** `shared/settings-catalog.json`.
- **Generator:** `tools/gen_settings.py`, following the r8 pattern for `alert-metrics.json`. It emits:
  - `firmware/include/settings_catalog.h`: entries, per-dialect string tables, the generated setter table, coupling list, escape flags and `CTL_CATALOG_VERSION` (= 2 for this design);
  - `web/src/settingsCatalog.ts`: labels, LCD texts, units, consequence texts (§8.3), value classes;
  - `--check`: the `key`/`val` validation in `database.rules.json` (§7.4) equals the catalog's remote-allowed coarse ranges, and the TS file is current. CI runs it.
- **Canonical values are LCD semantics, never wire codes.** For example `pcp` 0 = CUt, 1 = CSO, 2 = SNU, 3 = OSO. Per-dialect tables map them to wire strings, and the PWA and RTDB never see dialect codes. **Scaling is pinned per key in the catalog and tested:** currents in A; flags 0/1; enums as above. [INFERRED design]

### 3.2 Keys and value classes

| id | Prog | Canonical values | Remote class **R** (after acceptance) | Local only **L** | Read-back | Modes for any write |
|---|---|---|---|---|---|---|
| `pop` | 01 | 0 UtI, 1 SOL, 2 SbU | 0 always; 1, 2 only with the §5.4.2 #14 checks and battery data present | – | QPIRI[16] | 0: L, S, B (B only if SOC ≥ 50 %). 1, 2: L, S |
| `mchg` | 02 | 10…90 (STOCK), 10…100 (MAX24) step 10 | floor…cap (§3.3) | other manual values. Above the cap needs `RAISE` + press | QPIRI[14] | L, S |
| `muchg` | 11 | 2, 10…80 step 10 | 10…cap, and ≤ current `mchg` | 2; above the cap | QPIRI[13] | L, S |
| `pcp` | 16 | 0 CUt, 1 CSO, 2 SNU, 3 OSO | 0 (only if `pcpMap = HS`), 1, 2 | 3 | QPIRI[17] via `pcpMap` | L, S |
| `fa` | 18 | 0 bOF, 1 bON | 1 | 0 | QFLAG `a` | L, S, B |
| `fk` | 19 | 0 HEP, 1 ESP | 0, 1 | – | QFLAG `k` | L, S, B |
| `fx` | 20 | 0 LOF, 1 LON | 0, 1 | – | QFLAG `x` | L, S, B |
| `fy` | 22 | 0 AOF, 1 AON | 0, 1 | – | QFLAG `y` | L, S, B |

**Tier B keys** (local only, `INV_WRITE_TIER_B=1`, P2-M11):

| id | Prog | Values | Command | Notes |
|---|---|---|---|---|
| `pgr` | 03 | 0 APL, 1 UPS | `PGR00` / `PGR01` | NAK may come without a CRC |
| `fu` | 06 | 0 LtD, 1 LtE | `PDu` / `PEu` | **Not generated for MAX24** (Tier C there) |
| `fv` | 07 | 0, 1 | `PDv` / `PEv` | |
| `fb` | 23 | 0, 1 | `PDb` / `PEb` | |
| `fz` | 25 | 1 only | `PEz` | `PDz` is never generated |

**Mode letters** are QMOD `L` (Line), `S` (Standby) and `B` (Battery). No write is allowed in `F` (Fault), `P` (Power-on) or `H` (Power saving). Battery mode always needs SOC ≥ 50 % (SR F20).

**Cross-key guards (remote):**
- `muchg ≤ mchg`;
- refuse `pop` = SbU while `pcp` = OSO, and refuse `pcp` = OSO while `pop` = SbU (SR F4).

The rules allow the coarse ranges only (§7.4); the device enforces the rest.

### 3.3 Battery-derived floors and caps (SR §1)

**Inputs** (NVS `ctl/env.bat`, entered on the local page with staging + press, and copied to `docs/site-survey.md` §9 plus a new BMS row):
- capacity **Ah**;
- BMS continuous charge current **Ic**;
- BMS continuous discharge current **Id**.

**Formulas** [INFERRED, from general LiFePO4 datasheet practice, SECONDARY; must be replaced by the label or datasheet values]:

| Key | Remote floor | Remote cap |
|---|---|---|
| `mchg` | max(20 A, 0.2C) rounded **up** to 10 A | min(baseline, 0.5C, 0.8 × Ic, dialect max) rounded **down** to 10 A. Dialect max: STOCK 90; MAX24 100; GK n/a until U5 |
| `muchg` | 10 A | min(`mchg` cap, baseline), rounded down to 10 A |

- **baseline** = the value read from the inverter when the key was accepted (`env.base`).
- If cap < floor, the key has no remote values.

| Example | `mchg` remote | `muchg` remote |
|---|---|---|
| 100 Ah, 100 A BMS, baseline 60 / 30 | 20–50 A | 10–30 A |
| 200 Ah, 150 A BMS, baseline 90 / 30 (STOCK) | 40–90 A | 10–30 A |

**Before `env.bat` exists:**
- `mchg`, `muchg` and `pop` = SOL/SbU cannot become remote-allowed.
- On the local page, `mchg` and `muchg` are **decrease-only and never below 50 % of the baseline**, rounded up to the step.

**Read-only observation for the owner** (SR §1, independent of Phase 2):
- On a 100 Ah pack, the factory 60 A is already 0.6C.
- A 3.2 kW full load at 24 V is about 133 A, more than a 100 A BMS rating.

### 3.4 Per-dialect string tables (exact strings only)

- Encoders never format numbers. They **look up** the canonical value in a generated per-dialect table and return `nullptr` for anything else.
- `pi30::buildCrcFrame` adds the CRC and CR.

| Key | STOCK17 / STOCK21 | MAX24 | GK | REVO |
|---|---|---|---|---|
| `pop` | `POP00` `POP01` `POP02` | same | same | – |
| `mchg` | `MCHGC010`…`MCHGC090` | `MNCHGC0010`…`MNCHGC0100` | none until U5 (`MNCHGC010`…; "KING" `<mnnn>`) | – |
| `muchg` | `MUCHGC002`, `MUCHGC010`…`MUCHGC080` | same | same | – |
| `pcp` | `pcpMap = HS`: 0→`PCP00`, 1→`PCP01`, 2→`PCP02`, 3→`PCP03`. `pcpMap = MAXGK`: 1→`PCP01`, 2→`PCP02`, 3→`PCP03`. `none`: no strings | same mapping by `pcpMap` | same | – |
| flags | `PEa` `PDa` `PEk` `PDk` `PEx` `PDx` `PEy` `PDy` (+ Tier B `PEu` `PDu` `PEv` `PDv` `PEb` `PDb` `PEz`) | same; **no `PDu`/`PEu`** | same | – |

- 43 unique Tier A strings across dialects (GK `mchg` excluded), plus 9 Tier B (`PGR00`, `PGR01` and seven flag strings).
- **REVO never gets a string** (P2-D8).
- `pcpMap` is set during acceptance from U7, by comparing the LCD with QPIRI[17] (§11.6). The PWA shows `pcp` as unavailable while it is `none`.

### 3.5 Acceptance state (per value, bound to a fingerprint) (SR F2, F6)

NVS blob `ctl/acc`:

```
fp      16 hex chars   fingerprint at acceptance (§4.3); any mismatch clears every bit below
per key:
  pc    1 bit          inverter power-cycle persistence verified for this key (required: pop, pcp, mchg, muchg)
  rem   1 bit          one REMOTE change of this key verified by the owner at the LCD
  per value v (index into the catalog value list, ≤ 16):
    in  1 bit          a LOCAL write baseline→v was read back AND seen on the LCD
    out 1 bit          a LOCAL write v→baseline was read back AND seen on the LCD
```

**A remote request for (k, v) is allowed only if all of these hold:**
- v is in class R and inside the current floor/cap;
- `in` and `out` are set for v (the baseline counts as verified once any `out` is set);
- `rem` is set for k, except during the single owner-at-LCD remote test that sets it;
- `pc` is set if k requires it;
- `fp` matches.

**Escape-flagged strings** (`POP02`, `MCHGC090`; §3.7) need their own `in`/`out` like every other value. The flag is shown on the acceptance page so the owner knows these are the strings that test the 0x0A and 0x0D request escapes (SR F11).

**Revocation:**
- `failed`, `late`, `sidefx` or a revert clears `rem` for the key and `in`/`out` for the involved value.
- A fingerprint change clears everything.

### 3.6 `QMCHGCR` / `QMUCHGCR` (allowed-value lists)

- They are **not** re-added to the v1 whitelist or the scheduler (§0.2 item 1).
- The rw build may send each **once per acceptance session, from the local page with a press, with the owner present**. The parsed list (`(010 020 …`, P14 §2.11–2.12) is stored in `env.allow` and intersected with floor…cap.
- A NAK or timeout means "use the manual range".

### 3.7 Byte vectors (recomputed today with the `docs/02` §4 algorithm)

| Command | Wire bytes | Command | Wire bytes |
|---|---|---|---|
| `POP00` | `50 4F 50 30 30 C2 48 0D` | `PCP00` | `50 43 50 30 30 8D 7A 0D` |
| `POP01` | `… 30 31 D2 69 0D` | `PCP01` | `… 30 31 9D 5B 0D` |
| `POP02` | `… 30 32 E2 0B 0D` (**escape**: raw 0xE20A) | `PCP02` / `PCP03` | `… AD 38 0D` / `… BD 19 0D` |
| `MCHGC010`…`080` | `84 26`, `D1 75`, `E2 44`, `7B D3`, `48 E2`, `1D B1`, `2E 80`, `3E BE` | `MCHGC090` | `… 0E 8F 0D` (**escape**: raw 0x0D8F) |
| `MNCHGC0010`…`0100` (MAX24) | `12 3B`, `47 68`, `74 59`, `ED CE`, `DE FF`, `8B AC`, `B8 9D`, `A8 A3`, `9B 92`, `16 3A` | `MNCHGC060` (GK reference) | `… D4 2E 0D` |
| `MUCHGC002` | `4D 55 43 48 47 43 30 30 32 B5 D1 0D` | `MUCHGC010`…`080` | `A6 A2`, `F3 F1`, `C0 C0`, `59 57`, `6A 66`, `3F 35`, `0C 04`, `1C 3A` |
| `PEa` / `PDa` | `50 45 61 D0 70 0D` / `50 44 61 E3 41 0D` | `PEx` / `PDx` | `50 45 78 53 68 0D` / `50 44 78 60 59 0D` |
| `PEk` / `PDk` | `… 71 3A 0D` / `… 42 0B 0D` | `PEy` / `PDy` | `… 43 49 0D` / `… 70 78 0D` |
| `PEu` / `PDu` | `82 C5` / `B1 F4` | `PEv` / `PDv` | `B2 A6` / `81 97` |
| `PEb` / `PDb` | `E0 13` / `D3 22` | `PEz` | `73 2A` |
| `PGR00` / `PGR01` | `50 47 52 30 30 29 EB 0D` / `… 39 CA 0D` (no adjust) | `(ACK` / `(NAK` | `28 41 43 4B 39 20 0D` / `28 4E 41 4B 73 73 0D` |

- The pairs give the CRC high and low bytes, which come right before the closing `0D`.
- Tier C reference only: `PBDV27.5` → `00 C6` (zero byte).
- [CONFIRMED, own computation, scratchpad `p2final/crc.py`; its self-test reproduces every `docs/02` §4.3–4.4 vector]

---

## 4. Rollout gates and the go/no-go decision

### 4.1 Hard preconditions (all required before `savx-rw` is installed on the inverter)

| # | Gate | Evidence |
|---|---|---|
| G1 | v1 **M0–M3 accepted**. Cloud milestones (P2-M7 and later) also need **M5–M7** | `docs/07` acceptance boxes |
| G2 | v1 **M8 soak passed**: 7 days, ≥ 99.5 % of minutes, link errors < 1 %, zero false severity-5 alerts | `docs/07` M8 |
| G3 | **Dialect and fingerprint stable**: `docs/probe-report.json`; the same dialect on every probe during the soak; `meta.sup` lists which optional reads answer (QFLAG in particular) | §4.3 |
| G4 | **LCD ↔ QPIRI/QFLAG mapping** recorded for QPIRI fields 8, 13, 14, 15, 16, 17, 22 and QFLAG letters a, k, x, y, u, b, v (P2SET U7, U8) | `docs/site-survey.md` B6 |
| G5 | **A dated photo and table of every LCD program value**, including **06, 23, 40 and 50** (SR §4) | `docs/site-survey.md` §8 |
| G6 | **Battery data:** `docs/site-survey.md` §9 complete, plus a new row for BMS continuous charge and discharge ratings. Without it, §3.3 applies (no remote currents, no remote SOL/SbU) | site survey |
| G7 | Firmware unchanged since the soak apart from Phase 2 code. `pio test -e native` and `-e native-rw` pass. CI string and symbol checks pass (§11.3) | CI |
| G8 | The owner has read §13 (rollback) and accepts residual risks R-P2-3 and R-P2-11 (§14) | sign-off line in the build record |
| G9 | **v1.x hardening is installed (P2-M1):** OTA needs a BOOT press; the settings snapshot uses PQSE on REVO; the fingerprint is published | P2-M1 acceptance |
| G10 | **At least one local push channel** (ntfy or Telegram, NVS `alert`) configured, with a test push confirmed by the owner | §9.2 |
| G11 | **CRC probe done** (§4.4). If the inverter ignores the input CRC: link errors < 0.1 % for 7 days | §4.4 |
| G12 | **Per value:** acceptance bits as in §3.5 | NVS `ctl/acc` |

### 4.2 Go/no-go (zero writes first)

```
probe-report dialect + 3 explicit QALL/PQSE attempts (CRC and CHK framing, §4.3)
├─ REVO, or ANY QALL/PQSE answer ever seen ──► NO-GO, zero writes (SR F13). Keep the read-only
│                                              snapshot from PQSE (P2-D10). Stop Phase 2 unless Sorotec
│                                              documents an individual setter in writing.
├─ UNKNOWN / RAW / non-PI30 ─────────────────► NO-GO
├─ QFLAG not answering (meta.sup) ───────────► flags have no read-back → no flag keys; continue only if
│                                              a QPIRI key is wanted (pop), else NO-GO
└─ STOCK17 / STOCK21 / MAX24 / GK, fingerprint stable for 7 days
       └─► P2-M1…M4 (code, bench) ─► CRC probe (§4.4, read-only)
              └─► P2-M5: same-value PEa (buzzer already on) ─► then local PDa/PEa toggle (mute icon)
                     ACK + whole snapshot unchanged + LCD unchanged ─► GO (flags)
                     NAK on the same-value AND the toggle ─────────► NO-GO (setters not accepted)
                     anything else (side effect, unexpected) ─────► NO-GO, re-flash savx, record
```

- A **same-value write** re-sends the value just read, so an ACK proves acceptance while nothing changes (P2SET §5).
- The buzzer is the first write. `PEa` with the alarm already on is the *enable* direction (the safe direction), and the LCD mute icon [MAN p.12] then gives an independent polarity check for the toggle (SR F2, H8). This is only valid if G4 showed QFLAG `a` in the E list while the LCD shows bON.
- **Fallback:** if G4 is ambiguous for `a`, start with the backlight `x` instead.

### 4.3 Fingerprint (SR F6)

- **Definition:** `fp` = first 16 hex characters of SHA-256 over `dialect | QPI | QVFW | QVFW2 | QMN | QID`, from the identity the probe already reads.
  - Missing optional replies hash as empty.
  - Published as `meta.fp` (both builds, v1.x) and `settings.fp`.
- **Before acceptance:**
  - three explicit `QALL` and `PQSE` attempts, each in CRC and CHK framing, all failing (exact-match reads already on the whitelist);
  - the same dialect and `fp` on every probe during the soak.
- **Afterwards**, any of these means *control* treats the unit as REVO: a different `fp`, a probe yielding another dialect, or **any** QALL/PQSE answer. The device then:
  - clears `ctl/acc`;
  - closes the window;
  - refuses all writes;
  - raises `CTL_FEATURE` at sev 4, "inverter identity changed".
- v1's logging dialect logic is not changed.

### 4.4 Read-only probes before the first write

- **CRC probe** (SR F12; rw build, local page, staging + press, once):
  - `51 50 49 BE AD 0D`: `QPI` with the CRC low byte off by one.
  - `51 50 49 0D`: `QPI` with no CRC.
  - Both are fixed frames in `SerialLink::crcProbe()` (rw build only); they are reads.
  - If both answer `(PI30…`, the inverter **ignores the input CRC**. Raise R-P2-2 to Medium likelihood and require link errors < 0.1 % for 7 days (G11).
  - If either NAKs or is ignored, record "input CRC checked".
  - Result stored in NVS `ctl/crcp` and shown in `settings`.
- **Escape evidence for free** (SR F11):
  - `QGMN` is the only v1 request with an escaped CRC (low byte 0x28). The 0x0A and 0x0D escapes in requests are unproven.
  - If the unit answers QLD/QED, these v1 requests need escapes: `QLD20261216` (raw 0xFB0A), `QLD20261219` (0x0AE5), `QLD20270102` (0x0D0A) and `QED20270129` (0xFF0D) (recomputed today).
  - Compare their answer rate with the neighbouring days in the v1 logs. A miss is a warning sign for `POP02` and `MCHGC090`, which get their own acceptance anyway.

---

## 5. Firmware

### 5.1 Build environments and the compile guard

```ini
; Phase 2 (research/r9 §5.1). NEVER in default_envs. Flash only after the §4.1 gates.
[env:savx-rw]
extends = env:savx
build_unflags = ${env:savx.build_unflags} -DINV_ALLOW_WRITES=0
build_flags =
  ${env:savx.build_flags}
  -DINV_ALLOW_WRITES=1
  -DINV_RW_CATALOG=2          ; must equal the generated CTL_CATALOG_VERSION
  -DINV_WRITE_TIER_B=0        ; Tier B keys: P2-M11 only

[env:savx-rw-bench]           ; P2-M4 only: fake inverter on UART2, RAM-only link state
extends = env:savx-rw
build_flags = ${env:savx-rw.build_flags} -DBENCH_FAKE_INV=1

[env:native-rw]
extends = env:native
build_unflags = -DINV_ALLOW_WRITES=0
build_flags = ${env:native.build_flags} -DINV_ALLOW_WRITES=1 -DINV_RW_CATALOG=2 -DINV_WRITE_TIER_B=1
build_src_filter = ${env:native.build_src_filter} +<ctl/> +<link/SetterWhitelist.cpp>
```

- [INFERRED] Check the PlatformIO flag merge with `pio run -e savx-rw -v`: only `=1` may reach the compiler.
- `savx-rw-bench` must refuse to run with a real link arming: it uses RAM-only link state and never writes NVS `link`.

The `config.h` guard becomes:

```cpp
#if INV_ALLOW_WRITES != 0
#  if !defined(INV_RW_CATALOG) || INV_RW_CATALOG != CTL_CATALOG_VERSION
#    error "Writes need env savx-rw and a matching settings catalog (research/r9 §5.1)"
#  endif
#  define FW_VARIANT "rw"
#else
#  define FW_VARIANT "ro"
#  if defined(INV_WRITE_TIER_B) && INV_WRITE_TIER_B
#    error "INV_WRITE_TIER_B needs INV_ALLOW_WRITES=1"
#  endif
#endif
```

- `FW_VARIANT` is published as `meta.fwvar` (`meta` keys ≤ 12 characters are allowed) and in `/api/status`.
  - The PWA shows the controls only when `fwvar === "rw"`.
  - The local page shows a permanent "rw build" badge.
- Every Phase 2 source file is wrapped in `#if INV_ALLOW_WRITES`: `src/ctl/*`, `link/SetterWhitelist.*`, `SerialLink::writeSetting`, `SerialLink::crcProbe`.
  - The `savx` binary has **no setter string, no write symbol and no write path** (§11.3).

### 5.2 Setter whitelist and write path

```cpp
// link/SetterWhitelist.h - compiled only when INV_ALLOW_WRITES == 1. Host-portable.
namespace wlw {
struct SetterEntry { const char* cmd; uint8_t dialects; uint8_t tier; uint8_t key; uint8_t valIdx; bool escaped; };
extern const SetterEntry SETTERS[];   // generated (settings_catalog.h)
extern const size_t SETTERS_N;
const SetterEntry* setterAllowed(const char* cmd, Dialect d, bool tierBAllowed);    // exact strcmp; never REVO
bool setterFrameAllowed(const uint8_t* f, size_t n, Dialect d, bool tierBAllowed);  // re-parse built frame
}
// link/SerialLink.h addition
#if INV_ALLOW_WRITES
class WriteToken { friend class CommandExecutor; WriteToken() {} };   // only the executor can mint one
LinkResult writeSetting(const WriteToken&, const char* cmd, Dialect d, bool tierB, Reply& out, uint32_t firstByteMs);
LinkResult crcProbe(const WriteToken&, uint8_t which, Reply& out);    // the two fixed QPI frames of §4.4
#endif
```

**Gate order inside `writeSetting`:**
1. Compile flag.
2. Token.
3. `setterAllowed`.
4. Link armed and configured.
5. Build the frame.
6. `setterFrameAllowed`: printable ASCII, valid CRC, exact table match.
7. One `transact`, with no retry inside. (The persisted frame counter `sfr` was already committed in the executor's COMMIT step, §5.4.3.)
8. Ring-log with the `SET` tag.

`SerialLink::query()` is unchanged and still refuses every setter. The existing tests ("bad" list; "setters refused with zero bytes") run in both builds. [CONFIRMED-primary (code)]

### 5.3 Module map

| Module | Change |
|---|---|
| `src/ctl/SettingsCatalog.{h,cpp}` (new) | Entries from the generated header; value classes; floor/cap; encoders |
| `src/ctl/CommandExecutor.{h,cpp}` (new) | §5.4; dependencies injected (link, clock, persist, state view, uplink checks) so it is native-testable |
| `src/ctl/CtlWindow.{h,cpp}` (new) | §6; gesture decoder, staging, expiry; pure logic |
| `src/ctl/Snapshot.{h,cpp}` (new, **both builds**) | Canonical snapshot from QPIRI + QFLAG (stock) or PQSE (REVO); hash; diff with the coupling list |
| `src/link/SetterWhitelist.*` (new), `SerialLink` (changed) | §5.2 |
| `src/app/App.*` | `postCtl`/`takeCtl`, results, `ctlState()` |
| `src/app/Tasks.cpp` `invTask` | `while (app.takeCtl(rq)) executor.run(rq)` between ticks; follow-up scheduling; boot recovery |
| `src/main.cpp` | Gesture decoder next to the 3 s hold; LED modes; `esp_reset_reason()` lockout |
| `src/sys/StatusLed` | `CTL_WINDOW`, `CTL_STAGED`, `CTL_WRITE` |
| `src/net/Uplink.cpp` | "ctl" step between alerts (1) and live (2): GET `ctl/in` every 5 s **only while a window is open**; take, result, window and `wstat` PATCHes; `alerts/email` pin check; `cfg.ctlOff` |
| `src/net/UplinkJson.cpp` | Strict parsers and serialisers (unknown field → reject) |
| `src/web/LocalWeb.cpp` | `/api/ctl*` (§6.3); `needsAdmin()` gains `/api/ctl`; **OTA needs a press (both builds, v1.x)** |
| `src/agg/AlertEngine`, `NotifyGates` | `SETTING_*`/`CTL_*` types; separate ctl budget (§9.3) |
| `src/proto/pi30/Pi30Parse` | Parsed QFLAG struct; PQSE `SSTUV` / `WW.W`, both layouts (both builds) |
| `data/www/index.html` | "Inverter settings" section |

### 5.4 CommandExecutor

The executor runs inside `invTask`, the only owner of `SerialLink` (`Tasks.cpp`). It is **single-flight**: one change at a time, including its 10-min follow-up watch (§5.4.7). Requests that arrive in the meantime are rejected `busy`. The exceptions are close, kill and a restore of the same key to its baseline.

#### 5.4.1 Request

```cpp
struct CtlRequest {
  char id[20];      // "<cts>-<8 [a-z0-9]>"
  char wid[9];      // window id
  char key[7];      // catalog id
  int16_t val;      // canonical
  int16_t exp;      // value the user saw (CAS, SR F17)
  uint32_t cts;     // client click time, epoch s (server-offset corrected)
  uint64_t stsMs;   // server timestamp of the request write (REMOTE only)
  uint8_t origin;   // REMOTE | LOCAL
  bool force;       // LOCAL acceptance only: same-value write
};
```

#### 5.4.2 Pre-checks, in order (if any fails: `rejected`, zero frames)

| # | Check | `why` (≤ 12) |
|---|---|---|
| 1 | Feature on (`ctl/feat`) and no lockout (`ctl/lock` in the past) | `feature` / `lock` |
| 2 | Window open, `wid` equal, not expired, single-use not consumed, per-window change count not exceeded | `window` |
| 3 | `timeSync.source()` = NTP, last sync < 24 h (SR F21). REMOTE: `now − sts ≤ 120 s`, `sts ≤ now + 60 s`, `|sts − cts·1000| < 30 s`. LOCAL: request age ≤ 120 s | `time` / `expired` |
| 4 | `id` not in the persisted ring; `cts > lastTs` | `dup` |
| 5 | Key known; catalog version matches | `key` |
| 6 | Value class allowed for the origin: REMOTE → R only; LOCAL → R or L; Tier B keys only if compiled and staged | `tier` |
| 7 | Dialect supported (never REVO); current `fp` = `acc.fp`; no QALL/PQSE answer since acceptance | `dialect` / `fp` |
| 8 | Acceptance bits (§3.5). LOCAL `force` or acceptance writes are exempt from `in`/`out`, but need a staged + pressed acceptance session | `notacc` |
| 9 | Value inside floor…cap and `env.allow`; cross-key guards; `enableOnly`; battery data present where required | `range` / `cross` |
| 10 | REMOTE: `alerts/email` hash equals the pin (fetched now, ≤ 5 s old) and a local push channel is configured (SR F5) | `alerts` |
| 11 | Engine `RUN`; no probe or re-probe; link OK ≥ 5 min; last fast sample < 15 s old; uptime > 2 min; NVS write test OK | `state` / `nvs` |
| 12 | Rate limits (§5.5) | `rate` / `spacing` / `busy` |
| 13 | **Inverter state** (SR F20). QMOD letter in the value's modes (§3.2); Battery mode needs SOC ≥ 50 %. **QPIWS allow-list:** reject if any bit is set other than a0 PV loss (36-character firmware), a33 low PV energy (PGK) and, only for flag keys and `pop`→UtI, a5 LINE_FAIL. No open `INVERTER_FAULT` / `BATT_CRITICAL` event | `fault` / `mode` |
| 14 | **`pop` → SOL/SbU only** (SR F3), all of: load W (QPIGS AC output active power) < min(0.7 × 3200, 0.8 × Id × 24); SOC ≥ 60 %; grid present (LINE_FAIL clear, QMOD L); QFLAG `u`, `b`, `v` read (shown in the confirm sheet) | `load` / `soc` / `grid` |
| 15 | Pre-read: QPIRI + QFLAG (QPIRI only if the unit has no QFLAG), ≤ 3 tries → snapshot S0 and `before` | `preread` |
| 16 | **CAS:** `before == exp` (REMOTE and LOCAL) | `changed` |
| 17 | `before == target` and not `force` → result `noop`, **zero frames**. The UI names the LCD program to check (SR F16) | (`noop`) |

#### 5.4.3 State machine

```
IDLE ─take─► CHECK(1–14) ─fail─► DONE(rejected, why)
               │ok
               ▼
            PREREAD(15) ─fail─► DONE(rejected, preread)
               │ CAS(16) fail ─► DONE(rejected, changed) ; noop(17) ─► DONE(noop)
               ▼
            COMMIT: NVS ctl/inflt = {id,key,val,before,S0 packed,t}; id→ring; lastTs; send ring; frame count
               │ any NVS commit error ─► DONE(rejected, nvs)       (before any byte, SR F18)
               ▼
            SEND: writeSetting() ONCE → ACK | NAK | NONE
               ▼
            SETTLE 1500 ms
               ▼
            VERIFY: QPIRI+QFLAG, ≤ 3 tries 1 s apart → S1 | unreadable
               ▼
            CLASSIFY (§5.4.5) ─► DONE(applied | rejected | unverified | failed)
               ▼
            clear inflt ; audit ; snapshot ; event ; schedule FOLLOW-UP (§5.4.7)
```

**Engine interplay:**
- While the executor runs (worst case about 13 s), `engine.tick()` is skipped. Fast polls are skipped, not failed.
- The executor's reads refresh `lastOkMs`.
- `IClock::sleepMs` feeds the watchdog (`Port.h`, WDT 30 s). [CONFIRMED-primary (code)]
- After DONE it calls `sched.trigger(QPIRI)` and `sched.trigger(QFLAG)`.
- Alerts keep running on cached data; a test checks that `BATT_CRITICAL` still fires within its SLA during a transaction (SR H9).

#### 5.4.4 Reply classification (exact bytes, SR F15)

| Raw reply (after `transact` flushes RX before sending) | Class |
|---|---|
| exactly `28 41 43 4B 39 20 0D` | **ACK** |
| exactly `28 4E 41 4B 73 73 0D` or `28 4E 41 4B 0D` | **NAK** (P14 §3 shows both forms) |
| anything else, including a late reply to the pre-read, a timeout, `BAD_CRC`, or extra bytes | **NONE** (never treated as ACK or NAK) |

#### 5.4.5 Outcome (normal request; `target ≠ before`)

- **Whole-snapshot diff:** Δ = the fields that differ between S0 and S1, other than the key's own field and the catalog couplings.
- **Coupling list (initial):** `mchg` may also change QPIRI[13] (MAN p.14, "if 02 < 11 the 02 value is used"). Only local writes can produce it (the remote cross-key guard forbids `mchg < muchg`), and it is confirmed or removed by U14. [INFERRED]
- **If Δ ≠ ∅:** **failed/`sidefx`**, whatever the reply. Revoke the key, close the window, sev 4, and name every changed field.

**If Δ = ∅:**

| Reply \ key after | `== target` | `== before` | other value | unreadable |
|---|---|---|---|---|
| ACK | **applied** | **unverified** → follow-up | **failed/`unexpected`**, revoke, close | **failed/`verify`**, close; follow-up reads continue |
| NAK | **failed/`nakchg`**, revoke | **rejected/`nak`** (the QMOD letter is recorded; never retried in another mode) | failed/`unexpected` | failed/`verify` |
| NONE | **applied** (`why: noack`, sev 4) | **unverified** → follow-up | failed/`unexpected` | failed/`verify` |

- **`unverified`** (SR F19): the window closes and the key is revoked at once, and a sev-4 notice says "not confirmed yet, checking again until hh:mm".
  - Reads at +60 s and +10 min.
  - Target reached → final **`late`** (sev 4). The key stays revoked: re-accept on site with a longer settle.
  - Still `before` → final **failed/`mismatch`** or **failed/`noack`**.
  - It is never left as `failed` without the follow-up.
- **Acceptance (`force`, same value):**
  - ACK, unchanged, Δ = ∅ → **applied** (test passed);
  - NAK, unchanged → **rejected/`nak`** (setter unsupported);
  - anything else → **failed**, and the key is not acceptable.

#### 5.4.6 Boot recovery and lockout (SR F18)

- **Lockout.** If `esp_reset_reason()` is `ESP_RST_BROWNOUT`, `ESP_RST_PANIC`, `ESP_RST_INT_WDT`, `ESP_RST_TASK_WDT` or `ESP_RST_WDT`, or `inflt` exists at boot, then `ctl/lock = now + 24 h`. No setter is sent until then; reads continue.
- **`inflt` present:**
  - send nothing;
  - wait for `RUN`;
  - read the snapshot S1 and diff it against the stored S0;
  - publish **failed/`reboot`** with `before`/`after`, or **failed/`rebootsfx`** if any other field changed;
  - revoke the key, sev 4, clear `inflt`.

#### 5.4.7 Follow-up watch (SR F17, F19)

- After `applied`: reads at **+60 s, +5 min and +10 min**. `res.post` goes `watch` → `ok`.
- If the key moves away from the target → **`SETTING_REVERTED`** (sev 4), `post: reverted`, revoke.
- Any other field change during the watch → `SETTING_LCD` (sev 2), plus a sev-4 notice if it touches a catalog key.
- **No suppression window** (the draft's 5-min `SETTING_LCD` suppression is removed, SR errata 8). The snapshot loop knows the executor's expected state, so only changes the executor did *not* make are announced.

### 5.5 Rate limits and timing (initial values, [INFERRED]; tune from U4 and U17)

| Constant | Value | Note |
|---|---|---|
| `SET_FIRST_BYTE_MS` | 3000 | The ACK may follow an EEPROM write; the normal read timeout is 1500 (`cfg::FIRST_BYTE_MS`) |
| `SET_SETTLE_MS` | 1500 | Before VERIFY |
| `VERIFY_TRIES` / gap | 3 / 1000 ms | Reads only |
| Min gap between setter frames | 60 s | Global |
| Remote frames | ≤ 10 per **rolling 24 h** | Persisted ring of send times (SR F27) |
| Local frames | ≤ 20 per rolling 24 h | Acceptance sessions |
| Per key, remote | ≤ 4 per rolling 24 h, ≤ 2 per window | SR F4 |
| Remote reversal of the same key | not within 15 min | **Except** a restore to the baseline value (safe direction) |
| In flight | 1 change, including its 10-min watch | §5.4 |
| Window auto-close | 3 `rejected`, or 1 `failed`/`unverified`/`sidefx` | |
| Request age at execution | ≤ 120 s | §5.4.2 #3 |

### 5.6 Persistence

**NVS namespace `ctl`** (small, critical; NVS is 0x5000, `docs/05` §13.5):

| Key | Type | Written | Purpose |
|---|---|---|---|
| `feat` | u8 | local page + press; off from `ctlOff`/`kill` | Feature switch |
| `acc` | blob ≤ 96 B | acceptance sessions; revocation | §3.5 |
| `env` | blob ≤ 160 B | local page + press | `base`, `bat` (Ah, Ic, Id), `pcpMap`, `allow` lists |
| `ids` | blob 16 × 20 B | each COMMIT | Replay ring |
| `lastTs` | u32 | each COMMIT | Replay barrier |
| `inflt` | blob ≤ 96 B | COMMIT / DONE | Interrupted-write marker with S0 |
| `sends` | blob 32 × 8 B | each frame | Rolling 24 h ring `{t, key, origin}` |
| `mpin` | blob 32 B | feature-on, re-pin + press | Hash of `alerts/email/{on,to}` |
| `lock` | u32 | boot | Lockout end |
| `sfr` | u32 | each frame | Persisted setter frame counter, published as `status.sfr` (SR F28) |
| `crcp` | u8 | CRC probe | 0 not run, 1 CRC checked, 2 CRC ignored |

- The **audit ring** (32 × 32 B) moves to **LittleFS** `/ctl/audit.bin` (SR F18).
- The window and staging are **never persisted**.
- About 6 NVS writes per change, ≤ 30 changes a day: negligible wear. [INFERRED]

### 5.7 Settings snapshot and LCD-change detection (both builds, v1.x, P2-D10)

**Source:**
- stock: `Identity`'s QPIRI fields 2, 3, 8–17 and 22, plus the parsed QFLAG;
- REVO: PQSE tokens (C, D, E, F, G, LLL, MMM, SS, WW.W). QPIRI fields that the REVO document marks "Reserved" are **never shown** (SR F16).

**Publish:**
- `/devices/inv1/settings` (§7.2) when the canonical hash changes, checked at most every 60 s;
- immediately after DONE.

**`SETTING_LCD` (sev 2):**
- raised when the snapshot differs from the expected state, and the change was confirmed by two consecutive identical reads;
- ignored for the first snapshot after boot and after a dialect change;
- on lithium (05 = LIb), programs 12/13/29 are shown with "unit unknown (LIb)", never as volts (SR F29).

---

## 6. Local enable window and physical confirmation

### 6.1 Opening, staging, closing

| Aspect | Design |
|---|---|
| **Physical gesture** | **Three short BOOT presses**: each < 600 ms, all within 3 s; any press > 1 s aborts. A **single short press** confirms a staged action. The **3 s hold** still opens the Wi-Fi portal and never the window. Pressing BOOT's cap with the RJ45 plugged in is allowed (`docs/03` N17) |
| **Optional capacitive pad** (SR F1) | Foil or a jumper end taped **inside** the clear lid, wired to a free touch-capable GPIO that avoids the `docs/03` §3 never-use list, read with the ESP32-S3 touch peripheral. It is touched *through* the plastic, so there is no galvanic contact (compatible with N17). Bench-test first (P2-M3b) [INFERRED] |
| **The LAN page only stages** | The local page (admin) can *stage*: window options (5–30 min, multi-use up to 5 changes, Tier B allowed), feature on, acceptance ticks, battery data, envelope or cap raises, local-only (L) writes, Tier B writes, the CRC probe, the one-shot QMCHGCR/QMUCHGCR read, OTA. A staged item **executes only after a single BOOT press within 60 s**; otherwise it expires. Lowering, closing, revoking, feature-off and LOCAL writes of R-class values inside an open window need no press |
| **Default window** | Triple-press with nothing staged: **10 min, single use** (closes after the first request that reaches SEND), R-class remote and local |
| **Window id** | 8 random `[a-z0-9]` from `esp_random()`, new for every opening |
| **On open** (SR F10) | 1. GET `ctl/in`, honour `kill` / `close` if newer than seen. 2. DELETE `ctl/in/req`. 3. GET `alerts/email` and compare with the pin (mismatch → do not open, sev 4). 4. PUT `ctl/window` with `opened: {".sv":"timestamp"}` and `dur`. If 4 fails, the window is closed again |
| **Close** | Expiry; PWA `ctl/in/close`; local page "Close"; a triple-press while open |
| **Auto-close** | Reboot (never persisted); safe mode; OTA start; Wi-Fi portal; engine leaves `RUN` or re-probes; link lost; QMOD `F`; quota hard limit; the §5.5 auto-close counts; feature off; fingerprint change; alert-pin mismatch |
| **On close** | PUT `ctl/window {open:false, why, ts}` and DELETE `ctl/in/req` |
| **Notices** | Open → `CTL_WINDOW` sev 4 (push + e-mail). Normal close → sev 2. Auto-close after a problem → sev 4 |

### 6.2 LED

The existing priority is SAFE > PORTAL > PROBING > LINK_LOST > WIFI_DOWN > BACKFILL > LINK_OK (`main.cpp`). The new modes go after LINK_LOST:

| Mode | Pattern |
|---|---|
| `CTL_STAGED` | Fast white blink while a staged action waits for the press (≤ 60 s) |
| `CTL_WINDOW` | Double white blink every 2 s |
| `CTL_WRITE` | Solid white during a transaction |
| Gesture refused (feature off, lockout) | 3 fast red blinks |

The LED may need a solder bridge on some clones (`StatusLed.h`), so the local page and the PWA are the authoritative indicators.

### 6.3 Local API (rw build; all admin; CSRF guard)

| Route | Purpose | Press? |
|---|---|---|
| `GET /api/ctl` | Feature, window, staging, catalog with acceptance bits, floors/caps, counters, audit, last result | – |
| `POST /api/ctl/stage` `what=window&min=10..30&multi=0\|1&tierb=0\|1` | Stage a window | yes |
| `POST /api/ctl/stage` `what=feature&confirm=ENABLE-REMOTE-SETTINGS` | Stage feature-on (needs G10) | yes |
| `POST /api/ctl/feature` `on=0` | Feature off | no |
| `POST /api/ctl/window` `open=0` | Close | no |
| `POST /api/ctl/test` `key=fa` | Same-value acceptance write (`force`), window required | yes |
| `POST /api/ctl/set` `key&val&confirm=<key>=<val>` | LOCAL write. R-class: no press inside a window. L-class and Tier B: press | per class |
| `POST /api/ctl/accept` `key&val&dir=in\|out` / `pc=1` / `rem=1` | Tick acceptance bits after the LCD check | yes |
| `POST /api/ctl/revoke` `key` | Always allowed | no |
| `POST /api/ctl/env` | Battery data, `pcpMap`, caps (raise needs `confirm=RAISE`) | yes |
| `POST /api/ctl/qcr` | One-shot QMCHGCR/QMUCHGCR | yes |
| `POST /api/ctl/crcprobe` | §4.4 | yes |
| `POST /api/ctl/repin` | Re-pin `alerts/email` after a deliberate change | yes |

- **Handlers only queue work** and return `202` with an id; the page polls `GET /api/ctl`.
- **CSRF guard** (browser-only defence) [INFERRED]: the custom header `X-Ctl: 1`, `Host`/`Origin` equal to `solar.local` or the device IP, and the typed `confirm`.
  - **The press is the real defence** against a replayed Basic-auth header (SR F1).
- **No serial-console setter command.** The console gets only `ctl status`.

---

## 7. Data contract (RTDB)

### 7.1 Tree additions under `/devices/inv1/`

```
settings                     device PUT on change (both builds)                       ~400 B
ctl/window                   device PUT at open and at close only                       ~120 B
ctl/wstat                    device PATCH: left, sends24, rem24, lock (informational)   ~80 B
ctl/in/req/{id}              owner CREATE into an empty parent; owner/device DELETE      ~170 B
ctl/in/close                 owner (epoch s): close the window now
ctl/in/kill                  owner (epoch s): switch the feature off
ctl/res/{YYYY-MM-DD}/{id}    device: "pending" at take, final at DONE, post-watch update ~260 B
cfg/ctlOff                   owner (epoch s), written together with ctl/in/kill
status/sfr                   device: persisted setter frame counter (existing status rule)
meta/fwvar, meta/fp          device (existing meta rule)
events/{d}/{ts}-SETTING_*-i  device (existing event path, non-authoritative copy)
mailq/{ts}-SETTING_*-i       device (existing outbox path)
```

- The `{YYYY-MM-DD}` date is the **Asia/Dhaka date (UTC+6, no DST) of the request's server timestamp `sts`**. Both sides compute it the same way, and it reuses the date-keyed pruning of `events` (`keepEventDays`). [INFERRED]

### 7.2 Documents

`settings`:

```json
{"ts":1792580000,"src":"qpiri+qflag","dia":"STOCK21","fp":"3f9a0c1d2b4e5f60","feat":true,"crcp":1,
 "v":{"pop":0,"mchg":60,"muchg":30,"pcp":2,"fa":1,"fk":1,"fx":1,"fy":1,
      "pgr":0,"fu":0,"fv":0,"fb":0,"fz":1,"pbt":0,"ofreq":50,"ovolt":230,"pcvv":282,"pbft":270,"psdv":210},
 "ok":{"fx":"0,1","fa":"1","pop":"0"},
 "base":{"mchg":60,"muchg":30},"flr":{"mchg":20,"muchg":10},"cap":{"mchg":50,"muchg":30}}
```

- `v` holds only fields read successfully. Voltages are ×10 integers.
- `ok` lists the **remote-allowed values** per key.

`ctl/window` (open; closed is `{"open":false,"why":"expired","ts":…}`):

```json
{"open":true,"opened":{".sv":"timestamp"},"dur":600,"wid":"k3f9q2zt","by":"btn","multi":false,"ts":1792580000}
```

`ctl/in/req/1792580012-a1b2c3d4` (owner):

```json
{"id":"1792580012-a1b2c3d4","wid":"k3f9q2zt","key":"pop","val":0,"exp":2,"cts":1792580012,
 "sts":{".sv":"timestamp"},"by":"<OWNER_UID>","cv":2}
```

`ctl/res/2026-10-21/1792580012-a1b2c3d4` (final):

```json
{"key":"pop","val":0,"exp":2,"st":"applied","why":"","before":2,"after":0,"ack":"ACK",
 "org":"remote","wid":"k3f9q2zt","ts":1792580019,"ms":4210,"post":"watch"}
```

- `st` is one of `pending | unverified | applied | noop | rejected | failed | late`.
- `post` is one of `watch | ok | reverted`.
- `sfx` (string ≤ 40) lists side-effect fields.
- Local-origin changes also write a `res` record (`org: "local"`).

### 7.3 Request lifecycle

1. **The owner opens the PWA page.** The confirm sheet needs `.info/connected`, a step-up re-authentication within the last 5 min (`reauthenticateWithCredential`), and a 2 s delay. The PWA then **creates** `ctl/in/req/{id}`.
2. **The device**, while a window is open, GETs `ctl/in` every 5 s.
   - More than one child under `req` → reject all (`multi`), delete them, sev 3.
   - One child → strict parse, then **one multi-path PATCH**: `{"ctl/in/req/<id>": null, "ctl/res/<d>/<id>": {"st":"pending",…}}`.
   - Only after a 2xx does it queue the request.
   - **Take unknown** (timeout or lost reply, SR F22): the device remembers `<id>` in RAM. At the next poll:
     - if `req/<id>` is gone, it finalises `res/<id>` as **rejected/`take`** (never executed);
     - if it is still there, it retries the take.
3. The executor runs (§5.4) for ≤ about 13 s.
4. **One multi-path PATCH:** `{ctl/res/<d>/<id>: final, settings: snapshot, ctl/wstat: {…}, events/<d>/<k>: event, mailq/<k>: job}`. Pushes go through the existing alert path, using the ctl budget.
5. **The PWA's listener** on `ctl/res/<d>/<id>` shows the state.
   - Typical end to end: 7–15 s. [INFERRED]
   - No `pending` within 60 s → "Unknown. Check the history and the LCD; it expires by itself at hh:mm:ss."
6. **Kill:** the owner writes `{ctl/in/kill: now, cfg/ctlOff: now}` in one update. The device honours whichever it sees first: within 5 s while a window is open, else on the 60 s `cfg` poll.

**Offline PWA.** The Firebase JS SDK queues offline writes and replays them later [INFERRED from SDK behaviour; https://firebase.google.com/docs/database/web/offline-capabilities]. A late replay fails the rules (`sts` would resolve to the replay time, but `cts` is older than 30 s) and the device check (#3).

### 7.4 Rules snippet (merge under `devices/inv1`; placeholders as in the committed file)

```json
"settings": {
  ".write": "auth != null && auth.uid === 'DEVICE_UID'",
  ".validate": "newData.hasChildren(['ts', 'v'])",
  "ts":   { ".validate": "newData.isNumber() && newData.val() < now / 1000 + 600" },
  "src":  { ".validate": "newData.isString() && newData.val().matches(/^(qpiri|qpiri.qflag|pqse)$/)" },
  "dia":  { ".validate": "newData.isString() && newData.val().length <= 10" },
  "fp":   { ".validate": "newData.isString() && newData.val().matches(/^[0-9a-f]{16}$/)" },
  "feat": { ".validate": "newData.isBoolean()" },
  "crcp": { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 2" },
  "v":    { "$k": { ".validate": "$k.length <= 6 && newData.isNumber()" } },
  "ok":   { "$k": { ".validate": "$k.length <= 6 && newData.isString() && newData.val().matches(/^[0-9]{1,3}(,[0-9]{1,3}){0,11}$/)" } },
  "base": { "$k": { ".validate": "$k.length <= 6 && newData.isNumber()" } },
  "flr":  { "$k": { ".validate": "$k.length <= 6 && newData.isNumber()" } },
  "cap":  { "$k": { ".validate": "$k.length <= 6 && newData.isNumber()" } },
  "$other": { ".validate": false }
},
"ctl": {
  "window": {
    ".write": "auth != null && auth.uid === 'DEVICE_UID'",
    ".validate": "newData.hasChildren(['open', 'ts']) && (newData.child('open').val() === false || newData.hasChildren(['opened', 'dur', 'wid', 'by', 'multi']))",
    "open":   { ".validate": "newData.isBoolean()" },
    "opened": { ".validate": "newData.isNumber() && newData.val() === now" },
    "dur":    { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 60 && newData.val() <= 1800" },
    "wid":    { ".validate": "newData.isString() && newData.val().matches(/^[a-z0-9]{8}$/)" },
    "by":     { ".validate": "newData.isString() && newData.val().matches(/^(btn|touch)$/)" },
    "multi":  { ".validate": "newData.isBoolean()" },
    "why":    { ".validate": "newData.isString() && newData.val().length <= 12" },
    "ts":     { ".validate": "newData.isNumber()" },
    "$other": { ".validate": false }
  },
  "wstat": {
    ".write": "auth != null && auth.uid === 'DEVICE_UID'",
    "$k": { ".validate": "$k.length <= 8 && newData.isNumber()" }
  },
  "in": {
    "req": {
      ".write": "auth != null && auth.uid === 'DEVICE_UID' && !newData.exists()",
      "$id": {
        ".write": "auth != null && ((auth.uid === 'OWNER_UID' && (!newData.exists() || (!data.exists() && !root.child('devices/inv1/ctl/in/req').exists() && root.child('devices/inv1/ctl/window/open').val() === true && root.child('devices/inv1/ctl/window/opened').val() + root.child('devices/inv1/ctl/window/dur').val() * 1000 > now && newData.child('sts').val() === now && newData.child('cts').val() * 1000 > now - 30000 && newData.child('cts').val() * 1000 < now + 5000 && auth.token.auth_time * 1000 > now - 300000))) || (auth.uid === 'DEVICE_UID' && !newData.exists()))",
        ".validate": "newData.hasChildren(['id', 'wid', 'key', 'val', 'exp', 'cts', 'sts', 'by', 'cv']) && newData.child('id').val() === $id && $id.beginsWith(newData.child('cts').val() + '-') && newData.child('wid').val() === root.child('devices/inv1/ctl/window/wid').val()",
        "id":  { ".validate": "newData.isString() && newData.val().matches(/^[0-9]{10}-[a-z0-9]{8}$/)" },
        "wid": { ".validate": "newData.isString() && newData.val().matches(/^[a-z0-9]{8}$/)" },
        "key": { ".validate": "newData.isString() && newData.val().matches(/^(pop|pcp|mchg|muchg|fa|fk|fx|fy)$/)" },
        "val": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && (newData.parent().child('key').val() === 'pop' ? (newData.val() >= 0 && newData.val() <= 2) : newData.parent().child('key').val() === 'pcp' ? (newData.val() >= 0 && newData.val() <= 2) : newData.parent().child('key').val() === 'mchg' ? (newData.val() >= 20 && newData.val() <= 100 && newData.val() % 10 === 0) : newData.parent().child('key').val() === 'muchg' ? (newData.val() >= 10 && newData.val() <= 80 && newData.val() % 10 === 0) : newData.parent().child('key').val() === 'fa' ? newData.val() === 1 : newData.parent().child('key').val().matches(/^f[kxy]$/) ? (newData.val() === 0 || newData.val() === 1) : false)" },
        "exp": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && newData.val() >= 0 && newData.val() <= 100" },
        "cts": { ".validate": "newData.isNumber() && newData.val() % 1 === 0" },
        "sts": { ".validate": "newData.isNumber()" },
        "by":  { ".validate": "newData.isString() && newData.val() === auth.uid" },
        "cv":  { ".validate": "newData.isNumber() && newData.val() === 2" },
        "$other": { ".validate": false }
      }
    },
    "close": { ".write": "auth != null && auth.uid === 'OWNER_UID'", ".validate": "newData.isNumber() && newData.val() <= now / 1000 + 300" },
    "kill":  { ".write": "auth != null && auth.uid === 'OWNER_UID'", ".validate": "newData.isNumber() && newData.val() <= now / 1000 + 300" },
    "$other": { ".validate": false }
  },
  "res": {
    ".write": "auth != null && auth.uid === 'DEVICE_UID'",
    "$date": {
      ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
      "$id": {
        ".validate": "$id.matches(/^[0-9]{10}-[a-z0-9]{8}$/) && newData.hasChildren(['key', 'st', 'ts'])",
        "key":    { ".validate": "newData.isString() && newData.val().matches(/^[a-z]{2,5}$/)" },
        "val":    { ".validate": "newData.isNumber()" },
        "exp":    { ".validate": "newData.isNumber()" },
        "st":     { ".validate": "newData.isString() && newData.val().matches(/^(pending|unverified|applied|noop|rejected|failed|late)$/)" },
        "why":    { ".validate": "newData.isString() && newData.val().length <= 12" },
        "before": { ".validate": "newData.isNumber()" },
        "after":  { ".validate": "newData.isNumber()" },
        "ack":    { ".validate": "newData.isString() && newData.val().matches(/^(ACK|NAK|NONE|-)$/)" },
        "org":    { ".validate": "newData.isString() && newData.val().matches(/^(remote|local)$/)" },
        "wid":    { ".validate": "newData.isString() && newData.val().matches(/^[a-z0-9]{8}$/)" },
        "ts":     { ".validate": "newData.isNumber()" },
        "ms":     { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 60000" },
        "post":   { ".validate": "newData.isString() && newData.val().matches(/^(watch|ok|reverted)$/)" },
        "sfx":    { ".validate": "newData.isString() && newData.val().length <= 40" },
        "$other": { ".validate": false }
      }
    }
  },
  "$other": { ".validate": false }
}
```

And in `cfg`, beside `reprobe`:

```json
"ctlOff": { ".validate": "newData.isNumber() && newData.val() <= now / 1000 + 300" }
```

**Notes:**
- **Reads.** The owner and the device read everything under `devices/inv1` (existing cascade). The mailer gets **no** read on `settings` or `ctl`, because mailer reads are granted per node (`firebase/README` §1).
- **Single, immutable slot** (SR F22, F24).
  - Owner creation needs an empty `ctl/in/req` and a non-existent `$id`. A child-path write (`…/{id}/val`) on an existing request is evaluated against `data` at `$id`, which exists, so it is denied.
  - Stale clean-up is a DELETE (always allowed for the owner), then a new create.
  - The rules cannot count children, so a *single multi-path update* creating two ids would pass. The device rejects any `req` with more than one child (§7.3).
- **No grant above the leaves.** `ctl` has no `.write`, so nothing cascades. Every level validates itself, because `.validate` does not cascade and does not run on deletes (`firebase/README` §1; https://firebase.google.com/docs/database/security/core-syntax [CONFIRMED-primary]).
- **`root` and `data` are the pre-write state** (same page). The device's window writes and the owner's request writes are separate requests.
- **[INFERRED, verify in the emulator before P2-M7]:**
  1. **`newData.val() === now`** for a `{".sv":"timestamp"}` write (the documented server-timestamp pattern [SECONDARY]); the same from the device's REST client.
  2. **`auth.token.auth_time`**: if the emulator does not expose it, drop the term and rely on the PWA step-up (residual gap R-P2-11).
  3. **`$id.beginsWith(number + '-')`**: if number-to-string concatenation is not supported, drop it; the device checks the prefix (#4).
- **Coarse vs fine.** The rules check coarse remote ranges (for example 100 A is coarse-valid for MAX24). The device checks class, floor/cap, acceptance and cross-key guards.
- **CI.** The existing "no plain `auth != null`" check still passes, because every grant names a UID.

### 7.5 Events are a copy, not the record (SR F23)

- `SETTING_*` and `CTL_*` events are written to `events` so the Alerts page and the mailer work unchanged.
- The PWA's "Inverter settings → History" reads only `ctl/res`, which is device-only. The device's LittleFS audit ring is the second copy.
- Deleting an event therefore removes only the alert-page copy.

### 7.6 Polling and quota

| State | `cfg` GET | `ctl/in` GET | Writes |
|---|---|---|---|
| Window closed (normal) | 60 s | **never** | `settings` on change |
| Window open | 60 s | **every 5 s** | Take/result PATCHes; `wstat` on change |
| Executing | 60 s | paused | – |
| Quota guard hard limit | per budget | window auto-closes (`why: quota`) | – |

- A 30-min window costs about 360 small GETs (about 0.4 MB with headers), plus one `alerts/email` GET per remote change. That is well inside the 15 MB/day target and `BUDGET_RQ_GUARD` (20,000 requests a day). [INFERRED]
- Streaming (SSE) was rejected: it does not fit the single-TLS polling uplink (`Uplink.h`).

---

## 8. Web UI: "Inverter settings"

### 8.1 Placement and data

- **Route:** `#/settings/inverter`, a card on the Settings page. It adds no seventh nav item (`app.tsx` `ROUTES`).
- **`DataSource`** (`source.ts`) additions, with demo versions in `demoSource.ts`:
  - `onSettings`, `onCtlWindow`, `onCtlRes(date)`;
  - `requestSetting(key, val, exp, wid)`: `cts` from the existing `.info/serverTimeOffset`; `sts: serverTimestamp()`; id from `crypto.getRandomValues`;
  - `cancelSetting(id)`, `closeCtlWindow()`, `ctlKill()`;
  - `reauth(password)`, using `reauthenticateWithCredential(user, EmailAuthProvider.credential(email, pw))`.
- **Catalog:** generated `web/src/settingsCatalog.ts`.

### 8.2 Layout and states

1. **Status card:**
   - Build `ro`: "This logger runs the read-only firmware. Values are shown for reference; change settings on the inverter LCD."
   - Feature off: "Remote settings are switched off on the logger."
   - Window closed: "Remote changes are locked. To allow one change in the next 10 minutes, press the logger's BOOT button 3 times quickly."
   - Window open: countdown computed from `opened + dur` and the server offset, not from the device epoch. "Remote change allowed until 14:35 (one change) · 9 changes left in 24 h", with **Close now**.
   - Always: **Switch remote settings off** (kill switch, confirm sheet).
2. **Rows for the R-class keys:**
   - label, LCD program and LCD text, e.g. "Output source priority · prog 01 · **UtI**";
   - value age;
   - the control: a segmented picker for enums; for currents, a stepper bounded by `ok` (verified values only).
   - **Values not verified on site are greyed out** with "not yet tested on site" (SR F2).
   - **Currents** (SR F29):
     - `mchg` is "**Total** charging (solar + mains)" and `muchg` "From **mains only**";
     - show A, kW (× 24 V nominal) and C-rate (from Ah);
     - show "effective mains current = min(02, 11)";
     - 2 A is never offered remotely.
3. **Confirm sheet** (two steps):
   - from → to, the plain-language consequence (§8.3), cross-setting warnings;
   - the current QMOD mode, load W, SOC, grid status and the result of the F3 checks for `pop`;
   - QFLAG `u`/`b`/`v` in words ("If the inverter overloads it will **not** restart by itself (program 06 = LtD)");
   - "Make sure nobody is using the inverter's buttons."
   - **Password re-entry** if the last authentication is older than 5 min. Confirm enables after 2 s and only while `.info/connected` is true.
4. **Request states:**
   - *Sending*.
   - *Waiting for the logger*: after 20 s, "it expires by itself at hh:mm:ss", with **Cancel**; after 60 s, "Unknown, check the history and the LCD".
   - *Inverter is applying*.
   - **Applied**: "Inverter confirmed: now SbU (read back 14:21:07). Checking again for 10 min." A second line under the green state: "*Applied* is not always *in effect*: SbU stays on mains until the back-to-battery point (program 13) is reached." (SR F29)
   - **Not confirmed yet** (`unverified`).
   - **Applied late** (`late`).
   - **No change needed** (`noop`): "The logger reads UtI already. If the LCD (program 01) shows something else, tell us." (SR F16)
   - **Refused**, with the reason in words. Examples: `changed`: "the setting changed since you looked; reload"; `load`: "the house load is 2.4 kW; moving it to the battery could overload it".
   - **Failed**: red, names the LCD program to check, "remote control of this setting is disabled until it is tested on site again".
   - **Reverted**: sev 4, "the inverter's setting changed back at 14:26; someone may have used the LCD".
5. **Read-only rows:**
   - Tier B and L-class: "Change on the logger's page with the BOOT button, or on the LCD".
   - Tier C: "LCD only. The app never changes this, by design."
   - LCD-only programs (40, 42, 44, 45, 50): "not readable by the logger".
   - Lithium SOC programs: "unit unknown (LIb)".
6. **History:** `ctl/res` for 14 days (when, origin, key, before → after, result, reason; tap for `ack`, `ms` and `sfx`).

### 8.3 Consequence texts

Draft wording [INFERRED from MAN pp. 13–16 via `docs/01` §4.7]; check it against the paper manual.

| Key = value | Text |
|---|---|
| `pop` = UtI | "Mains powers the house whenever it is on. Solar and battery take over only during a power cut." |
| `pop` = SOL | "Solar powers the house first; mains adds what solar cannot cover." |
| `pop` = SbU | "Solar first, then battery, then mains. At night the battery runs the house until it falls to the back-to-utility point (program 12), then mains takes over. The battery cycles every day." |
| `pcp` = CUt / CSO / SNU | "Mains charges the battery first, solar only when mains is off" / "Solar charges first, mains only when solar is not available" / "Solar and mains charge together". Plus: "Applies in Line or Standby mode; in Battery mode only solar charges." |
| `mchg` = N | "Total charging current (solar + mains) limited to N A (was M A), about N×24 W, C-rate x. Lower means slower charging, **including from solar**." |
| `muchg` = N | "Mains may charge the battery with at most N A (was M A). If this is above program 02, the inverter uses program 02." |
| `fa` = 1 | "The inverter beeps for warnings and faults again." (Switching it off is only possible at the logger.) |
| `fx`, `fk`, `fy` | "LCD backlight off/on" · "LCD stays on the chosen page / returns to the main page after 1 min" · "No beep / beep when mains power is cut" |

---

## 9. Notifications

### 9.1 Types

The existing event path is reused (`<ts>-<TYPE>-<kind>` keys, `mailq`, the Apps Script mailer; r8 §3.6–3.7). The mailer needs no code change. The new types go into `shared/codes.json` `eventTypes`.

| Type | Sev | When | Push (ntfy/Telegram) | E-mail |
|---|---|---|---|---|
| `CTL_WINDOW` (`open`) | 4 | Window opened | forced | **always** (if the pin matches) |
| `CTL_WINDOW` (`clear`) | 2, or 4 after a problem | closed / expired / auto-closed | sev 4 only | sev 4 only |
| `CTL_FEATURE` | 4 | Feature on/off, fingerprint change, alert-pin mismatch, lockout | forced | **always** |
| `SETTING_CHANGED` | 4 | `applied` (remote or local) | forced | **always** |
| `SETTING_UNVERIFIED` | 4 | `unverified` | forced | always |
| `SETTING_FAILED` | 4 | `failed`, `late`, `sidefx`, boot recovery | forced | **always** |
| `SETTING_REVERTED` | 4 | Follow-up watch saw a revert | forced | always |
| `SETTING_REJECTED` | 3 | `rejected` (nothing sent) | forced | first 3 per 24 h |
| `SETTING_LCD` | 2 | Snapshot change the executor did not make | normal gates | per `builtin.minSev` |

### 9.2 Alert integrity (SR F5)

- **Feature-on (staged + press) requires** ntfy or Telegram in NVS `alert` (set only through `/api/config`) and a test push that the owner confirms on the local page (G10).
- **At feature-on**, the device stores `mpin` = SHA-256 of the canonical `alerts/email/{on,to}`.
- **At every window open and before every remote execution**, it GETs `alerts/email` and compares.
- **On a mismatch:**
  - reject `alerts`;
  - close the window;
  - push sev 4 "e-mail alert settings changed; remote control paused" through ntfy/Telegram only.
- **Re-pin** after a deliberate change: local page + press.
- **Notice text is built only from the executor's record** (invariant I6).

### 9.3 Budget (SR F26)

- **ctl mails:** a separate budget of **15 per 24 h**, outside the v1 cap of 40.
- **v1 cap:** the v1 rule stays (the last 5 of the 40 are reserved for severity ≥ 4), and of those 5 the last **3** become **severity 5 only** (rw build).
- **Pushes:** forced ctl pushes do **not** count toward the 40/day push gate.
- **Volume** is bounded by §5.5: about 10 changes and 3 rejection mails a day.

**Message examples** (`events.msg` ≤ 200 characters):
- "Setting changed (app): Output priority, prog 01, SbU -> UtI. ACK, read back UtI 14:21; nothing else changed. Watching until 14:31."
- "Setting NOT confirmed: Max utility charge, prog 11, 30 -> 20 A. ACK but reads 30 A. Checking again at 14:22 and 14:31. Remote control of it is disabled."
- "Remote inverter control enabled for 10 min (one change) with the logger's BOOT button. If this was not you, switch it off in the app."

---

## 10. Invariants

Each one has a test (§11).

| # | Invariant |
|---|---|
| I1 | **The cloud can never open a window, set acceptance, raise a floor/cap/baseline, enter battery data, re-pin alerts or switch the feature on.** The only cloud→ctl inputs are `ctl/in/req/*` (bounded and checked), and `ctl/in/close`, `ctl/in/kill`, `cfg/ctlOff` (safe direction) (SR F9) |
| I2 | `savx` contains no setter strings, no `writeSetting`/`SetterWhitelist`/`SETTERS` symbols and no write path |
| I3 | No Tier C string exists in any binary |
| I4 | `SerialLink::query()` refuses every setter in both builds |
| I5 | At most one setter frame per request; never retried; never sent within 60 s of another |
| I6 | Notice text and the history come only from the device's executor records (SR F8) |
| I7 | The window lives in RAM only, is closed at boot, and opens only after a physical gesture |
| I8 | No write within 24 h of a brown-out, panic or WDT reset, or after an interrupted write |
| I9 | "Applied" only when the whole-snapshot read-back shows exactly the expected change |
| I10 | REVO, any QALL/PQSE answer, or a fingerprint change → no writes |
| I11 | Safe-direction actions are always available: close, kill, revoke, lower within the floor; `pop`→UtI inside a window |

---

## 11. Tests

### 11.1 Native unit tests (`pio test -e native-rw`; `native` keeps the read-only suite)

| Suite | Cases |
|---|---|
| `test_ctl_catalog` | Every §3.7 vector byte-exact. Each canonical value maps to exactly one string per dialect. Out of range → `nullptr`: `mchg` 95/100 (STOCK), 0, −10, 110; `muchg` 5, 85; `pop` 3; `pcp` 0 with `pcpMap = MAXGK`, any `pcp` with `none`; `fa` 2; `fz` 0; `fu` on MAX24. REVO → `nullptr` for every key. Value classes and floor/cap formulas for the §3.3 examples. Escape flags exactly on `POP02`, `MCHGC090` |
| `test_ctl_whitelist` | Table count. No entry starts with a Tier C prefix (`PBT PSDV PCVV PBFT PBCV PBDV PBEQ PF PSET PEd PDd PEj PDj PDz POP03 F5 F6 V2 PBATCD RTEY RTDL PPCP MSCHGC PCVT PCST PSAVE`). Exact match only (`POP02 `, `POP2`, `pop02`, `POP020`, `PEab`, `PEA` refused). Tier B refused when not allowed. `setterFrameAllowed` refuses a tampered CRC and non-printables. **`wl::cmdAllowed` still refuses every generated setter** in all three framings |
| `test_ctl_executor` (`SimInverter` with a stateful settings model) | Every §5.4.5 cell. NAK with and without CRC; exact-reply rule; **the previous read's late reply arriving after the setter frame → NONE** (SR F15). Side effect in another field → `sidefx`. `unverified` → `late` at +60 s; → `failed` at +10 min. Revert during the watch → `SETTING_REVERTED`. CAS `changed`. Duplicate id; `cts ≤ lastTs`; expired, future or skewed `sts`; inverter-clock time source → `time`. Window closed, wrong `wid`, single-use consumed. Every rate limit including rolling 24 h, spacing, the baseline-restore exemption and `busy` during the watch. QMOD `F`/`P`/`H`; Battery mode with SOC < 50; every non-allow-listed QPIWS bit; F3 load/SOC/grid; missing battery data. `fp` mismatch. Alert-pin mismatch → `alerts`. **NVS commit failure → zero frames.** Boot recovery and lockout → zero frames. `noop` → zero frames. **Every test asserts at most one setter frame per request** |
| `test_ctl_window` | Triple-press decoding (bounce, 590/610 ms edges, 3 s span); single press confirms staging only within 60 s; **the 3 s hold still opens the portal and never the window**; feature off → refused; expiry; every auto-close trigger; reboot → closed; new `wid` each time. **Injected cloud `ctl/window {open:true}` (and `settings.ok`, `acc`-like fields) never opens a window or changes acceptance** (I1) |
| `test_uplink` (extended) | Strict `ctl/in` parse (missing or extra fields; two requests → reject all). Take PATCH failure → not executed; take unknown → `rejected/take` if gone. Serialisation within the rule limits. Kill seen through `ctl/in` and through `cfg` |
| `test_parsers` (extended) | QFLAG struct; PQSE `SSTUV` / `WW.W` (both layouts); snapshot hash and diff; REVO snapshot omits reserved QPIRI fields |
| `test_notify` (extended) | ctl budget separate; sev-5 reserve; forced pushes not counted; notice text built from the executor record only |

**Golden replies** (SR H7): QPIRI, QFLAG, PQSE and NAK forms captured from the real unit at v1 M3 become fixtures (`test/fixtures/golden_*.txt`). Every string table is cross-checked by hand against P14, PMAX and PGK, and the downloaded mpp-solar code is **never executed**.

### 11.2 Rules emulator tests (`tools/rules-test`, extended; run on the **complete merged** rules file)

| Who | Case | Expected |
|---|---|---|
| Owner | Create `ctl/in/req/{id}`: window open, right `wid`, fresh `cts`, `sts` server value, recent `auth_time` | Allow |
| Owner | Same with: window closed or expired (`opened + dur`), wrong `wid`, `cts` ± 31 s, `sts` a literal number, a request already present, old `auth_time` | Deny |
| Owner | Child-path write `ctl/in/req/{id}/val` on an existing request; overwrite an existing id | Deny |
| Owner | Multi-path update creating two ids | Allowed by the rules (documented gap) → the device test rejects both |
| Owner | Unknown key; `val` outside the coarse ranges (`fa` 0, `pcp` 3, `muchg` 2, `mchg` 10); extra field; `by` ≠ uid; `cv` ≠ 2; id prefix ≠ `cts` | Deny |
| Owner | Delete own request; write `ctl/in/close`, `ctl/in/kill`, `cfg/ctlOff` | Allow |
| Owner | Write `ctl/res`, `ctl/window`, `ctl/wstat`, `settings` | Deny |
| Device | Create `ctl/in/req/*` | Deny |
| Device | Delete `ctl/in/req` or one id; write `res`/`window`/`wstat`/`settings` | Allow |
| Device | `window.opened` ≠ server time; `dur` > 1800 | Deny |
| Mailer | Read `ctl`, `settings` | Deny |

The existing r8 N/P series must still pass.

### 11.3 CI binary checks (new step)

1. `pio run -e savx`, then:
   - `strings -n 3 .pio/build/savx/firmware.bin | grep -E '(POP0[0-3]|PCP0[0-3]|MN?CHGC[0-9]{3,4}|MUCHGC[0-9]{3}|P[ED][abdjkuvxyz]|PGR0[01])'` must print nothing.
   - The grep is unanchored, so merged strings are caught too (SR H6). A hit inside an unrelated longer string is investigated and, if harmless, allow-listed by its exact full string.
2. Symbol check: `xtensa-esp32s3-elf-nm -C .pio/build/savx/firmware.elf | grep -E 'writeSetting|SetterWhitelist|SETTERS|CommandExecutor|crcProbe'` must print nothing.
   - [INFERRED: the toolchain path is under `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/`.]
3. `pio run -e savx-rw`, then the strings check with the Tier C patterns (`PBT[0-9]{2}|PSDV|PCVV|PBFT|PBCV|PBDV|PBEQ|PSET|PBATCD|RTEY|RTDL|PSAVE|P[ED][dj]|PDz|POP03|F5[0-9]|F6[0-9]`) must print nothing.
   - `PF` is checked by symbol and source grep only (too short for `strings`).
4. `python3 tools/gen_settings.py --check` passes (catalog ↔ rules ↔ TS).
5. A source grep finds no reader of `ctl/window`, `settings/ok` or `ctl/wstat` in `src/net` other than the serialisers (I1).

### 11.4 Bench with a fake inverter (P2-M4)

- **Setup:**
  - Unplug the RJ45 at COM first (`docs/03` N8). Logging pauses for 2–3 h.
  - **Variant A:** `savx-rw-bench`, with a `FakeInverter` task on UART2 emulating QPI, QPIGS, QMOD, QPIWS, QPIRI, QFLAG and every catalog setter.
  - **Wiring:** GPIO17 → GPIO4 and GPIO5 → GPIO18, with two jumpers. [INFERRED: check that 4 and 5 are free on the clone and outside the `docs/03` §3 never-use list.]
  - RAM-only link state. The bench build never writes NVS `link`.
- **Scenarios:**
  - `ack`, `nak`, `nak-nocrc`, `silent`, `garbage`;
  - `ack-noapply`, `ack-late` (applies after 30 s), `ack-wrong`, `ack-sidefx` (changes a second field), `late-preread-reply`;
  - `slow-ack` (2.5 s), `readback-nak`, `mode-B-nak`;
  - `crc-ignored` / `crc-checked` for the probe;
  - a manual **RST press during SETTLE**;
  - a **brown-out simulation** (reset reason forced in the bench build).
- **Variant B (optional):** a Python simulator through the native USB CDC. [INFERRED: confirm that `HWCDC` works with `ARDUINO_USB_CDC_ON_BOOT=0` in Arduino-ESP32 3.3.12.] One USB cable at a time (`docs/03` N13).
- **Oracle independence** (SR H7): the FakeInverter replays the **golden replies** from M3, and its setter→field map is written from the protocol PDFs by hand, separately from the catalog.
- **Acceptance:**
  - each scenario produces its §5.4.5 cell, `res`, event, mail, push and LED;
  - exactly one setter frame per request (FakeInverter counter = `status.sfr` delta);
  - no write after reboot or during lockout;
  - the window never survives a reboot.
- **After the bench:** re-flash, re-install, and re-run the `docs/07` M3 quick checks (QPIGS ≥ 99 % valid over 200 polls; output V and battery V against the LCD) before any write.

### 11.5 Hardware-in-the-loop on site (SR H3, H4)

| Case | Expected |
|---|---|
| Owner holds ENTER in LCD setting mode during a local write of `fx` | `sidefx`/`reverted` or a clean apply, never a silent mismatch |
| `pop`→SbU with > 1.9 kW load (100 Ah/100 A example) | rejected `load`, zero frames |
| `pop`→UtI in Battery mode with SOC ≥ 50 % | allowed |
| Mains off (`LINE_FAIL`) | `pop`→SOL/SbU, currents and `pcp` rejected; flags allowed |
| Kill switch while a request is queued | the request is rejected `feature`, zero frames |
| Clock on the QT fallback (NTP blocked at boot) | rejected `time` |
| `alerts/email/to` changed from the PWA while a window is open | next remote request rejected `alerts`, window closed, push received |

### 11.6 On-site acceptance (owner at the LCD; `docs/07` global stop rules apply)

**For each value:**
1. LCD and snapshot agree.
2. Window open (press).
3. **Same-value test** (`/api/ctl/test`). Record the reply bytes (U4), latency, letter case (U3) and persistence at the next scheduled read (U11).
4. Local write baseline → v. Check the LCD → tick `in` (press).
5. Local write v → baseline. Check the LCD → tick `out` (press).
6. For `pop`, `pcp`, `mchg` and `muchg`: once per key, leave a non-baseline value set, have the owner **power-cycle the inverter** at a convenient time, read back, restore → tick `pc` (SR H1).
7. **One remote change** from the PWA with the owner at the LCD → tick `rem`.

**Order** (stop at the first unexpected result):

| Step | Key | Values and visible check | Conditions |
|---|---|---|---|
| 0 | – | CRC probe (§4.4); QMCHGCR/QMUCHGCR one-shot (§3.6). Read only | Any time |
| 1 | `fa` buzzer (**P2-M5 GO/NO-GO**) | Same-value `PEa` (alarm on, nothing changes). Then `PDa` → mute icon appears [MAN p.12] → `PEa` → icon gone. This is the polarity check (SR H8) | No active warning; daytime; keep bOF under 1 min |
| 2 | `fx` backlight | `PDx` → backlight off → `PEx` | Any time |
| 3 | `fk`, `fy` | ESP ↔ HEP; AON ↔ AOF (LCD text) | – |
| 4 | `pop` | Same-value UtI. UtI → SOL for 10 min (watch the load source on the LCD and in Live) → UtI. SbU likewise, only once G6 exists. **`POP02` is the 0x0A escape test** | Daytime, grid on, SOC > 60 %, load < the F3 limit |
| 5 | `muchg` | Same-value; 30 → 20 A in Line mode while mains-charging (`bci` falls within 2 min) → 30 | Grid on, battery not full |
| 6 | `mchg` | Same-value; baseline → baseline − 10 → baseline; then each remote value. **`MCHGC090` (0x0D escape) only if 90 is inside the cap** | As 5 |
| 7 | `pcp` | **Only after `pcpMap` is set from U7.** Same-value; SNU → CSO → SNU; CUt only on the HS map; OSO local-only test, short, then back | Daytime |

- **Never** send `PSAVE`, and never test any Tier C command (P2SET §5 U11, U15).

---

## 12. Milestones, effort and acceptance

Effort is in evenings (2–3 h), as in `docs/00` §8. [INFERRED]

| M | Name | Scope | Effort | Acceptance |
|---|---|---|---|---|
| **P2-M0** | Gates and zero-write go/no-go | §4.1 G1–G6, G8; §4.2 tree from `probe-report.json`, `meta.sup` and three explicit QALL/PQSE attempts; LCD table and photos incl. 06/23/40/50; U7/U8 mapping; battery and BMS data | 1–1.5 | Every listed gate ticked; decision written in the build record. **REVO or any QALL/PQSE answer → stop here** (keep the snapshot) |
| **P2-M1** | v1.x hardening (both builds) | OTA needs a BOOT press; `Snapshot` + `settings` + `SETTING_LCD` (PQSE on REVO); parsed QFLAG and PQSE `SSTUV`/`WW.W`; `meta.fp`; golden-reply fixtures | 1.5–2 | `pio test -e native` green; OTA refused without a press; `settings` matches the LCD table (G5); no change to what is sent on the link (raw log diff) |
| **P2-M2** | Catalog, generator, rw envs, write path | `settings-catalog.json`, `gen_settings.py`, `savx-rw`/`native-rw`, `config.h` guard, `SetterWhitelist`, `writeSetting`, `crcProbe`, value classes, floor/cap, escape flags, acceptance bitmap format, CI string and symbol checks | 2.5–3.5 | All §3.7 vectors byte-exact; Tier C absent; `savx` has no setter strings or symbols; `native` unchanged and green |
| **P2-M3** | Executor core, window, local page (flags) | `CommandExecutor` (checks 1–13, 15–17; CAS; whole-snapshot; outcome matrix; follow-ups; boot recovery; lockout; NTP-only), NVS `ctl` + LittleFS audit, rolling limits, gestures + staging + LED, `/api/ctl*` with press and CSRF, `test_ctl_*`. **P2-M3b (optional):** capacitive pad bench test | 3.5–4.5 (+0.5) | Every §5.4.5 cell tested; ≤ 1 setter frame per request in every test; reboot/lockout → zero frames; 3 s hold still opens the portal; I1 test passes |
| **P2-M4** | Bench with a fake inverter | §11.4 | 1–2 | All scenarios as specified; RST and brown-out tests pass; M3 quick check passes after re-installation |
| **P2-M5** | **On-site feasibility (GO/NO-GO)** | `savx-rw` OTA (with a press); feature on (G10); CRC probe (G11); window by button; §11.6 step 1 (buzzer), then step 2 (backlight); U2–U4, U11, U13 recorded | 1 | **GO:** `PEa` same-value ACK with an unchanged snapshot; `PDa`/`PEa` verified by the mute icon; no warning or fault. **NO-GO:** NAK on same-value and toggle, side effect, or QFLAG unusable → re-flash `savx`, record |
| **P2-M6** | Executor: priorities and currents | Check #14 (F3), battery-data gating, cross-key guards, reversal spacing with the baseline exemption, coupling list | 1–1.5 | `test_ctl_executor` F3/F4 cases green; HIL `pop` load case rejected (§11.5) |
| **P2-M7** | Cloud path | Rules §7.4 + emulator §11.2 + deploy; uplink ctl step; take/unknown handling; `ctl/in` kill/close; alert pin; ctl notification budget; event types | 3.5–4.5 | All §11.2 cases (the three [INFERRED] rule terms resolved or dropped and recorded); an owner `curl` request executes once; stale, replayed or doubled requests refused; window open → 5 s polling, closed → none; notices < 30 s by push and by e-mail |
| **P2-M8** | PWA "Inverter settings" | §8; DataSource + demo; re-auth; generated TS catalog; history from `ctl/res` | 2.5–3.5 | `npm run typecheck` green; the demo shows every state; on the real logger, `fx` from the iPhone completes in < 20 s; offline → Confirm disabled; unverified values greyed out |
| **P2-M9** | Per-value acceptance | §11.6 steps 3–7 for every remote value, local then remote, including the power-cycle checks; HIL §11.5 | 3–4 (over ≥ 3 days) | Each remote value has `in`/`out`; each key `rem` (+ `pc` where required) or is explicitly left disabled with a reason; audit, `ctl/res`, events and mails consistent |
| **P2-M10** | Soak with Phase 2 enabled + docs | 7 days of normal logging with the feature on; ≥ 3 windows and ≥ 5 remote changes; §16 doc updates | 1 + 7 days | v1 M8 criteria still met; **`status.sfr` delta = number of `ctl/res` records with a frame sent** (SR F28); zero `SETTING_LCD` false positives; zero lockouts not explained by a reset reason |
| **P2-M11** (optional) | Tier B keys | `INV_WRITE_TIER_B=1`; `pgr`, `fu` (not MAX24), `fv`, `fb`, `fz` enable; on-site acceptance per value, local only | 1.5–2 | As P2-M9, local only |

**Totals** (low–high):
- To the go/no-go (P2-M0…M5): **10.5–14.5 evenings**.
- Full remote feature (P2-M0…M10): **21.5–29 evenings + 7 days**.
- With Tier B: **23–31**.

The safety review added about 7–8 evenings over the draft's 14.5–21.5: physical confirmation, whole-snapshot and follow-ups, F3/F4 checks, fingerprint, alert integrity, keyed slot and server timestamps, per-value acceptance and power-cycle checks.

**Ordering rationale:**
- The cloud and PWA work (P2-M7/M8, about 6–8 evenings) is only spent after on-site feasibility.
- P2-M1 is useful even if Phase 2 stops at P2-M0 or P2-M5.

---

## 13. Rollback

Fastest first.

1. **Close the window**: PWA "Close now", the local page, or a triple-press. Nothing can be written.
2. **Kill switch**: PWA (`ctl/in/kill` + `cfg/ctlOff`) or local feature-off. Re-enabling needs the local page and a press.
3. **Revoke a key**: local page.
4. **Re-flash `savx`** (read-only) over OTA (with a press) or USB. It ignores `ctl/*` and publishes `meta.fwvar = "ro"`, so the PWA hides the controls.
5. **Rules**: remove the `ctl/in/req` owner grant (re-render, deploy). The cloud then refuses requests even from a stale PWA.
6. **Restore settings at the LCD** from the G5 table. **Never `PF`** (factory reset, `docs/02` §11.2).
7. **Account compromise:**
   - change the owner password: this revokes the other sessions' refresh tokens, though an issued ID token can stay valid for up to about 1 h [INFERRED, verify];
   - kill switch;
   - check `alerts/email`;
   - re-pin locally.

---

## 14. Risks

| # | Risk | L | I | Mitigation |
|---|---|---|---|---|
| R-P2-1 | The unit is REVO, so there is no setter path | M–H | M (sunk effort) | Zero-write decision at P2-M0; P2-M1 is useful anyway |
| R-P2-2 | The inverter ignores the input CRC, and a line error turns one setter into a neighbour | very L (M if the CRC probe says "ignored") | M–H | Whole-snapshot read-back; follow-ups; G11 (< 0.1 % link errors if CRC is ignored); revoke + close + sev 4 |
| R-P2-3 | Owner account compromised during a window | L | M | Physical window; single use; step-up re-auth; R-class values only; floors/caps; alert pin; local push channel; kill switch |
| R-P2-4 | ACK but not persisted across an inverter restart | M | L | `pc` acceptance bit; `SETTING_LCD`/`REVERTED` |
| R-P2-5 | Mode-dependent NAKs (U12) | M | L | `rejected/nak` with the mode recorded; widen modes only after tests |
| R-P2-6 | EEPROM wear | L | M | Rolling limits count every frame; `status.sfr` |
| R-P2-7 | Charge current too high or too low for this battery | L | H | §3.3 formulas from recorded Ah/BMS; no remote currents without them |
| R-P2-8 | Encoder or dialect bug | L | M–H | One catalog; byte vectors; golden replies; exact whitelist; fingerprint; per-value LCD acceptance |
| R-P2-9 | Executor pauses polling for ≤ 13 s | H | very L | Fast polls skipped, not failed; alert SLA test |
| R-P2-10 | Household member presses BOOT 3 times | L | L–M | Lid screwed; feature must be on; sev-4 notice; single use |
| R-P2-11 | `auth_time` not available in RTDB rules | M | L | Client step-up only; the window remains physical |
| R-P2-12 | `pop`→SOL/SbU overloads the battery or BMS anyway (load jumps after the check) | L | H | F3 margin (0.7 / 0.8 factors); UtI restore allowed in Battery mode; QFLAG `u/b/v` shown; owner keeps 06/23 decisions at the LCD |
| R-P2-13 | LAN attacker with replayed admin credentials | L | M | Every raising action needs a press; OTA needs a press (v1.x) |

---

## 15. Open questions

1. **Capacitive pad:** build it (P2-M3b), or accept opening the lid for every window?
2. **Window defaults:** are 10 min single use as default and 30 min / 5 changes as maximum right? The agreed principle said about 30 min. This design keeps 30 as the maximum but makes the default shorter (SR F7).
3. **Battery data:** brand, Ah and BMS charge/discharge ratings (G6). Without them, remote currents and SOL/SbU stay off.
4. **Power-cycle check:** is an inverter power cycle per key acceptable for `pop`, `pcp`, `mchg` and `muchg` (house output off for about a minute)?
5. **Tier B (P2-M11):** build it at all?
6. **Uppercase flags:** if lowercase `PEa` NAKs on a STOCK unit at P2-M5, should an uppercase variant be tried in catalog v3, or is that the stop?
7. **Remote changes while away from home:** allowed while a window is open. A "home Wi-Fi only" variant cannot be verified on the server.
8. **Should rejected attempts e-mail** at all, or push only?
9. **v1.x rules change** for `events` (owner writes limited to `ack`, SR F23): adopt after checking the PWA's ack writes?

---

## 16. Plan and doc changes when adopted; sources

### 16.1 Changes to other files (not made now)

- `docs/00` D11 and d1 §3.10: "D11 holds for `savx`; Phase 2 adds `savx-rw` (P2-D1…D12)". Fix the d1 §3.10 whitelist drift (§0.2 item 1).
- `docs/02` §11: add the generated setter table and the value classes. Add the escape cases to §4.4.
- `docs/04`: `settings`, `ctl/*`, `cfg/ctlOff`, `status.sfr`, `meta.fwvar`/`fp`.
- `docs/05`: the envs, `/api/ctl*`, gestures and staging, LED modes, NVS `ctl`, LittleFS audit, OTA press.
- `docs/06`: PWA page; rules table; emulator cases.
- `docs/07`: P2 milestones (or a pointer to `docs/10`).
- `docs/08`: rollback; troubleshooting for `rejected` reasons; new risks.
- `docs/site-survey.md`: BMS row in §9; 06/23/40/50 emphasis in §8; QFLAG `u/b/v` in B6.
- `firebase/README` §1; `shared/codes.json` `eventTypes`; v1.x `events` rules (§15 Q9).

### 16.2 Sources

**Project files read today:**
- `docs/00-plan-overview.md` §5, §8
- `docs/01-inverter-profile.md` §4.7, §4.10
- `docs/02-protocol-reference.md` §4, §7.7, §7.9, §7.10, §11
- `docs/03-hardware-and-wiring.md` N17, §3
- `docs/05-firmware-guide.md` §13.5
- `docs/07-milestones-and-test-plan.md`
- `docs/site-survey.md` §8, §9
- `firebase/database.rules.json` (`status`, `meta`, `events`, `cfg`, `alerts`)
- `firmware/include/config.h`, `platformio.ini`, `src/main.cpp`, `src/link/RawRing.h`, `src/net/TimeSync.h`, `src/agg/NotifyGates.h`, `src/proto/pi30/Pi30Adapter.cpp` (`classify`), `src/proto/pi30/Dialect.h`, `src/proto/Commands.h`, `src/web/LocalWeb.cpp`, `src/store/Settings.cpp`

**Via the draft and the review** (read there on the same day):
- `research/d1-architecture.md` §3.10, §9
- `research/r8-email-alerts.md` §3.6–3.7, §4.7
- `research/v1-protocol-verification.md` C14, V10, §2.2 A4
- `research/r1-oem-identification.md` §4.2
- `research/manual-extract.md`
- `research/user-constraints.md`
- `firmware/src/{link,proto,app,sys,net,web,store,model}`, `test/mocks/Mocks.h`
- `web/src/{app.tsx,data/*,pages/Settings.tsx}`

**Protocol documents** (local text copies, untrusted, read as text only), in `<scratch>`:
- `pi30_2014.txt` §2.6, §2.11–2.12, §3.1–3.16
- `pi30_2015.txt` §3
- `pi30max.txt` §3.3–3.31
- `pi30_gkmk.txt` §2.14–2.21, §3.3–3.12
- `pi30revo.txt` §1.5–1.7, §2, §4.3

Upstream: the mpp-solar `docs/protocols` folder, as listed in `docs/02` §15.

**Other sources:**
- mpp-solar protocols: https://github.com/jblance/mpp-solar/tree/master/mppsolar/protocols [SECONDARY reference implementation]
- ESPHome pipsolar: https://github.com/esphome/esphome/tree/dev/esphome/components/pipsolar [SECONDARY]
- DIY Solar thread 34404 (via r1 §4.2): https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/ [SECONDARY]
- mpp-solar issue #403: https://github.com/jblance/mpp-solar/issues/403 [SECONDARY]
- Firebase rules core syntax: https://firebase.google.com/docs/database/security/core-syntax [CONFIRMED-primary]
- Firebase offline capabilities: https://firebase.google.com/docs/database/web/offline-capabilities [not re-read today; INFERRED where used]
- Firebase docs on server timestamps, `auth.token` claims and token revocation: not re-read today; the dependent rule terms are [INFERRED] and gated by the P2-M7 emulator tests.

**Own computations:**
- `<scratch>`: §3.7 vectors, escape and zero-byte cases; self-test against `docs/02` §4.3–4.4.
- `…/scratchpad/p2final/qld.py`: the §4.4 QLD/QED escape dates.
