// RawRing.h - ring of recent link exchanges + last raw reply per command (for /api/raw, diag/raw, probe logs).
// Host-portable, no locking (the owner, SerialLink in invTask, copies out under the app mutex).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "model/Lock.h"

struct RawExchange {
  uint32_t upMs = 0;
  char cmd[14] = "";
  uint8_t result = 0;     // LinkResult
  uint8_t txLen = 0;
  uint16_t rxLen = 0;
  uint8_t tx[24];
  uint8_t rx[200];
};

struct LastReply {
  char cmd[14] = "";
  uint32_t upMs = 0;
  uint8_t result = 0;
  char text[301] = "";    // printable rendering, non-printables as <HH>, capped at 300 chars (rules: <=300)
};

class RawRing {
 public:
  static constexpr int N = 16;
  static constexpr int NLAST = 32;

  void setLock(ILock* l) { lock_ = l; }
  ILock* lockPtr() const { return lock_; }  // readers on other tasks take this lock while copying

  void add(uint32_t upMs, const char* cmd, uint8_t result, const uint8_t* tx, size_t txLen, const uint8_t* rx,
           size_t rxLen) {
    LockGuard g(lock_);
    RawExchange& e = ring_[head_];
    head_ = (head_ + 1) % N;
    if (count_ < N) count_++;
    e.upMs = upMs;
    snprintf(e.cmd, sizeof e.cmd, "%s", cmd ? cmd : "");
    e.result = result;
    e.txLen = (uint8_t)(txLen > sizeof e.tx ? sizeof e.tx : txLen);
    if (tx && e.txLen) memcpy(e.tx, tx, e.txLen);
    e.rxLen = (uint16_t)(rxLen > sizeof e.rx ? sizeof e.rx : rxLen);
    if (rx && e.rxLen) memcpy(e.rx, rx, e.rxLen);
    if (cmd && *cmd && rxLen > 0) setLast(upMs, cmd, result, rx, rxLen);
  }

  int count() const { return count_; }
  // i = 0 is the newest
  const RawExchange& at(int i) const { return ring_[(head_ - 1 - i + 2 * N) % N]; }

  int lastCount() const { return nLast_; }
  const LastReply& last(int i) const { return last_[i]; }
  const LastReply* findLast(const char* cmd) const {
    for (int i = 0; i < nLast_; i++)
      if (!strcmp(last_[i].cmd, cmd)) return &last_[i];
    return nullptr;
  }

  // Printable rendering that is also JSON-safe: bytes outside 0x20..0x7E and '"', '\\' become <HH>.
  static size_t render(const uint8_t* b, size_t n, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 5 < cap; i++) {
      uint8_t c = b[i];
      if (c >= 0x20 && c <= 0x7E && c != '\\' && c != '"') out[o++] = (char)c;
      else o += (size_t)snprintf(out + o, cap - o, "<%02X>", c);
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
    return o;
  }
  static size_t hex(const uint8_t* b, size_t n, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < cap; i++) o += (size_t)snprintf(out + o, cap - o, i ? " %02X" : "%02X", b[i]);
    if (cap) out[o < cap ? o : cap - 1] = 0;
    return o;
  }

 private:
  void setLast(uint32_t upMs, const char* cmd, uint8_t result, const uint8_t* rx, size_t rxLen) {
    LastReply* s = nullptr;
    for (int i = 0; i < nLast_; i++)
      if (!strcmp(last_[i].cmd, cmd)) { s = &last_[i]; break; }
    if (!s) {
      if (nLast_ < NLAST) s = &last_[nLast_++];
      else s = &last_[NLAST - 1];
    }
    snprintf(s->cmd, sizeof s->cmd, "%s", cmd);
    s->upMs = upMs;
    s->result = result;
    render(rx, rxLen, s->text, sizeof s->text);
  }

  RawExchange ring_[N];
  int head_ = 0, count_ = 0;
  LastReply last_[NLAST];
  int nLast_ = 0;
  ILock* lock_ = nullptr;
};
