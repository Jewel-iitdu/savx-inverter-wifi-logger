// ArduinoHal.h - device implementations of the link-layer abstractions (UART1, clock, GPIO, locks).
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "link/Port.h"
#include "model/Lock.h"

// UART1 for the inverter. SAFETY: begin(p, false) leaves GPIO17 as a high-impedance INPUT; the TX pin is
// attached to the UART only when txEnabled (link armed, or the explicit bench loopback).
class ArduinoUart : public IByteStream {
 public:
  explicit ArduinoUart(HardwareSerial& s) : s_(s) {}
  void begin(const LinkProfile& p, bool txEnabled) override;
  void end() override;
  int available() override { return s_.available(); }
  int read() override { return s_.read(); }
  size_t write(const uint8_t* b, size_t n) override { return txAttached_ ? s_.write(b, n) : 0; }
  void flushTx() override { if (txAttached_) s_.flush(true); }
  bool txAttached() const { return txAttached_; }

 private:
  HardwareSerial& s_;
  bool started_ = false;
  bool txAttached_ = false;
};

class ArduinoClock : public IClock {
 public:
  uint32_t ms() override { return millis(); }
  void sleepMs(uint32_t ms) override;  // vTaskDelay + task watchdog feed
};

class ArduinoGpio : public IGpio {
 public:
  void mode(int pin, GpioMode m) override;
  int level(int pin) override { return digitalRead(pin) ? 1 : 0; }
};

class RtosLock : public ILock {
 public:
  RtosLock() { m_ = xSemaphoreCreateRecursiveMutex(); }
  void lock() override { xSemaphoreTakeRecursive(m_, portMAX_DELAY); }
  void unlock() override { xSemaphoreGiveRecursive(m_); }

 private:
  SemaphoreHandle_t m_;
};

void* psramAlloc(size_t n);  // ps_malloc with internal-RAM fallback, zeroed
void driveHighNoGlitch(int pin);  // OUTPUT HIGH without the LOW glitch of pinMode(OUTPUT) + digitalWrite(HIGH)

// Construct a long-lived object / array in PSRAM (keeps big buffers out of the 320 KB internal RAM).
#include <new>
template <typename T>
T* psramNew(size_t count = 1) {
  void* p = psramAlloc(sizeof(T) * count);
  if (!p) return nullptr;
  T* t = static_cast<T*>(p);
  for (size_t i = 0; i < count; i++) new (&t[i]) T();
  return t;
}
