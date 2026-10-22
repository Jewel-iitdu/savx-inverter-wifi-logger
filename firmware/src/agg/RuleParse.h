// RuleParse.h - parse GET /devices/<id>/alerts.json into a rules::Set (research/r8-email-alerts.md §3.2-3.3, §4.1).
// Host-portable (ArduinoJson 7.4.3). The checks mirror the RTDB `.validate` rules of r8 §3.11 exactly, so a rule the
// database accepted is also accepted here; a rule this firmware does not understand (e.g. a metric from a newer
// catalogue) is NOT a parse failure: its slot is marked invalid (state INV, one RULE_INVALID event per alertsRev).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "agg/RuleTypes.h"

namespace rules {

// Returns false only if the document is not JSON / not an object or null (the caller keeps its previous set and
// reports `err`). `null` (no alerts configured) gives an empty set with e-mail off.
bool parse(const char* json, size_t n, Set& out, char* err, size_t errn);

// helpers (exposed for the tests and the web-facing formatter)
size_t utf16Len(const char* utf8);
void copyUtf8(char* dst, size_t cap, const char* src);     // truncates on a code-point boundary
void utf8Trim(char* s);                                       // drop a trailing partial UTF-8 sequence
bool parseHhmm(const char* s, uint16_t& minutes);

}  // namespace rules
