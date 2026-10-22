// RevoChk.h - Sorotec REVO "CHK" framing (V1 V10): CHK = (sum of all bytes + 1) & 0xFF, single byte, then CR.
// Used for QALL / PQSE / PQSA (and QLITH0 as written in the doc). Replies carry CHK over '(' + payload.
// Doc examples recomputed in V1 §4.4: QALL -> 2B, PQSE -> 3A, PQSA -> 36; QALL reply example CHK 04; PQSE reply 3C.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "proto/pi30/Pi30Frame.h"

namespace revo {

inline uint8_t chk(const uint8_t* d, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i < n; i++) s += d[i];
  return (uint8_t)((s + 1) & 0xFF);
}

// Build "<cmd><CHK>\r". Returns length (0 if it does not fit).
inline size_t buildChkFrame(const char* cmd, uint8_t* out, size_t cap) {
  size_t n = strlen(cmd);
  if (n + 2 > cap) return 0;
  memcpy(out, cmd, n);
  out[n] = chk((const uint8_t*)cmd, n);
  out[n + 1] = 0x0D;
  return n + 2;
}

// Validate a CHK-framed reply buf[0..len) including the terminator. The CHK byte itself may be 0x0D/0x0A,
// so the reader lingers after the first terminator (cfg::CHK_LINGER_MS); here we only require that the
// LAST byte is a terminator and the byte before it is the CHK over everything before that.
inline pi30::Check checkChkReply(const uint8_t* buf, size_t len, char* payload, size_t pcap) {
  if (pcap) payload[0] = 0;
  if (len < 3 || !pi30::isTerm(buf[len - 1])) return pi30::Check::SHORT;
  if (buf[0] != '(') return pi30::Check::BAD_START;
  if (len >= 4 && memcmp(buf, "(NAK", 4) == 0) return pi30::Check::NAK;
  // Try "<...><CHK><term>" first; if two terminators end the frame ("\r\n" or CHK==CR), also try
  // treating the second-to-last byte as the terminator.
  for (int extra = 0; extra < 2; extra++) {
    size_t L = len - (size_t)extra;
    if (L < 3 || !pi30::isTerm(buf[L - 1])) break;
    if (chk(buf, L - 2) == buf[L - 2]) {
      size_t pl = L - 3;
      if (pl + 1 > pcap) pl = pcap ? pcap - 1 : 0;
      if (pcap) { memcpy(payload, buf + 1, pl); payload[pl] = 0; }
      return pi30::Check::OK;
    }
  }
  return pi30::Check::BAD_CRC;
}

}  // namespace revo
