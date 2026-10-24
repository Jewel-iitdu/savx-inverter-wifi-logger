// SafeMode.h - boot-loop protection (d1 §5.8). NVS sys/bootFails counts crash-type resets (panic, watchdog,
// brownout); it is cleared after 120 s of healthy running. At >= 3 the device boots in SAFE MODE:
// no inverter TX, no cloud, Wi-Fi + local UI + /update only, LED solid red. 0.9.9: after 30 min it reboots into
// normal mode by itself (the counter starts again), so a logger nobody can reach does not stay silent for good.
// (Refinement of d1: plain power-on and our own restarts do not count, so power cuts never trigger it.)
#pragma once
#include <stdint.h>

namespace safemode {
bool evaluateAtBoot();         // returns true if this boot runs in safe mode
void loop(uint32_t nowMs);     // clears the counter after HEALTHY_AFTER_MS
void exitAndReboot();          // clear counter + manual flag, restart
uint8_t bootFails();
}  // namespace safemode
