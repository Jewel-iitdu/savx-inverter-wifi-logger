// LiveJson.h - Sample -> `live` JSON (d1 §4.2, exact short keys) and Identity -> flat `meta` JSON.
// Host-portable. Missing values are omitted. `gw` (r8 §3.9) is the grid-import estimate (W).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "model/Identity.h"
#include "model/Sample.h"

struct MeterLive;
// m: a fresh grid meter reading (0.9.0) or nullptr
size_t sampleToLiveJson(const Sample& s, char* out, size_t cap, int32_t ageMs = -1, const MeterLive* m = nullptr);
// meta keys: qpi qid vfw vfw2 qmn ratva ratw ratbv btype outpri chgpri inrng dia pv12 sup link fwver boot
size_t identityToMetaJson(const Identity& id, const char* fwver, uint32_t bootEpoch, char* out, size_t cap);
