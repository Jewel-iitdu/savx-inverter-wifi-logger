// PinDiag.h - receive-only wiring diagnostics (d1 §8.4 A, V1 §6.2-6.3). Host-portable (uses IGpio/IClock).
// NOTHING here ever drives GPIO17 or any RJ45 pin: self-ID, the idle-level tests and the pre-arm check only READ.
//
//  1. Module self-ID (bench, inverter NOT connected): both module header pins as INPUT_PULLUP.
//     a) A/B open  -> both read HIGH (fail-safe bias).
//     b) jumper A+ -> ESP GND and B- -> ESP 3V3 -> the header pin that reads LOW is RO, the other is DI.
//  2. Idle-level test, ref GND (module A+ on candidate RJ45 pin 1 or 2, B- on pin 8 = ESP GND): sample RO every
//     10 ms for 10 s. >= 99 % LOW with < 3 edges -> that pin idles NEGATIVE -> it is the inverter's TX.
//     Steady HIGH -> the pin is above about -0.2 V (an RS232 receive input, an open wire, OR a positive pin).
//  2b. Upper check, ref 3V3 (safety review): A+ on the candidate RX pin, B- on ESP 3V3, pin 8 still on ESP GND
//     (through the P5 jumper, out of the B- terminal). Steady LOW -> the pin sits below about 3.3 V (+-0.2 V) even with the
//     module's A pull-up on it -> safe for GPIO17. Steady HIGH -> a POSITIVE source (e.g. pin 4 "+VCC" picked up
//     by a colour mix-up) -> GPIO17 must never go there. Together 2 and 2b bound the RX pin to the GPIO-safe window.
//  3. Arm check: TX verdict twice on one pin; the other pin HIGH (ref GND), never TX, and twice LOW in the upper
//     check -> GPIO17 may go to the other pin.
//  4. Pre-arm check (just before TX is attached): GPIO17 with its internal PULL-DOWN must read LOW (not touching
//     the DI wire on GPIO16, not on a positive pin), and RO must read LOW (A+ still on the idling inverter TX).
#pragma once
#include <stdint.h>

#include "config.h"
#include "link/Port.h"

enum class SelfIdVerdict : uint8_t { NOT_RUN = 0, OPEN_OK, RO_FOUND, BOTH_LOW, NOISY, UNEXPECTED };
inline const char* selfIdName(SelfIdVerdict v) {
  switch (v) {
    case SelfIdVerdict::NOT_RUN: return "not-run";
    case SelfIdVerdict::OPEN_OK: return "open-ok";
    case SelfIdVerdict::RO_FOUND: return "ro-found";
    case SelfIdVerdict::BOTH_LOW: return "both-low";
    case SelfIdVerdict::NOISY: return "noisy";
    case SelfIdVerdict::UNEXPECTED: return "unexpected";
  }
  return "?";
}

struct SelfIdResult {
  SelfIdVerdict verdict = SelfIdVerdict::NOT_RUN;
  uint8_t pinA = cfg::PIN_MOD_HDR_RXD, pinB = cfg::PIN_MOD_HDR_TXD;
  uint16_t lowA = 0, lowB = 0, samples = 0;
  uint8_t roPin = 0, diPin = 0;  // valid when verdict == RO_FOUND
  char msg[160] = "";
};

enum class IdleVerdict : uint8_t {
  NONE = 0, TX_IDLES_NEGATIVE, HIGH_NOT_TX, ACTIVE_TRAFFIC, UNDECIDED,
  BELOW_3V3,  // upper check (B- on 3V3): steady LOW  -> the pin is below ~3.1 V -> GPIO17-safe
  ABOVE_3V3,  // upper check (B- on 3V3): steady HIGH -> a positive source -> NEVER connect GPIO17
};
inline const char* idleVerdictName(IdleVerdict v) {
  switch (v) {
    case IdleVerdict::NONE: return "none";
    case IdleVerdict::TX_IDLES_NEGATIVE: return "inverter-TX";
    case IdleVerdict::HIGH_NOT_TX: return "high-not-TX";
    case IdleVerdict::ACTIVE_TRAFFIC: return "traffic";
    case IdleVerdict::UNDECIDED: return "undecided";
    case IdleVerdict::BELOW_3V3: return "below-3V3";
    case IdleVerdict::ABOVE_3V3: return "ABOVE-3V3";
  }
  return "?";
}

// What the module's B- terminal is wired to during an idle-level test.
enum class IdleRef : uint8_t { GND = 0, V3V3 = 1 };
inline const char* idleRefName(IdleRef r) { return r == IdleRef::V3V3 ? "3v3" : "gnd"; }

struct IdleResult {
  uint8_t rjPin = 0;        // candidate RJ45 pin the user put A+ on (1 or 2)
  uint8_t gpio = 0;         // RO GPIO sampled
  IdleRef ref = IdleRef::GND;
  uint16_t samples = 0, lowCount = 0, edges = 0;
  float pctLow = 0;
  IdleVerdict verdict = IdleVerdict::NONE;
  uint32_t seq = 0;         // run counter (ordering across pins)
};

// History of idle results for RJ45 pins 1 and 2 (persisted in NVS "link" as a raw blob: a layout change makes
// the stored blob size differ, so it is ignored = no results = arming impossible until the tests are redone).
struct IdleHistory {
  static constexpr int N = 4;
  static constexpr int NUP = 2;
  IdleResult r[2][N];       // ref GND: [pin-1][0 = newest]
  IdleResult up[2][NUP];    // ref 3V3 (upper check): [pin-1][0 = newest]
  uint32_t seq = 0;
  uint8_t txSeen = 0, aboveSeen = 0;  // sticky bit (pin-1) until reset-link: "inverter-TX" / "ABOVE-3V3" ever seen
  void add(const IdleResult& x);
  const IdleResult* latest(int rjPin) const;     // newest ref-GND result
  const IdleResult* latestUp(int rjPin) const;   // newest upper-check result
  bool any() const;                              // at least one stored result (the RJ45 has been wired)
};

struct ArmCheck {
  bool ok = false;
  uint8_t txRj = 0, rxRj = 0;  // set only when every history gate passed: inverter TX pin (A+) and RX pin (GPIO17)
  uint8_t upperPin = 0;        // != 0: the upper check (B- on 3V3) on this RJ45 pin is the missing step
  char reason[200] = "";
};

struct PreArmResult {
  bool ok = false;
  uint16_t samples = 0, txHigh = 0, roHigh = 0;
  char reason[200] = "";
};

class PinDiag {
 public:
  PinDiag(IGpio& gpio, IClock& clk) : gpio_(gpio), clk_(clk) {}

  // One self-ID observation of both header pins (500 ms). The caller must have released the UART.
  SelfIdResult selfId(uint8_t pinA, uint8_t pinB);
  // Idle-level test on the RO pin. The caller must have released the UART. Never drives anything.
  IdleResult idleTest(uint8_t rjPin, uint8_t roGpio, IdleRef ref = IdleRef::GND,
                      uint32_t durationMs = cfg::IDLE_DURATION_MS, uint32_t periodMs = cfg::IDLE_SAMPLE_MS);
  // Pre-arm check (receive-only, ~200 ms): txGpio as INPUT_PULLDOWN must read LOW; if checkRo, roGpio (INPUT_PULLUP)
  // must read LOW too. txGpio is returned to a high-Z input afterwards. The caller must have released the UART.
  PreArmResult preArmCheck(uint8_t txGpio, uint8_t roGpio, bool checkRo);

  // Pure analysis helpers (unit-tested)
  static IdleVerdict classify(uint16_t samples, uint16_t lowCount, uint16_t edges, IdleRef ref = IdleRef::GND);
  static ArmCheck armCheck(const IdleHistory& h, uint8_t requestedRxRj);

 private:
  IGpio& gpio_;
  IClock& clk_;
};
