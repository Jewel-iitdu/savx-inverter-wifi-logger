// codes.h - warning/fault code tables. Hand-written for FW-1; intended to be regenerated later from
// shared/codes.json (d1 §3.9) so the device and the web app decode identically.
//
// Sources (all read during research, see research/v1-protocol-verification.md and r2 §5.6):
//  - LCD fault/warning codes: the Savx F4 paper manual §5/§6 (research/manual-extract.md) [CONFIRMED-primary]
//  - QPIWS bit meanings: Voltronic PI30 2014 doc §2.9 (a0..a31) + PI30MAX/PGK extras (r2 §5.6) [CONFIRMED-primary]
//  - REVO QALL warning 00-07/21-26 and fault 01-12: Sorotec "Revo RS232 Protocol V02" p.4 [CONFIRMED-primary]
// IMPORTANT (d1 K13): the numbering of these tables differs. Always store the raw code WITH its source tag.
#pragma once
#include <stdint.h>
#include <string.h>

namespace codes {

enum class Src : uint8_t { NONE = 0, LCD = 1, QPIWS32 = 2, QPIWS36 = 3, REVO = 4, SMG = 5, POWMR = 6 };

inline const char* srcTag(Src s) {
  switch (s) {
    case Src::LCD: return "lcd";
    case Src::QPIWS32: return "qpiws32";
    case Src::QPIWS36: return "qpiws36";
    case Src::REVO: return "revo";
    case Src::SMG: return "smg";
    case Src::POWMR: return "powmr";
    default: return "none";
  }
}

struct NumText { int code; const char* text; };
struct StrText { const char* code; const char* text; };

// Savx F4 manual §5 "Fault Reference Code" (Voltronic numbering; also used by QPIWS a31-a32 on 24 V GK/MAX).
static const NumText LCD_FAULTS[] = {
    {1, "Fan is locked when inverter is off"},
    {2, "Over temperature"},
    {3, "Battery voltage is too high"},
    {4, "Battery voltage is too low"},
    {5, "Output short circuited or over temperature detected by internal converter components"},
    {6, "Output voltage is too high"},
    {7, "Overload time out"},
    {8, "Bus voltage is too high"},
    {9, "Bus soft start failed"},
    {51, "Over current or surge"},
    {52, "Bus voltage is too low"},
    {53, "Inverter soft start failed"},
    {55, "Over DC voltage in AC output"},
    {57, "Current sensor failed"},
    {58, "Output voltage is too low"},
    {59, "PV voltage is over limitation"},
};

// Savx F4 manual §6 "Warning Indicator" (shown on the LCD only; not transmitted as numbers by PI30).
static const StrText LCD_WARNINGS[] = {
    {"01", "Fan is locked when inverter is on"},
    {"02", "Over temperature"},
    {"03", "Battery is over-charged"},
    {"04", "Low battery"},
    {"07", "Overload"},
    {"08", "Discharge current limiting"},
    {"10", "Output power derating"},
    {"15", "PV energy is low"},
    {"16", "High AC input (>280VAC) during BUS soft start"},
    {"E9", "Battery equalization"},
    {"bP", "Battery is not connected"},
};

// Sorotec REVO QALL warning code table (doc p.4). 08 and 39 are mpp-solar additions, NOT in the doc (V1 C12).
static const NumText REVO_WARNINGS[] = {
    {0, "No warning"},          {1, "Low battery"},          {2, "Mains low voltage"},
    {3, "Mains high voltage"},  {4, "Overload"},             {5, "Over temperature"},
    {6, "Fan lock"},            {7, "Battery overvoltage"},  {21, "PV low voltage"},
    {22, "PV overvoltage"},     {23, "PV overcurrent"},      {24, "PV over temperature"},
    {25, "PV overload"},        {26, "PV boost failed"},
};

// Sorotec REVO QALL fault code table (doc p.4). NOT the LCD numbering (V1 C13).
static const NumText REVO_FAULTS[] = {
    {1, "BUS overvoltage"},
    {2, "Inverter overvoltage"},
    {3, "Inverter low voltage"},
    {4, "BUS failure"},
    {5, "Overload fault"},
    {6, "Output short circuit"},
    {7, "Low battery voltage at start-up"},
    {8, "Inverter failed"},
    {9, "BUS low voltage"},
    {10, "Parallel failure"},
    {11, "Over temperature fault"},
    {12, "Battery over-voltage at start-up (check cell count)"},
};

// QPIWS bit classes. F = fault, W = warning, A1 = "fault if a1=1, else warning" (P2014 rule).
enum class BitClass : uint8_t { RESERVED, W, F, A1 };
struct QpiwsBit { const char* name; BitClass cls; };

// P2014 HS/MS/MSX a0..a31 (32 chars). Index = character position in the reply payload.
static const QpiwsBit QPIWS_P2014[32] = {
    {"Reserved", BitClass::RESERVED},              // a0  (PMAX/PGK: PV loss, W)
    {"Inverter fault", BitClass::F},               // a1
    {"Bus over", BitClass::F},                     // a2
    {"Bus under", BitClass::F},                    // a3
    {"Bus soft fail", BitClass::F},                // a4
    {"Line fail", BitClass::W},                    // a5  LINE_FAIL
    {"OPV short", BitClass::W},                    // a6  (PMAX/PGK: F)
    {"Inverter voltage too low", BitClass::F},     // a7
    {"Inverter voltage too high", BitClass::F},    // a8
    {"Over temperature", BitClass::A1},            // a9
    {"Fan locked", BitClass::A1},                  // a10
    {"Battery voltage high", BitClass::A1},        // a11
    {"Battery low alarm", BitClass::W},            // a12
    {"Reserved", BitClass::RESERVED},              // a13
    {"Battery under shutdown", BitClass::W},       // a14
    {"Reserved", BitClass::RESERVED},              // a15 (PMAX/PGK: battery derating, W)
    {"Over load", BitClass::A1},                   // a16
    {"EEPROM fault", BitClass::W},                 // a17
    {"Inverter over current", BitClass::F},        // a18
    {"Inverter soft fail", BitClass::F},           // a19
    {"Self test fail", BitClass::F},               // a20
    {"OP DC voltage over", BitClass::F},           // a21
    {"Battery open", BitClass::F},                 // a22
    {"Current sensor fail", BitClass::F},          // a23
    {"Battery short", BitClass::F},                // a24
    {"Power limit", BitClass::W},                  // a25
    {"PV voltage high", BitClass::W},              // a26
    {"MPPT overload fault", BitClass::W},          // a27
    {"MPPT overload warning", BitClass::W},        // a28
    {"Battery too low to charge", BitClass::W},    // a29
    {"Reserved", BitClass::RESERVED},              // a30 (PGK: DC/DC over current, F)
    {"Reserved", BitClass::RESERVED},              // a31 (24 V MAX/GK: fault-code digit 1)
};

// Bits used by the alert engine (d1 §7.2)
constexpr int QPIWS_INVERTER_FAULT = 1;
constexpr int QPIWS_LINE_FAIL = 5;
constexpr int QPIWS_OVER_TEMP = 9;
constexpr int QPIWS_FAN_LOCKED = 10;
constexpr int QPIWS_BATT_HIGH = 11;
constexpr int QPIWS_BATT_LOW_ALARM = 12;
constexpr int QPIWS_BATT_UNDER_SHUTDOWN = 14;
constexpr int QPIWS_OVERLOAD = 16;
constexpr int QPIWS_PV_VOLT_HIGH = 26;

inline const char* findNum(const NumText* t, size_t n, int code) {
  for (size_t i = 0; i < n; i++)
    if (t[i].code == code) return t[i].text;
  return nullptr;
}
inline const char* lcdFaultText(int code) { return findNum(LCD_FAULTS, sizeof(LCD_FAULTS) / sizeof(LCD_FAULTS[0]), code); }
inline const char* revoWarningText(int code) { return findNum(REVO_WARNINGS, sizeof(REVO_WARNINGS) / sizeof(REVO_WARNINGS[0]), code); }
inline const char* revoFaultText(int code) { return findNum(REVO_FAULTS, sizeof(REVO_FAULTS) / sizeof(REVO_FAULTS[0]), code); }

// Text for a fault code given its source table. Returns "unknown" for codes not in the table.
inline const char* faultText(Src src, int code) {
  const char* t = nullptr;
  if (src == Src::REVO) t = revoFaultText(code);
  else if (src == Src::QPIWS36 || src == Src::LCD) t = lcdFaultText(code);
  return t ? t : "unknown fault code";
}

// Name of QPIWS bit idx for a reply of length len (32 = P2014, 36 = PMAX/PGK, 38 = P2015).
inline const char* qpiwsBitName(int idx, int len) {
  if (len >= 36) {
    if (idx == 0) return "PV loss";
    if (idx == 6) return "OPV short";
    if (idx == 15) return "Battery derating";
    if (idx == 31 || idx == 32) return "Fault code digit";
    if (idx == 33) return "Low PV energy";
    if (idx == 34) return "High AC input during BUS soft start";
    if (idx == 35) return "Battery equalization";
  }
  if (idx >= 0 && idx < 32) return QPIWS_P2014[idx].name;
  return "Reserved";
}

// True if bit idx is fault-class (d1 §3.9: a1-a4, a7, a8, a18-a24, plus a9/a10/a11/a16 when a1=1).
inline bool qpiwsBitIsFault(int idx, bool a1) {
  if (idx < 0 || idx >= 32) return false;
  BitClass c = QPIWS_P2014[idx].cls;
  if (c == BitClass::F) return true;
  if (c == BitClass::A1) return a1;
  return false;
}

// Scan a QPIWS bit string; returns true if any fault-class bit is set.
inline bool qpiwsHasFault(const char* wf) {
  if (!wf) return false;
  size_t n = strlen(wf);
  bool a1 = n > 1 && wf[1] == '1';
  for (size_t i = 0; i < n && i < 31; i++)
    if (wf[i] == '1' && qpiwsBitIsFault((int)i, a1)) return true;
  return false;
}
inline bool qpiwsBit(const char* wf, int idx) {
  if (!wf) return false;
  size_t n = strlen(wf);
  return idx >= 0 && (size_t)idx < n && wf[idx] == '1';
}
inline int qpiwsCount(const char* wf) {
  int c = 0;
  if (!wf) return 0;
  size_t n = strlen(wf);
  size_t lim = n >= 36 ? 31 : n;  // on 36/38-char replies a31.. carry fault-code digits
  for (size_t i = 0; i < lim; i++) c += wf[i] == '1';
  return c;
}

}  // namespace codes
