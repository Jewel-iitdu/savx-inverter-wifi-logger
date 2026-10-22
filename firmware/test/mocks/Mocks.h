// Mocks.h - host test doubles: virtual clock, GPIO, and a simulated PI30-family inverter on the UART.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "link/Port.h"
#include "link/Whitelist.h"
#include "proto/modbus/ModbusRtu.h"
#include "proto/pi30/Pi30Frame.h"
#include "proto/pi30/RevoChk.h"

struct MockClock : IClock {
  uint32_t t = 1000;
  uint32_t ms() override { return t; }
  void sleepMs(uint32_t d) override { t += d ? d : 1; }
};

struct MockGpio : IGpio {
  std::map<int, int> levels;              // fixed level per pin
  std::function<int(int, uint32_t)> fn;    // optional: level(pin, timeMs)
  MockClock* clk = nullptr;
  std::map<int, GpioMode> modes;
  std::vector<std::pair<int, GpioMode>> modeLog;  // every mode change, in order
  void mode(int pin, GpioMode m) override { modes[pin] = m; modeLog.push_back({pin, m}); }
  int level(int pin) override {
    if (fn) return fn(pin, clk ? clk->t : 0);
    auto it = levels.find(pin);
    return it == levels.end() ? 1 : it->second;
  }
};

// Simulated inverter behind the RS232-lite link. It answers only when the UART profile matches the
// "true" one (default 2400 baud, RX+TX inverted) and the TX pin is attached (link armed).
struct SimInverter : IByteStream {
  MockClock& clk;
  explicit SimInverter(MockClock& c) : clk(c) {}

  // configuration of the simulated device
  uint32_t baud = 2400;
  bool rxInv = true, txInv = true;
  std::function<std::string(const std::string& cmd, Framing f)> respond;  // payload incl. '(' or "" = silence
  std::function<std::vector<uint8_t>(const std::vector<uint8_t>& req)> respondModbus;  // raw reply or empty
  bool chkReplyFor(const std::string& cmd) { return cmd == "QALL" || cmd == "PQSE" || cmd == "PQSA"; }
  uint32_t latencyMs = 60;
  std::vector<uint8_t> foreignNoise;  // bytes "on the line" during listening

  // observation
  std::vector<uint8_t> written;
  std::vector<std::string> commands;
  int unsafeWrites = 0;
  int beginCalls = 0;
  bool txEnabled = false;
  LinkProfile prof;

  // RX queue
  std::vector<uint8_t> rx;
  uint32_t rxReadyAt = 0;

  void begin(const LinkProfile& p, bool txEn) override {
    prof = p;
    txEnabled = txEn;
    beginCalls++;
    rx.clear();
  }
  void end() override { txEnabled = false; }
  uint32_t nextNoiseAt = 0;
  void noise() {  // a foreign master keeps talking every 500 ms
    if (!foreignNoise.empty() && rx.empty() && clk.t >= nextNoiseAt) {
      rx = foreignNoise;
      rxReadyAt = clk.t;
      nextNoiseAt = clk.t + 500;
    }
  }
  int available() override { noise(); return (clk.t >= rxReadyAt) ? (int)rx.size() : 0; }
  int read() override {
    noise();
    if (rx.empty() || clk.t < rxReadyAt) return -1;
    uint8_t b = rx.front();
    rx.erase(rx.begin());
    return b;
  }
  void flushTx() override {}
  bool profileOk() const { return prof.baud == baud && prof.rxInvert == rxInv && prof.txInvert == txInv; }

  size_t write(const uint8_t* b, size_t n) override {
    if (!txEnabled) { unsafeWrites += 1000; return 0; }  // writing with TX detached is a bug
    std::vector<uint8_t> f(b, b + n);
    written.insert(written.end(), b, b + n);
    bool safe = wl::frameAllowed(b, n, Framing::CRC) || wl::frameAllowed(b, n, Framing::CHK) ||
                wl::frameAllowed(b, n, Framing::NONE) || wl::modbusAllowed(b, n);
    if (!safe) unsafeWrites++;
    if (!profileOk()) return n;  // wrong polarity/baud: the inverter hears garbage and stays silent
    // Modbus?
    if (n == 8 && b[1] == 0x03 && modbus::crcOk(b, n)) {
      commands.push_back("MODBUS");
      if (respondModbus) {
        auto r = respondModbus(f);
        if (!r.empty()) { rx = r; rxReadyAt = clk.t + latencyMs; }
      }
      return n;
    }
    // decode PI30 command text
    std::string cmd;
    Framing fr = Framing::NONE;
    if (n >= 4 && wl::frameAllowed(b, n, Framing::CRC)) { cmd.assign((const char*)b, n - 3); fr = Framing::CRC; }
    else if (n >= 3 && wl::frameAllowed(b, n, Framing::CHK)) { cmd.assign((const char*)b, n - 2); fr = Framing::CHK; }
    else { cmd.assign((const char*)b, n - 1); fr = Framing::NONE; }
    commands.push_back(cmd);
    if (!respond) return n;
    std::string payload = respond(cmd, fr);
    if (payload.empty()) return n;
    std::vector<uint8_t> out(payload.begin(), payload.end());
    if (payload[0] == '#') {
      out.push_back(0x0D);
    } else if (fr == Framing::NONE) {
      out.push_back(0x0D);
    } else if (chkReplyFor(cmd)) {
      out.push_back(revo::chk(out.data(), out.size()));
      out.push_back(0x0D);
    } else {
      uint16_t c = pi30::crc(out.data(), out.size());
      out.push_back((uint8_t)(c >> 8));
      out.push_back((uint8_t)(c & 0xFF));
      out.push_back(0x0D);
    }
    rx = out;
    rxReadyAt = clk.t + latencyMs;
    return n;
  }
};

// Build a full CRC-framed reply "(payload<CRC>\r"
inline std::vector<uint8_t> crcReply(const std::string& payloadWithParen) {
  std::vector<uint8_t> v(payloadWithParen.begin(), payloadWithParen.end());
  uint16_t c = pi30::crc(v.data(), v.size());
  v.push_back((uint8_t)(c >> 8));
  v.push_back((uint8_t)(c & 0xFF));
  v.push_back(0x0D);
  return v;
}
