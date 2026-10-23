// Electricity money for the Today and History pages (tariff.ts): the billing-month start from alerts/email and
// the daily/ totals of each billing month, read back to its first day so the slab position is right.
import { useMemo } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { cycleStart, EMAIL_DEFAULTS } from '../data/alerts';
import { getDailySpan } from '../data/history';
import { todayKey } from './time';
import type { Daily } from '../data/types';
import { useAsync, useSubscription } from './hooks';
import { useBillView, viewDailyMap } from './power';
import { dayMoney, type Money } from './tariff';

/** First day of the billing month (1–28, alerts/email/monthStart); undefined while loading. */
export function useMonthStart(): number | undefined {
  const { source, visible } = useApp();
  const ms = useSubscription<number | null>((cb, err) => source.onMonthStart(cb, err), [source], visible);
  if (ms.value === undefined) return ms.error ? EMAIL_DEFAULTS.monthStart : undefined;
  return ms.value ?? EMAIL_DEFAULTS.monthStart;
}

export interface DayMoney {
  /** Per-day money from the billing-month start of `from` to `to`; null while loading. */
  money: Record<string, Money> | null;
  monthStart: number | undefined;
  /** First day of the billing month that contains `d`. */
  cycleOf: (d: string) => string;
}

/**
 * Money per day for [from, to] and the days before `from` in its billing month. `override` replaces daily/ for
 * some dates (today's live totals). Energies use the bill view (kWh, see useBillView), whatever unit is shown.
 */
export function useDayMoney(from: string, to: string, override?: Record<string, Daily | null | undefined>): DayMoney {
  const { source } = useApp();
  const bv = useBillView();
  const monthStart = useMonthStart();
  const cs = monthStart ? cycleStart(from, monthStart) : '';
  const end = to < todayKey() ? to : todayKey();
  const q = useAsync(() => getDailySpan(source, cs, end), [source, cs, end], !!cs);
  useReportError('money', q.error);
  const ovKey = JSON.stringify(override ?? null);
  const money = useMemo(() => {
    if (!monthStart || !q.data) return null;
    const days: Record<string, Daily> = { ...q.data };
    for (const [d, v] of Object.entries(override ?? {})) if (v) days[d] = v;
    return dayMoney(viewDailyMap(days, bv) ?? {}, (d) => cycleStart(d, monthStart));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [q.data, monthStart, bv.k, ovKey]);
  return { money, monthStart, cycleOf: (d) => cycleStart(d, monthStart ?? EMAIL_DEFAULTS.monthStart) };
}
