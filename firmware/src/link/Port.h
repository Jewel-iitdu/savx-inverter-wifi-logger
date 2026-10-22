// Port.h - hardware abstraction used by the link layer so it can be unit-tested on the host.
// Arduino implementations live in src/hal/ArduinoHal.*; test doubles in test/.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "link/LinkProfile.h"

// UART byte stream. begin() must leave the TX pin as a high-impedance INPUT when txEnabled == false.
class IByteStream {
 public:
  virtual ~IByteStream() {}
  virtual void begin(const LinkProfile& p, bool txEnabled) = 0;  // (re)configure baud, pins, inversion
  virtual void end() = 0;                                        // release UART pins
  virtual int available() = 0;
  virtual int read() = 0;                                        // -1 if nothing
  virtual size_t write(const uint8_t* b, size_t n) = 0;
  virtual void flushTx() = 0;                                    // block until the last bit has left
};

class IClock {
 public:
  virtual ~IClock() {}
  virtual uint32_t ms() = 0;
  virtual void sleepMs(uint32_t ms) = 0;  // must also feed the task watchdog on the device
};

// IN_PULLDOWN is used only by the receive-only pre-arm check of GPIO17 (PinDiag::preArmCheck).
enum class GpioMode : uint8_t { IN_HIZ, IN_PULLUP, OUT_HIGH, OUT_LOW, IN_PULLDOWN };

class IGpio {
 public:
  virtual ~IGpio() {}
  virtual void mode(int pin, GpioMode m) = 0;
  virtual int level(int pin) = 0;  // 0 or 1
};
