// Budget.h - quota guard, retry back-off and network watchdog for the uplink (d1 §4.6, §5.5). Host-portable.
//
// Spark bills RTDB download incl. ~3.5 KB per TLS handshake and refused requests; a retry loop that reconnects every
// second would reach the 10 GB/month cap in about a month (d1 §4.6). Hence:
//   (a) back-off 5, 10, 20 ... 300 s after network/5xx/429 errors (never a tight loop);
//   (b) a daily budget: > 1,500 handshakes or > 60,000 requests (0.7.2; was 20,000) -> level 1 (live >= 60 s, no diag, QUOTA_GUARD
//       event once a day); FW-2 adds level 2 at 2x those numbers (live paused, cfg every 5 min);
//   network watchdog: Wi-Fi up but no cloud success for 15 min -> drop TLS + re-auth; 60 min -> reboot (at most once
//       per 6 h, and only after a TCP/TLS connect worked in that hour, so a plain ISP outage never reboot-loops).
#pragma once
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
  void onCloudOk(uint32_t nowMs) { lastOk_ = nowMs; dropped_ = false; }
  void onConnectOk(uint32_t nowMs) { lastConnect_ = nowMs; haveConnect_ = true; }
  // wifiUp: station connected; nowEpoch/lastRestartEpoch: guard of one reboot per NETWD_RESTART_MIN_GAP_S
  Action step(uint32_t nowMs, bool wifiUp, bool cloudConfigured, uint32_t nowEpoch, uint32_t lastRestartEpoch);
  void reset(uint32_t nowMs) { lastOk_ = nowMs; dropped_ = false; }

 private:
  uint32_t lastOk_ = 0, lastConnect_ = 0;
  bool haveConnect_ = false, dropped_ = false, started_ = false;
};
