// Smtp.h - minimal SMTP client for alert e-mails sent directly by the logger (owner request 2026-10-22: the
// owner's own mail server instead of the Apps Script mailer of research/r8-email-alerts.md).
//  - implicit TLS only (port 465, "SMTPS"), pinned roots from include/roots.h, AUTH LOGIN (AUTH PLAIN fallback);
//  - one message per connection: EHLO, AUTH, MAIL FROM, RCPT TO x n, DATA, QUIT;
//  - UTF-8 subject (RFC 2047 base64) and body (8bit), CRLF line endings, dot-stuffing;
//  - runs ONLY in uplinkTask (all TLS lives there); never logs the password.
#pragma once
#include <Arduino.h>

struct SmtpConfig {
  String host;
  uint16_t port = 465;
  String user;  // login and From address
  String pass;
};

struct SmtpResult {
  bool ok = false;
  int code = 0;      // last SMTP reply code (0 = no reply)
  int tls = 0;       // mbedTLS error code of a failed connect (1.0.0)
  char err[64] = "";
};

// Parses "host" or "host:port" into cfg (port defaults to 465). Returns false if empty/invalid.
bool smtpParseServer(const String& s, SmtpConfig& cfg);

// Sends one plain-text message to 1..n recipients. rootsPem: PEM bundle of the trust anchors.
SmtpResult smtpSend(const SmtpConfig& cfg, const char* rootsPem, const char* fromName, const char* const* to, int nTo,
                    const char* subject, const char* body);
