// RuleTypes.h - usage-threshold rule definitions (research/r8-email-alerts.md §3.3, §4.2-4.3). Host-portable.
//
// The metric catalogue below is `acat` = 1 (cfg::ALERT_CATALOG_VERSION). Its ids MUST equal the metric regex in
// firebase/database.rules.json (`alerts/rules/$rid/metric`) and the web app's alertMetrics.ts. r8 §4.3 plans to
// generate it from shared/alert-metrics.json; until that generator exists it is maintained here and checked by
// test/test_rule_parse (test_metric_catalogue_matches_rules_regex).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace rules {

enum class Metric : uint8_t { W, PVW, GW, BDW, BW, SOC, LD, BV, GV, OV, PVV, BDI, TMP, GF, ONBAT, NOGRID,
                              ELD, EGR, EPV, EDIS, ECH, GOUT, COUNT };
enum class Scope : uint8_t { INST, DAY, MONTH };
enum class Op : uint8_t { GT, GE, LT, LE };
enum class Mode : uint8_t { HOLD, AT };
enum class St : uint8_t { OFF, WAIT, IDLE, PEND, FIRE, CLR, LATCH, DONE, INV };
enum class Unit : uint8_t { W, WH, V, A, PCT, DEGC, HZ, MIN, BOOL };

struct MetricInfo {
  const char* id;
  const char* label;
  Unit unit;
  bool energy;      // day/month scope only (eld egr epv edis ech gout); everything else is inst only
  bool est;         // "est." in every message (egr, gw)
  float hystAbs;    // default hysteresis when `clr` is absent: max(hystAbs, hystPct * |value|)
  float hystPct;
  float vmin, vmax; // allowed `value` range (same as the RTDB rules)
};

const MetricInfo& info(Metric m);
bool metricFromId(const char* id, Metric& out);
const char* scopeName(Scope s);
const char* opName(Op o);
const char* stName(St s);   // off wait idle pend fire clr latch done inv (alertState.st)
// all metric ids joined with '|' (for the rules-regex consistency test)
size_t metricIdList(char* out, size_t n);

struct Def {                       // one rule slot (r01..r16), canonical units
  bool present = false;            // slot exists in alerts/rules
  char rid[4] = "";
  char name[121] = "";             // <= 40 UTF-16 units (Bangla = 3 bytes each in UTF-8)
  bool on = false, email = false, ntfy = false, clrMail = false, hasClr = false, hasWin = false, invalid = false;
  Metric metric = Metric::W;
  Scope scope = Scope::INST;
  Op op = Op::GT;
  Mode mode = Mode::HOLD;
  float value = 0, clr = 0;
  uint32_t forSec = 60, clrSec = 60;
  uint16_t coolMin = 60, repeatMin = 0, atMin = 0, winFrom = 0, winTo = 0;  // minutes of the local day
  uint8_t maxDay = 3, minCov = 80, sev = 3, rc = 0, days = 127;           // rc 0 = all recipients
  uint32_t semHash = 0;            // FNV-1a over the semantic fields (r8 §4.2)
  char err[40] = "";               // why the rule is invalid
};

struct EmailPolicy {
  bool on = false;
  uint8_t builtinMinSev = 4;       // 0 = no built-in alert is e-mailed (TEST_ALERT still is while `on`)
  bool builtinClear = true;
  uint8_t monthStart = 1;          // billing-cycle start day 1..28
  char to[301] = "";               // alerts/email/to: 0-3 comma-separated addresses (bit i of a rule's rc = address i)
  char fromName[41] = "";          // alerts/email/fromName (display name of the sender)
  uint8_t maxPerDay = 40;          // alerts/email/maxPerDay (1..40)
};

struct Set {
  double rev = 0;                  // cfg/alertsRev (ms) this set was loaded for
  uint8_t n = 0;                   // slots present
  uint8_t nInvalid = 0;
  Def d[cfg::MAX_RULES];           // index = slot (r01 = 0)
  EmailPolicy email;
  char err[41] = "";               // first problem, for status.ruleErr ("" = none)
};

uint32_t semanticHash(const Def& d);
// slot from "r01".."r16" (-1 if not a valid rule id)
int slotFromRid(const char* rid);

}  // namespace rules
