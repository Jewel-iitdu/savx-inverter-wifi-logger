// Scheduler.cpp - see Scheduler.h.
#include "proto/Scheduler.h"

void CommandScheduler::clear() {
  n_ = 0;
  for (uint8_t& f : fails_) f = 0;
}

bool CommandScheduler::isFast(Cmd c) const {
  for (int i = 0; i < n_; i++)
    if (e_[i].active && e_[i].fast && e_[i].cmd == c) return true;
  return false;
}

bool CommandScheduler::noteFail(Cmd c, uint32_t nowMs) {
  if (!supported(c)) {
    markUnsupported(c, nowMs);
    return true;
  }
  if (isFast(c)) return false;
  if (++fails_[(int)c] < cfg::DEMOTE_FAILS) return false;
  markUnsupported(c, nowMs);
  return true;
}

void CommandScheduler::add(Cmd c, uint32_t periodMs, uint32_t offsetMs, bool fast, uint32_t nowMs) {
  if (n_ >= MAX) return;
  Entry& e = e_[n_++];
  e.cmd = c;
  e.periodMs = periodMs;
  e.nextMs = nowMs + offsetMs;
  e.fast = fast;
  e.active = true;
}

void CommandScheduler::addOnce(Cmd c, uint32_t delayMs, uint32_t nowMs) { add(c, 0, delayMs, false, nowMs); }

bool CommandScheduler::has(Cmd c) const {
  for (int i = 0; i < n_; i++)
    if (e_[i].active && e_[i].cmd == c) return true;
  return false;
}

int CommandScheduler::due(uint32_t nowMs) const {
  int best = -1;
  int32_t bestLate = -1;
  for (int i = 0; i < n_; i++) {
    const Entry& e = e_[i];
    if (!e.active) continue;
    int32_t late = (int32_t)(nowMs - e.nextMs);
    if (late < 0) continue;
    if (!usable(e.cmd, nowMs)) continue;
    if (e.fast) return i;  // the fast poll always goes first
    if (late > bestLate) { bestLate = late; best = i; }
  }
  return best;
}

void CommandScheduler::done(int idx, uint32_t nowMs) {
  if (idx < 0 || idx >= n_) return;
  Entry& e = e_[idx];
  if (e.periodMs == 0) { e.active = false; return; }
  e.nextMs += e.periodMs;
  // if we fell far behind (long probe, diagnostics), re-phase instead of bursting
  if ((int32_t)(nowMs - e.nextMs) > (int32_t)e.periodMs) e.nextMs = nowMs + e.periodMs;
}

void CommandScheduler::trigger(Cmd c, uint32_t nowMs) {
  for (int i = 0; i < n_; i++)
    if (e_[i].active && e_[i].cmd == c) e_[i].nextMs = nowMs;
}

void CommandScheduler::markSupported(Cmd c) {
  fails_[(int)c] = 0;
  sup_ |= cmdBit(c);
  unsup_ &= ~cmdBit(c);
}

void CommandScheduler::markUnsupported(Cmd c, uint32_t nowMs) {
  fails_[(int)c] = 0;
  unsup_ |= cmdBit(c);
  sup_ &= ~cmdBit(c);
  unsupAt_[(int)c] = nowMs;
}

bool CommandScheduler::usable(Cmd c, uint32_t nowMs) const {
  if (!(unsup_ & cmdBit(c))) return true;
  return (uint32_t)(nowMs - unsupAt_[(int)c]) >= cfg::UNSUPPORTED_RETRY_MS;
}

void CommandScheduler::setMasks(uint32_t sup, uint32_t unsup, uint32_t nowMs) {
  sup_ = sup;
  unsup_ = unsup & ~sup;
  for (int i = 0; i < (int)Cmd::COUNT; i++)
    if (unsup_ & (1UL << i)) unsupAt_[i] = nowMs;
}
