// Crumbs.cpp - see Crumbs.h.
#include "sys/Crumbs.h"

#include <Arduino.h>
#include <esp_attr.h>

namespace crumbs {
namespace {
RTC_NOINIT_ATTR outage::Crumbs s_c;
RTC_NOINIT_ATTR outage::Rec s_rec;
char s_prev[64] = "";
}  // namespace

void bootSnapshot() {
  outage::crumbsText(s_c, s_prev, sizeof s_prev);
  outage::crumbsReset(s_c);
}

const char* prevText() { return s_prev; }

void mark(outage::Task t) {
  if (t < outage::T_N) s_c.ms[t] = millis() | 1;  // 0 = "not running"
}

void step(uint8_t s) {
  s_c.stepMs = millis() | 1;
  s_c.step = s;
}

outage::Rec& rec() { return s_rec; }

}  // namespace crumbs
