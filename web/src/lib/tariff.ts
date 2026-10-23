// Grid electricity cost and the money solar saved, on the Bangladesh residential (LT-A) slab tariff.
// Rates from the owner (2026-10-23), ৳ per kWh (unit):
//   lifeline: a month of at most 50 units is billed entirely at 4.63;
//   otherwise each slab at its own rate: 1–75 5.26, 76–200 8.50, 201–300 9.10, 301–400 9.62, 401–600 15.01, above 600 17.35.
// Energy charge only: the demand charge, meter rent and 5 % VAT are not included.
// The lifeline is decided by the whole month, so each day is priced with its month's rule (lifeline or slabs; the
// running month: so far). Pricing day by day with billTk instead would put a ~৳31 jump on the day a month passes
// 50 units.
// The slabs restart every billing month (alerts/email/monthStart, also used by "this billing month" rules).
// Saved by solar = bill(grid + solar) − bill(grid): what the month would have cost with the solar energy bought
// from the grid too. A day's share is how much both bills grew that day, so the days of a billing month add up
// exactly to its bill and its saving. Only a type import, so `node` can run this file directly.
import type { Daily } from '../data/types';

export const LIFELINE_KWH = 50;
export const LIFELINE_RATE = 4.63;
/** [up to kWh, ৳ per kWh] */
export const SLABS: ReadonlyArray<readonly [number, number]> = [
  [75, 5.26],
  [200, 8.5],
  [300, 9.1],
  [400, 9.62],
  [600, 15.01],
  [Infinity, 17.35],
];

/** Energy charge of a billing month with `kwh` units from the grid. */
export function billTk(kwh: number): number {
  if (!(kwh > 0)) return 0;
  return kwh <= LIFELINE_KWH ? kwh * LIFELINE_RATE : slabTk(kwh);
}

/** Slab charge without the lifeline rule. */
function slabTk(kwh: number): number {
  if (!(kwh > 0)) return 0;
  let tk = 0;
  let lo = 0;
  for (const [hi, rate] of SLABS) {
    if (kwh <= lo) break;
    tk += (Math.min(kwh, hi) - lo) * rate;
    lo = hi;
  }
  return tk;
}

/** ৳ per kWh of the next unit after `kwh` units this month. */
export function rateAt(kwh: number): number {
  if (kwh < LIFELINE_KWH) return LIFELINE_RATE;
  for (const [hi, rate] of SLABS) if (kwh < hi) return rate;
  return SLABS[SLABS.length - 1][1];
}

export interface Money {
  grid: number; // kWh from the grid
  solar: number; // kWh from solar
  gridTk: number; // ৳ paid for the grid energy
  savedTk: number; // ৳ the solar energy would have cost
}

export const ZERO: Money = { grid: 0, solar: 0, gridTk: 0, savedTk: 0 };

export function addMoney(a: Money, b: Money): Money {
  return { grid: a.grid + b.grid, solar: a.solar + b.solar, gridTk: a.gridTk + b.gridTk, savedTk: a.savedTk + b.savedTk };
}

/** The month's pricing: lifeline for the whole month when it stays within 50 units, else the slabs. */
const priceFor = (monthKwh: number) => (monthKwh <= LIFELINE_KWH ? (k: number) => k * LIFELINE_RATE : slabTk);

/**
 * Per-day cost and saving from daily/ totals (Wh). Days are grouped by `cycleOf(date)` (the first day of the date's
 * billing month) and walked in order, so `days` must contain every recorded day from the billing-month start of the
 * first date you show. The days of a month add up to billTk(month). Missing egr/epv count as 0.
 */
export function dayMoney(days: Record<string, Pick<Daily, 'egr' | 'epv'>>, cycleOf: (date: string) => string): Record<string, Money> {
  const kwh = (v: number | undefined) => Math.max(0, v ?? 0) / 1000;
  const dates = Object.keys(days).sort();
  const month = new Map<string, { g: number; s: number }>();
  for (const d of dates) {
    const m = month.get(cycleOf(d)) ?? { g: 0, s: 0 };
    m.g += kwh(days[d].egr);
    m.s += kwh(days[d].epv);
    month.set(cycleOf(d), m);
  }
  const out: Record<string, Money> = {};
  let cycle = '';
  let g = 0;
  let s = 0;
  let gridP = slabTk;
  let allP = slabTk;
  for (const date of dates) {
    const c = cycleOf(date);
    if (c !== cycle) {
      cycle = c;
      g = s = 0;
      const m = month.get(c)!;
      gridP = priceFor(m.g);
      allP = priceFor(m.g + m.s);
    }
    const dg = kwh(days[date].egr);
    const ds = kwh(days[date].epv);
    const gridTk = gridP(g + dg) - gridP(g);
    const savedTk = allP(g + dg + s + ds) - allP(g + s) - gridTk;
    out[date] = { grid: dg, solar: ds, gridTk, savedTk };
    g += dg;
    s += ds;
  }
  return out;
}

/** Sum of dayMoney entries whose date is in [from, to]. */
export function sumMoney(m: Record<string, Money>, from: string, to: string): Money {
  let tot = ZERO;
  for (const [d, v] of Object.entries(m)) if (d >= from && d <= to) tot = addMoney(tot, v);
  return tot;
}
