# 03 — Hardware and wiring

This document explains how to build the logger hardware. It assumes no electronics background: every step says what to do, what you should see, and what to do if you see something else.

- **Read the whole document once before touching anything.**
- The order of work and the pass/fail checks are in [07 — Milestones and test plan](07-milestones-and-test-plan.md).
- Write your measurements and results in the site survey and build record.
- The design decisions behind this page are in [`research/d1-architecture.md`](../research/d1-architecture.md) §8. The electrical reasoning is in [`research/v1-protocol-verification.md`](../research/v1-protocol-verification.md) §6 and [`research/v2-platform-verification.md`](../research/v2-platform-verification.md) §3.2.

**The plan in five lines**

1. A cut LAN cable plugs into the inverter's free **COM** jack. Only three of its eight wires are used: pins 1, 2 and 8.
2. The RS485 module's receiver *listens* to the inverter's transmit wire and passes a safe 3.3 V copy to the ESP32.
3. The ESP32 *talks* to the inverter's receive wire directly, using a 0 / 3.3 V signal. We call this the **"RS232-lite"** link.
4. Before the ESP32 is allowed to touch either wire, receive-only tests find out which of pin 1 and pin 2 is the inverter's transmit wire, and prove that the other one sits between about 0 V and 3.3 V.
5. Everything is powered by the USB charger, plugged into a socket that the inverter keeps powered during grid outages.

No part needs to be bought and no soldering is needed. Joints are made by stripping, wrapping and clamping in screw terminals.

---

## 1. Safety rules: the NEVER list

> ⚠️ **Read this list before every work session.** Each rule protects you, the inverter, the lithium battery or the ESP32.

| # | NEVER… | Why |
|---|---|---|
| N1 | **…open any inverter cover, or touch the AC IN, AC OUT, BATTERY or PV IN terminals.** | PV input is up to **500 VDC**, the AC side is **230 VAC**, and the battery can deliver **145 A** and far more into a short circuit (nameplate, manual §11). Everything this project needs is on the outside of the bottom panel. |
| N2 | **…touch, unplug or re-seat the BMS jack or its grey cable.** | The inverter uses it to talk to the lithium battery's BMS. Disturbing it can raise battery-communication warnings and change how the battery is charged. |
| N3 | **…connect anything to COM pin 4**, or to pins 3, 5, 6 and 7. | Pin 4 is "+VCC", a supply for the vendor's WiFi dongle, at an **unknown voltage**. Pins 3, 5, 6 and 7 are undocumented. Only pins **1, 2 and 8** are ever connected. In a standard patch cable pin 4 is the **solid blue** wire (T568A and T568B alike); never strip a solid blue wire. |
| N4 | **…connect an ESP32 pin to COM pin 1 or pin 2 before the idle-level test (Stage C) has identified which one is the inverter's transmit wire, and the upper check (Stage C2) has shown that the other one is below 3.3 V.** | The inverter's transmit wire sits at about −5 to −12 V. The ESP32 is damaged by anything below −0.3 V or above 3.6 V. |
| N5 | **…connect GPIO17 to the pin that tested as the inverter's transmit wire.** | Same reason as N4. GPIO17 goes only to the *other* pin. |
| N6 | **…power the logger from the battery or from COM pin 4.** | No converter is available and pin 4's voltage is unknown. The USB charger is the only power source. |
| N7 | **…use a laptop that is plugged into its charger while the ESP32 is wired to the inverter.** | A mains-powered laptop can create a ground loop through the inverter's COM port. Run the Mac **on battery** for any test with the cable plugged into COM. |
| N8 | **…change any wiring while the RJ45 plug is in the COM jack or the ESP32 is powered.** | First pull the RJ45 out of COM, then unplug the USB cable. Rewire, check, photograph, and only then reconnect. |
| N9 | **…connect the module's B− terminal to 3V3 and to GND at the same time.** | That is a dead short of the ESP32's 3.3 V supply. The bench self-test (Stage B) and the upper check (Stage C2) use B− → 3V3 *temporarily*; pin 8 and the P5 jumper must be out of B− while that jumper is in, and the 3V3 jumper must be out before they go back. |
| N10 | **…power the RS485 module from the ESP32's 5V pin.** | Its outputs would then swing to 5 V, and no ESP32 pin tolerates 5 V. Use **3V3** only. |
| N11 | **…connect anything to the module's third screw terminal (marked 接大地, "connect to earth"), and never bridge its "R0" 120 Ω pad.** | The earth terminal is a surge return, not signal ground. A closed R0 pad would load the inverter's RS232 driver with 120 Ω. |
| N12 | **…plug the cut cable into a router, switch, computer, PoE port or the BMS jack.** | It is not an Ethernet cable any more. Label it (§7, step 9). |
| N13 | **…power the ESP32 from two sources at once.** | One USB cable at a time: either the charger or the Mac. |
| N14 | **…press ENTER while looking at the LCD's settings programs**, and never use the settings menu to change anything for this project. | This project is read-only towards the inverter. See site-survey §8 for the safe way to read the settings. |
| N15 | **…flash a firmware built with `INV_ALLOW_WRITES` set to anything but 0.** | The firmware only sends read commands (a whitelist). Setter commands could change battery voltages or factory-reset the inverter ([d1](../research/d1-architecture.md) §3.10). |
| N16 | **…let your hand, a tool or the logger cable touch the green AC IN / AC OUT, BATTERY or PV IN terminal blocks** while plugging the RJ45 into COM. | They sit uncovered just below the COM jack (photo) and carry 230 VAC, up to 500 VDC and the battery. Plug and unplug the RJ45 with one hand, from above, and route the cable up and away from them. |
| N17 | **…touch the ESP32, the module or bare wire ends while the RJ45 is in COM** (pressing the BOOT button's plastic cap is fine). | Pin 8 ties the logger to the inverter's internal signal ground. Nothing documents that this ground sits at earth potential [INFERRED]. Unplug the RJ45 first (N8). |

**Stop rules.** Stop, unplug the RJ45 from COM, and ask for help (or re-read [08 — Risks and troubleshooting](08-risks-troubleshooting-maintenance.md)) if any of these happen:

- The inverter beeps, shows a new warning or fault code, or its FAULT LED changes when you plug the RJ45 into COM.
- Any part gets warm to the touch, or you smell something hot.
- A test result does not match any row of the decision tables in this document.

---

## 2. Parts inventory (everything is already owned)

| # | Part | Where it is shown | What it does in this project |
|---|---|---|---|
| 1 | **ESP32-S3-N16R8 DevKitC-1 clone** with a USB-A to USB-C cable (about 50 cm) | order photo | The logger's computer: reads the inverter, stores data, runs WiFi, the local web page and the cloud upload. |
| 2 | **HW-0519-type RS485 ↔ TTL module** ("Serial Port RS485 To TTL UART Signal Level Converter DC 3.3V 5V Module", a local shop, ৳117) | order photo | Its **receiver** reads the inverter's transmit wire safely. Its transmitter is never used in the baseline. |
| 3 | **40 male-to-male jumper wires** | order photo | Cut and stripped to make wrap joints. The male pins clamp well in screw terminals and serve as splice posts. |
| 4 | **25 mm double-sided foam tape** | order photo | Mounts the boards and the box. With its liner left on one side, it also works as insulation. |
| 5 | **5 V / 2 A USB charger, US plug** | order photo | Powers the logger from an inverter-backed socket. |
| 6 | **100 × 68 × 50 mm ABS junction box with a clear lid** | order photo | Enclosure. |
| 7 | **Spare LAN patch cable** (at home) | – | Cut: the plug end goes into COM; three of its wires carry the signals. |
| 8 | **The inverter's paper manual** (at home) | `reference/Savx-F4-user-manual.pdf` | Source of the COM pinout, LCD menus and codes. |
| 9 | **Mac** (at home) | – | Builds and flashes the firmware once; later updates go over WiFi from the iPhone. |
| 10 | **iPhone** (at home) | – | Shows test results and, later, the dashboard. |

**Household tools you will use:** sharp scissors or a hobby knife, a small screwdriver for the module's screw terminals (an eyeglass/precision screwdriver fits), a Phillips screwdriver for the box lid, a ruler, a pen, and good light. A phone camera with zoom is useful for reading wire colours.

**Not owned, and not needed:** multimeter, soldering iron, MAX3232 module, resistors, female jumpers, electrical tape, heat-shrink, cable glands. They are optional upgrades only (§13).

---

## 3. Know your parts

### 3.1 The inverter's ports





The bottom panel (photo above) has two RJ45 jacks side by side:

| Jack | Label | Pins (chassis silkscreen and manual §2.3) | Status |
|---|---|---|---|
| Left | **COM** | 1 = RXD, 2 = TXD, 4 = +VCC (manual only), 8 = GND. RS232 according to the manual. | **Free. This is the logger's port.** |
| Right | **BMS** | 1 = 485-B, 2 = 485-A, 4 = CAN-H, 5 = CAN-L | **Occupied by the battery's grey cable. Never touch (N2).** |

What is **known** and what is **not**:

- **Known:** COM is an RS232 port and the protocol is very likely Voltronic "PI30" at 2400 baud ([02 — Protocol reference](02-protocol-reference.md)).
- **Not known:** whether "RXD" on pin 1 means "the inverter receives here" or "your PC receives here". Manufacturers label it both ways. Voltronic's own cable drawing suggests **pin 1 is the inverter's transmit wire**, but that is a probability, not a fact ([v1](../research/v1-protocol-verification.md) §0 item 6). The Stage C test (§8.4) settles it with no meter.
- **Not known:** the voltage on pin 4. It is never connected, so it never needs to be known.

### 3.2 The ESP32-S3 board



The board is a clone of Espressif's ESP32-S3-DevKitC-1 with the **N16R8** module: 16 MB flash and 8 MB PSRAM.

**Orientation.** Hold the board with the components facing up and the **two USB-C connectors towards you**:

- The metal-can module (ESP32-S3-WROOM-1) is at the far end. Its PCB antenna is the part of the module that sticks out beyond the can, at the far edge. The antenna needs free space (§11).
- The **BOOT** and **RST** buttons sit near the USB connectors.
- There is a small RGB LED. On clones it may stay dark until a solder jumper is bridged. That is fine: the web page is the real status display.
- The header pins point **down**, underneath the board, **if they are soldered**. Many clones ship with the two strips loose; then see §6.0 and do not fit them.

**The two USB-C ports.**

| Port label (check the silkscreen next to each connector) | What is behind it | Use it for |
|---|---|---|
| **"COM"** (sometimes "UART") | A CH343-type USB-to-serial chip connected to the ESP32's UART0 (GPIO43/44) | **Flashing, serial logs and serial commands. Also the power input in the final install.** |
| **"USB"** | The ESP32-S3's own USB pins (GPIO19/20) | Not used. |

If the labels are missing or unreadable, plug the board into the Mac and run:

```bash
# Newer macOS versions name the USB report SPUSBHostDataType; asking for both is harmless.
system_profiler SPUSBDataType SPUSBHostDataType 2>/dev/null | grep -iE -B3 -A6 "1a86|wch|303a|jtag"
```

- Vendor ID `0x1a86` (WCH) means you are on the **COM** port.
- Vendor ID `0x303a` (Espressif, "USB JTAG/serial debug unit") means you are on the native **USB** port. Move the cable to the other connector.
- If the command prints nothing, compare `ls /dev/cu.*` with the board unplugged and plugged in, and try each connector in turn: the one that flashes successfully with `pio run -e savx -t upload` and then shows the boot log at 115200 baud is the COM port ([05](05-firmware-guide.md)).

> ⚠️ Use one USB cable at a time (N13). On these clones the "5V" header pin is often *not* a 5 V output; never use it to power anything ([r5](../research/r5-hardware.md) §1.4).

**Pin map.** The ESP32 has two header rows. Espressif calls the row with 3V3/RST/GPIO4… **J1** and the row with G/TX/RX/GPIO1… **J3**. Positions below are from Espressif's DevKitC-1 user guide, counted from the end with **3V3** (J1) and the first **G** (J3). Clones normally match, but **always find a pin by the label printed next to it on your board**, not by counting alone.

| Header position | Label | Used for | Stage connected |
|---|---|---|---|
| J1-1 | 3V3 | Module **VCC** | B |
| J1-2 | 3V3 | *Temporary only:* module B− during the bench self-test | B (removed again) |
| J1-9 | 16 | Module header **TXD** (expected to be the module's driver input, "DI"). The firmware holds it HIGH so the module's transmitter stays off. | B |
| J1-10 | 17 | **UART1 TX → inverter receive wire** (inverted, 0 / 3.3 V) | **D only** (after the idle test) |
| J1-11 | 18 | Module header **RXD** (expected to be the module's receiver output, "RO") → UART1 RX (inverted) | B |
| J3-1 | G | *Temporary only:* module A+ during the bench self-test | B (removed again) |
| J3-21 | G | Module **B−** terminal (and through it, COM pin 8) | C |
| J3-22 | G | Module **GND** | B |
| J3-14 | 0 | BOOT button. Hold it 3 s while running to open the WiFi setup portal. | – (button only) |

If the self-test finds that the module's labels are swapped (header "TXD" is really the receiver output), the firmware simply swaps the roles of GPIO16 and GPIO18. You do not rewire anything.

**Pins never to use for anything:** 0, 3, 45, 46 (boot-strapping), 19 and 20 (native USB), 26–32 and 35–37 (flash and PSRAM), 43 and 44 (the COM port's serial lines), and the **5V** pin ([v1](../research/v1-protocol-verification.md) C18, [r5](../research/r5-hardware.md) §1.3).

> ⚠️ **GPIO16, 17 and 18 are neighbours** (J1-9, J1-10, J1-11), only 2.54 mm apart. Wrap joints on neighbouring pins can touch. §6.3 shows how to stagger them. The self-test also reports a short between GPIO16 and GPIO18 (both read LOW together).

### 3.3 The HW-0519 RS485 module



The module was identified from the order photo as the common auto-direction "HW-0519" board ([r5](../research/r5-hardware.md) §2.1, [v2](../research/v2-platform-verification.md) H1–H3). Check your board's silkscreen against this table:

| Where | Marking | Meaning | In this project |
|---|---|---|---|
| 3-way screw terminal | **A+** | Non-inverting bus input | Inverter **transmit** wire (pin 1 or 2, decided in Stage C) |
| | **B−** | Inverting bus input | COM **pin 8 (GND)** and ESP32 GND |
| | **接大地** ("connect to earth") | Surge-protection return. A forum measurement found it is **not** connected to GND. | **Leave empty (N11)** |
| 4-pin header | **VCC** | Supply, 3.3 V or 5 V | ESP32 **3V3** only (N10) |
| | **GND** | Logic ground | ESP32 GND |
| | **RXD** | Expected: receiver output (RO), "goes to the MCU's RX" | ESP32 GPIO18 |
| | **TXD** | Expected: driver input (DI), "comes from the MCU's TX" | ESP32 GPIO16 (held HIGH) |
| Small pad | **R0** / 120R | Termination resistor, **open as shipped** | **Leave open (N11)**. Check with the phone camera: two separate pads, no solder or resistor across them |
| Two small LEDs | – | RXD / TXD activity | Not used for any decision |
| Black SMD diodes and small parts | "WK", "T110" | Most likely TVS surge diodes and resettable fuses on A/B [INFERRED from the similar ProtoSupplies board] | They are why the A input survives the inverter's negative voltage |

#### 3.3.1 Your actual module (photos 2026-10-21) differs from the table above

 

| Seen on your board | Consequence |
|---|---|
| **No screw-terminal block.** A+, B− and 接大地 are three large plated holes. The 4-pin header is loose too. | Join wires through those holes (§6.0 loop-through joint; the large holes make it easy). §6.2's "screw terminal" steps apply only if a terminal block is fitted. |
| Transceiver **MAX485 CSA** and a **CD4069UBM** inverter (the auto-direction logic) | The MAX485 is specified for 5 V. Powered from 3V3 it normally still **receives** correctly, which is all RS232-lite uses; the self-test (Stage B) proves it. Never power the module from 5 V: its receiver output would then drive 5 V into GPIO18. |
| Two **T010** parts (resettable fuses) and three black SMB diodes (TVS surge diodes) | Series protection and clamping on A/B, as assumed in §4. |
| **R14 marked "121" = 120 Ω**, right below the A+/B− holes | Very likely the **120 Ω termination across A and B is fitted**, unlike the R0 row above. It changes two things: (1) the inverter's RS232 transmit line is loaded by 120 Ω (its driver is short-circuit protected, so this is within spec, and it actually keeps the A input far from its limits); (2) when A+ sits on the inverter's *receive* pin, A and B are almost equal, so the receiver output may not idle HIGH. **Bench check B3c** (§8.3) tests this before Stage C. |

**Why the header labels are tested, not trusted.** Sellers and tutorials disagree on whether "RXD" means "connect to the MCU's RX" or "this is the module's RX" ([r5](../research/r5-hardware.md) §2.3). The bench self-test (Stage B) identifies the receiver output electrically, with no risk.

**Why "auto-direction" matters.** This module has no DE/RE pin. Its transmitter switches on by itself when the TXD (DI) input goes LOW. Holding DI HIGH keeps the transmitter off for good, which is exactly what the RS232-lite link needs.

---

## 4. How the "RS232-lite" link works (plain-language version)

**The problem.** RS232 signals swing both ways around zero: roughly −5 to −12 V for "idle / 1" and +5 to +12 V for "0" ([Wikipedia: RS-232](https://en.wikipedia.org/wiki/RS-232)). The ESP32 works with 0 V and 3.3 V and is damaged below −0.3 V. A MAX3232 chip normally translates between the two, but there isn't one.

**Receiving (inverter → ESP32).** An RS485 receiver compares its two inputs. It outputs **HIGH** when A is at least 0.2 V *above* B, and **LOW** when A is at least 0.2 V *below* B. Connect **B− to the inverter's ground (pin 8)** and **A+ to the inverter's transmit wire**:

```
 Inverter transmit wire       A+ compared with B- (= GND)    Module RXD output     ESP32 UART (RX inverted)
 idle and "1" bits: -5..-12 V  A below B                  ->  LOW  (0 V)        ->  reads "1" / idle
 "0" bits:          +5..+12 V  A above B                  ->  HIGH (3.3 V)      ->  reads "0"
```

The module produces an **upside-down 3.3 V copy** of the inverter's data. The ESP32's UART can flip it back in hardware ("RX invert"). The negative voltage only ever reaches the module's protected A input, never an ESP32 pin.

**Sending (ESP32 → inverter).** An RS232 receiver chip such as the MAX3232 is guaranteed to read anything **below 0.6 V** as "idle / 1" and anything **above 2.4 V** as "0". Its input looks like a 3–7 kΩ resistor to ground ([TI MAX3232 datasheet](https://www.ti.com/lit/ds/symlink/max3232.pdf)). So the ESP32's TX pin, inverted ("TX invert"), works directly:

```
 ESP32 GPIO17 (TX inverted)          Inverter's RS232 receiver
 idle and "1" bits: 0 V          ->  below 0.6 V  -> "1" / idle   OK
 start and "0" bits: 3.3 V       ->  above 2.4 V  -> "0"          OK
 load: 3.3 V / 3 kOhm = about 1.1 mA from the GPIO               OK
```

This is not a standards-compliant RS232 link. It relies on the threshold behaviour of common RS232 receiver chips, and the inverter's actual chip is unknown [INFERRED]. It is a deliberate workaround with stated risks:

- **The module's A input can sit below its official rating.** A classic RS485 transceiver is rated to about −7.5 V (MAX3485; clones quote ±13–15 V); the inverter may idle at −5 to −12 V. The module's TVS diodes and fuses and the inverter's current-limited driver make this survivable in practice. The likely worst case is a dead ৳117 module ([v2](../research/v2-platform-verification.md) §3.2). The ESP32 is not directly exposed, but if the transceiver chip fails *internally* the negative voltage could reach GPIO18 through its RO pin. That is unlikely, but it can't be ruled out without a series resistor (§13).
- **The module's own transmitter must stay off.** It switches on whenever its DI pin (header "TXD") is LOW. In this wiring B− is tied to ground, so an active transmitter would push B− HIGH into that ground short and pull A+ against the inverter's transmit line. The firmware holds DI HIGH from the first lines of `setup()`. It can't do that during the ESP32's own ROM and bootloader start (a fraction of a second per boot), so there the module's own pull-up has to hold DI HIGH, if it has one (appendix A).
- **The inverter's receive wire sees the module's bias during the Stage C tests.** The module's A pull-up (about 4.7 kΩ on the similar ProtoSupplies board) lifts it to roughly 1.3–2 V against the 3–7 kΩ input, between an RS232 receiver's thresholds. At worst the inverter sees a few garbage bytes, which it ignores.
- **If it does not work,** the firmware tries the other polarities automatically. The clean fix, if a shop visit ever happens, is a ৳80 MAX3232 module (§9.4).

---

## 5. The final wiring

### 5.1 Diagram (baseline, after Stage D)

```
 INVERTER  COM jack (RJ45)                 HW-0519 module                         ESP32-S3 DevKitC-1
 (cut LAN cable)                           +-----------------------------+
                                           | screw terminal              |
  pin X = inverter TRANSMIT  ------------->| A+                          |
  pin 8 = GND  ----------------------+---->| B-                          |
                                     |     | 接大地 (earth)  -- EMPTY    |
                                     |     | R0 pad          -- OPEN     |
                                     |     |                             |
                                     |     | header   VCC <--------------+------  3V3   (J1-1)
                                     |     |          GND <--------------+------  G     (J3-22)
                                     |     |          RXD  --------------+----->  GPIO18 (J1-11)  RX, inverted
                                     |     |          TXD <--------------+------  GPIO16 (J1-9)   held HIGH
                                     |     +-----------------------------+
                                     +-- jumper (male pin clamped in B-) ------>  G     (J3-21)
  pin Y = inverter RECEIVE  ---------- splice ---------------------------------->  GPIO17 (J1-10)  TX, inverted
                                                                                    (connected ONLY in Stage D)
  pins 3, 4, 5, 6, 7: cut short, folded back, insulated -- NEVER CONNECTED
                                                                                    USB-C "COM" <-- 5 V charger
  X and Y are 1 and 2 in some order. Stage C decides. Most likely X = 1, Y = 2.     in an AC-OUT socket
```

### 5.2 Connection table

| # | From | To | How | Stage |
|---|---|---|---|---|
| P1 | Module header VCC | ESP32 3V3 (J1-1) | Jumper, wrap joints at both ends | B |
| P2 | Module header GND | ESP32 G (J3-22) | Jumper, wrap joints at both ends | B |
| P3 | Module header RXD | ESP32 GPIO18 (J1-11) | Jumper, wrap joints at both ends | B |
| P4 | Module header TXD | ESP32 GPIO16 (J1-9) | Jumper, wrap joints at both ends | B |
| T1 | Module A+ | ESP32 G (J3-1) | Jumper: male pin in the screw terminal, wrap at the ESP32. **Temporary.** | B only |
| T2 | Module B− | ESP32 3V3 (J1-2) | Jumper: male pin in the screw terminal, wrap at the ESP32. **Temporary (N9).** | B only |
| P5 | Module B− | ESP32 G (J3-21) | Jumper: male pin in the screw terminal, wrap at the ESP32 | C |
| C8 | LAN conductor of RJ45 pin 8 | Module B− (same terminal as P5) | Screw terminal | C |
| CX | LAN conductor of the candidate pin (1, then 2) | Module A+ | Screw terminal | C |
| T3 | Module B− | ESP32 3V3 (J1-2) | Same jumper as T2. **Temporary**, only while pin 8 + P5 are **out** of B− (N9) | C2 only |
| P6 | LAN conductor of the inverter's **receive** pin | ESP32 GPIO17 (J1-10) | Jumper: wrap at the ESP32; the LAN wire is wrapped onto the jumper's male pin (splice) | **D** |
| – | USB-C "COM" | Charger in an AC-OUT socket (or the Mac on battery) | Supplied cable | all |

---

## 6. Making connections without solder

### 6.0 If your ESP32 board came with loose (unsoldered) header strips

Many N16R8 clones, including the one in this photo (back), ship with the two header strips **not soldered**. Flashing and the Arduino board check need only the USB-C cable, so nothing has to be done about the headers to get started.

**Do not push the loose strips into the board and rely on friction.** The contact is intermittent, and an intermittent GND or receive wire produces exactly the CRC errors and "link lost" alarms you are trying to avoid.

The logger needs only these holes. Find each one by the label printed on the board, not by counting:

| Hole label | Row/position | Used for | When |
|---|---|---|---|
| **3V3** | J1-1 | Module VCC | Stage B onwards |
| **3V3** (second) | J1-2 | *Temporary* T2 jumper (module B− during the self-test) | Stage B only |
| **16** | J1-9 | Module header TXD (DI) | Stage B onwards |
| **17** | J1-10 | Inverter receive wire | **Stage D only** |
| **18** | J1-11 | Module header RXD (RO) | Stage B onwards |
| **GND** (first on J3) | J3-1 | *Temporary* T1 jumper (module A+ during the self-test) | Stage B only |
| **GND** | J3-21 | Module B− (and COM pin 8) | Stage C onwards |
| **GND** | J3-22 | Module GND | Stage B onwards |

**A0. Quick bench joint (Stage B only): the bent-pin jumper**

For the bench self-tests you can keep the jumpers' male ends. Push a male pin down through the hole from the top until its black plastic sits on the board, then bend the tip that sticks out underneath firmly sideways, towards the board edge, so it wedges against the hole. Tape it. The 2.54 mm housings fit side by side in neighbouring holes (16, 17, 18). This contact is good enough for an evening of tests but **not** for the permanent install. Before Stage C, replace each one with joint A or B below.

**A. Without a soldering iron (baseline): the loop-through-hole joint**

1. Prepare the jumper (§6.1) but strip **12–15 mm** and twist the strands very tightly.
2. From the **top** (component side), push the bare end **down through the hole**.
3. Bend the end that comes out underneath **back up around the board edge** (the header holes are about 1.3 mm from the edge) and twist it 3–4 turns around the insulated part of the same wire, on top of the board. The copper now presses on the hole's plating and on both pad rings.
4. Keep the loop **perpendicular to the board edge** so it cannot lean against the neighbouring hole 2.54 mm away. GPIO16, 17 and 18 are neighbours: lead their wires away at slightly different angles.
5. Put a small piece of foam tape (liner on, §6.5) over each loop, then tug test (§6.6).

**B. With a soldering iron (recommended if you get one): solder wires straight into the holes**

1. **Do not solder the full header strips.** They add nothing here: male pins still don't mate with your male-male jumpers, and the pins underneath make the board harder to mount.
2. Push a stripped, tinned jumper end 2–3 mm through the hole from the top, solder it on the underside, and trim the excess.
3. The board then lies flat in the box (§11.2).

Keep the yellow strips as spares. A single pin snapped off a strip makes a good splice post (§6.4) or a test point.

**The RS485 module's 4-pin header may be loose too.** Treat it the same way: don't fit the strip, and use the loop-through-hole joint (or solder) on its **VCC, TXD, RXD and GND** holes. Its screw terminals (A+, B−) need no soldering.

### 6.1 Preparing a jumper wire

1. Decide which end of the jumper keeps its male pin:
   - **Both ends cut** for header-to-header links (P1–P4).
   - **One male pin kept** for links to a screw terminal (T1, T2, P5) or to a LAN wire (P6).
2. Cut off the plastic plug you do not need, right behind the plug.
3. Strip **8–10 mm** of insulation:
   - with scissors: close the blades *gently* around the insulation until they bite the plastic but not the copper, rotate the wire a full turn, then pull the insulation off; or
   - with a hobby knife: lay the wire on a cutting surface and roll it under the blade with light pressure, then pull.
4. If the copper is stranded, twist the strands tightly in one direction until they form a single neat strand. Cut off any stray strand.
5. If you nicked the copper or lost strands, cut the end off and strip again. There are 40 jumpers; use a fresh one if in doubt.

### 6.2 Screw-terminal joints (module A+, B−)

1. Loosen the screw 2–3 turns.
2. Insert either a jumper's **male pin** (it clamps very well) or a stripped, twisted wire end:
   - for a thin LAN conductor, strip 15 mm, twist, and fold the bare end back on itself to double its thickness;
   - for **two wires in one terminal** (B− holds P5 and the pin-8 conductor), first wrap the LAN conductor tightly around P5's male pin and insert the pair. This is the recommended way: in Stage C2 the pair comes out of B− as one piece and pin 8 stays on ESP GND.
3. Tighten firmly but do not strip the screw.
4. **Tug test:** pull each wire gently on its own. It must not move. No bare copper may stick out more than 1 mm from the terminal.

### 6.3 Wrap joints on header pins (ESP32 and the module header)

1. Find the pin by its printed label.
2. Lay the stripped end against the pin and wind it **3–4 tight turns** around the exposed metal, starting right against the black plastic spacer. Pinch the turns tight with your fingernails.
3. Bend the remaining tail along the pin so nothing points sideways towards a neighbour.
4. **Stagger neighbours.** On J1-9 (GPIO16) and J1-11 (GPIO18) put the wrap at the **base** of the pin, next to the plastic. When GPIO17 (J1-10) is added in Stage D, put its wrap at the **tip** of the pin. Lead each wire away in a different direction. The firmware catches some shorts here: GPIO16–GPIO18 in the self-test (both LOW), and GPIO16–GPIO17 in the pre-arm check and at every boot (GPIO17 reads HIGH). It can't catch all of them, so look carefully.
5. Insulate the joint (§6.5).
6. Tug test and look closely (§6.6).

### 6.4 Wire-to-wire splice (GPIO17 to the LAN conductor, P6)

Use the jumper's male pin as a little splice post:

1. Keep the male pin on the LAN end of the P6 jumper; strip the other end and wrap it on GPIO17 (§6.3).
2. Strip **15 mm** of the LAN conductor (the inverter's receive pin) and twist it.
3. Wind it **5–6 tight turns** around the male pin's metal part.
4. Insulate the whole splice (§6.5) so no metal shows, and tug test.

### 6.5 Insulating with what you own

- **Foam tape with its liner on.** Cut a small piece (for example 8 × 12 mm), peel the liner from **one side only**, and fold the sticky side around the joint so the outer liner faces out. The foam and liner are insulators.
- On crowded neighbouring pins, a full foam wrap can be too bulky. Use a thin strip on one side as a "flag" between the two joints instead, and rely on the staggered positions from §6.3.
- Any thinner household tape (cellophane or masking tape) is fine for indoor 3.3 V wiring. PVC electrical tape or heat-shrink is nicer but optional (§13).
- **The cut LAN conductors (pins 3–7) must always be insulated** (§7, step 7).

### 6.6 Inspection after every wiring session

1. **Tug test** every joint you touched.
2. **Look for strays:** no strand of copper may bridge to a neighbouring pin. Use the phone camera zoomed in.
3. **Compare with §5.2** and tick each line in the build record (site-survey Part 2).
4. **Photograph** the wiring from above and from the side before powering up. If something fails later, these photos are the first thing to check.

### 6.7 Optional upgrade: if you get a soldering iron

The baseline does not need it. If a soldering iron and solder become available:

1. Solder each wrap joint to its header pin (heat the pin and the wire together for 2–3 s, feed a little solder, let it cool without moving).
2. Solder the P6 splice, or replace it with a direct soldered joint.
3. Tin the LAN conductors that go into the screw terminals.
4. Keep the screw terminals. Never solder anything to the inverter side.
5. Cover soldered joints with heat-shrink if you have it, otherwise with tape.

---

## 7. Preparing the LAN cable

> ⚠️ Do this with the cable **unplugged** from everything.

1. **Find pin 1 on the plug.** Hold the plug with the **clip away from you**, the **gold contacts facing you**, and the cable hanging **down**. Pin 1 is the contact on the **left**, pin 8 on the right.
2. **Record the colours.** Look through the clear plug body (slide back the rubber boot if there is one) and write down the insulation colour that runs to each contact, pin 1 to pin 8, in the build record (site-survey Part 2, table B3). Take a zoomed photo too.
   - Do **not** assume the T568A or T568B standard. The usual T568B order is white/orange, orange, white/green, blue, white/blue, green, white/brown, brown, but your cable may differ ([Wikipedia: TIA-568](https://en.wikipedia.org/wiki/ANSI/TIA-568)).
   - If you cannot tell white/orange from white/green, use daylight and the camera.
3. **Cut the cable** about **1–1.5 m** from the plug. Keep the plug end. Keep the rest of the cable as spare wire.
4. **Remove the outer jacket** for about **5 cm** at the cut end: score it lightly all round with the knife, bend it to crack it, and pull it off. Do not cut into the inner wires. Untwist the pairs.
5. **Identify the three wires you need** by colour from your record: the wires for **pin 1**, **pin 2** and **pin 8**. Then cross-check them by their twists, which needs no colour reading:
   - in every standard patch cable (T568A or T568B), **pins 1 and 2 are one twisted pair**: the two wires must be twisted around each other;
   - **pin 8 is twisted with pin 7** (the brown pair);
   - **pin 4 is solid blue, twisted with pin 5** (white/blue). If a wire you picked is solid blue, or is twisted with a blue wire, stop: you have the wrong wire (N3);
   - if the cable has no twists (a flat cable) or the twists contradict your colour record, do not guess. Use another cable or ask someone with good eyesight to re-read the colours.
6. **Strip only those three,** 8–10 mm for now (§6.1). You may re-strip to 15 mm later for the splice.
7. **Make the other five safe** (pins 3, 4, 5, 6, 7; **pin 4 is the +VCC supply**):
   1. Cut each one off near the jacket, at slightly different lengths (about 5 mm apart) so the cut ends cannot meet.
   2. Fold each one back along the jacket.
   3. Wrap a band of foam tape (liner outwards) over all five fold-backs.
   They are never stripped, never used, and never connected.
8. **Insulate the bare ends of pins 1 and 2** with a small piece of tape until they are needed. Only one of them is connected at a time during Stage C.
9. **Label the cable** near the plug: "INVERTER COM – NOT ETHERNET". Write on paper or a strip of foam-tape liner and stick it round the cable.

> ⚠️ The RJ45 plug goes **only** into the inverter's **COM** jack, never into BMS (N2, N12).

---

## 8. The staged connection procedure

### 8.1 Overview

```
 Stage A  Inspect (no tools, nothing connected)                        -> site-survey, milestone M0
 Stage B  Bench: ESP32 + module only, inverter NOT connected           -> milestone M1
           B1 UART loopback   B2 module wiring   B3 module self-test (A+ -> GND, B- -> 3V3)
 Stage C  Idle-level test, RECEIVE ONLY: A+ on pin 1, then on pin 2    -> milestone M2
           no ESP32 pin touches the RJ45 wires
 Stage C2 Upper check, RECEIVE ONLY: A+ on the RX candidate, B- on 3V3  -> milestone M2
           proves the pin GPIO17 will go to is below 3.3 V
 Stage D  Connect GPIO17 to the OTHER pin, arm the link, run the probe -> milestone M3
 Stage E  Dialect classification (automatic, in firmware)              -> milestone M3
```

Names used below (local web page buttons, `/api/...` routes and serial commands such as `selftest`, `idle`, `idle N 3v3`, `arm`, `probe`) match firmware 0.6.1. If the firmware uses different names, [05 — Firmware guide](05-firmware-guide.md) is authoritative.

### 8.2 Rules for every stage

1. **Before any wiring change:** if the link is armed, disarm it first (`reset-link`, or **Diagnostics → Disarm / reset link**), then pull the RJ45 out of COM, then unplug the ESP32's USB cable (N8). After the change, redo the tests of the stage you are back in.
2. **One power source:** the charger, *or* the Mac on battery (N7, N13).
3. **Before powering up:** inspect (§6.6), photograph, and tick the build record.
4. **When reconnecting:** plug in the ESP32's USB power first, wait for it to boot, then plug the RJ45 into COM.
5. **Keep the inverter on and running normally** (LCD lit). The COM port's transmit wire only idles negative while the inverter's control board is powered.
6. **Read results on the iPhone** (local web page, same WiFi) or on the Mac's serial monitor (Mac on battery).

### 8.3 Stage B: bench tests (inverter not connected)

This is part of milestone M1. The firmware is already flashed and has joined your WiFi ([07](07-milestones-and-test-plan.md) M1).

**B1. UART loopback (optional but recommended). Do it before B2.**

1. With nothing else wired to GPIO17 or GPIO18, wrap one short stripped jumper between **GPIO17 (J1-10)** and **GPIO18 (J1-11)**. The firmware refuses the loopback once a self-test or idle test has been stored. Those mean the module is wired, and the jumper would make GPIO17 fight the module's output. To run it later, remove the module's header wires and run `reset-link all` first.
2. Run the loopback test (serial command `loopback yes`, or as described in [05](05-firmware-guide.md)). The firmware sends the QPI frame `51 50 49 BE AC 0D` and must read it back byte-exact, inverted and non-inverted.
3. **Remove the loopback wire.**

**B2. Wire the module header.** Fit P1 (VCC → 3V3), P2 (GND → G), P3 (RXD → GPIO18) and P4 (TXD → GPIO16). Both screw terminals A+ and B− stay **empty**.

**B3. Module self-test.**

1. Power the ESP32. Open `http://solar.local` on the iPhone, go to **Diagnostics**, enter the admin password in the **Admin** box (**Use**), and tap **Step 1 → Run self-ID**. On the serial monitor the equivalent is `selftest`. The result is also available at `GET /api/diag/pins`.
2. **Step 1, A/B open.** Expected: GPIO16 and GPIO18 both read **HIGH** (the module's receiver output idles HIGH; the driver input is pulled up).
3. Unplug the USB. Fit the temporary jumpers **T1 (A+ → G, J3-1)** and **T2 (B− → 3V3, J1-2)**. This forces A below B, which makes the receiver output go LOW. It is the same polarity the module's own transmitter would produce, so nothing can fight [INFERRED, d1 §8.4].
4. Power up and run the self-test again (**Step 2**). Interpret:

| GPIO18 (module "RXD") | GPIO16 (module "TXD") | Meaning | Action |
|---|---|---|---|
| **LOW** | HIGH | Labels are straight-through (expected): RXD = receiver output | Firmware stores RO = 18, DI = 16. Continue. |
| HIGH | **LOW** | Labels are crossed: TXD = receiver output | Firmware stores RO = 16, DI = 18. No rewiring. Continue. |
| HIGH | HIGH | Nothing responded | Unpower. Check that T1 is on **A+** and T2 on **B−** (not swapped), that P1 really reaches 3V3, and that the screws are tight. Repeat. |
| LOW | LOW | Probably a **short between the GPIO16 and GPIO18 wraps** | Unpower. Separate the wraps (§6.3). Repeat. |

5. **Unplug the USB and remove T1 and T2 completely**, at both ends (N9). Put a small piece of tape over the now-bare J3-1 and J1-2 pins.
5b. **B3c, termination check (needed for modules with R14 = 120 Ω fitted, §3.3.1).** This imitates Stage C's "A+ on the inverter's receive pin" case on the bench. USB out. Put **one** wire from **A+** and **one** from **B−** both onto ESP32 **GND** (e.g. J3-1 and J3-21). USB in, run **Run self-ID** once more and note what the receiver-output pin (found in step 4) reads. Then USB out and remove both wires.
   - Receiver output reads **HIGH** steadily (verdict `open-ok`): the idle test in Stage C can tell the two pins apart with the termination fitted. Continue.
   - Receiver output reads **LOW** or toggles (`ro-found`, `both-low` or `noisy`): with R14 fitted, Stage C cannot identify the receive pin reliably (it would stop safely with "both LOW"). Before Stage C, R14 has to be removed: with a soldering iron, heat it and flick it off; without one, push it sideways off its pads with a small flat screwdriver until it breaks free, then check with the phone camera that no fragment bridges the two pads. Repeat B3c afterwards; it must now read HIGH.
6. Record the result (RO pin, DI pin) in the build record.

From now on the firmware drives the DI pin **HIGH** permanently, which keeps the module's transmitter off.

### 8.4 Stage C: idle-level test (receive only)

This is milestone M2. Goal: find which of pin 1 and pin 2 is the **inverter's transmit wire**, without any ESP32 pin touching either of them.

**Wiring for Stage C.**

| Connect | Leave unconnected and insulated |
|---|---|
| P5: B− → ESP32 G (J3-21) | GPIO17: nothing wired, or its jumper's free end taped |
| LAN **pin 8** → B− (same terminal as P5) | The other candidate's LAN wire (taped) |
| LAN **candidate pin** (pin 1 first) → A+ | Pins 3–7 (already insulated) |

**Procedure.**

1. Wire as above with **pin 1** on A+. Inspect, photograph.
2. Plug the ESP32's USB into the charger (in the AC-OUT socket near the inverter) or into the Mac on battery. Wait for it to boot.
3. Make sure the inverter is on and its LCD is lit. Plug the RJ45 into **COM**. Watch the LCD and LEDs for 10 seconds; nothing should change (stop rules, §1).
4. On the iPhone: **Diagnostics → Step 2 → Test pin 1 (10 s)**. This calls `POST /api/diag/idle?pin=1`. On the serial monitor: `idle 1`. The firmware samples the receiver output every 10 ms for 10 s and reports the percentage of time it was LOW and the number of edges (changes). An example result line (wording may differ):
   ```
   idle pin=1 secs=10 samples=1000 low=100.0% edges=0 -> idles NEGATIVE: looks like INVERTER TX
   ```
5. Repeat the test once more for pin 1.
6. Pull the RJ45 out of COM, unplug the USB. Move: pin 1's wire out of A+ (tape its end), **pin 2's** wire into A+.
7. Reconnect (USB first, then RJ45) and run the test twice with **"A+ on pin 2"** (`idle 2`).

**Decision table.** "LOW" means at least 99 % LOW with fewer than 3 edges in 10 s. "HIGH" means at most 1 % LOW.

| Pin 1 result | Pin 2 result | Meaning | Next |
|---|---|---|---|
| **LOW** | HIGH | Pin 1 = **inverter transmit**, pin 2 = probably the inverter receive (the expected case) | Stage C2 on **pin 2**, then Stage D: A+ on **pin 1**, GPIO17 → **pin 2** |
| HIGH | **LOW** | Pin 2 = **inverter transmit**, pin 1 = probably the inverter receive | Stage C2 on **pin 1**, then Stage D: A+ on **pin 2**, GPIO17 → **pin 1** |
| HIGH | HIGH | Neither pin idles negative | Check the basics below, then §9.1 (fallback F1) |
| LOW | LOW | Both look negative: should be impossible | **Stop.** Re-check the colour record: the wire in B− may not be pin 8. |
| Many edges or 2–98 % LOW on either | – | Something is transmitting, or the input is floating | Check that nothing else is plugged into COM and the inverter is on. Record the numbers. See [08](08-risks-troubleshooting-maintenance.md). |

Why the expected result looks like that: an RS232 transmit wire idles negative, so A is below B and the output is a steady LOW. An RS232 receive input is a 3–7 kΩ resistor to ground, so the module's own bias lifts A slightly above B and the output stays HIGH ([v1](../research/v1-protocol-verification.md) §6.3).

**If both read HIGH, check the basics first:**

- Is the inverter actually on, with the LCD lit?
- Did the self-test (B3) pass? If not, the firmware may be sampling the wrong module pin.
- Is the pin-8 wire really in B−, and the candidate really in A+? Compare colours with your record.
- Are the B− screw and the P5 jumper tight?
- Do the GPIO16 and GPIO18 wraps touch? A short there makes the module's receiver output fight the firmware's DI drive, and the test then reads HIGH. Unplug and separate them (§6.3).

Write all four results (pin 1 twice, pin 2 twice) in the build record.

### 8.4b Stage C2: upper check of the receive pin (receive only)

**Why.** Stage C proves which wire idles **negative** (the inverter's transmit wire). A HIGH result on the other wire only proves it is **not negative**. A wire carrying a **positive** voltage reads HIGH too. That could be pin 4 "+VCC", picked up by a colour mix-up, and it would destroy GPIO17. Stage C2 compares the receive candidate with 3.3 V instead of with ground. The module's receiver decides within ±0.2 V, so together the two tests bound the wire to about −0.2 V … +3.5 V, inside GPIO17's −0.3 … +3.6 V limits. A real RS232 receive input sits near 0 V. The firmware **refuses to arm** until this check has passed twice. Nothing in this stage connects an ESP32 pin to the RJ45 wires.

**Wiring for Stage C2** (only after a clear Stage C result: one pin LOW twice, the other HIGH):

1. Pull the RJ45 out of COM, then unplug the USB (N8).
2. Take the **pin-8 conductor together with the P5 jumper** out of B− as one piece (§6.2). They stay joined, and P5 stays wrapped on ESP **G (J3-21)**, so pin 8 is still on ESP ground. Cover the bare male pin with tape.
3. Check that **B− is now empty**. Then insert the **T3 jumper: B− → ESP 3V3 (J1-2)**. Never fit T3 while pin 8 or P5 is in B− (N9).
4. Put the **receive candidate** (the wire that read HIGH in Stage C) on **A+**. Tape the transmit wire's bare end.
5. Inspect and photograph. Power the ESP32. **If the board does not boot or the power LED stays dark, unplug the USB at once**: B− is probably touching ground somewhere.
6. Plug the RJ45 into COM and watch the LCD for 10 s.
7. Run the upper check **twice**: **Diagnostics → Step 2 → "2b - Upper check of the RX pin" → Upper check pin N**, serial **`idle N 3v3`**, or `POST /api/diag/idle?pin=N&ref=3v3`.

| Result (each run) | Meaning | Next |
|---|---|---|
| **`below-3V3`** (≥ 99 % LOW, < 3 edges) | The wire sits below about 3.3 V (±0.2 V) even with the module's pull-up on it: an RS232 receive input | Run it a second time. Two passes unlock arming. |
| **`ABOVE-3V3`** (≤ 1 % LOW) | A **positive** source, **not** the inverter's receive wire | **STOP.** Never connect GPIO17 to it. Re-check the colours and twists (§7 step 5), then `reset-link` and redo Stage C. The firmware blocks arming until you do. |
| Edges, or 2–98 % LOW | Noise, a loose A+ wire, or traffic | Tug test, repeat. Arming stays blocked. |

8. **Undo Stage C2:** RJ45 out, USB out. Remove T3 from B− **completely**. Put the **inverter transmit** wire back on A+, then put the **pin 8 + P5 pair** back into B−. B− now holds the pair and nothing else.

> ⚠️ **What Stage C2 cannot see.** If the A+ screw does not actually clamp the wire, both tests can pass on a wire that was never measured. Tug-test every wire before each run (§6.6).

### 8.5 Stage D: two-way link and probe

This is milestone M3. **Only start Stage D with a clear result from Stage C and two `below-3V3` results from Stage C2.**

1. Pull the RJ45 out of COM and unplug the USB.
2. The **inverter transmit** wire (from Stage C) is on **A+**, and the pin 8 + P5 pair is back in **B−** with the 3V3 jumper (T3) removed (§8.4b step 8).
3. Make the **P6** link: jumper wrapped on **GPIO17 (J1-10)**, at the tip of the pin (§6.3), and spliced to the **other** candidate wire, the inverter **receive** wire (§6.4).
4. Check against the table. **GPIO17 must not go to the wire that tested LOW (N5)**, and it may only go to the wire that passed Stage C2.

   | Wire | Goes to |
   |---|---|
   | Inverter transmit (tested LOW) | Module **A+** only |
   | Inverter receive (tested HIGH, then `below-3V3` twice) | **GPIO17** only |
   | Pin 8 | Module **B−**, together with the P5 jumper to ESP32 G |

5. Inspect, photograph, tick the build record.
6. Power the ESP32, then plug the RJ45 into COM. Watch the LCD for 10 s.
7. **Arm the link.** Until it is armed, the firmware never drives GPIO17 and sends nothing to the inverter. On the iPhone: **Diagnostics → Step 3 → Arm link (enable TX)** and confirm which RJ45 pin GPIO17 is on (`POST /api/link/arm`; serial `arm 2 YES` if GPIO17 is on pin 2). The firmware refuses to arm in any of these cases:
   - the module self-test (B3) is not stored;
   - no idle test has identified the transmit pin twice;
   - the other pin has not read HIGH, or has ever read like a transmit pin;
   - the upper check (C2) has not passed twice on it, or has ever read `ABOVE-3V3`;
   - you named the pin that tested as transmit.

   Before it enables TX it runs a **pre-arm check** (receive-only, 0.2 s):
   - GPIO17, with only its internal pull-down, must read **LOW**. HIGH means the GPIO17 wrap touches the GPIO16 wrap, or the wire is positive.
   - The module's receiver output must read **LOW**, which means A+ is on the idling transmit wire.

   If either fails, arming is refused with the reason. An armed logger repeats the GPIO17 check at **every boot**. If it fails, the logger stays silent for that boot and sends a `LINK_LOST` alert.
8. **Run the probe:** **Diagnostics → Step 4 → Re-run probe** (`POST /api/probe`; serial `probe`; the probe also runs by itself after arming). The probe only sends read commands from the whitelist. It tries the expected settings first (2400 baud, RX and TX inverted) and then the alternatives automatically ([02](02-protocol-reference.md), [d1](../research/d1-architecture.md) §3.5).
9. **What success looks like:**
   - QPI reply `(PI30` (bytes `28 50 49 33 30 9A 0B 0D`), or `(NAK`, which still means a PI30-family inverter.
   - QPIGS replies pass their CRC check.
   - The live **output voltage** agrees with the LCD within ±2 V and the **battery voltage** within ±0.3 V.
   - QPIRI reports 3200 VA and a 24.0 V battery rating.
   - QVFW / QVFW2 match the LCD's U1 / U2 versions.
   The full acceptance list is in [07](07-milestones-and-test-plan.md) M3.
10. If the probe finds nothing, go to §9.2.

### 8.6 Stage E: dialect

The firmware decides automatically between the stock PI30 variants and Sorotec's "REVO" dialect, and stores the result. Nothing to wire. See [02 — Protocol reference](02-protocol-reference.md).

---

## 9. Fallback branches

These follow [d1](../research/d1-architecture.md) §8.4 B–D and [v1](../research/v1-protocol-verification.md) §6.6. Try them in this order.

### 9.1 F1: neither pin idles negative

The port does not behave like an RS232 transmitter. Possible reasons: a wiring or colour mix-up, the inverter's port was off, or (unlikely) the port is TTL-level, not RS232.

1. Redo the basic checks in §8.4. Re-read the colours through the plug. Repeat Stage C.
2. If the result stays HIGH/HIGH, the architecture record allows one more **receive-only** diagnostic ([d1](../research/d1-architecture.md) §8.4 C): A+ on the candidate, **B− to ESP32 3V3** instead of GND, and pin 8 to ESP32 G by a splice.
   - A 5 V TTL transmit wire idles above 3.3 V, so the output reads HIGH and drops LOW on data. A receive input at 0 V reads LOW.
   - ⚠️ To do this you must first **remove P5** (B− → G). B− must never be on 3V3 and GND together (N9).
3. **If the voltage level cannot be established clearly, stop.** Do not connect any ESP32 pin to a pin of unknown voltage; the ESP32's absolute limit is 3.6 V. Escalate to §9.4 (MAX3232 or a borrowed multimeter) or §9.5 (vendor collector).

### 9.2 F2: transmit pin found, but the probe gets no reply

The firmware has already tried both polarities, 2400 and 9600 baud, the PI18 and legacy Q1/Q6 commands, and (last) read-only Modbus probes. Check the hardware:

1. GPIO17 is on the wire that tested **HIGH** (inverter receive), and the P6 splice is tight.
2. The **P5 jumper (B− → ESP32 G)** is present. Without it, receiving can still work but transmitting has no ground reference.
3. Nothing else is plugged into COM, and the inverter's LCD is not inside a settings menu.
4. Re-run the probe after power-cycling the ESP32.
5. Save the probe report from the Diagnostics page and see [08](08-risks-troubleshooting-maintenance.md).

### 9.3 F4: COM behaves like RS485 (contradicts the manual; not supported by firmware 0.6.x)

> ⚠️ **Do not wire F4.** The firmware has **no RS485 mode**: `LinkMode::RS485_ON_COM` exists only as a name and nothing can select it ([05](05-firmware-guide.md)). F4 would also switch on the module's transmitter and connect it to pins 1 and 2. That is only safe if those pins really are an RS485 pair, which a meter-free test cannot prove.

If F1 and F2 have failed and neither pin idled negative in Stage C, stop here. Record the Stage C numbers and the probe report, and go to §9.4 (MAX3232 or a borrowed multimeter) or §9.5. The original plan ([d1](../research/d1-architecture.md) §8.4 D) is kept there for a future firmware.

### 9.4 Upgrade: a MAX3232 module (only if one ever becomes available)

A "mini RS232 to TTL MAX3232" module (about ৳80) replaces the HW-0519 and gives a standards-compliant link ([d1](../research/d1-architecture.md) fallback F-A).

| From | To |
|---|---|
| Inverter transmit pin (from Stage C) | MAX3232 RS232-side receive input (R1IN; the printed label varies by module) |
| Inverter receive pin | MAX3232 RS232 transmit output (T1OUT) |
| Pin 8 | MAX3232 GND |
| MAX3232 VCC | ESP32 **3V3** |
| MAX3232 TTL output | ESP32 GPIO18 |
| MAX3232 TTL input | ESP32 GPIO17 |

Set the link mode to MAX3232 (no inversion) in the firmware: remove the HW-0519 first, run `reset-link all`, then `arm-max3232 YES`. The firmware refuses MAX3232 mode while an HW-0519 self-ID is stored, because this path has no idle-test gate. It also runs the GPIO17 pre-arm check, which refuses if GPIO17 sits on the MAX3232's (idle-HIGH) R-OUT instead of T-IN. The TTL-side labels on these modules are inconsistent, so first run a loopback: bridge the module's RS232 TX and RX pins together; one orientation of the TTL wires will echo ([r5](../research/r5-hardware.md) §3.3).

### 9.5 F-B: the vendor's WiFi collector

If no DIY link works, the manual's optional WiFi collector and the "SOLAR OF THINGS" app are the vendor's route. Ask IPS Bazar (01749 426 789). Only one device may talk on COM, so the ESP32 must then be removed.

---

## 10. Power

### 10.1 Use an inverter-backed socket

1. **Find the AC-OUT sockets** (site-survey §13). The simplest test is a natural grid outage: note which sockets stay live. Otherwise:
   1. Tell the household, and save work on any computer.
   2. Plug a phone charger with a phone into the candidate socket and confirm it charges.
   3. Switch off the breaker that feeds the **inverter's AC input** (the grid side). The inverter switches to battery (AC/INV LED flashing).
   4. If the phone still charges, that socket is on **AC OUT**.
   5. Switch the breaker back on.
2. **Plug the 5 V charger into an AC-OUT socket.**
3. **Put the router/ONU on AC OUT as well.** Otherwise logging continues during outages but nothing reaches the cloud until the grid returns (the firmware back-fills later).

### 10.2 The US plug and the charger

- Bangladeshi "universal" sockets accept US flat-pin plugs ([WorldStandards](https://www.worldstandards.eu/electricity/plug-voltage-by-country/bangladesh/)), but the grip can be loose.
- The plug must sit **fully inserted and firm, with no wobble**. A loose plug can heat up or arc. If a socket does not grip, use another socket. A universal travel adapter is an optional nice-to-have (§13).
- Check the charger's label says **100–240 V~ 50/60 Hz**. Use it only if its case is undamaged, it does not buzz, and it stays no more than warm. The charger's isolation is the only barrier between the AC-OUT mains and the logger, which pin 8 ties to the inverter's signal ground. A failed cheap charger is a hazard to you and to the inverter.
- Use the supplied USB cable into the ESP32's **"COM"** USB-C port.
- **Power budget:** the logger uses about 0.3–0.75 W on average. WiFi bursts need up to 355 mA, and the module datasheet asks for a supply that can deliver at least 0.5 A ([WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)). The 2 A charger has a large margin.

### 10.3 What not to do

- Never power from COM pin 4 or the battery (N6).
- **Laptop rule (N7):** whenever the RJ45 is in COM and the Mac is connected to the ESP32, the Mac runs **on battery**, with nothing else plugged into it (no charger, no external display).
- One power source at a time (N13).

---

## 11. Enclosure and installation

### 11.1 Layout

The box is 100 × 68 × 50 mm outside, about 92 × 60 × 45 mm inside (measure yours; the corner screw posts take some space). The ESP32 board is 62.7 × 25.4 mm ([r5](../research/r5-hardware.md) §6.1).

```
              wall B (TOP when mounted): antenna end, keep >= 15 mm clear of anything
 +-----------------------------------------------------------------+
 |                         ~24 mm free  ^                           |
 |   +-------------------------------+  |      +-----------------+  |
 |   | ESP32-S3 DevKitC-1  62.7x25.4 |         | HW-0519  ~44x15 |  |
 |   | [module + antenna end ^ up]   |         | header -> ESP   |  |
 |   |                               |         | screw terminals |  |
 |   | foam stack under the middle   |         | facing wall A   |  |
 |   | (pins point down)             |         +-----------------+  |
 |   | USB-C "COM" port v            |          1 layer of foam     |
 |   +-------------------------------+                              |
 |      ~5 mm                         knot in LAN cable  O          |
 +--[hole ~13x9 mm for USB-C plug]-------[notch: LAN cable]--------+
              wall A (BOTTOM when mounted): cable entries, 2-3 mm drain hole
```

### 11.2 Mounting the boards

1. **ESP32 without headers (§6.0):** it sits flat on **one layer** of foam tape under the module area. Keep the wire loops at the board edges clear of the tape. Skip the steps below.
   **ESP32 with soldered headers:** its header pins point down, so it cannot sit flat.
   1. Measure how far the pins stick out below the board, including the black plastic spacer (often about 8–9 mm).
   2. Cut foam tape into strips about 15 mm wide, narrower than the gap between the two pin rows.
   3. Stack layers on the box floor until the stack is about **2 mm taller than the pins**. Count the layers you need; it depends on your tape's thickness.
   4. Press the board onto the stack, under the module area, with the USB-C end towards wall A.
   5. The wrap joints must hang free, not touch the floor.
2. **Module:** one layer of foam tape, screw terminals facing wall A, header facing the ESP32.
3. Keep the USB-C end about 5 mm from wall A and the antenna end towards wall B.

### 11.3 Holes and cable entries

- **USB-C:** a plug inserted from inside does not fit (62.7 mm board + about 20 mm plug + 15 mm antenna gap is more than 92 mm). So:
  1. Build the foam stack first, then measure the height and side position of the "COM" USB-C port.
  2. Mark a **~13 × 9 mm** opening on wall A, in line with the port.
  3. Cut it with the hobby knife in many light passes, or melt it with a heated screwdriver or nail held in pliers.
  4. Insert the USB plug **from outside** into the board.
- **LAN cable:** cut a notch in the **top edge of wall A** (where the lid closes) so the cable lies in it and the lid holds it lightly.
- **Drain hole:** 2–3 mm at the lowest point of wall A, so condensation can escape.

> ⚠️ Melting ABS gives off fumes. Work by an open window or outdoors and do not breathe them. Hold the hot tool with pliers, keep children away, and let the box cool before handling.

### 11.4 Strain relief

1. Tie a **loose overhand knot** in the LAN cable just inside the notch. A pull on the cable is then taken by the knot against the wall, not by the screw terminals or wraps.
2. Tape the cable to the box floor with a strip of foam tape.
3. Outside, run the cable from the COM jack to the box without tension. The RJ45 plug must never hang by its cable, because the jack is soldered to the inverter's control board. Tape the cable to the wall near the inverter if needed, **not** to the PV or battery cables.
4. Tape the USB cable to the wall below the box so its weight does not lever the USB-C plug.

### 11.5 Placement

| Rule | Why |
|---|---|
| On the wall **beside** the inverter, at least **15 cm** from its steel case | Metal blocks and detunes the WiFi antenna |
| Antenna end **up**, nothing metal within 15 mm of it | Espressif recommends at least 15 mm clearance around the PCB antenna ([Espressif hardware design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html)) |
| **Not** above the inverter and **not** in line with its bottom fans | Hot air. The N16R8 module is rated to **65 °C ambient** only ([WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)) |
| Out of direct sun | The clear lid makes a greenhouse |
| Cable entries **pointing down** | Water and dust cannot track in |
| Within reach of the RJ45 cable (1–1.5 m) and of the AC-OUT socket | No extensions needed |
| Where you can open the lid | The BOOT button (WiFi re-setup) is inside |

**Mounting the box:** clean and dry the wall (the photos show tiles, which hold foam tape well). Put two or three strips of foam tape on the back of the box and press it on for 30 s.

**After installing:** check the WiFi signal (RSSI) on the local web page's status. Better than −67 dBm is good, down to −75 dBm is acceptable, below −75 dBm means move the box or the router.

---

## 12. Final pre-power checklist

Before the first power-up in each stage, tick every line (copy it into the build record):

- [ ] The RJ45 is **out** of COM and the USB is **unplugged** while you check.
- [ ] Only LAN pins **1, 2 and 8** are stripped. Pins 3–7 are cut, folded back and covered.
- [ ] Module VCC goes to **3V3**, not 5V.
- [ ] The module's earth terminal is **empty** and the R0 pad is **open**.
- [ ] Stage B jumpers **T1 and T2 are removed** (from Stage C onwards).
- [ ] Stages C and D: B− holds **pin 8 and the P5 jumper to G**, nothing else.
- [ ] Stage C2 only: pin 8 + P5 are **out** of B− (still joined, still on G, bare pin taped); B− holds **only** the T3 jumper to 3V3.
- [ ] Stage D: T3 is **removed** and the pin 8 + P5 pair is back in B−.
- [ ] A+ holds **only the inverter transmit wire** (or, in Stage C, the candidate under test).
- [ ] GPIO17 is unconnected (Stages B–C2) or goes **only to the wire that tested HIGH and `below-3V3`** (Stage D).
- [ ] The candidate wires are pins 1 and 2 by colour **and** by twist (one pair); no solid blue wire is stripped.
- [ ] No wrap touches a neighbouring pin; tug test done.
- [ ] Photo taken from above and from the side.
- [ ] Only one power source; the Mac, if used, is **on battery**.
- [ ] The BMS cable has not been touched.

---

## 13. Optional nice-to-haves (never required)

Everything above works with what you own. If a shop visit ever happens, these make the build cleaner or remove uncertainty ([d1](../research/d1-architecture.md) §8.8, prices [SECONDARY] from a local shop listings on 2026-10-21):

| Priority | Item | Approx. price | What it improves |
|---|---|---|---|
| 1 | Mini MAX3232 RS232↔TTL module | ৳80 | A standards-compliant RS232 link (§9.4) |
| 2 | Resistors: 1 kΩ, 4.7 kΩ and 10 kΩ | a few taka | 1 kΩ in series with GPIO17 → inverter receive; up to 4.7 kΩ in series with the A+ lead to limit stress on the module. Larger values in the A lead can stop the receiver working ([v2](../research/v2-platform-verification.md) §3.2). 10 kΩ from the module's DI (header "TXD") to 3V3 holds its transmitter off during the ESP32's boot (appendix A). |
| 3 | Female-to-female jumpers | ৳75 / 40 pcs | Replace wrap joints on header pins |
| 4 | PVC electrical tape or heat-shrink, cable ties | – | Neater insulation and strain relief |
| 5 | Soldering iron and solder (borrowed) | – | Permanent joints (§6.7) |
| 6 | Digital multimeter (e.g. DT830D) | ৳320 | Removes all pin and voltage ambiguity |
| 7 | Universal travel adapter | ৳319 | Firm grip for the US plug, if no socket grips it well |
| 8 | Silica-gel sachet (often free in shoe or electronics packaging) | – | Less condensation in the box |

---

## 14. Sources

Project research (deep detail):

- [`research/d1-architecture.md`](../research/d1-architecture.md) §8 (hardware plan), §3.2 (link profiles), §5.10–5.11 (local UI and LED)
- [`research/v1-protocol-verification.md`](../research/v1-protocol-verification.md) §0, §5, §6 (port facts, unknowns, discovery procedure)
- [`research/v2-platform-verification.md`](../research/v2-platform-verification.md) §2.3, §2.4, §3.2, §3.13, §3.14 (board, module, RS232-lite risks, power, heat)
- [`research/r5-hardware.md`](../research/r5-hardware.md) §1–§8 (board, module, enclosure, safety)
- [`research/manual-extract.md`](../research/manual-extract.md) and the manual scan §2.3 (COM/BMS pinout), §11 (specifications)

External:

- Espressif ESP32-S3-DevKitC-1 v1.1 user guide (header tables, USB ports, power options): https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
- ESP32-S3-WROOM-1 datasheet (65 °C rating, 355 mA TX peak, 0.5 A supply, PSRAM pins): https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- Espressif hardware design guidelines, PCB layout (antenna clearance): https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html
- TI MAX3232 datasheet (receiver thresholds and input resistance): https://www.ti.com/lit/ds/symlink/max3232.pdf
- ProtoSupplies, similar auto-direction RS485 module (TVS, poly fuses, R0, header convention) [SECONDARY]: https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/
- RoboticsBD HW-0519 listing (terminal markings, R0 termination) [SECONDARY]: https://store.roboticsbd.com/electronics-module/1816-ttl-to-rs485-converter-33v5v-hardware-automatic-control-converter-module-robotics-bangladesh.html
- Arduino forum, HW-0519 earth terminal [SECONDARY]: https://forum.arduino.cc/t/difference-between-the-max485-and-the-hw-0519-modules/704175
- Arduino forum, clone 5V pin warning [SECONDARY]: https://forum.arduino.cc/t/chinese-esp32-s3-5v-pin-warning/1192758
- Wikipedia, RS-232 and RS-485 signal levels [SECONDARY]: https://en.wikipedia.org/wiki/RS-232 , https://en.wikipedia.org/wiki/RS-485
- Wikipedia, TIA-568 colour codes [SECONDARY]: https://en.wikipedia.org/wiki/ANSI/TIA-568
- WorldStandards, Bangladesh sockets [SECONDARY]: https://www.worldstandards.eu/electricity/plug-voltage-by-country/bangladesh/

---

## Appendix A — Safety review (2026-10-21)

An adversarial review of this document, [07](07-milestones-and-test-plan.md), the site survey and every firmware path that can reach the inverter's UART: `src/link/*`, `src/proto/*`, `include/config.h`, `src/app/*`, `src/web/LocalWeb.*`, the serial console, `main.cpp`, and the FW-2 cloud and alert code. The starting assumption was that the docs and code were wrong until shown otherwise. It protects five things: **you, the inverter, the lithium battery, the ESP32 and the module**. Firmware **0.6.1** contains the fixes. Nothing here has run on the real hardware yet.

### A.1 What was checked, and the verdict

| # | Requirement | Verdict after the fixes | Evidence |
|---|---|---|---|
| 1 | No ESP32 GPIO touches an RJ45 pin before that pin is proven safe; GPIO17 high-Z until armed; self-test and idle tests never transmit; a fresh, erased or rebooted unarmed logger never transmits; arming refuses the transmit pin | **Met.** Before the review, "proven" only meant "not negative": the new upper check (C2) and pre-arm check close that gap (issue S1). An unarmed engine only listens (`RawCaptureAdapter`); every send passes `armed` → exact whitelist → re-parse of the built frame (`SerialLink::query`). `ArduinoUart::begin(p, false)` never attaches the TX pin. Arduino-ESP32 3.3.12 re-uses a stored TX pin when `begin()` gets −1, but `uartEnd()` resets it to −1, so a disarm really detaches GPIO17 (checked in `HardwareSerial.cpp`, `esp32-hal-uart.c` of the pinned framework). | unit tests `test_seriallink_not_armed_sends_nothing`, `test_unarmed_engine_never_transmits`, `test_pin_diag_*` |
| 2 | Pin 4 and the BMS jack never touched; unused conductors insulated separately | **Met (procedural).** N2/N3, §7 step 7 (staggered cuts, folded back, taped). The new twist cross-check in §7 step 5 (pins 1–2 are one pair, pin 4 is solid blue) and the upper check catch a colour mix-up that picks up pin 4. | §7, §8.4b |
| 3 | RS232-lite electrical reasoning | **Sound, with stated residual risks** (A.3). Receive: A+ on a −5…−12 V line is at or beyond a MAX3485-class rating (−7.5 V); the TVS diodes and the inverter's current-limited driver make it survivable. Transmit: 0 / 3.3 V meets the guaranteed thresholds of MAX3232-class receivers (below 0.6 V = mark, above 2.4 V = space) and loads GPIO17 with about 0.5–1.1 mA. The 120 Ω R0 pad stays open (else 120 Ω would load the inverter's driver). B− on pin 8 gives the common ground and the 0 V reference. RX and TX inversion are done in the UART. The module runs at 3.3 V, so RO can never exceed the ESP32's 3.6 V limit. | [TI MAX3232](https://www.ti.com/lit/ds/symlink/max3232.pdf); [v2](../research/v2-platform-verification.md) §3.2, H2 |
| 4 | Self-test trick (A+ → GND, B− → 3V3, header GPIOs as INPUT_PULLUP) cannot short 3V3 or create contention; T1/T2 removed before P5; GPIO16/17/18 adjacency | **Met.** A+ and B− are bus inputs (bias resistors about 4.7 kΩ on the similar ProtoSupplies board, fuses, TVS), not rails. If the module's driver switches on, it drives A LOW and B HIGH, the same polarity as T1/T2, so nothing fights. A 3V3 short is only possible through N9 (B− on 3V3 and GND at once). The checklist and §8.4b cover that, and a shorted board simply fails to boot. Adjacency: GPIO16–18 is caught by the self-test ("both LOW"), GPIO16–17 by the new pre-arm and boot check, GPIO17–18 only by inspection (A.3). | `test_pin_diag_self_id`, `test_pin_diag_prearm_check` |
| 5 | Whitelist read-only; `INV_ALLOW_WRITES=0` compiles out setters; refusal test; cloud cannot cause a write | **Met.** Every whitelisted command is a documented query ([02](02-protocol-reference.md) §6, §11). `PQSE`/`PQSA` are Sorotec REVO **reads**, verified against the REVO doc and mpp-solar `pi30revo.py`, and matched exactly, so `PSET` is refused ([v1](../research/v1-protocol-verification.md) V10, C14). Date queries are digit-checked with ranges. Modbus is FC 03 only, 8 bytes, valid CRC. `INV_ALLOW_WRITES≠0` is a compile `#error`; no setter string exists in `src/`. Refusal sends **0 bytes** (`test_seriallink_refuses_with_zero_bytes`). The cloud can only change thresholds and rules (alert logic) and bump `cfg.reprobe`, which queues a probe that runs only on an armed link and only sends whitelisted reads. The RTDB rules let only the owner write `cfg`. Six unused queries were removed, so the whitelist is now exactly the set the firmware sends (issue S7; a unit test checks the scheduler's command table against it). | `Whitelist.cpp`, `test_frames` |
| 6 | Ground loop, power source, heat, mains/PV | **Met (procedural), two additions.** N6, N7 and N13 stand. Added N16 (the AC/battery/PV terminal blocks below the COM jack are **uncovered** in the photo) and N17 (do not touch the electronics while the RJ45 is plugged in). Added a charger-condition rule in §10.2. Heat: the N16R8 module is rated to 65 °C ambient; §11.5 placement stands. | §1, §10, §11 |
| 7 | Garbage or a foreign master → listen-only | **Met for probes.** Every probe profile listens 3 s first. A `(…\r` frame or a valid Modbus frame → `RAW` (listen-only) with a re-probe after 6 h. Plain noise only continues the read-only probe. In `RUN` a second master is not looked for (A.3, R14). Only one plug fits COM. | `test_probe_stops_on_foreign_master` |

### A.2 Issues found and fixed

| ID | Severity | Where | Finding | Fix |
|---|---|---|---|---|
| S1 | **High** | 03 §8.4–8.5, `PinDiag`, 07 M2–M3 | The idle test proves the transmit wire idles negative, but the "other" wire only reads "not negative". A wire at a **positive** voltage reads the same. That includes pin 4 "+VCC" picked up through a colour mix-up, which with GPIO17 on it would destroy the ESP32. | New receive-only **Stage C2 upper check** (B− on 3V3, `idle N 3v3`, `?ref=3v3`). Arming needs `below-3V3` twice on the receive pin. An `ABOVE-3V3` result, or a transmit verdict ever seen on that pin, blocks arming until `reset-link` (sticky). Plus the twist cross-check (§7 step 5). |
| S2 | **High** | `PinDiag`, `Tasks.cpp` (ARM), boot | Nothing checked the GPIO17 wire itself before TX was attached. A GPIO16–17 wrap short makes two ESP32 outputs fight for as long as the logger is armed (DI held HIGH, TX idling LOW), including across reboots. | **Pre-arm check** (receive-only, 200 ms): GPIO17 with its pull-down must read LOW, and RO must read LOW (A+ on the idling transmit wire). It runs on `arm`, and the GPIO17 part runs again at **every armed boot**: on failure TX stays detached for that boot and a `LINK_LOST` alert (sev 4) is raised. |
| S3 | **Medium** | `Tasks.cpp`, `LocalWeb.cpp` | `arm-max3232 YES` / `POST /api/link/arm?mode=max3232` armed GPIO17 with **no** idle-test gate, so it bypassed requirement 1 with the HW-0519 wiring in place. | Refused while an HW-0519 self-ID is stored (`reset-link all` after swapping the hardware); runs the GPIO17 pre-arm check, which also catches GPIO17 on the MAX3232's idle-HIGH R-OUT. |
| S4 | **Medium** | `main.cpp`, `ArduinoHal.cpp` | The module's DI pin was first driven when `invTask` started. That came after a 300 ms delay, the LittleFS mount (seconds on a first-boot format) and the Wi-Fi start. If DI floats LOW, the auto-direction transmitter switches on and pushes B− (tied to ground here) HIGH into that short. `pinMode(OUTPUT)` before `digitalWrite(HIGH)` also emitted a short LOW pulse. | Both header pins INPUT_PULLUP as the **first** lines of `setup()`, then DI driven HIGH as soon as NVS is read. New `driveHighNoGlitch()` sets the latch before enabling the output (semantics checked in `esp32-hal-gpio.c`). |
| S5 | **Medium** | `Tasks.cpp` (ARM) | Arming did not need the module self-test. DI might then only be weakly pulled up, and the idle tests might have sampled the wrong header pin. | Arm refused until the self-ID is stored. |
| S6 | Low | `Tasks.cpp` (LOOPBACK) | The bench loopback could run after the module or the RJ45 was wired. A GPIO17–GPIO18 jumper then makes GPIO17 fight the module's RO output, and QPI could be sent into a spliced RX wire before arming. | Refused once a self-ID or any idle result is stored (B1 comes before B2). |
| S7 | Low | `Whitelist.cpp` | `QMCHGCR`, `QMUCHGCR`, `QBEQI`, `QLT`, `QEMyyyymm` and `QEYyyyy` were whitelisted but never sent. `QMCHGCR` and `QMUCHGCR` are one dropped leading byte from the `MCHGC…`/`MUCHGC…` charging-current **setters**. `QBEQI` is one bit (`Q`→`P`) from the `PBEQ…` equalisation setters, and `QEY…`/`QEM…` one bit from `PE` + flag letters. | Removed (least privilege). The whitelist now equals the commands the firmware sends; the refusal test covers the removed ones. |
| S8 | Low | `Settings.cpp` | A corrupted NVS value for the RO/DI pin numbers was accepted unless it was 17. The firmware could then drive an arbitrary GPIO (strapping, flash, USB) HIGH. | Only {16, 18}, distinct, are accepted; anything else resets to the defaults with self-ID cleared. |
| D1 | **High** (doc) | 03 §9.3 | F4 told you to "select the RS485 mode", which does not exist. F4 also turns the module's transmitter onto pins 1 and 2, whose nature is the open question at that point. | Marked unsupported: stop and escalate (§9.4/§9.5). |
| D2 | Medium (doc) | 03 §1, §4, §10.2 | Uncovered power terminals next to COM, the earth potential of pin 8, the "ESP32 is not exposed" claim (overstated), the driver-on failure mode and the charger's isolation role were not stated. | N16, N17, rewritten §4 risk bullets, charger rule. |
| D3 | Low (doc) | 03 §8.3, §6.2, §6.3, §12, §13; 07; site survey | Loopback order, the pin-8 + P5 "pair" method, which shorts the firmware detects, checklist lines for C2/D, and a 10 kΩ DI pull-up as an optional part were missing. | Added. |

Firmware tests: `pio test -e native` covers the new gates in `test_pin_diag_upper_check_blocks_positive_pin` and `test_pin_diag_prearm_check`, and the extended whitelist refusal list.

### A.3 Residual risks (what the fixes cannot remove)

| # | Risk | Likelihood | Worst case | What limits it |
|---|---|---|---|---|
| R1 | The module's A input is stressed beyond a MAX3485-class rating by a −10…−12 V idle. If the unmarked transceiver fails **internally**, the negative voltage could reach GPIO18 through RO. | Low | Dead module; possibly GPIO18 or the ESP32 | TVS diodes and fuses on the module [INFERRED from the similar board], current-limited RS232 driver. Removed fully only by an A-lead series resistor (≤ 4.7 kΩ) or a MAX3232 (§13). |
| R2 | During the ESP32's ROM/bootloader start (before `setup()`, a fraction of a second each boot) GPIO16 is not driven. If this HW-0519 has **no on-board pull-up on TXD/DI** [UNKNOWN], its transmitter can switch on and drive B− (grounded here) HIGH and A+ against the inverter's transmit line. | Unknown (most auto-direction boards hold TXD high, unverified for this one) | Driver short-circuit current for under a second per boot; a warm module | RS485 drivers are current-limited with thermal shutdown [INFERRED, chip unmarked]; the network watchdog limits reboots to one per 6 h. A 10 kΩ DI → 3V3 resistor removes it (§13). |
| R3 | The upper check measures under the module's pull-up. A wire that is **not clamped** in A+ can pass both Stage C and C2, because the reading is undefined at 0 V difference. | Very low (needs a colour mix-up **and** a loose terminal) | GPIO17 on an unverified wire | Twist cross-check, tug test before every run, two runs each, sticky failures. |
| R4 | The inverter's RS232 receiver is unknown. If it is opto-isolated or has unusual thresholds, 3.3 V may not register. | Low | No link (no damage) | Fallback F2/§9.4; GPIO17 current stays ≤ about 1.1 mA. |
| R5 | Line errors at 2400 baud: whether this inverter checks the CRC of incoming commands is not documented (PI17 devices take queries without CRC, [r2](../research/r2-voltronic-pi30.md) §2). A corrupted read could in theory become another command. | Very low (needs a specific bit error **and** an inverter that ignores a wrong CRC) | An unintended setting change | Whitelist narrowed to the sent set (S7). The closest remaining near-misses (`QET`→`PET`, `QDI`→`PDI`, `QED…`→`PED…`) would be `PE`/`PD` flag commands with invalid upper-case flag letters [INFERRED: rejected]. 1.5 m cable at 2400 baud; CRC error rate checked in M3 (≥ 99 % valid). |
| R6 | A GPIO16–GPIO18 or GPIO17–GPIO18 short that appears **after** Stage B (rewiring, mounting) is not detected while running. GPIO16–18 shows up as a failed idle test or pre-arm check. GPIO17–18 only shows as a failing probe. | Low | Continuous contention between two outputs until fixed | Staggered wraps (§6.3), inspection and photos (§6.6), pre-power checklist. |
| R7 | The arm state is stored. If you rewire later **without** `reset-link`, the logger re-arms at boot. The boot check only catches a HIGH GPIO17. | Low | GPIO17 driven onto a wire that was not re-tested | §8.2 rule 1 and [05](05-firmware-guide.md): disarm (`reset-link`) before any rewiring; Stage D → C needs the tests again. |
| R8 | Pin 8 ties the logger to the inverter's signal ground, whose earth potential is undocumented. The cheap USB charger is the only isolation from mains. | Low | Shock risk if touched during a fault; damage via a ground loop or a failed charger | N7, N13, N17, charger rule §10.2, Mac on battery. |
| R9 | Uncovered AC/battery/PV terminal blocks next to COM. | Low with N16 | Electric shock, arc | N1, N16; one-hand plugging; cable routed away. |
| R10 | Heat: sealed clear box, module rated 65 °C ambient, about 0.5 W inside. | Low | Brownouts, resets, PSRAM errors in a hot season | §11.5 placement; the drain hole; a reboot alert (`LOGGER_REBOOT`) shows trouble. |
| R11 | The owner's cloud `cfg.reprobe` (or anyone with the local admin password, over plain HTTP on the LAN) can trigger re-probes or arm. | Low | Probing traffic (reads only); a constant "space" level on the inverter's RX for a few seconds during the TX-non-inverted profile | Owner-only RTDB rule; admin password; every arm still passes all gates; reads only. |
| R12 | The fixes were checked in the framework source and by host tests, **not on the board** (GPIO reset states, glitch-free DI drive, pull-down values). | – | A gate behaving differently on hardware | Bench order in [07](07-milestones-and-test-plan.md) M1–M3; every gate fails closed (refuses to arm). |
| R13 | A foreign master is detected only during probes. A light probe after link loss transmits without listening first. | Very low (one plug per jack) | Collisions on the inverter's RX line | Physical: COM takes one plug. |

### A.4 Not changed (judged correct)

- **N5, N9, N10, N11, the R0 pad, VCC on 3V3, B− on pin 8, the T1/T2 polarity of the self-test**, and **the Mac on battery while the RJ45 is plugged in (N7)**.
- **TX inversion before the TX pin is attached** (`setTxInvert` before `setPins`), so the first level on the inverter's RX is the idle "mark".
- **Safe mode never transmits.** Self-test and idle tests are refused while armed.
- **Probe profiles with TX not inverted** only put a constant "space" (+3.3 V through the RS232 receiver's 3–7 kΩ) on the inverter's RX for a few seconds, which does not harm it.

### A.5 Sources for this appendix

- Arduino-ESP32 3.3.12 (pinned `pioarduino` 55.03.312-1): `cores/esp32/HardwareSerial.cpp` (`begin()` re-uses the stored pins, lines 340–344), `cores/esp32/esp32-hal-uart.c` (`_uartDetachPins` sets `_txPin = -1`), `cores/esp32/esp32-hal-gpio.c` (`__pinMode` / `__digitalWrite`).
- TI MAX3232 datasheet (receiver thresholds, 3–7 kΩ input): https://www.ti.com/lit/ds/symlink/max3232.pdf
- MAX3485 A/B absolute maximum and the HW-0519/ProtoSupplies module details: [v2](../research/v2-platform-verification.md) §2.4, §3.2 [SECONDARY].
- Wikipedia, TIA-568 (pair layout, pin 4 blue) [SECONDARY]: https://en.wikipedia.org/wiki/ANSI/TIA-568 ; RS-232 and RS-485 levels: https://en.wikipedia.org/wiki/RS-232 , https://en.wikipedia.org/wiki/RS-485
- ESP32-S3-WROOM-1 datasheet (65 °C for N16R8): https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- PQSE/PQSA/PSET framing and meaning: [v1](../research/v1-protocol-verification.md) V10, C14; whitelist rationale: [d1](../research/d1-architecture.md) §3.10.
