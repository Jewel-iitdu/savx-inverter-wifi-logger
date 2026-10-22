# R5: Hardware, wiring, power and enclosure for the Savx F4 3KW WiFi datalogger

Research date: 2026-10-21. Scope: the physical build. This covers the ESP32-S3 board, the RS485 or RS232 level converter, the RJ45 cable, power, the enclosure, safety and the parts still to buy. Protocol and register details belong to the other research tracks (R1 to R4). Here they appear only where they affect the hardware.

Confidence tags:
- **[CONFIRMED-primary]**: read in an official datasheet, vendor document or source file.
- **[SECONDARY]**: from a forum, blog, shop listing or community project README.
- **[INFERRED]**: my own reasoning from the cited facts.

---

## 0. Summary (decisions for the plan writer)

| # | Decision / finding | Confidence |
|---|---|---|
| 1 | The Savx F3 sister models list **"USB / RS232 (Standard), CAN & RS485 (Optional)"**. The F4's main data port is therefore most likely **RS232 on an RJ45 jack (plus USB)**, not RS485. The purchased RS485-TTL module will not talk to an RS232 port. Budget about **BDT 80 to 199 for a MAX3232 RS232-TTL module** and confirm the port type with a multimeter (Section 4.2) before wiring anything. ([F3 3500 listing](https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/), [F3 2500 listing](https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/)) | [SECONDARY] listing, [INFERRED] for F4 |
| 2 | Use **UART1 on GPIO17 (TX) / GPIO18 (RX)**. Espressif's DevKitC-1 header table labels these pins **U1TXD / U1RXD**. Set the pins explicitly in code. The Arduino core's default Serial1 pins on S3 are RX=15 and TX=16, and its default Serial2 pins (19/20) are the USB pins. | [CONFIRMED-primary] |
| 3 | Power the RS485 or RS232 module from the ESP32 **3V3** pin, never from 5V. ESP32-S3 inputs accept at most VDD+0.3 V. On these clones the 5V header pin often does not even carry 5 V when the board is USB-powered (IN-OUT jumper open). | [CONFIRMED-primary] / [SECONDARY] |
| 4 | Flash and read logs through the **"COM" USB-C port** (CH343P bridge to UART0 GPIO43/44) with *USB CDC On Boot = Disabled*. Use the native "USB" port only if you need USB-OTG. | [CONFIRMED-primary] + [SECONDARY] |
| 5 | Power: **USB charger on an inverter-backed AC socket** (the parts you already bought, plus a plug adapter) is the recommended first build. A fused DC-DC buck from the battery (LM2596HV, 5 to 50 V in) is the "always-on" upgrade. Use the comm-port +12 V pin only after you have measured it. | [INFERRED] |
| 6 | The enclosure is big enough. The DevKitC-1 is **62.74 x 25.40 mm** and the box interior is about 92 x 60 x 45 mm. Two snags: a USB-C plug will not pass through a PG7/PG9 gland, and the board's pins point down, so foam tape alone will not hold it. Use a vero-board carrier or a grommet (Section 6). | [CONFIRMED-primary] dims, [INFERRED] fit |
| 7 | The M-M jumpers do not fit male-pin boards. Buy **F-F Dupont jumpers (BDT 75 / 40 pcs)** or, better, build a vero-board carrier with female headers. | [SECONDARY] prices |
| 8 | The ESP32-S3-WROOM-1 **N16R8 (octal PSRAM) is rated only to 65 °C ambient**, not 85 °C. Keep the box out of the inverter's hot-air path and out of direct sun. The clear lid makes a greenhouse. | [CONFIRMED-primary] |
| 9 | The ESP32-S3 is **2.4 GHz only**. On an iPhone hotspot (iPhone 12 and later), turn on *Maximize Compatibility*. | [CONFIRMED-primary] |

---

## 1. ESP32-S3-N16R8 DevKitC-1 clone

### 1.1 What was bought
- Daraz order photo 5.png: "ESP32-S3 WiFi BT Module Development Board ... Color: S3-N16R8 with Cable", BDT 1,096. Same order: "USB 5V 1A/2A Universal Charger USA" BDT 410 and "Waterproof Plastic Junction Box ... 100-68-50mm" BDT 637. From the user's photo. [CONFIRMED-primary, user photo]
- The thumbnail shows two USB-C connectors side by side at the bottom, BOOT and RST buttons beside the module, and an RGB LED. That is the common Chinese clone of Espressif's ESP32-S3-DevKitC-1 (the "YD-ESP32-S3" / VCC-GND Studio style). [INFERRED from photo]
- On these clones one USB-C port is a **CH343P USB-to-serial bridge** and the other is the **ESP32-S3 native USB** port ([deepcoder/YD-ESP32-23 README](https://github.com/deepcoder/YD-ESP32-23)). [SECONDARY] The silkscreen next to the connectors normally reads "COM" (bridge) and "USB" (native). Check your board. [INFERRED]
- The same board is sold locally by a local shop as "ESP32-S3 DevKitC-1 N16R8 – 16MB Flash, 8MB PSRAM, Dual USB-C", BDT 699 ([a local shop]((local shop listing))). [SECONDARY]

### 1.2 Memory / module facts
- ESP32-S3-WROOM-1-**N16R8** = **16 MB Quad-SPI flash + 8 MB Octal-SPI PSRAM**, ambient **-40 to 65 °C** ([ESP32-S3-WROOM-1/1U datasheet v1.8, Table 1-1](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)). [CONFIRMED-primary]
- From the same datasheet: "R8 and R16V series modules operate at –40 ~ 65 °C ambient temperature … if the PSRAM ECC function is enabled, the maximum ambient temperature can be improved to 85 °C, while the usable size of PSRAM will be reduced by 1/16." [CONFIRMED-primary]
- From the same datasheet, note b: "For modules with Octal SPI PSRAM, i.e., modules embedded with ESP32-S3R8 or ESP32-S3R16V, pins IO35, IO36, and IO37 are connected to the Octal SPI PSRAM and are not available for other uses." [CONFIRMED-primary]
- Supply requirement: the external supply must deliver at least **0.5 A** (I_VDD min 0.5 A, Table 6-2). [CONFIRMED-primary]

### 1.3 Pins to avoid and pins to use

Sources: [ESP32-S3 datasheet v2.2 §2.3.5 and §3](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf), [ESP32-S3-DevKitC-1 v1.1 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html), [WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf).

| GPIO | Function / restriction | Use for datalogger? | Tag |
|---|---|---|---|
| 26–32 | SPI0/1 to in-package/off-package flash and PSRAM ("Priority 4 … not recommended for use"). Not broken out on the DevKitC-1. | **Never** | [CONFIRMED-primary] |
| 33, 34 | High data lines of the 8-line SPI. Not on the DevKitC-1 headers. | No | [CONFIRMED-primary] |
| 35, 36, 37 | Octal PSRAM data/DQS on R8 modules. Not available. | **Never** (N16R8) | [CONFIRMED-primary] |
| 0 | Strapping (boot mode, weak pull-up), wired to the BOOT button | No for wiring. It can be read *after boot* as a "hold for setup mode" button. | [CONFIRMED-primary] / [INFERRED] |
| 3 | Strapping (JTAG source, floating at reset) | Avoid | [CONFIRMED-primary] |
| 45 | Strapping (VDD_SPI voltage, weak pull-down). A wrong level at reset can break flash access. | **Never** | [CONFIRMED-primary] |
| 46 | Strapping (boot mode / ROM log, weak pull-down) | **Never** | [CONFIRMED-primary] |
| 19, 20 | USB D- / D+ (USB Serial/JTAG and OTG), wired to the native "USB" connector | Never as GPIO | [CONFIRMED-primary] |
| 43, 44 | UART0 TX/RX, wired to the CH343P bridge on the "COM" port (boot log and flashing) | Never for the inverter | [CONFIRMED-primary] |
| 39–42 | JTAG (MTCK/MTDO/MTDI/MTMS) | Usable, but avoid if you might use JTAG | [CONFIRMED-primary] |
| 48 (v1.0 and clones) / 38 (Espressif v1.1) | Addressable RGB LED. Espressif: "the initial version uses GPIO48, whereas v1.1 uses GPIO38." On the clone the RGB pad may need bridging ([Arduino forum](https://forum.arduino.cc/t/chinese-esp32-s3-5v-pin-warning/1192758)). | Status LED only | [CONFIRMED-primary] / [SECONDARY] |
| **17** | **U1TXD** (Espressif header label J1-10) | **UART1 TX to the converter** | [CONFIRMED-primary] |
| **18** | **U1RXD** (J1-11) | **UART1 RX from the converter** | [CONFIRMED-primary] |
| 1, 2, 4–14, 21, 47 | General purpose | Spare (e.g., DS18B20 box-temperature sensor or an external LED) | [INFERRED] |

Arduino-ESP32 core defaults (from [cores/esp32/HardwareSerial.h](https://raw.githubusercontent.com/espressif/arduino-esp32/master/cores/esp32/HardwareSerial.h)) [CONFIRMED-primary]:
- For S3: `SOC_RX0 44`, `SOC_TX0 43`, `RX1 15`, `TX1 16`, `RX2 19`, `TX2 20`.
- **Serial2's defaults are the USB pins**, so never call `Serial2.begin()` without pin arguments.
- Recommended call: `Serial1.begin(<baud>, SERIAL_8N1, /*RX=*/18, /*TX=*/17);`

Electrical limits [CONFIRMED-primary] (ESP32-S3 datasheet Table 5-1 and DC characteristics):
- Supply abs. max 3.6 V.
- V_IH max = VDD + 0.3 V. **No pin is 5 V tolerant.**

### 1.4 Which USB-C port for flashing and logs
- **COM (CH343P bridge → UART0 GPIO43/44): use this for development.**
  - Esptool drives DTR/RTS to auto-enter the bootloader, even when your firmware has crashed.
  - Logs appear on `Serial` when *USB CDC On Boot = Disabled*. The core maps `Serial` to `Serial0` when `ARDUINO_USB_CDC_ON_BOOT` is 0 ([HardwareSerial.h lines 438–452](https://raw.githubusercontent.com/espressif/arduino-esp32/master/cores/esp32/HardwareSerial.h)). [CONFIRMED-primary]
  - On macOS the CH34x family is usually handled by Apple's DriverKit extension (AppleUSBCHCOM). If no `/dev/cu.*` port appears, install WCH's driver, but never both ([PlatformIO forum](https://community.platformio.org/t/fails-to-upload-to-esp32s3-wroom-1/38152/29)). [SECONDARY]
- **USB (native, GPIO19/20)**:
  - Espressif describes it as "full-speed USB OTG interface, compliant with the USB 1.1 specification", usable for power, flashing, communication and JTAG ([DevKitC-1 v1.1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)). [CONFIRMED-primary]
  - To log here, set *USB Mode = Hardware CDC and JTAG* and *USB CDC On Boot = Enabled*. `Serial` then becomes `HWCDCSerial`. [CONFIRMED-primary]
  - Also needed for the optional USB-host route (Section 3.6).
- **Power:** either port powers the board. The DevKitC-1 also accepts 5 V on the **5V and G pins** or 3.3 V on the **3V3 and G pins**. Espressif calls these "three mutually exclusive ways to provide power" ([DevKitC-1 v1.1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)). [CONFIRMED-primary]
- **Clone solder jumpers.** These all ship open ([deepcoder/YD-ESP32-23](https://github.com/deepcoder/YD-ESP32-23), [Arduino forum](https://forum.arduino.cc/t/chinese-esp32-s3-5v-pin-warning/1192758)) [SECONDARY]:
  - *IN-OUT*: when open, USB VBUS reaches the board through a diode and does **not** appear on the 5Vin pin. Forum users measured 0.65–2.8 V there.
  - *USB-OTG*: joins the VBUS of both USB-C ports.
  - *RGB*: connects the LED to GPIO48.
  - Consequences:
    - Do not use the 5V header pin to power the converter module. Use 3V3. [INFERRED]
    - Feeding 5 V *into* the 5V pin works, because 5Vin feeds the regulator. [SECONDARY]
    - With IN-OUT open, a USB cable plugged in at the same time cannot back-feed into your 5 V supply. [SECONDARY]

### 1.5 Toolchain configuration (N16R8)

**Arduino-ESP32 core.**
- Latest stable is **3.3.12** (2026-09-18). **4.0.0-RC1** (2026-09-23) is a pre-release, so stay on 3.3.x ([GitHub releases API](https://api.github.com/repos/espressif/arduino-esp32/releases)). [CONFIRMED-primary]
- Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json` ([installing.rst](https://raw.githubusercontent.com/espressif/arduino-esp32/master/docs/en/installing.rst)). [CONFIRMED-primary]

Arduino IDE menu ("ESP32S3 Dev Module"). Menu keys come from [boards.txt](https://raw.githubusercontent.com/espressif/arduino-esp32/master/boards.txt). [CONFIRMED-primary]

| Menu | Value | boards.txt key |
|---|---|---|
| Board | ESP32S3 Dev Module | `esp32s3` |
| Flash Mode | QIO 80MHz | `FlashMode=qio` |
| Flash Size | 16MB (128Mb) | `FlashSize=16M` |
| PSRAM | **OPI PSRAM** (QSPI will fail on R8) | `PSRAM=opi` |
| Partition Scheme | "16M Flash (3MB APP/9.9MB FATFS)". This keeps OTA (2 x 3 MB) plus a 9.9 MB FAT store for offline buffering. | `PartitionScheme=app3M_fat9M_16MB` |
| USB CDC On Boot | Disabled (logs on the COM port) / Enabled (logs on native USB) | `CDCOnBoot=default` / `cdc` |
| USB Mode | Hardware CDC and JTAG | `USBMode=hwcdc` |
| Upload Mode | UART0 / Hardware CDC | `UploadMode=default` |

arduino-cli FQBN [INFERRED from the keys above]:
`esp32:esp32:esp32s3:FlashMode=qio,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default,USBMode=hwcdc,UploadMode=default`

Partition tables (from [arduino-esp32/tools/partitions](https://github.com/espressif/arduino-esp32/tree/master/tools/partitions)) [CONFIRMED-primary]:
- `app3M_fat9M_16MB.csv`: nvs 0x9000/0x5000, otadata, app0 0x10000/0x300000, app1 0x310000/0x300000, ffat 0x610000/0x9E0000, coredump 0xFF0000/0x10000.
- `default_16MB.csv`: app0/app1 0x640000 each, spiffs 0xc90000/0x360000, coredump.
- Arduino IDE does **not** list default_16MB for the S3 Dev Module. Use it in PlatformIO or through "Custom".

**PlatformIO.**
- The official PlatformIO espressif32 platform ships only `esp32-s3-devkitc-1.json`, which is the N8 variant: "8 MB QD, No PSRAM" ([platform-espressif32/boards](https://raw.githubusercontent.com/platformio/platform-espressif32/develop/boards/esp32-s3-devkitc-1.json)). [CONFIRMED-primary]
- For Arduino core 3.x use the community **pioarduino** platform. Its README says stable currently means "espressif Arduino 3.3.12 and IDF v5.5.5" ([pioarduino README](https://github.com/pioarduino/platform-espressif32)). [CONFIRMED-primary]
- pioarduino has a ready **`esp32-s3-devkitc1-n16r8`** board with `"memory_type": "qio_opi"`, `"psram_type": "opi"`, `-DBOARD_HAS_PSRAM`, 16MB, and default partitions `default_16MB.csv` ([board JSON](https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/boards/esp32-s3-devkitc1-n16r8.json)). [CONFIRMED-primary]

```ini
; platformio.ini
[env:datalogger]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip
board = esp32-s3-devkitc1-n16r8
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.partitions = app3M_fat9M_16MB.csv   ; or keep the board default default_16MB.csv
board_build.filesystem = fatfs                   ; match the ffat partition (use littlefs with default_16MB)
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=0                  ; logs on the "COM" (CH343P) port
    ; -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1   ; use these instead for logs on native "USB"
```
- Release asset `55.03.312-1/platform-espressif32.zip` exists ([release API](https://api.github.com/repos/pioarduino/platform-espressif32/releases/tags/55.03.312-1)). [CONFIRMED-primary]
- The builder composes memory type as `<flash_mode>_<psram_type>`, e.g. `qio_opi` ([builder/main.py `_get_board_memory_type`](https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/builder/main.py)). [CONFIRMED-primary]
- If you use the generic board instead: `board = esp32-s3-devkitc-1` + `board_build.arduino.memory_type = qio_opi` + `board_build.flash_mode = qio` + `board_build.psram_type = opi` + `board_upload.flash_size = 16MB` + `board_upload.maximum_size = 16777216` + `build_flags = -DBOARD_HAS_PSRAM`. [INFERRED from the two JSONs]

### 1.6 WiFi: 2.4 GHz only
- ESP32-S3: "2.4 GHz Wi-Fi (IEEE 802.11b/g/n) and Bluetooth 5 (LE)" ([datasheet cover](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)). It cannot join a 5 GHz-only SSID. [CONFIRMED-primary]
- iPhone hotspot: on iPhone 12 or later, enable *Settings > Personal Hotspot > Maximize Compatibility* to use 2.4 GHz ([Apple Platform Support guide](https://support.apple.com/guide/platform-support/sup025284ac0)). [CONFIRMED-primary]
- Dual-band home routers with one SSID are fine. The ESP32 simply associates on 2.4 GHz. [INFERRED]

### 1.7 Current draw (sizing the supply)

From the [ESP32-S3 datasheet Table 5-7](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf), measured at 3.3 V and 25 °C, TX at 100% duty [CONFIRMED-primary]:

| Mode | Peak (mA) |
|---|---|
| TX 802.11b 1 Mbps @21 dBm | **340** |
| TX 802.11g 54 Mbps @19 dBm | 291 |
| TX 802.11n HT20 MCS7 @18.5 dBm | 283 |
| RX 802.11b/g/n HT20 | 88 |
| Modem-sleep, dual core idle (WAITI) | 13.2–18.8 |

The datasheet adds: "ESP32-S3R8 … embedded with PSRAM, their current consumption might be higher."

Add the board overhead [INFERRED]: LDO, power LED, the CH343P when powered, the RGB LED, and the RS485/RS232 module (a few mA plus LEDs).
- Plan for **≥500 mA peak at 5 V**, which also matches the module requirement of ≥0.5 A.
- A connected, mostly idle logger should average roughly **60–150 mA at 5 V (0.3–0.75 W)**.
- The 5 V/2 A charger has ample margin.
- Use a short, good USB cable. Voltage droop during TX bursts is the usual cause of brown-out resets. Optionally add 470–1000 µF across 5V–GND.

---

## 2. The purchased RS485 ↔ TTL module (identified as the "HW-0519" type)

### 2.1 Identification
- **Order photo 4.webp:** "Serial Port RS485 RS-485 To TTL UART Signal Level Converter DC 3.3V 5V Module…", a local shop BD, BDT 117. The zoomed thumbnail shows a blue PCB with two gold-top SMD parts and black SMB diodes at the terminal end, an SOP-8 transceiver, a 14-pin SOIC logic IC and a 4-pin header at the far end. [CONFIRMED-primary, photo]
- **Matching board:** this layout matches RoboticsBD's product photo of the board silkscreened **"HW-0519"** ([RoboticsBD product 1816](https://store.roboticsbd.com/electronics-module/1816-ttl-to-rs485-converter-33v5v-hardware-automatic-control-converter-module-robotics-bangladesh.html), image `11827-large_default`). That photo shows:
  - header **"GND RXD TXD VCC"**;
  - terminal **"接大地 (connect-to-earth) / B- / A+"**;
  - **RXD and TXD LEDs**;
  - three SMD diodes marked "WK" and two parts marked "T110";
  - 14-pin logic IC (U2) and SOP-8 transceiver;
  - the identification as HW-0519 is [INFERRED]: photo vs photo.
- **Community use:** HW-0519 is used for inverter Modbus in community projects, e.g. "RS485-to-TTL module (`HW-0519` f.e.)" ([syssi/esphome-modbus-solax-x1](https://github.com/syssi/esphome-modbus-solax-x1/blob/main/README.md)). [SECONDARY]

### 2.2 Pinout

| Side | Pin | Meaning | Connect to |
|---|---|---|---|
| TTL header (2.54 mm) | VCC | 3.3 V or 5 V supply | **ESP32 3V3** |
| | GND | Logic ground | ESP32 GND (and the inverter's RS485 GND/signal common, if it has one) |
| | TXD | See 2.3 | ESP32 GPIO17 or GPIO18 |
| | RXD | See 2.3 | ESP32 GPIO18 or GPIO17 |
| Screw terminal | A+ | Non-inverting line (vendor convention) | Inverter "A" / "485+" / "D+" |
| | B- | Inverting line | Inverter "B" / "485-" / "D-" |
| | 接大地 ("connect to earth") | Surge-protection earth return. **Not signal GND.** One user measured "it is not connected to the Gnd pin" ([Arduino forum](https://forum.arduino.cc/t/difference-between-the-max485-and-the-hw-0519-modules/704175)). The vendor says earth it only for long outdoor runs: "the indoor short-distance transmission can not be connected to the earth" ([RoboticsBD](https://store.roboticsbd.com/electronics-module/1816-ttl-to-rs485-converter-33v5v-hardware-automatic-control-converter-module-robotics-bangladesh.html)). | **Leave unconnected** indoors |

[SECONDARY]: silkscreen from the vendor photo plus forum and listing text.

### 2.3 TXD/RXD labelling: sources conflict, so test it
- **Straight-through (labels describe the MCU side):**
  - syssi's ESPHome diagram for HW-0519 wires module **RXD ↔ ESP RX** and **TXD ↔ ESP TX** ([README](https://github.com/syssi/esphome-modbus-solax-x1/blob/main/README.md)). [SECONDARY]
  - ProtoSupplies documents a visually similar auto-direction board (poly fuses, TVS, R0 termination, 43 x 15.5 mm): "RXD … Connects to RXD TTL input on MCU", "TXD … Connects to TXD TTL output on MCU", and the example wires "Pin 10 (RX) connects to RXD", "Pin 11 (TX) connects to TXD" ([ProtoSupplies SCM TTL to RS-485](https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/)). [SECONDARY]
- **Crossed (labels describe the module side):**
  - The generic esp32io tutorial: module RXD → ESP32 TX, module TXD → ESP32 RX ([esp32io](https://esp32io.com/tutorials/esp32-rs485)).
  - An auto-generated HW-0519 page: D1(TX) → RXD, D0(RX) → TXD ([codey.online](https://codey.online/components/hw-0519-auto-flow-rs485-module)), low reliability. [SECONDARY]
- **Recommendation [INFERRED]:**
  1. Start **straight**: ESP32 **GPIO17 (TX) → module TXD**, **GPIO18 (RX) → module RXD**. This follows the source that names HW-0519 explicitly.
  2. Watch the onboard LEDs while the ESP32 sends a request. The LED tied to the module's *input* pin should flicker on every transmission.
  3. If the inverter never answers, **swap the two wires**. Swapping 3.3 V TTL lines is harmless.

### 2.4 3.3 V operation
- The listing says "DC 3.3V 5V". syssi: "make sure to power the RS485 module with 3.3V because it affects the TTL … voltage between RS485 module and ESP" ([README](https://github.com/syssi/esphome-modbus-solax-x1/blob/main/README.md)). [SECONDARY]
- If you powered it from 5 V, its TXD output would swing to 5 V into a non-5 V-tolerant ESP32 input (V_IH max VDD+0.3 V). [CONFIRMED-primary] for the ESP32 limit, [INFERRED] for the consequence.

### 2.5 Termination, bias, LEDs, speed, protection
- **Termination:** "With 120 ohm matching resistance, short-circuit R0 to enable matching resistance, short-circuit is recommended for long-distance transmission" ([RoboticsBD listing](https://store.roboticsbd.com/electronics-module/1816-ttl-to-rs485-converter-33v5v-hardware-automatic-control-converter-module-robotics-bangladesh.html)). [SECONDARY] For a 1–3 m cable to the inverter, **leave R0 open**. [INFERRED]
- **Bias and termination values:** Wikipedia's RS-485 article: "Biasing and termination values are not specified in the RS-485 standard." Termination should equal cable impedance, "typically, 120 ohms for twisted pairs" ([Wikipedia: RS-485](https://en.wikipedia.org/wiki/RS-485)). [SECONDARY]
- **Speed:** "Baud rate: 110 ~ 256000bps" (RoboticsBD / MakerLab PH listings). Auto-direction is "hardware automatic flow control", so no DE/RE GPIO is needed ([makerlab.ph](https://makerlab.ph/products/ttl-to-rs485-power-supply-converter-module-hardware-auto-control-for-arduino-avr-3-3v-5-0v)). [SECONDARY]
- **Wrong spec on the RoboticsBD page:** it also quotes "3.0V ~ 30V" supply. That belongs to the *other* variant pictured there, the one with an onboard regulator (U4) and a JST lead. The HW-0519 is the 3.3/5 V board. [INFERRED from the two photos]
- **Protection:** on the similar board, "Built-in poly fuses for over-current protection" and "transient surge protection diodes" ([ProtoSupplies](https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/)). [SECONDARY] On HW-0519 the T110 parts are probably the polyfuses and the WK parts the TVS diodes. [INFERRED]
- **Common-mode range:** RS-485 allows "−7 V to +12 V". Never apply the inverter's +12 V supply pin to A/B ([Wikipedia: RS-485](https://en.wikipedia.org/wiki/RS-485)). [SECONDARY]

### 2.6 A/B polarity
- The TIA-485 text defines A as negative for mark. But Maxim, TI, ADI and others label A as the non-inverting line, hence the "A+/B-" silkscreen. Wikipedia recommends TX+/RX+ or D+/D- labels to avoid confusion ([Wikipedia: RS-485](https://en.wikipedia.org/wiki/RS-485)). [SECONDARY]
- Practice:
  1. Connect inverter "A"/"+" → **A+** and "B"/"-" → **B-**.
  2. If there is no reply after the TX/RX swap test, swap A and B too. Reversed polarity does no damage; it just inverts the data. [INFERRED]

### 2.7 When the RS485 module is the right part
Only if the inverter's *monitoring* port is RS485: the optional RS485 board on the F-series, or a "2-in-1" BMS/monitor port like Deye's.
- An RS485 pair on a **BMS** port is often the inverter polling the *battery*, not a port you can poll.
- Exception: Deye's 2-in-1 port, where "the inverter reads the battery over CAN while SolarAssistant reads the inverter via RS485 on the same port" ([SolarAssistant: Deye 2-in-1 BMS port](https://solar-assistant.io/help/deye/2_in_1_bms_port)). [SECONDARY]

---

## 3. RS232 path (most likely needed): MAX3232 module

### 3.1 Why
- **Sister-model specs:**
  - F3 3500-24V: "USB / RS232 (Standard), CAN & RS485 (Optional)" ([ipsbazarbd F3 3500](https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/)).
  - F3 2500-24V: "USB / RS232 (Optional: CAN & RS485)" ([ipsbazarbd F3 2500](https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/)).
  - [SECONDARY] No F4 page was found. The F4 probably follows the same pattern. [INFERRED]
- **RS232 vs RS485 signalling:**
  - RS232 is single-ended, ±3 to ±15 V. "Logic one is represented as a negative voltage", with range −15 to −3 V ([Wikipedia: RS-232](https://en.wikipedia.org/wiki/RS-232)). [SECONDARY]
  - An RS485 transceiver cannot decode that. A ±12 V swing would also exceed the RS485 common-mode window. [INFERRED]
- **MAX3232 fit:** 3 V to 5.5 V supply, up to 250 kbit/s, two drivers and two receivers, ±15 kV HBM ESD on the bus pins. The receiver input tolerates ±25 V (abs. max). The driver swings about ±5.4 V into 3 kΩ ([TI MAX3232 datasheet SLLS410O](https://www.ti.com/lit/ds/symlink/max3232.pdf)). [CONFIRMED-primary] Clone chips on cheap modules may not meet the TI ESD figure. [INFERRED]

### 3.2 Module choices (Bangladesh)

| Module | TTL side | RS232 side | Size | Price (BDT) | Source |
|---|---|---|---|---|---|
| **Mini RS232 to TTL Converter MAX3232 3-5V** (bare pins; **best for cutting an RJ45 cable**) | "0V, VCC (3.0-5.5V), TTL-TXD, TTL-RXD" | "0V, VCC, RS232-RX, RS232-TX" | small (listing's "50 x 40mm" looks wrong) | **80** | [a local shop]((local shop listing)) [SECONDARY] |
| RS232 to TTL Converter Module with DB9 Female (MAX3232) | 4 pins | DB9 female | ~39 x 32 mm PCB | 199 | [a local shop]((local shop listing)) [SECONDARY] |
| RS232 to TTL Serial Interface Module (MAX3232, DB9) | 4 pins | DB9 | 45 x 32 x 15 mm | **105** (in stock) | [RoboticsBD](https://store.roboticsbd.com/electronics-module/1916-rs232-to-ttl-serial-interface-module-robotics-bangladesh.html) [SECONDARY] |
| DB9 male to screw-terminal breakout (to land a cut cable on a DB9 module) | – | DB9 male + screw terminals | – | 110 | [a local shop]((local shop listing)) [SECONDARY] |

Daraz and Techshop BD searches could not be scraped (JavaScript-rendered pages). No prices from them.

DB9 MAX3232 module pinout ([ProtoSupplies MAX3232 module](https://protosupplies.com/product/max3232-ttl-to-rs-232-interface-module/)) [SECONDARY]:
- TTL header:
  - "Tx: R1OUT on MAX3232 chip – Connect to uC RX line"
  - "Rx: D1IN on MAX3232 chip – Connect to uC TX line"
  - So this one is **crossed**, labels from the module's view.
- DB9 female:
  - **pin 2 = T1OUT** (module transmits)
  - **pin 3 = R1IN** (module receives)
  - **pin 5 = GND**
- In short, the module behaves like a DCE that plugs straight into a PC's male COM port.

### 3.3 RS232 wiring to the inverter RJ45

This uses the Voltronic / SMG-II style pinout. **It must be verified on the Savx with Section 4.2.**

| RJ45 pin (T-568B colour) | Inverter signal | Mini MAX3232 pin | DB9-module pin | Notes |
|---|---|---|---|---|
| 1 (white-orange) | Inverter **TX** (data out) | RS232-RX (R1IN) | DB9 **3** | syssi: "1 TX → P13 (RIN1)" |
| 2 (orange) | Inverter **RX** (data in) | RS232-TX (T1OUT) | DB9 **2** | syssi: "2 RX → P14 (DOUT1)" |
| 4 (blue) | **+12 V supply** (VCC 12V) | **not connected, insulate** | – | Used for WiFi dongles. Optional power source, Section 5.3. |
| 8 (brown) | GND | 0V (RS232 side) | DB9 **5** | |
| 3, 5, 6, 7 | unused (on that family) | insulate | – | Some older Axperts reportedly put +12 V on pin 7 |

Source for the table: the RJ45 tables in [syssi/esphome-smg-ii](https://github.com/syssi/esphome-smg-ii) and [syssi/esphome-pipsolar](https://github.com/syssi/esphome-pipsolar). [SECONDARY]

TTL side: VCC → ESP32 **3V3**, GND → GND. Then:
- DB9 module: module "Rx" (DIN) ← ESP32 **GPIO17 (TX)**; module "Tx" (ROUT) → ESP32 **GPIO18 (RX)** (ProtoSupplies convention).
- Mini module: direction of "TTL-TXD / TTL-RXD" is not documented. Use the loopback test below.

**Loopback test** for any MAX3232 module, before touching the inverter [INFERRED, uses TI pin functions]:
1. Power the module from 3V3.
2. Bridge its RS232-TX pin to its RS232-RX pin (or DB9 pins 2–3).
3. Run a sketch that writes bytes on Serial1 (GPIO17) and reads them back on GPIO18.
4. If nothing comes back, swap the two TTL wires. One orientation must echo.
5. With the ESP32 TX idle (high), the RS232-TX pin should measure about **−5 V** to GND. That is the MAX3232 V_OL spec of −5 V min / −5.4 V typ.

**Using the inverter's own RJ45-to-DB9 cable**, if one came in the box:
- That cable's DB9 female end is wired to plug into a PC (DTE male), the same way the DB9 module is. Joining the two DCE-style female ends needs a **DB9 male-male null-modem adapter** (pins 2↔3 crossed), not a straight gender changer. [INFERRED from the pinouts above]
- Simpler: use the cable as-is with a USB-RS232 adapter on a laptop (3.5).
- SolarAssistant gives the same convention from the cable side: "Voltronic USB RS232 cable: Pin 1 = RX, Pin 2 = TX, Pin 8 = GND" ([SolarAssistant Eco-worthy RS232](https://origin.solar-assistant.io/help/inverters/eco-worthy/ECO/rs232)). That is pin 1 = what the PC receives = inverter TX, consistent with syssi. [SECONDARY]

### 3.4 Variant seen on some SMG-II units: RS232 carried on a USB-A socket
syssi reports some SMG II units (confirmed 6KW/48V) "expose RS232 via a USB-A COM port … The USB data lines carry RS232 signals":
- D- = RX (inverter → ESP)
- D+ = TX (ESP → inverter)
- VBUS not used

That firmware also needs a wake-up frame ([esphome-smg-ii](https://github.com/syssi/esphome-smg-ii)). [SECONDARY]

**Warning:** if the Savx has a USB-A socket labelled COM, do not assume it is real USB. Measure it first (Section 4.2). [INFERRED]

### 3.5 Laptop test adapters (Mac)

| Purpose | Item | Price (BDT) | Source |
|---|---|---|---|
| USB to RS232 (DB9 male, CH340). Plugs straight into the inverter's own RJ45-DB9 cable. | HL340 USB to RS232 DB9 | 680 | [a local shop]((local shop listing)) |
| USB to RS485 + RS232 (CH340, industrial) | Industrial Grade USB to RS485/RS232 | 1,550 | [RoboticsBD](https://store.roboticsbd.com/programmer/3357-industrial-grade-usb-to-rs485rs232-converter-robotics-bangladesh.html) |
| USB to RS485 | Industrial USB to RS485/RS422 CH340, screw terminal | 480 | [a local shop]((local shop listing)) |
| USB to TTL/RS485 | FT232RL USB to TTL/RS485 | 330 | [a local shop]((local shop listing)) |
| USB to RS485 | RS485 USB-485 adapter | 750 | [RoboticsBD](https://store.roboticsbd.com/uart-rs232-rs485-ttl-modules/566-rs485-usb-485-converter-adapter-support-win7-xp-vista-linux-mac-os-robotics-bangladesh.html) |
| Laptop isolation | ADUM3160 USB Isolator (full-speed 12 Mbps) | 1,499 | [RoboticsBD](https://store.roboticsbd.com/logic-level-voltage-current-signal-converter/3972-adum3160-usb-isolator-module-robotics-bangaldesh.html) |

All rows [SECONDARY]: shop listings fetched 2026-10-21.

**Zero-cost alternative [INFERRED]:** use the ESP32-S3 itself as the USB-serial bridge.
- Flash a pass-through sketch (`Serial` ↔ `Serial1`, GPIO17/18).
- Then use CoolTerm, `screen` or pyserial on the Mac, over the MAX3232 or HW-0519, to try protocol commands before writing the logger firmware.

### 3.6 Optional: read the inverter's USB port directly (advanced)
- On Voltronic-style inverters the USB port shows up as a HID device. ned-kelly's config comment: "/dev/hidraw0 if you're connecting via the USB port on the inverter" ([inverter.conf](https://raw.githubusercontent.com/ned-kelly/docker-voltronic-homeassistant/master/config/inverter.conf)). [SECONDARY]
- ESP32-S3 "support[s] USB 2.0 OTG (supporting full-speed mode), and … Host and Device functions" and "can be used as HID Host" ([Espressif esp-faq usb.rst](https://raw.githubusercontent.com/espressif/esp-faq/master/docs/en/software-framework/peripherals/usb.rst)). [CONFIRMED-primary]
- Hardware needed:
  - native "USB" port + USB-C OTG-to-USB-B cable;
  - the clone's USB-OTG jumper bridged so the device gets VBUS.
- This saves the MAX3232 but needs custom HID-host firmware. Treat it as a fallback only. [INFERRED]

---

## 4. RJ45 ports on this class of inverter

### 4.1 Pinouts documented for comparable inverters (none confirmed for Savx yet)

| Inverter / port | Pin assignments | Source | Tag |
|---|---|---|---|
| Voltronic / PIP (Axpert family) RS232 RJ45 | 1 TX, 2 RX, **4 VCC 12V**, 8 GND | [syssi/esphome-pipsolar](https://github.com/syssi/esphome-pipsolar) | [SECONDARY] |
| ISolar/EASUN SMG II RS232 RJ45 | 1 TX, 2 RX, **4 VCC 12V**, 8 GND. Some 6 kW units: RS232 on USB-A D-/D+. | [syssi/esphome-smg-ii](https://github.com/syssi/esphome-smg-ii) | [SECONDARY] |
| "Voltronic USB RS232 cable" (named from the cable/PC side) | Pin 1 RX, Pin 2 TX, Pin 8 GND | [SolarAssistant](https://origin.solar-assistant.io/help/inverters/eco-worthy/ECO/rs232) | [SECONDARY] |
| Axpert-family forum reports | pin 4 = +12 V; some cables are powered and some are not; on older models +12 V on pin 7; RS232 comm boards have burned | search snippets of [powerforum 8635](https://powerforum.co.za/topic/8635-axpert-inverter-burning-rs232-card-when-ever-i-plug-serial-cable-to-computer/) and [17963](https://powerforum.co.za/topic/17963-inverter-connection-wifi-rj45-to-db9-r232-solved/) (site blocked direct reading: HTTP 403) | [SECONDARY, unverified] |
| SRNE (SR-HF, ABP, ASP) RS485 port | **7 = RS485A, 8 = RS485B** | [SolarAssistant SRNE SR-HF](https://solar-assistant.io/help/inverters/srne/SR-HF/rs485), [ABP](https://solar-assistant.io/help/inverters/srne/ABP/rs485) | [SECONDARY] |
| Sunsynk hybrid, BMS port, inverter side | CAN: 4 CAN-H, 5 CAN-L. RS485: 1 B, 2 A, 3 GND, 6 GND, 7 B, 8 A. RS232: 3 BMS-TX, 4 BMS-RX, 5 GND. | [Sunsynk Battery Compatibility v14 (PDF)](https://portal.segen.co.uk/reseller/docs/Sunsynk_BatteryCompatibility_v14_English.pdf) | [CONFIRMED-primary] for Sunsynk only |
| Deye "2 in 1" BMS port | RS485 1 = B, 2 = A (for *monitoring the inverter*); CAN to battery | [SolarAssistant](https://solar-assistant.io/help/deye/2_in_1_bms_port) | [SECONDARY] |
| WiFi dongle port (Voltronic/SMG-II style) | Usually *is* the RS232 RJ45, and the dongle is powered from the +12 V pin | inferred from the "VCC 12V" rows above | [INFERRED] |
| Parallel port (if fitted) | Proprietary current-sharing and sync signals. **Never connect the logger or Ethernet here.** | – | [INFERRED] |

Pinouts differ between brands and even between firmware versions. The only safe assumption is to **measure first**. Also:
- Never plug an inverter RJ45 port into an Ethernet switch, router or PoE injector.
- Never plug a LAN cable from the inverter into the BMS port "to see what happens".

[INFERRED]

### 4.2 Identify the port with a multimeter (before connecting any module)
1. **Read the labels.** Photograph the port labels under the inverter: RS232 / COM / RS485 / BMS / CAN / USB / WiFi / PARALLEL.
   - If the ports are **inside** the wiring compartment, de-energise everything first (Section 8).
   - The front photo shows the bottom wiring cover; no ports are visible in the photos.
2. **Plug in the cut cable.** Use a cheap straight patch cable, cut in half. Strip the 8 conductors, keep them apart and insulated. Plug the RJ45 end into the port and leave the cut end free.
3. **Measure each conductor.** With the inverter running normally, set the meter to DC volts. Put COM on the candidate ground (start with pin 8, brown, then try pins 3, 5, 6, 7) and measure every other conductor.
4. **Interpret the readings** [INFERRED from the level definitions cited]:

| Reading vs ground | Meaning | Action |
|---|---|---|
| Steady **+12 V** (or +5 V) | Supply pin for dongles | **Never** connect it to a module or the ESP32 GPIO. Optionally use it for power (5.3). |
| Steady about **−5 to −12 V** | RS232 **TX from the inverter**, idling at mark (negative per RS-232) | Port is RS232. Use the MAX3232. |
| About 0 V on a companion pin | RS232 RX input of the inverter | Wire it to the MAX3232 T1OUT. |
| Two pins between about 0 and 5 V, a few hundred mV apart (A slightly higher than B when biased) | RS485 pair (A/B) | Port is RS485. Use the HW-0519. |
| Two pins both near 2–3 V | Possibly CAN-H/CAN-L at recessive. Wikipedia: dominant drives "CANH … towards 3.5 V and … CANL towards 1.5 V". | Not usable with either module. |

   Sources: [Wikipedia: RS-232](https://en.wikipedia.org/wiki/RS-232), [Wikipedia: RS-485](https://en.wikipedia.org/wiki/RS-485), [Wikipedia: CAN bus](https://en.wikipedia.org/wiki/CAN_bus).
5. **Check the colour map.** Confirm which plug pin goes to which colour with the meter's continuity buzzer. Cheap cables are sometimes T-568A or use odd colours.

### 4.3 T-568B colour code (for cutting a straight patch cable)
Source: [Wikipedia: ANSI/TIA-568](https://en.wikipedia.org/wiki/ANSI/TIA-568). [SECONDARY]

| Pin | T-568B colour | T-568A colour | Pair |
|---|---|---|---|
| 1 | White/orange | White/green | 1-2 |
| 2 | Orange | Green | 1-2 |
| 3 | White/green | White/orange | 3-6 |
| 4 | Blue | Blue | 4-5 |
| 5 | White/blue | White/blue | 4-5 |
| 6 | Green | Orange | 3-6 |
| 7 | White/brown | White/brown | 7-8 |
| 8 | Brown | Brown | 7-8 |

- RS232 on pins 1/2/8 uses white-orange, orange and brown.
- RS485 on pins 7/8 uses the white-brown/brown *pair*, which is good twisted-pair practice.
- Hold the plug **clip facing away from you, contacts up, cable towards you**: pin 1 is on the left. [INFERRED]

---

## 5. Power options

| Option | How | Pros | Cons | Tag |
|---|---|---|---|---|
| **A. USB charger on an inverter-backed socket** (recommended first) | Purchased 5 V/2 A charger in a socket fed by the inverter's AC output → USB-C cable to the COM port. Or cut the cable and feed the 5V/G pins (6.3). | Uses parts already bought. Isolated supply, so ESP32 GND floats and is tied only to the inverter comm GND: no ground loop. No work in the DC compartment. | Logger dies whenever the inverter output is off (manual off, overload trip, low-battery cut-off). Small transfer gaps (F3 spec: 10 ms UPS / 20 ms appliance mode) usually ride through. Eco/"power-saving" modes that cut output at low load could switch the socket off (Victron documents this behaviour for its ECO mode; check whether the Savx has such a setting). | [INFERRED] / [SECONDARY] |
| **B. DC-DC buck from the 24 V battery** | Battery + → **inline fuse (1 A) at the battery** → LM2596HV module (DC 5–50 V in) set to 5.0–5.1 V **before** connecting → ESP32 **5V** + G pins. | Logger keeps running when the inverter is off or faulted. Independent of AC. | Wiring at the battery terminals (high short-circuit current). Drain is about 0.5–1 W, i.e. 12–24 Wh/day or 0.5–1 Ah/day at 24 V: under 1 %/day of a 100 Ah battery. Still stops when the BMS disconnects. Ties ESP32 GND to battery negative. | [INFERRED] |
| **C. Comm-port supply pin** | RJ45 pin 4 (+12 V on Voltronic/SMG-II style; **measure first**) → small buck (e.g., Mini-360, 4.75–23 V in) → 5V pin. | One cable. Same ground reference as the data lines. No charger. | Current budget undocumented. The pin may only be live when the inverter is on. Wrong-pin mistakes can kill the comm board (forum reports in 4.1). | [SECONDARY] pin / [INFERRED] |

Supporting facts:
- **Bangladesh sockets:** 230 V / 50 Hz. Original sockets are types D and M, and "Nowadays, most homes and hotels have universal sockets", which also accept type A, the US flat pin ([WorldStandards: Bangladesh](https://www.worldstandards.eu/electricity/plug-voltage-by-country/bangladesh/)). [SECONDARY]
  - The US-plug charger fits a universal socket but often loosely. Use a **universal travel adapter (BDT 319, a local shop)** for a firm contact.
  - Confirm the charger label reads **100–240 V~ 50/60 Hz**. [INFERRED]
- **Buck module input ranges:**
  - **LM2596** (TI): input "up to 40 V", operating quiescent current 5 mA typ ([TI LM2596 datasheet](https://www.ti.com/lit/ds/symlink/lm2596.pdf)). [CONFIRMED-primary]
  - LM2596 module BDT 70, "4.5-40V" ([a local shop]((local shop listing))). LM2596**HV** module BDT 250, "DC (5–50V)" ([RoboticsBD](https://store.roboticsbd.com/power-module-adapter/3326-lm2596hv-power-supply-module-dcdc-and-acdc-robotics-bangladesh.html)). [SECONDARY]
  - A 24 V lithium bank charges to about 27–29 V, and the nameplate's AC-charge DC output is 27 VDC. So **avoid** modules rated to 24 V or 28 V max on the battery:
    - "Mini-360 4.75V-23V"
    - "MP1584 4.5–28V"
    - "5V 3A Buck 6-24V"
    - RoboticsBD "12V-24V to 5V 3A Super Mini" (input "DC 4.5-24V")
    - the isolated **HLK-B2405S-2WR3**, "Input Voltage 24 VDC (±10%)", i.e. 21.6–26.4 V ([RoboticsBD](https://store.roboticsbd.com/power-module-adapter/4585-hi-link-hlk-b2405s-2wr3-24v-to-5v-2w-isolated-dc-dc-converter-module-robotics-bangladesh.html)).

  [SECONDARY] listings, [INFERRED] margin.
- **Fuse parts:** 5x20 mm inline fuse holder BDT 30 ([a local shop]((local shop listing))). [SECONDARY]

**Recommendation [INFERRED]:**
1. **Build with Option A.** It needs no new parts except a plug adapter.
2. Measure the comm port. If pin 4 really is +12 V, Option C is a tidy later upgrade.
3. Choose Option B only if you specifically want the logger to keep reporting when the inverter output is down. It still stops when the BMS opens.

Whichever you pick:
- Never connect two supplies at once on an *original* Espressif board.
- On the clone with IN-OUT open, a laptop USB cable plugged in for debugging is diode-separated from a 5V-pin supply.

---

## 6. Enclosure (100 x 68 x 50 mm ABS, clear lid)

### 6.1 Fit check

| Item | Size | Source / tag |
|---|---|---|
| Box outside | 100 x 68 x 50 mm | order photo [CONFIRMED-primary] |
| Box inside | about 92 x 60 x 45 mm, minus corner screw bosses. Sibling 100x68x40 listed "Inner Size: 92*60*35mm". | [SECONDARY] sibling / [INFERRED] |
| ESP32-S3-DevKitC-1 PCB | **62.74 x 25.40 mm** (Espressif DXF v1.1 dimension text). Clone about the same; **measure**. About 15 mm tall including downward header pins. | [DXF](https://dl.espressif.com/dl/schematics/esp_idf/DXF_ESP32-S3-DevKitC-1_V1.1_20220429.dxf) [CONFIRMED-primary] / [INFERRED] |
| HW-0519 RS485 module | about 44 x 15 mm (the similar ProtoSupplies board is 43 x 15.5 mm) | [SECONDARY] / [INFERRED] |
| MAX3232 DB9 module | 39 x 32 mm ([ProtoSupplies](https://protosupplies.com/product/max3232-ttl-to-rs-232-interface-module/)) / 45 x 32 x 15 mm ([RoboticsBD](https://store.roboticsbd.com/electronics-module/1916-rs232-to-ttl-serial-interface-module-robotics-bangladesh.html)) | [SECONDARY] |
| Screw-terminal expansion board for the S3 | **73 x 84.5 mm: does not fit** this box | [RoboticsBD BDT 550](https://store.roboticsbd.com/diy-pcb/4136-esp32-s3-gpio-terminal-expansion-board-robotics-bangladesh.html) [SECONDARY] |

Conclusion [INFERRED]:
- The ESP32 board plus a mini MAX3232 or the HW-0519 side by side need about 63 x 45 mm. That fits easily.
- A DB9-style MAX3232 makes the width tight: 25.4 + 32 = 57 mm of about 60 mm. Prefer the bare-pin mini module.

### 6.2 Suggested layout (top view, inside dimensions about 92 x 60)

```
   short wall A (cable entry, bottom when wall-mounted)
  +--------------------------------------------------------+
  | [PG7: RJ45 data cable]        [grommet/PG9: 5V cable]  |
  |                                                        |
  |  +---------------+   +------------------------------+  |
  |  | MAX3232 mini  |   | ESP32-S3 DevKitC-1 (USB-C end|  |
  |  | or HW-0519    |   |  towards wall A)             |  |
  |  | (screw term.  |   |                              |  |
  |  |  to wall A)   |   |                    [WROOM    |  |
  |  +---------------+   |                     antenna] |  |
  |                      +------------------------------+  |
  |                                  >= 15 mm free  ---->  |
  +--------------------------------------------------------+
   short wall B (antenna end, keep clear; points UP/away from inverter)
```

- Espressif: "A clearance of at least 15 mm is recommended in all directions" around the PCB antenna, and "consider the impact of the housing on the antenna" ([ESP32-S3 HW design guidelines: PCB layout](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html)). [CONFIRMED-primary]
- Board 62.74 mm + 15 mm = about 78 mm, which is under 92 mm. [INFERRED]

### 6.3 Cable entries, glands, grommets
- **PG7** gland, 3–6.5 mm cable, BDT 12 ([a local shop]((local shop listing))). Fits Cat5e (about 5–6 mm OD).
- **PG9**, 4–8 mm, BDT 13 ([a local shop]((local shop listing))). Use it for thicker Cat6 or the power cable.

  [SECONDARY]
- **Plugs vs glands:** a moulded USB-C or RJ45 plug will **not** pass through a PG7/PG9 seal insert. Choose one of these [INFERRED]:
  - (a) pass the **cut end** of the RJ45 cable through the gland, plug end outside to the inverter. This is the natural choice for the data cable;
  - (b) for power, **cut the USB cable**, keep the USB-A end at the charger, pass the bare end through a gland and land red/black on 5V/G;
  - (c) drill about 14 mm for the USB-C plug and seal with a split rubber grommet or neutral-cure silicone;
  - (d) leave the USB-C cable plugged in and use a slotted grommet. Fine for an indoor install.
- Add an internal **cable tie anchored to a lid screw boss or a stick-on tie mount** on each cable as strain relief. A tug on the cable must not pull the inverter's RJ45 jack or the Dupont leads. [INFERRED]

### 6.4 Heat, ventilation, condensation
- **Heat:** the module is rated to 65 °C ambient (1.2).
  - Mount the box **beside or below** the inverter, not above its exhaust and not stuck on the warm steel case.
  - Keep it out of direct sun; the clear lid traps heat.
  - Optional: put a DS18B20 on a spare GPIO, or use the S3's internal temperature reading, to log box temperature. [INFERRED]
- **Condensation:** humid Bangladeshi rooms condense inside sealed boxes.
  - Indoors, a **2–3 mm drain/breather hole at the lowest point** plus a **silica-gel sachet** beats a perfect seal.
  - Mount with glands pointing **down** so water cannot track in.

  [INFERRED]
- **Power dissipation:** about 0.5 W raises the inside temperature only a few °C. [INFERRED]

### 6.5 Antenna vs the metal inverter
- The WROOM-1 has an on-board PCB antenna. The WROOM-1U is the external-antenna version ([WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)). [CONFIRMED-primary]
- Don't attach the box to the inverter's sheet-steel case or a metal DB box. Mount it on the wall **≥ 10–15 cm** from the inverter, antenna end up/away. [INFERRED]
- Check RSSI in firmware: under about −75 dBm, move the box or the router. [INFERRED]

### 6.6 Mounting the boards (foam tape)
- 25 mm double-sided foam tape (BDT 92, purchased) works for the **RS485/MAX3232 module** and for a **vero-board carrier**.
- The DevKitC clone ships with headers soldered **pins down**, so it cannot sit flat on tape. Use one of these:
  - (a) plug it into **two 1x22 female headers on a vero board** and tape or screw the vero board down. Recommended;
  - (b) stack 3–4 layers of foam under the module area so the pins clear the floor;
  - (c) mount on M2/M3 standoffs.

  [INFERRED]
- The 100x68 box has screw bosses (order thumbnail), so a cut vero board (about 85 x 55 mm) can be screwed to them. [INFERRED from photo]

---

## 7. The jumper-wire problem (M-M wires vs male-pin boards)

- Both boards have **male** pins. **M-M jumpers cannot connect them.**
- The M-M set is still useful for breadboard bench tests. Note that the 25.4 mm-wide DevKitC covers most of a standard breadboard. [INFERRED]

| Fix | Parts (BDT) | Reliability | Notes |
|---|---|---|---|
| **F-F Dupont jumpers** (fastest) | 40 pcs 20 cm F-F, **75** ([a local shop]((local shop listing))) or single silicone F-F, 10 each ([a local shop]((local shop listing))) | Medium. Dupont contacts loosen with heat and vibration. | Fix each connector with a dab of hot glue or heat-shrink once working. |
| **Vero-board carrier** (recommended final build) | 15x10 cm vero board, 130 ([a local shop]((local shop listing))); 2 x 40-pin female header, 25 each ([a local shop]((local shop listing))); 3-pin 5.08 mm screw terminal, 12 ([a local shop]((local shop listing))) | High | Board and module plug in; short soldered links; RJ45 wires go into a screw terminal; whole carrier taped or screwed in the box. |
| **Solder directly** | none, or cut an M-M jumper and solder the bare end | High | Fine for one-off builds; harder to swap boards. |
| M-F jumpers (if a breadboard is used) | 40 pcs M-F, 75 ([a local shop]((local shop listing))) | Low | Bench only. |

Prices: [SECONDARY]. Advice: [INFERRED].

---

## 8. Safety and grounding

1. **Nameplate hazards** (from the user's photo) [CONFIRMED-primary]:
   - PV operating range **30–450 VDC**, Max solar **VOC 500 VDC**.
   - DC input **24 VDC, 145 A**.
   - AC 230 VAC.
   - Front labels: "ELECTRIC SHOCK HAZARD / ENERGY HAZARDOUS".
2. **Never open the wiring cover live.** If the comm port is behind the bottom cover, do all of this first [INFERRED]:
   - switch the inverter off;
   - open the **PV isolator** (PV strings stay live in daylight even with the inverter off);
   - open the **battery breaker**;
   - remove AC input;
   - wait for the DC-bus capacitors to discharge (follow the manual's wait time);
   - verify with the meter.

   Also remove rings and watches and use insulated tools. A 24 V battery can deliver kilo-amps into a short. [INFERRED]
3. **Comm-port isolation is unknown** for this inverter. Treat the RJ45 GND as possibly tied to battery negative. [INFERRED] Schneider's note for its Conext inverters:
   - comm circuits are "floating from ground" (SELV) and must connect only to circuits that prevent ground loops;
   - copper links can conduct surges back;
   - comm wiring needs separate conduit entries from PV and AC.

   Source: [Schneider AP-XC-104](https://solar.se.com/us/wp-content/uploads/sites/7/2021/12/TP20170301_Conext-Core-XC-Modbus_RS485-Wiring-AP-XC-104_rev-A.pdf). [SECONDARY, analogous product]
   - Never connect the ESP32 GND to mains earth or neutral.
   - Keep the data cable short (≤ 2–3 m) and away from PV and battery cables.
4. **Laptop plus inverter at the same time (debugging)** creates a second ground path: inverter comm GND → ESP32 → laptop USB → charger earth.
   - Run the laptop **on battery** during first tests, or use an **ADUM3160 USB isolator** (BDT 1,499).
   - The isolated USB charger in Option A avoids this path in normal operation.

   [INFERRED]
5. **Supply pins:** RJ45 pin 4 (and on some models pin 7) may carry +12 V (4.1).
   - Insulate unused conductors individually.
   - +12 V on a TTL pin or ESP32 GPIO will destroy it; the abs. max is about 3.6 V.
   - +12 V on RS485 A/B sits at the edge of the −7/+12 V common-mode range.
   - MAX3232 receiver inputs survive ±25 V; its TTL side does not.

   [CONFIRMED-primary] (TI, Espressif), [INFERRED] application.
6. **ESD:** touch the earthed inverter chassis before handling the boards.
   - MAX3232 bus pins are rated ±15 kV HBM (TI).
   - HW-0519 has TVS diodes and polyfuses on A/B (2.5).
   - The ESP32 GPIO wiring itself is unprotected, so keep it inside the box.

   [CONFIRMED-primary] / [SECONDARY]
7. **Plugging mistakes:** label the cable "INVERTER COMM – NOT ETHERNET". Never plug it into a router, switch or PoE port, or into the inverter's BMS/PARALLEL ports. [INFERRED]

---

## 9. Missing items BOM (Bangladesh, prices fetched 2026-10-21)

Already bought (order photos):

| Item | Price |
|---|---|
| ESP32-S3-N16R8 | BDT 1,096 |
| USB 5V/2A US charger | BDT 410 |
| 100x68x50 box | BDT 637 |
| RS485-TTL module | BDT 117 |
| 40 M-M jumpers | BDT 187 |
| 25 mm foam tape | BDT 92 |

**Must buy** (assuming an RS232 port, the likely case):

| # | Item | Qty | Unit BDT | Shop link | Note |
|---|---|---|---|---|---|
| 1 | Mini RS232↔TTL MAX3232 (bare pins) | 1 | 80 | [a local shop]((local shop listing)) | Alt: DB9 type 105 ([RoboticsBD](https://store.roboticsbd.com/electronics-module/1916-rs232-to-ttl-serial-interface-module-robotics-bangladesh.html)) or 199 (a local shop) |
| 2 | F-F Dupont jumpers 40 pcs 20 cm | 1 | 75 | [a local shop]((local shop listing)) | |
| 3 | Cat5e/Cat6 straight patch cable 1–2 m (to cut) | 1–2 | not verified | any computer shop | Or RJ45 plugs, 14 each ([RoboticsBD](https://store.roboticsbd.com/connector/1586-rj45-connector-robotics-bangladesh.html)) + crimper |
| 4 | Digital multimeter | 1 | 320 (DT830D) / 690 (ANENG M113) | [DT830D]((local shop listing)), [M113]((local shop listing)) | Skip if owned |
| 5 | PG7 cable gland (+ PG9 for power) | 2 | 12 / 13 | [PG7]((local shop listing)), [PG9]((local shop listing)) | |
| 6 | Universal travel adapter (US plug → BD socket) | 1 | 319 | [a local shop]((local shop listing)) | Skip if the socket grips the US plug firmly |

Subtotal for items 1, 2, 4 (DT830D), 5 (one PG7 + one PG9) and 6 = 80 + 75 + 320 + 25 + 319 = **BDT 819**, plus the patch cable. Computed from the listed prices.

**Recommended for a solid final build:**

| Item | Unit BDT | Link |
|---|---|---|
| 15x10 cm vero board | 130 | [a local shop]((local shop listing)) |
| 40-pin female header (x2) | 25 | [a local shop]((local shop listing)) |
| 3-pin screw terminal 5.08 mm | 12 | [a local shop]((local shop listing)) |
| Heat-shrink kit 94 pcs | 149 | [a local shop]((local shop listing)) |
| DB9 male screw-terminal breakout (only with a DB9 MAX3232) | 110 | [a local shop]((local shop listing)) |

**Optional (testing / alternative power):**

| Item | Unit BDT | Link |
|---|---|---|
| HL340 USB→RS232 DB9 (laptop test) | 680 | [a local shop]((local shop listing)) |
| USB→RS485 CH340 industrial | 480 | [a local shop]((local shop listing)) |
| ADUM3160 USB isolator | 1,499 | [RoboticsBD](https://store.roboticsbd.com/logic-level-voltage-current-signal-converter/3972-adum3160-usb-isolator-module-robotics-bangaldesh.html) |
| LM2596HV buck (DC 5–50 V in), Option B | 250 | [RoboticsBD](https://store.roboticsbd.com/power-module-adapter/3326-lm2596hv-power-supply-module-dcdc-and-acdc-robotics-bangladesh.html) |
| LM2596 buck (4.5–40 V in), Option B, less margin | 70 | [a local shop]((local shop listing)) |
| 5x20 mm inline fuse holder + 1 A fuse, Option B | 30 | [a local shop]((local shop listing)) |
| Mini-360 buck (4.75–23 V in), Option C from +12 V pin only | 59 | [a local shop]((local shop listing)) |
| LAN crimp tool + tester (only if making cables) | 750 | [a local shop]((local shop listing)) |

**Tools:** soldering iron and solder, side cutter and wire stripper, small screwdriver for the screw terminals, drill and step bit (12.5 mm for PG7, 15.2 mm for PG9; check the gland's thread), hot-glue gun, silica-gel sachet. [INFERRED]

All prices are [SECONDARY]: shop listings, BDT, in stock on 2026-10-21 unless noted. Daraz and Techshop BD could not be scraped.

---

## 10. ASCII wiring diagrams

### 10.1 RS232 variant (expected): ESP32-S3 + MAX3232 + inverter RJ45

```
  INVERTER RS232 RJ45                MAX3232 module (3.3 V)            ESP32-S3-DevKitC-1 (N16R8)
  (Voltronic/SMG-II style -          RS232 side     TTL side
   VERIFY with meter, Sec 4.2)
                                     +-------------------------+
  pin 1  wht/orange  (inv TX) ------>| RS232-RX (R1IN)   ROUT  |---(TTL out)------> GPIO18  (U1RXD, Serial1 RX)
  pin 2  orange      (inv RX) <------| RS232-TX (T1OUT)  DIN   |<--(TTL in)-------- GPIO17  (U1TXD, Serial1 TX)
  pin 8  brown       (GND)    -------| 0V                 GND  |------------------- GND
  pin 4  blue        (+12V)   --X    |                    VCC  |------------------- 3V3   (NOT 5V)
        (insulate; or Option C)      +-------------------------+
  pins 3,5,6,7                --X  (insulate each)
                                                                     USB-C "COM" <--- 5V charger (Option A)
  Mini-module labels: TTL-TXD/TTL-RXD direction undocumented -> run the loopback test (Sec 3.3)
  DB9 module: RJ45 pin1 -> DB9 pin3, RJ45 pin2 -> DB9 pin2, RJ45 pin8 -> DB9 pin5
              module "Rx" pin <- GPIO17, module "Tx" pin -> GPIO18 (ProtoSupplies convention)
```

### 10.2 RS485 variant (only if the port measures as RS485): ESP32-S3 + HW-0519

```
  INVERTER RS485 port                 HW-0519 RS485<->TTL (auto-direction, 3.3 V)        ESP32-S3
  (pin numbers model-specific;
   e.g. SRNE: 7=A, 8=B)               terminal            header
                                     +---------------------------------------+
  A / 485+ / D+  ------------------->| A+                             TXD    |<------- GPIO17 (TX)   } try straight first
  B / 485- / D-  ------------------->| B-                             RXD    |-------> GPIO18 (RX)   } swap if no reply
  (twist A/B: use wht/brown+brown)   | 接大地 (earth) --X leave open   VCC    |-------- 3V3
  GND / SC (if the port has one) ----|-------------------------------- GND   |-------- GND
                                     |  R0 pad: leave OPEN (short cable)     |
                                     +---------------------------------------+
  No DE/RE pin needed (hardware auto flow control). If still no reply: swap A+/B- (harmless).
```

### 10.3 Power variants

```
  Option A (recommended):  inverter-backed 230 V socket -> [universal adapter] -> 5V/2A charger -> USB-C "COM" port
                           (or cut cable: red -> 5V pin, black -> G pin; one supply at a time)

  Option B (always-on):    24 V battery (+) --[1 A fuse at battery]--> LM2596HV IN+  (set OUT = 5.0-5.1 V first!)
                           24 V battery (-) -----------------------> LM2596HV IN-
                           LM2596HV OUT+ -> ESP32 5V pin ;  OUT- -> ESP32 G

  Option C (after measuring): RJ45 pin 4 (+12 V) -> Mini-360 IN+ ; RJ45 pin 8 (GND) -> IN- ; OUT 5 V -> ESP32 5V / G
```

---

## 11. Bring-up order (hardware)

1. **Bench, ESP32 only.**
   - Flash a blink + WiFi test over the "COM" port (settings in 1.5).
   - Confirm PSRAM is detected (`ESP.getPsramSize()` should report about 8 MB).
   - Confirm it joins the 2.4 GHz network or the iPhone hotspot with Maximize Compatibility on.
2. **Bench, converter.**
   - MAX3232: wire GPIO17/18/3V3/GND and run the loopback test (3.3).
   - HW-0519: check the TX LED flickers on transmit.
3. **Inverter port ID.** Photograph the labels, cut the patch cable, run the meter procedure (4.2) and fill in the pin table.
4. **Laptop protocol test (optional).** Use a USB-RS232/485 adapter on the Mac, or the ESP32 pass-through bridge. The laptop runs on battery. Confirm the inverter answers the protocol chosen by R1–R4.
5. **Connect the logger.** RJ45 to the inverter, power by Option A. Verify replies, then log RSSI and box temperature for 24 h.
6. **Enclose.** Vero carrier, glands, strain relief, silica gel. Mount beside the inverter with the antenna end up and away from metal.

---

## 12. Open items for the plan writer and other tracks
- **Confirm the Savx F4 comm ports** (labels, RJ45 vs USB vs DB9) from a photo of the inverter's underside. This decides RS232 (MAX3232) vs RS485 (HW-0519), and none of the photos show it. Ask the user for a photo.
- **Confirm the protocol and baud rate** from R1–R4. Voltronic-style ASCII, SMG/Modbus-RTU over RS232 and others are all within MAX3232 / HW-0519 speed limits.
- **Ask whether an RJ45-DB9 or USB cable came with the inverter.** That affects whether the HL340 USB-RS232 adapter is the quickest laptop test.
- **Check whether the F4 has an "eco/power-saving" output mode** that would matter for power Option A.

---

## Sources (all fetched 2026-10-21)
- Espressif ESP32-S3 Series Datasheet v2.2: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- Espressif ESP32-S3-WROOM-1/1U Datasheet v1.8: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- ESP32-S3-DevKitC-1 v1.1 user guide: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
- DevKitC-1 v1.1 dimension drawing (DXF): https://dl.espressif.com/dl/schematics/esp_idf/DXF_ESP32-S3-DevKitC-1_V1.1_20220429.dxf
- ESP32-S3 hardware design guidelines, PCB layout: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html
- Espressif esp-faq USB: https://raw.githubusercontent.com/espressif/esp-faq/master/docs/en/software-framework/peripherals/usb.rst
- arduino-esp32 HardwareSerial.h: https://raw.githubusercontent.com/espressif/arduino-esp32/master/cores/esp32/HardwareSerial.h
- arduino-esp32 boards.txt: https://raw.githubusercontent.com/espressif/arduino-esp32/master/boards.txt
- arduino-esp32 partitions: https://github.com/espressif/arduino-esp32/tree/master/tools/partitions
- arduino-esp32 installing.rst: https://raw.githubusercontent.com/espressif/arduino-esp32/master/docs/en/installing.rst
- arduino-esp32 releases: https://github.com/espressif/arduino-esp32/releases
- PlatformIO esp32-s3-devkitc-1.json: https://raw.githubusercontent.com/platformio/platform-espressif32/develop/boards/esp32-s3-devkitc-1.json
- pioarduino README: https://github.com/pioarduino/platform-espressif32
- pioarduino N16R8 board JSON: https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/boards/esp32-s3-devkitc1-n16r8.json
- pioarduino main.py: https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/builder/main.py
- YD-ESP32 clone notes: https://github.com/deepcoder/YD-ESP32-23
- Mischianti clone page: https://mischianti.org/vcc-gnd-studio-yd-esp32-s3-devkitc-1-clone-high-resolution-pinout-and-specs/
- Arduino forum, Chinese ESP32-S3 5V pin: https://forum.arduino.cc/t/chinese-esp32-s3-5v-pin-warning/1192758
- Arduino forum, HW-0519: https://forum.arduino.cc/t/difference-between-the-max485-and-the-hw-0519-modules/704175
- syssi esphome-modbus-solax-x1 (HW-0519 wiring): https://github.com/syssi/esphome-modbus-solax-x1/blob/main/README.md
- syssi esphome-smg-ii: https://github.com/syssi/esphome-smg-ii
- syssi esphome-pipsolar: https://github.com/syssi/esphome-pipsolar
- ned-kelly docker-voltronic inverter.conf: https://raw.githubusercontent.com/ned-kelly/docker-voltronic-homeassistant/master/config/inverter.conf
- ProtoSupplies SCM TTL-RS485: https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/
- ProtoSupplies MAX3232 module: https://protosupplies.com/product/max3232-ttl-to-rs-232-interface-module/
- esp32io RS485 tutorial: https://esp32io.com/tutorials/esp32-rs485
- codey.online HW-0519: https://codey.online/components/hw-0519-auto-flow-rs485-module
- MakerLab PH TTL-RS485 listing: https://makerlab.ph/products/ttl-to-rs485-power-supply-converter-module-hardware-auto-control-for-arduino-avr-3-3v-5-0v
- TI MAX3232 datasheet: https://www.ti.com/lit/ds/symlink/max3232.pdf
- TI LM2596 datasheet: https://www.ti.com/lit/ds/symlink/lm2596.pdf
- Wikipedia RS-485: https://en.wikipedia.org/wiki/RS-485
- Wikipedia RS-232: https://en.wikipedia.org/wiki/RS-232
- Wikipedia CAN bus: https://en.wikipedia.org/wiki/CAN_bus
- Wikipedia ANSI/TIA-568: https://en.wikipedia.org/wiki/ANSI/TIA-568
- SolarAssistant SRNE SR-HF RS485: https://solar-assistant.io/help/inverters/srne/SR-HF/rs485
- SolarAssistant SRNE ABP RS485: https://solar-assistant.io/help/inverters/srne/ABP/rs485
- SolarAssistant Eco-worthy RS232: https://origin.solar-assistant.io/help/inverters/eco-worthy/ECO/rs232
- SolarAssistant Deye 2-in-1 BMS port: https://solar-assistant.io/help/deye/2_in_1_bms_port
- Sunsynk Battery Compatibility v14: https://portal.segen.co.uk/reseller/docs/Sunsynk_BatteryCompatibility_v14_English.pdf
- Schneider Conext AP-XC-104: https://solar.se.com/us/wp-content/uploads/sites/7/2021/12/TP20170301_Conext-Core-XC-Modbus_RS485-Wiring-AP-XC-104_rev-A.pdf
- powerforum threads (search snippets only; HTTP 403):
  - https://powerforum.co.za/topic/8635-axpert-inverter-burning-rs232-card-when-ever-i-plug-serial-cable-to-computer/
  - https://powerforum.co.za/topic/17963-inverter-connection-wifi-rj45-to-db9-r232-solved/
- PlatformIO forum (macOS CH34x driver): https://community.platformio.org/t/fails-to-upload-to-esp32s3-wroom-1/38152/29
- Apple Personal Hotspot Maximize Compatibility: https://support.apple.com/guide/platform-support/sup025284ac0
- WorldStandards Bangladesh plugs: https://www.worldstandards.eu/electricity/plug-voltage-by-country/bangladesh/
- IPS Bazar F3 3500-24V: https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/
- IPS Bazar F3 2500-24V: https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/
- RoboticsBD UART/RS232/RS485 category: https://store.roboticsbd.com/204-uart-rs232-rs485-ttl-modules
- RoboticsBD and a local shop product pages: linked inline above.
