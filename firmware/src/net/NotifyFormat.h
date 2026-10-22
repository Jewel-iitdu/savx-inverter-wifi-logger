// NotifyFormat.h - push message bodies for ntfy and Telegram (d1 §7.1, §7.3). Host-portable.
//   ntfy:     JSON publish, POST https://ntfy.sh/ {"topic","title","message","priority","tags","click"}
//             (docs.ntfy.sh/publish/#publish-as-json - avoids non-ASCII header encoding for °C and Bangla names)
//   Telegram: POST https://api.telegram.org/bot<token>/sendMessage {"chat_id","text","disable_web_page_preview"}
// Delayed delivery adds the original local time, e.g. "(14:02, delayed 37 min) " (d1 §7.3).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "agg/AlertEngine.h"

namespace notifyfmt {

const char* typeLabel(const char* type);   // mirrors shared/codes.json eventTypes (+ RULE, RULE_INVALID)
// "(14:02, delayed 37 min) " when the event is > 120 s old, else ""
void delayedPrefix(uint32_t eventTs, uint32_t nowEpoch, char* out, size_t n);
size_t ntfyJson(const AlertEvent& e, const char* topic, const char* clickUrl, uint32_t nowEpoch, char* out, size_t n);
// bangla = true (owner request 2026-10-22, NVS alert/tgLang "bn", the default): Bangla title, duration and local time,
// Bangla digits, then the logger's English detail line (it carries the measured values).
size_t telegramJson(const AlertEvent& e, const char* chatId, uint32_t nowEpoch, bool bangla, char* out, size_t n);
const char* typeLabelBn(const char* type);
void bnDigits(const char* in, char* out, size_t n);  // ASCII 0-9 -> Bangla ০-৯ (UTF-8), other bytes unchanged
// 12-hour Bangla clock with the part of the day (owner request): 0-3 রাত, 4-5 ভোর, 6-11 সকাল, 12-14 দুপুর,
// 15-17 বিকাল, 18-19 সন্ধ্যা, 20-23 রাত; e.g. 07:11 -> "সকাল ৭:১১", 00:30 -> "রাত ১২:৩০", 13:05 -> "দুপুর ১:০৫"
void bnClock(unsigned hour, unsigned minute, char* out, size_t n);
// sendMessage body confirming that the logger paired with this chat (Telegram auto-pairing, 0.7.7)
size_t tgWelcomeJson(const char* chatId, bool bangla, char* out, size_t n);
// Plain-text e-mail: subject "[Solar] <label>: <message>" and a short body with the local time and the dashboard link.
void emailText(const AlertEvent& e, uint32_t nowEpoch, const char* appUrl, char* subject, size_t sn, char* body, size_t bn);

}  // namespace notifyfmt
