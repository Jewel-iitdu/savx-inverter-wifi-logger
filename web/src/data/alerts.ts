// Alert rules and e-mail settings: presets, client-side validation, value formatting and
// subject preview (research/r8-email-alerts.md §3.2-3.3, §3.11, §4.3, §4.5, §6.2-6.4).
//
// validateRule() and validateEmailCfg() implement EXACTLY the `.validate` rules of
// firebase/database.rules.json (alerts/rules/$rid and alerts/email), so a PERMISSION_DENIED on
// save should be unreachable. The metric ids, energy set and value ranges come from the
// generated alertMetrics.ts, which tools/gen_codes.py checks against the rules file.
// Keep the two in step: change the rules, then this file, in the same commit.
import { CLR_RANGE, METRIC_BY_ID, METRICS, type MetricDef, type MetricGroup, type MetricId, type RuleScope } from '../alertMetrics';
import { SEVERITY_NAMES } from '../codes';
import { inEnglish, localizeDigits, metricBn, metricGroupBn, t, type Key } from '../lib/i18n';
import type { AlertRule, Daily, EmailCfg, Live, RuleMode, RuleOp, RuleStateId } from './types';
import { addDays, fmtClock, fmtDateTime, monthBounds, weekdayShort } from '../lib/time';

// ------------------------------------------------------------------ constants

export const RULE_IDS: readonly string[] = Array.from({ length: 16 }, (_, i) => `r${String(i + 1).padStart(2, '0')}`);
export const RID_RE = /^r(0[1-9]|1[0-6])$/;
const HHMM_RE = /^([01][0-9]|2[0-3])[0-5][0-9]$/;
/** One address, as in the rules' `to` regex. */
export const EMAIL_RE = /^[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,24}$/;
export const MAX_RECIPIENTS = 3;
/** The Apps Script mailer's subject prefix (script property SUBJECT_TAG, default "Solar"). */
export const SUBJECT_TAG = '[Solar]';
/** The mailer heartbeat is "stale" after this (r8 §6.4). */
export const MAILER_STALE_SEC = 15 * 60;

export const ENERGY_METRICS: ReadonlySet<MetricId> = new Set(METRICS.filter((m) => m.scopes[0] !== 'inst').map((m) => m.id));
export const isEnergy = (m: MetricId) => ENERGY_METRICS.has(m);
export const isBool = (m: MetricId) => METRIC_BY_ID[m]?.kind === 'bool';
export const metricDef = (m: string): MetricDef | undefined => (METRIC_BY_ID as Record<string, MetricDef | undefined>)[m];

export const OP_SYMBOL: Record<RuleOp, string> = { gt: '>', ge: '≥', lt: '<', le: '≤' };
/** 'above' … 'at or below' in the UI language (getters, so the current language is read on use). */
export const OP_WORD: Record<RuleOp, string> = {
  get gt() {
    return t('op.gt');
  },
  get ge() {
    return t('op.ge');
  },
  get lt() {
    return t('op.lt');
  },
  get le() {
    return t('op.le');
  },
};

/** Metric name in the UI language (alertMetrics.ts is generated and stays English). */
export function metricLabel(id: MetricId | string): string {
  return metricBn(id) ?? metricDef(id)?.label ?? id;
}

export function metricGroupLabel(g: { id: MetricGroup; label: string }): string {
  return metricGroupBn(g.id) ?? g.label;
}

/** Defaults the mailer and the logger assume when a field is absent (r8 §3.2). */
export interface FullEmailCfg {
  on: boolean;
  to: string;
  fromName: string;
  maxPerDay: number;
  monthStart: number;
  builtin: { minSev: 0 | 3 | 4 | 5; clear: boolean };
  summary: { on: boolean; hour: number };
  offline: { on: boolean; min: number; clear: boolean };
}

export const EMAIL_DEFAULTS: FullEmailCfg = {
  on: false,
  to: '',
  fromName: 'Solar logger',
  maxPerDay: 40,
  monthStart: 1,
  builtin: { minSev: 4, clear: true },
  summary: { on: true, hour: 7 },
  offline: { on: true, min: 30, clear: true },
};

export function withEmailDefaults(c: EmailCfg | null | undefined): FullEmailCfg {
  const d = EMAIL_DEFAULTS;
  return {
    on: c?.on ?? d.on,
    to: c?.to ?? d.to,
    fromName: c?.fromName ?? d.fromName,
    maxPerDay: c?.maxPerDay ?? d.maxPerDay,
    monthStart: c?.monthStart ?? d.monthStart,
    builtin: { minSev: c?.builtin?.minSev ?? d.builtin.minSev, clear: c?.builtin?.clear ?? d.builtin.clear },
    summary: { on: c?.summary?.on ?? d.summary.on, hour: c?.summary?.hour ?? d.summary.hour },
    offline: { on: c?.offline?.on ?? d.offline.on, min: c?.offline?.min ?? d.offline.min, clear: c?.offline?.clear ?? d.offline.clear },
  };
}

export function parseRecipients(to: string | undefined | null): string[] {
  return (to ?? '')
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean);
}

// ------------------------------------------------------------------ presets (r8 §6.3)

export interface Preset {
  id: string;
  hint?: string;
  rule: Omit<AlertRule, 'upd'>;
}

const P = (id: string, rule: Omit<AlertRule, 'upd' | 'on'>, hint?: string): Preset => ({ id, hint, rule: { ...rule, on: true } });

/**
 * The preset list in the UI language. The name becomes the stored rule name (the logger and the
 * e-mail subject use it; the firmware takes Bangla names), so its digits stay ASCII.
 */
export const presets = (): readonly Preset[] => [
  P('grid-day', { name: t('preset.gridDay'), metric: 'egr', scope: 'day', op: 'gt', value: 5000, mode: 'hold', forSec: 0, coolMin: 60, maxDay: 1, sev: 3, email: true, ntfy: false }, t('preset.gridDayHint')),
  P('load-5min', { name: t('preset.load5'), metric: 'w', scope: 'inst', op: 'gt', value: 2500, mode: 'hold', forSec: 300, clr: 2200, clrSec: 120, coolMin: 60, maxDay: 3, sev: 3, email: true, ntfy: true, clrMail: true }),
  P('soc-30', { name: t('preset.soc30'), metric: 'soc', scope: 'inst', op: 'lt', value: 30, mode: 'hold', forSec: 60, clr: 35, clrSec: 300, coolMin: 120, maxDay: 3, sev: 4, email: true, ntfy: false, clrMail: true }, t('preset.soc30Hint')),
  P('pv-17', { name: t('preset.pv17'), metric: 'epv', scope: 'day', op: 'lt', value: 3000, mode: 'at', at: '1700', minCov: 80, maxDay: 1, sev: 2, email: true, ntfy: false }),
  P('temp-70', { name: t('preset.temp70'), metric: 'tmp', scope: 'inst', op: 'gt', value: 70, mode: 'hold', forSec: 120, clr: 65, clrSec: 300, coolMin: 60, maxDay: 3, sev: 4, email: true, ntfy: true, clrMail: true }),
  P('grid-month', { name: t('preset.gridMonth'), metric: 'egr', scope: 'month', op: 'gt', value: 150000, mode: 'hold', maxDay: 1, sev: 3, email: true, ntfy: false }, t('preset.gridMonthHint')),
  P('outage-3h', { name: t('preset.outage3h'), metric: 'gout', scope: 'day', op: 'gt', value: 180, mode: 'hold', maxDay: 1, sev: 3, email: true, ntfy: false }),
  P('onbat-2h', { name: t('preset.onbat2h'), metric: 'onbat', scope: 'inst', op: 'ge', value: 1, mode: 'hold', forSec: 7200, clrSec: 60, coolMin: 120, maxDay: 3, sev: 3, email: true, ntfy: false, clrMail: true }),
  P('evening-peak', { name: t('preset.evening'), metric: 'w', scope: 'inst', op: 'gt', value: 1500, mode: 'hold', forSec: 60, clr: 1300, clrSec: 120, coolMin: 60, maxDay: 2, win: { from: '1800', to: '2300', days: 127 }, sev: 3, email: true, ntfy: false }),
];

/** Starting point for "Custom rule". */
export const BLANK_RULE: Omit<AlertRule, 'upd'> = { name: '', on: true, metric: 'w', scope: 'inst', op: 'gt', value: 2000, mode: 'hold', forSec: 300, coolMin: 60, maxDay: 3, sev: 3, email: true, ntfy: false };

export function firstFreeSlot(rules: Record<string, unknown> | null | undefined): string | null {
  return RULE_IDS.find((id) => !rules || !(id in rules)) ?? null;
}

// ------------------------------------------------------------------ validation (mirrors the rules exactly)

const RULE_KEYS = new Set(['name', 'on', 'metric', 'scope', 'op', 'value', 'mode', 'at', 'forSec', 'clr', 'clrSec', 'coolMin', 'repeatMin', 'maxDay', 'minCov', 'win', 'sev', 'email', 'ntfy', 'rc', 'clrMail', 'upd']);
const REQUIRED = ['name', 'on', 'metric', 'scope', 'op', 'value', 'mode', 'sev', 'email', 'ntfy', 'upd'] as const;
const isNumber = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const isInt = (v: unknown): v is number => isNumber(v) && v % 1 === 0;
const isBoolean = (v: unknown): v is boolean => typeof v === 'boolean';
const hasCtl = (s: string) => s.includes('\n') || s.includes('\r') || s.includes('\t');
const intIn = (v: unknown, lo: number, hi: number) => isInt(v) && v >= lo && v <= hi;
const has = (o: object, k: string) => Object.prototype.hasOwnProperty.call(o, k) && (o as Record<string, unknown>)[k] !== undefined && (o as Record<string, unknown>)[k] !== null;

/**
 * Every reason the database would refuse `rule` at alerts/rules/<rid> (empty = accepted).
 * `upd` may be a number (≤ now) or the serverTimestamp() placeholder object.
 */
export function validateRule(rid: string, rule: object): string[] {
  const e: string[] = [];
  const r = rule as Record<string, unknown>;
  if (!RID_RE.test(rid)) e.push(t('v.slot'));
  for (const k of Object.keys(r)) if (r[k] !== undefined && r[k] !== null && !RULE_KEYS.has(k)) e.push(t('v.unknownField', { k }));
  for (const k of REQUIRED) if (!has(r, k)) e.push(t('v.missing', { k }));

  const name = r.name;
  if (has(r, 'name') && !(typeof name === 'string' && name.length >= 1 && name.length <= 40 && !hasCtl(name))) e.push(t('v.name'));
  if (has(r, 'on') && !isBoolean(r.on)) e.push(t('v.bool', { k: 'on' }));
  const m = typeof r.metric === 'string' ? metricDef(r.metric) : undefined;
  if (has(r, 'metric') && !m) e.push(t('v.metric'));
  if (has(r, 'scope') && !(typeof r.scope === 'string' && /^(inst|day|month)$/.test(r.scope))) e.push(t('v.scope'));
  if (has(r, 'op') && !(typeof r.op === 'string' && /^(gt|ge|lt|le)$/.test(r.op))) e.push(t('v.op'));
  if (has(r, 'value')) {
    const v = r.value;
    if (!isNumber(v)) e.push(t('v.valueNum'));
    else if (m) {
      if (m.kind === 'bool' ? !(v === 0 || v === 1) : !(v >= m.min && v <= m.max)) e.push(m.kind === 'bool' ? t('v.valueBool') : t('v.valueRange', { min: fmtRaw(m, m.min), max: fmtRaw(m, m.max) }));
    } else if (!has(r, 'metric') || !m) {
      // rules: the value range depends on the metric; an unknown metric fails the value check too
      e.push(t('v.valueNoMetric'));
    }
  }
  if (has(r, 'mode') && !(typeof r.mode === 'string' && /^(hold|at)$/.test(r.mode))) e.push(t('v.mode'));
  if (has(r, 'at') && !(typeof r.at === 'string' && HHMM_RE.test(r.at))) e.push(t('v.at'));
  if (has(r, 'forSec') && !intIn(r.forSec, 0, 86400)) e.push(t('v.forSec'));
  if (has(r, 'clr') && !(isNumber(r.clr) && r.clr >= CLR_RANGE.min && r.clr <= CLR_RANGE.max)) e.push(t('v.clr', { min: CLR_RANGE.min, max: CLR_RANGE.max }));
  if (has(r, 'clrSec') && !intIn(r.clrSec, 0, 3600)) e.push(t('v.clrSec'));
  if (has(r, 'coolMin') && !intIn(r.coolMin, 5, 10080)) e.push(t('v.coolMin'));
  if (has(r, 'repeatMin') && !(isInt(r.repeatMin) && (r.repeatMin === 0 || (r.repeatMin >= 30 && r.repeatMin <= 1440)))) e.push(t('v.repeatMin'));
  if (has(r, 'maxDay') && !intIn(r.maxDay, 1, 10)) e.push(t('v.maxDay'));
  if (has(r, 'minCov') && !intIn(r.minCov, 0, 100)) e.push(t('v.minCov'));
  if (has(r, 'win')) {
    const w = r.win as Record<string, unknown> | null;
    if (typeof w !== 'object' || !w || !has(w, 'from') || !has(w, 'to') || !has(w, 'days')) e.push(t('v.winNeed'));
    else {
      if (!(typeof w.from === 'string' && HHMM_RE.test(w.from)) || !(typeof w.to === 'string' && HHMM_RE.test(w.to))) e.push(t('v.winTimes'));
      else if (w.from === w.to) e.push(t('v.winSame'));
      if (!intIn(w.days, 1, 127)) e.push(t('v.winDays'));
      for (const k of Object.keys(w)) if (!['from', 'to', 'days'].includes(k)) e.push(t('v.unknownField', { k: `win.${k}` }));
    }
  }
  if (has(r, 'sev') && !intIn(r.sev, 1, 5)) e.push(t('v.sev'));
  if (has(r, 'email') && !isBoolean(r.email)) e.push(t('v.bool', { k: 'email' }));
  if (has(r, 'ntfy') && !isBoolean(r.ntfy)) e.push(t('v.bool', { k: 'ntfy' }));
  if (has(r, 'rc') && !intIn(r.rc, 1, 7)) e.push(t('v.rc'));
  if (has(r, 'clrMail') && !isBoolean(r.clrMail)) e.push(t('v.bool', { k: 'clrMail' }));
  if (has(r, 'upd') && !(isNumber(r.upd) ? r.upd <= Date.now() : typeof r.upd === 'object')) e.push(t('v.upd'));

  // The rule-level .validate (only meaningful once the fields themselves are typed correctly).
  const scope = r.scope as RuleScope;
  const op = r.op as RuleOp;
  const mode = r.mode as RuleMode;
  if (m) {
    if (isEnergy(m.id) ? scope === 'inst' : scope !== 'inst') e.push(isEnergy(m.id) ? t('v.energyScope') : t('v.instScope'));
  }
  if (scope === 'month' && !((op === 'gt' || op === 'ge') && mode === 'hold')) e.push(t('v.monthOp'));
  if (scope === 'day' && (op === 'lt' || op === 'le') && mode !== 'at') e.push(t('v.dayBelow'));
  if (mode === 'at' && !has(r, 'at')) e.push(t('v.atMissing'));
  if (has(r, 'clr') && isNumber(r.clr) && isNumber(r.value)) {
    if ((op === 'gt' || op === 'ge') && !(r.clr <= r.value)) e.push(t('v.clrBelow'));
    if ((op === 'lt' || op === 'le') && !(r.clr >= r.value)) e.push(t('v.clrAbove'));
  }
  if (!(r.email === true || r.ntfy === true)) e.push(t('v.channel'));
  return Array.from(new Set(e));
}

/** Every reason the database would refuse `c` at alerts/email (empty = accepted). */
export function validateEmailCfg(cfg: object): string[] {
  const e: string[] = [];
  const c = cfg as EmailCfg & Record<string, unknown>;
  const known = ['on', 'to', 'fromName', 'maxPerDay', 'monthStart', 'builtin', 'summary', 'offline'];
  for (const k of Object.keys(c)) if (c[k] !== undefined && c[k] !== null && !known.includes(k)) e.push(t('v.unknownField', { k }));
  if (!isBoolean(c.on)) e.push(t('v.onOff', { k: t('em.send') }));
  if (has(c, 'to')) {
    const to = c.to;
    const list = typeof to === 'string' ? to.split(',') : [];
    if (typeof to !== 'string' || to.length > 300 || (to !== '' && (list.length > MAX_RECIPIENTS || !list.every((a) => EMAIL_RE.test(a)))))
      e.push(t('v.recipients', { n: MAX_RECIPIENTS }));
  }
  if (has(c, 'fromName') && !(typeof c.fromName === 'string' && c.fromName.length >= 1 && c.fromName.length <= 40 && !hasCtl(c.fromName))) e.push(t('v.fromName'));
  if (has(c, 'maxPerDay') && !intIn(c.maxPerDay, 1, 40)) e.push(t('v.maxPerDay'));
  if (has(c, 'monthStart') && !intIn(c.monthStart, 1, 28)) e.push(t('v.monthStart'));
  const sub = (name: string, v: unknown, keys: string[], required: string[], check: (o: Record<string, unknown>) => void) => {
    if (v === undefined || v === null) return;
    if (typeof v !== 'object') {
      e.push(t('v.object', { k: name }));
      return;
    }
    const o = v as Record<string, unknown>;
    for (const k of required) if (!has(o, k)) e.push(t('v.missing', { k: `${name}.${k}` }));
    for (const k of Object.keys(o)) if (o[k] !== undefined && o[k] !== null && !keys.includes(k)) e.push(t('v.unknownField', { k: `${name}.${k}` }));
    check(o);
  };
  sub('builtin', c.builtin, ['minSev', 'clear'], [], (o) => {
    if (has(o, 'minSev') && !(o.minSev === 0 || o.minSev === 3 || o.minSev === 4 || o.minSev === 5)) e.push(t('v.builtin'));
    if (has(o, 'clear') && !isBoolean(o.clear)) e.push(t('v.onOff', { k: t('em.recovery') }));
  });
  sub('summary', c.summary, ['on', 'hour'], ['on', 'hour'], (o) => {
    if (has(o, 'on') && !isBoolean(o.on)) e.push(t('v.onOff', { k: t('em.summaryShort') }));
    if (has(o, 'hour') && !intIn(o.hour, 0, 23)) e.push(t('v.summaryHour'));
  });
  sub('offline', c.offline, ['on', 'min', 'clear'], ['on', 'min'], (o) => {
    if (has(o, 'on') && !isBoolean(o.on)) e.push(t('v.onOff', { k: t('em.offline') }));
    if (has(o, 'min') && !intIn(o.min, 10, 240)) e.push(t('v.offlineMin'));
    if (has(o, 'clear') && !isBoolean(o.clear)) e.push(t('v.onOff', { k: t('em.backOnlineShort') }));
  });
  return Array.from(new Set(e));
}

/** Removes undefined fields (RTDB rejects them) and returns a fresh object. */
export function cleanRule(r: Omit<AlertRule, 'upd'>): Omit<AlertRule, 'upd'> {
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(r)) if (v !== undefined) out[k] = v;
  if (r.win) out.win = { from: r.win.from, to: r.win.to, days: r.win.days };
  return out as Omit<AlertRule, 'upd'>;
}

// ------------------------------------------------------------------ recipients and rc masks

/**
 * rc remapping when the recipient list changes (r8 §6.4: removing an address remaps every rule's rc
 * in the same update). Returns only the rules whose mask changes; null = "everyone on the list"
 * (used when none of a rule's addresses remain, because rc must be 1–7).
 */
export function remapRc(oldList: string[], newList: string[], rules: Record<string, AlertRule> | null | undefined): { remap: Record<string, number | null>; widened: string[] } {
  const remap: Record<string, number | null> = {};
  const widened: string[] = [];
  const lower = newList.map((a) => a.toLowerCase());
  for (const [rid, r] of Object.entries(rules ?? {})) {
    if (!isInt(r.rc)) continue;
    let mask = 0;
    oldList.forEach((addr, i) => {
      if (!(r.rc! & (1 << i))) return;
      const j = lower.indexOf(addr.toLowerCase());
      if (j >= 0) mask |= 1 << j;
    });
    const next = mask === 0 ? null : mask;
    if (next !== r.rc) {
      remap[rid] = next;
      if (next === null) widened.push(rid);
    }
  }
  return { remap, widened };
}

export function rcLabel(rc: number | undefined, list: string[]): string {
  if (!isInt(rc)) return t('rc.everyone');
  const picked = list.filter((_, i) => rc & (1 << i));
  return picked.length ? picked.join(', ') : t('rc.nobody');
}

// ------------------------------------------------------------------ hysteresis, values, formats

/** Default hysteresis of §4.3 (used by the logger when the rule has no clr). */
export function defaultHysteresis(metric: MetricId, value: number): number {
  const h = METRIC_BY_ID[metric]?.hyst;
  if (!h) return 0;
  return Math.max(h.abs, ((h.pct ?? 0) / 100) * Math.abs(value));
}

/** The level at which the rule recovers: clr, or value ∓ default hysteresis. */
export function clearLevel(r: Pick<AlertRule, 'metric' | 'op' | 'value' | 'clr'>): number {
  if (isNumber(r.clr)) return r.clr;
  const h = defaultHysteresis(r.metric, r.value);
  return r.op === 'gt' || r.op === 'ge' ? r.value - h : r.value + h;
}

/** True for rules that fire at most once per period (energy/outage day and month). */
export const isLatched = (r: Pick<AlertRule, 'scope'>) => r.scope !== 'inst';

function fmtRaw(m: MetricDef, v: number): string {
  return fmtValue(m.id, v, { est: false });
}

function fmtMinutes(v: number): string {
  const n = Math.round(v);
  if (n < 60) return t('u.min', { n });
  return t('u.hmin', { h: Math.floor(n / 60), m: localizeDigits(String(n % 60).padStart(2, '0')) });
}

/** Value formats of r8 §4.5 (the same the logger uses in the event message). */
export function fmtValue(metric: MetricId | string, v: number | null | undefined, opts: { est?: boolean } = {}): string {
  if (!isNumber(v)) return '–';
  const m = metricDef(metric);
  if (!m) return String(v);
  let s: string;
  if (m.kind === 'bool') s = v ? t('common.yes') : t('common.no');
  else
    switch (m.unit) {
      case 'Wh':
        s = Math.abs(v) >= 1000 ? `${(v / 1000).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })} kWh` : `${Math.round(v)} Wh`;
        break;
      case 'W':
        s = `${Math.round(v).toLocaleString('en-US')} W`;
        break;
      case 'V':
      case 'Hz':
        s = `${v.toFixed(1)} ${m.unit}`;
        break;
      case 'min':
        s = fmtMinutes(v);
        break;
      default:
        s = `${Math.round(v)} ${m.unit}`;
    }
  s = localizeDigits(s);
  return m.est && opts.est !== false ? t('val.est', { v: s }) : s;
}

/** Unit shown next to the value input (energy is typed in kWh, stored in Wh). */
export function inputUnit(metric: MetricId): string {
  const m = METRIC_BY_ID[metric];
  return m.unit === 'Wh' ? 'kWh' : m.unit === 'min' ? t('unit.min') : m.unit;
}

export function toInputValue(metric: MetricId, v: number): string {
  const m = METRIC_BY_ID[metric];
  if (m.unit === 'Wh') return String(Math.round(v) / 1000);
  return String(v);
}

/**
 * Parses a typed number: dot or comma as the decimal mark; "2,500" (comma + exactly 3 digits)
 * is read as a thousands separator. Energy is converted from kWh to Wh. NaN when invalid.
 */
export function parseInputValue(metric: MetricId, text: string): number {
  let txt = text.trim().replace(/\s+/g, '');
  if (/^-?\d{1,3}(,\d{3})+(\.\d+)?$/.test(txt)) txt = txt.replace(/,/g, '');
  else txt = txt.replace(',', '.');
  if (!/^-?(\d+\.?\d*|\.\d+)$/.test(txt)) return NaN;
  const n = Number(txt);
  if (!Number.isFinite(n)) return NaN;
  return METRIC_BY_ID[metric].unit === 'Wh' ? Math.round(n * 1000) : n;
}

/** 'hhmm' -> '17:00' ('বিকাল ৫:০০' in Bangla). */
export function fmtHhmm(hhmm: string | undefined): string {
  return hhmm && hhmm.length === 4 ? fmtClock(Number(hhmm.slice(0, 2)), Number(hhmm.slice(2))) : '–';
}

export function fmtSecs(sec: number | undefined): string {
  if (!isNumber(sec) || sec <= 0) return t('common.atOnce');
  if (sec < 60) return t('u.s', { n: sec });
  if (sec < 3600) return t('u.min', { n: Math.round(sec / 60) });
  const h = sec / 3600;
  return Number.isInteger(h) ? t('u.h', { n: h }) : t('u.hmin', { h: Math.floor(h), m: Math.round((sec % 3600) / 60) });
}

export function fmtMins(min: number | undefined): string {
  if (!isNumber(min)) return '–';
  if (min < 60) return t('u.min', { n: min });
  if (min % 1440 === 0) return t('u.d', { n: min / 1440 });
  if (min % 60 === 0) return t('u.h', { n: min / 60 });
  return t('u.hmin', { h: Math.floor(min / 60), m: min % 60 });
}

export function fmtDays(mask: number): string {
  if (mask === 127) return t('days.every');
  if (mask === 31) return t('days.sunThu');
  if (mask === 96) return t('days.friSat');
  return [0, 1, 2, 3, 4, 5, 6].filter((i) => mask & (1 << i)).map(weekdayShort).join(', ');
}

export function fmtWindow(r: Pick<AlertRule, 'win'>): string {
  if (!r.win) return t('win.allDay');
  return `${fmtHhmm(r.win.from)}–${fmtHhmm(r.win.to)}${r.win.days === 127 ? '' : ` (${fmtDays(r.win.days)})`}`;
}

/** ' today' / ' this cycle' after a metric name ('' for instantaneous rules). */
export function scopeWord(scope: RuleScope): string {
  return scope === 'day' ? t('scope.day') : scope === 'month' ? t('scope.month') : '';
}

/** "Load power > 2,500 W for 5 min" */
export function conditionText(r: Pick<AlertRule, 'metric' | 'scope' | 'op' | 'value' | 'mode' | 'at' | 'forSec'>): string {
  const m = metricDef(r.metric);
  const label = metricLabel(r.metric);
  if (m?.kind === 'bool') return `${label}: ${r.value ? t('common.yes') : t('common.no')}${r.forSec ? t('cond.for', { d: fmtSecs(r.forSec) }) : ''}`;
  const base = `${label}${scopeWord(r.scope)} ${OP_SYMBOL[r.op] ?? r.op} ${fmtValue(r.metric, r.value, { est: false })}`;
  if (r.mode === 'at') return t('cond.at', { base, t: fmtHhmm(r.at) });
  if (r.scope === 'inst' && r.forSec) return `${base}${t('cond.for', { d: fmtSecs(r.forSec) })}`;
  return base;
}

/** "E-mail, ntfy · 06:00–23:00 · cooldown 1 h" */
export function channelsText(r: AlertRule): string {
  const ch = [r.email ? t('common.email') : '', r.ntfy ? 'ntfy' : ''].filter(Boolean).join(', ');
  const parts = [ch || t('ch.none')];
  if (r.win) parts.push(fmtWindow(r));
  if (isLatched(r)) parts.push(r.scope === 'month' ? t('ch.oncePerCycle') : t('ch.oncePerDay'));
  else if (r.email) parts.push(t('ch.cooldown', { d: fmtMins(r.coolMin ?? 60) }));
  return parts.join(' · ');
}

/** Subject the mailer will use (r8 §4.5 open / at formats, prefixed with the subject tag). Always English, like the e-mail. */
export function previewSubject(r: Pick<AlertRule, 'name' | 'metric' | 'scope' | 'op' | 'value' | 'mode' | 'at'>, v: number | null | undefined): string {
  return inEnglish(() => previewSubjectEn(r, v));
}

function previewSubjectEn(r: Pick<AlertRule, 'name' | 'metric' | 'scope' | 'op' | 'value' | 'mode' | 'at'>, v: number | null | undefined): string {
  const m = metricDef(r.metric);
  const est = m?.est ? ', est.' : '';
  const val = isNumber(v) ? fmtValue(r.metric, v, { est: false }) : '…';
  const thr = fmtValue(r.metric, r.value, { est: false });
  const name = r.name || '(no name)';
  if (r.mode === 'at') return `${SUBJECT_TAG} ${name}: ${val} at ${fmtHhmm(r.at)} (limit ${thr}${est})`;
  return `${SUBJECT_TAG} ${name}: now ${val} (limit ${thr}${scopeWord(r.scope)}${est})`;
}

/** The e-mail body lines of the preview (English, like the mailer's e-mail). */
export function previewLines(rid: string | null, r: Pick<AlertRule, 'sev'>, nowEpoch: number): string[] {
  return inEnglish(() => [`When: ${fmtDateTime(nowEpoch)} (Asia/Dhaka)`, `Severity: ${SEVERITY_NAMES[r.sev] ?? r.sev}${rid ? ` | rule ${rid}` : ''}`]);
}

// ------------------------------------------------------------------ current values (what the logger compares)

function gridPresent(l: Live): boolean {
  if (l.wf && l.wf[5] === '1') return false; // QPIWS a5 line fail (Aggregator::gridPresent)
  return isNumber(l.gv) && l.gv >= 90;
}

function acChargeW(l: Live): number {
  const st = l.st ?? '';
  if (st.length !== 8 || st[7] !== '1') return 0; // b0: AC charging
  if (!isNumber(l.bv) || !isNumber(l.bci)) return 0;
  const p = l.bv * l.bci - (st[6] === '1' && isNumber(l.pvw) ? l.pvw : 0);
  return p > 0 ? p : 0;
}

/** Instantaneous value of a metric from `live` (null = not available). Mirrors r8 §4.3 "Source on the device". */
export function instValue(metric: MetricId, l: Live | null | undefined): number | null {
  if (!l) return null;
  const n = (v: number | undefined) => (isNumber(v) ? v : null);
  switch (metric) {
    case 'w':
      return n(l.w);
    case 'pvw':
      return n(l.pvw);
    case 'gw':
      if (isNumber(l.gw)) return l.gw;
      return gridPresent(l) && l.md === 'L' && isNumber(l.w) ? l.w + acChargeW(l) : 0;
    case 'bdw':
      return isNumber(l.bw) ? Math.max(0, -l.bw) : null;
    case 'bw':
      return n(l.bw);
    case 'soc':
      return n(l.soc);
    case 'ld':
      return n(l.ld);
    case 'bv':
      return n(l.bv);
    case 'gv':
      return n(l.gv);
    case 'ov':
      return n(l.ov);
    case 'pvv':
      return n(l.pvv);
    case 'bdi':
      return n(l.bdi);
    case 'tmp':
      return isNumber(l.tmp) && l.tmp <= 150 ? l.tmp : null; // > 150 = raw ADC reading
    case 'gf':
      return n(l.gf);
    case 'onbat':
      return l.md ? (l.md === 'B' ? 1 : 0) : null;
    case 'nogrid':
      return gridPresent(l) ? 0 : 1;
    default:
      return null;
  }
}

export interface ValueSources {
  live: Live | null | undefined;
  today: Daily | null | undefined;
  /** Sums of daily/<cycle start .. yesterday>, or null while not loaded. */
  cycle: Partial<Record<MetricId, number>> | null;
}

/** The value the logger compares for this rule right now (best effort from what the app has). */
export function currentValue(metric: MetricId, scope: RuleScope, src: ValueSources): number | null {
  if (scope === 'inst') return instValue(metric, src.live);
  const key = metric as keyof Daily & keyof Live;
  const todayV = isNumber(src.today?.[key]) ? (src.today![key] as number) : metric !== 'gout' && isNumber(src.live?.[key]) ? (src.live![key] as number) : null;
  if (scope === 'day') return todayV;
  if (!src.cycle) return null;
  return (src.cycle[metric] ?? 0) + (todayV ?? 0);
}

/** First local date of the billing cycle that contains `today` (r8 §4.5 cycleStart). */
export function cycleStart(today: string, startDay: number): string {
  const day = Number(today.slice(8, 10));
  const d = String(startDay).padStart(2, '0');
  if (day >= startDay) return `${today.slice(0, 7)}-${d}`;
  const [from] = monthBounds(today.slice(0, 7));
  const prev = addDays(from, -1).slice(0, 7);
  return `${prev}-${d}`;
}

export function sumDaily(days: Record<string, Daily>, before: string): Partial<Record<MetricId, number>> {
  const out: Partial<Record<MetricId, number>> = {};
  for (const [d, v] of Object.entries(days)) {
    if (d >= before) continue;
    for (const k of ['eld', 'egr', 'epv', 'edis', 'ech', 'gout'] as const) if (isNumber(v[k])) out[k] = (out[k] ?? 0) + v[k]!;
  }
  return out;
}

// ------------------------------------------------------------------ state badges and e-mail chips

type Tone = 'ok' | 'warn' | 'bad' | 'info' | 'muted';
/** { label, tone } whose label follows the UI language. */
const B = (key: Key, tone: Tone) => ({
  tone,
  get label() {
    return t(key);
  },
});

export const STATE_BADGE: Record<RuleStateId, { label: string; tone: Tone }> = {
  off: B('state.off', 'muted'),
  wait: B('state.wait', 'muted'),
  idle: B('state.idle', 'ok'),
  pend: B('state.pend', 'warn'),
  fire: B('state.fire', 'bad'),
  clr: B('state.clr', 'info'),
  latch: B('state.latch', 'bad'),
  done: B('state.done', 'ok'),
  inv: B('state.inv', 'bad'),
};

/** History chips for events/<d>/<k>/em (r8 §6.5). */
export const EM_CHIP: Record<string, { label: string; tone: Tone }> = {
  q: B('emc.q', 'info'),
  sent: B('emc.sent', 'ok'),
  cool: B('emc.cool', 'muted'),
  rcap: B('emc.rcap', 'warn'),
  cap: B('emc.cap', 'warn'),
  quota: B('emc.quota', 'warn'),
  off: B('emc.off', 'muted'),
  norc: B('emc.norc', 'warn'),
  exp: B('emc.exp', 'warn'),
  fail: B('emc.fail', 'bad'),
  skip: B('emc.skip', 'muted'),
};

/** Builds the mailer status summary the Rules header and the E-mail card show. */
export function mailerHealth(hb: number | undefined, now: number): 'none' | 'ok' | 'stale' {
  if (!isNumber(hb)) return 'none';
  return now - hb > MAILER_STALE_SEC ? 'stale' : 'ok';
}

export const isPermissionDenied = (e: unknown) => /permission[_ ]denied/i.test(e instanceof Error ? e.message : String(e)) || (e as { code?: string })?.code === 'PERMISSION_DENIED';

/** User-facing text for a failed alerts write. */
export function writeErrorText(e: unknown): string {
  if (isPermissionDenied(e))
    return t('err.permission');
  return e instanceof Error ? e.message : String(e);
}
