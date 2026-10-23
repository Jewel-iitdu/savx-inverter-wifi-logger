// Meter.cpp - see Meter.h.
#include "meter/Meter.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <math.h>
#include <string.h>

#include <initializer_list>

#include "app/App.h"
#include "config.h"
#include "meter/MeterCrypto.h"
#include "meter/MeterDay.h"
#include "meter/TuyaProto.h"
#include "model/TimeUtil.h"
#include "net/TimeSync.h"
#include "store/Settings.h"

namespace meter {
namespace {

#pragma pack(push, 1)
struct CfgBlob {  // NVS "meter"/"cfg" (layout fixed; append only)
  uint8_t ver = 1;
  char id[32] = "";
  char key[17] = "";
  uint8_t proto = 0;    // 0 = auto, else 33 / 34 / 35
  uint32_t ip = 0;      // last address that answered with our key (IPAddress as uint32)
  uint8_t protoOk = 0;  // last protocol that worked
};
#pragma pack(pop)

enum class R : uint8_t { OK, NOCONN, NOREPLY, BADKEY };

RtosLock lock_;
CfgBlob cfg_;
Snap snap_;
MeterDay day_;
MbedCrypto crypto_;
uint8_t rx_[tuya::MAX_FRAME + 100], tx_[512];
TaskHandle_t task_ = nullptr;
volatile bool enabled_ = true, guardStop_ = false, safe_ = false;
uint8_t crashes_ = 0;
uint32_t lastOkMs_ = 0, lastDaySaveMs_ = 0, phaseMs_ = 0;
bool finHave_ = false;
int32_t finDay_ = 0, finWh_ = -1;

bool configured() { return cfg_.id[0] && strlen(cfg_.key) == 16; }

void setState(const char* s) {
  LockGuard g(&lock_);
  snprintf(snap_.state, sizeof snap_.state, "%s", s);
}

void saveCfg() { settings.putBlob("meter", "cfg", &cfg_, sizeof cfg_); }
void saveCrashes() { settings.putBlob("meter", "cg", &crashes_, 1); }
void saveDay() {
  MeterDayState s;
  {
    LockGuard g(&lock_);
    s = day_.state();
  }
  settings.putBlob("meter", "day", &s, sizeof s);
  lastDaySaveMs_ = millis();
}

// Reads frames until on(frame) returns true (true) or the time runs out / the device closes (false).
// bad counts frames that failed their checksum or decryption: a wrong key or protocol.
template <typename F>
bool readUntil(WiFiClient& c, tuya::Session& s, size_t& have, uint32_t ms, uint32_t& bad, F on) {
  uint32_t t0 = millis();
  for (;;) {
    while (have) {
      tuya::Frame f;
      size_t used = 0;
      tuya::Parse p = s.parse(rx_, have, f, used);
      if (p == tuya::Parse::MORE) {
        if (used > sizeof rx_) have = 0;  // larger than any status frame: drop it
        break;
      }
      if (p == tuya::Parse::SKIP) used = 1;
      if (p == tuya::Parse::BAD) bad++;
      bool done = p == tuya::Parse::OK && on(f);  // f points into rx_: use it before the move
      memmove(rx_, rx_ + used, have - used);
      have -= used;
      if (done) return true;
    }
    if (millis() - t0 > ms) return false;
    int a = c.available();
    if (a <= 0) {
      if (!c.connected()) return false;
      delay(10);
      continue;
    }
    size_t room = sizeof rx_ - have;
    int r = c.read(rx_ + have, (size_t)a < room ? (size_t)a : room);
    if (r > 0) have += (size_t)r;
  }
}

R pollAt(IPAddress ip, int proto, MeterLive& out) {
  WiFiClient c;
  if (!c.connect(ip, cfg::METER_PORT, cfg::METER_CONNECT_MS)) return R::NOCONN;
  c.setNoDelay(true);
  tuya::Session s(crypto_);
  s.begin(proto, (const uint8_t*)cfg_.key, cfg_.id);
  size_t have = 0, n;
  uint32_t bad = 0;
  if (s.needsHandshake()) {
    n = s.buildStart(tx_, sizeof tx_);
    if (!n || c.write(tx_, n) != n) return R::NOREPLY;
    size_t fin = 0;
    bool got = readUntil(c, s, have, cfg::METER_REPLY_MS, bad, [&](const tuya::Frame& f) {
      if (f.cmd != tuya::CMD_SESS_RESP) return false;
      fin = s.onSessResp(f, tx_, sizeof tx_);
      return true;
    });
    if (!got) return bad ? R::BADKEY : R::NOREPLY;
    if (!fin) return R::BADKEY;
    if (c.write(tx_, fin) != fin) return R::NOREPLY;
  }
  n = s.buildQuery(tx_, sizeof tx_, timeSync.epochOrZero());
  if (!n || c.write(tx_, n) != n) return R::NOREPLY;
  MeterLive m;
  bool got = readUntil(c, s, have, cfg::METER_REPLY_MS, bad, [&](const tuya::Frame& f) {
    return f.n && meterDecodeDps((const char*)f.payload, f.n, m);
  });
  if (!got) {
    c.stop();
    return bad ? R::BADKEY : R::NOREPLY;
  }
  // V, A and W (phase_a) are not in this meter's status reply: ask for them (a refresh, nothing is changed)
  n = s.buildRefresh(tx_, sizeof tx_, cfg::METER_PHASE_DP);
  if (n && c.write(tx_, n) == n) {
    readUntil(c, s, have, cfg::METER_REPLY_MS, bad, [&](const tuya::Frame& f) {
      MeterLive t = m;
      if (!f.n || !meterDecodeDps((const char*)f.payload, f.n, t)) return false;
      m = t;
      return !isnan(m.w);
    });
  }
  c.stop();
  out = m;  // the counter etc. count even if the refresh went unanswered
  return R::OK;
}

// The configured protocol, or (auto) the last one that worked, then 3.5 / 3.4 / 3.3.
R pollDevice(IPAddress ip, MeterLive& m) {
  int list[4], k = 0;
  if (cfg_.proto) {
    list[k++] = cfg_.proto;
  } else {
    if (cfg_.protoOk) list[k++] = cfg_.protoOk;
    for (int p : {35, 34, 33})
      if (p != cfg_.protoOk) list[k++] = p;
  }
  R last = R::NOREPLY;
  for (int i = 0; i < k; i++) {
    last = pollAt(ip, list[i], m);
    if (last == R::OK) {
      if (cfg_.protoOk != list[i] || cfg_.ip != (uint32_t)ip) {
        cfg_.protoOk = (uint8_t)list[i];
        cfg_.ip = (uint32_t)ip;
        saveCfg();
      }
      LockGuard g(&lock_);
      snap_.proto = list[i];
      snprintf(snap_.ip, sizeof snap_.ip, "%s", ip.toString().c_str());
      return R::OK;
    }
    if (last == R::NOCONN) return last;  // nothing listening: no point in trying other protocols
  }
  return last;
}

// The meter is found by its key: every host in our /24 with port 6668 open gets a full poll.
bool scan(MeterLive& m) {
  IPAddress me = WiFi.localIP();
  if ((uint32_t)me == 0) return false;
  setState("searching the LAN for the meter");
  {
    LockGuard g(&lock_);
    snap_.scans++;
  }
  for (int h = 1; h < 255; h++) {
    if (!enabled_) return false;
    IPAddress ip(me[0], me[1], me[2], h);
    if (ip == me) continue;
    WiFiClient c;
    if (!c.connect(ip, cfg::METER_PORT, cfg::METER_SCAN_CONNECT_MS)) continue;
    c.stop();
    if (pollDevice(ip, m) == R::OK) {
      Serial.printf("[meter] found at %s\n", ip.toString().c_str());
      return true;
    }
  }
  return false;
}

void publish(MeterLive& m) {
  uint32_t ep = timeSync.epochOrZero();
  bool rolled = false, first = false;
  {
    LockGuard g(&lock_);
    if (m.haveCounter && tu::epochValid(ep)) {
      first = day_.state().day == 0;
      int32_t ld = tu::localDayNumber(ep);
      rolled = day_.update(ep, ld, m.counter);
      if (rolled) {
        finHave_ = true;
        finDay_ = day_.state().prevDay;
        finWh_ = day_.state().prevWh;
      }
      m.egm = day_.whFor(ld);
      snap_.resets = day_.resets();
    }
    if (!isnan(m.w)) {
      phaseMs_ = millis();
    } else if (phaseMs_ && millis() - phaseMs_ < cfg::METER_FRESH_MS) {  // refresh unanswered this time
      m.w = snap_.m.w;
      m.v = snap_.m.v;
      m.a = snap_.m.a;
    }
    snap_.m = m;
    snap_.polls++;
    lastOkMs_ = millis();
    snprintf(snap_.state, sizeof snap_.state, "ok");
  }
  if (rolled || first || millis() - lastDaySaveMs_ >= cfg::METER_DAY_SAVE_MS) saveDay();
}

void taskFn(void*) {
  if (!tuya::selfTest(crypto_)) {
    setState("crypto self-test failed");
    Serial.println(F("[meter] crypto self-test FAILED: meter off"));
    task_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  uint32_t lastScan = 0, scanEvery = 0;
  int fails = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(cfg::METER_POLL_MS));  // config changes wake it early
    if (!enabled_) {
      setState("off (app setting)");
      continue;
    }
    if (!configured()) {
      setState("not set up (/meter)");
      continue;
    }
    if (!(xEventGroupGetBits(app.events) & EV_WIFI_UP)) {
      setState("no Wi-Fi");
      continue;
    }
    if (crashes_ && millis() > cfg::METER_CRASH_CLEAR_MS) {  // healthy for hours: forget old crashes
      crashes_ = 0;
      saveCrashes();
    }
    MeterLive m;
    R r = cfg_.ip ? pollDevice(IPAddress(cfg_.ip), m) : R::NOCONN;
    if (r == R::OK) {
      fails = 0;
      scanEvery = 0;
      publish(m);
      continue;
    }
    fails++;
    {
      LockGuard g(&lock_);
      snap_.fails++;
    }
    setState(r == R::BADKEY ? "answers, but not to our key" : r == R::NOREPLY ? "no reply" : "not reachable");
    if (fails >= cfg::METER_FAILS_BEFORE_SCAN && (!lastScan || millis() - lastScan >= scanEvery)) {
      lastScan = millis();
      if (scan(m)) {
        fails = 0;
        scanEvery = 0;
        publish(m);
      } else {
        scanEvery = scanEvery ? scanEvery * 2 : cfg::METER_RESCAN_MIN_MS;
        if (scanEvery > cfg::METER_RESCAN_MAX_MS) scanEvery = cfg::METER_RESCAN_MAX_MS;
        setState("meter not found on the LAN");
      }
    }
  }
}

void startTask() {
  if (task_ || safe_ || guardStop_) return;
  xTaskCreatePinnedToCore(taskFn, "meterTask", cfg::STACK_METER, nullptr, cfg::PRIO_METER, &task_, cfg::CORE_METER);
}

void wake() {
  if (task_) xTaskNotifyGive(task_);
}

}  // namespace

void begin(bool safeMode) {
  safe_ = safeMode;
  if (!settings.getBlob("meter", "cfg", &cfg_, sizeof cfg_)) cfg_ = CfgBlob();
  MeterDayState ds;
  if (settings.getBlob("meter", "day", &ds, sizeof ds) && ds.ver == 1) day_.load(ds);
  if (!settings.getBlob("meter", "cg", &crashes_, 1)) crashes_ = 0;
  AlertCfg c;
  settings.loadCfg(c);
  enabled_ = c.meter != 0;
  if (safeMode) {
    setState("off (safe mode)");
    return;
  }
  // only software crashes count (not brown-outs: a power dip says nothing about the meter code)
  esp_reset_reason_t rr = esp_reset_reason();
  bool swCrash = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT;
  if (configured() && enabled_ && swCrash) {
    crashes_++;
    saveCrashes();
  }
  if (crashes_ >= cfg::METER_MAX_CRASHES) {
    guardStop_ = true;
    setState("stopped after crashes: toggle Grid meter");
    Serial.printf("[meter] %u crashes while the meter was on: meter reading stopped\n", crashes_);
    return;
  }
  setState(configured() ? "starting" : "not set up (/meter)");
  startTask();
}

Snap snap() {
  LockGuard g(&lock_);
  Snap s = snap_;
  s.configured = configured();
  s.enabled = enabled_;
  s.running = task_ != nullptr;
  s.ageS = lastOkMs_ ? (millis() - lastOkMs_) / 1000 : 0;
  s.fresh = lastOkMs_ && millis() - lastOkMs_ < cfg::METER_FRESH_MS;
  return s;
}

bool live(MeterLive& m) {
  LockGuard g(&lock_);
  if (!enabled_ || !lastOkMs_ || millis() - lastOkMs_ >= cfg::METER_FRESH_MS) return false;
  m = snap_.m;
  m.ageS = (int32_t)((millis() - lastOkMs_) / 1000);
  return true;
}

int32_t egmFor(int32_t day) {
  if (!enabled_) return -1;
  LockGuard g(&lock_);
  return day_.whFor(day);
}

bool takeFinishedDay(int32_t& day, int32_t& wh) {
  LockGuard g(&lock_);
  if (!finHave_) return false;
  finHave_ = false;
  if (!enabled_ || finWh_ < 0) return false;
  day = finDay_;
  wh = finWh_;
  return true;
}

void setEnabled(bool on) {
  bool was = enabled_;
  enabled_ = on;
  if (on && !was) {  // re-arm after a crash stop
    if (crashes_) {
      crashes_ = 0;
      saveCrashes();
    }
    guardStop_ = false;
    startTask();
  }
  if (on != was) wake();
}

bool saveConfig(const char* id, const char* key, int proto, const char* ip, char* err, size_t errN) {
  size_t il = strlen(id ? id : "");
  if (il < 10 || il >= sizeof cfg_.id) {
    snprintf(err, errN, "device id: 10-31 characters");
    return false;
  }
  for (size_t i = 0; i < il; i++)
    if (!isalnum((unsigned char)id[i])) {
      snprintf(err, errN, "device id: letters and digits only");
      return false;
    }
  if (strlen(key ? key : "") != 16) {
    snprintf(err, errN, "local key: exactly 16 characters");
    return false;
  }
  if (proto != 0 && proto != 33 && proto != 34 && proto != 35) {
    snprintf(err, errN, "protocol: auto, 3.3, 3.4 or 3.5");
    return false;
  }
  IPAddress a;
  if (ip && *ip && !a.fromString(ip)) {
    snprintf(err, errN, "IP address not valid");
    return false;
  }
  {
    LockGuard g(&lock_);
    if (strcmp(cfg_.id, id) != 0) {  // another meter: its counter starts a new history
      day_.load(MeterDayState());
      cfg_.protoOk = 0;
      cfg_.ip = 0;
    }
    snprintf(cfg_.id, sizeof cfg_.id, "%s", id);
    memcpy(cfg_.key, key, 16);
    cfg_.key[16] = 0;
    cfg_.proto = (uint8_t)proto;
    if (ip && *ip) cfg_.ip = (uint32_t)a;
  }
  saveCfg();
  saveDay();
  crashes_ = 0;
  saveCrashes();
  guardStop_ = false;
  startTask();
  wake();
  return true;
}

void clearConfig() {
  {
    LockGuard g(&lock_);
    cfg_ = CfgBlob();
    lastOkMs_ = 0;
  }
  saveCfg();
  wake();
}

}  // namespace meter
