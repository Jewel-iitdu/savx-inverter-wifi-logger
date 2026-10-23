// OtaPlan.h - decisions and parsing for the remote firmware update (0.9.2). Host-portable; the download itself is in
// RemoteOta.cpp. The owner asks for a version in the app (cfg.ota = version code, cfg.otaAt = request time); the
// logger downloads that version's OTA image from the public GitHub releases of the repo saved on the logger
// (never a URL from the cloud), checks its SHA-256 against the release's SHA256SUMS.txt, installs it and restarts.
// The new image runs on probation (bootloader rollback): it confirms itself after a cloud write and some uptime;
// a crash or no cloud within OTA_PROBATION_MAX_MS sends the bootloader back to the previous image.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ota {

// "0.9.3" -> 903 (major*10000 + minor*100 + patch, each 0..99); 0 = not a version
uint32_t versionCode(const char* v);
void versionStr(uint32_t code, char* out, size_t cap);

// owner/repo with GitHub's characters only (letters, digits, '-', '_', '.'), one slash
bool repoValid(const char* repo);

// "/<repo>/releases/download/v<ver>/<file>" on github.com; 0 if it does not fit
size_t releasePath(char* out, size_t cap, const char* repo, uint32_t code, const char* file);
// "savx-logger-<ver>-ota.bin" (tools/build-release.sh)
void imageName(uint32_t code, char* out, size_t cap);

// The SHA-256 of `file` in a sha256sum listing ("<64 hex>  <file>" lines); false if absent or malformed.
bool parseSums(const char* text, size_t n, const char* file, uint8_t sha[32]);

// The version of the OTA image listed in a SHA256SUMS.txt ("savx-logger-<ver>-ota.bin"); false if there is none.
bool versionInSums(const char* text, size_t n, char* ver, size_t cap);

enum class Go : uint8_t { NONE, START, NOT_NEWER, NO_REPO };
// A request (code, at) is handled once: `handledAt` is the request time of the last one acted on.
Go decide(uint32_t reqCode, uint32_t reqAt, uint32_t ownCode, uint32_t handledAt, bool repoSet);

// Enough internal RAM for the TLS download (largest free block, total free; cfg::OTA_MIN_BLOCK / OTA_MIN_FREE)
bool heapOk(uint32_t maxBlock, uint32_t freeHeap);

enum class Probation : uint8_t { WAIT, CONFIRM, ROLLBACK };
// The image's first run: confirm after a cloud write and minMs of uptime; roll back at maxMs without one.
Probation probation(uint32_t upMs, bool cloudOk, uint32_t minMs, uint32_t maxMs);

}  // namespace ota
