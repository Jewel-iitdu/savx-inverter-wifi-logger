// Whitelist.cpp - see Whitelist.h. Host-portable.
#include "link/Whitelist.h"

#include <string.h>

#include "proto/modbus/ModbusRtu.h"
#include "proto/pi30/Pi30Frame.h"
#include "proto/pi30/RevoChk.h"

namespace wl {

// Safety review (least privilege): the whitelist is exactly the set of commands the firmware sends (proto/Commands.h,
// Prober, Q6Adapter). Removed: QMCHGCR, QMUCHGCR, QBEQI, QLT, QEMyyyymm, QEYyyyy. They are genuine read-only queries,
// but unused, and QMCHGCR/QMUCHGCR sit one dropped leading byte from the MCHGC.../MUCHGC... charging-current SETTERS,
// QBEQI one bit ('Q' 0x51 -> 'P' 0x50) from the PBEQ... equalisation SETTERS, QEY/QEM one bit from "PE" + flag letters
// - which only matters on a firmware that does not check the CRC of incoming commands (not documented either way).
const char* const PI30_READ_COMMANDS[] = {
    "QPI",  "QID",   "QSID",  "QVFW", "QVFW2", "QVFW3", "QMN", "QGMN",   "QPIRI", "QPIGS",   "QPIGS2",
    "QMOD", "QPIWS", "QFLAG", "QDI",  "Q1",    "QT",    "QET", "QLITH0", "QALL",  "^P005PI",
};
const size_t PI30_READ_COMMANDS_N = sizeof(PI30_READ_COMMANDS) / sizeof(PI30_READ_COMMANDS[0]);

static bool allDigits(const char* s, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (s[i] < '0' || s[i] > '9') return false;
  return s[n] == 0;
}
static int num(const char* s, size_t n) {
  int v = 0;
  for (size_t i = 0; i < n; i++) v = v * 10 + (s[i] - '0');
  return v;
}
static bool validYear(const char* s) { int y = num(s, 4); return y >= 2000 && y <= 2099; }
static bool validMonth(const char* s) { int m = num(s, 2); return m >= 1 && m <= 12; }
static bool validDay(const char* s) { int d = num(s, 2); return d >= 1 && d <= 31; }

// QEDyyyymmdd, QLDyyyymmdd (energy reads with a date argument; PMAX §2.23-2.30). QEM/QEY removed (unused).
static bool dateQueryAllowed(const char* c) {
  size_t n = strlen(c);
  if (n == 11 && (!strncmp(c, "QED", 3) || !strncmp(c, "QLD", 3)) && allDigits(c + 3, 8))
    return validYear(c + 3) && validMonth(c + 7) && validDay(c + 9);
  return false;
}

bool cmdAllowed(const char* cmd, Framing f) {
  if (!cmd || !*cmd || strlen(cmd) > 16) return false;
  switch (f) {
    case Framing::CRC:
      for (size_t i = 0; i < PI30_READ_COMMANDS_N; i++)
        if (!strcmp(cmd, PI30_READ_COMMANDS[i])) return true;
      return dateQueryAllowed(cmd);
    case Framing::CHK:
      return !strcmp(cmd, "QALL") || !strcmp(cmd, "PQSE") || !strcmp(cmd, "PQSA") || !strcmp(cmd, "QLITH0");
    case Framing::NONE:
      return !strcmp(cmd, "Q1") || !strcmp(cmd, "Q6");
  }
  return false;
}

bool modbusAllowed(const uint8_t* f, size_t n) {
  if (!f || n != 8) return false;
  if (f[0] < 1 || f[0] > 247) return false;
  if (f[1] != 0x03) return false;  // FC 03 only
  uint16_t qty = (uint16_t)((uint16_t)f[4] << 8 | f[5]);
  if (qty < 1 || qty > 125) return false;
  return modbus::crcOk(f, n);
}

bool frameAllowed(const uint8_t* frame, size_t n, Framing f) {
  if (!frame || n < 2 || n > 24 || frame[n - 1] != 0x0D) return false;
  char cmd[20];
  size_t clen;
  switch (f) {
    case Framing::CRC: {
      if (n < 4) return false;
      clen = n - 3;
      uint16_t c = pi30::crc(frame, clen);
      if (frame[clen] != (uint8_t)(c >> 8) || frame[clen + 1] != (uint8_t)(c & 0xFF)) return false;
      break;
    }
    case Framing::CHK:
      if (n < 3) return false;
      clen = n - 2;
      if (frame[clen] != revo::chk(frame, clen)) return false;
      break;
    case Framing::NONE:
      clen = n - 1;
      break;
    default:
      return false;
  }
  if (clen == 0 || clen >= sizeof(cmd)) return false;
  for (size_t i = 0; i < clen; i++) {
    if (frame[i] < 0x20 || frame[i] > 0x7E) return false;  // command text must be printable ASCII
    cmd[i] = (char)frame[i];
  }
  cmd[clen] = 0;
  return cmdAllowed(cmd, f);
}

}  // namespace wl
