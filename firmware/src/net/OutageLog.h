// OutageLog.h - why the logger was offline (1.0.2, owner request 2026-10-25). Host-portable.
//
// 2026-10-25 the logger went offline a few times and came back by itself, and nothing said why. Now the uplink notes
// what happens between two cloud successes: Wi-Fi drops and their reason code, Wi-Fi down time, cloud errors (the
// first one), TLS out-of-memory, network-watchdog TLS drops, the lowest free / largest-block internal heap and the
// slowest uplink step. When the cloud works again, a gap of OUTAGE_MIN_MS or more, or a crash/watchdog reset inside
// it, becomes one log-only LOGGER_OFFLINE event (events/<date>, existing rules: no rule change, no push).
//
// The record (Rec) and the crumbs live in RTC memory on the device (RTC_NOINIT, sys/Crumbs.cpp): they survive a
// software restart, a panic and a watchdog reset, not a power cut. After a power cut the gap starts at the NVS
// 5-min checkpoint instead (marked "~"). The crumbs say what each task was doing when the previous boot ended.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct AlertEvent;

namespace outage {

constexpr uint32_t OUTAGE_MIN_MS = 3UL * 60000UL;  // shorter gaps are normal (token refresh, a TLS reconnect)
constexpr uint32_t STALL_MS = 10000;               // a task whose loop is this much older than the newest one stalled

// what the uplink task is doing (crumbs); names in stepName()
enum Step : uint8_t {
  S_IDLE, S_AUTH, S_EVENTS, S_LIVE, S_MINUTE, S_CFG, S_ALERTS, S_META, S_DAILY, S_METER, S_MTD, S_DIAG, S_PRUNE,
  S_OTA, S_PROBE, S_PUSH, S_TG_PAIR, S_MAIL, S_HC, S_N
};
const char* stepName(uint8_t s);

enum Task : uint8_t { T_LOOP, T_INV, T_AGG, T_UPLINK, T_N };

// Breadcrumbs: each task writes only its own field (aligned 32-bit stores), so no lock is needed.
struct Crumbs {
  uint32_t magic;
  uint32_t ms[T_N];  // millis() at the top of each task's last loop (0 = not running)
  uint32_t step;     // Step the uplink task is in
  uint32_t stepMs;   // millis() when it started
};
constexpr uint32_t CRUMBS_MAGIC = 0xC0B5A102;
void crumbsReset(Crumbs& c);
bool crumbsValid(const Crumbs& c);
// what the previous boot was doing when it ended, e.g. "up 3h12m; uplink in mail 31s; inv stalled 40s"
void crumbsText(const Crumbs& c, char* out, size_t n);

struct Rec {
  uint32_t magic;
  uint32_t sinceEp;      // UTC epoch of the last cloud success (0 = unknown)
  uint32_t sinceMs;      // millis() of it in this boot
  uint32_t wifiDownS;    // Wi-Fi down time inside the gap
  uint32_t slowMs;       // slowest uplink step inside the gap
  uint32_t minHeap;      // lowest free internal heap (0 = none seen)
  uint32_t minBlock;     // lowest largest free internal block
  uint32_t minStack;     // lowest free stack of the uplink task, bytes (0 = none seen)
  uint32_t fw;           // version code of the image that wrote it (a rollback or update starts afresh)
  uint16_t drops;        // Wi-Fi up -> down
  uint16_t fails;        // failed cloud requests / sign-ins
  uint16_t oom;          // of them: TLS out of memory
  uint8_t reason;        // last Wi-Fi disconnect reason (802.11 / IDF code; 0 = none)
  int8_t rssi;           // last signal while connected (0 = none)
  uint8_t tlsDrops;      // network-watchdog TLS drops
  uint8_t slowStep;
  uint8_t reboots;       // resets inside the gap
  uint8_t approx;        // 1: sinceEp is the NVS checkpoint (power cut)
  uint8_t crash;         // 1: a crash / watchdog reset inside the gap -> always reported
  uint8_t pad[3];
  char err[44];          // first cloud error in the gap
  char rr[12];           // reset reason of the last reboot in the gap ("" = none)
  char crumb[64];        // crumbsText() of the boot that ended
  uint32_t sum;
};
constexpr uint32_t REC_MAGIC = 0x0F1A0103;  // bump the low byte when Rec changes (a new image ignores the old one)

bool recValid(const Rec& r);
void recSeal(Rec& r);

struct Report {
  uint32_t startEp;  // event time: start of the gap (0 = unknown, use now)
  uint32_t durS;
  char msg[200];
};

// short name of a Wi-Fi disconnect reason ("NO_AP_FOUND"), nullptr if unknown
const char* wifiReasonName(uint8_t reason);

// Owns a Rec (the device's lives in RTC memory). Only the uplink task calls it, after boot().
class OutageLog {
 public:
  explicit OutageLog(Rec& r) : r_(r) {}
  // At boot. A valid Rec (it survived a reset) carries the open gap over; otherwise the gap starts at
  // checkpointEp (the NVS 5-min checkpoint; 0 = unknown). crashReset: PANIC / *_WDT / BROWNOUT / UNKNOWN.
  // fwCode: this image's version code; a record written by another image is not carried over.
  void boot(uint32_t nowMs, const char* resetReason, bool crashReset, bool poweron, uint32_t checkpointEp,
            const char* crumb, uint32_t fwCode);
  void onWifi(uint32_t nowMs, bool up, uint8_t lastReason, int8_t rssi);
  void onHeap(uint32_t freeInternal, uint32_t largestBlock, uint32_t freeStack);
  void onFail(const char* err, bool oom);
  void onTlsDrop();
  void onStep(uint8_t step, uint32_t ms);
  // a cloud success. true when the gap that ends here is worth a report (filled into out); the gap restarts.
  bool onCloudOk(uint32_t nowMs, uint32_t nowEp, Report& out);
  const Rec& rec() const { return r_; }

 private:
  void restart(uint32_t nowMs, uint32_t nowEp);
  Rec& r_;
  uint32_t lastWifiMs_ = 0, downAccMs_ = 0, fw_ = 0;
  int8_t lastUpRssi_ = 0;
  bool wifiKnown_ = false, wifiUp_ = false;
};

// the log-only LOGGER_OFFLINE event for a report (sev 1, no push, no e-mail); ts 0 = set later from upMs
void toEvent(const Report& rep, uint32_t nowEp, uint32_t upMs, AlertEvent& e);

// the event text (<= 199 chars), most important first: duration, Wi-Fi, cloud, heap, slowest step, reboot
void formatReport(const Rec& r, uint32_t durS, bool durKnown, char* out, size_t n);

}  // namespace outage
