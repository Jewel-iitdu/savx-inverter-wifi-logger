// Rule editor sheet (r8 §6.3 "Editor"). Works on a string-based draft, converts it to the stored
// rule (canonical units; kWh typed, Wh stored), and runs validateRule() — the exact mirror of the
// database rules — before Save is enabled.
import { useMemo, useState } from 'preact/hooks';
import { useApp } from '../../context';
import { fmtDayMonth, nowSec, todayKey, weekdayShort } from '../../lib/time';
import { localizeDigits as dg, t, tj } from '../../lib/i18n';
import { METRIC_BY_ID, METRIC_GROUPS, METRICS, type MetricId, type RuleScope } from '../../alertMetrics';
import { severityName } from '../../lib/decode';
import {
  OP_SYMBOL,
  currentValue,
  cycleStart,
  defaultHysteresis,
  fmtValue,
  inputUnit,
  isBool,
  isEnergy,
  metricGroupLabel,
  metricLabel,
  parseInputValue,
  parseRecipients,
  previewLines,
  previewSubject,
  toInputValue,
  validateRule,
  withEmailDefaults,
  writeErrorText,
  type ValueSources,
} from '../../data/alerts';
import type { AlertRule, AlertStateRec, RuleMode, RuleOp } from '../../data/types';
import { Banner, Chip, Segmented, Sheet, ToggleRow } from '../../components/ui';
import { TestMailButton, type AlertsData } from '../Alerts';

interface Draft {
  name: string;
  on: boolean;
  metric: MetricId;
  scope: RuleScope;
  op: RuleOp;
  value: string; // display units
  mode: RuleMode;
  at: string; // 'HH:MM'
  forSec: number;
  clrAuto: boolean;
  clr: string;
  clrSec: number;
  coolMin: number;
  repeatMin: number;
  maxDay: number;
  minCov: number;
  allDay: boolean;
  from: string; // 'HH:MM'
  to: string;
  days: number;
  sev: 1 | 2 | 3 | 4 | 5;
  email: boolean;
  ntfy: boolean;
  rcAll: boolean;
  rc: number;
  clrMail: boolean;
}

const hm = (hhmm: string | undefined, dflt: string) => (hhmm && hhmm.length === 4 ? `${hhmm.slice(0, 2)}:${hhmm.slice(2)}` : dflt);
const unhm = (t: string) => t.replace(':', '').slice(0, 4);

function toDraft(r: Omit<AlertRule, 'upd'>): Draft {
  return {
    name: r.name,
    on: r.on,
    metric: r.metric,
    scope: r.scope,
    op: r.op,
    value: toInputValue(r.metric, r.value),
    mode: r.mode,
    at: hm(r.at, '17:00'),
    forSec: r.forSec ?? (isEnergy(r.metric) ? 0 : 60),
    clrAuto: r.clr === undefined,
    clr: r.clr === undefined ? '' : toInputValue(r.metric, r.clr),
    clrSec: r.clrSec ?? 60,
    coolMin: r.coolMin ?? 60,
    repeatMin: r.repeatMin ?? 0,
    maxDay: r.maxDay ?? 3,
    minCov: r.minCov ?? 80,
    allDay: !r.win,
    from: hm(r.win?.from, '18:00'),
    to: hm(r.win?.to, '23:00'),
    days: r.win?.days ?? 127,
    sev: r.sev,
    email: r.email,
    ntfy: r.ntfy,
    rcAll: r.rc === undefined,
    rc: r.rc ?? 1,
    clrMail: r.clrMail ?? false,
  };
}

/** Sensible starting threshold and comparator when the metric changes. */
const METRIC_START: Partial<Record<MetricId, [RuleOp, number]>> = {
  w: ['gt', 2000], pvw: ['lt', 200], gw: ['gt', 1000], bdw: ['gt', 1500], bw: ['lt', -1500], soc: ['lt', 30], ld: ['gt', 80],
  bv: ['lt', 24], gv: ['gt', 250], ov: ['lt', 210], pvv: ['gt', 450], bdi: ['gt', 60], tmp: ['gt', 70], gf: ['lt', 49],
  onbat: ['ge', 1], nogrid: ['ge', 1], eld: ['gt', 8000], egr: ['gt', 5000], epv: ['lt', 3000], edis: ['gt', 3000], ech: ['lt', 1000], gout: ['gt', 180],
};

/** Draft -> stored rule (without upd). Only fields the UI shows for this kind of rule are written. */
function fromDraft(d: Draft, recipients: number): { rule: Omit<AlertRule, 'upd'>; errors: string[] } {
  const errors: string[] = [];
  const energy = isEnergy(d.metric);
  const bool = isBool(d.metric);
  const scope: RuleScope = energy ? (d.scope === 'month' ? 'month' : 'day') : 'inst';
  let value = bool ? (d.value === '0' ? 0 : 1) : parseInputValue(d.metric, d.value);
  if (!bool && !Number.isFinite(value)) {
    errors.push(t('re.errValue', { unit: inputUnit(d.metric) }));
    value = 0;
  }
  const op: RuleOp = bool ? (value === 1 ? 'ge' : 'le') : scope === 'month' && (d.op === 'lt' || d.op === 'le') ? 'gt' : d.op;
  const mode: RuleMode = scope === 'month' ? 'hold' : scope === 'day' ? (op === 'lt' || op === 'le' ? 'at' : d.mode) : 'hold';
  const latched = scope !== 'inst';
  const rule: Omit<AlertRule, 'upd'> = { name: d.name.trim(), on: d.on, metric: d.metric, scope, op, value, mode, sev: d.sev, email: d.email, ntfy: d.ntfy };
  if (mode === 'at') rule.at = unhm(d.at);
  if (mode === 'at' && (op === 'lt' || op === 'le')) rule.minCov = d.minCov;
  if (!latched) {
    rule.forSec = d.forSec;
    rule.clrSec = d.clrSec;
    if (!bool && !d.clrAuto) {
      const c = parseInputValue(d.metric, d.clr);
      if (!Number.isFinite(c)) errors.push(t('re.errClr', { unit: inputUnit(d.metric) }));
      else rule.clr = c;
    }
    if (d.email) {
      rule.coolMin = d.coolMin;
      if (d.repeatMin > 0) rule.repeatMin = d.repeatMin;
      if (d.clrMail) rule.clrMail = true;
    }
  }
  rule.maxDay = latched ? 1 : d.maxDay;
  if (!d.allDay) {
    rule.win = { from: unhm(d.from), to: unhm(d.to), days: d.days };
    if (!d.days) errors.push(t('re.errDays'));
  }
  if (d.email && !d.rcAll) {
    const mask = d.rc & ((1 << recipients) - 1);
    if (!mask) errors.push(t('re.errRc'));
    else rule.rc = mask;
  }
  return { rule, errors };
}

/** JSON with sorted keys, to compare two rules. */
function stable(v: unknown): string {
  if (Array.isArray(v)) return `[${v.map(stable).join(',')}]`;
  if (v && typeof v === 'object')
    return `{${Object.keys(v as Record<string, unknown>)
      .filter((k) => (v as Record<string, unknown>)[k] !== undefined)
      .sort()
      .map((k) => `${JSON.stringify(k)}:${stable((v as Record<string, unknown>)[k])}`)
      .join(',')}}`;
  return JSON.stringify(v);
}
const withoutUpd = (r: Omit<AlertRule, 'upd'> & { upd?: unknown }) => {
  const { upd: _u, ...rest } = r;
  return rest;
};

const FOR_SEC = [0, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 28800];
const CLR_SEC = [0, 30, 60, 120, 300, 600, 900, 1800, 3600];
const COOL_MIN = [5, 15, 30, 60, 120, 360, 720, 1440];
const REPEAT_MIN = [0, 30, 60, 120, 360];
const MIN_COV = [0, 50, 80, 90, 100];
const DAYS = [0, 1, 2, 3, 4, 5, 6]; // bit i = weekday i, 0 = Sunday

const secLabel = (s: number) => (s === 0 ? t('common.atOnce') : s < 60 ? t('u.s', { n: s }) : s < 3600 ? t('u.min', { n: s / 60 }) : t('u.h', { n: s / 3600 }));
const minLabel = (m: number) => (m === 0 ? t('common.off') : m < 60 ? t('u.min', { n: m }) : t('u.h', { n: m / 60 }));
const withCurrent = (list: number[], v: number) => (list.includes(v) ? list : [...list, v].sort((a, b) => a - b));

function NumSelect(props: { value: number; options: number[]; label: (v: number) => string; onChange: (v: number) => void; aria: string }) {
  return (
    <select aria-label={props.aria} value={String(props.value)} onChange={(e) => props.onChange(Number(e.currentTarget.value))}>
      {withCurrent(props.options, props.value).map((v) => (
        <option key={v} value={String(v)}>
          {props.label(v)}
        </option>
      ))}
    </select>
  );
}

export function RuleEditor(props: {
  rid: string | null;
  initial: Omit<AlertRule, 'upd'>;
  hint?: string;
  data: AlertsData;
  values: ValueSources;
  acat?: number;
  st?: AlertStateRec;
  onClose: () => void;
  onSaved: (slot: string, name: string) => void;
  onDeleted?: (name: string) => void;
}) {
  const { source } = useApp();
  const { data, rid } = props;
  const email = withEmailDefaults(data.alerts?.email);
  const recipients = parseRecipients(email.to);
  const [d, setD] = useState<Draft>(() => toDraft(props.initial));
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const set = (p: Partial<Draft>) => setD((cur) => ({ ...cur, ...p }));

  const { rule, errors: draftErrors } = useMemo(() => fromDraft(d, recipients.length), [d, recipients.length]);
  const errors = useMemo(() => [...draftErrors, ...validateRule(rid ?? 'r01', { ...rule, upd: {} })], [draftErrors, rule, rid]);
  const energy = isEnergy(d.metric);
  const bool = isBool(d.metric);
  const latched = rule.scope !== 'inst';
  const m = METRIC_BY_ID[d.metric];
  const unit = inputUnit(d.metric);
  const now = currentValue(rule.metric, rule.scope, props.values);
  const parsed = bool ? null : parseInputValue(d.metric, d.value);
  const autoClr = Number.isFinite(parsed) ? (rule.op === 'gt' || rule.op === 'ge' ? (parsed as number) - defaultHysteresis(d.metric, parsed as number) : (parsed as number) + defaultHysteresis(d.metric, parsed as number)) : NaN;
  const cs = cycleStart(todayKey(), email.monthStart);
  const metrics = METRICS.filter((x) => props.acat === undefined || x.since <= props.acat || x.id === d.metric);

  const changeMetric = (id: MetricId) => {
    const [op, v] = METRIC_START[id] ?? ['gt', 0];
    const e = isEnergy(id);
    set({ metric: id, op, value: isBool(id) ? '1' : toInputValue(id, v), scope: e ? (d.scope === 'month' ? 'month' : 'day') : 'inst', mode: e && (op === 'lt' || op === 'le') ? 'at' : 'hold', clrAuto: true, clr: '', forSec: e ? 0 : d.forSec || 60 });
  };

  const save = async () => {
    setBusy(true);
    setErr(null);
    try {
      const slot = await source.saveRule(rid, rule);
      props.onSaved(slot, rule.name);
      props.onClose();
    } catch (e) {
      setErr(writeErrorText(e));
    } finally {
      setBusy(false);
    }
  };
  const del = async () => {
    if (!rid || !confirm(t('re.deleteConfirm', { name: props.initial.name }))) return;
    setBusy(true);
    try {
      await source.deleteRule(rid);
      props.onDeleted?.(props.initial.name);
      props.onClose();
    } catch (e) {
      setErr(writeErrorText(e));
      setBusy(false);
    }
  };

  // An existing rule saves only when something changed (a new one can always be saved).
  const changed = useMemo(() => !rid || stable(rule) !== stable(withoutUpd(props.initial)), [rid, rule, props.initial]);
  const canSave = errors.length === 0 && !busy && data.connected && changed;
  return (
    <Sheet
      title={rid ? t('re.titleEdit', { rid }) : t('pp.title')}
      onClose={props.onClose}
      footer={
        <div class="sheet-actions">
          {errors.length > 0 && <span class="small bad">{errors.length === 1 ? errors[0] : t('re.problems', { n: errors.length })}</span>}
          {!data.connected && <span class="small warn">{t('re.offline')}</span>}
          <button type="button" class="btn btn-wide" disabled={!canSave} onClick={() => void save()}>
            {busy ? t('common.saving') : rid && !changed ? t('re.noChanges') : t('common.save')}
          </button>
        </div>
      }
    >
      <form class="form" onSubmit={(e) => (e.preventDefault(), canSave && void save())}>
        {props.hint && <Banner tone="info">{props.hint}</Banner>}
        <label class="field">
          <span class="field-l">{t('re.name')}</span>
          <input
            type="text"
            maxLength={40}
            value={d.name}
            placeholder={t('re.namePh')}
            autocomplete="off"
            onInput={(e) => set({ name: e.currentTarget.value.replace(/[\r\n\t]+/g, ' ') })}
          />
          <span class="field-h muted">{t('re.nameHelp', { n: d.name.length })}</span>
        </label>
        <ToggleRow label={t('re.enabled')} aria={t('re.enabled')} checked={d.on} onChange={(on) => set({ on })} />

        <fieldset class="fs">
          <legend>{t('re.condition')}</legend>
          <label class="field">
            <span class="field-l">{t('re.metric')}</span>
            <select value={d.metric} onChange={(e) => changeMetric(e.currentTarget.value as MetricId)}>
              {METRIC_GROUPS.map((g) => (
                <optgroup key={g.id} label={metricGroupLabel(g)}>
                  {metrics
                    .filter((x) => x.group === g.id)
                    .map((x) => {
                      const cur = currentValue(x.id, x.scopes[0] === 'inst' ? 'inst' : 'day', props.values);
                      return (
                        <option key={x.id} value={x.id}>
                          {metricLabel(x.id)}
                          {x.est ? t('re.optEst') : ''} · {x.kind === 'bool' ? t('re.yesNo') : inputUnit(x.id)}
                          {cur !== null ? t('re.now', { v: fmtValue(x.id, cur, { est: false }) }) : ''}
                        </option>
                      );
                    })}
                </optgroup>
              ))}
            </select>
            {m.est && <span class="field-h muted">{t('re.estHelp')}</span>}
          </label>

          {energy && (
            <div class="field">
              <span class="field-l">{t('re.period')}</span>
              <Segmented
                label={t('re.period')}
                value={d.scope === 'month' ? 'month' : 'day'}
                options={[
                  ['day', t('nav.today')],
                  ['month', t('re.thisMonth', { d: fmtDayMonth(cs) })],
                ]}
                onChange={(v) => set({ scope: v, ...(v === 'month' ? { mode: 'hold' as RuleMode, op: d.op === 'lt' || d.op === 'le' ? ('gt' as RuleOp) : d.op } : {}) })}
              />
            </div>
          )}

          {bool ? (
            <div class="field">
              <span class="field-l">{t('re.alertWhenBool', { label: metricLabel(d.metric) })}</span>
              <Segmented
                label={t('re.value')}
                value={d.value === '0' ? '0' : '1'}
                options={[
                  ['1', t('common.Yes')],
                  ['0', t('common.No')],
                ]}
                onChange={(v) => set({ value: v })}
              />
            </div>
          ) : (
            <div class="field">
              <span class="field-l">{t('re.alertWhenValue')}</span>
              <div class="cond">
                <select
                  aria-label={t('re.comparator')}
                  value={rule.op}
                  onChange={(e) => {
                    const op = e.currentTarget.value as RuleOp;
                    set({ op, ...(rule.scope === 'day' ? { mode: op === 'lt' || op === 'le' ? 'at' : 'hold' } : {}) });
                  }}
                >
                  {(['gt', 'ge', 'lt', 'le'] as RuleOp[]).map((o) => (
                    <option key={o} value={o} disabled={rule.scope === 'month' && (o === 'lt' || o === 'le')}>
                      {OP_SYMBOL[o]}
                    </option>
                  ))}
                </select>
                <input type="text" inputMode="decimal" aria-label={t('re.valueIn', { unit })} value={d.value} onInput={(e) => set({ value: e.currentTarget.value })} aria-invalid={!Number.isFinite(parsed as number)} />
                <span class="unit">{unit}</span>
              </div>
              <span class="field-h muted">
                {Number.isFinite(parsed as number) ? `= ${fmtValue(d.metric, parsed as number, { est: false })}` : t('re.decimals')}
                {now !== null ? t('re.now', { v: fmtValue(rule.metric, now, { est: false }) }) : ''}
                {t('re.allowed', { min: fmtValue(d.metric, m.min, { est: false }), max: fmtValue(d.metric, m.max, { est: false }) })}
              </span>
            </div>
          )}

          {rule.scope === 'inst' && (
            <label class="field">
              <span class="field-l">{t('re.hold')}</span>
              <NumSelect aria={t('re.holdAria')} value={d.forSec} options={FOR_SEC} label={secLabel} onChange={(forSec) => set({ forSec })} />
            </label>
          )}
          {rule.scope === 'day' && (rule.op === 'gt' || rule.op === 'ge') && (
            <div class="field">
              <span class="field-l">{t('re.evaluate')}</span>
              <Segmented
                label={t('re.evaluate')}
                value={d.mode}
                options={[
                  ['hold', t('re.continuously')],
                  ['at', t('re.checkOnceAt')],
                ]}
                onChange={(mode) => set({ mode })}
              />
            </div>
          )}
          {rule.mode === 'at' && (
            <label class="field">
              <span class="field-l">{t('re.checkAt')}</span>
              <input type="time" step={60} value={d.at} onInput={(e) => set({ at: e.currentTarget.value })} />
              {(rule.op === 'lt' || rule.op === 'le') && <span class="field-h muted">{t('re.belowHelp')}</span>}
            </label>
          )}
          {rule.mode === 'at' && (rule.op === 'lt' || rule.op === 'le') && (
            <label class="field">
              <span class="field-l">{t('re.minCov')}</span>
              <NumSelect aria={t('re.minCovAria')} value={d.minCov} options={MIN_COV} label={(v) => dg(`${v} %`)} onChange={(minCov) => set({ minCov })} />
              <span class="field-h muted">{t('re.minCovHelp')}</span>
            </label>
          )}
          {rule.scope === 'month' && <p class="small muted">{t('re.monthNote', { n: email.monthStart })}</p>}
        </fieldset>

        <fieldset class="fs">
          <legend>{t('re.recovers')}</legend>
          {latched ? (
            <p class="small muted">{rule.scope === 'month' ? t('re.latchedMonth') : t('re.latchedDay')}</p>
          ) : (
            <>
              {!bool && (
                <div class="field">
                  <Segmented
                    label={t('re.clrLevel')}
                    value={d.clrAuto ? 'auto' : 'custom'}
                    options={[
                      ['auto', t('re.auto', { v: Number.isFinite(autoClr) ? fmtValue(d.metric, autoClr, { est: false }) : '–' })],
                      ['custom', t('re.custom')],
                    ]}
                    onChange={(v) => set({ clrAuto: v === 'auto', clr: v === 'custom' && !d.clr && Number.isFinite(autoClr) ? toInputValue(d.metric, Math.round(autoClr * 100) / 100) : d.clr })}
                  />
                  {!d.clrAuto && (
                    <div class="cond">
                      <span class="unit">{rule.op === 'gt' || rule.op === 'ge' ? '≤' : '≥'}</span>
                      <input type="text" inputMode="decimal" aria-label={t('re.clrIn', { unit })} value={d.clr} onInput={(e) => set({ clr: e.currentTarget.value })} />
                      <span class="unit">{unit}</span>
                    </div>
                  )}
                  <span class="field-h muted">{t('re.clrHelp')}</span>
                </div>
              )}
              <label class="field">
                <span class="field-l">{t('re.forAtLeast')}</span>
                <NumSelect aria={t('re.clrHoldAria')} value={d.clrSec} options={CLR_SEC} label={secLabel} onChange={(clrSec) => set({ clrSec })} />
              </label>
            </>
          )}
        </fieldset>

        <fieldset class="fs">
          <legend>{t('re.activeHours')}</legend>
          <ToggleRow label={t('re.allDayEvery')} aria={t('re.allDay')} checked={d.allDay} onChange={(allDay) => set({ allDay })} />
          {!d.allDay && (
            <>
              <div class="cond">
                <label class="field grow">
                  <span class="field-l">{t('re.from')}</span>
                  <input type="time" step={60} value={d.from} onInput={(e) => set({ from: e.currentTarget.value })} />
                </label>
                <label class="field grow">
                  <span class="field-l">{t('re.to')}</span>
                  <input type="time" step={60} value={d.to} onInput={(e) => set({ to: e.currentTarget.value })} />
                </label>
              </div>
              <div class="chips" role="group" aria-label={t('re.days')}>
                {DAYS.map((i) => (
                  <Chip key={i} on={(d.days & (1 << i)) !== 0} class={i >= 5 ? 'weekend' : ''} title={i >= 5 ? t('re.weekend') : undefined} onClick={() => set({ days: d.days ^ (1 << i) })}>
                    {weekdayShort(i)}
                  </Chip>
                ))}
              </div>
              <span class="field-h muted">{t('re.winHelp')}</span>
            </>
          )}
        </fieldset>

        <fieldset class="fs">
          <legend>{t('re.notify')}</legend>
          <ToggleRow label={t('common.email')} aria={t('common.email')} help={t('re.emailHelp')} checked={d.email} onChange={(v) => set({ email: v })} />
          <ToggleRow label={t('re.ntfy')} aria={t('re.ntfy')} help={t('re.ntfyHelp')} checked={d.ntfy} onChange={(v) => set({ ntfy: v })} />
          <label class="field">
            <span class="field-l">{t('re.severity')}</span>
            <select value={String(d.sev)} onChange={(e) => set({ sev: Number(e.currentTarget.value) as Draft['sev'] })}>
              {[1, 2, 3, 4, 5].map((s) => (
                <option key={s} value={String(s)}>
                  {dg(String(s))} · {severityName(s)}
                </option>
              ))}
            </select>
            <span class="field-h muted">{t('re.sevHelp')}</span>
          </label>
          {d.email && (
            <>
              <div class="field">
                <span class="field-l">{t('re.recipients')}</span>
                {recipients.length === 0 ? (
                  <span class="small warn">{tj('re.noRecipients', { link: <a href="#/alerts/email">{t('common.email')}</a> })}</span>
                ) : (
                  <div class="chips">
                    <Chip on={d.rcAll} onClick={() => set({ rcAll: true })}>
                      {t('re.everyone')}
                    </Chip>
                    {recipients.map((a, i) => (
                      <Chip key={a} on={!d.rcAll && (d.rc & (1 << i)) !== 0} onClick={() => set(d.rcAll ? { rcAll: false, rc: 1 << i } : { rc: d.rc ^ (1 << i) })}>
                        {a}
                      </Chip>
                    ))}
                  </div>
                )}
              </div>
              {!latched && (
                <>
                  <div class="cond wrap">
                    <label class="field grow">
                      <span class="field-l">{t('re.cooldown')}</span>
                      <NumSelect aria={t('re.cooldown')} value={d.coolMin} options={COOL_MIN} label={minLabel} onChange={(coolMin) => set({ coolMin })} />
                    </label>
                    <label class="field grow">
                      <span class="field-l">{t('re.repeat')}</span>
                      <NumSelect aria={t('re.repeat')} value={d.repeatMin} options={REPEAT_MIN} label={minLabel} onChange={(repeatMin) => set({ repeatMin })} />
                    </label>
                    <label class="field grow">
                      <span class="field-l">{t('re.maxDay')}</span>
                      <NumSelect aria={t('re.maxDayAria')} value={d.maxDay} options={[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]} label={(v) => dg(String(v))} onChange={(maxDay) => set({ maxDay })} />
                    </label>
                  </div>
                  <ToggleRow label={t('re.clrMail')} aria={t('re.clrMailAria')} checked={d.clrMail} onChange={(clrMail) => set({ clrMail })} />
                </>
              )}
            </>
          )}
        </fieldset>

        <fieldset class="fs">
          <legend>{t('re.preview')}</legend>
          <pre class="preview">
            {previewSubject(rule, now)}
            {'\n'}
            {previewLines(rid, rule, nowSec()).join('\n')}
          </pre>
          {!email.on && d.email && <p class="small warn">{t('re.emailOffWarn')}</p>}
        </fieldset>

        {errors.length > 1 && (
          <ul class="errs small bad">
            {errors.map((x) => (
              <li key={x}>{x}</li>
            ))}
          </ul>
        )}
        {err && <Banner tone="bad">{err}</Banner>}

        {rid && (
          <div class="actions">
            <TestMailButton data={data} rid={rid} label={t('re.testForRule')} ghost disabled={!d.email} />
            <button type="button" class="btn btn-ghost bad" disabled={busy || !data.connected} onClick={() => void del()}>
              {t('re.delete')}
            </button>
          </div>
        )}
        {props.st?.st === 'inv' && <Banner tone="bad">{t('re.invalid', { err: props.st.err ?? t('re.unknownReason') })}</Banner>}
      </form>
    </Sheet>
  );
}
