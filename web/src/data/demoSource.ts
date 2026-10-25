// Demo mode (?demo=1): a deterministic simulation of a 3.2 kW / 24 V hybrid
// inverter with ~2.6 kWp of PV, a 24 V 100 Ah LiFePO4 battery, SBU priority
// and Bangladeshi load-shedding, so the whole UI can be previewed without
// Firebase. Every value is FAKE. Shapes follow d1 §4.2 exactly.
import { NoFreeSlotError, type DataSource, type Unsub } from './source';
import type { AlertRule, AlertStateRec, AlertsDoc, Cfg, Daily, EmailCfg, EventRec, HistRec, Live, MailerStatus, Meta, Status } from './types';
import { CFG_DEFAULTS } from './types';
import { addDays, dateKey, dateRange, hhmmFromMinute, keyToEpoch, localParts, nowSec, todayKey } from '../lib/time';
import { cleanRule, cycleStart, firstFreeSlot, parseRecipients, sumDaily, withEmailDefaults } from './alerts';
import { builtinEm, demoAlertsSeed, demoMailer, replayRules } from './demoAlerts';

const CAP_WH = 2560; // 24 V x 100 Ah LiFePO4 (8S)
const PV_PEAK = 2600;
const MAX_CHG_W = 2700;
// Grid meter (fw 0.9.0): "installed" 12 days ago, so month views mix metered and estimated days. The load is VA
// (REVO), the meter measures real W: about 0.91 of it. It is powered by the grid line, so it is silent in outages.
const METER_DAYS = 12;
const METER_PF = 0.91;
const METER_BASE_KWH = 1286.4;
const meterFrom = () => addDays(todayKey(), -METER_DAYS);
// fw 0.9.1: the meter refreshes V/A/W every ~15 s while live goes up every 2 s, so a reading is reused with a growing
// `mage` and the app scales the 2-second VA in between (lib/power.ts useLiveView). Alarm bitmap `mft` is 0; test
// paths: ?demo=1&mft=<bitmap> (e.g. 131072 = leakage early warning) and ?demo=1&mrc=<mA> (e.g. 185: the owner's meter).
const METER_POLL_S = 15;
const demoParam = (k: string): number | undefined => {
  try {
    const v = new URLSearchParams(location.search).get(k);
    return v !== null && v !== '' && Number.isFinite(Number(v)) ? Number(v) : undefined;
  } catch {
    return undefined;
  }
};

function hashStr(s: string): number {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619);
  return h >>> 0;
}
function rng(seed: number) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const r1 = (v: number) => Math.round(v * 10) / 10;
const r2 = (v: number) => Math.round(v * 100) / 100;

interface MinuteState {
  t: number; // minute of day
  pvw: number;
  pvv: number;
  w: number;
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

interface DaySim {
  date: string;
  minutes: MinuteState[]; // full day, 1440 entries
  events: Array<EventRec & { minute: number }>;
  battLowOpenAtEnd: boolean;
}

const simCache = new Map<string, DaySim>();

function simulateDay(date: string): DaySim {
  const hit = simCache.get(date);
  if (hit) return hit;
  const rand = rng(hashStr('savx-demo-' + date));
  const cloudiness = 0.55 + rand() * 0.45; // day-level clear-sky fraction
  const phases = [rand() * 6.28, rand() * 6.28, rand() * 6.28];
  const outages: Array<[number, number]> = [];
  const nOut = 1 + Math.floor(rand() * 3);
  for (let i = 0; i < nOut; i++) {
    const start = 9 * 60 + Math.floor(rand() * 13 * 60);
    outages.push([start, start + 40 + Math.floor(rand() * 50)]);
  }
  const ironAt = 19 * 60 + Math.floor(rand() * 150);
  let soc = 48 + rand() * 20;
  let lineMode = false;
  const minutes: MinuteState[] = [];
  const events: DaySim['events'] = [];
  let gridWasOk = true;
  let lostAt = 0;
  let battLowOpen = false;

  for (let t = 0; t < 1440; t++) {
    const h = t / 60;
    // --- PV ---
    const sun = h > 5.9 && h < 17.6 ? Math.sin((Math.PI * (h - 5.9)) / (17.6 - 5.9)) : 0;
    const cloud =
      cloudiness * (0.82 + 0.1 * Math.sin(t / 7 + phases[0]) + 0.08 * Math.sin(t / 2.3 + phases[1])) -
      (Math.sin(t / 23 + phases[2]) > 0.93 ? 0.35 : 0);
    const pvAvail = Math.max(0, PV_PEAK * Math.pow(sun, 1.25) * Math.max(0.15, Math.min(1, cloud)));
    // --- load ---
    let w = 170 + (Math.floor(t / 20) % 2 ? 110 : 0); // base + fridge cycling
    if (h >= 6.5 && h < 9) w += 300;
    if (h >= 11 && h < 16) w += 260;
    if (h >= 18 && h < 23) w += 520;
    if ((t >= 420 && t < 435) || (t >= 1170 && t < 1185)) w += 750; // water pump
    if (t >= ironAt && t < ironAt + 18) w += 1000; // iron
    w = Math.max(60, w + (rand() - 0.5) * 60);
    // --- grid ---
    const gridOk = !outages.some(([a, b]) => t >= a && t < b);
    const gv = gridOk ? 229 + 5 * Math.sin(t / 90) + (rand() - 0.5) * 3 : 0;
    // --- SBU policy (program 01 = SbU, 12 = back to utility at 40 %, 13 = back to battery at 80 %) ---
    if (lineMode && (soc >= 80 || !gridOk)) lineMode = false;
    if (!lineMode && gridOk && soc <= 40) lineMode = true;
    let pvw: number;
    let bw: number;
    let egrW = 0;
    let md: string;
    if (lineMode) {
      md = 'L';
      const room = soc >= 99.5 ? 0 : MAX_CHG_W;
      pvw = Math.min(pvAvail, room);
      bw = pvw; // PV charges the battery, grid carries the load
      egrW = w;
    } else {
      md = 'B';
      const surplus = pvAvail - w;
      if (surplus >= 0) {
        const room = soc >= 99.5 ? 0 : MAX_CHG_W;
        bw = Math.min(surplus, room);
        pvw = w + bw;
      } else {
        pvw = pvAvail;
        bw = surplus;
      }
    }
    if (soc <= 12 && bw < 0) {
      // low DC cut-off: output off
      w = 0;
      bw = Math.max(0, pvw);
    }
    soc = Math.max(5, Math.min(100, soc + (bw / CAP_WH) * (100 / 60) * (bw > 0 ? 0.96 : 1.04)));
    const bv0 = 24.6 + soc * 0.026;
    const bi = bw / bv0;
    const bv = bv0 + bi * 0.012;
    const pvv = pvAvail > 5 ? 225 + 45 * sun + (rand() - 0.5) * 4 : pvAvail > 0 ? 60 : 0;
    const tmp = 32 + w / 110 + pvw / 220 + (rand() - 0.5) * 1.5;
    minutes.push({ t, pvw, pvv, w, gv, bv, bi, bw, soc, md, tmp, ov: 230 + (rand() - 0.5) * 0.8, gridOk, egrW });

    // --- events (d1 §7.2 shapes) ---
    if (!gridOk && gridWasOk) {
      lostAt = t;
      events.push({ minute: t, ts: 0, type: 'GRID_LOSS', sev: 4, st: 'open', msg: `Grid lost, on battery (SOC ${Math.round(soc)}%)`, ch: 'ntfy' });
    }
    if (gridOk && !gridWasOk) {
      events.push({ minute: t, ts: 0, type: 'GRID_RESTORED', sev: 3, st: 'clear', msg: `Grid restored after ${t - lostAt} min`, ch: 'ntfy', dur: (t - lostAt) * 60 });
    }
    gridWasOk = gridOk;
    if (!battLowOpen && soc <= 30 && bw < 0) {
      battLowOpen = true;
      events.push({ minute: t, ts: 0, type: 'BATT_LOW', sev: 4, st: 'open', msg: `Battery low: SOC ${Math.round(soc)}%, ${bv.toFixed(2)} V`, ch: 'ntfy' });
    } else if (battLowOpen && soc >= 35) {
      battLowOpen = false;
      events.push({ minute: t, ts: 0, type: 'BATT_LOW', sev: 4, st: 'clear', msg: `Battery recovered: SOC ${Math.round(soc)}%`, ch: 'ntfy' });
    }
    if (t === ironAt + 2 && w > 1300) {
      events.push({ minute: t, ts: 0, type: 'GRID_ABNORMAL', sev: 2, st: 'info', msg: `Demo: load spike ${Math.round(w)} W`, ch: 'log' });
    }
  }
  const epv = minutes.reduce((s, m) => s + m.pvw / 60, 0);
  const eld = minutes.reduce((s, m) => s + m.w / 60, 0);
  const egr = minutes.reduce((s, m) => s + m.egrW / 60, 0);
  events.push({
    minute: 21 * 60,
    ts: 0,
    type: 'DAILY_SUMMARY',
    sev: 1,
    st: 'info',
    msg: `PV ${(epv / 1000).toFixed(1)} kWh, load ${(eld / 1000).toFixed(1)} kWh, grid ${(egr / 1000).toFixed(1)} kWh (est.)`,
    ch: 'ntfy',
  });
  for (const e of events) e.ts = keyToEpoch(date, hhmmFromMinute(e.minute)) + 17;
  const sim = { date, minutes, events, battLowOpenAtEnd: battLowOpen };
  if (simCache.size > 400) simCache.clear();
  simCache.set(date, sim);
  return sim;
}

/** Minutes of `date` that already exist (today: up to the last full minute). */
function availableMinutes(date: string): number {
  const today = todayKey();
  if (date > today) return 0;
  if (date < today) return 1440;
  const p = localParts(nowSec());
  return p.hh * 60 + p.mm; // the current minute is not complete yet
}

function histRec(m: MinuteState, rand: () => number): HistRec {
  const wx = m.w * (1 + rand() * 0.25);
  return {
    w: Math.round(m.w),
    wx: Math.round(wx),
    pvw: Math.round(m.pvw),
    pvx: Math.round(m.pvw * (1 + rand() * 0.08)),
    bv: r2(m.bv),
    bvn: r2(m.bv - 0.03 - rand() * 0.05),
    bvx: r2(m.bv + 0.03 + rand() * 0.05),
    bi: r1(m.bi),
    soc: Math.round(m.soc),
    gv: r1(m.gv),
    gvn: r1(Math.max(0, m.gv - rand() * 2)),
    ov: r1(m.ov),
    ld: Math.round((m.w / 3200) * 100),
    tmp: Math.round(m.tmp),
    pvv: r1(m.pvv),
    md: m.md,
    wn: m.gridOk ? 0 : 1,
    fc: 0,
    n: 12,
    epv: r1(m.pvw / 60),
    eld: r1(m.w / 60),
    ech: r1(Math.max(0, m.bw) / 60),
    edis: r1(Math.max(0, -m.bw) / 60),
    egr: r1(m.egrW / 60),
  };
}

const histCache = new Map<string, Record<string, HistRec>>();

function histFor(date: string): Record<string, HistRec> {
  const avail = availableMinutes(date);
  const key = `${date}:${avail}`;
  const hit = histCache.get(key);
  if (hit) return hit;
  const sim = simulateDay(date);
  const rand = rng(hashStr('hist-' + date));
  const out: Record<string, HistRec> = {};
  // A short data gap (logger reboot) on every 5th day, to show null gaps in charts.
  const gapStart = hashStr(date) % 5 === 0 ? 600 + (hashStr(date) % 300) : -1;
  for (let t = 0; t < avail; t++) {
    const rec = histRec(sim.minutes[t], rand);
    if (gapStart >= 0 && t >= gapStart && t < gapStart + 7) continue;
    out[hhmmFromMinute(t)] = rec;
  }
  if (histCache.size > 60) histCache.clear();
  histCache.set(key, out);
  return out;
}

function aggregate(recs: HistRec[]): HistRec {
  const n = recs.reduce((s, r) => s + r.n, 0);
  const avg = (k: keyof HistRec) => recs.reduce((s, r) => s + ((r[k] as number) ?? 0) * r.n, 0) / Math.max(1, n);
  const mx = (k: keyof HistRec) => Math.max(...recs.map((r) => (r[k] as number) ?? -Infinity));
  const mn = (k: keyof HistRec) => Math.min(...recs.map((r) => (r[k] as number) ?? Infinity));
  const tot = (k: keyof HistRec) => r1(recs.reduce((s, r) => s + ((r[k] as number) ?? 0), 0));
  const modes = new Map<string, number>();
  for (const r of recs) if (r.md) modes.set(r.md, (modes.get(r.md) ?? 0) + r.n);
  const md = [...modes.entries()].sort((a, b) => b[1] - a[1])[0]?.[0] ?? 'B';
  return {
    w: Math.round(avg('w')),
    wx: mx('wx'),
    pvw: Math.round(avg('pvw')),
    pvx: mx('pvx'),
    bv: r2(avg('bv')),
    bvn: mn('bvn'),
    bvx: mx('bvx'),
    bi: r1(avg('bi')),
    soc: recs[recs.length - 1].soc,
    gv: r1(avg('gv')),
    gvn: mn('gvn'),
    ov: r1(avg('ov')),
    ld: Math.round(avg('ld')),
    tmp: mx('tmp'),
    pvv: r1(avg('pvv')),
    md,
    wn: mx('wn'),
    fc: 0,
    n,
    epv: tot('epv'),
    eld: tot('eld'),
    ech: tot('ech'),
    edis: tot('edis'),
    egr: tot('egr'),
  };
}

function h15For(date: string): Record<string, HistRec> {
  const hist = histFor(date);
  const buckets = new Map<string, HistRec[]>();
  for (const [k, rec] of Object.entries(hist)) {
    const m = Number(k.slice(0, 2)) * 60 + Number(k.slice(2));
    const b = hhmmFromMinute(m - (m % 15));
    // only complete quarters are published, like the firmware
    const bEnd = m - (m % 15) + 15;
    if (date === todayKey() && bEnd > availableMinutes(date)) continue;
    const list = buckets.get(b) ?? [];
    list.push(rec);
    buckets.set(b, list);
  }
  const out: Record<string, HistRec> = {};
  for (const [k, list] of buckets) out[k] = aggregate(list);
  return out;
}

const dailyCache = new Map<string, Daily | null>();

function dailyFor(date: string): Daily | null {
  const past = date < todayKey();
  if (past && dailyCache.has(date)) return dailyCache.get(date) ?? null;
  const d = computeDaily(date);
  if (past) dailyCache.set(date, d);
  return d;
}

function computeDaily(date: string): Daily | null {
  const hist = Object.values(histFor(date));
  if (!hist.length) return null;
  const sim = simulateDay(date);
  const tot = (k: keyof HistRec) => Math.round(hist.reduce((s, r) => s + ((r[k] as number) ?? 0), 0));
  const epv = tot('epv');
  return {
    ...(date >= meterFrom() ? { egm: Math.round(tot('egr') * METER_PF) } : {}),
    epv,
    eld: tot('eld'),
    egr: tot('egr'),
    ech: tot('ech'),
    edis: tot('edis'),
    epvi: Math.round(epv * 0.985),
    pvx: Math.max(...hist.map((r) => r.pvx ?? 0)),
    wx: Math.max(...hist.map((r) => r.wx ?? 0)),
    bvn: Math.min(...hist.map((r) => r.bvn ?? 99)),
    bvx: Math.max(...hist.map((r) => r.bvx ?? 0)),
    socn: Math.min(...hist.map((r) => r.soc ?? 100)),
    socx: Math.max(...hist.map((r) => r.soc ?? 0)),
    gout: sim.minutes.slice(0, availableMinutes(date)).filter((m) => !m.gridOk).length,
    nf: 0,
    nw: sim.events.filter((e) => e.minute < availableMinutes(date) && e.sev >= 2 && e.st === 'open').length,
    n: hist.length,
  };
}

/** Earlier days of the billing cycle that contains `date` (for month rules). */
function cycleBefore(date: string, alerts: AlertsDoc) {
  const from = cycleStart(date, withEmailDefaults(alerts.email).monthStart);
  const days: Record<string, Daily> = {};
  for (const d of dateRange(from, addDays(date, -1))) {
    const v = d < from ? null : dailyFor(d);
    if (v) days[d] = v;
  }
  return sumDaily(days, date);
}

/** Seconds until the mailer's next 5-min run after `ts` (plus its own run time). */
const mailDelay = (ts: number) => 300 - (ts % 300) + 37 + (ts % 7);

/** events/<date> with deterministic keys `<ts>-<rid|TYPE>-<o|c|r|i>` and e-mail status (r8 §3.6). */
const DEMO_OUTAGES = [
  { minute: 192, dur: 23 * 60, msg: 'Offline 23 min from 03:12. Wi-Fi down 21 min, 1 drop, NO_AP_FOUND, -71 dBm. Heap min 61/31 KB' },
  { minute: 820, dur: 25, msg: 'Offline 25 s from 13:40. Reboot TASK_WDT (up 9h31m; uplink in mail 31s). Wi-Fi ok, -64 dBm' },
];

function eventsFor(date: string, acks: Set<string>, alerts: AlertsDoc): Record<string, EventRec> {
  const avail = availableMinutes(date);
  const out: Record<string, EventRec> = {};
  const sim = simulateDay(date);
  const evs = [...sim.events];
  // Each demo day starts with a fresh battery; close yesterday's BATT_LOW so alerts pair up.
  if (simulateDay(addDays(date, -1)).battLowOpenAtEnd) {
    evs.unshift({ minute: 1, ts: keyToEpoch(date, '0001') + 17, type: 'BATT_LOW', sev: 4, st: 'clear', msg: 'Battery recovered', ch: 'ntfy' });
  }
  const now = nowSec();
  const mailed = new Set<string>();
  const put = (rec: EventRec, src: string, kind: string) => {
    let key = `${rec.ts}-${src}-${kind}`;
    while (key in out) key = `${rec.ts + 1}-${src}-${kind}`;
    let r = rec;
    if (r.em === 'sent') r = now - r.ts < mailDelay(r.ts) ? { ...r, em: 'q' } : { ...r, emt: r.ts + mailDelay(r.ts) };
    out[key] = acks.has(`${date}/${key}`) ? { ...r, ack: true } : r;
  };
  for (const e of evs) {
    if (e.minute >= avail) continue;
    const { minute: _m, ...rec } = e;
    const em = builtinEm(rec, alerts, mailed);
    const kind = rec.st === 'open' ? 'o' : rec.st === 'clear' ? 'c' : 'i';
    put(em ? { ...rec, em, ch: rec.ch === 'log' ? 'email' : `${rec.ch},email` } : rec, rec.type, kind);
  }
  const replay = replayRules(date, sim.minutes, avail, alerts, cycleBefore(date, alerts));
  for (const e of replay.events) {
    const { minute: _m, kind, src, ...rec } = e;
    put(rec, src, kind);
  }
  // fw 1.0.2 outage log (Settings > Logger log)
  for (const o of DEMO_OUTAGES) {
    if (o.minute >= avail) continue;
    const ts = keyToEpoch(date, `${String(Math.floor(o.minute / 60)).padStart(2, '0')}${String(o.minute % 60).padStart(2, '0')}`);
    put({ ts, type: 'LOGGER_OFFLINE', sev: 1, st: 'info', msg: o.msg, ch: 'log', dur: o.dur }, 'LOGGER_OFFLINE', 'i');
  }
  return out;
}

/** The simulated inverter at epoch second `ts` (load VA and PV jitter around the minute's values). */
function inverterAt(ts: number) {
  const p = localParts(ts);
  const m = simulateDay(dateKey(ts)).minutes[Math.min(1439, p.hh * 60 + p.mm)];
  const j = rng(ts)();
  const jitter = (v: number, f: number) => v * (1 + (j - 0.5) * f);
  const w = Math.max(0, jitter(m.w, 0.08));
  const pvw = Math.max(0, jitter(m.pvw, 0.05));
  const bw = m.md === 'L' ? pvw : pvw - w;
  return { m, j, w, pvw, bw };
}

function liveNow(cfg: Cfg): Live {
  const now = nowSec();
  const date = dateKey(now);
  const { m, j, w, pvw, bw } = inverterAt(now);
  const bv = m.bv;
  const bi = bw / bv;
  const day = dailyFor(date);
  const wf = '0'.repeat(32).split('');
  if (!m.gridOk) wf[5] = '1';
  if (m.soc < 20) wf[12] = '1';
  // Grid meter: only while cfg.meter is on and the grid (which powers the meter) is present. Its V/A/W come from the
  // last 15-second reading (the inverter state at that second), the counters from now.
  let meter: Partial<Live> = {};
  const tm = now - (now % METER_POLL_S);
  const at = inverterAt(tm);
  if (cfg.meter !== 0 && m.gridOk && at.m.gridOk) {
    const mw = at.m.md === 'L' ? Math.round(Math.max(0, at.w + at.bw - at.pvw) * METER_PF) : Math.round(3 + at.j * 2);
    const mv = r1(at.m.gv + 1.2 + (at.j - 0.5) * 0.6);
    const mpf = mw > 30 ? r2(METER_PF + (at.j - 0.5) * 0.04) : 0.45;
    let kwh = METER_BASE_KWH;
    for (const d of dateRange(meterFrom(), date)) kwh += (dailyFor(d)?.egm ?? 0) / 1000;
    meter = {
      mw,
      mv,
      mi: r2(mw / (mv * mpf)),
      mpf,
      mhz: r2(50 + (at.j - 0.5) * 0.08),
      mrc: demoParam('mrc') ?? Math.round(2 + at.j * 3),
      mtc: Math.round(33 + mw / 400),
      mkwh: r2(kwh),
      mon: 1,
      mft: demoParam('mft') ?? 0,
      mage: now - tm,
    };
    if (typeof day?.egm === 'number') meter.egm = day.egm;
  }
  return {
    ts: now,
    lk: 1,
    gv: r1(m.gv),
    gf: m.gridOk ? r1(50 + (j - 0.5) * 0.1) : 0,
    ov: r1(m.ov),
    of: 50.0,
    va: Math.round(w / 0.93),
    w: Math.round(w),
    ld: Math.round((w / 3200) * 100),
    bus: 380 + Math.round(j * 6),
    bv: r2(bv),
    bci: Math.max(0, Math.round(bi)),
    bdi: Math.max(0, Math.round(-bi)),
    bi: r1(bi),
    bw: Math.round(bw),
    soc: Math.round(m.soc),
    tmp: Math.round(m.tmp),
    pvv: r1(m.pvv),
    pvi: m.pvv > 30 ? r1(pvw / m.pvv) : 0,
    pvw: Math.round(pvw),
    sccv: r2(bv + 0.02),
    md: m.md,
    st: `000${w > 0 ? 1 : 0}0${bw > 0 ? 1 : 0}${bw > 0 && pvw > 0 ? 1 : 0}0`,
    st2: '010',
    wf: wf.join(''),
    wc: 0,
    fc: 0,
    epv: day?.epv ?? 0,
    eld: day?.eld ?? 0,
    egr: day?.egr ?? 0,
    ech: day?.ech ?? 0,
    edis: day?.edis ?? 0,
    epvi: day?.epvi ?? 0,
    etot: 1874.2,
    q: 0,
    gw: m.md === 'L' && m.gridOk ? Math.round(w) : 0,
    ...meter,
  };
}

const BOOT = nowSec() - 3 * 86400 - 4200;

function statusNow(): Status {
  const now = nowSec();
  const j = rng(Math.floor(now / 60))();
  return {
    ts: now - (now % 60),
    fw: '1.0.0-demo',
    ip: '<logger-ip>',
    rssi: -55 - Math.round(j * 10),
    heap: 182000 + Math.round(j * 3000),
    psram: 8123456,
    up: now - BOOT,
    rr: 'POWERON',
    q: 0,
    proto: 'pi30-2400',
    dia: 'REVO',
    hs: 40 + Math.floor((now % 86400) / 600),
    rq: Math.floor((now % 86400) / 7),
    lok: now - 3,
    cok: now - (now % 60),
    chipC: 44.8,
    tgOk: 3,
    tgFail: 1,
    tgErr: 'TLS -0x7F00 out of memory',
    tgErrAt: now - 2 * 3600,
    mailOk: 2,
    mailFail: 0,
  };
}

const META: Meta = {
  qpi: 'PI30',
  qid: 'DEMO000000000',
  vfw: '00014.04',
  vfw2: '00003.03',
  qmn: 'DEMO-3200-24',
  ratva: 3200,
  ratw: 3200,
  ratbv: 24,
  btype: 8,
  outpri: 2,
  chgpri: 3,
  inrng: 0,
  dia: 'REVO',
  pv12: 'W',
  sup: 'QPI,QPIGS,QPIRI,QMOD,QALL,PQSE,QVFW,QVFW2,QID',
  link: 'rs232lite-2400-ii-tx1',
  fwver: '1.0.0-demo',
  boot: BOOT,
};

export function createDemoSource(): DataSource {
  // Rules were "saved" this morning and the logger has loaded them (status.arv === cfg.alertsRev).
  const seedRev = keyToEpoch(todayKey(), '0812') * 1000;
  let cfg: Cfg = { ...CFG_DEFAULTS, liveSec: 2, alertsRev: seedRev }; // the logger's live period with the meter (0.9.1)
  let arv = seedRev;
  let alerts: AlertsDoc = demoAlertsSeed();
  let mailerTest: MailerStatus['test'] | undefined;
  const acks = new Set<string>();
  const cfgListeners = new Set<(c: Partial<Cfg>) => void>();
  const statusListeners = new Set<(s: Status) => void>();
  const alertsListeners = new Set<(a: AlertsDoc) => void>();
  const refreshers = new Set<() => void>(); // alertState + mailer: recomputed when rules change
  const delay = <T>(v: T, ms = 120) => new Promise<T>((res) => setTimeout(() => res(v), ms));

  const statusWithRules = (): Status => {
    const st = statusNow();
    const n = Object.values(alerts.rules ?? {}).filter((r) => r.on).length;
    return { ...st, arv, acat: 1, nrul: n, mjq: todaysMailed().length, meter: meterStatus() };
  };
  /** status.meter as the logger writes it: the meter runs on the grid line, so it is unreachable while the grid is off. */
  const meterStatus = (): string => {
    if (cfg.meter === 0) return 'off (app setting)';
    const l = liveNow(cfg);
    return typeof l.mage === 'number' ? `ok 3.5 ${l.mage}s` : 'not reachable';
  };
  const todaysMailed = () => Object.values(eventsFor(todayKey(), acks, alerts)).filter((e) => e.em === 'sent');
  const alertStateNow = (): Record<string, AlertStateRec> => {
    const date = todayKey();
    return replayRules(date, simulateDay(date).minutes, availableMinutes(date), alerts, cycleBefore(date, alerts)).states;
  };
  const mailerNow = () => demoMailer(nowSec(), todayKey(), todaysMailed(), alerts, mailerTest);
  /** Simulates a save: cfg.alertsRev bumps now, the "logger" picks the rules up ~4 s later. */
  const changed = async () => {
    const rev = Date.now();
    cfg = { ...cfg, alertsRev: rev };
    for (const l of cfgListeners) l(cfg);
    for (const l of alertsListeners) l(alerts);
    for (const f of refreshers) f();
    setTimeout(() => {
      arv = rev;
      for (const l of statusListeners) l(statusWithRules());
      for (const f of refreshers) f();
    }, 4000);
    await delay(null, 250);
  };
  const setRules = (rules: Record<string, AlertRule>) => {
    alerts = { ...alerts, rules };
  };

  /** With cfg.meter = 0 the logger stops reading the meter, so today is no longer fully metered (no egm). */
  const meterCfg = (date: string, d: Daily | null): Daily | null => {
    if (!d || cfg.meter !== 0 || date !== todayKey() || !('egm' in d)) return d;
    const { egm: _egm, ...rest } = d;
    return rest;
  };

  function ticker<T>(make: () => T, everyMs: number, cb: (v: T) => void): Unsub {
    cb(make());
    const id = setInterval(() => cb(make()), everyMs);
    return () => clearInterval(id);
  }

  return {
    kind: 'demo',
    deviceId: 'inv1',
    onUser(cb) {
      cb({ uid: 'demo-owner', email: 'demo@example.com' });
      return () => {};
    },
    async signIn() {},
    async signOut() {
      location.href = location.pathname;
    },
    setActive() {},

    onLive: (cb) => ticker(() => liveNow(cfg), cfg.liveSec * 1000, cb),
    onStatus(cb) {
      statusListeners.add(cb);
      const un = ticker(statusWithRules, 60000, cb);
      return () => {
        statusListeners.delete(cb);
        un();
      };
    },
    onMeta(cb) {
      cb(META);
      return () => {};
    },
    onCfg(cb) {
      cfgListeners.add(cb);
      cb(cfg);
      return () => cfgListeners.delete(cb);
    },
    onDaily: (date, cb) => ticker(() => meterCfg(date, dailyFor(date)), 60000, cb),
    onHistAdded(date, afterKey, cb) {
      let last = afterKey;
      const emit = () => {
        const recs = histFor(date);
        for (const k of Object.keys(recs).sort()) {
          if (last === null || k > last) {
            cb(k, recs[k]);
            last = k;
          }
        }
      };
      emit();
      const id = setInterval(emit, 15000);
      return () => clearInterval(id);
    },

    getHist: (date) => delay(histFor(date)),
    getH15: (date) => delay(h15For(date)),
    getDailyRange(from, to) {
      const out: Record<string, Daily> = {};
      const today = todayKey();
      const start = from < addDays(today, -800) ? addDays(today, -800) : from;
      for (const d of dateRange(start, to < today ? to : today)) {
        const v = meterCfg(d, dailyFor(d));
        if (v) out[d] = v;
      }
      return delay(out, 200);
    },
    getEvents: (date) => delay(eventsFor(date, acks, alerts)),
    async setCfg(patch) {
      cfg = { ...cfg, ...patch };
      for (const l of cfgListeners) l(cfg);
      await delay(null, 200);
    },
    async ackEvent(date, id) {
      acks.add(`${date}/${id}`);
      await delay(null, 150);
    },

    // ---- alerts (in memory; reload to reset)
    onConnected(cb) {
      cb(true);
      return () => {};
    },
    onAlerts(cb) {
      alertsListeners.add(cb);
      cb(alerts);
      return () => alertsListeners.delete(cb);
    },
    onMonthStart(cb) {
      const f = (a: AlertsDoc | null) => cb(withEmailDefaults(a?.email).monthStart);
      alertsListeners.add(f);
      f(alerts);
      return () => alertsListeners.delete(f);
    },
    onAlertState(cb) {
      const f = () => cb(alertStateNow());
      refreshers.add(f);
      const un = ticker(alertStateNow, 15000, cb);
      return () => {
        refreshers.delete(f);
        un();
      };
    },
    onMailer(cb) {
      const f = () => cb(mailerNow());
      refreshers.add(f);
      const un = ticker(mailerNow, 30000, cb);
      return () => {
        refreshers.delete(f);
        un();
      };
    },
    async saveRule(rid, rule) {
      const slot = rid ?? firstFreeSlot(alerts.rules);
      if (!slot) throw new NoFreeSlotError();
      setRules({ ...(alerts.rules ?? {}), [slot]: { ...cleanRule(rule), upd: Date.now() } });
      await changed();
      return slot;
    },
    async setRuleEnabled(rid, on) {
      const cur = alerts.rules?.[rid];
      if (!cur) return;
      setRules({ ...(alerts.rules ?? {}), [rid]: { ...cur, on, upd: Date.now() } });
      await changed();
    },
    async deleteRule(rid) {
      const next = { ...(alerts.rules ?? {}) };
      delete next[rid];
      setRules(next);
      await changed();
    },
    async saveEmailCfg(email: EmailCfg, rcRemap) {
      const rules = { ...(alerts.rules ?? {}) };
      for (const [rid, rc] of Object.entries(rcRemap)) {
        if (!rules[rid]) continue;
        const { rc: _old, ...rest } = rules[rid];
        rules[rid] = rc === null ? { ...rest, upd: Date.now() } : { ...rest, rc, upd: Date.now() };
      }
      alerts = { email, rules };
      await changed();
    },
    async requestTestEmail(rid) {
      const ts = nowSec();
      // The fake mailer "runs" 3 s later and answers in mailer/test (r8 §6.6).
      setTimeout(() => {
        const to = parseRecipients(alerts.email?.to);
        const rc = rid ? alerts.rules?.[rid]?.rc : undefined;
        const n = rc ? to.filter((_, i) => rc & (1 << i)).length : to.length;
        mailerTest = n
          ? { ts, ok: true, msg: `sent to ${n} recipient${n > 1 ? 's' : ''}${alerts.email?.on ? '' : ' (alert e-mails are switched OFF)'}` }
          : { ts, ok: false, msg: 'no recipients: add one in Alerts > E-mail and to ALLOW_TO in the mailer' };
        for (const f of refreshers) f();
      }, 3000);
      await delay(null, 200);
      return ts;
    },
  };
}
