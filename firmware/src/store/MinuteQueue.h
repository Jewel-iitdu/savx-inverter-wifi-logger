// MinuteQueue.h - LittleFS append-only minute files + upload cursors + time anchors (d1 §5.5-5.6).
//   /q/YYYY-MM-DD.bin   MinuteRec (55 B) append-only      /q/YYYY-MM-DD.up  uint32 cursor (records uploaded)
//   /q/p<bootId>.bin    provisional records of a boot that had no time yet (header + MinuteRec...)
//   /q/anchors.bin      {bootId, upMs, epoch} written at the first NTP sync of each boot
//   /log/probe.json     last probe report        /log/raw-*.txt  raw capture (rotated, <= 256 KB)
//   /cfg/alerts.json    last accepted alerts.json (usage-threshold rules, r8 §4.8), loaded before the network
// Thread-safe (one mutex around all FS access). The uplink (FW-2) reads with read()/cursor()/setCursor().
#pragma once
#include <Arduino.h>

#include "hal/ArduinoHal.h"
#include "store/MinuteRec.h"

class AlertEngine;

class MinuteQueue {
 public:
  bool begin(uint16_t bootId, uint32_t lastEpochAtBoot);
  bool mounted() const { return mounted_; }

  bool append(const MinuteRec& r);  // r.ts == 0 -> provisional file of this boot
  // First NTP sync of this boot: anchor + move provisional records into their date files.
  void onTimeValid(uint32_t upMs, uint32_t epoch);

  int listDays(int32_t* days, int max);  // ascending local day numbers with a .bin file
  uint32_t count(int32_t day);
  uint32_t cursor(int32_t day);
  bool setCursor(int32_t day, uint32_t n);
  int read(int32_t day, uint32_t from, MinuteRec* out, int max);
  uint32_t pending();       // records not yet uploaded (cached)
  uint32_t provisional();   // records still waiting for a timestamp
  bool recomputeDay(int32_t day, mrec::Daily& d);

  // Retention: keep uploaded days KEEP_UPLOADED_DAYS; when space is low drop the oldest uploaded day, then
  // the oldest unuploaded one (raises DATA_DROPPED via the alert engine).
  void housekeeping(int32_t today, AlertEngine* alerts, uint32_t nowMs, uint32_t epoch);

  bool writeFile(const char* path, const char* data, size_t n);
  bool appendLine(const char* path, const char* line, size_t maxSize);
  size_t readFile(const char* path, char* out, size_t cap);
  size_t usedBytes();
  size_t totalBytes();

  static void dayPath(int32_t day, const char* ext, char* out, size_t n);

 private:
  void rescanPending();
  bool appendTo(const char* path, const MinuteRec& r);
  RtosLock lock_;
  bool mounted_ = false;
  uint16_t bootId_ = 0;
  uint32_t lastEpochAtBoot_ = 0;
  uint32_t pending_ = 0, prov_ = 0;
};

extern MinuteQueue minuteQueue;
