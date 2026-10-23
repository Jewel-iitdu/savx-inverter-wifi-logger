// One interface, two implementations: Firebase RTDB (firebaseSource.ts) and
// the offline demo generator (demoSource.ts, ?demo=1).
//
// Bandwidth discipline (d1 §6.4): every method is date-bounded. Nothing ever
// subscribes to the roots of hist/ h15/ events/ daily/. Pages attach
// listeners only while visible and detach them on `visibilitychange`.
import { t } from '../lib/i18n';
import type { AlertRule, AlertStateRec, AlertsDoc, Cfg, Daily, EmailCfg, EventRec, HistRec, Live, MailerStatus, Meta, Status } from './types';

export type Unsub = () => void;
export type ErrCb = (err: Error) => void;

export interface AuthUser {
  uid: string;
  email: string | null;
}

export interface DataSource {
  readonly kind: 'firebase' | 'demo';
  readonly deviceId: string;

  // auth
  onUser(cb: (u: AuthUser | null) => void): Unsub;
  signIn(email: string, password: string): Promise<void>;
  signOut(): Promise<void>;

  /** Close (false) / reopen (true) the realtime connection when the app is hidden / visible. */
  setActive(active: boolean): void;

  // realtime (one listener per node; callers unsubscribe on hide/unmount)
  onLive(cb: (v: Live | null) => void, err?: ErrCb): Unsub;
  onStatus(cb: (v: Status | null) => void, err?: ErrCb): Unsub;
  onMeta(cb: (v: Meta | null) => void, err?: ErrCb): Unsub;
  onCfg(cb: (v: Partial<Cfg> | null) => void, err?: ErrCb): Unsub;
  onDaily(date: string, cb: (v: Daily | null) => void, err?: ErrCb): Unsub;
  /** New minute records of `date` with key > afterKey (null = all). */
  onHistAdded(date: string, afterKey: string | null, cb: (key: string, rec: HistRec) => void, err?: ErrCb): Unsub;

  // one-shot reads
  getHist(date: string): Promise<Record<string, HistRec>>;
  getH15(date: string): Promise<Record<string, HistRec>>;
  getDailyRange(from: string, to: string): Promise<Record<string, Daily>>;
  getEvents(date: string): Promise<Record<string, EventRec>>;

  // owner writes (rules: cfg/* and events/*/*/ack only)
  setCfg(patch: Partial<Cfg>): Promise<void>;
  ackEvent(date: string, id: string): Promise<void>;

  // ---- alert rules and e-mail (research/r8-email-alerts.md §6.2). Listen only while Alerts is visible.
  /** true while the realtime connection is up (`.info/connected`, no download). Saves are disabled when false. */
  onConnected(cb: (up: boolean) => void): Unsub;
  /** alerts/ (e-mail settings + up to 16 rules, ≤ ~6 KB). */
  onAlerts(cb: (v: AlertsDoc | null) => void, err?: ErrCb): Unsub;
  /** alerts/email/monthStart (a few bytes): first day of the billing month, for the slab tariff on Today and History. */
  onMonthStart(cb: (v: number | null) => void, err?: ErrCb): Unsub;
  /** alertState/ (≈ 1.5 KB, written by the logger on rule transitions). */
  onAlertState(cb: (v: Record<string, AlertStateRec> | null) => void, err?: ErrCb): Unsub;
  /** mailer/ (≈ 300 B, rewritten every 5 min by the Apps Script mailer). */
  onMailer(cb: (v: MailerStatus | null) => void, err?: ErrCb): Unsub;
  /**
   * Save rule `rid` (or the first free slot when `rid` is null) and bump cfg/alertsRev.
   * Resolves the slot used. A new rule is created with a transaction, so two phones never share a slot.
   */
  saveRule(rid: string | null, rule: Omit<AlertRule, 'upd'>): Promise<string>;
  setRuleEnabled(rid: string, on: boolean): Promise<void>;
  deleteRule(rid: string): Promise<void>;
  /** alerts/email plus remapped rule rc masks (null = everyone) in one update, with the rev bump. */
  saveEmailCfg(cfg: EmailCfg, rcRemap: Record<string, number | null>): Promise<void>;
  /** Writes mailq/test {ts: server time (s), rid?}; resolves ts. The mailer answers in mailer/test. */
  requestTestEmail(rid?: string): Promise<number>;
}

/** Thrown by saveRule when all 16 slots are in use. */
export class NoFreeSlotError extends Error {
  constructor() {
    super(t('err.noFreeSlot'));
  }
}

export class ConfigMissingError extends Error {
  constructor(public missing: string[]) {
    super(`Firebase config missing: ${missing.join(', ')}`);
  }
}
