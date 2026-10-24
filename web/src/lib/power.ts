// Power display for inverters that send their AC output power as VA only (outputIsVA: the Sorotec REVO, 2026-10-22).
// The owner picks the unit in Settings (stored per browser, like the theme):
//  - 'W'  (default): output power estimated as VA × power factor. With the grid meter on, the load is MEASURED instead
//          (see "Measured load" below) and the power factor is only the fallback. It depends on the mode and the load:
//          0.48 on grid at night (198 W at the meter / 412 VA, 2026-10-24 01:40), about 1.0 on battery in the day
//          (7,674 Wh solar + battery vs 7,459 VAh, grid cut 2026-10-23). Default 0.90 fits the daytime cut.
//  - 'VA': load and grid as reported. Solar and battery are DC and always shown in W (the LCD shows PV in kW too).
// Only the AC output side is scaled: load power, the load part of grid power (fw 0.7.5+: grid = load + battery - PV,
// and only the load term is VA), and the load/grid energies. Solar and battery are never scaled.
// Grid meter (fw 0.9.0, cfg.meter on; PowerView.meter): a smart breaker on the grid line before the inverter reports
// real W and kWh. Where present, live `mw` replaces the grid power and `egm` (live and daily/) the grid energy, both
// unscaled, so every page and the electricity cost use them. cfg.meter = 0 or missing fields: the estimates as before.
// 2-second fusion (fw 0.9.1, `mage`): the meter refreshes W about every 15 s, the inverter its VA every 2 s. In line
// mode a payload with a fresh reading (mage <= METER_FRESH_AGE) gives k = meter load / inverter VA; between readings
// the load is VA(t) x k and the grid load + charging - solar, so both move every 2 s and snap back to the meter at
// each reading. k is kept for the session in this module (useLiveView), shared by every page that shows live power.
import { useMemo } from 'preact/hooks';
import { useApp } from '../context';
import { outputIsVA } from './decode';
import { clamp, isNum } from './format';
import { useStored } from './hooks';
import type { Daily, HistRec, Live } from '../data/types';

export type PowerUnit = 'W' | 'VA';
export const PF_DEFAULT = 0.9;
export const PF_MIN = 0.3; // the meter showed 0.48 for the night loads (2026-10-24)
export const PF_MAX = 1;

export interface PowerView {
  unit: PowerUnit; // unit of the AC output figures (load, grid); solar and battery are always W
  k: number; // factor applied to the AC output figures (1 = unchanged)
  est: boolean; // AC output figures are VA-based (load/grid kWh are estimates)
  meter: boolean; // cfg.meter !== 0: grid power and energy from the grid meter (mw, egm) where present
}

export const IDENTITY: PowerView = { unit: 'W', k: 1, est: false, meter: true };

export function parsePf(s: string | null | undefined): number {
  const v = Number(s);
  return Number.isFinite(v) && v >= PF_MIN && v <= PF_MAX ? v : PF_DEFAULT;
}

export function powerView(dia: string | undefined, unit: PowerUnit, pf: number, meter = true): PowerView {
  if (!outputIsVA(dia)) return { ...IDENTITY, meter }; // the inverter reports real W
  return unit === 'VA' ? { unit: 'VA', k: 1, est: true, meter } : { unit: 'W', k: pf, est: true, meter };
}

/** Settings: [unit, setUnit, pf text, setPf text]. */
export function usePowerPrefs() {
  const [unit, setUnit] = useStored<PowerUnit>('pwrUnit', 'W');
  const [pf, setPf] = useStored<string>('pwrPf', String(PF_DEFAULT));
  return { unit: unit === 'VA' ? 'VA' : ('W' as PowerUnit), setUnit, pf, setPf };
}

export function usePowerView(): PowerView {
  const { meta, meter } = useApp();
  const { unit, pf } = usePowerPrefs();
  return powerView(meta?.dia, unit, parsePf(pf), meter);
}

/** The view for the electricity bill: always W, since the meter bills kWh (VA × power factor on VA-only inverters). */
export function useBillView(): PowerView {
  const { meta, meter } = useApp();
  const { pf } = usePowerPrefs();
  return powerView(meta?.dia, 'W', parsePf(pf), meter);
}

/** The grid power in viewLive(l, p) is the meter's real W (show it in W, whatever the power unit). */
export const gridPowerMetered = (l: Live | null | undefined, p: PowerView): boolean => p.meter && isNum(l?.mw);
/** Line mode with the meter: viewLive's load is grid (meter) + solar - battery charging, in real W, not VA x pf.
 *  The inverter's VA read about 2x the real load at night (414 VA vs 200 W at the meter, 2026-10-24 01:40) and 8x
 *  the next evening (1,027 VA vs 126 W, power factor 0.37 at the meter, 2026-10-24 20:30). */
export const loadPowerMetered = (l: Live | null | undefined, p: PowerView): boolean =>
  p.meter && isNum(l?.mw) && l?.md === 'L';
// The battery's small discharge in line mode is the inverter's own standby draw, not load: only charging is taken off.
const meterLoad = (l: Live): number => Math.max(0, l.mw! + Math.max(0, l.pvw ?? 0) - Math.max(0, l.bw ?? 0));
// grid = load + battery charging - solar (the inverse of meterLoad)
const gridFromLoad = (l: Live, load: number): number => Math.max(0, load + Math.max(0, l.bw ?? 0) - Math.max(0, l.pvw ?? 0));

// Measured load (meter on): on grid = meter + solar - battery charging (meterLoad); on battery = solar + battery
// discharge - battery charging (dcLoad, both DC and measured: PV input V x A and the BMS). Both include the inverter's
// own use (~25 W standby and conversion losses). Day energy: grid (egm) + solar + discharge - charge, on metered days.
const dcLoad = (pvw: number, bw: number): number => Math.max(0, pvw - bw);
/** Battery mode with the meter on: viewLive's load is solar + battery (real W), not VA x pf. */
export const loadPowerDc = (l: Live | null | undefined, p: PowerView): boolean =>
  p.meter && l?.md === 'B' && isNum(l?.pvw) && isNum(l?.bw);
/** viewLive's load is measured (on grid from the meter, on battery from solar + battery). */
export const loadPowerMeasured = (l: Live | null | undefined, p: PowerView): boolean => loadPowerMetered(l, p) || loadPowerDc(l, p);
/** Load energy from measured energies (Wh): metered grid + solar + battery discharge - battery charge; null if one is missing. */
export function measuredEld(v: { egm?: number; epv?: number; edis?: number; ech?: number } | null | undefined): number | null {
  if (!v || !isNum(v.egm) || !isNum(v.epv) || !isNum(v.edis) || !isNum(v.ech)) return null;
  return Math.max(0, v.egm + v.epv + v.edis - v.ech);
}

/** A meter reading at most this old (live `mage`, s) shares its payload with an inverter VA within ~2 s of it. */
export const METER_FRESH_AGE = 3;
// 0.2 until 2026-10-24: the real ratio was 0.12 that evening, so between readings the app showed VA x 0.2 = 205 W for
// a 126 W load (and 126 W again at each reading)
export const METER_K_MIN = 0.05;
export const METER_K_MAX = 1;
/** A learned k is used for this long (s of live `ts`) after its reading: after a pause (app hidden, grid off) the
 *  load mix may have changed, so the meter's own value is shown until the next fresh reading. */
export const METER_K_TTL = 120;
const K_MIN_VA = 20; // below this the ratio is noise

/** k = meter load ÷ the inverter's output (live `w`: the VA on this inverter) from a payload with a fresh meter
 *  reading in line mode, clamped to [METER_K_MIN, 1]; null when the payload cannot give one. */
export function meterK(l: Live | null | undefined, p: PowerView): number | null {
  if (!l || !loadPowerMetered(l, p) || !isNum(l.mage) || l.mage > METER_FRESH_AGE || !isNum(l.w) || l.w < K_MIN_VA) return null;
  return clamp(meterLoad(l) / l.w, METER_K_MIN, METER_K_MAX);
}

/** viewLive(l, p, k) shows the 2-second load VA x k (and the grid from it) instead of the meter's 15-second value. */
export const powerFused = (l: Live | null | undefined, p: PowerView, k: number | null | undefined): boolean =>
  isNum(k) && !!l && loadPowerMetered(l, p) && isNum(l.mage) && l.mage > METER_FRESH_AGE && isNum(l.w);
/** The grid energy in viewLive/viewDaily(v, p) is the meter's (egm), not the estimate. */
export const gridEnergyMetered = (v: { egm?: number } | null | undefined, p: PowerView): boolean => p.meter && isNum(v?.egm);
/** Every day in `m` has metered grid energy (labels: "Grid (meter)" instead of "Grid (est.)"). */
export const allDaysMetered = (m: Record<string, Daily> | null | undefined, p: PowerView): boolean => {
  const days = Object.values(m ?? {});
  return days.length > 0 && days.every((d) => gridEnergyMetered(d, p));
};
/** Some days in `m` have metered grid energy. */
export const someDaysMetered = (m: Record<string, Daily> | null | undefined, p: PowerView): boolean =>
  Object.values(m ?? {}).some((d) => gridEnergyMetered(d, p));

const sc = (v: number | undefined, k: number) => (isNum(v) ? v * k : v);
// grid = load + AC charging: only the load part is VA.
const gridP = (gw: number | undefined, w: number | undefined, k: number) => (isNum(gw) && isNum(w) ? Math.max(0, gw - w * (1 - k)) : gw);
// Grid energy: the load part is at most min(load, grid) energy (the load also runs from the battery).
const gridE = (egr: number | undefined, eld: number | undefined, k: number) =>
  isNum(egr) && isNum(eld) ? Math.max(0, egr - Math.min(eld, egr) * (1 - k)) : egr;

/** `k`: the meter/VA ratio from useLiveView (2-second fusion); null or omitted: load and grid from the meter as read. */
export function viewLive(l: Live | null, p: PowerView, k: number | null = null): Live | null {
  if (!l) return l;
  const v = p.k === 1 ? l : { ...l, w: sc(l.w, p.k), gw: gridP(l.gw, l.w, p.k), eld: sc(l.eld, p.k), egr: gridE(l.egr, l.eld, p.k) };
  // The meter's figures and the measured load are real W and Wh: never scaled, also when k === 1.
  if (!p.meter) return v;
  const fused = powerFused(l, p, k);
  const load = fused ? l.w! * k! : loadPowerMetered(l, p) ? meterLoad(l) : loadPowerDc(l, p) ? dcLoad(l.pvw!, l.bw!) : null;
  const eld = measuredEld(l);
  if (load === null && eld === null && !isNum(l.mw) && !isNum(l.egm)) return v;
  return {
    ...v,
    gw: isNum(l.mw) ? (load !== null && fused ? gridFromLoad(l, load) : l.mw) : v.gw,
    egr: isNum(l.egm) ? l.egm : v.egr,
    eld: eld ?? v.eld,
    w: load ?? v.w,
    ld: load !== null ? measuredLd(l, load) : v.ld,
  };
}

/** Load % of rating for a measured load: the inverter's own % (its output VA / its rating, 3,200 VA here) scaled by
 *  measured load / VA, so it uses the inverter's rating without knowing it (QPIRI says 3,600 on this unit). Without
 *  a usable VA (dead band) there is no rating to use: undefined, shown as "–". Before, the inverter's % was shown next
 *  to the measured W: "219 W · 34 %" on a 3.2 kW inverter (2026-10-24). */
function measuredLd(l: Live, load: number): number | undefined {
  return isNum(l.ld) && isNum(l.w) && l.w >= K_MIN_VA ? Math.round((l.ld * load) / l.w) : undefined;
}

// The last k learned from a fresh reading, kept for the session and shared by every page (Live, History today, …).
let learned: { k: number; ts: number } | null = null;

/** Hook form of viewLive with the 2-second fusion: learns k from fresh meter readings and applies it in between.
 *  Without a k (first seconds, older firmware without `mage`, battery mode) it is viewLive(l, p) as before. */
export function useLiveView(raw: Live | null, p: PowerView): { live: Live | null; k: number | null; fused: boolean } {
  const k0 = meterK(raw, p);
  // Idempotent (the same payload gives the same k), so it may run during render; never moves back in time.
  if (k0 !== null && raw && (!learned || raw.ts >= learned.ts)) learned = { k: k0, ts: raw.ts };
  const k = learned && raw && raw.ts - learned.ts <= METER_K_TTL && raw.ts >= learned.ts ? learned.k : null;
  return useMemo(() => ({ live: viewLive(raw, p, k), k, fused: powerFused(raw, p, k) }), [raw, p.unit, p.k, p.meter, p.est, k]);
}

export function viewDaily<T extends Daily | null | undefined>(d: T, p: PowerView): T {
  if (!d) return d;
  const v = p.k === 1 ? d : { ...d, wx: sc(d.wx, p.k), eld: sc(d.eld, p.k), egr: gridE(d.egr, d.eld, p.k) };
  return gridEnergyMetered(d, p) ? { ...v, egr: d.egm, eld: measuredEld(d) ?? v.eld } : v;
}

export function viewDailyMap(m: Record<string, Daily> | null | undefined, p: PowerView): Record<string, Daily> | null | undefined {
  if (!m || (p.k === 1 && !someDaysMetered(m, p))) return m;
  const out: Record<string, Daily> = {};
  for (const [k, d] of Object.entries(m)) out[k] = viewDaily(d, p);
  return out;
}

/** Minute records: VA x pf; with the meter on (W view), battery-mode minutes use solar + battery instead (hist/ has
 *  no meter data, so on-grid minutes stay VA x pf). */
export function viewRecs(recs: Record<string, HistRec>, p: PowerView): Record<string, HistRec> {
  const dc = p.meter && p.unit === 'W';
  if (p.k === 1 && !dc) return recs;
  const out: Record<string, HistRec> = {};
  for (const [key, r] of Object.entries(recs)) {
    let o: HistRec = p.k === 1 ? r : { ...r, w: sc(r.w, p.k), wx: sc(r.wx, p.k), eld: sc(r.eld, p.k), egr: gridE(r.egr, r.eld, p.k) };
    if (dc && r.md === 'B' && isNum(r.pvw) && isNum(r.bv) && isNum(r.bi)) {
      const e = isNum(r.epv) && isNum(r.edis) && isNum(r.ech) ? Math.max(0, r.epv + r.edis - r.ech) : o.eld;
      o = { ...o, w: dcLoad(r.pvw, r.bv * r.bi), eld: e };
    }
    out[key] = o;
  }
  return out;
}
