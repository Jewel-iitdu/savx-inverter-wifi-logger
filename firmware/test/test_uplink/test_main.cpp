// test_uplink - FW-2 cloud uplink building blocks: HTTP response parser, RTDB paths/keys/bodies (d1 §4.1-4.2,
// r8 §3), back-fill cursor logic (no gaps, no duplicates), quota budget, back-off, network watchdog, push bodies.
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/TimeUtil.h"
#include "net/Backfill.h"
#include "net/Budget.h"
#include "net/HttpParse.h"
#include "net/NotifyFormat.h"
#include "net/UplinkJson.h"

void setUp() {}
void tearDown() {}

static const uint32_t T_1435 = 1792571700;  // 2026-10-21 14:35:00 +06
static const uint32_t MIDNIGHT = 1792519200;
static const int32_t DAY0 = 20747;

// ============================================================================================ HTTP parser
static HttpParse::R feedAll(HttpParse& p, const std::string& s, bool byteWise) {
  HttpParse::R r = HttpParse::R::MORE;
  if (!byteWise) return p.feed((const uint8_t*)s.data(), s.size());
  for (size_t i = 0; i < s.size(); i++) {
    r = p.feed((const uint8_t*)s.data() + i, 1);
    if (r != HttpParse::R::MORE) return r;
  }
  return r;
}

void test_http_content_length_and_204() {
  char body[64];
  for (int bw = 0; bw < 2; bw++) {
    HttpParse p;
    p.reset(body, sizeof body);
    std::string r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\ncontent-length: 13\r\n\r\n{\"liveSec\":10}";
    TEST_ASSERT_EQUAL((int)HttpParse::R::DONE, (int)feedAll(p, r, bw));
    TEST_ASSERT_EQUAL(200, p.status());
    TEST_ASSERT_EQUAL_STRING("{\"liveSec\":10", body);  // exactly Content-Length bytes
    TEST_ASSERT_TRUE(p.keepAlive());
  }
  HttpParse p;
  p.reset(body, sizeof body);
  TEST_ASSERT_EQUAL((int)HttpParse::R::DONE,
                    (int)feedAll(p, "HTTP/1.1 204 No Content\r\nConnection: keep-alive\r\n\r\n", false));
  TEST_ASSERT_EQUAL(204, p.status());
  TEST_ASSERT_EQUAL(0, (int)p.bodyLen());
  p.reset(body, sizeof body);
  TEST_ASSERT_EQUAL((int)HttpParse::R::DONE,
                    (int)feedAll(p, "HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}", false));
  TEST_ASSERT_EQUAL(401, p.status());
  TEST_ASSERT_FALSE(p.keepAlive());
}

void test_http_chunked_split_and_trailer() {
  char body[64];
  std::string r =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n7;ext=1\r\n{\"idTok\r\n6\r\nen\":1}\r\n0\r\nX-Trailer: a\r\n\r\n";
  for (int bw = 0; bw < 2; bw++) {
    HttpParse p;
    p.reset(body, sizeof body);
    TEST_ASSERT_EQUAL((int)HttpParse::R::DONE, (int)feedAll(p, r, bw));
    TEST_ASSERT_EQUAL_STRING("{\"idToken\":1}", body);
    TEST_ASSERT_TRUE(p.chunked());
  }
}

void test_http_truncation_close_delimited_headers_only_and_errors() {
  char small[8];
  HttpParse p;
  p.reset(small, sizeof small);
  TEST_ASSERT_EQUAL((int)HttpParse::R::DONE,
                    (int)feedAll(p, "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n01234567890123456789", false));
  TEST_ASSERT_EQUAL_STRING("0123456", small);
  TEST_ASSERT_TRUE(p.truncated());
  TEST_ASSERT_EQUAL(20, (int)p.bodyTotal());
  // HTTP/1.0 style: body until close
  char body[64];
  p.reset(body, sizeof body);
  TEST_ASSERT_EQUAL((int)HttpParse::R::MORE, (int)feedAll(p, "HTTP/1.1 200 OK\r\n\r\nhello", false));
  TEST_ASSERT_EQUAL((int)HttpParse::R::DONE, (int)p.finishOnClose());
  TEST_ASSERT_EQUAL_STRING("hello", body);
  // the mailer nudge: 302, headers only, Location captured, body never read
  p.reset(body, sizeof body, true);
  TEST_ASSERT_EQUAL((int)HttpParse::R::DONE,
                    (int)feedAll(p, "HTTP/1.1 302 Found\r\nLocation: https://script.googleusercontent.com/macros/echo?x=1\r\n"
                                    "Content-Length: 500\r\n\r\n<html>",
                                 false));
  TEST_ASSERT_EQUAL(302, p.status());
  TEST_ASSERT_EQUAL_STRING("https://script.googleusercontent.com/macros/echo?x=1", p.location());
  TEST_ASSERT_FALSE(p.keepAlive());
  // 100 Continue is skipped
  p.reset(body, sizeof body);
  TEST_ASSERT_EQUAL((int)HttpParse::R::DONE,
                    (int)feedAll(p, "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", true));
  TEST_ASSERT_EQUAL(200, p.status());
  TEST_ASSERT_EQUAL_STRING("ok", body);
  // garbage and premature close
  p.reset(body, sizeof body);
  TEST_ASSERT_EQUAL((int)HttpParse::R::ERROR, (int)feedAll(p, "SSH-2.0-OpenSSH\r\n", false));
  p.reset(body, sizeof body);
  feedAll(p, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", false);
  TEST_ASSERT_EQUAL((int)HttpParse::R::ERROR, (int)p.finishOnClose());
}

// ============================================================================================ JSON / paths
static AlertEvent mkEvent(const char* type, const char* st, char kind, uint8_t sev, const char* em = "") {
  AlertEvent e;
  e.ts = T_1435;
  snprintf(e.type, sizeof e.type, "%s", type);
  snprintf(e.st, sizeof e.st, "%s", st);
  e.kind = kind;
  e.sev = sev;
  snprintf(e.msg, sizeof e.msg, "Grid lost, on battery (SOC 78%%)");
  snprintf(e.em, sizeof e.em, "%s", em);
  return e;
}

void test_event_key_body_and_mailq_patch() {
  AlertEvent e = mkEvent("GRID_LOSS", "open", 'o', 4, "q");
  e.pushPrio = 4;
  e.telegram = true;
  char k[48];
  upj::eventKey(e, k, sizeof k);
  TEST_ASSERT_EQUAL_STRING("1792571700-GRID_LOSS-o", k);
  char ch[20];
  upj::channels(e, true, false, ch, sizeof ch);
  TEST_ASSERT_EQUAL_STRING("ntfy,email", ch);
  upj::channels(e, true, true, ch, sizeof ch);
  TEST_ASSERT_EQUAL_STRING("ntfy,tg,email", ch);
  char body[1024];
  bool mq = false;
  size_t n = upj::eventPatch(e, "ntfy,email", body, sizeof body, &mq);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(mq);
  TEST_ASSERT_EQUAL_STRING(
      "{\"events/2026-10-21/1792571700-GRID_LOSS-o\":{\"ts\":1792571700,\"type\":\"GRID_LOSS\",\"sev\":4,\"st\":\"open\","
      "\"msg\":\"Grid lost, on battery (SOC 78%)\",\"ch\":\"ntfy,email\",\"em\":\"q\"},"
      "\"mailq/1792571700-GRID_LOSS-o\":{\"d\":\"2026-10-21\",\"ts\":1792571700,\"sev\":4}}",
      body);
  // a rule event: rid as the key source, val/thr, rc copied into the job
  AlertEvent r = mkEvent("RULE", "open", 'o', 3, "q");
  snprintf(r.rid, sizeof r.rid, "r03");
  r.val = 5010;
  r.thr = 5000;
  r.rc = 1;
  snprintf(r.msg, sizeof r.msg, "Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)");
  upj::eventPatch(r, "email", body, sizeof body, &mq);
  TEST_ASSERT_NOT_NULL(strstr(body, "\"events/2026-10-21/1792571700-r03-o\":{\"ts\":1792571700,\"type\":\"RULE\",\"sev\":3,"
                                    "\"st\":\"open\",\"rid\":\"r03\",\"val\":5010,\"thr\":5000,"));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"mailq/1792571700-r03-o\":{\"d\":\"2026-10-21\",\"ts\":1792571700,\"sev\":3,\"rc\":1}"));
  // clear: dur always present; no em -> no outbox entry
  AlertEvent c = mkEvent("GRID_RESTORED", "clear", 'c', 3);
  c.dur = 8040;
  upj::eventPatch(c, "log", body, sizeof body, &mq);
  TEST_ASSERT_FALSE(mq);
  TEST_ASSERT_NULL(strstr(body, "mailq"));
  TEST_ASSERT_NOT_NULL(strstr(body, "\"dur\":8040"));
  // repeats are only written when they carry em:"q"
  AlertEvent rep = mkEvent("BATT_LOW", "open", 'r', 4);
  rep.repeat = true;
  TEST_ASSERT_FALSE(upj::wantsRtdbWrite(rep));
  snprintf(rep.em, sizeof rep.em, "q");
  TEST_ASSERT_TRUE(upj::wantsRtdbWrite(rep));
  // no clock yet: nothing can be written (the uplink fixes ts from the time anchor first)
  AlertEvent z = mkEvent("TEST_ALERT", "info", 'i', 3);
  z.ts = 0;
  TEST_ASSERT_EQUAL(0, (int)upj::eventPatch(z, "log", body, sizeof body));
  // the key matches the mailq rule pattern ^[0-9]{10}-[A-Za-z0-9_]{1,24}-[ocri]$ even for odd types
  AlertEvent odd = mkEvent("WEIRD/TYPE.X", "info", '?', 2);
  upj::eventKey(odd, k, sizeof k);
  TEST_ASSERT_EQUAL_STRING("1792571700-WEIRD_TYPE_X-i", k);
}

void test_alertstate_and_status_json_follow_the_rules() {
  AlertStateOut o;
  snprintf(o.rid, sizeof o.rid, "r01");
  o.st = rules::St::FIRE;
  o.since = 1792560123;
  o.val = 2710;
  o.last = 1792560125;
  o.cnt = 1;
  char b[512];
  upj::alertStateJson(o, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("{\"st\":\"fire\",\"since\":1792560123,\"val\":2710,\"last\":1792560125,\"cnt\":1}", b);
  o.st = rules::St::LATCH;
  o.pk = 20261021;
  upj::alertStateJson(o, b, sizeof b);
  TEST_ASSERT_NOT_NULL(strstr(b, "\"pk\":20261021"));
  o.removed = true;
  upj::alertStateJson(o, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("null", b);

  upj::StatusDoc s;
  s.ts = T_1435;
  s.fw = "0.6.0";
  snprintf(s.ip, sizeof s.ip, "<logger-ip>");
  s.rssi = -61;
  s.heap = 182340;
  s.psram = 8123456;
  s.up = 86400;
  s.rr = "POWERON";
  s.q = 0;
  snprintf(s.proto, sizeof s.proto, "pi30-2400");
  snprintf(s.dia, sizeof s.dia, "REVO");
  s.hs = 12;
  s.rq = 1450;
  s.lok = T_1435 - 5;
  s.cok = T_1435;
  s.arv = 1792560000000.0;
  s.mjq = 2;
  s.nrul = 3;
  snprintf(s.ruleErr, sizeof s.ruleErr, "r04: unknown metric 'xyz'");
  upj::statusJson(s, b, sizeof b);
  TEST_ASSERT_NOT_NULL(strstr(b, "\"arv\":1792560000000,\"acat\":1,\"mjq\":2,\"nrul\":3"));
  // every status key must satisfy the rules: key <= 8 chars, number or string <= 40
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, b));
  int keys = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    keys++;
    TEST_ASSERT_TRUE_MESSAGE(strlen(kv.key().c_str()) <= 8, kv.key().c_str());
    bool okv = kv.value().is<double>() || (kv.value().is<const char*>() && strlen(kv.value().as<const char*>()) <= 40);
    TEST_ASSERT_TRUE_MESSAGE(okv, kv.key().c_str());
  }
  TEST_ASSERT_TRUE(keys >= 20);
}

void test_paths_hosts_and_urls() {
  char b[256];
  upj::rtdbPath("live", "TOK", true, nullptr, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("/devices/inv1/live.json?auth=TOK&print=silent", b);
  upj::rtdbPath("", "TOK", true, nullptr, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("/devices/inv1.json?auth=TOK&print=silent", b);
  upj::rtdbPath("hist", "TOK", false, "shallow=true", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("/devices/inv1/hist.json?auth=TOK&shallow=true", b);
  char h[96];
  TEST_ASSERT_TRUE(upj::dbHostFromUrl("https://savx-1234-default-rtdb.asia-southeast1.firebasedatabase.app/", h, sizeof h));
  TEST_ASSERT_EQUAL_STRING("savx-1234-default-rtdb.asia-southeast1.firebasedatabase.app", h);
  TEST_ASSERT_TRUE(upj::dbHostFromUrl("https://Savx.firebaseio.com", h, sizeof h));
  TEST_ASSERT_EQUAL_STRING("savx.firebaseio.com", h);
  TEST_ASSERT_FALSE(upj::dbHostFromUrl("http://savx.firebaseio.com", h, sizeof h));
  TEST_ASSERT_FALSE(upj::dbHostFromUrl("https://evil.example.com/x.firebaseio.com", h, sizeof h));
  TEST_ASSERT_FALSE(upj::dbHostFromUrl("https://firebasedatabase.app.evil.com", h, sizeof h));
  upj::urlEncode("AMf-vB/x+y=", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("AMf-vB%2Fx%2By%3D", b);
  upj::urlEncode("\"$key\"", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("%22%24key%22", b);
  TEST_ASSERT_TRUE(upj::hcPath("0b9a7c1e-3f2d-4c5b-8a6e-1d2f3a4b5c6d", false, b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("/0b9a7c1e-3f2d-4c5b-8a6e-1d2f3a4b5c6d", b);
  TEST_ASSERT_TRUE(upj::hcPath("https://hc-ping.com/0b9a7c1e-3f2d-4c5b-8a6e-1d2f3a4b5c6d/", true, b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("/0b9a7c1e-3f2d-4c5b-8a6e-1d2f3a4b5c6d/fail", b);
  TEST_ASSERT_TRUE(upj::hcPath("https://hc-ping.com/PINGKEY123/savx-logger", false, b, sizeof b));
  TEST_ASSERT_FALSE(upj::hcPath("https://example.com/ping", false, b, sizeof b));
  TEST_ASSERT_FALSE(upj::hcPath("not-a-uuid", false, b, sizeof b));
  TEST_ASSERT_TRUE(upj::nudgePath("https://script.google.com/macros/s/AKfycbx/exec?k=0123456789abcdef01234567", b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("/macros/s/AKfycbx/exec?k=0123456789abcdef01234567", b);
  TEST_ASSERT_FALSE(upj::nudgePath("https://script.google.com/macros/s/AKfycbx/exec", b, sizeof b));  // no key
  TEST_ASSERT_FALSE(upj::nudgePath("https://evil.com/macros/s/x/exec?k=1", b, sizeof b));
}

// 0.9.0: a change of only the planned grid-off window (or the meter switch) must count as a change. Before, cfgEq
// ignored gridOffFrom/To, so a new window in the app was applied only after the next reboot.
void test_cfg_eq_sees_grid_off_and_meter() {
  AlertCfg a, b;
  TEST_ASSERT_TRUE(upj::cfgEq(a, b));
  b.gridOffFrom = 480;
  TEST_ASSERT_FALSE(upj::cfgEq(a, b));
  b = a;
  b.gridOffTo = 930;
  TEST_ASSERT_FALSE(upj::cfgEq(a, b));
  b = a;
  b.meter = 0;
  TEST_ASSERT_FALSE(upj::cfgEq(a, b));
  upj::CfgDoc c;
  TEST_ASSERT_TRUE(upj::parseCfg("{\"meter\":0}", 11, c));
  TEST_ASSERT_EQUAL(0, c.c.meter);
  TEST_ASSERT_TRUE(upj::parseCfg("{\"meter\":7}", 11, c));
  TEST_ASSERT_EQUAL(1, c.c.meter);  // out of range: default (on)
  TEST_ASSERT_TRUE(upj::parseCfg("{}", 2, c));
  TEST_ASSERT_EQUAL(1, c.c.meter);
}

void test_parse_cfg_auth_shallow_daily() {
  upj::CfgDoc c;
  const char* cfgJson = "{\"liveSec\":30,\"socLow\":25,\"bvLow\":19,\"quietFrom\":23,\"summaryHour\":-1,"
                        "\"testAlert\":1792571700123,\"alertsRev\":1792560000000,\"keepHistDays\":400}";
  TEST_ASSERT_TRUE(upj::parseCfg(cfgJson, strlen(cfgJson), c));
  TEST_ASSERT_EQUAL(30, c.c.liveSec);
  TEST_ASSERT_EQUAL_FLOAT(25, c.c.socLow);
  TEST_ASSERT_EQUAL_FLOAT(22.5f, c.c.bvLow);  // 19 is outside 20..30: default kept
  TEST_ASSERT_EQUAL(23, c.c.quietFrom);
  TEST_ASSERT_EQUAL(-1, c.c.summaryHour);
  TEST_ASSERT_TRUE(c.c.testAlert != 0);
  TEST_ASSERT_TRUE(c.hasRev);
  TEST_ASSERT_EQUAL_DOUBLE(1792560000000.0, c.alertsRev);
  TEST_ASSERT_TRUE(upj::parseCfg("{\"liveSec\":2}", 13, c));
  TEST_ASSERT_EQUAL(2, c.c.liveSec);  // 0.7.2: 2 s allowed
  TEST_ASSERT_TRUE(upj::parseCfg("{\"liveSec\":1}", 13, c));
  TEST_ASSERT_EQUAL(10, c.c.liveSec);  // below LIVE_SEC_MIN: default kept
  TEST_ASSERT_TRUE(upj::parseCfg("null", 4, c));
  TEST_ASSERT_EQUAL(10, c.c.liveSec);
  TEST_ASSERT_FALSE(c.hasRev);
  TEST_ASSERT_FALSE(upj::parseCfg("{bad", 4, c));

  static upj::AuthResult a;
  const char* si = "{\"kind\":\"identitytoolkit#VerifyPasswordResponse\",\"localId\":\"UID123\",\"email\":\"d@x.invalid\","
                   "\"idToken\":\"eyJ.abc.def\",\"registered\":true,\"refreshToken\":\"AMf-vBx\",\"expiresIn\":\"3600\"}";
  TEST_ASSERT_TRUE(upj::parseSignIn(si, strlen(si), a));
  TEST_ASSERT_EQUAL_STRING("eyJ.abc.def", a.idToken);
  TEST_ASSERT_EQUAL_STRING("AMf-vBx", a.refreshToken);
  TEST_ASSERT_EQUAL_STRING("UID123", a.uid);
  TEST_ASSERT_EQUAL_UINT32(3600, a.expiresIn);
  const char* rf = "{\"access_token\":\"eyJ.x\",\"expires_in\":\"3600\",\"token_type\":\"Bearer\",\"refresh_token\":\"AMf-2\","
                   "\"id_token\":\"eyJ.new\",\"user_id\":\"UID123\",\"project_id\":\"123\"}";
  TEST_ASSERT_TRUE(upj::parseRefresh(rf, strlen(rf), a));
  TEST_ASSERT_EQUAL_STRING("eyJ.new", a.idToken);
  TEST_ASSERT_EQUAL_STRING("AMf-2", a.refreshToken);
  char m[64];
  const char* er = "{\"error\":{\"code\":400,\"message\":\"INVALID_LOGIN_CREDENTIALS\",\"errors\":[]}}";
  TEST_ASSERT_TRUE(upj::parseApiError(er, strlen(er), m, sizeof m));
  TEST_ASSERT_EQUAL_STRING("INVALID_LOGIN_CREDENTIALS", m);
  TEST_ASSERT_TRUE(upj::parseApiError("{\"error\":\"Permission denied\"}", 29, m, sizeof m));
  TEST_ASSERT_EQUAL_STRING("Permission denied", m);

  int32_t days[8];
  const char* sh = "{\"2025-09-01\":true,\"2026-10-21\":true,\"junk\":true}";
  TEST_ASSERT_EQUAL(2, upj::parseShallowDates(sh, strlen(sh), days, 8));
  TEST_ASSERT_EQUAL(DAY0, days[1]);
  TEST_ASSERT_EQUAL(0, upj::parseShallowDates("null", 4, days, 8));

  mrec::Daily d[4];
  const char* dr = "{\"2026-10-01\":{\"epv\":12840,\"eld\":9710,\"egr\":1220,\"ech\":4100,\"edis\":3050,\"gout\":42,\"n\":1440},"
                   "\"2026-10-02\":{\"egr\":95.5,\"n\":700}}";
  TEST_ASSERT_EQUAL(2, upj::parseDailyRange(dr, strlen(dr), d, 4));
  TEST_ASSERT_EQUAL_FLOAT(1220, (float)d[0].egr);
  TEST_ASSERT_EQUAL(42, d[0].gout);
  TEST_ASSERT_EQUAL(1440, d[0].n);
  TEST_ASSERT_EQUAL_FLOAT(95.5f, (float)d[1].egr);
  TEST_ASSERT_EQUAL(tu::daysFromCivil(2026, 10, 2), d[1].day);
}

// ============================================================================================ back-fill
struct FakeStore : IMinuteStore {
  std::map<int32_t, std::vector<MinuteRec>> files;
  std::map<int32_t, uint32_t> cur;
  int listDays(int32_t* days, int max) override {
    int n = 0;
    for (auto& kv : files)
      if (n < max) days[n++] = kv.first;
    return n;
  }
  uint32_t count(int32_t day) override { return files.count(day) ? (uint32_t)files[day].size() : 0; }
  uint32_t cursor(int32_t day) override { return cur.count(day) ? cur[day] : 0; }
  bool setCursor(int32_t day, uint32_t n) override {
    cur[day] = n;
    return true;
  }
  int read(int32_t day, uint32_t from, MinuteRec* out, int max) override {
    if (!files.count(day)) return 0;
    auto& v = files[day];
    int n = 0;
    for (uint32_t i = from; i < v.size() && n < max; i++) out[n++] = v[i];
    return n;
  }
};

static MinuteRec rec(uint32_t ts, int w, bool prov = false) {
  MinuteRec r = mrec::empty();
  r.ts = ts;
  r.n = 12;
  r.w = (int16_t)w;
  r.pvw = 100;
  r.eld = 80;  // 8.0 Wh
  r.flags = prov ? mrec::F_PROVISIONAL : 0;
  mrec::seal(r);
  return r;
}

static void fillDay(FakeStore& st, int32_t day, int fromMin, int count) {
  uint32_t mid = MIDNIGHT + (uint32_t)(day - DAY0) * 86400u;
  for (int i = 0; i < count; i++) st.files[day].push_back(rec(mid + (uint32_t)(fromMin + i) * 60u, 100 + i));
}

static std::vector<std::string> keysOf(const char* body) {
  std::vector<std::string> out;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return out;
  for (JsonPair kv : doc.as<JsonObject>()) out.push_back(kv.key().c_str());
  return out;
}

void test_backfill_next_live_vs_backlog_and_commit_guard() {
  FakeStore st;
  fillDay(st, DAY0 - 1, 0, 1440);
  fillDay(st, DAY0, 0, 3);
  Batch b;
  TEST_ASSERT_TRUE(backfill::next(st, DAY0, b, 60, 2));
  TEST_ASSERT_EQUAL(DAY0 - 1, b.day);  // oldest day first
  TEST_ASSERT_FALSE(b.live);
  TEST_ASSERT_EQUAL(60, b.n);
  TEST_ASSERT_EQUAL_UINT32(1443, b.pendingTotal);
  st.cur[DAY0 - 1] = 1440;
  st.cur[DAY0] = 1;
  TEST_ASSERT_TRUE(backfill::next(st, DAY0, b, 60, 2));
  TEST_ASSERT_TRUE(b.live);  // today, 2 pending
  TEST_ASSERT_EQUAL(2, b.n);
  st.cur[DAY0] = 0;  // 3 pending today: a normal batch
  TEST_ASSERT_TRUE(backfill::next(st, DAY0, b, 60, 2));
  TEST_ASSERT_FALSE(b.live);
  // commit only advances when nobody moved the cursor meanwhile
  TEST_ASSERT_TRUE(backfill::commit(st, b));
  TEST_ASSERT_EQUAL_UINT32(3, st.cursor(DAY0));
  TEST_ASSERT_FALSE(backfill::commit(st, b));
  TEST_ASSERT_EQUAL_UINT32(3, st.cursor(DAY0));
  TEST_ASSERT_FALSE(backfill::next(st, DAY0, b, 60, 2));
}

// An outage of two days plus random upload failures: every record ends up in RTDB exactly under its key, every
// cursor reaches the end, and no record is committed twice.
void test_backfill_no_gaps_no_dupes_with_failures() {
  FakeStore st;
  fillDay(st, DAY0 - 2, 600, 700);
  fillDay(st, DAY0 - 1, 0, 1440);
  fillDay(st, DAY0, 0, 875);
  std::map<std::string, std::string> rtdb;
  static char body[32768];
  uint32_t committed = 0;
  srand(1234);
  int guard = 0;
  Batch b;
  while (backfill::next(st, DAY0, b, 60, 2) && guard++ < 10000) {
    std::vector<MinuteRec> recs(b.n);
    TEST_ASSERT_EQUAL(b.n, st.read(b.day, b.from, recs.data(), b.n));
    upj::Patch p(body, sizeof body);
    backfill::addRecords(st, b, recs.data(), DAY0, p);
    TEST_ASSERT_TRUE(p.close() > 0);
    int fate = rand() % 10;
    if (fate == 0) continue;  // the request failed: nothing written, cursor unchanged
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, body));
    for (JsonPair kv : doc.as<JsonObject>()) {
      std::string v;
      serializeJson(kv.value(), v);
      rtdb[kv.key().c_str()] = v;
    }
    if (fate == 1) continue;  // written, but the 204 was lost: the same keys are sent again
    TEST_ASSERT_TRUE(backfill::commit(st, b));
    committed += b.n;
  }
  TEST_ASSERT_EQUAL_UINT32(700 + 1440 + 875, committed);
  int hist = 0, h15 = 0;
  for (auto& kv : rtdb) {
    if (kv.first.rfind("hist/", 0) == 0) hist++;
    if (kv.first.rfind("h15/", 0) == 0) h15++;
  }
  TEST_ASSERT_EQUAL(700 + 1440 + 875, hist);
  TEST_ASSERT_EQUAL(1, (int)rtdb.count("hist/2026-10-19/1000"));
  TEST_ASSERT_EQUAL(1, (int)rtdb.count("hist/2026-10-21/1434"));
  // h15: past days complete (700 min = 46.67 quarters -> 47; 96), today 875 min = 58 full quarters + 1 open
  TEST_ASSERT_EQUAL(47 + 96 + 58, h15);
  TEST_ASSERT_EQUAL(0, (int)rtdb.count("h15/2026-10-21/1430"));  // still open (14:30..14:34 only)
  TEST_ASSERT_EQUAL(1, (int)rtdb.count("h15/2026-10-21/1415"));
  for (auto& kv : st.files) TEST_ASSERT_EQUAL_UINT32(kv.second.size(), st.cursor(kv.first));
}

void test_backfill_h15_closure_dedupe_and_daily_fields() {
  FakeStore st;
  fillDay(st, DAY0, 14 * 60, 14);  // 14:00..14:13
  st.cur[DAY0] = 14;
  st.files[DAY0].push_back(rec(MIDNIGHT + (14 * 60 + 14) * 60, 500));  // 14:14 closes the 14:00 quarter
  Batch b;
  TEST_ASSERT_TRUE(backfill::next(st, DAY0, b, 60, 2));
  TEST_ASSERT_TRUE(b.live);
  static char body[8192];
  std::vector<MinuteRec> recs(b.n);
  st.read(b.day, b.from, recs.data(), b.n);
  upj::Patch p(body, sizeof body);
  TEST_ASSERT_EQUAL(1, backfill::addRecords(st, b, recs.data(), DAY0, p));
  p.close();
  std::vector<std::string> k = keysOf(body);
  TEST_ASSERT_EQUAL(2, (int)k.size());
  TEST_ASSERT_EQUAL_STRING("hist/2026-10-21/1414", k[0].c_str());
  TEST_ASSERT_EQUAL_STRING("h15/2026-10-21/1400", k[1].c_str());
  JsonDocument doc;
  deserializeJson(doc, body);
  TEST_ASSERT_EQUAL(15 * 12, doc["h15/2026-10-21/1400"]["n"].as<int>());
  TEST_ASSERT_EQUAL_FLOAT(15 * 8.0f, doc["h15/2026-10-21/1400"]["eld"].as<float>());
  backfill::commit(st, b);
  // the next minute is missing (link off), 14:17 arrives: the previous quarter is (re)written, the new one is open
  st.files[DAY0].push_back(rec(MIDNIGHT + (14 * 60 + 17) * 60, 600));
  TEST_ASSERT_TRUE(backfill::next(st, DAY0, b, 60, 2));
  recs.resize(b.n);
  st.read(b.day, b.from, recs.data(), b.n);
  upj::Patch p2(body, sizeof body);
  backfill::addRecords(st, b, recs.data(), DAY0, p2);
  p2.close();
  k = keysOf(body);
  TEST_ASSERT_EQUAL(2, (int)k.size());
  TEST_ASSERT_EQUAL_STRING("hist/2026-10-21/1417", k[0].c_str());
  TEST_ASSERT_EQUAL_STRING("h15/2026-10-21/1400", k[1].c_str());
  backfill::commit(st, b);
  // an estimated (provisional) record for a minute that also exists with a real timestamp: the real one wins
  FakeStore s2;
  s2.files[DAY0].push_back(rec(T_1435, 111));
  s2.files[DAY0].push_back(rec(T_1435, 999, true));
  TEST_ASSERT_TRUE(backfill::next(s2, DAY0 + 1, b, 60, 2));
  recs.resize(b.n);
  s2.read(b.day, b.from, recs.data(), b.n);
  upj::Patch p3(body, sizeof body);
  TEST_ASSERT_EQUAL(1, backfill::addRecords(s2, b, recs.data(), DAY0 + 1, p3));
  p3.close();
  deserializeJson(doc, body);
  TEST_ASSERT_EQUAL(111, doc["hist/2026-10-21/1435"]["w"].as<int>());
  TEST_ASSERT_TRUE(doc["hist/2026-10-21/1435"]["tq"].isNull());
  // day totals of a past day: per field, never touching nf / nw / epvi
  upj::Patch p4(body, sizeof body);
  TEST_ASSERT_TRUE(backfill::addDailyFields(st, DAY0, p4));
  p4.close();
  deserializeJson(doc, body);
  TEST_ASSERT_EQUAL(16, doc["daily/2026-10-21/n"].as<int>());
  TEST_ASSERT_EQUAL_FLOAT(16 * 8.0f, doc["daily/2026-10-21/eld"].as<float>());
  TEST_ASSERT_TRUE(doc["daily/2026-10-21/nf"].isNull());
  TEST_ASSERT_TRUE(doc["daily/2026-10-21/epvi"].isNull());
  TEST_ASSERT_EQUAL_UINT32(0, backfill::oldestPendingTs(st));   // everything committed
  TEST_ASSERT_EQUAL_UINT32(T_1435, backfill::oldestPendingTs(s2));  // BACKLOG_STUCK input
}

void test_patch_builder_overflow() {
  char b[32];
  upj::Patch p(b, sizeof b);
  p.add("a", "1");
  p.add("bb", "{\"x\":2}");
  TEST_ASSERT_TRUE(p.ok());
  p.add("this/path/is/far/too/long", "123456");
  TEST_ASSERT_FALSE(p.ok());
  TEST_ASSERT_EQUAL(0, (int)p.close());
  upj::Patch q(b, sizeof b);
  q.add("a", "1");
  q.close();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", b);
}

// ============================================================================================ budget / watchdog
void test_budget_levels_and_backoff() {
  Budget g;
  g.roll(DAY0);
  TEST_ASSERT_EQUAL_UINT32(10000, g.liveMs(10));
  TEST_ASSERT_EQUAL_UINT32(2000, g.liveMs(2));
  TEST_ASSERT_EQUAL_UINT32(2000, g.liveMs(1));  // clamped to LIVE_SEC_MIN
  for (int i = 0; i < 1501; i++) g.onHandshake();
  TEST_ASSERT_EQUAL(1, g.level());
  TEST_ASSERT_EQUAL_UINT32(60000, g.liveMs(10));
  TEST_ASSERT_EQUAL_UINT32(120000, g.liveMs(120));
  TEST_ASSERT_FALSE(g.diagAllowed());
  TEST_ASSERT_TRUE(g.takeGuardEvent());
  TEST_ASSERT_FALSE(g.takeGuardEvent());  // once per day
  for (uint32_t i = 0; i <= cfg::BUDGET_RQ_HARD; i++) g.onRequest();
  TEST_ASSERT_EQUAL(2, g.level());
  TEST_ASSERT_EQUAL_UINT32(0, g.liveMs(10));  // live paused
  TEST_ASSERT_EQUAL_UINT32(5 * cfg::CFG_POLL_MS, g.cfgPollMs());
  g.roll(DAY0 + 1);
  TEST_ASSERT_EQUAL(0, g.level());
  TEST_ASSERT_TRUE(g.diagAllowed());

  Backoff bo;
  uint32_t t = 1000;
  TEST_ASSERT_TRUE(bo.ready(t));
  const uint32_t want[] = {5000, 10000, 20000, 40000, 80000, 160000, 300000, 300000};
  for (uint32_t w : want) {
    bo.fail(t);
    TEST_ASSERT_EQUAL_UINT32(w, bo.lastDelayMs());
    TEST_ASSERT_FALSE(bo.ready(t + w - 1));
    TEST_ASSERT_TRUE(bo.ready(t + w));
    t += w;
  }
  bo.ok();
  TEST_ASSERT_TRUE(bo.ready(t));
  bo.fail(t);
  TEST_ASSERT_EQUAL_UINT32(5000, bo.lastDelayMs());
}

void test_network_watchdog() {
  NetWatchdog w;
  uint32_t t = 0, ep = T_1435;
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::NONE, (int)w.step(t, true, true, ep, 0));
  t += 15 * 60000;
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::DROP_TLS, (int)w.step(t, true, true, ep, 0));
  t += 1000;
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::NONE, (int)w.step(t, true, true, ep, 0));  // only once
  // 60 min without success but no TCP/TLS connect worked (ISP down): no reboot, just another TLS drop
  t += 45 * 60000;
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::DROP_TLS, (int)w.step(t, true, true, ep, 0));
  // connects work but requests keep failing for an hour: reboot, unless one happened < 6 h ago
  w.onConnectOk(t);
  t += 45 * 60000;
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::DROP_TLS, (int)w.step(t, true, true, ep, ep - 3600));
  w.onConnectOk(t);
  t += 45 * 60000;
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::RESTART, (int)w.step(t, true, true, ep, ep - 7 * 3600));
  // Wi-Fi down resets the timer
  NetWatchdog w2;
  w2.step(0, true, true, ep, 0);
  w2.step(50 * 60000, false, true, ep, 0);
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::NONE, (int)w2.step(60 * 60000, true, true, ep, 0));
  w2.onCloudOk(60 * 60000);
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::NONE, (int)w2.step(70 * 60000, true, true, ep, 0));
}

// one auth failure a minute for an hour; counts the watchdog's TLS drops and the minute of its first reboot (-1: none)
static void authFailHour(bool credentialsHeld, bool outOfMemory, int& drops, int& restartAt) {
  NetWatchdog w;
  uint32_t ep = T_1435;
  w.step(0, true, true, ep, 0);
  drops = 0;
  restartAt = -1;
  for (int m = 1; m <= 60; m++) {
    uint32_t t = (uint32_t)m * 60000u;
    w.onAuthFail(t, credentialsHeld, outOfMemory);
    NetWatchdog::Action a = w.step(t, true, true, ep, ep - 7 * 3600);
    if (a == NetWatchdog::Action::DROP_TLS) drops++;
    if (a == NetWatchdog::Action::RESTART && restartAt < 0) restartAt = m;
  }
}

// 2026-10-24 10:18 on 0.9.7: the hourly token refresh failed with "SSL - Memory allocation failed" (the RTDB keep-alive
// session held the heap) and stayed stuck, because every auth failure told the watchdog "cloud fine".
void test_network_watchdog_auth_failures() {
  TEST_ASSERT_TRUE(tlsOutOfMemory(-0x7F00));             // SSL_ALLOC_FAILED, the code seen that day
  TEST_ASSERT_TRUE(tlsOutOfMemory(-(0x2880 + 0x006A)));  // X509 + ASN1 alloc, composite code
  TEST_ASSERT_TRUE(tlsOutOfMemory(-0x0010));             // MPI alloc
  TEST_ASSERT_TRUE(tlsOutOfMemory(-0x1180));             // PEM alloc (the root certificate is parsed at connect)
  TEST_ASSERT_FALSE(tlsOutOfMemory(0));
  TEST_ASSERT_FALSE(tlsOutOfMemory(-1));       // socket error from the Arduino layer
  TEST_ASSERT_FALSE(tlsOutOfMemory(-0x2700));  // X509 certificate verify failed
  TEST_ASSERT_FALSE(tlsOutOfMemory(-0x7780));  // SSL fatal alert
  TEST_ASSERT_FALSE(tlsOutOfMemory(-0x6800));  // SSL timeout
  TEST_ASSERT_FALSE(tlsOutOfMemory(-0x0050));  // NET connection reset
  int drops, restartAt;
  // out of TLS heap: drop TLS at 15 min (frees the RTDB session), reboot at 60 min (clears the heap)
  authFailHour(false, true, drops, restartAt);
  TEST_ASSERT_EQUAL(1, drops);
  TEST_ASSERT_EQUAL(60, restartAt);
  // other failures without a working connect (DNS, timeouts): drops only, as for an ISP outage
  authFailHour(false, false, drops, restartAt);
  TEST_ASSERT_EQUAL(2, drops);
  TEST_ASSERT_EQUAL(-1, restartAt);
  // wrong credentials wait for the owner: no drops, no reboot loop
  authFailHour(true, false, drops, restartAt);
  TEST_ASSERT_EQUAL(0, drops);
  TEST_ASSERT_EQUAL(-1, restartAt);
}

// 0.9.9: Wi-Fi up but no connect ever works (wedged IP stack or DNS). Before, the watchdog only dropped TLS every
// 45 min, for good; now it reboots after 6 h of silence, at most once per 6 h.
void test_network_watchdog_long_silence() {
  uint32_t ep = T_1435;
  NetWatchdog w;
  w.step(0, true, true, ep, 0);
  int restartAt = -1;
  for (int m = 1; m <= 7 * 60 && restartAt < 0; m++)
    if (w.step((uint32_t)m * 60000u, true, true, ep + m * 60, 0) == NetWatchdog::Action::RESTART) restartAt = m;
  TEST_ASSERT_EQUAL(6 * 60, restartAt);
  // a watchdog reboot 2 h ago: wait for the 6-h gap
  NetWatchdog w2;
  w2.step(0, true, true, ep, 0);
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::DROP_TLS, (int)w2.step(6 * 3600000u, true, true, ep, ep - 2 * 3600));
  TEST_ASSERT_EQUAL((int)NetWatchdog::Action::RESTART, (int)w2.step(10 * 3600000u, true, true, ep + 4 * 3600, ep - 2 * 3600));
  // a cloud success or Wi-Fi down restarts the 6 h
  NetWatchdog w3;
  w3.step(0, true, true, ep, 0);
  w3.onCloudOk(5 * 3600000u);
  TEST_ASSERT_NOT_EQUAL((int)NetWatchdog::Action::RESTART, (int)w3.step(6 * 3600000u, true, true, ep, 0));
  w3.step(7 * 3600000u, false, true, ep, 0);
  TEST_ASSERT_NOT_EQUAL((int)NetWatchdog::Action::RESTART, (int)w3.step(12 * 3600000u, true, true, ep, 0));
}

// counts a Wi-Fi watchdog's reconnects and the minute of its first reboot (-1: none) over `minutes`, one step a minute
static void wifiDown(WifiWatchdog& w, uint32_t t0, int minutes, bool portal, int& kicks, int& restartAt) {
  kicks = 0;
  restartAt = -1;
  for (int m = 1; m <= minutes && restartAt < 0; m++) {
    WifiWatchdog::Action a = w.step(t0 + (uint32_t)m * 60000u, false, portal);
    if (a == WifiWatchdog::Action::RECONNECT) kicks++;
    if (a == WifiWatchdog::Action::RESTART) restartAt = m;
  }
}

void test_wifi_watchdog() {
  int kicks, restartAt;
  // up, then lost (router reboot, AUTH_FAIL): a reconnect every 2 min, reboot after 30 min
  WifiWatchdog w;
  w.step(0, true, false);
  wifiDown(w, 0, 60, false, kicks, restartAt);
  TEST_ASSERT_EQUAL(14, kicks);  // minutes 2, 4 ... 28
  TEST_ASSERT_EQUAL(30, restartAt);
  // back within 10 min: no reboot, the timer starts again
  WifiWatchdog w2;
  w2.step(0, true, false);
  wifiDown(w2, 0, 10, false, kicks, restartAt);
  TEST_ASSERT_EQUAL(5, kicks);
  TEST_ASSERT_EQUAL((int)WifiWatchdog::Action::NONE, (int)w2.step(11 * 60000u, true, false));
  wifiDown(w2, 11 * 60000u, 29, false, kicks, restartAt);
  TEST_ASSERT_EQUAL(-1, restartAt);
  // never up in this boot (router off, password changed): reconnects, reboot only after 6 h
  WifiWatchdog w3;
  w3.step(0, false, false);
  wifiDown(w3, 0, 7 * 60, false, kicks, restartAt);
  TEST_ASSERT_EQUAL(6 * 60, restartAt);
  TEST_ASSERT_EQUAL(179, kicks);
  // the setup portal owns the radio
  WifiWatchdog w4;
  w4.step(0, true, false);
  wifiDown(w4, 0, 60, true, kicks, restartAt);
  TEST_ASSERT_EQUAL(0, kicks);
  TEST_ASSERT_EQUAL(-1, restartAt);
}

// 1.0.0: 2026-10-24 20:47 a Telegram alert came late and every e-mail failed after 3 tries in 3 min. Network, TLS and
// server errors now retry for about 6 h; a refusal (wrong token, chat or password) still stops after 3 tries.
void test_alert_retry_policy() {
  uint32_t total = 0;
  int tries = 0;
  for (uint8_t f = 1; f < 30; f++) {
    uint32_t w = alertRetryMs(f, false);
    if (!w) break;
    total += w;
    tries = f;
  }
  TEST_ASSERT_EQUAL(12, tries);                          // 13 attempts in all
  TEST_ASSERT_EQUAL_UINT32(20010000u, total);            // 5 h 34 min
  TEST_ASSERT_EQUAL_UINT32(30000u, alertRetryMs(1, false));
  TEST_ASSERT_TRUE(alertRetryMs(3, false) > 0);          // where 0.9.9 gave up
  TEST_ASSERT_EQUAL_UINT32(0, alertRetryMs(3, true));    // refused: 3 tries
  TEST_ASSERT_EQUAL_UINT32(60000u, alertRetryMs(2, true));
  TEST_ASSERT_EQUAL_UINT32(0, alertRetryMs(0, false));
  // what counts as refused
  TEST_ASSERT_TRUE(alertRefused(401, false));   // Telegram: bad token
  TEST_ASSERT_TRUE(alertRefused(400, false));   // chat not found
  TEST_ASSERT_FALSE(alertRefused(429, false));  // too many requests: retry
  TEST_ASSERT_FALSE(alertRefused(502, false));
  TEST_ASSERT_FALSE(alertRefused(-1, false));   // connect/TLS
  TEST_ASSERT_TRUE(alertRefused(535, true));    // SMTP: wrong password
  TEST_ASSERT_FALSE(alertRefused(451, true));   // SMTP: try later
  TEST_ASSERT_FALSE(alertRefused(0, true));     // SMTP: no reply / connect failed
  // the error text for the cloud status (<= 40 chars)
  char e[41];
  alertErrText(-0x7F00, "connect api.telegram.org: SSL - Memory allocation failed", e, sizeof e);
  TEST_ASSERT_EQUAL_STRING("TLS -0x7F00 out of memory", e);
  alertErrText(-0x2700, "connect x: X509 - Certificate verification failed", e, sizeof e);
  TEST_ASSERT_EQUAL_STRING("TLS -0x2700", e);
  alertErrText(0, "HTTP 401", e, sizeof e);
  TEST_ASSERT_EQUAL_STRING("HTTP 401", e);
  alertErrText(0, "connect/TLS to mail.example-company.com:465 failed: SSL - Conn reset", e, sizeof e);
  TEST_ASSERT_EQUAL(40, (int)strlen(e));
}

// 1.0.0 status fields: chip temperature, alert delivery. Every field at its longest still fits the uplink's 1 KB
// buffer and the rules (key <= 8 chars, number or string <= 40).
void test_status_json_worst_case() {
  upj::StatusDoc s;
  s.ts = 4000000000u;
  s.fw = "10.10.10";
  snprintf(s.ip, sizeof s.ip, "255.255.255.255");
  s.rssi = -100;
  s.heap = s.psram = s.up = 4000000000u;
  s.rr = "BROWNOUT";
  s.q = 4000000;
  snprintf(s.proto, sizeof s.proto, "%s", "pi30-115200-xxxxxxxxxxx");
  snprintf(s.dia, sizeof s.dia, "%s", "REVOXXXXX");
  s.hs = s.rq = s.lok = s.cok = 4000000000u;
  s.arv = 1792560000000.0;
  s.mjq = s.nrul = 99;
  s.edrop = 4000000000u;
  s.qg = 2;
  char* str[] = {s.ruleErr, s.meter, s.ota, s.repo, s.otachk, s.tgErr, s.mailErr};
  for (char* p : str) {
    memset(p, 'x', 40);
    p[40] = 0;
  }
  s.chipC = -12.5f;
  s.tgOn = s.mailOn = true;
  s.tgOk = s.tgFail = s.mailOk = s.mailFail = s.tgErrAt = s.mailErAt = 4000000000u;
  char b[1024];
  size_t n = upj::statusJson(s, b, sizeof b);
  TEST_ASSERT_TRUE_MESSAGE(n > 0, "status does not fit 1 KB");
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, b));
  for (JsonPair kv : doc.as<JsonObject>()) {
    TEST_ASSERT_TRUE_MESSAGE(strlen(kv.key().c_str()) <= 8, kv.key().c_str());
    bool okv = kv.value().is<double>() || (kv.value().is<const char*>() && strlen(kv.value().as<const char*>()) <= 40);
    TEST_ASSERT_TRUE_MESSAGE(okv, kv.key().c_str());
  }
  TEST_ASSERT_EQUAL_FLOAT(-12.5f, doc["chipC"].as<float>());
  TEST_ASSERT_TRUE(doc["tgErr"].is<const char*>());
  TEST_ASSERT_TRUE(doc["mailErAt"].is<double>());
  // channels that are not set up send nothing
  s.tgOn = s.mailOn = false;
  upj::statusJson(s, b, sizeof b);
  TEST_ASSERT_NULL(strstr(b, "tgOk"));
  TEST_ASSERT_NULL(strstr(b, "mailFail"));
}

// 0.9.9: a safe mode after a crash loop ends by itself after 30 min; the manual flag does not
void test_safe_mode_auto_exit() {
  TEST_ASSERT_FALSE(safeModeAutoExit(true, 29 * 60000u));
  TEST_ASSERT_TRUE(safeModeAutoExit(true, 30 * 60000u));
  TEST_ASSERT_FALSE(safeModeAutoExit(false, 10 * 3600000u));
}

// ============================================================================================ push bodies
void test_ntfy_and_telegram_bodies() {
  AlertEvent e = mkEvent("GRID_LOSS", "open", 'o', 4);
  e.pushPrio = 4;
  char b[600];
  notifyfmt::ntfyJson(e, "savx-topic-0123456789abcdef", "https://solar.example.workers.dev/#/alerts", T_1435 + 30, b,
                      sizeof b);
  TEST_ASSERT_EQUAL_STRING(
      "{\"topic\":\"savx-topic-0123456789abcdef\",\"title\":\"Solar: Grid lost\",\"message\":\"Grid lost, on battery (SOC 78%)\","
      "\"priority\":4,\"tags\":[\"warning\"],\"click\":\"https://solar.example.workers.dev/#/alerts\"}",
      b);
  // delivered 37 min late: the original local time is prefixed
  notifyfmt::ntfyJson(e, "t", "", T_1435 + 37 * 60, b, sizeof b);
  TEST_ASSERT_NOT_NULL(strstr(b, "\"message\":\"(14:35, delayed 37 min) Grid lost"));
  TEST_ASSERT_NULL(strstr(b, "click"));
  char p[48];
  notifyfmt::delayedPrefix(T_1435, T_1435 + 3 * 3600 + 5 * 60, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("(14:35, delayed 3 h 5 min) ", p);
  AlertEvent c = mkEvent("GRID_RESTORED", "clear", 'c', 3);
  snprintf(c.msg, sizeof c.msg, "Grid restored after 2h 14m");
  notifyfmt::telegramJson(c, "123456789", T_1435, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING(
      "{\"chat_id\":\"123456789\",\"text\":\"Solar: Grid restored\\u000aGrid restored after 2h 14m\","
      "\"disable_web_page_preview\":true}",
      b);
  TEST_ASSERT_EQUAL_STRING("Rule alert", notifyfmt::typeLabel("RULE"));
  TEST_ASSERT_EQUAL_STRING("SOMETHING_NEW", notifyfmt::typeLabel("SOMETHING_NEW"));
}

// 0.7.7: Bangla Telegram text (owner request), Bangla digits, and auto-pairing from getUpdates.
void test_telegram_bangla_and_pairing() {
  static char b[1400];
  char d[64];
  notifyfmt::bnDigits("14:35 & 2026", d, sizeof d);
  TEST_ASSERT_EQUAL_STRING("১৪:৩৫ & ২০২৬", d);
  struct { unsigned h, m; const char* want; } clk[] = {
      {0, 30, "রাত ১২:৩০"}, {3, 59, "রাত ৩:৫৯"},   {4, 0, "ভোর ৪:০০"},     {5, 10, "ভোর ৫:১০"},
      {7, 11, "সকাল ৭:১১"}, {11, 59, "সকাল ১১:৫৯"}, {12, 15, "দুপুর ১২:১৫"}, {13, 5, "দুপুর ১:০৫"},
      {16, 0, "বিকাল ৪:০০"}, {18, 45, "সন্ধ্যা ৬:৪৫"}, {20, 0, "রাত ৮:০০"},   {23, 59, "রাত ১১:৫৯"}};
  for (auto& k : clk) {
    notifyfmt::bnClock(k.h, k.m, d, sizeof d);
    TEST_ASSERT_EQUAL_STRING(k.want, d);
  }
  AlertEvent e = mkEvent("GRID_LOSS", "open", 'o', 4);
  e.ts = T_1435;
  snprintf(e.msg, sizeof e.msg, "Grid lost, on battery (SOC 78%%)");
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(e, "42", T_1435, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "বিদ্যুৎ চলে গেছে"));
  TEST_ASSERT_NOT_NULL(strstr(b, "সময়: দুপুর ২:৩৫"));  // 14:35, 12-hour with the part of the day
  TEST_ASSERT_NOT_NULL(strstr(b, "Grid lost, on battery (SOC 78%)"));  // the measured detail stays
  AlertEvent c = mkEvent("GRID_RESTORED", "clear", 'c', 3);
  c.ts = T_1435;
  c.dur = 43 * 60;
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(c, "42", T_1435 + 5 * 60, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "বিদ্যুৎ ফিরে এসেছে"));
  TEST_ASSERT_NOT_NULL(strstr(b, "বিদ্যুৎ ছিল না: ৪৩ মিনিট"));
  TEST_ASSERT_NOT_NULL(strstr(b, "৫ মিনিট দেরিতে"));
  TEST_ASSERT_NULL(strstr(b, "ঠিক হয়েছে"));  // GRID_RESTORED has its own title
  TEST_ASSERT_NULL(strstr(b, "পরিকল্পিত"));
  e.planned = c.planned = true;  // 0.8.1: the owner's planned grid-off is pushed, marked in the title
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(e, "42", T_1435, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "বিদ্যুৎ চলে গেছে (পরিকল্পিত বন্ধ)"));
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(c, "42", T_1435, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "বিদ্যুৎ ফিরে এসেছে (পরিকল্পিত বন্ধ শেষ)"));
  AlertEvent l = mkEvent("BATT_LOW", "clear", 'c', 4);
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(l, "42", 0, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "ব্যাটারি কম — ঠিক হয়েছে"));
  TEST_ASSERT_TRUE(notifyfmt::tgWelcomeJson("42", true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "যুক্ত হয়েছে"));
  // 1.0.0: the daily summary's body in Bangla too (2026-10-24 21:00 it came in English under the Bangla title)
  AlertEvent sum = mkEvent("DAILY_SUMMARY", "info", 'i', 1);
  sum.ts = T_1435;
  snprintf(sum.msg, sizeof sum.msg, "Today: PV 6.9 kWh, load 12.9 kWh, grid 6.1 kWh (est.), battery in 1.3 / out 1.3 kWh");
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(sum, "42", T_1435, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "আজকের সারাংশ"));
  TEST_ASSERT_NOT_NULL(strstr(b, "সোলার: ৬.৯ kWh\\u000aলোড: ১২.৯ kWh\\u000aগ্রিড: ৬.১ kWh (আনুমানিক)\\u000aব্যাটারি চার্জ / খরচ: ১.৩ / ১.৩ kWh"));
  TEST_ASSERT_NULL(strstr(b, "Today"));
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(sum, "42", T_1435, false, b, sizeof b) > 0);  // English chats unchanged
  TEST_ASSERT_NOT_NULL(strstr(b, "Today: PV 6.9 kWh"));
  snprintf(sum.msg, sizeof sum.msg, "something else");  // unknown wording: kept as it is
  TEST_ASSERT_TRUE(notifyfmt::telegramJson(sum, "42", T_1435, true, b, sizeof b) > 0);
  TEST_ASSERT_NOT_NULL(strstr(b, "something else"));

  char chats[5][24];
  long long maxId = 0;
  const char* upd =
      "{\"ok\":true,\"result\":["
      "{\"update_id\":500,\"message\":{\"chat\":{\"id\":-1001,\"type\":\"group\"},\"text\":\"hi\"}},"
      "{\"update_id\":501,\"message\":{\"chat\":{\"id\":5123456789,\"type\":\"private\"},\"text\":\"/start\"}},"
      "{\"update_id\":502,\"message\":{\"chat\":{\"id\":5123456789,\"type\":\"private\"},\"text\":\"hello\"}},"
      "{\"update_id\":503,\"message\":{\"chat\":{\"id\":-100777,\"type\":\"supergroup\"},\"text\":\"/start@your_solar_bot\"}},"
      "{\"update_id\":504,\"channel_post\":{\"chat\":{\"id\":-100999,\"type\":\"channel\"},\"text\":\"/start\"}}]}";
  int n = upj::tgPairChats(upd, strlen(upd), chats, 5, &maxId);
  TEST_ASSERT_EQUAL(2, n);                              // the chatting group and the channel are ignored, no duplicates
  TEST_ASSERT_EQUAL_STRING("5123456789", chats[0]);     // 64-bit id kept exactly
  TEST_ASSERT_EQUAL_STRING("-100777", chats[1]);        // a group that sent /start@bot
  TEST_ASSERT_EQUAL(504, (int)maxId);
  TEST_ASSERT_EQUAL(1, upj::tgPairChats(upd, strlen(upd), chats, 1, &maxId));  // capped
  const char* none = "{\"ok\":true,\"result\":[]}";
  TEST_ASSERT_EQUAL(0, upj::tgPairChats(none, strlen(none), chats, 5, &maxId));
  TEST_ASSERT_EQUAL(-1, (int)maxId);
  TEST_ASSERT_EQUAL(0, upj::tgPairChats("{bad", 4, chats, 5, &maxId));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_telegram_bangla_and_pairing);
  RUN_TEST(test_http_content_length_and_204);
  RUN_TEST(test_http_chunked_split_and_trailer);
  RUN_TEST(test_http_truncation_close_delimited_headers_only_and_errors);
  RUN_TEST(test_event_key_body_and_mailq_patch);
  RUN_TEST(test_alertstate_and_status_json_follow_the_rules);
  RUN_TEST(test_paths_hosts_and_urls);
  RUN_TEST(test_parse_cfg_auth_shallow_daily);
  RUN_TEST(test_backfill_next_live_vs_backlog_and_commit_guard);
  RUN_TEST(test_backfill_no_gaps_no_dupes_with_failures);
  RUN_TEST(test_backfill_h15_closure_dedupe_and_daily_fields);
  RUN_TEST(test_patch_builder_overflow);
  RUN_TEST(test_budget_levels_and_backoff);
  RUN_TEST(test_network_watchdog);
  RUN_TEST(test_network_watchdog_auth_failures);
  RUN_TEST(test_network_watchdog_long_silence);
  RUN_TEST(test_wifi_watchdog);
  RUN_TEST(test_safe_mode_auto_exit);
  RUN_TEST(test_alert_retry_policy);
  RUN_TEST(test_status_json_worst_case);
  RUN_TEST(test_ntfy_and_telegram_bodies);
  RUN_TEST(test_cfg_eq_sees_grid_off_and_meter);
  return UNITY_END();
}
