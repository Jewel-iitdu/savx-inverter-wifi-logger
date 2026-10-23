// Firebase RTDB implementation of DataSource. Loaded with a dynamic import so
// demo mode never downloads the SDK. Firebase JS SDK (modular) is bundled and
// self-hosted, never loaded from a CDN (d1 K11).
import { initializeApp, type FirebaseApp } from 'firebase/app';
import {
  browserLocalPersistence,
  indexedDBLocalPersistence,
  initializeAuth,
  onAuthStateChanged,
  signInWithEmailAndPassword,
  signOut as fbSignOut,
  type Auth,
} from 'firebase/auth';
import {
  endAt,
  get,
  getDatabase,
  goOffline,
  goOnline,
  onChildAdded,
  onValue,
  orderByKey,
  query,
  ref,
  runTransaction,
  serverTimestamp,
  set,
  startAfter,
  startAt,
  update,
  type Database,
  type DataSnapshot,
} from 'firebase/database';
import { ConfigMissingError, NoFreeSlotError, type DataSource, type ErrCb, type Unsub } from './source';
import type { AlertStateRec, AlertsDoc, Cfg, Daily, EventRec, HistRec, MailerStatus } from './types';
import { RULE_IDS, cleanRule } from './alerts';

const env = import.meta.env;

export function firebaseConfigFromEnv() {
  const cfg = {
    apiKey: env.VITE_FIREBASE_API_KEY as string | undefined,
    authDomain: env.VITE_FIREBASE_AUTH_DOMAIN as string | undefined,
    projectId: env.VITE_FIREBASE_PROJECT_ID as string | undefined,
    appId: env.VITE_FIREBASE_APP_ID as string | undefined,
    databaseURL: env.VITE_FIREBASE_DATABASE_URL as string | undefined,
  };
  const required: Array<[keyof typeof cfg, string]> = [
    ['apiKey', 'VITE_FIREBASE_API_KEY'],
    ['authDomain', 'VITE_FIREBASE_AUTH_DOMAIN'],
    ['projectId', 'VITE_FIREBASE_PROJECT_ID'],
    ['databaseURL', 'VITE_FIREBASE_DATABASE_URL'],
  ];
  const missing = required.filter(([k]) => !cfg[k] || String(cfg[k]).includes('your-project')).map(([, name]) => name);
  if (missing.length) throw new ConfigMissingError(missing);
  return cfg as Required<Omit<typeof cfg, 'appId'>> & { appId?: string };
}

const val = <T>(s: DataSnapshot): T | null => (s.exists() ? (s.val() as T) : null);
const toErr = (e: unknown) => (e instanceof Error ? e : new Error(String(e)));

export function createFirebaseSource(): DataSource {
  const config = firebaseConfigFromEnv();
  const deviceId = (env.VITE_DEVICE_ID as string | undefined) || 'inv1';
  const app: FirebaseApp = initializeApp(config);
  // Explicit persistence (IndexedDB first) and no popup/redirect resolver:
  // email/password only (d1 §6.6), smaller bundle, works in iOS standalone mode.
  const auth: Auth = initializeAuth(app, { persistence: [indexedDBLocalPersistence, browserLocalPersistence] });
  const db: Database = getDatabase(app, config.databaseURL);
  const base = `devices/${deviceId}`;
  const r = (path: string) => ref(db, `${base}/${path}`);
  const root = ref(db, base);
  let online = true;
  // Local-only listener (no download): used to stamp mailq/test with the server's clock (r8 §3.10).
  let serverOffsetMs = 0;
  onValue(ref(db, '.info/serverTimeOffset'), (s) => {
    serverOffsetMs = Number(s.val()) || 0;
  });

  function listen<T>(path: string, cb: (v: T | null) => void, err?: ErrCb): Unsub {
    return onValue(
      r(path),
      (s) => cb(val<T>(s)),
      (e) => err?.(toErr(e)),
    );
  }

  return {
    kind: 'firebase',
    deviceId,

    onUser(cb) {
      return onAuthStateChanged(auth, (u) => cb(u ? { uid: u.uid, email: u.email } : null));
    },
    async signIn(email, password) {
      await signInWithEmailAndPassword(auth, email.trim(), password);
    },
    async signOut() {
      await fbSignOut(auth);
    },

    setActive(active) {
      if (active === online) return;
      online = active;
      // Closes the WebSocket while the app is hidden: no keep-alive traffic,
      // and iOS would suspend it anyway (d1 §6.4).
      if (active) goOnline(db);
      else goOffline(db);
    },

    onLive: (cb, err) => listen('live', cb, err),
    onStatus: (cb, err) => listen('status', cb, err),
    onMeta: (cb, err) => listen('meta', cb, err),
    onCfg: (cb, err) => listen<Partial<Cfg>>('cfg', cb, err),
    onDaily: (date, cb, err) => listen<Daily>(`daily/${date}`, cb, err),

    onHistAdded(date, afterKey, cb, err) {
      const q = afterKey ? query(r(`hist/${date}`), orderByKey(), startAfter(afterKey)) : query(r(`hist/${date}`), orderByKey());
      return onChildAdded(
        q,
        (s) => {
          if (s.key) cb(s.key, s.val() as HistRec);
        },
        (e) => err?.(toErr(e)),
      );
    },

    async getHist(date) {
      return val<Record<string, HistRec>>(await get(r(`hist/${date}`))) ?? {};
    },
    async getH15(date) {
      return val<Record<string, HistRec>>(await get(r(`h15/${date}`))) ?? {};
    },
    async getDailyRange(from, to) {
      const s = await get(query(r('daily'), orderByKey(), startAt(from), endAt(to)));
      return val<Record<string, Daily>>(s) ?? {};
    },
    async getEvents(date) {
      return val<Record<string, EventRec>>(await get(r(`events/${date}`))) ?? {};
    },

    async setCfg(patch) {
      await update(r('cfg'), patch);
    },
    async ackEvent(date, id) {
      await set(r(`events/${date}/${id}/ack`), true);
    },

    // ---- alert rules and e-mail (r8 §3.10 request flows). Every alerts/* change bumps cfg/alertsRev
    // in the SAME update, so the logger's 60 s cfg poll notices it and re-reads alerts.json.
    onConnected(cb) {
      return onValue(ref(db, '.info/connected'), (s) => cb(s.val() === true));
    },
    onAlerts: (cb, err) => listen<AlertsDoc>('alerts', cb, err),
    onMonthStart: (cb, err) => listen<number>('alerts/email/monthStart', cb, err),
    onAlertState: (cb, err) => listen<Record<string, AlertStateRec>>('alertState', cb, err),
    onMailer: (cb, err) => listen<MailerStatus>('mailer', cb, err),

    async saveRule(rid, rule) {
      const body = { ...cleanRule(rule), upd: serverTimestamp() };
      if (rid) {
        await update(root, { [`alerts/rules/${rid}`]: body, 'cfg/alertsRev': serverTimestamp() });
        return rid;
      }
      // New rule: claim the first free slot with a transaction (r8 §6.3), so two phones never
      // overwrite each other. With the Alerts listener active the local cache answers "taken"
      // without a round trip; the server re-runs the function if the cache was stale.
      for (const slot of RULE_IDS) {
        const res = await runTransaction(r(`alerts/rules/${slot}`), (cur: unknown) => (cur === null ? body : undefined));
        if (res.committed) {
          await update(root, { 'cfg/alertsRev': serverTimestamp() });
          return slot;
        }
      }
      throw new NoFreeSlotError();
    },
    async setRuleEnabled(rid, on) {
      await update(root, { [`alerts/rules/${rid}/on`]: on, [`alerts/rules/${rid}/upd`]: serverTimestamp(), 'cfg/alertsRev': serverTimestamp() });
    },
    async deleteRule(rid) {
      await update(root, { [`alerts/rules/${rid}`]: null, 'cfg/alertsRev': serverTimestamp() });
    },
    async saveEmailCfg(cfg, rcRemap) {
      const patch: Record<string, unknown> = { 'alerts/email': cfg, 'cfg/alertsRev': serverTimestamp() };
      for (const [rid, rc] of Object.entries(rcRemap)) {
        patch[`alerts/rules/${rid}/rc`] = rc;
        patch[`alerts/rules/${rid}/upd`] = serverTimestamp();
      }
      await update(root, patch);
    },
    async requestTestEmail(rid) {
      // The rules accept ts within ±300 s of the server clock; the offset corrects a wrong phone clock.
      const ts = Math.floor((Date.now() + serverOffsetMs) / 1000);
      await set(r('mailq/test'), rid ? { ts, rid } : { ts });
      return ts;
    },
  };
}
