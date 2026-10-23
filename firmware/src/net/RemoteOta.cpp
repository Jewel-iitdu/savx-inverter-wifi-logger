// RemoteOta.cpp - see RemoteOta.h.
#include "net/RemoteOta.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "hal/ArduinoHal.h"
#include "net/OtaPlan.h"
#include "store/Settings.h"

// ESP-IDF's own full Mozilla root bundle (libmbedtls x509_crt_bundle.S, CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL):
// GitHub's download host has changed CAs before, so no pinned roots here. 0.9.2-0.9.5 embedded a bundle made with the
// Arduino generator; IDF 5.5 reads a newer layout (an offset table first), so every GitHub handshake failed (-1).
extern const uint8_t caBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t caBundleEnd[] asm("_binary_x509_crt_bundle_end");

// Arduino confirms a new image at boot unless this says "later" (cores/esp32/esp32-hal-misc.c): the image stays on
// probation (bootloader rollback) until probationLoop() confirms it.
extern "C" bool verifyRollbackLater() { return true; }

namespace rota {
namespace {

RtosLock lock_;
char status_[48] = "";
char check_[48] = "";   // the boot check of the GitHub download path (status.otachk)
char repo_[84] = "";
uint32_t handled_ = 0;
bool probation_ = false;
uint32_t tryCode_ = 0;
uint8_t buf_[4096];

uint32_t getU32(const char* key) {
  uint32_t v = 0;
  if (!settings.getBlob("ota", key, &v, sizeof v)) v = 0;
  return v;
}
void putU32(const char* key, uint32_t v) { settings.putBlob("ota", key, &v, sizeof v); }

bool fail(HTTPClient& http, const char* fmt, ...) {
  http.end();
  char m[48];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m, sizeof m, fmt, ap);
  va_end(ap);
  setStatus("failed: %s", m);
  Serial.printf("[ota] failed: %s\n", m);
  return false;
}

void prep(NetworkClientSecure& cli, HTTPClient& http) {
  cli.setCACertBundle(caBundleStart, (size_t)(caBundleEnd - caBundleStart));
  cli.setHandshakeTimeout(cfg::OTA_TLS_TIMEOUT_S);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // github.com -> its release-assets host
  http.setConnectTimeout(cfg::OTA_CONNECT_MS);
  http.setTimeout(cfg::OTA_IO_MS);
  http.setUserAgent("savx-logger/" FW_VERSION);
}

// GET a small text file (SHA256SUMS.txt) into out. A failed connection (TLS) is retried once; err gets the HTTP code
// or the connection and mbedTLS error codes (e.g. "conn -1 tls -0x2700": certificate not verified).
bool getText(NetworkClientSecure& cli, HTTPClient& http, const String& url, char* out, size_t cap, size_t& n, int& code,
             char* err, size_t errN) {
  n = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!http.begin(cli, url)) {
      snprintf(err, errN, "bad URL");
      return false;
    }
    code = http.GET();
    if (code == 200) {
      NetworkClient* s = http.getStreamPtr();
      uint32_t t0 = millis();
      int want = http.getSize();
      while (n < cap - 1 && (want < 0 || (int)n < want) && millis() - t0 < cfg::OTA_IO_MS) {
        int a = s->available();
        if (a <= 0) {
          if (!http.connected()) break;
          delay(5);
          continue;
        }
        int r = s->read((uint8_t*)out + n, min((size_t)a, cap - 1 - n));
        if (r > 0) n += (size_t)r;
      }
      out[n] = 0;
      http.end();
      return true;
    }
    char eb[64];
    int le = cli.lastError(eb, sizeof eb);
    http.end();
    if (code > 0) {
      snprintf(err, errN, "HTTP %d", code);
      return false;
    }
    snprintf(err, errN, "conn %d tls -0x%04x", code, (unsigned)(le < 0 ? -le : le));
    Serial.printf("[ota] %s: %s (%s)\n", url.c_str(), err, eb);
    delay(3000);
  }
  return false;
}

}  // namespace

void setStatus(const char* fmt, ...) {
  LockGuard g(&lock_);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(status_, sizeof status_, fmt, ap);
  va_end(ap);
}

void status(char* out, size_t cap) {
  LockGuard g(&lock_);
  snprintf(out, cap, "%s", status_);
}

void check(char* out, size_t cap) {
  LockGuard g(&lock_);
  snprintf(out, cap, "%s", check_);
}

void probe() {
  if (!repo_[0] || !ota::heapOk(ESP.getMaxAllocHeap(), ESP.getFreeHeap())) return;
  esp_task_wdt_delete(nullptr);
  NetworkClientSecure cli;
  HTTPClient http;
  prep(cli, http);
  char sums[640], err[40];
  size_t n;
  int code;
  // GitHub redirects .../releases/latest/download/<file> to the newest release's file
  String url = String("https://github.com/") + repo_ + "/releases/latest/download/SHA256SUMS.txt";
  char res[48];
  if (getText(cli, http, url, sums, sizeof sums, n, code, err, sizeof err)) {
    char v[16] = "?";
    ota::versionInSums(sums, n, v, sizeof v);
    snprintf(res, sizeof res, "GitHub ok, latest %s", v);
  } else {
    snprintf(res, sizeof res, "GitHub failed: %s", err);
  }
  esp_task_wdt_add(nullptr);
  {
    LockGuard g(&lock_);
    snprintf(check_, sizeof check_, "%s", res);
  }
  Serial.printf("[ota] check: %s (heap block %u)\n", res, (unsigned)ESP.getMaxAllocHeap());
}

const char* repo() { return repo_; }

bool setRepo(const char* r) {
  if (r && *r && !ota::repoValid(r)) return false;
  snprintf(repo_, sizeof repo_, "%s", r ? r : "");
  settings.setStr("ota", "repo", repo_);
  return true;
}

uint32_t handledAt() { return handled_; }
void setHandled(uint32_t at) {
  handled_ = at;
  putU32("handled", at);
}

bool inProbation() { return probation_; }

void begin() {
  String r = settings.getStr("ota", "repo", "");
  if (!r.length() && OTA_REPO_DEFAULT[0] && !settings.hasStr("ota", "repo")) {
    r = OTA_REPO_DEFAULT;  // the owner's own build seeds it once; public builds leave it empty
    settings.setStr("ota", "repo", r);
  }
  snprintf(repo_, sizeof repo_, "%s", r.c_str());
  handled_ = getU32("handled");
  tryCode_ = getU32("try");
  uint32_t own = ota::versionCode(FW_VERSION);
  char tv[16];
  ota::versionStr(tryCode_, tv, sizeof tv);
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    probation_ = true;
    setStatus("checking %s after the update", FW_VERSION);
    Serial.printf("[ota] %s runs on probation (rollback armed)\n", FW_VERSION);
  } else if (tryCode_) {
    if (tryCode_ == own) setStatus("updated to %s", FW_VERSION);
    else setStatus("update to %s failed, back on %s", tv, FW_VERSION);  // the bootloader rolled it back
    putU32("try", 0);
    tryCode_ = 0;
  }
}

void probationLoop(bool cloudOk) {
  if (!probation_) return;
  switch (ota::probation(millis(), cloudOk, cfg::OTA_PROBATION_MIN_MS, cfg::OTA_PROBATION_MAX_MS)) {
    case ota::Probation::CONFIRM:
      esp_ota_mark_app_valid_cancel_rollback();
      probation_ = false;
      setStatus("updated to %s", FW_VERSION);
      putU32("try", 0);
      Serial.printf("[ota] %s confirmed\n", FW_VERSION);
      break;
    case ota::Probation::ROLLBACK:
      Serial.println(F("[ota] no cloud since the update: rolling back to the previous image"));
      delay(100);
      esp_ota_mark_app_invalid_rollback_and_reboot();  // does not return
      break;
    default:
      break;
  }
}

bool run(uint32_t code) {
  char v[16], file[40], path[200];
  ota::versionStr(code, v, sizeof v);
  if (!repo_[0]) {
    setStatus("failed: no release repo on the logger");
    return false;
  }
  if (!ota::heapOk(ESP.getMaxAllocHeap(), ESP.getFreeHeap())) {
    setStatus("failed: low memory (%u/%u)", (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreeHeap());
    return false;
  }
  Serial.printf("[ota] heap: block %u, free %u\n", (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreeHeap());
  // Long blocking network calls below (each with its own timeout): leave the task watchdog for the duration.
  esp_task_wdt_delete(nullptr);
  struct Rewd {
    ~Rewd() { esp_task_wdt_add(nullptr); }
  } rewd;

  NetworkClientSecure cli;
  HTTPClient http;
  prep(cli, http);

  // 1. the release's SHA256SUMS.txt
  setStatus("checking release %s", v);
  ota::releasePath(path, sizeof path, repo_, code, "SHA256SUMS.txt");
  char sums[640], err[40];
  size_t sn = 0;
  int st = 0;
  if (!getText(cli, http, String("https://github.com") + path, sums, sizeof sums, sn, st, err, sizeof err)) {
    if (st == 404) return fail(http, "no release v%s", v);
    return fail(http, "checksums %s", err);
  }
  uint8_t want[32];
  ota::imageName(code, file, sizeof file);
  if (!ota::parseSums(sums, sn, file, want)) return fail(http, "%s not in checksums", file);

  // 2. the image, streamed into the other app slot and hashed on the way
  setStatus("downloading %s", v);
  ota::releasePath(path, sizeof path, repo_, code, file);
  if (!http.begin(cli, String("https://github.com") + path)) return fail(http, "bad URL");
  st = http.GET();
  if (st != 200) {
    char eb[64];
    int le = cli.lastError(eb, sizeof eb);
    return fail(http, "image %d tls -0x%04x", st, (unsigned)(le < 0 ? -le : le));
  }
  int len = http.getSize();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (len <= 0 || !next || (uint32_t)len > next->size) return fail(http, "image size %d", len);
  if (!Update.begin((size_t)len)) return fail(http, "flash: %s", Update.errorString());
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  NetworkClient* s = http.getStreamPtr();
  int got = 0, lastPct = -1;
  uint32_t lastData = millis(), t0 = millis();
  while (got < len && millis() - t0 < cfg::OTA_TOTAL_MS) {
    int a = s->available();
    if (a <= 0) {
      if (!http.connected() || millis() - lastData > cfg::OTA_IO_MS) break;
      delay(5);
      continue;
    }
    int r = s->read(buf_, min((size_t)a, min(sizeof buf_, (size_t)(len - got))));
    if (r <= 0) continue;
    mbedtls_sha256_update(&sha, buf_, (size_t)r);
    if (Update.write(buf_, (size_t)r) != (size_t)r) {
      mbedtls_sha256_free(&sha);
      Update.abort();
      return fail(http, "flash: %s", Update.errorString());
    }
    got += r;
    lastData = millis();
    int pct = (int)((int64_t)got * 100 / len);
    if (pct / 10 != lastPct / 10) {
      lastPct = pct;
      setStatus("downloading %s: %d%%", v, pct);
    }
  }
  uint8_t have[32];
  mbedtls_sha256_finish(&sha, have);
  mbedtls_sha256_free(&sha);
  if (got != len) {
    Update.abort();
    return fail(http, "download stopped at %d of %d", got, len);
  }
  if (memcmp(have, want, 32)) {
    Update.abort();
    return fail(http, "checksum mismatch");
  }
  if (!Update.end()) return fail(http, "flash: %s", Update.errorString());  // also checks the image's own digest
  http.end();
  putU32("try", code);
  setStatus("installed %s, restarting", v);
  Serial.printf("[ota] %s installed (%d bytes, SHA-256 ok), restarting\n", v, len);
  return true;
}

}  // namespace rota
