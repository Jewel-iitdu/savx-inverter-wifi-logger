// LocalWeb.h - http://solar.local : local UI (data/www/index.html), JSON API and ElegantOTA (sync mode).
// Routes (admin = HTTP Basic "admin:<password>", checked against the SHA-256 hash in NVS):
//   GET  /                      local UI                        GET  /api/live      latest Sample (+age_ms)
//   GET  /api/status            link/engine/system status       GET  /api/identity  meta (QPI, QVFW, ratings...)
//   GET  /api/raw               last raw reply per command      GET  /api/ring?min=1440&step=60  24 h chart data
//   GET  /api/hist?date=YYYY-MM-DD  minute records of a day     GET  /api/days      stored days + upload cursors
//   GET  /api/probe             last probe report               POST /api/probe     re-run probe (admin)
//   GET  /api/diag/pins (admin) self-ID / idle results / arm readiness
//   GET|POST /api/diag/selftest (admin)    GET|POST /api/diag/idle?pin=1|2 (admin)
//   POST /api/diag/loopback?bench=1 (admin)  POST /api/link/arm pin=1|2&confirm=ARM (admin)
//   POST /api/link/reset [all=1] (admin)     POST /api/capture on=1|0 (admin)
//   GET|POST /api/config (admin; also nudge / appUrl / ntfyTopic / hcUrl, write-only)
//   POST /api/reboot (admin)  POST /api/safemode/exit (admin)  GET /update (admin)
// /api/status also carries the uplink state (upState, upErr, qg, backlog, edrop, notify{...}) and
// rules:{n,rev,cloudRev,acat,email,mjq,err,st:[{rid,st,cnt}]} (r8 §4.1).
#pragma once

class LocalWeb {
 public:
  void begin();
  void stop();  // the WiFiManager portal needs port 80
  void loop();

 private:
  bool routes_ = false, running_ = false, mdns_ = false;
};

extern LocalWeb localWeb;
