// Whitelist.h - the read-only command gate (d1 §3.10 layer 2, V1 §6.0). Host-portable.
//
// Every byte sequence the firmware can ever put on the inverter's RX line must pass wl::frameAllowed().
// Matching is EXACT (never by prefix), so PQSE/PQSA (REVO reads) are allowed while PSET/POP/PF/... are not.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum class Framing : uint8_t { CRC = 0, CHK = 1, NONE = 2 };

inline const char* framingName(Framing f) {
  return f == Framing::CRC ? "crc" : f == Framing::CHK ? "chk" : "none";
}

namespace wl {

// The exact PI30-family read commands allowed with the given framing:
//   CRC : QPI QID QSID QVFW QVFW2 QVFW3 QMN QGMN QPIRI QPIGS QPIGS2 QMOD QPIWS QFLAG QDI Q1 QT QET
//         QLITH0 QALL QEDyyyymmdd QLDyyyymmdd ^P005PI
//         (QMCHGCR QMUCHGCR QBEQI QLT QEMyyyymm QEYyyyy removed by the safety review: unused - see Whitelist.cpp)
//   CHK : QALL PQSE PQSA QLITH0          (Sorotec REVO sum+1 framing)
//   NONE: Q1 Q6                           (legacy RP/RS-series, no CRC)
bool cmdAllowed(const char* cmd, Framing f);

// Modbus RTU: function 0x03 (read holding registers) ONLY, exactly 8 bytes, valid CRC-16/MODBUS,
// slave 1..247, quantity 1..125. FC 06/10, 8851 frames and everything else are refused.
bool modbusAllowed(const uint8_t* frame, size_t n);

// Defence in depth: re-parse a fully built frame (command + CRC/CHK + CR) and re-check it.
bool frameAllowed(const uint8_t* frame, size_t n, Framing f);

// For tests/diagnostics: the list of plain allowed CRC commands (without the date-argument ones).
extern const char* const PI30_READ_COMMANDS[];
extern const size_t PI30_READ_COMMANDS_N;

}  // namespace wl
