// test_outage - 1.0.2 outage log: why the logger was offline (net/OutageLog). Gap detection, Wi-Fi / cloud / heap
// details, carrying an open gap over a reboot (RTC record), power cuts, crumbs and the event text limit.
#include <string.h>
#include <unity.h>

#include <string>

#include "agg/AlertEngine.h"
#include "net/OutageLog.h"
#include "net/UplinkJson.h"

using namespace outage;

void setUp() {}
void tearDown() {}

static const uint32_t T_1200 = 1792648800;  // 2026-10-22 12:00:00 +06

static bool has(const char* s, const char* sub) { return strstr(s, sub) != nullptr; }

// a fresh boot whose first cloud success opens the first gap
static void freshBoot(OutageLog& lg, Rec& r, uint32_t ms, uint32_t ep) {
  memset(&r, 0, sizeof r);
  lg.boot(ms, "POWERON", false, false, 0, "", 10002);
  Report rep;
  lg.onWifi(ms, true, 0, -60);
  lg.onCloudOk(ms, ep, rep);
}

void test_short_gap_is_not_reported_long_one_is() {
  Rec r;
  OutageLog lg(r);
  freshBoot(lg, r, 1000, T_1200);
  Report rep;
  // 2 min without a cloud success (a token refresh, a TLS reconnect): normal
  lg.onFail("live: TLS -0x7F00 out of memory", true);
  TEST_ASSERT_FALSE(lg.onCloudOk(121000, T_1200 + 120, rep));
  TEST_ASSERT_EQUAL_UINT32(T_1200 + 120, r.sinceEp);  // the gap restarted
  TEST_ASSERT_EQUAL(0, r.fails);
  // 23 min: reported, starting at the last success
  lg.onFail("live: connect failed", false);
  lg.onFail("status: connect failed", false);
  TEST_ASSERT_TRUE(lg.onCloudOk(121000 + 23 * 60000, T_1200 + 120 + 23 * 60, rep));
  TEST_ASSERT_EQUAL_UINT32(T_1200 + 120, rep.startEp);
  TEST_ASSERT_EQUAL_UINT32(23 * 60, rep.durS);
  TEST_ASSERT_TRUE(has(rep.msg, "Offline 23 min from 12:02"));
  TEST_ASSERT_TRUE(has(rep.msg, "Wi-Fi ok, -60 dBm"));
  TEST_ASSERT_TRUE(has(rep.msg, "Cloud 2 fails: live: connect failed"));  // the first error of the gap
  TEST_ASSERT_FALSE(has(rep.msg, "no memory"));
  TEST_ASSERT_FALSE(has(rep.msg, "Reboot"));
}

void test_wifi_drop_reason_signal_and_down_time() {
  Rec r;
  OutageLog lg(r);
  freshBoot(lg, r, 1000, T_1200);
  uint32_t ms = 1000;
  for (int i = 0; i < 10; i++) lg.onWifi(ms += 1000, true, 0, -71);  // weak but up
  lg.onWifi(ms += 1000, false, 8, 0);                                  // dropped: signal before the drop is kept
  for (int i = 0; i < 600; i++) lg.onWifi(ms += 1000, false, 201, 0);  // 10 min, the AP is not found
  for (int i = 0; i < 30; i++) lg.onWifi(ms += 1000, true, 201, -55);  // back, stronger
  lg.onHeap(60000, 40000, 3000);
  lg.onHeap(18500, 12400, 2100);
  lg.onHeap(70000, 50000, 2500);
  lg.onStep(S_MAIL, 4000);
  lg.onStep(S_AUTH, 31000);
  lg.onStep(S_LIVE, 2000);
  lg.onTlsDrop();
  Report rep;
  TEST_ASSERT_TRUE(lg.onCloudOk(ms, T_1200 + (ms - 1000) / 1000, rep));
  TEST_ASSERT_EQUAL(1, r.drops == 0);  // restarted
  TEST_ASSERT_TRUE(has(rep.msg, "Offline 11 min from 12:00"));
  TEST_ASSERT_TRUE(has(rep.msg, "Wi-Fi down 10 min, 1 drop, NO_AP_FOUND, -71 dBm"));
  TEST_ASSERT_TRUE(has(rep.msg, "TLS reset x1"));
  TEST_ASSERT_TRUE(has(rep.msg, "Heap min 18/12 KB, stack 2100 B"));
  TEST_ASSERT_TRUE(has(rep.msg, "Slowest sign-in 31 s"));
  TEST_ASSERT_TRUE(strlen(rep.msg) < sizeof rep.msg);
  // an unknown reason code is shown as a number
  TEST_ASSERT_NULL(wifiReasonName(77));
  TEST_ASSERT_EQUAL_STRING("AUTH_FAIL", wifiReasonName(202));
}

void test_gap_survives_a_watchdog_reboot() {
  Rec r;  // RTC memory: the same bytes before and after the reset
  {
    OutageLog lg(r);
    freshBoot(lg, r, 1000, T_1200);
    lg.onFail("cfg: HTTP 503", false);
    lg.onWifi(5000, true, 0, -64);
  }
  // network watchdog restart (SW) 61 min later: millis() starts again
  OutageLog lg2(r);
  lg2.boot(800, "SW", false, false, T_1200 + 3000, "up 1h01m", 10002);
  TEST_ASSERT_TRUE(recValid(r));
  TEST_ASSERT_EQUAL_UINT32(T_1200, r.sinceEp);  // the gap goes on from the last success of the old boot
  TEST_ASSERT_EQUAL(1, r.reboots);
  Report rep;
  TEST_ASSERT_TRUE(lg2.onCloudOk(20000, T_1200 + 61 * 60 + 20, rep));
  TEST_ASSERT_TRUE(has(rep.msg, "Offline 61 min from 12:00. Reboot SW (up 1h01m)"));
  TEST_ASSERT_TRUE(has(rep.msg, "Cloud 1 fail: cfg: HTTP 503"));
  TEST_ASSERT_EQUAL(0, r.reboots);  // the next gap starts clean
  TEST_ASSERT_EQUAL_STRING("", r.rr);
}

void test_planned_restart_is_silent_crash_always_reported() {
  Rec r;
  {
    OutageLog lg(r);
    freshBoot(lg, r, 1000, T_1200);
  }
  // an update or a requested restart: back in 15 s, nothing to report
  OutageLog lg2(r);
  lg2.boot(500, "SW", false, false, 0, "up 0h05m", 10002);
  Report rep;
  TEST_ASSERT_FALSE(lg2.onCloudOk(15000, T_1200 + 15, rep));
  // a task watchdog: reported however short, with what the old boot was doing
  OutageLog lg3(r);
  lg3.boot(500, "TASK_WDT", true, false, 0, "up 3h12m; uplink in mail 31s", 10002);
  TEST_ASSERT_TRUE(lg3.onCloudOk(12000, T_1200 + 40, rep));
  TEST_ASSERT_TRUE(has(rep.msg, "Offline 25 s from 12:00. Reboot TASK_WDT (up 3h12m; uplink in mail 31s)"));
  TEST_ASSERT_EQUAL_UINT32(T_1200 + 15, rep.startEp);
}

void test_crash_loop_counts_reboots() {
  Rec r;
  {
    OutageLog lg(r);
    freshBoot(lg, r, 1000, T_1200);
  }
  for (int i = 0; i < 3; i++) {
    OutageLog lg(r);
    lg.boot(500, "PANIC", true, false, 0, "up 0h00m", 10002);
  }
  OutageLog lg(r);
  Report rep;
  TEST_ASSERT_TRUE(lg.onCloudOk(30000, T_1200 + 300, rep));
  TEST_ASSERT_TRUE(has(rep.msg, "Reboot PANIC x3"));
}

void test_new_image_or_lost_record_never_invents_a_gap() {
  Rec r;
  memset(&r, 0xA5, sizeof r);  // the previous image left other data there
  OutageLog lg(r);
  lg.boot(500, "SW", false, false, T_1200 - 4 * 60, "", 10002);  // first boot of an update: the checkpoint is NOT used
  TEST_ASSERT_TRUE(recValid(r));
  TEST_ASSERT_EQUAL_UINT32(0, r.sinceEp);
  TEST_ASSERT_EQUAL(0, r.reboots);
  Report rep;
  TEST_ASSERT_FALSE(lg.onCloudOk(20000, T_1200, rep));  // 19.5 s since this boot
  // a corrupted record is not trusted
  r.fails = 9;
  TEST_ASSERT_FALSE(recValid(r));
  recSeal(r);
  TEST_ASSERT_TRUE(recValid(r));
  r.magic = REC_MAGIC + 1;  // an older layout
  recSeal(r);
  TEST_ASSERT_FALSE(recValid(r));
}

// a record left by another image (an update, or a rollback to this one) is not carried over: no stale gap
void test_record_of_another_image_starts_afresh() {
  Rec r;
  {
    OutageLog lg(r);
    freshBoot(lg, r, 1000, T_1200);
  }
  TEST_ASSERT_EQUAL_UINT32(10002, r.fw);
  OutageLog other(r);
  other.boot(500, "SW", false, false, 0, "up 0h20m", 10003);  // rolled back 12 min later
  TEST_ASSERT_EQUAL_UINT32(0, r.sinceEp);
  TEST_ASSERT_EQUAL(0, r.reboots);
  Report rep;
  TEST_ASSERT_FALSE(other.onCloudOk(15000, T_1200 + 12 * 60, rep));
}

void test_power_cut_uses_the_checkpoint() {
  Rec r;
  memset(&r, 0, sizeof r);  // RTC memory lost
  OutageLog lg(r);
  lg.boot(500, "POWERON", false, true, T_1200, "", 10002);
  Report rep;
  TEST_ASSERT_TRUE(lg.onCloudOk(25000, T_1200 + 40 * 60, rep));
  TEST_ASSERT_TRUE(has(rep.msg, "Offline ~40 min from ~12:00 (power cut?). Reboot POWERON"));
  // a brown-out whose record was lost: approximate, but not called a power cut
  memset(&r, 0, sizeof r);
  OutageLog lg2(r);
  lg2.boot(500, "BROWNOUT", true, false, T_1200, "", 10002);
  TEST_ASSERT_TRUE(lg2.onCloudOk(25000, T_1200 + 60, rep));
  TEST_ASSERT_TRUE(has(rep.msg, "Offline ~60 s from ~12:00. Reboot BROWNOUT"));
  TEST_ASSERT_FALSE(has(rep.msg, "power cut"));
  // a power cut with no checkpoint: starts at this boot, nothing to report
  memset(&r, 0, sizeof r);
  OutageLog lg3(r);
  lg3.boot(500, "POWERON", false, true, 0, "", 10002);
  TEST_ASSERT_FALSE(lg3.onCloudOk(25000, T_1200, rep));
}

void test_no_clock() {
  Rec r;
  memset(&r, 0, sizeof r);
  OutageLog lg(r);
  lg.boot(500, "POWERON", false, true, 0, "", 10002);
  Report rep;
  // never had a clock in this boot: the length comes from millis()
  TEST_ASSERT_TRUE(lg.onCloudOk(500 + 5 * 60000, 0, rep));
  TEST_ASSERT_EQUAL_UINT32(0, rep.startEp);  // the uplink uses the time of the write
  TEST_ASSERT_TRUE(has(rep.msg, "Offline 5 min. Wi-Fi ok"));
  // a reboot inside a gap that never had a clock: the length is unknown, reported only for a crash
  OutageLog lg2(r);
  lg2.boot(500, "SW", false, false, 0, "", 10002);
  TEST_ASSERT_FALSE(lg2.onCloudOk(9000, 0, rep));
  OutageLog lg3(r);
  lg3.boot(500, "INT_WDT", true, false, 0, "up 0h01m", 10002);
  TEST_ASSERT_TRUE(lg3.onCloudOk(9000, 0, rep));
  TEST_ASSERT_TRUE(has(rep.msg, "Offline (length unknown). Reboot INT_WDT (up 0h01m)"));
}

void test_message_fits_the_rules_limit() {
  Rec r;
  memset(&r, 0, sizeof r);
  r.magic = REC_MAGIC;
  r.sinceEp = T_1200;
  r.approx = 1;
  r.reboots = 200;
  snprintf(r.rr, sizeof r.rr, "TASK_WDT");
  memset(r.crumb, 'c', sizeof r.crumb - 1);
  r.drops = 65535;
  r.wifiDownS = 999999;
  r.reason = 250;
  r.rssi = -128;
  r.fails = 65535;
  r.oom = 65535;
  memset(r.err, 'e', sizeof r.err - 1);
  r.tlsDrops = 255;
  r.minHeap = 4000000000u;
  r.minBlock = 4000000000u;
  r.slowMs = 4000000000u;
  r.minStack = 4000000000u;
  r.slowStep = S_TG_PAIR;
  char out[200];
  formatReport(r, 4000000000u, true, out, sizeof out);
  TEST_ASSERT_TRUE(strlen(out) <= 199);  // events/<d>/<id>/msg: <= 200 chars
  TEST_ASSERT_TRUE(has(out, "Offline ~"));
  // whole parts only: no part is cut in the middle
  std::string s(out);
  TEST_ASSERT_TRUE(s.back() != ',' && s.back() != ' ');
  // a tiny buffer never overflows
  char tiny[8];
  formatReport(r, 60, true, tiny, sizeof tiny);
  TEST_ASSERT_TRUE(strlen(tiny) < sizeof tiny);
}

void test_crumbs_text() {
  Crumbs c;
  crumbsReset(c);
  char t[64];
  crumbsText(c, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("", t);  // nothing ran
  c.ms[T_LOOP] = 3 * 3600000 + 12 * 60000;
  c.ms[T_INV] = c.ms[T_LOOP] - 40000;   // stalled 40 s
  c.ms[T_AGG] = c.ms[T_LOOP] - 500;
  c.ms[T_UPLINK] = c.ms[T_LOOP] - 35000;
  c.step = S_MAIL;
  c.stepMs = c.ms[T_LOOP] - 31000;
  crumbsText(c, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("up 3h12m; uplink in mail 31s; inv stalled 40s", t);  // uplink not listed twice
  c.step = S_IDLE;
  crumbsText(c, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("up 3h12m; inv stalled 40s; uplink stalled 35s", t);
  char small[12];
  crumbsText(c, small, sizeof small);
  TEST_ASSERT_TRUE(strlen(small) < sizeof small);
  c.magic = 0;  // power-on garbage
  crumbsText(c, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("", t);
  crumbsReset(c);
  c.step = 200;
  TEST_ASSERT_FALSE(crumbsValid(c));
}

// the event goes through the existing events/<date> path: key, fields and limits of firebase/database.rules.json
void test_event_follows_the_events_rules() {
  Report rep;
  rep.startEp = T_1200 + 120;
  rep.durS = 23 * 60;
  memset(rep.msg, 'x', sizeof rep.msg - 1);
  rep.msg[sizeof rep.msg - 1] = 0;
  AlertEvent e;
  toEvent(rep, T_1200 + 1500, 99, e);
  TEST_ASSERT_EQUAL(0, e.pushPrio);  // log only: no ntfy, no Telegram, no e-mail
  TEST_ASSERT_FALSE(e.telegram);
  TEST_ASSERT_EQUAL_STRING("", e.em);
  TEST_ASSERT_TRUE(upj::wantsRtdbWrite(e));
  char ch[20];
  upj::channels(e, true, true, ch, sizeof ch);
  TEST_ASSERT_EQUAL_STRING("log", ch);
  char body[1024];
  bool mq = true;
  TEST_ASSERT_TRUE(upj::eventPatch(e, ch, body, sizeof body, &mq) > 0);
  TEST_ASSERT_FALSE(mq);
  TEST_ASSERT_TRUE(has(body, "{\"events/2026-10-22/1792648920-LOGGER_OFFLINE-i\":{\"ts\":1792648920,\"type\":\"LOGGER_OFFLINE\",\"sev\":1,\"st\":\"info\",\"msg\":\"xxx"));
  TEST_ASSERT_TRUE(has(body, "\",\"ch\":\"log\",\"dur\":1380}}"));
  TEST_ASSERT_TRUE(strlen(e.type) <= 24 && strlen(e.msg) <= 200 && strlen(e.st) <= 8 && e.sev <= 5);
  // no start time (no clock yet): the time of the write, or 0 for the uplink's fixTimestamps()
  rep.startEp = 0;
  toEvent(rep, T_1200, 99, e);
  TEST_ASSERT_EQUAL_UINT32(T_1200, e.ts);
  toEvent(rep, 0, 99, e);
  TEST_ASSERT_EQUAL_UINT32(0, e.ts);
  TEST_ASSERT_EQUAL_UINT32(99, e.upMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_short_gap_is_not_reported_long_one_is);
  RUN_TEST(test_wifi_drop_reason_signal_and_down_time);
  RUN_TEST(test_gap_survives_a_watchdog_reboot);
  RUN_TEST(test_planned_restart_is_silent_crash_always_reported);
  RUN_TEST(test_crash_loop_counts_reboots);
  RUN_TEST(test_new_image_or_lost_record_never_invents_a_gap);
  RUN_TEST(test_record_of_another_image_starts_afresh);
  RUN_TEST(test_power_cut_uses_the_checkpoint);
  RUN_TEST(test_no_clock);
  RUN_TEST(test_message_fits_the_rules_limit);
  RUN_TEST(test_crumbs_text);
  RUN_TEST(test_event_follows_the_events_rules);
  return UNITY_END();
}
