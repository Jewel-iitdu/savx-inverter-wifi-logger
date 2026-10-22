// Alerts: Rules | History | E-mail (research/r8-email-alerts.md §6). Routes:
//   #/alerts            last used segment          #/alerts/rules/r03   rule editor
//   #/alerts/rules      rule list                  #/alerts/rules/new   preset picker + editor
//   #/alerts/history    events (was #/events)      #/alerts/email       e-mail settings + mailer
// Bandwidth (d1 §6.4): alerts (≤ 6 KB), mailer (≈ 300 B / 5 min) and .info/connected are only
// listened to while this page is on screen and the app is visible; the segments add their own.
import { useState } from 'preact/hooks';
import { useApp, useReportError } from '../context';
import { useHashPath, useNow, useStored, useSubscription } from '../lib/hooks';
import { fmtTime, nowSec } from '../lib/time';
import { t } from '../lib/i18n';
import { writeErrorText } from '../data/alerts';
import type { DataSource } from '../data/source';
import type { AlertsDoc, MailerStatus } from '../data/types';
import { Segmented } from '../components/ui';
import { RulesTab } from './alerts/RulesTab';
import { HistoryTab } from './alerts/HistoryTab';
import { EmailTab } from './alerts/EmailTab';

export type AlertsTab = 'rules' | 'history' | 'email';
const tabs = (): Array<[AlertsTab, string]> => [
  ['rules', t('al.rules')],
  ['history', t('al.history')],
  ['email', t('common.email')],
];
const isTab = (v: string | undefined): v is AlertsTab => v === 'rules' || v === 'history' || v === 'email';

export interface AlertsData {
  alerts: AlertsDoc | null | undefined; // undefined = still loading
  mailer: MailerStatus | null | undefined;
  connected: boolean;
  test: TestMail;
}

export function AlertsPage() {
  const { source, visible } = useApp();
  const path = useHashPath();
  const [stored, setStored] = useStored<AlertsTab>('alerts.tab', 'rules');
  const tab: AlertsTab = isTab(path[1]) ? path[1] : path[0] === 'events' ? 'history' : stored;

  const alerts = useSubscription<AlertsDoc | null>((cb, err) => source.onAlerts(cb, err), [source], visible);
  const mailer = useSubscription<MailerStatus | null>((cb, err) => source.onMailer(cb, err), [source], visible);
  const conn = useSubscription<boolean>((cb) => source.onConnected(cb), [source], visible);
  useReportError('alerts', alerts.error);
  useReportError('mailer', mailer.error);
  const test = useTestMail(source);
  const data: AlertsData = { alerts: alerts.value, mailer: mailer.value, connected: conn.value !== false, test };

  const go = (tab: AlertsTab) => {
    setStored(tab);
    location.hash = `#/alerts/${tab}`;
  };

  return (
    <div class="page">
      <div class="seg-wide">
        <Segmented label={t('al.section')} value={tab} options={tabs()} onChange={go} />
      </div>
      {tab === 'rules' && <RulesTab data={data} sub={path[2]} />}
      {tab === 'history' && <HistoryTab data={data} />}
      {tab === 'email' && <EmailTab data={data} />}
    </div>
  );
}

// ------------------------------------------------------------------ test e-mail (r8 §6.4)

interface TestReq {
  ts: number;
  rid?: string;
  at: number; // local epoch s when requested
  error?: string;
}

// Survives segment switches and leaving the page (same session).
let lastTest: TestReq | null = null;

export interface TestMail {
  req: TestReq | null;
  send: (rid?: string) => Promise<void>;
}

function useTestMail(source: DataSource): TestMail {
  const [req, setReq] = useState<TestReq | null>(lastTest);
  const send = async (rid?: string) => {
    try {
      const ts = await source.requestTestEmail(rid);
      lastTest = { ts, rid, at: nowSec() };
    } catch (e) {
      lastTest = { ts: 0, rid, at: nowSec(), error: writeErrorText(e) };
    }
    setReq(lastTest);
  };
  return { req, send };
}

/**
 * The "Send test e-mail" button and its result line. Disabled for 60 s after a request (the mailer
 * allows one test per minute). Waits for mailer/test.ts === the requested ts; after 6 min says the
 * mailer has not run.
 */
export function TestMailButton(props: { data: AlertsData; rid?: string; label: string; disabled?: boolean; ghost?: boolean }) {
  const { test, mailer, connected } = props.data;
  const now = useNow(1000);
  const req = test.req;
  const mine = !!req && (req.rid ?? null) === (props.rid ?? null);
  const res = req && !req.error && mailer?.test && mailer.test.ts === req.ts ? mailer.test : null;
  const age = req ? now - req.at : Infinity;
  const [busy, setBusy] = useState(false);
  let line: { tone: string; text: string } | null = null;
  if (mine && req) {
    if (req.error) line = { tone: 'bad', text: req.error };
    else if (res) {
      const when = fmtTime(mailer?.hb ?? res.ts);
      line = res.ok
        ? { tone: 'ok', text: t('tm.ok', { msg: `${res.msg.charAt(0).toUpperCase()}${res.msg.slice(1)}`, when }) }
        : { tone: 'bad', text: t('tm.notSent', { msg: res.msg, when }) };
    } else if (age < 360) line = { tone: 'muted', text: t('tm.requested', { t: fmtTime(req.at) }) };
    else line = { tone: 'warn', text: t('tm.notRun', { t: mailer?.hb ? fmtTime(mailer.hb) : t('common.never') }) };
  }
  const blocked = !!req && !req.error && age < 60;
  return (
    <div class="test-mail">
      <button
        type="button"
        class={`btn ${props.ghost ? 'btn-ghost' : ''}`}
        disabled={busy || blocked || !connected || props.disabled}
        onClick={async () => {
          setBusy(true);
          await test.send(props.rid);
          setBusy(false);
        }}
      >
        {busy ? t('tm.requesting') : blocked ? t('tm.wait', { label: props.label, n: 60 - age }) : props.label}
      </button>
      {line && <p class={`small ${line.tone}`}>{line.text}</p>}
    </div>
  );
}
