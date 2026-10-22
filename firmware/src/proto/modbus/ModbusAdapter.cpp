// ModbusAdapter.cpp - see ModbusAdapter.h.
#include "proto/modbus/ModbusAdapter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "model/Decode.h"
#include "proto/modbus/ModbusRtu.h"

static float s16(uint16_t v) { return (float)(int16_t)v; }

bool ModbusAdapter::decodeSmg(const uint16_t* r, int n, Sample& s) {
  if (n < 34) return false;
  auto R = [&](int reg) { return r[reg - 201]; };
  static const char modes[] = {'P', 'S', 'L', 'B', 'Y', 'C', 'F'};  // 201: 0 power-on .. 6 fault
  uint16_t m = R(201);
  s.md = m < sizeof modes ? modes[m] : 0;
  s.gv = s16(R(202)) * 0.1f;
  s.gf = s16(R(203)) * 0.01f;
  s.ov = s16(R(210)) * 0.1f;
  s.of = s16(R(212)) * 0.01f;
  s.w = s16(R(213));
  s.va = s16(R(214));
  s.bv = s16(R(215)) * 0.1f;
  s.pvv = s16(R(219)) * 0.1f;
  s.pvi = s16(R(220)) * 0.1f;
  s.pvw = s16(R(223));
  s.ld = s16(R(225));
  s.tmp = s16(R(227));
  s.soc = (float)R(229);
  float bi = s16(R(232)) * 0.1f;  // + = charging
  s.bci = bi > 0 ? bi : 0.f;
  s.bdi = bi < 0 ? -bi : 0.f;
  s.finalizeDerived();
  bool allZero = true;
  for (int i = 0; i < n; i++)
    if (r[i]) { allZero = false; break; }
  return !allZero;
}

bool ModbusAdapter::decodePowmr(const uint16_t* r, int n, Sample& s) {
  if (n < 14) return false;
  auto R = [&](int reg) { return r[reg - 4501]; };
  s.gv = R(4502) * 0.1f;
  s.gf = R(4503) * 0.1f;
  s.pvv = R(4504) * 0.1f;
  s.pvw = (float)R(4505);
  s.bv = R(4506) * 0.1f;
  s.soc = (float)R(4507);
  s.bci = (float)R(4508);
  s.bdi = (float)R(4509);
  s.ov = R(4510) * 0.1f;
  s.of = R(4511) * 0.1f;
  s.w = (float)R(4512);
  s.va = (float)R(4513);
  s.ld = (float)R(4514);
  s.finalizeDerived();
  bool allZero = true;
  for (int i = 0; i < n; i++)
    if (r[i]) { allZero = false; break; }
  return !allZero;
}

bool ModbusAdapter::readBlock(SerialLink& l, uint16_t reg, uint16_t qty, uint16_t* out, LinkResult& r) {
  uint8_t f[8];
  uint8_t slave = fam_ == ModbusFamily::SMG ? 1 : 5;
  modbus::buildRead(slave, reg, qty, f);
  r = l.queryModbus(f, 8, (size_t)5 + qty * 2, reply_);
  if (r != LinkResult::OK) return false;
  return modbus::parseRead(reply_.raw, reply_.rawLen, slave, qty, out, fam_ == ModbusFamily::POWMR) ==
         modbus::RespCheck::OK;
}

ProbeResult ModbusAdapter::probe(SerialLink& l, ProbeLog& log) {
  ProbeResult pr;
  snprintf(pr.family, sizeof pr.family, "%s", id());
  uint16_t regs[45];
  LinkResult r;
  Sample s;
  bool ok = false;
  if (fam_ == ModbusFamily::SMG) {
    uint16_t mode;
    if (readBlock(l, 201, 1, &mode, r) && mode <= 6) {
      log.add(l.clock().ms(), "SMG reg201 -> %u", (unsigned)mode);
      ok = readBlock(l, 201, 34, regs, r) && decodeSmg(regs, 34, s);  // all-zero data is rejected
    }
  } else {
    ok = readBlock(l, 4501, 45, regs, r) && decodePowmr(regs, 45, s);
  }
  log.add(l.clock().ms(), "%s FC03 probe -> %s%s", id(), linkResultName(r), ok ? " (data)" : "");
  if (!ok) {
    pr.code = r == LinkResult::TIMEOUT ? ProbeCode::SILENCE : ProbeCode::GARBAGE;
    snprintf(pr.detail, sizeof pr.detail, "%s: %s", id(), linkResultName(r));
    return pr;
  }
  pr.ok = decode::plausibleSample(s);
  pr.code = pr.ok ? ProbeCode::OK : ProbeCode::NO_DATA;
  pr.score = pr.ok ? 50 : 20;
  snprintf(pr.dialect, sizeof pr.dialect, "%s", id());
  snprintf(pr.detail, sizeof pr.detail, "%s Modbus FC03 detected (basic decode; bv=%.1f)", id(), s.bv);
  ident_ = Identity();
  snprintf(ident_.family, sizeof ident_.family, "%s", id());
  snprintf(ident_.dia, sizeof ident_.dia, "%s", id());
  return pr;
}

bool ModbusAdapter::lightProbe(SerialLink& l, ProbeLog& log) {
  uint16_t regs[45];
  LinkResult r;
  Sample s;
  bool ok = fam_ == ModbusFamily::SMG ? (readBlock(l, 201, 34, regs, r) && decodeSmg(regs, 34, s))
                                      : (readBlock(l, 4501, 45, regs, r) && decodePowmr(regs, 45, s));
  log.add(l.clock().ms(), "light %s -> %s", id(), linkResultName(r));
  return ok;
}

void ModbusAdapter::schedule(uint32_t nowMs) {
  sched_.clear();
  sched_.add(Cmd::QPIGS, cfg::P_FAST_MS, 0, true, nowMs);  // slot = "fast block read"
}

bool ModbusAdapter::step(SerialLink& l, uint32_t nowMs, Sample& io) {
  last_ = StepInfo();
  int idx = sched_.due(nowMs);
  if (idx < 0) return false;
  uint16_t regs[45];
  LinkResult r;
  bool ok = fam_ == ModbusFamily::SMG ? (readBlock(l, 201, 34, regs, r) && decodeSmg(regs, 34, io))
                                      : (readBlock(l, 4501, 45, regs, r) && decodePowmr(regs, 45, io));
  ok = ok && decode::plausibleSample(io);
  sched_.done(idx, l.clock().ms());
  last_.ran = true;
  last_.fast = true;
  last_.ok = ok;
  last_.res = r;
  return ok;
}

void ModbusAdapter::exportState(AdapterState& st) const {
  st = AdapterState();
  snprintf(st.adapter, sizeof st.adapter, "%s", id());
  snprintf(st.dialect, sizeof st.dialect, "%s", id());
}

void ModbusAdapter::importState(const AdapterState& st) {
  (void)st;
  ident_ = Identity();
  snprintf(ident_.family, sizeof ident_.family, "%s", id());
  snprintf(ident_.dia, sizeof ident_.dia, "%s", id());
}
