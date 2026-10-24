// Notify.cpp - see Notify.h.
#include "net/Notify.h"

#include <Arduino.h>

#include "net/HttpsClient.h"
#include "net/NotifyFormat.h"
#include "net/Smtp.h"
#include "net/TimeSync.h"
#include "net/UplinkJson.h"
#include "roots.h"
#include "store/Settings.h"

namespace {

class Notifier : public INotifier {
 public:
  Notifier() : ntfyCli_(roots::NTFY), tgCli_(roots::TELEGRAM), hcCli_(roots::HC) {}

  void reload() override {
    topic_ = settings.getStr("alert", "ntfyTopic", "");
    tgToken_ = settings.getStr("alert", "tgToken", "");
    tgChat_ = settings.getStr("alert", "tgChat", "");
    splitChats();
    tgBn_ = settings.getStr("alert", "tgLang", "bn") != "en";
    hc_ = settings.getStr("alert", "hcUrl", "");
    app_ = settings.getStr("alert", "appUrl", "");
    topic_.trim();
    tgToken_.trim();
    tgChat_.trim();
    hc_.trim();
    app_.trim();
    char p[200];
    hcValid_ = hc_.length() && upj::hcPath(hc_.c_str(), false, p, sizeof p);
    if (hc_.length() && !hcValid_) Serial.println(F("[notify] Healthchecks setting ignored: use the UUID or https://hc-ping.com/..."));
    smtpOk_ = smtpParseServer(settings.getStr("smtp", "server", ""), smtp_);
    smtp_.user = settings.getStr("smtp", "user", "");
    smtp_.pass = settings.getStr("smtp", "pass", "");
    smtp_.user.trim();
    smtpOk_ = smtpOk_ && smtp_.user.indexOf('@') > 0 && smtp_.pass.length() > 0;
    ntfyCli_.setHost("ntfy.sh");
    tgCli_.setHost("api.telegram.org");
    hcCli_.setHost("hc-ping.com");
  }
  bool configured() const override { return ntfyOn() || telegramOn() || healthcheckOn() || emailOn(); }
  bool emailOn() const override { return smtpOk_; }

  NotifyResult email(const AlertEvent& e, const char* const* to, int nTo, const char* fromName) override {
    NotifyResult r;
    if (!emailOn()) return fail(r, "smtp not configured");
    if (lastMail_ && millis() - lastMail_ < 5000) {
      r.rateLimited = true;
      return r;
    }
    lastMail_ = millis();
    static char subject[200], body[1200];
    notifyfmt::emailText(e, timeSync.epochOrZero(), app_.c_str(), subject, sizeof subject, body, sizeof body);
    // Let's Encrypt chain (YR2 <- Root YR <- ISRG X1) for mail.example.com; the ntfy bundle holds those roots.
    // If the server ever uses another CA, add its root to include/roots.h.
    SmtpResult s = smtpSend(smtp_, roots::NTFY, fromName, to, nTo, subject, body);
    r.ok = s.ok;
    r.httpCode = s.code;
    r.tls = s.tls;
    if (!s.ok) snprintf(r.error, sizeof r.error, "%s", s.err);
    return r;
  }
  bool ntfyOn() const override { return topic_.length() >= 1; }
  bool telegramOn() const override { return tgToken_.length() > 10 && nChats_ > 0; }
  int telegramChats() const override { return nChats_; }
  bool telegramPairing() const override {
    if (tgToken_.length() <= 10 || nChats_ >= TG_MAX_CHATS) return false;
    return nChats_ == 0 || (pairUntil_ && (int32_t)(pairUntil_ - millis()) > 0);
  }
  void openTelegramPairing(uint32_t ms) override { pairUntil_ = millis() + ms; }

  bool pollTelegramPairing() override {
    if (!telegramPairing()) return false;
    static char resp[4096];
    char path[200];
    snprintf(path, sizeof path, "/bot%s/getUpdates?limit=10&timeout=0", tgToken_.c_str());
    HttpResult h = tgCli_.request("GET", path, nullptr, nullptr, 0, resp, sizeof resp, false);
    if (h.status != 200) {
      if (h.status == 401 || h.status == 404) Serial.println(F("[notify] Telegram: bot token rejected (check it)"));
      return false;
    }
    char found[TG_MAX_CHATS][24];
    long long maxId = -1;
    int nf = upj::tgPairChats(resp, h.bodyLen, found, TG_MAX_CHATS, &maxId);
    if (maxId >= 0) {  // acknowledge what was read, so old messages are not seen again
      snprintf(path, sizeof path, "/bot%s/getUpdates?offset=%lld&limit=1&timeout=0", tgToken_.c_str(), maxId + 1);
      tgCli_.request("GET", path, nullptr, nullptr, 0, resp, sizeof resp, false);
    }
    int added = 0;
    for (int i = 0; i < nf && nChats_ < TG_MAX_CHATS; i++) {
      bool have = false;
      for (int k = 0; k < nChats_; k++) have |= !strcmp(chats_[k], found[i]);
      if (have) continue;
      snprintf(chats_[nChats_++], sizeof chats_[0], "%s", found[i]);
      added++;
      char body[700];
      if (notifyfmt::tgWelcomeJson(found[i], tgBn_, body, sizeof body)) {
        snprintf(path, sizeof path, "/bot%s/sendMessage", tgToken_.c_str());
        tgCli_.request("POST", path, "application/json", body, strlen(body), resp, sizeof resp, false);
      }
    }
    memset(path, 0, sizeof path);
    if (!added) return false;
    joinChats();
    settings.setStr("alert", "tgChat", tgChat_);
    Serial.printf("[notify] Telegram: %d chat(s) paired (%d of %d)\n", added, nChats_, TG_MAX_CHATS);
    return true;
  }
  bool healthcheckOn() const override { return hcValid_; }
  const char* clickUrl() const override { return app_.c_str(); }

  NotifyResult ntfy(const AlertEvent& e, const char* click) override {
    NotifyResult r;
    if (!ntfyOn()) return fail(r, "ntfy not configured");
    if (lastNtfy_ && millis() - lastNtfy_ < 2000) {
      r.rateLimited = true;
      return r;
    }
    lastNtfy_ = millis();
    static char body[900];
    if (!notifyfmt::ntfyJson(e, topic_.c_str(), click, timeSync.epochOrZero(), body, sizeof body)) return fail(r, "ntfy body");
    char resp[256];
    HttpResult h = ntfyCli_.request("POST", "/", "application/json", body, strlen(body), resp, sizeof resp, false);
    return done(r, h, 200);
  }

  NotifyResult telegram(const AlertEvent& e) override {
    NotifyResult r;
    if (!telegramOn()) return fail(r, "telegram not configured");
    if (lastTg_ && millis() - lastTg_ < 1100) {
      r.rateLimited = true;
      return r;
    }
    lastTg_ = millis();
    static char body[1100];  // Bangla is 3 bytes per letter
    static char path[160];
    snprintf(path, sizeof path, "/bot%s/sendMessage", tgToken_.c_str());
    char resp[512];
    HttpResult last;
    int ok = 0;
    for (int i = 0; i < nChats_; i++) {  // one message per paired chat, over one kept-alive connection
      if (!notifyfmt::telegramJson(e, chats_[i], timeSync.epochOrZero(), tgBn_, body, sizeof body)) continue;
      last = tgCli_.request("POST", path, "application/json", body, strlen(body), resp, sizeof resp, i + 1 < nChats_);
      if (last.status == 200) ok++;
    }
    memset(path, 0, sizeof path);
    if (ok) last.status = 200;  // delivered to at least one chat: done (a chat that blocked the bot must not cause retries)
    return done(r, last, 200);
  }

  NotifyResult healthcheck(bool failing) override {
    NotifyResult r;
    char path[200];
    if (!hcValid_ || !upj::hcPath(hc_.c_str(), failing, path, sizeof path)) return fail(r, "healthchecks not configured");
    char resp[64];
    HttpResult h = hcCli_.request("GET", path, nullptr, nullptr, 0, resp, sizeof resp, false);
    return done(r, h, 200);
  }

 private:
  static NotifyResult& fail(NotifyResult& r, const char* m) {
    snprintf(r.error, sizeof r.error, "%s", m);
    return r;
  }
  static NotifyResult& done(NotifyResult& r, const HttpResult& h, int want) {
    r.httpCode = h.status;
    r.tls = h.tls;
    r.ok = h.status == want;
    if (!r.ok) {
      if (h.status < 0) snprintf(r.error, sizeof r.error, "%s", h.err);
      else snprintf(r.error, sizeof r.error, "HTTP %d", h.status);
    }
    return r;
  }

  HttpsClient ntfyCli_, tgCli_, hcCli_;
  String topic_, tgToken_, tgChat_, hc_, app_;
  bool tgBn_ = true;
  static constexpr int TG_MAX_CHATS = 5;
  char chats_[TG_MAX_CHATS][24] = {};
  int nChats_ = 0;
  uint32_t pairUntil_ = 0;

  void splitChats() {  // "123,-1001,456" -> chats_
    nChats_ = 0;
    String s = tgChat_ + ",";
    int from = 0;
    for (int i = 0; i < (int)s.length() && nChats_ < TG_MAX_CHATS; i++) {
      if (s[i] != ',') continue;
      String c = s.substring(from, i);
      c.trim();
      from = i + 1;
      if (c.length() && c.length() < sizeof chats_[0]) snprintf(chats_[nChats_++], sizeof chats_[0], "%s", c.c_str());
    }
  }
  void joinChats() {
    tgChat_ = "";
    for (int i = 0; i < nChats_; i++) {
      if (i) tgChat_ += ",";
      tgChat_ += chats_[i];
    }
  }
  bool hcValid_ = false, smtpOk_ = false;
  SmtpConfig smtp_;
  uint32_t lastNtfy_ = 0, lastTg_ = 0, lastMail_ = 0;
};

}  // namespace

INotifier* createNotifier() {
  static Notifier n;
  n.reload();
  return &n;
}
