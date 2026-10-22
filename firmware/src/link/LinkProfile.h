// LinkProfile.h - physical link profiles (d1 §3.2). Host-portable.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "config.h"

enum class LinkMode : uint8_t { RS232_LITE = 0, MAX3232 = 1, TTL_DIRECT = 2, RS485_ON_COM = 3 };

inline const char* linkModeName(LinkMode m) {
  switch (m) {
    case LinkMode::RS232_LITE: return "rs232lite";
    case LinkMode::MAX3232: return "max3232";
    case LinkMode::TTL_DIRECT: return "ttl";
    case LinkMode::RS485_ON_COM: return "rs485";
  }
  return "?";
}

struct LinkProfile {
  LinkMode mode = LinkMode::RS232_LITE;
  uint32_t baud = cfg::BAUD_PI30;
  bool rxInvert = true, txInvert = true;      // RS232-lite: both inverted (V1 §6.4)
  uint8_t roPin = cfg::PIN_MOD_HDR_RXD;       // module RO -> UART1 RX
  uint8_t diPin = cfg::PIN_MOD_HDR_TXD;       // module DI -> held HIGH (driver disabled)
  uint8_t txPin = cfg::PIN_INV_TX;            // UART1 TX -> inverter RX (only when armed)
  uint8_t invTxRjPin = 0;                     // 1 or 2 from the idle-level test (0 = unknown); documentation only
  bool diConfirmed = false;                   // module self-ID done: diPin is driven OUTPUT HIGH, else INPUT_PULLUP

  // e.g. "rs232lite-2400-ii-tx1" (i = inverted, n = normal; first letter RX, second TX)
  void describe(char* out, size_t n) const {
    snprintf(out, n, "%s-%lu-%c%c-tx%u", linkModeName(mode), (unsigned long)baud, rxInvert ? 'i' : 'n',
             txInvert ? 'i' : 'n', (unsigned)invTxRjPin);
  }
  bool sameUart(const LinkProfile& o) const {
    return baud == o.baud && rxInvert == o.rxInvert && txInvert == o.txInvert && roPin == o.roPin && txPin == o.txPin;
  }
};
