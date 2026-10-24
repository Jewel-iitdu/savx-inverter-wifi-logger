// WifiProv.cpp - see WifiProv.h.
#include "net/WifiProv.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "net/Budget.h"
#include "net/Uplink.h"
#include "store/Settings.h"

WifiProv wifiProv;

const ProvParamDef PROV_PARAMS[] = {
    {"adminpw", "Admin password for the local UI / OTA (min 6 chars; blank = keep)", "sys", "adminHash", 64, true},
    {"fbkey", "Firebase Web API key", "cloud", "apiKey", 64, false},
    {"fbdb", "Firebase RTDB URL (https://...asia-southeast1.firebasedatabase.app)", "cloud", "dbUrl", 128, false},
    {"fbmail", "Firebase device user e-mail", "cloud", "email", 64, false},
    {"fbpass", "Firebase device user password (blank = keep)", "cloud", "pass", 64, true},
    {"ntfy", "ntfy topic, >= 24 random chars (blank = keep)", "alert", "ntfyTopic", 64, true},
    {"tgtok", "Telegram bot token (optional; blank = keep)", "alert", "tgToken", 64, true},
    {"tgchat", "Telegram chat id (optional: leave blank and send /start to your bot to pair)", "alert", "tgChat", 24, false},
    {"hc", "Healthchecks ping URL or UUID (optional)", "alert", "hcUrl", 128, false},
    {"appurl", "Dashboard URL for notification links (optional)", "alert", "appUrl", 128, false},
    {"mailurl", "Mailer nudge URL incl. ?k= (optional; blank = keep)", "mail", "nudge", 200, true},
    {"smtpsrv", "E-mail alerts: SMTP server, e.g. mail.example.com:465 (SSL/TLS only)", "smtp", "server", 80, false},
    {"smtpusr", "E-mail alerts: SMTP login = sender address", "smtp", "user", 80, false},
    {"smtppw", "E-mail alerts: SMTP password (blank = keep)", "smtp", "pass", 80, true},
};
const size_t PROV_PARAMS_N = sizeof(PROV_PARAMS) / sizeof(PROV_PARAMS[0]);

static WiFiManager wm;
static WifiWatchdog wifiWd;  // 0.9.9, see Budget.h
static WiFiManagerParameter* params[sizeof(PROV_PARAMS) / sizeof(PROV_PARAMS[0])];
static String defaults[sizeof(PROV_PARAMS) / sizeof(PROV_PARAMS[0])];

bool WifiProv::wifiUp() const { return WiFi.status() == WL_CONNECTED; }

void WifiProv::saveParams() {
  for (size_t i = 0; i < PROV_PARAMS_N; i++) {
    const ProvParamDef& d = PROV_PARAMS[i];
    String v = params[i]->getValue();
    v.trim();
    if (d.secret && v.length() == 0) continue;  // keep
    if (v == "-") v = "";                         // clear
    if (!strcmp(d.key, "adminHash")) {
      if (v.length() >= 6) settings.setAdminPassword(v);
      continue;
    }
    settings.setStr(d.ns, d.key, v);
    if (!strcmp(d.ns, "cloud") && (!strcmp(d.key, "email") || !strcmp(d.key, "pass") || !strcmp(d.key, "apiKey")))
      settings.setStr("cloud", "rtok", "");  // credentials changed: force a fresh sign-in (FW-2)
  }
  uplink::notifyConfigChanged();  // the uplink re-reads credentials / channels from NVS
  Serial.println(F("[prov] portal fields saved to NVS"));
}

void WifiProv::begin(const String& apName, const String& apPass, Hook onPortalStart, Hook onPortalStop) {
  apName_ = apName;
  apPass_ = apPass;
  onStart_ = onPortalStart;
  onStop_ = onPortalStop;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOST);
  WiFi.setAutoReconnect(true);
  wm.setDebugOutput(false);
  wm.setConfigPortalBlocking(false);
  wm.setTitle("Savx logger");
  wm.setHostname(MDNS_HOST);
  wm.setDarkMode(true);
  wm.setShowInfoUpdate(false);  // OTA lives at http://solar.local/update (admin)
  // WiFiManager's default menu ends with "update", and it registers /update and /u (firmware upload) with no
  // password, reachable from the AP and, for an on-demand portal, from the home LAN. Firmware may only be
  // replaced through the admin-protected ElegantOTA, so drop the menu entry and shadow both routes: handlers
  // added in the web-server callback run before WiFiManager adds its own, and the first match wins.
  static std::vector<const char*> menu = {"wifi", "info", "exit"};
  wm.setMenu(menu);
  wm.setWebServerCallback([]() {
    wm.server->on("/update", HTTP_ANY, []() {
      wm.server->send(403, "text/plain", "Firmware update is only at http://solar.local/update (admin password).");
    });
    wm.server->on("/u", HTTP_ANY, []() { wm.server->send(403, "text/plain", "forbidden"); }, []() {});
  });
  for (size_t i = 0; i < PROV_PARAMS_N; i++) {
    const ProvParamDef& d = PROV_PARAMS[i];
    defaults[i] = d.secret ? String("") : settings.getStr(d.ns, d.key, "");
    params[i] = new WiFiManagerParameter(d.id, d.label, defaults[i].c_str(), d.maxLen,
                                         d.secret ? " type='password' autocomplete='off'" : "");
    wm.addParameter(params[i]);
  }
  wm.setSaveParamsCallback([this]() { saveParams(); });
  if (!wm.getWiFiIsSaved()) {
    Serial.println(F("[prov] no Wi-Fi credentials saved: opening the setup portal"));
    openPortal(0);  // first boot: stays open until configured
  } else {
    WiFi.begin();  // stored credentials; reconnects automatically, the portal does NOT auto-open
  }
}

void WifiProv::openPortal(uint32_t timeoutS) {
  if (portal_) return;
  if (onStart_) onStart_();  // stop our web server: the portal needs port 80
  // Re-read the non-secret fields: /api/config may have changed hcUrl / appUrl since boot, and saving the
  // portal must not write stale boot-time values back.
  for (size_t i = 0; i < PROV_PARAMS_N; i++) {
    const ProvParamDef& d = PROV_PARAMS[i];
    if (d.secret) continue;
    defaults[i] = settings.getStr(d.ns, d.key, "");
    params[i]->setValue(defaults[i].c_str(), d.maxLen);
  }
  wm.setConfigPortalTimeout(timeoutS);
  portal_ = true;
  wm.startConfigPortal(apName_.c_str(), apPass_.c_str());
  Serial.printf("[prov] portal open: join Wi-Fi '%s' (password printed at boot), then open http://192.168.4.1\n",
                apName_.c_str());
}

void WifiProv::loop() {
  if (portal_) {
    wm.process();
    if (!wm.getConfigPortalActive()) {
      portal_ = false;
      Serial.println(F("[prov] portal closed"));
      if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
      if (WiFi.status() != WL_CONNECTED) WiFi.begin();
      if (onStop_) onStop_();
    }
  }
  if (WiFi.status() == WL_CONNECTED) everUp_ = true;
  uint32_t now = millis();
  switch (wifiWd.step(now, WiFi.status() == WL_CONNECTED, portal_)) {
    case WifiWatchdog::Action::RECONNECT:
      Serial.printf("[wifi] down %lu s (status %d): reconnecting\n", (unsigned long)(wifiWd.downMs(now) / 1000),
                    (int)WiFi.status());
      WiFi.disconnect(false, false);  // keeps the radio on and the saved network
      WiFi.reconnect();
      break;
    case WifiWatchdog::Action::RESTART: {
      char cause[40];
      snprintf(cause, sizeof cause, "no Wi-Fi for %lu min", (unsigned long)(wifiWd.downMs(now) / 60000));
      Serial.printf("[wifi] %s - rebooting\n", cause);
      settings.setStr("sys", "rbCause", cause);
      delay(200);
      ESP.restart();
      break;
    }
    default:
      break;
  }
}
