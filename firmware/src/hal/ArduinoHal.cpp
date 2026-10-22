// ArduinoHal.cpp - see ArduinoHal.h.
#include "hal/ArduinoHal.h"

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

void driveHighNoGlitch(int pin) {
  // Set the output latch HIGH while the pin is still an input, THEN enable the driver: a plain
  // pinMode(OUTPUT); digitalWrite(HIGH) emits a short LOW pulse first (the latch resets to 0), and a LOW on
  // the module's DI pin switches its auto-direction transmitter on (safety review, docs/03 appendix).
  pinMode(pin, INPUT_PULLUP);
  digitalWrite(pin, HIGH);  // gpio_set_level() on a GPIO-bus pin writes the latch even while it is an input
  pinMode(pin, OUTPUT);     // gpio_config() enables the output without touching the latch
}

void ArduinoUart::begin(const LinkProfile& p, bool txEnabled) {
  // Always start from a clean state: end() detaches the previous RX/TX pins (TX returns to a plain GPIO).
  if (started_) s_.end();
  txAttached_ = false;
  pinMode(p.txPin, INPUT);  // high-Z: nothing is driven on the inverter-RX wire
  // Module DI pin: driven HIGH (driver disabled) once self-ID confirmed it; otherwise a pulled-up input.
  if (p.mode == LinkMode::RS232_LITE && p.diConfirmed) {
    driveHighNoGlitch(p.diPin);
  } else if (p.diPin != p.roPin && p.diPin != p.txPin) {
    pinMode(p.diPin, INPUT_PULLUP);
  }
  s_.setRxBufferSize(1024);
  // RX only: txPin = -1 keeps TX unassigned (it was detached by end()).
  s_.begin(p.baud, SERIAL_8N1, p.roPin, -1, false);
  s_.setRxInvert(p.rxInvert);
  s_.setTxInvert(p.txInvert);  // set BEFORE the pin is attached so the idle level is right from the first edge
  started_ = true;
  if (txEnabled) {
    s_.setPins(-1, p.txPin);
    txAttached_ = true;
  }
}

void ArduinoUart::end() {
  if (started_) s_.end();
  started_ = false;
  txAttached_ = false;
  pinMode(cfg::PIN_INV_TX, INPUT);
}

void ArduinoClock::sleepMs(uint32_t ms) {
  if (ms == 0) ms = 1;
  // feed the TWDT on long waits (probe, idle test), but only from a subscribed task: calling reset from an
  // unsubscribed task logs "esp_task_wdt_reset: task not found" every time
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

void ArduinoGpio::mode(int pin, GpioMode m) {
  switch (m) {
    case GpioMode::IN_HIZ: pinMode(pin, INPUT); break;
    case GpioMode::IN_PULLUP: pinMode(pin, INPUT_PULLUP); break;
    case GpioMode::IN_PULLDOWN: pinMode(pin, INPUT_PULLDOWN); break;
    case GpioMode::OUT_HIGH: driveHighNoGlitch(pin); break;
    case GpioMode::OUT_LOW: pinMode(pin, OUTPUT); digitalWrite(pin, LOW); break;
  }
}

void* psramAlloc(size_t n) {
  void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = calloc(1, n);
  return p;
}
