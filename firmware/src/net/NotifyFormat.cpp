// NotifyFormat.cpp - see NotifyFormat.h.
#include "net/NotifyFormat.h"

#include <stdio.h>
#include <string.h>

#include "model/JsonOut.h"
#include "model/TimeUtil.h"

namespace notifyfmt {

struct Label { const char* type; const char* label; };
static const Label LABELS[] = {
    {"INVERTER_FAULT", "Inverter fault"},       {"BATT_CRITICAL", "Battery critical"},
    {"BATT_LOW", "Battery low"},                {"BATT_HIGH", "Battery voltage high"},
    {"GRID_LOSS", "Grid lost"},                 {"GRID_RESTORED", "Grid restored"},
    {"GRID_ABNORMAL", "Grid voltage abnormal"}, {"OVERLOAD", "Overload"},
    {"OVER_TEMP", "Over temperature"},          {"PV_OVERVOLT", "PV over-voltage"},
    {"PV_LOW_DAY", "Low solar today"},          {"LINK_LOST", "Inverter link lost"},
    {"LOGGER_REBOOT", "Logger rebooted"},       {"QUOTA_GUARD", "Quota guard active"},
    {"DATA_DROPPED", "Data dropped"},           {"BACKLOG_STUCK", "Upload backlog stuck"},
    {"DAILY_SUMMARY", "Daily summary"},         {"TEST_ALERT", "Test alert"},
    {"RULE", "Rule alert"},                     {"RULE_INVALID", "Rule ignored by the logger"},
};

// Bangla titles (Telegram, 0.7.7). Clears of other types get "<title> — ঠিক হয়েছে" (resolved).
struct LabelBn { const char* type; const char* icon; const char* label; };
static const LabelBn LABELS_BN[] = {
    {"INVERTER_FAULT", "\xF0\x9F\x9A\xA8", "ইনভার্টারে ফল্ট (সমস্যা)"},
    {"BATT_CRITICAL", "\xF0\x9F\x9A\xA8", "ব্যাটারি একদম কম"},
    {"BATT_LOW", "\xF0\x9F\xAA\xAB", "ব্যাটারি কম"},
    {"BATT_HIGH", "\xE2\x9A\xA0\xEF\xB8\x8F", "ব্যাটারির ভোল্টেজ বেশি"},
    {"GRID_LOSS", "\xE2\x9A\xA1", "বিদ্যুৎ চলে গেছে"},
    {"GRID_RESTORED", "\xE2\x9C\x85", "বিদ্যুৎ ফিরে এসেছে"},
    {"GRID_ABNORMAL", "\xE2\x9A\xA0\xEF\xB8\x8F", "গ্রিডের ভোল্টেজ অস্বাভাবিক"},
    {"OVERLOAD", "\xE2\x9A\xA0\xEF\xB8\x8F", "অতিরিক্ত লোড"},
    {"OVER_TEMP", "\xF0\x9F\x8C\xA1\xEF\xB8\x8F", "ইনভার্টার বেশি গরম"},
    {"PV_OVERVOLT", "\xE2\x9A\xA0\xEF\xB8\x8F", "সোলার প্যানেলের ভোল্টেজ বেশি"},
    {"PV_LOW_DAY", "\xF0\x9F\x8C\xA5\xEF\xB8\x8F", "আজ সোলার উৎপাদন কম"},
    {"LINK_LOST", "\xF0\x9F\x94\x8C", "ইনভার্টারের সাথে লগারের সংযোগ নেই"},
    {"LOGGER_REBOOT", "\xF0\x9F\x94\x84", "লগার রিস্টার্ট হয়েছে"},
    {"QUOTA_GUARD", "\xE2\x9A\xA0\xEF\xB8\x8F", "ক্লাউড ব্যবহারের সীমার কাছাকাছি"},
    {"DATA_DROPPED", "\xE2\x9A\xA0\xEF\xB8\x8F", "কিছু পুরোনো ডেটা মুছে গেছে"},
    {"BACKLOG_STUCK", "\xE2\x9A\xA0\xEF\xB8\x8F", "ডেটা আপলোড আটকে আছে"},
    {"DAILY_SUMMARY", "\xF0\x9F\x93\x8A", "আজকের সারাংশ"},
    {"TEST_ALERT", "\xF0\x9F\xA7\xAA", "পরীক্ষামূলক বার্তা"},
    {"RULE", "\xF0\x9F\x94\x94", "আপনার সেট করা সতর্কতা"},
    {"RULE_INVALID", "\xE2\x9A\xA0\xEF\xB8\x8F", "একটি সতর্কতার নিয়ম কাজ করছে না"},
};

static const LabelBn* findBn(const char* type) {
  for (const LabelBn& l : LABELS_BN)
    if (!strcmp(l.type, type)) return &l;
  return nullptr;
}

const char* typeLabelBn(const char* type) {
  const LabelBn* l = findBn(type);
  return l ? l->label : typeLabel(type);
}

void bnDigits(const char* in, char* out, size_t n) {
  if (!n) return;
  size_t k = 0;
  for (; *in && k + 1 < n; in++) {
    if (*in >= '0' && *in <= '9') {
      if (k + 3 >= n) break;
      out[k++] = (char)0xE0;
      out[k++] = (char)0xA7;
      out[k++] = (char)(0xA6 + (*in - '0'));
    } else {
      out[k++] = *in;
    }
  }
  out[k] = 0;
}

void bnClock(unsigned hour, unsigned minute, char* out, size_t n) {
  hour %= 24;
  const char* part = hour < 4 ? "রাত" : hour < 6 ? "ভোর" : hour < 12 ? "সকাল" : hour < 15 ? "দুপুর" : hour < 18 ? "বিকাল"
                     : hour < 20 ? "সন্ধ্যা" : "রাত";
  unsigned h12 = hour % 12 ? hour % 12 : 12;
  char a[48];
  snprintf(a, sizeof a, "%s %u:%02u", part, h12, minute % 60);
  bnDigits(a, out, n);
}

static void bnMinutes(uint32_t sec, char* out, size_t n) {
  uint32_t m = (sec + 59) / 60;
  char a[48];
  if (m >= 60) snprintf(a, sizeof a, "%lu ঘণ্টা %lu মিনিট", (unsigned long)(m / 60), (unsigned long)(m % 60));
  else snprintf(a, sizeof a, "%lu মিনিট", (unsigned long)m);
  bnDigits(a, out, n);
}

const char* typeLabel(const char* type) {
  for (const Label& l : LABELS)
    if (!strcmp(l.type, type)) return l.label;
  return type;
}

void delayedPrefix(uint32_t ts, uint32_t now, char* out, size_t n) {
  if (n) out[0] = 0;
  if (!tu::epochValid(ts) || !tu::epochValid(now) || now < ts + 120) return;
  char hm[8];
  tu::Local l = tu::toLocal(ts);
  snprintf(hm, sizeof hm, "%02u:%02u", l.hour, l.minute);
  uint32_t m = (now - ts) / 60;
  if (m >= 120) snprintf(out, n, "(%s, delayed %lu h %lu min) ", hm, (unsigned long)(m / 60), (unsigned long)(m % 60));
  else snprintf(out, n, "(%s, delayed %lu min) ", hm, (unsigned long)m);
}

static const char* tagFor(const AlertEvent& e) {
  if (!strcmp(e.st, "clear")) return "white_check_mark";
  if (!strcmp(e.type, "TEST_ALERT")) return "test_tube";
  if (e.sev >= 5) return "rotating_light";
  if (e.sev == 4) return "warning";
  if (e.sev == 3) return "bell";
  return "information_source";
}

static void title(const AlertEvent& e, char* out, size_t n) {
  snprintf(out, n, "Solar: %s%s", typeLabel(e.type), e.repeat ? " (still active)" : "");
}

size_t ntfyJson(const AlertEvent& e, const char* topic, const char* clickUrl, uint32_t now, char* out, size_t n) {
  char t[80], pre[48], msg[260];
  title(e, t, sizeof t);
  delayedPrefix(e.ts, now, pre, sizeof pre);
  snprintf(msg, sizeof msg, "%s%s", pre, e.msg);
  JsonOut j(out, n);
  j.open();
  j.s("topic", topic);
  j.s("title", t);
  j.s("message", msg);
  j.i("priority", e.pushPrio ? e.pushPrio : 3);
  j.openArr("tags");
  j.elStr(tagFor(e));
  j.closeArr();
  if (clickUrl && *clickUrl) j.s("click", clickUrl);
  j.close();
  return j.ok() ? j.len() : 0;
}

static void telegramTextBn(const AlertEvent& e, uint32_t now, char* text, size_t n) {
  const LabelBn* l = findBn(e.type);
  bool clear = !strcmp(e.st, "clear");
  bool ownClear = !strcmp(e.type, "GRID_RESTORED");  // a clear with its own title
  const char* icon = clear ? "\xE2\x9C\x85" : (l ? l->icon : "\xF0\x9F\x94\x94");
  const char* label = l ? l->label : typeLabel(e.type);
  const char* planned = !e.planned ? "" : clear ? " (পরিকল্পিত বন্ধ শেষ)" : " (পরিকল্পিত বন্ধ)";  // 0.8.1
  int k = snprintf(text, n, "%s %s%s%s%s", icon, label, planned, clear && !ownClear ? " — ঠিক হয়েছে" : "",
                   e.repeat ? " (এখনও চলছে)" : "");
  if (k < 0 || (size_t)k >= n) return;
  if (e.dur) {
    char d[64];
    bnMinutes(e.dur, d, sizeof d);
    int m = snprintf(text + k, n - k, "\n%s: %s", ownClear ? "বিদ্যুৎ ছিল না" : "স্থায়িত্ব", d);
    if (m > 0 && (size_t)(k + m) < n) k += m;
  }
  if (tu::epochValid(e.ts)) {
    tu::Local t = tu::toLocal(e.ts);
    char clock[64], late[96] = "";
    bnClock(t.hour, t.minute, clock, sizeof clock);
    if (tu::epochValid(now) && now >= e.ts + 120) {
      char a[64];
      snprintf(a, sizeof a, " (%lu মিনিট দেরিতে পাঠানো)", (unsigned long)((now - e.ts) / 60));
      bnDigits(a, late, sizeof late);
    }
    int m = snprintf(text + k, n - k, "\nসময়: %s%s", clock, late);
    if (m > 0 && (size_t)(k + m) < n) k += m;
  }
  if (e.msg[0] && (size_t)k < n) snprintf(text + k, n - k, "\n%s", e.msg);
}

size_t telegramJson(const AlertEvent& e, const char* chatId, uint32_t now, bool bangla, char* out, size_t n) {
  char t[80], pre[48], text[600];
  if (bangla) {
    telegramTextBn(e, now, text, sizeof text);
  } else {
    title(e, t, sizeof t);
    delayedPrefix(e.ts, now, pre, sizeof pre);
    snprintf(text, sizeof text, "%s\n%s%s", t, pre, e.msg);
  }
  JsonOut j(out, n);
  j.open();
  j.s("chat_id", chatId);
  j.s("text", text);
  j.b("disable_web_page_preview", true);
  j.close();
  return j.ok() ? j.len() : 0;
}

size_t tgWelcomeJson(const char* chatId, bool bangla, char* out, size_t n) {
  JsonOut j(out, n);
  j.open();
  j.s("chat_id", chatId);
  j.s("text", bangla ? "\xE2\x9C\x85 সোলার লগার এই চ্যাটের সাথে যুক্ত হয়েছে।\n"
                       "এখন থেকে বিদ্যুৎ চলে যাওয়া ও ফিরে আসা, ব্যাটারি কম এবং অন্যান্য সতর্কবার্তা এখানে আসবে।"
                     : "\xE2\x9C\x85 Solar logger paired with this chat. Alerts (grid lost/restored, battery low, faults) "
                       "will arrive here.");
  j.close();
  return j.ok() ? j.len() : 0;
}

static const char* sevName(uint8_t s) {
  switch (s) {
    case 5: return "Critical";
    case 4: return "Alert";
    case 3: return "Warning";
    case 2: return "Notice";
    default: return "Info";
  }
}

void emailText(const AlertEvent& e, uint32_t now, const char* appUrl, char* subject, size_t sn, char* body, size_t bn) {
  const char* label = typeLabel(e.type);
  snprintf(subject, sn, "[Solar] %s%s: %s", label, e.repeat ? " (still active)" : "", e.msg);
  char when[40] = "time unknown";
  if (tu::epochValid(e.ts)) {
    tu::Local l = tu::toLocal(e.ts);
    snprintf(when, sizeof when, "%04d-%02u-%02u %02u:%02u (Dhaka time)", l.year, l.month, l.day, l.hour, l.minute);
  }
  char pre[48];
  delayedPrefix(e.ts, now, pre, sizeof pre);
  int k = snprintf(body, bn,
                   "%s%s\n\n"
                   "Alert:    %s%s\n"
                   "Severity: %s\n"
                   "Status:   %s\n"
                   "Time:     %s\n",
                   pre, e.msg, label, e.rid[0] ? "" : "", sevName(e.sev),
                   !strcmp(e.st, "clear") ? "cleared" : !strcmp(e.st, "open") ? "active" : "information", when);
  if (k < 0 || (size_t)k >= bn) return;
  if (e.dur && (size_t)k < bn) {
    int m = snprintf(body + k, bn - k, "Duration: %lu min\n", (unsigned long)((e.dur + 59) / 60));
    if (m > 0) k += m;
  }
  if (appUrl && *appUrl && (size_t)k < bn) {
    int m = snprintf(body + k, bn - k, "\nDashboard: %s\n", appUrl);
    if (m > 0) k += m;
  }
  if ((size_t)k < bn) snprintf(body + k, bn - k, "\n-- \nSent by your Savx solar logger (ESP32).\n");
}

}  // namespace notifyfmt
