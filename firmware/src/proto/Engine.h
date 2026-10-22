// Engine.h - the inverter-side state machine run by invTask (d1 §3.5 boot/RUN, §5.3). Host-portable.
//
//   DISARMED : link not armed -> listen-only (RawCapture). GPIO17 stays high-Z. Nothing is ever sent.
//   LIGHT    : persisted profile -> light probe (QPI/QALL + QPIGS x3 with plausibility)
//   PROBING  : full read-only probe (Prober)
//   RUN      : adapter->step() every tick; 60 failed fast polls -> background re-probe with back-off
//   RAW      : nothing found / foreign master / PI18 -> listen-only, re-probe after 6 h (or on request)
//   SAFE     : safe mode -> no TX at all
#pragma once
#include "link/SerialLink.h"
#include "model/Identity.h"
#include "model/Sample.h"
#include "proto/Prober.h"
#include "proto/RawCapture.h"

enum class EngineState : uint8_t { DISARMED = 0, LIGHT, PROBING, RUN, RAW, SAFE };
inline const char* engineStateName(EngineState s) {
  switch (s) {
    case EngineState::DISARMED: return "disarmed";
    case EngineState::LIGHT: return "light-probe";
    case EngineState::PROBING: return "probing";
    case EngineState::RUN: return "run";
    case EngineState::RAW: return "raw";
    case EngineState::SAFE: return "safe";
  }
  return "?";
}

struct EngineStats {
  uint32_t fastOk = 0, fastFail = 0, consecutiveFails = 0, reprobes = 0, probes = 0;
  bool linkLostRaised = false;
};

class InverterEngine {
 public:
  explicit InverterEngine(SerialLink& link)
      : link_(link), smg_(ModbusFamily::SMG), powmr_(ModbusFamily::POWMR), prober_(link, pi30_, q6_, smg_, powmr_) {}

  // prof: pins/mode/self-ID from NVS; st: persisted adapter state (adapter[0]==0 if none)
  void begin(const LinkProfile& prof, const AdapterState& st, bool armed, bool safeMode, uint32_t nowMs);
  void setArmed(bool armed, uint32_t nowMs);
  void setBaseProfile(const LinkProfile& prof);  // pins / self-ID changed (re-applied on next configure)
  void requestProbe() { probeRequested_ = true; }

  // Runs one scheduler step. Returns true if `out` holds a fresh, valid sample.
  bool tick(uint32_t nowMs, uint32_t epoch, Sample& out);

  EngineState state() const { return state_; }
  const EngineStats& stats() const { return stats_; }
  const ProbeOutcome& lastProbe() const { return outcome_; }
  const ProbeLog& probeLog() const { return log_; }
  InverterAdapter* adapter() { return adapter_; }
  AdapterKind kind() const { return kind_; }
  void identity(Identity& out) const;
  const LinkProfile& profile() const { return link_.profile(); }
  bool linkExpected() const { return state_ == EngineState::RUN; }

  // Persistence: true once after something worth saving changed.
  bool takePersist(LinkProfile& prof, AdapterState& st);

 private:
  InverterAdapter* adapterFor(AdapterKind k);
  void runFullProbe(uint32_t nowMs);
  void enterRun(AdapterKind k, const LinkProfile& p, uint32_t nowMs);
  void enterRaw(uint32_t nowMs, uint32_t reprobeAfterMs);

  SerialLink& link_;
  Pi30Adapter pi30_;
  Q6Adapter q6_;
  ModbusAdapter smg_, powmr_;
  RawCaptureAdapter raw_;
  Prober prober_;
  ProbeLog log_;
  ProbeOutcome outcome_;
  LinkProfile base_;
  InverterAdapter* adapter_ = nullptr;
  AdapterKind kind_ = AdapterKind::NONE;
  EngineState state_ = EngineState::DISARMED;
  EngineStats stats_;
  Sample cur_;
  bool armed_ = false, safe_ = false, probeRequested_ = false, persist_ = false;
  uint32_t nextReprobeMs_ = 0;
  uint8_t backoffIdx_ = 0;
  uint32_t rawReprobeAtMs_ = 0;
  // A protocol already worked on this unit (persisted at boot, or RUN reached): a failed full probe then retries with
  // REPROBE_BACKOFF_MS (1, 5, 15, 60 min) and keeps the good adapter in NVS, instead of RAW for RAW_REPROBE_MS (6 h).
  bool hadGood_ = false;
  uint8_t rawBackoffIdx_ = 0;
};
