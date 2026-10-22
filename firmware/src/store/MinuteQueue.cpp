// MinuteQueue.cpp - see MinuteQueue.h.
#include "store/MinuteQueue.h"

#include <LittleFS.h>

#include "agg/AlertEngine.h"
#include "model/TimeUtil.h"

MinuteQueue minuteQueue;

namespace {
struct ProvHeader {
  uint32_t magic;           // 'PROV'
  uint16_t bootId;
  uint16_t pad;
  uint32_t lastEpochAtBoot; // NVS lastEpoch read at boot: estimate base if this boot never syncs
};
constexpr uint32_t PROV_MAGIC = 0x564F5250;
struct Anchor { uint16_t bootId; uint16_t pad; uint32_t upMs; uint32_t epoch; };
}  // namespace

void MinuteQueue::dayPath(int32_t day, const char* ext, char* out, size_t n) {
  char d[12];
  tu::dateKeyFromDay(day, d, sizeof d);
  snprintf(out, n, "/q/%s.%s", d, ext);
}

bool MinuteQueue::begin(uint16_t bootId, uint32_t lastEpochAtBoot) {
  LockGuard g(&lock_);
  bootId_ = bootId;
  lastEpochAtBoot_ = lastEpochAtBoot;
  mounted_ = LittleFS.begin(true);  // formats the "spiffs" partition only if it cannot be mounted
  if (!mounted_) return false;
  if (!LittleFS.exists("/q")) LittleFS.mkdir("/q");
  if (!LittleFS.exists("/log")) LittleFS.mkdir("/log");
  if (!LittleFS.exists("/cfg")) LittleFS.mkdir("/cfg");  // /cfg/alerts.json (r8 §4.8)
  rescanPending();
  return true;
}

bool MinuteQueue::appendTo(const char* path, const MinuteRec& r) {
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return false;
  size_t w = f.write((const uint8_t*)&r, sizeof r);
  f.close();
  return w == sizeof r;
}

bool MinuteQueue::append(const MinuteRec& r) {
  LockGuard g(&lock_);
  if (!mounted_) return false;
  char path[32];
  if (r.ts == 0) {
    snprintf(path, sizeof path, "/q/p%u.bin", (unsigned)bootId_);
    if (!LittleFS.exists(path)) {
      File f = LittleFS.open(path, FILE_WRITE);
      if (!f) return false;
      ProvHeader h{PROV_MAGIC, bootId_, 0, lastEpochAtBoot_};
      f.write((const uint8_t*)&h, sizeof h);
      f.close();
    }
    bool ok = appendTo(path, r);
    if (ok) { prov_++; pending_++; }
    return ok;
  }
  dayPath(tu::localDayNumber(r.ts), "bin", path, sizeof path);
  bool ok = appendTo(path, r);
  if (ok) pending_++;
  return ok;
}

void MinuteQueue::onTimeValid(uint32_t upMs, uint32_t epoch) {
  LockGuard g(&lock_);
  if (!mounted_) return;
  {
    File a = LittleFS.open("/q/anchors.bin", FILE_APPEND);
    if (a) {
      Anchor an{bootId_, 0, upMs, epoch};
      a.write((const uint8_t*)&an, sizeof an);
      a.close();
    }
  }
  // move every provisional file into date files
  File dir = LittleFS.open("/q");
  if (!dir) return;
  char names[16][24];
  int nn = 0;
  for (File f = dir.openNextFile(); f && nn < 16; f = dir.openNextFile()) {
    const char* nm = f.name();
    if (nm[0] == 'p' && strstr(nm, ".bin")) snprintf(names[nn++], sizeof names[0], "%s", nm);
    f.close();
  }
  dir.close();
  for (int i = 0; i < nn; i++) {
    char path[32];
    snprintf(path, sizeof path, "/q/%s", names[i]);
    File f = LittleFS.open(path, FILE_READ);
    if (!f) continue;
    ProvHeader h;
    if (f.read((uint8_t*)&h, sizeof h) != sizeof h || h.magic != PROV_MAGIC) {
      f.close();
      LittleFS.remove(path);
      continue;
    }
    MinuteRec r;
    while (f.read((uint8_t*)&r, sizeof r) == sizeof r) {
      if (!mrec::valid(r)) continue;
      if (h.bootId == bootId_) {
        mrec::fixupTs(r, bootId_, upMs, epoch);  // exact: same boot, anchored
      } else if (h.lastEpochAtBoot) {
        r.ts = h.lastEpochAtBoot + r.upMs / 1000;  // estimate (d1 §5.6), keeps the provisional flag -> "tq":0
        r.ts -= r.ts % 60;
        mrec::seal(r);
      } else {
        continue;  // no basis for a timestamp at all: drop
      }
      char dp[32];
      dayPath(tu::localDayNumber(r.ts), "bin", dp, sizeof dp);
      appendTo(dp, r);
    }
    f.close();
    LittleFS.remove(path);
  }
  rescanPending();
}

int MinuteQueue::listDays(int32_t* days, int max) {
  LockGuard g(&lock_);
  if (!mounted_) return 0;
  int n = 0;
  File dir = LittleFS.open("/q");
  if (!dir) return 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* nm = f.name();
    int32_t d;
    if (strlen(nm) == 14 && !strcmp(nm + 10, ".bin") && tu::parseDateKey(nm, d) && n < max) days[n++] = d;
    f.close();
  }
  dir.close();
  for (int i = 1; i < n; i++)  // insertion sort
    for (int j = i; j > 0 && days[j - 1] > days[j]; j--) { int32_t t = days[j]; days[j] = days[j - 1]; days[j - 1] = t; }
  return n;
}

uint32_t MinuteQueue::count(int32_t day) {
  LockGuard g(&lock_);
  char p[32];
  dayPath(day, "bin", p, sizeof p);
  if (!mounted_ || !LittleFS.exists(p)) return 0;
  File f = LittleFS.open(p, FILE_READ);
  uint32_t n = f ? (uint32_t)(f.size() / sizeof(MinuteRec)) : 0;
  if (f) f.close();
  return n;
}

uint32_t MinuteQueue::cursor(int32_t day) {
  LockGuard g(&lock_);
  char p[32];
  dayPath(day, "up", p, sizeof p);
  if (!mounted_ || !LittleFS.exists(p)) return 0;
  File f = LittleFS.open(p, FILE_READ);
  uint32_t c = 0;
  if (f) {
    if (f.read((uint8_t*)&c, sizeof c) != sizeof c) c = 0;
    f.close();
  }
  return c;
}

bool MinuteQueue::setCursor(int32_t day, uint32_t n) {
  LockGuard g(&lock_);
  char p[32];
  dayPath(day, "up", p, sizeof p);
  File f = LittleFS.open(p, FILE_WRITE);
  if (!f) return false;
  f.write((const uint8_t*)&n, sizeof n);
  f.close();
  rescanPending();
  return true;
}

int MinuteQueue::read(int32_t day, uint32_t from, MinuteRec* out, int max) {
  LockGuard g(&lock_);
  char p[32];
  dayPath(day, "bin", p, sizeof p);
  if (!mounted_ || !LittleFS.exists(p)) return 0;
  File f = LittleFS.open(p, FILE_READ);
  if (!f) return 0;
  if (!f.seek(from * sizeof(MinuteRec))) {
    f.close();
    return 0;
  }
  int n = 0;
  while (n < max && f.read((uint8_t*)&out[n], sizeof(MinuteRec)) == sizeof(MinuteRec)) n++;
  f.close();
  return n;
}

uint32_t MinuteQueue::pending() {
  LockGuard g(&lock_);
  return pending_;
}

uint32_t MinuteQueue::provisional() {
  LockGuard g(&lock_);
  return prov_;
}

void MinuteQueue::rescanPending() {
  pending_ = prov_ = 0;
  if (!mounted_) return;
  File dir = LittleFS.open("/q");
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* nm = f.name();
    int32_t d;
    if (strlen(nm) == 14 && !strcmp(nm + 10, ".bin") && tu::parseDateKey(nm, d)) {
      uint32_t n = (uint32_t)(f.size() / sizeof(MinuteRec));
      char up[32];
      dayPath(d, "up", up, sizeof up);
      uint32_t c = 0;
      File u = LittleFS.open(up, FILE_READ);
      if (u) {
        if (u.read((uint8_t*)&c, sizeof c) != sizeof c) c = 0;
        u.close();
      }
      pending_ += n > c ? n - c : 0;
    } else if (nm[0] == 'p' && strstr(nm, ".bin") && f.size() > sizeof(ProvHeader)) {
      uint32_t n = (uint32_t)((f.size() - sizeof(ProvHeader)) / sizeof(MinuteRec));
      prov_ += n;
      pending_ += n;
    }
    f.close();
  }
  dir.close();
}

bool MinuteQueue::recomputeDay(int32_t day, mrec::Daily& d) {
  mrec::dailyReset(d, day);
  MinuteRec buf[32];
  uint32_t from = 0;
  bool any = false;
  for (;;) {
    int n = read(day, from, buf, 32);
    if (n <= 0) break;
    for (int i = 0; i < n; i++)
      if (mrec::valid(buf[i])) { mrec::dailyAdd(d, buf[i]); any = true; }
    from += (uint32_t)n;
  }
  return any;
}

void MinuteQueue::housekeeping(int32_t today, AlertEngine* alerts, uint32_t nowMs, uint32_t epoch) {
  int32_t days[64];
  int n = listDays(days, 64);
  for (int i = 0; i < n; i++) {
    bool uploaded = cursor(days[i]) >= count(days[i]);
    bool old = days[i] < today - cfg::KEEP_UPLOADED_DAYS;
    if (uploaded && old) {
      LockGuard g(&lock_);
      char p[32];
      dayPath(days[i], "bin", p, sizeof p);
      LittleFS.remove(p);
      dayPath(days[i], "up", p, sizeof p);
      LittleFS.remove(p);
    }
  }
  // low space: drop the oldest uploaded day first, then the oldest unuploaded day (never today)
  for (int guard = 0; guard < 8; guard++) {
    size_t total = totalBytes(), used = usedBytes();
    if (total == 0 || total - used >= cfg::FS_MIN_FREE_BYTES) break;
    n = listDays(days, 64);
    int victim = -1;
    for (int i = 0; i < n && victim < 0; i++)
      if (days[i] != today && cursor(days[i]) >= count(days[i])) victim = i;
    bool unuploaded = false;
    if (victim < 0 && n > 0 && days[0] != today) {
      victim = 0;
      unuploaded = true;
    }
    if (victim < 0) break;
    char p[32], d[12];
    tu::dateKeyFromDay(days[victim], d, sizeof d);
    uint32_t lost = unuploaded ? count(days[victim]) - cursor(days[victim]) : 0;
    {
      LockGuard g(&lock_);
      dayPath(days[victim], "bin", p, sizeof p);
      LittleFS.remove(p);
      dayPath(days[victim], "up", p, sizeof p);
      LittleFS.remove(p);
      rescanPending();
    }
    if (unuploaded && alerts) {
      char msg[120];
      snprintf(msg, sizeof msg, "Flash full: dropped %lu un-uploaded minute records of %s", (unsigned long)lost, d);
      alerts->raise("DATA_DROPPED", 2, msg, nowMs, epoch);
    }
  }
  // rotate raw capture logs
  {
    LockGuard g(&lock_);
    if (LittleFS.exists("/log/raw-0.txt")) {
      File f = LittleFS.open("/log/raw-0.txt", FILE_READ);
      size_t sz = f ? f.size() : 0;
      if (f) f.close();
      if (sz > 128 * 1024) {
        LittleFS.remove("/log/raw-1.txt");
        LittleFS.rename("/log/raw-0.txt", "/log/raw-1.txt");
      }
    }
  }
}

bool MinuteQueue::writeFile(const char* path, const char* data, size_t n) {
  LockGuard g(&lock_);
  if (!mounted_) return false;
  File f = LittleFS.open(path, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write((const uint8_t*)data, n);
  f.close();
  return w == n;
}

bool MinuteQueue::appendLine(const char* path, const char* line, size_t maxSize) {
  LockGuard g(&lock_);
  if (!mounted_) return false;
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) return false;
  if (f.size() > maxSize) {
    f.close();
    return false;
  }
  f.print(line);
  f.print("\n");
  f.close();
  return true;
}

size_t MinuteQueue::readFile(const char* path, char* out, size_t cap) {
  LockGuard g(&lock_);
  if (!mounted_ || !LittleFS.exists(path) || cap == 0) return 0;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  size_t n = f.read((uint8_t*)out, cap - 1);
  out[n] = 0;
  f.close();
  return n;
}

size_t MinuteQueue::usedBytes() {
  LockGuard g(&lock_);
  return mounted_ ? LittleFS.usedBytes() : 0;
}

size_t MinuteQueue::totalBytes() {
  LockGuard g(&lock_);
  return mounted_ ? LittleFS.totalBytes() : 0;
}
