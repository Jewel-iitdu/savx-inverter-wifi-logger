import { localizeDigits } from './i18n';

export const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);

/** Battery power within ±this is "standby", not charging/discharging. The Savx lithium BMS reports a steady
 *  0.8–0.9 A (≈ 21–24 W) discharge whatever the load or sun (00:25 on grid, 07:11 and 07:34 on solar, 2026-10-22):
 *  the standby draw at 100 %. 1.2 A × 26.5 V ≈ 32 W. */
export const BATT_STANDBY_W = 32;

/** Display formatters below return Bangla digits in Bangla mode (never use them for inputs, CSV or Firebase).
 *  Number and unit are joined by a no-break space (NB), so narrow tiles wrap between values, never inside one. */
const NB = '\u00a0';
export function fmtNum(v: number | undefined | null, digits = 0, unit = ''): string {
  if (!isNum(v)) return '–';
  const s = v.toLocaleString('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits });
  return localizeDigits(unit ? `${s}${NB}${unit}` : s);
}

/** Watts: '512 W', '1.25 kW' (kd = decimals for kW; the power-flow circles use 1). */
export function fmtW(v: number | undefined | null, kd = 2): string {
  if (!isNum(v)) return '–';
  const a = Math.abs(v);
  if (a >= 1000) return localizeDigits(`${(v / 1000).toFixed(a >= 10000 ? 1 : kd)}${NB}kW`);
  return localizeDigits(`${Math.round(v)}${NB}W`);
}

/** Volt-amperes: '342 VA', '1.25 kVA'. */
export function fmtVA(v: number | undefined | null, kd = 2): string {
  if (!isNum(v)) return '–';
  const a = Math.abs(v);
  if (a >= 1000) return localizeDigits(`${(v / 1000).toFixed(a >= 10000 ? 1 : kd)}${NB}kVA`);
  return localizeDigits(`${Math.round(v)}${NB}VA`);
}

/** AC output power (load, grid) in the dashboard's unit: VA when `va` (see outputIsVA), else W. Not for solar/battery (DC: always W). */
export function fmtPower(v: number | undefined | null, va: boolean, kd = 2): string {
  return va ? fmtVA(v, kd) : fmtW(v, kd);
}

/** Watt-hours shown as kWh: '1.92 kWh'. */
export function fmtWh(v: number | undefined | null, digits = 2): string {
  if (!isNum(v)) return '–';
  return localizeDigits(`${(v / 1000).toFixed(digits)}${NB}kWh`);
}

export function fmtPct(v: number | undefined | null, digits = 0): string {
  return isNum(v) ? localizeDigits(`${v.toFixed(digits)}${NB}%`) : '–';
}

export function fmtBytes(v: number | undefined | null): string {
  if (!isNum(v)) return '–';
  if (v >= 1048576) return localizeDigits(`${(v / 1048576).toFixed(1)}${NB}MB`);
  if (v >= 1024) return localizeDigits(`${(v / 1024).toFixed(0)}${NB}KB`);
  return localizeDigits(`${v}${NB}B`);
}

export function sum(values: Array<number | undefined>): number {
  let s = 0;
  for (const v of values) if (isNum(v)) s += v;
  return s;
}

export function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}
