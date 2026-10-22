// Dialect.h - PI30 family dialects (d1 §3.4, §3.6). Host-portable.
#pragma once
#include <stdint.h>
#include <string.h>

enum class Dialect : uint8_t { UNKNOWN = 0, STOCK17, STOCK21, MAX24, GK, REVO };

inline const char* dialectName(Dialect d) {
  switch (d) {
    case Dialect::STOCK17: return "STOCK17";
    case Dialect::STOCK21: return "STOCK21";
    case Dialect::MAX24: return "MAX24";
    case Dialect::GK: return "GK";
    case Dialect::REVO: return "REVO";
    default: return "UNKNOWN";
  }
}
inline Dialect dialectFromName(const char* s) {
  if (!s) return Dialect::UNKNOWN;
  if (!strcmp(s, "STOCK17")) return Dialect::STOCK17;
  if (!strcmp(s, "STOCK21")) return Dialect::STOCK21;
  if (!strcmp(s, "MAX24")) return Dialect::MAX24;
  if (!strcmp(s, "GK")) return Dialect::GK;
  if (!strcmp(s, "REVO")) return Dialect::REVO;
  return Dialect::UNKNOWN;
}
