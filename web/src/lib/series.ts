// Turns minute / quarter-hour records into aligned uPlot arrays. Missing
// minutes become null so gaps stay visible (d1 §6.3).
import type { Daily, HistRec } from '../data/types';
import { keyToEpoch, minuteOfDay } from './time';
import { isNum } from './format';
import { toCsv, type CsvValue } from './csv';

export interface DaySeries {
  xs: number[];
  pv: Array<number | null>;
  load: Array<number | null>;
  batt: Array<number | null>;
  soc: Array<number | null>;
  bv: Array<number | null>;
  gv: Array<number | null>;
  ov: Array<number | null>;
  count: number;
}

const v = (x: number | undefined): number | null => (isNum(x) ? x : null);

/** One slot per `step` minutes from 00:00 to the last record (or `endMinute`). */
export function daySeries(date: string, recs: Record<string, HistRec>, step = 1, endMinute?: number): DaySeries {
  const keys = Object.keys(recs);
  const last = endMinute ?? keys.reduce((m, k) => Math.max(m, minuteOfDay(k)), -1);
  const slots = last < 0 ? 0 : Math.floor(last / step) + 1;
  const t0 = keyToEpoch(date, '0000');
  const s: DaySeries = {
    xs: Array.from({ length: slots }, (_, i) => t0 + i * step * 60),
    pv: new Array(slots).fill(null),
    load: new Array(slots).fill(null),
    batt: new Array(slots).fill(null),
    soc: new Array(slots).fill(null),
    bv: new Array(slots).fill(null),
    gv: new Array(slots).fill(null),
    ov: new Array(slots).fill(null),
    count: keys.length,
  };
  for (const k of keys) {
    const i = Math.floor(minuteOfDay(k) / step);
    if (i < 0 || i >= slots) continue;
    const r = recs[k];
    s.pv[i] = v(r.pvw);
    s.load[i] = v(r.w);
    s.batt[i] = isNum(r.bv) && isNum(r.bi) ? Math.round(r.bv * r.bi) : null;
    s.soc[i] = v(r.soc);
    s.bv[i] = v(r.bv);
    s.gv[i] = v(r.gv);
    s.ov[i] = v(r.ov);
  }
  return s;
}

/** Concatenate several days (week view from h15). */
export function multiDaySeries(days: Array<{ date: string; recs: Record<string, HistRec> }>, step: number): DaySeries {
  const parts = days.map((d) => daySeries(d.date, d.recs, step, 1439));
  const cat = <K extends keyof DaySeries>(k: K) => parts.flatMap((p) => p[k] as Array<number | null>);
  return {
    xs: cat('xs') as number[],
    pv: cat('pv'),
    load: cat('load'),
    batt: cat('batt'),
    soc: cat('soc'),
    bv: cat('bv'),
    gv: cat('gv'),
    ov: cat('ov'),
    count: parts.reduce((s, p) => s + p.count, 0),
  };
}

export interface EnergyTotals {
  epv: number;
  eld: number;
  egr: number;
  ech: number;
  edis: number;
  minutes: number;
}

export function totalsFromRecs(recs: Record<string, HistRec>): EnergyTotals {
  const t: EnergyTotals = { epv: 0, eld: 0, egr: 0, ech: 0, edis: 0, minutes: 0 };
  for (const r of Object.values(recs)) {
    t.epv += r.epv ?? 0;
    t.eld += r.eld ?? 0;
    t.egr += r.egr ?? 0;
    t.ech += r.ech ?? 0;
    t.edis += r.edis ?? 0;
    t.minutes += 1;
  }
  return t;
}

/** Share of the load NOT supplied by the grid (estimate, since egr is estimated). */
export function solarFraction(eld: number | undefined, egr: number | undefined): number | null {
  if (!isNum(eld) || eld <= 0 || !isNum(egr)) return null;
  return Math.max(0, Math.min(1, 1 - egr / eld));
}

export const HIST_FIELDS: Array<keyof HistRec> = [
  'w', 'wx', 'pvw', 'pvx', 'bv', 'bvn', 'bvx', 'bi', 'soc', 'gv', 'gvn', 'ov', 'ld', 'tmp', 'pvv', 'md', 'wn', 'fc', 'n', 'epv', 'eld', 'ech', 'edis', 'egr',
];

export function histCsv(date: string, recs: Record<string, HistRec>): string {
  const keys = Object.keys(recs).sort();
  const rows: CsvValue[][] = keys.map((k) => {
    const r = recs[k];
    return [date, `${k.slice(0, 2)}:${k.slice(2)}`, keyToEpoch(date, k), ...HIST_FIELDS.map((f) => r[f] as CsvValue)];
  });
  return toCsv(['date', 'time_local', 'epoch_utc', ...HIST_FIELDS], rows);
}

export const DAILY_FIELDS: Array<keyof Daily> = ['epv', 'eld', 'egr', 'ech', 'edis', 'epvi', 'pvx', 'wx', 'bvn', 'bvx', 'socn', 'socx', 'gout', 'nf', 'nw', 'n'];

export function dailyCsv(rows: Record<string, Daily>): string {
  const keys = Object.keys(rows).sort();
  return toCsv(
    ['date', ...DAILY_FIELDS.map((f) => (['epv', 'eld', 'egr', 'ech', 'edis', 'epvi'].includes(f) ? `${f}_wh` : f))],
    keys.map((d) => [d, ...DAILY_FIELDS.map((f) => rows[d][f] as CsvValue)]),
  );
}
