// Crumbs.h - breadcrumbs and the outage record in RTC memory (1.0.2, see net/OutageLog.h). Device only.
// RTC_NOINIT survives a software restart, a panic and a watchdog reset; a power cut leaves garbage, which the magic
// numbers and the record's checksum reject.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "net/OutageLog.h"

namespace crumbs {
// once, early in setup(): keeps what the previous boot was doing (prevText) and starts fresh crumbs
void bootSnapshot();
const char* prevText();
void mark(outage::Task t);  // top of a task's loop
void step(uint8_t s);       // the uplink task's current step (outage::S_IDLE when done)
outage::Rec& rec();         // the outage record (uplink task only)
}  // namespace crumbs
