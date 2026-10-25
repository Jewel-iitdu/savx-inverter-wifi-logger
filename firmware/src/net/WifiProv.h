// WifiProv.h - WiFiManager 2.0.17 captive portal (non-blocking) + custom credential fields (d1 §5.7).
// The portal opens ONLY when no Wi-Fi credentials are saved, or on demand (BOOT held 3 s / console "portal").
// It never opens just because the router is down (load-shedding): logging must continue.
#pragma once
#include <Arduino.h>

// Custom portal fields. Secrets are shown empty: leave blank to keep the stored value, "-" to clear it.
// FW-2 reads them from NVS with settings.getStr(ns, key).
struct ProvParamDef {
  const char* id;     // HTML id
  const char* label;
  const char* ns;     // NVS namespace
  const char* key;    // NVS key
  uint16_t maxLen;
  bool secret;
};
extern const ProvParamDef PROV_PARAMS[];
extern const size_t PROV_PARAMS_N;

class WifiProv {
 public:
  typedef void (*Hook)();
  void begin(const String& apName, const String& apPass, Hook onPortalStart, Hook onPortalStop);
  void loop();
  void openPortal(uint32_t timeoutS);
  bool portalActive() const { return portal_; }
  bool wifiUp() const;
  bool everConnected() const { return everUp_; }
  const String& apName() const { return apName_; }
  uint8_t lastDisconnectReason() const;  // 1.0.2: last STA_DISCONNECTED reason (outage log)

 private:
  void saveParams();
  String apName_, apPass_;
  Hook onStart_ = nullptr, onStop_ = nullptr;
  bool portal_ = false;
  bool everUp_ = false;
};

extern WifiProv wifiProv;
