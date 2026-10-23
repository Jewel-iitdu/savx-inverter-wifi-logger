// Power display for inverters that send their AC output power as VA only (outputIsVA: the Sorotec REVO, 2026-10-22).
// The owner picks the unit in Settings (stored per browser, like the theme):
//  - 'W'  (default): output power estimated as VA × power factor. Default 0.90: the owner's LCD comparison at
//          about 1.2 kVA (2026-10-22 10:50; 0.83 at 345 VA and about 0.71 at 400 VA earlier). It changes with the load mix.
//  - 'VA': load and grid as reported. Solar and battery are DC and always shown in W (the LCD shows PV in kW too).
// Only the AC output side is scaled: load power, the load part of grid power (fw 0.7.5+: grid = load + battery - PV,
// and only the load term is VA), and the load/grid energies. Solar and battery are never scaled.
import { useApp } from '../context';
import { outputIsVA } from './decode';
import { isNum } from './format';
import { useStored } from './hooks';
import type { Daily, HistRec, Live } from '../data/types';

export type PowerUnit = 'W' | 'VA';
export const PF_DEFAULT = 0.9;
export const PF_MIN = 0.5;
export const PF_MAX = 1;

export interface PowerView {
  unit: PowerUnit; // unit of the AC output figures (load, grid); solar and battery are always W
  k: number; // factor applied to the AC output figures (1 = unchanged)
  est: boolean; // AC output figures are VA-based (load/grid kWh are estimates)
}

export const IDENTITY: PowerView = { unit: 'W', k: 1, est: false };

export function parsePf(s: string | null | undefined): number {
  const v = Number(s);
  return Number.isFinite(v) && v >= PF_MIN && v <= PF_MAX ? v : PF_DEFAULT;
}

export function powerView(dia: string | undefined, unit: PowerUnit, pf: number): PowerView {
  if (!outputIsVA(dia)) return IDENTITY; // the inverter reports real W
  return unit === 'VA' ? { unit: 'VA', k: 1, est: true } : { unit: 'W', k: pf, est: true };
}

/** Settings: [unit, setUnit, pf text, setPf text]. */
export function usePowerPrefs() {
  const [unit, setUnit] = useStored<PowerUnit>('pwrUnit', 'W');
  const [pf, setPf] = useStored<string>('pwrPf', String(PF_DEFAULT));
  return { unit: unit === 'VA' ? 'VA' : ('W' as PowerUnit), setUnit, pf, setPf };
}

export function usePowerView(): PowerView {
  const { meta } = useApp();
  const { unit, pf } = usePowerPrefs();
  return powerView(meta?.dia, unit, parsePf(pf));
}

/** The view for the electricity bill: always W, since the meter bills kWh (VA × power factor on VA-only inverters). */
export function useBillView(): PowerView {
  const { meta } = useApp();
  const { pf } = usePowerPrefs();
  return powerView(meta?.dia, 'W', parsePf(pf));
}

const sc = (v: number | undefined, k: number) => (isNum(v) ? v * k : v);
// grid = load + AC charging: only the load part is VA.
const gridP = (gw: number | undefined, w: number | undefined, k: number) => (isNum(gw) && isNum(w) ? Math.max(0, gw - w * (1 - k)) : gw);
// Grid energy: the load part is at most min(load, grid) energy (the load also runs from the battery).
const gridE = (egr: number | undefined, eld: number | undefined, k: number) =>
  isNum(egr) && isNum(eld) ? Math.max(0, egr - Math.min(eld, egr) * (1 - k)) : egr;

export function viewLive(l: Live | null, p: PowerView): Live | null {
  if (!l || p.k === 1) return l;
  return { ...l, w: sc(l.w, p.k), gw: gridP(l.gw, l.w, p.k), eld: sc(l.eld, p.k), egr: gridE(l.egr, l.eld, p.k) };
}

export function viewDaily<T extends Daily | null | undefined>(d: T, p: PowerView): T {
  if (!d || p.k === 1) return d;
  return { ...d, wx: sc(d.wx, p.k), eld: sc(d.eld, p.k), egr: gridE(d.egr, d.eld, p.k) };
}

export function viewDailyMap(m: Record<string, Daily> | null | undefined, p: PowerView): Record<string, Daily> | null | undefined {
  if (!m || p.k === 1) return m;
  const out: Record<string, Daily> = {};
  for (const [k, d] of Object.entries(m)) out[k] = viewDaily(d, p);
  return out;
}

export function viewRecs(recs: Record<string, HistRec>, p: PowerView): Record<string, HistRec> {
  if (p.k === 1) return recs;
  const out: Record<string, HistRec> = {};
  for (const [key, r] of Object.entries(recs)) {
    out[key] = { ...r, w: sc(r.w, p.k), wx: sc(r.wx, p.k), eld: sc(r.eld, p.k), egr: gridE(r.egr, r.eld, p.k) };
  }
  return out;
}
