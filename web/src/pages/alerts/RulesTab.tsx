// Alerts > Rules (r8 §6.3): sync banner, mailer pill, rule list with on/off switches, live values,
// engine state badges; opens the preset picker and the rule editor as full-screen sheets.
// Extra listeners while visible: live, status, cfg, daily/<today>, alertState (≈ 1.5 KB); month
// rules add one daily/<cycle start..yesterday> range read.
import { useEffect, useState } from 'preact/hooks';
import { useApp, useReportError } from '../../context';
import { replaceHash, useAsync, useNow, useSubscription } from '../../lib/hooks';
import { addDays, dateKey, fmtDay, fmtTime } from '../../lib/time';
import { isNum } from '../../lib/format';
import { localizeDigits as dg, t, tj } from '../../lib/i18n';
import {
  OP_WORD,
  presets,
  BLANK_RULE,
  RULE_IDS,
  STATE_BADGE,
  channelsText,
  conditionText,
  currentValue,
  cycleStart,
  fmtHhmm,
  fmtSecs,
  fmtValue,
  isLatched,
  mailerHealth,
  metricDef,
  metricLabel,
  parseRecipients,
  rcLabel,
  scopeWord,
  sumDaily,
  withEmailDefaults,
  writeErrorText,
  type Preset,
  type ValueSources,
} from '../../data/alerts';
import type { AlertRule, AlertStateRec, Cfg, Daily, Live, Status } from '../../data/types';
import { Badge, Banner, Card, Empty, Sheet, Spinner, Switch } from '../../components/ui';
import type { AlertsData } from '../Alerts';
import { RuleEditor } from './RuleEditor';

const STALE_SEC = 60;

export function RulesTab({ data, sub }: { data: AlertsData; sub?: string }) {
  const { source, visible } = useApp();
  const now = useNow(1000);
  const today = dateKey(now);
  const live = useSubscription<Live | null>((cb, err) => source.onLive(cb, err), [source], visible);
  const status = useSubscription<Status | null>((cb, err) => source.onStatus(cb, err), [source], visible);
  const cfg = useSubscription<Partial<Cfg> | null>((cb, err) => source.onCfg(cb, err), [source], visible);
  const daily = useSubscription<Daily | null>((cb, err) => source.onDaily(today, cb, err), [source, today], visible);
  const states = useSubscription<Record<string, AlertStateRec> | null>((cb, err) => source.onAlertState(cb, err), [source], visible);
  useReportError('alertState', states.error);
  useReportError('live', live.error);

  const alerts = data.alerts;
  const rules = alerts?.rules ?? {};
  const email = withEmailDefaults(alerts?.email);
  const recipients = parseRecipients(email.to);
  const [seed, setSeed] = useState<Preset | null>(null);
  const [notice, setNotice] = useState<{ tone: 'ok' | 'bad'; text: string } | null>(null);
  const [busy, setBusy] = useState<string | null>(null);

  // Month-to-date sums (only when a month rule exists or is being edited).
  const editing = sub && sub !== 'new' ? rules[sub] : seed?.rule;
  const needMonth = Object.values(rules).some((r) => r.scope === 'month') || editing?.scope === 'month' || sub === 'new';
  const cs = cycleStart(today, email.monthStart);
  const month = useAsync(() => source.getDailyRange(cs, addDays(today, -1)), [source, cs, today], needMonth && visible);
  const values: ValueSources = { live: live.value, today: daily.value, cycle: month.data ? sumDaily(month.data, today) : cs === today ? {} : null };

  useEffect(() => {
    if (sub !== 'new') setSeed(null);
  }, [sub]);
  // A deleted / unknown slot in the URL just returns to the list.
  useEffect(() => {
    if (alerts !== undefined && sub && sub !== 'new' && !rules[sub]) replaceHash('#/alerts/rules');
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [alerts, sub]);

  const close = () => replaceHash('#/alerts/rules');
  const count = Object.keys(rules).length;

  const toggle = async (rid: string, on: boolean) => {
    setBusy(rid);
    setNotice(null);
    try {
      await source.setRuleEnabled(rid, on);
    } catch (e) {
      setNotice({ tone: 'bad', text: writeErrorText(e) });
    } finally {
      setBusy(null);
    }
  };

  if (alerts === undefined) return <Spinner />;

  return (
    <>
      <SyncBanner live={live.value} status={status.value} rev={cfg.value?.alertsRev} hasRules={count > 0} now={now} />
      {!data.connected && <Banner tone="warn">{t('common.offlineNoSave')}</Banner>}
      {notice && <Banner tone={notice.tone}>{notice.text}</Banner>}

      <Card title={t('rt.title', { n: count })} right={<MailerPill data={data} now={now} maxPerDay={email.maxPerDay} />}>
        {!email.on && count > 0 && (
          <p class="small warn">{tj('rt.emailOff', { link: <a href="#/alerts/email">{t('common.email')}</a> })}</p>
        )}
        {count === 0 ? (
          <Empty>{t('rt.empty')}</Empty>
        ) : (
          <ul class="rules">
            {RULE_IDS.filter((id) => rules[id]).map((id) => (
              <RuleRow key={id} rid={id} rule={rules[id]} st={states.value?.[id]} values={values} now={now} recipients={recipients} busy={busy === id || !data.connected} onToggle={toggle} />
            ))}
          </ul>
        )}
        <div class="row-end">
          <button type="button" class="btn" disabled={count >= 16} onClick={() => (location.hash = '#/alerts/rules/new')}>
            {t('rt.new')}
          </button>
        </div>
        <p class="muted small">{tj('rt.note', { link: <a href="#/alerts/email">{t('common.email')}</a> })}</p>
      </Card>

      {sub === 'new' && !seed && <PresetPicker monthStart={email.monthStart} onPick={setSeed} onClose={close} />}
      {sub === 'new' && seed && (
        <RuleEditor
          key={`new-${seed.id}`}
          rid={null}
          initial={seed.rule}
          hint={seed.hint}
          data={data}
          values={values}
          acat={status.value?.acat}
          onClose={close}
          onSaved={(slot, name) => setNotice({ tone: 'ok', text: t('rt.savedNew', { name, slot }) })}
        />
      )}
      {sub && sub !== 'new' && rules[sub] && (
        <RuleEditor
          key={sub}
          rid={sub}
          initial={rules[sub]}
          data={data}
          values={values}
          acat={status.value?.acat}
          st={states.value?.[sub]}
          onClose={close}
          onSaved={(slot, name) => setNotice({ tone: 'ok', text: t('rt.saved', { name, slot }) })}
          onDeleted={(name) => setNotice({ tone: 'ok', text: t('rt.deleted', { name }) })}
        />
      )}
    </>
  );
}

function SyncBanner(props: { live: Live | null | undefined; status: Status | null | undefined; rev: number | undefined; hasRules: boolean; now: number }) {
  const { live, status, rev, now } = props;
  if (!props.hasRules && rev === undefined) return null;
  const liveAge = live?.ts ? now - live.ts : Infinity;
  if (live !== undefined && liveAge > STALE_SEC)
    return (
      <Banner tone="warn">{t('sync.applyLater', { what: live?.ts ? t('sync.offlineSince', { t: fmtTime(live.ts) }) : t('sync.never') })}</Banner>
    );
  if (status && status.ruleErr) return <Banner tone="bad">{t('sync.ruleErr', { err: status.ruleErr })}</Banner>;
  if (status && status.arv === undefined && status.acat === undefined)
    return <Banner tone="info">{t('sync.noFw')}</Banner>;
  if (rev !== undefined && status?.arv === rev) return <Banner tone="ok">{t('sync.ok', { t: fmtTime(Math.floor(rev / 1000)) })}</Banner>;
  if (status === undefined) return null;
  return <Banner tone="info">{t('sync.waiting')}</Banner>;
}

function MailerPill({ data, now, maxPerDay }: { data: AlertsData; now: number; maxPerDay: number }) {
  const m = data.mailer;
  const h = mailerHealth(m?.hb, now);
  const sent = m?.day === dateKey(now) ? (m?.sent ?? 0) : 0;
  return (
    <a href="#/alerts/email" class="pill-link">
      {h === 'ok' ? (
        <Badge tone="ok" plain>
          {t('mp.ok', { sent, max: maxPerDay })}
        </Badge>
      ) : h === 'stale' ? (
        <Badge tone="bad" plain>
          {t('mp.stale')}
        </Badge>
      ) : (
        <Badge tone="muted" plain>
          {t('mp.none')}
        </Badge>
      )}
    </a>
  );
}

function mmss(sec: number): string {
  const s = Math.max(0, Math.round(sec));
  if (s >= 3600) return dg(`${Math.floor(s / 3600)}:${String(Math.floor((s % 3600) / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`);
  return dg(`${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`);
}

function RuleRow(props: { rid: string; rule: AlertRule; st: AlertStateRec | undefined; values: ValueSources; now: number; recipients: string[]; busy: boolean; onToggle: (rid: string, on: boolean) => void }) {
  const { rid, rule: r, st, now } = props;
  const m = metricDef(r.metric);
  const v = m ? currentValue(r.metric, r.scope, props.values) : null;
  const state = r.on ? st?.st : 'off';
  const badge = state ? STATE_BADGE[state] : null;
  let detail = '';
  if (state === 'pend' && isNum(st?.since) && r.forSec) detail = t('rr.pend', { op: OP_WORD[r.op], a: mmss(now - st!.since!), b: mmss(r.forSec) });
  else if (state === 'fire' && isNum(st?.since)) detail = t('common.since', { t: fmtTime(st!.since!) });
  else if (state === 'latch' && isNum(st?.since)) detail = t('rr.fired', { t: fmtTime(st!.since!) });
  else if (r.mode === 'at') detail = state === 'done' ? t('rr.checked', { t: fmtHhmm(r.at) }) : t('rr.checks', { t: fmtHhmm(r.at) });
  else if (state === 'inv') detail = st?.err ?? t('rr.notUnderstood');
  const pct = isLatched(r) && (r.op === 'gt' || r.op === 'ge') && isNum(v) && r.value > 0 ? Math.min(100, Math.round((v / r.value) * 100)) : null;
  return (
    <li class={`rule ${r.on ? '' : 'rule-off'}`}>
      <a class="rule-main" href={`#/alerts/rules/${rid}`} aria-label={t('rr.edit', { name: r.name })}>
        <div class="rule-h">
          <strong class="rule-n">{r.name}</strong>
        </div>
        <div class="rule-v">
          <span>
            {metricLabel(r.metric)}
            {scopeWord(r.scope)} <b>{m ? fmtValue(r.metric, v) : '–'}</b>
            {detail && <span class="muted"> · {detail}</span>}
          </span>
          {badge ? <Badge tone={badge.tone}>{badge.label}</Badge> : <Badge tone="muted">{t('rr.notLoaded')}</Badge>}
        </div>
        {pct !== null && (
          <div class="meter" role="meter" aria-valuemin={0} aria-valuemax={100} aria-valuenow={pct} aria-label={t('rr.meter', { pct })}>
            <span style={{ width: `${pct}%` }} class={pct >= 100 ? 'full' : pct >= 80 ? 'near' : ''} />
            <em>{dg(`${pct} %`)}</em>
          </div>
        )}
        <div class="rule-f muted small">
          {conditionText(r)} · {channelsText(r)}
          {r.email && isNum(r.rc) ? t('rr.to', { who: rcLabel(r.rc, props.recipients) }) : ''}
        </div>
      </a>
      <Switch checked={r.on} disabled={props.busy} label={`${r.name}: ${r.on ? t('common.on') : t('common.off')}`} onChange={(on) => props.onToggle(rid, on)} />
    </li>
  );
}

function PresetPicker(props: { monthStart: number; onPick: (p: Preset) => void; onClose: () => void }) {
  return (
    <Sheet title={t('pp.title')} onClose={props.onClose}>
      <p class="muted small">{t('pp.lead')}</p>
      <ul class="presets">
        {presets().map((p) => (
          <li key={p.id}>
            <button type="button" class="preset" onClick={() => props.onPick(p)}>
              <strong>{p.rule.name}</strong>
              <span class="muted small">
                {conditionText(p.rule)} · {p.rule.scope === 'month' ? t('pp.billing', { n: props.monthStart }) : channelsText({ ...p.rule, upd: 0 })}
              </span>
            </button>
          </li>
        ))}
        <li>
          <button type="button" class="preset" onClick={() => props.onPick({ id: 'custom', rule: BLANK_RULE })}>
            <strong>{t('pp.custom')}</strong>
            <span class="muted small">{t('pp.customSub', { hold: fmtSecs(BLANK_RULE.forSec) })}</span>
          </button>
        </li>
      </ul>
      <p class="muted small">{t('pp.note', { day: fmtDay(dateKey()) })}</p>
    </Sheet>
  );
}
