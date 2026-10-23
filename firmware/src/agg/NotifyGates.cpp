// NotifyGates.cpp - see NotifyGates.h.
#include "agg/NotifyGates.h"

#include "model/TimeUtil.h"

bool NotifyGates::quiet(uint32_t epoch) const {
  if (!tu::epochValid(epoch)) return false;
  int h = (int)tu::localHour(epoch);
  if (qFrom_ == qTo_) return false;
  return qFrom_ < qTo_ ? (h >= qFrom_ && h < qTo_) : (h >= qFrom_ || h < qTo_);
}

void NotifyGates::roll(uint32_t epoch) {
  if (!tu::epochValid(epoch)) return;
  int32_t d = tu::localDayNumber(epoch);
  if (d != pushDay_) {
    pushDay_ = d;
    pushes_ = 0;
  }
  if (d != jobDay_) {
    jobDay_ = d;
    jobs_ = 0;
  }
}

uint8_t NotifyGates::pushPrio(uint8_t sev, bool clear, uint32_t epoch, bool force, bool noQuiet) {
  roll(epoch);
  uint8_t prio = sev >= 5 ? 5 : sev == 4 ? 4 : sev == 3 ? 3 : 2;
  if (clear && prio > 3) prio = 3;
  bool push = true;
  if (!force && !noQuiet && prio <= 3 && quiet(epoch)) push = false;           // quiet hours (sev <= 3, clears)
  if (!force && pushes_ >= cfg::PUSH_CAP_PER_DAY && sev < 5) push = false;     // global daily cap
  if (!push) return 0;
  pushes_++;
  return prio;
}

bool NotifyGates::mailJobAvailable(uint8_t sev, uint32_t epoch) {
  roll(epoch);
  if (jobs_ < cfg::MAIL_JOBS_PER_DAY - cfg::MAIL_RESERVE_SEV4) return true;
  return jobs_ < cfg::MAIL_JOBS_PER_DAY && sev >= 4;
}

bool NotifyGates::takeMailJob(uint8_t sev, uint32_t epoch) {
  if (!mailJobAvailable(sev, epoch)) return false;
  jobs_++;
  if (store_) store_->saveJobs(jobDay_, jobs_);
  return true;
}
