// Budget.cpp - see Budget.h.
#include "net/Budget.h"

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

NetWatchdog::Action NetWatchdog::step(uint32_t nowMs, bool wifiUp, bool cloudConfigured, uint32_t nowEpoch,
                                      uint32_t lastRestartEpoch) {
  if (!started_) {
    started_ = true;
    lastOk_ = nowMs;
  }
  if (!wifiUp || !cloudConfigured) {  // only "Wi-Fi up but the cloud is unreachable" counts
    lastOk_ = nowMs;
    dropped_ = false;
    return Action::NONE;
  }
  uint32_t since = nowMs - lastOk_;
  if (since >= cfg::NETWD_RESTART_MS) {
    bool connectRecently = haveConnect_ && (uint32_t)(nowMs - lastConnect_) < cfg::NETWD_RESTART_MS;
    bool gapOk = nowEpoch == 0 || lastRestartEpoch == 0 || nowEpoch >= lastRestartEpoch + cfg::NETWD_RESTART_MIN_GAP_S;
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
