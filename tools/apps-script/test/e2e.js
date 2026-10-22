// End-to-end: the real Lib.gs + Code.gs against the real RTDB rules in the Firebase Database emulator.
// UrlFetchApp is shimmed with curl (synchronous, like Apps Script). MailApp, Properties and Cache are in-memory.
//
// Run (from the repo root; keeps the emulator JAR cache out of your home directory):
//   HOME=<scratch> npx --yes firebase-tools@15.32.1 emulators:exec --only database --project demo-savx \
//     --config tools/apps-script/test/firebase.emu.json "node tools/apps-script/test/e2e.js"
// RULES=<path> picks another rules file (default firebase/database.rules.json; it must contain MAILER_UID,
// i.e. r8 section 3.11 must be merged). Without the emulator (FIREBASE_DATABASE_EMULATOR_HOST unset) it skips.
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const { execFileSync } = require('child_process');
const { loadGs, DIR } = require('./_load');

const HOST = process.env.FIREBASE_DATABASE_EMULATOR_HOST;
if (!HOST) {
  console.log('SKIP e2e: FIREBASE_DATABASE_EMULATOR_HOST is not set (run it under "firebase emulators:exec", see the header).');
  process.exit(0);
}
const DB = 'http://' + HOST;
const NS = (process.env.GCLOUD_PROJECT || 'demo-savx') + '-default-rtdb';
const RULES = process.env.RULES || path.join(DIR, '..', '..', 'firebase', 'database.rules.json');

const b64 = (o) => Buffer.from(JSON.stringify(o)).toString('base64url');
const tok = (uid) => `${b64({ alg: 'none', typ: 'JWT' })}.${b64({ sub: uid, user_id: uid, iat: 1, exp: 9999999999, aud: 'demo-savx', iss: 'https://securetoken.google.com/demo-savx' })}.`;
function curl(method, url, body, ctype, headers) {
  const args = ['-s', '-o', '-', '-w', '\n%{http_code}', '-X', method.toUpperCase(), url];
  for (const h of headers || []) args.push('-H', h);
  if (body !== undefined) args.push('-H', 'Content-Type: ' + (ctype || 'application/json'), '--data-binary', body);
  const out = execFileSync('curl', args).toString();
  const i = out.lastIndexOf('\n');
  return { code: Number(out.slice(i + 1)), text: out.slice(0, i) };
}
const admin = (method, p, body) => {
  const r = curl(method, `${DB}/devices/inv1${p ? '/' + p : ''}.json?ns=${NS}`, body === undefined ? undefined : JSON.stringify(body), null, ['Authorization: Bearer owner']);
  return JSON.parse(r.text || 'null');
};
const as = (who, method, p, body) => curl(method, `${DB}/devices/inv1${p ? '/' + p : ''}.json?ns=${NS}&auth=${tok(who)}`, body === undefined ? undefined : JSON.stringify(body));

// 1. rules: render the placeholders with the emulator's test UIDs and load them
let rules = fs.readFileSync(RULES, 'utf8');
if (!rules.includes('MAILER_UID')) {
  console.error('e2e: ' + RULES + ' has no MAILER_UID placeholder. Merge r8 section 3.11 first, or set RULES=<merged rules>.');
  process.exit(1);
}
rules = rules.replace(/OWNER_UID/g, 'owner1').replace(/DEVICE_UID/g, 'device1').replace(/MAILER_UID/g, 'mailer1');
let r = curl('PUT', `${DB}/.settings/rules.json?ns=${NS}`, rules, 'application/json', ['Authorization: Bearer owner']);
assert.strictEqual(r.code, 200, 'load rules: ' + r.text);

// 2. the mailer, with the real rules behind its UrlFetchApp
const sent = [], cache = {};
const props = { FB_API_KEY: 'AIzaFAKE', FB_DB_URL: DB, FB_EMAIL: 'mailer@x.invalid', FB_PASSWORD: 'pw', ALLOW_TO: 'owner@gmail.com', APP_URL: 'https://savx.example.workers.dev' };
let quota = 100;
const gs = loadGs(['Lib.gs', 'Code.gs'], {
  UrlFetchApp: { fetch(url, o) {
    if (/identitytoolkit|securetoken/.test(url)) {
      return { getResponseCode: () => 200, getContentText: () => JSON.stringify({ idToken: tok('mailer1'), id_token: tok('mailer1'), refreshToken: 'rt', refresh_token: 'rt' }) };
    }
    const x = curl(o.method, url + '&ns=' + NS, o.payload, o.contentType);
    return { getResponseCode: () => x.code, getContentText: () => x.text };
  } },
  PropertiesService: { getScriptProperties: () => ({ getProperty: (k) => (k in props ? props[k] : null), setProperty: (k, v) => (props[k] = String(v)), deleteProperty: (k) => delete props[k] }) },
  CacheService: { getScriptCache: () => ({ get: (k) => (k in cache ? cache[k] : null), put: (k, v) => (cache[k] = String(v)), remove: (k) => delete cache[k] }) },
  LockService: { getScriptLock: () => ({ tryLock: () => true, releaseLock() {} }) },
  MailApp: { getRemainingDailyQuota: () => quota, sendEmail(m) { quota -= m.to.split(',').length; sent.push(m); } },
  ContentService: { createTextOutput: (s) => ({ s, setMimeType() { return this; } }), MimeType: { JSON: 'json' } },
  ScriptApp: { requireAllScopes() {}, AuthMode: { FULL: 1 }, getProjectTriggers: () => [], deleteTrigger() {}, newTrigger: () => ({ timeBased: () => ({ everyMinutes: () => ({ create() {} }) }) }) },
  Logger: { log: (s) => console.log('  log: ' + s) },
});

// 3. seed as admin, then the owner and the device write through the rules
const now = Math.floor(Date.now() / 1000);
const dkey = gs.dkey_(now), yday = gs.addDays_(dkey, -1);
admin('PUT', '', { live: { ts: now - 5, lk: 1, soc: 78, bv: 26.4, bw: 300, pvw: 400, epv: 1900, w: 500, eld: 2000, gv: 230, egr: 300, md: 'L' },
  daily: { [yday]: { epv: 12840, eld: 9710, egr: 1220, n: 1440 } } });
// the same multi-path write as README section 7.2 (and the PWA, r8 section 3.10)
r = as('owner1', 'PATCH', '', { 'alerts/email': { on: true, to: 'owner@gmail.com', fromName: 'Solar logger', maxPerDay: 40, monthStart: 1,
  builtin: { minSev: 4, clear: true }, summary: { on: true, hour: 0 }, offline: { on: true, min: 20, clear: true } }, 'cfg/alertsRev': { '.sv': 'timestamp' } });
assert.strictEqual(r.code, 200, 'owner writes alerts/email + cfg/alertsRev ' + r.text);
r = as('owner1', 'PATCH', '', { 'alerts/rules/r03': { name: 'Daily grid usage > 5 kWh', on: true, metric: 'egr', scope: 'day', op: 'gt', value: 5000, mode: 'hold', forSec: 0, sev: 3, email: true, ntfy: false, upd: { '.sv': 'timestamp' } }, 'cfg/alertsRev': { '.sv': 'timestamp' } });
assert.strictEqual(r.code, 200, 'owner saves a rule ' + r.text);
const k1 = `${now - 30}-r03-o`, k2 = `${now - 20}-GRID_LOSS-o`;
r = as('device1', 'PATCH', '', {
  [`events/${dkey}/${k1}`]: { ts: now - 30, type: 'RULE', sev: 3, st: 'open', rid: 'r03', val: 5010, thr: 5000, msg: 'Daily grid usage > 5 kWh: now 5.01 kWh', ch: 'email', em: 'q' },
  [`mailq/${k1}`]: { d: dkey, ts: now - 30, sev: 3 },
  [`events/${dkey}/${k2}`]: { ts: now - 20, type: 'GRID_LOSS', sev: 4, st: 'open', msg: 'Grid lost, on battery (SOC 78%)', ch: 'ntfy,email', em: 'q' },
  [`mailq/${k2}`]: { d: dkey, ts: now - 20, sev: 4 } });
assert.strictEqual(r.code, 200, 'device multi-path PATCH ' + r.text);
r = as('owner1', 'PUT', 'mailq/test', { ts: now, rid: 'r03' });
assert.strictEqual(r.code, 200, 'owner test request ' + r.text);

// 4. setup() = sign-in, access check, first run
gs.setup();
let db = admin('GET', '');
assert.strictEqual(db.events[dkey][k1].em, 'sent');
assert.strictEqual(db.events[dkey][k2].em, 'sent');
assert.strictEqual(db.events[dkey][k1].emt >= now, true);
assert.strictEqual(db.mailq, undefined, 'queue empty');
assert.strictEqual(db.mailer.test.ok, true);
assert.strictEqual(db.mailer.ver, '1.0.0');
assert.strictEqual(db.mailer.err, '');
assert(db.mailer.hb >= now);
console.log('  mails:', sent.map((m) => m.subject));
assert.strictEqual(sent.length, 3);   // test + digest(2) + summary
assert(sent.some((m) => m.subject === '[Solar] [TEST] Daily grid usage > 5 kWh'));
assert(sent.some((m) => m.subject.startsWith('[Solar] 2 alerts:')));
assert(sent.some((m) => m.subject.includes(': PV 12.84 kWh')));

// 5. device retries the same job after a lost reply -> no new mail, em restored to sent
r = as('device1', 'PATCH', '', { [`events/${dkey}/${k1}/em`]: 'q', [`mailq/${k1}`]: { d: dkey, ts: now - 30, sev: 3 } });
assert.strictEqual(r.code, 200);
gs.poll();
assert.strictEqual(sent.length, 3);
assert.strictEqual(admin('GET', `events/${dkey}/${k1}/em`), 'sent');

// 6. job whose event is missing: rules refuse em -> finish_ falls back to deleting the job
r = as('device1', 'PUT', `mailq/${now}-r09-o`, { d: dkey, ts: now, sev: 3 });
assert.strictEqual(r.code, 200);
gs.poll();
assert.strictEqual(admin('GET', 'mailq'), null);

// 7. the rules really pin the mailer (what Code.gs never attempts must be refused)
assert.strictEqual(as('mailer1', 'GET', 'cfg').code, 401, 'mailer cannot read cfg');
assert.strictEqual(as('mailer1', 'PUT', `events/${dkey}/${k1}/msg`, 'x').code, 401, 'mailer cannot edit an event');
assert.strictEqual(as('mailer1', 'PUT', 'alerts/email/to', 'evil@example.com').code, 401, 'mailer cannot change recipients');
db = admin('GET', 'mailer');
console.log('E2E OK: rules + Code.gs agree (' + sent.length + ' mails, heartbeat ' + JSON.stringify(db).slice(0, 110) + '...)');
