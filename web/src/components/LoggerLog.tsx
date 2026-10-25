// Settings > Logger log (fw 1.0.2, owner request 2026-10-25): one day of the logger's own entries, newest first,
// LOG_PAGE per page. Opens on today; ◀ ▶ step through the days. The entries are events/<date>: LOGGER_OFFLINE (why the
// logger was offline, fw 1.0.2), LOGGER_REBOOT and the cloud warnings; "All" also shows the inverter alerts. They are
// stored per day and kept for cfg.keepEventDays (the logger prunes older days after 00:05).
// Reads: get(events/<date>) once per day shown; today again on every visit and on refresh.
import { useEffect, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useStored } from '../lib/hooks';
import { addDays, fmtDateKey, fmtDuration, fmtTime, todayKey } from '../lib/time';
import { eventLabel } from '../lib/decode';
import { t } from '../lib/i18n';
import type { EventItem, EventRec } from '../data/types';
import { Card, Empty, Segmented, Spinner } from './ui';

export const LOGGER_TYPES = new Set(['LOGGER_OFFLINE', 'LOGGER_REBOOT', 'QUOTA_GUARD', 'DATA_DROPPED', 'BACKLOG_STUCK']);
export const LOG_PAGE = 20;
type Which = 'logger' | 'all';

export function LoggerLog({ keepDays }: { keepDays: number }) {
  const { source } = useApp();
  const today = todayKey();
  const [date, setDate] = useState(today);
  const [page, setPage] = useState(0);
  const [which, setWhich] = useStored<Which>('log.which', 'logger');
  const [byDate, setByDate] = useState<Record<string, Record<string, EventRec>>>({});
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  const [reload, setReload] = useState(0);
  useReportError('log', error);

  useEffect(() => {
    if (date in byDate && date !== today && !reload) return;
    let alive = true;
    setLoading(true);
    source
      .getEvents(date)
      .then((v) => alive && setByDate((prev) => ({ ...prev, [date]: v ?? {} })))
      .catch((e: unknown) => alive && setError(e instanceof Error ? e : new Error(String(e))))
      .finally(() => alive && setLoading(false));
    return () => {
      alive = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [source, date, reload]);

  const items: EventItem[] = Object.entries(byDate[date] ?? {})
    .map(([id, e]) => ({ ...e, id, date }))
    .filter((e) => which === 'all' || LOGGER_TYPES.has(e.type))
    .sort((a, b) => b.ts - a.ts);
  const pages = Math.max(1, Math.ceil(items.length / LOG_PAGE));
  const pg = Math.min(page, pages - 1);
  const shown = items.slice(pg * LOG_PAGE, (pg + 1) * LOG_PAGE);
  const oldest = addDays(today, -Math.max(0, keepDays - 1));
  const go = (d: string) => {
    setDate(d);
    setPage(0);
  };

  return (
    <Card
      title={t('log.title')}
      right={<Segmented label={t('log.show')} value={which} options={[['logger', t('log.logger')], ['all', t('common.all')]]} onChange={(w) => { setWhich(w); setPage(0); }} />}
    >
      <div class="log-nav">
        <button type="button" class="btn-link" disabled={date <= oldest} onClick={() => go(addDays(date, -1))} aria-label={t('log.prevDay')}>
          ◀
        </button>
        <span class="log-day">{date === today ? t('log.today') : fmtDateKey(date)}</span>
        <button type="button" class="btn-link" disabled={date >= today} onClick={() => go(addDays(date, 1))} aria-label={t('log.nextDay')}>
          ▶
        </button>
        <button type="button" class="btn-link log-refresh" disabled={loading} onClick={() => setReload((n) => n + 1)}>
          {loading ? t('common.loading') : t('log.refresh')}
        </button>
      </div>
      {loading && !(date in byDate) ? (
        <Spinner />
      ) : shown.length === 0 ? (
        <Empty>{which === 'logger' ? t('log.emptyLogger') : t('log.empty')}</Empty>
      ) : (
        <ul class="log-list">
          {shown.map((e) => (
            <li key={e.id} class={`log-row sev-${e.sev}`}>
              <div class="log-h">
                <span class="log-t">{fmtTime(e.ts)}</span>
                <strong>{eventLabel(e.type)}</strong>
                {typeof e.dur === 'number' && e.dur > 0 && <span class="muted small">{fmtDuration(e.dur)}</span>}
              </div>
              <div class="log-m">{e.msg}</div>
            </li>
          ))}
        </ul>
      )}
      {pages > 1 && (
        <div class="log-pages">
          <button type="button" class="btn-link" disabled={pg === 0} onClick={() => setPage(pg - 1)}>
            {t('log.newer')}
          </button>
          <span class="muted small">{t('log.page', { p: pg + 1, n: pages })}</span>
          <button type="button" class="btn-link" disabled={pg >= pages - 1} onClick={() => setPage(pg + 1)}>
            {t('log.older')}
          </button>
        </div>
      )}
      <p class="muted small">{t('log.keep', { n: keepDays })}</p>
    </Card>
  );
}
