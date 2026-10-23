// LiveJson.cpp - see LiveJson.h.
#include "model/LiveJson.h"

#include <math.h>

#include "agg/Aggregator.h"
#include "meter/MeterLive.h"
#include "model/JsonOut.h"

size_t sampleToLiveJson(const Sample& s, char* out, size_t cap, int32_t ageMs, const MeterLive* m) {
  JsonOut j(out, cap);
  j.open();
  j.u("ts", s.ts);
  j.i("lk", s.lk);
  j.num("gv", s.gv, 1);
  j.num("gf", s.gf, 1);
  j.num("ov", s.ov, 1);
  j.num("of", s.of, 1);
  j.num("va", s.va, 0);
  j.num("w", s.w, 0);
  j.num("ld", s.ld, 0);
  j.num("bus", s.bus, 0);
  j.num("bv", s.bv, 2);
  j.num("bci", s.bci, 1);
  j.num("bdi", s.bdi, 1);
  j.num("bi", s.bi, 1);
  j.num("bw", s.bw, 0);
  j.num("soc", s.soc, 0);
  if (!s.tmpRaw) j.num("tmp", s.tmp, 0);
  j.num("pvv", s.pvv, 1);
  j.num("pvi", s.pvi, 1);
  j.num("pvw", s.pvw, 0);
  j.num("sccv", s.sccv, 2);
  // r8 §3.9: grid import estimate = the `egr` integrand (same predicate as the energy, the rules and `nogrid`)
  if (!isnan(s.gv) || s.wf[0]) {
    if (Aggregator::gridPresent(s) && s.md == 'L') {
      if (!isnan(s.w)) j.num("gw", Aggregator::gridW(s), 0);
    } else if (s.md) {
      j.i("gw", 0);
    }
  }
  j.ch("md", s.md);
  if (s.st[0]) j.s("st", s.st);
  if (s.st2[0]) j.s("st2", s.st2);
  if (s.wf[0]) j.s("wf", s.wf);
  if (s.wc != INT16_MIN) j.i("wc", s.wc);
  if (s.fc != INT16_MIN) j.i("fc", s.fc);
  j.num("epv", s.epv, 0);
  j.num("eld", s.eld, 0);
  j.num("egr", s.egr, 0);
  j.num("ech", s.ech, 0);
  j.num("edis", s.edis, 0);
  j.num("epvi", s.epvi, 0);
  j.num("etot", s.etot, 1);
  if (s.q >= 0) j.i("q", s.q);
  if (ageMs >= 0) j.i("age_ms", ageMs);
  if (m) meterLiveFields(j, *m);
  j.close();
  return j.ok() ? j.len() : 0;
}

size_t identityToMetaJson(const Identity& id, const char* fwver, uint32_t bootEpoch, char* out, size_t cap) {
  JsonOut j(out, cap);
  j.open();
  if (id.qpi[0]) j.s("qpi", id.qpi);
  if (id.qid[0]) j.s("qid", id.qid);
  if (id.vfw[0]) j.s("vfw", id.vfw);
  if (id.vfw2[0]) j.s("vfw2", id.vfw2);
  if (id.qmn[0]) j.s("qmn", id.qmn);
  j.num("ratva", id.ratva, 0);
  j.num("ratw", id.ratw, 0);
  j.num("ratbv", id.ratbv, 1);
  if (id.btype >= 0) j.i("btype", id.btype);
  if (id.outpri >= 0) j.i("outpri", id.outpri);
  if (id.chgpri >= 0) j.i("chgpri", id.chgpri);
  if (id.inrng >= 0) j.i("inrng", id.inrng);
  if (id.dia[0]) j.s("dia", id.dia);
  if (id.pv12 == 'W' || id.pv12 == 'A') j.ch("pv12", id.pv12);
  if (id.sup[0]) j.s("sup", id.sup);
  if (id.link[0]) j.s("link", id.link);
  if (fwver) j.s("fwver", fwver);
  if (bootEpoch) j.u("boot", bootEpoch);
  j.close();
  return j.ok() ? j.len() : 0;
}
