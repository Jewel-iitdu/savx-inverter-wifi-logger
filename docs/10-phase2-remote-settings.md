# 10 — Phase 2: remote control of selected settings (design)

> ⚠️ **Status: design only. Phase 2 is not implemented.**
> - **v1 ships strictly read-only.** It has an exact-match read whitelist and `INV_ALLOW_WRITES=0`, and the build refuses to compile with writes on ([00 D11](00-plan-overview.md#5-decisions-adr-summary), [02 §11](02-protocol-reference.md#11-read-only-safety-what-we-send-and-what-we-never-send)).
> - Nothing in this chapter changes v1, and nothing here should be built until v1 has passed its 7-day soak ([07](07-milestones-and-test-plan.md) M8).
> - The full technical design, with sources and evidence tags, is [research/r9-remote-settings.md](../research/r9-remote-settings.md) (cited below as **R9 §n**).

This chapter explains, in plain language:
- what Phase 2 would add;
- what has to be true before it starts;
- which inverter settings it may touch, and which it never will;
- how one change travels from the iPhone to the inverter and back;
- how it is tested on site, one command at a time, starting with the buzzer flag;
- how to undo everything.

---

## Contents

1. [What Phase 2 is, and what it is not](#1-what-phase-2-is-and-what-it-is-not)
2. [Before anything happens](#2-before-anything-happens)
3. [Which settings, and how](#3-which-settings-and-how)
4. [How one change flows](#4-how-one-change-flows)
5. [The local enable window](#5-the-local-enable-window)
6. [What the web page will look like](#6-what-the-web-page-will-look-like)
7. [Safety guarantees](#7-safety-guarantees)
8. [Milestones](#8-milestones)
9. [On-site test procedure, per command](#9-on-site-test-procedure-per-command)
10. [Rollback](#10-rollback)
11. [Open questions](#11-open-questions)
12. [Related documents](#12-related-documents)

---

## 1. What Phase 2 is, and what it is not

**Phase 2 is:**
- an **opt-in** extra, built as a **separate firmware** (`savx-rw`). The normal firmware (`savx`) stays read-only, and contains no setting commands at all;
- a way to change **a few harmless or bounded settings** from the web app. Examples: output priority, charging current limits within battery-safe bounds, the buzzer (on only), backlight and two beep or display flags;
- only possible while a **short window opened at the logger itself** is active. You press its BOOT button three times;
- **one change at a time**. Each change is checked before anything is sent, sent exactly once, read back from the inverter, re-checked over the next 10 minutes, logged, and announced by push and e-mail.

**Phase 2 is not:**
- **remote access to battery settings.** Battery type, bulk/float/cut-off voltages, equalisation, output voltage and frequency, feed-to-grid and factory reset are **never** sent by the logger. Their command strings do not even exist in the firmware;
- a replacement for the LCD. Anything not in the remote list in §3 stays an LCD job;
- something that works by default. It is off after flashing, off after every reboot, and needs on-site tests for **each value** before that value can be sent remotely;
- guaranteed to be possible at all. The inverter is probably a Sorotec REVO-platform unit. Sorotec's protocol document has no individual setting commands, and owners report that RS232 setting writes are refused on REVO units. **If the logger identifies the unit as REVO, Phase 2 stops before a single setting command is sent** (R9 §4.2).

---

## 2. Before anything happens

All of these must be done, in order, before the Phase 2 firmware goes on the inverter (R9 §4.1):

| # | Precondition | Where it is recorded |
|---|---|---|
| 1 | v1 milestones M0–M3 accepted, including the LCD cross-check. The cloud parts of Phase 2 also need M5–M7 | [07](07-milestones-and-test-plan.md) |
| 2 | **7-day soak passed** (M8): ≥ 99.5 % of minutes present, link errors < 1 %, no false critical alerts | [07](07-milestones-and-test-plan.md) M8 |
| 3 | The logger has identified the inverter the same way on every probe for those 7 days, and three extra checks for REVO all came back negative | `docs/probe-report.json` |
| 4 | Every LCD settings program photographed and written down, including **06, 23, 40 and 50** | site survey §8 |
| 5 | How each LCD value appears in the logger's readings (settings programs 01, 02, 03, 11, 12, 13, 16 and the 18–23 flags) | site survey B6 |
| 6 | **Battery label data:** capacity (Ah) and the BMS continuous charge and discharge current ratings. Without them, remote charging-current changes and the solar/battery-first output priorities stay off | site survey §9 (a new BMS row) |
| 7 | **A v1.x update installed:** OTA needs a BOOT press; the read-only "inverter settings" view and the inverter fingerprint exist | P2-M1 below |
| 8 | ntfy or Telegram set up **on the logger**, and a test push received. Remote-change notices must not depend only on e-mail settings that the app account can change | [06 §8](06-cloud-and-dashboard-guide.md#8-push-channels-ntfy-telegram-healthchecks) |
| 9 | A one-time, read-only check of whether the inverter checks the CRC of incoming frames | R9 §4.4 |
| 10 | You have read §10 (rollback) and accept the remaining risks (R9 §14) | build record |

---

## 3. Which settings, and how

**The three answers:**
- **Remote**: can be changed from the iPhone, but only to values that were first tested on site and seen on the LCD.
- **Local only**: only on the logger's own page at `http://solar.local`, and each change needs a BOOT press at the logger.
- **LCD only**: the logger never changes it. Either there is no command for it, or the risk is too high.

| LCD program | What it does | Phase 2 | Plain-language risk |
|---|---|---|---|
| **01** Output source priority (UtI / SOL / SbU) | Who powers the house first: mains, solar, or solar-then-battery | **Remote.** UtI at any time. SOL and SbU only when the load is low enough for the battery, SOC ≥ 60 %, the grid is present and the battery data are recorded | Moving the house onto the battery with a big load can trip the battery's BMS or overload the inverter. With programs 06/23 at their defaults, the house would then stay dark until someone restarts the inverter by hand |
| **02** Max charging current (solar + mains) | Total charging current limit | **Remote** within a range worked out from the battery: at least max(20 A, 0.2C), at most min(the tested value, 0.5C, 0.8 × BMS rating). **Local only** outside it | Too high stresses the battery. Too low leaves it empty, and it also throttles solar charging |
| **03** AC input range (APL / UPS) | How fussy the inverter is about mains quality | **Local only** (optional extra, Tier B) | Changes when the inverter switches to battery; some loads may blink |
| **05** Battery type | Charging profile | **LCD only** | Wrong type can over- or under-charge the lithium battery |
| **06** Restart after overload | Whether the inverter restarts by itself | **Local only** (optional). LCD only if the logger detects the MAX firmware dialect | If off, an overload leaves the house dark until a manual restart |
| **07** Restart after over-temperature | Same, for overheating | **Local only** (optional) | Same |
| **09 / 10** Output frequency / voltage | 50/60 Hz; 220/230/240 V | **LCD only** | Can damage appliances |
| **11** Max utility charging current | How hard mains may charge the battery | **Remote** from 10 A up to the range above. **Local only** for 2 A and anything above the range | 2 A from mains is almost "no mains charging" |
| **12 / 13** Back-to-utility / back-to-battery points | When SbU switches between battery and mains | **LCD only** on this lithium system: they are set as SOC %, and there is no command for that | Cycling depth of the battery |
| **16** Charger source priority (CUt / CSO / SNU / OSO) | Who charges the battery | **Remote** for CUt (only if this unit's code order allows it), CSO and SNU. **Local only** for OSO | OSO means mains **never** charges the battery; on cloudy days it can run flat |
| **18** Alarm (buzzer) | Beeps for warnings and faults | **Remote: switch ON only.** Switching OFF is local only | A muted inverter cannot warn people at home of an overload or low battery |
| **19** Return to default screen | LCD display behaviour | **Remote**, both ways | None (display only) |
| **20** Backlight | LCD backlight | **Remote**, both ways | None |
| **22** Beep when mains is interrupted | Beep on power cut | **Remote**, both ways (with a notice) | You may not notice a power cut |
| **23** Overload bypass | Whether overload falls back to mains | **Local only** (optional) | Behaviour during overload |
| **25** Record fault code | Fault history | **Local only**, switch on only (optional) | Switching it off would lose diagnostics, so the logger never does that |
| **26 / 27 / 29** Bulk / float / low cut-off | Battery voltages | **LCD only** | Battery damage, deep discharge |
| **30–36** Equalisation | Lead-acid maintenance | **LCD only** | Wrong for lithium |
| **40** Discharge current limit | Battery discharge cap | **LCD only** (no known command) | – |
| **42, 44, 45, 50** EARTH LED, reconnection delay, zero export, battery activation | – | **LCD only** (no known command) | – |
| **43** Solar feed to grid | Export | **LCD only** | – |
| Factory reset, full-configuration write (REVO `PSET`), calibration, clock, energy-counter erase | – | **Never** | Loses the whole configuration, or more |

The full table, with every command string per firmware dialect and how each value is read back, is R9 §2–§3. The list of commands v1 refuses today is [02 §11.2](02-protocol-reference.md#112-never-sent-explicitly-refused--and-why).

---

## 4. How one change flows

```
 You (at the logger)    iPhone (web app)          Firebase (cloud)          Logger (ESP32)                 Inverter
 ───────────────────    ────────────────          ────────────────          ──────────────                 ────────
 press BOOT 3× ─────────────────────────────────────────────────────────► opens a 10-min,
                                                  window: open  ◄──────────  one-change window
                        "allowed until 14:35" ◄── (server time)             (push + e-mail: "window opened")

                        pick a value, re-enter
                        password, Confirm ──────► request (only if: owner,
                                                  window open, fresh, the
                                                  only one, value allowed)
                                                         ▲   ──── checked every 5 s ───┐
                                                         └──── take it (delete) ◄──────┤
                                                                                       │ 17 checks: window, time,
                                                                                       │ tested value, battery
                                                                                       │ limits, mode, warnings,
                                                                                       │ load, alert settings…
                                                                                       │ (any fail: NOTHING is sent)
                                                                                       │ read all settings ───────► QPIRI / QFLAG
                                                                                       │ ONE setting command ─────► e.g. PEa
                                                                                       │                ◄────────── (ACK
                                                                                       │ read all settings again ─► QPIRI / QFLAG
                                                                                       │ exactly that one setting
                                                                                       │ changed? → "applied"
                        "Applied, read back ◄──── result ◄─────────────────────────────┤
                        14:21:07"                                                      ├──► push + e-mail
                                                                                       │ re-read at +1, +5, +10 min
                                                                                       │ (changed back? → warning)
```

**Plain words:**
- The cloud can only *ask*.
- The logger decides, sends one command, and believes only what it reads back from the inverter.
- A command is **never repeated** automatically. If the answer is unclear, the app says so and you decide.

---

## 5. The local enable window

| Question | Answer |
|---|---|
| **How do I open it?** | Open the logger's lid and press **BOOT three times quickly** (each press under about half a second, all within 3 s). The LED double-blinks white while the window is open. **Holding BOOT for 3 s still opens the Wi-Fi setup portal** as before, and never the window |
| **How long does it last?** | **10 minutes, for one change**, by default. On the logger's page you can prepare a longer window (up to 30 min, up to 5 changes) and then confirm it with **one** BOOT press within 60 s |
| **Can I open it from the logger's web page?** | No. The page can only *prepare*. Every action that makes things less safe needs the button press: opening a window, switching the feature on, marking a value as tested, entering battery data, raising a limit, any local-only change, and firmware updates. A stolen admin password is therefore not enough |
| **Can I open it from the app?** | **No, by design.** The app can only close it or switch the whole feature off |
| **How does it close?** | Expiry. After the one change. "Close now" in the app. "Close" on the logger's page. Three presses again. It also closes automatically on reboot, firmware update, Wi-Fi portal, lost link, an inverter fault, a refused or failed change, a change to the e-mail alert settings, or if the inverter's identity changes |
| **Will I be told?** | Yes. Opening sends a push and an e-mail: "Remote inverter control enabled for 10 min … If this was not you, switch it off in the app." |
| **The lid is a pain to open every time.** | An optional touch pad (foil inside the clear lid, touched through the plastic) can replace the button for this. It is bench-tested first (P2-M3b). See §11 |

> ⚠️ The usual rule still applies: do not touch the ESP32, the module or bare wire ends while the RJ45 is in COM. Pressing the BOOT button's plastic cap is fine ([03](03-hardware-and-wiring.md) N17).

---

## 6. What the web page will look like

The page is **Settings → Inverter settings**. It adds no new tab.

```
┌──────────────────────────────────────────────┐
│ Inverter settings                            │
│ ┌──────────────────────────────────────────┐ │
│ │ ● Remote change allowed until 14:35      │ │
│ │   (opened with the button · 1 change)    │ │
│ │   9 changes left in 24 h    [Close now]  │ │
│ └──────────────────────────────────────────┘ │
│ Output source priority · prog 01             │
│   [ UtI ] [ SOL ] [ SbU ]   read 1 min ago   │
│   SbU: needs battery data (not yet recorded) │
│ Total charging (solar + mains) · prog 02     │
│   [-] 50 A  (1.2 kW, 0.5C) [+]   20–50 A     │
│ From mains only · prog 11                    │
│   [-] 30 A [+]   effective mains: 30 A       │
│ Charger priority · prog 16                   │
│   [ CUt ] [ CSO ] [ SNU ]  OSO: logger only  │
│ Alarm · prog 18   ON  (off: logger only)     │
│ Backlight · prog 20    [ on | off ]          │
│ …                                            │
│ Battery type · prog 05   LIb   LCD only      │
│ Bulk voltage · prog 26   28.2 V  LCD only    │
│ Back to utility · prog 12  unit unknown (LIb)│
│                                              │
│ History (14 days)                            │
│  14:21  app   Output priority SbU → UtI  ✔   │
│  09:02  local Backlight on → off → on    ✔   │
│                                              │
│ [ Switch remote settings off ]               │
└──────────────────────────────────────────────┘
```

**How the page behaves:**
- **Values not yet tested on site are greyed out** ("not yet tested on site").
- **Confirm sheet:**
  - shows *from → to* in plain words;
  - shows the inverter's mode, load, battery level and grid status;
  - shows what happens after an overload (programs 06/23);
  - reminds you that nobody should be using the inverter's buttons;
  - asks for your password if you signed in more than 5 minutes ago;
  - enables Confirm after 2 seconds, and only while the phone is online.
- **Results you can see:**
  - **Applied**, plus "checking again for 10 min". *Applied* is not always *in effect*: for example, SbU stays on mains until the battery reaches the program-13 point.
  - **No change needed**: the logger already reads that value. Check the LCD if it disagrees.
  - **Refused**: nothing was sent, with the reason in words.
  - **Not confirmed yet**, then **Applied late** or **Failed**. When it fails, the page names the LCD program to check, and that setting is disabled remotely until it is re-tested on site.
  - **Changed back**: someone may have used the LCD.
- **With the read-only firmware** the page still shows all values, with "change settings on the inverter LCD". That part is useful even if Phase 2 is never built.

---

## 7. Safety guarantees

These are the promises the design makes. Each one has a test (R9 §10–§11).

1. **The normal firmware cannot write.** `savx` contains no setting command strings and no write code. CI checks both the strings and the symbols in the binary.
2. **Dangerous commands do not exist in any firmware.** Battery type, battery voltages, equalisation, output voltage and frequency, feed-to-grid, factory reset and REVO `PSET` are absent even from `savx-rw`.
3. **The cloud alone can never change a setting.** It cannot open a window, mark a value as tested, raise a limit or switch the feature on. It can only ask, close, or switch off.
4. **Physical presence is required** to open a window and for every action that makes things less safe.
5. **Only values seen on the LCD during on-site tests** can be sent remotely, and only for this exact inverter. If its identity (firmware, model, serial) changes, everything is revoked.
6. **One command per change, never repeated.** At most 10 remote changes in any 24 hours, a minute apart, one at a time.
7. **Read-back decides.** "Applied" means the inverter's own settings, read back in full, changed exactly as asked and nothing else did. Anything else revokes that setting and closes the window.
8. **Battery-aware limits.** Charging currents stay inside a range derived from the battery's label. Putting the house on the battery needs a low load, a charged battery and the grid present. Returning the house to mains is always allowed.
9. **No surprise after a power glitch.** The window is closed after every reboot. Writes are locked for 24 h after a brown-out or crash, or after a change was interrupted.
10. **You are always told.** Every window, change, refusal and failure is announced by push, through channels set up on the logger itself, and by e-mail. The e-mail is used only while its settings still match what was pinned when the feature was switched on.

---

## 8. Milestones

Effort is in evenings of 2–3 hours, as in [00 §8](00-plan-overview.md#8-milestone-roadmap). The first milestone after the go/no-go that touches the cloud is P2-M7; nothing is spent on the cloud or the app until the inverter has accepted a harmless command on site.

| M | Name | What is done | Effort | Accepted when |
|---|---|---|---|---|
| **P2-M0** | Gates and zero-write go/no-go | §2 items 1–6 and 10; REVO checks; LCD table and photos; battery data | 1–1.5 | Every gate ticked. **REVO → Phase 2 stops here**, and the read-only settings view is kept |
| **P2-M1** | v1.x hardening (both firmwares) | OTA needs a BOOT press; read-only "Inverter settings" view; inverter fingerprint | 1.5–2 | Tests green; OTA refused without a press; the view matches the LCD table; nothing new is sent to the inverter |
| **P2-M2** | Catalog and write path | One settings catalog generating the firmware tables, app labels and cloud-rule checks; the `savx-rw` build; CI checks | 2.5–3.5 | Every command's bytes match the reference; dangerous strings absent; `savx` has no setter code |
| **P2-M3** | Executor, window, logger page (flags only) | The 17 checks, read-back, follow-ups, interrupted-write recovery, 24 h lockout, rate limits, BOOT gestures, LED, logger page with press confirmation. Optional P2-M3b: touch pad | 3.5–4.5 (+0.5) | Every outcome tested; never more than one command per request; nothing sent after a reboot |
| **P2-M4** | Bench test with a fake inverter | Logger off the inverter for 2–3 h; every failure scenario | 1–2 | All scenarios behave; after re-installing, the v1 M3 quick check passes |
| **P2-M5** | **On-site GO/NO-GO** | Install `savx-rw`; CRC check; §9 tests 1–2 (buzzer, backlight) | 1 | **GO:** the buzzer same-value command is acknowledged, nothing changed, then mute/unmute verified on the LCD. **NO-GO:** refused, or anything unexpected → re-flash `savx` and record |
| **P2-M6** | Priorities and currents | The load/SOC/grid checks, battery-derived limits, cross-setting guards, spacing rules | 1–1.5 | Tests green; a high-load SbU request is refused on site |
| **P2-M7** | Cloud path | Database rules and their tests; the logger polling only while a window is open; kill switch; alert-settings pin; notices | 3.5–4.5 | Every rules test passes; a stale, repeated or doubled request is refused; notices arrive in < 30 s |
| **P2-M8** | Web app page | §6; demo mode; password re-entry; history | 2.5–3.5 | A backlight change from the iPhone completes in < 20 s; offline → Confirm disabled |
| **P2-M9** | Per-value acceptance | §9 tests 3–9 for every remote value, local then remote, including the power-cycle checks | 3–4 (over ≥ 3 days) | Each remote value tested in both directions on the LCD, or left disabled with a reason |
| **P2-M10** | 7-day soak with Phase 2 on | Normal logging plus ≥ 3 windows and ≥ 5 remote changes; doc updates | 1 + 7 days | v1 soak criteria still met; the persisted command counter equals the number of logged changes |
| **P2-M11** (optional) | Local-only extras | Programs 03, 06, 07, 23, 25 on the logger page | 1.5–2 | As P2-M9, local only |

**Totals:**
- **10.5–14.5 evenings** to the on-site go/no-go;
- **21.5–29 evenings plus 7 days** for the full remote feature;
- **23–31 evenings** with the optional extras.

---

## 9. On-site test procedure, per command

**Rules for every test:**
- You stand at the inverter LCD.
- The logger's page is open on a phone or laptop on the home Wi-Fi.
- The [07 global stop rules](07-milestones-and-test-plan.md) apply. **If the inverter beeps unexpectedly, shows a new warning or fault, or anything gets warm: close the window, pull the RJ45 from COM, and stop.**

**Order for every value:**
1. **Same-value test** first: re-send what is already set. Nothing changes.
2. **Change** to the new value; check the LCD.
3. **Change back**; check the LCD.
4. Tick **"seen on LCD"** for each direction, with a BOOT press.

**Before each session:**
1. Compare the logger's "Inverter settings" view with the LCD; they must agree.
2. Check that no warning or fault is showing.
3. Open the window: lid open, BOOT × 3.

| Test | Setting | Steps | You should see | Record | Stop if |
|---|---|---|---|---|---|
| **0** | (read only) | On the logger page, run the **CRC check**, then the one-time **allowed-currents read** | "input CRC checked" or "ignored"; lists such as `010 020 …` | Both results | Any warning on the LCD. **If CRC is ignored:** wait until link errors are < 0.1 % for 7 days before test 1 |
| **1** | **18 Alarm (buzzer)**. First write ever; the **go/no-go** | 1a. Same-value **ON** (`PEa`; the alarm is already on). 1b. **OFF** (`PDa`, local only, BOOT press). 1c. **ON** again within a minute | 1a: "applied", nothing on the LCD changes. 1b: the **mute icon** appears. 1c: it disappears | Exact reply bytes, response time, letter case, all three results | 1a refused → **NO-GO** (after one retry on another day). Anything else changed → NO-GO |
| **2** | **20 Backlight** | Same-value **on**; **off** (backlight goes dark); **on** | Backlight follows | Results | Backlight does not follow |
| **3** | **19 Return to default screen**, **22 Beep on mains cut** | Same-value; change; back (check the program value on the LCD) | LCD shows ESP ↔ HEP, AON ↔ AOF | Results | Mismatch |
| **4** | **01 Output priority** | Daytime, grid on, battery > 60 %, load low. Same-value **UtI**; **UtI → SOL** for 10 min (watch the load source on the LCD and in Live); **→ UtI**. **SbU** the same way, only once the battery data are recorded. The SbU command tests a special CRC case | The load source changes as expected; returning to UtI always works | Load, SOC, timings | Any overload or BMS warning; the house blinks |
| **5** | **11 Max mains charging current** | Grid on, battery not full. Same-value; **30 → 20 A** while mains-charging (battery current falls within 2 min); **→ 30** | The charging current follows | Currents before and after | Current does not follow |
| **6** | **02 Max total charging current** | Same-value; **baseline → baseline − 10 A → baseline**; then each value inside the remote range | Charging follows | Currents | Mismatch, or any battery warning |
| **7** | **16 Charger priority** | Only after the code order for this unit has been confirmed from the LCD (site survey B6). Same-value; **SNU → CSO → SNU**. CUt only if the code order allows it. OSO only as a short local test, then back | LCD shows the new value | Code order | Mismatch |
| **8** | **Remote confirmation**, per setting | Repeat one change of each setting **from the iPhone** while you watch the LCD | Same as the local test; push and e-mail arrive | Times | Any difference from the local test |
| **9** | **Power-cycle check** (01, 02, 11, 16, once each) | Leave a non-default but safe value set; switch the inverter off and on at a convenient time; check the value; restore | The value survives | Result | The value reverted. That setting then stays local only |

Never test, and never let anyone send, any **LCD only** command from §3, nor `PSAVE` or `PF`.

---

## 10. Rollback

Fastest first:

1. **Close the window**: in the app ("Close now"), on the logger page, or with BOOT × 3. Nothing can be written once it is closed.
2. **Kill switch**: "Switch remote settings off" in the app, or on the logger page. Turning it back on needs the logger page **and** a BOOT press.
3. **Disable one setting**: "Revoke" on the logger page.
4. **Go back to the read-only firmware**: flash `savx` over OTA (with a BOOT press) or USB ([08 §4.3](08-risks-troubleshooting-maintenance.md#43-roll-back-firmware)). The app hides the controls automatically.
5. **Lock the cloud**: remove the request permission from the database rules and redeploy ([06 §3](06-cloud-and-dashboard-guide.md#3-database-rules-render-deploy-test)).
6. **Put a setting back by hand** at the LCD, using the photo table from the site survey. **Never use factory reset.**
7. **If your account may be compromised:**
   - change the password;
   - use the kill switch;
   - check the e-mail alert settings;
   - re-pin them on the logger page with a BOOT press.

---

## 11. Open questions

1. **Touch pad.** Should the touch pad be built, or is opening the lid for every window acceptable?
2. **Window length.** Is a 10-minute, one-change window a good default, with at most 30 min and 5 changes? The earlier idea was "about 30 minutes". The safety review shortened the default because of the stolen-phone case.
3. **Battery data.** What are the battery's brand, capacity, and BMS charge and discharge ratings? Without them, remote current changes and SOL/SbU stay off.
4. **Power cycles.** Is one inverter power cycle per setting (about a minute without inverter output) acceptable for programs 01, 02, 11 and 16?
5. **Optional extras.** Should the local-only extras (P2-M11) be built at all?
6. **Uppercase variant.** If the buzzer test is refused on a non-REVO unit, should an uppercase command variant be tried, or is that the end?
7. **Away from home.** Should remote changes be allowed while you are away from home? The design allows them while a window is open; "home Wi-Fi only" cannot be checked by the cloud.
8. **Refusal e-mails.** Should refused attempts send an e-mail, or only a push?
9. **Event history.** Should the app account lose the ability to delete alert-history entries (a v1.x database-rules change)?

---

## 12. Related documents

- [research/r9-remote-settings.md](../research/r9-remote-settings.md): the full design, command table, state machine, data contract, rules, tests and sources.
- [00 — Plan overview](00-plan-overview.md): decision D11 (read-only) and the roadmap.
- [02 — Protocol reference §11](02-protocol-reference.md#11-read-only-safety-what-we-send-and-what-we-never-send): what v1 sends and never sends.
- [03 — Hardware and wiring](03-hardware-and-wiring.md): the NEVER list, the BOOT button, and the box placement.
- [05 — Firmware guide](05-firmware-guide.md): builds, OTA, the logger page.
- [06 — Cloud and dashboard guide](06-cloud-and-dashboard-guide.md): rules deployment, push channels, e-mail.
- [07 — Milestones and test plan](07-milestones-and-test-plan.md): the v1 milestones that must pass first, and the global stop rules.
- [08 — Risks, troubleshooting and maintenance](08-risks-troubleshooting-maintenance.md): firmware rollback and recovery.
- Site survey and build record: the LCD program table, the battery data, the probe results.
