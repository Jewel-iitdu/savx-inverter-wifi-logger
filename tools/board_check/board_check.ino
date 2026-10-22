// Savx datalogger — ESP32-S3-N16R8 bench check (Arduino IDE).
// Not connected to the inverter. Confirms the board, flash, PSRAM, and 2.4 GHz WiFi signal.
// Open Serial Monitor at 115200 baud.

#include <WiFi.h>
#include <esp_chip_info.h>

static void scanWifi() {
  Serial.println("\nScanning 2.4 GHz WiFi...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  no networks found");
    return;
  }
  for (int i = 0; i < n; i++) {
    Serial.printf("  %2d  %-32s  ch %2d  RSSI %4d dBm%s\n", i + 1, WiFi.SSID(i).c_str(),
                  WiFi.channel(i), WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  (open)" : "");
  }
  Serial.println("  RSSI guide: > -67 great, -67..-75 OK, < -75 move the router or the logger.");
  WiFi.scanDelete();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== Savx datalogger: ESP32-S3 board check ===");

  esp_chip_info_t ci;
  esp_chip_info(&ci);
  Serial.printf("Chip:       %s rev %d, %d cores\n", ESP.getChipModel(), ci.revision, ci.cores);
  Serial.printf("Flash size: %u MB   (expect 16)\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("PSRAM size: %u KB (expect ~8192; 0 means PSRAM setting is wrong)\n",
                (unsigned)(ESP.getPsramSize() / 1024));
  Serial.printf("Free heap:  %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024));
  WiFi.mode(WIFI_STA);  // start WiFi first; before this the MAC reads as all zeros
  WiFi.disconnect();
  delay(100);
  Serial.printf("MAC:        %s\n", WiFi.macAddress().c_str());
  scanWifi();
}

void loop() {
  // Rescan every 20 s so you can carry the board (on a laptop running on battery) to where the logger will live.
  static uint32_t lastScan = 0;
  static uint32_t lastBlink = 0;
  static bool on = false;

  if (millis() - lastScan > 20000) {
    lastScan = millis();
    scanWifi();
  }

  if (millis() - lastBlink > 500) {
    lastBlink = millis();
    on = !on;
#ifdef RGB_BUILTIN
    rgbLedWrite(RGB_BUILTIN, 0, on ? 20 : 0, 0);  // dim green blink, if the clone wires the RGB LED
#endif
  }
}
