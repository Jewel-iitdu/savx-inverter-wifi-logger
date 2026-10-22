// test_engine - prober decision tree (d1 §3.5-3.6) against simulated inverters, the arming gate,
// light probe, re-probe, the scheduler and the receive-only pin diagnostics.
#include <math.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "link/PinDiag.h"
#include "link/SerialLink.h"
#include "mocks/Mocks.h"
#include "proto/Engine.h"
#include "proto/Scheduler.h"

void setUp() {}
void tearDown() {}

static const char* REVO_QALL = "(231 50.0 230 50.0 0512 016 26.4 078 012 000 145 03.2 0465 001920 000368 L 00 00";
static const char* REVO_QPIGS =
    "(231.0 50.0 230.0 50.0 0000 0512 016 401 26.40 012 078 0026 0465 145.0 26.40 01920 00010110 00 00 00465 010";
static const char* SP3200_QPIRI = "(230.0 13.9 230.0 50.0 13.9 3200 3200 24.0 23.0 21.0 28.2 27.0 0 30 60 0 0 2 6 01 0 0 27.0 0 1";
static const char* EASUN_QPIGS =
    "(000.0 00.0 220.1 49.9 0198 0155 006 395 26.30 000 060 0028 0000 000.0 00.00 00004 00010000 00 00 00001 010";

static std::string revoDevice(const std::string& c, Framing f) {
  if (c == "QPI") return "(PI30";
  if (c == "QPIGS") return REVO_QPIGS;
  if (c == "QMOD") return "(L";
  if (c == "QPIRI") return SP3200_QPIRI;
  if (c == "QVFW") return "(VERFW:00014.04";
  if (c == "QVFW2") return "(VERFW2:00003.03";
  if (c == "QALL" && f == Framing::CHK) return REVO_QALL;
  if (c == "QID") return "(9293333010501";
  if (c == "PQSE") return "(000100 28.2 27.0 22.0 21.0 060 030 2026 10 21 14 35 00 00 00";
  if (c == "PQSA") return "(11110000";
  return "(NAK";
}

static std::string stockDevice(const std::string& c, Framing) {
  if (c == "QPI") return "(PI30";
  if (c == "QPIGS") return EASUN_QPIGS;
  if (c == "QMOD") return "(B";
  if (c == "QPIRI") return "(220.0 14.5 220.0 50.0 13.9 3200 3000 24.0 22.5 23.2 27.6 27.0 2 40 060 0 0 2 6 01 0 0 27.5 0 1";
  if (c == "QPIWS") return "(00000100000000000000000000000000";
  if (c == "QFLAG") return "(EazDbjkuvxy";
  if (c == "Q1") return "(07288 00001 00 00 00 000 028 028 000 00 00 000 0030 0001 0000 00.00 10";
  return "(NAK";  // QALL, QET, QMN, QPIGS2 ... (EASun SP-3200 NAKs these, r2 §5.10)
}

struct Rig {
  MockClock clk;
  SimInverter port{clk};
  SerialLink link{port, clk};
  InverterEngine eng{link};
};

static void armAndProbe(Rig& r, const LinkProfile& p = LinkProfile()) {
  r.eng.begin(p, AdapterState(), true, false, r.clk.ms());
  Sample s;
  r.eng.tick(r.clk.ms(), 0, s);  // runs the full probe
}

void test_probe_revo() {
  Rig r;
  r.port.respond = revoDevice;
  armAndProbe(r);
  const ProbeOutcome& o = r.eng.lastProbe();
  TEST_ASSERT_TRUE_MESSAGE(o.ok, o.summary);
  TEST_ASSERT_EQUAL((int)AdapterKind::PI30, (int)o.kind);
  TEST_ASSERT_EQUAL_STRING("REVO", o.state.dialect);
  TEST_ASSERT_EQUAL('W', o.state.pv12);
  TEST_ASSERT_FALSE(o.state.flags & AF_QALL_CRC);  // CHK form answered first
  TEST_ASSERT_EQUAL(2400, (int)o.profile.baud);
  TEST_ASSERT_TRUE(o.profile.rxInvert && o.profile.txInvert);
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
  Identity id;
  r.eng.identity(id);
  TEST_ASSERT_EQUAL_STRING("PI30", id.qpi);
  TEST_ASSERT_EQUAL_STRING("00014.04", id.vfw);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 3200, id.ratva);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 24.0, id.ratbv);
  TEST_ASSERT_NOT_NULL(strstr(id.sup, "QALL"));
  TEST_ASSERT_NOT_NULL(strstr(id.link, "rs232lite-2400-ii"));
  // RUN produces samples from QALL every 5 s
  int got = 0;
  Sample s;
  for (int i = 0; i < 400; i++) {
    r.clk.sleepMs(50);
    if (r.eng.tick(r.clk.ms(), 1792560000 + i, s)) {
      got++;
      TEST_ASSERT_EQUAL(1, s.lk);
      TEST_ASSERT_FLOAT_WITHIN(0.01, 26.4, s.bv);
      TEST_ASSERT_FLOAT_WITHIN(0.01, 465, s.pvw);
      TEST_ASSERT_EQUAL('L', s.md);
    }
  }
  TEST_ASSERT_TRUE(got >= 3);
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
  // the probe must never have sent anything outside the whitelist (checked per write by the mock)
  for (const std::string& c : r.port.commands) TEST_ASSERT_TRUE_MESSAGE(c != "PSET" && c[0] != 'M', c.c_str());
}

void test_probe_stock21_with_reversed_rx_polarity() {
  Rig r;
  r.port.respond = stockDevice;
  r.port.rxInv = false;  // module A/B swapped in the wiring -> P2 (V1 D1c)
  armAndProbe(r);
  const ProbeOutcome& o = r.eng.lastProbe();
  TEST_ASSERT_TRUE_MESSAGE(o.ok, o.summary);
  TEST_ASSERT_EQUAL_STRING("STOCK21", o.state.dialect);
  TEST_ASSERT_FALSE(o.profile.rxInvert);
  TEST_ASSERT_TRUE(o.profile.txInvert);
  Sample s;
  bool fresh = false;
  for (int i = 0; i < 200 && !fresh; i++) {
    r.clk.sleepMs(50);
    fresh = r.eng.tick(r.clk.ms(), 0, s);
  }
  TEST_ASSERT_TRUE(fresh);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 26.30, s.bv);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 4, s.bdi);
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
}

void test_probe_silence_falls_back_to_raw() {
  Rig r;  // no respond -> silence everywhere
  armAndProbe(r);
  const ProbeOutcome& o = r.eng.lastProbe();
  TEST_ASSERT_FALSE(o.ok);
  TEST_ASSERT_EQUAL((int)AdapterKind::RAW, (int)o.kind);
  TEST_ASSERT_EQUAL((int)EngineState::RAW, (int)r.eng.state());
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
  TEST_ASSERT_TRUE(r.port.written.size() > 30);
  TEST_ASSERT_TRUE(r.port.beginCalls >= 7);  // P1..P5 + 2 Modbus profiles
  // RAW = listen-only: no further writes
  size_t before = r.port.written.size();
  Sample s;
  for (int i = 0; i < 100; i++) {
    r.clk.sleepMs(100);
    r.eng.tick(r.clk.ms(), 0, s);
  }
  TEST_ASSERT_EQUAL(before, r.port.written.size());
}

void test_probe_legacy_q6() {
  Rig r;
  r.port.respond = [](const std::string& c, Framing f) -> std::string {
    if (f == Framing::NONE && c == "Q6") return "(000 00.0 229 50.0 26.40 100 000.0 000 000.0 0 0040 0035 012 030.0 B 00 00 U1V0196 1";
    if (f != Framing::NONE) return "#-1";
    return "";
  };
  armAndProbe(r);
  const ProbeOutcome& o = r.eng.lastProbe();
  TEST_ASSERT_TRUE_MESSAGE(o.ok, o.summary);
  TEST_ASSERT_EQUAL((int)AdapterKind::Q6, (int)o.kind);
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
}

void test_probe_stops_on_foreign_master() {
  Rig r;
  const char* f = "(230.0 50.0 230.0 50.0 0100\r";
  r.port.foreignNoise.assign(f, f + strlen(f));
  armAndProbe(r);
  const ProbeOutcome& o = r.eng.lastProbe();
  TEST_ASSERT_TRUE(o.foreignMaster);
  TEST_ASSERT_EQUAL(0, (int)r.port.written.size());  // stopped before transmitting anything
}

void test_probe_smg_modbus_last_resort() {
  Rig r;
  r.port.baud = 9600;
  r.port.respondModbus = [](const std::vector<uint8_t>& q) -> std::vector<uint8_t> {
    uint16_t reg = (uint16_t)(q[2] << 8 | q[3]), qty = (uint16_t)(q[4] << 8 | q[5]);
    if (q[0] != 1 || reg != 201) return {};
    std::vector<uint8_t> r = {0x01, 0x03, (uint8_t)(qty * 2)};
    for (int i = 0; i < qty; i++) {
      int regno = 201 + i;
      int v = regno == 201 ? 2 : regno == 202 ? 2305 : regno == 210 ? 2300 : regno == 212 ? 5000 : regno == 215 ? 264 : regno == 229 ? 70 : 0;
      r.push_back((uint8_t)(v >> 8));
      r.push_back((uint8_t)(v & 0xFF));
    }
    uint16_t c = modbus::crc16(r.data(), r.size());
    r.push_back((uint8_t)(c & 0xFF));
    r.push_back((uint8_t)(c >> 8));
    return r;
  };
  armAndProbe(r);
  const ProbeOutcome& o = r.eng.lastProbe();
  TEST_ASSERT_TRUE_MESSAGE(o.ok, o.summary);
  TEST_ASSERT_EQUAL((int)AdapterKind::SMG, (int)o.kind);
  TEST_ASSERT_EQUAL(9600, (int)o.profile.baud);
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
}

void test_unarmed_engine_never_transmits() {
  Rig r;
  r.port.respond = revoDevice;
  r.eng.begin(LinkProfile(), AdapterState(), /*armed=*/false, false, r.clk.ms());
  TEST_ASSERT_FALSE(r.port.txEnabled);
  Sample s;
  r.eng.requestProbe();
  for (int i = 0; i < 300; i++) {
    r.clk.sleepMs(100);
    TEST_ASSERT_FALSE(r.eng.tick(r.clk.ms(), 0, s));
  }
  TEST_ASSERT_EQUAL((int)EngineState::DISARMED, (int)r.eng.state());
  TEST_ASSERT_EQUAL(0, (int)r.port.written.size());
  TEST_ASSERT_FALSE(r.port.txEnabled);
  // the safe mode never transmits either, even if "armed"
  Rig q;
  q.port.respond = revoDevice;
  q.eng.begin(LinkProfile(), AdapterState(), true, /*safeMode=*/true, q.clk.ms());
  for (int i = 0; i < 100; i++) {
    q.clk.sleepMs(100);
    q.eng.tick(q.clk.ms(), 0, s);
  }
  TEST_ASSERT_EQUAL(0, (int)q.port.written.size());
  // arming starts the probe
  r.eng.setArmed(true, r.clk.ms());
  TEST_ASSERT_TRUE(r.port.txEnabled);
  r.eng.tick(r.clk.ms(), 0, s);
  TEST_ASSERT_TRUE(r.eng.lastProbe().ok);
}

void test_light_probe_with_persisted_state() {
  Rig r;
  r.port.respond = revoDevice;
  AdapterState st;
  snprintf(st.adapter, sizeof st.adapter, "pi30");
  snprintf(st.dialect, sizeof st.dialect, "REVO");
  st.pv12 = 'W';
  st.supMask = cmdBit(Cmd::QPI) | cmdBit(Cmd::QPIGS) | cmdBit(Cmd::QALL) | cmdBit(Cmd::QPIRI);
  st.unsupMask = cmdBit(Cmd::QPIWS) | cmdBit(Cmd::QFLAG);
  r.eng.begin(LinkProfile(), st, true, false, r.clk.ms());
  TEST_ASSERT_EQUAL((int)EngineState::LIGHT, (int)r.eng.state());
  Sample s;
  r.eng.tick(r.clk.ms(), 0, s);
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  TEST_ASSERT_EQUAL(3, (int)r.port.commands.size());  // QALL x3, no full probe
  // persisted-unsupported QPIWS: one fresh try (0.7.4), demoted again on its first NAK, then never polled
  for (int i = 0; i < 1200; i++) {
    r.clk.sleepMs(50);
    r.eng.tick(r.clk.ms(), 0, s);
  }
  int qpiws = 0;
  for (const std::string& c : r.port.commands) qpiws += (c == "QPIWS");
  TEST_ASSERT_EQUAL(1, qpiws);
}

// Field case (2026-10-22, fw 0.7.3): ONE NAK of the fast REVO command QALL (2 s polling) marked it unsupported for
// 24 h; no fresh samples, dashboard "link down" for hours. A command that has answered must survive a NAK, and the
// fast command must never be demoted.
void test_single_nak_keeps_fast_command() {
  Rig r;
  r.port.respond = revoDevice;
  armAndProbe(r);
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  int qallSeen = 0;
  r.port.respond = [&](const std::string& c, Framing f) -> std::string {
    if (c == "QALL" && ++qallSeen <= 3) return "(NAK";  // even 3 in a row
    return revoDevice(c, f);
  };
  Sample s;
  int fresh = 0;
  for (int i = 0; i < 400; i++) {  // 20 s
    r.clk.sleepMs(50);
    fresh += r.eng.tick(r.clk.ms(), 0, s) ? 1 : 0;
  }
  TEST_ASSERT_TRUE_MESSAGE(qallSeen > 3, "QALL no longer polled after a NAK");
  TEST_ASSERT_TRUE(fresh >= 3);
  Identity id;
  r.eng.identity(id);
  TEST_ASSERT_NOT_NULL(strstr(id.sup, "QALL"));
}

// A working slow command survives isolated NAKs and is demoted only after DEMOTE_FAILS in a row.
void test_working_command_demoted_only_after_repeated_naks() {
  CommandScheduler sc;
  sc.add(Cmd::QALL, 2000, 0, true, 0);
  sc.add(Cmd::QPIWS, 10000, 0, false, 0);
  sc.markSupported(Cmd::QPIWS);
  sc.markSupported(Cmd::QALL);
  for (int i = 0; i < 50; i++) TEST_ASSERT_FALSE(sc.noteFail(Cmd::QALL, 0));  // fast: never
  for (int i = 0; i < cfg::DEMOTE_FAILS - 1; i++) TEST_ASSERT_FALSE(sc.noteFail(Cmd::QPIWS, 0));
  sc.noteOk(Cmd::QPIWS);  // an answer resets the count
  for (int i = 0; i < cfg::DEMOTE_FAILS - 1; i++) TEST_ASSERT_FALSE(sc.noteFail(Cmd::QPIWS, 0));
  TEST_ASSERT_TRUE(sc.noteFail(Cmd::QPIWS, 0));
  TEST_ASSERT_TRUE(sc.unsupported(Cmd::QPIWS));
  TEST_ASSERT_TRUE(sc.noteFail(Cmd::QMN, 0));  // never answered: demoted at once (probe semantics)
}

// 0.7.3 may have persisted QALL as unsupported on a REVO unit: the light probe proves it again and it is polled.
void test_persisted_unsupported_qall_recovers() {
  Rig r;
  r.port.respond = revoDevice;
  AdapterState st;
  snprintf(st.adapter, sizeof st.adapter, "pi30");
  snprintf(st.dialect, sizeof st.dialect, "REVO");
  st.pv12 = 'W';
  st.supMask = cmdBit(Cmd::QPI) | cmdBit(Cmd::QPIGS) | cmdBit(Cmd::QPIRI);
  st.unsupMask = cmdBit(Cmd::QALL) | cmdBit(Cmd::QPIWS);
  r.eng.begin(LinkProfile(), st, true, false, r.clk.ms());
  Sample s;
  r.eng.tick(r.clk.ms(), 0, s);
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  r.port.commands.clear();
  int fresh = 0;
  for (int i = 0; i < 200; i++) {
    r.clk.sleepMs(50);
    fresh += r.eng.tick(r.clk.ms(), 0, s) ? 1 : 0;
  }
  TEST_ASSERT_TRUE(fresh >= 3);
}

// Field case (2026-10-22): a known-good REVO unit boots with the RJ45 unplugged -> the light and full probes fail.
// It must retry after ~1 min (not 6 h) and recover by itself once the cable is back.
void test_boot_unplugged_with_persisted_state_retries_soon() {
  Rig r;
  r.port.respond = nullptr;  // cable out
  AdapterState st;
  snprintf(st.adapter, sizeof st.adapter, "pi30");
  snprintf(st.dialect, sizeof st.dialect, "REVO");
  st.pv12 = 'W';
  st.supMask = cmdBit(Cmd::QPI) | cmdBit(Cmd::QPIGS) | cmdBit(Cmd::QALL) | cmdBit(Cmd::QPIRI);
  r.eng.begin(LinkProfile(), st, true, false, r.clk.ms());
  Sample s;
  r.eng.tick(r.clk.ms(), 0, s);  // light probe fails -> PROBING
  r.eng.tick(r.clk.ms(), 0, s);  // full probe fails -> RAW with a short retry
  TEST_ASSERT_EQUAL((int)EngineState::RAW, (int)r.eng.state());
  r.port.respond = revoDevice;  // cable back
  bool ran = false;
  for (int i = 0; i < 1300 && !ran; i++) {  // 130 s of ticks
    r.clk.sleepMs(100);
    r.eng.tick(r.clk.ms(), 0, s);
    ran = (r.eng.state() == EngineState::RUN);
  }
  TEST_ASSERT_TRUE_MESSAGE(ran, "no recovery within ~2 min after the cable came back");
}

void test_link_loss_triggers_backoff_reprobe() {
  Rig r;
  r.port.respond = revoDevice;
  armAndProbe(r);
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  r.port.respond = nullptr;  // cable pulled
  Sample s;
  for (int i = 0; i < 20000 && r.eng.stats().reprobes == 0; i++) {
    r.clk.sleepMs(100);
    r.eng.tick(r.clk.ms(), 0, s);
  }
  TEST_ASSERT_TRUE(r.eng.stats().consecutiveFails >= cfg::REPROBE_FAILS);
  TEST_ASSERT_EQUAL(1, (int)r.eng.stats().reprobes);
  // the cable comes back: polling recovers without another full probe
  r.port.respond = revoDevice;
  bool fresh = false;
  for (int i = 0; i < 400 && !fresh; i++) {
    r.clk.sleepMs(50);
    fresh = r.eng.tick(r.clk.ms(), 0, s);
  }
  TEST_ASSERT_TRUE(fresh);
  TEST_ASSERT_EQUAL(0, (int)r.eng.stats().consecutiveFails);
  TEST_ASSERT_EQUAL(0, r.port.unsafeWrites);
  // Field case (2026-10-22, fw 0.7.2): the failed full probe during the silence wiped the REVO dialect, and polling
  // resumed as UNKNOWN (QPIGS/QMOD, no QALL). The dialect, identity and REVO schedule must survive.
  Identity id;
  r.eng.identity(id);
  TEST_ASSERT_EQUAL_STRING("REVO", id.dia);
  TEST_ASSERT_TRUE(id.ratva > 0);  // QPIRI ratings kept too
  r.port.commands.clear();
  for (int i = 0; i < 200; i++) {
    r.clk.sleepMs(50);
    r.eng.tick(r.clk.ms(), 0, s);
  }
  bool qall = false;
  for (const std::string& c : r.port.commands) qall |= (c == "QALL");
  TEST_ASSERT_TRUE_MESSAGE(qall, "REVO schedule (QALL) not restored");
}

// Field case (2026-10-22): QALL battery currents are whole amps (0 / 0) while the lithium BMS, via QLITH0, reports
// 0.9 A discharge (the LCD agrees with the BMS). 0.7.6 polls QLITH0 every 4 s and its values win while fresh.
void test_bms_current_overrides_whole_amp_qall() {
  Rig r;
  r.port.respond = [](const std::string& c, Framing f) -> std::string {
    if (c == "QLITH0") return "(026.5 000.0 000.9 300 100 000 100.0 010.0 028.4 002.2 0 0";
    return revoDevice(c, f);
  };
  armAndProbe(r);
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  Sample s, last;
  bool got = false;
  for (int i = 0; i < 400; i++) {  // 20 s
    r.clk.sleepMs(50);
    if (r.eng.tick(r.clk.ms(), 0, s)) {
      last = s;
      got = true;
    }
  }
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.9f, last.bdi);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0f, last.bci);
  TEST_ASSERT_FLOAT_WITHIN(0.1, -26.5f * 0.9f, last.bw);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 100.f, last.soc);
  int qlith = 0;
  for (const std::string& c : r.port.commands) qlith += (c == "QLITH0");
  TEST_ASSERT_TRUE_MESSAGE(qlith >= 4, "QLITH0 not polled periodically");
}

// A pi30 state persisted without a dialect (possible with 0.7.2 after the case above) gets a full probe at boot.
void test_persisted_unknown_dialect_reclassifies() {
  Rig r;
  r.port.respond = revoDevice;
  AdapterState st;
  snprintf(st.adapter, sizeof st.adapter, "pi30");
  snprintf(st.dialect, sizeof st.dialect, "UNKNOWN");
  st.supMask = cmdBit(Cmd::QPI) | cmdBit(Cmd::QPIGS) | cmdBit(Cmd::QMOD);
  r.eng.begin(LinkProfile(), st, true, false, r.clk.ms());
  Sample s;
  r.eng.tick(r.clk.ms(), 0, s);  // light probe refuses -> PROBING
  r.eng.tick(r.clk.ms(), 0, s);  // full probe
  TEST_ASSERT_EQUAL((int)EngineState::RUN, (int)r.eng.state());
  Identity id;
  r.eng.identity(id);
  TEST_ASSERT_EQUAL_STRING("REVO", id.dia);
}

void test_scheduler_rules() {
  CommandScheduler s;
  uint32_t t = 1000;
  s.add(Cmd::QPIGS, 5000, 0, true, t);
  s.add(Cmd::QMOD, 10000, 1000, false, t);
  s.add(Cmd::QPIWS, 10000, 6000, false, t);
  int i = s.due(t);
  TEST_ASSERT_EQUAL((int)Cmd::QPIGS, (int)s.entry(i).cmd);
  s.done(i, t);
  TEST_ASSERT_EQUAL(-1, s.due(t + 500));
  i = s.due(t + 1000);
  TEST_ASSERT_EQUAL((int)Cmd::QMOD, (int)s.entry(i).cmd);
  s.done(i, t + 1000);
  i = s.due(t + 5000);  // fast wins
  TEST_ASSERT_EQUAL((int)Cmd::QPIGS, (int)s.entry(i).cmd);
  s.done(i, t + 5000);
  i = s.due(t + 6000);
  TEST_ASSERT_EQUAL((int)Cmd::QPIWS, (int)s.entry(i).cmd);
  // unsupported: skipped, retried after 24 h
  s.markUnsupported(Cmd::QPIWS, t);
  s.done(i, t + 6000);
  TEST_ASSERT_EQUAL(-1, s.due(t + 16000) >= 0 && s.entry(s.due(t + 16000)).cmd == Cmd::QPIWS ? 0 : -1);
  TEST_ASSERT_TRUE(s.usable(Cmd::QPIWS, t + cfg::UNSUPPORTED_RETRY_MS));
  TEST_ASSERT_FALSE(s.usable(Cmd::QPIWS, t + 3600000));
  s.trigger(Cmd::QMOD, t + 7000);
  i = s.due(t + 7000);
  TEST_ASSERT_EQUAL((int)Cmd::QMOD, (int)s.entry(i).cmd);
}

// ---- receive-only pin diagnostics
void test_pin_diag_idle_classify_and_arm_gate() {
  TEST_ASSERT_EQUAL((int)IdleVerdict::TX_IDLES_NEGATIVE, (int)PinDiag::classify(1000, 1000, 0));
  TEST_ASSERT_EQUAL((int)IdleVerdict::TX_IDLES_NEGATIVE, (int)PinDiag::classify(1000, 991, 2));
  TEST_ASSERT_EQUAL((int)IdleVerdict::UNDECIDED, (int)PinDiag::classify(1000, 989, 2));
  TEST_ASSERT_EQUAL((int)IdleVerdict::HIGH_NOT_TX, (int)PinDiag::classify(1000, 0, 0));
  TEST_ASSERT_EQUAL((int)IdleVerdict::ACTIVE_TRAFFIC, (int)PinDiag::classify(1000, 500, 300));

  MockClock clk;
  MockGpio gpio;
  gpio.clk = &clk;
  PinDiag pd(gpio, clk);
  gpio.levels[18] = 0;  // RO LOW: A sits below B -> candidate idles negative
  IdleResult a = pd.idleTest(1, 18);
  TEST_ASSERT_EQUAL((int)IdleVerdict::TX_IDLES_NEGATIVE, (int)a.verdict);
  TEST_ASSERT_TRUE(a.samples >= 990 && a.samples <= 1002);
  TEST_ASSERT_EQUAL(GpioMode::IN_PULLUP, gpio.modes[18]);  // only ever an input
  TEST_ASSERT_TRUE(gpio.modes.find(17) == gpio.modes.end());  // GPIO17 never touched

  IdleHistory h;
  ArmCheck ac = PinDiag::armCheck(h, 2);
  TEST_ASSERT_FALSE(ac.ok);
  h.add(a);
  ac = PinDiag::armCheck(h, 2);
  TEST_ASSERT_FALSE(ac.ok);  // only one confirmation
  h.add(pd.idleTest(1, 18));
  ac = PinDiag::armCheck(h, 2);
  TEST_ASSERT_FALSE(ac.ok);  // pin 2 not tested yet
  gpio.levels[18] = 1;
  IdleResult b = pd.idleTest(2, 18);
  TEST_ASSERT_EQUAL((int)IdleVerdict::HIGH_NOT_TX, (int)b.verdict);
  h.add(b);
  // safety review: the RX pin also needs the upper check (B- on 3V3) twice before anything else
  ac = PinDiag::armCheck(h, 2);
  TEST_ASSERT_FALSE(ac.ok);
  TEST_ASSERT_EQUAL(2, ac.upperPin);
  TEST_ASSERT_EQUAL(0, ac.rxRj);  // the UI is not told to wire GPIO17 yet
  gpio.levels[18] = 0;            // B- on 3V3, A+ on the RX input (~1.7 V) -> RO LOW = below 3.3 V
  h.add(pd.idleTest(2, 18, IdleRef::V3V3));
  TEST_ASSERT_FALSE(PinDiag::armCheck(h, 2).ok);  // only one confirmation
  h.add(pd.idleTest(2, 18, IdleRef::V3V3));
  TEST_ASSERT_EQUAL_STRING("below-3V3", idleVerdictName(h.latestUp(2)->verdict));
  TEST_ASSERT_EQUAL((int)IdleVerdict::HIGH_NOT_TX, (int)h.latest(2)->verdict);  // ref-GND history untouched
  ac = PinDiag::armCheck(h, 0);  // status query: ready, but nothing requested
  TEST_ASSERT_FALSE(ac.ok);
  TEST_ASSERT_EQUAL(2, ac.rxRj);
  TEST_ASSERT_NOT_NULL(strstr(ac.reason, "ready"));
  ac = PinDiag::armCheck(h, 1);
  TEST_ASSERT_FALSE(ac.ok);  // GPIO17 on the TX pin is refused
  TEST_ASSERT_NOT_NULL(strstr(ac.reason, "refused"));
  ac = PinDiag::armCheck(h, 2);
  TEST_ASSERT_TRUE_MESSAGE(ac.ok, ac.reason);
  TEST_ASSERT_EQUAL(1, ac.txRj);
  TEST_ASSERT_EQUAL(2, ac.rxRj);
  // both pins LOW is impossible for RS232 -> refused
  IdleHistory h2;
  gpio.levels[18] = 0;
  for (int pin = 1; pin <= 2; pin++)
    for (int k = 0; k < 2; k++) h2.add(pd.idleTest((uint8_t)pin, 18));
  TEST_ASSERT_FALSE(PinDiag::armCheck(h2, 2).ok);
  // traffic on the line
  gpio.fn = [](int, uint32_t t) { return (int)((t / 30) & 1); };
  IdleResult c = pd.idleTest(1, 18);
  TEST_ASSERT_EQUAL((int)IdleVerdict::ACTIVE_TRAFFIC, (int)c.verdict);
}

// Safety review: upper check (B- on 3V3) catches a POSITIVE pin that the ref-GND test reads as "HIGH, not TX"
void test_pin_diag_upper_check_blocks_positive_pin() {
  TEST_ASSERT_EQUAL((int)IdleVerdict::BELOW_3V3, (int)PinDiag::classify(1000, 1000, 0, IdleRef::V3V3));
  TEST_ASSERT_EQUAL((int)IdleVerdict::ABOVE_3V3, (int)PinDiag::classify(1000, 0, 0, IdleRef::V3V3));
  TEST_ASSERT_EQUAL((int)IdleVerdict::ACTIVE_TRAFFIC, (int)PinDiag::classify(1000, 500, 300, IdleRef::V3V3));
  MockClock clk;
  MockGpio gpio;
  gpio.clk = &clk;
  PinDiag pd(gpio, clk);
  IdleHistory h;
  gpio.levels[18] = 0;
  h.add(pd.idleTest(1, 18));
  h.add(pd.idleTest(1, 18));  // pin 1 = inverter TX, twice
  gpio.levels[18] = 1;
  h.add(pd.idleTest(2, 18));  // "pin 2" HIGH: could be the RX input... or pin 4 (+VCC) after a colour mix-up
  h.add(pd.idleTest(2, 18, IdleRef::V3V3));  // upper check: still HIGH -> above 3.3 V
  ArmCheck ac = PinDiag::armCheck(h, 2);
  TEST_ASSERT_FALSE(ac.ok);
  TEST_ASSERT_NOT_NULL(strstr(ac.reason, "STOP"));
  TEST_ASSERT_EQUAL(0, ac.rxRj);
  gpio.levels[18] = 0;
  h.add(pd.idleTest(2, 18, IdleRef::V3V3));  // later LOW results never wash out an ABOVE result (sticky)
  h.add(pd.idleTest(2, 18, IdleRef::V3V3));
  h.add(pd.idleTest(2, 18, IdleRef::V3V3));
  TEST_ASSERT_FALSE(PinDiag::armCheck(h, 2).ok);
  TEST_ASSERT_NOT_NULL(strstr(PinDiag::armCheck(h, 2).reason, "STOP"));
  TEST_ASSERT_TRUE(gpio.modes.find(17) == gpio.modes.end());  // GPIO17 never touched by the idle tests

  // the RX pin must never have read "inverter-TX" (the wiring changed between tests)
  IdleHistory g;
  gpio.levels[18] = 0;
  g.add(pd.idleTest(2, 18));  // pin 2 once looked like TX
  g.add(pd.idleTest(1, 18));
  g.add(pd.idleTest(1, 18));
  gpio.levels[18] = 1;
  for (int k = 0; k < IdleHistory::N; k++) g.add(pd.idleTest(2, 18));  // pushes the TX result out of the ring...
  gpio.levels[18] = 0;
  g.add(pd.idleTest(2, 18, IdleRef::V3V3));
  g.add(pd.idleTest(2, 18, IdleRef::V3V3));
  ac = PinDiag::armCheck(g, 2);  // ...but the contradiction is remembered until reset-link
  TEST_ASSERT_FALSE(ac.ok);
  TEST_ASSERT_NOT_NULL(strstr(ac.reason, "wiring changed"));
  TEST_ASSERT_TRUE(g.any());
  TEST_ASSERT_FALSE(IdleHistory().any());
}

// Safety review: receive-only pre-arm check of GPIO17 (pull-down) and RO
void test_pin_diag_prearm_check() {
  MockClock clk;
  MockGpio gpio;
  gpio.clk = &clk;
  PinDiag pd(gpio, clk);
  gpio.levels[17] = 0;  // GPIO17 on the inverter RX input (3-7 kOhm to GND)
  gpio.levels[18] = 0;  // RO LOW: A+ on the idling inverter TX
  PreArmResult p = pd.preArmCheck(17, 18, true);
  TEST_ASSERT_TRUE_MESSAGE(p.ok, p.reason);
  TEST_ASSERT_TRUE(p.samples >= 30);
  // GPIO17 is only ever an input: pull-down while sampling, then high-Z
  for (auto& m : gpio.modeLog)
    if (m.first == 17) TEST_ASSERT_TRUE(m.second == GpioMode::IN_PULLDOWN || m.second == GpioMode::IN_HIZ);
  TEST_ASSERT_EQUAL(GpioMode::IN_HIZ, gpio.modes[17]);
  gpio.levels[17] = 1;  // touching the DI wrap (GPIO16 held HIGH) or a positive pin
  p = pd.preArmCheck(17, 18, true);
  TEST_ASSERT_FALSE(p.ok);
  TEST_ASSERT_NOT_NULL(strstr(p.reason, "GPIO17"));
  TEST_ASSERT_EQUAL(GpioMode::IN_HIZ, gpio.modes[17]);
  gpio.levels[17] = 0;
  gpio.levels[18] = 1;  // RO HIGH: A+ not on the inverter TX (or GPIO16/18 wraps touching)
  p = pd.preArmCheck(17, 18, true);
  TEST_ASSERT_FALSE(p.ok);
  TEST_ASSERT_NOT_NULL(strstr(p.reason, "RO"));
  p = pd.preArmCheck(17, 18, false);  // boot-time / MAX3232 variant ignores RO
  TEST_ASSERT_TRUE_MESSAGE(p.ok, p.reason);
}

void test_pin_diag_self_id() {
  MockClock clk;
  MockGpio gpio;
  gpio.clk = &clk;
  PinDiag pd(gpio, clk);
  gpio.levels[18] = 1;
  gpio.levels[16] = 1;
  SelfIdResult r = pd.selfId(18, 16);
  TEST_ASSERT_EQUAL((int)SelfIdVerdict::OPEN_OK, (int)r.verdict);
  gpio.levels[18] = 0;  // A+ -> GND, B- -> 3V3: RO goes LOW
  r = pd.selfId(18, 16);
  TEST_ASSERT_EQUAL((int)SelfIdVerdict::RO_FOUND, (int)r.verdict);
  TEST_ASSERT_EQUAL(18, r.roPin);
  TEST_ASSERT_EQUAL(16, r.diPin);
  gpio.levels[18] = 1;
  gpio.levels[16] = 0;  // header labels the other way round -> auto-swap
  r = pd.selfId(18, 16);
  TEST_ASSERT_EQUAL(16, r.roPin);
  TEST_ASSERT_EQUAL(18, r.diPin);
  gpio.levels[18] = 0;
  r = pd.selfId(18, 16);
  TEST_ASSERT_EQUAL((int)SelfIdVerdict::BOTH_LOW, (int)r.verdict);
  TEST_ASSERT_EQUAL(GpioMode::IN_PULLUP, gpio.modes[16]);
  TEST_ASSERT_EQUAL(GpioMode::IN_PULLUP, gpio.modes[18]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_probe_revo);
  RUN_TEST(test_probe_stock21_with_reversed_rx_polarity);
  RUN_TEST(test_probe_silence_falls_back_to_raw);
  RUN_TEST(test_probe_legacy_q6);
  RUN_TEST(test_probe_stops_on_foreign_master);
  RUN_TEST(test_probe_smg_modbus_last_resort);
  RUN_TEST(test_unarmed_engine_never_transmits);
  RUN_TEST(test_light_probe_with_persisted_state);
  RUN_TEST(test_boot_unplugged_with_persisted_state_retries_soon);
  RUN_TEST(test_persisted_unknown_dialect_reclassifies);
  RUN_TEST(test_bms_current_overrides_whole_amp_qall);
  RUN_TEST(test_single_nak_keeps_fast_command);
  RUN_TEST(test_working_command_demoted_only_after_repeated_naks);
  RUN_TEST(test_persisted_unsupported_qall_recovers);
  RUN_TEST(test_link_loss_triggers_backoff_reprobe);
  RUN_TEST(test_scheduler_rules);
  RUN_TEST(test_pin_diag_idle_classify_and_arm_gate);
  RUN_TEST(test_pin_diag_upper_check_blocks_positive_pin);
  RUN_TEST(test_pin_diag_prearm_check);
  RUN_TEST(test_pin_diag_self_id);
  return UNITY_END();
}
