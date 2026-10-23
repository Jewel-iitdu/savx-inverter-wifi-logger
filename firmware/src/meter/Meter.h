// Meter.h - the grid meter reader (0.9.0): a Smart Life / Tuya breaker with an energy meter on the grid line in
// front of the inverter, read over the LAN (port 6668) every METER_POLL_MS. Device only.
//  - Read-only: a status query per poll on a short connection, closed straight away. Nothing is ever switched or
//    set; the Smart Life app (and its cloud, schedule and statistics) keeps working as before.
//  - Found by its key, not its IP: if the saved IP stops answering, the /24 is scanned for port 6668 and the device
//    that answers our key wins (DHCP can move it after a router restart).
//  - Isolated: its own low-priority task, not on the task watchdog. Two software crashes (panic / watchdog, not
//    brown-outs) while it was enabled switch it off until it is re-armed (cloud cfg.meter off -> on, or saving
//    /meter); the rest of the logger is unaffected.
#pragma once
#include <stdint.h>

#include "meter/MeterLive.h"

namespace meter {

struct Snap {
  bool configured = false, enabled = false, running = false, fresh = false;
  char state[40] = "";     // short English status for /meter, /api/status and the cloud `status`
  char ip[16] = "";
  int proto = 0;           // working protocol 33/34/35 (0 = not found yet)
  uint32_t ageS = 0;       // seconds since the last good reading
  uint32_t polls = 0, fails = 0, scans = 0, resets = 0;
  MeterLive m;             // last good reading (with today's egm)
};

void begin(bool safeMode);            // after settings and Wi-Fi; starts the task if configured
Snap snap();                          // thread-safe copy
bool live(MeterLive& m);              // the reading if fresh and enabled (for `live`)
int32_t egmFor(int32_t day);          // metered Wh of a day (-1 = unknown / partial / disabled)
bool takeFinishedDay(int32_t& day, int32_t& wh);  // once per roll-over: write daily/<day>/egm
void setEnabled(bool on);             // cloud cfg.meter (on -> re-arms after a crash stop)

// /meter (admin): device id and local key from tools/meter/get-key.sh. proto 0 = auto. ip "" = keep / search.
bool saveConfig(const char* id, const char* key, int proto, const char* ip, char* err, size_t errN);
void clearConfig();

}  // namespace meter
