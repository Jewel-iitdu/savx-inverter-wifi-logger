// RawCapture.h - listen-only adapter (d1 §3.4): used when nothing was detected, a foreign master is present,
// or the link is not armed. Never transmits. Bytes are kept in the link's RawRing (/api/raw). Host-portable.
#pragma once
#include "proto/Adapter.h"

class RawCaptureAdapter : public InverterAdapter {
 public:
  const char* id() const override { return "raw"; }
  ProbeResult probe(SerialLink& l, ProbeLog& log) override {
    (void)l; (void)log;
    ProbeResult pr;
    pr.ok = true;
    pr.code = ProbeCode::OK;
    snprintf(pr.family, sizeof pr.family, "raw");
    snprintf(pr.detail, sizeof pr.detail, "listen-only");
    return pr;
  }
  bool lightProbe(SerialLink& l, ProbeLog& log) override { (void)l; (void)log; return true; }
  void schedule(uint32_t nowMs) override { sched_.clear(); sched_.add(Cmd::COUNT, 1000, 0, false, nowMs); }
  bool step(SerialLink& l, uint32_t nowMs, Sample& io) override {
    (void)io;
    last_ = StepInfo();
    if ((uint32_t)(nowMs - lastListen_) < 1000) return false;
    lastListen_ = nowMs;
    uint8_t buf[200];
    uint32_t total = 0;
    l.listen(200, buf, sizeof buf, &total);  // receive-only
    bytes_ += total;
    last_.ran = true;
    return false;
  }
  void identity(Identity& out) const override { out = Identity(); snprintf(out.family, sizeof out.family, "raw"); }
  const StepInfo& lastStep() const override { return last_; }
  void exportState(AdapterState& st) const override { st = AdapterState(); snprintf(st.adapter, sizeof st.adapter, "raw"); }
  void importState(const AdapterState& st) override { (void)st; }
  uint32_t bytesSeen() const { return bytes_; }

 private:
  StepInfo last_;
  uint32_t lastListen_ = 0;
  uint32_t bytes_ = 0;
};
