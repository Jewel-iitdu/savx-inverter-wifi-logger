// Q6Adapter.h - legacy Voltronic RP/RS-series "Q1/Q6" protocol without CRC (d1 §3.4; r2 §3.5 [SECONDARY]).
// Detected when QPI answers "#-1". Field map (Q6): 0 in V, 1 in Hz, 2 out V, 3 out Hz, 4 batt V, 5 SOC,
// 6 charge A, 10 VA x10, 11 W x10, 12 load %, 13 temp °C, 14 mode (B/L). Host-portable.
#pragma once
#include "proto/Adapter.h"

class Q6Adapter : public InverterAdapter {
 public:
  const char* id() const override { return "q6"; }
  ProbeResult probe(SerialLink& l, ProbeLog& log) override;
  bool lightProbe(SerialLink& l, ProbeLog& log) override;
  void schedule(uint32_t nowMs) override;
  bool step(SerialLink& l, uint32_t nowMs, Sample& io) override;
  void identity(Identity& out) const override { out = ident_; }
  const StepInfo& lastStep() const override { return last_; }
  void exportState(AdapterState& st) const override;
  void importState(const AdapterState& st) override;

  static bool decodeQ6(const char* payload, Sample& s);
  static bool decodeQ1(const char* payload, Sample& s);  // UPS-style Q1: in V, fault V, out V, load %, Hz, batt V, temp, bits

 private:
  bool q6_ = false;
  Identity ident_;
  StepInfo last_;
  Reply reply_;
};
