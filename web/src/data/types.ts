// Data contract: d1 §3.8 (field list) and §4.1–4.2 (RTDB tree and exact JSON).
// The firmware writes these keys; never rename them here without changing
// the firmware and firebase/database.rules.json in the same commit.
// A missing value is simply an absent key (the firmware omits NaN).
// Alert rules and e-mail: research/r8-email-alerts.md section 3 (alerts, alertState, mailq, mailer).
import type { MetricId, RuleScope } from '../alertMetrics';
import { t, type Key } from '../lib/i18n';

/** /devices/inv1/live — PUT every cfg.liveSec seconds (default 10). */
export interface Live {
  ts: number; // UTC epoch s
  lk: number; // 1 = valid inverter reply within 15 s
  gv?: number; // grid V
  gf?: number; // grid Hz
  ov?: number; // output V
  of?: number; // output Hz
  va?: number; // output VA
  w?: number; // output (load) W
  ld?: number; // load %
  bus?: number; // DC bus V
  bv?: number; // battery V
  bci?: number; // battery charge A
  bdi?: number; // battery discharge A
  bi?: number; // net battery A, + = charging
  bw?: number; // battery W, + = charging
  soc?: number; // %
  tmp?: number; // heat-sink °C (raw ADC if > 150)
  pvv?: number; // PV V
  pvi?: number; // PV A
  pvw?: number; // PV W
  sccv?: number; // battery V seen by the SCC
  md?: string; // QMOD letter
  st?: string; // QPIGS status b7..b0
  st2?: string; // b10 b9 b8
  wf?: string; // QPIWS bit string (32/36/38)
  wc?: number; // REVO warning code (QALL[16])
  fc?: number; // fault code (table depends on dialect)
  epv?: number; // Wh today, integrated on the device
  eld?: number;
  egr?: number; // estimate
  ech?: number;
  edis?: number;
  epvi?: number; // Wh today reported by the inverter
  etot?: number; // kWh lifetime reported by the inverter
  q?: number; // minute records waiting for upload
  gw?: number; // grid import power estimate W (r8 §3.9): (grid present && md=='L') ? w + AC charge W : 0
  // fw 0.9.0 grid meter (Tuya/Smart Life breaker on the grid line before the inverter), read on the LAN.
  // Present only while cfg.meter is on and the reading is fresh; any of them can be missing on its own.
  // The app uses mw/egm in place of the gw/egr estimates (lib/power.ts); gw and egr themselves stay estimates.
  mw?: number; // grid active power W (real W, never scaled by the power factor)
  mv?: number; // V
  mi?: number; // A
  mpf?: number; // power factor 0–1
  mhz?: number; // Hz
  mrc?: number; // residual (leakage) current mA
  mtc?: number; // meter temperature °C
  mkwh?: number; // the meter's lifetime forward energy counter, kWh
  mon?: number; // breaker: 1 = on (closed), 0 = off
  egm?: number; // metered grid energy today, Wh (only when the whole day is metered)
  mft?: number; // fw 0.9.1: the meter's alarm bitmap, bit i = METER_ALARMS[i] (lib/meter.ts); 0 = no alarm
  mage?: number; // fw 0.9.1: seconds since the meter reading (the meter refreshes V/A/W about every 15 s)
}

/** /devices/inv1/status — in the 60 s PATCH. */
export interface Status {
  ts?: number;
  fw?: string;
  ip?: string;
  rssi?: number;
  heap?: number;
  psram?: number;
  up?: number;
  rr?: string;
  q?: number;
  proto?: string;
  dia?: string;
  hs?: number;
  rq?: number;
  lok?: number;
  cok?: number;
  // r8 §3.9 (alert rules)
  arv?: number; // loaded cfg.alertsRev (ms)
  acat?: number; // metric catalogue version the firmware understands
  mjq?: number; // e-mail jobs queued today
  nrul?: number; // enabled rules loaded
  ruleErr?: string; // why the last alerts.json was refused (old set kept)
  // FW-2 (firmware/README.md §8 D6)
  edrop?: number; // events dropped after 3 refused writes
  qg?: number; // quota-guard level 0-2
  // fw 0.9.0 grid meter: "ok <proto> <age>s" while readings are fresh, else a short English state ("not reachable", …)
  meter?: string;
  ota?: string; // fw 0.9.2: remote firmware update state ("updated to 0.9.3", "downloading 0.9.3: 40%", "failed: …")
  repo?: string; // fw 0.9.2: GitHub repo whose releases the logger installs (absent: remote update off)
  otachk?: string; // fw 0.9.6: the logger's own check of the GitHub download path after boot ("GitHub ok, latest 0.9.7")
}

/** /devices/inv1/meta — flat, boot and on change. */
export interface Meta {
  qpi?: string | number;
  qid?: string | number;
  vfw?: string | number;
  vfw2?: string | number;
  qmn?: string | number;
  ratva?: number | string;
  ratw?: number | string;
  ratbv?: number | string;
  btype?: number | string;
  outpri?: number | string;
  chgpri?: number | string;
  inrng?: number | string;
  dia?: string; // STOCK17 | STOCK21 | MAX24 | GK | REVO | ...
  pv12?: string; // 'W' | 'A'
  sup?: string; // comma list of supported commands
  link?: string;
  fwver?: string;
  boot?: number | string;
}

/** /devices/inv1/hist/{date}/{hhmm} and h15/{date}/{hhmm}. */
export interface HistRec {
  w?: number; // avg load W
  wx?: number; // max load W
  pvw?: number; // avg PV W
  pvx?: number; // max PV W
  bv?: number; // avg battery V
  bvn?: number; // min battery V
  bvx?: number; // max battery V
  bi?: number; // avg battery A (+ charging)
  soc?: number; // last SOC
  gv?: number; // avg grid V
  gvn?: number; // min grid V
  ov?: number; // avg output V
  ld?: number; // avg load %
  tmp?: number; // max heat-sink °C
  pvv?: number; // avg PV V
  md?: string; // dominant mode
  wn?: number; // distinct warning bits seen
  fc?: number; // last non-zero fault code
  n: number; // valid samples (12 = complete minute)
  epv?: number; // Wh in this interval (0.1 Wh)
  eld?: number;
  ech?: number;
  edis?: number;
  egr?: number;
  tq?: number; // 0 = provisional timestamp
}

/** /devices/inv1/daily/{date} — energies in Wh. */
export interface Daily {
  epv?: number;
  eld?: number;
  egr?: number;
  ech?: number;
  edis?: number;
  epvi?: number;
  pvx?: number;
  wx?: number;
  bvn?: number;
  bvx?: number;
  socn?: number;
  socx?: number;
  gout?: number; // minutes without grid
  nf?: number;
  nw?: number;
  n?: number; // minutes with data
  egm?: number; // fw 0.9.0: metered grid energy of the day, Wh (only on fully metered days; egr stays the estimate)
}

/** E-mail status of an event (r8 §3.6): device gates, then the mailer's result. */
export type EmailState = 'q' | 'sent' | 'cool' | 'rcap' | 'cap' | 'quota' | 'off' | 'norc' | 'exp' | 'fail' | 'skip';

/** /devices/inv1/events/{date}/{key}; key = `<ts>-<rid|TYPE>-<o|c|r|i>` (older push ids still valid). */
export interface EventRec {
  ts: number;
  type: string;
  sev: number;
  st?: 'open' | 'clear' | 'info' | string;
  msg: string;
  ch?: string;
  dur?: number;
  ack?: boolean;
  rid?: string; // rule slot r01..r16 (type RULE / RULE_INVALID)
  val?: number; // value at the transition, canonical units
  thr?: number; // the rule's threshold, canonical units
  em?: EmailState | string;
  emt?: number; // epoch s when the mailer set em
}

export interface EventItem extends EventRec {
  id: string;
  date: string;
}

/** /devices/inv1/cfg — owner-written. */
export interface Cfg {
  liveSec: number;
  socLow: number;
  bvLow: number;
  bvHigh: number;
  tempHigh: number;
  loadHigh: number;
  notifyRepeatMin: number;
  keepHistDays: number;
  keepEventDays: number;
  quietFrom: number;
  quietTo: number;
  /** 0.7.9: the owner's daily planned grid-off window, local minutes 0..1439 (-1 = none). Not in CFG_FIELDS. */
  gridOffFrom: number;
  gridOffTo: number;
  summaryHour: number;
  testAlert: number;
  reprobe: number;
  /** 0.9.0: 1 = read the grid meter and use its W/kWh (default when absent), 0 = off (inverter estimates only). Not in CFG_FIELDS. */
  meter: number;
  /** 0.9.2: remote firmware update request: version code (0.9.3 -> 903) and the request time (ms). Not in CFG_FIELDS. */
  ota: number;
  otaAt: number;
  /** serverTimestamp() (ms) bumped with every alerts/* change (r8 §3.4). Never in CFG_FIELDS. */
  alertsRev?: number;
}

export const CFG_DEFAULTS: Cfg = {
  liveSec: 10,
  socLow: 30,
  bvLow: 22.5,
  bvHigh: 30.0,
  tempHigh: 75,
  loadHigh: 90,
  notifyRepeatMin: 30,
  keepHistDays: 400,
  keepEventDays: 730,
  quietFrom: 22,
  quietTo: 7,
  gridOffFrom: -1,
  gridOffTo: -1,
  summaryHour: 21,
  testAlert: 0,
  reprobe: 0,
  meter: 1,
  ota: 0,
  otaAt: 0,
};

export interface CfgField {
  key: Exclude<keyof Cfg, 'testAlert' | 'reprobe' | 'alertsRev' | 'gridOffFrom' | 'gridOffTo' | 'meter' | 'ota' | 'otaAt'>;
  label: string;
  unit: string;
  min: number;
  max: number;
  step: number;
  help: string;
}

/** Time units shown after a cfg label; electrical units stay as they are. */
const UNIT_KEY: Record<string, Key> = { s: 'unit.s', min: 'unit.min', h: 'unit.h', days: 'unit.days' };

/** label/unit/help are getters: they follow the UI language (texts in lib/i18n.ts, keys cfg.<key>.l / .h). */
const F = (key: CfgField['key'], unit: string, min: number, max: number, step: number): CfgField => ({
  key,
  min,
  max,
  step,
  get label() {
    return t(`cfg.${key}.l`);
  },
  get unit() {
    return UNIT_KEY[unit] ? t(UNIT_KEY[unit]) : unit;
  },
  get help() {
    return t(`cfg.${key}.h`);
  },
});

/** Same ranges as the `.validate` rules on cfg (d1 §4.7). */
export const CFG_FIELDS: CfgField[] = [
  F('liveSec', 's', 2, 300, 1),
  F('socLow', '%', 5, 80, 1),
  F('bvLow', 'V', 20, 30, 0.1),
  F('bvHigh', 'V', 26, 34, 0.1),
  F('tempHigh', '°C', 40, 100, 1),
  F('loadHigh', '%', 50, 120, 1),
  F('notifyRepeatMin', 'min', 5, 1440, 1),
  F('keepHistDays', 'days', 30, 3650, 1),
  F('keepEventDays', 'days', 30, 3650, 1),
  F('quietFrom', 'h', 0, 23, 1),
  F('quietTo', 'h', 0, 23, 1),
  F('summaryHour', 'h', -1, 23, 1),
];

export function validateCfgValue(f: CfgField, v: number): string | null {
  if (!Number.isFinite(v)) return t('cfg.enterNumber');
  if (v < f.min || v > f.max) return t('cfg.range', { min: f.min, max: f.max });
  return null;
}

// ---------------------------------------------------------------- alerts (r8 §3.2-3.8)

export type RuleOp = 'gt' | 'ge' | 'lt' | 'le';
export type RuleMode = 'hold' | 'at';

/** /devices/inv1/alerts/rules/{r01..r16} — owner-written; canonical units (W, Wh, V, A, %, °C, Hz, min, 0/1). */
export interface AlertRule {
  name: string;
  on: boolean;
  metric: MetricId;
  scope: RuleScope;
  op: RuleOp;
  value: number;
  mode: RuleMode;
  at?: string; // 'hhmm', required when mode = 'at'
  forSec?: number;
  clr?: number;
  clrSec?: number;
  coolMin?: number;
  repeatMin?: number;
  maxDay?: number;
  minCov?: number;
  win?: { from: string; to: string; days: number }; // days bit 0 = Sunday … bit 6 = Saturday
  sev: 1 | 2 | 3 | 4 | 5;
  email: boolean;
  ntfy: boolean;
  rc?: number; // 1–7 bit mask over alerts/email/to; absent = everyone
  clrMail?: boolean;
  upd: number | object; // serverTimestamp() on write, ms on read
}

/** /devices/inv1/alerts/email — owner-written; read by the device and the mailer. */
export interface EmailCfg {
  on: boolean;
  to: string; // 0–3 comma-separated addresses; position i = bit i of a rule's rc
  fromName?: string;
  maxPerDay?: number;
  monthStart?: number;
  builtin?: { minSev: 0 | 3 | 4 | 5; clear: boolean };
  summary?: { on: boolean; hour: number };
  offline?: { on: boolean; min: number; clear?: boolean };
}

export interface AlertsDoc {
  email?: EmailCfg;
  rules?: Record<string, AlertRule>;
}

export type RuleStateId = 'off' | 'wait' | 'idle' | 'pend' | 'fire' | 'clr' | 'latch' | 'done' | 'inv';

/** /devices/inv1/alertState/{rid} — device-written on transitions. */
export interface AlertStateRec {
  st: RuleStateId;
  since?: number;
  val?: number;
  last?: number;
  cnt?: number;
  pk?: number;
  err?: string;
}

/** /devices/inv1/mailer — the Apps Script mailer's heartbeat (every 5 min). */
export interface MailerStatus {
  hb?: number;
  ver?: string;
  day?: string;
  sent?: number;
  quota?: number;
  off?: number;
  err?: string;
  warn?: string;
  last?: { ts: number; ok: boolean; msg: string };
  test?: { ts: number; ok: boolean; msg: string };
}
