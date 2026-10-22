// Settings.cpp - see Settings.h.
#include "store/Settings.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

Settings settings;

void Settings::begin() {
  // Create every namespace once (read-write open). Read-only opens of a namespace that does not exist yet fail
  // with "nvs_open failed: NOT_FOUND" and log an error on every call on a fresh or erased board.
  static const char* const kNamespaces[] = {"link", "alert", "energy", "sys", "cloud", "mail", "rules"};
  for (const char* ns : kNamespaces) {
    Preferences p;
    if (p.begin(ns, false)) p.end();
  }
}

String Settings::sha256hex(const String& s) {
  uint8_t h[32];
  mbedtls_sha256((const unsigned char*)s.c_str(), s.length(), h, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
  return String(hex);
}

void Settings::loadLink(LinkSettings& o) {
  LockGuard g(&lock_);
  Preferences p;
  o = LinkSettings();
  if (!p.begin("link", true)) return;
  o.prof.mode = (LinkMode)p.getUChar("mode", (uint8_t)LinkMode::RS232_LITE);
  o.prof.baud = p.getULong("baud", cfg::BAUD_PI30);
  o.prof.rxInvert = p.getBool("rxInv", true);
  o.prof.txInvert = p.getBool("txInv", true);
  o.prof.roPin = p.getUChar("roPin", cfg::PIN_MOD_HDR_RXD);
  o.prof.diPin = p.getUChar("diPin", cfg::PIN_MOD_HDR_TXD);
  o.prof.diConfirmed = p.getBool("diOk", false);
  o.prof.txPin = cfg::PIN_INV_TX;
  o.prof.invTxRjPin = p.getUChar("invTxRj", 0);
  if (p.isKey("adapter")) p.getString("adapter", o.st.adapter, sizeof o.st.adapter);
  if (p.isKey("dialect")) p.getString("dialect", o.st.dialect, sizeof o.st.dialect);
  o.st.pv12 = (char)p.getUChar("pv12", '?');
  o.st.supMask = p.getULong("supMask", 0);
  o.st.unsupMask = p.getULong("unsupMask", 0);
  o.st.flags = p.getUChar("aflags", 0);
  o.armed = p.getBool("armed", false);
  o.armRx = p.getUChar("armRx", 0);
  o.lastProbe = p.getULong("lastProbe", 0);
  if (p.isKey("idle") && p.getBytesLength("idle") == sizeof(IdleHistory)) p.getBytes("idle", &o.idle, sizeof(IdleHistory));
  p.end();
  // sanity: RO/DI can only be the two module header pins (16, 18), never equal, never GPIO17. A corrupted NVS value
  // must not make the firmware drive some other GPIO (strapping, flash, USB) HIGH (safety review).
  auto hdr = [](uint8_t g) { return g == cfg::PIN_MOD_HDR_RXD || g == cfg::PIN_MOD_HDR_TXD; };
  if (!hdr(o.prof.roPin) || !hdr(o.prof.diPin) || o.prof.roPin == o.prof.diPin) {
    o.prof.roPin = cfg::PIN_MOD_HDR_RXD;
    o.prof.diPin = cfg::PIN_MOD_HDR_TXD;
    o.prof.diConfirmed = false;
  }
}

void Settings::saveLinkState(const LinkProfile& pr, const AdapterState& st, uint32_t probeEpoch) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("link", false)) return;
  p.putUChar("mode", (uint8_t)pr.mode);
  p.putULong("baud", pr.baud);
  p.putBool("rxInv", pr.rxInvert);
  p.putBool("txInv", pr.txInvert);
  p.putString("adapter", st.adapter);
  p.putString("dialect", st.dialect);
  p.putUChar("pv12", (uint8_t)st.pv12);
  p.putULong("supMask", st.supMask);
  p.putULong("unsupMask", st.unsupMask);
  p.putUChar("aflags", st.flags);
  if (probeEpoch) p.putULong("lastProbe", probeEpoch);
  p.end();
}

void Settings::saveArm(bool armed, uint8_t rxRj, uint8_t txRj, LinkMode mode) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("link", false)) return;
  p.putBool("armed", armed);
  p.putUChar("armRx", rxRj);
  p.putUChar("invTxRj", txRj);
  p.putUChar("mode", (uint8_t)mode);
  p.end();
}

void Settings::saveSelfId(uint8_t roPin, uint8_t diPin) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("link", false)) return;
  p.putUChar("roPin", roPin);
  p.putUChar("diPin", diPin);
  p.putBool("diOk", true);
  p.end();
}

void Settings::saveIdle(const IdleHistory& h) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("link", false)) return;
  p.putBytes("idle", &h, sizeof h);
  p.end();
}

void Settings::clearLink(bool keepSelfId) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("link", false)) return;
  uint8_t ro = p.getUChar("roPin", cfg::PIN_MOD_HDR_RXD), di = p.getUChar("diPin", cfg::PIN_MOD_HDR_TXD);
  bool ok = p.getBool("diOk", false);
  p.clear();
  if (keepSelfId && ok) {
    p.putUChar("roPin", ro);
    p.putUChar("diPin", di);
    p.putBool("diOk", true);
  }
  p.end();
}

void Settings::loadCfg(AlertCfg& c) {
  LockGuard g(&lock_);
  Preferences p;
  c = AlertCfg();
  if (!p.begin("alert", true)) return;
  // fields are only ever appended (0.7.9: gridOffFrom/To), so an older, shorter blob loads as a prefix
  size_t n = p.isKey("cfg") ? p.getBytesLength("cfg") : 0;
  if (n > 0 && n <= sizeof(AlertCfg)) p.getBytes("cfg", &c, n);
  p.end();
}

void Settings::saveCfg(const AlertCfg& c) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("alert", false)) return;
  p.putBytes("cfg", &c, sizeof c);
  p.end();
}

String Settings::getStr(const char* ns, const char* key, const char* def) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin(ns, true)) return String(def);
  String v = p.isKey(key) ? p.getString(key, def) : String(def);
  p.end();
  return v;
}

void Settings::setStr(const char* ns, const char* key, const String& v) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin(ns, false)) return;
  if (v.length()) p.putString(key, v);
  else if (p.isKey(key)) p.remove(key);
  p.end();
}

bool Settings::hasStr(const char* ns, const char* key) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin(ns, true)) return false;
  bool k = p.isKey(key);
  p.end();
  return k;
}

bool Settings::getBlob(const char* ns, const char* key, void* out, size_t len) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin(ns, true)) return false;
  bool ok = p.isKey(key) && p.getBytesLength(key) == len && p.getBytes(key, out, len) == len;
  p.end();
  return ok;
}

void Settings::putBlob(const char* ns, const char* key, const void* data, size_t len) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin(ns, false)) return;
  p.putBytes(key, data, len);
  p.end();
}

void Settings::removeKey(const char* ns, const char* key) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin(ns, false)) return;
  if (p.isKey(key)) p.remove(key);
  p.end();
}

void Settings::saveEnergy(const mrec::Daily& d, uint32_t epoch) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("energy", false)) return;
  p.putLong("day", d.day);
  p.putDouble("epv", d.epv);
  p.putDouble("eld", d.eld);
  p.putDouble("ech", d.ech);
  p.putDouble("edis", d.edis);
  p.putDouble("egr", d.egr);
  p.putInt("nf", d.nf);
  p.putInt("nw", d.nw);
  p.putFloat("epvi", d.epvi);
  if (epoch) p.putULong("lastEpoch", epoch);
  p.end();
}

bool Settings::loadEnergy(mrec::Daily& d, uint32_t& lastEpoch) {
  LockGuard g(&lock_);
  Preferences p;
  lastEpoch = 0;
  if (!p.begin("energy", true)) return false;
  bool ok = p.isKey("day");
  if (ok) {
    mrec::dailyReset(d, p.getLong("day", 0));
    d.epv = p.getDouble("epv", 0);
    d.eld = p.getDouble("eld", 0);
    d.ech = p.getDouble("ech", 0);
    d.edis = p.getDouble("edis", 0);
    d.egr = p.getDouble("egr", 0);
    d.nf = p.getInt("nf", 0);
    d.nw = p.getInt("nw", 0);
    d.epvi = p.getFloat("epvi", -1);
  }
  lastEpoch = p.getULong("lastEpoch", 0);
  p.end();
  return ok;
}

uint32_t Settings::lastEpoch() {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("energy", true)) return 0;
  uint32_t e = p.getULong("lastEpoch", 0);
  p.end();
  return e;
}

uint16_t Settings::nextBootId() {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", false)) return 0;
  bootId_ = (uint16_t)(p.getUShort("bootId", 0) + 1);
  if (bootId_ == 0) bootId_ = 1;
  p.putUShort("bootId", bootId_);
  p.end();
  return bootId_;
}

uint8_t Settings::bootFails() {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", true)) return 0;
  uint8_t n = p.getUChar("bootFails", 0);
  p.end();
  return n;
}

void Settings::setBootFails(uint8_t n) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", false)) return;
  p.putUChar("bootFails", n);
  p.end();
}

bool Settings::safeFlag() {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", true)) return false;
  bool s = p.getBool("safe", false);
  p.end();
  return s;
}

void Settings::setSafeFlag(bool on) {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", false)) return;
  p.putBool("safe", on);
  p.end();
}

String Settings::apPassword() {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", false)) return String("savxlogger");
  String pw = p.getString("apPass", "");
  if (pw.length() < 8) {
#ifdef AP_PASSWORD
    pw = AP_PASSWORD;
#else
    static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/O/1/l/i
    pw = "";
    for (int i = 0; i < 10; i++) pw += A[esp_random() % (sizeof(A) - 1)];
#endif
    p.putString("apPass", pw);
  }
  if (!p.isKey("adminHash")) {  // the admin password defaults to the AP password
    p.putString("adminHash", sha256hex(pw));
    p.putBool("adminDef", true);
  }
  p.end();
  return pw;
}

bool Settings::adminCheck(const String& pw) {
  if (!pw.length()) return false;
  String h = getStr("sys", "adminHash", "");
  String c = sha256hex(pw);
  if (h.length() != c.length()) return false;
  uint8_t diff = 0;  // constant-time compare
  for (size_t i = 0; i < h.length(); i++) diff |= (uint8_t)(h[i] ^ c[i]);
  return diff == 0;
}

void Settings::setAdminPassword(const String& pw) {
  if (pw.length() < 6) return;
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", false)) return;
  p.putString("adminHash", sha256hex(pw));
  p.putBool("adminDef", false);
  p.end();
}

bool Settings::adminIsDefault() {
  LockGuard g(&lock_);
  Preferences p;
  if (!p.begin("sys", true)) return true;
  bool d = p.getBool("adminDef", true);
  p.end();
  return d;
}
