// ModbusAdapter.h - last-resort Modbus RTU families on the RS232 COM port (d1 §3.4): detect + basic decode,
// FC 03 ONLY. SMG: 9600, slave 1, registers 201..234 (R3 §2.5). PowMr: 2400, slave 5, 4501..4545 with
// byte-swapped words (R3 §3). Host-portable.
#pragma once
#include "proto/Adapter.h"

enum class ModbusFamily : uint8_t { SMG = 1, POWMR = 2 };

class ModbusAdapter : public InverterAdapter {
 public:
  explicit ModbusAdapter(ModbusFamily f) : fam_(f) {}
  const char* id() const override { return fam_ == ModbusFamily::SMG ? "smg" : "powmr"; }
  ProbeResult probe(SerialLink& l, ProbeLog& log) override;
  bool lightProbe(SerialLink& l, ProbeLog& log) override;
  void schedule(uint32_t nowMs) override;
  bool step(SerialLink& l, uint32_t nowMs, Sample& io) override;
  void identity(Identity& out) const override { out = ident_; }
  const StepInfo& lastStep() const override { return last_; }
  void exportState(AdapterState& st) const override;
  void importState(const AdapterState& st) override;

  uint32_t baud() const { return fam_ == ModbusFamily::SMG ? 9600 : 2400; }
  // decoders (exposed for tests); regs[0] = first register of the block
  static bool decodeSmg(const uint16_t* regs201, int n, Sample& s);
  static bool decodePowmr(const uint16_t* regs4501, int n, Sample& s);

 private:
  bool readBlock(SerialLink& l, uint16_t reg, uint16_t qty, uint16_t* out, LinkResult& r);
  ModbusFamily fam_;
  Identity ident_;
  StepInfo last_;
  Reply reply_;
};
