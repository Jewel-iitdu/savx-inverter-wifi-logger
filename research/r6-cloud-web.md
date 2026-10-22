# R6: Free cloud backend and iPhone web dashboard for the Savx F4 3 kW datalogger (ESP32-S3-N16R8)

Research date: 2026-10-21. Scope: this file covers only the cloud, web and alerting side. The inverter protocol (Modbus RTU or PI30 over the RS485 port) is covered by other research tracks. Field names used here (`bv`, `soc`, `pvw`, ...) are placeholders that the firmware maps from whatever the protocol returns.

Confidence tags:
- **[CONFIRMED-primary]**: read in an official doc page, an official repo file, or measured directly (TLS chains, CDN file sizes, response headers).
- **[SECONDARY]**: forum, blog, third-party listing, or search-result summary.
- **[INFERRED]**: my own reasoning or arithmetic.

Hard user constraints (from `user-constraints.md` in this research folder) that shape the design:
- No new purchases, so there is no RTC module, no SD card and no extra power supply.
- The ESP32 runs only from the 5 V 2 A USB charger.
- The phone is an iPhone and the Wi-Fi is 2.4 GHz.

---

## 0. TL;DR recommendation

| Layer | Choice | Why |
|---|---|---|
| Cloud DB | **Firebase Realtime Database (RTDB), Spark (no-cost) plan, location `asia-southeast1` (Singapore)** | Spark RTDB has no per-operation daily caps. The only limits are 1 GB stored, 10 GB/month downloaded and 100 simultaneous connections ([pricing](https://firebase.google.com/pricing), [limits](https://firebase.google.com/docs/database/usage/limits)) [CONFIRMED-primary]. Singapore is the closest RTDB region to Bangladesh, since only us-central1, europe-west1 and asia-southeast1 exist ([locations](https://firebase.google.com/docs/database/locations)) [CONFIRMED-primary]. |
| Device to cloud | **mobizt FirebaseClient v2.2.13** (MIT, released 2026-07-13) with a dedicated email/password "device" user, or the plain REST fallback in §3.3 | Actively maintained. Its UserAuth handles the token refresh. Its predecessors Firebase-ESP-Client and Firebase-ESP32 are now tagged DEPRECATED on GitHub [CONFIRMED-primary]. |
| Data shape | `live` node overwritten every 10 s; 1-minute aggregates under `hist/{YYYY-MM-DD}/{hhmm}`; `daily/{YYYY-MM-DD}` totals; `events` | Roughly 83 MB/year in total, so 1 GB lasts more than 10 years. Downloads stay under about 1 GB/month against the 10 GB allowance [INFERRED, §1.2]. |
| Web dashboard | **Static PWA on Firebase Hosting (`*.web.app`)**, Firebase JS SDK **13.0.0** modular from the gstatic CDN, **uPlot 1.6.32**, email/password sign-in | Small download (uPlot is about 22 KB gzipped). Works as an iOS Home Screen web app. Email/password sidesteps the iOS redirect and popup sign-in problems. |
| Alerts to iPhone | **ntfy.sh** via HTTPS POST from the ESP32 (free iOS app), with a **Telegram bot** as backup | Neither needs a server. iOS Web Push needs a sender holding VAPID or FCM credentials, which is not practical on Spark without Cloud Functions (§6). |
| On-device extras | WiFiManager captive portal, local web page at `http://solar.local` (mDNS), ElegantOTA (AGPL-3.0 Lite) or ArduinoOTA, task watchdog, LittleFS outage buffer with back-fill | Wi-Fi or router outages during load-shedding are likely. The device buffers to flash and back-fills when the connection returns (§3.7). |
| Avoid | Firestore for the 5–10 s live value; Cloud Storage; Cloud Functions; raw 5 s history | Firestore Spark allows 20K writes/day. Cloud Storage needs Blaze since 2026-02-03. Functions are "Not applicable" on Spark. Raw 5 s history is about 2.4 GB/year (§1.2). |

---

## 1. Firebase Spark plan quotas (checked 2026-10-21)

### 1.1 Quota table

| Product | Spark (no-cost) limit | Source / tag |
|---|---|---|
| **RTDB** simultaneous connections | **100** (Blaze: 200,000) | [pricing](https://firebase.google.com/pricing); limits page footnote says the Spark limit is 100 ([limits](https://firebase.google.com/docs/database/usage/limits)) [CONFIRMED-primary] |
| RTDB storage | **1 GB** | [pricing](https://firebase.google.com/pricing), [billing](https://firebase.google.com/docs/database/usage/billing) [CONFIRMED-primary] |
| RTDB download | **10 GB/month** (the Blaze column lists "No-cost up to 360 MB/day, equivalent to ~10 GB/month") | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| RTDB multiple databases per project | Not on Spark | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| RTDB what is billed | Storage, plus **outbound** traffic at the session layer, including protocol and TLS overhead (about 3.5 KB per TLS handshake and tens of bytes of TLS record header per message). Uploaded bytes are not billed; responses and overhead are. Traffic from requests denied by rules is billed too. | [billing](https://firebase.google.com/docs/database/usage/billing) [CONFIRMED-primary] |
| RTDB per-op limits | Write rate about 1,000/s sustained (soft); max write 256 MB (REST) or 16 MB (SDK); 64 MB/min written; depth under 32; key at most 768 bytes, and keys cannot contain `. $ # [ ] /` or control characters | [limits](https://firebase.google.com/docs/database/usage/limits) [CONFIRMED-primary] |
| **Firestore** (Standard edition) | 1 GiB stored; **50K reads/day**; **20K writes/day**; **20K deletes/day**; 10 GiB/month egress; one free database per project; quotas reset around midnight Pacific | [pricing](https://firebase.google.com/pricing), [quotas](https://firebase.google.com/docs/firestore/quotas) [CONFIRMED-primary] |
| Firestore (Enterprise edition) | 1 GiB; 50K read units/day; 40K write units/day; 50K realtime-update units/day | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| Firestore document limits | 1 MiB per doc; map/array depth 20; 40,000 index entries per document; TTL deletes need billing | [quotas](https://firebase.google.com/docs/firestore/quotas), [best practices](https://firebase.google.com/docs/firestore/best-practices) [CONFIRMED-primary] |
| **Hosting** | Storage 10 GB. Transfer: the pricing page says **360 MB/day**; the Hosting usage page says **10 GB/month**. The two pages disagree, so plan for the stricter 360 MB/day. Over the cap on a non-Blaze plan, sites are disabled until next month. Max file size 2 GB. Custom domain and SSL included. | [pricing](https://firebase.google.com/pricing), [hosting quotas](https://firebase.google.com/docs/hosting/usage-quotas-pricing) [CONFIRMED-primary] |
| **Authentication** | 50K MAU (email/password, Google and others); SAML/OIDC 50 MAU; phone auth not available on Spark (billed per SMS) | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| **Cloud Functions** | **Not applicable on Spark.** On Blaze: 2M invocations/month and 400K GB-s/month at no cost. | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| **Cloud Storage for Firebase** | **Needs Blaze, even for the default bucket.** The requirement took effect **2026-02-03**. On Spark, data in `*.appspot.com` buckets stays but is inaccessible; API calls return 402/403. | [Storage FAQ](https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024) [CONFIRMED-primary] |
| App Hosting | Not on Spark (needs billing) | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| Cloud Messaging (FCM) | Listed under no-cost products | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| Remote Config | New per-request pricing from **2026-09-01**; Spark gets 100,000 fetches/day/project (not needed here) | [pricing](https://firebase.google.com/pricing) [CONFIRMED-primary] |
| **Over-quota behaviour** | If Spark resources are exceeded in a calendar month, the app is **shut off for the rest of that month**. Spark has no surprise bill, but it does have a hard outage. | [Firebase FAQ](https://firebase.google.com/support/faq) [CONFIRMED-primary] |

Changes between 2024 and 2026 that matter here:
1. Cloud Storage moved to Blaze-only (enforced 2026-02-03). Do not plan OTA files or CSV exports in Firebase Storage on Spark [CONFIRMED-primary].
2. Remote Config pricing changed on 2026-09-01 (irrelevant here).
3. Firebase JS SDK 13.0.0 shipped on 2026-10-07. Its breaking changes are all in AI Logic; nothing changed in Auth or RTDB ([JS release notes](https://firebase.google.com/support/release-notes/js)) [CONFIRMED-primary].

### 1.2 Data-volume arithmetic for about 30 fields

Assumptions [INFERRED]:
- **Live JSON**: 30 short keys with rounded values, about **354 bytes** (example in §2.2, measured with `json.dumps` compact).
- **1-minute aggregate**: 14 fields, about **147 bytes** including its `"hhmm":` key.
- **Daily totals**: about 146 bytes.
- **Response header**: an RTDB REST response carries about **300 bytes** of HTTP headers. I measured 299 bytes with `curl -D` against `docs-examples.firebaseio.com` [CONFIRMED-primary, measured].
- **TLS overhead**: about 3.5 KB per handshake and about 40 bytes of TLS record overhead per message ([billing](https://firebase.google.com/docs/database/usage/billing)).
- RTDB does not publish its exact on-disk overhead per node, so storage figures are JSON-size estimates [INFERRED].

**Storage if raw samples are kept as history (push-ID keys):**

| Upload interval | Samples/day | Raw history/day | Raw history/year | Fits in 1 GB? |
|---|---|---|---|---|
| 5 s | 17,280 | 6.50 MB | **2.37 GB** | No: full in about 5 months |
| 10 s | 8,640 | 3.25 MB | **1.19 GB** | No: full in about 10 months |
| 30 s | 2,880 | 1.08 MB | 0.40 GB | About 2.2 years |
| 60 s | 1,440 | 0.54 MB | 0.20 GB | About 4.5 years |

**Storage if only aggregates are kept as history (recommended):**

| History granularity | Rows/day | Per day | Per year | Years in about 0.9 GB usable |
|---|---|---|---|---|
| 1-minute | 1,440 | 212 KB | 77 MB | **about 11.6** |
| 5-minute | 288 | 42 KB | 15.5 MB | about 58 |
| 15-minute | 96 | 14 KB | 5.2 MB | about 175 |
| daily totals | 1 | 0.15 KB | 0.05 MB | effectively forever |

**Download (billed) for the device's own writes.** Each write's response counts as download. With `print=silent` the body is empty and only headers remain:

| Write every | Writes/day | Keep-alive connection | New TLS connection per write |
|---|---|---|---|
| 5 s | 17,280 | 5.9 MB/day, 0.18 GB/month | 66 MB/day, **2.0 GB/month** |
| 10 s | 8,640 | 2.9 MB/day, 0.09 GB/month | 33 MB/day, 1.0 GB/month |
| 30 s | 2,880 | 1.0 MB/day, 0.03 GB/month | 11 MB/day, 0.33 GB/month |
| 60 s | 1,440 | 0.5 MB/day, 0.01 GB/month | 5.5 MB/day, 0.17 GB/month |

If the response echoes the payload (no `print=silent`), add 354 B per write. At 10 s that is about 3 MB/day, which is still small [INFERRED].

**Download for the dashboard.** The live listener over the SDK WebSocket costs about 354 B plus about 60 B framing per update:

| Live update every | Dashboard open 1 h/day | Open 24 h/day |
|---|---|---|
| 5 s | 0.3 MB/day | 7.2 MB/day, 0.21 GB/month |
| 10 s | 0.15 MB/day | 3.6 MB/day, 0.11 GB/month |
| 30 s | 0.05 MB/day | 1.2 MB/day |

History loads:
- "Today" at 1-minute resolution: about **212 KB**.
- Last 7 days at 15-minute resolution: about 100 KB.
- Last 30 daily totals: about 5 KB.
- A full year of daily totals: about 58 KB.
- 50 "today" views a day comes to about 10.6 MB/day, or 0.32 GB/month.

**Conclusion [INFERRED].** The comfortable design is:
- `live` written every 10 s (5 s is also fine) over a **kept-alive** TLS connection;
- 1-minute aggregates plus daily totals as history;
- raw samples never stored in the cloud.

Total download is under about 1 GB/month (10% of Spark) and storage is about 83 MB/year (8% of Spark per year).

The only realistic ways to break Spark are:
- storing raw 5–10 s history;
- a bug that reconnects TLS on every write in a tight loop. A 1-second retry loop with new handshakes is about 300 MB/day, which can exhaust 10 GB in a month;
- leaving the Firebase console data viewer open on a big node. Console reads are billed ([billing](https://firebase.google.com/docs/database/usage/billing)).

Add a client-side guard: exponential back-off on errors, and at most 1 retry per 5 s.

---

## 2. Data model

### 2.1 RTDB vs Firestore for this time series

| Criterion | RTDB (Spark) | Firestore (Spark) |
|---|---|---|
| Live value every 5–10 s | No operation quota; only bandwidth | 8,640–17,280 writes/day against a 20K/day cap; too tight once history writes are added [INFERRED from [quotas](https://firebase.google.com/docs/firestore/quotas)] |
| Dashboard listener | Billed by bytes | **1 read per update** of the watched doc. A dashboard open all day at 10 s uses 8,640 of 50K reads/day ([pricing](https://firebase.google.com/docs/firestore/pricing)) [CONFIRMED-primary] |
| Storage efficiency | JSON-ish, no automatic indexes | Every field, including map subfields, is **auto-indexed** ascending and descending, and index entries count as storage ([index overview](https://firebase.google.com/docs/firestore/query-data/index-overview), [storage size](https://firebase.google.com/docs/firestore/storage-size)) [CONFIRMED-primary] |
| Queries | Order by key with range and limit; no automatic aggregation | Richer queries (not needed here) |
| ESP32 REST support | `PUT`/`PATCH`/`POST`/`DELETE` on `.json` paths; multi-path `PATCH`; `print=silent`; `{".sv":"timestamp"}` ([save data](https://firebase.google.com/docs/database/rest/save-data)) [CONFIRMED-primary] | Verbose typed JSON (`{"doubleValue":…}`); `updateMask` patches |
| **Verdict** | **Use RTDB** | Only as an optional history store; not for live data |

### 2.2 RTDB tree (exact JSON)

Path conventions:
- Device id: `inv1`.
- Date keys use the **local Asia/Dhaka date** `YYYY-MM-DD`.
- Minute keys use `hhmm` (24 h).

All these keys are legal because they avoid `. $ # [ ] /` ([limits](https://firebase.google.com/docs/database/usage/limits)) [CONFIRMED-primary]. Node keys are indexed automatically, so `orderByKey()` range queries need no `.indexOn` ([indexing](https://firebase.google.com/docs/database/security/indexing-data)) [CONFIRMED-primary].

```
/devices/inv1/
    live                  (overwritten by PUT every 10 s; about 354 B)
    status                (PATCH every 60 s and on boot; about 150 B)
    hist/2026-10-21/1435  (1-minute aggregate, written once per minute; about 147 B)
    h15/2026-10-21/1430   (optional 15-minute aggregate for week/month charts)
    daily/2026-10-21      (energy totals, PATCHed every minute or 5 minutes; about 146 B)
    events/{pushId}       (alarms and transitions, for history in the dashboard)
    cfg                   (written by the owner from the dashboard; read by the device every 10 min)
```

`live` example (30 fields, 354 bytes compact) [INFERRED]. The field meanings are placeholders that map to the inverter protocol:

```json
{"ts":1792560000,"up":86400,"rssi":-61,"heap":182340,
 "gv":231.4,"gf":50.0,"ov":230.1,"of":50.0,"va":540,"w":512,"ld":16,"bus":381,
 "bv":26.42,"bci":12,"bdi":0,"soc":78,"tmp":38,"pvi":3.2,"pvv":145.3,"pvw":465,
 "sccv":26.44,"md":"L","st":"00010110","wf":"00000000000000000000000000000000",
 "eday":2.41,"epv":1.92,"egrid":0.33,"eload":2.08,"fw":"1.0.3","q":0}
```

Field key:
- `ts`: Unix seconds (UTC). `up`: uptime in seconds.
- `gv`/`gf`: grid voltage and frequency. `ov`/`of`: output voltage and frequency.
- `va`/`w`: apparent and active load power. `ld`: load %. `bus`: DC bus voltage.
- `bv`: battery voltage. `bci`/`bdi`: battery charge and discharge current. `soc`: state of charge %. `tmp`: heatsink temperature.
- `pvi`/`pvv`/`pvw`: PV current, voltage and power. `sccv`: SCC battery voltage.
- `md`: mode letter. `st`: status bits. `wf`: warning/fault bits.
- `e*`: today's kWh counters integrated on the device.
- `q`: count of records waiting in the outage queue.

`hist/2026-10-21/1435` (1-minute aggregate, 14 fields, about 140 B plus key):

```json
{"gv":231.2,"ov":230.0,"w":498,"va":530,"bv":26.40,"bvn":26.31,"bvx":26.48,"bi":11.6,"soc":78,"pvv":144.9,"pvw":461,"tmp":38,"md":"L","wf":0}
```

(`bvn`/`bvx` are the minute's min and max battery voltage. `bi` is signed battery current: positive for charging.)

`daily/2026-10-21` (energy integrated on the device and persisted to NVS every 5 min so it survives a reboot) [INFERRED]:

```json
{"epv":12.84,"eld":9.71,"egr":1.22,"ech":4.10,"edis":3.05,"pvmax":2410,"ldmax":1840,"bvmin":24.9,"bvmax":28.6,"gridout_min":42,"faults":0,"n":1440}
```

`status`:

```json
{"ts":1792560000,"fw":"1.0.3","ip":"<logger-ip>","rssi":-61,"heap":182340,"psram":8123456,"boot":1792473600,"rr":"POWERON","q":0,"proto":"modbus-9600"}
```

`events/-Nx...` (written by POST to push):

```json
{"ts":1792560123,"type":"GRID_LOSS","sev":2,"msg":"Grid lost, running on battery (SOC 78%)","notified":"ntfy"}
```

`cfg` (owner-editable):

```json
{"liveSec":10,"socLow":25,"bvLow":24.0,"tempHigh":70,"notifyRepeatMin":30,"keepHistDays":1095}
```

### 2.3 Request plan (per device)

| Every | Request | Body | Notes |
|---|---|---|---|
| 10 s | `PUT /devices/inv1/live.json?print=silent` | about 354 B | Over a kept-alive TLS connection |
| 60 s | One **multi-path `PATCH /devices/inv1.json?print=silent`** with keys `"hist/2026-10-21/1435"`, `"daily/2026-10-21"`, `"status"` | about 600 B | Multi-path PATCH updates several locations atomically ([save data](https://firebase.google.com/docs/database/rest/save-data)) [CONFIRMED-primary] |
| 15 min | Add `"h15/2026-10-21/1430"` to the minute PATCH | +150 B | Optional, for multi-day charts |
| 10 min | `GET /devices/inv1/cfg.json` | about 200 B down | Polling is cheaper in RAM than a second SSE stream client [INFERRED] |
| On event | `POST /devices/inv1/events.json` plus ntfy POST | about 120 B | §6 |
| Daily, 00:05 local | `DELETE /devices/inv1/hist/{today - keepHistDays}.json` | none | Pruning without Functions (§2.4) |

Writes per day: 8,640 live + 1,440 minute + 144 cfg reads + about 1 delete, about 10.2K requests/day. RTDB has no operation quota, only bandwidth [INFERRED].

Note on the device clock: `ts` comes from NTP (§3.5). Alternatively, the server timestamp `{".sv":"timestamp"}` returns milliseconds since the epoch and is the only supported server value ([save data](https://firebase.google.com/docs/database/rest/save-data)) [CONFIRMED-primary]. Keep the device time for history keys because back-filled records must carry their original time.

### 2.4 Retention and pruning with no Cloud Functions

- At about 83 MB/year, **pruning is optional for roughly 10 years** [INFERRED]. A simple policy:
  - keep `hist` (1-minute) for `cfg.keepHistDays` (default 1095 days);
  - keep `h15` and `daily` forever.
- The device prunes once a day just after local midnight with `DELETE .../hist/{date}`. Date-keyed nodes make this a single small request.
- If the device was offline for days, it can walk back over missing dates. `GET /devices/inv1/hist.json?shallow=true` returns only the keys and is cheap.
- `.validate` rules do not run on deletes (writing null), so the delete only needs `.write` permission ([rules conditions](https://firebase.google.com/docs/database/security/rules-conditions)) [CONFIRMED-primary].
- The dashboard can show storage: the Firebase console Usage tab, or a hidden "maintenance" button.

### 2.5 Firestore alternative (if you want it anyway): one document per hour

Layout: `devices/inv1/h/{YYYYMMDDHH}`, holding a map `m` keyed `t00`…`t59`, each a map of 14 numbers.
- Keys must not start with a digit: a Firestore "simple field name" cannot start with 0–9, otherwise it needs backtick quoting in field paths (Document reference) [CONFIRMED-primary].
- Write by REST `PATCH https://firestore.googleapis.com/v1/projects/{PROJECT}/databases/(default)/documents/devices/inv1/h/2026102114?updateMask.fieldPaths=m.t35` with `Authorization: Bearer <ID_TOKEN>` ([REST auth](https://firebase.google.com/docs/firestore/use-rest-api)) [CONFIRMED-primary].
- With an update mask, fields not covered by the mask are left untouched (Write) [CONFIRMED-primary].

Sizes, computed with the documented formulas [INFERRED from [storage size](https://firebase.google.com/docs/firestore/storage-size)]:
- Formulas: string = UTF-8 bytes + 1; number = 8; doc = name + fields + 32; index entry = doc name + parent + field path + value + 32.
- One hour document (60 × 14 numbers) is about **11.4 KB**.
- **Without an index exemption**, 1,804 index entries per hour doc (about 258 KB of index) give **about 6.5 MB/day, or 2.36 GB/year**. That **breaks the 1 GiB Spark storage within about 5 months**.
- **With a single-field index exemption on `m`** (subfields inherit the exemption ([index overview](https://firebase.google.com/docs/firestore/query-data/index-overview))), it is about **279 KB/day, or about 102 MB/year, about 10 years**.

Write budget [INFERRED]:
- Batch 5 minutes per PATCH: 288 history writes/day.
- Live doc every 30 s: 2,880 writes/day.
- Daily doc: 24 writes/day.
- Total about 3.2K/day against the 20K cap. That is OK, but live updates at 5–10 s would leave little margin.

Pitfall: do **not** use the `appendMissingElements` array transform for samples. It only appends elements **not already present**, so repeated values (for example two identical `bv` readings) are silently dropped (Write) [CONFIRMED-primary]. The FirebaseClient example `Firestore/Documents/Commit/AppendArray` uses exactly that transform ([repo](https://github.com/mobizt/FirebaseClient/tree/main/examples/FirestoreDatabase/Documents)) [CONFIRMED-primary].

Firestore listener billing:
- 1 read each time a watched document changes.
- Reconnecting after more than 30 min offline is billed as a new query.
- Minimum 1 read per query ([pricing](https://firebase.google.com/docs/firestore/pricing)) [CONFIRMED-primary].

---

## 3. ESP32-S3 client

### 3.1 Library and toolchain status (as of 2026-10-21)

| Component | Current | Status / notes | Source |
|---|---|---|---|
| Arduino-ESP32 core | **3.3.12** (2026-09-18) | Uses `NetworkClientSecure`; `WiFiClientSecure` is a typedef of it | [releases](https://github.com/espressif/arduino-esp32/releases), [WiFiClientSecure.h](https://github.com/espressif/arduino-esp32/blob/master/libraries/NetworkClientSecure/src/WiFiClientSecure.h) [CONFIRMED-primary] |
| PlatformIO platform | pioarduino `platform-espressif32` 55.03.312-1 (2026-09-22) | The FirebaseClient README says the stock PlatformIO espressif32 platform is outdated and to use pioarduino | [FirebaseClient README](https://github.com/mobizt/FirebaseClient) [CONFIRMED-primary] |
| **mobizt/FirebaseClient** | **v2.2.13** (2026-07-13), MIT, not archived | Supports RTDB, Firestore, FCM, Storage, Functions, Rules; UserAuth, CustomAuth, ServiceAuth, ID token, access token, legacy token; `ENABLE_PSRAM`; OTA from RTDB. Recent fixes include #330 (reused-connection HTTP 400) in 2.2.13. | [releases API](https://api.github.com/repos/mobizt/FirebaseClient/releases), [library.properties](https://raw.githubusercontent.com/mobizt/FirebaseClient/main/library.properties) [CONFIRMED-primary] |
| mobizt/Firebase-ESP-Client, Firebase-ESP32 | Description begins "[DEPRECATED]" | Do not use | GitHub repo metadata [CONFIRMED-primary] |
| ArduinoJson | v7.4.3 (2026-03-02) | For the REST path and local API | GitHub [CONFIRMED-primary] |

FirebaseClient facts worth knowing:
- It is REST-based. The README notes that Firestore change listening and RTDB disconnect events are not supported by the REST API. There is no `onDisconnect`, so the dashboard must infer "offline" from `live.ts` age [CONFIRMED-primary, README].
- Since v2.1.0 it is "network independent": you pass an SSL client such as `WiFiClientSecure` into `AsyncClientClass` [CONFIRMED-primary, README].
- **Bandwidth detail found in source.** In `src/database/RealtimeDatabase.h`, `storeAsync()` sets `options.silent = true` (which appends `print=silent`) **only for the await/sync form** (no callback, no AsyncResult, no ETag). Async `set`/`update` calls get the echoed body back, which is billed download. Prefer the await form for periodic writes, or accept about 3 MB/day extra at 10 s [CONFIRMED-primary, source read].
- The `UserAuth` constructor takes an optional token expire period in seconds (< 3600). The Update example uses 3000 [CONFIRMED-primary, [Update.ino](https://github.com/mobizt/FirebaseClient/blob/main/examples/RealtimeDatabase/Update/Update.ino)].
- The examples call `setInsecure()` and, "for ESP32 core sdk v3.x", `setConnectionTimeout(1000)` / `setHandshakeTimeout(5)` ([BareMinimum/RealtimeDatabase.ino](https://github.com/mobizt/FirebaseClient/blob/main/examples/BareMinimum/RealtimeDatabase/RealtimeDatabase.ino)) [CONFIRMED-primary]. Use `setCACert()` instead of `setInsecure()` in production (§3.4).
- An RTDB SSE stream would need its own SSL client, according to the comments in the bare-minimum example [CONFIRMED-primary]. That is about +22–42 KB of TLS RAM, so poll `cfg` instead [INFERRED].

### 3.2 Minimal FirebaseClient logger (API calls verified against the repo examples)

This is my condensed sketch. Every call used appears in `examples/RealtimeDatabase/Update/Update.ino`, `Timestamp/Timestamp.ino`, `Push/Push.ino` and `BareMinimum/RealtimeDatabase/RealtimeDatabase.ino`, and `object_t(const String&)` is an explicit constructor in `src/core/AsyncResult/Value.h` [CONFIRMED-primary for API names; the sketch as a whole is INFERRED and not compiled].

```cpp
// Board: "ESP32S3 Dev Module", Flash Size 16MB, PSRAM "OPI PSRAM" (N16R8 = octal PSRAM),
// Partition: 16 MB with OTA (see §8.6). Libraries: FirebaseClient 2.2.13 (+ its dep ESP_SSLClient).
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
// #define ENABLE_PSRAM            // optional: let the library allocate its buffers in PSRAM (README "Memory Options for ESP32")
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include "roots.h"                 // const char ROOT_CAS[] = GTS Root R1 + GTS Root R4 PEMs (§3.4)

#define WIFI_SSID     "your-2.4GHz-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define API_KEY       "AIza...your-web-api-key"
#define DEVICE_EMAIL  "inv1@example.com"      // a Firebase Auth user created only for the device
#define DEVICE_PASS   "long-random-device-password"
#define DATABASE_URL  "https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DEV           "/devices/inv1"

WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
UserAuth user_auth(API_KEY, DEVICE_EMAIL, DEVICE_PASS, 3000 /* token period, s (<3600) */);
FirebaseApp app;
RealtimeDatabase Database;

void authCb(AsyncResult &r) {
  if (r.isError()) Firebase.printf("auth error: %s (%d)\n", r.error().message().c_str(), r.error().code());
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);                 // replace with WiFiManager in production (§8.1)
  while (WiFi.status() != WL_CONNECTED) delay(300);
  configTzTime("<+06>-6", "pool.ntp.org", "time.google.com");   // Asia/Dhaka (§3.5)

  ssl_client.setCACert(ROOT_CAS);                       // examples use setInsecure(); don't ship that
  ssl_client.setConnectionTimeout(1000);                // as in the repo example for core v3.x
  ssl_client.setHandshakeTimeout(5);

  initializeApp(aClient, app, getAuth(user_auth), authCb, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

uint32_t lastLive = 0, lastMin = 0;
void loop() {
  app.loop();                                           // maintains/refreshes the ID token
  if (!app.ready()) return;

  if (millis() - lastLive >= 10000) {
    lastLive = millis();
    String live = "{\"ts\":" + String((uint32_t)time(nullptr)) + ",\"bv\":26.42,\"soc\":78,\"pvw\":465,\"w\":512}";
    // Await form => library appends print=silent (RealtimeDatabase.h storeAsync)
    if (!Database.set<object_t>(aClient, DEV "/live", object_t(live)))
      Firebase.printf("live: %s (%d)\n", aClient.lastError().message().c_str(), aClient.lastError().code());
  }

  if (millis() - lastMin >= 60000) {
    lastMin = millis();
    // One multi-path PATCH at the device root updates history + daily + status together
    String patch = "{\"hist/2026-10-21/1435\":{\"w\":498,\"bv\":26.40,\"soc\":78,\"pvw\":461},"
                   "\"daily/2026-10-21\":{\"epv\":12.84,\"eld\":9.71},"
                   "\"status/ts\":" + String((uint32_t)time(nullptr)) + "}";
    if (!Database.update(aClient, DEV, object_t(patch)))
      Firebase.printf("minute: %s (%d)\n", aClient.lastError().message().c_str(), aClient.lastError().code());
  }
}
```

The await calls block the calling task, often for hundreds of ms and for seconds when a handshake is needed. Run all FirebaseClient calls in **one dedicated "uplink" FreeRTOS task**, separate from the RS485 polling task, connected by a queue (§3.8) [INFERRED].

### 3.3 Plain HTTPS REST alternative (no Firebase library)

Endpoints (all [CONFIRMED-primary], Auth REST, [RTDB REST auth](https://firebase.google.com/docs/database/rest/auth)):

| Step | Request | Response fields |
|---|---|---|
| Sign in | `POST https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=[API_KEY]`, `Content-Type: application/json`, body `{"email":"…","password":"…","returnSecureToken":true}` | `idToken`, `refreshToken`, `expiresIn` ("3600"), `localId` (uid) |
| Refresh | `POST https://securetoken.googleapis.com/v1/token?key=[API_KEY]`, `Content-Type: application/x-www-form-urlencoded`, body `grant_type=refresh_token&refresh_token=[REFRESH_TOKEN]` | `id_token`, `refresh_token`, `expires_in`, `user_id`, `project_id` |
| Write | `PUT https://<db>.asia-southeast1.firebasedatabase.app/devices/inv1/live.json?auth=<ID_TOKEN>&print=silent` | 204 No Content with `print=silent` ([save data](https://firebase.google.com/docs/database/rest/save-data)) |

```cpp
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "roots.h"

String idToken, refreshTok; uint32_t tokenDeadline = 0;   // millis()
NetworkClientSecure dbTls;  HTTPClient dbHttp;             // kept alive for RTDB

static bool parseTokens(const String& body, const char* idKey, const char* rtKey, const char* expKey) {
  JsonDocument d; if (deserializeJson(d, body)) return false;
  idToken = d[idKey].as<String>(); refreshTok = d[rtKey].as<String>();
  long exp = String(d[expKey].as<const char*>()).toInt();         // e.g. "3600"
  tokenDeadline = millis() + (uint32_t)(exp - 300) * 1000UL;        // refresh 5 min early
  return idToken.length() > 0;
}

bool signIn() {
  NetworkClientSecure c; c.setCACert(ROOT_CAS); HTTPClient h;
  h.begin(c, String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + API_KEY);
  h.addHeader("Content-Type", "application/json");
  JsonDocument q; q["email"] = DEVICE_EMAIL; q["password"] = DEVICE_PASS; q["returnSecureToken"] = true;
  String b; serializeJson(q, b);
  int code = h.POST(b); bool ok = (code == 200) && parseTokens(h.getString(), "idToken", "refreshToken", "expiresIn");
  h.end(); return ok;
}

bool refreshToken() {
  NetworkClientSecure c; c.setCACert(ROOT_CAS); HTTPClient h;
  h.begin(c, String("https://securetoken.googleapis.com/v1/token?key=") + API_KEY);
  h.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = h.POST("grant_type=refresh_token&refresh_token=" + refreshTok);
  bool ok = (code == 200) && parseTokens(h.getString(), "id_token", "refresh_token", "expires_in");
  h.end(); return ok;
}

bool ensureToken() {
  if (idToken.length() && (int32_t)(tokenDeadline - millis()) > 0) return true;
  return (refreshTok.length() && refreshToken()) || signIn();
}

// method: "PUT" | "PATCH" | "POST" | "DELETE"
int rtdb(const char* method, const String& path, const String& json) {
  if (!ensureToken()) return -1;
  dbHttp.setReuse(true);                                   // HTTP keep-alive (HTTPClient.h)
  dbHttp.begin(dbTls, String(DATABASE_URL) + path + ".json?auth=" + idToken + "&print=silent");
  dbHttp.addHeader("Content-Type", "application/json");
  int code = dbHttp.sendRequest(method, json);
  if (code == 401) { idToken = ""; }                       // force refresh next time
  dbHttp.end();                                            // keeps the socket if the server allows reuse
  return code;                                             // 200 or 204 on success
}
// setup(): dbTls.setCACert(ROOT_CAS);
```

`setReuse`, `sendRequest`, `PUT`/`PATCH`/`POST` and `begin(NetworkClient&, String url)` exist in [HTTPClient.h](https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPClient/src/HTTPClient.h) [CONFIRMED-primary]. That `end()` with reuse keeps the TLS socket open is my reading [INFERRED]. Persist `refreshTok` in NVS (`Preferences`) so a reboot does not need a full sign-in [INFERRED].

Do not print URLs to Serial, because the ID token is in the query string.

**Which to choose [INFERRED]:** FirebaseClient saves writing token code and is well maintained. Plain REST gives full control over keep-alive, back-off and memory, and has no dependency risk. Both are fine on an S3. Start with FirebaseClient and keep plain REST as plan B.

### 3.4 TLS trust anchors (measured 2026-10-21 with `openssl s_client`) [CONFIRMED-primary]

| Host | Chain (leaf → root) | Root to embed |
|---|---|---|
| `*.asia-southeast1.firebasedatabase.app`, `*.firebaseio.com` (served as `*.us-central1.firebasedatabase.app`) | WR1 → **GTS Root R1** (cross-signed by GlobalSign Root CA) | GTS Root R1 |
| `identitytoolkit.googleapis.com`, `securetoken.googleapis.com`, `fcm.googleapis.com`, `www.gstatic.com` | WE2 → **GTS Root R4** (cross-signed by GlobalSign Root CA) | GTS Root R4 |
| `ntfy.sh` | YR1 → Root YR → **ISRG Root X1** | ISRG Root X1. Let's Encrypt says chains ending at Root YR/YE are not yet in major trust stores, and that X1 is trusted until 2030-06-04 ([LE certificates](https://letsencrypt.org/certificates/)) |
| `api.telegram.org` | Go Daddy Secure CA G2 → **Go Daddy Root CA G2** | Go Daddy Root Certificate Authority - G2 |

- Google publishes its recommended trust bundle at **https://pki.goog/roots.pem**, which has 21 roots including GTS Root R1–R4 and Go Daddy G2 [CONFIRMED-primary].
- PEM sizes: GTS R1 1,910 B; R2 1,910 B; R3 764 B; R4 764 B; Go Daddy G2 1,366 B.
- Embed **GTS Root R1 + R2 + R3 + R4** as one concatenated PEM string (about 5.3 KB) for Google endpoints, to survive future Google rotations [INFERRED].
- Use a separate PEM for ntfy (ISRG Root X1, plus ISRG Root X2 as a spare) and Telegram (Go Daddy G2).
- `NetworkClientSecure` also offers `setCACertBundle(const uint8_t*, size_t)` for a Mozilla bundle, but the Arduino README says embedding it under the Arduino IDE needs a makefile or loading about 64 KB from SPIFFS ([README](https://github.com/espressif/arduino-esp32/blob/master/libraries/NetworkClientSecure/README.md)) [CONFIRMED-primary]. Explicit PEMs are simpler.

### 3.5 NTP time sync

- Arduino-ESP32 gives `configTime(gmtOffset, dstOffset, server1, server2)` or `configTzTime(TZ, server1, server2)`, plus an optional `sntp_set_time_sync_notification_cb` ([SimpleTime.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino)) [CONFIRMED-primary].
- POSIX TZ for Dhaka is **`<+06>-6`** (no DST) ([esp8266 TZ.h](https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h)) [CONFIRMED-primary].
- The Arduino build re-syncs SNTP every **3 h** (`CONFIG_LWIP_SNTP_UPDATE_DELAY=10800000`), with up to 3 servers ([lib-builder defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common)) [CONFIRMED-primary].
- No RTC module may be bought. After a **power loss with no Wi-Fi**, the device has no wall-clock time. In that case, queue records with `ts=0` plus `uptime_ms` and a `bootId`, and fix them on the first NTP sync as `ts = syncTime - (syncUptime - uptime)`. If the inverter protocol exposes a clock, use it [INFERRED].
- Use UTC seconds in `ts`, and Asia/Dhaka local time only for the date and `hhmm` keys [INFERRED].

### 3.6 TLS RAM on the ESP32-S3

- ESP-IDF measures heap per TLS session for the `https_request` example with server validation CONFIRMED-primary, [ESP-IDF mbedTLS]:
  - default: about 42.2 KB;
  - without keeping the peer certificate: 38.5 KB;
  - with dynamic TX/RX buffers (`CONFIG_MBEDTLS_DYNAMIC_BUFFER` plus the free-config/CA options): **about 22.0 KB**.
- The **Arduino-ESP32 prebuilt libraries already enable** `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, `CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT=y` and `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y`, and set `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` ([defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common)) [CONFIRMED-primary]. The S3 config also sets `CONFIG_SPIRAM=y` and `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` ([defconfig.esp32s3](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.esp32s3)) [CONFIRMED-primary].
- Budget about 25–45 KB per concurrent TLS connection. With RTDB kept alive plus a one-shot ntfy or Telegram call, that is under 100 KB [INFERRED]. The S3 has 512 KB internal SRAM, plus 8 MB PSRAM on N16R8 [SECONDARY: Espressif ESP32-S3 product page https://www.espressif.com/en/products/socs/esp32-s3].
- Allocations over 4 KB may land in PSRAM when PSRAM is enabled [INFERRED from the ALWAYSINTERNAL=4096 setting].
- Enable **"OPI PSRAM"** in the board menu (`esp32s3.menu.PSRAM.opi` in [boards.txt](https://github.com/espressif/arduino-esp32/blob/master/boards.txt)) [CONFIRMED-primary]. N16R8 boards use octal PSRAM [SECONDARY, seller listing].
- Monitor `ESP.getFreeHeap()`, `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` and `ESP.getFreePsram()` in `status` [INFERRED].

### 3.7 Offline buffering and back-fill (power cuts take down the router)

During load-shedding, the router may go down even while the inverter keeps the ESP32 powered. Plan for hours to days of outage [INFERRED].

Layered buffer [INFERRED design]:
1. **RAM/PSRAM ring of raw samples** for the local web UI: `ps_malloc` exists in `esp32-hal-psram.h` [CONFIRMED-primary]. For example, 24 h at 10 s is 8,640 × 64 B = 553 KB of PSRAM. This is lost on reboot, which is acceptable because it is only for local charts.
2. **LittleFS queue of 1-minute aggregates** (the cloud-history records). Write a packed binary record of about 64 B per minute: 92 KB/day.
   - `default_16MB.csv` gives a 3.4 MB data partition (`spiffs` subtype at 0xC90000, size 0x360000): about **37 days** of outage buffer.
   - `app3M_fat9M_16MB.csv` gives 9.9 MB (FAT): about 100 days ([partitions](https://github.com/espressif/arduino-esp32/tree/master/tools/partitions)) [CONFIRMED-primary for the partition sizes].
   - `LittleFS.begin(formatOnFail, "/littlefs", 10, "spiffs")`: the default partition label is `spiffs` ([LittleFS.h](https://github.com/espressif/arduino-esp32/blob/master/libraries/LittleFS/src/LittleFS.h)) [CONFIRMED-primary].
3. **NVS (Preferences)** for the daily energy counters, the last uploaded minute and the refresh token. Write every 5 min to limit flash wear [INFERRED].

Queue format: one file per local date `/q/2026-10-21.bin`, plus a cursor file `/q/2026-10-21.pos` (byte offset already uploaded).

Back-fill algorithm [INFERRED]:
```
on every minute: append record to today's queue file (always, online or not)
uplink task, when online && token valid:
   1) live PUT has priority (every 10 s)
   2) if queue not empty and (last backfill > 2 s ago):
        read up to 60 records from oldest file at cursor
        build ONE PATCH body  {"<date>/<hhmm>":{...}, ...}  against /devices/inv1/hist
        if HTTP 200/204 -> advance cursor (write .pos), delete file when fully sent
        else exponential back-off (2 s, 4 s, ... max 5 min)
   3) recompute daily/<date> for back-filled days from the queue and PATCH it
```

Why this works:
- Keys are deterministic (`date/hhmm`), so retries are **idempotent**: the same key is overwritten and never duplicated.
- One PATCH of 60 records is about 9 KB, well under the 256 MB REST write limit and the 64 MB/min write limit ([limits](https://firebase.google.com/docs/database/usage/limits)) [CONFIRMED-primary].
- A 2-day outage back-fills in about 48 requests [INFERRED].
- Flash wear is low: about 1,440 small appends per day on a wear-levelled LittleFS [INFERRED].

### 3.8 Task layout

| Task | Core | Period | Job |
|---|---|---|---|
| `rs485Task` | 1 | 2–5 s | Poll the inverter (protocol from other research); fill the `Sample` struct; push to queue `qSamples` |
| `aggTask` | 1 | 1 s / 60 s | Integrate energy (W × dt); build the minute aggregate; append to LittleFS; evaluate alert rules (§6) |
| `uplinkTask` | 0 | 10 s | **The only task that touches TLS/Firebase**: live PUT, minute PATCH, back-fill, cfg poll, ntfy/Telegram |
| `loop()` / web | any | n/a | WiFiManager, local web server, OTA handle, watchdog feed |

[INFERRED] Keeping all TLS clients inside one task avoids cross-task use of the non-thread-safe client objects. The default Arduino `loopTask` stack is 8192 bytes (`ARDUINO_LOOP_STACK_SIZE`, [main.cpp](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/main.cpp)) [CONFIRMED-primary]. Give `uplinkTask` 12–16 KB of stack for TLS plus JSON [INFERRED].

---

## 4. RTDB security rules

Principles:
- `.read`/`.write` cascade, and shallower rules cannot be revoked deeper.
- `.validate` does **not** cascade and is skipped for deletes (null).
- `$other`-style wildcards match only keys not explicitly listed.
- `$` variables are strings.
- `now` is server milliseconds.
- `matches(/regex/)`, `isNumber()`, `isString()` and `hasChildren([...])` are available.

Sources: [core syntax](https://firebase.google.com/docs/database/security/core-syntax), [rules conditions](https://firebase.google.com/docs/database/security/rules-conditions), rules reference [CONFIRMED-primary].

Setup:
1. Create two Auth users in the console: **owner** (you) and **device** (`inv1@…`).
2. Paste their UIDs below. Hard-coding UIDs is the simplest and safest approach for a single-owner project [INFERRED].
3. Deploy with `firebase deploy --only database` or the console Rules tab.

`database.rules.json`:

```json
{
  "rules": {
    ".read": false,
    ".write": false,
    "devices": {
      "inv1": {
        ".read": "auth != null && (auth.uid === 'OWNER_UID' || auth.uid === 'DEVICE_UID')",

        "live": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          ".validate": "newData.hasChildren(['ts', 'bv', 'soc', 'w', 'pvw'])",
          "ts":  { ".validate": "newData.isNumber() && newData.val() > 1700000000 && newData.val() < now / 1000 + 600" },
          "bv":  { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 40" },
          "soc": { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 100" },
          "md":  { ".validate": "newData.isString() && newData.val().length <= 2" },
          "st":  { ".validate": "newData.isString() && newData.val().length <= 40" },
          "wf":  { ".validate": "newData.isString() && newData.val().length <= 64" },
          "fw":  { ".validate": "newData.isString() && newData.val().length <= 16" },
          "$num": { ".validate": "newData.isNumber() && $num.length <= 6" }
        },

        "status": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          "$k": { ".validate": "(newData.isNumber() || newData.isString()) && $k.length <= 8 && (!newData.isString() || newData.val().length <= 40)" }
        },

        "hist": {
          ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'OWNER_UID')",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$hhmm": {
              ".validate": "$hhmm.matches(/^([01][0-9]|2[0-3])[0-5][0-9]$/) && newData.hasChildren(['w', 'bv'])",
              "md": { ".validate": "newData.isString() && newData.val().length <= 2" },
              "$f": { ".validate": "newData.isNumber() && $f.length <= 4" }
            }
          }
        },

        "h15": {
          ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'OWNER_UID')",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$hhmm": {
              ".validate": "$hhmm.matches(/^([01][0-9]|2[0-3])(00|15|30|45)$/)",
              "md": { ".validate": "newData.isString() && newData.val().length <= 2" },
              "$f": { ".validate": "newData.isNumber() && $f.length <= 4" }
            }
          }
        },

        "daily": {
          ".write": "auth != null && auth.uid === 'DEVICE_UID'",
          "$date": {
            ".validate": "$date.matches(/^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/)",
            "$f": { ".validate": "newData.isNumber() && $f.length <= 12" }
          }
        },

        "events": {
          ".write": "auth != null && (auth.uid === 'DEVICE_UID' || auth.uid === 'OWNER_UID')",
          "$eid": {
            ".validate": "newData.hasChildren(['ts', 'type', 'msg'])",
            "ts":   { ".validate": "newData.isNumber()" },
            "type": { ".validate": "newData.isString() && newData.val().length <= 24" },
            "msg":  { ".validate": "newData.isString() && newData.val().length <= 200" },
            "sev":  { ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 5" },
            "notified": { ".validate": "newData.isString() && newData.val().length <= 16" },
            "$other": { ".validate": false }
          }
        },

        "cfg": {
          ".write": "auth != null && auth.uid === 'OWNER_UID'",
          "liveSec":  { ".validate": "newData.isNumber() && newData.val() >= 5 && newData.val() <= 300" },
          "socLow":   { ".validate": "newData.isNumber() && newData.val() >= 5 && newData.val() <= 80" },
          "bvLow":    { ".validate": "newData.isNumber() && newData.val() >= 20 && newData.val() <= 30" },
          "tempHigh": { ".validate": "newData.isNumber() && newData.val() >= 40 && newData.val() <= 100" },
          "notifyRepeatMin": { ".validate": "newData.isNumber() && newData.val() >= 5 && newData.val() <= 1440" },
          "keepHistDays":    { ".validate": "newData.isNumber() && newData.val() >= 30 && newData.val() <= 3650" },
          "$other": { ".validate": false }
        }
      }
    }
  }
}
```

Notes [INFERRED unless tagged]:
- Each multi-path PATCH key is checked against its own path's rules, so the per-minute PATCH at `/devices/inv1` needs `.write` on `hist`, `daily` and `status`. All are granted to `DEVICE_UID`.
- The **Web API key is not a secret**. Security comes from Auth plus these rules. If the device credentials leak, the attacker can write only `live`, `status`, `hist`, `h15`, `daily` and `events`, and cannot read other paths or write `cfg`.
- To stop strangers creating accounts with your API key, check whether the console's Authentication settings let you disable sign-up (an Identity Platform user-actions setting). Either way, the rules above make extra accounts useless. That console setting is unverified.
- Denied requests still count toward billed bandwidth ([billing](https://firebase.google.com/docs/database/usage/billing)) [CONFIRMED-primary]. That is not a concern at this scale.

---

## 5. Web dashboard for the iPhone

### 5.1 Hosting options (free)

| Host | Free limits | Fit |
|---|---|---|
| **Firebase Hosting** (`<project>.web.app` / `.firebaseapp.com`) | 10 GB storage; 360 MB/day (pricing page) or 10 GB/month (Hosting doc); disabled until next month if exceeded on non-Blaze; custom domain plus SSL ([pricing](https://firebase.google.com/pricing), [hosting quotas](https://firebase.google.com/docs/hosting/usage-quotas-pricing)) [CONFIRMED-primary] | **Recommended.** Same project. If Google sign-in is ever added, an app on `firebaseapp.com` is unaffected by the third-party-storage redirect issue ([redirect best practices](https://firebase.google.com/docs/auth/web/redirect-best-practices)) [CONFIRMED-primary]. The page is only about 50 KB from Hosting because the SDKs come from gstatic and the charts from jsDelivr. |
| GitHub Pages | Site at most 1 GB; soft limit 100 GB/month bandwidth; soft limit 10 builds/hour ([GH Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits)) [CONFIRMED-primary] | Fine with email/password auth. Not a good fit for Google sign-in by redirect (options 2–5 only). |
| Cloudflare Pages | 500 builds/month; 20,000 files; 25 MiB per file; 100 custom domains per project ([Pages limits](https://developers.cloudflare.com/pages/platform/limits/)). Cloudflare states static-asset requests are free and unlimited on Workers static assets ([Workers static assets](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/)) [CONFIRMED-primary] | Good if you later move to Workers + D1 (§7) |

### 5.2 Firebase JS SDK

- Current major is **13.0.0** (npm `latest`, published 2026-10-07). The 13.0.0 breaking changes are AI Logic only. The last 12.x is 12.19.0 (2026-09-09) ([npm registry](https://registry.npmjs.org/firebase), [release notes](https://firebase.google.com/support/release-notes/js)) [CONFIRMED-primary].
- The official CDN example now imports from `https://www.gstatic.com/firebasejs/13.0.0/firebase-app.js` ([alt setup](https://firebase.google.com/docs/web/alt-setup)) [CONFIRMED-primary]. `firebase-database.js` and `firebase-auth.js` both return HTTP 200 on gstatic for 13.0.0 and 12.19.0 [CONFIRMED-primary, measured].
- Measured gzip sizes (13.0.0) [CONFIRMED-primary, measured]: app 23.7 KB, auth 41.6 KB, **database 49.0 KB**, firestore 186.5 KB. RTDB is also lighter on the phone than Firestore.
- RTDB modular API: `getDatabase`, `ref`, `onValue`, `get`, `query`, plus one order-by from `orderByKey`/`orderByChild`/`orderByValue` and the filters `startAt`/`startAfter`/`endAt`/`endBefore`/`limitToFirst`/`limitToLast`/`equalTo` ([lists of data](https://firebase.google.com/docs/database/web/lists-of-data)) [CONFIRMED-primary].

### 5.3 Auth on the web (iPhone)

| Method | iOS Home Screen web app behaviour | Verdict |
|---|---|---|
| **Email + password** (`signInWithEmailAndPassword`) | Runs entirely inside the web app; no redirects or popups [INFERRED] | **Use this.** One owner account; iOS Keychain autofill works with `autocomplete="username"`/`"current-password"`. |
| Email link (passwordless) | The link in Mail opens in Safari, not in the Home Screen app. Home Screen apps have storage separate from Safari, so the session ends up in the wrong context ([WWDC23 notes](https://wwdcnotes.com/notes/wwdc23/10120), [firebase-js-sdk #77](https://github.com/firebase/firebase-js-sdk/issues/77)) [SECONDARY] | Avoid |
| Google sign-in | Redirect flows are affected by third-party storage blocking (Safari 16.1+). Fixes: host on Firebase Hosting with a matching `authDomain`, use a popup, proxy, or self-host the helper ([redirect best practices](https://firebase.google.com/docs/auth/web/redirect-best-practices)) [CONFIRMED-primary]. Popups inside standalone web apps are fragile [SECONDARY, same issue thread]. | Possible on Firebase Hosting; not needed |

### 5.4 Charting library (mobile Safari)

| Library | Version (2026-10) | Min JS size (measured, jsDelivr) | gzip | Notes |
|---|---|---|---|---|
| **uPlot** | 1.6.32 (2025-03-14), MIT | 51 KB | **22 KB** (+0.8 KB CSS) | Canvas 2D, built for time series. README benchmark: 166,650 points in 25 ms cold start; streaming 3,600 points at 60 fps uses 10% CPU / 12.3 MB, versus Chart.js 40% / 77 MB and ECharts 70% / 85 MB. Supports IANA time zones, missing data, cursor sync and touch-zoom demos ([README](https://github.com/leeoniya/uPlot)) [CONFIRMED-primary for the README claims]. |
| Chart.js | 4.5.1 (2025-10-13), MIT | 209 KB | 71 KB | Easier API and nicer defaults; heavier for long series |
| Apache ECharts | 6.1.0 (2026-05-19), Apache-2.0 | 1,122 KB (`echarts.min.js`); 500 KB simple build | 368 KB; 169 KB | Very rich; overkill for this |

**Pick uPlot** for the 1-minute and 15-minute line charts (1,440 to 2,880 points). It uses Unix **seconds** for the x axis. Chart.js is acceptable if you prefer its look for daily bar charts [INFERRED].

### 5.5 PWA on iOS: requirements and limits

- **Install:** on iOS 16.4+, PWAs can be installed from the Share menu in Safari, Chrome, Edge, Firefox and Orion ([MDN](https://developer.mozilla.org/en-US/docs/Web/Progressive_web_apps/Guides/Making_PWAs_installable)) [CONFIRMED-primary].
  - On **iOS 26**, every site added to the Home Screen opens as a web app by default; an "Open as Web App" toggle lets the user opt out. A manifest is still honoured for icons and other members ([WebKit Safari 26.0](https://webkit.org/blog/17333/webkit-features-in-safari-26-0/), [WWDC25 post](https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/)) [SECONDARY: search summary of the WebKit posts].
- **Manifest:** `name`, `short_name`, `start_url`, `scope`, `display: "standalone"`, `theme_color`, `background_color`, and 192 and 512 icons. Also add `<link rel="apple-touch-icon" href="/icons/apple-touch-icon.png">` (180×180 PNG) for older iOS versions [INFERRED, standard practice].
- **Safe area:** use `<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">` and CSS `env(safe-area-inset-*)` padding for the notch and home indicator [INFERRED, standard practice]. Use 16 px inputs so iOS does not zoom on focus.
- **Storage:**
  - Home Screen web apps have the same quotas as Safari: up to 60% of disk per origin for a browser app.
  - `navigator.storage.persist()` is supported (Safari 17+), and WebKit grants it based on heuristics such as being a Home Screen app ([WebKit storage policy](https://webkit.org/blog/14403/updates-to-storage-policy/)) [CONFIRMED-primary].
  - Home Screen apps are not subject to Safari's 7-day script-writable storage deletion, because they keep their own day counter ([WebKit ITP 2020](https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/)) [CONFIRMED-primary].
- **Push and badges:** Web Push works only for Home Screen web apps on iOS/iPadOS 16.4+. The manifest `display` must be `standalone` or `fullscreen`, and the permission must be requested from a user gesture. Delivery goes through Apple's push service. The Badging API is supported ([WebKit 16.4 web push](https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/)) [CONFIRMED-primary]. A **server-side sender** is still needed (§6).
- **No background execution:** when the app is closed, nothing runs except push handling. Live data streams only while the app is open [INFERRED].

### 5.6 Minimal live view (Firebase Hosting `public/` folder)

`public/index.html` [INFERRED; uses only the APIs cited above; not run]:

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Solar Logger</title>
<link rel="manifest" href="/manifest.webmanifest">
<link rel="apple-touch-icon" href="/icons/apple-touch-icon.png">
<meta name="apple-mobile-web-app-title" content="Solar">
<meta name="theme-color" content="#0f172a">
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/uplot@1.6.32/dist/uPlot.min.css">
<style>
  :root { --bg:#0f172a; --fg:#e2e8f0; --card:#1e293b; --muted:#94a3b8; --bad:#f87171; }
  body { margin:0; background:var(--bg); color:var(--fg); font:15px/1.4 -apple-system, system-ui, sans-serif;
         padding: calc(env(safe-area-inset-top) + 12px) calc(env(safe-area-inset-right) + 16px)
                  calc(env(safe-area-inset-bottom) + 16px) calc(env(safe-area-inset-left) + 16px); }
  .grid { display:grid; grid-template-columns:repeat(2, minmax(0,1fr)); gap:10px; }
  .card { background:var(--card); border-radius:12px; padding:12px; }
  .v { font-size:24px; font-variant-numeric:tabular-nums; } .l { color:var(--muted); font-size:12px; }
  #stale { color:var(--bad); } input, button { font-size:16px; }
</style>
</head>
<body>
<form id="login" hidden>
  <input id="email" type="email" autocomplete="username" placeholder="Email" required>
  <input id="pass" type="password" autocomplete="current-password" placeholder="Password" required>
  <button>Sign in</button> <span id="err"></span>
</form>
<main id="app" hidden>
  <p><span id="age"></span> <span id="stale"></span></p>
  <div class="grid">
    <div class="card"><div class="l">PV</div><div class="v" id="pvw">–</div></div>
    <div class="card"><div class="l">Load</div><div class="v" id="w">–</div></div>
    <div class="card"><div class="l">Battery</div><div class="v" id="bat">–</div></div>
    <div class="card"><div class="l">Grid</div><div class="v" id="gv">–</div></div>
  </div>
  <div id="chart" class="card" style="margin-top:10px"></div>
  <button id="out">Sign out</button>
</main>
<script src="https://cdn.jsdelivr.net/npm/uplot@1.6.32/dist/uPlot.iife.min.js"></script>
<script type="module">
  import { initializeApp } from "https://www.gstatic.com/firebasejs/13.0.0/firebase-app.js";
  import { getAuth, onAuthStateChanged, signInWithEmailAndPassword, signOut } from "https://www.gstatic.com/firebasejs/13.0.0/firebase-auth.js";
  import { getDatabase, ref, onValue, get } from "https://www.gstatic.com/firebasejs/13.0.0/firebase-database.js";

  const app = initializeApp({                 // from Firebase console > Project settings > Your apps (Web)
    apiKey: "AIza...", authDomain: "<project>.firebaseapp.com",
    databaseURL: "https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app",
    projectId: "<project>", appId: "1:...:web:..."
  });
  const auth = getAuth(app), db = getDatabase(app), DEV = "devices/inv1";
  const $ = (id) => document.getElementById(id);
  const dhakaDate = (d = new Date()) => new Intl.DateTimeFormat("en-CA", { timeZone: "Asia/Dhaka" }).format(d); // YYYY-MM-DD
  let stopLive = null, lastTs = 0, plot = null;

  $("login").onsubmit = async (e) => { e.preventDefault();
    try { await signInWithEmailAndPassword(auth, $("email").value, $("pass").value); }
    catch (err) { $("err").textContent = err.code; } };
  $("out").onclick = () => signOut(auth);

  onAuthStateChanged(auth, (user) => {
    $("login").hidden = !!user; $("app").hidden = !user;
    if (stopLive) { stopLive(); stopLive = null; }
    if (!user) return;
    stopLive = onValue(ref(db, `${DEV}/live`), (snap) => {      // ~354 B per update
      const v = snap.val(); if (!v) return;
      lastTs = v.ts;
      $("pvw").textContent = `${v.pvw ?? "–"} W`;
      $("w").textContent = `${v.w ?? "–"} W`;
      $("bat").textContent = `${v.soc ?? "–"} % · ${typeof v.bv === "number" ? v.bv.toFixed(2) : "–"} V`;
      $("gv").textContent = v.gv > 100 ? `${v.gv} V` : "Grid off";
    });
    loadToday();
  });

  setInterval(() => {                           // no onDisconnect over REST: infer "offline" from age
    if (!lastTs) return;
    const age = Math.round(Date.now() / 1000 - lastTs);
    $("age").textContent = `updated ${age}s ago`;
    $("stale").textContent = age > 60 ? "· device offline?" : "";
  }, 1000);

  async function loadToday() {
    const day = dhakaDate(), [y, m, d] = day.split("-").map(Number);
    const rows = (await get(ref(db, `${DEV}/hist/${day}`))).val() || {};   // ≤1440 rows ≈ 0.2 MB
    const xs = [], pv = [], load = [], soc = [];
    for (const k of Object.keys(rows).sort()) {
      xs.push(Date.UTC(y, m - 1, d, +k.slice(0, 2), +k.slice(2)) / 1000 - 6 * 3600); // Dhaka = UTC+6, no DST
      pv.push(rows[k].pvw ?? null); load.push(rows[k].w ?? null); soc.push(rows[k].soc ?? null);
    }
    const ax = { stroke: "#94a3b8", grid: { stroke: "#334155" } };
    const opts = {
      width: $("chart").clientWidth - 24, height: 260,
      tzDate: (ts) => uPlot.tzDate(new Date(ts * 1e3), "Asia/Dhaka"),
      series: [{}, { label: "PV W", stroke: "#f59e0b" }, { label: "Load W", stroke: "#38bdf8" },
               { label: "SOC %", stroke: "#22c55e", scale: "%" }],
      axes: [ax, ax, { ...ax, scale: "%", side: 1, grid: { show: false } }]
    };
    plot?.destroy(); plot = new uPlot(opts, [xs, pv, load, soc], $("chart"));
  }
  if ("serviceWorker" in navigator) navigator.serviceWorker.register("/sw.js");
</script>
</body>
</html>
```

Range queries for longer charts, for example the last 30 days of totals: `query(ref(db, "devices/inv1/daily"), orderByKey(), startAt("2026-09-22"), endAt("2026-10-21"))`. This needs no index because keys are indexed automatically [CONFIRMED-primary, §2.2].

`public/manifest.webmanifest`:

```json
{
  "name": "Solar Inverter Logger",
  "short_name": "Solar",
  "start_url": "/",
  "scope": "/",
  "display": "standalone",
  "background_color": "#0f172a",
  "theme_color": "#0f172a",
  "icons": [
    { "src": "/icons/icon-192.png", "sizes": "192x192", "type": "image/png" },
    { "src": "/icons/icon-512.png", "sizes": "512x512", "type": "image/png" },
    { "src": "/icons/icon-maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable" }
  ]
}
```

`public/sw.js` (app-shell cache only; it never intercepts Firebase or Google traffic):

```js
const CACHE = "solar-v1";
const SHELL = ["/", "/index.html", "/manifest.webmanifest", "/icons/icon-192.png", "/icons/icon-512.png"];
self.addEventListener("install", (e) => e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL))));
self.addEventListener("activate", (e) => e.waitUntil(caches.keys().then((ks) =>
  Promise.all(ks.filter((k) => k !== CACHE).map((k) => caches.delete(k))))));
self.addEventListener("fetch", (e) => {
  const u = new URL(e.request.url);
  if (e.request.method !== "GET" || u.origin !== location.origin) return;
  e.respondWith(fetch(e.request).then((r) => { const c = r.clone(); caches.open(CACHE).then((x) => x.put(e.request, c)); return r; })
    .catch(() => caches.match(e.request)));
});
```

`firebase.json`:

```json
{
  "database": { "rules": "database.rules.json" },
  "hosting": {
    "public": "public",
    "ignore": ["firebase.json", "**/.*"],
    "headers": [
      { "source": "/sw.js", "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "**/*.@(png|webmanifest)", "headers": [{ "key": "Cache-Control", "value": "max-age=86400" }] }
    ]
  }
}
```

Deploy (from a laptop, one time): `npm i -g firebase-tools`, then `firebase login`, `firebase init hosting database`, `firebase deploy`. On the iPhone, open `https://<project>.web.app` in Safari, then Share > Add to Home Screen [INFERRED, standard CLI flow].

---

## 6. Alerts to the iPhone (faults, low battery, grid loss)

| Channel | iPhone side | Sender requirement | Free limits | ESP32 effort | Verdict |
|---|---|---|---|---|---|
| **Web Push to the PWA** | iOS 16.4+, Home Screen app, permission from a user gesture ([WebKit](https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/)) | A sender that signs VAPID and encrypts the payload, or FCM HTTP v1, which needs an **OAuth2 access token from a service account** with scope `firebase.messaging` ([FCM v1](https://firebase.google.com/docs/cloud-messaging/send/v1-api)) [CONFIRMED-primary] | FCM is no-cost | There is no Cloud Function on Spark to hold the credentials. FirebaseClient can do `ServiceAuth` plus Messaging on the device, but that puts an **admin-level service-account private key** on the ESP32 [INFERRED] | **Not recommended** (security risk, complex) |
| **ntfy.sh** | Free iOS app by Philipp Heckel, iOS 15+ ([App Store](https://apps.apple.com/us/app/ntfy/id1625396347)) [CONFIRMED-primary]. For ntfy.sh topics, instant iOS delivery goes through ntfy.sh's APNs relay ([config docs, upstream-base-url](https://docs.ntfy.sh/config/)) [CONFIRMED-primary] | None: a plain `POST https://ntfy.sh/<topic>` with the body as the message; headers `Title`, `Priority` (1–5), `Tags`, `Click`, `Markdown` ([publish](https://docs.ntfy.sh/publish/)) [CONFIRMED-primary] | ntfy.sh: **250 messages/day** per visitor; burst 60 requests then 1 per 5 s; message at most 4,096 B; 5 e-mails/day; 200 MB/day bandwidth. Topics are public: **the topic name is effectively the password**. Paid tiers are $5, $10 and $20 per month ([publish limits](https://docs.ntfy.sh/publish/), [pricing](https://ntfy.sh/#pricing)) [CONFIRMED-primary] | Trivial (one HTTPS POST, ISRG root) | **Primary** |
| **Telegram bot** | Telegram iOS app (free) | Bot token from @BotFather; `POST https://api.telegram.org/bot<token>/sendMessage` with `chat_id` and `text`; GET or POST with query, form or JSON parameters ([Bot API 10.3, 2026-08-24](https://core.telegram.org/bots/api)) [CONFIRMED-primary] | Free. Keep to at most 1 message/s per chat; about 30 messages/s for broadcasts ([Bot FAQ](https://core.telegram.org/bots/faq)) [CONFIRMED-primary] | Easy (Go Daddy G2 root). Get `chat_id` once via `getUpdates` [INFERRED] | **Backup / second channel** |
| Pushover | iOS app; 30-day trial, then **$4.99 one-time** per platform ([pricing](https://pushover.net/pricing)) | `POST https://api.pushover.net/1/messages.json` with `token`, `user`, `message` ([API](https://pushover.net/api)) [CONFIRMED-primary] | 10,000 messages/month per account; at most 2 concurrent connections; 429 when over quota | Easy | Good if paid is OK. Not zero-cost. |
| E-mail (SMTP) | Mail app | SMTP server plus an app password (for example Gmail) | Provider limits | Heavier (SMTP over TLS; library such as mobizt ESP-Mail-Client) [INFERRED] | Last resort |
| Home Assistant Companion | Free iOS app ([App Store](https://apps.apple.com/us/app/home-assistant/id1099568401)) [CONFIRMED-primary] | Needs an HA server running somewhere | n/a | n/a | Only if HA is adopted (§7) |

**Recommendation [INFERRED]:** send every alert to **ntfy.sh** using a long random topic, for example `savx-f4-<20 random chars>`, at priorities 4–5 for critical. Mirror critical ones to **Telegram**. Also write each event to `/devices/inv1/events` so the dashboard shows a history.

Alert rules (on the device, in `aggTask`) with hysteresis and rate limiting [INFERRED]. Thresholds are owner-tunable through `cfg`. The 24 V lithium numbers are starting points; confirm them against the battery and BMS settings.

| Event | Trigger | Clear | Priority |
|---|---|---|---|
| GRID_LOSS | `gv < 170 V` (or the protocol's "line fail" bit) for ≥ 10 s | `gv > 200 V` for 30 s, which sends a "Grid restored, outage 42 min" message | 4 |
| BATT_LOW | `soc ≤ cfg.socLow` (25%) or `bv ≤ cfg.bvLow` (24.0 V) for 60 s | `soc ≥ socLow + 5` | 5 |
| FAULT | Protocol fault/warning bits change to non-zero (`wf`) | Bits return to zero | 5 |
| OVER_TEMP | `tmp ≥ cfg.tempHigh` | `tmp ≤ tempHigh - 5` | 4 |
| OVERLOAD | `ld ≥ 100%` for 5 s | `ld < 90%` | 4 |
| DEVICE_SILENT | Seen from the phone: dashboard shows stale `live.ts`. Push is not possible without a server; an ntfy "heartbeat" topic is not needed. | n/a | n/a |
| LOGGER_REBOOT | Boot with `esp_reset_reason()` not POWERON | n/a | 2 |

Each alert type re-notifies at most every `cfg.notifyRepeatMin` (30 min). If the internet is down, queue up to 20 pending alerts in RAM and send them on reconnect with the original time in the text. This stays far below ntfy's 250/day.

```cpp
bool ntfy(const char* title, const String& msg, int prio, const char* tags) {
  NetworkClientSecure c; c.setCACert(ISRG_ROOTS);          // ISRG Root X1 (+X2)
  HTTPClient h;
  h.begin(c, "https://ntfy.sh/" NTFY_TOPIC);
  h.addHeader("Title", title);                              // header names per docs.ntfy.sh/publish
  h.addHeader("Priority", String(prio));
  h.addHeader("Tags", tags);                                // e.g. "warning,zap"
  int code = h.POST(msg);
  h.end();
  return code == 200;
}

bool telegram(const String& text) {
  NetworkClientSecure c; c.setCACert(GODADDY_G2_ROOT);
  HTTPClient h;
  h.begin(c, String("https://api.telegram.org/bot") + TG_TOKEN + "/sendMessage");
  h.addHeader("Content-Type", "application/json");
  JsonDocument d; d["chat_id"] = TG_CHAT_ID; d["text"] = text;
  String b; serializeJson(d, b);
  int code = h.POST(b); h.end(); return code == 200;
}
```

---

## 7. Alternatives compared (free tiers as of 2026-10)

Criteria: ESP32 writes directly, no server, multi-year history, viewing on the iPhone, zero cost.

| Option | Free-tier limits (source) | ESP32 direct | Multi-year history free | iPhone viewing | Verdict |
|---|---|---|---|---|---|
| **Firebase RTDB (Spark)** | 1 GB stored; 10 GB/month download; 100 connections; no operation caps; shut off for the rest of the month if exceeded ([pricing](https://firebase.google.com/pricing), [FAQ](https://firebase.google.com/support/faq)) | Yes (REST or FirebaseClient) | **Yes**, about 11 years at 1-minute resolution (§1.2) | Own PWA | **Best fit** |
| Firestore (Spark) | 1 GiB; 50K reads, 20K writes, 20K deletes per day; 10 GiB/month ([quotas](https://firebase.google.com/docs/firestore/quotas)) | Yes | Yes, **only with an index exemption** (§2.5) | Own PWA | OK for history; poor for live |
| Supabase Free | 500 MB DB per project; 5 GB egress; 50K MAU; 1 GB files; 2 active projects; **paused after 1 week of low activity**, restorable up to 1 year; 1-day API logs ([pricing](https://supabase.com/pricing), [pausing](https://supabase.com/docs/guides/platform/free-project-pausing)) | Yes (PostgREST HTTPS plus RLS) | Yes for about 3–5 years at 1 minute (Postgres row overhead) [INFERRED]. The device's daily writes count as activity, but a week-long outage risks a pause [INFERRED]. | Own PWA (supabase-js) | Strong #2 if you want SQL |
| InfluxDB Cloud Serverless Free | **30-day retention**; writes 5 MB per 5 min; queries 300 MB per 5 min; 2 buckets; 2 notification rules ([pricing](https://www.influxdata.com/influxdb-cloud-pricing/)) | Yes (line protocol over HTTPS) | **No** | Web UI | No |
| Grafana Cloud Free | 10k active metric series, **14-day retention**; logs and traces 50 GB/month each, 14 days; 3 users ([pricing](https://grafana.com/pricing/)). Inactivity policy: not found [unverified]. | Yes via Prometheus remote-write or Influx-compatible ingest [INFERRED, not verified] | **No** | Grafana web | No for history; could be a live view |
| ThingSpeak Free | 3M messages/year; **15 s minimum interval**; **4 channels**; **8 fields/channel**; storage cap 10M messages ([license FAQ](https://thingspeak.mathworks.com/pages/license_faq), [channels](https://www.mathworks.com/help/thingspeak/channel-settings.html)); non-commercial only | Yes (HTTP GET/POST) | Yes (30 fields means 4 channels at about 1 per minute ≈ 2.1M messages/year) [INFERRED] | Web charts; third-party iOS viewers | Fallback; clunky |
| Blynk Free | 5 devices; 1 user; **100,000 messages/month**; **1-week retention**; no-code iOS/Android app builder ([pricing](https://blynk.io/pricing)) | Yes (Blynk library) | **No** | Native app (nice) | Live only |
| Cloudflare Workers + D1 Free | Workers 100K requests/day, 10 ms CPU, 5 cron triggers ([Workers limits](https://developers.cloudflare.com/workers/platform/limits/)); D1 10 DBs, 500 MB per DB, 5 GB total, 5M rows read/day, **100K rows written/day**, resets 00:00 UTC ([D1 limits](https://developers.cloudflare.com/d1/platform/limits/), [D1 pricing](https://developers.cloudflare.com/d1/platform/pricing/)); static assets free and unlimited | Yes, ESP32 → **your Worker** over HTTPS (you write about 100 lines of JS) | Yes (500 MB at about 100 B per 1-minute row is about 50 MB/year) [INFERRED] | Own PWA on Workers static assets | Best "free with server logic" option (cron aggregation, server-side alerts, even Web Push from a Worker) [INFERRED] |
| Adafruit IO Free | **30 data points/min**; 10 feeds; 5 dashboards; **30 days** of storage ([IO+ guide](https://learn.adafruit.com/welcome-to-adafruit-io/io-plus)) | Yes | **No** | Web dashboards | No (30 fields × 1/min hits the cap) |
| Home Assistant (self-hosted) | Free open source; **needs an always-on host** (Pi or PC). Modbus RTU (serial or USB-RS485), TCP and RTU-over-TCP integration ([modbus](https://www.home-assistant.io/integrations/modbus/)). Companion iOS app free ([App Store](https://apps.apple.com/us/app/home-assistant/id1099568401)). Remote access through Nabu Casa at $6.50/month or $65/year ([Nabu Casa](https://www.nabucasa.com/pricing/)) or a DIY tunnel. | ESP32 via ESPHome or MQTT [INFERRED] | Yes (local DB) [INFERRED] | Excellent native app and push | Best UX, but **needs hardware the user doesn't have**; out under the no-purchase rule |

**Best fit for "ESP32 direct writes, no server, multi-year history, iPhone, $0": Firebase RTDB on Spark** with a Firebase-Hosted PWA and ntfy alerts. Plan B, if more server logic is wanted later, is Cloudflare Workers + D1. Plan C, for SQL, is Supabase, minding the 7-day pausing rule [INFERRED].

---

## 8. Firmware niceties

### 8.1 Wi-Fi provisioning

| Option | Status | Notes |
|---|---|---|
| **tzapu/WiFiManager** | MIT; latest release v2.0.17 (2024-03-02); repo active (pushed 2026-02); README lists ESP32/S2/C3/S3 badges ([repo](https://github.com/tzapu/WiFiManager)) [CONFIRMED-primary] | Captive portal: the iPhone joins `Solar-Setup` and iOS pops the captive sheet [INFERRED]. `wm.setConfigPortalTimeout(180); if (!wm.autoConnect("Solar-Setup", "setup-pass")) ESP.restart();` Use custom parameters for the ntfy topic and device password. Uses the synchronous `WebServer`. |
| Espressif `WiFiProv` (BLE or SoftAP, in the core) | Example in arduino-esp32 ([WiFiProv.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFiProv/examples/WiFiProv/WiFiProv.ino)); uses Espressif's provisioning phone apps; app image over 1.4 MB [CONFIRMED-primary] | Works on the S3 over BLE; iOS app "ESP BLE Provisioning" [SECONDARY]. More code size; fine on 16 MB. |
| khoih-prog ESPAsync_WiFiManager | **Archived** (2023) [CONFIRMED-primary] | Avoid |

Mind the 2.4 GHz constraint: the ESP32-S3 is 2.4 GHz-only. Make sure the router's 2.4 GHz SSID is separate or band steering is off if joining fails [INFERRED].

### 8.2 OTA updates

| Method | Status | Use |
|---|---|---|
| **ArduinoOTA** (in the core) | `ArduinoOTA.setHostname()`, `setPassword()`, **`setPasswordHash()`** (example shows a SHA-256 hash), `begin()`, `handle()` ([BasicOTA.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/ArduinoOTA/examples/BasicOTA/BasicOTA.ino)) [CONFIRMED-primary] | Pushing from Arduino IDE/PlatformIO on the LAN during development |
| **ElegantOTA** (Lite) | **v4.0.0 (2026-09-05)**, GitHub licence **AGPL-3.0**; Pro is a paid commercial licence with direct-download and auto-update modes. v4 notes say MD5 verification previously did nothing because `Update.setMD5()` was called before `begin()`, and that this is now fixed. ESP32 incl. S3; async mode via `-D ELEGANTOTA_USE_ASYNC_WEBSERVER=1`, but you cannot mix the sync `WebServer` and `ESPAsyncWebServer` in the same firmware ([README](https://github.com/ayushsharma82/ElegantOTA)) [CONFIRMED-primary] | Browser upload at `http://solar.local/update` from Safari on the iPhone, picking the `.bin` from Files. AGPL is fine for a personal, unshared device [INFERRED]. Protect it with basic auth. |
| HTTP(S) pull OTA (`HTTPUpdate`, `HttpsOTAUpdate`) | In the core ([httpUpdateSecure.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/HTTPUpdate/examples/httpUpdateSecure/httpUpdateSecure.ino), [HTTPS_OTA_Update.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/Update/examples/HTTPS_OTA_Update/HTTPS_OTA_Update.ino)) [CONFIRMED-primary] | Remote updates from a URL (for example a GitHub release asset or Firebase Hosting; Hosting allows 2 GB files, but each OTA counts toward 360 MB/day). Trigger with an owner-set `cfg.otaUrl` [INFERRED]. Not Firebase Storage, which is Blaze-only. |
| FirebaseClient OTA from RTDB | Example `examples/RealtimeDatabase/OTA` exists [CONFIRMED-primary] | Possible on Spark (firmware stored as data in RTDB, so storage and download are counted); HTTPUpdate is simpler [INFERRED] |

### 8.3 Local web UI on the ESP32

- **ESP32Async/ESPAsyncWebServer v3.12.1** (2026-09-12) with **ESP32Async/AsyncTCP**; LGPL-3.0. Supports WebSocket, SSE, middleware (auth, CORS, rate limiting), static files and JSON. Works on Arduino core 2.x/3.x ([README](https://github.com/ESP32Async/ESPAsyncWebServer)) [CONFIRMED-primary].
- The old `me-no-dev/ESPAsyncWebServer` and `mathieucarbou/ESPAsyncWebServer` repos are **archived** (January 2025), with work continuing in the ESP32Async org [CONFIRMED-primary, GitHub metadata].
- **Simpler alternative:** the core's synchronous `WebServer` is adequate for a one-user LAN page that polls `/api/live` every 2 s. It also matches WiFiManager's server type and avoids the ElegantOTA sync/async conflict [INFERRED].
- Serve one gzipped `index.html` from LittleFS (with uPlot inlined or cached by Safari) and show the PSRAM ring of the last 24 h. It works with **no internet**, for example during an ISP outage, as long as the phone is on the same Wi-Fi [INFERRED].

```cpp
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>      // ESP32Async/ESPAsyncWebServer + ESP32Async/AsyncTCP
#include <ElegantOTA.h>             // build flag: -D ELEGANTOTA_USE_ASYNC_WEBSERVER=1
#include <LittleFS.h>
AsyncWebServer server(80);
extern String liveJson;             // updated by aggTask (guard with a mutex in real code)

void startLocalUi() {
  LittleFS.begin(true);
  MDNS.begin("solar");                                   // http://solar.local
  server.on("/api/live", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "application/json", liveJson); });
  server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");
  ElegantOTA.begin(&server, "admin", "change-me");       // /update with HTTP basic auth
  server.begin();
  MDNS.addService("http", "tcp", 80);
}
// in loop(): ElegantOTA.loop();
```

The `ElegantOTA.begin(&server, user, pass)` signature and `ElegantOTA.loop()` follow ElegantOTA v3/v4 usage [INFERRED; check `examples/AsyncDemo`]. mDNS `.local` names resolve natively on iOS through Bonjour [SECONDARY]. The mDNS example is at [mDNS_Web_Server.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS/examples/mDNS_Web_Server/mDNS_Web_Server.ino) [CONFIRMED-primary].

### 8.4 Watchdogs and self-healing

- **Task watchdog (TWDT):** `esp_task_wdt_config_t {timeout_ms, idle_core_mask, trigger_panic}`; `esp_task_wdt_init()` (only after the scheduler starts, `ESP_ERR_INVALID_STATE` if already initialised); `esp_task_wdt_reconfigure()`; `esp_task_wdt_add()`; `esp_task_wdt_reset()` (ESP-IDF WDT) [CONFIRMED-primary].
- Arduino builds set `CONFIG_ESP_TASK_WDT_PANIC=y` ([defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common)), and the core feeds the loop task only when `loopTaskWDTEnabled` is set ([main.cpp](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/main.cpp)) [CONFIRMED-primary].

```cpp
#include <esp_task_wdt.h>
void wdtSetup() {
  esp_task_wdt_config_t cfg = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
}
// in each long-running task: esp_task_wdt_add(NULL); ... esp_task_wdt_reset(); every loop
```

- Network watchdog [INFERRED]: if Wi-Fi is up but there has been no successful cloud write for 15 min, close the TLS client and re-sign-in. After 60 min, call `ESP.restart()`. Record `esp_reset_reason()` in `status.rr` (the core has a ResetReason example: [ResetReason.ino](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/ResetReason/ResetReason/ResetReason.ino)) [CONFIRMED-primary that the example exists].
- Call `WiFi.setAutoReconnect(true)`. Disable modem sleep (`WiFi.setSleep(false)`) for steadier latency on USB power [INFERRED].

### 8.5 PlatformIO skeleton [INFERRED; verify the pioarduino release asset URL on its releases page]

```ini
[env:solar]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi      ; N16R8: QIO flash + octal PSRAM
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv      ; 2 x 6.25 MB OTA apps + 3.4 MB LittleFS + coredump
board_build.filesystem = littlefs
monitor_speed = 115200
build_flags = -DBOARD_HAS_PSRAM -DELEGANTOTA_USE_ASYNC_WEBSERVER=1
lib_compat_mode = strict
lib_deps =
  mobizt/FirebaseClient@2.2.13
  bblanchon/ArduinoJson@7.4.3
  ESP32Async/ESPAsyncWebServer@3.12.1
  ESP32Async/AsyncTCP
  ayushsharma82/ElegantOTA@4.0.0
  tzapu/WiFiManager@2.0.17
```

### 8.6 Partition choice (16 MB flash) [CONFIRMED-primary for the tables]

| CSV | app0 / app1 (OTA) | Data FS | Notes |
|---|---|---|---|
| `default_16MB.csv` | 6.25 MB / 6.25 MB | `spiffs` 3.4 MB (use as LittleFS) | Plus a 64 KB coredump. Recommended: OTA-safe, about 37-day outage queue. |
| `app3M_fat9M_16MB.csv` (Arduino IDE menu "16M Flash (3MB APP/9.9MB FATFS)") | 3 MB / 3 MB | `ffat` 9.9 MB (FAT) | Longer queue (about 100 days) if you use FFat instead of LittleFS |

Source: [tools/partitions](https://github.com/espressif/arduino-esp32/tree/master/tools/partitions), [boards.txt](https://github.com/espressif/arduino-esp32/blob/master/boards.txt).

---

## 9. Setup checklist (cloud and web side)

1. Create a Firebase project on **Spark**. Do not upgrade to Blaze unless you want it; Spark cannot bill you.
2. Add a **Web app** and copy `firebaseConfig`.
3. **Build > Realtime Database > Create**:
   - location **Singapore (asia-southeast1)**;
   - start in locked mode;
   - the URL looks like `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app` ([locations](https://firebase.google.com/docs/database/locations)).
4. **Authentication > Email/Password: enable.** Add the users `owner` and `inv1@…` (device), and note both UIDs.
5. Paste the UIDs into `database.rules.json` (§4) and deploy the rules.
6. Hosting: `firebase init hosting`, put §5.6 files in `public/`, then `firebase deploy`. Add to the iPhone Home Screen.
7. Firmware: put the Web API key, the device email/password and the DB URL in NVS through WiFiManager custom fields (not hard-coded). Embed the GTS R1–R4, ISRG X1/X2 and Go Daddy G2 PEMs.
8. Install the **ntfy** iOS app, subscribe to your random topic, and send a test from the ESP32 with `curl -d "test" ntfy.sh/<topic>` first.
9. Optional: create a Telegram bot with @BotFather, message it once, read `chat_id` via `getUpdates`, and store the token and chat id in NVS.
10. Watch **Realtime Database > Usage** for the first week. Expect about 3–10 MB/day download and about 0.25 MB/day storage growth [INFERRED].

---

## 10. Risks, unknowns, open questions

- **Hosting quota discrepancy:** the pricing page says 360 MB/day and the Hosting doc says 10 GB/month. Not material for one user [CONFIRMED-primary, both pages].
- **Spark hard shutoff** applies if any quota is exceeded. Reconnect storms and raw-sample history are the two realistic causes; add back-off and never store raw 5 s data in the cloud [INFERRED].
- **Time without RTC:** after a power cut with the router down, there is no wall-clock time until NTP succeeds. The firmware must use the uptime-relative scheme in §3.5 [INFERRED].
- **FirebaseClient await-mode blocking:** keep it in a dedicated uplink task. Async mode loses `print=silent`, which costs bandwidth but is harmless at this scale [CONFIRMED-primary, source].
- **iOS 26 web-app default** comes from a search summary of WebKit posts and was not read first-hand [SECONDARY].
- **Disabling public sign-up** in Firebase Auth: the setting's location was not verified; the rules make it moot [INFERRED].
- **ElegantOTA API details** (`begin(&server, user, pass)`): confirm against `examples/AsyncDemo/AsyncDemo.ino` for v4.0.0 [INFERRED].
- **pioarduino asset URL and the `qio_opi` key:** verify on the pioarduino releases page and in the board JSON [INFERRED].
- **Field set:** the final list of about 30 fields depends on whether the RS485 port speaks Modbus RTU or PI30 (other research tracks). The rules in §4 accept any short numeric key in `live` and `hist`, so the model does not need changing.
- **Grafana Cloud inactivity policy:** not found [unverified].

---

## 11. Sources (all accessed 2026-10-21)

Firebase (official):
- Pricing: https://firebase.google.com/pricing
- FAQ (Spark shutoff): https://firebase.google.com/support/faq
- RTDB limits: https://firebase.google.com/docs/database/usage/limits
- RTDB billing: https://firebase.google.com/docs/database/usage/billing
- RTDB locations: https://firebase.google.com/docs/database/locations
- RTDB REST save data: https://firebase.google.com/docs/database/rest/save-data
- RTDB REST auth: https://firebase.google.com/docs/database/rest/auth
- RTDB rules core syntax: https://firebase.google.com/docs/database/security/core-syntax
- RTDB rules conditions: https://firebase.google.com/docs/database/security/rules-conditions
- RTDB rules reference: https://firebase.google.com/docs/reference/security/database
- RTDB indexing: https://firebase.google.com/docs/database/security/indexing-data
- RTDB web lists: https://firebase.google.com/docs/database/web/lists-of-data
- Firestore quotas: https://firebase.google.com/docs/firestore/quotas
- Firestore pricing: https://firebase.google.com/docs/firestore/pricing
- Firestore storage size: https://firebase.google.com/docs/firestore/storage-size
- Firestore index overview: https://firebase.google.com/docs/firestore/query-data/index-overview
- Firestore best practices: https://firebase.google.com/docs/firestore/best-practices
- Firestore REST Write: https://firebase.google.com/docs/firestore/reference/rest/v1/Write
- Firestore REST Document: https://firebase.google.com/docs/firestore/reference/rest/v1/projects.databases.documents
- Firestore REST usage: https://firebase.google.com/docs/firestore/use-rest-api
- Storage changes FAQ: https://firebase.google.com/docs/storage/faqs-storage-changes-announced-sept-2024
- Hosting quotas: https://firebase.google.com/docs/hosting/usage-quotas-pricing
- Auth REST: https://firebase.google.com/docs/reference/rest/auth
- Auth redirect best practices: https://firebase.google.com/docs/auth/web/redirect-best-practices
- FCM web client: https://firebase.google.com/docs/cloud-messaging/js/client
- FCM HTTP v1: https://firebase.google.com/docs/cloud-messaging/send/v1-api
- JS alternative setup (CDN): https://firebase.google.com/docs/web/alt-setup
- JS release notes: https://firebase.google.com/support/release-notes/js
- npm registry: https://registry.npmjs.org/firebase

ESP32 / libraries:
- FirebaseClient repo: https://github.com/mobizt/FirebaseClient
- FirebaseClient library.properties: https://raw.githubusercontent.com/mobizt/FirebaseClient/main/library.properties
- FirebaseClient examples: `examples/BareMinimum/RealtimeDatabase/RealtimeDatabase.ino`, `examples/RealtimeDatabase/{Update,Push,Set,Timestamp}`, `examples/FirestoreDatabase/Documents/Commit/AppendArray`
- FirebaseClient sources: `src/database/RealtimeDatabase.h`, `src/database/DataOptions.h`, `src/core/AsyncResult/Value.h`, `src/ExampleFunctions.h`
- arduino-esp32 releases: https://github.com/espressif/arduino-esp32/releases
- arduino-esp32 core files and examples: NetworkClientSecure (README, WiFiClientSecure.h, NetworkClientSecure.h), HTTPClient.h, LittleFS.h, esp32-hal-psram.h, main.cpp, SimpleTime.ino, BasicOTA.ino, httpUpdateSecure.ino, HTTPS_OTA_Update.ino, mDNS_Web_Server.ino, WiFiProv.ino, ResetReason.ino, tools/partitions/*.csv, boards.txt
- arduino-esp32 lib-builder configs: https://github.com/espressif/esp32-arduino-lib-builder/tree/master/configs (defconfig.common, defconfig.esp32s3)
- ESP-IDF mbedTLS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/mbedtls.html
- ESP-IDF watchdogs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/wdts.html
- esp8266 TZ.h: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
- Google Trust Services roots: https://pki.goog/roots.pem
- Let's Encrypt certificates: https://letsencrypt.org/certificates/
- WiFiManager: https://github.com/tzapu/WiFiManager
- ElegantOTA: https://github.com/ayushsharma82/ElegantOTA
- ESPAsyncWebServer: https://github.com/ESP32Async/ESPAsyncWebServer
- ArduinoJson: https://github.com/bblanchon/ArduinoJson
- pioarduino platform: https://github.com/pioarduino/platform-espressif32

Web / iOS:
- uPlot: https://github.com/leeoniya/uPlot
- Chart.js: https://github.com/chartjs/Chart.js
- ECharts: https://github.com/apache/echarts
- jsDelivr file sizes: https://cdn.jsdelivr.net/npm/
- WebKit iOS web push: https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/
- WebKit storage policy: https://webkit.org/blog/14403/updates-to-storage-policy/
- WebKit ITP 2020 (7-day cap): https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/
- WebKit Safari 26.0: https://webkit.org/blog/17333/webkit-features-in-safari-26-0/
- WebKit WWDC25: https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/
- MDN installable PWAs: https://developer.mozilla.org/en-US/docs/Web/Progressive_web_apps/Guides/Making_PWAs_installable
- WWDC23 notes: https://wwdcnotes.com/notes/wwdc23/10120
- firebase-js-sdk issue #77: https://github.com/firebase/firebase-js-sdk/issues/77
- GitHub Pages limits: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits
- Cloudflare Pages limits: https://developers.cloudflare.com/pages/platform/limits/
- Cloudflare Workers static assets: https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/

Alerts:
- ntfy publish (incl. limits): https://docs.ntfy.sh/publish/
- ntfy server config: https://docs.ntfy.sh/config/
- ntfy phone apps: https://docs.ntfy.sh/subscribe/phone/
- ntfy pricing: https://ntfy.sh/#pricing
- ntfy iOS app: https://apps.apple.com/us/app/ntfy/id1625396347
- Telegram Bot API: https://core.telegram.org/bots/api
- Telegram Bot FAQ: https://core.telegram.org/bots/faq
- Pushover pricing: https://pushover.net/pricing
- Pushover API: https://pushover.net/api

Alternatives:
- Supabase pricing: https://supabase.com/pricing
- Supabase project pausing: https://supabase.com/docs/guides/platform/free-project-pausing
- InfluxDB Cloud pricing: https://www.influxdata.com/influxdb-cloud-pricing/
- Grafana pricing: https://grafana.com/pricing/
- ThingSpeak license FAQ: https://thingspeak.mathworks.com/pages/license_faq
- ThingSpeak channel settings: https://www.mathworks.com/help/thingspeak/channel-settings.html
- Blynk pricing: https://blynk.io/pricing
- Cloudflare D1 limits: https://developers.cloudflare.com/d1/platform/limits/
- Cloudflare D1 pricing: https://developers.cloudflare.com/d1/platform/pricing/
- Cloudflare Workers limits: https://developers.cloudflare.com/workers/platform/limits/
- Adafruit IO+ guide: https://learn.adafruit.com/welcome-to-adafruit-io/io-plus
- Home Assistant Modbus: https://www.home-assistant.io/integrations/modbus/
- Home Assistant iOS app: https://apps.apple.com/us/app/home-assistant/id1099568401
- Nabu Casa pricing: https://www.nabucasa.com/pricing/

Measurements I ran:
- `openssl s_client` chains for the Firebase, googleapis, ntfy.sh and Telegram hosts.
- `curl -D` RTDB response header size (299 B).
- jsDelivr and gstatic file sizes, raw and gzip -9.
- GitHub API release tags and dates for FirebaseClient, arduino-esp32, ElegantOTA, ESPAsyncWebServer, uPlot, Chart.js, ECharts, ArduinoJson, ntfy and pioarduino.
