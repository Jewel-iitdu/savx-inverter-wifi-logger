// StatusLed.h - onboard RGB LED patterns (d1 §5.11). GPIO48 (v1.0 / clones) or GPIO38 (v1.1) via LED_PIN.
// On some clones the LED needs a solder bridge; /api/status is the authoritative indicator.
#pragma once
#include <stdint.h>

namespace led {
enum class Mode : uint8_t { BOOT, PORTAL, PROBING, WIFI_DOWN, LINK_OK, BACKFILL, LINK_LOST, SAFE, IDLE };
void begin();
void set(Mode m);
void pulseSample();  // short green blink on every valid inverter reply
void loop(uint32_t nowMs);
}  // namespace led
