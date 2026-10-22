// Decode.h - map parsed replies into the normalized Sample (d1 §3.8 table) + plausibility gates (§3.5).
// Host-portable.
#pragma once
#include "model/Sample.h"
#include "proto/pi30/Dialect.h"
#include "proto/pi30/Pi30Parse.h"

namespace decode {

// QPIGS field-12 semantics for one sample (d1 §3.6 rule 5): 'W' power, 'A' current, '?' undecided.
char classifyPv12(const pi30p::Qpigs& q);

// Merge QPIGS. On REVO only bus V and the status bits are taken (QALL is primary).
// pv12: persisted classification ('W'/'A'/'?'); '?' uses this sample's evidence, default current.
void qpigs(const pi30p::Qpigs& q, Dialect d, char pv12, Sample& s);
void qall(const pi30p::Qall& a, Sample& s);
void qmod(char mode, Sample& s);
void qpiws(const char* wf, Dialect d, Sample& s);
void q1(const pi30p::Q1& q, Sample& s);

// Gates on the core values (d1 §3.5).
bool plausibleQpigs(const pi30p::Qpigs& q);
bool plausibleQall(const pi30p::Qall& a);
bool plausibleSample(const Sample& s);

// Copy the slow-changing fields (mode, status, warnings, energy counters) from `from` into `to`.
void carrySlowFields(const Sample& from, Sample& to);

}  // namespace decode
