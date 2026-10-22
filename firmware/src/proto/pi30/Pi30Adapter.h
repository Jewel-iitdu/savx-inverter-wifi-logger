// Pi30Adapter.h - Voltronic PI30 family adapter: dialects STOCK17 / STOCK21 / MAX24 / GK / REVO (d1 §3.4-3.7).
// Host-portable. Read-only: every command goes through SerialLink's whitelist.
#pragma once
#include "proto/Adapter.h"
#include "proto/Scheduler.h"
#include "proto/pi30/Dialect.h"
#include "proto/pi30/Pi30Parse.h"

class Pi30Adapter : public InverterAdapter {
 public:
  const char* id() const override { return "pi30"; }
  ProbeResult probe(SerialLink& l, ProbeLog& log) override;
  bool lightProbe(SerialLink& l, ProbeLog& log) override;
  void schedule(uint32_t nowMs) override;
  bool step(SerialLink& l, uint32_t nowMs, Sample& io) override;
  void identity(Identity& out) const override { out = ident_; }
  void restoreIdentity(const Identity& id) override { ident_ = id; }
  void onConfigChangedBit(uint32_t nowMs) override;
  const StepInfo& lastStep() const override { return last_; }
  void exportState(AdapterState& st) const override;
  void importState(const AdapterState& st) override;
  bool stateDirty() const override { return dirty_; }
  void clearDirty() override { dirty_ = false; }

  Dialect dialect() const { return dialect_; }
  char pv12() const { return pv12_; }
  bool qallUsesCrc() const { return qallCrc_; }

  // Dialect rules (d1 §3.6), exposed for tests.
  static Dialect classify(bool qallOk, int qpiriFields, const char* qflagAll, int qpiwsLen, char qmod,
                          int qpigsFields);

 private:
  // one query with the right framing; returns the link result and fills reply_
  LinkResult send(SerialLink& l, Cmd c);
  // parse + merge one reply; returns true if valid
  bool handle(Cmd c, LinkResult r, uint32_t nowMs, Sample& io);
  void notePv12(const pi30p::Qpigs& q);
  void rebuildSup();
  bool tryOptional(SerialLink& l, ProbeLog& log, Cmd c, Sample& scratch, uint32_t nowMs);

  Dialect dialect_ = Dialect::UNKNOWN;
  char pv12_ = '?';
  int pv12VotesA_ = 0;
  bool qallCrc_ = false;
  bool qlithChk_ = false;
  // 0.7.6: the lithium BMS's own battery figures (QLITH0, 0.1 A) override the inverter's whole-amp QALL/QPIGS
  // battery current while fresh. On 2026-10-22 QALL read 0 A / 0 A while the BMS (and the LCD) showed 0.9 A discharge.
  float bmsV_ = NAN, bmsChg_ = NAN, bmsDis_ = NAN, bmsSoc_ = NAN;
  uint32_t bmsAtMs_ = 0;
  bool haveBms_ = false;
  void applyBms(Sample& io, uint32_t nowMs) const;
  bool dirty_ = false;
  uint32_t lastCfgTrigger_ = 0;
  bool cfgTriggered_ = false;
  Identity ident_;
  StepInfo last_;
  Reply reply_;
  char cmdBuf_[16];
};
