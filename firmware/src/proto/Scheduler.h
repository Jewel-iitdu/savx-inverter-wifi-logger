// Scheduler.h - per-command periods and the supported-command bitmap (d1 §3.6-3.7). Host-portable.
#pragma once
#include <stdint.h>

#include "config.h"
#include "proto/Commands.h"

class CommandScheduler {
 public:
  struct Entry {
    Cmd cmd = Cmd::COUNT;
    uint32_t periodMs = 0;  // 0 = one-shot
    uint32_t nextMs = 0;
    bool fast = false;
    bool active = false;
  };
  static constexpr int MAX = 24;

  void clear();
  void add(Cmd c, uint32_t periodMs, uint32_t offsetMs, bool fast, uint32_t nowMs);
  void addOnce(Cmd c, uint32_t delayMs, uint32_t nowMs);
  // Index of the entry to run now, or -1. The fast command wins; otherwise the most overdue usable entry.
  int due(uint32_t nowMs) const;
  void done(int idx, uint32_t nowMs);
  void trigger(Cmd c, uint32_t nowMs);  // make it due now (e.g. QPIRI after status b6)
  bool has(Cmd c) const;

  // supported-command bitmap
  void markSupported(Cmd c);
  void markUnsupported(Cmd c, uint32_t nowMs);
  bool usable(Cmd c, uint32_t nowMs) const;  // not known-unsupported, or the daily retry is due
  bool supported(Cmd c) const { return (sup_ & cmdBit(c)) != 0; }
  bool unsupported(Cmd c) const { return (unsup_ & cmdBit(c)) != 0; }
  uint32_t supMask() const { return sup_; }
  uint32_t unsupMask() const { return unsup_; }
  void setMasks(uint32_t sup, uint32_t unsup, uint32_t nowMs);
  void forgetUnsupported(Cmd c) { unsup_ &= ~cmdBit(c); }
  // 0.7.4: a NAK or timeout of a command that has answered before is transient until DEMOTE_FAILS in a row, and the
  // fast command is never demoted (a dead link is the engine's job: light/full re-probe). On 2026-10-22 one NAK at 2 s
  // polling marked the REVO fast command QALL unsupported for 24 h and the dashboard showed "link down" all morning.
  // A command that never answered is still demoted at once (probe semantics). Returns true when it was demoted.
  bool noteFail(Cmd c, uint32_t nowMs);
  void noteOk(Cmd c) { fails_[(int)c] = 0; }
  bool isFast(Cmd c) const;

  int size() const { return n_; }
  const Entry& entry(int i) const { return e_[i]; }

 private:
  Entry e_[MAX];
  int n_ = 0;
  uint32_t sup_ = 0, unsup_ = 0;
  uint32_t unsupAt_[(int)Cmd::COUNT] = {};
  uint8_t fails_[(int)Cmd::COUNT] = {};
};
