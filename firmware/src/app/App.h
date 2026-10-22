// App.h - shared application state between the FreeRTOS tasks (d1 §5.3). Every accessor is thread-safe.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>

#include "agg/AlertEngine.h"
#include "agg/Aggregator.h"
#include "agg/RuleEngine.h"
#include "agg/RuleTypes.h"
#include "hal/ArduinoHal.h"
#include "link/PinDiag.h"
#include "link/RawRing.h"
#include "link/SerialLink.h"
#include "model/Identity.h"
#include "model/Sample.h"
#include "net/Uplink.h"
#include "proto/Engine.h"
#include "store/MinuteRec.h"

// event group bits (d1 §5.3)
constexpr EventBits_t EV_WIFI_UP = BIT0, EV_TIME_OK = BIT1, EV_AUTH_OK = BIT2, EV_LINK_OK = BIT3,
                      EV_SAFE_MODE = BIT4, EV_PORTAL = BIT5;

enum class DiagKind : uint8_t { SELF_ID = 0, IDLE, LOOPBACK, ARM, ARM_MAX3232, RESET_LINK, PROBE, CAPTURE };
struct DiagRequest {
  DiagKind kind = DiagKind::SELF_ID;
  uint8_t arg = 0;    // IDLE: RJ45 pin 1|2; ARM: RJ45 pin GPIO17 is wired to; RESET_LINK: 1 = also forget self-ID
  bool flag = false;  // LOOPBACK: bench confirmed; ARM: user confirmed; CAPTURE: on/off
};

struct DiagState {
  bool busy = false;
  char running[24] = "";
  uint32_t startedMs = 0, expectMs = 0;
  SelfIdResult selfId;
  IdleHistory idle;
  SerialLink::LoopbackResult loop;
  ArmCheck arm;
  char lastMsg[200] = "";
  uint32_t seq = 0;
};

struct LinkSnapshot {
  EngineState state = EngineState::DISARMED;
  char adapter[8] = "";
  char dialect[10] = "";
  char profile[40] = "";
  uint32_t baud = 0;
  bool armed = false, safe = false, linkOk = false;
  LinkMode mode = LinkMode::RS232_LITE;
  uint8_t roPin = 0, diPin = 0, invTxRj = 0, armRx = 0;
  bool diConfirmed = false;
  LinkStats ls;
  EngineStats es;
  char probeSummary[120] = "";
  uint32_t probeAtMs = 0;
  bool probeRan = false, probeOk = false;
};

// rule engine summary (aggTask -> uplink `status`, /api/status, console)
struct RuleInfo {
  double rev = 0;          // alertsRev of the loaded set (status.arv)
  uint8_t n = 0;           // enabled rules (status.nrul)
  uint8_t present = 0, invalid = 0;
  uint16_t jobsToday = 0;  // mailq jobs queued today (status.mjq)
  bool emailOn = false;
  uint8_t monthStart = 1;
  char err[41] = "";       // first invalid rule (status.ruleErr)
  struct Slot { char rid[4]; uint8_t st, cnt; } slot[cfg::MAX_RULES];
  uint8_t nSlots = 0;
};

struct RaiseReq {
  char type[24];
  uint8_t sev;
  bool force;
  char msg[120];
};

class App {
 public:
  void begin();

  void setLatest(const Sample& s);
  Sample latest(uint32_t* ageMs = nullptr);
  void setEnergy(float epv, float eld, float ech, float edis, float egr, int32_t q);

  void setIdentity(const Identity& id);
  Identity identity();

  void setLink(const LinkSnapshot& l);
  LinkSnapshot link();

  bool postDiag(const DiagRequest& r);
  bool takeDiag(DiagRequest& r);
  DiagState diag();
  template <typename F> void editDiag(F f) { LockGuard g(&lock_); f(diag_); }

  void setProbeReport(const String& json);
  String probeReport();

  void pushH15(const mrec::H15& h);
  bool takeH15(mrec::H15& h);
  void setDaily(const mrec::Daily& d);
  mrec::Daily daily();

  void applyCloudCfg(const AlertCfg& c);  // FW-2 -> aggTask (thresholds, testAlert, reprobe)
  bool takeCfg(AlertCfg& c);
  void raise(const char* type, uint8_t sev, const char* msg, bool force = false);  // any task -> alert engine
  bool takeRaise(RaiseReq& r);

  void setUplinkStatus(const UplinkStatus& s);
  UplinkStatus uplinkStatus();

  // usage-threshold rules (r8 §4.1): uplink -> aggTask
  void applyRules(const rules::Set& s);
  bool takeRules(rules::Set& out);
  // aggTask -> uplink / local API
  void setRuleInfo(const RuleInfo& r);
  RuleInfo ruleInfo();
  void putAlertState(const AlertStateOut& o);          // newest record per slot, marked dirty
  int takeAlertStates(AlertStateOut* out, int max);    // dirty records (clears the marks)
  void returnAlertStates(const AlertStateOut* in, int n);  // upload failed: re-mark unless superseded
  // month-to-date bootstrap (r8 §4.5): aggTask asks, the uplink answers with RTDB `daily` records
  void setMtdNeed(int32_t firstDay, int32_t lastDay);  // 0,0 = nothing needed
  bool mtdNeed(int32_t& firstDay, int32_t& lastDay);
  void postMtdDays(const mrec::Daily* d, int n);       // n may be 0: "asked, nothing in RTDB"
  int takeMtdDays(mrec::Daily* out, int max, bool* answered);
  // finished day (00:00 roll-over) -> the uplink writes the final daily/<d> incl. nf/nw/epvi
  void pushFinishedDay(const mrec::Daily& d);
  bool takeFinishedDay(mrec::Daily& d);

  void setRaw(RawRing* r) { raw_ = r; }
  int copyLastReplies(LastReply* out, int max);
  int copyExchanges(RawExchange* out, int max);

  EventGroupHandle_t events = nullptr;
  volatile bool safeMode = false, captureOn = false;
  volatile uint32_t lastValidSampleMs = 0;
  uint32_t bootEpoch = 0;
  char resetReason[16] = "";
  uint16_t bootId = 0;

 private:
  RtosLock lock_;
  Sample latest_;
  uint32_t latestMs_ = 0;
  bool haveLatest_ = false;
  Identity ident_;
  LinkSnapshot link_;
  DiagState diag_;
  String probeReport_;
  mrec::H15 h15_[4];
  int h15n_ = 0;
  mrec::Daily daily_;
  AlertCfg cfg_;
  bool cfgPending_ = false;
  UplinkStatus up_;
  rules::Set* rulesPending_ = nullptr;  // PSRAM (~3.4 KB)
  bool rulesHave_ = false;
  RuleInfo ruleInfo_;
  AlertStateOut states_[cfg::MAX_RULES];
  uint32_t stateDirty_ = 0;
  int32_t mtdFirst_ = 0, mtdLast_ = 0;
  mrec::Daily* mtdDays_ = nullptr;      // [31] PSRAM
  int mtdN_ = 0;
  bool mtdAnswered_ = false;
  mrec::Daily finished_;
  bool finishedHave_ = false;
  RawRing* raw_ = nullptr;
  QueueHandle_t qDiag_ = nullptr, qRaise_ = nullptr;
};

extern App app;
extern AlertQueue alertQueue;

// tasks (src/app/Tasks.cpp)
void startInvTask();
void startAggTask();
void startUplinkTask();
// thread-safe access to the aggregator's PSRAM ring for /api/ring
int aggDownsample(uint32_t nowMs, uint32_t spanS, uint32_t stepS, SampleRing::Bucket* out, int max);
