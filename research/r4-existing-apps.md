# R4: Existing apps vs DIY for a Savx F4 3KW (iPhone user, Bangladesh)

Research date: 2026-10-21. Confidence tags: **[CONFIRMED-primary]** means I read it in an official document, a store listing or source code. **[SECONDARY]** means a forum, blog, marketplace listing or search-engine snippet. **[INFERRED]** means my own reasoning. Every factual claim has a URL in the text or in §8.

---

## 0. Summary

1. **There is no Savx- or IPS-Bazar-branded WiFi module or app.** IPS Bazar's Savx F3 and F4 listings name only "USB / RS232", with "CAN / RS485" marked optional. None of them mentions WiFi, an app or a dongle. A search of the IPS Bazar catalog for "wifi", "monitoring" and "datalogger" returned no standalone WiFi module. [CONFIRMED-primary: ipsbazarbd.com listings, fetched 2026-10-21]
2. **Generic dongles would only work if the F4 speaks a protocol that the Eybond cloud recognizes.** These include the Eybond "WiFi-Plug Pro" family, the Voltronic WiFi module and PowMr modules, which use the SmartESS, Energy-Mate, WatchPower or SolarPower apps. Recognized protocols include Voltronic PI30/PI18 ASCII and SMG Modbus. R1 and R2 have to establish which one the F4 uses. Vendors state that these modules are model-locked, so buy only with a return option. [CONFIRMED-primary: PowMr pages; SECONDARY: forum reports]
3. **All the "Voltronic-class" iOS apps come from one Eybond developer account and feed one Eybond cloud.** SmartESS, WatchPower, Energy-Mate, SolarPower Wi-Fi and SmartClient for Solar (ShineMonitor) are all published by seller "祥梯 孟", with © Shenzhen Eybond or © VOL FW. Their App Store ratings sit around 2.5–2.9 out of 5, except Energy-Mate at 4.2 from 13 ratings. The dongle uploads data every 5 minutes by default. History retention and export limits are not documented. [CONFIRMED-primary: App Store/iTunes lookup, Eybond spec PDF]
4. **A dongle and a DIY ESP32 cannot both poll the same serial line.** "One inverter bus should have one active master" (ESP EyeBond Collector README). On Axpert-class units the USB and RS232 ports share one DSP UART. Coexistence is still possible in three ways:
   - The ESP32 only listens passively (sniffs the traffic).
   - The ESP32 or Home Assistant pulls data from the dongle over the LAN, using the documented Eybond local callback (UDP 58899 → TCP 8899).
   - The vendor dongle is replaced entirely. [CONFIRMED-primary: READMEs and source; SECONDARY: forum]
5. **The DessMonitor/ShineMonitor cloud API works but is unofficial.** It uses SHA-1-signed GET requests to `https://api.dessmonitor.com/public/`, as implemented in ha-dessmonitor. It supports custom dashboards, but data has 5-minute resolution. There is no official API documentation, no SLA and no 2FA, and the SHA-1 of your password works as a credential. It is a weak foundation for a long-term logger. [CONFIRMED-primary: source code; SECONDARY: reverse-engineered docs]
6. **Recommendation:** Build the DIY logger. Use the ESP32-S3 with the correct level converter (MAX3232 if the port is RS232, as the listings suggest). Bring the protocol up with ESPHome and its local web UI. Push samples to **Firebase Realtime Database (Spark/free tier)**, and view them in a **Firebase-Hosted PWA added to the iPhone Home Screen**. Send alerts from the ESP32 through **ntfy or a Telegram bot**, because Cloud Functions are not available on Spark.
   - **Fallback A:** a vendor Eybond/PowMr-type WiFi dongle with the SmartESS/Energy-Mate app.
   - **Fallback B:** if you ever get an always-on box, SolarAssistant ($59 licence plus a Raspberry Pi) or Home Assistant with the Companion app.
7. **Hardware gap:** The listings say the F4's standard ports are **USB and RS232**, with RS485 optional. The RS485-to-TTL module you bought cannot talk to an RS232 port. A MAX3232 RS232↔TTL module costs **৳137–336 on Daraz BD**. Check the actual port under the bottom cover first. [CONFIRMED-primary listing; Daraz prices; INFERRED compatibility]

---

## 1. Inverter facts that matter for app/dongle choice

| Fact | Value | Source / tag |
|---|---|---|
| Comm ports, Savx F4 3000-24V listing | "Smart LCD Touch Display \| USB / RS232 \| Optional: CAN / RS485" and "Communication Interface: USB, RS232 (Optional: CAN / RS485)". No WiFi or app is mentioned. Price ৳35,000 (regular ৳50,000). | https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/ [CONFIRMED-primary] |
| Comm ports, Savx F4 1500-12V listing | "Communication: LCD Display \| USB / RS232 \| Optional: CAN / RS485" | https://ipsbazarbd.com/product/savx-f%e0%a7%aa-1500-12v-solar-inverter-mppt-base/ [CONFIRMED-primary] |
| Comm ports, Savx F3 3500-24V and F3 2500-24V | "USB / RS232 (Standard), CAN & RS485 (Optional)". No WiFi or app. Prices ৳38,000 and ৳33,000, both out of stock. | https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/ , https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/ [CONFIRMED-primary] |
| How reliable the listings are | The F4 3000 listing contradicts the nameplate: it says PV 3000W and AC charge 60A, while the nameplate says PV 3500W and AC charge max 100A. Its images are named "ChatGPT Image …". The listing is templated, so treat port claims as a hint and verify them physically. | same URL [CONFIRMED-primary] vs nameplate photo; [INFERRED] |
| IPS Bazar WiFi products | A site search for "wifi", "monitoring" and "datalogger" returned only complete Growatt/Deye/Solis/Crown inverters and a "WiFi voltage protector". There is no standalone inverter WiFi module. | `https://ipsbazarbd.com/?s=wifi&post_type=product` (fetched 2026-10-21) [CONFIRMED-primary] |
| IPS Bazar YouTube video | The video is "Savx সোলার ইনভার্টারে রয়েছে ১০% ছাড় । Savx Solar Inverter Price in BD" by channel "IPS Bazar - Bangladesh". It is a price/discount promo, and I could not fetch the page itself (blocked). | YouTube oEmbed for https://www.youtube.com/watch?v=tdCSpWiejBQ [CONFIRMED-primary metadata] |
| IPS Bazar Facebook | facebook.com/Ipsbazardhaka is linked from the shop homepage. Facebook content was not fetchable. | https://ipsbazarbd.com/ (via search snippet) [SECONDARY] |
| BME (bme.com.bd) Savx pages | The pages render without specs or price in the fetched HTML. No WiFi accessory was visible. | https://bme.com.bd/savx-f3-3500-24v-mppt-base-solar-inverter [CONFIRMED-primary, inconclusive] |

**What this implies for dongles [INFERRED]:** If the F4 exposes an RJ45 or DB9 RS232 port that speaks Voltronic PI30/PI18 or SMG Modbus, then the Eybond dongles and apps may work. If it uses a proprietary protocol, they will not, and only the DIY route remains. That makes R1/R2's protocol finding the deciding input.

A cheap pre-check is possible [INFERRED]. PowMr's manual lists Voltronic "WatchPower" PC software among its software packages (https://powmr.com/es/powmr-user-manual/). If WatchPower on a Windows PC recognizes the F4 over USB or RS232, the F4 is Voltronic-protocol compatible.

---

## 2. Vendor monitoring solutions

### 2.1 iOS app inventory

All data below comes from the iTunes Lookup API (US storefront) and the App Store pages, fetched 2026-10-21.

| App (iOS link) | Seller / © | Version (date) | Min iOS | Rating | Backend cloud | Dongle / port |
|---|---|---|---|---|---|---|
| SmartESS – https://apps.apple.com/us/app/smartess/id1334656760 | 祥梯 孟 / © Shenzhen Eybond Co.,Ltd | 3.56.0 (2026-08-24) | 13.0 | 2.69 (127) | DessMonitor (dessmonitor.com) | Eybond WiFi-Plug / WiFi-Plug Pro (RS232 DB9/RJ45/USB variants); PowMr modules |
| Energy-Mate – https://apps.apple.com/us/app/energy-mate/id1672569936 | 祥梯 孟 / © VOL FW | 1.8.5 (2026-02-11) | 11.0 | 4.23 (13) | DessMonitor | Voltronic WiFi module (RS232) |
| WatchPower – https://apps.apple.com/us/app/watchpower/id1487569321 | 祥梯 孟 / © VOL FW | 2.1.1 (2025-11-10) | 11.0 | 2.48 (75) | DessMonitor (with a compatibility check) | Voltronic WiFi module (RS232) |
| SolarPower Wi-Fi – https://apps.apple.com/us/app/solarpower-wi-fi/id1491698936 | 祥梯 孟 | 2.2.1 (2025-11-10) | 11.0 | 2.46 (35) | Eybond cloud | "monitor hybrid inverters via Wi-Fi module" |
| SmartClient for Solar (the ShineMonitor app) – https://apps.apple.com/us/app/smartclient-for-solar/id1070155085 | 祥梯 孟 / © Shenzhen Eybond | 6.51.0 (2026-09-21) | 15.6 | 2.91 (23) | ShineMonitor (shinemonitor.com) | Eybond collectors (mostly PV/grid-tie) |
| DESS Monitor | No iOS app exists under this name. It is the web portal for SmartESS/Energy-Mate accounts. | – | – | – | dessmonitor.com | – |
| SOLARMAN Smart – https://apps.apple.com/us/app/solarman-smart/id1469487897 | IGEN Tech Co., Ltd. | 2.4.6 (2026-10-19) | 15.1 | 4.62 (5,771) | Solarman cloud | IGEN "stick loggers" (Deye/Sunsynk-type inverters) |
| SRNE – https://apps.apple.com/us/app/srne/id1635684571 | SRNE Solar Co.,Ltd | 9.7 (2026-09-24) | 15.0 | 4.2 (5) | Bluetooth app with history view | SRNE BT adapter. An "SRNE PRO" WiFi app (first released Jul 2026) appears in a search snippet [SECONDARY] |
| SmartLife (Tuya) – https://apps.apple.com/us/app/smart-life-smart-living/id1115101477 | Volcano Technology Ltd | – | 14.0 | 4.7 (267K) | Tuya cloud | Tuya WiFi modules |
| Home Assistant Companion – https://apps.apple.com/us/app/home-assistant/id1099568401 | Nabu Casa, Inc | 2026.10.1 (2026-10-11) | 16.4 | 4.74 (4,003) | Your own HA server | none (DIY) |

Notes:
- **Shared developer account.** SmartESS, WatchPower, Energy-Mate, SolarPower Wi-Fi and SmartClient for Solar are all published by the same seller account "祥梯 孟". [CONFIRMED-primary: iTunes lookup] A reverse-engineered ShineMonitor API doc states that the "Eybond 'Shine' cloud" is "shared by WatchPower/SmartESS/SolarPower/Dessmonitor". [SECONDARY: https://github.com/tarun7singh/ha-shinemonitor/blob/main/docs/shinemonitor-api.md] ha-dessmonitor maps "SmartESS and Energy-Mate → DessMonitor" and "WatchPower (Wi-Fi) → DessMonitor (with compatibility check)". [CONFIRMED-primary README: https://github.com/andreas-glaser/ha-dessmonitor]
- **Energy-Mate replaces two older apps.** Voltronic announced Energy-Mate on 2023-06-16 as the replacement for "Watchpower" and "Solarpower". [CONFIRMED-primary: https://voltronicpower.com/en-US/Company/News/174] Both older apps still received updates on 2025-11-10.
- **Privacy labels.** All the Eybond apps self-declare "Data Not Collected", which Apple has not verified. The dongle still streams all of your inverter telemetry to the Eybond cloud. SmartClient for Solar also notes that it "may use your location even when it isn't open". SmartLife (Tuya) declares Usage Data used for tracking, plus location, contact info, identifiers and more linked to the user. [CONFIRMED-primary App Store pages; INFERRED on telemetry]
- **Bangladesh storefront.** The iTunes Lookup API returned 0 results for `country=bd`, even for WhatsApp, and `apps.apple.com/bd/...` redirects to `/us/`. Bangladeshi Apple IDs typically use another storefront such as US or IN, and all the apps above are present in both of those. [CONFIRMED-primary API behaviour; INFERRED interpretation]

### 2.2 Eybond "WiFi-Plug Pro" dongle (the SmartESS hardware)

This section comes from the official spec PDF for WFBLE.DTU.PlugProA-03: https://www.eybond.com/uploads/upload/20260422/20260422/b71d32b09cd31448a2fc274f33fa04cc.pdf. The product family page is https://www.eybond.com/data-communication-type/dtu-plugpro-series.html. [CONFIRMED-primary]

| Item | Value (quoted or paraphrased from the PDF) |
|---|---|
| Interface | DB9 RS-232. Pin 2 RS-232-RX, pin 3 RS-232-TX, pin 5 GND, pin 9 VCC "DC5V-DC12V" (powered by the inverter port). The family page also lists "USB/RJ-45/DB9/Aviation Connector" variants and RS-232/RS-485/TTL levels. |
| WiFi | 2.412–2.484 GHz, 802.11 b/g/n, WEP/WPA/WPA2, AP+STA co-existence mode, external antenna. This is compatible with your 2.4 GHz network. |
| Bluetooth | BLE 5.1, "Bluetooth local monitoring refresh rate 1s", 1 phone at a time. This gives a local/offline view in the app. |
| Upload | "Data upload cycle: 5 minutes (default)", transparent mode, TCP/UDP |
| Robustness | Hardware and software watchdog. "The system automatically restarts after 24 hours of continuous operation". |
| Cloud | "Value of cloud, customers custom platform" (spec); "Eybond Cloud/Private Cloud" (family page) |
| Other | "supports automatic recognition and switching of multiple communication protocols"; remote firmware upgrade "(including devices)"; IP65; -40…+85 °C |

Implications [INFERRED]:
- The 5-minute default upload interval caps the cloud history at 5-minute resolution. DessMonitor sells a faster "fast data" add-on. [SECONDARY: Antoxa1081 README says WebSocket push needs a paid "fast data" add-on; ha-dessmonitor README says a faster collection option may exist on eligible accounts and advises checking its "availability and pricing"]
- Remote firmware upgrade of the *inverter* through the cloud is a capability you are handing to a third party.

### 2.3 Per-vendor notes

| Solution | iOS | Dongle / port / protocol | BD price found | Cloud dependency | History / export | Local/offline | Known issues |
|---|---|---|---|---|---|---|---|
| **SmartESS + DessMonitor** (Eybond) | Yes (id1334656760). Web at dessmonitor.com. | WiFi-Plug Pro (DB9/RJ45 RS232; USB/RS485 variants). Firmware auto-recognizes PI30/PI18/SMG-Modbus families (devcodes such as 2376 for SMG). | No Eybond-branded dongle found on Daraz BD or IPS Bazar. International listings run ~US$57–75 on eBay [SECONDARY]. | Full. The app and portal work only through the Eybond cloud. | Real-time and historical views. The web UI has a per-day "Export" (queryDeviceDataOneDayPaging) [SECONDARY]. Retention is **not documented**. 5-minute resolution. | BLE local view on WFBLE models (1 s refresh) [CONFIRMED-primary]. Community HA integrations can read the dongle over the LAN (§4.3). | App rating 2.69. A dongle bound to a previous owner's account blocks re-adding it, and some dongles connect to the router but not the server (4th "SRV" LED off) [SECONDARY: powerforum snippets 16253, 19284]. No 2FA [SECONDARY]. |
| **WatchPower / Energy-Mate / SolarPower** (Voltronic, built by Eybond) | Yes (ids 1487569321, 1672569936, 1491698936). | Voltronic WiFi module on RS232; "Network protocol: Modbus-TCP"; 5–12 V; 2 W; 2.4 GHz 802.11b/g/n [CONFIRMED-primary: https://voltronicpower.com/en-US/Product/Detail/Wifi-Module] | Not found in BD. Pakistan "Volnex" WatchPower dongle Rs.10,000 [SECONDARY: w11stop.com] | Full | "Cloud storage for historical data and event logs". App descriptions mention "alarm notifications" and "record device data history" [CONFIRMED-primary]. Retention undocumented. | none documented (WiFi) | Same Eybond backend. Low ratings. Voltronic says Energy-Mate supersedes the other two. |
| **ShineMonitor / SmartClient for Solar** | Yes (id1070155085) | Eybond collectors, mostly PV/grid-tie oriented | – | Full | Day, month and year energy endpoints. Per-field time series [SECONDARY API doc]. | – | Rating 2.91. Location use in the background. |
| **Solarman Smart** (IGEN) | Yes (id1469487897), the best-rated app here | IGEN stick loggers. Local Modbus over "tcp/8899" via pysolarmanv5 [CONFIRMED-primary: https://pysolarmanv5.readthedocs.io/en/latest/] | – | Cloud app. Local Modbus is possible. | "historical data" [CONFIRMED-primary App Store] | Local TCP 8899 | **Not applicable unless the inverter OEM ships an IGEN logger.** I found no evidence that Savx/Voltronic-class units use one [INFERRED]. |
| **SRNE app** | Yes (id1635684571, Bluetooth) | SRNE BT adapter. SRNE-family inverters also appear in Eybond tools. | – | none (BT) | "history view" | BT local | Only useful for SRNE OEM units. Tracking label: Identifiers. |
| **PowMr** | Uses **SmartESS** per PowMr's manual ("monitor device data using the 'SmartEss' mobile app"; "a separate WiFi module needs to be purchased") [CONFIRMED-primary: https://powmr.com/es/powmr-user-manual/]. Some models use the "Sun House" app per reviews [SECONDARY]. | PowMr WIFI-VM etc., RS232. Each module is "only compatible with the inverters below" [CONFIRMED-primary: PowMr product pages]. | **Daraz BD: "PowMr WiFi Module … RS232 … WIFI Port Wifi-VM" ৳3,755 (list ৳4,988), sold by PowMr, out of stock, 6-month warranty** [CONFIRMED-primary: https://www.daraz.com.bd/products/powmr-232-wifi-vm-i315732315.html]. PowMr.com: US$59–89 per module. | Full (Eybond) | as SmartESS | as SmartESS | A WIFI-VM reviewer says it does not work with newer all-in-one models [SECONDARY]. PowMr's SmartESS list includes **POW-HVM3.2H-24V**, the same 3.2 kW/24 V class as the Savx F4. That is a hint only, not evidence of the same OEM [INFERRED]. |
| **Anenji** | Uses SmartESS [SECONDARY: mydealz listing "SmartESS app … RS485"]. Listed among the brands supported by ha-eybond-local and ha-dessmonitor [CONFIRMED-primary READMEs]. | Built-in or external Eybond-type collector | – | Full | as SmartESS | as SmartESS | – |
| **Tuya / Smart Life dongles** | Yes (SmartLife) | Generic Tuya WiFi modules | – | Full (Tuya cloud) | App-dependent | LocalTuya requires HA [INFERRED] | I found no evidence of Tuya modules for Voltronic-class off-grid inverters (search returned none) [SECONDARY/negative]. The app declares tracking data. **Not recommended.** |

**Other BD price references** (not compatible with the Savx, listed for scale):
- EPEVER WiFi 2.4G RJ45 adapter (charge controllers only): ৳3,279 on Daraz. [CONFIRMED-primary Daraz search JSON]
- Growatt ShineWiFi-F: ৳3,000 on BDStall. [SECONDARY: search snippet of https://www.bdstall.com/details/growatt-shinewifi-f-inverter-monitoring-wi-fi-usb-dongle-163826/]

**How to buy a vendor dongle (fallback path) [INFERRED]:**
1. Open the bottom cover and note the port: RJ45 labelled RS232/COM, DB9, a dedicated "WiFi" port, or USB.
2. Ask IPS Bazar whether Savx supplies a WiFi kit. None is listed.
3. Buy only a module with a matching connector, from a seller who accepts returns. Vendors and forum users report model lock-in and dongles that fail to talk to the inverter.

---

## 3. DIY ecosystems

### 3.1 ESPHome (runs on your ESP32-S3)

| Component | What it gives | Notes | Source |
|---|---|---|---|
| `pipsolar` (built into ESPHome) | Voltronic/PIP protocol: grid, AC out, battery, PV values, warning/fault binary sensors, mode text. Raw `last_qpigs` / `last_qpiri` / `last_qmod` / `last_qflag` text. Switches and outputs for priorities and charge voltages. | Commands are queued "up to 10". Many settings are intentionally not exposed because they could damage equipment. On ESP8266 many sensors can exhaust memory, which is not a concern on an S3. | https://esphome.io/components/pipsolar/ [CONFIRMED-primary] |
| `esphome-smg-ii` (syssi, external component) | SMG-II/III, EASUN, ISolar, PowMr, Qoltec, Anenji. "The protocol is Modbus RTU via RS232." | RJ45 pinout: pin 1 TX, pin 2 RX, pin 4 12V, pin 8 GND. Uses MAX3232. ESP32 GPIO16/17. Debug example `baud_rate: 9600`. | https://raw.githubusercontent.com/syssi/esphome-smg-ii/main/README.md [CONFIRMED-primary] |
| `modbus_controller` | A generic Modbus client for any register map | `update_interval` defaults to 60 s. Register types: coil, discrete input, holding, read. | https://esphome.io/components/modbus_controller/ [CONFIRMED-primary] |
| `web_server` (standalone, no HA needed) | Local page and REST API. `/sensor/<name>` returns JSON with `id`, `state` and `value`. `/events` is a Server-Sent-Events stream. | Version 3 adds graphs. `local: true` bundles the JS so the page works offline. `auth:` supports digest auth (recommended). Without auth, anyone on the LAN can OTA-flash the device. **v3 graph history is only the last 50 points, held in browser memory** (`MAX_HISTORY = 50` in `esp-entity-table.ts`). It is not a logger. | https://esphome.io/components/web_server/ , https://esphome.io/web-api/ , https://github.com/esphome/esphome-webserver/blob/main/packages/v3/src/esp-entity-table.ts [CONFIRMED-primary] |
| `http_request` | Can POST JSON to any HTTPS endpoint from the ESP32 (`verify_ssl: true` default on ESP32, Mozilla CA bundle), for example Firebase RTDB REST, ntfy or Telegram | 4.5 s default timeout | https://esphome.io/components/http_request/ [CONFIRMED-primary] |
| `psram` | The ESP32-S3 N16R8 needs `psram: mode: octal` | "8 or 16MB will use octal mode". 120 MHz is not available in octal mode except on ESP-IDF. | https://esphome.io/components/psram/ [CONFIRMED-primary] |
| ESP EyeBond Collector (groove-max) | ESPHome firmware that emulates an Eybond collector locally for HA. "PI30 / Voltronic-style ASCII … validated on real hardware". SMG/Modbus is "available for testing". | Gives no SmartESS cloud or app access. RS232 needs a MAX3232, and RS485 needs a MAX485/MAX3485. | https://github.com/groove-max/esp-eybond-collector [CONFIRMED-primary README] |

**ESPHome standalone vs with HA [INFERRED]:**
- Standalone, ESPHome gives you a live dashboard on your home WiFi, plus REST/SSE endpoints and HTTPS POSTs to a cloud database. This fits the "no home server" constraint.
- With HA, you also get long-term history, the Energy dashboard and push notifications through the Companion app, but HA needs an always-on host.

### 3.2 Home Assistant + Companion iOS app

- The Companion app is free (Nabu Casa, id1099568401) and requires iOS 16.4 or later. Since 2026.8.0 it no longer supports iOS 15. [CONFIRMED-primary iTunes lookup; https://www.home-assistant.io/blog/2026/07/07/companion-app-changing-support-for-apple-platforms/]
- Remote access through Home Assistant Cloud (Nabu Casa, now "Home Assistant Link") costs **US$6.50/month or $65/year**, with a 31-day trial. [CONFIRMED-primary: https://www.nabucasa.com/pricing/]
- **Blocker for this user:** HA needs an always-on server (a Pi, an old laptop or a mini-PC), which the user does not have. Keep it as a future upgrade. [INFERRED]
- Vendor-cloud HA integrations exist if you buy a dongle:
  - ha-dessmonitor: cloud polling "configurable from 1 to 60 minutes", 5-minute default. Local mode polls every 2–60 s. [CONFIRMED-primary]
  - ha-eybond-local and the Antoxa1081 dess-monitor integrations. [CONFIRMED-primary READMEs]

### 3.3 SolarAssistant (Raspberry Pi appliance)

- Prices: "Software only $59", or a device with the software preinstalled for $179 (1.8 GHz) / $229 (2.4 GHz). A Voltronic RS232 USB cable is $29. [CONFIRMED-primary: https://solar-assistant.io/shop]
- Licence: "a one-time purchase still grants you lifetime use". The terms page is now superseded with "software updates are free forever". Licences can be transferred between devices, one active at a time. [CONFIRMED-primary: https://solar-assistant.io/help/updates/terms_update_oct_2024]
- Remote access: you pick a site name at registration and get a URL. It needs no network setup. "We do not store your data in the cloud." An iPhone app is available. [CONFIRMED-primary: https://solar-assistant.io/help/access/overview] "No monthly fees" incl. remote access. [SECONDARY: https://solar-assistant.io/explore/icc snippet]
- Supported brands include Voltronic, MPP Solar, PowMr, Anenji, SRNE, MUST and Easun. "Many inverters are rebranded Voltronic or Deye inverters." It integrates with HA via MQTT/WebSocket/REST. [CONFIRMED-primary: https://solar-assistant.io/]
- Verdict for this user [INFERRED]: this is the most polished option and is reasonably priced. However, it needs a Pi, which you would have to buy, and it does not use the ESP32 you already own.

### 3.4 mpp-solar on a Pi

- This is a Python package for "MPP-Solar and similar inverters" (PIP-4048MS, Axpert MKS 5KVA, …), JK and Daly BMS, and Victron VE.Direct. It has a systemd daemon mode and optional MongoDB, PostgreSQL and Prometheus PushGateway outputs. [CONFIRMED-primary: https://github.com/jblance/mpp-solar README]
- It needs an always-on Linux host, and you have to build the dashboard yourself (InfluxDB/Grafana). It is not a good fit here. [INFERRED]

### 3.5 Hosted IoT dashboards (no server of your own)

| Service | Free tier (quoted) | Fit | Source |
|---|---|---|---|
| **Blynk** | "5 devices, 1 user", "100,000 messages" per month, "1 week data retention". Paid plans start at $29/mo. | Good live iOS UI, but 1-week history is far too short for an energy log. | https://blynk.io/pricing [CONFIRMED-primary] |
| **ThingSpeak** (MathWorks) | "3 million/year" messages, updates "Every 15 seconds" at most, 4 channels, 3 private shares, "small non-commercial projects". Retention is not stated on the page. | Zero-code charts and a decent free history backend. Its web UI is not iPhone-native. | https://thingspeak.mathworks.com/prices/thingspeak_home [CONFIRMED-primary] |
| **Firebase (Spark)** | RTDB: "100" simultaneous connections, 1 GB stored, 360 MB/day download. Firestore: 1 GiB, 50K reads/day, 20K writes/day, 20K deletes/day. Hosting: 10 GB storage, 360 MB/day transfer. Auth: 50K MAU. **Cloud Functions: "Not applicable" on Spark.** FCM: no cost. | Best fit for "free DB + own web app". | https://firebase.google.com/pricing [CONFIRMED-primary] |
| **Supabase (Free)** | 500 MB DB, 5 GB egress, 2 active projects, "Free projects are paused after 1 week of inactivity". | A workable alternative (Postgres/SQL). Continuous writes probably keep it active, but that is unverified. | https://supabase.com/pricing [CONFIRMED-primary; INFERRED on pausing] |

**Firebase capacity math [INFERRED]**, assuming a ~400-byte JSON sample with about 20 fields:
- At 30 s intervals: 2,880 samples/day ≈ 1.15 MB/day ≈ 420 MB/year. The 1 GB RTDB free quota holds about 2.4 years of raw data.
  - Better plan: keep raw 30-s data for 30–90 days and keep 5-minute and daily aggregates indefinitely.
- If you use Firestore with one document per sample, a single day-chart costs 2,880 reads, so the 50K/day read quota allows only about 17 full-day chart views.
  - Batch samples into hourly or daily documents, or use RTDB, which has no per-operation quota.
- **RTDB REST auth from the ESP32:** use `?auth=<ID_TOKEN>` (subject to Security Rules; tokens "expire quickly"), an OAuth2 `access_token` from a service account, or legacy database secrets ("long-lived credentials", which Google advises migrating away from). [CONFIRMED-primary: https://firebase.google.com/docs/database/rest/auth]
- **Alerts without Functions:** the Spark plan has no Cloud Functions, so server-side alert logic is impossible on the free tier. The ESP32 itself must send the alerts. [INFERRED from the pricing page]

### 3.6 Custom Arduino / ESP-IDF firmware and an ESP32 local web UI [INFERRED]

**Pros:**
- Full control over batching, offline buffering in 8 MB PSRAM or flash during internet outages (common in BD), and backfill.
- Direct Firebase writes via an Arduino Firebase library or plain REST.
- Custom fault-bit decoding and an OTA strategy you choose.

**Cons:**
- You must implement and validate the protocol parser yourself, whereas ESPHome's pipsolar or smg-ii components are already tested.

**Pragmatic path:**
1. Use ESPHome first to discover and validate the protocol (one evening).
2. Then either stay on ESPHome with `http_request`, or port the proven command set to custom firmware if you need buffering or backfill.

**ESP32 local web UI:**
- It is reachable only on the home LAN, at `http://<name>.local`.
- An HTTPS Firebase-hosted PWA **cannot fetch `http://` LAN endpoints** because browsers block mixed content. Live data for the cloud PWA must therefore come through Firebase. The ESP page is the "internet is down" fallback.

### 3.7 iPhone surfaces

| Surface | Capability | Limits | Source |
|---|---|---|---|
| **PWA (Add to Home Screen)** | Needs a manifest with `display` set to `standalone` or `fullscreen`. **Web Push for Home-Screen web apps since iOS/iPadOS 16.4.** The permission request must be "in response to direct user interaction". Badging API supported. | Push only works for installed web apps. Users manage permissions per web app. | https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/ [CONFIRMED-primary] |
| PWA storage | Since Safari 17 / iOS 17: origin quota up to 60% of disk for browser apps (15% for other apps), with least-recently-used eviction "when the system is under storage pressure" or after a long time without interaction. `navigator.storage.persist()` is granted by heuristics "like whether the website is opened as a Home Screen Web App". | Do not use the PWA as the system of record. Firebase holds the data and the PWA is a viewer. | https://webkit.org/blog/14403/updates-to-storage-policy/ [CONFIRMED-primary] |
| PWA and ITP 7-day cap | Safari deletes script-writable storage after 7 days of Safari use without interaction. Home-Screen apps keep their own day counter, so this does not hit them the same way. | – | https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/ [CONFIRMED-primary] |
| PWA background | Background Sync, Periodic Background Sync and Background Fetch are **not supported** on iOS. Push is the only background wake-up. | The PWA cannot poll in the background, so alerts must be pushed from elsewhere. | https://firt.dev/notes/pwa-ios/ [SECONDARY] |
| **Apple Shortcuts** | The "Get Contents of URL" action (GET/POST/PUT/PATCH/DELETE, JSON body) can read the ESP32 REST endpoint or the Firebase RTDB `.json` REST URL. | Manual or automation-triggered only. | https://support.apple.com/guide/shortcuts/request-your-first-api-apd58d46713f/ios [CONFIRMED-primary] |
| **Scriptable widgets** | `Request.loadJSON()` fetches JSON for a home-screen widget, for example SOC% and PV W. | iOS controls how often widgets refresh, so they are not real-time [INFERRED]. | https://docs.scriptable.app/request/ [CONFIRMED-primary] |
| **Alerts: ntfy** | iOS app available. The ESP32 can POST to an ntfy topic. | Use a long, unguessable topic name. | https://docs.ntfy.sh/subscribe/phone/ [CONFIRMED-primary] |
| **Alerts: Telegram bot** | Bot API over HTTPS. The ESP32 can call `sendMessage`. | – | https://core.telegram.org/bots/api [CONFIRMED-primary] |
| HA Companion | Native push, widgets, Shortcuts | Needs an HA server | App Store [CONFIRMED-primary] |

---

## 4. Coexistence with a vendor dongle, and using the vendor cloud API

### 4.1 Port-sharing evidence

- "**While this bridge is connected, no other device should poll the inverter on the same serial line. One inverter bus should have one active master.**" [CONFIRMED-primary: https://github.com/groove-max/esp-eybond-collector README]
- On Axpert units, the RJ45 RS232 port and the USB port "use the same serial port on the DSP, so you can't use them independently". In the same thread, a USB device and an RS485 device on one inverter interfered with each other. Another member's claim that both can be used "so long as there are not conflicting commands" is anecdotal. [SECONDARY: powerforum.co.za topic 14589, via search snippet; the site blocks direct fetch]
- SolarAssistant (Voltronic family): "you can use the WiFi dongle cable with a USB serial cable" as an alternative to the RS232 cable. RJ45 pins: "Pin 1 RX, Pin 2 TX, Pin 8 GND". "Each inverter with its own cable." Some units therefore have a separate dongle port, but it is unclear whether that port is electrically independent of the RS232 port. [CONFIRMED-primary: https://solar-assistant.io/help/inverters/growtech/Axpert-GT/rs232; INFERRED on independence]
- Deye/Sunsynk (a different architecture with separate ports): users run the vendor dongle and an ESP32 at the same time on RS485. [SECONDARY: powerforum 8646 snippet] This does not transfer to Voltronic-class units.

### 4.2 Coexistence options (ranked for this user) [INFERRED unless tagged]

| # | Approach | Works? | Risk |
|---|---|---|---|
| 1 | **No dongle. ESP32 is the only master.** | Yes | Lowest risk |
| 2 | Dongle stays the master. The ESP32 **only listens** on the RX lines (inverter TX and dongle TX) through a MAX3232 receiver, and decodes the query/response pairs. | Likely. This is passive sniffing. | You only get data at the dongle's own poll cadence, which is unknown. The ESP32 must never transmit. |
| 3 | Dongle on RS232, ESP32 on USB (or the reverse) | Unreliable on Axpert-class units because they share one UART [SECONDARY] | Garbled replies and lost data |
| 4 | RS232 Y-cable with two masters | **No.** Bus collisions. RS232 is point-to-point. | High |
| 5 | **Read the dongle over the LAN.** The Eybond local callback protocol is used by ha-dessmonitor local mode and ha-eybond-local (§4.3). An ESP32 or HA acts as the TCP server. | Proven in HA integrations [CONFIRMED-primary]. Porting it to an ESP32 is unproven. | ha-eybond-local "Known issue": in Cloud+HA mode "the vendor app stops" updating for some users [CONFIRMED-primary README]. |
| 6 | Replace the dongle firmware or role with ESP EyeBond Collector | Yes, but you lose the SmartESS cloud and app [CONFIRMED-primary] | – |

### 4.3 Eybond local LAN access (documented in source)

- ha-dessmonitor local mode sends a "targeted `set>server=<HA IP>:8899;` callback request". It uses **UDP port 58899** to the collector, and the collector then opens **TCP port 8899** to HA. The mode is read-only and parses PI17/PI18 and PI30 (`QPIGS` with 17 or 21 fields) as well as SMG Modbus (devcode 2376).
  - "Do not expose either port to the internet."
  - In hybrid mode, settings changes still go via the cloud.
  - [CONFIRMED-primary: https://raw.githubusercontent.com/andreas-glaser/ha-dessmonitor/main/docs/LOCAL_MODE.md; `local/announcer.py` "Periodically request a callback from one configured collector"]
- Security implication [INFERRED]: the callback request appears to be unauthenticated UDP. Anything on your LAN could redirect the collector's session, so put IoT devices on a guest or IoT SSID.

### 4.4 Is the DessMonitor/Eybond cloud API usable for a custom dashboard?

**How it works:**
- Endpoint: `https://api.dessmonitor.com/public/`, with auth action `authSource`. ShineMonitor uses `https://ios.shinemonitor.com/public/` with action `auth`. [CONFIRMED-primary: ha-dessmonitor `const.py`]
- Request signing: `sign = SHA1(salt + secret + token + action_string)`. Before login it is `SHA1(salt + SHA1(password) + action_string)`, where `salt` is the time in milliseconds. Requests are GET, of the form `?sign=…&salt=…&token=…&action=…`. [CONFIRMED-primary: ha-dessmonitor `api.py`]
- Actions used in ha-dessmonitor: `queryPlants`, `webQueryCollectorsEs`, `queryCollectorCountEs`, `queryCollectorDevices`, `queryDeviceLastData`, `webQueryDeviceEs`, `queryDeviceCtrlField`, `queryDeviceParsEs`, `queryDeviceCtrlValue`, `ctrlDevice`. [CONFIRMED-primary: api.py]
- History actions in the reverse-engineered ShineMonitor doc: `queryDeviceDataOneDayPaging` (raw day table, used by "Export"), `queryDeviceChartFieldDetailData` (any field over any range), `queryPlantEnergyMonthPerDay`, `queryPlantEnergyYearPerMonth` and `webQueryPlantsWarning` (alarms). [SECONDARY: https://github.com/tarun7singh/ha-shinemonitor/blob/main/docs/shinemonitor-api.md]
- Operational notes from the same doc [SECONDARY]:
  - CORS is "Permissive (`Access-Control-Allow-Origin: *`)".
  - Do not poll faster than the collector's `datFetch` (300 s observed).
  - Rate limiting is "Not documented".
  - "There is no TOTP/2FA".
  - "SHA-1 of the password, once leaked, is as good as the password".

**Verdict [INFERRED]:**
- It works technically. A Firebase-hosted page could even call it directly, given the permissive CORS.
- It would expose your account credential (the password hash) in browser code unless you add a proxy, and a proxy needs Cloud Functions, which Spark does not allow.
- Resolution is 5 minutes, retention is undocumented, the API is unofficial and can change without notice, and I found no official developer program.
- Use it only as a stopgap. It does not make a solid logger.

---

## 5. Recommendation matrix

The scores assume no always-on home server, a free solution, the ESP32-S3 and RS485 module already bought, an iPhone and 2.4 GHz WiFi. Each criterion is scored 1 (poor) to 5 (best). Scores are my judgement [INFERRED], based on the cited facts above.

| Option | Cost | Effort (5 = easy) | Data ownership | Remote access | History depth | Reliability | Alerts | **Total /35** | Hard blockers |
|---|---|---|---|---|---|---|---|---|---|
| **D. ESP32-S3 → Firebase RTDB + Hosted PWA (+ ntfy/Telegram alerts, + ESP local web UI)** | 5 (free tier, ~৳150–350 for a MAX3232) | 2 | 4 | 5 | 4 (1 GB ≈ years with downsampling) | 4 (ESP buffers when offline; no home server) | 4 | **28** | Protocol must be decodable (R1/R2) |
| F. Home Assistant + ESPHome + Companion app | 3 (host needed; remote $6.50/mo or DIY) | 3 | 5 | 3 | 5 | 3 | 5 | 27 | **Needs an always-on host** |
| G. SolarAssistant on a Pi | 2 ($59 + Pi, or $179–229 device, + $29 cable) | 4 | 5 | 5 | 4 | 4 | 3 | 27 | Pi purchase; does not use the ESP32 |
| E. ESP32 → ThingSpeak / Blynk | 5 | 3 | 2 | 5 | 2 (Blynk 1 week; ThingSpeak retention unstated) | 4 | 2–3 | 23–24 | Blynk's short retention |
| A. Vendor dongle + SmartESS/Energy-Mate app | 3 (~৳3,755 PowMr-type; Eybond imports ~US$57–75) | 5 (if compatible) | 1 | 5 | 3 (5-min, undocumented retention) | 3 (cloud; ratings ~2.5–2.7) | 3 | 23 | **Model lock-in; protocol compatibility unknown** |
| C. ESPHome standalone local web UI only | 5 | 4 | 5 | 1 (home WiFi only) | 1 (50 browser points) | 4 | 1 | 21 | No remote access or history |
| H. mpp-solar on a Pi + Influx/Grafana | 3 | 2 | 5 | 2 | 4 | 3 | 2 | 21 | Needs a host |
| B. Vendor dongle + DessMonitor API → custom dashboard | 3 | 2 | 2 | 5 | 3 | 2 (unofficial API) | 3 | 20 | Credential exposure or needs a proxy |

### Recommendation (primary): Option D

1. **Hardware.** Open the F4's bottom cover and identify the comm port.
   - If it is **RS232** (RJ45 or DB9), which the listings suggest, buy a **MAX3232 RS232↔TTL module** (৳137–336 on Daraz BD, for example https://www.daraz.com.bd/products/max3232-rs232-to-ttl-serial-port-converter-module-db9-connector-i1572590799.html).
   - The purchased RS485 module only applies if the optional RS485 card is fitted.
   - Never wire ESP pins directly to RS232 levels. [CONFIRMED-primary: esp-eybond-collector "Do not connect ESP pins directly to RS232"]
2. **Bring-up with ESPHome.**
   - Try `pipsolar` (PI30) or `esphome-smg-ii` / `modbus_controller` (SMG Modbus), depending on what R1/R2 find.
   - Enable `web_server` v3 with `local: true` and digest `auth`, and `psram: mode: octal`.
   - This gives an immediate local dashboard at `http://<name>.local` on the iPhone.
3. **Logging.**
   - Every 30–60 s, POST a compact JSON sample to Firebase RTDB over REST, using `http_request` in ESPHome or custom firmware.
   - Keep a `latest` node for live tiles and time-bucketed history nodes.
   - Have the device or the web app write 5-minute and daily aggregates.
   - Use custom firmware if you want PSRAM buffering and backfill during internet outages.
4. **iPhone UI.**
   - Host a static dashboard on Firebase Hosting and add it to the Home Screen as a PWA.
   - Optionally add a Scriptable widget that reads the RTDB `latest.json` for SOC and PV at a glance.
5. **Alerts.** On fault bits, grid loss, low SOC or overload, the ESP32 POSTs to ntfy or a Telegram bot. This is reliable on iOS and needs no server. Web Push to the PWA is possible on iOS 16.4+, but sending it needs a server or Function that Spark does not have.
6. **Power continuity [INFERRED].** Feed the ESP32's USB charger and the WiFi router from the inverter's AC output, so logging continues during grid outages.

### Fallbacks

- **Fallback A: the protocol is undecodable, or you want zero effort.** Buy a connector-matched Eybond/PowMr-style RS232 WiFi dongle with a return option, and use **SmartESS or Energy-Mate** on iOS (DessMonitor web on a PC). Accept 5-minute cloud data and Eybond cloud dependency.
  - To keep a copy of your data, a later HA box can read the dongle locally (ha-dessmonitor or ha-eybond-local).
  - Alternatively, the ESP32 can passively sniff the dongle's RS232 traffic (§4.2 #2).
- **Fallback B: you get an always-on device later.** Either keep the ESP32 and add **Home Assistant + Companion** (ESPHome native API, long-term statistics, native push), or replace the whole stack with **SolarAssistant** ($59 software on your own Pi, data stays on the device, built-in remote access).

---

## 6. Open items to verify (feeds into the final plan)

1. The physical port type, connector and pin labels under the F4 bottom cover: RJ45 vs DB9 vs dedicated WiFi port vs USB, and whether the port supplies 5–12 V.
2. The protocol (R1/R2): PI30/PI18 ASCII vs SMG Modbus vs proprietary, and the baud rate. Check this before buying anything vendor-side.
3. Whether IPS Bazar or Savx supplies an official WiFi kit. Ask the seller directly (Facebook: facebook.com/Ipsbazardhaka), because none is listed.
4. If a dongle is ever added, whether the F4's USB and RS232 ports share one UART. Test by polling both and watching for CRC errors.

---

## 7. Hardware compatibility flag (purchased parts) [INFERRED]

- **ESP32-S3-N16R8:** suitable. It is 2.4 GHz and needs `psram: mode: octal` in ESPHome.
- **RS485↔TTL auto-direction module:** suitable **only** if the F4 has the optional RS485 port and the protocol runs on RS485. It will not work on an RS232 port, because RS232 and RS485 levels and wiring differ. You will most likely need an additional **MAX3232 module**.
- **USB 5V 2A charger, junction box, jumpers and foam tape:** fine. Mount the box outside the inverter's hot airflow, and keep the WiFi antenna area of the ESP32 away from the metal case.

---

## 8. Sources

**Inverter and seller**
- https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/
- https://ipsbazarbd.com/product/savx-f%e0%a7%aa-1500-12v-solar-inverter-mppt-base/
- https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/
- https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/
- https://ipsbazarbd.com/?s=wifi&post_type=product
- https://bme.com.bd/savx-f3-3500-24v-mppt-base-solar-inverter
- https://www.youtube.com/watch?v=tdCSpWiejBQ (metadata via oEmbed)

**Apps (App Store / iTunes Lookup, US storefront)**
- https://apps.apple.com/us/app/smartess/id1334656760
- https://apps.apple.com/us/app/watchpower/id1487569321
- https://apps.apple.com/us/app/energy-mate/id1672569936
- https://apps.apple.com/us/app/solarpower-wi-fi/id1491698936
- https://apps.apple.com/us/app/smartclient-for-solar/id1070155085
- https://apps.apple.com/us/app/solarman-smart/id1469487897
- https://apps.apple.com/us/app/srne/id1635684571
- https://apps.apple.com/us/app/smart-life-smart-living/id1115101477
- https://apps.apple.com/us/app/home-assistant/id1099568401
- https://itunes.apple.com/lookup?id=1334656760,…&country=us|in|bd
- https://voltronicpower.com/en-US/Company/News/174

**Dongle hardware and pricing**
- https://www.eybond.com/data-communication-type/dtu-plugpro-series.html
- https://www.eybond.com/uploads/upload/20260422/20260422/b71d32b09cd31448a2fc274f33fa04cc.pdf
- https://www.eybond.com/smartess/smartess.html
- https://voltronicpower.com/en-US/Product/Detail/Wifi-Module
- https://www.daraz.com.bd/products/powmr-232-wifi-vm-i315732315.html
- https://www.daraz.com.bd/catalog/?q=max3232
- https://powmr.com/es/powmr-user-manual/
- https://powmr.com/products/powmr-wifi-module-eco-max
- https://powmr.com/products/powmr-wifi-module-wifi-relab
- https://w11stop.com/solar/solar-system-accessories/volnex-wifi-dongle-price-in-pakistan [SECONDARY]
- https://www.bdstall.com/details/growatt-shinewifi-f-inverter-monitoring-wi-fi-usb-dongle-163826/ [SECONDARY, snippet]

**Cloud API and local integrations**
- https://github.com/andreas-glaser/ha-dessmonitor (README, `docs/LOCAL_MODE.md`, `custom_components/dessmonitor/api.py`, `const.py`, `local/announcer.py`)
- https://github.com/Antoxa1081/home-assistant-dess-monitor
- https://github.com/Antoxa1081/home-assistant-dess-monitor-local
- https://github.com/groove-max/ha-eybond-local
- https://github.com/groove-max/esp-eybond-collector
- https://github.com/tarun7singh/ha-shinemonitor/blob/main/docs/shinemonitor-api.md [SECONDARY]
- https://pysolarmanv5.readthedocs.io/en/latest/

**DIY stack**
- https://esphome.io/components/pipsolar/
- https://esphome.io/components/modbus_controller/
- https://esphome.io/components/web_server/
- https://esphome.io/web-api/
- https://esphome.io/components/http_request/
- https://esphome.io/components/psram/
- https://github.com/esphome/esphome-webserver/blob/main/packages/v3/src/esp-entity-table.ts
- https://raw.githubusercontent.com/syssi/esphome-smg-ii/main/README.md
- https://github.com/jblance/mpp-solar
- https://solar-assistant.io/
- https://solar-assistant.io/shop
- https://solar-assistant.io/help/access/overview
- https://solar-assistant.io/help/updates/terms_update_oct_2024
- https://solar-assistant.io/help/inverters/growtech/Axpert-GT/rs232
- https://www.nabucasa.com/pricing/
- https://www.home-assistant.io/blog/2026/07/07/companion-app-changing-support-for-apple-platforms/

**Hosted services and alerts**
- https://blynk.io/pricing
- https://thingspeak.mathworks.com/prices/thingspeak_home
- https://firebase.google.com/pricing
- https://firebase.google.com/docs/database/rest/auth
- https://supabase.com/pricing
- https://docs.ntfy.sh/subscribe/phone/
- https://core.telegram.org/bots/api

**iOS platform**
- https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/
- https://webkit.org/blog/14403/updates-to-storage-policy/
- https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/
- https://firt.dev/notes/pwa-ios/ [SECONDARY]
- https://support.apple.com/guide/shortcuts/request-your-first-api-apd58d46713f/ios
- https://docs.scriptable.app/request/

**Forums** (powerforum.co.za blocks direct fetch, so these come from search-engine snippets) [SECONDARY]
- https://powerforum.co.za/topic/14589-what-kind-of-data-is-available-on-the-different-ports-of-mppvoltronicclones/
- https://powerforum.co.za/topic/16253-wifi-data-logger-for-smartess-app/
- https://powerforum.co.za/topic/19284-wifi-already-exists/
- https://powerforum.co.za/topic/8646-my-sunsynk-8kw-data-collection-setup/page/4/
- https://powerforum.co.za/topic/17000-recommended-software-to-get-data-from-axpert-mks-v4-via-wifi/
