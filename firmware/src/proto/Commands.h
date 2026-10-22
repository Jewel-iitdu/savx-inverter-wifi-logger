// Commands.h - command identifiers used by the scheduler and the supported-command bitmap. Host-portable.
#pragma once
#include <stdint.h>
#include <string.h>

#include "link/Whitelist.h"

enum class Cmd : uint8_t {
  QPI = 0, QID, QSID, QVFW, QVFW2, QVFW3, QMN, QGMN, QPIRI, QPIGS, QPIGS2, QMOD, QPIWS, QFLAG, QDI,
  Q1, QT, QET, QED, QLD, QLITH0, QALL, PQSE, PQSA,
  COUNT
};

struct CmdInfo {
  const char* name;   // QED/QLD get a yyyymmdd suffix at run time
  Framing framing;    // default framing (QALL/QLITH0 may switch, see Pi30Adapter)
  bool optional;      // tried once in the probe; NAK/timeout -> unsupported, re-tried daily
};

static const CmdInfo CMD_INFO[(int)Cmd::COUNT] = {
    {"QPI", Framing::CRC, false},   {"QID", Framing::CRC, true},    {"QSID", Framing::CRC, true},
    {"QVFW", Framing::CRC, true},   {"QVFW2", Framing::CRC, true},  {"QVFW3", Framing::CRC, true},
    {"QMN", Framing::CRC, true},    {"QGMN", Framing::CRC, true},   {"QPIRI", Framing::CRC, false},
    {"QPIGS", Framing::CRC, false}, {"QPIGS2", Framing::CRC, true}, {"QMOD", Framing::CRC, false},
    {"QPIWS", Framing::CRC, true},  {"QFLAG", Framing::CRC, true},  {"QDI", Framing::CRC, true},
    {"Q1", Framing::CRC, true},     {"QT", Framing::CRC, true},     {"QET", Framing::CRC, true},
    {"QED", Framing::CRC, true},    {"QLD", Framing::CRC, true},    {"QLITH0", Framing::CRC, true},
    {"QALL", Framing::CHK, true},   {"PQSE", Framing::CHK, true},   {"PQSA", Framing::CHK, true},
};

inline const char* cmdName(Cmd c) { return (int)c < (int)Cmd::COUNT ? CMD_INFO[(int)c].name : "?"; }
inline uint32_t cmdBit(Cmd c) { return 1UL << (int)c; }
inline bool cmdFromName(const char* n, Cmd& out) {
  for (int i = 0; i < (int)Cmd::COUNT; i++)
    if (!strcmp(CMD_INFO[i].name, n)) { out = (Cmd)i; return true; }
  return false;
}
