# R9: Phase 2 design draft (opt-in remote control of selected inverter settings)

**Status:** draft for review, 2026-10-21. Nothing here is implemented. **v1 stays strictly read-only.** This is a new file; no existing project file was changed.

**Scope.** After read-only logging has been proven, let the owner change a small, fixed set of harmless inverter settings from the PWA. Every change must pass a locally opened time window, an allow-list, range checks, an ACK and a read-back. Every change is logged and announced.

**Inputs:**
- the Phase 2 setter research relayed with this task (cited as **P2SET §n**);
- the firmware, web and rules code as read today;
- the project docs listed in §12.

**Evidence tags:**
- **[CONFIRMED-primary]**: read today in a primary document or in this repository's code. Code facts are marked "(code)".
- **[SECONDARY]**: a non-official source.
- **[INFERRED]**: design reasoning, still to be verified.

---

## 0. Summary

1. **The protocol may not allow this at all on this unit.** The unit is most likely on the Sorotec REVO platform. Sorotec's protocol document defines no individual setters, only the all-in-one `PSET`, and the field report says RS232 setting writes NAK on REVO units. [CONFIRMED-primary by absence, REVO doc §1.6; SECONDARY FORUM via r1 §4.2; P2SET §0.4]
   - The rest of this draft assumes a stock PI30 dialect.
   - The go/no-go decision is designed to cost **zero writes** where possible (§1.2), and at most one same-value write of the backlight flag (P2-M4).
2. **Tier A (remote-allowed) has eight keys:**
   - program 01 output priority (`POP`);
   - 02 max charging current (`MCHGC`/`MNCHGC`);
   - 11 max utility charging current (`MUCHGC`);
   - 16 charger priority (`PCP`);
   - flags 18 alarm, 19 return-to-default screen, 20 backlight and 22 source-interrupt beep (`PE`/`PD` with `a k x y`).
   - Tier B is local-only and sits behind a second compile flag.
   - Tier C is **absent from the binary**.
3. **Eight independent layers must all say yes** before one setter frame leaves the ESP32 (table below). The cloud alone can never cause a write.
4. **Read-back is the only proof.** An ACK without a matching read-back is never reported as "applied". A write is **never retried**; only the reads around it are.
5. **Effort:** about 7.5–11.5 evenings to reach the go/no-go point (P2-M4) and 14.5–21.5 evenings plus a 7-day soak for the full Tier A feature (§8).

| Layer | Gate | Where | Default |
|---|---|---|---|
| L0 | Separate build `savx-rw` with `INV_ALLOW_WRITES=1` **and** a catalog-version acknowledgement flag | `platformio.ini`, `config.h` | Not built. `savx` stays `default_envs` |
| L1 | Feature switch "remote settings" | NVS `ctl/feat`, local admin page only; off from the PWA via `cfg/ctlOff` | Off after flashing |
| L2 | Per-command on-site acceptance bit plus the on-site value envelope | NVS `ctl/acc`, `ctl/env`, local admin page only | All off |
| L3 | Local enable window (BOOT pattern or local admin page), about 30 min, RAM only | firmware | Closed. Closed at every boot |
| L4 | RTDB rules: owner-only, window open, window id matches, `ts` fresh, single slot, enum/range | `database.rules.json` | – |
| L5 | Executor pre-checks: catalog, tier for the origin, dialect, envelope, inverter state, rate limits, request id unseen | `ctl/CommandExecutor` | – |
| L6 | Exact-match setter table generated from the catalog; the final frame is re-checked | `link/SetterWhitelist` | – |
| L7 | ACK, then read-back compare; auto-revoke and close the window on any mismatch | `ctl/CommandExecutor` | – |
| (L8) | Notice on every change, rejection and window opening (ntfy + e-mail) | `AlertEngine` path | – |

### 0.1 Decision records (proposed)

| # | Decision | Rejected alternative | Why |
|---|---|---|---|
| P2-D1 | Separate build env `savx-rw`. `savx` stays default and read-only | A runtime flag in one binary | A read-only binary that physically lacks setter strings keeps v1's D11 guarantee intact (`docs/00` D11, d1 §3.10). |
| P2-D2 | The setter whitelist is a **generated table of complete command strings** (about 51 for Tier A), matched by `strcmp` | A regex or prefix rule such as `MUCHGC\d{3}` | Same exact-match philosophy as `Whitelist.cpp` (code). Tier C strings cannot exist in the binary. |
| P2-D3 | **Single-slot** request node `ctl/req`, enforced by the rules | A request collection `ctl/req/{id}` | The rules cannot count children, but they can refuse a write into an occupied slot. That enforces "one setting at a time" on the server [INFERRED]. |
| P2-D4 | The window is required for local **and** remote changes, lives in RAM only, and closes at boot | A persisted window | A reboot, OTA or brown-out must never leave control open. |
| P2-D5 | **Read-back decides the result**; ACK is advisory | Trust the ACK | Not every firmware's ACK is honest, and whether the inverter checks the CRC of incoming frames is undocumented (`docs/03` R5). |
| P2-D6 | **Never retry a write.** Retry only reads; the owner resubmits with a new id | Automatic retry on timeout | A lost ACK plus a retry means a double write (EEPROM wear, U17) and a confusing audit. |
| P2-D7 | **Envelope:** remote charge-current values can never exceed the value accepted on site | Manual range only | The 24 V lithium battery's BMS limit is unknown to the logger [INFERRED]. |
| P2-D8 | REVO: `PSET` is never used. Phase 2 is a no-go on REVO unless `PE`/`PD` **and** `QFLAG` read-back are proven | A read-modify-write `PSET` | P2SET §3 lists six reasons (all-or-nothing frame, layout uncertainty, checksum erratum, vendor statement). |
| P2-D9 | **Safe-direction actions are always allowed remotely:** close the window, switch the feature off | Local only | Remote can make things safer, never less safe. |
| P2-D10 | The settings snapshot (`/settings`) and LCD-change detection ship in **both** builds | rw build only | They are read-only (QPIRI/QFLAG are already polled), so the PWA page is useful even without Phase 2 [INFERRED]. |

### 0.2 Corrections and drift found while reading the code (today)

1. **`QMCHGCR` / `QMUCHGCR` are not on the v1 whitelist.** P2SET §1 says they are. `firmware/src/link/Whitelist.cpp` explains that the safety review removed them, with `QBEQI`, as unused and "one dropped leading byte" away from the `MCHGC`/`MUCHGC` setters. [CONFIRMED-primary (code)]
   - `docs/02` §11.1 and d1 §3.10 still list all three. That is doc drift; the code wins.
   - Phase 2 handling: §2.2.6.
2. **The `PGR00` vector note in P2SET §4 is wrong.** It says the high CRC byte 0x28 was adjusted to 0x29. The raw CRC is already **0x29EB**, so no adjust is involved; the wire bytes `29 EB 0D` are correct.
   - A genuine adjust case among the Tier A setters is **`MCHGC090`**: raw 0x0D8F → wire `0E 8F`.
   - Recomputed today with the `docs/02` §4 algorithm. The script reproduces the `docs/02` §4.3–4.4 vectors `QPI BE AC`, `QGMN 49 29` and `QBOOT 0B 88`. [CONFIRMED, own computation, scratchpad `p2design/crcv.py`]
3. **`config.h` stops any write build** with `#error` when `INV_ALLOW_WRITES != 0`. `[env:native]` hard-codes `-DINV_ALLOW_WRITES=0`. Phase 2 needs a deliberate guard change and a `native-rw` test env (§2.1). [CONFIRMED-primary (code)]
4. **BOOT held for 3 s already opens the Wi-Fi portal** (`main.cpp` `loop()`, `cfg::BOOT_HOLD_MS`). The window gesture must not be a hold (§3). The button is **inside the screwed clear-lid box** (`docs/03` §11 placement table, "Where you can open the lid"). [CONFIRMED-primary (code, docs)]
5. **`struct Pqse` does not parse `SSTUV` / `WW.W`** (`Pi30Parse.h`), and QFLAG is kept only as a raw string (`Identity::qflag`). The snapshot (§2.8) needs parsed flags. [CONFIRMED-primary (code)]
6. **Notification gates:**
   - Quiet hours make severity ≤ 3 log-only (`NotifyGates.h`).
   - Built-ins are e-mailed only when `sev ≥ builtinMinSev` (default 4); only `TEST_ALERT` is always e-mailed (r8 §4.7).
   - Setting notices therefore need severity 4 or a forced flag (§6). [CONFIRMED-primary (code, r8)]
7. **Local-page CSRF exposure.** The local web uses HTTP Basic auth, and `/update` triggers the browser's credential prompt (`LocalWeb.cpp` `AdminMiddleware`). Browsers may then attach the cached credentials to cross-site form POSTs. New `/api/ctl/*` POST routes need a CSRF guard (§3.3). [INFERRED from code + Basic-auth caching behaviour]
8. **No USB-serial adapter exists** (`research/user-constraints.md`), and `docs/03` §3 says "use one USB cable at a time" (N13). The "Python simulator via USB-serial" bench is therefore adapted (§7.4).

---

## 1. Rollout gates and preconditions

### 1.1 Hard preconditions (all required before flashing `savx-rw` on the inverter)

| # | Gate | Evidence |
|---|---|---|
| G1 | v1 milestones **M0–M3 accepted**, including the LCD cross-check. The cloud milestones P2-M5 and later also need **M5–M7** (uplink, PWA, alerts and e-mail). | `docs/07` acceptance boxes |
| G2 | **At least 7 consecutive days of stable read-only logging**, in practice the **M8 soak**: at least 99.5 % of minutes present, link errors below 1 %, zero false severity-5 alerts | `docs/07` M8, soak checklist |
| G3 | **Dialect known and stored**: `docs/probe-report.json` names STOCK17, STOCK21, MAX24, GK or REVO, and `meta.sup` lists which optional reads answer (QFLAG in particular) | `docs/07` M3 step 8; `Pi30Adapter::probe` (code) |
| G4 | **QPIRI/QFLAG ↔ LCD mapping table complete** for fields 8, 13, 14, 15, 16, 17 and 22 and letters a, k, x, y. The owner records each LCD program value next to the decoded field (P2SET U7, U8) | `docs/01` §7, `docs/site-survey.md` |
| G5 | **A dated photo and table of every LCD program value** (00–50), so any setting can be restored by hand | new row in `docs/site-survey.md` (on adoption) |
| G6 | The firmware is unchanged since the soak, apart from Phase 2 code. `pio test -e native` and `-e native-rw` pass. CI binary checks pass (§7.3) | CI |
| G7 | The owner has read §7.6 (rollback) and accepts residual risk R-P2-3 (owner account compromise while a window is open) | sign-off line in the build record |
| G8 | **Per-command:** an on-site acceptance test passed for that command (§7.5) before its `acc` bit can be set; the remote path is enabled per command | NVS `ctl/acc` |

### 1.2 Go/no-go decision (zero writes first)

```
probe-report dialect
├─ REVO ──► does QFLAG answer (meta.sup contains QFLAG)?
│            ├─ no  ──► NO-GO: no read-back source for flags, no documented individual setters.
│            │          Stop Phase 2. Keep D10 read-only snapshot. (P2SET §0.4, U13)
│            └─ yes ──► P2-M4 feasibility with ONE same-value PEx (backlight is on).
│                        NAK → NO-GO (confirms forum report). ACK + QFLAG unchanged → GO for flags only.
├─ STOCK17 / STOCK21 / GK / MAX24 ──► P2-M1…M4, then P2-M4 feasibility (same-value PEx, then PDx/PEx toggle)
│            NAK on same-value AND toggle ──► NO-GO (setters not accepted on this firmware)
└─ UNKNOWN / RAW / non-PI30 ──► NO-GO
```

A **same-value write** re-sends the value just read back. An ACK then proves the command is accepted while changing nothing (P2SET §5). [INFERRED]

### 1.3 Per-command enablement ladder

```
absent (Tier C, never in binary)
compiled (in savx-rw catalog) ──local same-value test OK + visible test OK + owner ticks "verified on LCD"──►
accepted-local (acc bit, local page may use it) ──one remote change verified with owner at the LCD──►
enabled-remote ──any read-back mismatch / unexpected value / NAK-but-changed──► revoked (acc bit cleared, window closed, sev 4 notice)
revoked ──repeat on-site acceptance──► accepted-local
```

---

## 2. Firmware

### 2.1 Build environments and the compile guard

```ini
; Phase 2 (research/r9 §2.1). NEVER in default_envs. Flash only after the §1.1 gates.
[env:savx-rw]
extends = env:savx
build_unflags = ${env:savx.build_unflags} -DINV_ALLOW_WRITES=0
build_flags =
  ${env:savx.build_flags}
  -DINV_ALLOW_WRITES=1
  -DINV_RW_CATALOG=1          ; must equal the generated CTL_CATALOG_VERSION
  -DINV_WRITE_TIER_B=0        ; Tier B local-only setters: P2-M9, off by default

[env:savx-rw-bench]           ; P2-M3 only: fake inverter on UART2, RAM-only link state
extends = env:savx-rw
build_flags = ${env:savx-rw.build_flags} -DBENCH_FAKE_INV=1

[env:native-rw]
extends = env:native
build_unflags = -DINV_ALLOW_WRITES=0
build_flags = ${env:native.build_flags} -DINV_ALLOW_WRITES=1 -DINV_RW_CATALOG=1 -DINV_WRITE_TIER_B=1
build_src_filter = ${env:native.build_src_filter} +<ctl/> +<link/SetterWhitelist.cpp>
```

- PlatformIO's `extends` and `build_unflags` must remove `-DINV_ALLOW_WRITES=0` from the inherited flags. Check with `pio run -e savx-rw -v` that only `=1` reaches the compiler. [INFERRED: verify PlatformIO flag-merge order before relying on it]
- The `config.h` guard becomes:

```cpp
#if INV_ALLOW_WRITES != 0
#  if !defined(INV_RW_CATALOG) || INV_RW_CATALOG != CTL_CATALOG_VERSION
#    error "Writes need env savx-rw and a matching settings catalog (research/r9 §2.1)"
#  endif
#  define FW_VARIANT "rw"
#else
#  define FW_VARIANT "ro"
#  if defined(INV_WRITE_TIER_B) && INV_WRITE_TIER_B
#    error "INV_WRITE_TIER_B needs INV_ALLOW_WRITES=1"
#  endif
#endif
```

- `FW_VARIANT` is published as `meta.fwvar` (key ≤ 12 chars, which the `meta` rule allows) and in `/api/status`.
  - The PWA shows the Inverter-settings controls only when `fwvar === "rw"`.
  - The local page shows a permanent "rw build" badge.
- Every Phase 2 source file (`src/ctl/*`, `link/SetterWhitelist.*`, `SerialLink::writeSetting`) is wrapped in `#if INV_ALLOW_WRITES`. The `savx` binary therefore contains **no setter string and no write path**, which CI checks (§7.3).

### 2.2 SettingsCatalog

**Single source:** `shared/settings-catalog.json`. A new generator, `tools/gen_settings.py`, follows the r8 pattern for `alert-metrics.json`, which already "must assert that the metric regex in `database.rules.json` lists exactly these ids" (r8 §4.3). It emits:

- `firmware/include/settings_catalog.h`: entries, encoders' string tables, the generated setter table (§2.3), and `CTL_CATALOG_VERSION`;
- `web/src/settingsCatalog.ts`: labels, LCD texts, consequences (§5.3), ranges and tiers;
- a **check** that the `key`/`val` validation in `database.rules.json` (§4.4) matches the catalog's Tier A keys and ranges.

#### 2.2.1 Entry fields

```cpp
namespace ctl {
enum class Tier : uint8_t { A = 0, B = 1 };          // Tier C entries are never generated
enum class Rb : uint8_t { QPIRI, QFLAG };            // read-back source (REVO PQSE: none in v1 catalog)
struct Def {
  const char* id;         // "pop" (<= 6 chars, RTDB key)
  uint8_t prog;           // LCD program number, e.g. 1
  Tier tier;
  uint8_t dialects;       // bitmask STOCK17|STOCK21|MAX24|GK. REVO never set (P2-D8)
  uint8_t modes;          // allowed QMOD letters bitmask (L, S, B ...); initial values from §2.2.2
  bool standbyOnly;       // reserved: no Tier A entry needs it today (F50/F60 would; they are Tier C)
  Rb rb; int8_t rbIdx;    // QPIRI 0-based index, or -1
  char flag;              // QFLAG letter, or 0
  int16_t vmin, vmax, vstep;  // canonical value range (x10 for voltages); or enum list below
  const int16_t* enumVals; uint8_t enumN;
  bool enableOnly;        // e.g. Tier B 'z': only PEz may be sent
  // encoder: canonical value + dialect -> exact command string from the generated table, or nullptr
};
}
```

**Canonical values are LCD semantics**, never wire codes. Example: `pcp` 0 = CUt, 1 = CSO, 2 = SNU, 3 = OSO. Per-dialect encoders map them to wire codes. The PWA and RTDB never see dialect codes. [INFERRED]

#### 2.2.2 Tier A entries (remote-allowed after acceptance)

| id | LCD prog | Canonical values (24 V) | Command per dialect [CONFIRMED-primary unless noted] | Read-back | Initial modes | Notes |
|---|---|---|---|---|---|---|
| `pop` | 01 | 0 UtI, 1 SOL, 2 SbU [MAN p.13] | All non-REVO: `POP00` / `POP01` / `POP02` (P14 §3.4, P15 §3.8, PMAX §3.9, PGK) | QPIRI[16] | L, S | **Never `POP03`** (ESPHome "hybrid", undocumented) [SECONDARY ESPH] |
| `mchg` | 02 | 10…90 A step 10 (STOCK, GK); 10…100 A (MAX24); always ≤ envelope `env.mchgMax` | STOCK17/21: `MCHGC0nn` (P14 `<mnn>` with m = 0 is the same bytes as P15 `<nnn>`). MAX24: `MNCHGC0nnn`. GK: `MNCHGCnnn` (PGK §3.3; "KING" uses `<mnnn>`) | QPIRI[14] | L, S | STOCK is capped at 90 A because P14 reads `MCHGC100` as "unit 1, 00 A" (P2SET §2 prog 02) [INFERRED]. GK stays not-accepted until U5 settles 3 vs 4 digits. |
| `muchg` | 11 | 2, 10…80 step 10; ≤ current `mchg` and ≤ `env.muchgMax` | All non-REVO: `MUCHGC002`, `MUCHGC010` … `MUCHGC080` (identical bytes under P14/P15/PGK/PMAX) | QPIRI[13] | L, S | Manual: if 02 < 11, the 02 value is used [MAN p.14]. The 4-digit LV6548 form is not used [SECONDARY]. |
| `pcp` | 16 | 0 CUt, 1 CSO, 2 SNU, 3 OSO [MAN p.16] | Map per installation, `env.pcpMap`, set at acceptance from U7: **HS** 0→`PCP00` … 3→`PCP03`; **MAX/GK** 1→`PCP01`, 2→`PCP02`, 3→`PCP03` (no CUt) | QPIRI[17] via the same map | L, S | Not acceptable until `pcpMap` is set. Applies in Line/Standby/Fault only [MAN p.16]. Never `PPCP` (parallel). |
| `fa` | 18 | 0 bOF, 1 bON | `PDa` / `PEa` | QFLAG `a` | any except F, P | E = alarm on [INFERRED, matches the LCD default bON] |
| `fk` | 19 | 0 HEP, 1 ESP | `PDk` / `PEk` | QFLAG `k` | any except F, P | Display only |
| `fx` | 20 | 0 LOF, 1 LON | `PDx` / `PEx` | QFLAG `x` | any except F, P | **First live test** (visible) |
| `fy` | 22 | 0 AOF, 1 AON | `PDy` / `PEy` | QFLAG `y` | any except F, P | |

Rules for the flags:
- One **lowercase** letter per frame (U3). PMAX documents `PE<X>/PD<X>` with lowercase letters, and QFLAG replies use lowercase; P14 prints uppercase. [CONFIRMED-primary PMAX §3.3, P14 §3.1]
- Uppercase variants are **not** in catalog v1. If lowercase NAKs on a STOCK unit at P2-M4, a catalog v2 decision is needed (§10 Q5).

"Initial modes" restricts writes to Line or Standby (QMOD `L`/`S`) until U12 shows Battery mode is safe. Flags are allowed in every mode except Fault and Power-on. [INFERRED]

#### 2.2.3 Tier B entries (local page only, compiled only with `INV_WRITE_TIER_B=1`, P2-M9)

| id | Prog | Values | Command | Read-back | Notes |
|---|---|---|---|---|---|
| `pgr` | 03 | 0 APL, 1 UPS | `PGR00` / `PGR01` | QPIRI[15] | Changes the transfer thresholds and the transfer time (20/10 ms) [MAN §8]. NAK may arrive without a CRC. |
| `fu` | 06 | 0 LtD, 1 LtE | `PDu` / `PEu` | QFLAG `u` | On PMAX, `u` also covers "battery over discharge restart" |
| `fv` | 07 | 0 tFd, 1 tFE | `PDv` / `PEv` | QFLAG `v` | |
| `fb` | 23 | 0 byd, 1 byE | `PDb` / `PEb` | QFLAG `b` | |
| `fz` | 25 | 1 FEn **only** (`enableOnly`) | `PEz` | QFLAG `z` | Disabling it loses diagnostics, so `PDz` is never generated |
| `pbcv` | 12 | 22.0…25.5 V step 0.5 | `PBCV22.0` … `PBCV25.5` | QPIRI[8] | **Only if** QPIRI[12] ≠ the LIb code **and** U9 passes. The lithium SOC variant has **no command**, so it is LCD-only. |
| `pbdv` | 13 | FUL (`00.0`) or 24.0…29.0 V step 0.5, and > `pbcv` | `PBDV00.0`, `PBDV24.0` … `PBDV29.0` | QPIRI[22] | Same LIb condition. The SOC variant is LCD-only. |

On this installation the battery is lithium, so 05 is probably LIb and 12/13 are SOC. `pbcv`/`pbdv` are then expected to stay **not acceptable** (P2SET U9). [INFERRED]

#### 2.2.4 Tier C and LCD-only (never in the catalog; displayed read-only where readable)

| Program / command | Tier | Read-only display source |
|---|---|---|
| 05 battery type `PBT` | C | QPIRI[12] (LIb code still unknown, U10) |
| 09 frequency `F50`/`F60`; 10 voltage `V2xx` | C | QPIRI[3], QPIRI[2] |
| 26 bulk `PCVV`, 27 float `PBFT`, 29 cut-off `PSDV` (voltage variants) | C | QPIRI[10], [11], [9] |
| 30–36 equalization `PBEQ*` | C | none (QBEQI is not whitelisted, §0.2) |
| 43 feed to grid `PEd`/`PDd` (PMAX "reserved") | C | QFLAG `d` if present |
| 12, 13, 29 lithium SOC variants | LCD only (no command) | QPIRI reports unknown (U9) |
| 40, 42, 44, 45, 50 | LCD only (no known command) | none |
| `PF`, `PSET`, `~S`, `PEj`/`PDj`, `PCVT`, `PCST`, `MSCHGC`, `POPM`, `PPCP`, `POPLG`, `PPVOKC`, `PSPB`, `DAT`, `RTEY`, `RTDL`, `PBATCD`, `PBATMAXDISC`, `BTA*`, `ATE0`/`ATE1`, `PLED*`, `PBMS`, `PSAVE` | C, never | – |

Source: P2SET §2 and the never-expose list; `docs/02` §11.2. [CONFIRMED-primary]

#### 2.2.5 Encoders: exact strings only

An encoder never formats a number into a command with `snprintf`. It **looks up** the canonical value in the generated per-dialect string table, for example `MCHGC_STOCK[] = {"MCHGC010", …, "MCHGC090"}`, and returns `nullptr` for anything else. The CRC is computed by the existing `pi30::buildCrcFrame`. [INFERRED design; strings CONFIRMED-primary per §2.2.2]

#### 2.2.6 Envelope and allowed-value lists

`env` (NVS blob, local admin page only) holds:
- `mchgMax` and `muchgMax`: defaults are the **values read at acceptance time**, so remote can lower and later restore, never exceed (P2-D7);
- `pcpMap`: `none`, `HS` or `MAXGK`;
- optional allowed lists from `QMCHGCR` / `QMUCHGCR`.

`QMCHGCR` / `QMUCHGCR` (§0.2 item 1):
- They are **not** re-added to the scheduler.
- The rw build may send each **once, from the local page during on-site acceptance**, with the owner present, and stores the parsed list (`(010 020 …`, P14 §2.11–2.12) in `env`.
- A NAK or timeout means "use the manual range". This keeps the safety review's concern confined to deliberate on-site moments. [INFERRED]

### 2.3 Setter whitelist and the write path

```cpp
// link/SetterWhitelist.h  - compiled only when INV_ALLOW_WRITES == 1. Host-portable.
namespace wlw {
struct SetterEntry { const char* cmd; uint8_t dialects; uint8_t tier; uint8_t key; };
extern const SetterEntry SETTERS[];      // generated from the catalog (settings_catalog.h)
extern const size_t SETTERS_N;           // ~51 Tier A (+ ~29 Tier B when INV_WRITE_TIER_B=1)
// exact strcmp over SETTERS; tier B entries only if compiled AND tierBAllowed; never REVO
const SetterEntry* setterAllowed(const char* cmd, Dialect d, bool tierBAllowed);
// re-parse a fully built CRC frame (cmd + CRC + CR), printable ASCII, CRC valid, then setterAllowed()
bool setterFrameAllowed(const uint8_t* frame, size_t n, Dialect d, bool tierBAllowed);
}

// link/SerialLink.h addition
#if INV_ALLOW_WRITES
class WriteToken { friend class CommandExecutor; WriteToken() {} };   // only the executor can mint one
// ONE frame, CRC framing only (no setter uses CHK). No retry inside. Ring-logged with tag "SET".
LinkResult writeSetting(const WriteToken&, const char* cmd, Dialect d, bool tierB, Reply& out,
                        uint32_t firstByteMs);
#endif
```

**Gate order inside `writeSetting`:**
1. Compile flag.
2. Token, so nothing but the executor (no console command, no probe) can call it.
3. `setterAllowed`.
4. Armed and configured.
5. Build the frame.
6. `setterFrameAllowed`.
7. One `transact`.

**The existing read path is untouched.** `SerialLink::query()` still refuses every setter (`wl::cmdAllowed`). The existing tests `test_frames` "bad" list and "setters refused with zero bytes" keep running in both builds. [CONFIRMED-primary (code)]

### 2.4 CommandExecutor

`ctl/CommandExecutor.{h,cpp}` is host-portable and runs **inside `invTask`**, the only owner of `SerialLink` (`Tasks.cpp`). It is fed through an `App` queue, like `DiagRequest` (`postCtl` / `takeCtl`). It is **single-flight**: one request at a time; others wait or are rejected with `busy`.

#### 2.4.1 Request

```cpp
struct CtlRequest {
  char id[20];       // "<10-digit ts>-<8 [a-z0-9]>", unique, unguessable (the nonce is folded into the id)
  char wid[9];       // window id the request was made for
  char key[7];       // catalog id
  int16_t val;       // canonical (x10 for voltages)
  uint32_t ts;       // owner's click time (server-offset corrected), epoch s
  uint8_t origin;    // REMOTE (RTDB) | LOCAL (admin page)
  bool force;        // LOCAL acceptance only: same-value write even if before == target
};
```

#### 2.4.2 Pre-checks, in order (nothing is sent if any fails → `rejected`)

| # | Check | Reason code (≤ 12 chars) |
|---|---|---|
| 1 | Feature switch on (`ctl/feat`) | `feature` |
| 2 | Window open, `wid` equals the current window id, not expired | `window` |
| 3 | Valid clock (NTP or inverter clock); `now − ts ≤ 120 s` and `ts ≤ now + 60 s` | `time` / `expired` |
| 4 | `id` not in the persisted ring and `ts > lastTs` | `dup` |
| 5 | Key known, catalog version matches | `key` |
| 6 | Tier allowed for the origin: REMOTE → A; LOCAL → A, and B if compiled | `tier` |
| 7 | Dialect supported (never REVO) | `dialect` |
| 8 | `acc` bit set for the key (REMOTE needs `enabled-remote`; LOCAL needs `accepted-local`, or `force` acceptance mode) | `notacc` |
| 9 | Value valid: enum/range/step, envelope, `muchg ≤ mchg`, `pbdv > pbcv`, `enableOnly` | `range` |
| 10 | Engine `RUN`, no probe or re-probe running, link OK for ≥ 5 min, last fast sample < 15 s old, uptime > 2 min | `state` |
| 11 | QMOD letter in the entry's `modes`; no QPIWS fault bit; no open `INVERTER_FAULT` / `BATT_CRITICAL` | `fault` |
| 12 | Rate limits (§2.4.6) | `rate` |
| 13 | Pre-read (read-back command, ≤ 3 tries) succeeded → `before` | `preread` |
| 14 | `before == target` and not `force` → result `noop`, **zero frames sent** | (`noop`) |

#### 2.4.3 State machine

```
IDLE ─take─► CHECK ─fail─► DONE(rejected, why)
               │ok
               ▼
            PREREAD ─fail─► DONE(rejected, preread)
               │ before==target && !force ─► DONE(noop)
               ▼
            COMMIT: NVS ctl/inflt = {id,key,val,before,t}; id→ring; lastTs; counters++   (before any byte)
               ▼
            SEND: writeSetting() ONCE  → reply class ACK | NAK | NONE (timeout/garbage/bad CRC)
               ▼
            SETTLE: wait SET_SETTLE_MS (1500)
               ▼
            VERIFY: read-back query, ≤ 3 tries, 1 s apart → after | unreadable
               ▼
            CLASSIFY (§2.4.5) ─► DONE(applied|rejected|failed) ; clear ctl/inflt ; audit ; snapshot ; event
```

- **Boot recovery.** If `ctl/inflt` exists at boot, a write was interrupted, for example by a brown-out between COMMIT and DONE.
  - The executor sends **nothing**.
  - It waits for `RUN`, does one read-back, and publishes `failed/reboot` with `before` and `after`. This counts as a mismatch for acceptance purposes if `after` is not `before` or the target.
  - It then clears the marker.
- **Engine interplay.** While the executor runs (≤ about 13 s worst case), `invTask` does not call `engine.tick()`.
  - Fast polls are skipped, not failed, so `consecutiveFails` does not grow.
  - The executor's own successful reads refresh `lastOkMs`.
  - `IClock::sleepMs` already feeds the task watchdog (`Port.h`; WDT 30 s). [CONFIRMED-primary (code)]
- After DONE, the executor calls `sched.trigger(QPIRI)` and `sched.trigger(QFLAG)`, as `onConfigChangedBit` does, so the regular snapshot also refreshes.

#### 2.4.4 Reply classification

| Raw reply | Class | Basis |
|---|---|---|
| `(ACK` + valid CRC (`39 20`) + CR → `LinkResult::OK` with payload exactly `ACK` | **ACK** | P14 §3; `checkCrcReply` (code) |
| `(NAK` with or without a CRC → `LinkResult::NAK` | **NAK** | Several sections show `(NAK<cr>` with no CRC (P14 §3.1); `checkCrcReply` matches the prefix (code) |
| Timeout, `BAD_START`, `BAD_CRC`, other payload | **NONE** (unknown) | Never treated as ACK or NAK |

#### 2.4.5 Outcome matrix (normal, non-`force` request; `target ≠ before`)

| Reply \ read-back | `after == target` | `after == before` | Other value | Unreadable |
|---|---|---|---|---|
| ACK | **applied** | **failed** `mismatch`, auto-revoke | **failed** `unexpected`, revoke, close window | **failed** `verify`, close window |
| NAK | **failed** `nakchg`, revoke | **rejected** `nak` | **failed** `unexpected`, revoke, close window | **failed** `verify`, close window |
| NONE | **applied** (`why: noack`) | **failed** `noack` (**no retry**; the owner may resubmit) | **failed** `unexpected`, revoke, close window | **failed** `verify`, close window |

**Acceptance (`force`, same-value) mode:**
- ACK with `after == before` → `applied` (test passed).
- NAK with `after == before` → `rejected/nak` (setter unsupported).
- Anything else → `failed`, and the key stays not acceptable.

Every `failed` raises a severity-4 notice that names the LCD program to check (§6).

#### 2.4.6 Timing and rate limits (initial values, [INFERRED]; tune from U4 and U17)

| Constant | Value | Note |
|---|---|---|
| `SET_FIRST_BYTE_MS` | 3000 | The ACK may come after an EEPROM write; the normal read timeout is 1500 (`cfg::FIRST_BYTE_MS`) |
| `SET_SETTLE_MS` | 1500 | Before the first read-back |
| `VERIFY_TRIES` / gap | 3 / 1000 ms | Reads only |
| Minimum gap between setter frames | 60 s | Global |
| Setter frames per local day | 10 remote + 20 local | Counts every frame sent, including failures and acceptance tests (U17, EEPROM wear) |
| Per key per day (remote) | 4 | |
| Window auto-close | 3 `rejected` or 1 `failed` in one window | |
| Request age | ≤ 120 s at execution | §2.4.2 #3 |

### 2.5 Persistence (new NVS namespace `ctl`, added to the `Settings.h` header list)

| Key | Type | Written | Purpose |
|---|---|---|---|
| `feat` | u8 | Local page; `cfg/ctlOff` | L1 feature switch |
| `acc` | u32 | Local page; auto-revoke | Bit per catalog key: accepted-local / enabled-remote (2 bits each) |
| `env` | blob | Local page | `mchgMax`, `muchgMax`, `pcpMap`, allowed lists |
| `ids` | blob | Each COMMIT | Ring of the last 16 request ids (16 × 20 B) |
| `lastTs` | u32 | Each COMMIT | Monotonic replay barrier |
| `inflt` | blob | COMMIT / DONE | Interrupted-write marker (§2.4.3) |
| `day` | blob | Each send | `{localDay, sendsRemote, sendsLocal, perKey[8]}`; survives reboots, so the limits cannot be reset by power-cycling |
| `audit` | blob | Each DONE | Ring of 32 audit records (§2.6) |
| `ctlOffSeen` | u32 | cfg poll | Last applied `cfg/ctlOff` |

- About 5 NVS writes per change and at most 30 changes a day: negligible wear. [INFERRED]
- **The window is never persisted** (§3).
- Replay after a reboot is blocked three ways:
  1. the window is closed at boot;
  2. a new `wid` is drawn for the next window;
  3. `ids` and `lastTs` persist.

### 2.6 Audit log

- **Device:** an NVS ring of 32 × 32 B records `{ts, keyIdx, origin, state, why, before×10, after×10, val×10, id[8], ms}`. It survives a LittleFS reformat and is shown on the local page (`GET /api/ctl`).
- **Cloud:**
  - `ctl/res/{date}/{id}` is the full history, pruned like `events` (`keepEventDays`);
  - the matching `events` entry and e-mail (§6) carry the human-readable story.
- **Raw:** the `RawRing` entry tagged `SET` keeps the exact request and reply bytes (`/api/raw`, `diag/raw/SET`). [INFERRED: `diag/raw/{cmd}` keys are ≤ 12 chars, so `SET` fits the rule]

### 2.7 Integration points (new and changed modules)

| Module | Change |
|---|---|
| `src/ctl/SettingsCatalog.{h,cpp}` (new) | Entries from the generated header; validation; encoders |
| `src/ctl/CommandExecutor.{h,cpp}` (new) | §2.4; dependencies injected (link, clock, persist, state view) so it is native-testable |
| `src/ctl/CtlWindow.{h,cpp}` (new) | §3; pure logic (gesture decoder, expiry), native-tested |
| `src/link/SetterWhitelist.{h,cpp}` (new), `SerialLink` (changed) | §2.3 |
| `src/app/App.{h,cpp}` | `postCtl` / `takeCtl`, `setCtlResult` / `takeCtlResult`, `ctlState()` snapshot (window, feat, counters) |
| `src/app/Tasks.cpp` `invTask` | `while (app.takeCtl(rq)) executor.run(rq)` between ticks; boot recovery of `inflt` |
| `src/main.cpp` `loop()` | BOOT gesture decoder next to the existing 3 s hold; LED mode `CTL_WINDOW` |
| `src/sys/StatusLed` | New modes `CTL_WINDOW` and `CTL_WRITE` (§3.2) |
| `src/net/Uplink.cpp` | New step "ctl" between alerts (1) and live (2): GET `ctl/req` every 5 s **only while the window is open**; PATCH take/result/window; `cfg.ctlOff` handling |
| `src/net/UplinkJson.cpp` | Strict parsers and serialisers for req/res/window/settings (unknown fields → reject) |
| `src/web/LocalWeb.cpp` | `/api/ctl*` routes (§3.3); `needsAdmin()` gains the `/api/ctl` prefix |
| `src/agg/AlertEngine` / `NotifyGates` | Types `SETTING_*` / `CTL_*` forced like `TEST_ALERT` (§6) |
| `src/proto/pi30/Pi30Parse` | Parsed QFLAG struct; PQSE `SSTUV` / `WW.W` (read-only, useful for v1 too) |
| `data/www/index.html` | "Inverter settings" section (§3.3) |

### 2.8 Settings snapshot and LCD-change detection (both builds, P2-D10)

- **Source.** `Identity` already decodes QPIRI fields 2, 3, 8–17 and 22 (`Pi30Adapter::handle`, code). QFLAG is added as a parsed struct.
- **Publish.** The device PUTs `/devices/inv1/settings` (§4.2) when the canonical snapshot hash changes, checked at most every 60 s, and immediately after the executor finishes.
- **Change not made by the executor.** If a snapshot differs from the previous one and no executor DONE for that key happened in the last 5 min, the device raises `SETTING_LCD` (severity 2). Examples: an owner change at the LCD, or a reverted value after a power cycle (U11).
  - Ignore the first snapshot after boot and after a dialect change.
  - Require two consecutive identical reads before announcing, to filter misreads. [INFERRED]

---

## 3. Local enable window

### 3.1 Opening, duration, closing

| Aspect | Design |
|---|---|
| **Open, physical** | **Three short BOOT presses**: each press under 600 ms, all three within 3 s, then release. The LED shows the window colour, and the window opens **only if the feature switch is on**. This never collides with the existing **3 s hold** (Wi-Fi portal, `main.cpp`): the decoder aborts if any press lasts more than 1 s. Pressing BOOT's cap with the RJ45 plugged in is allowed (`docs/03` N17). The lid must be unscrewed, which makes this a strong physical-presence factor. |
| **Open, LAN** | Local page → Inverter settings → "Allow remote changes". Requires the admin password, a typed confirmation `OPEN`, and the CSRF guard (§3.3). LocalWeb is LAN-only. |
| Optional hardening | NVS `ctl/winsrc = btn`, settable on the local page, disables the LAN way of opening. |
| Duration | 30 min default, selectable 5–60 min on the local page. **No extension**: open it again. `until ≤ now + 3700 s` is also enforced by the rules. |
| Window id | 8 random `[a-z0-9]` from `esp_random()`, new for every opening |
| Close | Expiry; local page "Close"; PWA "Close now" (`key: "close"`); a second triple-press |
| Auto-close | Reboot (never persisted); safe mode; OTA start; Wi-Fi portal opened; engine leaves `RUN` or re-probes; link lost; QMOD `F`; quota guard hard limit; 3 `rejected` or 1 `failed` in this window; feature switched off (`cfg/ctlOff` or local) |
| On close | PATCH `{ctl/window: {open:false, why, …}, ctl/req: null}` |
| Notices | Opening → `CTL_WINDOW` severity 4 (push + e-mail). Normal close → severity 2. Auto-close after a failure → severity 4 (§6). |

### 3.2 LED indication

The existing priority in `main.cpp` is SAFE > PORTAL > PROBING > LINK_LOST > WIFI_DOWN > BACKFILL > LINK_OK. `CTL_WINDOW` goes **after LINK_LOST** and before WIFI_DOWN:

| Mode | Pattern | Colour |
|---|---|---|
| `CTL_WINDOW` | Double blink every 2 s | White (dim, L = 24) |
| `CTL_WRITE` | Solid during a transaction | White |
| Gesture accepted but feature off | 3 fast red blinks | Red |

On some clones the LED needs a solder bridge (`StatusLed.h`), so the local page and the PWA are the authoritative indicators. [CONFIRMED-primary (code)]

### 3.3 Local page and API (rw build)

| Route (all admin) | Purpose |
|---|---|
| `GET /api/ctl` | Feature, window, catalog with acceptance bits, envelope, today's counters, audit ring, last result |
| `POST /api/ctl/feature` `on=1&confirm=ENABLE-REMOTE-SETTINGS` / `on=0` | L1 switch. Off is always allowed. |
| `POST /api/ctl/window` `open=1&min=30&confirm=OPEN` / `open=0` | Window |
| `POST /api/ctl/test` `key=fx` | Same-value acceptance write (`force`), window required |
| `POST /api/ctl/set` `key=…&val=…&confirm=<key>=<val>` | LOCAL-origin change (Tier A, or B if compiled), window required |
| `POST /api/ctl/accept` `key=…&lcd=1&remote=0\|1` | Set `accepted-local`. `remote=1` sets `enabled-remote`, only after a verified remote change with the owner at the LCD (§7.5) |
| `POST /api/ctl/revoke` `key=…` | Always allowed |
| `POST /api/ctl/env` `mchgMax=…&muchgMax=…&pcpMap=…` | Envelope. Values above the current inverter value need `confirm=RAISE` |
| `POST /api/ctl/qcr` | One-shot `QMCHGCR` / `QMUCHGCR` read (§2.2.6), window required |

- Handlers only post to the `invTask` queue and return `202` with the request id. The page polls `GET /api/ctl`. Nothing blocks the web server.
- **CSRF guard** [INFERRED]:
  - require the custom header `X-Ctl: 1`. A cross-site form cannot set it, and the ESP answers no CORS preflight;
  - require `Host` / `Origin` to be `solar.local` or the device IP;
  - require the typed `confirm` value.
- **No serial-console setter command.** The console gets only `ctl status`.

---

## 4. Data contract (RTDB)

### 4.1 Tree additions under `/devices/inv1/`

```
settings                          device PUT on change (both builds)                      ~350 B
ctl/window                        device PUT on every window change; boot: {open:false}   ~110 B
ctl/req                           owner SET into an empty slot; device/owner DELETE        ~150 B
ctl/res/{YYYY-MM-DD}/{id}         device: "pending" at take, final at DONE; pruned like events  ~220 B
cfg/ctlOff                        owner (kill switch, epoch s), read by the 60 s cfg poll
events/{d}/{ts}-SETTING_*-i       device (existing event path, §6)
mailq/{ts}-SETTING_*-i            device (existing outbox path, §6)
```

The date in `ctl/res` is the **local Asia/Dhaka date of the request `ts`**. Both sides can compute it, and it reuses the date-keyed pruning of `events` (`Uplink.cpp` prune). [INFERRED]

### 4.2 Documents

`settings`:
```json
{"ts":1792580000,"src":"qpiri+qflag","dia":"STOCK21","feat":true,
 "v":{"pop":0,"mchg":60,"muchg":30,"pcp":2,"fa":1,"fk":1,"fx":1,"fy":1,
      "pgr":0,"fu":0,"fv":0,"fb":0,"fz":1,"pbcv":230,"pbdv":270,
      "pbt":0,"ofreq":50,"ovolt":230,"pcvv":282,"pbft":270,"psdv":210},
 "ok":{"fx":true,"fa":true,"pop":true},
 "cap":{"mchg":60,"muchg":30},
 "allow":{"mchg":"10,20,30,40,50,60"}}
```
- Voltages are ×10 integers.
- `ok` lists the keys that are `enabled-remote`.
- `v` contains only fields that were read successfully.

`ctl/window`:
```json
{"open":true,"until":1792581800,"wid":"k3f9q2zt","by":"btn","left":9,"feat":true,"ts":1792580000}
```

`ctl/req` (owner, single slot):
```json
{"id":"1792580012-a1b2c3d4","wid":"k3f9q2zt","key":"pop","val":2,"ts":1792580012,"by":"<OWNER_UID>","cv":1}
```

`ctl/res/2026-10-21/1792580012-a1b2c3d4` (final):
```json
{"key":"pop","val":2,"st":"applied","why":"","before":0,"after":2,"ack":"ACK",
 "org":"remote","wid":"k3f9q2zt","ts":1792580019,"ms":4210}
```
- `st` is one of `pending | applied | noop | rejected | failed`.
- Local-origin changes also produce a `res` record (`org: "local"`), so the PWA history is complete.

### 4.3 Request lifecycle

1. The owner confirms in the PWA, which `set`s `ctl/req`. The rules check the window, `wid`, `ts` freshness, the empty slot and the enum/range (§4.4).
2. The device, with the window open, GETs `ctl/req` every 5 s. On a non-null document it parses strictly, then sends **one multi-path PATCH**: `{"ctl/req": null, "ctl/res/<d>/<id>": {"st":"pending",…}}`.
   - Only after a 2xx does it post the request to `invTask`. If the PATCH fails, nothing is executed, and the next poll retries the take with the same id.
3. The executor runs (§2.4) and finishes in ≤ 13 s.
4. The device sends **one multi-path PATCH**: `{ctl/res/<d>/<id>: final, settings: snapshot, ctl/window: {…left}, events/<d>/<k>: event, mailq/<k>: job}`. The ntfy push goes out through the existing alert path.
5. The PWA's listener on `ctl/res/<d>/<id>` shows the final state.
   - Typical end-to-end time: 5 s poll + 5–8 s execution + about 1 s upload, so **7–15 s**. [INFERRED]

**Offline PWA.** The Firebase JS SDK queues writes made while offline and sends them on reconnect [INFERRED from SDK behaviour; check https://firebase.google.com/docs/database/web/offline-capabilities]. A late replay of an old request is refused by the `ts` ± 60 s rule. The PWA also disables Confirm unless `.info/connected` is true (an existing `onConnected` hook).

### 4.4 Rules snippet (merge under `devices/inv1`; placeholders as in the committed file)

```json
"settings": {
  ".write": "auth != null && auth.uid === 'DEVICE_UID'",
  ".validate": "newData.hasChildren(['ts', 'v'])",
  "ts":   { ".validate": "newData.isNumber() && newData.val() < now / 1000 + 600" },
  "src":  { ".validate": "newData.isString() && newData.val().length <= 16" },
  "dia":  { ".validate": "newData.isString() && newData.val().length <= 10" },
  "feat": { ".validate": "newData.isBoolean()" },
  "v":     { "$k": { ".validate": "$k.length <= 6 && newData.isNumber()" } },
  "ok":    { "$k": { ".validate": "$k.length <= 6 && newData.isBoolean()" } },
  "cap":   { "$k": { ".validate": "$k.length <= 6 && newData.isNumber()" } },
  "allow": { "$k": { ".validate": "$k.length <= 6 && newData.isString() && newData.val().length <= 60" } },
  "$other": { ".validate": false }
},
"ctl": {
  "window": {
    ".write": "auth != null && auth.uid === 'DEVICE_UID'",
    ".validate": "newData.hasChildren(['open', 'ts'])",
    "open":  { ".validate": "newData.isBoolean()" },
    "until": { ".validate": "newData.isNumber() && newData.val() <= now / 1000 + 3700" },
    "wid":   { ".validate": "newData.isString() && newData.val().matches(/^[a-z0-9]{8}$/)" },
    "by":    { ".validate": "newData.isString() && newData.val().matches(/^(btn|local)$/)" },
    "why":   { ".validate": "newData.isString() && newData.val().length <= 12" },
    "left":  { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 10" },
    "feat":  { ".validate": "newData.isBoolean()" },
    "ts":    { ".validate": "newData.isNumber()" },
    "$other": { ".validate": false }
  },
  "req": {
    ".write": "auth != null && ((auth.uid === 'OWNER_UID' && (!newData.exists() || ((!data.exists() || data.child('ts').val() < now / 1000 - 180) && root.child('devices/inv1/ctl/window/open').val() === true && root.child('devices/inv1/ctl/window/until').val() > now / 1000))) || (auth.uid === 'DEVICE_UID' && !newData.exists()))",
    ".validate": "newData.hasChildren(['id', 'wid', 'key', 'val', 'ts', 'by', 'cv']) && newData.child('wid').val() === root.child('devices/inv1/ctl/window/wid').val()",
    "id":  { ".validate": "newData.isString() && newData.val().matches(/^[0-9]{10}-[a-z0-9]{8}$/)" },
    "wid": { ".validate": "newData.isString() && newData.val().matches(/^[a-z0-9]{8}$/)" },
    "key": { ".validate": "newData.isString() && newData.val().matches(/^(pop|pcp|mchg|muchg|fa|fk|fx|fy|close)$/)" },
    "val": { ".validate": "newData.isNumber() && newData.val() % 1 === 0 && (newData.parent().child('key').val() === 'pop' ? (newData.val() >= 0 && newData.val() <= 2) : newData.parent().child('key').val() === 'pcp' ? (newData.val() >= 0 && newData.val() <= 3) : newData.parent().child('key').val() === 'mchg' ? (newData.val() >= 10 && newData.val() <= 100 && newData.val() % 10 === 0) : newData.parent().child('key').val() === 'muchg' ? (newData.val() === 2 || (newData.val() >= 10 && newData.val() <= 80 && newData.val() % 10 === 0)) : newData.parent().child('key').val().matches(/^f[akxy]$/) ? (newData.val() === 0 || newData.val() === 1) : newData.parent().child('key').val() === 'close' ? newData.val() === 0 : false)" },
    "ts":  { ".validate": "newData.isNumber() && newData.val() > now / 1000 - 60 && newData.val() < now / 1000 + 60" },
    "by":  { ".validate": "newData.isString() && newData.val() === auth.uid" },
    "cv":  { ".validate": "newData.isNumber() && newData.val() === 1" },
    "$other": { ".validate": false }
  },
  "res": {
    ".write": "auth != null && auth.uid === 'DEVICE_UID'",
    "$date": {
      ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
      "$id": {
        ".validate": "$id.matches(/^[0-9]{10}-[a-z0-9]{8}$/) && newData.hasChildren(['key', 'st', 'ts'])",
        "key":    { ".validate": "newData.isString() && newData.val().length <= 6" },
        "val":    { ".validate": "newData.isNumber()" },
        "st":     { ".validate": "newData.isString() && newData.val().matches(/^(pending|applied|noop|rejected|failed)$/)" },
        "why":    { ".validate": "newData.isString() && newData.val().length <= 12" },
        "before": { ".validate": "newData.isNumber()" },
        "after":  { ".validate": "newData.isNumber()" },
        "ack":    { ".validate": "newData.isString() && newData.val().matches(/^(ACK|NAK|NONE|-)$/)" },
        "org":    { ".validate": "newData.isString() && newData.val().matches(/^(remote|local)$/)" },
        "wid":    { ".validate": "newData.isString() && newData.val().matches(/^[a-z0-9]{8}$/)" },
        "ts":     { ".validate": "newData.isNumber()" },
        "ms":     { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 60000" },
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
- **Reads.** Owner and device read everything under `devices/inv1` (existing cascade). The mailer gets **no** read on `settings` or `ctl` (it is granted per node; `firebase/README` §1).
- **Single slot.** The owner may create `ctl/req` only when it is empty, or when the occupant is more than 180 s old (stale after a device outage). The device and the owner may delete it.
- **Validation scope.** `.validate` does not run on deletes and does not cascade (`firebase/README` §1, [CONFIRMED-primary](https://firebase.google.com/docs/database/security/core-syntax)), so every level validates itself, as in the existing file.
- **`root` and `data`** in these rules refer to the database before the write [CONFIRMED-primary, same core-syntax page]. The device's window writes and the owner's request writes are separate requests.
- **Coarse vs fine checks.** The rules do the coarse manual-range check (for example 100 A is allowed for MAX24). The device does the dialect, envelope and cross-key checks.
- **CI.** The existing "no plain `auth != null`" check still passes: every grant names a UID. Add the new emulator cases to `tools/rules-test` (§7.2).

### 4.5 Polling and quota

| State | `cfg` GET | `ctl/req` GET | Writes |
|---|---|---|---|
| Window closed (normal) | 60 s (`budget.cfgPollMs()`) | **never** | `settings` on change |
| Window open | 60 s | **every 5 s** | Window on change |
| Executing | 60 s | Paused | Take PATCH and result PATCH |
| Quota guard hard limit (`qg`) | Per budget | Window auto-closes (`why: quota`) | – |

- A 30-minute window costs about 360 small GETs, roughly 0.4 MB with headers. That is about 2–3 % of the 15 MB/day v1 target and well inside `BUDGET_RQ_GUARD` (20,000 requests/day). [INFERRED]
- Streaming (SSE) would be faster, but it does not fit the single-TLS-owner polling uplink (`Uplink.h`), so it was not chosen.

---

## 5. Web UI: "Inverter settings"

### 5.1 Placement and data

- **Route:** `#/settings/inverter`, a sub-page of Settings. It is reached from a card on the Settings page and adds no seventh tab to the six-item nav (`app.tsx` `ROUTES`).
- **New `DataSource` methods** (`source.ts`), with a demo implementation in `demoSource.ts` that simulates window and results:
  - `onSettings(cb)`;
  - `onCtlWindow(cb)`;
  - `onCtlRes(date, cb)`;
  - `requestSetting(key, val, wid): Promise<{id, date}>`, which computes `ts` with the existing `.info/serverTimeOffset` subscription (`firebaseSource.ts`) and makes `id` from `crypto.getRandomValues`;
  - `cancelSetting()`;
  - `closeCtlWindow(wid)`;
  - `ctlKill()`, which writes `cfg/ctlOff`.
- **Catalog:** `web/src/settingsCatalog.ts`, generated (§2.2).

### 5.2 Layout and states

1. **Status card:**
   - Build `ro` (from `meta.fwvar`): "This logger runs the read-only firmware. Values are shown for reference; change settings on the inverter LCD."
   - Feature off: "Remote settings are switched off on the logger. Turn them on at http://solar.local → Inverter settings."
   - Window closed: "Remote changes are locked. To allow them for 30 minutes, press the logger's BOOT button 3 times quickly (lid open), or open http://solar.local → Inverter settings → Allow remote changes."
   - Window open: a countdown, "Remote changes allowed until 14:35 (opened with the button) · 9 changes left today", with **Close now**.
   - Always: **Switch remote settings off** (kill switch, confirm sheet).
2. **Tier A rows**, one per key:
   - label, LCD program and the LCD text (e.g. "Output source priority · prog 01 · **UtI**");
   - the value's age ("read 3 min ago");
   - the control: Segmented picker for enums, a stepper bounded by `allow` ∩ manual range ∩ `cap` for currents;
   - disabled with a reason when not `ok` ("not yet tested on site"), the window is closed, the device is offline (`.info/connected`) or another request is pending.
3. **Two-step confirm** (`Sheet`):
   - Step 1 shows *from → to* and the plain-language consequence (§5.3), plus any cross-setting warning. Example: SbU with charger OSO: "the battery is never charged from mains".
   - The **Confirm** button enables after 2 s. Step 2 sends.
4. **Request states:**
   - *Sending* (write in flight);
   - *Waiting for the logger* (`pending` not yet seen; after 20 s: "The logger has not picked this up. It expires by itself at hh:mm:ss; nothing will happen after that." with **Cancel**);
   - *Inverter is applying* (`pending`);
   - **Applied** (green: "Inverter confirmed: now SbU (read back 14:21:07)");
   - **No change needed** (`noop`);
   - **Refused** (`rejected`, with a reason in plain words, e.g. `fault` → "the inverter shows a fault; nothing was sent");
   - **Failed** (red: "The inverter's answer did not match. Check LCD program 01 and correct it there if needed. Remote control for this setting is now disabled until it is tested on site again.").
5. **Tier B rows:** read-only, "Change on the logger's local page or on the inverter LCD (program 03)".
   - **Tier C rows:** read-only, "LCD only. The app never changes this, by design."
   - **LCD-only programs** (40, 42, 44, 45, 50, SOC variants): "not readable by the logger".
6. **History:** `ctl/res` for the last 14 days, newest first. Each item shows when, origin (app/local), key, before → after, the result and the reason; a tap shows the raw `ack` and `ms`.

### 5.3 Consequence texts (draft; [INFERRED from MAN pp. 13–16 via `docs/01` §4.7], check the wording against the paper manual)

| Key = value | Text |
|---|---|
| `pop` = UtI | "Mains powers the house whenever it is on. Solar and battery take over only during a power cut." |
| `pop` = SOL | "Solar powers the house first; mains adds what solar cannot cover. The battery is used only during a power cut." |
| `pop` = SbU | "Solar first, then battery, then mains. At night the battery runs the house until it falls to the back-to-utility point (program 12: *value*), then mains takes over. The battery cycles every day." |
| `pcp` = CUt / CSO / SNU / OSO | "Mains charges the battery first, solar only when mains is off" / "Solar charges first, mains only when solar is not available" / "Solar and mains charge together" / "**Mains never charges the battery.** On cloudy days it may stay low." Plus: "Applies in Line/Standby mode; in Battery mode only solar charges." |
| `mchg` = N | "Total charging current (solar + mains) is limited to N A (was M A). Lower means slower, gentler charging; the battery's BMS limit still applies." |
| `muchg` = N | "Mains may charge the battery with at most N A (was M A). If this is above program 02, the inverter uses program 02." |
| `fa` = 0 / 1 | "The inverter will **not beep** for warnings and faults (you still see them on the LCD and here)" / "The inverter beeps for warnings and faults again." |
| `fx`, `fk`, `fy` | "LCD backlight off/on" · "LCD stays on the page you chose / returns to the main page after 1 min" · "No beep / beep when mains power is cut" |

---

## 6. Notifications

The existing event path is reused: deterministic keys `<ts>-<TYPE>-<kind>`, the `mailq` outbox and the Apps Script mailer (r8 §3.6–3.7). The mailer needs **no code change**: it sends what the device queues. The new types are added to `shared/codes.json` `eventTypes` so the PWA and mail subjects get labels.

| Type | Sev | When | ntfy | E-mail (`em:"q"`) |
|---|---|---|---|---|
| `CTL_WINDOW` (st `open`) | 4 | Window opened (by button or local page) | Yes, forced past quiet hours | **Always** while `alerts.email.on` |
| `CTL_WINDOW` (st `clear`) | 2, or 4 if auto-closed after a failure | Closed / expired / auto-closed | Sev 4 only | Sev 4 only |
| `SETTING_CHANGED` (kind `i`) | 4 | `applied` (remote or local) | Yes, forced | **Always** |
| `SETTING_FAILED` | 4 | `failed` (any cell of §2.4.5), including boot recovery | Yes, forced | **Always** |
| `SETTING_REJECTED` | 3 | `rejected` (nothing changed) | Yes, forced | First 3 per day, then push only |
| `SETTING_LCD` | 2 | Snapshot change not made by the executor (§2.8) | Normal gates | Per `builtin.minSev` |
| `CTL_FEATURE` | 4 | Feature switched on or off (local, or off via `cfg/ctlOff`) | Yes, forced | **Always** |

**Forcing.**
- The device sets the `force` flag that `TEST_ALERT` already uses (`NotifyGates::pushPrio(..., force)`, code) and the "always e-mail" rule (r8 §4.7).
- The device mail-job cap still applies: 40 per day, 5 reserved for severity ≥ 4 (`NotifyGates.h`).
- Volume is bounded by the rate limits: at most about 10 changes and 3 rejection e-mails a day. [CONFIRMED-primary (code) for the gates; INFERRED for the policy]

**Message examples** (`events.msg` ≤ 200 chars, per the rules):
- `SETTING_CHANGED`: "Setting changed (app): Output priority, prog 01, UtI -> SbU. Inverter ACK, read back SbU 14:21. Remote changes allowed until 14:35."
- `SETTING_FAILED`: "Setting NOT confirmed: Max utility charge, prog 11, 30 -> 20 A. Inverter ACK but reads 30 A. Check LCD prog 11. Remote control of it is disabled."
- `SETTING_REJECTED`: "Change refused by the logger: Charger priority -> OSO (inverter in fault mode). Nothing was sent."
- `CTL_WINDOW`: "Remote inverter control enabled for 30 min with the logger's BOOT button. If this was not you, switch it off in the app."

---

## 7. Testing, acceptance and rollback

### 7.1 Native unit tests (`pio test -e native-rw`; `native` keeps the read-only suite)

| Suite | Cases |
|---|---|
| `test_ctl_catalog` | **Every vector in §7.1.1**, byte-exact. Every canonical value maps to exactly one string per dialect. Out of range → `nullptr`: `mchg` 95/100 (STOCK), 0, −10, 110; `muchg` 5, 85; `pop` 3; `pcp` 0 on MAX24/GK; `pcp` with `pcpMap=none`; `fa` 2; `fz` 0 (`enableOnly`). REVO → `nullptr` for every key. |
| `test_ctl_whitelist` | Generated table count is as expected. **No entry starts with a Tier C prefix** (`PBT PSDV PCVV PBFT PBEQ PF PSET PEd PDd PEj PDj PDz POP03 F5 F6 V2 PBATCD RTEY RTDL PPCP MSCHGC PCVT PCST`). Exact match only: `POP02 `, `POP2`, `pop02`, `POP020`, `PEab`, `PEA` refused. Tier B entries refused when `tierBAllowed=false`. `setterFrameAllowed` refuses a tampered CRC and non-printables. **`wl::cmdAllowed` still refuses every generated setter** for all three framings (the read path is unchanged). |
| `test_ctl_executor` (with `SimInverter` extended by a stateful settings model, `test/mocks/Mocks.h`) | Happy path ACK + read-back. NAK with and without CRC. Timeout. Garbage. `BAD_CRC` ACK. ACK but unchanged (→ `mismatch`, revoke). ACK with a third value (→ revoke + close). Read-back NAK/timeout (3 read tries, then `verify`). NAK but changed. Duplicate id. `ts` ≤ `lastTs`. Expired / future `ts`. Window closed, wrong `wid`. Every rate limit. QMOD `F`, a QPIWS fault bit, link not OK, engine probing, stale sample. Envelope; `muchg > mchg`. `noop` sends **zero** frames. `force` same-value. Boot recovery with `inflt` present sends **zero** frames. **Every test asserts `sim.commands` holds at most one setter frame per request** and that `sim.unsafeWrites` counts only expected frames. |
| `test_ctl_window` | Triple-press decode (bounce, 590/610 ms edges, 3 s span); a **3 s hold still opens the portal and never the window**; feature off → no window; expiry; every auto-close trigger; reboot → closed; new `wid` each time. |
| `test_uplink` (extended) | Strict `ctl/req` parse (missing or extra fields → reject). `res`/`window`/`settings` serialisation within the rule limits. Take-PATCH failure → not executed. |
| `test_parsers` (extended) | QFLAG struct; PQSE `SSTUV` / `WW.W` (both layouts) |

#### 7.1.1 Byte vectors (recomputed today, `docs/02` §4 algorithm; they match P2SET §4 except the PGR00 note in §0.2)

| Command | Wire bytes | Command | Wire bytes |
|---|---|---|---|
| `POP00` | `50 4F 50 30 30 C2 48 0D` | `PCP00` | `50 43 50 30 30 8D 7A 0D` |
| `POP01` | `… 30 31 D2 69 0D` | `PCP01` | `… 30 31 9D 5B 0D` |
| `POP02` | `… 30 32 E2 0B 0D` | `PCP02` / `PCP03` | `… AD 38 0D` / `… BD 19 0D` |
| `MCHGC060` | `4D 43 48 47 43 30 36 30 1D B1 0D` | `MCHGC090` | `… 30 39 30 0E 8F 0D` (**raw 0x0D8F → adjusted**) |
| `MNCHGC0060` (MAX24) | `4D 4E 43 48 47 43 30 30 36 30 8B AC 0D` | `MNCHGC060` (GK) | `4D 4E 43 48 47 43 30 36 30 D4 2E 0D` |
| `MUCHGC002` | `4D 55 43 48 47 43 30 30 32 B5 D1 0D` | `MUCHGC030` | `… 30 33 30 C0 C0 0D` |
| `MUCHGC010` / `020` | `… A6 A2 0D` / `… F3 F1 0D` | `MUCHGC040` | `… 59 57 0D` |
| `PEa` / `PDa` | `50 45 61 D0 70 0D` / `50 44 61 E3 41 0D` | `PEx` / `PDx` | `50 45 78 53 68 0D` / `50 44 78 60 59 0D` |
| `PEk` / `PDk` | `… 71 3A 0D` / `… 42 0B 0D` | `PEy` / `PDy` | `… 43 49 0D` / `… 70 78 0D` |
| `PGR00` / `PGR01` | `50 47 52 30 30 29 EB 0D` / `… 39 CA 0D` | `(ACK` / `(NAK` replies | `28 41 43 4B 39 20 0D` / `28 4E 41 4B 73 73 0D` |

### 7.2 Rules emulator tests (`tools/rules-test`, extended)

| Who | Case | Expected |
|---|---|---|
| Owner | Create `ctl/req` while the window is open with the right `wid` and a fresh `ts` | Allow |
| Owner | Same with the window closed, `until` in the past, a wrong `wid`, `ts` ± 61 s, or the slot occupied by a fresh request | Deny |
| Owner | Overwrite a stale (> 180 s) slot; delete the slot | Allow |
| Owner | Unknown key, `val` out of range, extra field, `by` ≠ uid, `cv` ≠ 1 | Deny |
| Owner | Write `ctl/res`, `ctl/window` or `settings` | Deny |
| Device | Create `ctl/req` | Deny |
| Device | Delete `ctl/req`; write `res`/`window`/`settings` | Allow |
| Device | `window.until` more than now + 3700 s | Deny |
| Mailer | Read `ctl`, `settings` | Deny |
| Owner | `cfg/ctlOff` | Allow (with the range) |

The existing tests (r8 N/P series) must still pass.

### 7.3 CI binary checks (`.github` workflow, new step)

- `pio run -e savx`, then `strings -n 3 .pio/build/savx/firmware.bin | grep -E '^(POP0[0-2]|PCP0[0-3]|MCHGC[0-9]{3}|MNCHGC[0-9]{3,4}|MUCHGC[0-9]{3}|P[ED][akxyubvz])$'` must print **nothing**. The read-only binary has no setters.
- `pio run -e savx-rw`, then the same command with `^(PBT[0-9]{2}|PSDV|PCVV|PBFT|PBEQ|PSET|PBATCD|RTEY|RTDL|PF|P[ED][dj]|PDz|POP03)` must print **nothing**. Tier C is absent.
- The generator check passes: catalog ↔ rules ↔ TS. [INFERRED]

### 7.4 Bench tests with a fake inverter (P2-M3)

The board must come off the inverter for this: unplug the RJ45 at COM first (`docs/03` N8). Logging pauses for 2–3 h.

**Variant A (recommended; needs only two M-M jumpers): on-board fake inverter.**
- Build `savx-rw-bench` (`BENCH_FAKE_INV=1`).
- A `FakeInverter` task on **UART2** emulates `QPI`, `QPIGS`, `QMOD`, `QPIWS`, `QPIRI`, `QFLAG` and every catalog setter with a settings state.
- Wiring: GPIO17 (logger TX) → GPIO4 (UART2 RX), and GPIO5 (UART2 TX) → GPIO18 (logger RX).
  - The pins avoid the "never use" list in `docs/03` §3; [INFERRED] check that 4/5 are free on the clone.
- The link runs in MAX3232 mode (non-inverted, `arm-max3232` path, `Tasks.cpp`) with **RAM-only link state**. The bench build never writes NVS `link`, so the field arming from M2/M3 stays intact. [INFERRED design requirement]
- If the pre-arm check (GPIO17 must read LOW with pull-down, `PinDiag::preArmCheck`) trips on a UART2 RX pull-up, start UART2 after arming. [INFERRED]
- Scenarios are selectable on the local page: `ack`, `nak`, `nak-nocrc`, `silent`, `garbage`, `ack-noapply`, `ack-wrong`, `slow-ack` (2.5 s), `readback-nak`, `mode-B-nak`. Plus a manual **RST press during SETTLE** to exercise boot recovery.

**Variant B (optional): Python simulator on the Mac.**
- The bench firmware bridges UART2 ↔ the ESP32-S3 native USB port (HW CDC). The Mac runs `tools/inv-sim/sim.py` (new, `pyserial`) on that CDC port. The PI30 link itself still runs at 2400 baud over the jumpers.
- This respects `docs/03` N13 ("one USB cable at a time"): flash through COM, then move the single cable to the native USB port. Logs are then read over Wi-Fi from `/api/raw` and `/api/ctl`.
- [INFERRED: confirm that `HWCDC` serial is usable with `ARDUINO_USB_CDC_ON_BOOT=0` in Arduino-ESP32 3.3.12 before choosing this.]

**Bench acceptance:**
- each scenario produces the §2.4.5 cell, the right `ctl/res`, event, e-mail and push, and the right LED;
- exactly one setter frame per request (FakeInverter counter);
- no write after a reboot;
- rate limits hold;
- the window never survives a reboot.

After the bench:
1. Re-flash `savx` or `savx-rw`.
2. Re-install.
3. Re-run the `docs/07` M3 acceptance quick checks (QPIGS ≥ 99 % valid over 200 polls; output V and battery V against the LCD) before any write.

### 7.5 On-site acceptance per command (owner at the LCD; the `docs/07` global stop rules apply)

**General procedure, for each key:**
1. Read the LCD value and the snapshot; they must agree.
2. Open the window.
3. Run the **same-value test** from the local page (`/api/ctl/test`) and expect `applied` with `after == before`. Record the reply bytes (U4), latency, letter case (U3) and persistence at the next scheduled QPIRI/QFLAG (U11).
4. Make a **visible, reversible change** from the local page and check it on the LCD.
5. Revert, and check on the LCD.
6. Tick "verified on LCD" → `accepted-local`.
7. Repeat the change once **from the PWA** with the owner at the LCD → `enabled-remote`.

**Order (stop at the first unexpected result):**

| Step | Key | Visible test | Conditions |
|---|---|---|---|
| 1 | `fx` backlight (**P2-M4 feasibility**) | `PDx` → backlight off → `PEx` | Any time |
| 2 | `fa` alarm | bON → bOF (mute icon appears [MAN p.12]) → bON | Daytime |
| 3 | `fk`, `fy` | ESP ↔ HEP; AON ↔ AOF (check the LCD text only) | – |
| 4 | `pop` | UtI → SOL for 10 min (watch the load source on the LCD and in Live), then → UtI | Daytime, grid on, SOC > 60 % |
| 5 | `muchg` | 30 → 20 A in Line mode while mains-charging; `bci` falls within 2 min; → 30 | Grid on, battery not full |
| 6 | `mchg` | 60 → 50 → 60 (same-value first); record `QMCHGCR` once | As 5 |
| 7 | `pcp` | **Only after `pcpMap` is set from U7.** Same-value, then SNU → CSO → SNU | Daytime |

- Optional U11 power-cycle check: the owner decides. If skipped, record "persistence over a power cycle not verified". `SETTING_LCD` will reveal a revert later (§2.8).
- **Never** send `PSAVE`, and never test any Tier C command (P2SET §5 U11, U15).

### 7.6 Rollback (fastest first)

1. **Close the window**, from the PWA, the local page or a triple-press. Nothing can be written.
2. **Kill switch.** `cfg/ctlOff` from the PWA, or feature off on the local page. Re-enabling is local only.
3. **Revoke a key** on the local page.
4. **Re-flash `savx`** (the read-only build) over OTA (`/update`, admin) or USB. The read-only build ignores `ctl/*`, and its `meta.fwvar` is `ro`, so the PWA hides the controls.
5. **Rules.** Remove the `ctl/req` owner grant (re-render and deploy) so the cloud refuses requests even from a stale PWA.
6. **Restore settings** at the LCD from the G5 table. **Never `PF`** (factory reset, `docs/02` §11.2).

---

## 8. Milestones, effort and acceptance

Effort is in evenings, as in d1 §9. [INFERRED]

| M | Name | Scope | Effort | Acceptance |
|---|---|---|---|---|
| **P2-M0** | Gate review and zero-write go/no-go | §1.1 G1–G5, G7; decision tree §1.2 from `probe-report.json` and `meta.sup`; LCD table and photos; U7/U8 mapping recorded | 0.5–1 | Every gate ticked. Go/no-go written in the build record. REVO without QFLAG → **stop here** (keep D10). |
| **P2-M1** | Catalog, generator, rw envs, write path | `shared/settings-catalog.json`, `tools/gen_settings.py`, `savx-rw` / `native-rw` envs, `config.h` guard, `SetterWhitelist`, `SerialLink::writeSetting`, `test_ctl_catalog`, `test_ctl_whitelist`, CI binary checks | 2–3 | All §7.1.1 vectors byte-exact. Tier C absent (both CI greps empty). `savx` binary has no setter strings. `pio test -e native` unchanged and green. |
| **P2-M2** | Executor, persistence, window, local page | `CommandExecutor`, NVS `ctl`, audit ring, boot recovery, BOOT gesture, LED, `/api/ctl*` with the CSRF guard, snapshot + `SETTING_LCD` (both builds), `test_ctl_executor`, `test_ctl_window` | 3–4 | Every §2.4.5 cell covered by a test. At most one setter frame per request in every test. Reboot → window closed and no replay. 3 s hold still opens the portal. |
| **P2-M3** | Bench with a fake inverter | §7.4 variant A (B optional) | 1–2 | All scenarios behave as specified; the boot-recovery RST test passes; M3 quick check passes after re-installation |
| **P2-M4** | **On-site feasibility (GO/NO-GO)** | `savx-rw` OTA; feature on; window via the local page **and** via the button; `fx` same-value, then toggle; U2–U4, U11, U13 recorded | 1 | **GO:** `fx` ACK + read-back verified, LCD agrees, no warning or fault. **NO-GO:** NAK on same-value and toggle, or QFLAG unusable → re-flash `savx`; record the result. |
| **P2-M5** | Cloud path | Rules §4.4 + emulator tests §7.2 + deploy; uplink ctl step and polling §4.5; events and e-mail §6; `cfg/ctlOff`; `shared/codes.json` types | 2–3 | All §7.2 cases pass. A request from `curl` as the owner is executed once. A stale or replayed request is refused. Window open → 5 s polling, closed → none. Notices arrive by ntfy in < 30 s and by e-mail. |
| **P2-M6** | PWA "Inverter settings" | §5; DataSource and demo; generated TS catalog; history | 2–3 | `npm run typecheck` green. The demo shows every state. With the real logger, `fx` from the iPhone completes in < 20 s. Offline → Confirm disabled. Window closed → controls locked, with the how-to-open text. |
| **P2-M7** | Per-command acceptance | §7.5 steps 1–7, local then remote, over at least 3 days | 2–3 | Each Tier A key either `enabled-remote`, or explicitly left disabled with the reason recorded. Every change verified on the LCD and reverted. Audit, `ctl/res`, events and e-mails consistent. |
| **P2-M8** | Soak with Phase 2 enabled + docs | 7 days of normal logging with the feature on; at least 3 window openings and at least 5 remote changes spread over the week; doc updates §11 | 1 + 7 days | v1 M8 criteria still met (≥ 99.5 % of minutes, link errors < 1 %, zero false severity-5 alerts). **Zero setter frames outside executor requests** (RawRing `SET` count = `ctl/res` count). Zero `SETTING_LCD` false positives. |
| **P2-M9** (optional) | Tier B on the local page | `INV_WRITE_TIER_B=1`; §2.2.3 entries; on-site acceptance per key | 2 | As P2-M7, local only. `pbcv`/`pbdv` left disabled if 05 = LIb. |

**Totals:**
- To the go/no-go point (P2-M0…M4): **7.5–11.5 evenings**.
- Core feature (P2-M0…M8): **14.5–21.5 evenings plus 7 days**.
- With Tier B: **16.5–23.5 evenings**.

Ordering rationale: the cloud and PWA work (P2-M5/M6, about 4–6 evenings) is only spent after on-site feasibility is proven.

---

## 9. Risks added by Phase 2

| # | Risk | L | I | Mitigation |
|---|---|---|---|---|
| R-P2-1 | The unit is REVO, so there is no usable setter path | M–H | M (sunk effort) | Zero-write decision at P2-M0; cheapest test at P2-M4 before cloud/PWA work (§1.2, §8) |
| R-P2-2 | The inverter ignores the incoming CRC and a line error turns one setter into a neighbour (e.g. `POP02` → `POP03`/`POP00` by one bit, `MUCHGC030` → `MUCHGC020`) | very L | M–H | Read-back on every write; mismatch → revoke, close the window, severity-4 notice with the LCD program; M3 CRC error rate < 1 % (`docs/03` R5) |
| R-P2-3 | Owner account compromised while a window is open | L | M | A window needs local action; Tier A only; envelope; ≤ 10 changes a day; every change and window opening is notified; kill switch |
| R-P2-4 | ACK but not persisted across an inverter restart (U11) | M | L | `SETTING_LCD` detects a revert; documented per key |
| R-P2-5 | Mode-dependent NAKs (U12) | M | L | `rejected/nak`, nothing changed; widen `modes` only after tests |
| R-P2-6 | EEPROM wear | L | M | Rate limits count every frame (U17) |
| R-P2-7 | Charge current above what the BMS or battery tolerates | L | H | Envelope ≤ the on-site value; STOCK capped at 90 A; BMS limits still apply [INFERRED] |
| R-P2-8 | Firmware bug in an encoder or dialect choice | L | M–H | Lookup tables from one catalog; byte vectors; exact-match whitelist; bench scenarios; per-dialect acceptance (GK `mchg` blocked until U5) |
| R-P2-9 | Executor pauses polling for ≤ 13 s | H | very L | Fast polls skipped, not failed; at most 3 samples missing per change |
| R-P2-10 | A household member presses BOOT 3 times or knows the admin password | L | L–M | Severity-4 "window opened" notice; optional button-only mode; the lid is screwed |

## 10. Open questions for the owner

1. Should the window be openable from the local page at all, or only with the BOOT button (lid open)?
2. Is 30 min a good default (5–60 selectable)?
3. Should rejected attempts always be e-mailed, or pushed only after the third of the day?
4. Should Tier B (P2-M9) be built at all, given that 12/13 are probably SOC (LCD-only) on this lithium setup?
5. If lowercase `PEx` NAKs on a STOCK unit at P2-M4, should an uppercase variant be tested (catalog v2), or is that the stop?
6. Should a remote change be allowed while the owner is away from home? The design allows it while a window is open; a "window only while the PWA is on the home Wi-Fi" variant is not possible to verify server-side. [INFERRED]

## 11. Plan and doc changes when this draft is adopted (not made now)

- `docs/00` D11 and d1 §3.10: add "D11 holds for `savx`; Phase 2 adds `savx-rw` (P2-D1…D10)". **Fix the drift**: `QMCHGCR` / `QMUCHGCR` / `QBEQI` are not whitelisted (§0.2).
- `docs/02` §11: add the generated setter table and the Tier list; fix §11.1.
- `docs/04`: `settings`, `ctl/*`, `cfg/ctlOff`.
- `docs/05`: envs, `/api/ctl*` routes, BOOT gesture, LED modes, NVS `ctl`.
- `docs/06`: PWA page, rules table.
- `docs/07`: P2 milestones.
- `docs/08`: rollback and troubleshooting.
- `firebase/README` §1 table; `shared/codes.json` `eventTypes`; `docs/site-survey.md` G5 table.

## 12. Sources

**Project files read today:**
- `docs/01-inverter-profile.md` §3.2–3.4, §4.7, §7
- `docs/02-protocol-reference.md` §4, §7.7, §7.10, §11
- `docs/03-hardware-and-wiring.md` §3, N8, N13, N17, §11, R5
- `docs/05-firmware-guide.md` §3.1, "MAX3232 module"
- `docs/07-milestones-and-test-plan.md` M0–M8
- `research/d1-architecture.md` §3.10, §4.1, §9
- `research/r8-email-alerts.md` §3.6–3.7, §4.7
- `research/manual-extract.md`
- `research/user-constraints.md`
- `research/v1-protocol-verification.md` §2.2 A4, C14, V10
- `firebase/database.rules.json`, `firebase/README.md` §1
- `shared/codes.json`
- `firmware/platformio.ini`
- `firmware/include/config.h`
- `firmware/src/link/{Whitelist.*,SerialLink.*,Port.h,LinkProfile.h,PinDiag.h}`
- `firmware/src/proto/{Engine.*,Scheduler.h,Adapter.h,Commands.h}`
- `firmware/src/proto/pi30/{Dialect.h,Pi30Adapter.*,Pi30Parse.h,Pi30Frame.h}`
- `firmware/src/app/{App.h,Tasks.cpp}`, `firmware/src/main.cpp`
- `firmware/src/sys/StatusLed.*`
- `firmware/src/agg/NotifyGates.h`
- `firmware/src/net/Uplink.h`
- `firmware/src/web/LocalWeb.*`
- `firmware/src/store/Settings.h`
- `firmware/src/model/Identity.h`
- `firmware/test/mocks/Mocks.h`, `firmware/test/test_frames`
- `web/src/{app.tsx,data/source.ts,data/types.ts,data/firebaseSource.ts,pages/Settings.tsx,components/ui.tsx}`

**Protocol documents.** Local text copies, read as text only (untrusted downloaded data), in `<scratch>`. Re-checked today:
- `pi30_2014.txt`: §2.11–2.12, §3.1, §3.4, §3.7, §3.15–3.16
- `pi30_2015.txt`: §3.3–3.5 (index)
- `pi30max.txt`: §3.3, §3.5–3.6
- `pi30_gkmk.txt`: §2.14–2.15, §3.3–3.4
- `pi30revo.txt`: §1.5–1.7, §4.3

Upstream: the mpp-solar `docs/protocols` folder, as listed in `docs/02` §15.

**Setter research** (P2SET, relayed with this task). Its own sources:
- MAN pp. 13–20;
- mpp-solar https://github.com/jblance/mpp-solar/tree/master/mppsolar/protocols (`pi30.py`, `pi30revo.py`, `pi30max.py`, `pi30m044.py`, `pi30m045.py`);
- ESPHome pipsolar https://github.com/esphome/esphome/tree/dev/esphome/components/pipsolar;
- DIY Solar thread 34404 https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/ (via r1 §4.2);
- mpp-solar issue #403 https://github.com/jblance/mpp-solar/issues/403.

**Firebase docs:**
- rules core syntax https://firebase.google.com/docs/database/security/core-syntax;
- offline write queueing https://firebase.google.com/docs/database/web/offline-capabilities (not re-read today; §4.3 marks the claim [INFERRED]).

**Own computation:** `<scratch>` (CRC vectors in §7.1.1).
