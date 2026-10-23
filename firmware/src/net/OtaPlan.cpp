// OtaPlan.cpp - see OtaPlan.h.
#include "net/OtaPlan.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace ota {

uint32_t versionCode(const char* v) {
  if (!v) return 0;
  unsigned a, b, c;
  char tail;
  if (sscanf(v, "%u.%u.%u%c", &a, &b, &c, &tail) != 3 || a > 99 || b > 99 || c > 99) return 0;
  return a * 10000 + b * 100 + c;
}

void versionStr(uint32_t code, char* out, size_t cap) {
  snprintf(out, cap, "%u.%u.%u", (unsigned)(code / 10000), (unsigned)(code / 100 % 100), (unsigned)(code % 100));
}

bool repoValid(const char* repo) {
  if (!repo || !*repo || strlen(repo) > 80) return false;
  int slashes = 0;
  size_t n = strlen(repo);
  for (size_t i = 0; i < n; i++) {
    char c = repo[i];
    if (c == '/') {
      if (i == 0 || i == n - 1) return false;
      slashes++;
    } else if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') {
      return false;
    }
  }
  return slashes == 1 && !strstr(repo, "..");
}

void imageName(uint32_t code, char* out, size_t cap) {
  char v[16];
  versionStr(code, v, sizeof v);
  snprintf(out, cap, "savx-logger-%s-ota.bin", v);
}

size_t releasePath(char* out, size_t cap, const char* repo, uint32_t code, const char* file) {
  char v[16];
  versionStr(code, v, sizeof v);
  int n = snprintf(out, cap, "/%s/releases/download/v%s/%s", repo, v, file);
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseSums(const char* text, size_t n, const char* file, uint8_t sha[32]) {
  size_t fl = strlen(file);
  size_t i = 0;
  while (i < n) {
    size_t e = i;
    while (e < n && text[e] != '\n') e++;
    // "<64 hex><space><space or *><file>[\r]"
    size_t len = e - i;
    if (len > 0 && text[e - 1] == '\r') len--;
    if (len == 64 + 2 + fl && (text[i + 64] == ' ') && (text[i + 65] == ' ' || text[i + 65] == '*') &&
        !memcmp(text + i + 66, file, fl)) {
      for (int k = 0; k < 32; k++) {
        int h = hexv(text[i + 2 * k]), l = hexv(text[i + 2 * k + 1]);
        if (h < 0 || l < 0) return false;
        sha[k] = (uint8_t)(h << 4 | l);
      }
      return true;
    }
    i = e + 1;
  }
  return false;
}

bool versionInSums(const char* text, size_t n, char* ver, size_t cap) {
  static const char PRE[] = "savx-logger-", SUF[] = "-ota.bin";
  size_t i = 0;
  while (i < n) {  // line by line: the factory image's line comes first and must not be read across
    size_t e = i;
    while (e < n && text[e] != '\n') e++;
    size_t len = e - i;
    if (len && text[e - 1] == '\r') len--;
    for (size_t k = i; k + sizeof PRE - 1 <= i + len; k++) {
      if (memcmp(text + k, PRE, sizeof PRE - 1)) continue;
      const char* v = text + k + sizeof PRE - 1;
      size_t vl = i + len - (size_t)(v - text);
      if (vl > sizeof SUF - 1 && !memcmp(v + vl - (sizeof SUF - 1), SUF, sizeof SUF - 1)) {
        vl -= sizeof SUF - 1;
        char tmp[16];
        if (vl == 0 || vl >= sizeof tmp) break;
        memcpy(tmp, v, vl);
        tmp[vl] = 0;
        if (!versionCode(tmp)) break;
        snprintf(ver, cap, "%s", tmp);
        return true;
      }
      break;
    }
    i = e + 1;
  }
  return false;
}

Go decide(uint32_t reqCode, uint32_t reqAt, uint32_t ownCode, uint32_t handledAt, bool repoSet) {
  if (!reqCode || !reqAt || reqAt == handledAt) return Go::NONE;
  if (reqCode <= ownCode) return Go::NOT_NEWER;
  if (!repoSet) return Go::NO_REPO;
  return Go::START;
}

bool heapOk(uint32_t maxBlock, uint32_t freeHeap) {
  return maxBlock >= cfg::OTA_MIN_BLOCK && freeHeap >= cfg::OTA_MIN_FREE;
}

Probation probation(uint32_t upMs, bool cloudOk, uint32_t minMs, uint32_t maxMs) {
  if (cloudOk && upMs >= minMs) return Probation::CONFIRM;
  if (upMs >= maxMs) return Probation::ROLLBACK;
  return Probation::WAIT;
}

}  // namespace ota
