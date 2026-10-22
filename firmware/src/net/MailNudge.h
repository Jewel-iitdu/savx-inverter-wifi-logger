// MailNudge.h - v1.1 (optional) "nudge" of the Apps Script mailer (research/r8-email-alerts.md §1.2, §4.10).
// After a PATCH that created a `mailq` job the logger may call GET <exec-url>?k=<NUDGE_KEY> once, so the mailer polls
// now instead of within 5 min. Body-less, the 302 is NEVER followed and no body is read (arduino-esp32 3.3.12
// #8300 / getString() hang, r8c C1/M1). >= 60 s apart, <= 48 per day. Pinned GTS roots. Off when NVS mail/nudge is
// empty (the default); the URL is a secret: never logged, never echoed by the local API.
#pragma once
#include <stdint.h>

namespace mailnudge {
void reload();                                // re-read NVS mail/nudge
bool configured();
// returns true if a nudge was sent and accepted (302 or 200); false if skipped (rate limit / not set) or failed
bool maybeNudge(uint32_t nowMs, int32_t day);
uint32_t sentToday();
}  // namespace mailnudge
