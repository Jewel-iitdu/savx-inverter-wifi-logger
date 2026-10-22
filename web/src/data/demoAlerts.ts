// Demo mode (?demo=1) for the Alerts page: a fake alerts/ document, and a minute-by-minute replay
// of the demo simulation through a simplified copy of the logger's rule engine (r8 §4.4-4.6), so
// rule events, e-mail chips, alertState badges and the mailer heartbeat look like the real thing.
// Every value is FAKE. The real evaluation happens on the ESP32, never in the browser.
import type { MetricId } from '../alertMetrics';
import type { AlertRule, AlertStateRec, AlertsDoc, EmailCfg, EventRec, MailerStatus } from './types';
import { clearLevel, fmtValue, isBool, parseRecipients, withEmailDefaults } from './alerts';
import { keyToEpoch, hhmmFromMinute } from '../lib/time';
import { inEnglish } from '../lib/i18n';

/** The per-minute values the demo simulation produces (demoSource.ts MinuteState). */
export interface DemoMinute {
  w: number;
  pvw: number;
  pvv: number;
  gv: number;
  bv: number;
  bi: number;
  bw: number;
  soc: number;
  md: string;
  tmp: number;
  ov: number;
  gridOk: boolean;
  egrW: number;
}

export type DemoEvent = EventRec & { minute: number; kind: 'o' | 'c' | 'r' | 'i'; src: string };

const UPD = Date.UTC(2026, 9, 20, 2, 12) as number; // a fixed "last edited" time for the seed rules

export function demoAlertsSeed(): AlertsDoc {
  const email: EmailCfg = {
    on: true,
    to: 'owner@example.com,family@example.com',
    fromName: 'Solar logger',
    maxPerDay: 40,
    monthStart: 1,
    builtin: { minSev: 4, clear: true },
    summary: { on: true, hour: 7 },
    offline: { on: true, min: 30, clear: true },
  };
  const rules: Record<string, AlertRule> = {
    r01: { name: 'Daily grid usage > 3 kWh', on: true, metric: 'egr', scope: 'day', op: 'gt', value: 3000, mode: 'hold', maxDay: 1, sev: 3, email: true, ntfy: false, upd: UPD },
    r02: { name: 'Load > 2500 W for 5 min', on: true, metric: 'w', scope: 'inst', op: 'gt', value: 2500, mode: 'hold', forSec: 300, clr: 2200, clrSec: 120, coolMin: 60, maxDay: 3, sev: 3, email: true, ntfy: true, clrMail: true, upd: UPD },
    r03: { name: 'SOC < 30 %', on: true, metric: 'soc', scope: 'inst', op: 'lt', value: 30, mode: 'hold', forSec: 60, clr: 35, clrSec: 300, coolMin: 120, maxDay: 3, sev: 4, email: true, ntfy: false, clrMail: true, rc: 1, upd: UPD },
    r04: { name: 'PV today < 3 kWh by 17:00', on: true, metric: 'epv', scope: 'day', op: 'lt', value: 3000, mode: 'at', at: '1700', minCov: 80, maxDay: 1, sev: 2, email: true, ntfy: false, upd: UPD },
    r05: { name: 'Evening peak load > 1500 W', on: true, metric: 'w', scope: 'inst', op: 'gt', value: 1500, mode: 'hold', forSec: 60, clr: 1300, clrSec: 120, coolMin: 60, maxDay: 2, win: { from: '1800', to: '2300', days: 127 }, sev: 3, email: true, ntfy: false, upd: UPD },
    r06: { name: 'Monthly grid usage > 150 kWh', on: true, metric: 'egr', scope: 'month', op: 'gt', value: 150000, mode: 'hold', maxDay: 1, sev: 3, email: true, ntfy: false, upd: UPD },
    r07: { name: 'Inverter temp > 70 °C', on: false, metric: 'tmp', scope: 'inst', op: 'gt', value: 70, mode: 'hold', forSec: 120, clr: 65, clrSec: 300, coolMin: 60, maxDay: 3, sev: 4, email: true, ntfy: true, clrMail: true, upd: UPD },
  };
  return { email, rules };
}

function instAt(metric: MetricId, m: DemoMinute): number {
  switch (metric) {
    case 'w':
      return m.w;
    case 'pvw':
      return m.pvw;
    case 'gw':
      return m.md === 'L' && m.gridOk ? m.w : 0;
    case 'bdw':
      return Math.max(0, -m.bw);
    case 'bw':
      return m.bw;
    case 'soc':
      return m.soc;
    case 'ld':
      return (m.w / 3200) * 100;
    case 'bv':
      return m.bv;
    case 'gv':
      return m.gv;
    case 'ov':
      return m.ov;
    case 'pvv':
      return m.pvv;
    case 'bdi':
      return Math.max(0, -m.bi);
    case 'tmp':
      return m.tmp;
    case 'gf':
      return m.gridOk ? 50 : 0;
    case 'onbat':
      return m.md === 'B' ? 1 : 0;
    case 'nogrid':
      return m.gridOk ? 0 : 1;
    default:
      return NaN;
  }
}

const cmp = (op: AlertRule['op'], v: number, t: number) => (op === 'gt' ? v > t : op === 'ge' ? v >= t : op === 'lt' ? v < t : v <= t);

function dayOfWeek(date: string): number {
  const [y, m, d] = date.split('-').map(Number);
  return (Math.floor(Date.UTC(y, m - 1, d) / 86400000) + 4) % 7; // 1970-01-01 was a Thursday; 0 = Sunday
}

function inWindow(r: AlertRule, minute: number, dow: number): boolean {
  if (!r.win) return true;
  const from = Number(r.win.from.slice(0, 2)) * 60 + Number(r.win.from.slice(2));
  const to = Number(r.win.to.slice(0, 2)) * 60 + Number(r.win.to.slice(2));
  const bit = (d: number) => (r.win!.days & (1 << ((d + 7) % 7))) !== 0;
  if (from < to) return minute >= from && minute < to && bit(dow);
  return (minute >= from && bit(dow)) || (minute < to && bit(dow - 1));
}

export interface ReplayResult {
  events: DemoEvent[];
  states: Record<string, AlertStateRec>;
}

/**
 * Replays one local day (minutes 0..avail-1) through every rule. Day-level counters (maxDay,
 * events per rule) reset at local midnight like the logger's; cooldowns do not cross days here.
 */
export function replayRules(date: string, minutes: DemoMinute[], avail: number, alerts: AlertsDoc, cycleEgrBefore: Partial<Record<MetricId, number>>): ReplayResult {
  const email = withEmailDefaults(alerts.email);
  const dow = dayOfWeek(date);
  const at = (minute: number) => keyToEpoch(date, hhmmFromMinute(minute)) + 23;
  const events: DemoEvent[] = [];
  const states: Record<string, AlertStateRec> = {};
  const cum: Record<string, number[]> = { eld: [], egr: [], epv: [], edis: [], ech: [], gout: [] };
  let s = { eld: 0, egr: 0, epv: 0, edis: 0, ech: 0, gout: 0 };
  for (let t = 0; t < avail; t++) {
    const m = minutes[t];
    s = { eld: s.eld + m.w / 60, egr: s.egr + m.egrW / 60, epv: s.epv + m.pvw / 60, edis: s.edis + Math.max(0, -m.bw) / 60, ech: s.ech + Math.max(0, m.bw) / 60, gout: s.gout + (m.gridOk ? 0 : 1) };
    for (const k of Object.keys(cum)) cum[k].push(s[k as keyof typeof s]);
  }

  for (const [rid, r] of Object.entries(alerts.rules ?? {})) {
    if (!r.on) {
      states[rid] = { st: 'off' };
      continue;
    }
    let st: 'idle' | 'pend' | 'fire' | 'clr' | 'latch' | 'done' = 'idle';
    let since = at(0) - 23;
    let t0 = 0;
    let t1 = 0;
    let openT = 0;
    let lastMail = -Infinity;
    let cnt = 0;
    let evCnt = 0;
    let openMailed = false;
    let lastVal: number | undefined;
    // The logger writes its event messages in English whatever the app language is.
    const fmt = (v: number) => inEnglish(() => fmtValue(r.metric, v, { est: false }));
    let lastMailMinute = -1e9;
    // Device gates in r8 §4.6 order: channel + master switch, cooldown, rule cap (the 40/day job cap never bites here).
    const gate = (clear: boolean, minute: number): string | undefined => {
      if (!r.email || !email.on) return undefined;
      if (clear) return r.clrMail && openMailed ? 'sent' : undefined;
      if (minute - lastMailMinute < (r.coolMin ?? 60)) return 'cool';
      if (cnt >= (r.maxDay ?? 3)) return 'rcap';
      return 'sent';
    };
    const emit = (minute: number, kind: 'o' | 'c', v: number, msg: string, dur?: number) => {
      if (evCnt >= 20) return;
      evCnt++;
      const em = gate(kind === 'c', minute);
      if (kind === 'o') {
        if (em === 'sent') {
          cnt++;
          lastMail = at(minute);
          lastMailMinute = minute;
          openMailed = true;
        } else openMailed = false;
      } else openMailed = false;
      events.push({ minute, kind, src: rid, ts: at(minute), type: 'RULE', sev: r.sev, st: kind === 'o' ? 'open' : 'clear', rid, val: Math.round(v * 100) / 100, thr: r.value, msg, ch: [r.email ? 'email' : '', r.ntfy ? 'ntfy' : ''].filter(Boolean).join(','), ...(dur ? { dur } : {}), ...(em ? { em } : {}) });
    };

    if (r.scope === 'month') {
      // Month-to-date: earlier days of the cycle + today. No event replay (it would span days).
      const mtd = (cycleEgrBefore[r.metric] ?? 0) + (cum[r.metric]?.[avail - 1] ?? 0);
      lastVal = mtd;
      states[rid] = cmp(r.op, mtd, r.value) ? { st: 'latch', since: at(Math.max(0, avail - 30)), val: Math.round(mtd), cnt: 1 } : { st: 'idle', since, val: Math.round(mtd) };
      continue;
    }
    if (r.scope === 'day') {
      const series = cum[r.metric] ?? [];
      if (r.mode === 'at') {
        const atMin = Number(r.at!.slice(0, 2)) * 60 + Number(r.at!.slice(2));
        if (avail > atMin) {
          const v = series[atMin] ?? 0;
          lastVal = v;
          if (cmp(r.op, v, r.value)) {
            emit(atMin, 'o', v, `${r.name}: ${fmt(v)} at ${r.at!.slice(0, 2)}:${r.at!.slice(2)} (limit ${fmt(r.value)})`);
            st = 'latch';
          } else st = 'done';
          since = at(atMin);
        }
      } else {
        for (let t = 0; t < avail && st === 'idle'; t++) {
          if (!inWindow(r, t, dow)) continue;
          const v = series[t];
          if (cmp(r.op, v, r.value)) {
            lastVal = v;
            emit(t, 'o', v, `${r.name}: now ${fmt(v)} (limit ${fmt(r.value)} today${r.metric === 'egr' ? ', est.' : ''})`);
            st = 'latch';
            since = at(t);
          }
        }
        if (st === 'idle') lastVal = series[avail - 1];
      }
      states[rid] = { st, since, ...(lastVal !== undefined ? { val: Math.round(lastVal) } : {}), cnt, ...(lastMail > -Infinity ? { last: lastMail } : {}) };
      continue;
    }

    // inst rules, hold mode (minute resolution)
    const forMin = Math.round((r.forSec ?? 60) / 60);
    const clrMin = Math.round((r.clrSec ?? 60) / 60);
    const bool = isBool(r.metric);
    const level = clearLevel(r);
    for (let t = 0; t < avail; t++) {
      const v = instAt(r.metric, minutes[t]);
      lastVal = v;
      const win = inWindow(r, t, dow);
      const cond = bool ? v === r.value : cmp(r.op, v, r.value);
      const clr = bool ? v !== r.value : r.op === 'gt' || r.op === 'ge' ? v <= level : v >= level;
      if (st === 'idle') {
        if (win && cond) {
          if (forMin === 0) {
            st = 'fire';
            openT = t;
            since = at(t);
            emit(t, 'o', v, `${r.name}: now ${fmt(v)} (limit ${fmt(r.value)})`);
          } else {
            st = 'pend';
            t0 = t;
            since = at(t);
          }
        }
      } else if (st === 'pend') {
        if (!cond || !win) {
          st = 'idle';
          since = at(t);
        } else if (t - t0 >= forMin) {
          st = 'fire';
          openT = t;
          since = at(t);
          emit(t, 'o', v, `${r.name}: now ${fmt(v)} (limit ${fmt(r.value)})`);
        }
      } else if (st === 'fire' || st === 'clr') {
        if (!win) {
          st = 'idle';
          since = at(t);
          openMailed = false;
        } else if (st === 'fire' && clr) {
          st = 'clr';
          t1 = t;
        } else if (st === 'clr' && !clr) st = 'fire';
        else if (st === 'clr' && t - t1 >= clrMin) {
          emit(t, 'c', v, `${r.name}: back to ${fmt(v)} (clear level ${fmt(level)})`, (t - openT) * 60);
          st = 'idle';
          since = at(t);
        }
      }
    }
    states[rid] = { st, since, ...(lastVal !== undefined && Number.isFinite(lastVal) ? { val: Math.round(lastVal * 10) / 10 } : {}), cnt, ...(lastMail > -Infinity ? { last: lastMail } : {}) };
  }
  return { events, states };
}

/** em for a built-in event per alerts/email (r8 §4.7). */
export function builtinEm(e: EventRec, alerts: AlertsDoc, openMailed: Set<string>): string | undefined {
  const email = withEmailDefaults(alerts.email);
  if (!email.on) return undefined;
  if (e.type === 'TEST_ALERT') return 'sent';
  if (e.type === 'DAILY_SUMMARY') return undefined;
  const key = e.type === 'GRID_RESTORED' ? 'GRID_LOSS' : e.type;
  if (e.st === 'open' && e.sev >= email.builtin.minSev && email.builtin.minSev > 0) {
    openMailed.add(key);
    return 'sent';
  }
  if (e.st === 'clear' && email.builtin.clear && openMailed.has(key)) {
    openMailed.delete(key);
    return 'sent';
  }
  return undefined;
}

/** Mailer heartbeat consistent with today's e-mailed events. */
export function demoMailer(now: number, today: string, sentEvents: EventRec[], alerts: AlertsDoc, test: MailerStatus['test'] | undefined): MailerStatus {
  const to = parseRecipients(alerts.email?.to);
  const recipientsOf = (e: EventRec) => {
    const rc = e.rid ? alerts.rules?.[e.rid]?.rc : undefined;
    return rc ? to.filter((_, i) => rc & (1 << i)).length : to.length;
  };
  const sent = sentEvents.reduce((n, e) => n + recipientsOf(e), 0) + (test?.ok ? to.length : 0);
  const last = [...sentEvents].sort((a, b) => (b.emt ?? 0) - (a.emt ?? 0))[0];
  let hb = now - (now % 300) + 37;
  if (hb > now) hb -= 300;
  if (test && test.ts + 3 <= now) hb = Math.max(hb, test.ts + 3); // the run that answered the test
  return {
    hb,
    ver: '1.0.0',
    day: today,
    sent,
    quota: Math.max(0, 100 - sent),
    off: 0,
    err: '',
    warn: '',
    ...(last ? { last: { ts: last.emt ?? last.ts, ok: true, msg: `[Solar] ${last.msg}`.slice(0, 100) } } : {}),
    ...(test ? { test } : {}),
  };
}
