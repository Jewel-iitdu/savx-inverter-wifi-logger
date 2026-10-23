// History day view: overlay the load and PV of another day on `date` (same clock).
import { useMemo, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useAsync } from '../lib/hooks';
import { keyToEpoch, todayKey } from '../lib/time';
import { t } from '../lib/i18n';
import { daySeries } from '../lib/series';
import { usePowerView, viewRecs } from '../lib/power';
import { getHistDay } from '../data/history';
import type { HistRec } from '../data/types';
import { UChart } from './UChart';
import { Card, Spinner } from './ui';

/** `recs`: the day's minute records, already in the power view (viewRecs). */
export function CompareCard({ date, recs }: { date: string; recs: Record<string, HistRec> }) {
  const { source, theme } = useApp();
  const pv = usePowerView();
  const pu = pv.unit;
  const [compare, setCompare] = useState<string>('');
  const other = useAsync(() => getHistDay(source, compare), [source, compare], !!compare);
  useReportError('history', other.error);
  const otherRecs = useMemo(() => (other.data ? viewRecs(other.data, pv) : null), [other.data, pv.k, pv.meter, pv.unit]);
  const t0 = keyToEpoch(date, '0000');
  const overlay = useMemo(() => {
    if (!compare || !otherRecs) return null;
    const a = daySeries(date, recs, 1, 1439);
    const b = daySeries(date, otherRecs, 1, 1439); // mapped onto the same clock
    return [a.xs, a.load, b.load, a.pv, b.pv];
  }, [compare, otherRecs, recs, date]);
  return (
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
  );
}
