// TuyaProto.cpp - see TuyaProto.h.
#include "meter/TuyaProto.h"

#include <stdio.h>
#include <string.h>

namespace tuya {

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

bool selfTest(Crypto& c) {
  static const uint8_t ECB[16] = {0x25, 0xda, 0x71, 0x8a, 0x6a, 0xa2, 0xe4, 0x16,
                                  0xf0, 0x0e, 0xe1, 0xc4, 0xe6, 0x91, 0x84, 0x74};
  static const uint8_t MAC[32] = {0x13, 0x45, 0x04, 0xc9, 0x57, 0xf4, 0xb6, 0x0d, 0xe8, 0xf3, 0x43,
                                  0x55, 0x75, 0x51, 0xf3, 0x66, 0x20, 0xe1, 0x03, 0x43, 0xda, 0xe2,
                                  0x9b, 0x78, 0x11, 0x7d, 0x26, 0x70, 0x1e, 0x52, 0xd7, 0x80};
  static const uint8_t GCM[16] = {0x04, 0xb8, 0x05, 0x2d, 0x2a, 0x2e, 0xfb, 0xa3,
                                  0xbb, 0xdc, 0x86, 0x07, 0x6f, 0x8a, 0x4a, 0x09};
  static const uint8_t TAG[16] = {0xb8, 0x34, 0x9d, 0xd2, 0x6c, 0x9a, 0x1b, 0x0e,
                                  0x09, 0x74, 0x40, 0xc8, 0x1b, 0x8b, 0x83, 0x87};
  const uint8_t* k = (const uint8_t*)"k3y-For-Tests-16";
  const uint8_t* d = (const uint8_t*)"0123456789abcdef";
  const uint8_t* aad = (const uint8_t*)"aad-14-bytes!!";
  const uint8_t iv[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  uint8_t a[32], b[16], tag[16];
  if (!c.ecb(true, k, d, 16, a) || memcmp(a, ECB, 16)) return false;
  if (!c.ecb(false, k, ECB, 16, b) || memcmp(b, d, 16)) return false;
  if (!c.hmac(k, d, 16, a) || memcmp(a, MAC, 32)) return false;
  if (!c.gcmEnc(k, iv, aad, 14, d, 16, a, tag) || memcmp(a, GCM, 16) || memcmp(tag, TAG, 16)) return false;
  if (!c.gcmDec(k, iv, aad, 14, GCM, 16, TAG, b) || memcmp(b, d, 16)) return false;
  tag[0] ^= 1;
  return !c.gcmDec(k, iv, aad, 14, GCM, 16, tag, b);  // a forged tag must fail
}

// "3.x" + 12 zero bytes in front of some payloads
static void stripVersionHeader(const uint8_t*& p, size_t& n) {
  if (n >= 15 && p[0] == '3' && p[1] == '.' && p[2] >= '0' && p[2] <= '9') {
    p += 15;
    n -= 15;
  }
}

static bool unpad(const uint8_t* p, size_t& n) {
  if (!n) return false;
  uint8_t k = p[n - 1];
  if (k < 1 || k > 16 || k > n) return false;
  n -= k;
  return true;
}

void Session::begin(int ver, const uint8_t localKey[16], const char* devId) {
  ver_ = (ver == 34 || ver == 35) ? ver : 33;
  memcpy(real_, localKey, 16);
  memcpy(key_, localKey, 16);
  snprintf(devId_, sizeof devId_, "%s", devId ? devId : "");
  keyed_ = false;
  seq_ = 1;
}

size_t Session::pack(uint32_t cmd, const uint8_t* plain, size_t n, uint8_t* out, size_t cap) {
  if (ver_ == 35) {
    size_t total = 18 + 12 + n + 16 + 4;
    if (total > cap) return 0;
    put32(out, 0x00006699);
    out[4] = out[5] = 0;
    put32(out + 6, seq_++);
    put32(out + 10, cmd);
    put32(out + 14, (uint32_t)(12 + n + 16));
    uint8_t* iv = out + 18;
    c_.random(iv, 12);
    if (!c_.gcmEnc(key_, iv, out + 4, 14, plain, n, out + 30, out + 30 + n)) return 0;
    put32(out + total - 4, 0x00009966);
    return total;
  }
  size_t m = (n / 16 + 1) * 16;  // PKCS7 always adds 1..16 bytes
  size_t endLen = ver_ == 34 ? 36 : 8;
  size_t total = 16 + m + endLen;
  if (total > cap) return 0;
  put32(out, 0x000055AA);
  put32(out + 4, seq_++);
  put32(out + 8, cmd);
  put32(out + 12, (uint32_t)(m + endLen));
  uint8_t* p = out + 16;
  memmove(p, plain, n);
  memset(p + n, (int)(m - n), m - n);
  if (!c_.ecb(true, key_, p, m, p)) return 0;
  if (ver_ == 34) {
    if (!c_.hmac(key_, out, 16 + m, out + 16 + m)) return 0;
  } else {
    put32(out + 16 + m, crc32(out, 16 + m));
  }
  put32(out + total - 4, 0x0000AA55);
  return total;
}

size_t Session::buildStart(uint8_t* out, size_t cap) {
  keyed_ = false;
  memcpy(key_, real_, 16);
  c_.random(nonce_, 16);
  return pack(CMD_SESS_START, nonce_, 16, out, cap);
}

size_t Session::onSessResp(const Frame& f, uint8_t* out, size_t cap) {
  if (keyed_ || f.cmd != CMD_SESS_RESP || f.n < 48) return 0;
  uint8_t mac[32], remote[16];
  if (!c_.hmac(real_, nonce_, 16, mac) || memcmp(mac, f.payload + 16, 32) != 0) return 0;
  memcpy(remote, f.payload, 16);
  if (!c_.hmac(real_, remote, 16, mac)) return 0;
  size_t len = pack(CMD_SESS_FINISH, mac, 32, out, cap);  // still under the local key
  if (!len) return 0;
  uint8_t x[16], sess[16], tag[16];
  for (int i = 0; i < 16; i++) x[i] = nonce_[i] ^ remote[i];
  bool ok = ver_ == 34 ? c_.ecb(true, real_, x, 16, sess) : c_.gcmEnc(real_, nonce_, nullptr, 0, x, 16, sess, tag);
  if (!ok) return 0;
  memcpy(key_, sess, 16);
  keyed_ = true;
  return len;
}

size_t Session::buildQuery(uint8_t* out, size_t cap, uint32_t epoch) {
  if (ver_ >= 34) return pack(CMD_DP_QUERY_NEW, (const uint8_t*)"{}", 2, out, cap);
  char js[160];
  int n = snprintf(js, sizeof js, "{\"gwId\":\"%s\",\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%lu\"}", devId_, devId_,
                   devId_, (unsigned long)epoch);
  if (n <= 0 || n >= (int)sizeof js) return 0;
  return pack(CMD_DP_QUERY, (const uint8_t*)js, (size_t)n, out, cap);
}

size_t Session::buildRefresh(uint8_t* out, size_t cap, int dp) {
  char js[24];
  int n = snprintf(js, sizeof js, "{\"dpId\":[%d]}", dp);
  if (n <= 0 || n >= (int)sizeof js) return 0;
  return pack(CMD_UPDATEDPS, (const uint8_t*)js, (size_t)n, out, cap);
}

Parse Session::parse(uint8_t* buf, size_t n, Frame& f, size_t& consumed) {
  consumed = 0;
  f = Frame();
  if (n < 4) {
    consumed = 4;
    return Parse::MORE;
  }
  uint32_t prefix = be32(buf);
  if (prefix == 0x000055AA) {
    if (n < 16) {
      consumed = 16;
      return Parse::MORE;
    }
    size_t endLen = ver_ == 34 ? 36 : 8;
    uint32_t len = be32(buf + 12);
    if (len < 4 + endLen || len > MAX_FRAME) return Parse::SKIP;
    size_t total = 16 + len;
    if (n < total) {
      consumed = total;
      return Parse::MORE;
    }
    consumed = total;
    if (be32(buf + total - 4) != 0x0000AA55) return Parse::BAD;
    if (ver_ == 34) {
      uint8_t mac[32];
      if (!c_.hmac(key_, buf, total - 36, mac) || memcmp(mac, buf + total - 36, 32) != 0) return Parse::BAD;
    } else if (crc32(buf, total - 8) != be32(buf + total - 8)) {
      return Parse::BAD;
    }
    f.seq = be32(buf + 4);
    f.cmd = be32(buf + 8);
    f.retcode = be32(buf + 16);
    uint8_t* p = buf + 20;
    size_t pn = total - 20 - endLen;
    const uint8_t* q = p;
    if (ver_ == 33) stripVersionHeader(q, pn);
    if (pn) {
      uint8_t* w = p + (q - p);
      if (pn % 16 || !c_.ecb(false, key_, w, pn, w) || !unpad(w, pn)) return Parse::BAD;
      q = w;
      if (ver_ == 34) stripVersionHeader(q, pn);
    }
    f.payload = q;
    f.n = pn;
    return Parse::OK;
  }
  if (prefix == 0x00006699) {
    if (n < 18) {
      consumed = 18;
      return Parse::MORE;
    }
    uint32_t len = be32(buf + 14);
    if (len < 12 + 16 || len > MAX_FRAME) return Parse::SKIP;
    size_t total = 18 + len + 4;
    if (n < total) {
      consumed = total;
      return Parse::MORE;
    }
    consumed = total;
    if (be32(buf + total - 4) != 0x00009966) return Parse::BAD;
    uint8_t* ct = buf + 30;
    size_t ctn = len - 28;
    if (!c_.gcmDec(key_, buf + 18, buf + 4, 14, ct, ctn, buf + total - 20, ct)) return Parse::BAD;
    f.seq = be32(buf + 6);
    f.cmd = be32(buf + 10);
    const uint8_t* q = ct;
    size_t pn = ctn;
    if (pn >= 4) {
      f.retcode = be32(q);
      q += 4;
      pn -= 4;
    }
    stripVersionHeader(q, pn);
    f.payload = q;
    f.n = pn;
    return Parse::OK;
  }
  return Parse::SKIP;
}

}  // namespace tuya
