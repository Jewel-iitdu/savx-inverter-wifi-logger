// Alerts > History (r8 §6.5; formerly the Events page): open alerts first, the last 7 days
// (+ "load 7 more"), filters All / Rules / Built-in / E-mail plus minimum severity, owner ack
// (events/<date>/<key>/ack = true), value vs threshold and e-mail status chips, code legends.
// Reads: get(events/<date>) once per day shown (date-bounded, d1 §6.4).
import { useEffect, useMemo, useState } from 'preact/hooks';
import { useApp, useReportError } from '../../context';
import { useStored } from '../../lib/hooks';
import { addDays, fmtDateTime, fmtDuration, fmtTime, todayKey } from '../../lib/time';
import { eventLabel, pickVariant, severityName } from '../../lib/decode';
import { isBn, localizeDigits as dg, t, tj } from '../../lib/i18n';
import { EVENT_TYPES, LCD_FAULT, LCD_WARNING, QPIWS_VARIANTS, REVO_FAULT, REVO_WARNING, VOLTRONIC_FAULT_EXTRA } from '../../codes';
import { EM_CHIP, fmtValue } from '../../data/alerts';
import type { AlertRule, EventItem, EventRec } from '../../data/types';
import { Badge, Card, Empty, Segmented, Spinner } from '../../components/ui';
import type { AlertsData } from '../Alerts';

const sevTone = (sev: number) => (sev >= 5 ? 'bad' : sev >= 4 ? 'warn' : sev >= 3 ? 'info' : 'muted');
type Kind = 'all' | 'rules' | 'builtin' | 'email';
const isRuleEvent = (e: EventRec) => !!e.rid || e.type === 'RULE' || e.type === 'RULE_INVALID';

export function HistoryTab({ data }: { data: AlertsData }) {
  const { source, meta } = useApp();
  const [days, setDays] = useState(7);
  const [byDate, setByDate] = useState<Record<string, Record<string, EventRec>>>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<Error | null>(null);
  const [minSev, setMinSev] = useStored<'0' | '2' | '3' | '4'>('events.minSev', '0');
  const [kind, setKind] = useStored<Kind>('events.kind', 'all');
  const [acking, setAcking] = useState<string | null>(null);
  const rules = data.alerts?.rules ?? {};
  useReportError('events', error);

  useEffect(() => {
    let alive = true;
    const today = todayKey();
    const want = Array.from({ length: days }, (_, i) => addDays(today, -i)).filter((d) => !(d in byDate) || d === today);
    setLoading(true);
    Promise.all(want.map((d) => source.getEvents(d).then((v) => [d, v] as const)))
      .then((pairs) => {
        if (!alive) return;
        setByDate((prev) => {
          const next = { ...prev };
          for (const [d, v] of pairs) next[d] = v;
          return next;
        });
      })
      .catch((e: unknown) => alive && setError(e instanceof Error ? e : new Error(String(e))))
      .finally(() => alive && setLoading(false));
    return () => {
      alive = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [source, days]);

  const all: EventItem[] = useMemo(() => {
    const out: EventItem[] = [];
    for (const [date, evs] of Object.entries(byDate)) for (const [id, e] of Object.entries(evs ?? {})) out.push({ ...e, id, date });
    return out.sort((a, b) => b.ts - a.ts);
  }, [byDate]);

  // Open alert = the newest open/clear event of its alert (within the loaded range) is 'open'.
  // A paired type (GRID_RESTORED clears GRID_LOSS, see shared/codes.json) counts as a clear of the
  // type it names; rule events are tracked per rule slot.
  const open = useMemo(() => {
    const latest = new Map<string, EventItem>();
    for (const e of all) {
      const clears = EVENT_TYPES[e.type]?.clears;
      const key = clears ?? (e.rid ? `RULE:${e.rid}` : e.type);
      const st = clears ? 'clear' : e.st;
      if ((st === 'open' || st === 'clear') && !latest.has(key)) latest.set(key, { ...e, st });
    }
    return [...latest.values()].filter((e) => e.st === 'open').sort((a, b) => b.sev - a.sev || b.ts - a.ts);
  }, [all]);
  const openIds = new Set(open.map((e) => e.id));
  const shown = all.filter(
    (e) =>
      e.sev >= Number(minSev) &&
      !openIds.has(e.id) &&
      (kind === 'all' || (kind === 'rules' ? isRuleEvent(e) : kind === 'builtin' ? !isRuleEvent(e) : !!e.em)),
  );

  const ack = async (e: EventItem) => {
    setAcking(e.id);
    try {
      await source.ackEvent(e.date, e.id);
      setByDate((prev) => ({ ...prev, [e.date]: { ...prev[e.date], [e.id]: { ...prev[e.date][e.id], ack: true } } }));
    } catch (err) {
      setError(err instanceof Error ? err : new Error(String(err)));
    } finally {
      setAcking(null);
    }
  };

  return (
    <>
      <Card title={t('ht.open')} class={open.some((e) => e.sev >= 5) ? 'card-bad' : open.length ? 'card-warn' : ''}>
        {loading && !all.length ? (
          <Spinner />
        ) : open.length === 0 ? (
          <Empty>{t('ht.noOpen', { n: days })}</Empty>
        ) : (
          <EventList items={open} rules={rules} onAck={ack} acking={acking} canAck />
        )}
      </Card>
      <Card
        title={t('al.history')}
        right={
          <Segmented
            label={t('ht.kind')}
            value={kind}
            options={[
              ['all', t('common.all')],
              ['rules', t('al.rules')],
              ['builtin', t('ht.builtin')],
              ['email', t('common.email')],
            ]}
            onChange={setKind}
          />
        }
      >
        <div class="row-start">
          <span class="muted small">{t('ht.minSev')}</span>
          <Segmented
            label={t('ht.minSev')}
            value={minSev}
            options={[
              ['0', t('common.all')],
              ['2', dg('≥2')],
              ['3', dg('≥3')],
              ['4', dg('≥4')],
            ]}
            onChange={setMinSev}
          />
        </div>
        {shown.length === 0 && !loading ? <Empty>{t('ht.noEvents')}</Empty> : <EventList items={shown} rules={rules} onAck={ack} acking={acking} />}
        <div class="row-end">
          {loading && <span class="muted small">{t('common.loading')}</span>}
          <button type="button" class="btn-link" disabled={loading} onClick={() => setDays(days + 7)}>
            {t('ht.more')}
          </button>
        </div>
      </Card>
      <Legend dia={meta?.dia} />
    </>
  );
}

function ValueVsLimit({ e, rule }: { e: EventItem; rule: AlertRule | undefined }) {
  if (typeof e.val !== 'number' && typeof e.thr !== 'number') return null;
  const f = (v: number | undefined) => (typeof v !== 'number' ? '–' : rule ? fmtValue(rule.metric, v, { est: false }) : dg(String(v)));
  return (
    <span class="ev-vt">
      {f(e.val)} <span class="muted">{t('ht.vsLimit')}</span> {f(e.thr)}
    </span>
  );
}

function EmChip({ e }: { e: EventItem }) {
  if (!e.em) return null;
  const c = EM_CHIP[e.em] ?? { label: e.em, tone: 'muted' as const };
  const text = e.em === 'sent' && typeof e.emt === 'number' ? t('ht.emailed', { t: fmtTime(e.emt) }) : c.label;
  return (
    <Badge tone={c.tone} plain>
      ✉ {text}
    </Badge>
  );
}

function EventList(props: { items: EventItem[]; rules: Record<string, AlertRule>; onAck: (e: EventItem) => void; acking: string | null; canAck?: boolean }) {
  return (
    <ul class="events">
      {props.items.map((e) => {
        const rule = e.rid ? props.rules[e.rid] : undefined;
        return (
          <li key={e.date + e.id} class={`ev sev-${e.sev}`}>
            <div class="ev-h">
              <Badge tone={sevTone(e.sev)}>{severityName(e.sev)}</Badge>
              {e.rid && rule ? (
                <a class="ev-rule" href={`#/alerts/rules/${e.rid}`} title={t('ht.openRule')}>
                  <strong>{rule.name}</strong>
                </a>
              ) : (
                <strong>{eventLabel(e.type)}</strong>
              )}
              {e.st && e.st !== 'info' && <span class={`ev-st st-${e.st}`}>{e.st === 'open' ? t('ht.stOpen') : e.st === 'clear' ? t('ht.stClear') : e.st}</span>}
              <span class="muted small ev-t">{fmtDateTime(e.ts)}</span>
            </div>
            <div class="ev-m">{e.msg}</div>
            <div class="ev-f muted small">
              <EmChip e={e} />
              <ValueVsLimit e={e} rule={rule} />
              <span>
                {e.rid ? `${e.type} ${e.rid}` : e.type}
                {typeof e.dur === 'number' ? t('ht.lasted', { d: fmtDuration(e.dur) }) : ''}
                {e.ch ? t('ht.via', { ch: isBn() ? e.ch.split(',').map((c) => (c === 'email' ? t('common.email') : c)).join(', ') : e.ch }) : ''}
                {e.ack ? t('ht.acked') : ''}
              </span>
              {props.canAck && !e.ack && e.sev >= 3 && (
                <button type="button" class="btn-link" disabled={props.acking === e.id} onClick={() => props.onAck(e)}>
                  {props.acking === e.id ? t('common.saving') : t('ht.ack')}
                </button>
              )}
            </div>
          </li>
        );
      })}
    </ul>
  );
}

function CodeTable(props: { rows: Array<[string, string]> }) {
  return (
    <table class="tbl codes">
      <tbody>
        {props.rows.map(([c, t]) => (
          <tr key={c}>
            <td>{c}</td>
            <td>{t}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

function Legend({ dia }: { dia?: string }) {
  const variant = pickVariant('0'.repeat(dia === 'MAX24' || dia === 'GK' ? 36 : 32), dia) ?? QPIWS_VARIANTS[0];
  return (
    <Card title={t('ht.legend')}>
      <p class="muted small">{tj('ht.legendNote', { different: <strong>{t('ht.different')}</strong>, dia: dia ? t('ht.dia', { d: dia }) : '' })}</p>
      <details>
        <summary>{t('ht.emChips')}</summary>
        <CodeTable rows={Object.entries(EM_CHIP).map(([k, v]) => [k, v.label])} />
        <p class="muted small">
          {tj('ht.emChipsNote', {
            q: <code>q</code>,
            cool: <code>cool</code>,
            rcap: <code>rcap</code>,
            cap: <code>cap</code>,
            cap2: <code>cap</code>,
            quota: <code>quota</code>,
            norc: <code>norc</code>,
            exp: <code>exp</code>,
          })}
        </p>
      </details>
      <details>
        <summary>{t('ht.lcdFault')}</summary>
        <CodeTable rows={Object.entries(LCD_FAULT)} />
        <p class="muted small">{t('ht.voltronicExtra')}</p>
        <CodeTable rows={Object.entries(VOLTRONIC_FAULT_EXTRA)} />
      </details>
      <details>
        <summary>{t('ht.lcdWarn')}</summary>
        <CodeTable rows={Object.entries(LCD_WARNING)} />
      </details>
      <details>
        <summary>{t('ht.qpiws', { v: variant.label })}</summary>
        <CodeTable
          rows={variant.bits
            .filter((b) => b[1] !== 'R')
            .map((b) => [`a${b[0]}`, `${b[2]} (${b[1] === 'F' ? t('ht.kindFault') : b[1] === 'FA1' ? t('ht.kindFaultA1') : b[1] === 'FC' ? t('ht.kindFc') : t('ht.kindWarning')})`])}
        />
      </details>
      <details>
        <summary>{t('ht.revo')}</summary>
        <p class="muted small">{t('ht.warnings')}</p>
        <CodeTable rows={Object.entries(REVO_WARNING).map(([c, e]) => [c, e.text])} />
        <p class="muted small">{t('ht.faults')}</p>
        <CodeTable rows={Object.entries(REVO_FAULT).map(([c, e]) => [c, e.text])} />
      </details>
    </Card>
  );
}
