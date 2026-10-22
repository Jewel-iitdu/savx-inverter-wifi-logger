// Pi30Adapter.cpp - see Pi30Adapter.h.
#include "proto/pi30/Pi30Adapter.h"

#include <stdio.h>
#include <string.h>

#include "model/Decode.h"
#include "model/TimeUtil.h"

Dialect Pi30Adapter::classify(bool qallOk, int qpiriFields, const char* qflagAll, int qpiwsLen, char qmod,
                              int qpigsFields) {
  if (qallOk) return Dialect::REVO;                                             // rule 1
  if (qpiriFields >= 28 || (qflagAll && strchr(qflagAll, 'd'))) return Dialect::MAX24;  // rule 2
  if (qpiwsLen == 36 || (qmod && strchr("ECYD", qmod))) return Dialect::GK;     // rule 3
  return qpigsFields >= 21 ? Dialect::STOCK21 : Dialect::STOCK17;               // rule 4
}

LinkResult Pi30Adapter::send(SerialLink& l, Cmd c) {
  Framing f = CMD_INFO[(int)c].framing;
  const char* name = CMD_INFO[(int)c].name;
  if (c == Cmd::QALL) f = qallCrc_ ? Framing::CRC : Framing::CHK;
  if (c == Cmd::QLITH0) f = qlithChk_ ? Framing::CHK : Framing::CRC;
  if (c == Cmd::QED || c == Cmd::QLD) {
    if (!tu::epochValid(epoch_)) return LinkResult::REFUSED;  // date unknown: skip, nothing sent
    char d[12];
    tu::yyyymmdd(epoch_, d, sizeof d);
    snprintf(cmdBuf_, sizeof cmdBuf_, "%s%s", name, d);
    name = cmdBuf_;
  }
  return l.query(name, f, reply_);
}

void Pi30Adapter::notePv12(const pi30p::Qpigs& q) {
  if (dialect_ == Dialect::REVO) {
    if (pv12_ != 'W') { pv12_ = 'W'; dirty_ = true; }  // REVO doc: field 12 is PV power
    return;
  }
  char c = decode::classifyPv12(q);
  if (c == 'W' && pv12_ != 'W') {  // decisive: > 15 A is impossible
    pv12_ = 'W';
    dirty_ = true;
  } else if (c == 'A' && pv12_ == '?') {
    if (++pv12VotesA_ >= 3) { pv12_ = 'A'; dirty_ = true; }
  }
}

bool Pi30Adapter::handle(Cmd c, LinkResult r, uint32_t nowMs, Sample& io) {
  const char* p = reply_.payload;
  bool ok = r == LinkResult::OK;
  if (r == LinkResult::NAK && CMD_INFO[(int)c].optional) {
    if (sched_.noteFail(c, nowMs)) rebuildSup();  // 0.7.4: one NAK of a working command is transient
    return false;
  }
  if (!ok) return false;
  bool valid = false;
  switch (c) {
    case Cmd::QPIGS: {
      pi30p::Qpigs q;
      if (pi30p::parseQpigs(p, q) && (dialect_ == Dialect::REVO || decode::plausibleQpigs(q))) {
        notePv12(q);
        decode::qpigs(q, dialect_, pv12_, io);
        applyBms(io, nowMs);
        valid = true;
        if (io.stBit(6)) onConfigChangedBit(nowMs);
      }
      break;
    }
    case Cmd::QALL: {
      pi30p::Qall a;
      if (pi30p::parseQall(p, a) && decode::plausibleQall(a)) {
        decode::qall(a, io);
        applyBms(io, nowMs);
        valid = true;
      }
      break;
    }
    case Cmd::QMOD: {
      char m = pi30p::parseQmod(p);
      if (m) { decode::qmod(m, io); valid = true; }
      break;
    }
    case Cmd::QPIWS: {
      char wf[41];
      if (pi30p::parseQpiws(p, wf, sizeof wf)) { decode::qpiws(wf, dialect_, io); valid = true; }
      break;
    }
    case Cmd::QPIRI: {
      pi30p::Qpiri q;
      if (pi30p::parseQpiri(p, q)) {
        ident_.qpiriFields = q.n;
        ident_.ratGridV = q.v[0];
        ident_.ratGridA = q.v[1];
        ident_.ratOutV = q.v[2];
        ident_.ratOutHz = q.v[3];
        ident_.ratOutA = q.v[4];
        ident_.ratva = q.v[5];
        ident_.ratw = q.v[6];
        ident_.ratbv = q.v[7];
        ident_.rechgV = q.v[8];
        ident_.cutV = q.v[9];
        ident_.bulkV = q.v[10];
        ident_.floatV = q.v[11];
        ident_.btype = isnan(q.v[12]) ? -1 : (int)q.v[12];
        ident_.maxAcChg = isnan(q.v[13]) ? -1 : (int)q.v[13];
        ident_.maxChg = isnan(q.v[14]) ? -1 : (int)q.v[14];
        ident_.inrng = isnan(q.v[15]) ? -1 : (int)q.v[15];
        ident_.outpri = q.n > 16 && !isnan(q.v[16]) ? (int)q.v[16] : -1;
        ident_.chgpri = q.n > 17 && !isnan(q.v[17]) ? (int)q.v[17] : -1;
        ident_.machine = q.n > 19 && !isnan(q.v[19]) ? (int)q.v[19] : -1;
        ident_.redischgV = q.n > 22 ? q.v[22] : NAN;
        valid = true;
      }
      break;
    }
    case Cmd::QFLAG: {
      char en[28], dis[28];
      if (pi30p::parseQflag(p, en, sizeof en, dis, sizeof dis)) {
        snprintf(ident_.qflag, sizeof ident_.qflag, "%s", p);
        valid = true;
      }
      break;
    }
    case Cmd::QPI:
      if (p[0] == 'P' && p[1] == 'I') { snprintf(ident_.qpi, sizeof ident_.qpi, "%s", p); valid = true; }
      break;
    case Cmd::QID: valid = pi30p::parseQid(p, ident_.qid, sizeof ident_.qid); break;
    case Cmd::QSID: if (!ident_.qid[0]) valid = pi30p::parseQsid(p, ident_.qid, sizeof ident_.qid); else valid = true; break;
    case Cmd::QVFW: valid = pi30p::parseVersion(p, "VERFW:", ident_.vfw, sizeof ident_.vfw); break;
    case Cmd::QVFW2: valid = pi30p::parseVersion(p, "VERFW2:", ident_.vfw2, sizeof ident_.vfw2); break;
    case Cmd::QVFW3: valid = pi30p::parseVersion(p, "VERFW3:", ident_.vfw3, sizeof ident_.vfw3); break;
    case Cmd::QMN: if (p[0]) { snprintf(ident_.qmn, sizeof ident_.qmn, "%s", p); valid = true; } break;
    case Cmd::QGMN: if (p[0]) { snprintf(ident_.qgmn, sizeof ident_.qgmn, "%s", p); valid = true; } break;
    case Cmd::QET: {
      uint32_t wh;
      if (pi30p::parseEnergyWh(p, wh)) { io.etot = (float)wh / 1000.f; valid = true; }
      break;
    }
    case Cmd::QED: {
      uint32_t wh;
      if (pi30p::parseEnergyWh(p, wh)) { io.epvi = (float)wh; valid = true; }
      break;
    }
    case Cmd::QLD: {
      uint32_t wh;
      valid = pi30p::parseEnergyWh(p, wh);  // inverter-reported load Wh: raw only (no Sample key in d1 §3.8)
      break;
    }
    case Cmd::Q1: {
      pi30p::Q1 q;
      if (pi30p::parseQ1(p, q)) { decode::q1(q, io); valid = true; }
      break;
    }
    case Cmd::QT: {
      uint32_t e = tu::parseQt(p);
      if (e) { ident_.qtEpoch = e; ident_.qtAtMs = nowMs; valid = true; }
      break;
    }
    case Cmd::PQSE: {
      pi30p::Pqse q;
      if (pi30p::parsePqse(p, q)) { snprintf(ident_.pqse, sizeof ident_.pqse, "%s", p); valid = true; }
      break;
    }
    case Cmd::QLITH0: {
      pi30p::Qlith0 b;
      if (pi30p::parseQlith0(p, b)) {
        bmsV_ = b.v[0];
        bmsChg_ = b.v[1];
        bmsDis_ = b.v[2];
        bmsSoc_ = b.v[4];
        bmsAtMs_ = nowMs;
        haveBms_ = true;
        applyBms(io, nowMs);
        valid = true;
      } else {
        valid = p[0] != 0;  // answered but not the 12-field layout: raw capture only
      }
      break;
    }
    case Cmd::QDI:
    case Cmd::QPIGS2:
    case Cmd::PQSA:
      valid = p[0] != 0;  // raw capture only (kept in the RawRing for /api/raw and diag/raw)
      break;
    default:
      break;
  }
  if (valid) sched_.noteOk(c);
  if (valid && !sched_.supported(c)) {
    sched_.markSupported(c);
    rebuildSup();
  }
  return valid;
}

void Pi30Adapter::rebuildSup() {
  size_t o = 0;
  ident_.sup[0] = 0;
  for (int i = 0; i < (int)Cmd::COUNT; i++) {
    if (!sched_.supported((Cmd)i)) continue;
    o += (size_t)snprintf(ident_.sup + o, sizeof ident_.sup - o, o ? ",%s" : "%s", CMD_INFO[i].name);
    if (o >= sizeof ident_.sup) break;
  }
  dirty_ = true;
}

void Pi30Adapter::applyBms(Sample& io, uint32_t nowMs) const {
  if (!haveBms_ || (uint32_t)(nowMs - bmsAtMs_) > cfg::BMS_FRESH_MS) return;
  io.bci = bmsChg_;
  io.bdi = bmsDis_;
  if (!isnan(bmsV_)) io.bv = bmsV_;
  if (!isnan(bmsSoc_)) io.soc = bmsSoc_;
  io.finalizeDerived();
}

bool Pi30Adapter::tryOptional(SerialLink& l, ProbeLog& log, Cmd c, Sample& scratch, uint32_t nowMs) {
  LinkResult r = send(l, c);
  bool v = handle(c, r, nowMs, scratch);
  if (!v && r != LinkResult::NAK && CMD_INFO[(int)c].optional) {
    // timeout / garbage on an otherwise working link: treat as unsupported for now (retried daily)
    sched_.markUnsupported(c, nowMs);
    rebuildSup();
  }
  log.add(l.clock().ms(), "%-7s -> %s%s%.40s", CMD_INFO[(int)c].name, linkResultName(r), reply_.payload[0] ? " (" : "",
          reply_.payload);
  return v;
}

ProbeResult Pi30Adapter::probe(SerialLink& l, ProbeLog& log) {
  ProbeResult pr;
  snprintf(pr.family, sizeof pr.family, "pi30");
  uint32_t now = l.clock().ms();
  ident_ = Identity();
  sched_ = CommandScheduler();
  dialect_ = Dialect::UNKNOWN;
  pv12_ = '?';
  pv12VotesA_ = 0;
  qallCrc_ = qlithChk_ = false;

  // D1 / D1b: QPI up to 3x (the first command after idle can time out)
  bool qpiOk = false, nak = false;
  int garbage = 0;
  for (int i = 0; i < 3 && !qpiOk; i++) {
    LinkResult r = l.query("QPI", Framing::CRC, reply_);
    log.add(l.clock().ms(), "QPI #%d -> %s %.24s", i + 1, linkResultName(r), reply_.payload);
    if (r == LinkResult::OK && reply_.payload[0] == 'P' && reply_.payload[1] == 'I') {
      snprintf(ident_.qpi, sizeof ident_.qpi, "%s", reply_.payload);
      qpiOk = true;
    } else if (r == LinkResult::NAK) {
      nak = true;
      break;
    } else if (r == LinkResult::LEGACY_HASH) {
      pr.code = ProbeCode::LEGACY_HASH;
      snprintf(pr.detail, sizeof pr.detail, "QPI answered '%.8s' (legacy RP/RS firmware)", reply_.payload);
      return pr;
    } else if (r == LinkResult::BAD_START || r == LinkResult::BAD_CRC || r == LinkResult::RX_OVERFLOW) {
      garbage++;
    } else if (r == LinkResult::NOT_ARMED || r == LinkResult::REFUSED) {
      pr.code = ProbeCode::NOT_ARMED;
      snprintf(pr.detail, sizeof pr.detail, "link not armed: nothing sent");
      return pr;
    }
  }
  if (!qpiOk && !nak) {
    pr.code = garbage ? ProbeCode::GARBAGE : ProbeCode::SILENCE;
    snprintf(pr.detail, sizeof pr.detail, "QPI: %s", garbage ? "garbage bytes (polarity?)" : "no reply");
    return pr;
  }
  if (qpiOk) sched_.markSupported(Cmd::QPI);

  // D2: QPIGS must parse and be plausible
  Sample s;
  pi30p::Qpigs q;
  bool qpigsOk = false;
  for (int i = 0; i < 3 && !qpigsOk; i++) {
    LinkResult r = l.query("QPIGS", Framing::CRC, reply_);
    log.add(l.clock().ms(), "QPIGS #%d -> %s %.60s", i + 1, linkResultName(r), reply_.payload);
    if (r == LinkResult::OK && pi30p::parseQpigs(reply_.payload, q)) qpigsOk = true;
  }
  if (!qpigsOk) {
    pr.code = nak ? ProbeCode::NAK_ONLY : ProbeCode::NO_DATA;
    snprintf(pr.detail, sizeof pr.detail, "QPI %s but no valid QPIGS", qpiOk ? "ok" : "NAK");
    return pr;
  }
  sched_.markSupported(Cmd::QPIGS);
  bool qpigsPlausible = decode::plausibleQpigs(q);

  // D3-D8
  now = l.clock().ms();
  tryOptional(l, log, Cmd::QMOD, s, now);
  tryOptional(l, log, Cmd::QPIRI, s, now);
  tryOptional(l, log, Cmd::QVFW, s, now);
  tryOptional(l, log, Cmd::QVFW2, s, now);
  tryOptional(l, log, Cmd::QPIWS, s, now);
  // D7: QALL with CHK, then the CRC variant (Solar2MQTT)
  pi30p::Qall qa;
  bool qallOk = false;
  for (int v = 0; v < 3 && !qallOk; v++) {  // CHK, CRC, CHK again (0.7.3)
    qallCrc_ = v == 1;
    LinkResult r = send(l, Cmd::QALL);
    log.add(l.clock().ms(), "QALL(%s) -> %s %.60s", qallCrc_ ? "crc" : "chk", linkResultName(r), reply_.payload);
    if (r == LinkResult::OK && pi30p::parseQall(reply_.payload, qa)) qallOk = true;
  }
  if (!qallOk) qallCrc_ = false;
  if (qallOk) sched_.markSupported(Cmd::QALL);
  else sched_.markUnsupported(Cmd::QALL, now);
  tryOptional(l, log, Cmd::QFLAG, s, now);
  tryOptional(l, log, Cmd::QID, s, now);
  if (!ident_.qid[0]) tryOptional(l, log, Cmd::QSID, s, now);
  tryOptional(l, log, Cmd::QMN, s, now);
  tryOptional(l, log, Cmd::QGMN, s, now);
  tryOptional(l, log, Cmd::QDI, s, now);
  tryOptional(l, log, Cmd::QET, s, now);
  tryOptional(l, log, Cmd::QT, s, now);
  tryOptional(l, log, Cmd::QPIGS2, s, now);
  tryOptional(l, log, Cmd::Q1, s, now);
  // QLITH0: CRC first, then the CHK form as the doc writes it
  qlithChk_ = false;
  if (!tryOptional(l, log, Cmd::QLITH0, s, now)) {
    qlithChk_ = true;
    if (!tryOptional(l, log, Cmd::QLITH0, s, now)) qlithChk_ = false;
  }

  // classify (d1 §3.6)
  const char* qflag = ident_.qflag;
  int qpiwsLen = (int)strlen(s.wf);
  dialect_ = classify(qallOk, ident_.qpiriFields, qflag, qpiwsLen, s.md, q.n);
  if (dialect_ == Dialect::REVO) {
    tryOptional(l, log, Cmd::PQSE, s, now);
    tryOptional(l, log, Cmd::PQSA, s, now);
  }
  notePv12(q);
  snprintf(ident_.dia, sizeof ident_.dia, "%s", dialectName(dialect_));
  snprintf(ident_.family, sizeof ident_.family, "pi30");
  ident_.pv12 = pv12_;
  rebuildSup();

  pr.ok = (dialect_ == Dialect::REVO) ? decode::plausibleQall(qa) || qpigsPlausible : qpigsPlausible;
  pr.score = (uint8_t)((qpiOk ? 50 : 30) + (pr.ok ? 40 : 0) + (ident_.qpiriFields ? 10 : 0));
  snprintf(pr.dialect, sizeof pr.dialect, "%s", dialectName(dialect_));
  if (pr.ok) {
    pr.code = ProbeCode::OK;
    snprintf(pr.detail, sizeof pr.detail, "%s %s QPIGS=%d fields QPIRI=%d pv12=%c", qpiOk ? ident_.qpi : "QPI-NAK",
             dialectName(dialect_), q.n, ident_.qpiriFields, pv12_);
  } else {
    pr.code = ProbeCode::NO_DATA;
    snprintf(pr.detail, sizeof pr.detail, "replies valid but implausible (bv=%.2f ov=%.1f) - check LCD", q.v[8], q.v[2]);
  }
  ident_.pv12 = pv12_;
  dirty_ = true;
  return pr;
}

bool Pi30Adapter::lightProbe(SerialLink& l, ProbeLog& log) {
  int good = 0;
  if (dialect_ == Dialect::UNKNOWN) {  // 0.7.2 could persist this after a failed re-probe: classify again
    log.add(l.clock().ms(), "light: dialect unknown -> full probe");
    return false;
  }
  if (dialect_ == Dialect::REVO) {
    for (int i = 0; i < 3; i++) {
      LinkResult r = send(l, Cmd::QALL);
      pi30p::Qall a;
      bool ok = r == LinkResult::OK && pi30p::parseQall(reply_.payload, a) && decode::plausibleQall(a);
      log.add(l.clock().ms(), "light QALL -> %s%s", linkResultName(r), ok ? " plausible" : "");
      good += ok;
    }
    if (good >= 2 && !sched_.supported(Cmd::QALL)) {  // proven again (0.7.3 could have persisted it as unsupported)
      sched_.markSupported(Cmd::QALL);
      rebuildSup();
    }
    return good >= 2;
  }
  LinkResult r = l.query("QPI", Framing::CRC, reply_);
  log.add(l.clock().ms(), "light QPI -> %s %.12s", linkResultName(r), reply_.payload);
  for (int i = 0; i < 3; i++) {
    LinkResult r2 = l.query("QPIGS", Framing::CRC, reply_);
    pi30p::Qpigs q;
    bool ok = r2 == LinkResult::OK && pi30p::parseQpigs(reply_.payload, q) && decode::plausibleQpigs(q);
    log.add(l.clock().ms(), "light QPIGS -> %s%s", linkResultName(r2), ok ? " plausible" : "");
    good += ok;
  }
  return good >= 2;
}

void Pi30Adapter::schedule(uint32_t now) {
  sched_.clear();
  auto opt = [&](Cmd c) { return !sched_.unsupported(c); };
  if (dialect_ == Dialect::REVO) {
    sched_.add(Cmd::QALL, cfg::P_FAST_MS, 0, true, now);
    if (opt(Cmd::QPIWS)) sched_.add(Cmd::QPIWS, cfg::P_MID_MS, 2500, false, now);
    sched_.add(Cmd::QPIGS, cfg::P_REVO_QPIGS_MS, 1500, false, now);
    if (opt(Cmd::PQSE)) sched_.add(Cmd::PQSE, cfg::P_RATING_MS, 20000, false, now);
    if (opt(Cmd::QLITH0)) sched_.add(Cmd::QLITH0, cfg::P_BMS_MS, 1000, false, now);  // 0.7.6: BMS every 4 s
  } else {
    sched_.add(Cmd::QPIGS, cfg::P_FAST_MS, 0, true, now);
    sched_.add(Cmd::QMOD, cfg::P_MID_MS, 1000, false, now);  // QMOD and QPIWS alternate, each every 10 s
    if (opt(Cmd::QPIWS)) sched_.add(Cmd::QPIWS, cfg::P_MID_MS, 6000, false, now);
    if (opt(Cmd::QFLAG)) sched_.add(Cmd::QFLAG, cfg::P_RATING_MS, 22000, false, now);
    if (opt(Cmd::QT)) sched_.add(Cmd::QT, cfg::P_ENERGY_MS, 32000, false, now);
    if (opt(Cmd::QET)) sched_.add(Cmd::QET, cfg::P_ENERGY_MS, 37000, false, now);
    if (opt(Cmd::QED)) sched_.add(Cmd::QED, cfg::P_ENERGY_MS, 42000, false, now);
    if (opt(Cmd::QLD)) sched_.add(Cmd::QLD, cfg::P_ENERGY_MS, 47000, false, now);
  }
  if (opt(Cmd::Q1)) sched_.add(Cmd::Q1, cfg::P_Q1_MS, 12000, false, now);
  sched_.add(Cmd::QPIRI, cfg::P_RATING_MS, 17000, false, now);
  // identity: boot + hourly
  sched_.add(Cmd::QPI, cfg::P_IDENT_MS, 50000, false, now);
  if (opt(Cmd::QID)) sched_.add(Cmd::QID, cfg::P_IDENT_MS, 52000, false, now);
  else if (opt(Cmd::QSID)) sched_.add(Cmd::QSID, cfg::P_IDENT_MS, 52000, false, now);
  if (opt(Cmd::QVFW)) sched_.add(Cmd::QVFW, cfg::P_IDENT_MS, 54000, false, now);
  if (opt(Cmd::QVFW2)) sched_.add(Cmd::QVFW2, cfg::P_IDENT_MS, 56000, false, now);
  if (opt(Cmd::QMN)) sched_.add(Cmd::QMN, cfg::P_IDENT_MS, 58000, false, now);
  if (opt(Cmd::QGMN)) sched_.add(Cmd::QGMN, cfg::P_IDENT_MS, 59000, false, now);
}

void Pi30Adapter::onConfigChangedBit(uint32_t nowMs) {
  if (cfgTriggered_ && (uint32_t)(nowMs - lastCfgTrigger_) < cfg::CONFIG_BIT_DEBOUNCE_MS) return;
  cfgTriggered_ = true;
  lastCfgTrigger_ = nowMs;
  sched_.trigger(Cmd::QPIRI, nowMs);
  if (dialect_ == Dialect::REVO) sched_.trigger(Cmd::PQSE, nowMs);
  else sched_.trigger(Cmd::QFLAG, nowMs);
}

bool Pi30Adapter::step(SerialLink& l, uint32_t nowMs, Sample& io) {
  last_ = StepInfo();
  int idx = sched_.due(nowMs);
  if (idx < 0) return false;
  Cmd c = sched_.entry(idx).cmd;
  bool fast = sched_.entry(idx).fast;
  LinkResult r = send(l, c);
  if (r == LinkResult::REFUSED && (c == Cmd::QED || c == Cmd::QLD)) {  // no date yet: skip silently
    sched_.done(idx, nowMs);
    return false;
  }
  bool ok = handle(c, r, nowMs, io);
  if (!ok && r == LinkResult::TIMEOUT && !fast && CMD_INFO[(int)c].optional && l.linkOk(nowMs)) {
    if (sched_.noteFail(c, nowMs)) rebuildSup();  // the inverter answers others but not this one (5x in a row)
  }
  sched_.done(idx, l.clock().ms());
  last_.ran = true;
  last_.fast = fast;
  last_.ok = ok;
  last_.cmd = c;
  last_.res = r;
  return fast && ok;
}

void Pi30Adapter::exportState(AdapterState& st) const {
  st = AdapterState();
  snprintf(st.adapter, sizeof st.adapter, "pi30");
  snprintf(st.dialect, sizeof st.dialect, "%s", dialectName(dialect_));
  st.pv12 = pv12_;
  st.supMask = sched_.supMask();
  st.unsupMask = sched_.unsupMask();
  st.flags = (uint8_t)((qallCrc_ ? AF_QALL_CRC : 0) | (qlithChk_ ? AF_QLITH_CHK : 0));
}

void Pi30Adapter::importState(const AdapterState& st) {
  dialect_ = dialectFromName(st.dialect);
  pv12_ = st.pv12 ? st.pv12 : '?';
  qallCrc_ = st.flags & AF_QALL_CRC;
  qlithChk_ = st.flags & AF_QLITH_CHK;
  sched_.setMasks(st.supMask, st.unsupMask, 0);
  // 0.7.2/0.7.3 demoted working core commands after a single NAK and persisted that: give them a fresh start
  // (a really unsupported one NAKs again and is demoted at once, because it is not in the supported set).
  static const Cmd CORE[] = {Cmd::QALL, Cmd::QPIWS, Cmd::QPIGS, Cmd::QMOD, Cmd::QPIRI};
  for (Cmd c : CORE) sched_.forgetUnsupported(c);
  snprintf(ident_.dia, sizeof ident_.dia, "%s", dialectName(dialect_));
  snprintf(ident_.family, sizeof ident_.family, "pi30");
  ident_.pv12 = pv12_;
  rebuildSup();
  dirty_ = false;
}
