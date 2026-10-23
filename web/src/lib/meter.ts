// Grid meter (fw 0.9.x: a Smart Life/Tuya breaker with an energy meter on the grid line before the inverter).
// The app only READS it: switching, schedules and alarm settings stay in the Smart Life app.
// - live `mft` (fw 0.9.1): the meter's alarm bitmap (Tuya data point 9), bit i = METER_ALARMS[i];
// - status `meter`: the logger's short English state of the meter link ("ok 3.5 2s", "not reachable", …).
import { t, type Key } from './i18n';
import { localParts } from './time';
import type { EventRec } from '../data/types';

/** The meter's alarm names in bit order (bit 0 first), as the meter reports them. */
export const METER_ALARMS = [
  'short_circuit_alarm',
  'surge_alarm',
  'overload_alarm',
  'leakagecurr_alarm',
  'temp_dif_fault',
  'fire_alarm',
  'high_power_alarm',
  'self_test_alarm',
  'ov_cr',
  'unbalance_alarm',
  'ov_vol',
  'undervoltage_alarm',
  'miss_phase_alarm',
  'outage_alarm',
  'magnetism_alarm',
  'credit_alarm',
  'no_balance_alarm',
  'leakage_early_warning',
  'overcur_early_warning',
  'overvol_early_warning',
  'overpow_early_warning',
  'undvol_early_warning',
  'higtemp_early_warning',
] as const;
export type MeterAlarmId = (typeof METER_ALARMS)[number];

export interface MeterAlarm {
  bit: number;
  id: MeterAlarmId | null; // null: a bit beyond the known list
  label: string;
  early: boolean; // an early warning, not an alarm
}

/** The set bits of `mft`, labelled in the UI language. */
export function meterAlarms(mft: number): MeterAlarm[] {
  const out: MeterAlarm[] = [];
  for (let bit = 0; bit < 32; bit++) {
    if (((mft >>> bit) & 1) === 0) continue;
    const id = METER_ALARMS[bit] ?? null;
    out.push({ bit, id, label: id ? t(`mft.${id}`) : t('meter.alarmBit', { n: bit }), early: !!id && id.endsWith('_early_warning') });
  }
  return out;
}

/** Residual (leakage) current above this is marked high: a 30 mA safety breaker (RCD) trips well below. */
export const LEAK_HIGH_MA = 30;

export type MeterTone = 'ok' | 'warn' | 'bad' | 'info' | 'neutral';

// status.meter strings written by the firmware (firmware/src/meter/Meter.cpp, net/Uplink.cpp), longest prefix first.
const STATES: Array<[string, Key, MeterTone]> = [
  ['ok', 'meter.stOk', 'ok'],
  ['off (app setting)', 'meter.stOffApp', 'neutral'],
  ['off (safe mode)', 'meter.stOffSafe', 'warn'],
  ['not set up', 'meter.stNotSetUp', 'warn'],
  ['no Wi-Fi', 'meter.stNoWifi', 'bad'],
  ['searching', 'meter.stSearching', 'info'],
  ['meter not found', 'meter.stNotFound', 'bad'],
  ['not reachable', 'meter.stUnreachable', 'bad'],
  ['no reply', 'meter.stNoReply', 'bad'],
  ['answers, but not to our key', 'meter.stBadKey', 'bad'],
  ['starting', 'meter.stStarting', 'info'],
  ['stopped after crashes', 'meter.stCrashed', 'bad'],
  ['crypto self-test failed', 'meter.stSelfTest', 'bad'],
];

/** status.meter in the UI language (unknown states as sent); null when the logger reports none. */
export function meterState(s: string | undefined | null): { text: string; tone: MeterTone; raw: string } | null {
  if (!s) return null;
  const hit = STATES.find(([p]) => (p === 'ok' ? s === 'ok' || s.startsWith('ok ') : s.startsWith(p)));
  return hit ? { text: t(hit[1]), tone: hit[2], raw: s } : { text: s, tone: 'neutral', raw: s };
}

// ---- Schedule card: the planned grid-off (Settings) and the grid cuts the logger recorded ----------------------
// The Smart Life schedule itself lives in Tuya's cloud and the meter does not report it, so the card shows the
// owner's planned window (cfg.gridOffFrom/To, which should match it) and what actually happened (GRID_LOSS /
// GRID_RESTORED events: the inverter saw the grid go and come back).

/** One grid cut: from GRID_LOSS to GRID_RESTORED (null: before / after the events that were read). */
export interface GridCut {
  from: number | null;
  to: number | null;
  planned: boolean;
}

/** The logger's grace around the planned start (firmware cfg::GRID_OFF_GRACE_MIN). */
export const GRID_OFF_GRACE_MIN = 10;

/** `ts` lies within the grace of the planned window's start (local minutes, -1 = none). */
export function nearPlannedStart(ts: number, goFrom: number): boolean {
  if (goFrom < 0) return false;
  const p = localParts(ts);
  const d = Math.abs(((p.hh * 60 + p.mm - goFrom + 720) % 1440 + 1440) % 1440 - 720);
  return d <= GRID_OFF_GRACE_MIN;
}

/** Pairs GRID_LOSS and GRID_RESTORED events (any order of days) into cuts, oldest first. Planned: labelled by the
 *  logger (" (planned grid-off)", fw 0.8.1+) or starting at the planned time (older events). */
export function gridCuts(evs: EventRec[], goFrom: number): GridCut[] {
  const list = evs
    .filter((e) => e.ts > 0 && (e.type === 'GRID_LOSS' || e.type === 'GRID_RESTORED'))
    .sort((a, b) => a.ts - b.ts);
  const out: GridCut[] = [];
  let open: GridCut | null = null;
  for (const e of list) {
    const labelled = (e.msg ?? '').includes('(planned grid-off)');
    if (e.type === 'GRID_LOSS' && e.st !== 'clear') {
      if (open) out.push(open); // a second loss without a restore in between: keep both
      open = { from: e.ts, to: null, planned: labelled || nearPlannedStart(e.ts, goFrom) };
    } else if (open) {
      open.to = e.ts;
      open.planned = open.planned || labelled;
      out.push(open);
      open = null;
    } else {
      out.push({ from: null, to: e.ts, planned: labelled });
    }
  }
  if (open) out.push(open);
  return out;
}

/** The next change of the planned window: grid off at goFrom or back on at goTo (local minutes); null if none. */
export function nextPlanned(goFrom: number, goTo: number, now: number): { off: boolean; min: number; inSec: number } | null {
  if (goFrom < 0 || goTo < 0 || goFrom === goTo) return null;
  const p = localParts(now);
  const m = p.hh * 60 + p.mm;
  const inside = goFrom < goTo ? m >= goFrom && m < goTo : m >= goFrom || m < goTo;
  const target = inside ? goTo : goFrom;
  const inSec = ((((target - m) % 1440) + 1440) % 1440) * 60 - p.ss;
  return { off: !inside, min: target, inSec: inSec > 0 ? inSec : inSec + 86400 };
}
