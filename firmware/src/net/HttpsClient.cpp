// HttpsClient.cpp - see HttpsClient.h.
#include "net/HttpsClient.h"

#include <esp_task_wdt.h>

#include "config.h"
#include "hal/ArduinoHal.h"
#include "net/HttpParse.h"

// Request-line + headers. Shared by every client: all of them run in uplinkTask only. The RTDB path carries the
// ~1 KB ID token (`?auth=`), hence the size.
static constexpr size_t HDR_CAP = 2560;
static char* s_hdr = nullptr;  // PSRAM, allocated on first use

void HttpsClient::setHost(const char* host) {
  if (strcmp(host_, host ? host : "") != 0) {
    stop();
    snprintf(host_, sizeof host_, "%s", host ? host : "");
  }
}

void HttpsClient::stop() {
  if (open_) cli_.stop();
  open_ = false;
}

bool HttpsClient::connect(HttpResult& r) {
  if (!host_[0]) {
    r.status = -5;
    snprintf(r.err, sizeof r.err, "no host configured");
    return false;
  }
#ifdef FW_TLS_INSECURE_DEBUG
#warning "FW_TLS_INSECURE_DEBUG: certificate checks are OFF - bench debugging only, never flash this to the logger"
  cli_.setInsecure();
#else
  cli_.setCACert(root_);
#endif
  cli_.setHandshakeTimeout(cfg::HTTP_HANDSHAKE_S);
  hs_++;
  esp_task_wdt_reset();
  int ok = cli_.connect(host_, 443, (int32_t)cfg::HTTP_CONNECT_MS);
  esp_task_wdt_reset();
  if (!ok) {
    char e[64] = "";
    r.tls = cli_.lastError(e, sizeof e);
    snprintf(r.err, sizeof r.err, "connect %s: %s", host_, e[0] ? e : "failed");
    r.status = -1;
    cli_.stop();
    open_ = false;
    return false;
  }
  open_ = true;
  everConn_ = true;
  lastConnOk_ = millis();
  return true;
}

static bool writeAll(NetworkClientSecure& c, const char* p, size_t n) {
  size_t off = 0;
  uint32_t start = millis();
  while (off < n) {
    size_t chunk = n - off > 2048 ? 2048 : n - off;
    size_t w = c.write((const uint8_t*)p + off, chunk);
    if (w == 0) {
      if ((uint32_t)(millis() - start) > cfg::HTTP_RESPONSE_MS || !c.connected()) return false;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    off += w;
    esp_task_wdt_reset();
  }
  return true;
}

HttpResult HttpsClient::request(const char* method, const char* pathQuery, const char* contentType, const char* body,
                                size_t len, char* resp, size_t respCap, bool keepAlive, bool headersOnly) {
  HttpResult r;
  if (!s_hdr) s_hdr = (char*)psramAlloc(HDR_CAP);
  if (!s_hdr) {
    r.status = -2;
    snprintf(r.err, sizeof r.err, "out of memory");
    return r;
  }
  rq_++;
  for (int attempt = 0; attempt < 2; attempt++) {
    r = HttpResult();
    bool reused = open_ && cli_.connected();
    if (!reused) {
      stop();
      if (!connect(r)) return r;
    }
    r.reused = reused;
    bool withBody = body != nullptr || strcmp(method, "GET") != 0;
    int h = snprintf(s_hdr, HDR_CAP,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " FW_NAME "/" FW_VERSION "\r\nAccept: */*\r\n"
                     "Connection: %s\r\n",
                     method, pathQuery, host_, (keepAlive && !headersOnly) ? "keep-alive" : "close");
    if (withBody && h > 0 && (size_t)h < HDR_CAP)
      h += snprintf(s_hdr + h, HDR_CAP - h, "Content-Type: %s\r\nContent-Length: %u\r\n",
                    contentType ? contentType : "application/json", (unsigned)(body ? len : 0));
    if (h > 0 && (size_t)h < HDR_CAP) h += snprintf(s_hdr + h, HDR_CAP - h, "\r\n");
    if (h <= 0 || (size_t)h >= HDR_CAP) {
      r.status = -2;
      snprintf(r.err, sizeof r.err, "request line too long");
      return r;
    }
    bool wrote = writeAll(cli_, s_hdr, (size_t)h) && (!body || !len || writeAll(cli_, body, len));
    if (!wrote) {
      stop();
      if (reused && attempt == 0) continue;  // stale keep-alive socket: one retry on a fresh connection
      r.status = -2;
      snprintf(r.err, sizeof r.err, "write to %s failed", host_);
      return r;
    }
    HttpParse p;
    p.reset(resp, resp ? respCap : 0, headersOnly, !strcmp(method, "HEAD"));
    uint8_t buf[512];
    uint32_t deadline = millis() + cfg::HTTP_RESPONSE_MS;
    HttpParse::R pr = HttpParse::R::MORE;
    size_t got = 0;
    bool retry = false;
    while (pr == HttpParse::R::MORE) {
      int a = cli_.available();
      if (a > 0) {
        int n = cli_.read(buf, a > (int)sizeof buf ? sizeof buf : (size_t)a);
        if (n > 0) {
          got += (size_t)n;
          pr = p.feed(buf, (size_t)n);
          deadline = millis() + cfg::HTTP_RESPONSE_MS;  // progress: extend (slow but alive)
          continue;
        }
      }
      if (!cli_.connected()) {
        if (got == 0 && reused && attempt == 0) {
          retry = true;
          break;
        }
        pr = p.finishOnClose();
        break;
      }
      if ((int32_t)(millis() - deadline) > 0) {
        stop();
        r.status = -3;
        snprintf(r.err, sizeof r.err, "%s: response timeout", host_);
        return r;
      }
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(3));
    }
    if (retry) {
      stop();
      continue;
    }
    if (pr != HttpParse::R::DONE) {
      stop();
      r.status = -4;
      snprintf(r.err, sizeof r.err, "%s: %s", host_, p.error());
      return r;
    }
    r.status = p.status();
    r.bodyLen = p.bodyLen();
    r.truncated = p.truncated();
    if (!p.keepAlive() || !keepAlive || headersOnly) stop();
    return r;
  }
  return r;
}
