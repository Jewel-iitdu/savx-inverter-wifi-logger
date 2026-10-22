# 09 – Sources

This is a consolidated, de-duplicated list of every source used by the research and verification reports, grouped by topic. The tag in *italics* after each entry shows which report(s) cite it:

| Tag | Report |
|---|---|
| *R1* | OEM identification |
| *R2* | Voltronic PI30 |
| *R3* | Modbus families |
| *R4* | Existing apps |
| *R5* | Hardware |
| *R6* | Cloud and web |
| *V1* | Protocol verification |
| *V2* | Platform verification |
| *D1* | Architecture |
| *R7a*, *R7b*, *R7c*, *R7* | Web hosting: landscape, integration, verification, and the decision |
| *R8a*, *R8b*, *R8c*, *R8* | E-mail alerts: transports, rules and UX, verification, and the decision |

The reports are in [`research/`](../research/), and their text shows exactly what each source was used for. Most sources were fetched on **2026-10-21**.

**How much weight each source carries:**
- Official documents, datasheets, source code, the user's photos and the paper manual are **primary**.
- Forums, listings and search snippets are **[SECONDARY]**.
- Where V1/V2 re-checked a fact, they override R1–R6. Likewise R7c overrides R7a/R7b, and R8c overrides R8a/R8b; the decisions R7 and R8 apply those corrections.

---

## Contents

1. [User-provided primary material](#1-user-provided-primary-material)
2. [Project research files](#2-project-research-files)
3. [Inverter protocol documents](#3-inverter-protocol-documents)
4. [OEM and product identification](#4-oem-and-product-identification)
5. [Open-source implementations](#5-open-source-implementations)
6. [Espressif ESP32-S3 documentation and core sources](#6-espressif-esp32-s3-documentation-and-core-sources)
7. [Toolchain (PlatformIO / pioarduino)](#7-toolchain-platformio--pioarduino)
8. [ESP32 firmware libraries](#8-esp32-firmware-libraries)
9. [Interface hardware, wiring and electrical references](#9-interface-hardware-wiring-and-electrical-references)
10. [Firebase and Google](#10-firebase-and-google)
11. [iOS, WebKit, PWA and charting](#11-ios-webkit-pwa-and-charting)
12. [Alerts and notifications](#12-alerts-and-notifications)
13. [Alternative back-ends and hosting](#13-alternative-back-ends-and-hosting)
14. [Existing apps, vendor dongles and monitoring services](#14-existing-apps-vendor-dongles-and-monitoring-services)
15. [Forums and community threads](#15-forums-and-community-threads)
16. [Shops and prices (Bangladesh)](#16-shops-and-prices-bangladesh)
17. [Measurements and API queries made during research](#17-measurements-and-api-queries-made-during-research)
18. [Web hosting and CI/CD (R7)](#18-web-hosting-and-cicd-r7)
19. [E-mail alerts and Apps Script (R8)](#19-e-mail-alerts-and-apps-script-r8)

---

## 1. User-provided primary material

These are the most authoritative sources for this specific unit.

| Item | What it establishes |
|---|---|
| `reference/Savx-F4-user-manual.pdf` (20 scanned pages; "1.6KW/3.2KW Solar Inverter/Charger", v3.0, part no. 614.C0556-02) | COM/BMS pinouts (§2.3), WatchPower (§3.8), the optional WiFi collector + "SOLAR OF THINGS" (§3.9), LCD (§4), settings programs (§4.4), fault and warning codes (§5/§6), specs (§11–§13) |
| [`research/manual-extract.md`](../research/manual-extract.md) | A text extraction of the manual's facts |
| `docs/images/inverter-bottom-ports.jpg`, `docs/images/ports-label-zoom.jpg` | The bottom panel: the **COM** (free) and **BMS** (battery cable) jacks and their silkscreen pinouts; no USB or WiFi jack |
| `docs/images/nameplate.jpg` | Ratings: 3200 VA/W, 24 V DC 145 A, PV 3500 W, 30–450 V MPPT, Voc 500 V |
| `docs/images/inverter-front.jpg`, `docs/images/lcd-closeup.jpg` | Front panel: round dial, LEDs, keys; LCD icons |
| `docs/images/order-rs485-module.webp`, `docs/images/order-esp32-box.png` | The parts actually owned: HW-0519-type RS485 module (৳117) and the ESP32-S3-N16R8 board |
| [`research/user-constraints.md`](../research/user-constraints.md) | Hard constraints: no purchases, no multimeter, pins 1/2/8 only, BMS untouched |
| [`research/gemini-claims.md`](../research/gemini-claims.md) | Unsourced claims the user received. **Corrected** by V1 C9–C11; see [00 §4.4](00-plan-overview.md) |

---

## 2. Project research files

| File | Content |
|---|---|
| [`research/d1-architecture.md`](../research/d1-architecture.md) | **The approved architecture and decision record** |
| [`research/v1-protocol-verification.md`](../research/v1-protocol-verification.md) | Fact-check of OEM, port and protocol; byte-exact frames (§4.4); discovery procedure (§6) |
| [`research/v2-platform-verification.md`](../research/v2-platform-verification.md) | Fact-check of apps, hardware and cloud; verified free-tier limits; TLS chains; library versions |
| [`research/r1-oem-identification.md`](../research/r1-oem-identification.md) | OEM identification (Sorotec REVO VM II PRO) |
| [`research/r2-voltronic-pi30.md`](../research/r2-voltronic-pi30.md) | The Voltronic PI30 protocol family in depth |
| [`research/r3-modbus-families.md`](../research/r3-modbus-families.md) | Modbus families: SMG, PowMr, SRNE, MUST, 8851 |
| [`research/r4-existing-apps.md`](../research/r4-existing-apps.md) | Existing apps and dongles vs DIY |
| [`research/r5-hardware.md`](../research/r5-hardware.md) | ESP32-S3 board, RS485 module, power, enclosure |
| [`research/r6-cloud-web.md`](../research/r6-cloud-web.md) | Firebase quotas, data model, PWA, alerts, alternatives |
| [`research/r7a-hosting-landscape.md`](../research/r7a-hosting-landscape.md) | Free static hosts compared: limits, behaviour at the limit, terms, PoPs |
| [`research/r7b-hosting-integration.md`](../research/r7b-hosting-integration.md) | How each host fits this PWA: headers, CSP, SPA routing, previews, API-key restrictions, CI workflows, latency samples |
| [`research/r7c-hosting-verification.md`](../research/r7c-hosting-verification.md) | Fact-check of R7a/R7b (corrections C1–C11, confirmations 1–25) |
| [`research/r7-web-hosting.md`](../research/r7-web-hosting.md) | **The hosting and CI/CD decision**: Cloudflare Workers static assets, Firebase Hosting fallback, GitHub Actions workflows, secrets, rollback |
| [`research/r8a-email-transports.md`](../research/r8a-email-transports.md) | Free e-mail transports compared (Apps Script, SMTP, Brevo, Resend, EmailJS, …) |
| [`research/r8b-email-rules-ux.md`](../research/r8b-email-rules-ux.md) | Rule model, RTDB schema, web Alerts page, e-mail content |
| [`research/r8c-email-verification.md`](../research/r8c-email-verification.md) | Fact-check of R8a/R8b (critical C1–C3, major M1–M10) |
| [`research/r8-email-alerts.md`](../research/r8-email-alerts.md) | **The e-mail usage-threshold alerts decision**: Apps Script mailer, data contract, rules, firmware spec, acceptance tests |
| `r2-code/pi30.h`, `r2-code/sketch_core.h` (session scratchpad) | Original PI30 CRC/frame helpers and tests, ported into `firmware/` |

---

## 3. Inverter protocol documents

### 3.1 Voltronic PI30 family and relatives

Mirrored in the mpp-solar repository.

- [PI30 "HS_MS_MSX RS232 Protocol 20140822 after current upgrade"](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf). The base PI30 field maps (QPIGS, QPIRI, QPIWS). A second copy is on [forums.aeva.asn.au](https://forums.aeva.asn.au/uploads/293/HS_MS_MSX_RS232_Protocol_20140822_after_current_upgrade.pdf). *(D1, R1, R2, V1)*
- [PI30 Communication Protocol 2015-09-24 (Customer)](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Communication-Protocol-20150924-Customer.pdf) *(D1, R2, V1)*
- [PI30MAX Communication Protocol 2021-02-17](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30MAX.Communication.Protocol20210217.pdf). The 24/28-field dialect; its fault wording matches the Savx manual. *(D1, R2, V1)*
- [PI30 PIP-GK/MK protocol](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_PIP-GK_MK-Protocol.pdf) *(R2)*
- [PI30 Q1 definition](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30_Q1%20definition.md) *(R2)*
- [Sorotec "PI30REVO" protocol V03 (2020-11-12)](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI30REVO.20Protocol.20V03--20201112.pdf). `QALL`, sum+1 CHK, QPIGS field 12 = PV W, warning 00–07/21–26, fault 01–12. *(D1, R1, R2, V1)*
- [PI16 (Voltronic SUNNY)](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI16_VoltronicPowerSUNNYProtocol.md), [PI17 (InfiniSolar 5KW, 2016)](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI17_InfiniSolar-5KW-protocol-20160309.pdf), [PI18 (InfiniSolar V, 2017)](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI18_InfiniSolar-V-protocol-20170926.pdf), [PI41 (LV5048, 2019)](https://raw.githubusercontent.com/jblance/mpp-solar/master/docs/protocols/PI41_LV5048.5KW.protocol-20190222.for.customer.pdf) *(R2)*

### 3.2 Modbus-family protocol documents

These are last-resort probes only.

- [SMG RS232 Communication Protocol V1.0.1](https://github.com/syssi/esphome-smg-ii/blob/main/docs/SMG-RS232%2BCommunication%2BProtocol%2BV1.0.1.pdf) *(R3)*
- [GM6200 Communication Protocol 2 (CN)](https://github.com/syssi/esphome-smg-ii/blob/main/docs/GM6200%2BCommunication%2BProtocol%2B2_CN.html) *(R3)*
- [SRNE Solar Charge Inverter MODBUS Protocol 1.96](https://github.com/syssi/esphome-smg-ii/blob/main/docs/SRNE.Solar.Charge.Inverter.MODBUS.Protocol1.96.pdf) *(R3, V1)*
- [SRNE hybrid solar inverter Modbus protocol v1.7](https://github.com/RAR/esphome-srne-inverter/blob/main/srne-hybrid-solar-inverter-modbus-protocol-v1-7.pdf) *(R3)*
- [SRNE fault codes table (CSV)](https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485/blob/main/docs/MODBUS%20Protocol%20for%20Energy%20Storage%20Inverter%20-%20Fault%20Codes%20Table.csv) *(R3)*
- [MUST PH1800/PV1800/EP1800/PV3500/EP3500 RS485 Modbus RTU protocol 1.4.15 (xlsx)](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/docs/PH1800%20PV1800%20EP1800%20PV3500%20EP3500%20RS485%20Modbud%20RTU%20communication%20Protocol%201.4.15.xlsx) *(R3)*
- [Anenji "Modbus_RTU RS232 communication protocol V1.0" (220209)](https://github.com/user-attachments/files/20907869/RS232.Communication.Protocol.V1.0-220209.pdf) *(R3)*

### 3.3 Transceiver datasheet

- [TI MAX3232 datasheet (SLLS410)](https://www.ti.com/lit/ds/symlink/max3232.pdf). RS232 receiver thresholds (V_IT+ ≤ 2.4 V, V_IT− ≥ 0.6 V, 3–7 kΩ input), which justify RS232-lite. *(D1, R5, V1)* The [product page](https://www.ti.com/product/MAX3232) is cited by *(R2)*.

---

## 4. OEM and product identification

### 4.1 Sorotec (probable platform)

- [Sorotec REVO VM II PRO 1.6–10 kW product page](https://www.soropower.com/product/off-grid-revo-vm-iipro-series-1-6-3-2-4-6-8-10kw/) *(D1, R1, V1)*
- Sorotec 2025 spec images: [part 1](https://www.soropower.com/wp-content/uploads/2025/01/0e9d9c96421b6fdc9130eb0ea6ad422.png) and [part 2](https://www.soropower.com/wp-content/uploads/2025/01/3ce086e72ac3589270e34a76a8232df.png); [bottom-view render](https://www.soropower.com/wp-content/uploads/2025/01/03-底部-768x768.webp) *(R1, V1)*
- [Sorotec REVO VP/VM datasheet (2024)](https://www.soropower.com/wp-content/uploads/2024/08/REVO-VP-VM.pdf) and [REVO VM II PRO US 2.2 kW datasheet](https://www.soropower.com/wp-content/uploads/2026/06/REVO-VM-II-PRO-US-2.2KW.pdf) *(R1)*
- [Older Sorotec REVO VM II Pro 3.5/5.5 kW (120–450 V DC)](https://www.sorosolar.com/wide-range-120-450vdc-new-style-sorotec-revo-vm-ii-pro-3-5kw-5-5kw-off-grid-solar-inverter-product/) *(R1)*
- [Sorotec-branded "Wi-Fi-Plug-Pro-03" (Qatar listing)](https://alannabi.qa/product/wifi-module-for-solar-inverters-sorotec-brand-wi-fi-plug-pro-03/) *(R1)* [SECONDARY]

### 4.2 Savx and IPS Bazar (seller)

- [IPS Bazar: Savx F4 3000-24V (MPPT base) listing](https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/). The listing contradicts itself (its title says F3), so treat it as a hint only. *(D1, R1, R4, V1, V2)*
- [Savx F3 datasheet PDF "SAVX-2025-01-08"](https://ipsbazarbd.com/wp-content/uploads/2025/03/SAVX-2025-01-08.pdf). Contains Sorotec's "REVO VM II PRO series" text. *(D1, R1, V1)*
- Other Savx listings: [F3 2500-24V](https://ipsbazarbd.com/product/savx-solar-inverter-f3-2500-24v-mppt-base/) *(R1, R4, R5)*, [F3 3500-24V](https://ipsbazarbd.com/product/savx-solar-inverter-f3-3500-24v-mppt-base/) *(R1, R4, R5)*, [F4 1500-12V](https://ipsbazarbd.com/product/savx-f%e0%a7%aa-1500-12v-solar-inverter-mppt-base/) *(R1, R4)*
- [Savx F2 100Ah 24V lithium battery PDF](https://ipsbazarbd.com/wp-content/uploads/2024/11/Savx-F2-100Ah-24V-Lithium-Battery.pdf) *(R1)*
- F4 product images: [1](https://ipsbazarbd.com/wp-content/uploads/2026/02/Savx-f4-Solar-Inverter.jpg), [2](https://ipsbazarbd.com/wp-content/uploads/2026/04/gthdeee-1.jpg) *(R1)*
- Models ruled out (sold by IPS Bazar): [Crown Xavier 3KW 24V](https://ipsbazarbd.com/product/crown-xavier-3kw-24v-solar-inverter/), [SAKO Sunon Pro 3.5kW](https://ipsbazarbd.com/product/sunonpro-3-5kw-off-grid-solar-inverter/) *(R1)*
- [IPS Bazar store](https://ipsbazarbd.com/) and [its search for "wifi"](https://ipsbazarbd.com/?s=wifi&post_type=product). No Savx WiFi accessory was found. *(R4)*
- BME (another seller): [Savx brand page](https://bme.com.bd/electrical-equipment/inverter/savx), [Savx F3 3500-24V](https://bme.com.bd/savx-f3-3500-24v-mppt-base-solar-inverter) *(R1, R4)*
- [YouTube: "Savx Solar Inverter Price in BD"](https://www.youtube.com/watch?v=tdCSpWiejBQ) *(R1, R4)* [SECONDARY]

### 4.3 Related or comparison products

- [BestEn 3.5/5.5 kW manual (same "614.C" manual family, "RevoMonitor" app)](https://deps.ua/downloads/Documentation/besten/autonomous-sm-1p-32kw-24v/BestEn_Autonomous-sm-1p-32kw-24v_EN.pdf) *(V1)*
- [Tripp Lite APSWX 230 V owner's manual (Voltronic-style RJ45)](https://tripplite.eaton.com/support/owners-manual/932564) *(R2, V1)*
- Voltronic Axpert VM: [Bimble docs page](https://www.bimblesolar.com/docs/Axpert_VM) and [VM II Premium datasheet](https://www.bimblesolar.com/docs/Axpert_VM%20II%20Premium%20datasheet.pdf) *(R1)*
- [MUST PV1800 PRO](https://www.mustpower.com/product/pv1800-pro-series-26-2kw/) *(R1)*
- PowMr: [3000 W 24 V all-in-one](https://powmr.com/products/all-in-one-inverter-charger-3000watt-220vac-24vdc) *(R1, R3)*, [user manuals page](https://powmr.com/es/powmr-user-manual/) *(R4)*
- [Y&H 3200VA (eBay listing)](https://www.ebay.com/itm/226715729314) *(R3)* [SECONDARY]

---

## 5. Open-source implementations

### 5.1 PI30 / Voltronic family

- [jblance/mpp-solar](https://github.com/jblance/mpp-solar): the reference Python implementation and protocol PDFs. *(D1, R3, R4)*
  - Files: [pi30.py](https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/pi30.py) *(R3)*, [protocol_helpers.py](https://github.com/jblance/mpp-solar/blob/master/mppsolar/protocols/protocol_helpers.py) *(R3)* ([raw](https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/protocol_helpers.py) *(R1, V1)*), [pi30revo.py](https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/pi30revo.py) *(R1, V1)*, [abstractprotocol.py](https://raw.githubusercontent.com/jblance/mpp-solar/master/mppsolar/protocols/abstractprotocol.py) *(V1)*, [package tree](https://github.com/jblance/mpp-solar/tree/master/mppsolar) *(R2)*
  - Threads: [issue #11 (QPGS / LV5048)](https://github.com/jblance/mpp-solar/issues/11) *(R2)*, [discussion #78 (REVO II / iGrid VE II)](https://github.com/jblance/mpp-solar/discussions/78) *(R1, V1)*
- ESPHome `pipsolar`: [docs](https://esphome.io/components/pipsolar/) *(D1, R2, R4)*, [source tree](https://github.com/esphome/esphome/tree/dev/esphome/components/pipsolar) *(R2)*, [pipsolar.cpp](https://raw.githubusercontent.com/esphome/esphome/dev/esphome/components/pipsolar/pipsolar.cpp) *(V1)*
- [syssi/esphome-pipsolar](https://github.com/syssi/esphome-pipsolar) *(D1, R2, R3, R5)*
  - [README](https://raw.githubusercontent.com/syssi/esphome-pipsolar/main/README.md) *(R1, V1)*
  - Issues: [#111 (MAX485 on an RS232 port)](https://github.com/syssi/esphome-pipsolar/issues/111), [#166 (Voltronic 24V/3kW no data)](https://github.com/syssi/esphome-pipsolar/issues/166) *(R2)*; [#169 (Sorotec 8 kW at 2400 baud)](https://github.com/syssi/esphome-pipsolar/issues/169) *(R1)*
- [softwarecrash/Solar2MQTT](https://github.com/softwarecrash/Solar2MQTT): **reference only**, licence CC BY-NC-ND, no code copied. *(D1, R2, V1)* Its [Reported Working Devices wiki](https://raw.githubusercontent.com/wiki/softwarecrash/Solar2MQTT/Reported-Working-Devices.md) is cited by *(R2)*.
- [ned-kelly/docker-voltronic-homeassistant](https://github.com/ned-kelly/docker-voltronic-homeassistant) *(R2)*: [inverter.cpp](https://raw.githubusercontent.com/ned-kelly/docker-voltronic-homeassistant/master/sources/inverter-cli/inverter.cpp) *(V1)*, [inverter.conf](https://raw.githubusercontent.com/ned-kelly/docker-voltronic-homeassistant/master/config/inverter.conf) *(R5)*
- [Network UPS Tools `nutdrv_qx.c`](https://github.com/networkupstools/nut/blob/master/drivers/nutdrv_qx.c) *(R2)*
- Legacy Q1/Q6 (Voltronic RP/RS): [aguglie/esphome-rp6000](https://github.com/aguglie/esphome-rp6000), [syssi esp8266-voltronic-rp-rs.yaml](https://github.com/syssi/esphome-config-examples/blob/main/esp8266-voltronic-rp-rs.yaml) *(R2)*
- [GitHub community discussion #49529 (mentions the Sorotec V02 protocol)](https://github.com/orgs/community/discussions/49529) *(R1)* [SECONDARY]

### 5.2 Modbus families

- **SMG / Easun / Anenji:** [syssi/esphome-smg-ii](https://github.com/syssi/esphome-smg-ii) *(D1, R3, R5, V1)*
  - [README](https://github.com/syssi/esphome-smg-ii/blob/main/README.md) *(R3)* ([raw](https://raw.githubusercontent.com/syssi/esphome-smg-ii/main/README.md) *(R4)*)
  - Examples: [esp32](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-example.yaml), [gm6200](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-gm6200-example.yaml), [s2-mini](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-s2-mini-usb-a-example.yaml), [srne-asf](https://github.com/syssi/esphome-smg-ii/blob/main/esp32-srne-asf-example.yaml) *(R3)*
  - Issues: [list](https://github.com/syssi/esphome-smg-ii/issues), [#66](https://github.com/syssi/esphome-smg-ii/issues/66), [#80](https://github.com/syssi/esphome-smg-ii/issues/80), [#88](https://github.com/syssi/esphome-smg-ii/issues/88), [#101](https://github.com/syssi/esphome-smg-ii/issues/101), [#130](https://github.com/syssi/esphome-smg-ii/issues/130) *(R3)*
- **PowMr:**
  - [odya/esphome-powmr-hybrid-inverter](https://github.com/odya/esphome-powmr-hybrid-inverter) *(D1, R3, V1)*, with its [registers map](https://github.com/odya/esphome-powmr-hybrid-inverter/blob/main/docs/registers-map.md) and [inverter.yaml](https://github.com/odya/esphome-powmr-hybrid-inverter/blob/main/src/modules/inverter.yaml) *(R3)*
  - [leodesigner/powmr_comm](https://github.com/leodesigner/powmr_comm) *(D1, R2, R3)*
  - [leodesigner/powmr4500_comm](https://github.com/leodesigner/powmr4500_comm) and its [inv8851.h](https://github.com/leodesigner/powmr4500_comm/blob/main/include/inv8851.h) *(R3)*
  - [lufton/esphome-inv-8851](https://github.com/lufton/esphome-inv-8851) *(R3)*
- **SRNE:** [RAR/esphome-srne-inverter](https://github.com/RAR/esphome-srne-inverter) (and its [WIRING.md](https://github.com/RAR/esphome-srne-inverter/blob/main/WIRING.md)), [phinix-org/SRNE-inverters-by-modbus-rs485](https://github.com/phinix-org/SRNE-inverters-by-modbus-rs485), [HotNoob PythonProtocolGateway SRNE maps](https://github.com/HotNoob/PythonProtocolGateway/tree/main/protocols/srne) *(R3)*
- **MUST:**
  - [vladyspavlov/esphome-must-inverter](https://github.com/vladyspavlov/esphome-must-inverter) *(R3, V1)*, with its [docs](https://github.com/vladyspavlov/esphome-must-inverter/tree/main/docs), [esp32-pv18.yaml](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/esp32-pv18.yaml), [C6 display notes](https://github.com/vladyspavlov/esphome-must-inverter/blob/main/esp32-c6-pv19-display.md) and [discussion #9](https://github.com/vladyspavlov/esphome-must-inverter/discussions/9) *(R3)*
  - [mukaschultze/ha-must-inverter](https://github.com/mukaschultze/ha-must-inverter): [const.py](https://github.com/mukaschultze/ha-must-inverter/blob/master/custom_components/must_inverter/const.py), [`__init__.py`](https://github.com/mukaschultze/ha-must-inverter/blob/master/custom_components/must_inverter/__init__.py), [mapper.py](https://github.com/mukaschultze/ha-must-inverter/blob/master/custom_components/must_inverter/mapper.py) *(R3)*
  - [aquarat/must_inverter](https://github.com/aquarat/must_inverter), [xxx87/must-inverter-mon](https://github.com/xxx87/must-inverter-mon), [weby-homelab/Must-Inverter-Dashboard](https://github.com/weby-homelab/Must-Inverter-Dashboard) *(R3)*
- **Anenji and others:**
  - [PreK/esphome-anenji-11kw-native-modbus](https://github.com/PreK/esphome-anenji-11kw-native-modbus), [jul-ienfr/esphome-anenji-4.2kw-wifi](https://github.com/jul-ienfr/esphome-anenji-4.2kw-wifi), [psalkiewicz/esphome-2026-anenji4200](https://github.com/psalkiewicz/esphome-2026-anenji4200) *(R3)*
  - [davidrapan/ha-solarman](https://github.com/davidrapan/ha-solarman) and its [anenji_hybrid.yaml](https://github.com/davidrapan/ha-solarman/blob/main/custom_components/solarman/inverter_definitions/anenji_hybrid.yaml) *(R3)*
  - [duwke/Y-H-5000U](https://github.com/duwke/Y-H-5000U), [aogden24-byte/esphome-datouboss](https://github.com/aogden24-byte/esphome-datouboss) *(R3)*

### 5.3 Eybond, DessMonitor and SmartESS ecosystem

- [groove-max/esp-eybond-collector](https://github.com/groove-max/esp-eybond-collector) *(R3, R4)*, with its [FLASHING.md](https://github.com/groove-max/esp-eybond-collector/blob/main/docs/FLASHING.md) *(R3)*
- [groove-max/ha-eybond-local](https://github.com/groove-max/ha-eybond-local) *(R3, R4)*. Files cited *(R3)*:
  - Code: [collector/protocol.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/collector/protocol.py), [drivers/smg.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/drivers/smg.py), [drivers/srne.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/drivers/srne.py), [payload/ascii_line.py](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/payload/ascii_line.py)
  - Catalogs: [inverter_catalog.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/inverter_catalog.json), [modbus_smg/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/modbus_smg/base.json), [modbus_smg protocol 3.10](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/modbus_smg/protocols/communication_protocol_3_10.json), [srne_modbus/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/srne_modbus/base.json), [sumry_ges_7530/base.json](https://github.com/groove-max/ha-eybond-local/blob/main/custom_components/eybond_local/protocol_catalogs/register_schemas/sumry_ges_7530/base.json)
  - Model sources: [community GES Modbus v2.63](https://github.com/groove-max/ha-eybond-local/blob/main/catalog/inverter_models/sources/community_ges_modbus_v263.json), [JSD Solar G-ASCII](https://github.com/groove-max/ha-eybond-local/blob/main/catalog/inverter_models/sources/documentation_jsdsolar_g_ascii_protocol.json)
- DessMonitor integrations:
  - [SilverFire/dessmonitor-homeassistant](https://github.com/SilverFire/dessmonitor-homeassistant) *(R1)*, whose [README](https://raw.githubusercontent.com/SilverFire/dessmonitor-homeassistant/main/README.md) lists "Sorotec REVO VM II PRO 3.2kW/24V Wi-Fi" *(V1)*
  - [andreas-glaser/ha-dessmonitor](https://github.com/andreas-glaser/ha-dessmonitor) and its [LOCAL_MODE.md](https://raw.githubusercontent.com/andreas-glaser/ha-dessmonitor/main/docs/LOCAL_MODE.md) *(R4)*
  - [Antoxa1081/home-assistant-dess-monitor](https://github.com/Antoxa1081/home-assistant-dess-monitor) and [-local](https://github.com/Antoxa1081/home-assistant-dess-monitor-local) *(R4)*
- [tarun7singh/ha-shinemonitor API notes](https://github.com/tarun7singh/ha-shinemonitor/blob/main/docs/shinemonitor-api.md) *(R4)*
- [Paxy/SmartESS-proxy](https://github.com/Paxy/SmartESS-proxy), [ubombi/ha-smartess-local](https://github.com/ubombi/ha-smartess-local), [samuelolteanu/Local-Cloud-Bridge-for-Anenji-Easun-MPP-Solar-Inverters](https://github.com/samuelolteanu/Local-Cloud-Bridge-for-Anenji-Easun-MPP-Solar-Inverters) *(R3)*
- [pysolarmanv5 docs](https://pysolarmanv5.readthedocs.io/en/latest/) *(R4)*

### 5.4 ESPHome (cross-check tool only)

- [UART (inverted pins)](https://esphome.io/components/uart/) *(V2)*, [http_request](https://esphome.io/components/http_request/) *(R4, V2)*, [modbus](https://esphome.io/components/modbus/) *(R3)*, [modbus_controller](https://esphome.io/components/modbus_controller/) *(R4)*, [psram](https://esphome.io/components/psram/) *(R4)*, [web_server](https://esphome.io/components/web_server/) *(R4)*, [web API](https://esphome.io/web-api/) *(R4)*, [esphome-webserver entity table](https://github.com/esphome/esphome-webserver/blob/main/packages/v3/src/esp-entity-table.ts) *(R4)*

---

## 6. Espressif ESP32-S3 documentation and core sources

### 6.1 Datasheets and guides

- [ESP32-S3-WROOM-1/1U datasheet v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf). PSRAM pins IO35–37, 65 °C rating, 355 mA TX peak, ≥ 0.5 A supply. *(D1, R5, V1, V2)*
- [ESP32-S3 SoC datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) *(R5)* and the [product page](https://www.espressif.com/en/products/socs/esp32-s3) *(R6)*
- [ESP32-S3-DevKitC-1 v1.1 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html). Header J1 pins (GPIO17 U1TXD, GPIO18 U1RXD), RGB LED GPIO48/38. *(D1, R2, R5, V2)*
- [DevKitC-1 v1.1 board outline (DXF)](https://dl.espressif.com/dl/schematics/esp_idf/DXF_ESP32-S3-DevKitC-1_V1.1_20220429.dxf) *(R5)*
- [ESP32-S3 PCB layout design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html) *(R5)*
- ESP-IDF API references: GPIO (stable *(R2)*, latest *(R3)*), watchdogs *(D1, R6)*, mbedTLS *(D1, R6)*
- [esp-faq: USB](https://raw.githubusercontent.com/espressif/esp-faq/master/docs/en/software-framework/peripherals/usb.rst) *(R5)*

### 6.2 arduino-esp32 core (3.3.12)

- [Repository](https://github.com/espressif/arduino-esp32) *(D1)*, [releases](https://github.com/espressif/arduino-esp32/releases) *(R5, R6)* ([API](https://api.github.com/repos/espressif/arduino-esp32/releases) *(R5)*), [board package index](https://espressif.github.io/arduino-esp32/package_esp32_index.json) *(R5)*, [installing.rst](https://raw.githubusercontent.com/espressif/arduino-esp32/master/docs/en/installing.rst) *(R5)*
- `HardwareSerial.h`: inversion API (`setRxInvert`/`setTxInvert`) and default pins. Copies: [3.3.12](https://raw.githubusercontent.com/espressif/arduino-esp32/3.3.12/cores/esp32/HardwareSerial.h) *(V2)*, [master raw](https://raw.githubusercontent.com/espressif/arduino-esp32/master/cores/esp32/HardwareSerial.h) *(R5)*, [master blob](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/HardwareSerial.h) *(V1)*
- [esp32-hal-uart.c](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-uart.c) *(V1)*, [main.cpp](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/main.cpp) *(R6)*
- `boards.txt`: [3.3.12](https://raw.githubusercontent.com/espressif/arduino-esp32/3.3.12/boards.txt) *(V2)*, [master raw](https://raw.githubusercontent.com/espressif/arduino-esp32/master/boards.txt) *(R5)*, [master blob](https://github.com/espressif/arduino-esp32/blob/master/boards.txt) *(R6)*
- [Partition tables](https://github.com/espressif/arduino-esp32/tree/master/tools/partitions) (`default_16MB.csv`) *(R5, R6)*
- Library sources and examples *(R6)*: [ArduinoOTA BasicOTA](https://github.com/espressif/arduino-esp32/blob/master/libraries/ArduinoOTA/examples/BasicOTA/BasicOTA.ino), [ResetReason](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/ResetReason/ResetReason/ResetReason.ino), [SimpleTime](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino), [mDNS_Web_Server](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS/examples/mDNS_Web_Server/mDNS_Web_Server.ino), [HTTPClient.h](https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPClient/src/HTTPClient.h), [httpUpdateSecure](https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPUpdate/examples/httpUpdateSecure/httpUpdateSecure.ino), [LittleFS.h](https://github.com/espressif/arduino-esp32/blob/master/libraries/LittleFS/src/LittleFS.h), [NetworkClientSecure README](https://github.com/espressif/arduino-esp32/blob/master/libraries/NetworkClientSecure/README.md), [WiFiClientSecure.h](https://github.com/espressif/arduino-esp32/blob/master/libraries/NetworkClientSecure/src/WiFiClientSecure.h), [HTTPS_OTA_Update](https://github.com/espressif/arduino-esp32/blob/master/libraries/Update/examples/HTTPS_OTA_Update/HTTPS_OTA_Update.ino), [WiFiProv](https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFiProv/examples/WiFiProv/WiFiProv.ino)
- Also read by V2 at tag 3.3.12: `WiFiGeneric.h` (TX power enum), `NetworkClientSecure.h` and `esp32-hal-misc.c` (`temperatureRead()`)

### 6.3 Prebuilt configuration and IDF

- esp32-arduino-lib-builder configs ([tree](https://github.com/espressif/esp32-arduino-lib-builder/tree/master/configs) *(D1, R6)*; [defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common), [defconfig.esp32s3](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.esp32s3) *(R6)*). These show dynamic mbedTLS buffers, the SNTP interval and TWDT panic.
- ESP-IDF release/v5.5 `components/mbedtls/Kconfig` (`CONFIG_MBEDTLS_HAVE_TIME_DATE` defaults to n) and `components/esp_psram/esp32s3/Kconfig.spiram` (PSRAM ECC) *(V2)*
- [esp8266 `TZ.h`](https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h): the POSIX TZ string `<+06>-6` for Dhaka *(R6)*

---

## 7. Toolchain (PlatformIO / pioarduino)

- [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) *(D1, R5, R6)*
  - The pinned [release 55.03.312-1 asset](https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip) *(D1, R5, R6)* and its [release API record](https://api.github.com/repos/pioarduino/platform-espressif32/releases/tags/55.03.312-1) *(R5)*
  - [README](https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/README.md) *(V2)*, [board `esp32-s3-devkitc1-n16r8.json`](https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/boards/esp32-s3-devkitc1-n16r8.json) *(R5, V2)*, [builder/main.py](https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/builder/main.py) *(R5)*
- [Official PlatformIO board `esp32-s3-devkitc-1.json`](https://raw.githubusercontent.com/platformio/platform-espressif32/develop/boards/esp32-s3-devkitc-1.json). The 8 MB, no-PSRAM board, which is why it is not used. *(R5, V2)*
- [PlatformIO forum: S3 upload failures / CH34x on macOS](https://community.platformio.org/t/fails-to-upload-to-esp32s3-wroom-1/38152/29) *(R5)* [SECONDARY]

---

## 8. ESP32 firmware libraries

- [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson): v7.4.3, MIT *(D1, R6)*
- [tzapu/WiFiManager](https://github.com/tzapu/WiFiManager): v2.0.17, MIT *(D1, R6)*
- [ayushsharma82/ElegantOTA](https://github.com/ayushsharma82/ElegantOTA): v4.0.0, AGPL-3.0. Its README says sync and async web servers cannot be mixed. *(D1, R6, V2)*
- [ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer): v3.12.1, LGPL-3.0. **Not used** (sync stack chosen). *(R6)*
- [mobizt/FirebaseClient](https://github.com/mobizt/FirebaseClient): v2.2.13, MIT, the **fallback** cloud client *(D1, R6, V2)*
  - [README](https://raw.githubusercontent.com/mobizt/FirebaseClient/main/README.md) *(V2)*, [library.properties](https://raw.githubusercontent.com/mobizt/FirebaseClient/main/library.properties) *(R6, V2)*, [releases API](https://api.github.com/repos/mobizt/FirebaseClient/releases) *(R6)*
  - Examples *(R6)*: [BareMinimum RTDB](https://github.com/mobizt/FirebaseClient/blob/main/examples/BareMinimum/RealtimeDatabase/RealtimeDatabase.ino), [RTDB Update](https://github.com/mobizt/FirebaseClient/blob/main/examples/RealtimeDatabase/Update/Update.ino), [Firestore Documents](https://github.com/mobizt/FirebaseClient/tree/main/examples/FirestoreDatabase/Documents)
  - `src/database/RealtimeDatabase.h`: `print=silent` applies only to sync calls *(V2)*

---

## 9. Interface hardware, wiring and electrical references

### 9.1 RS485 / RS232 modules

- [ProtoSupplies: TTL to RS-485 module (HW-0519 class)](https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/). 4.7 kΩ A/B bias, TVS diodes, polyfuses, R0 open. *(D1, R5, V2)* [SECONDARY]
- [ProtoSupplies: MAX3232 TTL to RS-232 module](https://protosupplies.com/product/max3232-ttl-to-rs-232-interface-module/) *(R5)*
- [Arduino forum: MAX485 vs HW-0519 modules](https://forum.arduino.cc/t/difference-between-the-max485-and-the-hw-0519-modules/704175) *(R5, V2)* [SECONDARY]
- [syssi/esphome-modbus-solax-x1 README (HW-0519 wiring, 3.3 V supply)](https://github.com/syssi/esphome-modbus-solax-x1/blob/main/README.md) *(R5)* ([raw](https://raw.githubusercontent.com/syssi/esphome-modbus-solax-x1/main/README.md) *(V2)*)
- [codey.online HW-0519 page](https://codey.online/components/hw-0519-auto-flow-rs485-module) *(R5)* [SECONDARY, low reliability]
- [MakerLab PH: TTL to RS485 auto-direction module](https://makerlab.ph/products/ttl-to-rs485-power-supply-converter-module-hardware-auto-control-for-arduino-avr-3-3v-5-0v) *(R5)* [SECONDARY]
- [esp32io: ESP32 RS485 tutorial](https://esp32io.com/tutorials/esp32-rs485) *(R5)* [SECONDARY]

### 9.2 ESP32-S3 clone boards

- [Arduino forum: Chinese ESP32-S3 5 V pin warning / RGB pad](https://forum.arduino.cc/t/chinese-esp32-s3-5v-pin-warning/1192758) *(R5)* [SECONDARY]
- [Mischianti: VCC-GND YD-ESP32-S3 clone pinout](https://mischianti.org/vcc-gnd-studio-yd-esp32-s3-devkitc-1-clone-high-resolution-pinout-and-specs/) *(R5)* [SECONDARY]
- [deepcoder/YD-ESP32-23 (CH343P bridge + native USB)](https://github.com/deepcoder/YD-ESP32-23) *(R5)* [SECONDARY]

### 9.3 Background standards

- Wikipedia: [RS-232](https://en.wikipedia.org/wiki/RS-232), [RS-485](https://en.wikipedia.org/wiki/RS-485), [CAN bus](https://en.wikipedia.org/wiki/CAN_bus), [ANSI/TIA-568 (T568A/B colours)](https://en.wikipedia.org/wiki/ANSI/TIA-568) *(R5)*

### 9.4 RJ45 pinouts on other products

These are for comparison only. None of them is used for this unit's wiring.

- SolarAssistant wiring pages:
  - RS232: [Growatt Axpert GT](https://solar-assistant.io/help/inverters/growtech/Axpert-GT/rs232) *(R2, R4, V1)*, [Eco-worthy](https://origin.solar-assistant.io/help/inverters/eco-worthy/ECO/rs232) *(R5)*, [PowMr HVM-KP](https://solar-assistant.io/help/inverters/powmr/POW-HVM-KP/rs232) *(R2)*
  - RS485: [SRNE SR-HF](https://solar-assistant.io/help/inverters/srne/SR-HF/rs485), [SRNE ABP](https://solar-assistant.io/help/inverters/srne/ABP/rs485) *(R5)*
  - [Deye 2-in-1 BMS port](https://solar-assistant.io/help/deye/2_in_1_bms_port) *(R5)*
  - Others *(R3)*: [inverter list](https://solar-assistant.io/help/inverters), [Easun ISolar SMG II 4kW 24V](https://solar-assistant.io/help/inverters/easun/ISOLAR-SMG-II-4kW-24V) ([RS232](https://solar-assistant.io/help/inverters/easun/ISOLAR-SMG-II-4kW-24V/rs232)), [Anenji ANJ-4000W-24V](https://solar-assistant.io/help/inverters/anenji/ANJ-4000W-24V), [Sumry HGX](https://solar-assistant.io/help/inverters/sumry/HGX)
- [Schneider Conext AP-XC-104 Modbus RS485 wiring (PDF)](https://solar.se.com/us/wp-content/uploads/sites/7/2021/12/TP20170301_Conext-Core-XC-Modbus_RS485-Wiring-AP-XC-104_rev-A.pdf) *(R5)*
- [Sunsynk battery compatibility v14 (BMS port pinout)](https://portal.segen.co.uk/reseller/docs/Sunsynk_BatteryCompatibility_v14_English.pdf) *(R5)*

### 9.5 Power and mains

- [TI LM2596 datasheet](https://www.ti.com/lit/ds/symlink/lm2596.pdf): buck converter, **not used** (not owned) *(R5)*
- [WorldStandards: Bangladesh plugs and voltage](https://www.worldstandards.eu/electricity/plug-voltage-by-country/bangladesh/): 230 V/50 Hz; universal sockets accept US plugs *(R5)*
- [Apple: Personal Hotspot "Maximize Compatibility" (2.4 GHz)](https://support.apple.com/guide/platform-support/sup025284ac0) *(R5)*

---

## 10. Firebase and Google

### 10.1 Pricing, limits and policy

- [Firebase pricing](https://firebase.google.com/pricing): Spark RTDB 1 GB / 10 GB per month / 100 connections; Hosting 360 MB/day; Functions and Storage not on Spark *(D1, R4, R6, V2)*
- [Firebase FAQ](https://firebase.google.com/support/faq): over quota, "shut off for the remainder of that month" *(D1, R6, V2)*
- [Cloud Storage changes FAQ (Spark loses buckets from 2026-02-03)](https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024) *(D1, R6, V2)*
- [JS SDK release notes](https://firebase.google.com/support/release-notes/js) *(R6, V2)*
- [Google inactive-account policy (2 years)](https://support.google.com/accounts/answer/12418290) *(D1, V2)*

### 10.2 Realtime Database

- [Usage limits](https://firebase.google.com/docs/database/usage/limits) *(D1, R6, V2)*
- [Billing: what counts as download, 3.5 KB per TLS handshake](https://firebase.google.com/docs/database/usage/billing) *(D1, R6, V2)*
- [Locations (asia-southeast1)](https://firebase.google.com/docs/database/locations) *(D1, R6, V2)*
- REST: [saving data (PUT/PATCH/POST, `print=silent`)](https://firebase.google.com/docs/database/rest/save-data) *(D1, R6)*, [authenticating REST requests](https://firebase.google.com/docs/database/rest/auth) *(D1, R4, R6, V2)*
- Rules: [core syntax](https://firebase.google.com/docs/database/security/core-syntax) *(D1, R6)*, [conditions](https://firebase.google.com/docs/database/security/rules-conditions) *(R6)*, [indexing](https://firebase.google.com/docs/database/security/indexing-data) *(R6)*, rules reference *(R6)*
- [Web: working with lists of data](https://firebase.google.com/docs/database/web/lists-of-data) *(R6)*

### 10.3 Authentication

- Auth REST API (signInWithPassword, token refresh) *(D1, R6, V2)*
- [Manage sessions (token lifetimes, refresh-token revocation)](https://firebase.google.com/docs/auth/admin/manage-sessions) *(D1, V2)*
- [Auth limits](https://firebase.google.com/docs/auth/limits) *(D1, V2)*
- [Redirect best practices](https://firebase.google.com/docs/auth/web/redirect-best-practices) *(R6)*

### 10.4 Hosting, web setup and messaging

- [Hosting usage quotas and pricing](https://firebase.google.com/docs/hosting/usage-quotas-pricing) *(D1, R6, V2, R7a, R7b, R7c)*. Firebase Hosting is now only the cold fallback host; see [§18](#18-web-hosting-and-cicd-r7)
- [Web alternative setup (self-hosted SDK)](https://firebase.google.com/docs/web/alt-setup) *(R6)*
- Cloud Messaging: [JS client](https://firebase.google.com/docs/cloud-messaging/js/client), [HTTP v1 send](https://firebase.google.com/docs/cloud-messaging/send/v1-api). Considered for Web Push and rejected. *(R6)*
- [npm registry: firebase](https://registry.npmjs.org/firebase): `latest` = 13.0.0 *(D1, R6, V2)*
- gstatic SDK 13.0.0 files: [app](https://www.gstatic.com/firebasejs/13.0.0/firebase-app.js), [auth](https://www.gstatic.com/firebasejs/13.0.0/firebase-auth.js), [database](https://www.gstatic.com/firebasejs/13.0.0/firebase-database.js). Measured for size; the PWA bundles the SDK instead. *(R6)*
- [firebase-js-sdk issue #77](https://github.com/firebase/firebase-js-sdk/issues/77) *(R6)*
- [Google Trust Services roots (roots.pem)](https://pki.goog/roots.pem) *(R6)*

### 10.5 Firestore (compared, not used)

- [Quotas](https://firebase.google.com/docs/firestore/quotas) *(R6, V2)*, [pricing](https://firebase.google.com/docs/firestore/pricing), [best practices](https://firebase.google.com/docs/firestore/best-practices), [index overview](https://firebase.google.com/docs/firestore/query-data/index-overview), [storage size](https://firebase.google.com/docs/firestore/storage-size), REST Write, REST documents, [using the REST API](https://firebase.google.com/docs/firestore/use-rest-api) *(R6)*

### 10.6 API endpoints used by the device

These are not documents.

- `https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=…`
- `https://securetoken.googleapis.com/v1/token?key=…`
- `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app/devices/inv1/….json`

---

## 11. iOS, WebKit, PWA and charting

- WebKit blog:
  - [Web Push for Home Screen web apps (iOS 16.4)](https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/) *(D1, R4, R6, V2)*
  - [Storage policy updates](https://webkit.org/blog/14403/updates-to-storage-policy/) *(D1, R4, R6, V2)*
  - [Full third-party cookie blocking / 7-day cap](https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/) *(D1, R4, R6, V2)*
  - [WWDC25 / Safari 26 (Home Screen sites open as web apps)](https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/) *(D1, R6, V2)*
  - [WebKit features in Safari 26.0](https://webkit.org/blog/17333/webkit-features-in-safari-26-0/) *(R6)*
- [WWDC23 notes 10120 (Home Screen web apps)](https://wwdcnotes.com/notes/wwdc23/10120) *(R6)*
- [firt.dev: PWA on iOS notes](https://firt.dev/notes/pwa-ios/) *(R4)* [SECONDARY]
- [MDN: making PWAs installable](https://developer.mozilla.org/en-US/docs/Web/Progressive_web_apps/Guides/Making_PWAs_installable) *(R6)*
- [Apple: Media Services availability by country (Bangladesh not listed)](https://support.apple.com/en-us/118205) *(D1, V2)*
- iPhone automation (considered): [Apple Shortcuts "Get Contents of URL"](https://support.apple.com/guide/shortcuts/request-your-first-api-apd58d46713f/ios), [Scriptable Request](https://docs.scriptable.app/request/) *(R4)*
- Charting:
  - [leeoniya/uPlot](https://github.com/leeoniya/uPlot) (v1.6.32, MIT, **used**) *(D1, R6)*. jsDelivr files [uPlot.iife.min.js](https://cdn.jsdelivr.net/npm/uplot@1.6.32/dist/uPlot.iife.min.js) and [uPlot.min.css](https://cdn.jsdelivr.net/npm/uplot@1.6.32/dist/uPlot.min.css) were measured for size; the PWA bundles uPlot instead *(R6)*
  - [Chart.js](https://github.com/chartjs/Chart.js) and [Apache ECharts](https://github.com/apache/echarts) (compared) *(R6)*

---

## 12. Alerts and notifications

- ntfy:
  - Docs: [publish (headers, limits)](https://docs.ntfy.sh/publish/) *(D1, R6, V2)*, [config (iOS delivery via Firebase/APNs, `upstream-base-url`)](https://docs.ntfy.sh/config/) *(D1, R6, V2)*, [phone subscription](https://docs.ntfy.sh/subscribe/phone/) *(R4, R6)*
  - Service: [ntfy.sh](https://ntfy.sh/) *(R6, V2)* and its [pricing](https://ntfy.sh/#pricing) *(R6)*
  - [ntfy iOS app](https://apps.apple.com/us/app/ntfy/id1625396347) *(R6)*
  - The source repository `binwiederhier/ntfy` was read through the GitHub API *(V2)*
- Telegram: [Bot API](https://core.telegram.org/bots/api) *(D1, R4, R6, V2)*, [Bot FAQ (rate limits)](https://core.telegram.org/bots/faq) *(D1, R6, V2)*
- [Prothom Alo: Telegram restrictions in Bangladesh](https://en.prothomalo.com/bangladesh/government/igr6isfgo4) *(D1, V2)* [SECONDARY]
- Healthchecks.io: [pricing (Hobbyist free, 20 checks)](https://healthchecks.io/pricing/) *(D1, V2)*, [HTTP API (pinging)](https://healthchecks.io/docs/http_api/) *(D1, V2)*, [configuring checks (period, grace)](https://healthchecks.io/docs/configuring_checks/) *(V2)*, [Telegram integration](https://healthchecks.io/integrations/telegram) *(V2)*
- Pushover (considered, paid): [API](https://pushover.net/api), [pricing](https://pushover.net/pricing) *(R6)*
- [Let's Encrypt certificate hierarchy (X1/X2, YR/YE)](https://letsencrypt.org/certificates/) *(D1, R6, V2)*

---

## 13. Alternative back-ends and hosting

- Supabase: [pricing](https://supabase.com/pricing) *(R4, R6, V2)*, [free project pausing (7 days)](https://supabase.com/docs/guides/platform/free-project-pausing) *(D1, R6, V2)*
- [Blynk pricing (1-week retention)](https://blynk.io/pricing) *(R4, R6, V2)*
- ThingSpeak: [licence FAQ (3M messages/year, non-commercial)](https://thingspeak.mathworks.com/pages/license_faq) *(D1, R6, V2)*, [prices](https://thingspeak.mathworks.com/prices/thingspeak_home) *(R4)*, [channel settings](https://www.mathworks.com/help/thingspeak/channel-settings.html) *(R6)*
- Cloudflare: [D1 limits](https://developers.cloudflare.com/d1/platform/limits/) *(D1, R6, V2)*, [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/) *(R6, V2)*, [Workers limits](https://developers.cloudflare.com/workers/platform/limits/) *(D1, R6, V2)*, [Pages limits](https://developers.cloudflare.com/pages/platform/limits/), [Workers static assets](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/) *(R6)*
- [InfluxDB Cloud pricing](https://www.influxdata.com/influxdb-cloud-pricing/) and [Grafana Cloud pricing](https://grafana.com/pricing/) *(R6)*: not re-verified by V2
- [Adafruit IO+ guide (free-tier limits)](https://learn.adafruit.com/welcome-to-adafruit-io/io-plus) *(R6)*: not re-verified by V2
- [GitHub Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits) *(R6)*
- Home Assistant: [Modbus integration](https://www.home-assistant.io/integrations/modbus/) *(R6)*, [companion app Apple-platform support change (2026-07-07)](https://www.home-assistant.io/blog/2026/07/07/companion-app-changing-support-for-apple-platforms/) *(R4)*, [Nabu Casa pricing](https://www.nabucasa.com/pricing/) *(R4, R6)*

---

## 14. Existing apps, vendor dongles and monitoring services

### 14.1 iOS App Store listings (US storefront)

- [SOLAR OF THINGS](https://apps.apple.com/us/app/solar-of-things/id6445806075): the app named in the Savx manual *(D1, V1)*
- Eybond / Voltronic apps (same seller account):
  - [SmartESS](https://apps.apple.com/us/app/smartess/id1334656760) *(R1, R4)*
  - [WatchPower](https://apps.apple.com/us/app/watchpower/id1487569321), [Energy-Mate](https://apps.apple.com/us/app/energy-mate/id1672569936), [SolarPower Wi-Fi](https://apps.apple.com/us/app/solarpower-wi-fi/id1491698936), [SmartClient for Solar](https://apps.apple.com/us/app/smartclient-for-solar/id1070155085) *(R4)*
- Others *(R4)*: [SOLARMAN Smart](https://apps.apple.com/us/app/solarman-smart/id1469487897), [SRNE](https://apps.apple.com/us/app/srne/id1635684571), [Smart Life (Tuya)](https://apps.apple.com/us/app/smart-life-smart-living/id1115101477)
- [Home Assistant](https://apps.apple.com/us/app/home-assistant/id1099568401) *(R4, R6)*

### 14.2 Vendor hardware and services

- Eybond: [SmartESS page](https://www.eybond.com/smartess/smartess.html), [DTU PlugPro series](https://www.eybond.com/data-communication-type/dtu-plugpro-series.html), [WFBLE.DTU.PlugProA-03 spec PDF](https://www.eybond.com/uploads/upload/20260422/20260422/b71d32b09cd31448a2fc274f33fa04cc.pdf) *(R4)*
- Eybond Wi-Fi Plug Pro quick guide (inverter.com) *(R1, V1)*
- Voltronic: [news 174 (Energy-Mate replaces WatchPower/SolarPower)](https://voltronicpower.com/en-US/Company/News/174), [WiFi module](https://voltronicpower.com/en-US/Product/Detail/Wifi-Module) *(R4)*
- PowMr WiFi modules: [ECO MAX](https://powmr.com/products/powmr-wifi-module-eco-max), [WiFi-Relab](https://powmr.com/products/powmr-wifi-module-wifi-relab) *(R4)*
- Dongle listings [SECONDARY] *(R4)*: [Volnex WiFi dongle (Pakistan)](https://w11stop.com/solar/solar-system-accessories/volnex-wifi-dongle-price-in-pakistan), [Growatt ShineWiFi-F (BDStall)](https://www.bdstall.com/details/growatt-shinewifi-f-inverter-monitoring-wi-fi-usb-dongle-163826/)
- Unofficial cloud APIs: `https://api.dessmonitor.com/public/` and `https://ios.shinemonitor.com/public/` *(R4)*, and a DessMonitor `querySPDeviceLastData` query on `web.dessmonitor.com` *(R1)*
- SolarAssistant (needs a Pi; rejected) *(R4)*: [home](https://solar-assistant.io/), [ICC comparison](https://solar-assistant.io/explore/icc), [remote access](https://solar-assistant.io/help/access/overview), [terms update Oct 2024](https://solar-assistant.io/help/updates/terms_update_oct_2024), [shop / prices](https://solar-assistant.io/shop)

---

## 15. Forums and community threads

All of these are [SECONDARY].

- powerforum.co.za:
  - [Data on the ports of MPP/Voltronic clones](https://powerforum.co.za/topic/14589-what-kind-of-data-is-available-on-the-different-ports-of-mppvoltronicclones/) *(R2, R4)*
  - [Flaky RS232/USB comms on Voltronic](https://powerforum.co.za/topic/32076-any-fix-for-flaky-rs232usb-comms-on-voltronic-inverters/) *(R2)*
  - [Axpert RS232 card burning with a mains-earthed PC (ground loop)](https://powerforum.co.za/topic/8635-axpert-inverter-burning-rs232-card-when-ever-i-plug-serial-cable-to-computer/) *(R2, R5)*
  - [RJ45 to DB9 RS232 (solved)](https://powerforum.co.za/topic/17963-inverter-connection-wifi-rj45-to-db9-r232-solved/) *(R5)*
  - [WiFi data logger for SmartESS](https://powerforum.co.za/topic/16253-wifi-data-logger-for-smartess-app/) *(R4)*
  - [Software for Axpert MKS V4 via WiFi](https://powerforum.co.za/topic/17000-recommended-software-to-get-data-from-axpert-mks-v4-via-wifi/) *(R4)*
  - ["WiFi already exists"](https://powerforum.co.za/topic/19284-wifi-already-exists/) *(R4)*
  - [Sunsynk 8 kW data collection setup](https://powerforum.co.za/topic/8646-my-sunsynk-8kw-data-collection-setup/page/4/) *(R4)*
- diysolarforum.com:
  - [REVO E 5.5 kW cannot change parameters over RS232](https://diysolarforum.com/threads/revo-e-5-5kw-cannot-change-parameters-using-rs232.34404/) *(R1, V1)*
  - [Sorotec REVO II 5.5 kW = Easun/Axpert/Powland/iGrid VE II](https://diysolarforum.com/threads/sorotec-revo-ii-5-5kw-easun-axpert-powland-igrid-ve-ii-5-5kw.23525/) *(R1)*
  - [Sorotec REVO II factory password](https://diysolarforum.com/threads/sorotec-revo-ii-factory-password.35132/) *(R1)*
- GitHub issue and discussion threads are listed with their repositories in §5.

---

## 16. Shops and prices (Bangladesh)

These are all [SECONDARY] listings. **Nothing needs to be bought.** They only inform the optional list in [03](03-hardware-and-wiring.md).

- **a local shop** (*R5*; marked items also *D1/V2*):
  - Interface modules: [mini RS232↔TTL (MAX3232), ৳80]((local shop listing)) *(D1, V2)*, [RS232↔TTL module]((local shop listing))
  - Boards: [ESP32-S3 DevKitC-1 N16R8]((local shop listing))
  - Wiring: [F-F jumpers, ৳75]((local shop listing)) *(D1, V2)*, [silicone F-F jumpers]((local shop listing)), [M-F jumpers]((local shop listing)), [heat-shrink]((local shop listing)), [3-pin screw terminal]((local shop listing)), [40-pin female header]((local shop listing)), [Vero board]((local shop listing))
  - Connectors and glands: [DB9 male screw terminal]((local shop listing)), [PG7 gland (white)]((local shop listing)), [PG7 gland (black)]((local shop listing))
  - Tools: [DT830D multimeter, ৳320]((local shop listing)) *(D1)*, [ANENG M113 multimeter]((local shop listing)), [RJ45 crimping tool]((local shop listing))
  - USB adapters: [HL340 USB↔RS232 DB9]((local shop listing)), [USB↔RS485]((local shop listing)), [industrial USB↔RS485 (CH340)]((local shop listing))
  - Power: [LM2596 buck]((local shop listing)), [Mini-360 buck]((local shop listing)), [5×20 mm fuse holder]((local shop listing)), [universal travel adapter]((local shop listing))
- **RoboticsBD** (*R5*):
  - [UART/RS232/RS485 modules category](https://store.roboticsbd.com/204-uart-rs232-rs485-ttl-modules)
  - Modules: [TTL↔RS485 auto-direction (RBD-1816)](https://store.roboticsbd.com/electronics-module/1816-ttl-to-rs485-converter-33v5v-hardware-automatic-control-converter-module-robotics-bangladesh.html), [RS232↔TTL](https://store.roboticsbd.com/electronics-module/1916-rs232-to-ttl-serial-interface-module-robotics-bangladesh.html)
  - Connectors and boards: [RJ45 connector](https://store.roboticsbd.com/connector/1586-rj45-connector-robotics-bangladesh.html), [ESP32-S3 GPIO terminal expansion board](https://store.roboticsbd.com/diy-pcb/4136-esp32-s3-gpio-terminal-expansion-board-robotics-bangladesh.html)
  - Power: [LM2596HV](https://store.roboticsbd.com/power-module-adapter/3326-lm2596hv-power-supply-module-dcdc-and-acdc-robotics-bangladesh.html), [Hi-Link 24 V→5 V isolated](https://store.roboticsbd.com/power-module-adapter/4585-hi-link-hlk-b2405s-2wr3-24v-to-5v-2w-isolated-dc-dc-converter-module-robotics-bangladesh.html)
  - USB: [ADUM3160 USB isolator](https://store.roboticsbd.com/logic-level-voltage-current-signal-converter/3972-adum3160-usb-isolator-module-robotics-bangaldesh.html), [industrial USB↔RS485/RS232](https://store.roboticsbd.com/programmer/3357-industrial-grade-usb-to-rs485rs232-converter-robotics-bangladesh.html), [USB↔RS485](https://store.roboticsbd.com/uart-rs232-rs485-ttl-modules/566-rs485-usb-485-converter-adapter-support-win7-xp-vista-linux-mac-os-robotics-bangladesh.html)
- **Daraz** (*R4*, *V2*): [MAX3232 search](https://www.daraz.com.bd/catalog/?q=max3232), [MAX3232 DB9 module](https://www.daraz.com.bd/products/max3232-rs232-to-ttl-serial-port-converter-module-db9-connector-i1572590799.html), [PowMr 232 WiFi-VM dongle](https://www.daraz.com.bd/products/powmr-232-wifi-vm-i315732315.html)

---

## 17. Measurements and API queries made during research

These are first-hand results recorded in the reports, not citable pages.

| What | Where recorded |
|---|---|
| TLS chains via `openssl s_client -showcerts` for RTDB (asia-southeast1), identitytoolkit, securetoken, ntfy.sh, api.telegram.org and hc-ping.com | V2 I6, §3.3 |
| RTDB REST response header size (299 B), measured with `curl -D` | R6 §1.2 |
| Firebase JS SDK 13.0.0 gzip sizes (app 23.7 KB, auth 41.6 KB, database 49.0 KB, firestore 186.5 KB) | R6 §5.2 |
| Compact JSON sizes of the `live`/`hist`/`daily`/`events` examples (`json.dumps(separators=(',',':'))`) | D1 §4.1 |
| Every PI30 / REVO / Modbus request frame and CRC, recomputed with an independent script | V1 §4.4 |
| iTunes Lookup API with `country=us`, `in` and `bd` (app availability; Bangladesh returns 0 results) | R4, V2 I8 |
| IPS Bazar WooCommerce store API and WordPress media search for "savx" (`/wp-json/wc/store/products`, `/wp-json/wp/v2/media`) | R1 |
| GitHub API release metadata (arduino-esp32, pioarduino, FirebaseClient, ElegantOTA, WiFiManager, uPlot, ArduinoJson, ESPAsyncWebServer, ntfy) | R5, R6, V2 |
| Time to first byte from Dhaka, 5 samples per host (Cloudflare `colo=DAC` 49–58 ms, Firebase Hosting 52–55 ms, Vercel via `bom1` 177–196 ms, Netlify 155–222 ms, GitHub Pages 53–61 ms) | R7b, R7 §1.1 |
| `wrangler dev` 4.146.0 (workerd) runs of the built PWA with `_headers` and the SPA fallback: cache headers, `/index.html` 307, the immutable SPA fallback | R7 §3.9 |
| `git ls-remote` and `action.yml` of every pinned GitHub Action (all `node24`); npm registry engines for wrangler, firebase-tools and Vite | R7 §4.1, §10 |
| `actionlint` 1.7.12 + `shellcheck` on the three workflows; a git change-detection simulation | R7 §4.1, §10 |
| RTDB emulator (firebase-tools 15.32.1, database emulator v4.11.2): 69 allow/deny cases for the R8 rules; the finding that `\s`/`\S` are rejected in rules regexes | R8 §0, §8.1, §11 |
| Mock harness (Code.gs in a Node VM, 18 cases) and an end-to-end run against the emulator | R8 §8.1, §11 |
| Live TLS chain of `script.google.com` (WE2 → GTS Root R4) | R8c, R8 §1.2 |

---

## 18. Web hosting and CI/CD (R7)

These support the hosting decision in [R7](../research/r7-web-hosting.md) and its implementation ([06](06-cloud-and-dashboard-guide.md) §4–§5, [04 §12](04-architecture-and-data-model.md#12-web-hosting-and-cicd)). Fetched on 2026-10-21.

### 18.1 Cloudflare Workers and Pages (chosen host)

- Static assets: [overview ("deployed in a single operation")](https://developers.cloudflare.com/workers/static-assets/) *(R7, R7a)*, [billing and limitations (static requests free and unlimited)](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/) *(R7a, R7b, R7c)*, [headers (`_headers`)](https://developers.cloudflare.com/workers/static-assets/headers/) *(R7, R7a, R7b, R7c)*, [assets binding and `.assetsignore`](https://developers.cloudflare.com/workers/static-assets/binding/) *(R7, R7b)*, [single-page application routing](https://developers.cloudflare.com/workers/static-assets/routing/single-page-application/) *(R7b, R7c)*, [HTML handling](https://developers.cloudflare.com/workers/static-assets/routing/advanced/html-handling/) *(R7b, R7c)*, [migrating from Pages](https://developers.cloudflare.com/workers/static-assets/migration-guides/migrate-from-pages/) *(R7a, R7b, R7c)*
- Workers platform: [limits](https://developers.cloudflare.com/workers/platform/limits/) *(R7a, R7b, R7c)*, [pricing](https://developers.cloudflare.com/workers/platform/pricing/) *(R7c)*, [workers.dev routing and the account subdomain](https://developers.cloudflare.com/workers/configuration/routing/workers-dev/) *(R7)*, [Version URLs / `preview_urls`](https://developers.cloudflare.com/workers/configuration/previews/) *(R7, R7a, R7b, R7c)*, [previews](https://developers.cloudflare.com/workers/previews/) and [automation examples](https://developers.cloudflare.com/workers/previews/automation-examples/) *(R7b, R7c)*, [rollbacks (100 versions)](https://developers.cloudflare.com/workers/configuration/versions-and-deployments/rollbacks/) *(R7)*
- Wrangler: [configuration](https://developers.cloudflare.com/workers/wrangler/configuration/) *(R7b)*, [commands: deploy, versions, rollback](https://developers.cloudflare.com/workers/wrangler/commands/workers/) *(R7)*, [config schema 4.146.0](https://unpkg.com/wrangler@4.146.0/config-schema.json) *(R7, R7b)*, [`bin/wrangler.js` (minimum Node version)](https://unpkg.com/wrangler@4.146.0/bin/wrangler.js) *(R7)*, [npm registry: wrangler](https://registry.npmjs.org/wrangler) *(R7)*
- CI/CD: [Workers with GitHub Actions (token template, secret names)](https://developers.cloudflare.com/workers/ci-cd/external-cicd/github-actions/) *(R7)*, [find account and zone IDs](https://developers.cloudflare.com/fundamentals/account/find-account-and-zone-ids/) *(R7)*, Workers Builds ([configuration](https://developers.cloudflare.com/workers/ci-cd/builds/configuration/) *(R7b, R7c)*, [GitHub integration](https://developers.cloudflare.com/workers/ci-cd/builds/git-integration/github-integration/) *(R7a, R7c)*, [limits and pricing](https://developers.cloudflare.com/workers/ci-cd/builds/limits-and-pricing/) *(R7a, R7b, R7c)*; considered, not used), [cloudflare/wrangler-action](https://github.com/cloudflare/wrangler-action) and its [releases](https://github.com/cloudflare/wrangler-action/releases) *(R7c; not used)*
- Pages (equivalent, not chosen): [docs](https://developers.cloudflare.com/pages/) *(R7b, R7c)*, [limits](https://developers.cloudflare.com/pages/platform/limits/) *(R7a, R7b, R7c)*, [GitHub integration](https://developers.cloudflare.com/pages/configuration/git-integration/github-integration/) *(R7a)*, [preview deployments](https://developers.cloudflare.com/pages/configuration/preview-deployments/) *(R7b)*, [serving pages](https://developers.cloudflare.com/pages/configuration/serving-pages/) *(R7c)*, [Functions pricing](https://developers.cloudflare.com/pages/functions/pricing/) *(R7a)*
- Access (protecting previews needs a card): [Cloudflare One setup](https://developers.cloudflare.com/cloudflare-one/setup/) *(R7c)*; [Zero Trust pricing overview](https://controld.com/blog/cloudflare-zero-trust-pricing/) *(R7b)* [SECONDARY]
- Network and incidents: [Cloudflare network map (Dhaka, Chittagong PoPs)](https://www.cloudflare.com/network/) *(R7a, R7b, R7c)*, [5 December 2025 outage report](https://blog.cloudflare.com/5-december-2025-outage/) *(R7a, R7c)*
- [Public Suffix List (`workers.dev` is listed)](https://publicsuffix.org/list/public_suffix_list.dat) *(R7c)*
- Pages vs Workers comparisons: [mecanik.dev](https://mecanik.dev/en/posts/cloudflare-pages-vs-workers-which-to-use-in-2026/), [morphllm.com](https://www.morphllm.com/comparisons/cloudflare-pages-vs-workers) *(R7a)* [SECONDARY]

### 18.2 Firebase Hosting (fallback) and Firebase-side settings

- [Hosting GitHub integration](https://firebase.google.com/docs/hosting/github-integration) *(R7a, R7c)*, [manage cache behaviour](https://firebase.google.com/docs/hosting/manage-cache) *(R7a, R7c)*, [test and preview deploys](https://firebase.google.com/docs/hosting/test-preview-deploy) *(R7b)*, [Firebase CLI](https://firebase.google.com/docs/cli) *(R7b, R7c)*, [firebase-tools issue #3455](https://github.com/firebase/firebase-tools/issues/3455) *(R7b)*
- [Firebase API keys ("do not need to be treated as secrets")](https://firebase.google.com/docs/projects/api-keys) *(R7b, R7c)*, [Google Cloud: add API key restrictions](https://docs.cloud.google.com/api-keys/docs/add-restrictions-api-keys) *(R7b, R7c)*
- [Auth FAQ and troubleshooting (authorized domains)](https://firebase.google.com/docs/auth/faq-and-troubleshooting) *(R7b, R7c)*, [passing state in e-mail actions](https://firebase.google.com/docs/auth/web/passing-state-in-email-actions) *(R7b, R7c)*
- App Check (left off: it would reject the device's REST sign-in): [enable enforcement](https://firebase.google.com/docs/app-check/enable-enforcement), [reCAPTCHA Enterprise provider](https://firebase.google.com/docs/app-check/web/recaptcha-enterprise-provider), [custom provider](https://firebase.google.com/docs/app-check/custom-provider) *(R7b)*
- [Predefined product IAM roles (Firebase Realtime Database Admin)](https://firebase.google.com/docs/projects/iam/roles-predefined-product) *(R7b)*
- [Firebase status incident](https://status.firebase.google.com/incidents/aibvHDiSS2AXiszCBL8Q) *(R7a)*, [Fastly network map (Firebase Hosting's CDN)](https://www.fastly.com/network-map/) *(R7a, R7c)*, [netify.ai: Firebase platform profile](https://www.netify.ai/resources/platforms/firebase) *(R7a)* [SECONDARY]

### 18.3 GitHub and Google Cloud (CI/CD)

- GitHub: [Actions billing (2,000 min/month on Free, blocked without a card)](https://docs.github.com/en/billing/concepts/product-billing/github-actions) *(R7, R7b, R7c)*, runner pricing (per-job rounding) *(R7)*, dependency caching (7-day eviction, 10 GB) *(R7)*, [GitHub's plans (protected branches in private repos need Pro)](https://docs.github.com/en/get-started/learning-about-github/githubs-plans) *(R7, R7a, R7c)*, [deployment environments](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments) *(R7b, R7c)*, [secret-scanning patterns](https://docs.github.com/en/code-security/secret-scanning/introduction/supported-secret-scanning-patterns) *(R7b)*, [Node 20 removed from Actions (2026-09-23)](https://github.blog/changelog/2026-09-23-node-20-is-no-longer-available-in-github-actions/) *(R7c)*
- [google-github-actions/auth (Workload Identity Federation)](https://github.com/google-github-actions/auth) *(R7b)*, [Google Cloud Shell pricing (free)](https://cloud.google.com/shell/pricing) *(R7)*
- [PlatformIO with GitHub Actions](https://docs.platformio.org/en/latest/integration/ci/github-actions.html) *(R7b)*
- [Vite: env variables and modes](https://vite.dev/guide/env-and-mode) *(R7b, R7c)*, [web.dev: the service worker lifecycle](https://web.dev/articles/service-worker-lifecycle) *(R7b)*
- [Google Search: robots meta tag / `X-Robots-Tag`](https://developers.google.com/search/docs/crawling-indexing/robots-meta-tag) *(R7)*

### 18.4 Hosts compared and not chosen

- **Vercel Hobby** (runner-up): [Hobby plan ("non-commercial personal use only")](https://vercel.com/docs/plans/hobby) *(R7a, R7b, R7c)*, [limits](https://vercel.com/docs/limits) and [fair-use guidelines](https://vercel.com/docs/limits/fair-use-guidelines) *(R7a, R7c)*, [pricing](https://vercel.com/pricing) *(R7a)*, [regions](https://vercel.com/docs/regions) *(R7a, R7c)*, [`vercel.json`](https://vercel.com/docs/project-configuration/vercel-json) *(R7a, R7b, R7c)* and its [schema](https://openapi.vercel.sh/vercel.json) *(R7b)*, [cache-control headers](https://vercel.com/docs/caching/cache-control-headers), [Git configuration](https://vercel.com/docs/project-configuration/git-configuration), [Toolbar](https://vercel.com/docs/vercel-toolbar/managing-toolbar), [GitHub Actions guide](https://vercel.com/kb/guide/how-can-i-use-github-actions-with-vercel) *(R7b)*, [deployment protection](https://vercel.com/docs/deployment-protection) and [Vercel for GitHub](https://vercel.com/docs/git/vercel-for-github) *(R7b, R7c)*
- **Netlify Free**: [pricing](https://www.netlify.com/pricing/) *(R7a, R7b)*, [credit-based plans](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/) *(R7a, R7b, R7c)*, [how credits work](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/how-credits-work/) *(R7b, R7c)*, [billing FAQ](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/billing-faq-for-credit-based-plans/) *(R7a)*, [headers](https://docs.netlify.com/manage/routing/headers/) and [rewrites](https://docs.netlify.com/manage/routing/redirects/rewrites-proxies/) *(R7a, R7b)*, [password protection](https://docs.netlify.com/manage/security/secure-access-to-sites/password-protection/) *(R7b)*, [CDN PoPs thread](https://answers.netlify.com/t/is-there-a-list-of-where-netlifys-cdn-pops-are-located/855) *(R7a)* [SECONDARY]
- **GitHub Pages**: [what it is](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages) *(R7b)*, [creating a site](https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site) *(R7b, R7c)*, [limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits) *(R6, R7a, R7b, R7c)*; no custom headers: [GitHub community discussion](https://github.com/orgs/community/discussions/4444), [Hugo forum](https://discourse.gohugo.io/t/is-it-possible-to-add-custom-http-header-in-hugo-using-github-pages/32182) *(R7a)* [SECONDARY]
- **Others**: Azure Static Web Apps ([plans](https://learn.microsoft.com/en-us/azure/static-web-apps/plans) *(R7a, R7c)*, [quotas](https://learn.microsoft.com/en-us/azure/static-web-apps/quotas) *(R7a)*, [account (card)](https://azure.microsoft.com/en-us/pricing/purchase-options/azure-account) *(R7a, R7c)*); Render ([free](https://render.com/docs/free), [static sites](https://render.com/docs/static-sites), [new workspace plans](https://render.com/docs/new-workspace-plans) *(R7a)*, [plan changelog](https://render.com/changelog/updated-plans-for-render-workspaces), [outbound bandwidth](https://render.com/docs/outbound-bandwidth) *(R7a, R7c)*); Deno Deploy ([pricing](https://deno.com/deploy/pricing), [classic](https://docs.deno.com/deploy/classic/) *(R7a, R7c)*); [Surge pricing](https://surge.sh/pricing), [Codeberg Pages](https://docs.codeberg.org/codeberg-pages/), [GitLab Pages](https://docs.gitlab.com/user/project/pages/) and [its custom-headers issue](https://gitlab.com/gitlab-org/gitlab-pages/-/issues/50) *(R7a)*
- [heise: iOS 26 Home Screen web apps](https://heise.de/-10749652) *(R7b)* [SECONDARY]

---

## 19. E-mail alerts and Apps Script (R8)

These support the e-mail decision in [R8](../research/r8-email-alerts.md) and its implementation ([`tools/apps-script/`](../tools/apps-script/), [06](06-cloud-and-dashboard-guide.md) §9, [04 §9.5–9.6](04-architecture-and-data-model.md#95-e-mail-alerts-the-apps-script-mailer)). Fetched or run on 2026-10-21.

### 19.1 Google Apps Script (chosen transport)

- [Quotas (100 recipients/day, 90 min/day triggers, 20,000 URL fetches/day; may change "without notice")](https://developers.google.com/apps-script/guides/services/quotas) *(R8, R8a, R8b)*
- Services: MailApp *(R8, R8a, R8b)*, UrlFetchApp *(R8)*, ScriptApp *(R8, R8a)*, ClockTriggerBuilder (`everyMinutes` 1/5/10/15/30) *(R8, R8a)*, [installable triggers](https://developers.google.com/apps-script/guides/triggers/installable) *(R8a)*, Lock *(R8)*, PropertiesService *(R8)*, Cache *(R8a, R8b)*, Utilities *(R8a, R8b)*
- Project set-up: [manifest](https://developers.google.com/apps-script/manifest) *(R8)*, [web-app manifest settings](https://developers.google.com/apps-script/manifest/web-app-api-executable) *(R8a)*, [scopes](https://developers.google.com/apps-script/concepts/scopes) *(R8, R8a)*, [web apps (`doGet`, `/exec`)](https://developers.google.com/apps-script/guides/web) *(R8, R8a, R8b)*, [Content service](https://developers.google.com/apps-script/guides/content) *(R8, R8a, R8b)*, deployments REST API *(R8a)*, [Google OAuth 2.0](https://developers.google.com/identity/protocols/oauth2) *(R8a)*
- Gmail and accounts: [Gmail sending limits](https://support.google.com/mail/answer/22839) *(R8, R8a, R8b)*, [App passwords (for the v2 SMTP fallback)](https://support.google.com/accounts/answer/185833) *(R8, R8a, R8b)*, [Gmail with other mail apps (no more password sign-in; App Passwords remain)](https://support.google.com/mail/answer/7126229) *(R8a)*, [Google's e-mail sender guidelines (SPF/DKIM, no Gmail From: impersonation)](https://support.google.com/a/answer/81126) and [sending from a device or app](https://knowledge.workspace.google.com/admin/gmail/send-email-from-a-printer-scanner-or-app) *(R8a)*, [inactive-account policy](https://support.google.com/accounts/answer/12418290) *(D1, V2; R8c M4)*
- Reading RTDB from Apps Script: [RTDB REST auth](https://firebase.google.com/docs/database/rest/auth) *(D1, R8, R8a)*, REST reference *(R8, R8b)*, [FirebaseApp library for Apps Script](https://github.com/RomainVialard/FirebaseApp) *(R8a; not used)*

### 19.2 RTDB rules and the web side

- Rules reference *(R6, R8, R8b)*, rules regular expressions *(R8b)*, [rules conditions](https://firebase.google.com/docs/database/security/rules-conditions) *(R6, R8b)*, [saving data](https://firebase.google.com/docs/database/rest/save-data) *(D1, R6, R8b)*, [offline capabilities / `serverTimeOffset`](https://firebase.google.com/docs/database/web/offline-capabilities) *(R8)*
- [Firebase "Trigger Email" extension (needs Blaze)](https://extensions.dev/extensions/firebase/firestore-send-email) *(R8b; rejected)*

### 19.3 ESP32 HTTP client findings (nudge and uplink)

- [arduino-esp32 `HTTPClient` sources at 3.3.12](https://github.com/espressif/arduino-esp32/tree/3.3.12/libraries/HTTPClient/src) *(R8a)*, [issue #8300 (redirect with a stale `Content-Length`)](https://github.com/espressif/arduino-esp32/issues/8300) and [PR #12990 (`getString()` hang fix, after 3.3.12)](https://github.com/espressif/arduino-esp32/pull/12990) *(R8; via R8c C1, M1)*. These are why the firmware uses its own HTTP framing (`HttpParse`) and never follows the nudge's redirect
- [ESP-IDF RAM usage guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html) *(R8a)*, ESP-IDF mbedTLS *(R8a)*, [esp32-arduino-lib-builder `defconfig.common`](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common) *(R8a)*
- SMTP libraries (for the v2 fallback, not used): [ESP-Mail-Client (deprecated)](https://github.com/mobizt/ESP-Mail-Client), [ReadyMail](https://github.com/mobizt/ReadyMail) *(R8a, R8b)*

### 19.4 Transports compared and rejected

- **Brevo** (300/day, the gmail.com sender is rewritten): [pricing](https://www.brevo.com/pricing/) *(R8a, R8b)* and [its Markdown version](https://www.brevo.com/pricing.md) *(R8a)*, [send a transactional e-mail](https://developers.brevo.com/docs/send-a-transactional-email), help articles [14925263522578](https://help.brevo.com/hc/en-us/articles/14925263522578) and [16045394674066](https://help.brevo.com/hc/en-us/articles/16045394674066) *(R8a)*
- **Resend** (`resend.dev` is for testing only): [pricing](https://resend.com/pricing) *(R8a, R8b)*, [quotas and limits](https://resend.com/docs/knowledge-base/account-quotas-and-limits), [403 on the resend.dev domain](https://resend.com/docs/knowledge-base/403-error-resend-dev-domain.md) *(R8a)*
- **EmailJS** (200/month): [pricing](https://www.emailjs.com/pricing/), [REST send](https://www.emailjs.com/docs/rest-api/send/), [connecting services](https://www.emailjs.com/docs/user-guide/connecting-email-services/), [emailjs-nodejs](https://github.com/emailjs-com/emailjs-nodejs) *(R8a)*
- **Postmark** (needs a domain): [pricing](https://postmarkapp.com/pricing), [why not a Gmail address](https://postmarkapp.com/blog/why-cant-i-use-gmail-address), [account approval](https://postmarkapp.com/support/article/1084-how-does-the-account-approval-process-work) *(R8a)*
- **MailerSend**: [pricing](https://www.mailersend.com/pricing), [sandbox and trial](https://www.mailersend.com/help/sandbox-mode-and-the-professional-trial) *(R8a)*; **Mailgun** (sandbox only): [pricing](https://www.mailgun.com/pricing/), [authorized recipients](https://help.mailgun.com/hc/en-us/articles/217531258-Authorized-Recipients) *(R8a)*; **Mailjet**: [pricing](https://www.mailjet.com/pricing/), [sender validation](https://dev.mailjet.com/docs/email-api/senders-domains/sender-validation), [help article](https://documentation.mailjet.com/hc/en-us/articles/19308800104091) *(R8a)*
- **SMTP2GO** (refuses Gmail sign-ups): [pricing](https://www.smtp2go.com/pricing/) *(R8a)*; **SendGrid** (free plan retired): [changelog](https://www.twilio.com/en-us/changelog/sendgrid-free-plan), [pricing](https://www.twilio.com/en-us/products/email-api/pricing) *(R8a)*
- **Cloudflare Email** (needs a domain on Cloudflare): [Email Service](https://developers.cloudflare.com/email-service/), [send e-mail from Workers](https://developers.cloudflare.com/email-routing/email-workers/send-email-workers/) *(R8a)*
- **Independent channels kept**: ntfy ([publish](https://docs.ntfy.sh/publish/), [config](https://docs.ntfy.sh/config/), [ntfy.sh](https://ntfy.sh/) *(D1, R6, V2, R8a)*); Healthchecks ([pricing](https://healthchecks.io/pricing/) *(D1, V2, R8a)*, [signalling failures](https://healthchecks.io/docs/signaling_failures/), [attaching logs](https://healthchecks.io/docs/attaching_logs/) *(R8a)*)

---

## 20. Phase 2 remote settings (R9)

The Phase 2 design ([10](10-phase2-remote-settings.md)) reuses the protocol sources of §§ on PI30/REVO above (Voltronic PI30 2014/2015, PI30MAX, PIP-GK/MK and Sorotec REVO protocol documents; mpp-solar `pi30.py`, `pi30revo.py`, `pi30max.py`) for the setter commands, their value encodings and their read-back fields. The full per-command table, evidence tags and source list are in [research/r9-remote-settings.md §16](../research/r9-remote-settings.md). The earlier draft and its safety review are kept for traceability in [research/r9-remote-settings-draft.md](../research/r9-remote-settings-draft.md).
