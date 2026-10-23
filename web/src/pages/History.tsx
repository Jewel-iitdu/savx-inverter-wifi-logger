// History: day (hist/<date>, optional overlay of a second day), week (h15,
// 7 dates), month and year (daily range queries). All reads date-bounded.
// Opens on today's day view, which is the former Today tab (pages/Today.tsx: live-updating minutes, energy and
// cost today); #/today redirects here (app.tsx).
import { useMemo, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useAsync } from '../lib/hooks';
import { addDays, addMonths, fmtDay, fmtMonth, fmtMonthShort, keyToEpoch, monthBounds, monthKey, todayKey } from '../lib/time';
import { localizeDigits as dg, t } from '../lib/i18n';
import { daySeries, histCsv, multiDaySeries, solarFraction, totalsFromRecs } from '../lib/series';
import { fmtNum, fmtPct, fmtTk, fmtWh, isNum } from '../lib/format';
import { useDayMoney } from '../lib/money';
import { sumMoney, type Money } from '../lib/tariff';
import { MoneyCard } from '../components/MoneyCard';
import { allDaysMetered, gridEnergyMetered, measuredEld, someDaysMetered, usePowerView, viewDailyMap, viewRecs, type PowerView } from '../lib/power';
import { saveCsv } from '../lib/csv';
import { getDailyMonth, getDailyYear, getHistDay, getWeek } from '../data/history';
import type { Daily } from '../data/types';
import { DayCharts } from '../components/DayCharts';
import { UChart } from '../components/UChart';
import { CompareCard } from '../components/CompareCard';
import { Card, Empty, Segmented, Spinner, Tile } from '../components/ui';
import { TodayView } from './Today';

type Mode = 'day' | 'week' | 'month' | 'year';

export function HistoryPage() {
  // Every visit opens on today's day view (the former Today tab).
  const [mode, setMode] = useState<Mode>('day');
  const [date, setDate] = useState(todayKey);
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
      {mode === 'day' && (date === today ? <TodayView /> : <DayView date={date} />)}
      {mode === 'week' && <WeekView end={date} />}
      {mode === 'month' && <MonthView ym={monthKey(date)} />}
      {mode === 'year' && <YearView year={date.slice(0, 4)} />}
    </div>
  );
}

/** A past day (today is TodayView). */
function DayView({ date }: { date: string }) {
  const { source, theme } = useApp();
  const pv = usePowerView();
  const va = pv.unit === 'VA';
  const main = useAsync(() => getHistDay(source, date), [source, date]);
  useReportError('history', main.error);
  const recs = useMemo(() => viewRecs(main.data ?? {}, pv), [main.data, pv.k, pv.meter, pv.unit]);
  const s = useMemo(() => daySeries(date, recs), [date, recs]);
  const tot = useMemo(() => totalsFromRecs(recs), [recs]);
  const t0 = keyToEpoch(date, '0000');
  const money = useDayMoney(date, date);
  // hist/ has no meter data: the day's grid kWh comes from daily/ (egm) when that day was fully metered
  const dd = money.days?.[date];
  const egrM = gridEnergyMetered(dd, pv);
  const egr = egrM ? dd!.egm! : tot.egr;
  const eld = (egrM ? measuredEld(dd) : null) ?? tot.eld; // metered day: grid + solar + battery, all measured
  const frac = solarFraction(eld, egr);

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
      </Card>
      {s.count > 0 && (
        <Card title={t('w.energy')}>
          <div class="tiles">
            <Tile label={t('w.solar')} value={fmtWh(tot.epv)} tone="pv" />
            <Tile label={egrM ? t('tile.loadMeasured') : pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(eld)} tone="load" />
            <Tile label={egrM ? t('tile.gridMeter') : t('tile.gridEst')} value={fmtWh(egr)} tone="grid" />
            <Tile label={t('tile.battInOut')} value={`${fmtWh(tot.ech, 1)} / ${fmtWh(tot.edis, 1)}`} tone="batt" />
            <Tile label={t('tile.solarFracEst')} value={frac === null ? '–' : dg(`${Math.round(frac * 100)} %`)} />
            <Tile label={t('tile.coverage')} value={dg(`${Math.round((tot.minutes / 1440) * 100)} %`)} sub={t('tile.coverageSub', { n: tot.minutes })} />
          </div>
        </Card>
      )}
      {s.count > 0 && <MoneyCard title={t('money.title')} date={date} m={money} />}
      <CompareCard date={date} recs={recs} />
    </>
  );
}

function WeekView({ end }: { end: string }) {
  const { source, theme } = useApp();
  const pv = usePowerView();
  const pu = pv.unit;
  const week = useAsync(() => getWeek(source, end), [source, end]);
  useReportError('week', week.error);
  const s = useMemo(() => (week.data ? multiDaySeries(week.data.map((d) => ({ ...d, recs: viewRecs(d.recs, pv) })), 15) : null), [week.data, pv.k, pv.meter, pv.unit]);
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

const tk0 = (v: number | undefined) => (isNum(v) ? fmtNum(v, 0) : '–');

/** The "kWh · *grid …" note: from the grid meter on every day, on some days, or estimated. */
const kwhNote = (m: Record<string, Daily> | null | undefined, p: PowerView) =>
  allDaysMetered(m, p) ? t('hist.kwhNoteMeter') : someDaysMetered(m, p) ? t('hist.kwhNoteMixed') : t('hist.kwhNote');

function DailyTable({ rows, money }: { rows: Array<[string, Daily]>; money: Record<string, Money> | null }) {
  return (
    <div class="table-wrap">
      <table class="tbl">
        <thead>
          <tr>
            <th>{t('th.day')}</th>
            <th>PV</th>
            <th>{t('w.load')}</th>
            <th>{t('th.gridStar')}</th>
            <th>{t('th.gridTk')}</th>
            <th>{t('th.savedTk')}</th>
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
              <td>{tk0(money?.[d]?.gridTk)}</td>
              <td>{tk0(money?.[d]?.savedTk)}</td>
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
  const data = useMemo(() => viewDailyMap(q.data, pv), [q.data, pv.k, pv.meter]);
  const rows = Object.entries(data ?? {}).sort((a, b) => (a[0] < b[0] ? -1 : 1));
  const tot = rows.reduce((s, [, r]) => ({ epv: s.epv + (r.epv ?? 0), eld: s.eld + (r.eld ?? 0), egr: s.egr + (r.egr ?? 0) }), { epv: 0, eld: 0, egr: 0 });
  const [first, last] = monthBounds(ym);
  const m = useDayMoney(first, last);
  const mt = m.money ? sumMoney(m.money, first, last) : null;
  return (
    <Card title={fmtMonth(ym)} right={<span class="muted small">{kwhNote(q.data, pv)}</span>}>
      {q.loading ? (
        <Spinner />
      ) : rows.length === 0 ? (
        <Empty>{t('hist.monthEmpty')}</Empty>
      ) : (
        <>
          <div class="tiles">
            <Tile label={t('w.solar')} value={fmtWh(tot.epv, 1)} tone="pv" />
            <Tile label={allDaysMetered(q.data, pv) ? t('tile.loadMeasured') : pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(tot.eld, 1)} tone="load" />
            <Tile label={allDaysMetered(q.data, pv) ? t('tile.gridMeter') : t('tile.gridEst')} value={fmtWh(tot.egr, 1)} tone="grid" />
          </div>
          <MoneyTiles m={mt} />
          <DailyTable rows={rows} money={m.money} />
          <p class="muted small">{t('money.note', { n: m.monthStart ?? 1 })}</p>
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
  const data = useMemo(() => viewDailyMap(q.data, pv), [q.data, pv.k, pv.meter]);
  const m = useDayMoney(`${year}-01-01`, `${year}-12-31`);
  const yt = m.money ? sumMoney(m.money, `${year}-01-01`, `${year}-12-31`) : null;
  const mm = (k: string) => (m.money ? sumMoney(m.money, `${k}-01`, `${k}-31`) : null);
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
    <Card title={dg(year)} right={<span class="muted small">{kwhNote(q.data, pv)}</span>}>
      {q.loading ? (
        <Spinner />
      ) : months.length === 0 ? (
        <Empty>{t('hist.yearEmpty')}</Empty>
      ) : (
        <>
          <MoneyTiles m={yt} />
          <div class="table-wrap">
            <table class="tbl">
              <thead>
                <tr>
                  <th>{t('th.month')}</th>
                  <th>PV</th>
                  <th>{t('w.load')}</th>
                  <th>{t('th.gridStar')}</th>
                  <th>{t('th.billTk')}</th>
                  <th>{t('th.savedTk')}</th>
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
                    <td>{tk0(mm(k)?.gridTk)}</td>
                    <td>{tk0(mm(k)?.savedTk)}</td>
                    <td>{dg((e.epv / 1000 / Math.max(1, e.days)).toFixed(1))}</td>
                    <td>{t('u.h', { n: Number((e.gout / 60).toFixed(0)) })}</td>
                    <td>{dg(String(e.days))}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p class="muted small">{t('money.note', { n: m.monthStart ?? 1 })}</p>
        </>
      )}
    </Card>
  );
}

/** Grid bill, solar saving, the bill without solar and how much solar cut it, for a month or a year. */
function MoneyTiles({ m }: { m: Money | null }) {
  if (!m) return <Spinner />;
  return (
    <div class="tiles">
      <Tile label={t('money.bill')} value={fmtTk(m.gridTk)} sub={t('money.subGrid', { e: fmtNum(m.grid, 0, 'kWh') })} tone="grid" />
      <Tile label={t('money.saved')} value={fmtTk(m.savedTk)} sub={t('money.subSolar', { e: fmtNum(m.solar, 0, 'kWh') })} tone="pv" />
      <Tile label={t('money.without')} value={fmtTk(m.gridTk + m.savedTk)} sub={t('money.withoutSub')} />
      <Tile label={t('money.cut')} value={m.gridTk + m.savedTk > 0 ? fmtPct((m.savedTk / (m.gridTk + m.savedTk)) * 100) : '–'} sub={t('money.cutSub')} tone="pv" />
    </div>
  );
}
