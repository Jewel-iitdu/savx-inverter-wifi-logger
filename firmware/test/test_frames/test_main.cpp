// test_frames - byte-exact frames from research/v1-protocol-verification.md §4.4, CRC/CHK/Modbus CRC,
// the read-only whitelist and the SerialLink safety gates (0 bytes sent on refusal / when not armed).
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "link/SerialLink.h"
#include "link/Whitelist.h"
#include "mocks/Mocks.h"
#include "proto/Commands.h"
#include "proto/modbus/ModbusRtu.h"
#include "proto/pi30/Pi30Frame.h"
#include "proto/pi30/RevoChk.h"

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> v;
  while (*s) {
    while (*s == ' ') s++;
    if (!*s) break;
    unsigned b;
    sscanf(s, "%2x", &b);
    v.push_back((uint8_t)b);
    s += 2;
  }
  return v;
}

static void expectCrcFrame(const char* cmd, const char* wire) {
  uint8_t out[32];
  size_t n = pi30::buildCrcFrame(cmd, out, sizeof out);
  std::vector<uint8_t> exp = hex(wire);
  char msg[64];
  snprintf(msg, sizeof msg, "frame for %s", cmd);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(exp.size(), n, msg);
  TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(exp.data(), out, n, msg);
}

// ---- V1 §4.4 PI30 requests (ASCII + CRC-hi + CRC-lo + 0D)
void test_pi30_request_frames() {
  expectCrcFrame("QPI", "51 50 49 BE AC 0D");
  expectCrcFrame("QPIGS", "51 50 49 47 53 B7 A9 0D");
  expectCrcFrame("QPIRI", "51 50 49 52 49 F8 54 0D");
  expectCrcFrame("QMOD", "51 4D 4F 44 49 C1 0D");
  expectCrcFrame("QPIWS", "51 50 49 57 53 B4 DA 0D");
  expectCrcFrame("QFLAG", "51 46 4C 41 47 98 74 0D");
  expectCrcFrame("QID", "51 49 44 D6 EA 0D");
  expectCrcFrame("QSID", "51 53 49 44 BB 05 0D");
  expectCrcFrame("QVFW", "51 56 46 57 62 99 0D");
  expectCrcFrame("QVFW2", "51 56 46 57 32 C3 F5 0D");
  expectCrcFrame("QDI", "51 44 49 71 1B 0D");
  expectCrcFrame("QMN", "51 4D 4E BB 64 0D");
  expectCrcFrame("QGMN", "51 47 4D 4E 49 29 0D");     // raw 0x4928, low byte 0x28 bumped
  expectCrcFrame("QBOOT", "51 42 4F 4F 54 0B 88 0D");  // raw 0x0A88 bumped (not whitelisted; CRC only)
  expectCrcFrame("QT", "51 54 27 FF 0D");
  expectCrcFrame("QET", "51 45 54 81 B6 0D");
  expectCrcFrame("Q1", "51 31 1B FC 0D");
  expectCrcFrame("QPIGS2", "51 50 49 47 53 32 68 2D 0D");
  expectCrcFrame("QPGS0", "51 50 47 53 30 3F DA 0D");
  expectCrcFrame("QLITH0", "51 4C 49 54 48 30 02 A4 0D");
  expectCrcFrame("^P005PI", "5E 50 30 30 35 50 49 71 8B 0D");
  expectCrcFrame("QALL", "51 41 4C 4C E8 FB 0D");  // Solar2MQTT CRC variant
}

void test_dated_and_pi18_trailers() {
  uint8_t out[32];
  size_t n = pi30::buildCrcFrame("QED20261021", out, sizeof out);
  TEST_ASSERT_EQUAL(14, n);
  TEST_ASSERT_EQUAL_HEX8(0x95, out[11]);
  TEST_ASSERT_EQUAL_HEX8(0xD0, out[12]);
  n = pi30::buildCrcFrame("QLY2026", out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(0xBF, out[7]);  // raw 0xBF0D -> low byte bumped to 0x0E
  TEST_ASSERT_EQUAL_HEX8(0x0E, out[8]);
  n = pi30::buildCrcFrame("^P007PIRI", out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(0xEE, out[n - 3]);
  TEST_ASSERT_EQUAL_HEX8(0x38, out[n - 2]);
  n = pi30::buildCrcFrame("^P005GS", out, sizeof out);
  TEST_ASSERT_EQUAL_HEX8(0x58, out[n - 3]);
  TEST_ASSERT_EQUAL_HEX8(0x14, out[n - 2]);
}

static void expectReplyCrc(const char* payload, uint8_t hi, uint8_t lo) {
  uint16_t c = pi30::crc((const uint8_t*)payload, strlen(payload));
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(hi, c >> 8, payload);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(lo, c & 0xFF, payload);
}

void test_reply_trailers() {
  expectReplyCrc("(PI30", 0x9A, 0x0B);
  expectReplyCrc("(NAK", 0x73, 0x73);
  expectReplyCrc("(ACK", 0x39, 0x20);
  expectReplyCrc("(B", 0xE7, 0xC9);
  expectReplyCrc("(S", 0xE5, 0xD9);
  expectReplyCrc("(L", 0x06, 0x07);
  expectReplyCrc("(F", 0xA7, 0x4D);
  expectReplyCrc("(H", 0x46, 0x83);
  expectReplyCrc("(P", 0xD5, 0xBA);
  expectReplyCrc("(VERFW:00014.04", 0x60, 0xA8);
  expectReplyCrc("(VERFW2:00003.03", 0x1D, 0x64);
}

void test_crc_reply_check() {
  std::vector<uint8_t> r = crcReply("(PI30");
  char pl[32];
  TEST_ASSERT_EQUAL((int)pi30::Check::OK, (int)pi30::checkCrcReply(r.data(), r.size(), pl, sizeof pl, false));
  TEST_ASSERT_EQUAL_STRING("PI30", pl);
  r[3] ^= 1;  // corrupt
  TEST_ASSERT_EQUAL((int)pi30::Check::BAD_CRC, (int)pi30::checkCrcReply(r.data(), r.size(), pl, sizeof pl, false));
  const uint8_t nak[] = {'(', 'N', 'A', 'K', 0x0D};  // NAK without CRC is accepted as NAK
  TEST_ASSERT_EQUAL((int)pi30::Check::NAK, (int)pi30::checkCrcReply(nak, sizeof nak, pl, sizeof pl, false));
  const uint8_t echo[] = {'Q', 'P', 'I', 0xBE, 0xAC, 0x0D};
  TEST_ASSERT_EQUAL((int)pi30::Check::BAD_START, (int)pi30::checkCrcReply(echo, sizeof echo, pl, sizeof pl, false));
  // LF terminator is accepted too (V1 C15)
  std::vector<uint8_t> lf = crcReply("(B");
  lf.back() = 0x0A;
  TEST_ASSERT_EQUAL((int)pi30::Check::OK, (int)pi30::checkCrcReply(lf.data(), lf.size(), pl, sizeof pl, false));
}

// ---- REVO CHK (V1 V10, §4.4; doc examples)
void test_revo_chk() {
  uint8_t out[16];
  size_t n = revo::buildChkFrame("QALL", out, sizeof out);
  std::vector<uint8_t> e = hex("51 41 4C 4C 2B 0D");
  TEST_ASSERT_EQUAL(e.size(), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(e.data(), out, n);
  n = revo::buildChkFrame("PQSE", out, sizeof out);
  e = hex("50 51 53 45 3A 0D");
  TEST_ASSERT_EQUAL_HEX8_ARRAY(e.data(), out, n);
  n = revo::buildChkFrame("PQSA", out, sizeof out);
  e = hex("50 51 53 41 36 0D");
  TEST_ASSERT_EQUAL_HEX8_ARRAY(e.data(), out, n);
  n = revo::buildChkFrame("QLITH0", out, sizeof out);
  e = hex("51 4C 49 54 48 30 B3 0D");
  TEST_ASSERT_EQUAL_HEX8_ARRAY(e.data(), out, n);
  // doc reply examples: QALL -> 04, PQSE -> 3C, "ACK" -> D0, PSET example -> F7 (doc erratum says F6, V1 C14)
  const char* qall = "(000 00.0 000 00.0 0000 000 00.0 000 000 000 000 00.0 0000 000000 000000 S 00 00";
  TEST_ASSERT_EQUAL_HEX8(0x04, revo::chk((const uint8_t*)qall, strlen(qall)));
  const char* pqse = "(000100 56.0 54.0 44.0 42.0 030 010 2018 06 01 20 00 00 00";
  TEST_ASSERT_EQUAL_HEX8(0x3C, revo::chk((const uint8_t*)pqse, strlen(pqse)));
  TEST_ASSERT_EQUAL_HEX8(0xD0, revo::chk((const uint8_t*)"ACK", 3));
  const char* pset = "PSET120103 56.3 54.6 43.8 42.6 040 020 2020 02 18 17 06 00";
  TEST_ASSERT_EQUAL_HEX8(0xF7, revo::chk((const uint8_t*)pset, strlen(pset)));
  // CHK reply validation incl. the CHK == CR corner case
  std::vector<uint8_t> r(qall, qall + strlen(qall));
  r.push_back(0x04);
  r.push_back(0x0D);
  char pl[160];
  TEST_ASSERT_EQUAL((int)pi30::Check::OK, (int)revo::checkChkReply(r.data(), r.size(), pl, sizeof pl));
  TEST_ASSERT_EQUAL_STRING(qall + 1, pl);
  r.push_back(0x0A);  // "\r\n" ending
  TEST_ASSERT_EQUAL((int)pi30::Check::OK, (int)revo::checkChkReply(r.data(), r.size(), pl, sizeof pl));
}

// ---- Modbus RTU (V1 §4.4, all low CRC byte first)
static void expectMb(uint8_t slave, uint16_t reg, uint16_t qty, const char* wire) {
  uint8_t f[8];
  modbus::buildRead(slave, reg, qty, f);
  std::vector<uint8_t> e = hex(wire);
  TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(e.data(), f, 8, wire);
  TEST_ASSERT_TRUE(modbus::crcOk(e.data(), e.size()));
}

void test_modbus_crc_vectors() {
  expectMb(1, 201, 1, "01 03 00 C9 00 01 54 34");
  expectMb(1, 201, 34, "01 03 00 C9 00 22 15 ED");
  expectMb(1, 184, 1, "01 03 00 B8 00 01 04 2F");
  expectMb(1, 171, 1, "01 03 00 AB 00 01 F5 EA");
  expectMb(1, 643, 1, "01 03 02 83 00 01 74 5A");
  expectMb(1, 186, 12, "01 03 00 BA 00 0C 64 2A");
  expectMb(1, 443, 3, "01 03 01 BB 00 03 74 12");
  expectMb(1, 0x000B, 1, "01 03 00 0B 00 01 F5 C8");
  expectMb(1, 0x0100, 3, "01 03 01 00 00 03 04 37");
  expectMb(1, 0x0210, 1, "01 03 02 10 00 01 84 77");
  expectMb(1, 0x0035, 20, "01 03 00 35 00 14 55 CB");
  expectMb(0xFF, 0x0100, 3, "FF 03 01 00 00 03 11 E9");
  expectMb(1, 0xC738, 2, "01 03 C7 38 00 02 78 B2");
  expectMb(4, 20000, 2, "04 03 4E 20 00 02 D2 BC");
  expectMb(4, 25201, 79, "04 03 62 71 00 4F 4B C8");
  expectMb(4, 15201, 21, "04 03 3B 61 00 15 D8 AA");
  expectMb(5, 4501, 45, "05 03 11 95 00 2D 91 43");
  expectMb(5, 4546, 16, "05 03 11 C2 00 10 E1 42");
  // R3 erratum re-checked by V1: 01 03 00 10 00 02 -> C5 CE
  uint8_t f[8];
  modbus::buildRead(1, 0x0010, 2, f);
  TEST_ASSERT_EQUAL_HEX8(0xC5, f[6]);
  TEST_ASSERT_EQUAL_HEX8(0xCE, f[7]);
  std::vector<uint8_t> wake = hex("01 AA 06 DE A2");
  TEST_ASSERT_TRUE(modbus::crcOk(wake.data(), wake.size()));
  std::vector<uint8_t> s8851 = hex("88 51 00 03 00 00 00 00 4D 08");
  TEST_ASSERT_TRUE(modbus::crcOk(s8851.data(), s8851.size()));
}

void test_modbus_parse_powmr_swap() {
  // response for 2 registers, values 0x1234 and 0x00FF, bytes swapped on the wire
  uint8_t r[9] = {0x05, 0x03, 0x04, 0x34, 0x12, 0xFF, 0x00, 0, 0};
  uint16_t c = modbus::crc16(r, 7);
  r[7] = (uint8_t)(c & 0xFF);
  r[8] = (uint8_t)(c >> 8);
  uint16_t regs[2];
  TEST_ASSERT_EQUAL((int)modbus::RespCheck::OK, (int)modbus::parseRead(r, 9, 5, 2, regs, true));
  TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
  TEST_ASSERT_EQUAL_HEX16(0x00FF, regs[1]);
  TEST_ASSERT_EQUAL((int)modbus::RespCheck::WRONG_SLAVE, (int)modbus::parseRead(r, 9, 1, 2, regs, true));
}

// ---- whitelist
void test_whitelist_accepts_every_read_command() {
  for (size_t i = 0; i < wl::PI30_READ_COMMANDS_N; i++) {
    const char* c = wl::PI30_READ_COMMANDS[i];
    TEST_ASSERT_TRUE_MESSAGE(wl::cmdAllowed(c, Framing::CRC), c);
    uint8_t f[24];
    size_t n = pi30::buildCrcFrame(c, f, sizeof f);
    TEST_ASSERT_TRUE_MESSAGE(wl::frameAllowed(f, n, Framing::CRC), c);
  }
  const char* chk[] = {"QALL", "PQSE", "PQSA", "QLITH0"};
  for (const char* c : chk) {
    TEST_ASSERT_TRUE_MESSAGE(wl::cmdAllowed(c, Framing::CHK), c);
    uint8_t f[24];
    size_t n = revo::buildChkFrame(c, f, sizeof f);
    TEST_ASSERT_TRUE_MESSAGE(wl::frameAllowed(f, n, Framing::CHK), c);
  }
  TEST_ASSERT_TRUE(wl::cmdAllowed("Q1", Framing::NONE));
  TEST_ASSERT_TRUE(wl::cmdAllowed("Q6", Framing::NONE));
  TEST_ASSERT_TRUE(wl::cmdAllowed("QED20261021", Framing::CRC));
  TEST_ASSERT_TRUE(wl::cmdAllowed("QLD20261231", Framing::CRC));
  // the whitelist is exactly what the firmware sends: every PI30 command of the scheduler table is allowed
  for (int i = 0; i < (int)Cmd::COUNT; i++) {
    const CmdInfo& ci = CMD_INFO[i];
    std::string c = ci.name;
    if (c == "QED" || c == "QLD") c += "20261021";
    TEST_ASSERT_TRUE_MESSAGE(wl::cmdAllowed(c.c_str(), ci.framing), c.c_str());
  }
  std::vector<uint8_t> m = hex("01 03 00 C9 00 22 15 ED");
  TEST_ASSERT_TRUE(wl::modbusAllowed(m.data(), m.size()));
}

void test_whitelist_refuses_setters_and_junk() {
  const char* bad[] = {"POP02",  "POP00", "PCP03", "PBT01", "PSDV21.0", "PCVV28.2", "PBFT27.0", "PF",
                       "PSET120103 56.3 54.6 43.8 42.6 040 020 2020 02 18 17 06 00", "PE", "PEa", "PDa",
                       "PBATCD010", "MCHGC060", "MUCHGC030", "MNCHGC060", "F50", "F60", "V230", "RTEY", "RTDL",
                       "BTA+01", "DAT2610211200", "~R", "~S", "^S005XX", "PGR01", "QPIGSX", "qpigs", "QPI ",
                       "QED20261341", "QED2026102", "QEDabcdefgh", "QEM202613", "QEY1999", "PQSEX", "PQS",
                       "QBOOT", "QPGS0", "QBMS", "Q6X", "", "QLY2026", "QMUCHGCR1",
                       // safety review: unused queries next to setters are no longer whitelisted
                       "QMCHGCR", "QMUCHGCR", "QBEQI", "PBEQI", "MCHGCR", "MUCHGCR", "QLT", "QEM202610",
                       "QEY2026", "PEY2026", "PET", "PDI"};
  for (const char* c : bad) {
    TEST_ASSERT_FALSE_MESSAGE(wl::cmdAllowed(c, Framing::CRC), c);
    TEST_ASSERT_FALSE_MESSAGE(wl::cmdAllowed(c, Framing::CHK), c);
    TEST_ASSERT_FALSE_MESSAGE(wl::cmdAllowed(c, Framing::NONE), c);
  }
  // framing must match: QPIGS is not allowed as CHK or plain
  TEST_ASSERT_FALSE(wl::cmdAllowed("QPIGS", Framing::CHK));
  TEST_ASSERT_FALSE(wl::cmdAllowed("QPIGS", Framing::NONE));
  TEST_ASSERT_FALSE(wl::cmdAllowed("PQSE", Framing::CRC));
  // a frame whose CRC does not match the text is refused
  uint8_t f[16];
  size_t n = pi30::buildCrcFrame("QPIGS", f, sizeof f);
  f[n - 2] ^= 0x01;
  TEST_ASSERT_FALSE(wl::frameAllowed(f, n, Framing::CRC));
  // Modbus writes and 8851 frames are refused
  std::vector<uint8_t> fc06 = hex("01 06 01 2D 00 02 00 00");
  uint16_t c = modbus::crc16(fc06.data(), 6);
  fc06[6] = (uint8_t)(c & 0xFF);
  fc06[7] = (uint8_t)(c >> 8);
  TEST_ASSERT_FALSE(wl::modbusAllowed(fc06.data(), fc06.size()));
  std::vector<uint8_t> fc10 = hex("01 10 00 10 00 01 02 00 01 00 00");
  TEST_ASSERT_FALSE(wl::modbusAllowed(fc10.data(), fc10.size()));
  std::vector<uint8_t> f8851 = hex("88 51 00 10 00 00 00 00 00 00");
  TEST_ASSERT_FALSE(wl::modbusAllowed(f8851.data(), f8851.size()));
  std::vector<uint8_t> badcrc = hex("01 03 00 C9 00 22 15 EE");
  TEST_ASSERT_FALSE(wl::modbusAllowed(badcrc.data(), badcrc.size()));
}

// ---- SerialLink gates: refusal sends ZERO bytes; unarmed sends ZERO bytes and never attaches TX
void test_seriallink_refuses_with_zero_bytes() {
  MockClock clk;
  SimInverter port(clk);
  SerialLink link(port, clk);
  LinkProfile p;
  link.setArmed(true);
  link.configure(p);
  TEST_ASSERT_TRUE(port.txEnabled);
  Reply r;
  const char* setters[] = {"POP02", "PF", "PCP00", "PBT02", "PSDV21.0", "MUCHGC030", "F60", "PE", "RTEY"};
  for (const char* s : setters) {
    TEST_ASSERT_EQUAL_MESSAGE((int)LinkResult::REFUSED, (int)link.query(s, Framing::CRC, r), s);
    TEST_ASSERT_EQUAL_MESSAGE((int)LinkResult::REFUSED, (int)link.query(s, Framing::CHK, r), s);
    TEST_ASSERT_EQUAL_MESSAGE((int)LinkResult::REFUSED, (int)link.query(s, Framing::NONE, r), s);
    TEST_ASSERT_EQUAL(0, r.bytesSent);
  }
  TEST_ASSERT_EQUAL((int)LinkResult::REFUSED,
                    (int)link.query("PSET120103 56.3 54.6 43.8 42.6 040 020 2020 02 18 17 06 00", Framing::CHK, r));
  std::vector<uint8_t> fc06 = hex("01 06 01 2D 00 02 00 00");
  TEST_ASSERT_EQUAL((int)LinkResult::REFUSED, (int)link.queryModbus(fc06.data(), fc06.size(), 8, r));
  TEST_ASSERT_EQUAL(0, (int)port.written.size());
  TEST_ASSERT_EQUAL(0, port.unsafeWrites);
  TEST_ASSERT_TRUE(link.stats().refused >= 28);
}

void test_seriallink_not_armed_sends_nothing() {
  MockClock clk;
  SimInverter port(clk);
  port.respond = [](const std::string&, Framing) { return std::string("(PI30"); };
  SerialLink link(port, clk);
  LinkProfile p;
  link.configure(p);  // fresh device: not armed
  TEST_ASSERT_FALSE(port.txEnabled);
  Reply r;
  TEST_ASSERT_EQUAL((int)LinkResult::NOT_ARMED, (int)link.query("QPI", Framing::CRC, r));
  TEST_ASSERT_EQUAL((int)LinkResult::NOT_ARMED, (int)link.query("QPIGS", Framing::CRC, r));
  std::vector<uint8_t> m = hex("01 03 00 C9 00 01 54 34");
  TEST_ASSERT_EQUAL((int)LinkResult::NOT_ARMED, (int)link.queryModbus(m.data(), m.size(), 7, r));
  TEST_ASSERT_EQUAL(0, (int)port.written.size());
  // listening is allowed and never writes
  uint8_t buf[16];
  link.listen(100, buf, sizeof buf);
  TEST_ASSERT_EQUAL(0, (int)port.written.size());
  // the bench loopback needs explicit confirmation
  SerialLink::LoopbackResult lb = link.loopbackTest(false);
  TEST_ASSERT_FALSE(lb.ran);
  TEST_ASSERT_EQUAL(0, (int)port.written.size());
  // and is refused once armed
  link.setArmed(true);
  lb = link.loopbackTest(true);
  TEST_ASSERT_FALSE(lb.ran);
}

void test_seriallink_query_ok_nak_timeout_echo() {
  MockClock clk;
  SimInverter port(clk);
  port.respond = [](const std::string& c, Framing) -> std::string {
    if (c == "QPI") return "(PI30";
    if (c == "QMN") return "(NAK";
    return "";
  };
  SerialLink link(port, clk);
  link.setArmed(true);
  link.configure(LinkProfile());
  Reply r;
  TEST_ASSERT_EQUAL((int)LinkResult::OK, (int)link.query("QPI", Framing::CRC, r));
  TEST_ASSERT_EQUAL_STRING("PI30", r.payload);
  TEST_ASSERT_EQUAL(6, r.bytesSent);
  TEST_ASSERT_EQUAL((int)LinkResult::NAK, (int)link.query("QMN", Framing::CRC, r));
  uint32_t t0 = clk.ms();
  TEST_ASSERT_EQUAL((int)LinkResult::TIMEOUT, (int)link.query("QVFW", Framing::CRC, r));
  TEST_ASSERT_TRUE(clk.ms() - t0 >= cfg::FIRST_BYTE_MS);
  // wrong polarity -> silence
  LinkProfile wrong;
  wrong.rxInvert = false;
  link.configure(wrong);
  TEST_ASSERT_EQUAL((int)LinkResult::TIMEOUT, (int)link.query("QPI", Framing::CRC, r));
  // echo of the request before the reply is skipped
  link.configure(LinkProfile());
  port.respond = nullptr;
  link.query("QPI", Framing::CRC, r);  // sets nothing
  std::vector<uint8_t> echo = hex("51 50 49 BE AC 0D");
  std::vector<uint8_t> rep = crcReply("(PI30");
  echo.insert(echo.end(), rep.begin(), rep.end());
  port.respond = [&](const std::string&, Framing) -> std::string { return ""; };
  // inject manually after the write
  struct EchoPort : SimInverter {
    std::vector<uint8_t> inj;
    using SimInverter::SimInverter;
    size_t write(const uint8_t* b, size_t n) override {
      size_t w = SimInverter::write(b, n);
      rx = inj;
      rxReadyAt = clk.t + 10;
      return w;
    }
  };
  EchoPort ep(clk);
  ep.inj = echo;
  SerialLink l2(ep, clk);
  l2.setArmed(true);
  l2.configure(LinkProfile());
  TEST_ASSERT_EQUAL((int)LinkResult::OK, (int)l2.query("QPI", Framing::CRC, r));
  TEST_ASSERT_EQUAL_STRING("PI30", r.payload);
  TEST_ASSERT_EQUAL(1, (int)l2.stats().echoDropped);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pi30_request_frames);
  RUN_TEST(test_dated_and_pi18_trailers);
  RUN_TEST(test_reply_trailers);
  RUN_TEST(test_crc_reply_check);
  RUN_TEST(test_revo_chk);
  RUN_TEST(test_modbus_crc_vectors);
  RUN_TEST(test_modbus_parse_powmr_swap);
  RUN_TEST(test_whitelist_accepts_every_read_command);
  RUN_TEST(test_whitelist_refuses_setters_and_junk);
  RUN_TEST(test_seriallink_refuses_with_zero_bytes);
  RUN_TEST(test_seriallink_not_armed_sends_nothing);
  RUN_TEST(test_seriallink_query_ok_nak_timeout_echo);
  return UNITY_END();
}
