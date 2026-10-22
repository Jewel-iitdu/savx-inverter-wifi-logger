// test_parsers - QPIGS / QPIRI / QMOD / QPIWS / QALL / QFLAG / Q1 / PQSE / legacy Q6 parsers on realistic frames
// from the research reports (r2 Appendix B, r2 §5, r1 §4.2, the Sorotec REVO doc), plausibility gates,
// dialect classification and the `live` JSON keys.
#include <math.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "codes.h"
#include "mocks/Mocks.h"
#include "model/Decode.h"
#include "model/LiveJson.h"
#include "proto/legacy/Q6Adapter.h"
#include "proto/modbus/ModbusAdapter.h"
#include "proto/pi30/Pi30Adapter.h"
#include "proto/pi30/Pi30Parse.h"

void setUp() {}
void tearDown() {}

// EASun SP-3200 (24 V / 3.2 kW, PI30, 21-field QPIGS), battery mode at night (r2 §5.2 / App. B, S2M dump)
static const char* EASUN_QPIGS =
    "000.0 00.0 220.1 49.9 0198 0155 006 395 26.30 000 060 0028 0000 000.0 00.00 00004 00010000 00 00 00001 010";
// Voltronic Axpert MAX II 8 kW (48 V) QPIGS with its device CRC 48 D1 (r2 App. B)
static const char* MAX8K_QPIGS =
    "(222.6 49.9 230.1 50.0 0551 0499 006 398 54.30 000 061 0039 01.0 214.4 00.00 00000 00010110 00 00 00226 010";
static const char* MAX8K_QPIRI =
    "(230.0 34.7 230.0 50.0 34.7 8000 8000 48.0 51.0 48.0 55.2 55.0 2 010 040 0 2 3 9 01 0 0 52.0 1 1 480 0 140";
static const char* EASUN55_QPIRI =
    "(230.0 23.9 230.0 50.0 23.9 5500 5500 48.0 51.0 48.0 55.3 55.3 2 02 100 0 2 3 1 01 0 0 53.0 0 1";

void test_real_frames_have_valid_crc() {
  // MAX II 8 kW: the device sent 48 D1
  uint16_t c = pi30::crc((const uint8_t*)MAX8K_QPIGS, strlen(MAX8K_QPIGS));
  TEST_ASSERT_EQUAL_HEX16(0x48D1, c);
  c = pi30::crc((const uint8_t*)MAX8K_QPIRI, strlen(MAX8K_QPIRI));
  TEST_ASSERT_EQUAL_HEX16(0xC031, c);
  c = pi30::crc((const uint8_t*)EASUN55_QPIRI, strlen(EASUN55_QPIRI));
  TEST_ASSERT_EQUAL_HEX16(0x0E3E, c);  // raw 0x0D3E, high byte bumped
  // QPIGS2 "(01.0 202.0 00205 " with trailing space -> 27 4A
  const char* q2 = "(01.0 202.0 00205 ";
  TEST_ASSERT_EQUAL_HEX16(0x274A, pi30::crc((const uint8_t*)q2, strlen(q2)));
  const char* qflag = "(EbkuzDajvxy";
  TEST_ASSERT_EQUAL_HEX16(0xB4EE, pi30::crc((const uint8_t*)qflag, strlen(qflag)));
}

void test_qpigs_stock21_easun() {
  pi30p::Qpigs q;
  TEST_ASSERT_TRUE(pi30p::parseQpigs(EASUN_QPIGS, q));
  TEST_ASSERT_EQUAL(21, q.n);
  TEST_ASSERT_EQUAL_STRING("00010000", q.st);
  TEST_ASSERT_EQUAL_STRING("010", q.st2);
  TEST_ASSERT_TRUE(decode::plausibleQpigs(q));
  Sample s;
  decode::qpigs(q, Dialect::STOCK21, 'A', s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, s.gv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 220.1, s.ov);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 49.9, s.of);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 198, s.va);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 155, s.w);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 6, s.ld);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 395, s.bus);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 26.30, s.bv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0, s.bci);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 60, s.soc);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 28, s.tmp);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 4, s.bdi);  // field 15 = discharge A (NOT the gemini "field 15" claim, V1 C9)
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1, s.pvw);  // field 19 PV charging power
  TEST_ASSERT_FLOAT_WITHIN(0.01, -4, s.bi);
  TEST_ASSERT_FLOAT_WITHIN(0.1, -105.2, s.bw);
  TEST_ASSERT_TRUE(s.stBit(4));   // load on
  TEST_ASSERT_FALSE(s.stBit(2));  // not charging
}

void test_qpigs_max8k_and_pv12_current() {
  pi30p::Qpigs q;
  TEST_ASSERT_TRUE(pi30p::parseQpigs(MAX8K_QPIGS + 1, q));
  TEST_ASSERT_EQUAL(21, q.n);  // 21 fields even on a MAX (r2 §5.2)
  TEST_ASSERT_FALSE(decode::plausibleQpigs(q));  // 48 V battery fails the 24 V gate (18-34 V) by design
  TEST_ASSERT_EQUAL('A', decode::classifyPv12(q));  // 1.0 A x 214.4 V ~ 226 W (field 19) within 20 %
  Sample s;
  decode::qpigs(q, Dialect::STOCK21, '?', s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, s.pvi);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 226, s.pvw);
  TEST_ASSERT_TRUE(s.stBit(1) && s.stBit(2) && s.stBit(4));  // 00010110: load on, charging, SCC charging
}

void test_qpigs_pv12_power_rule_revo_trace() {
  // REVO VE II trace (V1 V16): field 12 = 0248 at PV 355.0 V, field 19 = 00000 -> field 12 is POWER
  const char* t = "230.0 50.0 230.0 50.0 0000 0300 006 400 26.40 010 080 0026 0248 355.0 26.40 00000 00010110 00 00 00000 010";
  pi30p::Qpigs q;
  TEST_ASSERT_TRUE(pi30p::parseQpigs(t, q));
  TEST_ASSERT_EQUAL('W', decode::classifyPv12(q));
  Sample s;
  decode::qpigs(q, Dialect::STOCK21, 'W', s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 248, s.pvw);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 0.7, s.pvi);  // derived 248 / 355
  // at night (PV V < 30) the rule cannot decide
  const char* night = "230.0 50.0 230.0 50.0 0000 0300 006 400 26.40 010 080 0026 0000 000.0 26.40 00000 00010110 00 00 00000 010";
  TEST_ASSERT_TRUE(pi30p::parseQpigs(night, q));
  TEST_ASSERT_EQUAL('?', decode::classifyPv12(q));
}

void test_qpigs_17_field_and_bad_frames() {
  const char* t17 = "230.0 50.0 230.0 50.0 0161 0119 003 360 26.50 012 100 0045 0003 120.0 26.45 00000 00110110";
  pi30p::Qpigs q;
  TEST_ASSERT_TRUE(pi30p::parseQpigs(t17, q));
  TEST_ASSERT_EQUAL(17, q.n);
  Sample s;
  decode::qpigs(q, Dialect::STOCK17, 'A', s);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 360, s.pvw);  // 17 fields: pvv x pvi
  TEST_ASSERT_FALSE(pi30p::parseQpigs("230.0 50.0 230.0", q));                  // too short
  TEST_ASSERT_FALSE(pi30p::parseQpigs("PI30", q));
  const char* badBits = "230.0 50.0 230.0 50.0 0161 0119 003 360 26.50 012 100 0045 0003 120.0 26.45 00000 0011011X";
  TEST_ASSERT_FALSE(pi30p::parseQpigs(badBits, q));
  // raw ADC heat-sink value (> 150) is not reported as °C
  const char* adc = "230.0 50.0 230.0 50.0 0161 0119 003 360 26.50 012 100 0512 0003 120.0 26.45 00000 00110110";
  TEST_ASSERT_TRUE(pi30p::parseQpigs(adc, q));
  decode::qpigs(q, Dialect::STOCK17, 'A', s);
  TEST_ASSERT_TRUE(s.tmpRaw);
  TEST_ASSERT_TRUE(isnan(s.tmp));
}

void test_qpiri_variants_and_identity() {
  pi30p::Qpiri r;
  TEST_ASSERT_TRUE(pi30p::parseQpiri(MAX8K_QPIRI + 1, r));
  TEST_ASSERT_EQUAL(28, r.n);
  TEST_ASSERT_TRUE(pi30p::parseQpiri(EASUN55_QPIRI + 1, r));
  TEST_ASSERT_EQUAL(25, r.n);
  // EASun SP-3200 24 V / 3.2 kW (r2 §5.4) - the expected identity of this unit: field 5 = 3200, 7 = 24.0, 4 = 13.9
  const char* sp = "220.0 14.5 220.0 50.0 13.9 3200 3000 24.0 22.5 23.2 27.6 27.0 2 40 060 0 0 2 6 01 0 0 27.5 0 1";
  TEST_ASSERT_TRUE(pi30p::parseQpiri(sp, r));
  TEST_ASSERT_EQUAL(25, r.n);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 3200, r.v[5]);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 24.0, r.v[7]);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 13.9, r.v[4]);
  // 24 V Axpert with "-" as parallel max
  const char* ax = "230.0 13.0 230.0 50.0 13.0 3000 2400 24.0 23.0 21.0 28.2 27.0 0 30 50 0 2 1 - 01 1 0 26.0 0 0";
  TEST_ASSERT_TRUE(pi30p::parseQpiri(ax, r));
  TEST_ASSERT_TRUE(isnan(r.v[18]));
}

void test_qmod_qpiws_qflag_versions() {
  TEST_ASSERT_EQUAL('B', pi30p::parseQmod("B"));
  TEST_ASSERT_EQUAL('L', pi30p::parseQmod("L"));
  TEST_ASSERT_EQUAL(0, pi30p::parseQmod("X"));
  TEST_ASSERT_EQUAL(0, pi30p::parseQmod("LB"));
  char wf[41];
  // EASun SP-3200 in battery mode: only a5 LINE_FAIL (r2 §5.6)
  TEST_ASSERT_TRUE(pi30p::parseQpiws("00000100000000000000000000000000", wf, sizeof wf));
  TEST_ASSERT_TRUE(codes::qpiwsBit(wf, codes::QPIWS_LINE_FAIL));
  TEST_ASSERT_FALSE(codes::qpiwsHasFault(wf));
  TEST_ASSERT_FALSE(pi30p::parseQpiws("0000010000000000000000000000000", wf, sizeof wf));   // 31 chars
  TEST_ASSERT_FALSE(pi30p::parseQpiws("00000100000000000000000000000002", wf, sizeof wf));  // digit outside a31-32
  // a9 over-temperature is a fault only when a1 = 1
  TEST_ASSERT_FALSE(codes::qpiwsHasFault("00000000010000000000000000000000"));
  TEST_ASSERT_TRUE(codes::qpiwsHasFault("01000000010000000000000000000000"));
  TEST_ASSERT_TRUE(codes::qpiwsHasFault("00000000000000000000001000000000"));  // a22 battery open (F)
  // 36-char (PMAX/PGK) with the 24 V fault code at a31-a32
  std::string w36 = std::string(31, '0') + "05" + "000";
  TEST_ASSERT_TRUE(pi30p::parseQpiws(w36.c_str(), wf, sizeof wf));
  TEST_ASSERT_EQUAL(5, pi30p::qpiwsFaultCode(wf));
  Sample s;
  decode::qpiws(wf, Dialect::MAX24, s);
  TEST_ASSERT_EQUAL(5, s.fc);
  TEST_ASSERT_EQUAL((int)codes::Src::QPIWS36, (int)s.fcSrc);
  TEST_ASSERT_EQUAL_STRING("Output short circuited or over temperature detected by internal converter components",
                           codes::faultText(s.fcSrc, s.fc));
  char en[28], dis[28];
  TEST_ASSERT_TRUE(pi30p::parseQflag("EakxyDbjuvz", en, sizeof en, dis, sizeof dis));
  TEST_ASSERT_EQUAL_STRING("akxy", en);
  TEST_ASSERT_EQUAL_STRING("bjuvz", dis);
  TEST_ASSERT_TRUE(pi30p::parseQflag("EauxyzDbjkv;", en, sizeof en, dis, sizeof dis));  // PowMr stray ';'
  TEST_ASSERT_EQUAL_STRING("bjkv", dis);
  char v[20];
  TEST_ASSERT_TRUE(pi30p::parseVersion("VERFW:00014.04", "VERFW:", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("00014.04", v);
  TEST_ASSERT_TRUE(pi30p::parseVersion("VERFW2:00003.03", "VERFW2:", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("00003.03", v);
  TEST_ASSERT_FALSE(pi30p::parseVersion("VERFW2:00003.03", "VERFW:", v, sizeof v));
  char id[32];
  TEST_ASSERT_TRUE(pi30p::parseQid("9293333010501", id, sizeof id));
  TEST_ASSERT_TRUE(pi30p::parseQsid("1492932105105335000000", id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("92932105105335", id);
  uint32_t wh;
  TEST_ASSERT_TRUE(pi30p::parseEnergyWh("10636700", wh));
  TEST_ASSERT_EQUAL_UINT32(10636700, wh);
  TEST_ASSERT_FALSE(pi30p::parseEnergyWh("NAK", wh));
}

void test_qall_revo() {
  // Sorotec doc example (standby, all zero) -> parses, but fails plausibility (no battery voltage)
  const char* doc = "000 00.0 000 00.0 0000 000 00.0 000 000 000 000 00.0 0000 000000 000000 S 00 00";
  pi30p::Qall a;
  TEST_ASSERT_TRUE(pi30p::parseQall(doc, a));
  TEST_ASSERT_EQUAL('S', a.mode);
  TEST_ASSERT_FALSE(decode::plausibleQall(a));
  // mpp-solar discussion #78 sample (REVO II, 48 V class)
  const char* d78 = "223 49.9 223 49.9 1962 035 00.1 000 013 000 355 00.3 0106 018859 000056 L 00 00";
  TEST_ASSERT_TRUE(pi30p::parseQall(d78, a));
  TEST_ASSERT_EQUAL(18, a.n);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 18859, a.v[13]);
  TEST_ASSERT_FALSE(decode::plausibleQall(a));  // battery 0.1 V is rejected by the gate
  // plausible 24 V REVO reply in the documented field order (V1 V17)
  const char* ok = "231 50.0 230 50.0 0512 016 26.4 078 012 000 145 03.2 0465 001920 000368 L 00 00";
  TEST_ASSERT_TRUE(pi30p::parseQall(ok, a));
  TEST_ASSERT_TRUE(decode::plausibleQall(a));
  Sample s;
  decode::qall(a, s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 231, s.gv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 512, s.w);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 16, s.ld);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 26.4, s.bv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 78, s.soc);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 12, s.bci);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 145, s.pvv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 3.2, s.pvi);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 465, s.pvw);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1920, s.epvi);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 368, s.etot);
  TEST_ASSERT_EQUAL('L', s.md);
  TEST_ASSERT_EQUAL(0, s.wc);
  TEST_ASSERT_EQUAL(0, s.fc);
  TEST_ASSERT_EQUAL((int)codes::Src::REVO, (int)s.fcSrc);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 316.8, s.bw);
  // REVO QPIGS only contributes bus V and status bits (field 15 is PV Wh, not discharge A)
  const char* revoQpigs = "231.0 50.0 230.0 50.0 0000 0512 016 401 26.40 012 078 0026 0465 145.0 26.40 01920 00010110 00 00 00465 010";
  pi30p::Qpigs q;
  TEST_ASSERT_TRUE(pi30p::parseQpigs(revoQpigs, q));
  decode::qpigs(q, Dialect::REVO, 'W', s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 401, s.bus);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0, s.bdi);  // still from QALL
  TEST_ASSERT_EQUAL_STRING("00010110", s.st);
  // wrong field count / bad mode letter
  TEST_ASSERT_FALSE(pi30p::parseQall("231 50.0 230 50.0 0512 016 26.4 078 012 000 145 03.2 0465 001920 000368 L 00", a));
  TEST_ASSERT_FALSE(pi30p::parseQall("231 50.0 230 50.0 0512 016 26.4 078 012 000 145 03.2 0465 001920 000368 X 00 00", a));
  TEST_ASSERT_EQUAL_STRING("Mains low voltage", codes::revoWarningText(2));
  TEST_ASSERT_EQUAL_STRING("BUS overvoltage", codes::revoFaultText(1));
  TEST_ASSERT_NULL(codes::revoWarningText(8));  // mpp-solar addition, not in the Sorotec doc (V1 C12)
}

void test_q1_pqse_legacy() {
  pi30p::Q1 q;
  TEST_ASSERT_TRUE(pi30p::parseQ1("07288 00001 00 00 00 000 028 028 000 00 00 000 0030 0001 0000 00.00 10", q));
  TEST_ASSERT_EQUAL(17, q.n);
  Sample s;
  s.tmpRaw = true;
  decode::q1(q, s);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 28, s.tmp);
  pi30p::Pqse p;
  TEST_ASSERT_TRUE(pi30p::parsePqse("000100 56.0 54.0 44.0 42.0 030 010 2018 06 01 20 00 00 00", p));
  TEST_ASSERT_EQUAL(1, p.outVSet);
  TEST_ASSERT_EQUAL(0, p.batType);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 56.0, p.bulkV);
  TEST_ASSERT_EQUAL(30, p.sumChgA);
  TEST_ASSERT_EQUAL(2018, p.year);
  TEST_ASSERT_EQUAL(20, p.hour);
  // legacy RP/RS Q6 and UPS-style Q1 (r2 §3.5 [SECONDARY])
  Sample l;
  TEST_ASSERT_TRUE(Q6Adapter::decodeQ6("000 00.0 229 50.0 12.78 100 000.0 000 000.0 0 0000 0000 000 009.1 B 00 00 U1V0196 1", l));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 229, l.ov);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 12.78, l.bv);
  TEST_ASSERT_EQUAL('B', l.md);
  Sample l1;
  TEST_ASSERT_TRUE(Q6Adapter::decodeQ1("000.0 000.0 229.6 000 50.0 12.7 09.0 10001001", l1));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 229.6, l1.ov);
}

void test_modbus_decoders() {
  uint16_t r[34] = {0};
  auto set = [&](int reg, int v) { r[reg - 201] = (uint16_t)(int16_t)v; };
  set(201, 3);      // off-grid -> 'B'
  set(202, 2305);   // 230.5 V
  set(203, 5000);   // 50.00 Hz
  set(210, 2300);
  set(212, 5000);
  set(213, 480);
  set(214, 520);
  set(215, 264);    // 26.4 V
  set(219, 1453);
  set(220, 32);
  set(223, 465);
  set(225, 16);
  set(227, 38);
  set(229, 78);
  set(232, -45);    // -4.5 A (discharging)
  Sample s;
  TEST_ASSERT_TRUE(ModbusAdapter::decodeSmg(r, 34, s));
  TEST_ASSERT_EQUAL('B', s.md);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 230.5, s.gv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 26.4, s.bv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 4.5, s.bdi);
  TEST_ASSERT_FLOAT_WITHIN(0.01, -4.5, s.bi);
  TEST_ASSERT_TRUE(decode::plausibleSample(s));
  uint16_t z[34] = {0};
  Sample sz;
  TEST_ASSERT_FALSE(ModbusAdapter::decodeSmg(z, 34, sz));  // all-zero data rejected (V1 F3)
  uint16_t pm[45] = {0};
  pm[4502 - 4501] = 2310;
  pm[4506 - 4501] = 265;
  pm[4507 - 4501] = 80;
  pm[4508 - 4501] = 10;
  pm[4510 - 4501] = 2300;
  pm[4511 - 4501] = 500;
  pm[4512 - 4501] = 300;
  Sample sp;
  TEST_ASSERT_TRUE(ModbusAdapter::decodePowmr(pm, 45, sp));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 26.5, sp.bv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 50.0, sp.of);
}

void test_plausibility_gates() {
  Sample s;
  s.bv = 26.4f;
  s.ov = 230;
  s.of = 50;
  s.soc = 80;
  s.pvv = 145;
  s.ld = 16;
  TEST_ASSERT_TRUE(decode::plausibleSample(s));
  Sample t = s;
  t.ov = 0;
  t.of = 0;  // inverter output off is allowed (exactly 0)
  TEST_ASSERT_TRUE(decode::plausibleSample(t));
  t = s; t.bv = 17.9f; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.bv = 34.1f; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.ov = 150; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.ov = 270; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.of = 70; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.soc = 101; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.pvv = 521; TEST_ASSERT_FALSE(decode::plausibleSample(t));
  t = s; t.ld = 251; TEST_ASSERT_FALSE(decode::plausibleSample(t));
}

void test_dialect_classification() {
  TEST_ASSERT_EQUAL((int)Dialect::REVO, (int)Pi30Adapter::classify(true, 25, "EakxyDbjuvz", 32, 'L', 21));
  TEST_ASSERT_EQUAL((int)Dialect::MAX24, (int)Pi30Adapter::classify(false, 28, "", 36, 'L', 24));
  TEST_ASSERT_EQUAL((int)Dialect::MAX24, (int)Pi30Adapter::classify(false, 25, "EadDbjk", 32, 'L', 21));
  TEST_ASSERT_EQUAL((int)Dialect::GK, (int)Pi30Adapter::classify(false, 26, "Eak", 36, 'L', 21));
  TEST_ASSERT_EQUAL((int)Dialect::GK, (int)Pi30Adapter::classify(false, 25, "Eak", 32, 'E', 21));
  TEST_ASSERT_EQUAL((int)Dialect::STOCK21, (int)Pi30Adapter::classify(false, 25, "EazDbjkuvxy", 32, 'B', 21));
  TEST_ASSERT_EQUAL((int)Dialect::STOCK17, (int)Pi30Adapter::classify(false, 25, "", 32, 'B', 17));
}

void test_live_json_keys() {
  pi30p::Qall a;
  TEST_ASSERT_TRUE(pi30p::parseQall("231 50.0 230 50.0 0512 016 26.4 078 012 000 145 03.2 0465 001920 000368 L 00 00", a));
  Sample s;
  decode::qall(a, s);
  s.ts = 1792560000;
  s.lk = 1;
  s.epv = 1920.4f;
  s.q = 0;
  char buf[600];
  size_t n = sampleToLiveJson(s, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":1792560000"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"lk\":1"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"bv\":26.4"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"md\":\"L\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"epv\":1920"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"q\":0"));
  TEST_ASSERT_NULL(strstr(buf, "\"tmp\""));  // missing on REVO -> omitted
  TEST_ASSERT_NULL(strstr(buf, "\"va\""));
  TEST_ASSERT_NULL(strstr(buf, "nan"));
}


void test_parse_qlith0_revo_unit() {
  pi30p::Qlith0 b;
  TEST_ASSERT_TRUE(pi30p::parseQlith0("026.5 000.0 000.9 300 100 000 100.0 010.0 028.4 002.2 0 0", b));
  TEST_ASSERT_EQUAL(12, b.n);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 26.5f, b.v[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0f, b.v[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.9f, b.v[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 100.f, b.v[4]);
  TEST_ASSERT_FALSE(pi30p::parseQlith0("026.5 000.0 000.9", b));          // short
  TEST_ASSERT_FALSE(pi30p::parseQlith0("999.9 000.0 000.9 300 100 000 100.0 010.0 028.4 002.2 0 0", b));  // implausible V
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_qlith0_revo_unit);
  RUN_TEST(test_real_frames_have_valid_crc);
  RUN_TEST(test_qpigs_stock21_easun);
  RUN_TEST(test_qpigs_max8k_and_pv12_current);
  RUN_TEST(test_qpigs_pv12_power_rule_revo_trace);
  RUN_TEST(test_qpigs_17_field_and_bad_frames);
  RUN_TEST(test_qpiri_variants_and_identity);
  RUN_TEST(test_qmod_qpiws_qflag_versions);
  RUN_TEST(test_qall_revo);
  RUN_TEST(test_q1_pqse_legacy);
  RUN_TEST(test_modbus_decoders);
  RUN_TEST(test_plausibility_gates);
  RUN_TEST(test_dialect_classification);
  RUN_TEST(test_live_json_keys);
  return UNITY_END();
}
