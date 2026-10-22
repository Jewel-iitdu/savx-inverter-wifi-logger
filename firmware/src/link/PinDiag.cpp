// PinDiag.cpp - see PinDiag.h.
#include "link/PinDiag.h"

#include <stdio.h>
#include <string.h>

void IdleHistory::add(const IdleResult& x) {
  if (x.rjPin < 1 || x.rjPin > 2) return;
  IdleResult* a = x.ref == IdleRef::V3V3 ? up[x.rjPin - 1] : r[x.rjPin - 1];
  int n = x.ref == IdleRef::V3V3 ? NUP : N;
  for (int i = n - 1; i > 0; i--) a[i] = a[i - 1];
  a[0] = x;
  a[0].seq = ++seq;
  if (x.verdict == IdleVerdict::TX_IDLES_NEGATIVE) txSeen |= (uint8_t)(1u << (x.rjPin - 1));
  if (x.verdict == IdleVerdict::ABOVE_3V3) aboveSeen |= (uint8_t)(1u << (x.rjPin - 1));
}

const IdleResult* IdleHistory::latest(int rjPin) const {
  if (rjPin < 1 || rjPin > 2) return nullptr;
  const IdleResult& x = r[rjPin - 1][0];
  return x.verdict == IdleVerdict::NONE ? nullptr : &x;
}

const IdleResult* IdleHistory::latestUp(int rjPin) const {
  if (rjPin < 1 || rjPin > 2) return nullptr;
  const IdleResult& x = up[rjPin - 1][0];
  return x.verdict == IdleVerdict::NONE ? nullptr : &x;
}

bool IdleHistory::any() const {
  for (int p = 0; p < 2; p++) {
    for (int i = 0; i < N; i++)
      if (r[p][i].verdict != IdleVerdict::NONE) return true;
    for (int i = 0; i < NUP; i++)
      if (up[p][i].verdict != IdleVerdict::NONE) return true;
  }
  return false;
}

IdleVerdict PinDiag::classify(uint16_t samples, uint16_t lowCount, uint16_t edges, IdleRef ref) {
  if (samples == 0) return IdleVerdict::NONE;
  float pctLow = 100.f * (float)lowCount / (float)samples;
  bool steadyLow = pctLow >= cfg::IDLE_TX_LOW_PCT && edges < cfg::IDLE_MAX_EDGES;
  bool steadyHigh = pctLow <= 100.f - cfg::IDLE_TX_LOW_PCT && edges < cfg::IDLE_MAX_EDGES;
  if (steadyLow) return ref == IdleRef::V3V3 ? IdleVerdict::BELOW_3V3 : IdleVerdict::TX_IDLES_NEGATIVE;
  if (steadyHigh) return ref == IdleRef::V3V3 ? IdleVerdict::ABOVE_3V3 : IdleVerdict::HIGH_NOT_TX;
  if (edges >= 10) return IdleVerdict::ACTIVE_TRAFFIC;  // something is transmitting unsolicited
  return IdleVerdict::UNDECIDED;
}

SelfIdResult PinDiag::selfId(uint8_t pinA, uint8_t pinB) {
  SelfIdResult r;
  r.pinA = pinA;
  r.pinB = pinB;
  gpio_.mode(pinA, GpioMode::IN_PULLUP);
  gpio_.mode(pinB, GpioMode::IN_PULLUP);
  clk_.sleepMs(20);
  uint32_t t0 = clk_.ms();
  while ((uint32_t)(clk_.ms() - t0) < cfg::SELFID_SAMPLE_MS) {
    r.samples++;
    if (!gpio_.level(pinA)) r.lowA++;
    if (!gpio_.level(pinB)) r.lowB++;
    clk_.sleepMs(5);
  }
  if (r.samples == 0) {
    r.verdict = SelfIdVerdict::UNEXPECTED;
    return r;
  }
  auto steadyLow = [&](uint16_t low) { return low * 100u >= r.samples * 98u; };
  auto steadyHigh = [&](uint16_t low) { return low * 100u <= r.samples * 2u; };
  if (steadyHigh(r.lowA) && steadyHigh(r.lowB)) {
    r.verdict = SelfIdVerdict::OPEN_OK;
    snprintf(r.msg, sizeof r.msg,
             "Both header pins HIGH (A/B open = fail-safe idle). Now jumper A+ -> ESP GND and B- -> ESP 3V3, "
             "then run self-ID again.");
  } else if (steadyLow(r.lowA) && steadyHigh(r.lowB)) {
    r.verdict = SelfIdVerdict::RO_FOUND;
    r.roPin = pinA;
    r.diPin = pinB;
  } else if (steadyLow(r.lowB) && steadyHigh(r.lowA)) {
    r.verdict = SelfIdVerdict::RO_FOUND;
    r.roPin = pinB;
    r.diPin = pinA;
  } else if (steadyLow(r.lowA) && steadyLow(r.lowB)) {
    r.verdict = SelfIdVerdict::BOTH_LOW;
    snprintf(r.msg, sizeof r.msg, "Both pins LOW: check module VCC (3V3) and GND, and that nothing else drives them.");
  } else {
    r.verdict = SelfIdVerdict::NOISY;
    snprintf(r.msg, sizeof r.msg, "Pins toggle (A low %u/%u, B low %u/%u): loose wrap joint or floating A/B?",
             r.lowA, r.samples, r.lowB, r.samples);
  }
  if (r.verdict == SelfIdVerdict::RO_FOUND)
    snprintf(r.msg, sizeof r.msg, "RO = GPIO%u (reads LOW with A<B), DI = GPIO%u (will be held HIGH). Remove the "
             "A/B jumpers.", r.roPin, r.diPin);
  return r;
}

IdleResult PinDiag::idleTest(uint8_t rjPin, uint8_t roGpio, IdleRef ref, uint32_t durationMs, uint32_t periodMs) {
  IdleResult r;
  r.rjPin = rjPin;
  r.gpio = roGpio;
  r.ref = ref;
  gpio_.mode(roGpio, GpioMode::IN_PULLUP);  // RO drives the pin; the pull-up only makes an open pin read HIGH
  clk_.sleepMs(20);
  int prev = -1;
  uint32_t t0 = clk_.ms();
  uint32_t next = t0;
  while ((uint32_t)(clk_.ms() - t0) < durationMs && r.samples < 65000) {
    int v = gpio_.level(roGpio);
    r.samples++;
    if (!v) r.lowCount++;
    if (prev >= 0 && v != prev) r.edges++;
    prev = v;
    next += periodMs;
    uint32_t now = clk_.ms();
    if ((int32_t)(next - now) > 0) clk_.sleepMs(next - now);
  }
  r.pctLow = r.samples ? 100.f * (float)r.lowCount / (float)r.samples : 0.f;
  r.verdict = classify(r.samples, r.lowCount, r.edges, ref);
  return r;
}

PreArmResult PinDiag::preArmCheck(uint8_t txGpio, uint8_t roGpio, bool checkRo) {
  PreArmResult p;
  gpio_.mode(txGpio, GpioMode::IN_PULLDOWN);  // ~45 kOhm to GND: a passive load, never a drive
  if (checkRo) gpio_.mode(roGpio, GpioMode::IN_PULLUP);
  clk_.sleepMs(20);
  uint32_t t0 = clk_.ms();
  while ((uint32_t)(clk_.ms() - t0) < cfg::PREARM_SAMPLE_MS) {
    p.samples++;
    if (gpio_.level(txGpio)) p.txHigh++;
    if (checkRo && gpio_.level(roGpio)) p.roHigh++;
    clk_.sleepMs(5);
  }
  gpio_.mode(txGpio, GpioMode::IN_HIZ);  // back to high-Z before anything else happens
  auto steadyLow = [&](uint16_t high) { return p.samples > 0 && high * 100u <= p.samples * 2u; };
  if (!steadyLow(p.txHigh)) {
    snprintf(p.reason, sizeof p.reason,
             "GPIO%u reads HIGH with its pull-down (%u/%u): it touches the GPIO16 (DI) wrap or sits on a POSITIVE "
             "pin. Unplug the RJ45 and USB, separate the wraps, re-check the colours.",
             (unsigned)txGpio, p.txHigh, p.samples);
    return p;
  }
  if (checkRo && !steadyLow(p.roHigh)) {
    snprintf(p.reason, sizeof p.reason,
             "RO (GPIO%u) is not steady LOW (%u/%u HIGH): module A+ is not on the idling inverter TX pin, the "
             "inverter is off, or the GPIO16/GPIO18 wraps touch. Unplug and check.",
             (unsigned)roGpio, p.roHigh, p.samples);
    return p;
  }
  p.ok = true;
  snprintf(p.reason, sizeof p.reason, "pre-arm OK: GPIO%u LOW with pull-down%s", (unsigned)txGpio,
           checkRo ? ", RO steady LOW (A+ on the inverter TX)" : "");
  return p;
}

ArmCheck PinDiag::armCheck(const IdleHistory& h, uint8_t requestedRxRj) {
  ArmCheck a;
  // find the TX pin: its last IDLE_CONFIRMATIONS results are all TX verdicts
  int tx = 0;
  for (int pin = 1; pin <= 2; pin++) {
    const IdleResult* r = h.r[pin - 1];
    bool all = true;
    for (int i = 0; i < cfg::IDLE_CONFIRMATIONS; i++)
      if (r[i].verdict != IdleVerdict::TX_IDLES_NEGATIVE) { all = false; break; }
    if (all) {
      if (tx) {
        snprintf(a.reason, sizeof a.reason, "both pins idle LOW - impossible for RS232: check the wiring (B- on pin 8?)");
        return a;
      }
      tx = pin;
    }
  }
  if (!tx) {
    snprintf(a.reason, sizeof a.reason,
             "no pin has %u consecutive 'inverter-TX' idle results yet (run the idle test on pins 1 and 2, twice)",
             (unsigned)cfg::IDLE_CONFIRMATIONS);
    return a;
  }
  int rx = tx == 1 ? 2 : 1;
  const IdleResult* o = h.latest(rx);
  if (!o || o->verdict != IdleVerdict::HIGH_NOT_TX) {
    snprintf(a.reason, sizeof a.reason, "pin %d must also be tested and read HIGH (not TX) before arming", rx);
    return a;
  }
  if (h.txSeen & (1u << (rx - 1))) {
    snprintf(a.reason, sizeof a.reason,
             "pin %d read 'inverter-TX' in an earlier test: the wiring changed between tests. Run reset-link and "
             "redo the idle tests.", rx);
    return a;
  }
  // Upper check (B- on 3V3): the RX pin must sit below ~3.1 V, twice. Catches a positive pin (e.g. pin 4).
  if (h.aboveSeen & (1u << (rx - 1))) {
    snprintf(a.reason, sizeof a.reason,
             "STOP: pin %d read ABOVE 3.3 V in an upper check - a positive source, not the inverter RX. Never connect "
             "GPIO17 to it. Re-check the cable colours (pin 4?), then reset-link and redo all tests.", rx);
    return a;
  }
  for (int i = 0; i < IdleHistory::NUP; i++) {
    const IdleResult& u = h.up[rx - 1][i];
    if (u.verdict != IdleVerdict::BELOW_3V3) {
      a.upperPin = (uint8_t)rx;
      snprintf(a.reason, sizeof a.reason,
               "inverter TX = pin %d. Next: upper check on pin %d twice (A+ on pin %d, B- on ESP 3V3, pin 8 + P5 out of "
               "B- and still on ESP GND) before GPIO17 may be connected", tx, rx, rx);
      return a;
    }
  }
  a.txRj = (uint8_t)tx;
  a.rxRj = (uint8_t)rx;
  if (requestedRxRj == 0) {  // status query, nothing requested yet
    snprintf(a.reason, sizeof a.reason,
             "ready: inverter TX = RJ45 pin %d (module A+, B- back on pin 8 + P5). Now wire GPIO17 to RJ45 pin %d "
             "(inverter RX) and arm with pin %d", tx, rx, rx);
    return a;
  }
  if (requestedRxRj != rx) {
    snprintf(a.reason, sizeof a.reason, "refused: GPIO17 must go to RJ45 pin %d (inverter RX); pin %d is the inverter TX",
             rx, tx);
    return a;
  }
  a.ok = true;
  snprintf(a.reason, sizeof a.reason, "OK: inverter TX = RJ45 pin %d (module A+), inverter RX = pin %d (GPIO17)", tx, rx);
  return a;
}
