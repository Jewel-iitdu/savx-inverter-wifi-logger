// Budget.cpp - see Budget.h.
#include "net/Budget.h"

#include <stdio.h>

void Backoff::fail(uint32_t nowMs) {
  uint32_t d = cfg::BACKOFF_MIN_MS;
  for (uint8_t i = 0; i < step_ && d < cfg::BACKOFF_MAX_MS; i++) d *= 2;
  if (d > cfg::BACKOFF_MAX_MS) d = cfg::BACKOFF_MAX_MS;
  delay_ = d;
  until_ = nowMs + d;
  active_ = true;
  if (step_ < 16) step_++;
}

void Budget::roll(int32_t day) {
  if (day == day_) return;
  day_ = day;
  rq_ = hs_ = 0;
  guardRaised_ = false;
}

uint8_t Budget::level() const {
  if (hs_ > cfg::BUDGET_HS_HARD || rq_ > cfg::BUDGET_RQ_HARD) return 2;
  if (hs_ > cfg::BUDGET_HS_GUARD || rq_ > cfg::BUDGET_RQ_GUARD) return 1;
  return 0;
}

bool Budget::takeGuardEvent() {
  if (guardRaised_ || level() == 0) return false;
  guardRaised_ = true;
  return true;
}

uint32_t Budget::liveMs(int liveSec) const {
  if (liveSec < cfg::LIVE_SEC_MIN) liveSec = cfg::LIVE_SEC_MIN;
  if (liveSec > cfg::LIVE_SEC_MAX) liveSec = cfg::LIVE_SEC_MAX;
  uint8_t l = level();
  if (l >= 2) return 0;
  if (l == 1 && liveSec < 60) liveSec = 60;
  return (uint32_t)liveSec * 1000UL;
}

bool alertRefused(int code, bool smtp) {
  if (smtp) return code >= 500 && code < 600;
  return code >= 400 && code < 500 && code != 408 && code != 429;
}

uint32_t alertRetryMs(uint8_t failed, bool refused) {
  static const uint16_t DELAY_S[] = {30, 60, 120, 300, 600, 900, 1800, 1800, 3600, 3600, 3600, 3600};
  if (failed == 0) return 0;
  if (refused && failed >= 3) return 0;
  if (failed > sizeof DELAY_S / sizeof DELAY_S[0]) return 0;
  return DELAY_S[failed - 1] * 1000UL;
}

void alertErrText(int tls, const char* err, char* out, size_t n) {
  if (!n) return;
  if (tls < 0) snprintf(out, n, "TLS -0x%04X%s", (unsigned)-tls, tlsOutOfMemory(tls) ? " out of memory" : "");
  else snprintf(out, n, "%s", err ? err : "");
}

bool tlsOutOfMemory(int mbedtlsErr) {
  if (mbedtlsErr >= 0) return false;
  int v = -mbedtlsErr;
  int high = v & 0x7F80, low = v & 0x007F;
  // SSL, X509, PEM, PK, ECP, DHM, CIPHER, MD, PKCS7 *_ALLOC_FAILED (mbedTLS 3.x)
  static const int HIGH[] = {0x7F00, 0x2880, 0x1180, 0x3F80, 0x4D80, 0x3400, 0x6180, 0x5180, 0x5780};
  for (int h : HIGH)
    if (high == h) return true;
  return low == 0x0010 || low == 0x006A || low == 0x0017;  // MPI, ASN1, LMS *_ALLOC_FAILED
}

NetWatchdog::Action NetWatchdog::step(uint32_t nowMs, bool wifiUp, bool cloudConfigured, uint32_t nowEpoch,
                                      uint32_t lastRestartEpoch) {
  if (!started_) {
    started_ = true;
    lastOk_ = silentSince_ = nowMs;
  }
  if (!wifiUp || !cloudConfigured) {  // only "Wi-Fi up but the cloud is unreachable" counts
    lastOk_ = silentSince_ = nowMs;
    dropped_ = false;
    return Action::NONE;
  }
  uint32_t since = nowMs - lastOk_;
  bool gapOk = nowEpoch == 0 || lastRestartEpoch == 0 || nowEpoch >= lastRestartEpoch + cfg::NETWD_RESTART_MIN_GAP_S;
  if ((uint32_t)(nowMs - silentSince_) >= cfg::NETWD_SILENT_RESTART_MS && gapOk) return Action::RESTART;
  if (since >= cfg::NETWD_RESTART_MS) {
    bool connectRecently = haveConnect_ && (uint32_t)(nowMs - lastConnect_) < cfg::NETWD_RESTART_MS;
    if (connectRecently && gapOk) return Action::RESTART;
    lastOk_ = nowMs - cfg::NETWD_DROP_MS;  // no reboot: drop TLS again and re-check in 45 min
    dropped_ = true;
    return Action::DROP_TLS;
  }
  if (since >= cfg::NETWD_DROP_MS && !dropped_) {
    dropped_ = true;
    return Action::DROP_TLS;
  }
  return Action::NONE;
}

WifiWatchdog::Action WifiWatchdog::step(uint32_t nowMs, bool wifiUp, bool portal) {
  if (!started_) {
    started_ = true;
    downSince_ = lastKick_ = nowMs;
  }
  if (wifiUp || portal) {
    if (wifiUp) everUp_ = true;
    downSince_ = lastKick_ = nowMs;
    return Action::NONE;
  }
  if (downMs(nowMs) >= (everUp_ ? cfg::WIFI_RESTART_MS : cfg::WIFI_RESTART_NEVER_UP_MS)) return Action::RESTART;
  if ((uint32_t)(nowMs - lastKick_) >= cfg::WIFI_KICK_MS) {
    lastKick_ = nowMs;
    return Action::RECONNECT;
  }
  return Action::NONE;
}
