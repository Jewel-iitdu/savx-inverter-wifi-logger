// Budget.h - quota guard, retry back-off and network watchdog for the uplink (d1 §4.6, §5.5). Host-portable.
//
// Spark bills RTDB download incl. ~3.5 KB per TLS handshake and refused requests; a retry loop that reconnects every
// second would reach the 10 GB/month cap in about a month (d1 §4.6). Hence:
//   (a) back-off 5, 10, 20 ... 300 s after network/5xx/429 errors (never a tight loop);
//   (b) a daily budget: > 1,500 handshakes or > 60,000 requests (0.7.2; was 20,000) -> level 1 (live >= 60 s, no diag, QUOTA_GUARD
//       event once a day); FW-2 adds level 2 at 2x those numbers (live paused, cfg every 5 min);
//   network watchdog: Wi-Fi up but no cloud success for 15 min -> drop TLS + re-auth; 60 min -> reboot (at most once
//       per 6 h, and only after a TCP/TLS connect worked in that hour, so a plain ISP outage never reboot-loops).
//       0.9.8: a failed sign-in/token refresh counts as "no cloud success" unless the credentials are wrong, and a
//       TLS session that ran out of internal heap counts as a working connect (a reboot clears the heap).
//       0.9.9: 6 h without any cloud success -> reboot even without a working connect (a wedged IP stack or DNS never
//       connects), still at most once per 6 h; Wi-Fi watchdog; a crash-loop safe mode ends after 30 min.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"

class Backoff {
 public:
  void fail(uint32_t nowMs);
  void ok() { step_ = 0; until_ = 0; active_ = false; }
  bool ready(uint32_t nowMs) const { return !active_ || (int32_t)(nowMs - until_) >= 0; }
  uint32_t lastDelayMs() const { return delay_; }
  uint8_t step() const { return step_; }

 private:
  uint8_t step_ = 0;
  uint32_t until_ = 0, delay_ = 0;
  bool active_ = false;
};

class Budget {
 public:
  void roll(int32_t day);  // local day number (or uptime days without a clock)
  void onRequest() { rq_++; }
  void onHandshake() { hs_++; }
  uint8_t level() const;
  bool takeGuardEvent();   // true once per day when level first becomes >= 1
  uint32_t requests() const { return rq_; }
  uint32_t handshakes() const { return hs_; }
  // effective `live` period for cfg.liveSec at the current level (0 = paused)
  uint32_t liveMs(int liveSec) const;
  uint32_t cfgPollMs() const { return level() >= 2 ? 5UL * cfg::CFG_POLL_MS : cfg::CFG_POLL_MS; }
  bool diagAllowed() const { return level() == 0; }

 private:
  int32_t day_ = -1;
  uint32_t rq_ = 0, hs_ = 0;
  bool guardRaised_ = false;
};

class NetWatchdog {
 public:
  enum class Action : uint8_t { NONE, DROP_TLS, RESTART };
  void onCloudOk(uint32_t nowMs) { lastOk_ = silentSince_ = nowMs; dropped_ = false; }
  void onConnectOk(uint32_t nowMs) { lastConnect_ = nowMs; haveConnect_ = true; }
  // TCP reached the server but mbedTLS found no heap: our fault, not the network's
  void onOutOfMemory(uint32_t nowMs) { onConnectOk(nowMs); }
  // a failed sign-in / token refresh (0.9.8). Only wrong credentials (held for the owner, no reboot loop) keep the
  // watchdog quiet; before 0.9.8 every auth failure did, and a refresh that ran out of heap stayed stuck for good.
  void onAuthFail(uint32_t nowMs, bool credentialsHeld, bool outOfMemory) {
    if (credentialsHeld) onCloudOk(nowMs);
    else if (outOfMemory) onOutOfMemory(nowMs);
  }
  // wifiUp: station connected; nowEpoch/lastRestartEpoch: guard of one reboot per NETWD_RESTART_MIN_GAP_S
  Action step(uint32_t nowMs, bool wifiUp, bool cloudConfigured, uint32_t nowEpoch, uint32_t lastRestartEpoch);
  void reset(uint32_t nowMs) { lastOk_ = silentSince_ = nowMs; dropped_ = false; }

 private:
  uint32_t lastOk_ = 0, lastConnect_ = 0;
  uint32_t silentSince_ = 0;  // the real last cloud success (lastOk_ is moved forward between TLS drops)
  bool haveConnect_ = false, dropped_ = false, started_ = false;
};

// Wi-Fi watchdog (0.9.9). Arduino-ESP32 3.3 reconnects only for some disconnect reasons, once per event: after
// AUTH_FAIL (routers send it while they reboot), IDF 5's newer "no AP found ..." reasons or a lost IP it never tries
// again. Down WIFI_KICK_MS -> reconnect, and again every WIFI_KICK_MS. Down WIFI_RESTART_MS after it was up in this
// boot -> reboot (the stack may be wedged); never up in this boot -> reboot after WIFI_RESTART_NEVER_UP_MS. The setup
// portal owns the radio: no action while it is open.
class WifiWatchdog {
 public:
  enum class Action : uint8_t { NONE, RECONNECT, RESTART };
  Action step(uint32_t nowMs, bool wifiUp, bool portal);
  uint32_t downMs(uint32_t nowMs) const { return nowMs - downSince_; }

 private:
  uint32_t downSince_ = 0, lastKick_ = 0;
  bool started_ = false, everUp_ = false;
};

// a safe mode entered after crash-type resets (not the manual flag) ends by itself: nobody may be home to end it
inline bool safeModeAutoExit(bool crashSafe, uint32_t upMs) { return crashSafe && upMs >= cfg::SAFE_MODE_EXIT_MS; }

// Alert delivery retries (0.9.10). Before, Telegram and e-mail gave up after 3 tries within 1.5 / 3 min, so an alert
// raised during a short glitch was lost (2026-10-24 20:47: "grid restored" came late, every e-mail failed). Now a
// network/TLS/server error retries for about 6 h (30 s, 1, 2, 5, 10, 15, 30, 30, 60, 60, 60, 60 min); a refusal that a
// retry cannot fix (HTTP 4xx except 408/429: wrong token or chat; SMTP 5xx: wrong password, recipient refused) stops
// after 3 tries. The message carries its own time, so a late one still reads right.
bool alertRefused(int code, bool smtp);
// delay before the next try after `failed` failed tries (1 = the first failure); 0 = give up
uint32_t alertRetryMs(uint8_t failed, bool refused);
// short error for the cloud status (<= 40 chars, the rules' limit): "TLS -0x7F00 out of memory", "TLS -0x2700" or the
// sender's own text ("HTTP 401", "AUTH password 535")
void alertErrText(int tls, const char* err, char* out, size_t n);

// true for an mbedTLS error code (as NetworkClientSecure::lastError returns it) that means "out of memory", also
// inside a composite code (high-level module error + low-level error)
bool tlsOutOfMemory(int mbedtlsErr);
