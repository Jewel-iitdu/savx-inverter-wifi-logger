// Alerts > E-mail (r8 §6.4): master switch, up to 3 recipients (removing one remaps every rule's
// rc in the same update), sender name, daily limit, billing-month start, built-in alerts, daily
// summary, logger-offline mail, mailer status card and "Send test e-mail".
import { useEffect, useMemo, useState } from 'preact/hooks';
import { useApp } from '../../context';
import { useNow } from '../../lib/hooks';
import { dateKey, fmtAgo, fmtClock, fmtDateTime, fmtTime } from '../../lib/time';
import { localizeDigits as dg, t, tj } from '../../lib/i18n';
import {
  EMAIL_RE,
  MAX_RECIPIENTS,
  mailerHealth,
  parseRecipients,
  remapRc,
  validateEmailCfg,
  withEmailDefaults,
  writeErrorText,
  type FullEmailCfg,
} from '../../data/alerts';
import type { EmailCfg } from '../../data/types';
import { Banner, Card, KV, Segmented, Spinner, ToggleRow } from '../../components/ui';
import { TestMailButton, type AlertsData } from '../Alerts';

type MinSev = '0' | '3' | '4' | '5';
const HOURS = Array.from({ length: 24 }, (_, h) => h);

function toDoc(f: FullEmailCfg, list: string[]): EmailCfg {
  return {
    on: f.on,
    to: list.join(','),
    fromName: f.fromName.trim(),
    maxPerDay: f.maxPerDay,
    monthStart: f.monthStart,
    builtin: { minSev: f.builtin.minSev, clear: f.builtin.clear },
    summary: { on: f.summary.on, hour: f.summary.hour },
    offline: { on: f.offline.on, min: f.offline.min, clear: f.offline.clear },
  };
}

export function EmailTab({ data }: { data: AlertsData }) {
  const { source } = useApp();
  const now = useNow(15000);
  const saved = useMemo(() => withEmailDefaults(data.alerts?.email), [data.alerts?.email]);
  const savedList = useMemo(() => parseRecipients(saved.to), [saved.to]);
  const [f, setF] = useState<FullEmailCfg>(saved);
  const [list, setList] = useState<string[]>(savedList);
  const [addr, setAddr] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ tone: 'ok' | 'bad'; text: string } | null>(null);

  // Re-sync with the database when it changes and there are no local edits.
  const doc = toDoc(f, list);
  const savedDoc = toDoc(saved, savedList);
  const dirty = JSON.stringify(doc) !== JSON.stringify(savedDoc);
  useEffect(() => {
    if (!dirty) {
      setF(saved);
      setList(savedList);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [saved, savedList]);

  if (data.alerts === undefined) return <Spinner />;

  const errors = validateEmailCfg(doc);
  const addrTrim = addr.trim();
  const addrErr =
    addrTrim === ''
      ? null
      : !EMAIL_RE.test(addrTrim)
        ? t('em.invalidAddr')
        : list.some((a) => a.toLowerCase() === addrTrim.toLowerCase())
          ? t('em.dup')
          : list.length >= MAX_RECIPIENTS
            ? t('em.maxRecipients', { n: MAX_RECIPIENTS })
            : null;
  const add = () => {
    if (!addrTrim || addrErr) return;
    setList([...list, addrTrim]);
    setAddr('');
  };
  const set = (p: Partial<FullEmailCfg>) => setF((cur) => ({ ...cur, ...p }));

  const save = async () => {
    const { remap, widened } = remapRc(savedList, list, data.alerts?.rules);
    if (widened.length) {
      const names = widened.map((rid) => `"${data.alerts?.rules?.[rid]?.name ?? rid}"`).join(', ');
      if (!confirm(t(widened.length > 1 ? 'em.widenedMany' : 'em.widenedOne', { names }))) return;
    }
    setBusy(true);
    setMsg(null);
    try {
      await source.saveEmailCfg(doc, remap);
      setMsg({ tone: 'ok', text: t('em.saved') });
    } catch (e) {
      setMsg({ tone: 'bad', text: writeErrorText(e) });
    } finally {
      setBusy(false);
    }
  };

  const m = data.mailer;
  const health = mailerHealth(m?.hb, now);
  const sentToday = m?.day === dateKey(now) ? (m?.sent ?? 0) : 0;

  return (
    <>
      {!data.connected && <Banner tone="warn">{t('common.offlineNoSave')}</Banner>}
      <Card title={t('em.title')}>
        <form
          class="form"
          onSubmit={(e) => {
            e.preventDefault();
            if (dirty && !errors.length && data.connected) void save();
          }}
        >
          <ToggleRow label={t('em.send')} aria={t('em.send')} help={t('em.sendHelp')} checked={f.on} onChange={(on) => set({ on })} />

          <div class="field">
            <span class="field-l">{t('em.recipients', { n: list.length, max: MAX_RECIPIENTS })}</span>
            {list.length === 0 && <span class="small warn">{t('em.noRecipients')}</span>}
            <ul class="recips">
              {list.map((a, i) => (
                <li key={a}>
                  <span class="recip-i">{dg(String(i + 1))}</span>
                  <span class="recip-a">{a}</span>
                  <button type="button" class="btn-link bad" aria-label={t('em.removeAria', { a })} onClick={() => setList(list.filter((x) => x !== a))}>
                    {t('em.remove')}
                  </button>
                </li>
              ))}
            </ul>
            {list.length < MAX_RECIPIENTS && (
              <div class="cond">
                <input
                  type="email"
                  autocomplete="email"
                  autocapitalize="off"
                  spellcheck={false}
                  placeholder="name@gmail.com"
                  value={addr}
                  aria-label={t('em.newRecipient')}
                  aria-invalid={!!addrErr}
                  onInput={(e) => setAddr(e.currentTarget.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') {
                      e.preventDefault();
                      add();
                    }
                  }}
                />
                <button type="button" class="btn btn-ghost" disabled={!addrTrim || !!addrErr} onClick={add}>
                  {t('em.add')}
                </button>
              </div>
            )}
            <span class={`field-h ${addrErr ? 'bad' : 'muted'}`}>
              {addrErr ?? tj('em.allowHelp', { code: <code>ALLOW_TO</code> })}
            </span>
            {m?.warn && <span class="small warn">{t('em.mailerWarn', { w: m.warn })}</span>}
          </div>

          <div class="cond wrap">
            <label class="field grow">
              <span class="field-l">{t('em.sender')}</span>
              <input type="text" maxLength={40} value={f.fromName} onInput={(e) => set({ fromName: e.currentTarget.value.replace(/[\r\n\t]+/g, ' ') })} />
            </label>
            <label class="field">
              <span class="field-l">{t('em.dailyLimit')}</span>
              <input type="number" inputMode="numeric" min={1} max={40} step={1} value={String(f.maxPerDay)} onInput={(e) => set({ maxPerDay: Number(e.currentTarget.value) })} />
            </label>
            <label class="field">
              <span class="field-l">{t('em.billingDay')}</span>
              <input type="number" inputMode="numeric" min={1} max={28} step={1} value={String(f.monthStart)} onInput={(e) => set({ monthStart: Number(e.currentTarget.value) })} />
            </label>
          </div>
          <span class="field-h muted">{t('em.limitHelp')}</span>

          <div class="field">
            <span class="field-l">{t('em.builtin')}</span>
            <Segmented
              label={t('em.builtin')}
              value={String(f.builtin.minSev) as MinSev}
              options={[
                ['0', t('common.Off')],
                ['3', t('em.warnPlus')],
                ['4', t('em.alertPlus')],
                ['5', t('em.critical')],
              ]}
              onChange={(v) => set({ builtin: { ...f.builtin, minSev: Number(v) as 0 | 3 | 4 | 5 } })}
            />
            <span class="field-h muted">{t('em.builtinHelp')}</span>
          </div>
          <ToggleRow label={t('em.recovery')} aria={t('em.recoveryAria')} help={t('em.recoveryHelp')} checked={f.builtin.clear} disabled={f.builtin.minSev === 0} onChange={(clear) => set({ builtin: { ...f.builtin, clear } })} />

          <ToggleRow label={t('em.summary')} aria={t('em.summary')} help={t('em.summaryHelp')} checked={f.summary.on} onChange={(on) => set({ summary: { ...f.summary, on } })} />
          {f.summary.on && (
            <label class="field">
              <span class="field-l">{t('em.summaryAt')}</span>
              <select value={String(f.summary.hour)} onChange={(e) => set({ summary: { ...f.summary, hour: Number(e.currentTarget.value) } })}>
                {HOURS.map((h) => (
                  <option key={h} value={String(h)}>
                    {fmtClock(h, 0)}
                  </option>
                ))}
              </select>
            </label>
          )}

          <ToggleRow label={t('em.offline')} aria={t('em.offline')} help={t('em.offlineHelp')} checked={f.offline.on} onChange={(on) => set({ offline: { ...f.offline, on } })} />
          {f.offline.on && (
            <>
              <label class="field">
                <span class="field-l">{t('em.offlineAfter')}</span>
                <input type="number" inputMode="numeric" min={10} max={240} step={1} value={String(f.offline.min)} onInput={(e) => set({ offline: { ...f.offline, min: Number(e.currentTarget.value) } })} />
                <span class="field-h muted">{t('em.offlineAfterHelp')}</span>
              </label>
              <ToggleRow label={t('em.backOnline')} aria={t('em.backOnlineAria')} checked={f.offline.clear} onChange={(clear) => set({ offline: { ...f.offline, clear } })} />
            </>
          )}

          {errors.length > 0 && (
            <ul class="errs small bad">
              {errors.map((x) => (
                <li key={x}>{x}</li>
              ))}
            </ul>
          )}
          <div class="row-end">
            <button
              type="button"
              class="btn-link"
              disabled={!dirty || busy}
              onClick={() => {
                setF(saved);
                setList(savedList);
                setAddr('');
              }}
            >
              {t('common.reset')}
            </button>
            <button type="submit" class="btn" disabled={!dirty || errors.length > 0 || busy || !data.connected}>
              {busy ? t('common.saving') : t('common.save')}
            </button>
          </div>
          {msg && <Banner tone={msg.tone}>{msg.text}</Banner>}
        </form>
      </Card>

      <Card
        title={t('em.mailer')}
        class={health === 'stale' ? 'card-bad' : ''}
        right={<span class={`small ${health === 'stale' ? 'bad' : 'muted'}`}>{m?.hb ? t('em.lastRun', { ago: fmtAgo(now - m.hb) }) : t('em.noHb')}</span>}
      >
        {health === 'none' && (
          <p class="small">{t('em.noneYet')}</p>
        )}
        {health === 'stale' && (
          <Banner tone="bad">
            <strong>{t('em.staleTitle')}</strong> {t('em.staleBody')}
          </Banner>
        )}
        {m?.err && <p class="small bad">{t('em.lastError', { e: m.err })}</p>}
        {m && (
          <KV
            rows={[
              [t('em.version'), m.ver ?? '–'],
              [t('em.sentToday'), t('em.sentTodayVal', { n: sentToday, max: f.maxPerDay })],
              [t('em.quotaLeft'), typeof m.quota === 'number' ? dg(String(m.quota)) : '–'],
              [t('em.lastEmail'), m.last ? `${fmtDateTime(m.last.ts)} · ${m.last.ok ? '' : t('em.failed')}${m.last.msg}` : '–'],
              [t('em.seenOffline'), m.off ? t('common.since', { t: fmtTime(m.off) }) : t('common.no')],
            ]}
          />
        )}
        <TestMailButton data={data} label={t('em.testBtn')} />
        <p class="muted small">{tj('em.testNote', { code: <code>ALLOW_TO</code> })}</p>
      </Card>
    </>
  );
}
