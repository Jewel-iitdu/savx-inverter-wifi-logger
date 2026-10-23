// main.cpp - Savx F4 WiFi datalogger (ESP32-S3-N16R8). Boot, safe-mode check, task creation (d1 §5.3).
//
// SAFETY SUMMARY (d1 §3.10, §8.4; user constraints):
//  * Read-only: INV_ALLOW_WRITES=0, exact-match whitelist in SerialLink, zero bytes sent on refusal.
//  * A fresh device never transmits on GPIO17: it stays a high-Z input until the idle-level test has found the
//    inverter TX pin and the user explicitly arms the link (local UI Diagnostics page or console "arm <pin> YES").
//  * Self-ID, the idle-level tests (incl. the 3V3 upper check) and the pre-arm check only READ pins. Arming also
//    needs the self-ID and a passing pre-arm check (GPIO17 LOW with pull-down, RO LOW). Only COM RJ45 pins 1, 2
//    and 8 are ever wired; never pin 4, never the BMS jack.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>

#include <esp_heap_caps.h>

#include "app/App.h"
#include "app/Console.h"
#include "config.h"
#include "meter/Meter.h"
#include "model/JsonAlloc.h"
#include "net/TimeSync.h"
#include "net/Uplink.h"
#include "net/WifiProv.h"
#include "store/MinuteQueue.h"
#include "store/Settings.h"
#include "sys/SafeMode.h"
#include "sys/StatusLed.h"
#include "sys/Watchdog.h"
#include "web/LocalWeb.h"

static uint32_t bootPressedAt = 0;

// ArduinoJson documents of cloud responses (alerts.json ~12 KB transient, shallow key lists) live in PSRAM (r8 §4.12)
struct PsramJsonAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
  }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
  }
};
static PsramJsonAllocator psramJson;
static uint32_t lastSampleSeen = 0;

static void portalStarted() {
  localWeb.stop();
  xEventGroupSetBits(app.events, EV_PORTAL);
}
static void portalStopped() {
  localWeb.begin();
  xEventGroupClearBits(app.events, EV_PORTAL);
}

void setup() {
  // SAFETY (review): put the link pins into a safe state before anything else (the 300 ms delay, the LittleFS
  // mount or a first-boot format and the Wi-Fi start can take seconds). GPIO17 high-Z; both module header pins
  // pulled up, which is harmless on RO and keeps an auto-direction module's transmitter off (DI HIGH).
  pinMode(cfg::PIN_INV_TX, INPUT);
  pinMode(cfg::PIN_MOD_HDR_TXD, INPUT_PULLUP);
  pinMode(cfg::PIN_MOD_HDR_RXD, INPUT_PULLUP);
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n\n=== %s %s  (Savx F4 3KW logger, read-only) ===\n", FW_NAME, FW_VERSION);
  app.begin();
  if (ESP.getPsramSize()) jsonalloc::set(&psramJson);
  snprintf(app.resetReason, sizeof app.resetReason, "%s", watchdog::resetReasonName());
  settings.begin();
  {
    // ...and once the self-ID is known, drive the DI pin HIGH right away (invTask re-applies it later).
    LinkSettings early;
    settings.loadLink(early);
    if (early.prof.mode == LinkMode::RS232_LITE && early.prof.diConfirmed) driveHighNoGlitch(early.prof.diPin);
  }
  app.bootId = settings.nextBootId();
  bool safe = safemode::evaluateAtBoot();
  app.safeMode = safe;
  if (safe) xEventGroupSetBits(app.events, EV_SAFE_MODE);
  Serial.printf("boot #%u reset=%s bootFails=%u%s\n", app.bootId, app.resetReason, safemode::bootFails(),
                safe ? "  ** SAFE MODE: no inverter TX, no cloud **" : "");

  watchdog::begin(cfg::WDT_TIMEOUT_S);
  esp_task_wdt_add(nullptr);  // loopTask
  led::begin();
  led::set(safe ? led::Mode::SAFE : led::Mode::BOOT);
  pinMode(cfg::PIN_BOOT, INPUT_PULLUP);

  Serial.printf("PSRAM %u bytes, flash %u bytes\n", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFlashChipSize());
  if (!minuteQueue.begin(app.bootId, settings.lastEpoch())) Serial.println(F("!! LittleFS mount failed"));

  // Start the Wi-Fi driver (and with it the lwIP/TCP-IP stack) before anything opens a socket or reads the MAC:
  // WebServer::begin() on an uninitialised stack asserts in sys_mutex_lock (seen on first hardware boot), and the
  // MAC reads as a placeholder until the driver runs. WifiProv::begin() sets the same mode again; that is harmless.
  WiFi.mode(WIFI_STA);

  String apPass = settings.apPassword();
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);  // factory MAC from eFuse; WiFi.macAddress() can still read 0 this early
  char apName[32];
  snprintf(apName, sizeof apName, AP_NAME_PREFIX "%02X%02X", mac[4], mac[5]);
  Serial.printf("Setup Wi-Fi AP: %s  password: %s\n", apName, apPass.c_str());
  if (settings.adminIsDefault()) Serial.println(F("Local admin password: same as the AP password (change it!)"));

#ifdef WIFI_TX_POWER_CAP
  WiFi.setTxPower(WIFI_POWER_15dBm);  // only if brownouts appear (d1 §5.9)
#endif
  localWeb.begin();
  wifiProv.begin(String(apName), apPass, portalStarted, portalStopped);
  timeSync.begin();

  startInvTask();
  startAggTask();
  if (!safe) {
    uplink::begin();
    startUplinkTask();
  }
  meter::begin(safe);  // 0.9.0 grid meter (own low-priority task; off in safe mode)
  console::begin();
}

void loop() {
  esp_task_wdt_reset();
  uint32_t now = millis();
  wifiProv.loop();
  localWeb.loop();
  console::loop();
  safemode::loop(now);

  // BOOT (GPIO0) held 3 s while running -> provisioning portal (10 min)
  if (digitalRead(cfg::PIN_BOOT) == LOW) {
    if (!bootPressedAt) bootPressedAt = now ? now : 1;
    else if (now - bootPressedAt >= cfg::BOOT_HOLD_MS && !wifiProv.portalActive()) {
      Serial.println(F("[boot] BOOT held 3 s: opening the portal"));
      wifiProv.openPortal(cfg::PORTAL_TIMEOUT_S);
    }
  } else {
    bootPressedAt = 0;
  }

  bool wifi = wifiProv.wifiUp();
  if (wifi) xEventGroupSetBits(app.events, EV_WIFI_UP);
  else xEventGroupClearBits(app.events, EV_WIFI_UP);

  // LED
  LinkSnapshot l = app.link();
  UplinkStatus up = app.uplinkStatus();
  if (app.lastValidSampleMs != lastSampleSeen) {
    lastSampleSeen = app.lastValidSampleMs;
    led::pulseSample();
  }
  if (app.safeMode) led::set(led::Mode::SAFE);
  else if (wifiProv.portalActive()) led::set(led::Mode::PORTAL);
  else if (l.state == EngineState::PROBING || l.state == EngineState::LIGHT) led::set(led::Mode::PROBING);
  else if (l.state == EngineState::RUN && !l.linkOk) led::set(led::Mode::LINK_LOST);
  else if (!wifi) led::set(led::Mode::WIFI_DOWN);
  else if (up.backfilling) led::set(led::Mode::BACKFILL);
  else led::set(led::Mode::LINK_OK);
  led::loop(now);
  delay(2);
}
