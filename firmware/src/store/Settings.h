// Settings.h - NVS (Preferences) namespaces (d1 §5.12). Thread-safe (one mutex around every access).
//   net    : host                                       (WiFiManager keeps its own Wi-Fi credentials)
//   cloud  : apiKey dbUrl email pass rtok uid            (FW-2 uplink)
//   alert  : ntfyTopic tgToken tgChat hcUrl + cached cloud cfg thresholds (cfg blob)
//   link   : mode baud rxInv txInv roPin diPin diOk invTxRj adapter dialect pv12 supMask unsupMask aflags
//            armed armRx lastProbe idle(blob)
//   energy : day epv eld ech edis egr nf nw epvi lastEpoch  (5-min checkpoint; today's file is the truth)
//   sys    : bootId bootFails safe adminHash adminDef apPass fwPrev nwdEp
//   rules  : rs01..rs16 (RulePersist, 28 B) mj ({day,cnt} mail jobs) rev (double alertsRev)   (r8 §4.12)
//   mail   : nudge (v1.1 mailer nudge URL, secret)                                           (r8 §4.12)
//   energy : + mtd (MtdBlob, billing-cycle sums)                                              (r8 §4.5)
#pragma once
#include <Arduino.h>

#include "agg/AlertEngine.h"
#include "hal/ArduinoHal.h"
#include "link/LinkProfile.h"
#include "link/PinDiag.h"
#include "proto/Adapter.h"
#include "store/MinuteRec.h"

struct LinkSettings {
  LinkProfile prof;
  AdapterState st;
  bool armed = false;
  uint8_t armRx = 0;       // RJ45 pin the user confirmed GPIO17 is wired to
  uint32_t lastProbe = 0;  // epoch of the last full probe
  IdleHistory idle;
};

class Settings {
 public:
  void begin();

  // link
  void loadLink(LinkSettings& out);
  void saveLinkState(const LinkProfile& p, const AdapterState& st, uint32_t probeEpoch);
  void saveArm(bool armed, uint8_t rxRj, uint8_t txRj, LinkMode mode);
  void saveSelfId(uint8_t roPin, uint8_t diPin);
  void saveIdle(const IdleHistory& h);
  void clearLink(bool keepSelfId);

  // cloud cfg thresholds cache
  void loadCfg(AlertCfg& c);
  void saveCfg(const AlertCfg& c);

  // generic strings (credentials). Secrets are never echoed by the local API.
  String getStr(const char* ns, const char* key, const char* def = "");
  void setStr(const char* ns, const char* key, const String& v);
  bool hasStr(const char* ns, const char* key);

  // fixed-size binary records (rule persistence, mail-job counter, month-to-date sums)
  bool getBlob(const char* ns, const char* key, void* out, size_t len);  // false if absent or a different size
  void putBlob(const char* ns, const char* key, const void* data, size_t len);
  void removeKey(const char* ns, const char* key);

  // energy checkpoint
  void saveEnergy(const mrec::Daily& d, uint32_t epoch);
  bool loadEnergy(mrec::Daily& d, uint32_t& lastEpoch);
  uint32_t lastEpoch();

  // sys
  uint16_t nextBootId();
  uint16_t bootId() const { return bootId_; }
  uint8_t bootFails();
  void setBootFails(uint8_t n);
  bool safeFlag();
  void setSafeFlag(bool on);
  String apPassword();                  // random per device, generated on first boot
  bool adminCheck(const String& pw);    // SHA-256 compare
  void setAdminPassword(const String& pw);
  bool adminIsDefault();                // still equal to the AP password

  static String sha256hex(const String& s);

 private:
  RtosLock lock_;
  uint16_t bootId_ = 0;
};

extern Settings settings;
