import { useApp, useReportError } from '../context';
import { useNow, useSubscription } from '../lib/hooks';
import { fmtAgo, fmtClock, fmtDateKey, fmtTime, localParts, nowSec, todayKey } from '../lib/time';
import { localizeDigits as dg, t } from '../lib/i18n';
import { BATT_STANDBY_W, fmtNum, fmtPct, fmtPower, fmtW, fmtWh, isNum } from '../lib/format';
import { activeAlarms, modeLong, modeName, modeTone } from '../lib/decode';
import { gridEnergyMetered, gridPowerMetered, loadPowerMeasured, useLiveView, usePowerView, viewDaily } from '../lib/power';
import { solarFraction } from '../lib/series';
import type { Cfg, Daily, Live as LiveT, Status } from '../data/types';
import { PowerFlow } from '../components/PowerFlow';
import { Gauge } from '../components/Gauge';
import { Badge, Banner, Card, KV, Tile } from '../components/ui';

export const STALE_SEC = 60;

// Charging/discharging from measured power. The QPIGS status bits are not used here: on the Sorotec REVO unit they
// report "solar charging" at night with 0 W PV (first contact 2026-10-22).
function flowNote(l: LiveT | null): string {
  if (!l) return '';
  const thr = 15;
  const pvOn = isNum(l.pvw) && l.pvw > thr;
  if (isNum(l.bw) && l.bw > BATT_STANDBY_W) return pvOn ? t('live.flowSolarCharging') : l.md === 'L' ? t('live.flowGridCharging') : t('live.flowCharging');
  if (isNum(l.bw) && l.bw < -BATT_STANDBY_W) return t('live.flowDischarging');
  return pvOn ? t('live.flowSolar') : '';
}

export function LivePage() {
  const { source, visible, meta } = useApp();
  const now = useNow(1000);
  const today = todayKey();
  const live = useSubscription<LiveT | null>((cb, err) => source.onLive(cb, err), [source], visible);
  const status = useSubscription<Status | null>((cb, err) => source.onStatus(cb, err), [source], visible);
  const cfgSub = useSubscription<Partial<Cfg> | null>((cb, err) => source.onCfg(cb, err), [source], visible);
  const daily = useSubscription<Daily | null>((cb, err) => source.onDaily(today, cb, err), [source, today], visible);
  useReportError('live', live.error);
  useReportError('status', status.error);

  const pv = usePowerView();
  const raw = live.value ?? null;
  // fw 0.9.1: in line mode load and grid move with the inverter's 2-second VA between the meter's 15-second readings
  const { live: l, k: meterK } = useLiveView(raw, pv);
  const age = l?.ts ? now - l.ts : Infinity;
  const stale = live.value !== undefined && age > STALE_SEC;
  const dia = meta?.dia ?? status.value?.dia;
  // 0.7.9 planned grid-off (Settings): same window and 10-min grace as the logger. While it runs with the grid off,
  // the inverter's own "line fail" warnings are expected, so they are replaced by a short note.
  const goFrom = cfgSub.value?.gridOffFrom ?? -1;
  const goTo = cfgSub.value?.gridOffTo ?? -1;
  const lp = localParts(nowSec());
  const mNow = lp.hh * 60 + lp.mm;
  const ga = (goFrom - 10 + 1440) % 1440;
  const gb = (goTo + 10) % 1440;
  const inGo = goFrom >= 0 && goTo >= 0 && (ga <= gb ? mNow >= ga && mNow < gb : mNow >= ga || mNow < gb);
  const gridOff = !!l && isNum(l.gv) && l.gv < 90;
  const plannedNow = inGo && gridOff;
  const alarms = activeAlarms(l, dia).filter((a) => !(plannedNow && (a.source === 'QPIWS a5' || a.text.startsWith('02:'))));
  const va = pv.unit === 'VA';
  const d = viewDaily(daily.value ?? null, pv);
  const epv = l?.epv ?? d?.epv;
  const eld = l?.eld ?? d?.eld;
  const egr = l?.egr ?? d?.egr;
  // fw 0.9.0 grid meter (cfg.meter on): viewLive/viewDaily already put its real W and Wh in gw/egr.
  const gm = gridPowerMetered(raw, pv);
  const lm = loadPowerMeasured(raw, pv); // load measured: meter on grid, solar + battery on battery (real W)
  const egrM = isNum(l?.egr) ? gridEnergyMetered(raw, pv) : gridEnergyMetered(daily.value, pv);
  const frac = solarFraction(eld, egr);
  const tmpRaw = isNum(l?.tmp) && l!.tmp! > 150;
  const loading = live.value === undefined;

  return (
    <div class="page">
      {stale && (
        <Banner tone="bad">
          <strong>{t('live.offlineTitle')}</strong> {t('live.offlineBody', { ago: fmtAgo(age), at: l?.ts ? ` (${fmtTime(l.ts)})` : '' })}
        </Banner>
      )}
      {live.value === null && !loading && <Banner tone="warn">{t('live.noData')}</Banner>}
      {l && !stale && l.lk === 0 && (
        <Banner tone="warn">
          <strong>{t('live.linkDownTitle')}</strong> {t('live.linkDownBody')}
        </Banner>
      )}
      {plannedNow && (
        <p class="muted small center">{t('live.plannedOff', { to: fmtClock(Math.floor(goTo / 60), goTo % 60) })}</p>
      )}
      {alarms.length > 0 && (
        <Card title={t('live.activeWarnings')} class={alarms.some((a) => a.cls === 'fault') ? 'card-bad' : 'card-warn'}>
          <ul class="alarms">
            {alarms.map((a) => (
              <li key={a.source + a.text}>
                <Badge tone={a.cls === 'fault' ? 'bad' : 'warn'}>{a.cls === 'fault' ? t('live.clsFault') : t('live.clsWarning')}</Badge> {a.text} <span class="muted small">({a.source})</span>
              </li>
            ))}
          </ul>
        </Card>
      )}

      <Card
        title={
          <span>
            {t('live.now')} <Badge tone={modeTone(l?.md)}>{modeName(l?.md)}</Badge>
          </span>
        }
        right={<span class={`muted small ${stale ? 'bad' : ''}`}>{loading ? t('live.connecting') : l?.ts ? t('live.updated', { ago: fmtAgo(age) }) : ''}</span>}
      >
        <PowerFlow live={l} stale={stale} va={va} gridVa={va && !gm} loadVa={va && !lm} />
        <p class="muted small center">{modeLong(l?.md)}{flowNote(l)}</p>
      </Card>

      <div class="grid2">
        <Card title={t('w.battery')}>
          <div class="batt">
            <Gauge value={l?.soc} label="SOC" lowAt={30} />
            <KV
              rows={[
                [t('live.voltage'), fmtNum(l?.bv, 2, 'V')],
                [t('live.current'), isNum(l?.bi) ? dg(`${l!.bi! > 0 ? '+' : ''}${l!.bi!.toFixed(1)} A`) : '–'],
                [t('w.power'), isNum(l?.bw) ? `${l!.bw! > 0 ? '+' : ''}${fmtW(l!.bw)}` : '–'],
                [t('live.chgDis'), `${fmtNum(l?.bci, 1, 'A')} / ${fmtNum(l?.bdi, 1, 'A')}`],
              ]}
            />
          </div>
        </Card>
        <Card title={t('live.gridOutput')}>
          <KV
            rows={[
              [t('w.grid'), `${fmtNum(l?.gv, 1, 'V')} · ${fmtNum(l?.gf, 1, 'Hz')}`],
              gm ? [t('live.gridPowerMeter'), fmtW(l?.gw)] : [t('live.gridPower'), fmtPower(l?.gw, va)],
              [t('w.output'), `${fmtNum(l?.ov, 1, 'V')} · ${fmtNum(l?.of, 1, 'Hz')}`],
              [t(lm ? 'live.loadMeter' : 'w.load'), `${lm ? fmtW(l?.w) : fmtPower(l?.w, va)} · ${fmtPct(l?.ld)}`],
              // VA mode: Load already is the VA. W mode on a VA-only inverter: the reported VA is the raw `w`.
              ...(va ? [] : [[t('live.apparent'), fmtNum(pv.est ? (raw?.va ?? raw?.w) : l?.va, 0, 'VA')] as [string, string]]),
              ['PV', `${fmtNum(l?.pvv, 0, 'V')} · ${fmtNum(l?.pvi, 1, 'A')}`],
              [t('live.heatSink'), tmpRaw ? t('live.rawAdc', { v: l!.tmp! }) : fmtNum(l?.tmp, 0, '°C')],
            ]}
          />
          {pv.est && (
            <p class="muted small">
              {va ? t(gm ? 'live.vaNoteVAm' : 'live.vaNoteVA') : t(gm ? 'live.vaNoteWm' : 'live.vaNoteW', { pf: fmtNum(pv.k, 2) })}
            </p>
          )}
          {lm && isNum(meterK) && <p class="muted small">{t('live.fusedNote', { k: fmtNum(meterK, 2) })}</p>}
        </Card>
      </div>

      <Card title={t('nav.today')} right={<span class="muted small">{fmtDateKey(today)}</span>}>
        <div class="tiles">
          <Tile label={t('w.solar')} value={fmtWh(epv)} tone="pv" sub={isNum(d?.epvi ?? l?.epvi) ? t('tile.inverter', { v: fmtWh(d?.epvi ?? l?.epvi) }) : undefined} />
          <Tile label={egrM ? t('tile.loadMeasured') : pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(eld)} tone="load" />
          <Tile label={egrM ? t('tile.gridMeter') : t('tile.gridEst')} value={fmtWh(egr)} tone="grid" />
          <Tile label={t('tile.battInOut')} value={`${fmtWh(l?.ech ?? d?.ech, 1)} / ${fmtWh(l?.edis ?? d?.edis, 1)}`} tone="batt" />
          <Tile label={t('tile.solarFracEst')} value={frac === null ? '–' : dg(`${Math.round(frac * 100)} %`)} sub={t('tile.solarFracSub')} />
          <Tile label={t('tile.peak')} value={`${fmtW(d?.pvx)} / ${fmtPower(d?.wx, va)}`} sub={isNum(d?.gout) ? t('tile.gridOff', { n: d!.gout! }) : undefined} />
        </div>
      </Card>

      <p class="muted small center">
        {status.value?.rssi !== undefined ? t('live.wifi', { n: status.value.rssi }) : ''}
        {isNum(l?.q) && l!.q! > 0 ? t('live.queued', { n: l!.q! }) : ''}
        {dia ? t('live.dialect', { d: dia }) : ''}
      </p>
    </div>
  );
}
