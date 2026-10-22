// SerialLink.h - the ONLY code that can transmit to the inverter (d1 §3.2, §3.7, §3.10). Host-portable.
//
// Safety gates, in order, for every transmission:
//   1. armed_ must be true (NVS "link/armed", set only by the explicit arm procedure after the idle-level test).
//      A fresh device is NOT armed: the UART TX pin is never attached and GPIO17 stays a high-Z input.
//   2. The command must be in the exact-match whitelist (wl::cmdAllowed / wl::modbusAllowed).
//   3. The fully built frame is re-parsed and re-checked (wl::frameAllowed) right before write().
// A refused query returns REFUSED / NOT_ARMED and writes ZERO bytes (unit-tested).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "link/LinkProfile.h"
#include "link/Port.h"
#include "link/RawRing.h"
#include "link/Whitelist.h"

enum class LinkResult : uint8_t {
  OK = 0,       // valid CRC/CHK frame (or a valid Modbus response)
  NAK,          // "(NAK"
  TIMEOUT,      // no first byte within the timeout
  BAD_START,    // bytes received, but not a '(' frame (garbage, wrong polarity, echo only)
  BAD_CRC,      // '(' frame with wrong CRC/CHK
  RX_OVERFLOW,  // more than RX_BUF bytes
  REFUSED,      // not whitelisted: nothing sent
  NOT_ARMED,    // link not armed: nothing sent
  NO_CRC_OK,    // accepted "(..." frame without CRC (legacy Q1/Q6, PI18 detection)
  LEGACY_HASH,  // "#-1" style reply of legacy RP/RS firmware to a PI30 command
};
const char* linkResultName(LinkResult r);

struct Reply {
  LinkResult res = LinkResult::TIMEOUT;
  uint16_t rawLen = 0;
  uint8_t raw[cfg::RX_BUF];
  char payload[cfg::RX_BUF];  // after '(' without CRC/CHK/terminator, NUL-terminated (PI30 replies)
  uint32_t elapsedMs = 0;
  uint16_t bytesSent = 0;
  bool ok() const { return res == LinkResult::OK || res == LinkResult::NO_CRC_OK; }
};

struct LinkStats {
  uint32_t txFrames = 0, bytesSent = 0, ok = 0, nak = 0, timeouts = 0, badStart = 0, badCrc = 0, overflow = 0;
  uint32_t refused = 0, notArmed = 0, echoDropped = 0;
  uint32_t lastOkMs = 0;
  bool everOk = false;
};

class SerialLink {
 public:
  SerialLink(IByteStream& port, IClock& clk) : port_(port), clk_(clk) {}

  // (Re)configure the UART. TX is attached only if armed. Safe to call repeatedly.
  void configure(const LinkProfile& p);
  void setArmed(bool armed);
  bool armed() const { return armed_; }
  const LinkProfile& profile() const { return prof_; }
  void release();  // detach the UART (for pin diagnostics)

  // PI30-family query: builds the frame (CRC/CHK/NONE), applies all gates, sends, reads one reply.
  LinkResult query(const char* cmd, Framing f, Reply& out, uint32_t firstByteMs = cfg::FIRST_BYTE_MS);
  // Modbus FC03 read: frame must pass wl::modbusAllowed. Reads up to expectLen bytes (or until silence).
  LinkResult queryModbus(const uint8_t* frame, size_t n, size_t expectLen, Reply& out,
                         uint32_t firstByteMs = cfg::FIRST_BYTE_MS);
  // Receive-only: collect bytes for ms milliseconds. Never transmits. Returns the number of bytes stored.
  size_t listen(uint32_t ms, uint8_t* buf, size_t cap, uint32_t* totalBytes = nullptr);

  // Bench loopback (d1 M1): GPIO17 jumpered to the RO pin, module and inverter DISCONNECTED.
  // Requires benchConfirmed == true and the link NOT armed; sends only the whitelisted QPI frame.
  struct LoopbackResult { bool ran = false, invertedOk = false, normalOk = false; char detail[96] = ""; };
  LoopbackResult loopbackTest(bool benchConfirmed);

  bool linkOk(uint32_t nowMs) const {
    return stats_.everOk && (uint32_t)(nowMs - stats_.lastOkMs) < cfg::LINK_OK_WINDOW_MS;
  }
  const LinkStats& stats() const { return stats_; }
  RawRing& ring() { return ring_; }
  const RawRing& ring() const { return ring_; }
  IClock& clock() { return clk_; }

 private:
  LinkResult transact(const char* tag, const uint8_t* frame, size_t n, Reply& out, uint32_t firstByteMs,
                      size_t expectLen, bool pi30Reply, uint32_t lingerMs);
  void readReply(Reply& out, uint32_t firstByteMs, size_t expectLen, bool pi30Reply, uint32_t lingerMs,
                 const uint8_t* sent, size_t sentLen);
  void gap();
  void count(LinkResult r);

  IByteStream& port_;
  IClock& clk_;
  LinkProfile prof_;
  bool armed_ = false;
  bool configured_ = false;
  uint32_t lastTxEndMs_ = 0;
  LinkStats stats_;
  RawRing ring_;
};
