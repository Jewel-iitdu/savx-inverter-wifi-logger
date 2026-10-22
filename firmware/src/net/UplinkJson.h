// UplinkJson.h - RTDB paths, keys and JSON bodies of the cloud uplink (d1 §4.1-4.2, §4.8 + r8 §3). Host-portable.
// Everything that decides WHAT is written where lives here so the native tests pin the data contract.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "agg/AlertEngine.h"
#include "agg/RuleEngine.h"
#include "store/MinuteRec.h"

namespace upj {

// ---------------------------------------------------------------------------------------------- multi-path PATCH
class Patch {
 public:
  Patch(char* buf, size_t cap);
  void add(const char* path, const char* json);  // "path":json
  size_t close();                                 // returns the body length (0 if it overflowed)
  bool ok() const { return ok_; }
  int count() const { return n_; }
  size_t len() const { return len_; }
  size_t room() const { return cap_ > len_ + 2 ? cap_ - len_ - 2 : 0; }

 private:
  char* b_;
  size_t cap_, len_ = 0;
  int n_ = 0;
  bool ok_ = true, closed_ = false;
};

// ---------------------------------------------------------------------------------------------- events (r8 §3.6-3.7)
size_t eventKey(const AlertEvent& e, char* out, size_t n);  // "<10-digit ts>-<rid|TYPE>-<o|c|r|i>"
// channels used for the `ch` field: "ntfy", "tg", "email" joined by ',' (<= 16 chars), "log" if none
void channels(const AlertEvent& e, bool ntfyOn, bool tgOn, char* out, size_t n);
size_t eventJson(const AlertEvent& e, const char* ch, char* out, size_t n);
size_t mailqJson(const AlertEvent& e, const char* dateKey, char* out, size_t n);
// {"events/<d>/<k>":{...}[,"mailq/<k>":{d,ts,sev[,rc]}]} - one request, so the mailer never sees a job
// without its event. Returns the length (0 on overflow); *mailq = true when the outbox entry is included.
size_t eventPatch(const AlertEvent& e, const char* ch, char* out, size_t n, bool* mailq = nullptr);
bool wantsRtdbWrite(const AlertEvent& e);  // repeats are written only when they carry em:"q" (r8 §4.9)

// ---------------------------------------------------------------------------------------------- alertState / status
size_t alertStateJson(const AlertStateOut& o, char* out, size_t n);  // "null" for a removed slot

struct StatusDoc {
  uint32_t ts = 0;
  const char* fw = "";
  char ip[16] = "";
  int rssi = 0;
  uint32_t heap = 0, psram = 0, up = 0;
  const char* rr = "";
  long q = -1;
  char proto[24] = "", dia[10] = "";
  uint32_t hs = 0, rq = 0, lok = 0, cok = 0;
  double arv = 0;         // r8: loaded alertsRev (ms)
  int acat = 1, mjq = 0, nrul = 0;
  uint32_t edrop = 0;     // FW-2: events dropped after permanent RTDB errors
  int qg = 0;             // FW-2: quota guard level 0/1/2
  char ruleErr[41] = "";  // r8 §4.9: last alerts.json problem ("" omitted)
};
size_t statusJson(const StatusDoc& s, char* out, size_t n);

// ---------------------------------------------------------------------------------------------- URLs
// "/devices/<id>/<path>.json?auth=<tok>[&print=silent][&<extra>]"  (path "" = the device root)
size_t rtdbPath(const char* path, const char* idToken, bool silent, const char* extraQuery, char* out, size_t n);
// https://<db>.asia-southeast1.firebasedatabase.app[/...] -> host. Only Firebase RTDB hosts are accepted because
// include/roots.h pins Google's roots for them.
bool dbHostFromUrl(const char* url, char* host, size_t n);
size_t urlEncode(const char* s, char* out, size_t n);
// Healthchecks: "<uuid>" or "https://hc-ping.com/<uuid|key/slug>" -> "/<...>[/fail]"
bool hcPath(const char* cfg, bool fail, char* path, size_t n);
// v1.1 mailer nudge: only https://script.google.com/macros/s/<id>/exec?k=... is accepted -> path+query
bool nudgePath(const char* url, char* path, size_t n);

// ---------------------------------------------------------------------------------------------- parsers
struct CfgDoc {
  AlertCfg c;
  double alertsRev = 0;
  bool hasRev = false;
};
bool parseCfg(const char* json, size_t n, CfgDoc& out);  // `null` -> defaults; false only on bad JSON
// Telegram getUpdates reply -> distinct chat ids that may pair (auto-pairing, 0.7.7): any PRIVATE chat that sent a
// message, or a group/supergroup whose message starts with "/start" (an explicit request). Oldest first, at most maxOut.
// maxUpdateId gets the highest update_id seen (for the acknowledging offset), -1 if none. Returns the count.
int tgPairChats(const char* json, size_t n, char (*out)[24], int maxOut, long long* maxUpdateId);

struct AuthResult {
  char idToken[2048] = "";
  char refreshToken[1024] = "";
  uint32_t expiresIn = 0;
  char uid[64] = "";
};
bool parseSignIn(const char* json, size_t n, AuthResult& r);   // idToken refreshToken expiresIn localId
bool parseRefresh(const char* json, size_t n, AuthResult& r);  // id_token refresh_token expires_in user_id
bool parseApiError(const char* json, size_t n, char* msg, size_t mn);  // {"error":{"message":"..."}} | {"error":"..."}

int parseShallowDates(const char* json, size_t n, int32_t* days, int max);  // {"2026-10-21":true,...}
int parseDailyRange(const char* json, size_t n, mrec::Daily* out, int max);  // {"2026-10-21":{epv..n},...}

}  // namespace upj
