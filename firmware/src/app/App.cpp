// App.cpp - see App.h.
#include "app/App.h"

App app;
AlertQueue alertQueue;

void App::begin() {
  events = xEventGroupCreate();
  qDiag_ = xQueueCreate(4, sizeof(DiagRequest));
  qRaise_ = xQueueCreate(8, sizeof(RaiseReq));
  static RtosLock alertLock;
  alertQueue.setLock(&alertLock);
  rulesPending_ = psramNew<rules::Set>();
  mtdDays_ = psramNew<mrec::Daily>(31);
}

void App::setLatest(const Sample& s) {
  LockGuard g(&lock_);
  // keep the energy / queue fields maintained by aggTask
  float epv = latest_.epv, eld = latest_.eld, ech = latest_.ech, edis = latest_.edis, egr = latest_.egr;
  int32_t q = latest_.q;
  latest_ = s;
  latest_.epv = epv;
  latest_.eld = eld;
  latest_.ech = ech;
  latest_.edis = edis;
  latest_.egr = egr;
  latest_.q = q;
  latestMs_ = millis();
  haveLatest_ = true;
  lastValidSampleMs = latestMs_;
}

Sample App::latest(uint32_t* ageMs) {
  LockGuard g(&lock_);
  Sample s = latest_;
  uint32_t age = haveLatest_ ? millis() - latestMs_ : 0xFFFFFFFF;
  if (ageMs) *ageMs = age;
  if (!haveLatest_ || age > cfg::LINK_OK_WINDOW_MS) s.lk = 0;
  return s;
}

void App::setEnergy(float epv, float eld, float ech, float edis, float egr, int32_t q) {
  LockGuard g(&lock_);
  latest_.epv = epv;
  latest_.eld = eld;
  latest_.ech = ech;
  latest_.edis = edis;
  latest_.egr = egr;
  latest_.q = q;
}

void App::setIdentity(const Identity& id) { LockGuard g(&lock_); ident_ = id; }
Identity App::identity() { LockGuard g(&lock_); return ident_; }
void App::setLink(const LinkSnapshot& l) { LockGuard g(&lock_); link_ = l; }
LinkSnapshot App::link() { LockGuard g(&lock_); return link_; }

bool App::postDiag(const DiagRequest& r) { return qDiag_ && xQueueSend(qDiag_, &r, 0) == pdTRUE; }
bool App::takeDiag(DiagRequest& r) { return qDiag_ && xQueueReceive(qDiag_, &r, 0) == pdTRUE; }
DiagState App::diag() { LockGuard g(&lock_); return diag_; }

void App::setProbeReport(const String& json) { LockGuard g(&lock_); probeReport_ = json; }
String App::probeReport() { LockGuard g(&lock_); return probeReport_; }

void App::pushH15(const mrec::H15& h) {
  LockGuard g(&lock_);
  if (h15n_ == 4) {
    for (int i = 1; i < 4; i++) h15_[i - 1] = h15_[i];
    h15n_ = 3;
  }
  h15_[h15n_++] = h;
}

bool App::takeH15(mrec::H15& h) {
  LockGuard g(&lock_);
  if (!h15n_) return false;
  h = h15_[0];
  for (int i = 1; i < h15n_; i++) h15_[i - 1] = h15_[i];
  h15n_--;
  return true;
}

void App::setDaily(const mrec::Daily& d) { LockGuard g(&lock_); daily_ = d; }
mrec::Daily App::daily() { LockGuard g(&lock_); return daily_; }

void App::applyCloudCfg(const AlertCfg& c) {
  LockGuard g(&lock_);
  cfg_ = c;
  cfgPending_ = true;
}

bool App::takeCfg(AlertCfg& c) {
  LockGuard g(&lock_);
  if (!cfgPending_) return false;
  cfgPending_ = false;
  c = cfg_;
  return true;
}

void App::raise(const char* type, uint8_t sev, const char* msg, bool force) {
  if (!qRaise_) return;
  RaiseReq r;
  snprintf(r.type, sizeof r.type, "%s", type);
  r.sev = sev;
  r.force = force;
  snprintf(r.msg, sizeof r.msg, "%s", msg);
  xQueueSend(qRaise_, &r, 0);
}

bool App::takeRaise(RaiseReq& r) { return qRaise_ && xQueueReceive(qRaise_, &r, 0) == pdTRUE; }

void App::setUplinkStatus(const UplinkStatus& s) { LockGuard g(&lock_); up_ = s; }
UplinkStatus App::uplinkStatus() { LockGuard g(&lock_); return up_; }

int App::copyLastReplies(LastReply* out, int max) {
  if (!raw_) return 0;
  LockGuard g(raw_->lockPtr());
  int n = raw_->lastCount();
  if (n > max) n = max;
  for (int i = 0; i < n; i++) out[i] = raw_->last(i);
  return n;
}

int App::copyExchanges(RawExchange* out, int max) {
  if (!raw_) return 0;
  LockGuard g(raw_->lockPtr());
  int n = raw_->count();
  if (n > max) n = max;
  for (int i = 0; i < n; i++) out[i] = raw_->at(i);
  return n;
}

// ------------------------------------------------------------------------------------------- rules (r8)
void App::applyRules(const rules::Set& s) {
  LockGuard g(&lock_);
  if (!rulesPending_) return;
  *rulesPending_ = s;
  rulesHave_ = true;
}

bool App::takeRules(rules::Set& out) {
  LockGuard g(&lock_);
  if (!rulesHave_ || !rulesPending_) return false;
  rulesHave_ = false;
  out = *rulesPending_;
  return true;
}

void App::setRuleInfo(const RuleInfo& r) { LockGuard g(&lock_); ruleInfo_ = r; }
RuleInfo App::ruleInfo() { LockGuard g(&lock_); return ruleInfo_; }

void App::putAlertState(const AlertStateOut& o) {
  int slot = rules::slotFromRid(o.rid);
  if (slot < 0) return;
  LockGuard g(&lock_);
  states_[slot] = o;
  stateDirty_ |= (1u << slot);
}

int App::takeAlertStates(AlertStateOut* out, int max) {
  LockGuard g(&lock_);
  int n = 0;
  for (int i = 0; i < cfg::MAX_RULES && n < max; i++)
    if (stateDirty_ & (1u << i)) {
      out[n++] = states_[i];
      stateDirty_ &= ~(1u << i);
    }
  return n;
}

void App::returnAlertStates(const AlertStateOut* in, int n) {
  LockGuard g(&lock_);
  for (int k = 0; k < n; k++) {
    int slot = rules::slotFromRid(in[k].rid);
    if (slot >= 0 && !(stateDirty_ & (1u << slot))) stateDirty_ |= (1u << slot);  // states_[slot] is the newest
  }
}

void App::setMtdNeed(int32_t firstDay, int32_t lastDay) {
  LockGuard g(&lock_);
  mtdFirst_ = firstDay;
  mtdLast_ = lastDay;
}

bool App::mtdNeed(int32_t& firstDay, int32_t& lastDay) {
  LockGuard g(&lock_);
  firstDay = mtdFirst_;
  lastDay = mtdLast_;
  return mtdFirst_ != 0 && !mtdAnswered_;
}

void App::postMtdDays(const mrec::Daily* d, int n) {
  LockGuard g(&lock_);
  if (n > 31) n = 31;
  if (!mtdDays_) n = 0;
  for (int i = 0; i < n; i++) mtdDays_[i] = d[i];
  mtdN_ = n;
  mtdAnswered_ = true;
}

int App::takeMtdDays(mrec::Daily* out, int max, bool* answered) {
  LockGuard g(&lock_);
  if (answered) *answered = mtdAnswered_;
  if (!mtdAnswered_) return 0;
  int n = mtdN_ < max ? mtdN_ : max;
  for (int i = 0; i < n; i++) out[i] = mtdDays_[i];
  mtdN_ = 0;
  mtdAnswered_ = false;
  mtdFirst_ = mtdLast_ = 0;
  return n;
}

void App::pushFinishedDay(const mrec::Daily& d) {
  LockGuard g(&lock_);
  finished_ = d;
  finishedHave_ = true;
}

bool App::takeFinishedDay(mrec::Daily& d) {
  LockGuard g(&lock_);
  if (!finishedHave_) return false;
  finishedHave_ = false;
  d = finished_;
  return true;
}
