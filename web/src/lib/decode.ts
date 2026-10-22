// Decoding of mode letters, status bits, QPIWS strings and fault/warning codes
// using the generated tables in codes.ts (source of truth: shared/codes.json).
// LCD codes, QPIWS bits and REVO QALL codes are DIFFERENT tables (d1 K13), so
// every decoded code carries the name of the table it came from.
import {
  EVENT_TYPES,
  LCD_FAULT,
  QMOD,
  QPIWS_VARIANTS,
  REVO_FAULT,
  REVO_WARNING,
  SEVERITY_NAMES,
  ST_BITS,
  VOLTRONIC_FAULT_EXTRA,
  type QpiwsVariant,
} from '../codes';
import { eventBn, qmodBn, severityBn, t } from './i18n';

export type FlagClass = 'fault' | 'warning';

export interface ActiveFlag {
  index: number;
  cls: FlagClass;
  text: string;
}

export type CodeTable = 'lcd' | 'voltronic' | 'revoFault' | 'revoWarn' | 'unknown';

export interface DecodedCode {
  code: string;
  text: string;
  table: CodeTable;
  cls: FlagClass;
}

/** 'LCD fault (Savx manual §5)' etc. (the code text itself stays as in codes.ts). */
export function tableName(table: CodeTable): string {
  return t(`table.${table}`);
}

export interface QpiwsResult {
  variant: QpiwsVariant | null;
  flags: ActiveFlag[];
  fault: DecodedCode | null; // embedded fault code (36-char variants)
}

// The Sorotec REVO firmware (VERFW 00072.40) puts the APPARENT power in QALL field 5 and in BOTH QPIGS power fields:
// the owner's LCD showed 330-360 VA and 280-290 W while the logger read 342 in all three (2026-10-22). The real watts
// are not sent over the COM port, so load and grid power, and the load/grid energies, are VA-based on this dialect.
export function outputIsVA(dia: string | undefined): boolean {
  return dia === 'REVO';
}

export function modeName(md: string | undefined): string {
  if (!md) return t('mode.unknown');
  return qmodBn(md)?.short ?? QMOD[md]?.short ?? t('mode.other', { md });
}

export function modeLong(md: string | undefined): string {
  if (!md) return t('mode.unknown');
  return qmodBn(md)?.name ?? QMOD[md]?.name ?? t('mode.unknownLong', { md });
}

export function modeTone(md: string | undefined): 'ok' | 'warn' | 'bad' | 'muted' {
  switch (md) {
    case 'L':
    case 'Y':
      return 'ok';
    case 'B':
    case 'C':
    case 'E':
      return 'warn';
    case 'F':
      return 'bad';
    default:
      return 'muted';
  }
}

export function pickVariant(wf: string, dia?: string): QpiwsVariant | null {
  const len = wf.length;
  if (len === 36) {
    const id = dia === 'MAX24' ? 'pmax' : 'pgk';
    return QPIWS_VARIANTS.find((v) => v.id === id) ?? null;
  }
  return QPIWS_VARIANTS.find((v) => v.length === len) ?? null;
}

export function decodeQpiws(wf: string | undefined, dia?: string): QpiwsResult {
  if (!wf || !/^[0-9]+$/.test(wf)) return { variant: null, flags: [], fault: null };
  const variant = pickVariant(wf, dia);
  const flags: ActiveFlag[] = [];
  let fault: DecodedCode | null = null;
  if (!variant) {
    // Unknown length: still report raw bit positions so nothing is hidden.
    for (let i = 0; i < wf.length; i++) {
      if (wf[i] === '1') flags.push({ index: i, cls: 'warning', text: t('dec.unknownBit', { i: String(i), n: wf.length }) });
    }
    return { variant, flags, fault };
  }
  const a1 = wf[1] === '1';
  for (const [i, kind, text] of variant.bits) {
    if (kind === 'FC' || kind === 'R') continue;
    if (wf[i] !== '1') continue;
    const cls: FlagClass = kind === 'F' || (kind === 'FA1' && a1) ? 'fault' : 'warning';
    flags.push({ index: i, cls, text });
  }
  if (variant.faultCodeAt) {
    const [p, q] = variant.faultCodeAt;
    const code = (wf[p] ?? '0') + (wf[q] ?? '0');
    if (code !== '00') fault = decodeVoltronicFault(code);
  }
  flags.sort((x, y) => (x.cls === y.cls ? x.index - y.index : x.cls === 'fault' ? -1 : 1));
  return { variant, flags, fault };
}

function decodeVoltronicFault(code: string): DecodedCode {
  if (LCD_FAULT[code]) return { code, text: LCD_FAULT[code], table: 'lcd', cls: 'fault' };
  if (VOLTRONIC_FAULT_EXTRA[code]) return { code, text: VOLTRONIC_FAULT_EXTRA[code], table: 'voltronic', cls: 'fault' };
  return { code, text: t('dec.faultUnknown', { code }), table: 'unknown', cls: 'fault' };
}

/** `fc` from live/hist: REVO QALL table when the dialect is REVO, else the Voltronic/LCD table. */
export function decodeFault(fc: number | undefined, dia?: string): DecodedCode | null {
  if (typeof fc !== 'number' || !Number.isFinite(fc) || fc === 0) return null;
  const code = String(Math.trunc(fc)).padStart(2, '0');
  if (dia === 'REVO') {
    const e = REVO_FAULT[code];
    return { code, text: e ? e.text : t('dec.revoFaultUnknown', { code }), table: e ? 'revoFault' : 'unknown', cls: 'fault' };
  }
  return decodeVoltronicFault(code);
}

/** `wc` (REVO QALL warning code). */
export function decodeRevoWarning(wc: number | undefined): DecodedCode | null {
  if (typeof wc !== 'number' || !Number.isFinite(wc) || wc === 0) return null;
  const code = String(Math.trunc(wc)).padStart(2, '0');
  const e = REVO_WARNING[code];
  return { code, text: e ? e.text : t('dec.revoWarnUnknown', { code }), table: e ? 'revoWarn' : 'unknown', cls: 'warning' };
}

export interface StatusFlags {
  loadOn: boolean | null;
  charging: boolean | null;
  sccCharging: boolean | null;
  acCharging: boolean | null;
  configChanged: boolean | null;
}

export function decodeStatus(st: string | undefined): StatusFlags {
  const bit = (pos: number): boolean | null => (st && st.length === 8 ? st[pos] === '1' : null);
  return { loadOn: bit(3), charging: bit(5), sccCharging: bit(6), acCharging: bit(7), configChanged: bit(1) };
}

export function statusBitList(st: string | undefined): string[] {
  if (!st || st.length !== 8) return [];
  return ST_BITS.filter((b) => st[b.pos] === '1').map((b) => b.name);
}

export function severityName(sev: number): string {
  return severityBn(sev) ?? SEVERITY_NAMES[sev] ?? t('sev.other', { n: sev });
}

export function eventLabel(type: string): string {
  return eventBn(type) ?? EVENT_TYPES[type]?.label ?? type;
}

/** Collected decoded alarms for the Live page. */
// Grid voltage at or above this is "grid normal" (manual: low-loss 170 V narrow / 90 V wide).
export const GRID_OK_V = 180;

export function activeAlarms(live: { wf?: string; fc?: number; wc?: number; md?: string; gv?: number; soc?: number; bv?: number } | null, dia?: string): Array<{ cls: FlagClass; text: string; source: string }> {
  if (!live) return [];
  const out: Array<{ cls: FlagClass; text: string; source: string }> = [];
  // The Sorotec REVO firmware keeps QPIWS a5 "line fail" and QALL warning 02 "mains low voltage" set while the grid
  // is at 236 V in line mode (first contact 2026-10-22). A measured normal grid voltage wins, as in the firmware.
  const gridOk = typeof live.gv === 'number' && Number.isFinite(live.gv) && live.gv >= GRID_OK_V;
  // Same firmware, battery mode: QALL warning 01 "low battery" at SOC 99 % / 26.4 V (power-cut test 2026-10-22).
  // Hide it while the measured SOC and voltage are healthy (24 V model: low-DC warning 22.0 V).
  const battOk = typeof live.soc === 'number' && live.soc >= 30 && typeof live.bv === 'number' && live.bv >= 23;
  const q = decodeQpiws(live.wf, dia);
  for (const f of q.flags) {
    if (gridOk && f.index === 5) continue;
    out.push({ cls: f.cls, text: f.text, source: `QPIWS a${f.index}` });
  }
  if (q.fault) out.push({ cls: 'fault', text: `${q.fault.code}: ${q.fault.text}`, source: tableName(q.fault.table) });
  const fc = decodeFault(live.fc, dia);
  if (fc && !(q.fault && q.fault.code === fc.code)) out.push({ cls: 'fault', text: `${fc.code}: ${fc.text}`, source: tableName(fc.table) });
  const wc = decodeRevoWarning(live.wc);
  if (wc && !(gridOk && wc.code === '02') && !(battOk && wc.code === '01')) out.push({ cls: 'warning', text: `${wc.code}: ${wc.text}`, source: tableName(wc.table) });
  if (live.md === 'F' && !out.some((o) => o.cls === 'fault')) out.push({ cls: 'fault', text: t('dec.faultMode'), source: 'QMOD F' });
  return out;
}
