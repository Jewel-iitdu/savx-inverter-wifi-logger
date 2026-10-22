// HttpsClient.h - minimal HTTPS/1.1 client over NetworkClientSecure with pinned roots (d1 §5.13), keep-alive,
// hard timeouts and task-watchdog feeding. Used ONLY from uplinkTask (all TLS lives there, d1 §5.3); the shared
// header buffer in HttpsClient.cpp relies on that.
//
// Timeouts (d1 §5.4): TCP connect 5 s, TLS handshake 10 s, response 10 s; esp_task_wdt_reset() between steps (TWDT
// 30 s). The response is framed by HttpParse (Content-Length / chunked), never by HTTPClient::getString(), which can
// hang on 3.3.12 (r8c M1). A request on a reused connection that gets no response byte at all (the server closed
// the idle keep-alive socket) is retried once on a fresh connection; RTDB writes are idempotent (deterministic keys).
#pragma once
#include <Arduino.h>
#include <NetworkClientSecure.h>

struct HttpResult {
  int status = 0;        // HTTP status, or < 0: -1 connect/TLS, -2 write, -3 timeout, -4 bad response, -5 no host
  size_t bodyLen = 0;
  bool truncated = false;
  bool reused = false;   // went over an already open connection
  char err[64] = "";
};

class HttpsClient {
 public:
  explicit HttpsClient(const char* rootPem) : root_(rootPem) {}
  void setHost(const char* host);  // closes the connection when the host changes
  const char* host() const { return host_; }
  // resp may be nullptr (body discarded). headersOnly: return after the header block and close (mailer nudge).
  HttpResult request(const char* method, const char* pathQuery, const char* contentType, const char* body, size_t len,
                     char* resp, size_t respCap, bool keepAlive = true, bool headersOnly = false);
  void stop();
  bool isOpen() { return open_ && cli_.connected(); }
  uint32_t handshakes() const { return hs_; }  // TLS connects attempted (cumulative)
  uint32_t requests() const { return rq_; }
  uint32_t lastConnectOkMs() const { return lastConnOk_; }
  bool everConnected() const { return everConn_; }

 private:
  bool connect(HttpResult& r);
  NetworkClientSecure cli_;
  const char* root_;
  char host_[96] = "";
  uint32_t hs_ = 0, rq_ = 0, lastConnOk_ = 0;
  bool open_ = false, everConn_ = false;
};
