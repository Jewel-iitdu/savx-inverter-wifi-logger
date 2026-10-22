// StatusLed.cpp - see StatusLed.h.
#include "sys/StatusLed.h"

#include <Arduino.h>

#include "config.h"

namespace led {

static Mode mode_ = Mode::BOOT;
static uint32_t pulseAt_ = 0;
static uint32_t last_ = 0xFFFFFFFF;

void begin() { rgbLedWrite(cfg::PIN_LED, 0, 0, 0); }
void set(Mode m) { mode_ = m; }
void pulseSample() { pulseAt_ = millis(); }

static void out(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t v = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (v == last_) return;
  last_ = v;
  rgbLedWrite(cfg::PIN_LED, r, g, b);
}

void loop(uint32_t now) {
  const uint8_t L = 24;  // brightness (keep it dim)
  bool slow = (now / 500) & 1, fast = (now / 150) & 1;
  switch (mode_) {
    case Mode::SAFE: out(L, 0, 0); return;
    case Mode::BOOT:
    case Mode::PORTAL: slow ? out(0, 0, L) : out(0, 0, 0); return;
    case Mode::PROBING: fast ? out(L, 0, L) : out(0, 0, 0); return;
    case Mode::WIFI_DOWN: out(L, L / 2, 0); return;
    case Mode::LINK_LOST: slow ? out(L, 0, 0) : out(0, 0, 0); return;
    case Mode::BACKFILL: { uint8_t p = (uint8_t)((now / 40) % 2 ? L : L / 3); out(0, p, p); return; }
    case Mode::LINK_OK:
    case Mode::IDLE:
    default:
      if ((uint32_t)(now - pulseAt_) < 80) out(0, L, 0);
      else out(0, 0, 0);
      return;
  }
}

}  // namespace led
