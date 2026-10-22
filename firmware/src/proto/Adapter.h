// Adapter.h - inverter protocol adapter interface (d1 §3.3). Host-portable.
#pragma once
#include <stdint.h>

#include "link/SerialLink.h"
#include "model/Identity.h"
#include "model/Sample.h"
#include "proto/Commands.h"
#include "proto/ProbeLog.h"
#include "proto/Scheduler.h"

// d1 §3.3 ProbeResult, plus a machine-readable outcome code for the prober's decision tree.
enum class ProbeCode : uint8_t { OK = 0, SILENCE, GARBAGE, LEGACY_HASH, NAK_ONLY, NO_DATA, FOREIGN, NOT_ARMED };
inline const char* probeCodeName(ProbeCode c) {
  switch (c) {
    case ProbeCode::OK: return "ok";
    case ProbeCode::SILENCE: return "silence";
    case ProbeCode::GARBAGE: return "garbage";
    case ProbeCode::LEGACY_HASH: return "legacy#-1";
    case ProbeCode::NAK_ONLY: return "nak-only";
    case ProbeCode::NO_DATA: return "no-valid-data";
    case ProbeCode::FOREIGN: return "foreign-master";
    case ProbeCode::NOT_ARMED: return "not-armed";
  }
  return "?";
}

struct ProbeResult {
  bool ok = false;
  uint8_t score = 0;
  char family[12] = "";
  char dialect[10] = "";
  char detail[96] = "";
  ProbeCode code = ProbeCode::SILENCE;
};

// Result of the last step() call (used by the engine for failure counting).
struct StepInfo {
  bool ran = false;   // a command was sent (or a listen slot executed)
  bool fast = false;  // it was the fast-cycle command (QPIGS / QALL / Q6 / Modbus block)
  bool ok = false;    // valid, plausible reply
  Cmd cmd = Cmd::COUNT;
  LinkResult res = LinkResult::TIMEOUT;
};

// Persisted adapter state (NVS namespace "link", d1 §5.12).
struct AdapterState {
  char adapter[8] = "";   // "pi30", "q6", "smg", "powmr", "raw"
  char dialect[10] = "";
  char pv12 = '?';
  uint32_t supMask = 0;
  uint32_t unsupMask = 0;
  uint8_t flags = 0;      // b0: QALL uses the CRC framing; b1: QLITH0 uses CHK; b2: legacy Q6 supported
};
constexpr uint8_t AF_QALL_CRC = 0x01, AF_QLITH_CHK = 0x02, AF_Q6 = 0x04;

class InverterAdapter {
 public:
  virtual ~InverterAdapter() {}
  virtual const char* id() const = 0;                          // "pi30", "q6", "smg", "powmr", "raw"
  virtual ProbeResult probe(SerialLink& l, ProbeLog& log) = 0;  // read-only; < ~15-25 s
  virtual bool lightProbe(SerialLink& l, ProbeLog& log) = 0;    // boot re-validation of a persisted profile
  // Registers commands + periods (§3.7) in the adapter's scheduler. (d1 passes the scheduler in; here each
  // adapter owns one so the supported-command bitmap travels with the adapter state.)
  virtual void schedule(uint32_t nowMs) = 0;
  CommandScheduler& scheduler() { return sched_; }
  const CommandScheduler& scheduler() const { return sched_; }
  virtual bool step(SerialLink& l, uint32_t nowMs, Sample& io) = 0;  // runs ONE due command, merges into io
  virtual void identity(Identity& out) const = 0;
  virtual void restoreIdentity(const Identity& id) { (void)id; }  // undo a failed re-probe (0.7.3)
  virtual void onConfigChangedBit(uint32_t nowMs) { (void)nowMs; }  // QPIGS status b6 -> re-read QPIRI/QFLAG
  virtual const StepInfo& lastStep() const = 0;
  virtual void exportState(AdapterState& st) const = 0;
  virtual void importState(const AdapterState& st) = 0;
  virtual void setEpoch(uint32_t epoch) { epoch_ = epoch; }   // for QEDyyyymmdd etc.
  virtual bool stateDirty() const { return false; }           // persist again (e.g. pv12 decided)
  virtual void clearDirty() {}

 protected:
  uint32_t epoch_ = 0;
  CommandScheduler sched_;
};
