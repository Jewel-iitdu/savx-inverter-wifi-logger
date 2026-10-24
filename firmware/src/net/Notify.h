// Notify.h - alert transports (d1 §7.1): ntfy.sh primary, Telegram secondary (sev >= 4), Healthchecks.io dead-man
// ping (5 min, /fail while the inverter link is down). Implemented in src/net/Notify.cpp (FW-2).
// Rules for the implementation:
//  - runs ONLY in uplinkTask (all TLS lives there, d1 §5.3); one-shot TLS sessions with the pinned roots of
//    include/roots.h (ISRG X1/X2 + Root YR/YE for ntfy, Go Daddy G2 for Telegram, USERTrust/Sectigo for hc-ping);
//  - ntfy: JSON publish POST https://ntfy.sh/ {topic,title,message,priority = AlertEvent::pushPrio,tags,click}
//    (docs.ntfy.sh/publish/#publish-as-json - same fields as the d1 headers, but UTF-8 safe);
//  - Telegram: POST https://api.telegram.org/bot<token>/sendMessage {chat_id,text}; every pushed alert (0.7.7), Bangla text
//    by default (NVS alert/tgLang = "bn" | "en"); auto-pairing via getUpdates when alert/tgChat is empty;
//  - nothing is pushed when AlertEvent::pushPrio == 0 (quiet hours / daily cap) - the RTDB event is still written;
//  - delayed delivery prefixes the original local time, e.g. "(14:02, delayed 37 min)" (d1 §7.3);
//  - rate limits: ntfy >= 2 s apart (ntfy.sh: burst 60, then 1 per 5 s), Telegram >= 1.1 s apart (1 msg/s per chat);
//  - never log the topic, token or any URL containing secrets.
// Credentials come from NVS namespace "alert" (keys ntfyTopic, tgToken, tgChat, hcUrl, appUrl) via settings.getStr().
#pragma once
#include <stdint.h>

#include "agg/AlertEngine.h"

struct NotifyResult {
  bool ok = false;
  int httpCode = 0;
  bool rateLimited = false;  // not attempted: too soon after the previous message (retry later)
  int tls = 0;               // mbedTLS error code of a failed connect (1.0.0)
  char error[48] = "";
};

class INotifier {
 public:
  virtual ~INotifier() {}
  virtual void reload() = 0;                                                   // re-read NVS (portal / API change)
  virtual bool configured() const = 0;                                         // a channel is set up
  virtual bool ntfyOn() const = 0;
  virtual bool telegramOn() const = 0;
  virtual bool healthcheckOn() const = 0;
  virtual const char* clickUrl() const = 0;                                    // dashboard URL ("" = none)
  virtual NotifyResult ntfy(const AlertEvent& e, const char* clickUrl) = 0;    // primary
  virtual NotifyResult telegram(const AlertEvent& e) = 0;                      // every pushed alert (0.7.7)
  // Telegram auto-pairing (0.7.7): token set, no chat id yet -> poll getUpdates; the first private message (/start)
  // becomes the chat. Returns true when it just paired (chat saved to NVS, welcome message sent).
  // Up to TG_MAX_CHATS chats (NVS alert/tgChat = comma list). The first one pairs by itself; more only while a pairing
  // window is open ("Add another person" on the logger's /telegram page, 10 min).
  virtual bool telegramPairing() const = 0;
  virtual bool pollTelegramPairing() = 0;
  virtual void openTelegramPairing(uint32_t ms) = 0;
  virtual int telegramChats() const = 0;
  virtual NotifyResult healthcheck(bool fail) = 0;                             // GET <hcUrl>[/fail]
  // e-mail through the owner's own SMTP server (NVS "smtp": server = host[:465], user, pass); see src/net/Smtp.h
  virtual bool emailOn() const = 0;
  virtual NotifyResult email(const AlertEvent& e, const char* const* to, int nTo, const char* fromName) = 0;
};

// a process-lifetime instance configured from NVS
INotifier* createNotifier();
