// Console.h - USB serial console (115200, the "COM" USB-C port) mirroring the local diagnostics.
#pragma once
#include <Arduino.h>

namespace console {
void begin();
void loop();  // non-blocking; call from loopTask
void printStatus();
}  // namespace console
