// Fake Apps Script services + a fake RTDB (with the mailer's slice of the r8 section 3.11 rules) for Code.gs + Lib.gs.
// Ported from the decide-phase harness (r8 section 8.1, "18/18 pass"). Utilities is deliberately NOT provided.
'use strict';
const assert = require('assert');
const { loadGs } = require('./_load');

const MAILER_READ = /^(mailq|alerts|live|daily|events|mailer)(\/|$)/;   // r8 section 1.4: what MAILER_UID may read

function makeEnv(opts) {
  opts = opts || {};
  const env = {
    now: opts.now || Date.UTC(2026, 9, 21, 8, 0, 0),               // 14:00 Asia/Dhaka
    sent: [], fetches: [], db: { devices: { inv1: {} } },
    props: Object.assign({
      FB_API_KEY: 'AIzaFAKE', FB_DB_URL: 'https://p-default-rtdb.asia-southeast1.firebasedatabase.app',
      FB_EMAIL: 'mailer@x.invalid', FB_PASSWORD: 'pw', ALLOW_TO: 'owner@gmail.com,family@gmail.com',
      APP_URL: 'https://savx.example.workers.dev', NUDGE_KEY: 'k'.repeat(32) }, opts.props || {}),
    cache: {}, quota: opts.quota === undefined ? 100 : opts.quota, mailThrows: 0, tokenValid: 'tok1', tokenCalls: 0,
    propWrites: 0, triggers: [], locked: false, log: [],
  };
  const dev = () => env.db.devices.inv1;
  const getPath = (p) => p.split('/').filter(Boolean).reduce((o, k) => (o && typeof o === 'object' ? o[k] : undefined), dev());
  const setPath = (p, v) => {
    const parts = p.split('/').filter(Boolean);
    if (!parts.length) throw new Error('root set');
    let o = dev();
    for (let i = 0; i < parts.length - 1; i++) {
      if (typeof o[parts[i]] !== 'object' || o[parts[i]] === null) o[parts[i]] = {};
      o = o[parts[i]];
    }
    if (v === null) delete o[parts[parts.length - 1]];
    else o[parts[parts.length - 1]] = JSON.parse(JSON.stringify(v));
    for (let n = parts.length - 1; n > 0; n--) {                     // RTDB has no empty objects: prune empty parents
      const par = parts.slice(0, n - 1).reduce((x, k) => x[k], dev());
      if (par[parts[n - 1]] && typeof par[parts[n - 1]] === 'object' && !Object.keys(par[parts[n - 1]]).length) delete par[parts[n - 1]];
    }
  };
  // The mailer's write grant: mailq, mailer, and em/emt of an EXISTING event only.
  const writable = (p) => {
    const m = p.match(/^events\/([^/]+)\/([^/]+)\/(em|emt)$/);
    if (m) return !!getPath('events/' + m[1] + '/' + m[2]);
    return /^(mailq(\/|$)|mailer(\/|$))/.test(p);
  };
  const resp = (code, body) => ({ getResponseCode: () => code, getContentText: () => (body === undefined ? '' : body) });
  const UrlFetchApp = {
    fetch(url, o) {
      env.fetches.push({ url, method: o.method });
      if (url.startsWith('https://securetoken.googleapis.com/')) { env.tokenCalls++; return resp(200, JSON.stringify({ id_token: env.tokenValid, refresh_token: 'rt1' })); }
      if (url.startsWith('https://identitytoolkit.googleapis.com/')) {
        env.tokenCalls++;
        if (JSON.parse(o.payload).password !== env.props.FB_PASSWORD || env.badPassword) return resp(400, '{"error":{"message":"INVALID_LOGIN_CREDENTIALS"}}');
        return resp(200, JSON.stringify({ idToken: env.tokenValid, refreshToken: 'rt1' }));
      }
      const u = new URL(url);
      assert(u.pathname.startsWith('/devices/inv1'), 'path ' + u.pathname);
      if (env.rejectAll || u.searchParams.get('auth') !== env.tokenValid) return resp(401, '{"error":"Auth token is expired"}');
      const p = decodeURIComponent(u.pathname.replace(/^\/devices\/inv1\/?/, '').replace(/\.json$/, ''));
      const silent = u.searchParams.get('print') === 'silent';
      if (o.method === 'get') {
        assert(!silent, 'GET must not use print=silent');
        if (!MAILER_READ.test(p)) return resp(401, '{"error":"Permission denied"}');
        let v = getPath(p);
        if (u.searchParams.get('orderBy') === '"$key"' && v) {
          const a = JSON.parse(u.searchParams.get('startAt')), b = JSON.parse(u.searchParams.get('endAt'));
          const r = {};
          Object.keys(v).filter((k) => k >= a && k <= b).forEach((k) => (r[k] = v[k]));
          v = r;
        }
        return resp(200, JSON.stringify(v === undefined ? null : v));
      }
      assert(silent, 'writes use print=silent');
      const body = o.payload === undefined ? null : JSON.parse(o.payload);
      if (o.method === 'patch') {
        const entries = p === '' ? Object.entries(body) : Object.entries(body).map(([k, v]) => [p + '/' + k, v]);
        for (const [k] of entries) if (!writable(k)) return resp(401, '{"error":"Permission denied"}');
        for (const [k, v] of entries) setPath(k, v);
        return resp(204);
      }
      if (!writable(p)) return resp(401, '{"error":"Permission denied"}');
      if (o.method === 'put') { setPath(p, body); return resp(204); }
      if (o.method === 'delete') { setPath(p, null); return resp(204); }
      throw new Error('method ' + o.method);
    },
  };
  const store = env.props;
  class FakeDate extends Date {
    constructor(...a) { if (a.length) super(...a); else super(env.now); }
    static now() { return env.now; }
  }
  const globals = {
    Date: FakeDate,
    UrlFetchApp,
    PropertiesService: { getScriptProperties: () => ({
      getProperty: (k) => (k in store ? store[k] : null),
      setProperty: (k, v) => { assert(String(v).length <= 9216, 'prop too long ' + k); env.propWrites++; store[k] = String(v); },
      deleteProperty: (k) => delete store[k] }) },
    CacheService: { getScriptCache: () => ({ get: (k) => (k in env.cache ? env.cache[k] : null), put: (k, v) => (env.cache[k] = String(v)), remove: (k) => delete env.cache[k] }) },
    LockService: { getScriptLock: () => ({ tryLock: () => !env.locked, releaseLock: () => {} }) },
    MailApp: {
      getRemainingDailyQuota: () => env.quota,
      sendEmail(m) {
        if (env.mailThrows > 0) { env.mailThrows--; throw new Error('Service invoked too many times for one day: email.'); }
        assert(m.to && m.subject && m.body && m.htmlBody, 'mail fields');
        assert(!/[\r\n]/.test(m.subject), 'CRLF in subject');
        assert(m.subject.length <= 120, 'subject <= 120');
        env.quota -= m.to.split(',').length;
        env.sent.push(m);
      },
    },
    ContentService: { createTextOutput: (s) => ({ s, setMimeType() { return this; } }), MimeType: { JSON: 'json' } },
    ScriptApp: {
      requireAllScopes() {}, AuthMode: { FULL: 'FULL' },
      getProjectTriggers: () => env.triggers.map((h) => ({ getHandlerFunction: () => h })),
      deleteTrigger: (t) => { env.triggers.splice(env.triggers.indexOf(t.getHandlerFunction()), 1); },
      newTrigger: (h) => ({ timeBased: () => ({ everyMinutes: (n) => {
        assert([1, 5, 10, 15, 30].includes(n), 'everyMinutes allows 1, 5, 10, 15, 30');
        return { create() { env.triggers.push(h); env.trigger = n; } };
      } }) }),
    },
    Logger: { log: (s) => env.log.push(String(s)) },
  };
  env.gs = loadGs(['Lib.gs', 'Code.gs'], globals);
  env.getPath = getPath;
  env.setPath = setPath;
  env.tick = (sec) => { env.now += sec * 1000; };
  env.nowS = () => Math.floor(env.now / 1000);
  return env;
}

function addEvent(env, d, k, ev, rc) {
  env.setPath('events/' + d + '/' + k, Object.assign({ em: 'q' }, ev));
  const it = { d, ts: ev.ts, sev: ev.sev };
  if (rc) it.rc = rc;
  env.setPath('mailq/' + k, it);
}

function baseCfg(env, extra) {
  env.setPath('alerts/email', Object.assign({ on: true, to: 'owner@gmail.com,family@gmail.com', fromName: 'Solar logger', maxPerDay: 40,
    summary: { on: false, hour: 7 }, offline: { on: true, min: 20, clear: true }, monthStart: 1 }, extra || {}));
  env.setPath('live', { ts: env.nowS() - 10, lk: 1, soc: 78, bv: 26.42, bw: 317, pvw: 465, epv: 1920, w: 512, eld: 2080, gv: 231.4, egr: 330, md: 'L' });
}

module.exports = { makeEnv, addEvent, baseCfg };
