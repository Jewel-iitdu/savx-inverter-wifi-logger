// Energy: daily kWh bars for a month, monthly totals for a year (computed
// client-side from daily/), CSV export of a range or of one day's minutes.
import { useMemo, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useAsync } from '../lib/hooks';
import { addDays, addMonths, dateRange, fmtDateKey, fmtMonth, fmtMonthTiny, monthBounds, monthKey, todayKey } from '../lib/time';
import { localizeDigits as dg, t } from '../lib/i18n';
import { dailyCsv, histCsv } from '../lib/series';
import { fmtWh } from '../lib/format';
import { allDaysMetered, usePowerView, viewDailyMap } from '../lib/power';
import { saveCsv } from '../lib/csv';
import { getDailyMonth, getDailyYear, getHistDay } from '../data/history';
import type { HistRec } from '../data/types';
import { Bars } from '../components/Bars';
import { Card, Empty, Spinner, Tile } from '../components/ui';

const kwh = (v: number | undefined) => (typeof v === 'number' ? v / 1000 : null);

export function EnergyPage() {
  const { source } = useApp();
  const pv = usePowerView();
  const today = todayKey();
  const [ym, setYm] = useState(monthKey(today));
  const [year, setYear] = useState(today.slice(0, 4));
  const month = useAsync(() => getDailyMonth(source, ym), [source, ym]);
  const yearQ = useAsync(() => getDailyYear(source, year), [source, year]);
  useReportError('energy', month.error ?? yearQ.error);

  const days = useMemo(() => {
    const [a, b] = monthBounds(ym);
    return dateRange(a, b);
  }, [ym]);
  // viewDailyMap: the grid meter's kWh (egm) in place of the estimate on metered days (cfg.meter on)
  const md = useMemo(() => viewDailyMap(month.data, pv) ?? {}, [month.data, pv.k, pv.meter]);
  const yd = useMemo(() => viewDailyMap(yearQ.data, pv), [yearQ.data, pv.k, pv.meter]);
  const mGrid = allDaysMetered(month.data, pv) ? t('tile.gridMeter') : t('tile.gridEst');
  const yGrid = allDaysMetered(yearQ.data, pv) ? t('tile.gridMeter') : t('tile.gridEst');
  const mTot = Object.values(md).reduce<{ epv: number; eld: number; egr: number }>(
    (s, r) => ({ epv: s.epv + (r.epv ?? 0), eld: s.eld + (r.eld ?? 0), egr: s.egr + (r.egr ?? 0) }),
    { epv: 0, eld: 0, egr: 0 },
  );

  const months = useMemo(() => {
    const out: Array<{ k: string; epv: number; eld: number; egr: number }> = [];
    for (let i = 1; i <= 12; i++) out.push({ k: `${year}-${String(i).padStart(2, '0')}`, epv: 0, eld: 0, egr: 0 });
    for (const [d, r] of Object.entries(yd ?? {})) {
      const m = out[Number(d.slice(5, 7)) - 1];
      m.epv += r.epv ?? 0;
      m.eld += r.eld ?? 0;
      m.egr += r.egr ?? 0;
    }
    return out;
  }, [yd, year]);
  const yTot = months.reduce((s, m) => ({ epv: s.epv + m.epv, eld: s.eld + m.eld, egr: s.egr + m.egr }), { epv: 0, eld: 0, egr: 0 });

  return (
    <div class="page">
      <Card
        title={
          <span class="picker inline">
            <button type="button" class="btn-icon" aria-label={t('en.prevMonth')} onClick={() => setYm(addMonths(ym, -1))}>
              ‹
            </button>
            {fmtMonth(ym)}
            <button type="button" class="btn-icon" aria-label={t('en.nextMonth')} disabled={ym >= monthKey(today)} onClick={() => setYm(addMonths(ym, 1))}>
              ›
            </button>
          </span>
        }
        right={
          <button class="btn-link" type="button" disabled={!Object.keys(md).length} onClick={() => void saveCsv(`solar-daily-${ym}.csv`, dailyCsv(month.data ?? {}))}>
            CSV
          </button>
        }
      >
        {month.loading ? (
          <Spinner />
        ) : (
          <>
            <Bars
              labels={days.map((d) => String(Number(d.slice(8))))}
              unit="kWh"
              tickEvery={5}
              series={[
                { name: t('w.solar'), color: '--c-pv', values: days.map((d) => kwh(md[d]?.epv)) },
                { name: t('w.load'), color: '--c-load', values: days.map((d) => kwh(md[d]?.eld)) },
                { name: mGrid, color: '--c-grid', values: days.map((d) => kwh(md[d]?.egr)) },
              ]}
            />
            <div class="tiles">
              <Tile label={t('w.solar')} value={fmtWh(mTot.epv, 1)} tone="pv" />
              <Tile label={allDaysMetered(month.data, pv) ? t('tile.loadMeasured') : pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(mTot.eld, 1)} tone="load" />
              <Tile label={mGrid} value={fmtWh(mTot.egr, 1)} tone="grid" />
            </div>
          </>
        )}
      </Card>

      <Card
        title={
          <span class="picker inline">
            <button type="button" class="btn-icon" aria-label={t('en.prevYear')} onClick={() => setYear(String(Number(year) - 1))}>
              ‹
            </button>
            {dg(year)}
            <button type="button" class="btn-icon" aria-label={t('en.nextYear')} disabled={year >= today.slice(0, 4)} onClick={() => setYear(String(Number(year) + 1))}>
              ›
            </button>
          </span>
        }
        right={
          <button class="btn-link" type="button" disabled={!Object.keys(yearQ.data ?? {}).length} onClick={() => void saveCsv(`solar-daily-${year}.csv`, dailyCsv(yearQ.data ?? {}))}>
            CSV
          </button>
        }
      >
        {yearQ.loading ? (
          <Spinner />
        ) : (
          <>
            <Bars
              labels={months.map((m) => fmtMonthTiny(m.k))}
              unit="kWh"
              digits={0}
              series={[
                { name: t('w.solar'), color: '--c-pv', values: months.map((m) => (m.epv ? m.epv / 1000 : null)) },
                { name: t('w.load'), color: '--c-load', values: months.map((m) => (m.eld ? m.eld / 1000 : null)) },
                { name: yGrid, color: '--c-grid', values: months.map((m) => (m.egr ? m.egr / 1000 : null)) },
              ]}
            />
            <div class="tiles">
              <Tile label={t('w.solar')} value={fmtWh(yTot.epv, 0)} tone="pv" />
              <Tile label={allDaysMetered(yearQ.data, pv) ? t('tile.loadMeasured') : pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(yTot.eld, 0)} tone="load" />
              <Tile label={yGrid} value={fmtWh(yTot.egr, 0)} tone="grid" />
            </div>
          </>
        )}
      </Card>

      <MinuteExport />
      <p class="muted small center">{t('en.csvNote')}</p>
    </div>
  );
}

function MinuteExport() {
  const { source } = useApp();
  const [date, setDate] = useState(addDays(todayKey(), -1));
  const [loaded, setLoaded] = useState<{ date: string; recs: Record<string, HistRec> } | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<Error | null>(null);
  useReportError('export', err);
  const load = async () => {
    setBusy(true);
    setErr(null);
    try {
      setLoaded({ date, recs: await getHistDay(source, date) });
    } catch (e) {
      setErr(e instanceof Error ? e : new Error(String(e)));
    } finally {
      setBusy(false);
    }
  };
  const ready = loaded && loaded.date === date;
  const n = ready ? Object.keys(loaded.recs).length : 0;
  return (
    <Card title={t('en.exportTitle')}>
      <div class="picker">
        <input type="date" value={date} max={todayKey()} onChange={(e) => e.currentTarget.value && setDate(e.currentTarget.value)} aria-label={t('en.exportDay')} />
        {!ready ? (
          <button type="button" class="btn" disabled={busy} onClick={load}>
            {busy ? t('common.loading') : t('en.loadBtn')}
          </button>
        ) : (
          <button type="button" class="btn" disabled={!n} onClick={() => void saveCsv(`solar-${date}-minutes.csv`, histCsv(date, loaded.recs))}>
            {t('en.saveCsv', { n })}
          </button>
        )}
      </div>
      {ready && !n && <Empty>{t('en.noMinutes', { date: fmtDateKey(date) })}</Empty>}
      <p class="muted small">{t('en.size')}</p>
    </Card>
  );
}

