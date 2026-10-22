// MailNudge.cpp - see MailNudge.h.
#include "net/MailNudge.h"

#include <Arduino.h>

#include "config.h"
#include "net/HttpsClient.h"
#include "net/UplinkJson.h"
#include "roots.h"
#include "store/Settings.h"

namespace mailnudge {

static HttpsClient s_cli(roots::GOOGLE);
static char s_path[260];
static bool s_ok = false;
static uint32_t s_last = 0, s_sent = 0;
static int32_t s_day = -1;

void reload() {
  String u = settings.getStr("mail", "nudge", "");
  u.trim();
  s_ok = u.length() && upj::nudgePath(u.c_str(), s_path, sizeof s_path);
  if (u.length() && !s_ok) Serial.println(F("[nudge] ignored: expected https://script.google.com/macros/s/<id>/exec?k=<key>"));
  s_cli.setHost("script.google.com");
}

bool configured() { return s_ok; }
uint32_t sentToday() { return s_sent; }

bool maybeNudge(uint32_t nowMs, int32_t day) {
  if (!s_ok) return false;
  if (day != s_day) {
    s_day = day;
    s_sent = 0;
  }
  if (s_sent >= cfg::NUDGE_MAX_PER_DAY) return false;
  if (s_last && (uint32_t)(nowMs - s_last) < cfg::NUDGE_MIN_GAP_MS) return false;
  s_last = nowMs;
  s_sent++;
  // headersOnly: the status line tells us doGet already ran; the connection is closed without reading the body
  HttpResult r = s_cli.request("GET", s_path, nullptr, nullptr, 0, nullptr, 0, false, true);
  return r.status == 302 || r.status == 200;
}

}  // namespace mailnudge
