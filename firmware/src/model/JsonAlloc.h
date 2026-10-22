// JsonAlloc.h - optional ArduinoJson allocator for parsed cloud documents (alerts.json, cfg, auth, shallow keys).
// The device installs a PSRAM allocator at boot (r8 §4.12: the alerts.json parse is a ~12 KB transient); host
// tests use ArduinoJson's default malloc allocator. Host-portable.
#pragma once
#include <ArduinoJson.h>

namespace jsonalloc {
inline ArduinoJson::Allocator*& slot() {
  static ArduinoJson::Allocator* a = nullptr;
  return a;
}
inline void set(ArduinoJson::Allocator* a) { slot() = a; }
inline ArduinoJson::Allocator* get() {
  return slot() ? slot() : ArduinoJson::detail::DefaultAllocator::instance();
}
}  // namespace jsonalloc

// usage: JsonDocument doc(jsonalloc::get());
