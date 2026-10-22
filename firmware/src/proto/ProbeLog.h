// ProbeLog.h - human-readable probe trace (saved to /log/probe.json, shown in the local UI). Host-portable.
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class ProbeLog {
 public:
  void clear() { n_ = 0; buf_[0] = 0; lines_ = 0; }
  void setTimeBase(uint32_t t0) { t0_ = t0; }
  void add(uint32_t nowMs, const char* fmt, ...) {
    if (n_ + 16 >= sizeof buf_) return;
    n_ += (size_t)snprintf(buf_ + n_, sizeof buf_ - n_, "+%5.1fs ", (double)(nowMs - t0_) / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf_ + n_, sizeof buf_ - n_, fmt, ap);
    va_end(ap);
    if (w > 0) n_ += (size_t)w;
    if (n_ >= sizeof buf_ - 1) n_ = sizeof buf_ - 2;
    buf_[n_++] = '\n';
    buf_[n_] = 0;
    lines_++;
  }
  const char* text() const { return buf_; }
  size_t size() const { return n_; }
  int lines() const { return lines_; }

 private:
  char buf_[4096];
  size_t n_ = 0;
  int lines_ = 0;
  uint32_t t0_ = 0;
};
