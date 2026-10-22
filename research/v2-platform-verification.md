# V2: Platform verification of R4 (apps), R5 (hardware) and R6 (cloud/web)

Checked on 2026-10-21 against primary sources. I fetched pages with WebFetch or curl, read GitHub sources through raw.githubusercontent.com and `gh api`, ran `openssl s_client` and queried the iTunes Lookup API.

Tags:
- **[CONFIRMED-primary]**: read today in an official doc, datasheet, source file or API, or measured directly.
- **[SECONDARY]**: forum, listing, blog or aggregator.
- **[INFERRED]**: my own reasoning.

Hard user constraints come from `user-constraints.md` in this folder:
- No new purchases.
- The free inverter port is the RJ45 **"COM"** jack. Its chassis silkscreen reads pin 1 RXD, pin 2 TXD, pin 8 GND.
- The **"BMS"** RJ45 jack (pin 1 485-B, pin 2 485-A, pin 4 CAN-H, pin 5 CAN-L) is occupied by the battery and must stay connected.
- The protocol is probably Voltronic PI30 at 2400 8N1.

I re-checked the port label photo (`scratchpad/ports_label_zoom.jpg`) and it matches. These constraints override parts of R4, R5 and R6.

---

## 1. Corrections

| # | Claim in report | Correct value | Source | Tag |
|---|---|---|---|---|
| C1 | **R4** §3.5 table: Firebase Spark RTDB download is "360 MB/day". | The Spark column says **"GB downloaded 10 GB/month"**. "No-cost up to 360 MB/day (equivalent to ~10 GB/month)" is the **Blaze** column. Spark Hosting transfer *is* 360 MB/day. R6 has this right. | https://firebase.google.com/pricing (raw HTML grep) | [CONFIRMED-primary] |
| C2 | **R4** §0.7, §5, §7 and **R5** §0, §3, §9 say to buy a MAX3232 (৳80–336), F-F jumpers, cable glands, a travel adapter and a multimeter. **R5** says "the port type is unknown, measure first". | The user cannot buy anything and has no multimeter. The port is **already identified by photo** as COM = RS232-style (RXD/TXD/GND), with RS485 only on the occupied BMS jack. The primary path is the "RS232-lite" wiring in `user-constraints.md`: the HW-0519 receiver reads inverter TX, and the ESP32 inverted TX drives inverter RX directly. All purchases go to an optional list. The MAX3232 is the clean upgrade if a shop trip ever happens. a local shop prices checked today: mini MAX3232 ৳80, DB9 MAX3232 ৳199, F-F jumpers ৳75, PG7 gland ৳12, all in stock. | user photo 6.jpg crop; (local shop listing) ; (local shop listing) | [CONFIRMED-primary photo] / [SECONDARY prices] |
| C3 | **R6** scope line and §10 say the protocol is "Modbus RTU or PI30 over the RS485 port". The `status` example uses `"proto":"modbus-9600"`. | The monitoring port is **RS232 COM**. The cloud design does not change because the field names are placeholders. Change the example to something like `"proto":"pi30-2400"`. The `live`/`hist` rules (§4) already accept any short numeric key. | port photo; R6 §4 | [CONFIRMED-primary photo] / [INFERRED] |
| C4 | **R6** §8.5 `lib_deps` combines **WiFiManager** (built on the core's synchronous `WebServer`), **ESPAsyncWebServer** and **ElegantOTA** in async mode (`-DELEGANTOTA_USE_ASYNC_WEBSERVER=1`). | The ElegantOTA v4 README says: "You cannot run the synchronous `WebServer` and `ESPAsyncWebServer` in the same firmware - pick one." Use the **sync stack**: WiFiManager 2.0.17, the core `WebServer` for the local page, and ElegantOTA 4.0.0 in default sync mode with `ElegantOTA.begin(&server, user, pass)`. That signature is confirmed in `src/ElegantOTA.h`. R6 §8.3 already suggested sync as the "simpler alternative". This makes it mandatory. | https://github.com/ayushsharma82/ElegantOTA (README L110–120; `src/ElegantOTA.h` L114) | [CONFIRMED-primary] |
| C5 | **R4** §3.1 and §5 step 3: ESPHome `http_request` POSTs samples to Firebase RTDB. | ESPHome has no Firebase Auth. RTDB REST needs `auth=<ID_TOKEN>` or an OAuth2 token. ID tokens "last for an hour", so the YAML would need hand-written sign-in and refresh lambdas. The other option, legacy database secrets, is "long-lived credentials". Secrets are widely reported to act as admin credentials that skip Security Rules [SECONDARY]. ESPHome requests also "run on the main loop". Put the cloud uplink in custom Arduino firmware (R6 design) and use ESPHome, if at all, only for protocol bring-up. | https://firebase.google.com/docs/database/rest/auth ; https://firebase.google.com/docs/auth/admin/manage-sessions ; https://esphome.io/components/http_request/ | [CONFIRMED-primary] + [SECONDARY] |
| C6 | **R5** §1.7: peak TX current is 340 mA (chip datasheet). | The **module** datasheet (WROOM-1 v1.8, Table 6-4) is the relevant figure: **355 mA** at 802.11b 1 Mbps / 20.5 dBm, and 344 mA for BLE at 20 dBm. Table 6-2 requires the external supply to deliver at least **0.5 A**. The conclusion (≥0.5 A) does not change. | https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf | [CONFIRMED-primary] |
| C7 | **R6** §5.5 tags the claim "iOS 26 opens every Home Screen site as a web app by default" as [SECONDARY]. | This is now **[CONFIRMED-primary]**. WebKit says: "By default, every website added to the Home Screen opens as a web app." Users can turn off "Open as Web App". No manifest is required, but manifest icons are used. | https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/ | [CONFIRMED-primary] |
| C8 | **R4** §1: the "Savx F4 3000-24V listing", ৳35,000 (regular ৳50,000), "USB / RS232 \| Optional: CAN / RS485". | The price and port text are confirmed. However, the page **title/og:title reads "Savx Solar Inverter F3 3000-24V( MPPT BASE)"**, while the slug is `…f4-3000-24v…` and the body says "F4 Series". The listing contradicts itself, which supports R4's advice to treat it as a hint only. | https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/ | [CONFIRMED-primary] |
| C9 | **R4**: Daraz PowMr WIFI-VM costs ৳3,755 (list ৳4,988). | The static HTML only shows `pdt_price "৳ 4,988"`. The ৳3,755 sale price is not visible. The point is moot because of the no-purchase rule and because the dongle is model-locked. | https://www.daraz.com.bd/products/powmr-232-wifi-vm-i315732315.html | [CONFIRMED-primary, partial] |
| C10 | **R6**: FirebaseClient v2.2.13 was released "2026-07-13". | GitHub shows it published **2026-07-14T04:31Z**, probably a time-zone difference. The version is current: `library.properties` says `version=2.2.13`. | `gh api repos/mobizt/FirebaseClient/releases`; https://raw.githubusercontent.com/mobizt/FirebaseClient/main/library.properties | [CONFIRMED-primary] |
| C11 | **R6** §8.5 uses `board = esp32-s3-devkitc-1` plus overrides. | This works because the pioarduino builder reads `build.arduino.memory_type` first. However, the stock `esp32-s3-devkitc-1.json` is the 8 MB, no-PSRAM board (`default_8MB.csv`, `-DARDUINO_USB_MODE=1`). The simpler and safer choice is R5's **`board = esp32-s3-devkitc1-n16r8`**, which sets `qio_opi`, `psram_type: opi`, `default_16MB.csv`, `-DBOARD_HAS_PSRAM` and 16 MB. | https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/boards/esp32-s3-devkitc1-n16r8.json ; https://raw.githubusercontent.com/platformio/platform-espressif32/develop/boards/esp32-s3-devkitc-1.json ; pioarduino `builder/main.py` `_get_board_memory_type` | [CONFIRMED-primary] |

---

## 2. Confirmed (load-bearing claims re-verified today)

### 2.1 Firebase (Spark)

| # | Claim | Verified value | Source |
|---|---|---|---|
| F1 | RTDB Spark limits | 100 simultaneous connections, 1 GB stored, 10 GB/month downloaded. Multiple databases per project are not available on Spark. | https://firebase.google.com/pricing [CONFIRMED-primary] |
| F2 | What counts as a "connection" | "one mobile device, browser tab, or server app connected to the database". The Connections metric **"does not include RESTful requests"**, so the device's REST writes do not use the 100 slots. | https://firebase.google.com/docs/database/usage/limits ; https://firebase.google.com/docs/database/usage/billing [CONFIRMED-primary] |
| F3 | What is billed as RTDB download | "All bytes downloaded … including protocol and encryption overhead". Each new TLS connection costs about 3.5 KB for the handshake, plus "tens of bytes" per message. **Requests denied by rules are billed.** **Console reads are billed.** Google advises HTTP keep-alive or SSE over REST churn. | https://firebase.google.com/docs/database/usage/billing [CONFIRMED-primary] |
| F4 | RTDB write limits | 1,000 writes/s (soft limit). Single write: 256 MB via REST, 16 MB via SDK. 64 MB/min. Depth under 32. Keys up to 768 B, with no `. $ # [ ] /` or control characters. | https://firebase.google.com/docs/database/usage/limits [CONFIRMED-primary] |
| F5 | RTDB regions | us-central1 (`*.firebaseio.com`), europe-west1 and **asia-southeast1** (`DATABASE_NAME.asia-southeast1.firebasedatabase.app`). Singapore is the only Asian region. | https://firebase.google.com/docs/database/locations [CONFIRMED-primary] |
| F6 | Firestore Spark | 1 GiB stored, 50K reads/day, 20K writes/day, 20K deletes/day, 10 GiB/month egress. Quotas reset "around midnight Pacific time". | https://firebase.google.com/docs/firestore/quotas [CONFIRMED-primary] |
| F7 | Hosting Spark | 10 GB storage. Transfer is **360 MB/day** on the pricing page but **10 GB/month** in the Hosting doc; the two disagree. Over the limit: "short grace period but then your sites will be disabled" until the next month. Maximum file size 2 GB. | https://firebase.google.com/pricing ; https://firebase.google.com/docs/hosting/usage-quotas-pricing [CONFIRMED-primary] |
| F8 | Products not available on Spark | Cloud Functions, Cloud Storage and App Hosting are all "Not applicable" on Spark. Since **2026-02-03**, Spark projects lose access to every Cloud Storage bucket, including `appspot.com`, and API calls return 402/403. FCM is no-cost. Auth gives 50K MAU, with phone auth not available. | https://firebase.google.com/pricing ; https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024 [CONFIRMED-primary] |
| F9 | Over-quota behaviour | "your app will be shut off for the remainder of that month". | https://firebase.google.com/support/faq [CONFIRMED-primary] |
| F10 | Auth REST endpoints | `POST https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=[API_KEY]` with JSON `{email,password,returnSecureToken:true}` returns `idToken, refreshToken, expiresIn, localId`. `POST https://securetoken.googleapis.com/v1/token?key=[API_KEY]` (form `grant_type=refresh_token&refresh_token=…`) returns `id_token, refresh_token, expires_in ("3600"), user_id, project_id`. The project number of the refresh token is validated against the API key (`PROJECT_NUMBER_MISMATCH`). | https://firebase.google.com/docs/reference/rest/auth [CONFIRMED-primary] |
| F11 | Token lifetime | ID tokens "last for an hour". Refresh tokens expire only when the user is deleted or disabled, or on a "major account change … password or email address updates". | https://firebase.google.com/docs/auth/admin/manage-sessions [CONFIRMED-primary] |
| F12 | Auth limits on Spark | 100 account creations per hour per IP. Password-reset email: 150/day on Spark. Identity Toolkit allows 30,000 requests/min per user. | https://firebase.google.com/docs/auth/limits [CONFIRMED-primary] |
| F13 | Firebase JS SDK | npm `latest` = **13.0.0**, published 2026-10-07. The 13.0.0 breaking changes are all in AI Logic, with none in App, Auth or RTDB. `gstatic.com/firebasejs/13.0.0/firebase-{app,auth,database}.js` all return HTTP 200. | https://registry.npmjs.org/firebase ; https://firebase.google.com/support/release-notes/js [CONFIRMED-primary] |

### 2.2 FirebaseClient library

| # | Claim | Verified value | Source |
|---|---|---|---|
| L1 | Version and status | `mobizt/FirebaseClient` v2.2.13 is current. The `Firebase-ESP-Client` and `Firebase-ESP32` repo descriptions start with "[DEPRECATED]". | `gh api` repo metadata [CONFIRMED-primary] |
| L2 | API names | All of these appear in `examples/RealtimeDatabase/Update/Update.ino` and `examples/BareMinimum/RealtimeDatabase/RealtimeDatabase.ino`: `UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD, 3000 /* expire period in seconds (<3600) */)`, `initializeApp(aClient, app, getAuth(user_auth), cb, "authTask")`, `app.getApp<RealtimeDatabase>(Database)`, `Database.url(...)`, `app.loop()`, `app.ready()`, `Database.update(aClient, path, object_t)`, `Database.set<T>(...)`, `aClient.lastError().message()/.code()`, `Firebase.printf`, and `ssl_client.setConnectionTimeout(1000); setHandshakeTimeout(5)`. | raw.githubusercontent.com/mobizt/FirebaseClient/main/examples/... [CONFIRMED-primary] |
| L3 | `print=silent` only in sync form | In `src/database/RealtimeDatabase.h`, `storeAsync()` contains `if (!async && etag.length() == 0) options.silent = true;`. Callback and AsyncResult forms therefore receive the echoed body, which is billed download. | `RealtimeDatabase.h` L986–995 [CONFIRMED-primary] |
| L4 | README notes | "Firestore data change listening and Realtime Database disconnected event are not support[ed] by the Firebase REST API". The library is "network independent" since v2.1.0. "In case ESP32, PlatformIO's platform-espressif32 core is outdated. You should use pioarduino". `ENABLE_PSRAM` is available. | https://raw.githubusercontent.com/mobizt/FirebaseClient/main/README.md [CONFIRMED-primary] |

### 2.3 ESP32-S3-N16R8 and toolchain

| # | Claim | Verified value | Source |
|---|---|---|---|
| E1 | Octal-PSRAM pins | "pins IO35, IO36, and IO37 are connected to the Octal SPI PSRAM and are not available for other uses". The DevKitC-1 guide says the same for GPIO35/36/37. | WROOM-1 datasheet v1.8 Table 3-1 note b; DevKitC-1 v1.1 guide [CONFIRMED-primary] |
| E2 | Temperature rating | "R8 and R16V series modules operate at –40 ~ 65 °C ambient". With PSRAM ECC enabled the limit rises to 85 °C, and usable PSRAM shrinks by 1/16. N16R8 is listed as –40 to 65 °C. | WROOM-1 datasheet v1.8 §1 and Table 1-1 [CONFIRMED-primary] |
| E3 | UART1 pins | J1-10 = GPIO17 "U1TXD" and J1-11 = GPIO18 "U1RXD". The RGB LED is on GPIO48 (v1.0) or GPIO38 (v1.1). Power can come in three "mutually exclusive" ways. | https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html [CONFIRMED-primary] |
| E4 | Arduino core defaults on S3 (`HardwareSerial.h` @3.3.12) | RX0 44, TX0 43, **RX1 15, TX1 16, RX2 19, TX2 20**. RX2/TX2 are the USB D-/D+ pins. `begin(baud, config, rxPin, txPin, bool invert=false, …)`, where "invert will invert RX/TX polarity". `setRxInvert(bool)` and `setTxInvert(bool)` also exist, which the RS232-lite path needs. | https://raw.githubusercontent.com/espressif/arduino-esp32/3.3.12/cores/esp32/HardwareSerial.h [CONFIRMED-primary] |
| E5 | Core releases | **3.3.12** (stable, 2026-09-18). **4.0.0-RC1** (2026-09-23) is a pre-release. | `gh api repos/espressif/arduino-esp32/releases` [CONFIRMED-primary] |
| E6 | boards.txt menu keys for `esp32s3` (@3.3.12) | `PSRAM.opi=OPI PSRAM`, `FlashMode.qio`, `FlashSize.16M`, `PartitionScheme.app3M_fat9M_16MB=16M Flash (3MB APP/9.9MB FATFS)`, `CDCOnBoot.default=Disabled` / `cdc=Enabled`, `USBMode.hwcdc`, `UploadMode.default=UART0 / Hardware CDC`. There is **no `default_16MB` entry** in the S3 Dev Module menu. | https://raw.githubusercontent.com/espressif/arduino-esp32/3.3.12/boards.txt [CONFIRMED-primary] |
| E7 | Partition tables | `app3M_fat9M_16MB.csv`: app0/app1 at 0x300000 each, ffat 0x610000/0x9E0000. `default_16MB.csv`: app0/app1 at 0x640000 each, spiffs 0xc90000/0x360000, coredump 0xFF0000. | arduino-esp32 `tools/partitions` @3.3.12 [CONFIRMED-primary] |
| E8 | pioarduino | Release `55.03.312-1` (2026-09-22) ships asset `platform-espressif32.zip`. The README says stable means "Arduino 3.3.12 and IDF v5.5.5". The board `esp32-s3-devkitc1-n16r8` exists, along with n4r2, n4r8, n8r2, n8r8, n16r2 and n16r16. | `gh api`; https://raw.githubusercontent.com/pioarduino/platform-espressif32/main/README.md [CONFIRMED-primary] |
| E9 | TLS heap settings in the Arduino prebuilt config | `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, `…FREE_PEER_CERT=y`, `…FREE_CONFIG_DATA=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, `CONFIG_LWIP_SNTP_UPDATE_DELAY=10800000`, `CONFIG_ESP_TASK_WDT_PANIC=y`. The S3 config adds `CONFIG_SPIRAM=y` and `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`. | esp32-arduino-lib-builder `configs/defconfig.common`, `defconfig.esp32s3` [CONFIRMED-primary] |

### 2.4 RS485 module (HW-0519 type)

| # | Claim | Verified value | Source |
|---|---|---|---|
| H1 | Identification | The order photo shows a blue "RS485 To TTL UART Sig Level Converter Module" bought from a local shop BD for ৳117. The layout is consistent with the auto-direction HW-0519 family. | user photo 4.webp [CONFIRMED-primary photo] / [INFERRED match] |
| H2 | TX/RX convention (straight through) | syssi's HW-0519 wiring is module **RXD ↔ ESP RX**, **TXD ↔ ESP TX**, with "power the RS485 module with 3.3V". ProtoSupplies documents the similar board the same way: "RXD … Connects to RXD TTL input on MCU", "TXD … Connects to TXD TTL output on MCU". It also notes **4.7 kΩ pull-ups on A/B**, poly fuses, TVS diodes, R0 = 120 Ω open as shipped, and unmarked chips. | https://raw.githubusercontent.com/syssi/esphome-modbus-solax-x1/main/README.md ; https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/ [SECONDARY] |
| H3 | Earth terminal | It is "not connected to the Gnd pin" according to a forum measurement. The vendor says indoor short runs should not use earth. The RoboticsBD page (RBD-1816, ৳440, 73 in stock) states "3.0V ~ 30V" supply and 110–256000 bps. | https://forum.arduino.cc/t/difference-between-the-max485-and-the-hw-0519-modules/704175 ; RoboticsBD RBD-1816 page (curl with browser UA) [SECONDARY] |

### 2.5 iOS, alerts and TLS

| # | Claim | Verified value | Source |
|---|---|---|---|
| I1 | Web Push on iOS | Supported in iOS/iPadOS 16.4+ for **Home Screen web apps only**. The manifest `display` must be `standalone` or `fullscreen`. The permission request must be "in response to direct user interaction". Delivery uses APNs, and no Apple Developer membership is needed. The Badging API is supported. | https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/ [CONFIRMED-primary] |
| I2 | Storage | Browser apps get up to 60% of disk and other apps up to 15%. Eviction is least-recently-used. `persist()` is granted by heuristics "like whether the website is opened as a Home Screen Web App". Home Screen apps "have their own counter of days of use", so they escape Safari's 7-day script-writable storage cap. | https://webkit.org/blog/14403/updates-to-storage-policy/ ; https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/ [CONFIRMED-primary] |
| I3 | ntfy.sh limits | 250 messages/day. Bursts of 60 requests are allowed, then one per 5 s. Messages up to 4,096 B, 5 e-mails/day, 200 MB/day bandwidth, 2 MB attachments. "the topic is essentially a password". Headers: `Title`, `Priority` (1–5), `Tags`, `Click`. Paid tiers are $5, $10 and $20 per month. | https://docs.ntfy.sh/publish/ ; https://ntfy.sh/ [CONFIRMED-primary] |
| I4 | ntfy iOS delivery | "The ntfy.sh server publishes the poll request message to Firebase, which forwards it to APNS". Self-hosted servers need `upstream-base-url`, otherwise delivery "can take hours". | https://docs.ntfy.sh/config/ [CONFIRMED-primary] |
| I5 | Telegram Bot API | Bot API 10.3 (2026-08-24). Requests take the form `https://api.telegram.org/bot<token>/METHOD`, accept GET or POST, and take query, form or JSON parameters. Keep to at most 1 msg/s per chat and about 30 msg/s for broadcasts. | https://core.telegram.org/bots/api ; https://core.telegram.org/bots/faq [CONFIRMED-primary] |
| I6 | TLS chains (measured today) | RTDB `*.asia-southeast1.firebasedatabase.app`: WR1 → **GTS Root R1**. `identitytoolkit` and `securetoken`: WE2 → **GTS Root R4**. `ntfy.sh`: YR1 → Root YR → **ISRG Root X1**. `api.telegram.org`: **Go Daddy Root CA G2**. | `openssl s_client -showcerts` [CONFIRMED-primary, measured] |
| I7 | Let's Encrypt roots | Root YR and Root YE were generated on 2025-09-03 and are cross-signed by X1 and X2. They are "not yet included in Root Program Trust Stores". X1 is trusted until 2030-06-04 and X2 until 2035-09-04. | https://letsencrypt.org/certificates/ [CONFIRMED-primary] |
| I8 | App availability | **Bangladesh is not listed** in Apple's Media Services availability list, and iTunes Lookup with `country=bd` returns 0 results. In both the **US and IN** storefronts: SmartESS 3.56.0 (seller 祥梯 孟, US rating 2.69 from 127), Energy-Mate 1.8.5, ntfy 1.7.0 (iOS 15.0+), Home Assistant 2026.10.1 (iOS 16.4+). Telegram 13.1 (iOS 15.0+) is in the US storefront. | https://support.apple.com/en-us/118205 ; https://itunes.apple.com/lookup?id=…&country=us\|in\|bd [CONFIRMED-primary] |

### 2.6 Alternative free tiers

| # | Service | Verified free tier | Source |
|---|---|---|---|
| A1 | Supabase Free | 500 MB database, 5 GB egress, 50K MAU, 1 GB files, 2 active projects. Projects are paused after **7 days** of low activity; API calls count as activity. Paused projects can be restored for up to 1 year. | https://supabase.com/pricing ; https://supabase.com/docs/guides/platform/free-project-pausing [CONFIRMED-primary] |
| A2 | Blynk Free | 5 devices, 1 user, 100,000 messages/month, **1-week retention**. The cheapest paid plan is $29/month. | https://blynk.io/pricing [CONFIRMED-primary] |
| A3 | ThingSpeak Free (non-commercial) | 3M messages/year, 15 s minimum interval, 4 channels. Storage is capped at 10M messages ("3+ years at minimum update interval"). A message carries up to 8 fields. No retention period is stated. | https://thingspeak.mathworks.com/pages/license_faq [CONFIRMED-primary] |
| A4 | Cloudflare Workers + D1 Free | Workers: 100K requests/day, 10 ms CPU, 5 cron triggers, Error 1027 when over the limit. D1: 10 databases, 500 MB each, 5 GB total, 5M rows read/day, **100K rows written/day**, reset at 00:00 UTC, and queries **fail** when over the limit. | https://developers.cloudflare.com/workers/platform/limits/ ; https://developers.cloudflare.com/d1/platform/limits/ ; https://developers.cloudflare.com/d1/platform/pricing/ [CONFIRMED-primary] |

### 2.7 Library versions (GitHub API)

All [CONFIRMED-primary]:

| Library | Version (date) | Licence |
|---|---|---|
| ElegantOTA | 4.0.0 (2026-09-05) | AGPL-3.0 |
| ESP32Async/ESPAsyncWebServer | v3.12.1 (2026-09-12) | LGPL-3.0 |
| WiFiManager | v2.0.17 (2024-03-02; repo pushed 2026-02-25) | MIT |
| uPlot | 1.6.32 (2025-03-14) | MIT |
| ArduinoJson | v7.4.3 (2026-03-02) | MIT |

### 2.8 BDT prices

Prices from a local shop product JSON-LD, fetched today. All are [SECONDARY] listings, all in stock.

| Item | Price |
|---|---|
| ESP32-S3 DevKitC-1 N16R8 | ৳699 |
| Mini MAX3232 | ৳80 |
| DB9 MAX3232 | ৳199 |
| F-F jumpers | ৳75 |
| PG7 gland | ৳12 |

The user's own order photo shows the RS485 module at ৳117 and the M-M jumpers at ৳187 [CONFIRMED-primary photo].

---

## 3. Missed risks and recommendations

1. **Hard-constraint mismatch.** This is the biggest gap.
   - R4 and R5 build the plan around buying a MAX3232.
   - R6 assumes an RS485 port.
   - The final plan must use the **RS232-lite** path with only the parts the user already has, and treat every purchase as optional.

   [CONFIRMED-primary photo; INFERRED]

2. **RS232-lite electrical risks.** None of the three reports analysed this path.
   - RS-232 mark (idle) is a negative voltage, roughly −3 to −15 V. With B tied to GND, the HW-0519's **A input sits negative most of the time**.
   - The original Maxim-spec MAX3485 lists A/B absolute maximum as −7.5 to +12.5 V. Clone datasheets quote ±13 to ±15 V. The chip on this module is unmarked. [SECONDARY: aggregator snippets via search; ProtoSupplies "chip markings are removed"]
   - The module's TVS diodes and poly fuses will clamp, and RS-232 drivers are current-limited, so the circuit is **probably survivable but off-label**. [INFERRED]
   - Mitigations, all [INFERRED]:
     - (a) Find the inverter-TX pin with the "idle-level" test from `user-constraints.md` **before** any ESP32 GPIO touches the jack. ESP32 V_IL min is −0.3 V, so a negative idle line on a GPIO risks damage.
     - (b) Do **not** hard-wire either module header pin to 3V3 until you know which is DI and which is RO. Sources disagree on the labels (R5 §2.3). Read both pins as ESP32 inputs first. RO follows the line, so it reads LOW while A sits below B. Then hold DI HIGH from a GPIO rather than a solid jumper.
     - (c) If a resistor turns up, a series resistor of at most about 4.7 kΩ in the A lead limits clamp current. Larger values fight the module's 4.7 kΩ A pull-up and can push the mark level above the −200 mV receiver threshold.
     - (d) In firmware, use `Serial1.begin(2400, SERIAL_8N1, RX, TX, true)`, which inverts both RX and TX ([CONFIRMED-primary] HardwareSerial.h). Use `setRxInvert`/`setTxInvert` to probe each polarity on its own.

3. **There is no alert when the logger or router dies.** With no server, nothing can say "device offline", yet that is the most likely failure during load-shedding.
   - Add **Healthchecks.io Hobbyist (free, $0, 20 checks)** as a dead-man's switch.
   - The device pings `https://hc-ping.com/<uuid>` every 5 minutes (GET/POST/HEAD; "do not ping a single check more than 5 times per minute").
   - Set grace to 15–30 minutes. Healthchecks then alerts by e-mail, and Telegram is documented as an integration. An ntfy integration is reported by its maintainer but I did not see it on official pages.
   - TLS chain measured today: Sectigo DV E36 → Sectigo Root E46 → **USERTrust ECC** root, which must be embedded.

   [CONFIRMED-primary: https://healthchecks.io/pricing/ , https://healthchecks.io/docs/http_api/ , https://healthchecks.io/integrations/telegram ; SECONDARY: ntfy integration]

4. **Telegram may be blocked in Bangladesh.**
   - Telegram was made inaccessible on mobile data on 2024-08-02 [SECONDARY: summary of Prothom Alo/TBS reporting].
   - On 2025-09-28 a home-ministry committee discussed slowing it and then shutting it down after the election schedule. Prothom Alo reported "an initial decision was made to control the use of Telegram and Botim".
   - Keep **ntfy as primary**, Telegram as secondary, and Healthchecks e-mail as the third channel.

   [SECONDARY: https://en.prothomalo.com/bangladesh/government/igr6isfgo4]

5. **The ntfy dependency chain.**
   - On iOS, notifications for ntfy.sh topics travel ntfy.sh → Firebase → APNs [CONFIRMED-primary]. That is two third parties.
   - Rate limits are per "visitor". Anonymous publishers are probably identified by IP, and Bangladeshi ISPs commonly use CGNAT, so the 250 messages/day quota could be shared [INFERRED; low risk at the R6 alert rates].
   - Use a long random topic name, because the topic acts as the password.

6. **The Let's Encrypt chain will change.** ntfy.sh is already on the new YR1 → Root YR chain, cross-signed by X1.
   - Embed **ISRG Root X1 + Root YR**, and **X2 + Root YE** in case of an ECDSA certificate.
   - Alternatively, use `setCACertBundle()` with a Mozilla bundle.
   - Re-test after each LE hierarchy change.

   [CONFIRMED-primary chain and LE page; INFERRED advice]

7. **NTP vs TLS.**
   - ESP-IDF's `CONFIG_MBEDTLS_HAVE_TIME_DATE` defaults to **n**. The Kconfig help says certificate "valid from"/"valid to" fields "are ignored". The Arduino defconfig does not enable it.
   - So **HTTPS works before NTP sync**, which is good after a power cut. Only data timestamps need NTP (R6 §3.5 uptime scheme).
   - The downside is that an expired root will not be detected, so embed several roots.

   [CONFIRMED-primary: esp-idf release/v5.5 `components/mbedtls/Kconfig` L704–718; lib-builder defconfig]

8. **Token lifecycle traps.**
   - Refresh tokens die when the device user's password or email changes, or when the user is disabled or deleted [CONFIRMED-primary]. Provide a field re-provisioning path: hold BOOT/GPIO0 after boot to open the WiFiManager portal with the credential fields.
   - Persist the refresh token in NVS.
   - Never print URLs containing `auth=` (R6 already says this).

9. **Spark shut-off and download guards.**
   - Over-quota means the app is shut off for the rest of the month, and Hosting is disabled until the next month [CONFIRMED-primary].
   - Console reads and rule-denied requests are billed [CONFIRMED-primary].
   - Don't leave the console data viewer open on `/devices/inv1/hist`.
   - Dashboards should only query date-bounded nodes.
   - Firmware needs exponential back-off with at most 1 retry per 5 s.
   - Use FirebaseClient's **sync/await** form for periodic writes, or plain REST with `print=silent` (L3).

10. **Account and project longevity.**
    - The Firebase pricing and FAQ pages state no Spark inactivity-deletion policy (searched, none found).
    - Google may delete an **inactive Google Account and its data after 2 years** of inactivity across Google [CONFIRMED-primary: https://support.google.com/accounts/answer/12418290].
    - Signing into the PWA uses Firebase Auth, not the Google Account, so the project owner must use the Google Account itself occasionally. Export RTDB JSON yearly. [INFERRED]

11. **iOS PWA quirks.**
    - (a) A Home Screen web app keeps its own storage separate from Safari, so sign in **inside the installed app** [CONFIRMED-primary I2; INFERRED].
    - (b) R6's `sw.js` caches only same-origin requests, but the Firebase SDK (gstatic) and uPlot (jsDelivr) are cross-origin. As written, the shell **won't open offline**. Self-host or bundle the SDK and uPlot on Firebase Hosting [INFERRED].
    - (c) The RTDB WebSocket is suspended when the app is backgrounded. Re-render on `visibilitychange` and always show "updated N s ago" [INFERRED].
    - (d) Web Push still needs a VAPID or FCM sender holding credentials, which Spark cannot host. Keep ntfy [CONFIRMED-primary I1, F8].

12. **App Store storefront.** Bangladesh is not an App Store country [CONFIRMED-primary]. ntfy and Telegram install only if the user's Apple ID belongs to another storefront, such as US or IN, where they are confirmed available. Ask the user which one. The PWA path needs no App Store.

13. **Power and brownouts.**
    - The module needs a supply that can deliver at least 0.5 A, and TX peaks at 355 mA at 3.3 V [CONFIRMED-primary].
    - Without buying a capacitor, use a short good cable and plug the charger into the **inverter output** together with the router/ONU, otherwise uploads stop in outages even though the logger keeps running.
    - Optionally cap TX power with `WiFi.setTxPower(WIFI_POWER_15dBm)`. The API exists in `WiFiGeneric.h`, with enum value 60 [CONFIRMED-primary API; INFERRED benefit].
    - Log `esp_reset_reason()` and RSSI in `status`.

14. **Heat.**
    - The N16R8 limit is 65 °C ambient, against Bangladeshi room temperatures of 35–40 °C plus inverter exhaust and a clear-lid "greenhouse".
    - The datasheet's ECC option (`CONFIG_SPIRAM_ECC_ENABLE`, default n, octal only) raises the limit to 85 °C but is off in the Arduino prebuilt libraries. Enabling it needs a custom sdkconfig, for example pioarduino hybrid compile [CONFIRMED-primary Kconfig; INFERRED feasibility].
    - Simpler measures: keep the outage queue in flash (LittleFS), not PSRAM; mount the box away from exhaust and sun; log `temperatureRead()`, which exists in `esp32-hal-misc.c`.

15. **Toolchain pinning.**
    - Pin `…/releases/download/55.03.312-1/platform-espressif32.zip` rather than `…/stable/…`.
    - Avoid Arduino core 4.0.0-RC1 and pioarduino 61.04.00-RC1, both pre-releases.

    [CONFIRMED-primary E5, E8]

16. **Open sign-up.** Anyone holding the Web API key can create Auth accounts (100/hour/IP).
    - The UID-pinned rules in R6 make extra accounts harmless.
    - I could not confirm a console toggle to disable sign-up; it is unverified.
    - Do not write rules of the form `auth != null`.

    [CONFIRMED-primary F12; INFERRED]

17. **The ESPHome route should not be the cloud path** (C5). If ESPHome is used for protocol discovery, it needs `rx_pin`/`tx_pin` `inverted: true`, which the docs support. Its logger uses UART0 by default. [CONFIRMED-primary: https://esphome.io/components/uart/]

---

## 4. Final platform recommendation

### Recommended stack: ESP32 custom firmware → Firebase Spark RTDB → Firebase-Hosted PWA, with ntfy alerts

**Device**
- ESP32-S3-N16R8 running custom Arduino firmware.
- Toolchain: pioarduino `55.03.312-1` (Arduino-ESP32 3.3.12 / IDF 5.5.5) with `board = esp32-s3-devkitc1-n16r8`.
- Inverter link: UART1 on GPIO17/18, 2400 8N1 with inverted RX/TX, using the RS232-lite wiring through the HW-0519 receiver to the COM jack.
- Power: the existing USB charger on an inverter-backed socket.

**Uplink**
- FirebaseClient 2.2.13 with `UserAuth` for a dedicated device user, using the sync/await calls in **one** uplink task.
- Fallback within the stack: plain REST with `print=silent` and the R6 §3.3 token code.
- Database: RTDB in **asia-southeast1**.
- Write schedule: `live` every 10 s; 1-minute aggregates under `hist/{date}/{hhmm}`; `daily` totals.
- Outages: a LittleFS outage queue with idempotent back-fill.

**Viewing**
- A Firebase Hosting PWA using the Firebase JS SDK 13.0.0 modular build and uPlot 1.6.32, both **self-hosted**, with email/password sign-in.
- An ESP32 LAN page using the sync stack: WiFiManager, `WebServer` and ElegantOTA sync.

**Alerts**
- **ntfy.sh** as primary.
- Telegram as secondary.
- **Healthchecks.io** as the dead-man's switch, alerting by e-mail.

**Why this stack**
- It costs nothing and needs no server.
- The R6 data model uses about 8% of the 1 GB storage per year and well under 10% of the 10 GB/month download, so it is far from the hard shut-off.
- REST writes do not use the 100-connection limit.
- It reaches the iPhone as a Home Screen web app, which iOS 26 makes the default.
- The user owns the data.
- It keeps working through internet outages by buffering and back-filling.
- Every API it relies on was verified today (§2).

**What must be validated on the bench first**
- The RS232-lite physical layer (§3 item 2).
- The PI30 replies at 2400 baud.

### Fallback: ESP32 custom firmware → ThingSpeak (free, non-commercial) + ESP32 LAN page + ntfy

**Use it when**
- The Firebase part proves too much to build, or
- the user wants zero web-app code.

**Why it works**
- Writing is a plain HTTPS request with a write API key: no token refresh, no rules, no hosting.
- Charts open in iPhone Safari.
- The free licence gives 3M messages/year, which covers 4 channels at 1 per minute (about 2.1M/year).
- Storage holds 10M messages, described as "3+ years at minimum update interval".

**Costs to accept**
- 8 fields per channel and a 15 s minimum interval.
- A clunky UI and no PWA.
- Non-commercial use only.
- No stated retention period.

[CONFIRMED-primary A3; INFERRED fit]

**If server-side logic is wanted later** (cron aggregation, server-side alerts, Web Push): the upgrade path is **Cloudflare Workers + D1**. The free tier allows 100K requests/day and 100K rows written/day, resetting at 00:00 UTC, and queries fail when over the limit (A4).

**Rejected options**
- Vendor dongle with SmartESS/Energy-Mate: it needs a purchase, which the constraint forbids, and it is model-locked and cloud-only. SmartESS is rated 2.69 from 127 ratings in the US store.
- Blynk: 1-week retention.
- Supabase: 7-day pause risk during long outages.
- Home Assistant and SolarAssistant: they need an always-on host the user doesn't have.

---

## 5. Sources (all fetched 2026-10-21)

**Firebase**
- https://firebase.google.com/pricing
- https://firebase.google.com/support/faq
- https://firebase.google.com/docs/database/usage/limits
- https://firebase.google.com/docs/database/usage/billing
- https://firebase.google.com/docs/database/locations
- https://firebase.google.com/docs/database/rest/auth
- https://firebase.google.com/docs/reference/rest/auth
- https://firebase.google.com/docs/auth/admin/manage-sessions
- https://firebase.google.com/docs/auth/limits
- https://firebase.google.com/docs/firestore/quotas
- https://firebase.google.com/docs/hosting/usage-quotas-pricing
- https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024
- https://firebase.google.com/support/release-notes/js
- https://registry.npmjs.org/firebase

**Google account policy**
- https://support.google.com/accounts/answer/12418290

**FirebaseClient**
- https://github.com/mobizt/FirebaseClient : `README.md`, `library.properties`, `src/database/RealtimeDatabase.h`, `examples/RealtimeDatabase/Update/Update.ino`, `examples/BareMinimum/RealtimeDatabase/RealtimeDatabase.ino`
- GitHub API metadata for mobizt/Firebase-ESP-Client and mobizt/Firebase-ESP32

**Espressif**
- WROOM-1/1U datasheet v1.8: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- DevKitC-1 v1.1 user guide: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
- arduino-esp32 @3.3.12: `cores/esp32/HardwareSerial.h`, `boards.txt`, `tools/partitions/*.csv`, `libraries/WiFi/src/WiFiGeneric.h`, `libraries/NetworkClientSecure/src/NetworkClientSecure.h`, `cores/esp32/esp32-hal-misc.c`
- esp32-arduino-lib-builder: `configs/defconfig.common`, `configs/defconfig.esp32s3`
- esp-idf release/v5.5: `components/mbedtls/Kconfig`, `components/esp_psram/esp32s3/Kconfig.spiram`

**PlatformIO**
- pioarduino: README, `boards/esp32-s3-devkitc1-n16r8.json`, `builder/main.py` @55.03.312-1, releases
- Official PlatformIO: `boards/esp32-s3-devkitc-1.json` (develop branch)

**RS485 module**
- https://raw.githubusercontent.com/syssi/esphome-modbus-solax-x1/main/README.md
- https://protosupplies.com/product/scm-ttl-to-rs-485-interface-module/
- https://forum.arduino.cc/t/difference-between-the-max485-and-the-hw-0519-modules/704175
- RoboticsBD RBD-1816 product page
- MAX3485 absolute maximum ratings: search-aggregator snippets [SECONDARY]

**ESPHome**
- https://esphome.io/components/uart/
- https://esphome.io/components/http_request/

**iOS / WebKit**
- https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/
- https://webkit.org/blog/14403/updates-to-storage-policy/
- https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/
- https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/
- https://support.apple.com/en-us/118205
- iTunes Lookup API (US, IN, BD)

**Alerts**
- https://docs.ntfy.sh/publish/
- https://docs.ntfy.sh/config/
- https://ntfy.sh/
- https://core.telegram.org/bots/api
- https://core.telegram.org/bots/faq
- https://letsencrypt.org/certificates/
- https://healthchecks.io/pricing/
- https://healthchecks.io/docs/http_api/
- https://healthchecks.io/docs/configuring_checks/
- https://healthchecks.io/integrations/telegram
- https://en.prothomalo.com/bangladesh/government/igr6isfgo4 [SECONDARY]

**Alternatives**
- https://supabase.com/pricing
- https://supabase.com/docs/guides/platform/free-project-pausing
- https://blynk.io/pricing
- https://thingspeak.mathworks.com/pages/license_faq
- https://developers.cloudflare.com/d1/platform/limits/
- https://developers.cloudflare.com/d1/platform/pricing/
- https://developers.cloudflare.com/workers/platform/limits/

**Libraries (GitHub API)**
- ayushsharma82/ElegantOTA (README, `src/ElegantOTA.h`)
- ESP32Async/ESPAsyncWebServer
- tzapu/WiFiManager
- leeoniya/uPlot
- bblanchon/ArduinoJson
- binwiederhier/ntfy

**Shops and listings**
- https://ipsbazarbd.com/product/savx-solar-inverter-f4-3000-24v-mppt-base/
- https://www.daraz.com.bd/products/powmr-232-wifi-vm-i315732315.html
- a local shop product pages linked in C2 and §2.8

**Measured**
- `openssl s_client` on `api.telegram.org`, `ntfy.sh`, `identitytoolkit.googleapis.com`, `securetoken.googleapis.com`, `*.asia-southeast1.firebasedatabase.app` and `hc-ping.com`

**User data**
- User photos `images/4.webp`, `images/6.jpg` (crop `scratchpad/ports_label_zoom.jpg`)
- `research/user-constraints.md`

**Not re-verified** (low weight for the decision)
- InfluxDB Cloud and Grafana Cloud free retention
- Adafruit IO
- Nabu Casa and SolarAssistant prices
- ESPHome `web_server` 50-point history
- R5's Apple hotspot note
- Daraz MAX3232 prices (pages are JavaScript-rendered; moot under the no-purchase rule)
