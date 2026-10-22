// Pi30Frame.h - Voltronic PI30 framing: CRC-16/XMODEM + Voltronic byte adjust, frame build/check, split.
// Ported from the research helper r2-code/pi30.h (original code written for this project; host-tested).
// Facts: V1 V8/V9 - CRC-16/XMODEM (poly 0x1021, init 0, no reflection, no xorout), HIGH byte first,
// each CRC byte equal to 0x28 '(' / 0x0D CR / 0x0A LF is incremented by 1. mpp-solar also bumps 0x00:
// we SEND with the 3-value rule and ACCEPT both on receive.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace pi30 {

inline uint16_t crc16Xmodem(const uint8_t* d, size_t n) {
  uint16_t crc = 0;
  while (n--) {
    crc ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

inline uint8_t adj(uint8_t b) { return (b == 0x28 || b == 0x0D || b == 0x0A) ? (uint8_t)(b + 1) : b; }

// PI30 CRC as transmitted (after the byte adjust).
inline uint16_t crc(const uint8_t* d, size_t n) {
  uint16_t c = crc16Xmodem(d, n);
  return (uint16_t)((uint16_t)adj((uint8_t)(c >> 8)) << 8) | adj((uint8_t)(c & 0xFF));
}

// mpp-solar variant that also bumps 0x00 (accepted on receive only).
inline uint16_t crcZ(const uint8_t* d, size_t n) {
  uint16_t c = crc(d, n);
  uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)(c & 0xFF);
  if (hi == 0) hi = 1;
  if (lo == 0) lo = 1;
  return (uint16_t)((uint16_t)hi << 8) | lo;
}

// Build "<cmd><CRC_hi><CRC_lo>\r". out needs strlen(cmd)+3 bytes. Returns the length (0 if it does not fit).
inline size_t buildCrcFrame(const char* cmd, uint8_t* out, size_t cap) {
  size_t n = strlen(cmd);
  if (n + 3 > cap) return 0;
  memcpy(out, cmd, n);
  uint16_t c = crc((const uint8_t*)cmd, n);
  out[n] = (uint8_t)(c >> 8);
  out[n + 1] = (uint8_t)(c & 0xFF);
  out[n + 2] = 0x0D;
  return n + 3;
}

// Build "<cmd>\r" (legacy Q1/Q6 without CRC).
inline size_t buildPlainFrame(const char* cmd, uint8_t* out, size_t cap) {
  size_t n = strlen(cmd);
  if (n + 1 > cap) return 0;
  memcpy(out, cmd, n);
  out[n] = 0x0D;
  return n + 1;
}

enum class Check : uint8_t { OK = 0, NAK, BAD_START, SHORT, BAD_CRC, NO_CRC_OK };

inline bool isTerm(uint8_t b) { return b == 0x0D || b == 0x0A; }

// Validate a raw CRC-framed reply buf[0..len) that INCLUDES the trailing CR (or LF).
// On OK/NO_CRC_OK copies the payload (after '(' , without CRC/terminator) NUL-terminated into payload.
// "(NAK" is recognised with or without CRC (r2 §8 quirk 4).
inline Check checkCrcReply(const uint8_t* buf, size_t len, char* payload, size_t pcap, bool allowNoCrc) {
  if (pcap) payload[0] = 0;
  if (len < 2 || !isTerm(buf[len - 1])) return Check::SHORT;
  if (buf[0] != '(') return Check::BAD_START;
  if (len >= 4 && memcmp(buf, "(NAK", 4) == 0) return Check::NAK;
  if (len >= 4) {
    uint16_t rx = (uint16_t)((uint16_t)buf[len - 3] << 8) | buf[len - 2];
    if (rx == crc(buf, len - 3) || rx == crcZ(buf, len - 3)) {
      size_t pl = len - 4;  // minus '(' , 2 CRC bytes, terminator
      if (pl + 1 > pcap) pl = pcap ? pcap - 1 : 0;
      if (pcap) { memcpy(payload, buf + 1, pl); payload[pl] = 0; }
      return Check::OK;
    }
  }
  if (allowNoCrc) {  // frame "(...\r" without CRC, all printable
    for (size_t i = 1; i < len - 1; i++)
      if (buf[i] < 0x20 || buf[i] > 0x7E) return Check::BAD_CRC;
    size_t pl = len - 2;
    if (pl + 1 > pcap) pl = pcap ? pcap - 1 : 0;
    if (pcap) { memcpy(payload, buf + 1, pl); payload[pl] = 0; }
    return Check::NO_CRC_OK;
  }
  return Check::BAD_CRC;
}

// Split payload on spaces IN PLACE. Returns token count (<= maxTok). A trailing space is ignored.
inline int split(char* s, char** tok, int maxTok) {
  int n = 0;
  char* p = s;
  while (*p && n < maxTok) {
    while (*p == ' ') p++;
    if (!*p) break;
    tok[n++] = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
  }
  return n;
}

// Numeric helpers: return NAN-like sentinel handling is done by the caller (see model/Decode.h).
inline bool isNumberToken(const char* t) {
  if (!t || !*t) return false;
  const char* p = t;
  if (*p == '-' || *p == '+') p++;
  bool digit = false;
  for (; *p; p++) {
    if (*p >= '0' && *p <= '9') digit = true;
    else if (*p != '.') return false;
  }
  return digit;
}

}  // namespace pi30
