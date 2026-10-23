// Meter: the grid meter (fw 0.9.x, a Smart Life/Tuya breaker with an energy meter on the grid line before the
// inverter), READ-ONLY. Live values from live/ (the m* fields), the link state from status/meter, today's metered kWh
// (live egm) and the last 30 days of daily/ egm (data/history.ts cache). Switching the breaker, its schedules and
// alarm limits stay in the Smart Life app: nothing here writes to the meter.
import type { ComponentChildren } from 'preact';
import { useApp, useReportError } from '../context';
import { useMemo } from 'preact/hooks';
import { useAsync, useNow, useSubscription } from '../lib/hooks';
import { addDays, dateKey, dateRange, fmtAgo, fmtClock, fmtDay, fmtDuration, fmtTime, todayKey } from '../lib/time';
import { t, tj } from '../lib/i18n';
import { fmtNum, fmtW, fmtWh, isNum } from '../lib/format';
import { LEAK_HIGH_MA, gridCuts, meterAlarms, meterState, nextPlanned } from '../lib/meter';
import { getDailySpan } from '../data/history';
import type { Cfg, EventRec, Live, Status } from '../data/types';
import { Bars } from '../components/Bars';
import { Badge, Banner, Card, KV, Spinner, Tile } from '../components/ui';
import { STALE_SEC } from './Live';

const DAYS = 30;
const SCHED_DAYS = 7;
const SCHED_ROWS = 8;
const clock = (min: number) => fmtClock(Math.floor(min / 60), min % 60);

/** Planned grid-off (Settings) and the grid cuts of the last 7 days from the logger's events (read-only). */
function ScheduleCard({ goFrom, goTo, now }: { goFrom: number; goTo: number; now: number }) {
  const { source } = useApp();
  const today = todayKey();
  const dates = useMemo(() => dateRange(addDays(today, -(SCHED_DAYS - 1)), today), [today]);
  const tick = Math.floor(now / 300); // re-read every 5 min while the page is open
  const q = useAsync(
    () => Promise.all(dates.map((d) => source.getEvents(d))).then((rs) => rs.flatMap((r) => Object.values(r ?? {}) as EventRec[])),
    [source, dates, tick],
  );
  useReportError('events', q.error);
  const cuts = useMemo(() => (q.data ? gridCuts(q.data, goFrom) : []), [q.data, goFrom]);
  const shown = cuts.slice(-SCHED_ROWS).reverse();
  const next = nextPlanned(goFrom, goTo, now);
  const dayOf = (ts: number) => {
    const d = dateKey(ts);
    return d === today ? t('nav.today') : d === addDays(today, -1) ? t('meter.yesterday') : fmtDay(d);
  };
  return (
    <Card title={t('meter.sched')}>
      <KV
        rows={[
          [t('meter.schedPlanned'), next ? t('meter.schedWindow', { from: clock(goFrom), to: clock(goTo) }) : <a href="#/settings">{t('meter.schedNone')}</a>],
          ...(next
            ? ([[t('meter.schedNext'), t(next.off ? 'meter.schedNextOff' : 'meter.schedNextOn', { at: clock(next.min), in: fmtDuration(next.inSec) })]] as Array<[string, string]>)
            : []),
        ]}
      />
      <h3 class="chart-t">{t('meter.schedActual')}</h3>
      {q.loading && !q.data ? (
        <Spinner />
      ) : shown.length === 0 ? (
        <p class="muted small">{t('meter.schedNoCuts')}</p>
      ) : (
        <dl class="kv">
          {shown.map((c, i) => {
            const at = c.from ?? c.to!;
            const span = c.from && c.to ? fmtDuration(c.to - c.from) : c.from ? t('meter.schedSoFar', { d: fmtDuration(now - c.from) }) : '';
            const times = c.from && c.to ? `${fmtTime(c.from)} → ${fmtTime(c.to)}` : c.from ? t('meter.schedSince', { off: fmtTime(c.from) }) : `… → ${fmtTime(c.to!)}`;
            return (
              <div class="kv-r" key={`${at}-${i}`}>
                <dt>{dayOf(at)}</dt>
                <dd>
                  {times}
                  <br />
                  <span class="muted small">
                    {span}
                    {span ? ' · ' : ''}
                    <Badge tone={c.planned ? 'ok' : 'warn'} plain>
                      {c.planned ? t('meter.schedTagPlanned') : t('meter.schedTagOutage')}
                    </Badge>
                  </span>
                </dd>
              </div>
            );
          })}
        </dl>
      )}
      <p class="muted small">{t('meter.schedNote')}</p>
    </Card>
  );
}

export function MeterPage() {
  const { source, visible, meter } = useApp();
  const now = useNow(1000);
  const today = todayKey();
  const from = addDays(today, -(DAYS - 1));
  const live = useSubscription<Live | null>((cb, err) => source.onLive(cb, err), [source], visible);
  const status = useSubscription<Status | null>((cb, err) => source.onStatus(cb, err), [source], visible);
  const cfgSub = useSubscription<Partial<Cfg> | null>((cb, err) => source.onCfg(cb, err), [source], visible);
  const span = useAsync(() => getDailySpan(source, from, today), [source, from, today]);
  useReportError('live', live.error);
  useReportError('status', status.error);
  useReportError('meter', span.error);

  const l = live.value ?? null;
  const loading = live.value === undefined;
  const age = l?.ts ? now - l.ts : Infinity;
  const stale = !loading && age > STALE_SEC;
  // The logger sends the m* fields only while the meter is on and its reading is fresh.
  const has = meter && !!l && [l.mw, l.mv, l.mi, l.mon, l.mkwh].some(isNum);
  const gridOff = !!l && isNum(l.gv) && l.gv < 90;
  const st = meterState(status.value?.meter);
  const readAge = has && isNum(l!.mage) ? age + l!.mage! : null;
  const leakHigh = has && isNum(l!.mrc) && l!.mrc! > LEAK_HIGH_MA;
  // While the grid is off (the daily planned cut) the meter has no power: "not reachable" is expected, not red.
  const connTone = st && !(gridOff && st.tone === 'bad') && (st.tone === 'ok' || st.tone === 'warn' || st.tone === 'bad') ? st.tone : '';
  const conn = st ? <span class={connTone}>{st.text}</span> : '–';
  const rows: Array<[string, ComponentChildren]> = [];
  if (has) {
    const m = l!;
    rows.push(
      [t('meter.breaker'), isNum(m.mon) ? <Badge tone={m.mon ? 'ok' : 'bad'}>{m.mon ? t('common.On') : t('common.Off')}</Badge> : '–'],
      [t('live.voltage'), fmtNum(m.mv, 1, 'V')],
      [t('live.current'), fmtNum(m.mi, 2, 'A')],
      [t('meter.power'), fmtW(m.mw)],
      [t('meter.pf'), fmtNum(m.mpf, 2)],
      [t('meter.freq'), fmtNum(m.mhz, 2, 'Hz')],
      [
        t('meter.leak'),
        leakHigh ? (
          <>
            <Badge tone="warn" plain>
              {t('meter.leakHigh')}
            </Badge>{' '}
            {fmtNum(m.mrc, 0, 'mA')}
          </>
        ) : (
          fmtNum(m.mrc, 0, 'mA')
        ),
      ],
      [t('meter.temp'), fmtNum(m.mtc, 0, '°C')],
      [t('meter.total'), fmtNum(m.mkwh, 2, 'kWh')],
    );
    if (readAge !== null) rows.push([t('meter.read'), fmtAgo(readAge)]);
  }
  rows.push([t('meter.conn'), conn]);

  // metered grid energy: today from live (updates every upload), the other days from daily/
  const dates = dateRange(from, today);
  const egmToday = (meter && isNum(l?.egm) ? l!.egm : undefined) ?? span.data?.[today]?.egm;
  const egm = (d: string) => (d === today ? egmToday : span.data?.[d]?.egm);
  const past = dates.filter((d) => d !== today && isNum(egm(d)));
  const metered = past.length + (isNum(egmToday) ? 1 : 0);
  const total = past.reduce((s, d) => s + egm(d)!, 0) + (egmToday ?? 0);
  const avg = past.length ? past.reduce((s, d) => s + egm(d)!, 0) / past.length : undefined;

  return (
    <div class="page">
      {!meter && (
        <Banner tone="info">
          <strong>{t('meter.offTitle')}</strong> {tj('meter.offBody', { link: <a href="#/settings">{t('set.meter')}</a> })}
        </Banner>
      )}
      {meter && stale && (
        <Banner tone="bad">
          <strong>{t('live.offlineTitle')}</strong> {t('live.offlineBody', { ago: fmtAgo(age), at: l?.ts ? ` (${fmtTime(l.ts)})` : '' })}
        </Banner>
      )}
      {meter && !loading && !stale && !has && <Banner tone={gridOff ? 'info' : 'warn'}>{t(gridOff ? 'meter.gridOff' : 'meter.noReading')}</Banner>}

      {meter && (
        <Card title={t('meter.title')}>
          {loading ? <Spinner /> : <KV rows={rows} />}
          {leakHigh && <p class="muted small">{t('meter.leakHint')}</p>}
          <p class="muted small">{t('meter.readOnly')}</p>
        </Card>
      )}

      {has && isNum(l!.mft) && (
        <Card title={t('meter.alarms')}>
          {l!.mft === 0 ? (
            <div>
              <Badge tone="ok" plain>
                {t('meter.noAlarms')}
              </Badge>
            </div>
          ) : (
            <ul class="alarms">
              {meterAlarms(l!.mft!).map((a) => (
                <li key={a.bit}>
                  <Badge tone={a.early ? 'warn' : 'bad'}>{a.early ? t('live.clsWarning') : t('meter.clsAlarm')}</Badge> {a.label}
                </li>
              ))}
            </ul>
          )}
          <p class="muted small">{t('meter.alarmsNote')}</p>
        </Card>
      )}

      <ScheduleCard goFrom={cfgSub.value?.gridOffFrom ?? -1} goTo={cfgSub.value?.gridOffTo ?? -1} now={now} />

      <Card title={t('meter.energy')}>
        <div class="tiles">
          <Tile label={t('nav.today')} value={fmtWh(egmToday)} sub={isNum(egmToday) ? undefined : t('meter.partial')} tone="grid" />
          <Tile label={t('meter.last30')} value={fmtWh(metered ? total : undefined, 1)} sub={t('meter.daysMetered', { n: metered })} tone="grid" />
          <Tile label={t('meter.avg')} value={fmtWh(avg)} />
        </div>
        {span.loading && !span.data ? (
          <Spinner />
        ) : (
          <Bars
            labels={dates.map((d) => String(Number(d.slice(8))))}
            unit="kWh"
            tickEvery={5}
            digits={2}
            series={[{ name: t('tile.gridMeter'), color: '--c-grid', values: dates.map((d) => (isNum(egm(d)) ? egm(d)! / 1000 : null)) }]}
          />
        )}
        <p class="muted small">{t('meter.daysNote')}</p>
      </Card>

      {!meter && <p class="muted small center">{t('meter.readOnly')}</p>}
      <p class="muted small center">{t('meter.note')}</p>
    </div>
  );
}
