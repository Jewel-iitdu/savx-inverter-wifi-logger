# 05 — Firmware guide

This guide covers the ESP32-S3 logger firmware in [`firmware/`](../firmware): installing the tools on the Mac, building and flashing, the first-boot setup portal, the local web page, the bench self-tests, the inverter procedure (idle test → upper check → wire GPIO17 → arm → probe), OTA updates, safe mode and recovery, every setting, a code tour, the host unit tests, and troubleshooting.

It describes **the code as it is in this repository** (firmware `0.6.1`, `FW_VERSION` in [`include/config.h`](../firmware/include/config.h), which includes the fixes of the safety review in [03 Appendix A](03-hardware-and-wiring.md#appendix-a--safety-review-2026-10-21)). Where [03](03-hardware-and-wiring.md) or [07](07-milestones-and-test-plan.md) use a different button name, route or command, **this guide wins**. Cloud, dashboard and e-mail set-up are in [06](06-cloud-and-dashboard-guide.md). The module-level reference is [`firmware/README.md`](../firmware/README.md).

> ⚠️ **Safety, always.** The firmware is read-only towards the inverter: it is compiled with `INV_ALLOW_WRITES=0` (any other value stops the build), every frame passes an exact-match whitelist of the read commands it actually sends, and **GPIO17 stays a high-Z input** until (1) the module self-ID is stored, (2) the idle-level test has found the inverter's transmit pin, (3) the **upper check** has shown the other pin sits below 3.3 V, (4) you arm the link yourself, and (5) the **pre-arm check** finds GPIO17 and the module's receiver output LOW. An armed logger repeats the GPIO17 check at every boot. Wire COM pins **1, 2 and 8 only**. **Never pin 4. Never touch the BMS jack.** Before any wiring change: RJ45 out of COM first, then USB out ([03](03-hardware-and-wiring.md) §8.2), and **disarm (`reset-link`) before you rewire**.

**Status on 2026-10-21.** Built and host-tested on the Mac; **not yet run on the real board or inverter**.

| Check | Command (in `firmware/`) | Result |
|---|---|---|
| Firmware build | `pio run -e savx` | SUCCESS, RAM 30.6 % (100,300 B), flash 22.2 % (1,456,227 B of 6,553,600) |
| Host unit tests | `pio test -e native` | **98/98** passed in 8 suites |
| Local UI image | `pio run -e savx -t buildfs` | SUCCESS (`.pio/build/savx/littlefs.bin`) |
| Arduino bench sketch | `pio ci … tools/board_check/board_check.ino` (§2.4) | SUCCESS, no warnings |

> **Not yet implemented** (do not look for these; the workaround is given where one exists)
> - **No HTTP download of the raw-capture file** `/log/raw-0.txt`. Use `GET /api/raw` (recent exchanges) or read the whole filesystem over USB with `-t download_fs` (§9.3).
> - **No local-page form for the alert channels.** ntfy topic, Healthchecks URL, dashboard URL and nudge URL are set in the setup portal or with `POST /api/config` (§6.5). Firebase and Telegram credentials are **portal only**.
> - **No "factory reset" command.** Use the NVS erase in §13.5, or a full erase (§13.6).
> - **No way to force safe mode on purpose.** Only crash-type resets count (§13.1); plain power cuts never do. That is why the "power-cycle 3 times" check in [07](07-milestones-and-test-plan.md) M4 step 6 expects **normal** boots.
> - **No safe-mode "Exit" button** on the local page: use `safe-exit` (serial) or `POST /api/safemode/exit` (§13.2).
> - **RS485-on-COM and TTL-direct link modes** exist as names in `LinkProfile.h`, but nothing can select them. Only RS232-lite (default) and MAX3232 (`arm-max3232`) can be armed. [03](03-hardware-and-wiring.md) §9.3 (F4) is therefore "unsupported: stop and escalate".
> - **PI18** inverters are detected but not decoded (raw capture only).
> - **`include/codes.h` is hand-written**, not generated from [`shared/codes.json`](../shared/codes.json); the rule-metric catalogue lives in `src/agg/RuleTypes.cpp` (no generated `alert_metrics.h`).
> - **No fixture loader:** `firmware/test/fixtures/` does not exist. Real frames are added as string literals in the test sources (§16.3).
> - **Gmail SMTP fallback** (r8 "v2") is not built. E-mail goes only through the Apps Script mailer ([06](06-cloud-and-dashboard-guide.md) §9).
> - **No static-IP fields** in the portal: use a DHCP reservation in the router if you want a fixed address.

---

## Contents

1. [Mac setup](#1-mac-setup)
2. [Arduino IDE quick bench check (optional)](#2-arduino-ide-quick-bench-check-optional)
3. [Connecting the board](#3-connecting-the-board)
4. [Build, flash, filesystem, monitor](#4-build-flash-filesystem-monitor)
5. [First boot and the setup portal](#5-first-boot-and-the-setup-portal)
6. [The local web page (`http://solar.local`)](#6-the-local-web-page-httpsolarlocal)
7. [Bench self-tests (Stage B)](#7-bench-self-tests-stage-b)
8. [The inverter procedure (Stages C, C2, D, E)](#8-the-inverter-procedure-stages-c-c2-d-e)
9. [Probe report and raw capture](#9-probe-report-and-raw-capture)
10. [Day-to-day: LED, serial console, status](#10-day-to-day-led-serial-console-status)
11. [Cloud, alerts and rules on the device](#11-cloud-alerts-and-rules-on-the-device)
12. [OTA updates (`/update`)](#12-ota-updates-update)
13. [Safe mode and recovery](#13-safe-mode-and-recovery)
14. [Configuration reference](#14-configuration-reference)
15. [Code tour](#15-code-tour)
16. [Host unit tests](#16-host-unit-tests)
17. [Adding a decoded field](#17-adding-a-decoded-field)
18. [Troubleshooting](#18-troubleshooting)
19. [Sources](#19-sources)

---

## 1. Mac setup

You need the Mac, the ESP32-S3 board, and a USB-C **data** cable (some cables only charge). Nothing else is installed globally except the PlatformIO tool you pick below. All commands are for the macOS default shell (zsh).

### 1.1 Option A (recommended): VS Code + PlatformIO IDE

1. Install **Visual Studio Code** from <https://code.visualstudio.com>.
2. In VS Code: **Extensions** (⇧⌘X) → search **PlatformIO IDE** (publisher PlatformIO) → **Install**. It installs PlatformIO Core into `~/.platformio` the first time ([PlatformIO IDE for VS Code](https://docs.platformio.org/en/latest/integration/ide/vscode.html)).
3. **File → Open Folder…** → choose the **`firmware`** folder (the one that contains `platformio.ini`), not the repository root. PlatformIO only detects a project whose `platformio.ini` is at the top of the opened folder.
4. Wait for the status bar to show the PlatformIO toolbar (house icon, ✓ Build, → Upload, plug icon = Serial Monitor) and the environment selector `env:savx`.
5. For CLI commands inside VS Code, open **PlatformIO: New Terminal** from the command palette (⇧⌘P); `pio` is on the PATH there.

Optional, so `pio` also works in the normal Terminal ([shell commands](https://docs.platformio.org/en/latest/core/installation/shell-commands.html)):

```zsh
mkdir -p ~/.local/bin
ln -s ~/.platformio/penv/bin/platformio ~/.local/bin/platformio
ln -s ~/.platformio/penv/bin/pio ~/.local/bin/pio
echo 'export PATH=$PATH:$HOME/.local/bin' >> ~/.zprofile    # then open a new Terminal window
```

### 1.2 Option B: PlatformIO Core CLI only

Choose **one** method ([installation methods](https://docs.platformio.org/en/latest/core/installation/methods/index.html)):

| Method | Command | Notes |
|---|---|---|
| Homebrew | `brew install platformio` | [brew page](https://docs.platformio.org/en/latest/core/installation/methods/brew.html) |
| PlatformIO installer script | `curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py && python3 get-platformio.py` | PlatformIO's recommended method ([installer script](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html)); then add the `~/.local/bin` links above |
| pipx | `brew install pipx && pipx install platformio` | Standard pipx usage; PlatformIO's own docs list only pip (`python3 -m pip install -U platformio`) and recommend it only for CI |

Check: `pio --version` (CI pins `platformio==6.2.0`; any 6.x works because the platform itself is pinned in `platformio.ini`).

### 1.3 First build

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/firmware
pio run -e savx
```

The first run downloads the pinned platform (pioarduino `55.03.312-1` = Arduino-ESP32 3.3.12 + ESP-IDF 5.5.5), the Xtensa toolchain and the three libraries (ArduinoJson 7.4.3, WiFiManager 2.0.17, ElegantOTA 4.0.0). Expect several GB under `~/.platformio` (3.8 GB unpacked on this Mac, [r7](../research/r7-web-hosting.md) §4.7) and a few minutes. Later builds take about 10 s. It must end with `[SUCCESS]` and the RAM/flash lines shown in the status table above.

> The development copy of this repository ran PlatformIO from a scratch folder (`PLATFORMIO_CORE_DIR=<scratch>/pio-core <scratch>/pio-venv/bin/pio …`). You do not need that: a normal install uses `~/.platformio`.

---

## 2. Arduino IDE quick bench check (optional)

The real firmware is built with PlatformIO. The Arduino IDE (2.3.10 is on the Mac) is only for a 2-minute check that the board is what it claims to be: chip, 16 MB flash, 8 MB PSRAM and the Wi-Fi signal where the logger will live. The sketch is [`tools/board_check/board_check.ino`](../tools/board_check/board_check.ino). **Nothing is connected to the board except USB.**

### 2.1 Install the ESP32 core

1. Arduino IDE → **Settings…** (⌘,) → **Additional boards manager URLs** → add
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` → **OK** ([Espressif: installing](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)).
2. **Tools → Board → Boards Manager…** → search `esp32` → install **esp32 by Espressif Systems**, version **3.x** (3.3.12 matches the PlatformIO build). Do not pick "Arduino ESP32 Boards" by Arduino.
3. Restart the IDE.

### 2.2 Board settings

**Tools → Board → esp32 → ESP32S3 Dev Module**, then set (menu names as in the 3.3.12 `boards.txt`):

| Tools menu | Value | Why |
|---|---|---|
| Flash Mode | **QIO 80MHz** | The N16R8 module's flash is quad SPI |
| Flash Size | **16MB (128Mb)** | |
| PSRAM | **OPI PSRAM** | The 8 MB PSRAM is octal. "Disabled" or "QSPI PSRAM" gives 0 KB |
| USB CDC On Boot | **Disabled** when the cable is in the **COM** port (CH343 bridge); **Enabled** only if you use the native **USB** port | Decides whether `Serial` goes to UART0 (COM) or to the native USB ([r5](../research/r5-hardware.md) §1.4) |
| USB Mode | Hardware CDC and JTAG (only matters on the native USB port) | |
| Upload Mode | UART0 / Hardware CDC | Works on both ports |
| Partition Scheme | leave the default | The sketch needs no filesystem |
| Port | the board's `/dev/cu.*` port (§3) | |

### 2.3 Run it

1. Open `tools/board_check/board_check.ino`, press **Upload** (→).
2. **Tools → Serial Monitor**, set **115200 baud**. Press the board's **RST** button if nothing appears.
3. Expected output (your values differ):

   ```
   === Savx datalogger: ESP32-S3 board check ===
   Chip:       ESP32-S3 rev 0, 2 cores
   Flash size: 16 MB   (expect 16)
   PSRAM size: 8192 KB (expect ~8192; 0 means PSRAM setting is wrong)
   Free heap:  … KB
   MAC:        xx:xx:xx:xx:xx:xx

   Scanning 2.4 GHz WiFi...
      1  YourHomeWiFi                      ch  6  RSSI  -58 dBm
     RSSI guide: > -67 great, -67..-75 OK, < -75 move the router or the logger.
   ```

   The revision number varies (0–2 is normal). The flash size is read from the chip ID, so it is the real chip size. The scan repeats every 20 s: carry the board (Mac on battery) to the inverter and note the RSSI of your network in site-survey §14. If the clone's RGB LED is bridged, it blinks dim green.
4. Afterwards the real firmware simply overwrites the sketch (§4).

### 2.4 Compile check from the command line (what was run here)

The sketch was compile-checked with PlatformIO's `ci` command against the same pinned platform. It builds without warnings (RAM 13.8 %, flash 13.5 %):

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
pio ci --board esp32-s3-devkitc1-n16r8 \
  --project-option="platform=https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip" \
  --project-option="framework=arduino" tools/board_check/board_check.ino
```

---

## 3. Connecting the board

### 3.1 Which USB-C port

The clone has two USB-C ports ([03](03-hardware-and-wiring.md) §3.2):

| Silkscreen | Behind it | Use |
|---|---|---|
| **COM** (sometimes "UART") | CH343 USB-serial bridge → UART0 (GPIO43/44) | **Always this one**: flashing, boot log, serial console, and power in the final install |
| **USB** | ESP32-S3 native USB (GPIO19/20) | Not used. The firmware is built with `ARDUINO_USB_CDC_ON_BOOT=0`, so **the native port shows no log at all** |

> ⚠️ One USB cable at a time, and never use the board's **5V** header pin ([03](03-hardware-and-wiring.md) N13).

### 3.2 macOS port names and drivers

1. Plug the cable into **COM**, then list the ports:
   ```zsh
   pio device list
   ```
   The board is the entry whose **Hardware ID** shows `VID:PID=1A86:…` (WCH, the CH343 bridge). `303A:…` (Espressif) means you are on the native **USB** port: move the cable. Bluetooth entries such as `/dev/cu.Bluetooth-Incoming-Port` have `Hardware ID: n/a`.
2. Typical names [INFERRED, depends on the driver]: `/dev/cu.usbmodem…` with Apple's built-in driver, `/dev/cu.wchusbserial…` with WCH's driver. Always use the **`/dev/cu.*`** form, not `/dev/tty.*`.
3. **Drivers.** macOS normally handles the CH34x family with its own driver extension. Install WCH's macOS driver only if no new `/dev/cu.*` appears with a known-good data cable on both ports, and **never install both** ([r5](../research/r5-hardware.md) §1.4).
4. If PlatformIO picks the wrong port (for example a Bluetooth one), name it: `--upload-port /dev/cu.usbmodemXXXX` for uploads, `-p /dev/cu.usbmodemXXXX` for the monitor.

The `system_profiler` command in [03](03-hardware-and-wiring.md) §3.2 shows the same vendor IDs.

---

## 4. Build, flash, filesystem, monitor

Run everything in `firmware/` (CLI), or use the PlatformIO toolbar in VS Code. The environment is always **`savx`**.

| # | What | CLI | VS Code (PlatformIO sidebar → Project Tasks → `savx`) |
|---|---|---|---|
| 1 | Build | `pio run -e savx` | General → **Build** |
| 2 | Flash the firmware (COM port) | `pio run -e savx -t upload` | General → **Upload** |
| 3 | Build the local-page image | `pio run -e savx -t buildfs` | Platform → **Build Filesystem Image** |
| 4 | Flash the local page into LittleFS | `pio run -e savx -t uploadfs` | Platform → **Upload Filesystem Image** |
| 5 | Serial console (115200, crash decoder) | `pio device monitor -e savx` (add `--echo` to see what you type) | General → **Monitor** |
| 6 | Read the whole filesystem back | `pio run -e savx -t download_fs` → `firmware/unpacked_fs/` | Platform → **Download and extract filesystem from device** |
| 7 | Erase the whole flash (last resort, §13.6) | `pio run -e savx -t erase` | Platform → **Erase Flash** |
| 8 | Host unit tests (no board) | `pio test -e native` | Advanced → **Test** (env `native`) |

**First flash of a new board:** run #2, then #4. The order does not matter.

Expected end of an upload: esptool connects (`Connecting...`), writes `firmware.bin` at `0x10000`, then `Hard resetting via RTS pin...`. If it cannot connect, put the chip into download mode by hand: **hold BOOT, tap RST, release BOOT**, then upload again.

> ⚠️ **`uploadfs` replaces the whole LittleFS partition**, not only the web page. That partition also holds the minute queue (`/q/…`, including records **not yet uploaded**), today's minute file (from which today's totals are rebuilt after a reboot), the probe report and the cached `alerts.json`. Run `uploadfs` only on a new board, or when the upload backlog is 0 (`cloud` command or the PWA's "Upload queue") and preferably just after midnight. Firmware uploads (`upload`, OTA) never touch LittleFS or the settings.

**Opening the monitor** may reset the board through the bridge's DTR/RTS lines [INFERRED]; that is harmless. Exit with **Ctrl+C**. If the local page shows only "The local UI is not in flash yet", you skipped #4.

---

## 5. First boot and the setup portal

### 5.1 The boot log

Open the monitor (§4 #5) and press **RST**. Example (values differ):

```
=== savx-logger 0.6.1  (Savx F4 3KW logger, read-only) ===
boot #1 reset=POWERON bootFails=0
PSRAM 83xxxxx bytes, flash 16777216 bytes
Setup Wi-Fi AP: SavxLogger-A1B2  password: k7mpq2xw9d
Local admin password: same as the AP password (change it!)
[prov] no Wi-Fi credentials saved: opening the setup portal
[prov] portal open: join Wi-Fi 'SavxLogger-A1B2' (password printed at boot), then open http://192.168.4.1
[inv] link NOT armed (GPIO17 high-Z), mode rs232lite, RO=GPIO18 DI=GPIO16 (self-ID pending)
[agg] rules: 0 loaded from flash (0 enabled), e-mail off
[up] cloud OFF, ntfy off, telegram off, healthchecks off, mailer nudge off
Type 'help' for console commands.
```

Check: PSRAM about 8 MB (0 means the wrong board type), flash 16 MB, `read-only` in the banner, and **write down the AP name and password** (build record site-survey B1).

- The AP name is `SavxLogger-` plus the last two MAC bytes in hex.
- The AP password is 10 random characters (no 0/O/1/l/i), generated on the first boot and stored in NVS (`sys/apPass`). It stays the same on every boot, so if you missed it, open the monitor and press RST again. It changes only after an NVS erase (§13.5).
- The **local admin password starts equal to the AP password**. Change it in the portal (§5.3) or later with the console command `adminpw <new>`.
- The first lines of `setup()` put GPIO17 into high-Z and both module header pins into pull-up, and drive the DI pin HIGH as soon as a stored self-ID is read, before the LittleFS mount and Wi-Fi ([03](03-hardware-and-wiring.md) Appendix A, S4).
- On an **armed** logger, `invTask` repeats the GPIO17 part of the pre-arm check at every boot (§8.4). If it fails you see `[diag] Boot: TX NOT attached this boot. GPIO17 reads HIGH with its pull-down …` and a `LINK_LOST` alert (severity 4); the logger stays silent towards the inverter for that boot.

### 5.2 Join the portal from the iPhone

The portal opens **only** when no Wi-Fi is saved (first boot, or after an NVS/Wi-Fi erase), or on demand: hold **BOOT for 3 s** while the logger runs, or type `portal` in the console (both: 10 min, then it closes by itself). It never opens just because the router is down, so logging continues through load-shedding. While the portal is open, the LED blinks blue slowly and the local page `http://solar.local` is off (the portal needs port 80).

1. iPhone → **Settings → Wi-Fi** → join **`SavxLogger-XXXX`** with the AP password.
2. iOS usually opens the captive "Log In" sheet by itself. If not, open Safari and go to **`http://192.168.4.1`**. If iOS says the network has no internet, stay connected to it [INFERRED, normal for setup access points].
3. Tap **Configure WiFi**. The page lists nearby networks, then the custom fields.

The portal menu also has **Info** and **Exit**. The firmware hides WiFiManager's own **Update** entry and answers its `/update` and `/u` routes with HTTP 403, because that uploader has no password and an on-demand portal is also reachable from the home LAN. Firmware is only replaced through `http://solar.local/update` (§12), which checks the admin password.

### 5.3 Every portal field

ESP32-S3 Wi-Fi is **2.4 GHz only**: pick a 2.4 GHz SSID (on an iPhone hotspot turn on *Maximize Compatibility*).

**Secret fields** (marked *secret*) always show empty: **leave a secret blank to keep the stored value**; type a single **`-`** to clear any field. Non-secret fields show their current value.

| Portal field (label on the page) | Stored in NVS | Fill in | When |
|---|---|---|---|
| SSID (tap a network) and password | ESP-IDF Wi-Fi storage (not our namespaces) | Your 2.4 GHz network | First boot; leave both empty later to keep Wi-Fi |
| Admin password for the local UI / OTA (min 6 chars; blank = keep) *secret* | `sys/adminHash` (SHA-256 only) | A new password ≥ 6 characters. Shorter values are ignored | First boot |
| Firebase Web API key | `cloud/apiKey` | The **device key** `savx-device` ([06](06-cloud-and-dashboard-guide.md) §2.6), not the browser key | Cloud set-up (M5) |
| Firebase RTDB URL (https://...asia-southeast1.firebasedatabase.app) | `cloud/dbUrl` | `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app` (no path). Only `*.firebasedatabase.app` / `*.firebaseio.com` hosts are accepted | M5 |
| Firebase device user e-mail | `cloud/email` | The device Auth user, e.g. `inv1@…` | M5 |
| Firebase device user password (blank = keep) *secret* | `cloud/pass` | Its password | M5 |
| ntfy topic, >= 24 random chars (blank = keep) *secret* | `alert/ntfyTopic` | Your ntfy topic ([06](06-cloud-and-dashboard-guide.md) §8.1) | Alerts (M7) |
| Telegram bot token (optional; blank = keep) *secret* | `alert/tgToken` | From @BotFather; used for severity ≥ 4 only | Optional |
| Telegram chat id (optional) | `alert/tgChat` | Numeric chat id | Optional |
| Healthchecks ping URL or UUID (optional) | `alert/hcUrl` | The check's UUID or `https://hc-ping.com/<uuid>`; other hosts are ignored | M7 |
| Dashboard URL for notification links (optional) | `alert/appUrl` | e.g. `https://solar.<acct>.workers.dev/#/alerts` (tap-through link in ntfy) | Optional |
| Mailer nudge URL incl. ?k= (optional; blank = keep) *secret* | `mail/nudge` | `https://script.google.com/macros/s/<id>/exec?k=<NUDGE_KEY>` (r8 v1.1). Blank = the mailer only polls every 5 min | Optional |

Then press **Save**:

- On first boot the logger connects to your Wi-Fi and the portal closes. The serial log shows `[prov] portal fields saved to NVS`, then `[web] http://solar.local  (http://192.168.x.y)` once mDNS starts. If the connection fails the portal stays open: check the password and save again.
- Saving with the SSID left empty keeps the current Wi-Fi and only stores the other fields (the page says "Saved"); then tap **Exit** or let the 10 min run out.
- Every save of the portal re-writes the non-secret fields (they come back pre-filled) and clears the stored refresh token (`cloud/rtok`), so the logger signs in afresh once. Any channel change is picked up by the uplink without a reboot.
- The pre-filled values are re-read from NVS each time the portal opens, so a *Healthchecks* or *Dashboard URL* change made with `POST /api/config` (§6.5) is shown and kept.

**Forget only the Wi-Fi network:** portal → **Info** → **Erase WiFi config** (WiFiManager's own button; it reboots, and the portal then opens because no Wi-Fi is saved). [From the WiFiManager 2.0.17 source; not tried on this board.]

> ⚠️ NVS is **not encrypted** ([d1](../research/d1-architecture.md) §5.13): anyone holding the board can read the Firebase device password, the refresh token and the channel secrets. The database rules limit what the device account can do ([firebase/README.md](../firebase/README.md)). If the board is ever lost, change the device user's password in the Firebase console.

---

## 6. The local web page (`http://solar.local`)

### 6.1 Opening it

- Same Wi-Fi as the logger, then **`http://solar.local`** (mDNS host `solar`). It works without internet.
- **IP fallback:** the serial log prints `[web] http://solar.local  (http://<ip>)`; the PWA's **Settings → Actions** card links the IP the logger last reported; the router's client list shows the host name `solar`. A DHCP reservation keeps the address fixed.
- **iPhone notes** [INFERRED]: `.local` names resolve through Bonjour only on the same network, so it fails on mobile data, on guest networks and on routers with client isolation. If Safari shows a "not secure" warning for plain `http://`, continue to the site: the page never leaves your LAN. The HTTPS dashboard can only *link* to this page (mixed-content rules forbid it from calling `http://` APIs).
- From the Mac: `curl -s http://solar.local/api/status | python3 -m json.tool`.

### 6.2 Tabs

| Tab | Shows / does |
|---|---|
| **Live** | Tiles: PV, Load, Battery (SOC, V), Battery power, Grid, Output, Heat sink, Upload queue; today's kWh (logger-integrated; grid is an estimate); decoded warnings and faults. Red banner when there is no fresh inverter data. Refreshes every 5 s |
| **24 h** | Power and battery charts from the RAM ring (5 s samples, **cleared on reboot**); 6 h / 12 h / 24 h buttons |
| **History** | Pick a date → **Load day**: minute records still stored on the logger, how many are uploaded, the day's kWh; chips list the stored days |
| **Diagnostics** | Admin password box, link status, the four wiring steps (§7–§8), probe log, **Disarm / reset link**, **Reset incl. self-ID** |
| **Raw** | Last reply per command (**Refresh**) and the identity (`meta`) JSON |
| **Settings** | Alert thresholds form (**Save**), **Admin password → Change**, System JSON, **Firmware update (OTA)** link, **Reboot** |

### 6.3 Admin password

Routes that change something, plus `/api/diag/*`, `/api/config` and `/update`, need HTTP Basic auth with user **`admin`** and the admin password. On the page: **Diagnostics → Admin** box → type the password → **Use** (kept in this tab's session storage only). A red "password needed / wrong" means it was refused. Read-only routes (`/api/live`, `/api/status`, `/api/raw`, …) need no password.

### 6.4 JSON API

| Route | Auth | Returns / does |
|---|---|---|
| `GET /api/live` | – | Latest sample (the `live` keys incl. `gw`) + `age_ms` |
| `GET /api/status` | – | Link, engine, system, uplink (`upState`, `upErr`, `hs`, `rq`, `qg`, `backlog`, `edrop`, `notify{…}`) and `rules{…}` |
| `GET /api/identity` | – | `meta` (QPI, QVFW, ratings, dialect …) |
| `GET /api/raw` | – | Last raw reply per command + recent exchanges (hex and text) |
| `GET /api/ring?min=1440&step=60` | – | Chart data from the RAM ring (`min` 5–1440, `step` 5–3600 s) |
| `GET /api/hist?date=YYYY-MM-DD` (alias `/api/minutes`) | – | That day's minute records from flash |
| `GET /api/days` | – | Stored days, record counts, upload cursors |
| `GET /api/probe` / `POST /api/probe` | – / admin | Last probe report / re-run the probe (armed only) |
| `GET /api/diag/pins` | admin | Self-ID, every idle/upper result (`rj`, `ref` `gnd`/`3v3`, `pctLow`, `edges`, `verdict`), `arm{ready, txRj, rxRj, upperPin, reason}`, loopback |
| `GET\|POST /api/diag/selftest` | admin | Module self-ID (0.5 s) |
| `GET\|POST /api/diag/idle?pin=1\|2[&ref=3v3]` | admin | 10 s idle-level test; `ref=3v3` = upper check (§8.2) |
| `POST /api/diag/loopback?bench=1` | admin | Bench UART loopback |
| `POST /api/link/arm` with `pin=1\|2&confirm=ARM` (or `mode=max3232&confirm=ARM-MAX3232`) | admin | Arm the link |
| `POST /api/link/reset` (`all=1` also forgets the self-ID) | admin | Disarm (GPIO17 high-Z) |
| `POST /api/capture` with `on=1\|0` | admin | Raw capture on/off |
| `GET /api/config` | admin | Thresholds, which secrets are set (`cloudSet`, `ntfySet`, `telegramSet`, `hcSet`, `nudgeSet`), `appUrl`, `adminDefault` |
| `POST /api/config` | admin | Thresholds (same ranges as the RTDB rules), `adminpw`, and the write-only `ntfyTopic`, `hcUrl`, `appUrl`, `nudge` (`-` clears) |
| `POST /api/reboot`, `POST /api/safemode/exit` | admin | Reboot / leave safe mode |
| `GET /update` | admin | ElegantOTA page (§12) |

### 6.5 Setting channels without the portal

The page has no form for them, but `POST /api/config` accepts them. This keeps the secret out of your shell history:

```zsh
read -rs "PW?Admin password: "; echo
read -rs "T?ntfy topic: "; echo
curl -s -u "admin:$PW" -X POST http://solar.local/api/config --data-urlencode "ntfyTopic=$T"
unset PW T
```

Use `hcUrl=…`, `appUrl=…` or `nudge=…` the same way. Expect `{"ok":true,"msg":"saved"}`. The uplink reloads the channels at once. Firebase and Telegram credentials can only be set in the portal.

---

## 7. Bench self-tests (Stage B)

Inverter **not** connected. Physical steps and the jumper names (P1–P4, T1, T2) are in [03](03-hardware-and-wiring.md) §8.3; this section gives the firmware side. On the page you first enter the admin password (**Diagnostics → Admin → Use**).

### 7.1 B1: UART loopback (optional, and only first)

1. Do this **before** the module is wired (B2). Only a wire between **GPIO17** and **GPIO18**; the module and the inverter are **disconnected**.
2. Page: **Diagnostics → Step 1 → "Optional bench UART loopback"** → tick *GPIO17-GPIO18 jumper fitted, nothing else on those pins* → **Run loopback**. Serial: `loopback yes` (without `yes` it only prints the warning).
3. Expected: `Loopback: inverted byte-exact, non-inverted byte-exact`. The test transmits the whitelisted QPI frame `51 50 49 BE AC 0D` for under a second, once with inversion and once without.
4. **Remove the wire.**

It is refused while the link is armed, and also as soon as a self-ID or any idle result is stored: "Loopback refused: the module (and maybe the RJ45) is already wired - loopback is the bench test before B2. …" (a GPIO17–GPIO18 jumper would then make GPIO17 fight the module's RO output). To repeat it later: remove the module wires from GPIO16/18, run `reset-link all`, then retry.

### 7.2 B2/B3: module self-ID

Wiring P1–P4 only (module VCC → **3V3**, never 5 V); screw terminals empty.

1. **Diagnostics → Step 1 → Run self-ID** (serial `selftest`). Expected verdict **`open-ok`**: "Both header pins HIGH (A/B open = fail-safe idle). Now jumper A+ -> ESP GND and B- -> ESP 3V3, then run self-ID again."
2. USB out, fit **T1** (A+ → G) and **T2** (B− → 3V3), USB in.
3. **Run self-ID** again. Verdicts:

   | Verdict | Meaning | Action |
   |---|---|---|
   | `ro-found` "RO = GPIO18 … DI = GPIO16" | Labels straight-through (expected) | Stored (`link/roPin`, `diPin`, `diOk`). Step 1 turns green |
   | `ro-found` "RO = GPIO16 … DI = GPIO18" | Labels crossed | Stored; the firmware swaps roles, no rewiring |
   | `open-ok` | Nothing reacted to T1/T2 | Check T1 is on **A+**, T2 on **B−**, VCC really on 3V3; repeat |
   | `both-low` | Short between the GPIO16 and GPIO18 wraps, or no module power | Unpower, separate the wraps ([03](03-hardware-and-wiring.md) §6.3), repeat |
   | `noisy` | Pins toggle: loose wrap or floating A/B | Re-wrap, repeat |

4. USB out, **remove T1 and T2 completely**, tape the bare pins ([03](03-hardware-and-wiring.md) N9).

After a successful self-ID the firmware drives the DI pin **HIGH** permanently (the module's transmitter stays off). Self-ID is refused while the link is armed. The result is visible in `GET /api/diag/pins` (`selfIdDone: true`).

---

## 8. The inverter procedure (Stages C, C2, D, E)

Read [03](03-hardware-and-wiring.md) §8.2 (rules for every stage), §8.4–§8.5 (wiring for each stage) and §12 (pre-power checklist) first. Power the ESP32 from the charger in an AC-OUT socket, or from the Mac **on battery**. Plug the USB in first, wait for the boot, then plug the RJ45 into **COM** and watch the LCD for 10 s. The module self-ID (§7.2) must be stored before any of this.

```
 Stage C (receive only)     Stage C2 (receive only)          Stage D (two-way)                    Stage E
 A+ ── pin 1, then pin 2    A+ ── RX candidate               A+ ── inverter TX (tested LOW)       dialect, pv12
 B− ── pin 8 + P5 (to G)    B− ── T3 jumper to ESP 3V3       B− ── pin 8 + P5 (T3 removed)        decided and
 GPIO17 ── NOTHING          pin 8 + P5 out of B−, on G       GPIO17 ── inverter RX                stored
                            GPIO17 ── NOTHING                         (HIGH, then below-3V3 ×2)   automatically
      │                            │                                 │
 idle 1 ×2, idle 2 ×1–2 ──► idle <RX> 3v3 ×2 ──────────────► arm <RX> YES ─► pre-arm check ─► probe (auto)
```

### 8.1 Stage C: idle-level test (B− on pin 8 = GND)

1. Wiring: P5 (B− → G), RJ45 pin 8 → B−, **pin 1** → A+; pin 2's wire taped; **GPIO17 not wired**.
2. Page: **Diagnostics → Step 2 → Test pin 1 (10 s)** (serial `idle 1`; API `POST /api/diag/idle?pin=1`). A progress bar runs for 10 s; the table lists each run with **Pin**, **B-** (`GND` here), **% LOW**, **Edges** and **Verdict**. Serial example:
   ```
   [diag] Idle test RJ45 pin 1 (B- on pin 8/GND): 100.0% LOW, 0 edges in 1000 samples -> this pin idles NEGATIVE = inverter TX
   ```
3. Run **Test pin 1** a second time.
4. RJ45 out, USB out; move **pin 2** onto A+ (tape pin 1's end); USB in, RJ45 in.
5. **Test pin 2 (10 s)** (`idle 2`), once or twice.

### 8.2 Stage C2: upper check of the receive pin (B− on 3V3)

A `high-not-TX` result only proves that a wire is *not negative*; a wire at a **positive** voltage (for example pin 4 picked up through a colour mix-up) reads the same and would destroy GPIO17. The upper check compares the receive candidate with 3.3 V instead of ground. Arming needs **two** `below-3V3` results on that pin ([03](03-hardware-and-wiring.md) §8.4b, Appendix A issue S1).

1. RJ45 out, USB out. Take the **pin-8 conductor together with the P5 jumper** out of B− as one piece (they stay joined and on ESP **G**); tape the bare pin. B− must now be empty.
2. Fit the **T3** jumper **B− → ESP 3V3**. ⚠️ Never with pin 8 or P5 still in B−: B− on 3V3 and GND at once shorts the ESP32's supply ([03](03-hardware-and-wiring.md) N9).
3. Put the **receive candidate** (the pin that read `high-not-TX`) on A+. USB in. **If the board does not boot, unplug at once.** Then RJ45 in.
4. Page: **Diagnostics → Step 2 → "2b - Upper check of the RX pin" → Upper check pin N** (serial **`idle N 3v3`**; API `POST /api/diag/idle?pin=N&ref=3v3`). Run it **twice**. The table shows `3V3` in the **B-** column. Serial example:
   ```
   [diag] Upper check RJ45 pin 2 (B- on 3V3): 100.0% LOW, 0 edges in 1000 samples -> upper check: below 3.3 V = safe for GPIO17
   ```
5. **Undo:** RJ45 out, USB out, remove T3 **completely**, put the inverter **transmit** wire back on A+ and the **pin 8 + P5 pair** back into B−.

**All verdicts** (sampling every 10 ms for 10 s; "steady" = < 3 edges):

| Verdict | Test | Rule | Meaning |
|---|---|---|---|
| `inverter-TX` | idle (GND) | ≥ 99 % LOW, steady | This pin idles negative: the inverter's **transmit** wire |
| `high-not-TX` | idle (GND) | ≤ 1 % LOW, steady | Not negative: receive input, open wire **or a positive pin** (→ upper check) |
| `below-3V3` | upper (3V3) | ≥ 99 % LOW, steady | Below about 3.3 V: safe for GPIO17 |
| `ABOVE-3V3` | upper (3V3) | ≤ 1 % LOW, steady | A **positive source**. **STOP: never connect GPIO17 to it.** Re-check the colours ([03](03-hardware-and-wiring.md) §7), then `reset-link` and redo Stage C |
| `traffic` | either | ≥ 10 edges | Something is transmitting unsolicited (a vendor dongle on COM?) |
| `undecided` | either | anything else | Loose joint or floating input: tug test and repeat |

**When arming becomes possible.** Step 3 then says "Inverter TX = RJ45 pin X: module A+ there, B- back on pin 8 + P5 (3V3 jumper removed). Now wire GPIO17 to RJ45 pin Y. Arming first re-checks GPIO17 (must read LOW)." All of these must hold ([`PinDiag::armCheck`](../firmware/src/link/PinDiag.cpp)):

1. The **two most recent** idle (GND) results of one pin are `inverter-TX` (only one pin may qualify).
2. The most recent idle result of the other pin is `high-not-TX`, and that pin has **never** read `inverter-TX` since the last `reset-link`.
3. That pin has **two** `below-3V3` upper checks and has **never** read `ABOVE-3V3` since the last `reset-link`.

Otherwise the page (and `GET /api/diag/pins` → `arm.reason`) says what is missing, for example "no pin has 2 consecutive 'inverter-TX' idle results yet …", "pin 2 must also be tested and read HIGH (not TX) before arming", "inverter TX = pin 1. Next: upper check on pin 2 twice …" (the 2b box then opens by itself), "STOP: pin 2 read ABOVE 3.3 V in an upper check …", "pin 2 read 'inverter-TX' in an earlier test: the wiring changed between tests …", or "both pins idle LOW - impossible for RS232: check the wiring (B- on pin 8?)". The last 4 idle and last 2 upper results per pin survive reboots (`link/idle`). Record every run in site-survey B4/B5. If neither pin reads LOW, follow [03](03-hardware-and-wiring.md) §9.1 and **do not connect GPIO17**.

### 8.3 Stage D: wire GPIO17, then arm

1. RJ45 out, USB out. A+ on the **inverter TX** wire (tested LOW), the pin 8 + P5 pair in B−, T3 gone. Make **P6**: GPIO17 → the receive wire (tested HIGH and `below-3V3` twice) ([03](03-hardware-and-wiring.md) §8.5). Inspect, photograph.
2. USB in, wait for the boot, RJ45 in, watch the LCD 10 s.
3. Page: **Diagnostics → Step 3 – Connect GPIO17 and arm**. Tick all four boxes:
   - *Module A+ is on the inverter TX pin, B- on pin 8 + P5 again (the 3V3 jumper is removed)*
   - *GPIO17 is wired to RJ45 pin Y (inverter RX) and to nothing else*
   - *Pin 4 and the unused conductors are cut and taped; BMS cable untouched*
   - *The Mac/laptop is unplugged or on battery (no ground loop)*
4. **Arm link (enable TX)** → confirm the dialog. Serial instead: **`arm <Y> YES`**, where `<Y>` is the RJ45 pin GPIO17 is on (e.g. `arm 2 YES`). API: `POST /api/link/arm` with `pin=<Y>&confirm=ARM`.
5. The firmware now runs the **pre-arm check** (§8.4) and only then attaches TX. Expected message: `ARMED: GPIO17 -> RJ45 pin Y (inverter RX). Read-only probe starts now (about 1-2 min).`

Refusals you may see:

| Message | Cause |
|---|---|
| `Arm refused: module self-ID (Stage B3) not done - the module's DI pin is not held HIGH yet.` | §7.2 first |
| `Arm refused: <reason from §8.2>` | Idle or upper results incomplete or failed |
| `Arm refused: refused: GPIO17 must go to RJ45 pin Y (inverter RX); pin X is the inverter TX` | You named the transmit pin |
| `Arm needs confirmation: …` | Serial command without `YES` |
| `Arm refused: GPIO17 reads HIGH with its pull-down …` / `Arm refused: RO (GPIO18) is not steady LOW …` | Pre-arm check failed (§8.4) |
| `Arm refused: safe mode (no inverter TX).` | §13 |

The arm state is stored (`link/armed`, `armRx`, `invTxRj`) and survives reboots. **Never force it**; fix what the message names.

**MAX3232 module instead of the HW-0519** (optional, [03](03-hardware-and-wiring.md) §9.4): the MAX3232 **replaces** the RS485 module. Remove the HW-0519, run `reset-link all`, then `arm-max3232 YES` (or `POST /api/link/arm` with `mode=max3232&confirm=ARM-MAX3232`). It is refused while an HW-0519 self-ID is stored, and it runs the GPIO17 part of the pre-arm check ("… is GPIO17 on its R-OUT instead of T-IN?"). It uses a non-inverted UART on GPIO18 (RX) / GPIO17 (TX).

### 8.4 Pre-arm check, boot check and disarming

**Pre-arm check** (receive-only, 200 ms, `PREARM_SAMPLE_MS`), run on every arm request after the gates of §8.2 pass:

- GPIO17 with only its internal pull-down must read **LOW**. HIGH means the GPIO17 wrap touches the GPIO16 (DI) wrap, or the wire is positive: "GPIO17 reads HIGH with its pull-down (n/m): it touches the GPIO16 (DI) wrap or sits on a POSITIVE pin. Unplug the RJ45 and USB, separate the wraps, re-check the colours."
- The module's receiver output (RO) must read **LOW**, i.e. A+ is on the idling transmit wire: "RO (GPIO18) is not steady LOW (…): module A+ is not on the idling inverter TX pin, the inverter is off, or the GPIO16/GPIO18 wraps touch. Unplug and check."

GPIO17 returns to high-Z right after the check.

**Boot check.** An armed logger repeats the GPIO17 part at every boot. If it fails, TX stays detached for that boot, the log says `Boot: TX NOT attached this boot. …`, and a `LINK_LOST` alert (severity 4) asks you to unplug the COM cable and inspect the GPIO16/17 wraps. RO is not checked at boot (the inverter may be off).

**Disarm at any time:** **Diagnostics → Disarm / reset link** (or **Reset incl. self-ID**), serial `reset-link` / `reset-link all`, API `POST /api/link/reset[?all=1]`. GPIO17 goes back to high-Z at once, and the idle and upper results (including the sticky "ever TX" / "ever ABOVE-3V3" marks) are forgotten. ⚠️ **Always disarm before any rewiring**: the stored arm state would otherwise re-arm at the next boot, and the boot check only catches a HIGH GPIO17 ([03](03-hardware-and-wiring.md) Appendix A, R7).

### 8.5 The probe (Stage D/E)

The probe starts **by itself** after arming. Re-run it with **Step 4 → Re-run probe**, serial `probe`, `POST /api/probe`, or the PWA's **Settings → Re-probe inverter**. It is refused while the link is not armed. It takes 1–2 minutes; the page shows the summary in green (ok) or red, and the step-by-step log below it.

What it does (read-only, every frame whitelisted; [d1](../research/d1-architecture.md) §3.5):

1. For each serial profile, first **listens 3 s**. If another master is talking (a WiFi collector), it stops: "another master is talking on COM (WiFi collector?) - listen-only".
2. Profiles in order: P1 2400 baud RX+TX inverted (the expected one), P2–P5 the other polarity/baud combinations (`rs232lite-2400-ii-tx1` means RS232-lite, 2400 baud, RX inverted, TX inverted, inverter TX on RJ45 pin 1).
3. PI30 family first: `QPI`, then `QPIGS` with the plausibility gates, then `QMOD`, `QPIRI`, `QVFW`, `QVFW2`, `QPIWS`, `QALL` (REVO dialect, CHK or CRC framing), and the optional queries (`QFLAG`, `QID`/`QSID`, `QMN`/`QGMN`, `QDI`, `QET`, `QT`, `QPIGS2`, `Q1`, `QLITH0`, and `PQSE`/`PQSA` once the dialect is REVO; a NAK only marks one unsupported). If that fails: PI18 detection (`^P005PI`), legacy Q1/Q6, and as a last resort Modbus function 03 only. Every step is in the probe log.
4. On success the profile, adapter, dialect, `pv12` decision and supported-command masks are stored (`link/*`) and the engine goes to **RUN**. The next boot does a short "light probe" with the stored profile instead of the full one.

Typical summaries: `PI30 on rs232lite-2400-ii-tx1: …`, `legacy Q6 on …`, `PI18 detected on … (no decoder in v1): raw capture only`, `no protocol found: listen-only, re-probe in 6 h (check wiring/polarity)`. After 60 failed fast polls (~5 min) in RUN the firmware re-probes in the background with back-off (1, 5, 15, 60 min).

**Cross-check against the LCD** exactly as in [07](07-milestones-and-test-plan.md) M3 (output V ±2 V, battery V ±0.3 V, QPIRI 3200 VA / 24.0 V, QVFW/QVFW2 = U1/U2). Use **Raw** for the replies and **Live** for the decoded values. If anything on the inverter changes (new warning, fault, beeping): **pull the RJ45 at once** and stop.

---

## 9. Probe report and raw capture

### 9.1 Save the probe report

The last report is in RAM and in `/log/probe.json`. From the Mac on the same Wi-Fi:

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
curl -s http://solar.local/api/probe | python3 -m json.tool > docs/probe-report.json
```

Fields: `ts`, `ok`, `kind` (`pi30`, `q6`, `smg`, `powmr`, `raw`), `family`, `dialect` (`STOCK17`, `STOCK21`, `MAX24`, `GK`, `REVO`), `code` (`ok`, `silence`, `garbage`, `nak-only`, `no-valid-data`, `foreign-master`, `not-armed` …), `profile`, `foreignMaster`, `pi18`, `durationMs`, `summary`, `detail`, `log`. The report contains no secrets.

### 9.2 Recent exchanges

```zsh
curl -s http://solar.local/api/raw | python3 -m json.tool > raw-$(date +%Y%m%d-%H%M).json
```

`last` holds the newest reply per command (text, escaped); `recent` holds the latest exchanges with the transmitted frame and the reply in hex and text. The uplink also copies the last replies to RTDB `diag/raw` every 10 minutes.

### 9.3 Raw capture to flash

1. **Diagnostics → Step 4 → Raw capture on** (serial `capture on`, API `POST /api/capture` with `on=1`). Every exchange is appended to `/log/raw-0.txt` as `<uptime-ms> <command> <result> <reply text>`.
2. Let it run (about 200 fast-poll cycles ≈ 17 min at 5 s, [07](07-milestones-and-test-plan.md) M3 step 8), then **Raw capture off**. Capture also stops on reboot, and the file stops growing at 256 KB.
3. `ls` on the console lists `/log/raw-0.txt` and its size.
4. To copy it to the Mac (no HTTP download exists): logger on the bench, USB to COM, then
   ```zsh
   pio run -e savx -t download_fs
   ls unpacked_fs/log/          # raw-0.txt, probe.json
   ```
   This reads the LittleFS partition over USB into `firmware/unpacked_fs/` (a pioarduino platform target; not yet tried on this board). It does not change the device. `firmware/unpacked_fs/` is in the root `.gitignore`, so minute data and captures never end up in git; still move the files you need out of it and delete the folder when done.

---

## 10. Day-to-day: LED, serial console, status

### 10.1 RGB LED

On many clones the LED stays dark until its solder bridge is closed; `/api/status` is the real indicator. If yours is on GPIO38 (DevKitC-1 v1.1), build with `-DLED_PIN=38` (§14.3).

| Pattern | Meaning |
|---|---|
| Solid red | **Safe mode** |
| Slow blue blink | Booting, or the setup portal is open |
| Fast magenta blink | Probing the inverter |
| Slow red blink | Link armed and running, but no valid reply (link lost) |
| Solid orange | Wi-Fi down |
| Cyan flicker | Back-filling the upload backlog |
| Off with a short green blink per valid reply | Normal |

### 10.2 Serial console (115200 baud)

Type a command and press Enter (§4 #5). Everything is receive-only unless noted.

| Command | Does |
|---|---|
| `help` | List the commands |
| `status` | Firmware, boot, reset reason, Wi-Fi/IP/RSSI, portal, time and its source (`ntp`, `inverter`, `none`), link state, RO/DI pins, counters, latest idle and upper-check result per pin, probe summary, latest sample, queue, heap/PSRAM |
| `live` | Latest sample as JSON |
| `selftest` | Module self-ID (§7.2) |
| `idle 1` / `idle 2` | 10 s idle-level test, A+ on RJ45 pin 1 or 2, B− on pin 8 (§8.1) |
| `idle 1 3v3` / `idle 2 3v3` | 10 s **upper check** of the receive candidate, B− on ESP 3V3, pin 8 + P5 out of B− (§8.2) |
| `loopback yes` | **Bench only, before B2**: GPIO17–GPIO18 jumper, module and inverter disconnected (transmits QPI) |
| `arm <pin> YES` | Arm the link: GPIO17 is wired to RJ45 `<pin>` (inverter RX); needs self-ID, idle and upper results, then runs the pre-arm check |
| `arm-max3232 YES` | Arm with a MAX3232 module that replaces the HW-0519 (refused while an HW-0519 self-ID is stored) |
| `probe` | Re-run the probe (armed only) |
| `raw` | Last raw reply per command |
| `reset-link [all]` | Disarm (GPIO17 high-Z), forget probe/idle results (`all`: also the self-ID) |
| `capture on` / `capture off` | Raw capture to `/log/raw-0.txt` |
| `portal` | Open the setup portal for 10 min |
| `adminpw <new>` | Set the admin password (≥ 6 characters) |
| `cfg` | Current alert thresholds |
| `cloud` | Uplink state, auth, handshakes/requests today, quota level, backlog, pending/dropped events, channel on/off, last error |
| `rules` | Rules revision, counts, e-mail on/off, mail jobs today (of 40), per-slot state and e-mails today |
| `test-alert` | Raise `TEST_ALERT` (ntfy push; plus an e-mail job when alert e-mails are on) |
| `ls` | List the LittleFS files (`/`, `/www`, `/q`, `/log`, `/cfg`) |
| `safe-exit` | Leave safe mode and reboot |
| `reboot` | Reboot |

### 10.3 Engine and uplink states

| `engine` (status) | Meaning |
|---|---|
| `disarmed` | Not armed: listen-only, nothing is ever sent, GPIO17 high-Z |
| `light-probe` | Re-checking the stored profile after a boot |
| `probing` | Full probe running |
| `run` | Polling normally (QPIGS/QALL every 5 s, QMOD/QPIWS every 10 s, ratings every 10 min, energy every 5 min, identity hourly) |
| `raw` | Nothing usable found (or another master/PI18): listen-only, re-probe after 6 h |
| `safe` | Safe mode: no TX at all |

| `upState` / `cloud` | Meaning |
|---|---|
| `offline` | No Wi-Fi |
| `no-cloud` | Wi-Fi up, Firebase not configured (ntfy/Healthchecks still work) |
| `auth` | Signing in, or sign-in refused (see `upErr`) |
| `backoff` | Waiting after an error (5 s doubling to 5 min) |
| `ready` | Uploading |

---

## 11. Cloud, alerts and rules on the device

Set up the cloud side first ([06](06-cloud-and-dashboard-guide.md) §2–§3), then enter the four Firebase values in the portal (§5.3). Within a minute `cloud` should show `uplink ready … auth ok` and `devices/inv1/live` starts updating every 10 s.

What the uplink does (details in [`firmware/README.md`](../firmware/README.md) §4):

- Signs in to Identity Toolkit as the **device** user with the device key, keeps the refresh token in NVS, refreshes the 1-hour ID token 5 min before expiry. Wrong credentials pause sign-in for 30 min; saving the portal again ends the pause at once.
- Per 1 s tick, at most 3 database requests, in this order: events (with their e-mail jobs) → `live` (every `cfg.liveSec`, default 10 s) → minute record PATCH (with `daily`, `status`, `h15`, `alertState`) → back-fill (≤ 60 records per request, ≥ 2 s apart) → `cfg` poll every 60 s (and `alerts` when `cfg/alertsRev` changes) → housekeeping (meta, final day totals, `diag/raw` every 10 min, pruning of old `hist`/`events` after 00:05).
- **Offline:** minute records stay in flash (about 40 days of space) and are back-filled in order; alerts wait in RAM (24 entries); pushes delivered late say "(14:02, delayed 37 min)".
- **Quota guard:** more than 1,500 TLS handshakes or 60,000 requests (0.7.2; was 20,000) in a day → level 1 (`QUOTA_GUARD` event, `live` slowed to ≥ 60 s); twice that → level 2 (`live` paused, `cfg` every 5 min).
- **Network watchdog:** Wi-Fi up but no database answer for 15 min → drop TLS and sign in again; 60 min → reboot, but only if a TLS connection worked in that hour and at most once per 6 h (so an ISP outage never causes reboot loops).

**Alert channels** (no Firebase needed): ntfy for every pushed alert, Telegram additionally for severity ≥ 4, a Healthchecks ping every 5 min (`/fail` while the link is armed but no valid reply came for 5 min). Quiet hours suppress pushes of severity ≤ 3 (still logged); at most 40 pushes a day (above that, severity 5 only). `test-alert` and the dashboard's **Send test alert** ignore quiet hours.

**Built-in alerts and their debounce** (`src/agg/AlertEngine.cpp`; thresholds from `cfg`, §14.5):

| Type | Severity (open / clear) | Raised when | Held for | Clears after |
|---|---|---|---|---|
| `INVERTER_FAULT` | 5 | Mode `F`, a fault-class QPIWS bit or a fault code | 2 consecutive polls | 60 s without |
| `BATT_CRITICAL` | 5 | SOC ≤ 20 %, battery ≤ 21.5 V, or QPIWS battery-low/under-shutdown | 30 s | 5 min with SOC ≥ 25 % and ≥ 22.5 V |
| `BATT_LOW` | 4 | SOC ≤ `socLow` or V ≤ `bvLow` while discharging | 60 s | 5 min |
| `BATT_HIGH` | 4 | QPIWS battery-high or V ≥ `bvHigh` | 30 s | 5 min below `bvHigh` − 0.5 V |
| `GRID_LOSS` / `GRID_RESTORED` | 4 / 3 | QPIWS line fail or grid < 90 V (< 170 V in UPS input range); **never from mode `B` alone** (SBU runs on battery with grid present) | 10 s | 30 s, message includes the outage length |
| `GRID_ABNORMAL` | 2 | 90–180 V or > 270 V | 5 min | 5 min |
| `OVERLOAD` | 4 | QPIWS overload or load ≥ `loadHigh` | 30 s | 60 s below 80 % |
| `OVER_TEMP` | 4 | QPIWS over-temperature / fan locked, or heat sink ≥ `tempHigh` | 30 s | 5 min |
| `PV_OVERVOLT` | 4 | QPIWS PV high or PV ≥ 480 V | 10 s | 5 min |
| `PV_LOW_DAY` | 2 | 10:00–15:00, PV < 50 W for 60 min while SOC < 95 % | – | once a day |
| `LINK_LOST` | 3 (4 from the boot check) | No valid reply for 60 s while running | – | first valid reply |
| `LOGGER_REBOOT`, `QUOTA_GUARD`, `DATA_DROPPED`, `BACKLOG_STUCK` | 2 | Reset reason ≠ POWERON; quota guard; old queue days deleted; backlog > 6 h with Wi-Fi up | – | – |
| `DAILY_SUMMARY` | 1 | At `summaryHour` (push only; the e-mail summary comes from the mailer) | – | – |
| `TEST_ALERT` | 3 | `test-alert` or the PWA's **Send test alert** (ignores quiet hours) | – | – |

Severity 4–5 alerts that stay active are pushed again every `notifyRepeatMin` (30 min).

**Usage-threshold rules** (configured in the PWA, [06](06-cloud-and-dashboard-guide.md) §9.5): the logger evaluates up to 16 rules every 5 s, also while the internet is down, from the last `alerts.json` it stored in flash. A rule event that passes its gates carries `em:"q"` and gets a `mailq` job in the same request; the Apps Script mailer sends it. `rules` shows each slot's state with the short names of `src/agg/RuleTypes.h`: `off`, `wait` (needs the clock, which is not set yet), `idle`, `pend` (condition true, hold time running), `fire`, `clr` (recovering), `latch` (energy rule fired for this day or cycle), `done` ("check once at" rule already checked), `inv`. The PWA shows them as idle / pending / firing / recovering / fired / checked / invalid. A rule the firmware cannot use becomes `inv` with one `RULE_INVALID` event; a broken `alerts.json` keeps the previous set and shows `status.ruleErr`.

> ⚠️ **Deploy order: database rules → mailer → PWA → this firmware** ([r8](../research/r8-email-alerts.md) §7). Without the new rules the device's `alertState` writes are refused and dropped (minute uploads continue), and refused events are dropped after 3 tries (`edrop` in `cloud`).

**TLS roots** are pinned per host in [`include/roots.h`](../firmware/include/roots.h) (Google GTS R1/R3/R4 for Firebase and Apps Script, ISRG/Let's Encrypt for ntfy.sh, Go Daddy G2 for Telegram, USERTrust/Sectigo for hc-ping.com). The ESP does not check certificate dates, so **re-check the chains once a year** with the `openssl s_client` loop in [`firmware/README.md`](../firmware/README.md) §7 and add a root if a chain changed.

---

## 12. OTA updates (`/update`)

After the first USB flash, updates can go over Wi-Fi. OTA writes the other app slot (6.25 MB each) and never touches NVS or the LittleFS data.

1. Build: `pio run -e savx`. The file is **`firmware/.pio/build/savx/firmware.bin`**. Keep a copy of every version you install, for rollback:
   ```zsh
   mkdir -p ~/savx-releases && cp .pio/build/savx/firmware.bin ~/savx-releases/firmware-$(grep -o '"[0-9.]*"' include/config.h | head -1 | tr -d '"')-$(date +%Y%m%d).bin
   ```
2. On a device on the same Wi-Fi open **`http://solar.local/update`** (local page **Settings → Firmware update (OTA)**). Log in as **`admin`** with the admin password.
3. Keep the mode on **firmware**, choose `firmware.bin` (iPhone: AirDrop it to Files first), and wait for the success message. The logger reboots (`[ota] update OK - rebooting` on serial).
4. Check the version: local page **Settings → System** (`"fw"`), the PWA's Settings → Logger, or `status`.

**Rollback:** upload the previous `.bin` the same way. If the new version crashes repeatedly, the logger falls into safe mode (§13), where `/update` still works.

The ElegantOTA page also offers a filesystem mode for `littlefs.bin` [not tried on this board]. It replaces the whole LittleFS partition, with the same data loss as `uploadfs` (§4 ⚠️), so prefer USB for the local page.

---

## 13. Safe mode and recovery

### 13.1 When safe mode starts

NVS `sys/bootFails` counts **crash-type resets only**: panic, task/interrupt watchdog, brownout, unknown. It is cleared after **120 s** of healthy running. At **3 or more** the next boot runs in **safe mode**:

- no inverter TX (engine `safe`, arm refused), no cloud, no pushes (the uplink task is not started);
- Wi-Fi, the local page, the console and `/update` work; LED solid red; `status` says `safe YES`; the boot line ends with `** SAFE MODE: no inverter TX, no cloud **`.

Plain power-on resets, `reboot`, OTA restarts and the network-watchdog restart **never count**, so power cuts and load-shedding cannot trigger safe mode.

### 13.2 Leaving safe mode

1. Find the cause first: `status` shows the reset reason (`rr`), and the monitor shows the decoded backtrace of the next crash (`esp32_exception_decoder`). Brownouts point at the cable, charger or socket (§18).
2. Fix it (for example OTA the previous good firmware, §12).
3. Leave: serial **`safe-exit`**, or `curl -u "admin:$PW" -X POST http://solar.local/api/safemode/exit`. The logger clears the counter and reboots.

Because the counter is also cleared after 120 s of running, a plain `reboot` after 2 minutes in safe mode also boots normally; if the crash comes back, safe mode returns after 3 more crashes.

### 13.3 Recovery ladder

| Step | When | How | What is lost |
|---|---|---|---|
| 1 | Hung or odd behaviour | `reboot`, local page **Settings → Reboot**, or unplug for 10 s | RAM only (24 h chart, alert queue) |
| 2 | Bad update | OTA the previous `.bin` (§12) | Nothing |
| 3 | No Wi-Fi access | USB to COM, `pio run -e savx -t upload` (BOOT+RST download mode if needed) | Nothing |
| 4 | Wrong settings, unknown passwords | Settings reset (§13.5) | All NVS (list below) |
| 5 | Corrupt flash | Full erase (§13.6) | Everything, including minutes not yet uploaded |

### 13.4 Lost the admin password

Serial console: `adminpw <new password>`, or open the portal (BOOT 3 s) and fill in the admin field. No erase needed.

### 13.5 Settings reset (NVS only)

The partition table `default_16MB.csv` (checked in the pinned framework package) puts **`nvs` at `0x9000`, size `0x5000`**. Erasing it resets: Wi-Fi, the AP password (a new one is generated), the admin password, Firebase credentials and refresh token, alert channels, cached thresholds, the **link state (self-ID, idle results, arm, probe profile)**, the energy checkpoint, rule states and boot counters. The LittleFS data (minute queue, web page) stays.

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/firmware
pio device list                                          # find the COM port, e.g. /dev/cu.usbmodemXXXX
~/.platformio/penv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX erase-region 0x9000 0x5000
```

The `esptool` 5.4.0 that the pinned platform uses lives in PlatformIO's Python environment (`~/.platformio/penv/bin/`; with a custom `PLATFORMIO_CORE_DIR` it is `$PLATFORMIO_CORE_DIR/penv/bin/`). Its subcommand is `erase-region`; the old `erase_region` still works with a deprecation warning. (The `pio pkg exec -p tool-esptoolpy -- esptool.py …` form in [`firmware/README.md`](../firmware/README.md) §1 fails with `No module named 'rich_click'` on this setup.) After the erase: the portal opens, note the **new** AP password from the boot log, and **redo the self-ID, idle test and arm** before the logger talks to the inverter again.

### 13.6 Full erase

`pio run -e savx -t erase`, then `-t upload` and `-t uploadfs`. Everything on the board is gone, including minute records that were not uploaded yet. Start again at §5.

---

## 14. Configuration reference

### 14.1 NVS (Preferences) namespaces and keys

| Namespace | Key | Contents | Written by |
|---|---|---|---|
| `sys` | `bootId` | Boot counter | every boot |
| | `bootFails`, `safe` | Crash counter (§13.1); manual safe flag (read, but nothing sets it) | firmware |
| | `apPass` | Setup-AP password (random, or `-DAP_PASSWORD` on a fresh NVS) | first boot |
| | `adminHash`, `adminDef` | SHA-256 of the admin password; "still equal to the AP password" flag | portal, `adminpw`, `POST /api/config` |
| | `nwdEp` | Epoch of the last network-watchdog reboot | uplink |
| `cloud` | `apiKey`, `dbUrl`, `email`, `pass` | Firebase device key, DB URL, device user | portal |
| | `rtok`, `uid` | Refresh token, device UID | uplink |
| `alert` | `ntfyTopic`, `tgToken`, `tgChat`, `hcUrl`, `appUrl` | Push channels and the click URL | portal, `POST /api/config` (not Telegram) |
| | `cfg` | Cached thresholds (the cloud `cfg` or the local Settings form) | aggTask |
| `mail` | `nudge` | Mailer nudge URL (v1.1, optional) | portal, `POST /api/config` |
| `rules` | `rs01`…`rs16` | Per-rule state (28 B each, written on transitions) | RuleEngine |
| | `mj`, `rev` | Mail jobs today; loaded `alertsRev` | RuleEngine, uplink |
| `link` | `mode`, `baud`, `rxInv`, `txInv` | Link profile from the probe | engine |
| | `roPin`, `diPin`, `diOk` | Module self-ID result (only GPIO16/18, distinct, are accepted on load) | self-ID |
| | `idle` | Last 4 idle (GND) and 2 upper-check (3V3) results per pin, plus the sticky "ever TX" / "ever ABOVE-3V3" marks (a layout change discards the blob: redo the tests) | idle test |
| | `armed`, `armRx`, `invTxRj` | Arm state and the pin map | arm / reset |
| | `adapter`, `dialect`, `pv12`, `supMask`, `unsupMask`, `aflags`, `lastProbe` | Protocol adapter state | engine |
| `energy` | `day`, `epv`, `eld`, `ech`, `edis`, `egr`, `nf`, `nw`, `epvi`, `lastEpoch` | 5-min checkpoint of today's totals | aggTask |
| | `mtd` | Billing-cycle sums for month rules | aggTask |

Wi-Fi credentials are kept by ESP-IDF itself (also inside the `nvs` partition). No secret is compiled into the firmware; there is no `secrets.h`.

### 14.2 LittleFS files

| Path | Contents |
|---|---|
| `/www/index.html` (or `.gz`) | The local page (from `data/www/`, flashed by `uploadfs`) |
| `/q/<YYYY-MM-DD>.bin` + `.up` | 55-byte minute records per local day + upload cursor |
| `/q/p<boot>.bin`, `/q/anchors.bin` | Records of a boot that had no clock yet; time anchors to fix them |
| `/log/probe.json`, `/log/raw-0.txt` | Last probe report; raw capture (≤ 256 KB) |
| `/cfg/alerts.json` | Last rule set from RTDB (loaded at boot, before Wi-Fi) |

Housekeeping keeps uploaded days for 14 days and deletes the oldest days if less than 200 KB stays free (`DATA_DROPPED` event).

### 14.3 Build flags (`platformio.ini`, `[env:savx]` → `build_flags`)

| Flag | Default | Effect |
|---|---|---|
| `-DINV_ALLOW_WRITES=0` | set | Read-only gate. **Any other value stops the build** with `#error`. There is no setter code |
| `-DARDUINO_USB_CDC_ON_BOOT=0` | set | `Serial` = UART0 on the COM port |
| `-DCORE_DEBUG_LEVEL=2` | set | Arduino core warnings on serial |
| `-DLED_PIN=38` | 48 | RGB LED on GPIO38 (DevKitC-1 v1.1) |
| `-DWIFI_TX_POWER_CAP` | off | Wi-Fi TX power 15 dBm, only if brownouts appear |
| `-DAP_PASSWORD=\"yourpass8\"` | random | Fixed setup-AP password (≥ 8 characters for WPA2). Used **only when no AP password is stored yet** (new board or after an NVS erase) |
| `-DFW_TLS_INSECURE_DEBUG` | off | ⚠️ Bench only: disables certificate checks and prints a build warning. **Never flash this to the installed logger** |

### 14.4 Compile-time constants worth knowing ([`include/config.h`](../firmware/include/config.h))

| Constant | Value | Meaning |
|---|---|---|
| `PIN_INV_TX`, `PIN_MOD_HDR_RXD`, `PIN_MOD_HDR_TXD`, `PIN_BOOT` | 17, 18, 16, 0 | Pins (never 0/3/45/46, 19/20, 26–32, 35–37, 43/44 for wiring) |
| `BAUD_PI30` | 2400 | First probe baud |
| `P_FAST_MS`, `P_MID_MS`, `P_RATING_MS`, `P_ENERGY_MS`, `P_IDENT_MS` | 5 s, 10 s, 10 min, 5 min, 1 h | Poll periods |
| `IDLE_TX_LOW_PCT`, `IDLE_MAX_EDGES`, `IDLE_CONFIRMATIONS` | 99 %, 3, 2 | Idle-test and upper-check thresholds (§8.2) |
| `PREARM_SAMPLE_MS` | 200 ms | Pre-arm check of GPIO17 and RO (§8.4) |
| `LINK_OK_WINDOW_MS`, `REPROBE_FAILS` | 15 s, 60 | `lk=1` window; failed polls before a re-probe |
| `TZ_POSIX`, `NTP_SERVER_1/2` | `<+06>-6`, pool.ntp.org, time.google.com | Asia/Dhaka, no DST |
| `PUSH_CAP_PER_DAY`, `MAIL_JOBS_PER_DAY` | 40, 40 (5 reserved for severity ≥ 4) | Daily caps |
| `MAX_RULES` | 16 | Usage-threshold rule slots |
| `BUDGET_HS_GUARD`, `BUDGET_RQ_GUARD` | 1,500, 60,000 | Quota guard level 1 (level 2 at twice) |
| `SAFE_MODE_BOOTFAILS`, `HEALTHY_AFTER_MS`, `WDT_TIMEOUT_S` | 3, 120 s, 30 s | Safe mode and watchdog |
| `BOOT_HOLD_MS`, `PORTAL_TIMEOUT_S` | 3 s, 600 s | Portal on demand |
| `MDNS_HOST`, `AP_NAME_PREFIX`, `ADMIN_USER`, `DEVICE_ID` | `solar`, `SavxLogger-`, `admin`, `inv1` | Names |

### 14.5 Settings that come from the cloud (`devices/inv1/cfg`)

Set in the PWA's **Settings → Alerts and logging**; the logger reads them every 60 s and caches them in NVS. The local page's **Settings → Alert thresholds** form edits the same values; a later change in the PWA overrides it.

| Key | Range (rules) | Default | Used for |
|---|---|---|---|
| `liveSec` | 5–300 s | 10 | `live` upload interval |
| `socLow`, `bvLow` | 5–80 %, 20–30 V | 30, 22.5 | `BATT_LOW` |
| `bvHigh` | 26–34 V | 30.0 | `BATT_HIGH` |
| `tempHigh` | 40–100 °C | 75 | `OVER_TEMP` |
| `loadHigh` | 50–120 % | 90 | `OVERLOAD` |
| `notifyRepeatMin` | 5–1440 | 30 | Re-push of active severity 4–5 alerts |
| `keepHistDays`, `keepEventDays` | 30–3650 | 400, 730 | Cloud pruning of `hist` / `events` |
| `quietFrom`, `quietTo` | 0–23 | 22, 7 | Quiet hours (local) |
| `summaryHour` | −1–23 | 21 | `DAILY_SUMMARY` push (−1 off) |
| `testAlert`, `reprobe` | epoch s | 0 | A new value triggers `TEST_ALERT` / a re-probe |
| `alertsRev` | ms | – | Bumped by the PWA on every rules change: the logger re-reads `alerts` |

---

## 15. Code tour

```
                        ┌──────────── core 1 ─────────────┐   ┌──────── core 0 ────────┐
 COM RJ45 ─ module ─►   │ invTask (prio 5)                │   │ uplinkTask (prio 3)    │
 UART1 RX GPIO18        │  SerialLink (whitelist gate)    │   │  Uplink state machine  │
 UART1 TX GPIO17 ◄──────│  InverterEngine: Prober,        │   │  FirebaseRest (TLS)    │──► RTDB
 (only when ARMED)      │   Scheduler, PI30/REVO/Q6/Modbus│   │  Notify: ntfy, Telegram│──► ntfy / Telegram
                        │  PinDiag (self-ID, idle test)   │   │   Healthchecks         │──► hc-ping.com
                        └───────────┬─────────────────────┘   │  MailNudge (optional)  │──► Apps Script
                                    │ qSample (5 s)            └──────────▲─────────────┘
                        ┌───────────▼─────────────────────┐              │ alertQueue, minute
                        │ aggTask (prio 4)                │──────────────┘ files, app state
                        │  Aggregator (Wh, minute, h15,   │
                        │   24 h PSRAM ring)              │──► LittleFS /q/<date>.bin
                        │  AlertEngine + RuleEngine       │
                        └─────────────────────────────────┘
 loopTask: LocalWeb (http://solar.local, /update), WifiProv portal, Console, StatusLed, SafeMode
```

| Folder | Main files | Role |
|---|---|---|
| `include/` | `config.h`, `codes.h`, `roots.h` | Pins, timing, safety gate; code tables (hand-written); TLS trust anchors |
| `src/main.cpp` | | Boot, safe-mode check, PSRAM JSON allocator, task start, BOOT button, LED |
| `src/app/` | `App`, `Tasks`, `Console` | Shared state between tasks, the three FreeRTOS tasks, serial commands |
| `src/link/` | `SerialLink`, `Whitelist`, `PinDiag`, `RawRing`, `LinkProfile` | UART framing and the read-only gate, wiring diagnostics, raw history |
| `src/proto/` | `Prober`, `Engine`, `Scheduler`, `pi30/`, `legacy/`, `modbus/` | Protocol discovery and polling adapters |
| `src/model/` | `Sample`, `Decode`, `LiveJson`, `Identity`, `TimeUtil` | Normalised data model and its JSON |
| `src/agg/` | `Aggregator`, `AlertEngine`, `NotifyGates`, `Rule*`, `MonthToDate` | Energy, minute records, built-in alerts, usage-threshold rules |
| `src/store/` | `MinuteQueue`, `MinuteRec`, `Settings` | LittleFS queue and cursors, 55-byte record format, NVS |
| `src/net/` | `WifiProv`, `TimeSync`, `HttpsClient`, `HttpParse`, `FirebaseRest`, `UplinkJson`, `Backfill`, `Budget`, `Notify*`, `MailNudge`, `Uplink` | Network side |
| `src/web/` | `LocalWeb` | Local page, JSON API, ElegantOTA |
| `src/sys/` | `Watchdog`, `SafeMode`, `StatusLed` | System services |
| `data/www/` | `index.html` | The local page (plain HTML/JS, no libraries) |
| `test/` | 8 suites + `mocks/` | Host tests (§16) |

Everything except `hal/`, `web/`, `net/` I/O, `sys/` and `app/` is host-portable (no `Arduino.h`) and compiled into the native tests (`build_src_filter` in `platformio.ini`).

---

## 16. Host unit tests

### 16.1 Run them

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/firmware
pio test -e native                      # all suites; ends with "98 test cases: 98 succeeded"
pio test -e native -f test_parsers      # one suite
```

They need no board. The native build uses your Mac's C++ compiler (Xcode Command Line Tools; macOS offers to install them the first time `cc` is used).

| Suite | Cases | Covers |
|---|---|---|
| `test_frames` | 12 | Byte-exact request frames, PI30 CRC, REVO CHK, Modbus CRC, the whitelist (a refused setter sends **0 bytes**; the removed near-miss queries are refused; the whitelist equals the commands the firmware sends) |
| `test_parsers` | 13 | QPIGS/QPIRI/QMOD/QPIWS/QALL/QFLAG/Q1/PQSE/Q6 parsers on real frames, plausibility gates, dialect classification, `live` JSON keys |
| `test_engine` | 14 | Probe decision tree against simulated inverters, the arming gates (upper check blocks a positive pin, pre-arm check), an unarmed engine never transmits |
| `test_agg` | 16 | Energy integration (1000 W × 1 h = 1000 Wh ± 0.5 %), minute/h15/daily records, built-in alerts and their e-mail flags |
| `test_rules` | 19 | Rule state machine: hold, hysteresis, cooldown, daily limits, energy "at" checks, persistence |
| `test_rule_parse` | 6 | `alerts.json` parsing mirrors the RTDB `.validate` rules |
| `test_mtd` | 4 | Billing-cycle (month-to-date) sums |
| `test_uplink` | 14 | HTTP response parser, RTDB paths/keys/bodies, back-fill planning, budget, ntfy/Telegram bodies |

CI runs the same two commands on every push that touches `firmware/` or `shared/` ([06](06-cloud-and-dashboard-guide.md) §4).

### 16.2 Rules for changes

- The whitelist in `src/link/Whitelist.cpp` is exactly the set of commands the firmware sends (`QMCHGCR`, `QMUCHGCR`, `QBEQI`, `QLT`, `QEM…`, `QEY…` were removed in 0.6.1: unused, and several are one byte or one bit away from setter commands, [03](03-hardware-and-wiring.md) Appendix A S7). Never add a command that is not a documented **read** query ([02](02-protocol-reference.md)), add it to `Commands.h` at the same time (a test compares the two), and add a `test_frames` case for it.
- Keep `INV_ALLOW_WRITES=0`.
- Run `pio test -e native` and `pio run -e savx` before flashing.

### 16.3 Adding your inverter's real frames

There is no fixture loader. After M3, copy reply payloads from `/api/raw` or the capture (§9) into a new `static const char*` next to `EASUN_QPIGS` in [`test/test_parsers/test_main.cpp`](../firmware/test/test_parsers/test_main.cpp), write a `void test_savx_qpigs()` that parses it and asserts the values you read on the LCD, add `RUN_TEST(test_savx_qpigs);` in `main()`, and run the suite.

---

## 17. Adding a decoded field

Example: a value from a reply that the logger does not show yet.

1. **Is the command already polled?** The schedule is in `src/proto/Scheduler.cpp` and the adapters in `src/proto/pi30/Pi30Adapter.cpp`. A new command must also be added to the whitelist (§16.2) and to `CMD_INFO` in `src/proto/Commands.h` (as `optional`, so a NAK only marks it unsupported).
2. **Parse it.** Field arrays come from `src/proto/pi30/Pi30Parse.cpp` (`parseQpigs`, `parseQall`, …). Field indexes and meanings are in [02](02-protocol-reference.md) §7.
3. **Map it into `Sample`.** Add a member to `src/model/Sample.h` (SI units, `NAN` when missing) and set it in the matching function in `src/model/Decode.cpp` (`decode::qpigs`, `decode::qall`, …). If it changes slowly, also copy it in `decode::carrySlowFields`.
4. **Publish it live.** Add one line to `sampleToLiveJson` in `src/model/LiveJson.cpp`, e.g. `j.num("xyz", s.xyz, 1);`. The key must be **≤ 6 characters** and numeric: the RTDB rules accept any such key in `live` without a change ([firebase/README.md](../firebase/README.md) §1). Missing values are omitted automatically.
5. **Test it.** Extend `test_live_json_keys` (or a parser test) in `test/test_parsers/test_main.cpp`, run `pio test -e native`.
6. **Show it.** Add the key to the `Live` type in [`web/src/data/types.ts`](../web/src/data/types.ts) and a tile in `web/src/pages/Live.tsx`; on the local page, add a `tile(...)` in `data/www/index.html` (then `uploadfs`, §4 ⚠️).
7. **History is different.** Minute history uses the fixed 55-byte `MinuteRec` (`src/store/MinuteRec.h`, `static_assert` on the size). Adding a field there changes the flash file format, the RTDB `hist` keys (≤ 4 characters) and the PWA's charts; it needs a migration plan for the files already on the board, so treat it as a design change, not a quick edit.
8. Build, flash, check `live` in the Firebase console (open only the `live` node) and on the page.

---

## 18. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| First build fails while downloading | No internet, or GitHub blocked | Retry; the platform comes from the pinned GitHub release URL in `platformio.ini` |
| `pio: command not found` in Terminal | Installed only inside VS Code | Use **PlatformIO: New Terminal**, or add the `~/.local/bin` links (§1.1) |
| No new `/dev/cu.*` | Charge-only cable, wrong port, driver | Other cable/port; §3.2. Try the supplied cable |
| Upload: "Failed to connect" / timeout | Board not in download mode, monitor still open, wrong port | Close the monitor; **hold BOOT, tap RST, release BOOT**; `--upload-port` |
| Monitor shows nothing | Cable in the native **USB** port | Use **COM** (the firmware logs on UART0 only) |
| Garbage on the monitor | Wrong baud | `pio device monitor -e savx` (115200 from `platformio.ini`) |
| `PSRAM 0 bytes` | Not the `savx` environment / wrong board | Build with `-e savx` (board `esp32-s3-devkitc1-n16r8`) |
| Local page says "not in flash yet" | `uploadfs` not done | §4 #4 |
| Portal network not visible | Portal not open (Wi-Fi already saved) | Hold BOOT 3 s, or `portal` |
| Logger never joins Wi-Fi | 5 GHz SSID, wrong password, band steering | Separate 2.4 GHz SSID; re-enter in the portal |
| `solar.local` does not open | mDNS blocked, phone on mobile data or guest Wi-Fi | Use the IP (§6.1) |
| "password needed / wrong" on Diagnostics | Admin password missing/wrong | Default = AP password; reset with `adminpw` (§13.4) |
| Self-ID `both-low` / `noisy` | Short or loose wraps | §7.2 table |
| Idle test `traffic` | Another device on COM | Remove it; see [03](03-hardware-and-wiring.md) §9.5 |
| Arm refused | Self-ID missing, idle/upper results incomplete, or the wrong pin named | Read the reason; §8.2–§8.3. **Never force it** |
| Upper check `ABOVE-3V3` | A positive wire on A+ (colour mix-up, pin 4?) | **Stop.** RJ45 out; re-check colours and twists ([03](03-hardware-and-wiring.md) §7); `reset-link`; redo Stage C |
| Arm refused: "GPIO17 reads HIGH with its pull-down" | GPIO17 wrap touches the GPIO16 wrap, or a positive wire | Unplug, separate the wraps ([03](03-hardware-and-wiring.md) §6.3), re-check colours |
| Arm refused: "RO … is not steady LOW" | A+ not on the transmit wire, inverter off, GPIO16/18 wraps touch | Check against the Stage D table |
| Boot log `Boot: TX NOT attached this boot` + `LINK_LOST` (sev 4) | Boot check found GPIO17 HIGH | Unplug COM, inspect the GPIO16/17 wraps, `reset-link`, redo §8 |
| Loopback refused | Module or RJ45 already wired | Loopback is only for B1 (§7.1) |
| Probe: "no protocol found" | TX path, polarity, or GPIO17 on the wrong wire | [03](03-hardware-and-wiring.md) §9.2 |
| `crcErr` / `timeouts` climbing (`status`) | Loose joint, noise | Tug test, route the cable away from PV/battery cables |
| `cloud`: `cloud credentials incomplete …` | A portal field is empty | Fill all four Firebase fields |
| `cloud`: `DB URL must be https://<db>.<region>.firebasedatabase.app` | URL with path or other host | Bare `https://…firebasedatabase.app` |
| `upErr`: `sign-in HTTP 400 …` | Wrong device e-mail/password, user disabled, Email/Password sign-in off | Fix in Firebase / the portal; sign-in pauses 30 min unless you save the portal |
| `upErr`: `sign-in HTTP 403 …` | Browser (referrer-restricted) key entered | Use the device key ([06](06-cloud-and-dashboard-guide.md) §2.6) |
| `upErr`: `GET cfg: HTTP 401 Permission denied` (or PUT live) | Rules not deployed, or the device UID in the rules is wrong | [06](06-cloud-and-dashboard-guide.md) §3 |
| `upErr`: `connect <host>: X509 …` | TLS chain changed, or a captive/filtered network | Re-check the roots (§11); try another network |
| `notify.hcCode` not 200 / no ntfy | Wrong topic/UUID, ISP blocks the host | Test from the Mac ([06](06-cloud-and-dashboard-guide.md) §8) |
| `QUOTA_GUARD` event, `qg` > 0 | A retry loop or a very short `liveSec` | Check `upErr`, raise `liveSec`; see [06](06-cloud-and-dashboard-guide.md) §11 |
| `BACKLOG_STUCK` event | Uploads failing for > 6 h with Wi-Fi up | `cloud` → last error |
| `edrop` > 0 | Events refused by the rules (old rules deployed) | Deploy the current rules ([06](06-cloud-and-dashboard-guide.md) §3) |
| Brownout resets (`rr` = `BROWNOUT`) | Weak charger or cable | Better cable/socket; `-DWIFI_TX_POWER_CAP` |
| Inverter shows a new warning/fault | Unexpected interaction | **Pull the RJ45 now**, note the code, stop, disarm |

More trees: [08](08-risks-troubleshooting-maintenance.md).

---

## 19. Sources

- Code: [`firmware/`](../firmware) ([`README.md`](../firmware/README.md), [`platformio.ini`](../firmware/platformio.ini), [`include/config.h`](../firmware/include/config.h)); the WiFiManager 2.0.17 and ElegantOTA 4.0.0 sources under `firmware/.pio/libdeps/savx/` (portal menu, Erase button, filesystem OTA mode); the pinned platform's `default_16MB.csv` and `boards.txt` (Arduino-ESP32 3.3.12).
- Design: [d1](../research/d1-architecture.md) §3–§5, §7; [r8](../research/r8-email-alerts.md) §4; [v1](../research/v1-protocol-verification.md); [v2](../research/v2-platform-verification.md); [r5](../research/r5-hardware.md) §1.4; [user constraints](../research/user-constraints.md).
- PlatformIO: [installation methods](https://docs.platformio.org/en/latest/core/installation/methods/index.html), [installer script](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html), [Homebrew](https://docs.platformio.org/en/latest/core/installation/methods/brew.html), [pip](https://docs.platformio.org/en/latest/core/installation/methods/pypi.html), [shell commands](https://docs.platformio.org/en/latest/core/installation/shell-commands.html), [VS Code](https://docs.platformio.org/en/latest/integration/ide/vscode.html) (read 2026-10-21).
- Espressif: [Arduino-ESP32 installing](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) (read 2026-10-21); [ESP32-S3-DevKitC-1 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
- ntfy JSON publishing: <https://docs.ntfy.sh/publish/#publish-as-json>.
