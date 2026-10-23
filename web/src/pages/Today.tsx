// Today: get(hist/<today>) once, then onChildAdded(startAfter(lastKey)) while
// visible (d1 §6.2, §6.4). Minutes stay in memory for the session.
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useNow, useStored, useSubscription } from '../lib/hooks';
import { dateKey, fmtDateKey, keyToEpoch } from '../lib/time';
import { localizeDigits as dg, t } from '../lib/i18n';
import { daySeries, histCsv, solarFraction, totalsFromRecs } from '../lib/series';
import { fmtNum, fmtWh, isNum } from '../lib/format';
import { usePowerView, viewDaily, viewRecs } from '../lib/power';
import { saveCsv } from '../lib/csv';
import { todayMem } from '../data/history';
import type { Daily, HistRec } from '../data/types';
import { DayCharts } from '../components/DayCharts';
import { MoneyCard } from '../components/MoneyCard';
import { useDayMoney } from '../lib/money';
import { Card, Empty, Segmented, Spinner, Tile } from '../components/ui';

type Range = '6' | '12' | '24';

export function TodayPage() {
  const { source, visible, theme } = useApp();
  const pv = usePowerView();
  const now = useNow(30000);
  const date = dateKey(now);
  const [recs, setRecs] = useState<Record<string, HistRec>>(() => (todayMem.date === date ? todayMem.recs : {}));
  const [loading, setLoading] = useState(todayMem.date !== date || todayMem.lastKey === null);
  const [error, setError] = useState<Error | null>(null);
  const [range, setRange] = useStored<Range>('today.range', '24');
  const [reloadNonce, setReloadNonce] = useState(0);
  const flush = useRef<number | null>(null);
  useReportError('today', error);
  const daily = useSubscription<Daily | null>((cb, err) => source.onDaily(date, cb, err), [source, date], visible);

  useEffect(() => {
    if (!visible) return;
    let alive = true;
    let unsub: (() => void) | null = null;
    const schedule = () => {
      if (flush.current !== null) return;
      flush.current = window.setTimeout(() => {
        flush.current = null;
        if (alive) setRecs(todayMem.recs);
      }, 150);
    };
    (async () => {
      if (todayMem.date !== date) todayMem.reset(date);
      if (todayMem.lastKey === null && Object.keys(todayMem.recs).length === 0) {
        setLoading(true);
        const all = await source.getHist(date);
        if (!alive) return;
        todayMem.recs = { ...all };
        const keys = Object.keys(all).sort();
        todayMem.lastKey = keys.length ? keys[keys.length - 1] : null;
        setRecs(todayMem.recs);
      }
      setLoading(false);
      unsub = source.onHistAdded(
        date,
        todayMem.lastKey,
        (k, r) => {
          if (todayMem.date !== date) return;
          todayMem.recs = { ...todayMem.recs, [k]: r };
          if (!todayMem.lastKey || k > todayMem.lastKey) todayMem.lastKey = k;
          schedule();
        },
        (e) => setError(e),
      );
    })().catch((e: unknown) => {
      if (alive) {
        setLoading(false);
        setError(e instanceof Error ? e : new Error(String(e)));
      }
    });
    return () => {
      alive = false;
      unsub?.();
      if (flush.current !== null) {
        clearTimeout(flush.current);
        flush.current = null;
      }
    };
  }, [visible, date, source, reloadNonce]);

  const vrecs = useMemo(() => viewRecs(recs, pv), [recs, pv.k]);
  const s = useMemo(() => daySeries(date, vrecs), [date, vrecs]);
  const totals = useMemo(() => totalsFromRecs(vrecs), [vrecs]);
  const d = viewDaily(daily.value ?? null, pv);
  const epv = d?.epv ?? totals.epv;
  const eld = d?.eld ?? totals.eld;
  const egr = d?.egr ?? totals.egr;
  const frac = solarFraction(eld, egr);
  // money from the raw totals: useDayMoney applies the kWh (bill) view itself
  const raw = useMemo(() => totalsFromRecs(recs), [recs]);
  const money = useDayMoney(date, date, { [date]: daily.value ?? { egr: raw.egr, epv: raw.epv } });
  const recon = isNum(d?.epvi) && d!.epvi! > 0 ? ((epv - d!.epvi!) / d!.epvi!) * 100 : null;
  const t0 = keyToEpoch(date, '0000');
  const xRange: [number, number] = range === '24' ? [t0, t0 + 86400] : [now - Number(range) * 3600, now + 300];

  const reload = () => {
    todayMem.reset(date);
    setRecs({});
    setReloadNonce((n) => n + 1);
  };

  return (
    <div class="page">
      <Card
        title={t('today.title', { date: fmtDateKey(date) })}
        right={<Segmented label={t('today.range')} value={range} options={[['6', t('u.h', { n: 6 })], ['12', t('u.h', { n: 12 })], ['24', t('today.day')]]} onChange={setRange} />}
      >
        {loading ? (
          <Spinner />
        ) : s.count === 0 ? (
          <Empty>{t('today.empty')}</Empty>
        ) : (
          <DayCharts s={s} themeKey={theme} xRange={xRange} syncKey="today" va={pv.unit === 'VA'} />
        )}
        <div class="row-end">
          <span class="muted small">{t('today.count', { n: s.count, total: Math.min(1440, Math.floor((now - t0) / 60)) })}</span>
          <button class="btn-link" onClick={reload} type="button">
            {t('common.reload')}
          </button>
          <button class="btn-link" type="button" disabled={!s.count} onClick={() => void saveCsv(`solar-${date}-minutes.csv`, histCsv(date, recs))}>
            {t('common.exportCsv')}
          </button>
        </div>
      </Card>

      <Card title={t('today.energy')}>
        <div class="tiles">
          <Tile label={t('w.solar')} value={fmtWh(epv)} tone="pv" />
          <Tile label={pv.est ? t('tile.loadEst') : t('w.load')} value={fmtWh(eld)} tone="load" />
          <Tile label={t('tile.gridEst')} value={fmtWh(egr)} tone="grid" />
          <Tile label={t('tile.battIn')} value={fmtWh(d?.ech ?? totals.ech)} tone="batt" />
          <Tile label={t('tile.battOut')} value={fmtWh(d?.edis ?? totals.edis)} tone="batt" />
          <Tile label={t('tile.solarFracEst')} value={frac === null ? '–' : dg(`${Math.round(frac * 100)} %`)} sub={t('tile.solarFracSub')} />
        </div>
        {isNum(d?.epvi) && (
          <p class="muted small">
            {t('today.recon', { inv: fmtWh(d!.epvi), log: fmtWh(epv), pct: recon !== null ? ` (${recon >= 0 ? '+' : ''}${fmtNum(recon, 1)} %)` : '' })}
          </p>
        )}
        <p class="muted small">{t('today.coarse')}</p>
      </Card>

      <MoneyCard title={t('money.titleToday')} date={date} m={money} />
    </div>
  );
}
