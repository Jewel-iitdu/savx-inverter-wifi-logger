// NotifyGates.h - shared notification gates for AlertEngine and RuleEngine (r8 §4.1, §4.6; d1 §7.3). Host-portable.
//   * push priority: quiet hours (sev <= 3 and clears are log-only), global 40 pushes/day (above it only sev 5);
//   * device e-mail job cap: <= MAIL_JOBS_PER_DAY (40) mailq jobs per local day, the last MAIL_RESERVE_SEV4 (5)
//     kept for severity >= 4. The counter is persisted (NVS rules/mj) through IGateStore so reboots cannot reset it.
// Both engines run in aggTask, so no locking is needed; jobsToday() is read by other tasks as a plain int.
#pragma once
#include <stdint.h>

#include "config.h"

class IGateStore {
 public:
  virtual ~IGateStore() {}
  virtual void saveJobs(int32_t day, uint16_t cnt) = 0;
  virtual void saveOutage(uint32_t openEpoch) { (void)openEpoch; }  // 0.7.9: open grid outage (0 = none)
};

class NotifyGates {
 public:
  void setQuiet(int from, int to) { qFrom_ = from; qTo_ = to; }
  void setStore(IGateStore* s) { store_ = s; }
  IGateStore* store() const { return store_; }
  void restoreJobs(int32_t day, uint16_t cnt) { jobDay_ = day; jobs_ = cnt; }

  bool quiet(uint32_t epoch) const;
  // ntfy priority 1..5, or 0 = log only. Counts the push when it returns > 0 (force: TEST_ALERT bypasses gates).
  uint8_t pushPrio(uint8_t sev, bool clear, uint32_t epoch, bool force);
  // true = one more mailq job allowed now (the counter is incremented and persisted).
  bool takeMailJob(uint8_t sev, uint32_t epoch);
  bool mailJobAvailable(uint8_t sev, uint32_t epoch);

  int pushesToday() const { return pushes_; }
  int jobsToday() const { return jobs_; }
  int32_t jobDay() const { return jobDay_; }

 private:
  void roll(uint32_t epoch);
  int qFrom_ = 22, qTo_ = 7;
  int32_t pushDay_ = 0, jobDay_ = 0;
  int pushes_ = 0;
  uint16_t jobs_ = 0;
  IGateStore* store_ = nullptr;
};
