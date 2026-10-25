// Settings: cfg form (same ranges as the rules), device info from meta/status,
// test alert / re-probe (device acts within 60 s), theme, cache, sign out.
import { useEffect, useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useNow, useSubscription } from '../lib/hooks';
import { fmtAgo, fmtDateTime, fmtDuration, nowSec } from '../lib/time';
import { fmtBytes, fmtNum, isNum } from '../lib/format';
import { idbClear } from '../lib/idb';
import { CFG_DEFAULTS, CFG_FIELDS, validateCfgValue, type Cfg, type Live, type Status } from '../data/types';
import { Banner, Card, KV, Segmented, Switch, ToggleRow } from '../components/ui';
import { FirmwareCard, versionCode } from '../components/FirmwareCard';
import { LoggerLog } from '../components/LoggerLog';
import { outputIsVA } from '../lib/decode';
import { PF_DEFAULT, PF_MAX, PF_MIN, parsePf, usePowerPrefs } from '../lib/power';
import { localizeDigits as dg, t, tj } from '../lib/i18n';

/** Settings > Logger rows for one alert channel (fw 1.0.0): sends since restart, and the last failure with its time. */
function alertRows(label: string, errLabel: string, ok?: number, fail?: number, err?: string, at?: number): Array<[string, string]> {
  if (!isNum(ok)) return [];
  const rows: Array<[string, string]> = [[label, t('set.sentFailed', { ok: fmtNum(ok), fail: fmtNum(fail ?? 0) })]];
  if (err) rows.push([errLabel, isNum(at) ? `${err} · ${fmtDateTime(at)}` : err]);
  return rows;
}

export function SettingsPage() {
  const { source, visible, meta, user, themePref, setThemePref, lang, setLang } = useApp();
  const pp = usePowerPrefs();
  const vaOnly = outputIsVA(meta?.dia);
  // Calibration: the W shown on the LCD now ÷ the VA the inverter reports now (on a VA-only inverter `w` is the VA)
  const liveSub = useSubscription<Live | null>((cb, err) => source.onLive(cb, err), [source], visible && vaOnly);
  const nowVA = isNum(liveSub.value?.va) ? liveSub.value!.va! : liveSub.value?.w;
  const [lcdW, setLcdW] = useState('');
  const calPf = isNum(nowVA) && nowVA > 0 && Number(lcdW) > 0 ? Number(lcdW) / nowVA : NaN;
  const calOk = calPf >= PF_MIN && calPf <= PF_MAX;
  const now = useNow(5000);
  const cfgSub = useSubscription<Partial<Cfg> | null>((cb, err) => source.onCfg(cb, err), [source], visible);
  const status = useSubscription<Status | null>((cb, err) => source.onStatus(cb, err), [source], visible);
  useReportError('cfg', cfgSub.error);
  const cfg: Cfg = { ...CFG_DEFAULTS, ...(cfgSub.value ?? {}) };
  const [draft, setDraft] = useState<Record<string, string>>({});
  const [saving, setSaving] = useState(false);
  const [msg, setMsg] = useState<{ tone: 'ok' | 'bad' | 'info'; text: string } | null>(null);
  const [goDraft, setGoDraft] = useState<{ on: boolean; from: string; to: string } | null>(null);
  const [goMsg, setGoMsg] = useState<{ tone: 'ok' | 'bad'; text: string } | null>(null);
  const [goSaving, setGoSaving] = useState(false);
  const [meterMsg, setMeterMsg] = useState<{ tone: 'ok' | 'bad'; text: string } | null>(null);
  const [meterSaving, setMeterSaving] = useState(false);

  useEffect(() => setDraft({}), [cfgSub.value]);

  const errors: Record<string, string> = {};
  const patch: Partial<Cfg> = {};
  for (const f of CFG_FIELDS) {
    if (!(f.key in draft)) continue;
    const v = Number(draft[f.key]);
    const e = validateCfgValue(f, v);
    if (e) errors[f.key] = e;
    else if (v !== cfg[f.key]) patch[f.key] = v;
  }
  const dirty = Object.keys(patch).length > 0;
  const invalid = Object.keys(errors).length > 0;

  const write = async (p: Partial<Cfg>, ok: string) => {
    setSaving(true);
    setMsg(null);
    try {
      await source.setCfg(p);
      setMsg({ tone: 'ok', text: ok });
    } catch (e) {
      setMsg({ tone: 'bad', text: t('set.notSaved', { msg: e instanceof Error ? e.message : String(e) }) });
    } finally {
      setSaving(false);
    }
  };

  const s = status.value ?? null;
  const live = s?.ts ? now - s.ts : Infinity;
  const pending = (k: 'testAlert' | 'reprobe') => cfg[k] > 0 && nowSec() - cfg[k] < 90;
  // fw 1.0.0 remote restart = an update request for the running version (cfg.ota/otaAt, ms). Pending until the
  // logger reports a boot after the request (status ts - up), at most 2 min; then "restarted … ago" for 10 min.
  const rq = cfgSub.value;
  const restartAt = rq?.ota && rq.ota === versionCode(s?.fw) ? (rq.otaAt ?? 0) : 0;
  const bootAt = isNum(s?.ts) && isNum(s?.up) ? s!.ts! - s!.up! : 0;
  const restarted = restartAt > 0 && bootAt * 1000 > restartAt;
  const restartPending = restartAt > 0 && !restarted && Date.now() - restartAt < 120000;
  const restartDone = restarted && Date.now() - restartAt < 600000;

  return (
    <div class="page">
      <Card title={t('set.alertsLogging')} right={<a class="small" href="#/alerts/rules">{t('set.usageAlertsLink')}</a>}>
        <form
          class="form"
          onSubmit={(e) => {
            e.preventDefault();
            if (dirty && !invalid) void write(patch, t('set.savedCfg'));
          }}
        >
          {CFG_FIELDS.map((f) => (
            <label class="field" key={f.key}>
              <span class="field-l">
                {f.label} <span class="muted">({f.unit})</span>
              </span>
              <input
                type="number"
                inputMode={f.step < 1 || f.min < 0 ? 'decimal' : 'numeric'}
                min={f.min}
                max={f.max}
                step={f.step}
                value={f.key in draft ? draft[f.key] : String(cfg[f.key])}
                onInput={(e) => setDraft({ ...draft, [f.key]: e.currentTarget.value })}
                aria-invalid={!!errors[f.key]}
              />
              <span class={`field-h ${errors[f.key] ? 'bad' : 'muted'}`}>{errors[f.key] ?? f.help}</span>
            </label>
          ))}
          <div class="row-end">
            <button type="button" class="btn-link" disabled={!Object.keys(draft).length} onClick={() => setDraft({})}>
              {t('common.reset')}
            </button>
            <button type="submit" class="btn" disabled={!dirty || invalid || saving}>
              {saving ? t('common.saving') : t('common.save')}
            </button>
          </div>
        </form>
        {msg && <Banner tone={msg.tone}>{msg.text}</Banner>}
      </Card>

      <Card title={t('go.title')}>
        {(() => {
          const hm = (m: number) => `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;
          const toMin = (v: string) => Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5));
          const cur = {
            on: cfg.gridOffFrom >= 0 && cfg.gridOffTo >= 0,
            from: hm(cfg.gridOffFrom >= 0 ? cfg.gridOffFrom : 480),
            to: hm(cfg.gridOffTo >= 0 ? cfg.gridOffTo : 930),
          };
          const g = goDraft ?? cur;
          const same = g.on && g.from === g.to;
          return (
            <div class="form">
              <Switch checked={g.on} label={t('go.enable')} onChange={(v) => setGoDraft({ ...g, on: v })} />
              {g.on && (
                <div class="grid2">
                  <label class="field">
                    <span class="field-l">{t('go.from')}</span>
                    <input type="time" value={g.from} onInput={(e) => setGoDraft({ ...g, from: e.currentTarget.value })} />
                  </label>
                  <label class="field">
                    <span class="field-l">{t('go.to')}</span>
                    <input type="time" value={g.to} onInput={(e) => setGoDraft({ ...g, to: e.currentTarget.value })} />
                  </label>
                </div>
              )}
              <p class={`small ${same ? 'bad' : 'muted'}`}>{same ? t('go.bad') : t('go.help')}</p>
              <div class="row-end">
                <button
                  type="button"
                  class="btn"
                  disabled={!goDraft || same || goSaving}
                  onClick={async () => {
                    setGoSaving(true);
                    setGoMsg(null);
                    try {
                      await source.setCfg({ gridOffFrom: g.on ? toMin(g.from) : -1, gridOffTo: g.on ? toMin(g.to) : -1 });
                      setGoMsg({ tone: 'ok', text: t('go.saved') });
                      setGoDraft(null);
                    } catch (e) {
                      setGoMsg({ tone: 'bad', text: t('set.notSaved', { msg: e instanceof Error ? e.message : String(e) }) });
                    } finally {
                      setGoSaving(false);
                    }
                  }}
                >
                  {goSaving ? t('common.saving') : t('common.save')}
                </button>
              </div>
              {goMsg && <Banner tone={goMsg.tone}>{goMsg.text}</Banner>}
            </div>
          );
        })()}
      </Card>

      <Card title={t('set.meter')}>
        <ToggleRow
          label={t('set.meterUse')}
          aria={t('set.meter')}
          help={t('set.meterHelp')}
          checked={cfg.meter !== 0}
          disabled={meterSaving || cfgSub.value === undefined}
          onChange={async (on) => {
            if (on === (cfg.meter !== 0)) return;
            setMeterSaving(true);
            setMeterMsg(null);
            try {
              await source.setCfg({ meter: on ? 1 : 0 });
              setMeterMsg({ tone: 'ok', text: t('set.savedCfg') });
            } catch (e) {
              setMeterMsg({ tone: 'bad', text: t('set.notSaved', { msg: e instanceof Error ? e.message : String(e) }) });
            } finally {
              setMeterSaving(false);
            }
          }}
        />
        {meterMsg && <Banner tone={meterMsg.tone}>{meterMsg.text}</Banner>}
      </Card>

      <FirmwareCard status={status.value} fw={status.value?.fw ?? meta?.fwver} reqOta={cfgSub.value?.ota} reqAt={cfgSub.value?.otaAt} />

      <Card title={t('set.actions')}>
        <div class="actions">
          <button
            type="button"
            class="btn"
            disabled={saving}
            onClick={() => void write({ testAlert: nowSec() }, t('set.testAlertOk'))}
          >
            {t('set.sendTestAlert')}
          </button>
          <button
            type="button"
            class="btn btn-ghost"
            disabled={saving}
            onClick={() => {
              if (confirm(t('set.reprobeConfirm'))) void write({ reprobe: nowSec() }, t('set.reprobeOk'));
            }}
          >
            {t('set.reprobe')}
          </button>
          {versionCode(s?.fw) >= 10000 && (
            // fw 1.0.0: an update request for the running version restarts the logger (no new rule line)
            <button
              type="button"
              class="btn btn-ghost"
              disabled={saving || restartPending}
              onClick={() => {
                if (confirm(t('set.restartConfirm'))) void write({ ota: versionCode(s!.fw), otaAt: Date.now() }, t('set.restartOk'));
              }}
            >
              {t('set.restart')}
            </button>
          )}
        </div>
        <p class="muted small">
          {pending('testAlert') ? t('set.testPending') : ''}
          {pending('reprobe') ? t('set.reprobePending') : ''}
          {restartPending ? t('set.restartPending') : ''}
          {restartDone ? t('set.restarted', { ago: fmtAgo(now - bootAt) }) : ''}
          {tj('set.actionsNote', { link: <a href="#/alerts/email">{t('set.alertsEmailLink')}</a> })}
        </p>
        <p class="small">
          {tj('set.localPage', {
            url: (
              <a href="http://solar.local" target="_blank" rel="noopener">
                http://solar.local
              </a>
            ),
            ip: s?.ip ? (
              <a href={`http://${s.ip}`} target="_blank" rel="noopener">
                {s.ip}
              </a>
            ) : (
              t('set.fromRouter')
            ),
          })}
        </p>
      </Card>

      <Card title={t('set.logger')} right={<span class={`small ${live > 120 ? 'bad' : 'muted'}`}>{s?.ts ? t('set.status', { ago: fmtAgo(live) }) : t('set.noStatus')}</span>}>
        <KV
          rows={[
            [t('set.firmware'), String(s?.fw ?? meta?.fwver ?? '–')],
            [t('set.protocol'), `${s?.proto ?? '–'} · ${meta?.dia ?? s?.dia ?? '–'}`],
            [t('set.linkProfile'), String(meta?.link ?? '–')],
            ['Wi-Fi', isNum(s?.rssi) ? dg(`${s!.rssi} dBm`) : '–'],
            ['IP', String(s?.ip ?? '–')],
            [t('set.uptime'), isNum(s?.up) ? fmtDuration(s!.up!) : '–'],
            [t('set.lastReset'), String(s?.rr ?? '–')],
            [t('set.heap'), `${fmtBytes(s?.heap)} / ${fmtBytes(s?.psram)}`],
            [t('set.uploadQueue'), isNum(s?.q) ? t('u.min', { n: s!.q! }) : '–'],
            [t('set.inverterReply'), isNum(s?.lok) ? fmtAgo(now - s!.lok!) : '–'],
            [t('set.cloudOk'), isNum(s?.cok) ? fmtAgo(now - s!.cok!) : '–'],
            [t('set.requests'), `${fmtNum(s?.rq)} / ${fmtNum(s?.hs)}`],
            // fw 1.0.0: chip temperature and alert delivery (rows only when the logger sends them)
            ...(isNum(s?.chipC) ? [[t('set.chipTemp'), fmtNum(s!.chipC, 0, '°C')] as [string, string]] : []),
            ...alertRows(t('set.alertTg'), t('set.tgErr'), s?.tgOk, s?.tgFail, s?.tgErr, s?.tgErrAt),
            ...alertRows(t('set.alertMail'), t('set.mailErr'), s?.mailOk, s?.mailFail, s?.mailErr, s?.mailErAt),
          ]}
        />
      </Card>

      <LoggerLog keepDays={cfg.keepEventDays} />

      <Card title={t('w.inverter')}>
        <KV
          rows={[
            [t('set.protocolId'), String(meta?.qpi ?? '–')],
            [t('set.model'), String(meta?.qmn ?? '–')],
            [t('set.serial'), String(meta?.qid ?? '–')],
            ['CPU U1 / U2', `${meta?.vfw ?? '–'} / ${meta?.vfw2 ?? '–'}`],
            [t('set.rating'), dg(`${meta?.ratva ?? '–'} VA / ${meta?.ratw ?? '–'} W / ${meta?.ratbv ?? '–'} V`)],
            [t('set.battType'), String(meta?.btype ?? '–')],
            [t('set.priority'), `${meta?.outpri ?? '–'} / ${meta?.chgpri ?? '–'}`],
            [t('set.pv12'), meta?.pv12 === 'W' ? t('set.pv12W') : meta?.pv12 === 'A' ? t('set.pv12A') : '–'],
            [t('set.supported'), String(meta?.sup ?? '–')],
            [t('set.boot'), isNum(meta?.boot) ? fmtDateTime(meta!.boot as number) : String(meta?.boot ?? '–')],
          ]}
        />
      </Card>

      <Card title={t('set.thisApp')}>
        <div class="field">
          <span class="field-l">{t('set.theme')}</span>
          <Segmented
            label={t('set.theme')}
            value={themePref}
            options={[
              ['auto', t('set.themeAuto')],
              ['light', t('set.themeLight')],
              ['dark', t('set.themeDark')],
            ]}
            onChange={setThemePref}
          />
        </div>
        <div class="field">
          <span class="field-l">{t('lang.label')}</span>
          <Segmented
            label={t('lang.label')}
            value={lang}
            options={[
              ['en', 'English'],
              ['bn', 'বাংলা'],
            ]}
            onChange={setLang}
          />
        </div>
        {vaOnly && (
          <>
            <div class="field">
              <span class="field-l">{t('set.powerUnit')}</span>
              <Segmented
                label={t('set.powerUnit')}
                value={pp.unit}
                options={[
                  ['W', t('set.unitW')],
                  ['VA', t('set.unitVA')],
                ]}
                onChange={pp.setUnit}
              />
            </div>
            {pp.unit === 'W' && (
              <div class="field">
                <span class="field-l">{t('set.pf')}</span>
                <input
                  type="number"
                  inputMode="decimal"
                  min={PF_MIN}
                  max={PF_MAX}
                  step="0.01"
                  value={pp.pf}
                  onInput={(e) => pp.setPf((e.target as HTMLInputElement).value)}
                />
                {pp.pf.trim() !== '' && parsePf(pp.pf) !== Number(pp.pf) && (
                  <span class="field-h bad">{t('set.pfBad', { min: PF_MIN, max: PF_MAX, def: PF_DEFAULT })}</span>
                )}
              </div>
            )}
            {pp.unit === 'W' && (
              <div class="field">
                <span class="field-l">{t('set.calibrate')}</span>
                <span class="field-h muted">{t('set.calHelp', { va: isNum(nowVA) ? dg(`${Math.round(nowVA)} VA`) : '–' })}</span>
                <div class="cal">
                  <input
                    type="number"
                    inputMode="numeric"
                    min="1"
                    step="1"
                    placeholder="LCD W"
                    value={lcdW}
                    onInput={(e) => setLcdW((e.target as HTMLInputElement).value)}
                  />
                  <button
                    type="button"
                    class="btn"
                    disabled={!calOk}
                    onClick={() => {
                      pp.setPf(calPf.toFixed(2));
                      setLcdW('');
                      setMsg({ tone: 'ok', text: t('set.pfSet', { pf: fmtNum(calPf, 2), w: dg(lcdW), va: Math.round(nowVA!) }) });
                    }}
                  >
                    {t('set.setPf', { pf: calOk ? fmtNum(calPf, 2) : '' })}
                  </button>
                </div>
                {lcdW && isNum(calPf) && !calOk && (
                  <span class="field-h bad">{t('set.calBad', { w: dg(lcdW), va: Math.round(nowVA!), pf: fmtNum(calPf, 2), min: PF_MIN, max: PF_MAX })}</span>
                )}
              </div>
            )}
            <p class="muted small">
              {t('set.vaNote', {
                range: pp.unit === 'W' && parsePf(pp.pf) !== Number(pp.pf) ? t('set.pfRange', { min: PF_MIN, max: PF_MAX, def: PF_DEFAULT }) : '',
              })}
            </p>
          </>
        )}
        <KV
          rows={[
            [t('set.signedInAs'), user.email ?? '–'],
            [t('set.userUid'), <code class="uid">{user.uid}</code>],
            [t('set.dataSource'), source.kind === 'demo' ? t('set.demoSource') : `Firebase · devices/${source.deviceId}`],
            [t('set.appVersion'), `${__APP_VERSION__} (${__BUILD_TIME__.slice(0, 16).replace('T', ' ')} UTC)`],
          ]}
        />
        <div class="actions">
          <button
            type="button"
            class="btn btn-ghost"
            onClick={() =>
              void idbClear().then(() => {
                setMsg({ tone: 'info', text: t('set.cacheCleared') });
              })
            }
          >
            {t('set.clearCache')}
          </button>
          <button type="button" class="btn btn-ghost bad" onClick={() => void source.signOut()}>
            {t('set.signOut')}
          </button>
        </div>
        <p class="muted small">{tj('set.tip', { hist: <code>hist</code> })}</p>
      </Card>
    </div>
  );
}
