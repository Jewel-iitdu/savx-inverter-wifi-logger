// Smtp.cpp - see Smtp.h.
#include "net/Smtp.h"

#include <NetworkClientSecure.h>
#include <esp_task_wdt.h>
#include <mbedtls/base64.h>
#include <time.h>

namespace {

constexpr uint32_t CONNECT_MS = 8000, REPLY_MS = 15000;

void feedWdt() {
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
}

// Reads one SMTP reply (possibly multi-line "250-..." then "250 ..."). Returns the code, or 0 on timeout.
int readReply(NetworkClientSecure& c, char* last, size_t lastN) {
  char line[256];
  size_t n = 0;
  uint32_t t0 = millis();
  if (lastN) last[0] = 0;
  while (millis() - t0 < REPLY_MS) {
    if (!c.available()) {
      if (!c.connected()) return 0;
      delay(5);
      feedWdt();
      continue;
    }
    char ch = (char)c.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (n < sizeof line - 1) line[n++] = ch;
      continue;
    }
    line[n] = 0;
    n = 0;
    if (strlen(line) < 3) continue;
    if (lastN) snprintf(last, lastN, "%s", line);
    if (line[3] == '-') continue;  // more lines follow
    return atoi(line);
  }
  return 0;
}

bool cmd(NetworkClientSecure& c, const char* text, int want, SmtpResult& r, const char* what) {
  if (text) {
    c.print(text);
    c.print("\r\n");
  }
  char last[96];
  int code = readReply(c, last, sizeof last);
  r.code = code;
  if (code / 100 == want / 100) return true;
  if (code == 0) snprintf(r.err, sizeof r.err, "%s: no reply", what);
  else snprintf(r.err, sizeof r.err, "%s: %d", what, code);
  return false;
}

String b64(const uint8_t* p, size_t n) {
  size_t out = 0;
  mbedtls_base64_encode(nullptr, 0, &out, p, n);
  String s;
  s.reserve(out + 1);
  char* buf = (char*)malloc(out + 1);
  if (!buf) return s;
  if (mbedtls_base64_encode((unsigned char*)buf, out + 1, &out, p, n) == 0) {
    buf[out] = 0;
    s = buf;
  }
  free(buf);
  return s;
}
String b64(const String& s) { return b64((const uint8_t*)s.c_str(), s.length()); }

bool asciiOnly(const char* s) {
  for (; *s; s++)
    if ((uint8_t)*s >= 0x80) return false;
  return true;
}

// RFC 2822 date in UTC, e.g. "Thu, 22 Oct 2026 01:40:00 +0000"
void rfcDate(char* out, size_t n) {
  time_t now = time(nullptr);
  struct tm g;
  gmtime_r(&now, &g);
  strftime(out, n, "%a, %d %b %Y %H:%M:%S +0000", &g);
}

}  // namespace

bool smtpParseServer(const String& s0, SmtpConfig& cfg) {
  String s = s0;
  s.trim();
  if (!s.length()) return false;
  int colon = s.lastIndexOf(':');
  cfg.port = 465;
  if (colon > 0) {
    long p = s.substring(colon + 1).toInt();
    if (p <= 0 || p > 65535) return false;
    cfg.port = (uint16_t)p;
    s = s.substring(0, colon);
  }
  if (s.indexOf('.') < 0 || s.indexOf(' ') >= 0) return false;
  cfg.host = s;
  return true;
}

SmtpResult smtpSend(const SmtpConfig& cfg, const char* rootsPem, const char* fromName, const char* const* to, int nTo,
                    const char* subject, const char* body) {
  SmtpResult r;
  if (!cfg.host.length() || !cfg.user.length() || !cfg.pass.length() || nTo <= 0) {
    snprintf(r.err, sizeof r.err, "smtp not configured");
    return r;
  }
  NetworkClientSecure c;
  c.setCACert(rootsPem);
  c.setHandshakeTimeout(10);
  c.setTimeout(REPLY_MS);
  feedWdt();
  if (!c.connect(cfg.host.c_str(), cfg.port, (int32_t)CONNECT_MS)) {
    snprintf(r.err, sizeof r.err, "connect/TLS to %s:%u failed", cfg.host.c_str(), cfg.port);
    return r;
  }
  feedWdt();
  bool ok = cmd(c, nullptr, 220, r, "greeting") && cmd(c, "EHLO savx-logger", 250, r, "EHLO");
  if (ok) {  // AUTH LOGIN, then AUTH PLAIN if LOGIN is refused
    ok = cmd(c, "AUTH LOGIN", 334, r, "AUTH") && cmd(c, b64(cfg.user).c_str(), 334, r, "AUTH user") &&
         cmd(c, b64(cfg.pass).c_str(), 235, r, "AUTH password");
    if (!ok && r.code != 535 && r.code != 0) {  // LOGIN not offered/refused (535 = wrong credentials: stop)
      uint8_t raw[160];  // "\0user\0password"
      size_t k = 0;
      raw[k++] = 0;
      for (size_t i = 0; i < cfg.user.length() && k < sizeof raw; i++) raw[k++] = cfg.user[i];
      if (k < sizeof raw) raw[k++] = 0;
      for (size_t i = 0; i < cfg.pass.length() && k < sizeof raw; i++) raw[k++] = cfg.pass[i];
      String tok = "AUTH PLAIN " + b64(raw, k);
      memset(raw, 0, sizeof raw);
      ok = cmd(c, tok.c_str(), 235, r, "AUTH PLAIN");
    }
  }
  char line[320];
  if (ok) {
    snprintf(line, sizeof line, "MAIL FROM:<%s>", cfg.user.c_str());
    ok = cmd(c, line, 250, r, "MAIL FROM");
  }
  int accepted = 0;
  for (int i = 0; ok && i < nTo; i++) {
    snprintf(line, sizeof line, "RCPT TO:<%s>", to[i]);
    if (cmd(c, line, 250, r, "RCPT TO")) accepted++;
  }
  if (ok && !accepted) ok = false;
  if (ok) ok = cmd(c, "DATA", 354, r, "DATA");
  if (ok) {
    char date[40];
    rfcDate(date, sizeof date);
    String subj = asciiOnly(subject) ? String(subject) : "=?UTF-8?B?" + b64(String(subject)) + "?=";
    String name = (fromName && *fromName) ? String(fromName) : String("Solar logger");
    if (!asciiOnly(name.c_str())) name = "=?UTF-8?B?" + b64(name) + "?=";
    else name = "\"" + name + "\"";
    c.printf("From: %s <%s>\r\n", name.c_str(), cfg.user.c_str());
    c.print("To: ");
    for (int i = 0; i < nTo; i++) c.printf("%s%s", i ? ", " : "", to[i]);
    c.print("\r\n");
    c.printf("Subject: %s\r\n", subj.c_str());
    c.printf("Date: %s\r\n", date);
    c.printf("Message-ID: <%lu.%lu@savx-logger>\r\n", (unsigned long)time(nullptr), (unsigned long)millis());
    c.print("MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n");
    // body: normalise to CRLF, dot-stuff lines that start with '.'
    bool bol = true;
    for (const char* p = body; *p; p++) {
      if (*p == '\r') continue;
      if (bol && *p == '.') c.print('.');
      if (*p == '\n') {
        c.print("\r\n");
        bol = true;
        continue;
      }
      c.print(*p);
      bol = false;
    }
    if (!bol) c.print("\r\n");
    feedWdt();
    ok = cmd(c, ".", 250, r, "message");
  }
  SmtpResult q;
  cmd(c, "QUIT", 221, q, "QUIT");  // best effort; does not change the result
  c.stop();
  r.ok = ok;
  if (ok) r.err[0] = 0;
  return r;
}
