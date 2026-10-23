// config.h - pins, defaults, intervals and safety switches for the Savx F4 datalogger.
// Design reference: research/d1-architecture.md §3.7, §5.4, §8.2. Host-portable (no Arduino.h).
#pragma once
#include <stdint.h>

#define FW_VERSION "0.9.7"
#define FW_NAME "savx-logger"

// ---------------------------------------------------------------------------------------------
// Read-only safety (d1 §3.10 layer 1). There is NO setter code in this firmware. The flag exists so
// a build that tries to enable writes fails loudly instead of silently doing nothing.
#ifndef INV_ALLOW_WRITES
#define INV_ALLOW_WRITES 0
#endif
#if INV_ALLOW_WRITES != 0
#error "INV_ALLOW_WRITES must be 0: this firmware is read-only towards the inverter (d1 D11)."
#endif

namespace cfg {

// ---------------------------------------------------------------------------------------------
// Pins (d1 §8.2). Never use 0/3/45/46 (strapping), 19/20 (USB), 26-32 + 35-37 (flash/PSRAM), 43/44.
constexpr int PIN_INV_TX = 17;   // UART1 TX (inverted) -> inverter RX pin. High-Z INPUT until the link is ARMED.
constexpr int PIN_MOD_HDR_RXD = 18;  // module header "RXD" (expected RO). Self-ID may swap with 16.
constexpr int PIN_MOD_HDR_TXD = 16;  // module header "TXD" (expected DI, held HIGH = driver disabled).
constexpr int PIN_METER = 4;     // diagnostic "meter" input (ADC1). Wire it ONLY to a LAN wire that passed idle + upper check.
constexpr int PIN_BOOT = 0;      // BOOT button: hold 3 s while running -> Wi-Fi/credential portal.
#ifndef LED_PIN
#define LED_PIN 48               // RGB LED: GPIO48 on DevKitC-1 v1.0 and most clones, GPIO38 on v1.1.
#endif
constexpr int PIN_LED = LED_PIN;

// ---------------------------------------------------------------------------------------------
// Inverter serial link (d1 §3.2, §3.7)
constexpr uint32_t BAUD_PI30 = 2400;
constexpr uint32_t FIRST_BYTE_MS = 1500;   // wait for the first reply byte
constexpr uint32_t INTER_BYTE_MS = 200;    // silence that ends a frame
constexpr uint32_t INTER_CMD_GAP_MS = 150; // gap between commands
constexpr uint32_t CHK_LINGER_MS = 30;     // after CR on a CHK-framed reply, wait for a possible CHK==0x0D/0x0A byte
constexpr uint32_t FRAME_MAX_MS = 2500;    // hard cap for one reply
constexpr uint16_t RX_BUF = 256;           // receive buffer (d1 §3.7)
constexpr uint32_t PROBE_LISTEN_MS = 3000; // silent listening before each probe profile
constexpr uint32_t LINK_OK_WINDOW_MS = 15000;  // lk=1 if a valid reply arrived within this window
constexpr uint16_t LINK_LOST_FAILS = 12;   // consecutive failed fast polls (~60 s) -> LINK_LOST
constexpr uint16_t REPROBE_FAILS = 60;     // (~5 min) -> background re-probe with back-off
constexpr uint32_t REPROBE_BACKOFF_MS[] = {60000UL, 300000UL, 900000UL, 3600000UL};
constexpr uint32_t RAW_REPROBE_MS = 6UL * 3600UL * 1000UL;  // RawCapture: re-probe every 6 h
constexpr uint32_t UNSUPPORTED_RETRY_MS = 24UL * 3600UL * 1000UL;  // NAK'd commands retried once a day
constexpr uint32_t GRID_BOOT_GRACE_MS = 60000;   // 0.7.9: no GRID_LOSS open in the first minute (cfg + outage state load)
constexpr int GRID_OFF_GRACE_MIN = 10;           // planned grid-off window +- this (clock drift, slow switch)
constexpr uint32_t OUTAGE_RESUME_MAX_S = 86400;  // an outage persisted before a reboot is resumed within 24 h
constexpr uint8_t DEMOTE_FAILS = 5;  // 0.7.4: NAKs/timeouts in a row before a command that answered before is demoted

// Command periods (d1 §3.7)
constexpr uint32_t P_FAST_MS = 2000;       // QPIGS (stock) / QALL (REVO); 0.7.2: was 5 s, the LCD refreshes ~1 s
constexpr uint32_t P_MID_MS = 10000;       // QMOD / QPIWS alternating
constexpr uint32_t P_REVO_QPIGS_MS = 30000;
constexpr uint32_t P_BMS_MS = 4000;          // REVO QLITH0 (lithium BMS via the inverter), 0.7.6
constexpr uint32_t BMS_FRESH_MS = 15000;     // BMS values older than this are not used
constexpr uint32_t P_Q1_MS = 60000;
constexpr uint32_t P_RATING_MS = 600000;   // QPIRI, QFLAG, PQSE
constexpr uint32_t P_ENERGY_MS = 300000;   // QT, QET, QEDyyyymmdd, QLDyyyymmdd
constexpr uint32_t P_IDENT_MS = 3600000;   // QPI, QID/QSID, QVFW, QVFW2, QMN, QGMN
constexpr uint32_t CONFIG_BIT_DEBOUNCE_MS = 60000;  // QPIGS b6 re-read trigger at most once a minute
constexpr uint32_t SCHED_TICK_MS = 50;

// Plausibility gates (d1 §3.5; manual §11). 24 V system.
constexpr float GATE_BV_MIN = 18.0f, GATE_BV_MAX = 34.0f;
constexpr float GATE_OV_MIN = 180.0f, GATE_OV_MAX = 260.0f;  // or exactly 0
constexpr float GATE_OF_MIN = 45.0f, GATE_OF_MAX = 65.0f;    // or exactly 0
constexpr float GATE_PVV_MAX = 520.0f;
constexpr float GATE_LD_MAX = 250.0f;
constexpr float PV_MAX_INPUT_A = 15.0f;   // manual §11: max PV input current 15 A (field-12 rule)
constexpr float TMP_RAW_ADC_ABOVE = 150.0f;

// ---------------------------------------------------------------------------------------------
// Pin diagnostics (d1 §8.4 A, M2)
constexpr uint32_t IDLE_SAMPLE_MS = 10;
constexpr uint32_t IDLE_DURATION_MS = 10000;
constexpr float IDLE_TX_LOW_PCT = 99.0f;    // >= 99 % LOW ...
constexpr uint16_t IDLE_MAX_EDGES = 3;      // ... with < 3 edges -> idles negative -> inverter TX
constexpr uint8_t IDLE_CONFIRMATIONS = 2;   // the TX verdict must repeat twice before arming
constexpr uint32_t SELFID_SAMPLE_MS = 500;
constexpr uint32_t PREARM_SAMPLE_MS = 200;  // pre-arm check: GPIO17 (pull-down) and RO must read LOW (safety review)

// ---------------------------------------------------------------------------------------------
// Aggregation, storage (d1 §4.3-4.4, §5.5)
constexpr uint32_t MINUTE_MS = 60000;
constexpr uint32_t ENERGY_MAX_DT_MS = 30000;  // longer gaps are missing data, never interpolated
constexpr uint32_t RING_SAMPLES = 17280;      // 24 h of 5 s samples in PSRAM
constexpr uint32_t NVS_CHECKPOINT_MS = 300000;
constexpr uint8_t KEEP_UPLOADED_DAYS = 14;
constexpr uint32_t FS_MIN_FREE_BYTES = 200UL * 1024UL;  // drop old queue files below this
constexpr uint32_t TIME_VALID_MIN_EPOCH = 1767225600UL;  // 2026-01-01T00:00:00Z
constexpr int32_t TZ_OFFSET_S = 6 * 3600;    // Asia/Dhaka, UTC+6, no DST
#define TZ_POSIX "<+06>-6"
// NTP servers as IP literals, NOT host names (field crash 2026-10-22): Arduino-ESP32 3.3.x NetworkManager::hostByName()
// calls dns_clear_cache() without the lwIP core lock when the interface state changes (first lookup after boot or a
// Wi-Fi reconnect). That fires a pending SNTP DNS callback outside the TCPIP thread -> udp_new() asserts "Required to
// lock TCPIP core functionality" -> PANIC reboot. With literal IPs SNTP never has a pending DNS request.
#define NTP_SERVER_1 "216.239.35.0"   // time.google.com (anycast)
#define NTP_SERVER_2 "162.159.200.1"  // time.cloudflare.com (anycast)

// ---------------------------------------------------------------------------------------------
// Alerts (d1 §7.3)
constexpr uint8_t ALERT_QUEUE_DEPTH = 24;
constexpr uint8_t PUSH_CAP_PER_DAY = 40;

// ---------------------------------------------------------------------------------------------
// Usage-threshold rules + e-mail outbox (research/r8-email-alerts.md §4.12)
constexpr uint8_t MAX_RULES = 16;
constexpr uint8_t RULE_EVENTS_PER_DAY = 20;
constexpr uint8_t MAIL_JOBS_PER_DAY = 40, MAIL_RESERVE_SEV4 = 5;
constexpr uint32_t RULE_NAN_GRACE_MS = 30000;
constexpr uint32_t ALERTS_REFRESH_MS = 6UL * 3600UL * 1000UL;
constexpr uint16_t ALERT_CATALOG_VERSION = 1;  // status.acat
constexpr uint32_t NUDGE_MIN_GAP_MS = 60000;   // v1.1 mailer nudge
constexpr uint8_t NUDGE_MAX_PER_DAY = 48;
constexpr uint32_t ALERTS_JSON_MAX = 10240;    // GET alerts.json cap (r8: <= ~6 KB)

// ---------------------------------------------------------------------------------------------
// Cloud uplink (d1 §4.3, §4.6, §4.8, §5.4, §5.5)
#define DEVICE_ID "inv1"
constexpr uint32_t HTTP_CONNECT_MS = 5000, HTTP_HANDSHAKE_S = 10, HTTP_RESPONSE_MS = 10000;
constexpr uint32_t TOKEN_REFRESH_BEFORE_S = 300;    // refresh the ID token 5 min before expiry
constexpr int LIVE_SEC_MIN = 2, LIVE_SEC_MAX = 300;  // cfg.liveSec range (rules: same)
constexpr uint32_t CFG_POLL_MS = 60000;
constexpr uint32_t STATUS_MS = 60000;
constexpr uint32_t DIAG_RAW_MS = 600000;
constexpr uint32_t HC_PING_MS = 300000;
constexpr uint32_t BACKFILL_GAP_MS = 2000;          // >= 2 s between back-fill PATCHes
constexpr uint16_t BACKFILL_MAX_RECS = 60;          // records per back-fill PATCH (~15 KB)
constexpr uint16_t LIVE_BATCH_MAX = 2;              // <= 2 pending records of today -> "minute PATCH"
constexpr uint32_t BACKOFF_MIN_MS = 5000, BACKOFF_MAX_MS = 300000;
// Requests per day: live every 2 s = 43,200, plus minute PATCH and cfg GET (1,440 each). 0.7.2 raised the request
// limits from 20k/40k; they are keep-alive requests (~340 B billed each, ~15 MB/day), the costly runaway is handshakes.
constexpr uint32_t BUDGET_HS_GUARD = 1500, BUDGET_RQ_GUARD = 60000;   // d1 §4.6 (b)
constexpr uint32_t BUDGET_HS_HARD = 3000, BUDGET_RQ_HARD = 120000;    // FW-2 addition: live paused
constexpr uint32_t NETWD_DROP_MS = 15UL * 60000UL, NETWD_RESTART_MS = 60UL * 60000UL;
constexpr uint32_t NETWD_RESTART_MIN_GAP_S = 6UL * 3600UL;           // at most one network-watchdog reboot per 6 h
constexpr uint8_t EVENT_MAX_TRIES = 3;              // permanent RTDB errors per event before it is dropped (edrop)
constexpr uint8_t PUSH_MAX_TRIES = 3;
constexpr uint32_t PRUNE_AFTER_MIN = 5;             // prune at/after 00:05 local
constexpr uint32_t BACKLOG_STUCK_S = 6UL * 3600UL;

// ---------------------------------------------------------------------------------------------
// System (d1 §5.7-5.9)
constexpr uint8_t SAFE_MODE_BOOTFAILS = 3;
constexpr uint32_t HEALTHY_AFTER_MS = 120000;
constexpr uint32_t WDT_TIMEOUT_S = 30;
constexpr uint32_t BOOT_HOLD_MS = 3000;
constexpr uint32_t PORTAL_TIMEOUT_S = 600;   // on-demand portal closes after 10 min
#define MDNS_HOST "solar"
#define AP_NAME_PREFIX "SavxLogger-"
#define ADMIN_USER "admin"
// Remote firmware update (0.9.2, net/RemoteOta.h): GitHub repo whose public releases the logger installs from. Seeds
// the NVS setting once (change it at /api/config otaRepo); public builds leave it empty (remote update off).
#define OTA_REPO_DEFAULT ""

// Task layout (d1 §5.3)
constexpr uint32_t STACK_INV = 12288, STACK_AGG = 10240, STACK_UPLINK = 16384;  // d1: 8/8/16 KB; +headroom for the probe and alert texts
constexpr int PRIO_INV = 5, PRIO_AGG = 4, PRIO_UPLINK = 3;
constexpr int CORE_INV = 1, CORE_AGG = 1, CORE_UPLINK = 0;

// Grid meter (0.9.0, src/meter/Meter.h): Smart Life / Tuya breaker on the LAN, read-only
constexpr uint32_t STACK_METER = 8192;
constexpr int PRIO_METER = 1, CORE_METER = 0;
constexpr uint16_t METER_PORT = 6668;
constexpr uint32_t METER_POLL_MS = 15000;          // one short connection per poll, closed straight away
constexpr uint32_t METER_FRESH_MS = 60000;         // older readings are not uploaded
constexpr int32_t METER_CONNECT_MS = 1500;
constexpr int32_t METER_SCAN_CONNECT_MS = 300;     // per address while searching the /24
constexpr uint32_t METER_REPLY_MS = 4000;
constexpr int METER_PHASE_DP = 6;                  // phase_a (V, A, W): refreshed after each status query
constexpr int METER_FAILS_BEFORE_SCAN = 3;         // then the saved IP is presumed moved (DHCP)
constexpr uint32_t METER_RESCAN_MIN_MS = 10UL * 60000UL, METER_RESCAN_MAX_MS = 60UL * 60000UL;
constexpr uint32_t METER_DAY_SAVE_MS = 30UL * 60000UL;   // day accounting to NVS (and at every roll-over)
constexpr uint8_t METER_MAX_CRASHES = 2;           // crashes with the meter on -> meter reading stops (re-arm)
constexpr uint32_t METER_CRASH_CLEAR_MS = 6UL * 3600000UL;

// Remote firmware update (0.9.2, net/OtaPlan.h): probation of a new image, download limits
constexpr uint32_t OTA_PROBATION_MIN_MS = 120000;   // confirmed after a cloud write and 2 min of uptime
constexpr uint32_t OTA_PROBATION_MAX_MS = 600000;   // no cloud write within 10 min: back to the previous image
// One TLS session: a ~17 KB record buffer and ~40 KB in all (internal RAM). 0.9.2/0.9.3 asked for a 45 KB block and
// refused on the owner's logger (largest block 31.7 KB, the fragmentation its other TLS sessions run in).
constexpr uint32_t OTA_MIN_BLOCK = 20000;
constexpr uint32_t OTA_MIN_FREE = 45000;
constexpr uint32_t OTA_TLS_TIMEOUT_S = 15;
constexpr int32_t OTA_CONNECT_MS = 10000;
constexpr uint32_t OTA_IO_MS = 20000;               // no data for this long: give up
constexpr uint32_t OTA_TOTAL_MS = 5UL * 60000UL;
constexpr uint32_t OTA_PROBE_AFTER_MS = 60000;      // 0.9.6: check the GitHub path once, a minute after boot

}  // namespace cfg
