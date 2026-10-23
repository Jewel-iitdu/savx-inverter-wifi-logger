// RemoteOta.h - remote firmware update from the public GitHub releases (0.9.2), see net/OtaPlan.h. Device only.
// run() is called from uplinkTask (all TLS lives there); probationLoop() from the main loop (also in safe mode).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rota {

void begin();                       // at boot: repo (seeded from OTA_REPO_DEFAULT), probation, last update's outcome
void probationLoop(bool cloudOk);   // first run of a new image: confirm it, or roll back to the previous one
bool inProbation();

const char* repo();                 // "owner/repo" of the releases ("" = remote update off)
bool setRepo(const char* r);        // /api/config otaRepo (local admin only); "" turns it off
uint32_t handledAt();               // cfg.otaAt of the last request acted on
void setHandled(uint32_t at);
void setStatus(const char* fmt, ...);
void status(char* out, size_t cap); // short state for status.ota and /api/status ("" = nothing yet)
// 0.9.6: once per boot, read the latest release's SHA256SUMS.txt over the same path as an update (status.otachk:
// "GitHub ok, latest 0.9.7" or the HTTP / TLS error). The uplink connection must be dropped first.
void probe();
void check(char* out, size_t cap);

// Download, check and install release `code`. true: installed, the caller writes the status and restarts.
// false: nothing installed (status says why). Blocks ~0.5-2 min; the uplink connection must be dropped first.
bool run(uint32_t code);

}  // namespace rota
