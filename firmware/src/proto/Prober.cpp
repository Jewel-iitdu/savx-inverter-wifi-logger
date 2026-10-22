// Prober.cpp - see Prober.h.
#include "proto/Prober.h"

#include <stdio.h>
#include <string.h>

#include "proto/modbus/ModbusRtu.h"

int Prober::profiles(const LinkProfile& base, LinkProfile* out, int max) {
  struct P { uint32_t baud; bool rx, tx; };
  // P1..P5 (RS232-lite expectations, d1 §3.2), P6 = non-inverted for MAX3232 / TTL wiring
  static const P lite[] = {{2400, true, true}, {2400, false, true}, {2400, true, false}, {9600, true, true}, {9600, false, true}};
  static const P plain[] = {{2400, false, false}, {9600, false, false}};
  int n = 0;
  auto push = [&](const P& p) {
    if (n >= max) return;
    out[n] = base;
    out[n].baud = p.baud;
    out[n].rxInvert = p.rx;
    out[n].txInvert = p.tx;
    n++;
  };
  if (base.mode == LinkMode::RS232_LITE) {
    for (const P& p : lite) push(p);
  } else {
    for (const P& p : plain) push(p);  // P6 first for MAX3232 / TTL
    for (const P& p : lite) push(p);
  }
  return n;
}

// 3 s of silent listening before transmitting on a profile. Returns true if another master is talking.
bool Prober::listenForForeign(ProbeLog& log) {
  uint8_t buf[256];
  uint32_t total = 0;
  size_t n = l_.listen(cfg::PROBE_LISTEN_MS, buf, sizeof buf, &total);
  if (total == 0) {
    log.add(l_.clock().ms(), "listen 3 s: silent");
    return false;
  }
  // frame-like ASCII: '(' followed by >= 3 printable chars and a CR/LF
  for (size_t i = 0; i + 4 < n; i++) {
    if (buf[i] != '(') continue;
    size_t j = i + 1, printable = 0;
    while (j < n && buf[j] >= 0x20 && buf[j] <= 0x7E) { j++; printable++; }
    if (printable >= 3 && j < n && (buf[j] == 0x0D || buf[j] == 0x0A)) {
      log.add(l_.clock().ms(), "listen 3 s: %lu bytes incl. a '(' frame -> FOREIGN MASTER", (unsigned long)total);
      return true;
    }
  }
  // Modbus-looking request/response with a valid CRC
  for (size_t i = 0; i + 8 <= n; i++) {
    if (buf[i + 1] == 0x03 && buf[i] >= 1 && buf[i] <= 247 && modbus::crcOk(buf + i, 8)) {
      log.add(l_.clock().ms(), "listen 3 s: Modbus frame seen -> FOREIGN MASTER");
      return true;
    }
  }
  log.add(l_.clock().ms(), "listen 3 s: %lu bytes of noise (no frames) - continuing", (unsigned long)total);
  return false;
}

ProbeOutcome Prober::full(const LinkProfile& base, ProbeLog& log) {
  ProbeOutcome o;
  o.ran = true;
  o.startedMs = l_.clock().ms();
  log.clear();
  log.setTimeBase(o.startedMs);
  if (!l_.armed()) {
    o.result.code = ProbeCode::NOT_ARMED;
    snprintf(o.summary, sizeof o.summary, "link not armed: complete the idle-level test and arm first");
    log.add(l_.clock().ms(), "%s", o.summary);
    return o;
  }
  LinkProfile list[8];
  int np = profiles(base, list, 8);
  for (int i = 0; i < np; i++) {
    LinkProfile p = list[i];
    char d[40];
    p.describe(d, sizeof d);
    l_.configure(p);
    log.add(l_.clock().ms(), "== profile P%d %s", i + 1, d);
    if (listenForForeign(log)) {
      o.foreignMaster = true;
      o.kind = AdapterKind::RAW;
      o.profile = p;
      o.result.code = ProbeCode::FOREIGN;
      snprintf(o.summary, sizeof o.summary, "another master is talking on COM (WiFi collector?) - listen-only");
      o.durationMs = l_.clock().ms() - o.startedMs;
      return o;
    }
    // D1: PI30 family
    ProbeResult pr = pi30_.probe(l_, log);
    if (pr.ok || pr.code == ProbeCode::NO_DATA) {
      o.ok = pr.ok;
      o.kind = AdapterKind::PI30;
      o.profile = p;
      o.result = pr;
      pi30_.exportState(o.state);
      snprintf(o.summary, sizeof o.summary, "%s on %s: %s", pr.ok ? "PI30" : "PI30 (implausible)", d, pr.detail);
      if (pr.ok) {
        o.durationMs = l_.clock().ms() - o.startedMs;
        log.add(l_.clock().ms(), "RESULT %s", o.summary);
        return o;
      }
      // implausible data: keep looking at other profiles but remember this one
      continue;
    }
    if (pr.code == ProbeCode::LEGACY_HASH) {
      ProbeResult q = q6_.probe(l_, log);
      if (q.ok) {
        o.ok = true;
        o.kind = AdapterKind::Q6;
        o.profile = p;
        o.result = q;
        q6_.exportState(o.state);
        snprintf(o.summary, sizeof o.summary, "legacy %s on %s", q.dialect, d);
        o.durationMs = l_.clock().ms() - o.startedMs;
        log.add(l_.clock().ms(), "RESULT %s", o.summary);
        return o;
      }
    }
    // F2.3: PI18
    if (p.baud == 2400) {
      LinkResult r = l_.query("^P005PI", Framing::CRC, reply_);
      log.add(l_.clock().ms(), "^P005PI -> %s %.16s", linkResultName(r), reply_.payload);
      if ((r == LinkResult::OK || r == LinkResult::NO_CRC_OK) && !strncmp(reply_.payload, "^D00518", 7)) {
        o.pi18 = true;
        o.kind = AdapterKind::RAW;
        o.profile = p;
        o.result.code = ProbeCode::OK;
        snprintf(o.result.family, sizeof o.result.family, "pi18");
        snprintf(o.summary, sizeof o.summary, "PI18 detected on %s (no decoder in v1): raw capture only", d);
        o.durationMs = l_.clock().ms() - o.startedMs;
        log.add(l_.clock().ms(), "RESULT %s", o.summary);
        return o;
      }
      // F2.4: legacy Q1/Q6 without CRC
      ProbeResult q = q6_.probe(l_, log);
      if (q.ok) {
        o.ok = true;
        o.kind = AdapterKind::Q6;
        o.profile = p;
        o.result = q;
        q6_.exportState(o.state);
        snprintf(o.summary, sizeof o.summary, "legacy %s on %s", q.dialect, d);
        o.durationMs = l_.clock().ms() - o.startedMs;
        log.add(l_.clock().ms(), "RESULT %s", o.summary);
        return o;
      }
    }
  }
  if (o.kind == AdapterKind::PI30) {  // only implausible PI30 data was found: report it, do not run
    o.durationMs = l_.clock().ms() - o.startedMs;
    log.add(l_.clock().ms(), "RESULT %s", o.summary);
    return o;
  }
  // F3: Modbus last resort (FC 03 only), each preceded by 3 s of listening
  struct MB { ModbusAdapter* a; uint32_t baud; };
  MB mbs[] = {{&powmr_, 2400}, {&smg_, 9600}};
  for (const MB& m : mbs) {
    LinkProfile p = list[0];  // same polarity as the primary profile
    p.baud = m.baud;
    l_.configure(p);
    char d[40];
    p.describe(d, sizeof d);
    log.add(l_.clock().ms(), "== Modbus %s on %s", m.a->id(), d);
    if (listenForForeign(log)) {
      o.foreignMaster = true;
      o.kind = AdapterKind::RAW;
      o.profile = p;
      o.result.code = ProbeCode::FOREIGN;
      snprintf(o.summary, sizeof o.summary, "another master is talking on COM - listen-only");
      o.durationMs = l_.clock().ms() - o.startedMs;
      return o;
    }
    ProbeResult pr = m.a->probe(l_, log);
    if (pr.ok) {
      o.ok = true;
      o.kind = m.a == &smg_ ? AdapterKind::SMG : AdapterKind::POWMR;
      o.profile = p;
      o.result = pr;
      m.a->exportState(o.state);
      snprintf(o.summary, sizeof o.summary, "%s on %s", pr.detail, d);
      o.durationMs = l_.clock().ms() - o.startedMs;
      log.add(l_.clock().ms(), "RESULT %s", o.summary);
      return o;
    }
  }
  o.kind = AdapterKind::RAW;
  o.profile = list[0];
  o.result.code = ProbeCode::SILENCE;
  snprintf(o.summary, sizeof o.summary, "no protocol found: listen-only, re-probe in 6 h (check wiring/polarity)");
  o.durationMs = l_.clock().ms() - o.startedMs;
  log.add(l_.clock().ms(), "RESULT %s", o.summary);
  l_.configure(list[0]);
  return o;
}
