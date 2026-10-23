// History reads with caching (d1 §6.4):
// - today's minutes live in memory for the session (Today page re-attaches
//   with startAfter(lastKey), so a re-open costs only the new minutes);
// - finished days are cached in IndexedDB. d1 says "date < today"; we wait
//   until the day is 2+ days old (or complete with 1440 minutes) because the
//   logger back-fills minutes it could not upload during an outage.
import type { DataSource } from './source';
import type { Daily, HistRec } from './types';
import { addDays, addMonths, daysBetween, monthBounds, monthKey, todayKey } from '../lib/time';
import { idbGet, idbSet } from '../lib/idb';

const mem = new Map<string, Record<string, HistRec>>();

function immutable(date: string, count?: number): boolean {
  const age = daysBetween(date, todayKey());
  return age >= 2 || (age >= 1 && (count ?? 0) >= 1440);
}

function cacheKey(src: DataSource, kind: string, key: string) {
  return `${src.kind}:${src.deviceId}:${kind}:${key}`;
}

async function cachedDay(src: DataSource, kind: 'hist' | 'h15', date: string, fetcher: () => Promise<Record<string, HistRec>>) {
  const k = cacheKey(src, kind, date);
  const m = mem.get(k);
  if (m && immutable(date, Object.keys(m).length)) return m;
  if (src.kind === 'firebase' && daysBetween(date, todayKey()) >= 1) {
    const hit = await idbGet<Record<string, HistRec>>(k);
    if (hit) {
      mem.set(k, hit);
      return hit;
    }
  }
  const data = await fetcher();
  mem.set(k, data);
  if (src.kind === 'firebase' && immutable(date, kind === 'hist' ? Object.keys(data).length : 0)) void idbSet(k, data);
  return data;
}

export function getHistDay(src: DataSource, date: string) {
  return cachedDay(src, 'hist', date, () => src.getHist(date));
}

export function getH15Day(src: DataSource, date: string) {
  return cachedDay(src, 'h15', date, () => src.getH15(date));
}

/** Week view: h15 for the 7 days ending at `endDate`. */
export async function getWeek(src: DataSource, endDate: string) {
  const dates = Array.from({ length: 7 }, (_, i) => addDays(endDate, i - 6));
  const days = await Promise.all(dates.map((d) => getH15Day(src, d)));
  return dates.map((d, i) => ({ date: d, recs: days[i] }));
}

/** daily/{from..to}, cached per finished month. */
export async function getDailyMonth(src: DataSource, ym: string): Promise<Record<string, Daily>> {
  const [from, to] = monthBounds(ym);
  const k = cacheKey(src, 'dailyMonth', ym);
  const finished = daysBetween(to, todayKey()) >= 2;
  if (finished && src.kind === 'firebase') {
    const hit = await idbGet<Record<string, Daily>>(k);
    if (hit) return hit;
  }
  const data = await src.getDailyRange(from, to);
  if (finished && src.kind === 'firebase') void idbSet(k, data);
  return data;
}

export async function getDailyYear(src: DataSource, year: string): Promise<Record<string, Daily>> {
  const months = Array.from({ length: 12 }, (_, i) => `${year}-${String(i + 1).padStart(2, '0')}`).filter(
    (ym) => `${ym}-01` <= todayKey(),
  );
  const parts = await Promise.all(months.map((ym) => getDailyMonth(src, ym)));
  return Object.assign({}, ...parts) as Record<string, Daily>;
}

/** daily/{from..to} across months (billing months span two), from the per-month cache. */
export async function getDailySpan(src: DataSource, from: string, to: string): Promise<Record<string, Daily>> {
  const months: string[] = [];
  for (let ym = monthKey(from); ym <= monthKey(to); ym = addMonths(ym, 1)) months.push(ym);
  const parts = await Promise.all(months.map((ym) => getDailyMonth(src, ym)));
  const out: Record<string, Daily> = {};
  for (const p of parts) for (const [d, v] of Object.entries(p)) if (d >= from && d <= to) out[d] = v;
  return out;
}

/** Session cache for today's minutes (Today page). */
export const todayMem = {
  date: '',
  recs: {} as Record<string, HistRec>,
  lastKey: null as string | null,
  reset(date: string) {
    this.date = date;
    this.recs = {};
    this.lastKey = null;
  },
};
