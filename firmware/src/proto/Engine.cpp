// Engine.cpp - see Engine.h.
#include "proto/Engine.h"

#include <string.h>

#include "model/Decode.h"

InverterAdapter* InverterEngine::adapterFor(AdapterKind k) {
  switch (k) {
    case AdapterKind::PI30: return &pi30_;
    case AdapterKind::Q6: return &q6_;
    case AdapterKind::SMG: return &smg_;
    case AdapterKind::POWMR: return &powmr_;
    case AdapterKind::RAW: return &raw_;
    default: return nullptr;
  }
}

void InverterEngine::begin(const LinkProfile& prof, const AdapterState& st, bool armed, bool safeMode, uint32_t nowMs) {
  base_ = prof;
  armed_ = armed && !safeMode;
  safe_ = safeMode;
  link_.setArmed(armed_);
  link_.configure(prof);
  kind_ = adapterKindFromId(st.adapter);
  adapter_ = adapterFor(kind_);
  hadGood_ = (kind_ != AdapterKind::NONE && kind_ != AdapterKind::RAW);
  if (adapter_ && kind_ != AdapterKind::RAW) adapter_->importState(st);
  if (safe_) {
    state_ = EngineState::SAFE;
  } else if (!armed_) {
    state_ = EngineState::DISARMED;
    adapter_ = &raw_;
    raw_.schedule(nowMs);
  } else if (adapter_ && kind_ != AdapterKind::RAW) {
    state_ = EngineState::LIGHT;
  } else {
    state_ = EngineState::PROBING;
  }
}

void InverterEngine::setBaseProfile(const LinkProfile& prof) {
  // keep the probed baud/inversion, take pins/mode/self-ID from prof
  LinkProfile p = link_.profile();
  p.mode = prof.mode;
  p.roPin = prof.roPin;
  p.diPin = prof.diPin;
  p.txPin = prof.txPin;
  p.diConfirmed = prof.diConfirmed;
  p.invTxRjPin = prof.invTxRjPin;
  base_.mode = prof.mode;
  base_.roPin = prof.roPin;
  base_.diPin = prof.diPin;
  base_.txPin = prof.txPin;
  base_.diConfirmed = prof.diConfirmed;
  base_.invTxRjPin = prof.invTxRjPin;
  link_.configure(p);
}

void InverterEngine::setArmed(bool armed, uint32_t nowMs) {
  armed_ = armed && !safe_;
  link_.setArmed(armed_);
  if (!armed_) {
    if (state_ != EngineState::SAFE) state_ = EngineState::DISARMED;
    adapter_ = &raw_;
    raw_.schedule(nowMs);
    return;
  }
  state_ = (kind_ != AdapterKind::NONE && kind_ != AdapterKind::RAW) ? EngineState::LIGHT : EngineState::PROBING;
}

void InverterEngine::identity(Identity& out) const {
  out = Identity();
  if (adapter_) adapter_->identity(out);
  link_.profile().describe(out.link, sizeof out.link);
}

bool InverterEngine::takePersist(LinkProfile& prof, AdapterState& st) {
  if (adapter_ && adapter_->stateDirty()) {
    persist_ = true;
    adapter_->clearDirty();
  }
  if (!persist_) return false;
  persist_ = false;
  prof = link_.profile();
  if (adapter_) adapter_->exportState(st);
  else st = AdapterState();
  return true;
}

void InverterEngine::enterRun(AdapterKind k, const LinkProfile& p, uint32_t nowMs) {
  kind_ = k;
  adapter_ = adapterFor(k);
  link_.configure(p);
  adapter_->schedule(nowMs);
  state_ = EngineState::RUN;
  stats_.consecutiveFails = 0;
  backoffIdx_ = 0;
  nextReprobeMs_ = 0;
  cur_ = Sample();
  persist_ = true;
  hadGood_ = true;
  rawBackoffIdx_ = 0;
}

void InverterEngine::enterRaw(uint32_t nowMs, uint32_t reprobeAfterMs) {
  kind_ = AdapterKind::RAW;
  adapter_ = &raw_;
  raw_.schedule(nowMs);
  state_ = EngineState::RAW;
  rawReprobeAtMs_ = nowMs + reprobeAfterMs;
  persist_ = true;
}

void InverterEngine::runFullProbe(uint32_t nowMs) {
  (void)nowMs;
  stats_.probes++;
  outcome_ = prober_.full(base_, log_);
  uint32_t now = link_.clock().ms();
  if (outcome_.ok) enterRun(outcome_.kind, outcome_.profile, now);
  else {
    link_.configure(outcome_.profile.baud ? outcome_.profile : base_);
    if (hadGood_) {
      // e.g. RJ45 unplugged or inverter off at boot: retry soon, and keep the known-good adapter persisted
      static const int NB = sizeof(cfg::REPROBE_BACKOFF_MS) / sizeof(cfg::REPROBE_BACKOFF_MS[0]);
      enterRaw(now, cfg::REPROBE_BACKOFF_MS[rawBackoffIdx_ < NB ? rawBackoffIdx_ : NB - 1]);
      if (rawBackoffIdx_ < NB - 1) rawBackoffIdx_++;
      persist_ = false;
    } else {
      enterRaw(now, cfg::RAW_REPROBE_MS);
    }
  }
}

bool InverterEngine::tick(uint32_t nowMs, uint32_t epoch, Sample& out) {
  if (state_ == EngineState::SAFE) return false;
  if (!armed_) {
    state_ = EngineState::DISARMED;
    raw_.step(link_, nowMs, cur_);  // receive-only
    return false;
  }
  if (probeRequested_) {
    probeRequested_ = false;
    state_ = EngineState::PROBING;
  }
  switch (state_) {
    case EngineState::LIGHT: {
      log_.clear();
      log_.setTimeBase(nowMs);
      bool ok = adapter_ && adapter_->lightProbe(link_, log_);
      if (ok) {
        uint32_t now = link_.clock().ms();
        LinkProfile p = link_.profile();
        enterRun(kind_, p, now);
        outcome_ = ProbeOutcome();
        outcome_.ran = true;
        outcome_.ok = true;
        outcome_.kind = kind_;
        outcome_.profile = p;
        snprintf(outcome_.summary, sizeof outcome_.summary, "light probe OK (%s)", adapter_->id());
        persist_ = false;  // nothing new
      } else {
        state_ = EngineState::PROBING;
      }
      return false;
    }
    case EngineState::PROBING:
      runFullProbe(nowMs);
      return false;
    case EngineState::RAW:
      if ((int32_t)(nowMs - rawReprobeAtMs_) >= 0) {
        state_ = EngineState::PROBING;
        return false;
      }
      raw_.step(link_, nowMs, cur_);
      return false;
    case EngineState::RUN:
      break;
    default:
      return false;
  }

  // RUN
  adapter_->setEpoch(epoch);
  bool fresh = adapter_->step(link_, nowMs, cur_);
  const StepInfo& si = adapter_->lastStep();
  if (si.ran && si.fast) {
    if (si.ok) {
      stats_.fastOk++;
      stats_.consecutiveFails = 0;
      backoffIdx_ = 0;
      nextReprobeMs_ = 0;
    } else {
      stats_.fastFail++;
      stats_.consecutiveFails++;
    }
  }
  if (stats_.consecutiveFails >= cfg::REPROBE_FAILS) {
    if (nextReprobeMs_ == 0 || (int32_t)(nowMs - nextReprobeMs_) >= 0) {
      stats_.reprobes++;
      log_.clear();
      log_.setTimeBase(nowMs);
      if (adapter_->lightProbe(link_, log_)) {
        stats_.consecutiveFails = 0;
        backoffIdx_ = 0;
        nextReprobeMs_ = 0;
      } else {
        // A full probe resets every adapter it tries, including the running one (dialect, schedule, identity).
        // 0.7.3: snapshot it first and put it back when the probe fails. On 2026-10-22 a 7-minute inverter silence
        // made the failed probe wipe the REVO dialect; polling then resumed as dialect UNKNOWN.
        AdapterState keepSt;
        adapter_->exportState(keepSt);
        Identity keepId;
        adapter_->identity(keepId);
        ProbeOutcome o = prober_.full(base_, log_);
        uint32_t now = link_.clock().ms();
        if (o.ok) {
          outcome_ = o;
          enterRun(o.kind, o.profile, now);
        } else {
          // keep polling the old adapter, as it was, on its profile; retry later with back-off
          adapter_->importState(keepSt);
          adapter_->restoreIdentity(keepId);
          link_.configure(outcome_.profile.baud ? outcome_.profile : base_);
          static const int NB = sizeof(cfg::REPROBE_BACKOFF_MS) / sizeof(cfg::REPROBE_BACKOFF_MS[0]);
          nextReprobeMs_ = now + cfg::REPROBE_BACKOFF_MS[backoffIdx_ < NB ? backoffIdx_ : NB - 1];
          if (backoffIdx_ < NB - 1) backoffIdx_++;
          adapter_->schedule(now);
        }
      }
    }
  }
  if (!fresh) return false;
  out = cur_;
  out.upMs = nowMs;
  out.ts = epoch;
  out.lk = 1;
  return true;
}
