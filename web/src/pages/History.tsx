// History: day (hist/<date>, optional overlay of a second day), week (h15,
// 7 dates), month and year (daily range queries). All reads date-bounded.
import { useMemo, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useAsync, useStored } from '../lib/hooks';
import { addDays, addMonths, fmtDay, fmtMonth, fmtMonthShort, keyToEpoch, monthKey, todayKey } from '../lib/time';
import { localizeDigits as dg, t } from '../lib/i18n';
import { daySeries, histCsv, multiDaySeries, solarFraction, totalsFromRecs } from '../lib/series';
import { fmtNum, fmtWh, isNum } from '../lib/format';
import { usePowerView, viewDailyMap, viewRecs } from '../lib/power';
import { saveCsv } from '../lib/csv';
import { getDailyMonth, getDailyYear, getHistDay, getWeek } from '../data/history';
import type { Daily } from '../data/types';
import { DayCharts } from '../components/DayCharts';
import { UChart } from '../components/UChart';
import { Card, Empty, Segmented, Spinner, Tile } from '../components/ui';

type Mode = 'day' | 'week' | 'month' | 'year';

export function HistoryPage() {
  const [mode, setMode] = useStored<Mode>('history.mode', 'day');
  const [date, setDate] = useState(() => addDays(todayKey(), -1));
  const today = todayKey();
  const step = mode === 'day' ? 1 : mode === 'week' ? 7 : 0;

  const shift = (dir: -1 | 1) => {
    if (mode === 'month') setDate(`${addMonths(monthKey(date), dir)}-01`);
    else if (mode === 'year') setDate(`${Number(date.slice(0, 4)) + dir}-01-01`);
    else setDate(addDays(date, dir * step));
  };
  const atEnd = mode === 'month' ? monthKey(date) >= monthKey(today) : mode === 'year' ? date.slice(0, 4) >= today.slice(0, 4) : date >= today;

  return (
    <div class="page">
      <Card>
        <Segmented
          label={t('hist.view')}
          value={mode}
          options={[
            ['day', t('hist.day')],
            ['week', t('hist.week')],
            ['month', t('hist.month')],
            ['year', t('hist.year')],
          ]}
          onChange={setMode}
        />
        <div class="picker">
          <button type="button" class="btn-icon" aria-label={t('common.prev')} onClick={() => shift(-1)}>
            ‹
          </button>
          {mode === 'day' || mode === 'week' ? (
            <input type="date" value={date} max={today} onChange={(e) => e.currentTarget.value && setDate(e.currentTarget.value)} aria-label={t('common.date')} />
          ) : (
            <strong>{mode === 'month' ? fmtMonth(monthKey(date)) : dg(date.slice(0, 4))}</strong>
          )}
          <button type="button" class="btn-icon" aria-label={t('common.next')} disabled={atEnd} onClick={() => shift(1)}>
            ›
          </button>
        </div>
      </Card>
      {mode === 'day' && <DayView date={date} />}
      {mode === 'week' && <WeekView end={date} />}
      {mode === 'month' && <MonthView ym={monthKey(date)} />}
      {mode === 'year' && <YearView year={date.slice(0, 4)} />}
    </div>
  );
}

function DayView({ date }: { date: string }) {
  const { source, theme } = useApp();
  const pv = usePowerView();
  const va = pv.unit === 'VA';
  const pu = pv.unit;
  const [compare, setCompare] = useState<string>('');
  const main = useAsync(() => getHistDay(source, date), [source, date]);
  const other = useAsync(() => getHistDay(source, compare), [source, compare], !!compare);
  useReportError('history', main.error ?? other.error);
  const recs = useMemo(() => viewRecs(main.data ?? {}, pv), [main.data, pv.k]);
  const otherRecs = useMemo(() => (other.data ? viewRecs(other.data, pv) : null), [other.data, pv.k]);
  const s = useMemo(() => daySeries(date, recs), [date, recs]);
  const tot = useMemo(() => totalsFromRecs(recs), [recs]);
  const t0 = keyToEpoch(date, '0000');
  const overlay = useMemo(() => {
    if (!compare || !otherRecs) return null;
    const a = daySeries(date, recs, 1, 1439);
    const b = daySeries(date, otherRecs, 1, 1439); // mapped onto the same clock
    return [a.xs, a.load, b.load, a.pv, b.pv];
  }, [compare, otherRecs, recs, date]);
  const frac = solarFraction(tot.eld, tot.egr);

  return (
    <>
      <Card
        title={fmtDay(date)}
        right={
          <button class="btn-link" type="button" disabled={!s.count} onClick={() => void saveCsv(`solar-${date}-minutes.csv`, histCsv(date, recs))}>
            {t('common.exportCsv')}
          </button>
        }
      >
        {main.loading ? <Spinner /> : s.count === 0 ? <Empty>{t('hist.dayEmpty')}</Empty> : <DayCharts s={s} themeKey={theme} syncKey="hist-day" xRange={[t0, t0 + 86400]} va={va} />}
        {date === todayKey() && <p class="muted small">{t('hist.todayHint')}</p>}
      </Card>
      {s.count > 0 && (
        <Card title={t('w.energy')}>
          <div class="tiles">
            <Tile label={t('w.solar')} value={fmtWh(tot.epv)} tone="pv" />
            <Tile label={pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(tot.eld)} tone="load" />
            <Tile label={t('tile.gridEst')} value={fmtWh(tot.egr)} tone="grid" />
            <Tile label={t('tile.battInOut')} value={`${fmtWh(tot.ech, 1)} / ${fmtWh(tot.edis, 1)}`} tone="batt" />
            <Tile label={t('tile.solarFracEst')} value={frac === null ? '–' : dg(`${Math.round(frac * 100)} %`)} />
            <Tile label={t('tile.coverage')} value={dg(`${Math.round((tot.minutes / 1440) * 100)} %`)} sub={t('tile.coverageSub', { n: tot.minutes })} />
          </div>
        </Card>
      )}
      <Card title={t('hist.compare')}>
        <div class="picker">
          <input type="date" value={compare} max={todayKey()} onChange={(e) => setCompare(e.currentTarget.value)} aria-label={t('hist.compareDate')} />
          {compare && (
            <button type="button" class="btn-link" onClick={() => setCompare('')}>
              {t('common.clear')}
            </button>
          )}
        </div>
        {compare && other.loading && <Spinner />}
        {overlay && (
          <UChart
            data={overlay}
            themeKey={theme}
            timeOnly
            xRange={[t0, t0 + 86400]}
            series={[
              { label: `${t('w.load')} ${date.slice(5)}`, color: '--c-load', unit: pu },
              { label: `${t('w.load')} ${compare.slice(5)}`, color: '--c-load', unit: pu, dash: true },
              { label: `PV ${date.slice(5)}`, color: '--c-pv', unit: 'W' },
              { label: `PV ${compare.slice(5)}`, color: '--c-pv', unit: 'W', dash: true },
            ]}
            axes={[{ scale: 'y', label: pu === 'VA' ? 'W / VA' : 'W' }]}
          />
        )}
      </Card>
    </>
  );
}

function WeekView({ end }: { end: string }) {
  const { source, theme } = useApp();
  const pv = usePowerView();
  const pu = pv.unit;
  const week = useAsync(() => getWeek(source, end), [source, end]);
  useReportError('week', week.error);
  const s = useMemo(() => (week.data ? multiDaySeries(week.data.map((d) => ({ ...d, recs: viewRecs(d.recs, pv) })), 15) : null), [week.data, pv.k]);
  const start = addDays(end, -6);
  const x0 = keyToEpoch(start, '0000');
  return (
    <Card title={`${fmtDay(start)} – ${fmtDay(end)}`} right={<span class="muted small">{t('hist.avg15')}</span>}>
      {week.loading || !s ? (
        <Spinner />
      ) : s.count === 0 ? (
        <Empty>{t('hist.weekEmpty')}</Empty>
      ) : (
        <>
          <UChart
            data={[s.xs, s.pv, s.load]}
            themeKey={theme}
            syncKey="week"
            xRange={[x0, x0 + 7 * 86400]}
            series={[
              { label: 'PV', color: '--c-pv', unit: 'W', fill: true },
              { label: t('w.load'), color: '--c-load', unit: pu },
            ]}
            axes={[{ scale: 'y', label: pu === 'VA' ? 'W / VA' : 'W' }]}
          />
          <UChart
            data={[s.xs, s.soc, s.gv]}
            themeKey={theme}
            syncKey="week"
            height={150}
            xRange={[x0, x0 + 7 * 86400]}
            series={[
              { label: 'SOC', color: '--c-batt', unit: '%', scale: '%', fill: true },
              { label: t('chart.gridV'), color: '--c-grid', unit: 'V', scale: 'V' },
            ]}
            axes={[
              { scale: '%', label: '%', range: [0, 100] },
              { scale: 'V', side: 'right', label: 'V' },
            ]}
          />
        </>
      )}
    </Card>
  );
}

function DailyTable({ rows }: { rows: Array<[string, Daily]> }) {
  return (
    <div class="table-wrap">
      <table class="tbl">
        <thead>
          <tr>
            <th>{t('th.day')}</th>
            <th>PV</th>
            <th>{t('w.load')}</th>
            <th>{t('th.gridStar')}</th>
            <th>SOC</th>
            <th>{t('th.battV')}</th>
            <th>{t('th.noGrid')}</th>
            <th>{t('th.data')}</th>
          </tr>
        </thead>
        <tbody>
          {rows.map(([d, r]) => (
            <tr key={d}>
              <td>{dg(d.slice(8))}</td>
              <td>{fmtNum(isNum(r.epv) ? r.epv / 1000 : undefined, 1)}</td>
              <td>{fmtNum(isNum(r.eld) ? r.eld / 1000 : undefined, 1)}</td>
              <td>{fmtNum(isNum(r.egr) ? r.egr / 1000 : undefined, 1)}</td>
              <td>
                {fmtNum(r.socn)}–{fmtNum(r.socx)}
              </td>
              <td>
                {fmtNum(r.bvn, 1)}–{fmtNum(r.bvx, 1)}
              </td>
              <td>{isNum(r.gout) ? t('hist.minShort', { n: r.gout }) : '–'}</td>
              <td>{isNum(r.n) ? dg(`${Math.round((r.n / 1440) * 100)}%`) : '–'}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function MonthView({ ym }: { ym: string }) {
  const { source } = useApp();
  const pv = usePowerView();
  const q = useAsync(() => getDailyMonth(source, ym), [source, ym]);
  useReportError('month', q.error);
  const data = useMemo(() => viewDailyMap(q.data, pv), [q.data, pv.k]);
  const rows = Object.entries(data ?? {}).sort((a, b) => (a[0] < b[0] ? -1 : 1));
  const tot = rows.reduce((s, [, r]) => ({ epv: s.epv + (r.epv ?? 0), eld: s.eld + (r.eld ?? 0), egr: s.egr + (r.egr ?? 0) }), { epv: 0, eld: 0, egr: 0 });
  return (
    <Card title={fmtMonth(ym)} right={<span class="muted small">{t('hist.kwhNote')}</span>}>
      {q.loading ? (
        <Spinner />
      ) : rows.length === 0 ? (
        <Empty>{t('hist.monthEmpty')}</Empty>
      ) : (
        <>
          <div class="tiles">
            <Tile label={t('w.solar')} value={fmtWh(tot.epv, 1)} tone="pv" />
            <Tile label={pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(tot.eld, 1)} tone="load" />
            <Tile label={t('tile.gridEst')} value={fmtWh(tot.egr, 1)} tone="grid" />
          </div>
          <DailyTable rows={rows} />
        </>
      )}
    </Card>
  );
}

function YearView({ year }: { year: string }) {
  const { source } = useApp();
  const pv = usePowerView();
  const q = useAsync(() => getDailyYear(source, year), [source, year]);
  useReportError('year', q.error);
  const data = useMemo(() => viewDailyMap(q.data, pv), [q.data, pv.k]);
  const months = useMemo(() => {
    const m = new Map<string, { epv: number; eld: number; egr: number; days: number; gout: number }>();
    for (const [d, r] of Object.entries(data ?? {})) {
      const k = d.slice(0, 7);
      const e = m.get(k) ?? { epv: 0, eld: 0, egr: 0, days: 0, gout: 0 };
      e.epv += r.epv ?? 0;
      e.eld += r.eld ?? 0;
      e.egr += r.egr ?? 0;
      e.gout += r.gout ?? 0;
      e.days += 1;
      m.set(k, e);
    }
    return [...m.entries()].sort((a, b) => (a[0] < b[0] ? -1 : 1));
  }, [data]);
  return (
    <Card title={dg(year)} right={<span class="muted small">{t('hist.kwhNote')}</span>}>
      {q.loading ? (
        <Spinner />
      ) : months.length === 0 ? (
        <Empty>{t('hist.yearEmpty')}</Empty>
      ) : (
        <div class="table-wrap">
          <table class="tbl">
            <thead>
              <tr>
                <th>{t('th.month')}</th>
                <th>PV</th>
                <th>{t('w.load')}</th>
                <th>{t('th.gridStar')}</th>
                <th>{t('th.pvDay')}</th>
                <th>{t('th.noGrid')}</th>
                <th>{t('th.days')}</th>
              </tr>
            </thead>
            <tbody>
              {months.map(([k, e]) => (
                <tr key={k}>
                  <td>{fmtMonthShort(k)}</td>
                  <td>{dg((e.epv / 1000).toFixed(0))}</td>
                  <td>{dg((e.eld / 1000).toFixed(0))}</td>
                  <td>{dg((e.egr / 1000).toFixed(0))}</td>
                  <td>{dg((e.epv / 1000 / Math.max(1, e.days)).toFixed(1))}</td>
                  <td>{t('u.h', { n: Number((e.gout / 60).toFixed(0)) })}</td>
                  <td>{dg(String(e.days))}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </Card>
  );
}
