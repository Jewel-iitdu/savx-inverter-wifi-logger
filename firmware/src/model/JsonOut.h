// JsonOut.h - tiny allocation-free JSON writer for compact device JSON (keys are short, NaN omitted).
// Host-portable. Output is truncated safely (ok() == false) if the buffer is too small.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class JsonOut {
 public:
  JsonOut(char* buf, size_t cap) : b_(buf), cap_(cap) { if (cap_) b_[0] = 0; }
  JsonOut& open() { raw("{"); first_ = true; return *this; }
  JsonOut& close() { raw("}"); first_ = false; return *this; }
  JsonOut& openArr(const char* k) { key(k); raw("["); first_ = true; return *this; }
  JsonOut& closeArr() { raw("]"); first_ = false; return *this; }
  JsonOut& openObj(const char* k) { key(k); raw("{"); first_ = true; return *this; }
  // number with fixed decimals; NaN is omitted
  JsonOut& num(const char* k, double v, int dec) {
    if (isnan(v) || isinf(v)) return *this;
    key(k);
    char t[32];
    if (dec <= 0) snprintf(t, sizeof t, "%.0f", v);
    else snprintf(t, sizeof t, "%.*f", dec, v);
    trimZeros(t, dec);
    return raw(t);
  }
  JsonOut& i(const char* k, long long v) {
    key(k);
    char t[24];
    snprintf(t, sizeof t, "%lld", v);
    return raw(t);
  }
  JsonOut& u(const char* k, unsigned long long v) {
    key(k);
    char t[24];
    snprintf(t, sizeof t, "%llu", v);
    return raw(t);
  }
  JsonOut& b(const char* k, bool v) { key(k); return raw(v ? "true" : "false"); }
  JsonOut& s(const char* k, const char* v) {
    key(k);
    raw("\"");
    esc(v ? v : "");
    return raw("\"");
  }
  JsonOut& ch(const char* k, char c) {
    if (!c) return *this;
    char t[2] = {c, 0};
    return s(k, t);
  }
  // array elements
  JsonOut& elNum(double v, int dec) {
    sep();
    if (isnan(v) || isinf(v)) return raw("null");
    char t[32];
    snprintf(t, sizeof t, "%.*f", dec < 0 ? 0 : dec, v);
    trimZeros(t, dec);
    return raw(t);
  }
  JsonOut& elStr(const char* v) { sep(); raw("\""); esc(v); return raw("\""); }
  JsonOut& elRaw(const char* v) { sep(); return raw(v); }
  JsonOut& elOpen() { sep(); raw("{"); first_ = true; return *this; }
  JsonOut& rawKey(const char* k, const char* rawJson) { key(k); return raw(rawJson); }

  const char* c_str() const { return b_; }
  size_t len() const { return n_; }
  bool ok() const { return !overflow_; }

  JsonOut& raw(const char* t) {
    size_t l = strlen(t);
    if (n_ + l + 1 > cap_) { overflow_ = true; return *this; }
    memcpy(b_ + n_, t, l);
    n_ += l;
    b_[n_] = 0;
    return *this;
  }

 private:
  void sep() { if (!first_) raw(","); first_ = false; }
  void key(const char* k) { sep(); raw("\""); raw(k); raw("\":"); }
  void esc(const char* v) {
    char t[8];
    for (; *v; v++) {
      unsigned char c = (unsigned char)*v;
      if (c == '"' || c == '\\') { t[0] = '\\'; t[1] = (char)c; t[2] = 0; raw(t); }
      else if (c < 0x20) { snprintf(t, sizeof t, "\\u%04x", c); raw(t); }
      else { t[0] = (char)c; t[1] = 0; raw(t); }
    }
  }
  static void trimZeros(char* t, int dec) {
    if (dec <= 0 || !strchr(t, '.')) return;
    size_t l = strlen(t);
    while (l > 0 && t[l - 1] == '0') t[--l] = 0;
    if (l > 0 && t[l - 1] == '.') t[--l] = 0;
    if (!strcmp(t, "-0")) strcpy(t, "0");
  }
  char* b_;
  size_t cap_;
  size_t n_ = 0;
  bool first_ = true;
  bool overflow_ = false;
};
