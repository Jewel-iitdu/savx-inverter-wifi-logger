// RuleTypes.cpp - see RuleTypes.h.
#include "agg/RuleTypes.h"

#include <stdio.h>
#include <string.h>

namespace rules {

// id, label, unit, energy, est, hystAbs, hystPct, vmin, vmax   (r8 §4.3 + the value ranges of §3.11)
static const MetricInfo CATALOG[(int)Metric::COUNT] = {
    {"w", "Load power", Unit::W, false, false, 50.f, 0.05f, -10000.f, 20000.f},
    {"pvw", "Solar power", Unit::W, false, false, 50.f, 0.05f, -10000.f, 20000.f},
    {"gw", "Grid import power (est.)", Unit::W, false, true, 50.f, 0.05f, -10000.f, 20000.f},
    {"bdw", "Battery discharge power", Unit::W, false, false, 50.f, 0.05f, -10000.f, 20000.f},
    {"bw", "Battery net power", Unit::W, false, false, 50.f, 0.05f, -10000.f, 20000.f},
    {"soc", "Battery SOC", Unit::PCT, false, false, 2.f, 0.f, 0.f, 250.f},
    {"ld", "Load % of rating", Unit::PCT, false, false, 2.f, 0.f, 0.f, 250.f},
    {"bv", "Battery voltage", Unit::V, false, false, 0.3f, 0.f, 0.f, 600.f},
    {"gv", "Grid voltage", Unit::V, false, false, 5.f, 0.f, 0.f, 600.f},
    {"ov", "Output voltage", Unit::V, false, false, 5.f, 0.f, 0.f, 600.f},
    {"pvv", "Solar voltage", Unit::V, false, false, 10.f, 0.f, 0.f, 600.f},
    {"bdi", "Battery discharge current", Unit::A, false, false, 2.f, 0.f, 0.f, 300.f},
    {"tmp", "Inverter temperature", Unit::DEGC, false, false, 3.f, 0.f, -20.f, 150.f},
    {"gf", "Grid frequency", Unit::HZ, false, false, 0.3f, 0.f, 40.f, 70.f},
    {"onbat", "On battery", Unit::BOOL, false, false, 0.f, 0.f, 0.f, 1.f},
    {"nogrid", "Grid absent", Unit::BOOL, false, false, 0.f, 0.f, 0.f, 1.f},
    {"eld", "Load energy", Unit::WH, true, false, 0.f, 0.f, 0.f, 5000000.f},
    {"egr", "Grid energy (est.)", Unit::WH, true, true, 0.f, 0.f, 0.f, 5000000.f},
    {"epv", "Solar energy", Unit::WH, true, false, 0.f, 0.f, 0.f, 5000000.f},
    {"edis", "Battery discharged", Unit::WH, true, false, 0.f, 0.f, 0.f, 5000000.f},
    {"ech", "Battery charged", Unit::WH, true, false, 0.f, 0.f, 0.f, 5000000.f},
    {"gout", "Grid outage time", Unit::MIN, true, false, 0.f, 0.f, 0.f, 44640.f},
};

const MetricInfo& info(Metric m) {
  int i = (int)m;
  if (i < 0 || i >= (int)Metric::COUNT) i = 0;
  return CATALOG[i];
}

bool metricFromId(const char* id, Metric& out) {
  if (!id) return false;
  for (int i = 0; i < (int)Metric::COUNT; i++)
    if (!strcmp(CATALOG[i].id, id)) {
      out = (Metric)i;
      return true;
    }
  return false;
}

const char* scopeName(Scope s) { return s == Scope::DAY ? "day" : s == Scope::MONTH ? "month" : "inst"; }
const char* opName(Op o) { return o == Op::GT ? "gt" : o == Op::GE ? "ge" : o == Op::LT ? "lt" : "le"; }

const char* stName(St s) {
  switch (s) {
    case St::OFF: return "off";
    case St::WAIT: return "wait";
    case St::IDLE: return "idle";
    case St::PEND: return "pend";
    case St::FIRE: return "fire";
    case St::CLR: return "clr";
    case St::LATCH: return "latch";
    case St::DONE: return "done";
    case St::INV: return "inv";
  }
  return "off";
}

size_t metricIdList(char* out, size_t n) {
  size_t len = 0;
  if (n) out[0] = 0;
  for (int i = 0; i < (int)Metric::COUNT; i++) {
    int w = snprintf(out + len, n > len ? n - len : 0, "%s%s", i ? "|" : "", CATALOG[i].id);
    if (w < 0) break;
    len += (size_t)w;
  }
  return len;
}

static uint32_t fnv(uint32_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 16777619u;
  }
  return h;
}

uint32_t semanticHash(const Def& d) {
  uint32_t h = 2166136261u;
  uint8_t e[4] = {(uint8_t)d.metric, (uint8_t)d.scope, (uint8_t)d.op, (uint8_t)d.mode};
  h = fnv(h, e, sizeof e);
  h = fnv(h, &d.value, sizeof d.value);
  h = fnv(h, &d.atMin, sizeof d.atMin);
  h = fnv(h, &d.forSec, sizeof d.forSec);
  uint8_t hc = d.hasClr ? 1 : 0, hw = d.hasWin ? 1 : 0;
  h = fnv(h, &hc, 1);
  if (d.hasClr) h = fnv(h, &d.clr, sizeof d.clr);
  h = fnv(h, &d.clrSec, sizeof d.clrSec);
  h = fnv(h, &hw, 1);
  if (d.hasWin) {
    h = fnv(h, &d.winFrom, sizeof d.winFrom);
    h = fnv(h, &d.winTo, sizeof d.winTo);
    h = fnv(h, &d.days, sizeof d.days);
  }
  return h ? h : 1;
}

int slotFromRid(const char* rid) {
  if (!rid || rid[0] != 'r' || strlen(rid) != 3) return -1;
  if (rid[1] < '0' || rid[1] > '1' || rid[2] < '0' || rid[2] > '9') return -1;
  int n = (rid[1] - '0') * 10 + (rid[2] - '0');
  if (n < 1 || n > (int)cfg::MAX_RULES) return -1;
  return n - 1;
}

}  // namespace rules
