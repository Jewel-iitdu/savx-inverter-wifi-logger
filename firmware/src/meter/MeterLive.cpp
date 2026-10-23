// MeterLive.cpp - see MeterLive.h.
#include "meter/MeterLive.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "model/JsonAlloc.h"
#include "model/JsonOut.h"

namespace {

enum class K : uint8_t { COUNTER, PHASE, W, V, A, PF, HZ, RC, TC, ON, FAULT };
struct Dp {
  const char* id;
  K k;
  float div;  // value / div = unit
};
// The owner's breaker: Smart Life "Intelligent switch", category dlq, product kupa78rwoclnsx7j, protocol 3.5. Map
// read with tools/meter/get-key.sh on 2026-10-24 and checked against the Smart Life screen. 25 / 50 are not in the
// cloud map: 25 matched "Temp_C 37", and 50 = 75 matched P / (V x I) = 201 / (245.6 x 1.090) = 0.75.
const Dp MAP[] = {
    {"1", K::COUNTER, 1},  // forward_energy_total, 0.01 kWh (kept as the integer)
    {"6", K::PHASE, 1},    // phase_a, raw: V (2 B, 0.1 V), I (3 B, mA), P (3 B, W); only on refresh / every ~15 s
    {"9", K::FAULT, 1},    // fault, bitmap: bit 0 short circuit ... 22 high-temperature early warning (0.9.1)
    {"15", K::RC, 1},      // leakage_current, mA
    {"16", K::ON, 1},      // switch (breaker closed)
    {"25", K::TC, 1},      // temperature, °C
    {"32", K::HZ, 10},     // supply_frequency, 0.1 Hz
    {"50", K::PF, 100},    // power factor x 100
};

int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

size_t b64decode(const char* s, uint8_t* out, size_t cap) {
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (; *s && *s != '='; s++) {
    int v = b64val(*s);
    if (v < 0) return 0;
    acc = acc << 6 | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= cap) return 0;
      out[n++] = (uint8_t)(acc >> bits);
    }
  }
  return n;
}

}  // namespace

bool meterDecodeDps(const char* json, size_t n, MeterLive& m) {
  JsonDocument doc(jsonalloc::get());
  if (deserializeJson(doc, json, n)) return false;
  JsonObjectConst dps = doc["dps"].as<JsonObjectConst>();
  if (dps.isNull()) dps = doc["data"]["dps"].as<JsonObjectConst>();
  if (dps.isNull()) return false;
  for (const Dp& d : MAP) {
    JsonVariantConst v = dps[d.id];
    if (v.isNull()) continue;
    switch (d.k) {
      case K::COUNTER:
        if (v.is<long long>() && v.as<long long>() >= 0 && v.as<long long>() < 4000000000LL) {
          m.counter = (uint32_t)v.as<long long>();
          m.haveCounter = true;
        }
        break;
      case K::PHASE: {
        uint8_t b[16];
        if (v.is<const char*>() && b64decode(v.as<const char*>(), b, sizeof b) >= 8) {
          m.v = ((b[0] << 8) | b[1]) / 10.f;
          m.a = ((b[2] << 16) | (b[3] << 8) | b[4]) / 1000.f;
          m.w = (float)((b[5] << 16) | (b[6] << 8) | b[7]);
        }
        break;
      }
      case K::ON:
        if (v.is<bool>()) m.on = v.as<bool>() ? 1 : 0;
        break;
      case K::FAULT:
        if (v.is<long long>() && v.as<long long>() >= 0 && v.as<long long>() < (1LL << 31)) m.fault = (int32_t)v.as<long long>();
        break;
      default:
        if (v.is<float>()) {
          float x = v.as<float>() / d.div;
          float* f = d.k == K::W ? &m.w : d.k == K::V ? &m.v : d.k == K::A ? &m.a : d.k == K::PF ? &m.pf
                   : d.k == K::HZ ? &m.hz : d.k == K::RC ? &m.rc : &m.tc;
          *f = x;
        }
        break;
    }
  }
  return true;
}

void meterLiveFields(JsonOut& j, const MeterLive& m) {
  j.num("mw", m.w, 0);
  j.num("mv", m.v, 1);
  j.num("mi", m.a, 3);
  j.num("mpf", m.pf, 2);
  j.num("mhz", m.hz, 1);
  j.num("mrc", m.rc, 0);
  j.num("mtc", m.tc, 0);
  if (m.haveCounter) j.num("mkwh", m.counter / 100.0, 2);
  if (m.on >= 0) j.i("mon", m.on);
  if (m.egm >= 0) j.i("egm", m.egm);
  if (m.fault >= 0) j.i("mft", m.fault);
  if (m.ageS >= 0) j.i("mage", m.ageS);
}

bool jsonAppendInt(char* buf, size_t cap, const char* key, long v) {
  size_t len = strlen(buf);
  if (len < 2 || buf[len - 1] != '}') return false;
  char t[48];
  int k = snprintf(t, sizeof t, "%s\"%s\":%ld}", buf[len - 2] == '{' ? "" : ",", key, v);
  if (k <= 0 || len - 1 + (size_t)k >= cap) return false;
  memcpy(buf + len - 1, t, (size_t)k + 1);
  return true;
}
