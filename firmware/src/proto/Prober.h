// Prober.h - read-only protocol discovery decision tree (d1 §3.5, V1 §6.4-6.6). Host-portable.
#pragma once
#include "link/SerialLink.h"
#include "proto/Adapter.h"
#include "proto/ProbeLog.h"
#include "proto/legacy/Q6Adapter.h"
#include "proto/modbus/ModbusAdapter.h"
#include "proto/pi30/Pi30Adapter.h"

enum class AdapterKind : uint8_t { NONE = 0, PI30, Q6, SMG, POWMR, RAW };
inline const char* adapterKindName(AdapterKind k) {
  switch (k) {
    case AdapterKind::PI30: return "pi30";
    case AdapterKind::Q6: return "q6";
    case AdapterKind::SMG: return "smg";
    case AdapterKind::POWMR: return "powmr";
    case AdapterKind::RAW: return "raw";
    default: return "none";
  }
}
inline AdapterKind adapterKindFromId(const char* s) {
  if (!s) return AdapterKind::NONE;
  if (!strcmp(s, "pi30")) return AdapterKind::PI30;
  if (!strcmp(s, "q6")) return AdapterKind::Q6;
  if (!strcmp(s, "smg")) return AdapterKind::SMG;
  if (!strcmp(s, "powmr")) return AdapterKind::POWMR;
  if (!strcmp(s, "raw")) return AdapterKind::RAW;
  return AdapterKind::NONE;
}

struct ProbeOutcome {
  bool ran = false;
  bool ok = false;
  AdapterKind kind = AdapterKind::NONE;
  LinkProfile profile;
  AdapterState state;
  ProbeResult result;
  bool foreignMaster = false;
  bool pi18 = false;
  uint32_t startedMs = 0, durationMs = 0;
  char summary[120] = "";
};

class Prober {
 public:
  Prober(SerialLink& l, Pi30Adapter& pi30, Q6Adapter& q6, ModbusAdapter& smg, ModbusAdapter& powmr)
      : l_(l), pi30_(pi30), q6_(q6), smg_(smg), powmr_(powmr) {}

  // Full read-only probe over all profiles. Leaves the link configured on the found profile.
  ProbeOutcome full(const LinkProfile& base, ProbeLog& log);

  // Profile list (d1 §3.2 P1..P6). Returns count.
  static int profiles(const LinkProfile& base, LinkProfile* out, int max);

 private:
  bool listenForForeign(ProbeLog& log);
  SerialLink& l_;
  Pi30Adapter& pi30_;
  Q6Adapter& q6_;
  ModbusAdapter& smg_;
  ModbusAdapter& powmr_;
  Reply reply_;
};
