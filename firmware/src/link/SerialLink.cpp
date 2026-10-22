// SerialLink.cpp - see SerialLink.h. Host-portable.
#include "link/SerialLink.h"

#include <stdio.h>
#include <string.h>

#include "proto/modbus/ModbusRtu.h"
#include "proto/pi30/Pi30Frame.h"
#include "proto/pi30/RevoChk.h"

const char* linkResultName(LinkResult r) {
  switch (r) {
    case LinkResult::OK: return "OK";
    case LinkResult::NAK: return "NAK";
    case LinkResult::TIMEOUT: return "TIMEOUT";
    case LinkResult::BAD_START: return "BAD_START";
    case LinkResult::BAD_CRC: return "BAD_CRC";
    case LinkResult::RX_OVERFLOW: return "RX_OVERFLOW";
    case LinkResult::REFUSED: return "REFUSED";
    case LinkResult::NOT_ARMED: return "NOT_ARMED";
    case LinkResult::NO_CRC_OK: return "NO_CRC_OK";
    case LinkResult::LEGACY_HASH: return "LEGACY_HASH";
  }
  return "?";
}

static void resetReply(Reply& r) {
  r.res = LinkResult::TIMEOUT;
  r.rawLen = 0;
  r.payload[0] = 0;
  r.elapsedMs = 0;
  r.bytesSent = 0;
}

static bool chkCapable(const char* cmd) {
  return !strcmp(cmd, "QALL") || !strcmp(cmd, "PQSE") || !strcmp(cmd, "PQSA") || !strcmp(cmd, "QLITH0");
}

void SerialLink::configure(const LinkProfile& p) {
  prof_ = p;
  port_.begin(prof_, armed_);
  configured_ = true;
  // drop whatever the line produced while reconfiguring
  while (port_.available() > 0) port_.read();
}

void SerialLink::setArmed(bool armed) {
  armed_ = armed;
  if (configured_) port_.begin(prof_, armed_);
}

void SerialLink::release() {
  port_.end();
  configured_ = false;
}

void SerialLink::count(LinkResult r) {
  switch (r) {
    case LinkResult::OK:
    case LinkResult::NO_CRC_OK:
      stats_.ok++;
      stats_.lastOkMs = clk_.ms();
      stats_.everOk = true;
      break;
    case LinkResult::NAK: stats_.nak++; break;
    case LinkResult::TIMEOUT: stats_.timeouts++; break;
    case LinkResult::BAD_START: stats_.badStart++; break;
    case LinkResult::BAD_CRC: stats_.badCrc++; break;
    case LinkResult::RX_OVERFLOW: stats_.overflow++; break;
    case LinkResult::REFUSED: stats_.refused++; break;
    case LinkResult::NOT_ARMED: stats_.notArmed++; break;
    default: break;
  }
}

void SerialLink::gap() {
  uint32_t since = (uint32_t)(clk_.ms() - lastTxEndMs_);
  if (lastTxEndMs_ != 0 && since < cfg::INTER_CMD_GAP_MS) clk_.sleepMs(cfg::INTER_CMD_GAP_MS - since);
}

LinkResult SerialLink::query(const char* cmd, Framing f, Reply& out, uint32_t firstByteMs) {
  resetReply(out);
  // Gate 2 (whitelist) is checked first so a setter is refused even on an armed link.
  if (!wl::cmdAllowed(cmd, f)) {
    out.res = LinkResult::REFUSED;
    count(out.res);
    ring_.add(clk_.ms(), cmd ? cmd : "?", (uint8_t)out.res, nullptr, 0, nullptr, 0);
    return out.res;
  }
  // Gate 1 (armed)
  if (!armed_ || !configured_) {
    out.res = LinkResult::NOT_ARMED;
    count(out.res);
    return out.res;
  }
  uint8_t frame[24];
  size_t n = 0;
  if (f == Framing::CRC) n = pi30::buildCrcFrame(cmd, frame, sizeof frame);
  else if (f == Framing::CHK) n = revo::buildChkFrame(cmd, frame, sizeof frame);
  else n = pi30::buildPlainFrame(cmd, frame, sizeof frame);
  // Gate 3: re-parse the exact bytes about to be sent.
  if (n == 0 || !wl::frameAllowed(frame, n, f)) {
    out.res = LinkResult::REFUSED;
    count(out.res);
    return out.res;
  }
  LinkResult r = transact(cmd, frame, n, out, firstByteMs, 0, true, chkCapable(cmd) ? cfg::CHK_LINGER_MS : 0);
  if (r == LinkResult::OK || r == LinkResult::BAD_CRC || r == LinkResult::NO_CRC_OK) {
    // Validate the PI30 reply according to the framing used.
    // locate the frame start (first '(' / '#' / '^' after any echo)
    size_t start = 0;
    if (out.rawLen >= n && memcmp(out.raw, frame, n) == 0) start = n;  // exact echo of our request
    while (start < out.rawLen && out.raw[start] != '(' && out.raw[start] != '#' && out.raw[start] != '^') start++;
    const uint8_t* fr = out.raw + start;
    size_t flen = out.rawLen - start;
    // trim to the last terminator
    while (flen > 0 && !pi30::isTerm(fr[flen - 1])) flen--;
    if (flen == 0) {
      r = start < out.rawLen ? LinkResult::BAD_CRC : LinkResult::BAD_START;
    } else if (fr[0] == '#') {
      size_t pl = flen - 1;
      if (pl >= sizeof out.payload) pl = sizeof out.payload - 1;
      memcpy(out.payload, fr, pl);
      out.payload[pl] = 0;
      r = LinkResult::LEGACY_HASH;
    } else if (fr[0] == '^') {
      // PI18 reply "^D00518...<CRC><cr>" - detection only.
      uint16_t rx = flen >= 4 ? (uint16_t)((uint16_t)fr[flen - 3] << 8 | fr[flen - 2]) : 0;
      bool crcok = flen >= 4 && (rx == pi30::crc(fr, flen - 3) || rx == pi30::crcZ(fr, flen - 3));
      size_t pl = crcok ? flen - 3 : flen - 1;
      if (pl >= sizeof out.payload) pl = sizeof out.payload - 1;
      memcpy(out.payload, fr, pl);
      out.payload[pl] = 0;
      r = crcok ? LinkResult::OK : LinkResult::NO_CRC_OK;
    } else {
      // Use only up to the FIRST terminator for CRC frames; CHK frames may carry CHK==CR (linger).
      size_t firstTerm = 0;
      while (firstTerm < flen && !pi30::isTerm(fr[firstTerm])) firstTerm++;
      size_t crcLen = firstTerm + 1;
      pi30::Check c = pi30::checkCrcReply(fr, crcLen, out.payload, sizeof out.payload, false);
      if (c != pi30::Check::OK && c != pi30::Check::NAK && chkCapable(cmd)) {
        size_t chkLen = flen > crcLen + 1 ? crcLen + 1 : flen;  // at most one extra lingering byte
        pi30::Check c2 = revo::checkChkReply(fr, chkLen, out.payload, sizeof out.payload);
        if (c2 == pi30::Check::OK) c = c2;
      }
      if (c != pi30::Check::OK && c != pi30::Check::NAK && f == Framing::NONE)
        c = pi30::checkCrcReply(fr, crcLen, out.payload, sizeof out.payload, true);
      switch (c) {
        case pi30::Check::OK: r = LinkResult::OK; break;
        case pi30::Check::NO_CRC_OK: r = LinkResult::NO_CRC_OK; break;
        case pi30::Check::NAK: r = LinkResult::NAK; break;
        case pi30::Check::BAD_START: r = LinkResult::BAD_START; break;
        default: r = LinkResult::BAD_CRC; break;
      }
    }
  }
  out.res = r;
  count(r);
  ring_.add(clk_.ms(), cmd, (uint8_t)r, frame, n, out.raw, out.rawLen);
  return r;
}

LinkResult SerialLink::queryModbus(const uint8_t* frame, size_t n, size_t expectLen, Reply& out,
                                   uint32_t firstByteMs) {
  resetReply(out);
  char tag[14];
  snprintf(tag, sizeof tag, "MB%02X-%02X%02X", frame && n > 0 ? frame[0] : 0, n > 2 ? frame[2] : 0, n > 3 ? frame[3] : 0);
  if (!wl::modbusAllowed(frame, n)) {
    out.res = LinkResult::REFUSED;
    count(out.res);
    ring_.add(clk_.ms(), tag, (uint8_t)out.res, nullptr, 0, nullptr, 0);
    return out.res;
  }
  if (!armed_ || !configured_) {
    out.res = LinkResult::NOT_ARMED;
    count(out.res);
    return out.res;
  }
  LinkResult r = transact(tag, frame, n, out, firstByteMs, expectLen, false, 0);
  if (r == LinkResult::OK) {
    // Skip an exact echo of our request if present (RS485-style wiring).
    size_t off = 0;
    if (out.rawLen >= n && memcmp(out.raw, frame, n) == 0) {
      off = n;
      stats_.echoDropped++;
    }
    if (off) {
      memmove(out.raw, out.raw + off, out.rawLen - off);
      out.rawLen = (uint16_t)(out.rawLen - off);
    }
    if (out.rawLen < 5) r = out.rawLen ? LinkResult::BAD_START : LinkResult::TIMEOUT;
    else if (out.raw[0] != frame[0]) r = LinkResult::BAD_START;
    else {
      size_t L = out.raw[1] == 0x83 ? 5 : (expectLen && out.rawLen >= expectLen ? expectLen : out.rawLen);
      r = modbus::crcOk(out.raw, L) ? LinkResult::OK : LinkResult::BAD_CRC;
    }
  }
  out.res = r;
  count(r);
  ring_.add(clk_.ms(), tag, (uint8_t)r, frame, n, out.raw, out.rawLen);
  return r;
}

LinkResult SerialLink::transact(const char* tag, const uint8_t* frame, size_t n, Reply& out, uint32_t firstByteMs,
                                size_t expectLen, bool pi30Reply, uint32_t lingerMs) {
  (void)tag;
  gap();
  while (port_.available() > 0) port_.read();  // flush stale RX before sending
  uint32_t t0 = clk_.ms();
  size_t w = port_.write(frame, n);
  port_.flushTx();
  stats_.txFrames++;
  stats_.bytesSent += (uint32_t)w;
  out.bytesSent = (uint16_t)w;
  readReply(out, firstByteMs, expectLen, pi30Reply, lingerMs, frame, n);
  lastTxEndMs_ = clk_.ms();
  out.elapsedMs = lastTxEndMs_ - t0;
  return out.res;
}

void SerialLink::readReply(Reply& out, uint32_t firstByteMs, size_t expectLen, bool pi30Reply, uint32_t lingerMs,
                           const uint8_t* sent, size_t sentLen) {
  uint32_t start = clk_.ms(), last = start;
  bool got = false, started = false, termSeen = false;
  size_t echoIdx = 0;
  bool echoing = pi30Reply;
  out.rawLen = 0;
  for (;;) {
    int c = port_.read();
    if (c >= 0) {
      if (out.rawLen >= sizeof out.raw) {
        out.res = LinkResult::RX_OVERFLOW;
        return;
      }
      uint8_t b = (uint8_t)c;
      out.raw[out.rawLen++] = b;
      last = clk_.ms();
      got = true;
      if (pi30Reply) {
        if (!started) {
          if (echoing && echoIdx < sentLen && b == sent[echoIdx]) {
            echoIdx++;  // still matching our own request (echo)
            if (echoIdx == sentLen) { stats_.echoDropped++; echoing = false; }
            continue;
          }
          echoing = false;
          if (b == '(' || b == '#' || b == '^') started = true;
        } else if (pi30::isTerm(b)) {
          if (termSeen || lingerMs == 0) {
            out.res = LinkResult::OK;
            return;
          }
          termSeen = true;
        }
      } else if (expectLen && out.rawLen >= expectLen) {
        out.res = LinkResult::OK;
        return;
      }
    } else {
      uint32_t now = clk_.ms();
      if (!got && (uint32_t)(now - start) > firstByteMs) {
        out.res = LinkResult::TIMEOUT;
        return;
      }
      if (termSeen && (uint32_t)(now - last) >= lingerMs) {
        out.res = LinkResult::OK;
        return;
      }
      if (got && (uint32_t)(now - last) > cfg::INTER_BYTE_MS) {
        // silence ends the frame
        if (pi30Reply) out.res = started ? (termSeen ? LinkResult::OK : LinkResult::BAD_CRC) : LinkResult::BAD_START;
        else out.res = LinkResult::OK;
        return;
      }
      if ((uint32_t)(now - start) > firstByteMs + cfg::FRAME_MAX_MS) {
        out.res = pi30Reply ? (started ? LinkResult::BAD_CRC : LinkResult::BAD_START) : LinkResult::OK;
        return;
      }
      clk_.sleepMs(2);
    }
  }
}

size_t SerialLink::listen(uint32_t ms, uint8_t* buf, size_t cap, uint32_t* totalBytes) {
  size_t n = 0;
  uint32_t total = 0;
  if (!configured_) {
    if (totalBytes) *totalBytes = 0;
    return 0;
  }
  uint32_t start = clk_.ms();
  while ((uint32_t)(clk_.ms() - start) < ms) {
    int c;
    bool any = false;
    while ((c = port_.read()) >= 0) {
      any = true;
      total++;
      if (buf && n < cap) buf[n++] = (uint8_t)c;
    }
    if (!any) clk_.sleepMs(5);
  }
  if (totalBytes) *totalBytes = total;
  if (total) ring_.add(clk_.ms(), "LISTEN", (uint8_t)LinkResult::OK, nullptr, 0, buf, n);
  return n;
}

SerialLink::LoopbackResult SerialLink::loopbackTest(bool benchConfirmed) {
  LoopbackResult res;
  if (!benchConfirmed) {
    snprintf(res.detail, sizeof res.detail, "refused: confirm GPIO%u<->GPIO%u jumper, module+inverter disconnected",
             (unsigned)prof_.txPin, (unsigned)prof_.roPin);
    return res;
  }
  if (armed_) {
    snprintf(res.detail, sizeof res.detail, "refused: link is armed (inverter wired); run reset-link first");
    return res;
  }
  uint8_t frame[24];
  size_t n = pi30::buildCrcFrame("QPI", frame, sizeof frame);
  if (!n || !wl::frameAllowed(frame, n, Framing::CRC)) {
    snprintf(res.detail, sizeof res.detail, "internal: QPI frame failed the whitelist");
    return res;
  }
  res.ran = true;
  for (int pass = 0; pass < 2; pass++) {
    bool inv = pass == 0;
    LinkProfile lp = prof_;
    lp.rxInvert = lp.txInvert = inv;
    port_.begin(lp, /*txEnabled=*/true);  // bench only: TX attached for < 1 s
    clk_.sleepMs(20);
    while (port_.available() > 0) port_.read();
    port_.write(frame, n);
    port_.flushTx();
    stats_.txFrames++;
    stats_.bytesSent += (uint32_t)n;
    uint8_t rx[32];
    size_t got = 0;
    uint32_t t0 = clk_.ms();
    while (got < n && (uint32_t)(clk_.ms() - t0) < 300) {
      int c = port_.read();
      if (c >= 0) rx[got++] = (uint8_t)c;
      else clk_.sleepMs(2);
    }
    bool ok = got == n && memcmp(rx, frame, n) == 0;
    if (inv) res.invertedOk = ok;
    else res.normalOk = ok;
    ring_.add(clk_.ms(), inv ? "LOOP-INV" : "LOOP-NORM", ok ? 0 : (uint8_t)LinkResult::BAD_CRC, frame, n, rx, got);
  }
  port_.begin(prof_, armed_);  // restore: TX detached again (not armed)
  snprintf(res.detail, sizeof res.detail, "inverted %s, non-inverted %s", res.invertedOk ? "byte-exact" : "FAILED",
           res.normalOk ? "byte-exact" : "FAILED");
  return res;
}
