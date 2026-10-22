// ModbusRtu.h - CRC-16/MODBUS and FC03 helpers (V1 V12). Host-portable. FC 03 ONLY: no write helpers exist.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace modbus {

// CRC-16/MODBUS: poly 0xA001 (reflected 0x8005), init 0xFFFF, transmitted LOW byte first.
inline uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
  }
  return crc;
}

inline bool crcOk(const uint8_t* f, size_t n) {
  if (n < 3) return false;
  uint16_t c = crc16(f, n - 2);
  return f[n - 2] == (uint8_t)(c & 0xFF) && f[n - 1] == (uint8_t)(c >> 8);
}

// Build "slave 03 regHi regLo qtyHi qtyLo crcLo crcHi" (8 bytes).
inline size_t buildRead(uint8_t slave, uint16_t reg, uint16_t qty, uint8_t* out) {
  out[0] = slave;
  out[1] = 0x03;
  out[2] = (uint8_t)(reg >> 8);
  out[3] = (uint8_t)(reg & 0xFF);
  out[4] = (uint8_t)(qty >> 8);
  out[5] = (uint8_t)(qty & 0xFF);
  uint16_t c = crc16(out, 6);
  out[6] = (uint8_t)(c & 0xFF);
  out[7] = (uint8_t)(c >> 8);
  return 8;
}

enum class RespCheck : uint8_t { OK, SHORT, BAD_CRC, WRONG_SLAVE, EXCEPTION, WRONG_FC, WRONG_COUNT };

// Validate an FC03 response for (slave, qty). On OK, regs[i] = register i (big-endian on the wire,
// or byte-swapped if swapBytes, as PowMr does). regs must hold qty entries.
inline RespCheck parseRead(const uint8_t* r, size_t n, uint8_t slave, uint16_t qty, uint16_t* regs, bool swapBytes) {
  if (n < 5) return RespCheck::SHORT;
  if (r[0] != slave) return RespCheck::WRONG_SLAVE;
  if (r[1] == 0x83) return crcOk(r, 5) ? RespCheck::EXCEPTION : RespCheck::BAD_CRC;
  if (r[1] != 0x03) return RespCheck::WRONG_FC;
  if (r[2] != qty * 2) return RespCheck::WRONG_COUNT;
  size_t need = (size_t)5 + (size_t)qty * 2;
  if (n < need) return RespCheck::SHORT;
  if (!crcOk(r, need)) return RespCheck::BAD_CRC;
  for (uint16_t i = 0; i < qty; i++) {
    uint8_t a = r[3 + 2 * i], b = r[4 + 2 * i];
    regs[i] = swapBytes ? (uint16_t)((uint16_t)b << 8 | a) : (uint16_t)((uint16_t)a << 8 | b);
  }
  return RespCheck::OK;
}

}  // namespace modbus
