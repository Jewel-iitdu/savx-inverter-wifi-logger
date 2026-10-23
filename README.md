# Savx F4 WiFi Logger (ESP32-S3)

A do-it-yourself **WiFi datalogger for the Savx F4 3KW 24 V solar inverter**, the model sold in Bangladesh under
"Brands of IPS Bazar". Inside it is a re-branded **Sorotec REVO VM II PRO**: a Voltronic-family inverter that speaks
**PI30, REVO dialect**, at 2400 baud. It reads the inverter's free **COM** port and shows everything on your phone,
with alerts on Telegram (in Bangla) and by e-mail.

It is built and running on a real Savx F4. The protocol work also makes it a good starting point for other
Voltronic / PI30 inverters (Axpert, MPP Solar, EASun and other re-brands).

> **Read-only by design.** The logger only *asks* the inverter for data and never changes a setting. On the Savx F4
> firmware 72.40, remote settings are not possible anyway (see [below](#what-to-expect-from-the-savx-f4)).

---

## What you get

- **Live dashboard** (web app / PWA): updated every 2 s on any phone or computer, with a power-flow diagram
  (solar, grid, battery, load).
- **History:**
  - Today, day, week, month and year charts;
  - daily, monthly and yearly kWh;
  - CSV export.
- **English or বাংলা (Bangla):** switchable in the app; Bangla numerals and a 12-hour Bangla clock (সকাল ৭:১১).
- **Alerts:**
  - **Channels:** Telegram (Bangla or English, up to 5 people) and e-mail through your own SMTP account. ntfy and
    Healthchecks are optional.
  - **Built-in:** grid lost/restored, battery low/critical, inverter fault, overload, over-temperature and inverter
    link lost.
  - **Your own:** usage-threshold rules, e.g. "daily grid use > 5 kWh".
  - **Planned grid-off window:** if you switch the grid off on a schedule, you get one message when the cut starts and
    one when it ends, marked "planned". A real outage still alerts normally.
- **Free cloud:** Firebase Realtime Database (Spark plan) and Cloudflare Workers static hosting. No card needed.
- **Optional Android app:** a small Capacitor wrapper around the dashboard.
- **Local page on your home WiFi**, with status, raw replies and over-the-air firmware updates.

## Hardware (about ৳1,500 / US$12)

| Part | Notes |
|---|---|
| ESP32-S3 DevKitC-1 **N16R8** (16 MB flash, 8 MB PSRAM) | Any ESP32-S3 dev board with PSRAM should work |
| RS485↔TTL module with a **MAX485** (e.g. HW-0519, header `VCC TXD RXD GND`, terminals `A+ B−`) | Used as a **receiver** for the inverter's RS232 signal: see below |
| A spare **LAN cable** (cut in half) | Gives the RJ45 plug for the inverter's COM port |
| Male–male jumpers, foam tape | Soldering is optional but recommended for a permanent install |
| 5 V USB charger + USB-C cable | Plug it into a socket fed by the **inverter's output**, so the logger keeps running in power cuts |
| Small junction box (about 100 × 68 × 50 mm) | |

**No MAX3232 needed, thanks to the "RS232-lite" trick:**
- **Inverter → logger:** the inverter's RS232 transmit line swings to ± several volts. The RS485 module's receiver
  reads it safely through its A+/B− inputs.
- **Logger → inverter:** the ESP32's *inverted* UART output (0 / 3.3 V) drives the inverter's RS232 receive pin
  directly, and typical RS232 receivers accept that.

It is explained in detail, with its limits, in [docs/03 §4](docs/03-hardware-and-wiring.md).

## Wiring (Savx F4 COM port)

| RJ45 pin (T568B colour) | Inverter signal | Connect to |
|---|---|---|
| **1** (white-orange) | inverter **TX** (idles negative) | module **A+** |
| **2** (orange) | inverter **RX** | ESP32 **GPIO17** |
| **8** (brown) | **GND** | module **B−** **and** ESP32 **G** (short jumper) |
| 3, 5, 6, 7 | not used | cut and insulate |
| **4** (blue) | **+VCC: never connect** | cut and insulate |

| RS485 module header | ESP32 |
|---|---|
| VCC | **3V3** (not 5 V) |
| GND | G |
| TXD (driver input) | GPIO16 (the firmware holds it HIGH, so the module never transmits) |
| RXD (receiver output) | GPIO18 |

⚠️ **Verify on your own unit before connecting GPIO17.** The firmware has built-in idle-level and upper-voltage
tests, used in the staged procedure in [docs/03 §8](docs/03-hardware-and-wiring.md). Pin assignments can differ
between batches, and a negative RS232 line on an ESP32 pin will damage it.

**Never** use the **BMS** port (it belongs to the lithium battery), never connect pin 4, and never open the inverter.

Two mistakes we actually made while building:
- **A+ and B− swapped:** the inverter answers, but every reply arrives garbled (`badStart` in `/api/status`).
- **A faulty LAN cable:** every pin reads "floating". Redo the idle tests after any cable change.

## Build it

1. **Firmware** ([docs/05](docs/05-firmware-guide.md)):
   ```bash
   cd firmware
   pio test -e native               # 113 host tests
   pio run -e savx -t upload        # USB-C on the board's "COM" port
   ```
   **No toolchain?** Download the prebuilt `…-factory.bin` from
   [Releases](../../releases) (ESP32-S3 **N16R8** boards only)
   and write it at address **0x0**, either with `esptool.py --chip esp32s3 write_flash 0x0 <file>.bin` or with the
   browser flasher https://espressif.github.io/esptool-js/. Writing at 0x0 erases saved settings.
2. **Setup portal** ([docs/05 §5](docs/05-firmware-guide.md)):
   1. On first boot the board opens the WiFi `SavxLogger-XXXX`; the password is printed on the serial console.
   2. Open http://192.168.4.1.
   3. Enter your WiFi, an admin password and, optionally, the cloud login and SMTP server.
   4. Later, hold **BOOT** for 3 s to open the portal again.
3. **Connect the inverter in stages** ([docs/03 §8](docs/03-hardware-and-wiring.md),
   [docs/05 §8](docs/05-firmware-guide.md)):
   1. Bench self-test.
   2. Idle test on each candidate pin.
   3. Upper-voltage check.
   4. Arm the link.
   5. The logger probes the protocol and finds PI30 REVO by itself.
4. **Cloud**, needed for remote viewing ([docs/06 §2–3](docs/06-cloud-and-dashboard-guide.md),
   [firebase/README](firebase/README.md)):
   1. Create a Firebase project with a Realtime Database and two e-mail/password users (you and the device).
   2. Render the rules with your user IDs: `tools/render-rules.sh`.
   3. Publish them.
5. **Dashboard** ([docs/06 §5](docs/06-cloud-and-dashboard-guide.md), [web/README](web/README.md)):
   ```bash
   cd web
   cp .env.example .env             # your Firebase web config
   npm ci && npm run build
   npx wrangler deploy              # -> https://solar.<your-subdomain>.workers.dev
   ```
   Open it on your phone and add it to the Home Screen. `?demo=1` shows fake data without any cloud.
6. **Telegram:**
   1. Create a bot with **@BotFather**.
   2. Open `http://<logger-ip>/telegram`, paste the token and pick বাংলা or English.
   3. Press **Start** in your bot. The logger pairs by itself.
7. **Android app** (optional, [android-app/README](android-app/README.md)): set `server.url` in
   `capacitor.config.json` to your dashboard URL, then `npm run apk`.
8. **Updates:** build, then upload `firmware.bin` at `http://<logger-ip>/update`. Or upload the release's `…-ota.bin`
   there. Settings are kept.

[docs/00](docs/00-plan-overview.md) is the full plan. [docs/08](docs/08-risks-troubleshooting-maintenance.md)
covers troubleshooting.

## What to expect from the Savx F4

Measured on a unit with main firmware `VERFW:00072.40` (October 2026):

| Behaviour | What the project does |
|---|---|
| Output power is reported as **VA only**, in every power field | Load and grid shown in VA, or **W estimated** with a power factor you calibrate from the LCD (Settings) |
| **Settings cannot be read back**: QPIRI priorities are fixed placeholders, QFLAG does not change when you change the LCD, `PQSE`/`PQSA` get no answer | **No remote settings.** Change settings on the LCD ([research/r10](research/r10-revo-remote-settings.md)) |
| Battery current from the inverter is whole amps | Uses the lithium **BMS** values via `QLITH0` (0.1 A) |
| The inverter draws about **0.9 A (~22 W)** from the battery all the time | Shown as "standby". Set LCD program 29 (low-DC cut-off) to about 25 % so a long outage cannot drain the battery to 0 % |
| The "line fail" bit stays set with the grid present | The measured grid voltage wins |
| Occasional transient NAKs at 2 s polling | Harmless: commands are only dropped after 5 failures in a row |

## Repository map

| Path | What |
|---|---|
| [firmware/](firmware/) | ESP32-S3 firmware (PlatformIO, Arduino-ESP32 3.3, FreeRTOS tasks). `include/config.h` holds pins, intervals and limits |
| [web/](web/) | Dashboard PWA (Vite, TypeScript, Preact, Firebase JS SDK, uPlot), English + Bangla (`src/lib/i18n.ts`) |
| [android-app/](android-app/) | Capacitor shell for Android |
| [firebase/](firebase/) | Realtime Database rules (with placeholders for your user IDs) |
| [docs/](docs/) | The build guides: plan, inverter profile, protocol, hardware and wiring, architecture, firmware, cloud, tests, troubleshooting, sources |
| [research/](research/) | Background research: Voltronic PI30 and REVO protocol, Modbus families, hosting, e-mail, remote settings |
| [shared/](shared/), [tools/](tools/) | Shared code tables, rules renderer and tests, board check sketch |

## Safety and disclaimer

Solar inverters and lithium batteries carry dangerous voltages and currents. This project only touches the
inverter's low-voltage **COM** communication port. Follow the staged procedure, never connect pin 4, and never open
the inverter. You build and use it at your own risk. The software comes with **no warranty** (see [LICENSE](LICENSE)).
"Savx", "Sorotec", "Voltronic" and other names are trademarks of their owners. This project is not affiliated with
them.

## Credits

- **Protocol documentation and community findings:** [mpp-solar](https://github.com/jblance/mpp-solar) (including
  the Sorotec REVO protocol PDF), ESPHome *pipsolar*, Solar2MQTT, the DIY Solar Forum and photovoltaikforum.com
  communities. See [docs/09](docs/09-sources.md).
- **Built** with help from Claude (Anthropic).

Contributions welcome, especially captures and findings from other Savx models or firmware versions.
