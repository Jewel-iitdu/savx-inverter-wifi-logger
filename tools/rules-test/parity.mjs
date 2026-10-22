// Parity fuzz: the PWA's client-side validators (web/src/data/alerts.ts, bundled to ./alerts.mjs by
// run.sh) must accept exactly what firebase/database.rules.json accepts. ~5,400 rule and e-mail
// documents (random, plus near-boundary mutations of the presets) are PUT into the emulator as the
// owner and compared with validateRule() / validateEmailCfg(). Deterministic (fixed seed).
import { validateRule, validateEmailCfg, PRESETS } from './alerts.mjs';
const NS = 'demo-savx-default-rtdb', BASE = 'http://127.0.0.1:9123';
const b64 = (o) => Buffer.from(JSON.stringify(o)).toString('base64url');
const tok = (uid) => `${b64({ alg: 'none', typ: 'JWT' })}.${b64({ sub: uid, user_id: uid, uid, iat: 1, exp: 9999999999, aud: 'demo-savx', iss: 'https://securetoken.google.com/demo-savx', firebase: { sign_in_provider: 'password' } })}.`;
const OWNER = tok('owner1');
async function put(path, body) {
  const r = await fetch(`${BASE}/devices/inv1/${path}.json?ns=${NS}&auth=${OWNER}`, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  return r.status;
}
let seed = 12345; const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
const pick = (a) => a[Math.floor(rnd() * a.length)];
const maybe = (p) => rnd() < p;
const METRICS = ['w','pvw','gw','bdw','bw','soc','ld','bv','gv','ov','pvv','bdi','tmp','gf','onbat','nogrid','eld','egr','epv','edis','ech','gout'];
const VALS = [-20000,-10000,-5,0,0.5,1,2,30,49.5,70,150,151,250,300,600,601,2500,20000,20001,44640,44641,5000000,5000001];
function rule() {
  const r = {};
  if (maybe(0.97)) r.name = pick(['Load > 2500 W', 'x', '', 'a'.repeat(40), 'a'.repeat(41), 'tab\there', 'সোলার', 'new\nline']);
  if (maybe(0.97)) r.on = pick([true, false, 1]);
  if (maybe(0.97)) r.metric = pick([...METRICS, ...METRICS, 'xyz']);
  if (maybe(0.97)) r.scope = pick(['inst', 'day', 'month', 'inst', 'day', 'week']);
  if (maybe(0.97)) r.op = pick(['gt', 'ge', 'lt', 'le', 'eq']);
  if (maybe(0.97)) r.value = pick([...VALS, '5']);
  if (maybe(0.97)) r.mode = pick(['hold', 'hold', 'at', 'once']);
  if (maybe(0.4)) r.at = pick(['1700', '2400', '0000', '17:00']);
  if (maybe(0.6)) r.forSec = pick([0, 60, 86400, 86401, 1.5, -1]);
  if (maybe(0.4)) r.clr = pick([...VALS]);
  if (maybe(0.4)) r.clrSec = pick([0, 120, 3600, 3601]);
  if (maybe(0.5)) r.coolMin = pick([4, 5, 60, 10080, 10081]);
  if (maybe(0.3)) r.repeatMin = pick([0, 10, 29, 30, 1440, 1441]);
  if (maybe(0.5)) r.maxDay = pick([0, 1, 10, 11]);
  if (maybe(0.3)) r.minCov = pick([0, 80, 100, 101]);
  if (maybe(0.3)) r.win = pick([{ from: '0600', to: '2300', days: 127 }, { from: '2300', to: '0600', days: 31 }, { from: '0600', to: '0600', days: 1 }, { from: '0600', to: '2300', days: 0 }, { from: '0600', to: '2300', days: 128 }, { from: '0600', to: '2300' }, { from: '0600', to: '2300', days: 1, x: 1 }]);
  if (maybe(0.97)) r.sev = pick([0, 1, 3, 5, 6, 2.5]);
  if (maybe(0.97)) r.email = pick([true, false]);
  if (maybe(0.97)) r.ntfy = pick([true, false]);
  if (maybe(0.3)) r.rc = pick([0, 1, 7, 8]);
  if (maybe(0.3)) r.clrMail = pick([true, false, 'yes']);
  if (maybe(0.03)) r.evil = 1;
  if (maybe(0.97)) r.upd = maybe(0.95) ? 'SV' : Date.now() + 3600000;
  return r;
}
function email() {
  const c = {};
  if (maybe(0.95)) c.on = pick([true, false, 'on']);
  if (maybe(0.8)) c.to = pick(['', 'a@b.cc', 'owner@gmail.com,family.member@gmail.com', 'a@b.cc,c@d.ee,e@f.gg', 'a@b.cc,c@d.ee,e@f.gg,h@i.jj', 'a@b', 'a@b.cc, c@d.ee', 'x'.repeat(290) + '@b.cc']);
  if (maybe(0.6)) c.fromName = pick(['Solar logger', '', 'x'.repeat(41), 'a\nb', 'সোলার লগার']);
  if (maybe(0.6)) c.maxPerDay = pick([0, 1, 40, 41, 2.5]);
  if (maybe(0.6)) c.monthStart = pick([0, 1, 28, 29]);
  if (maybe(0.5)) c.builtin = pick([{ minSev: 4, clear: true }, { minSev: 2 }, { minSev: 0 }, { clear: 'x' }, { minSev: 5, x: 1 }, {}]);
  if (maybe(0.5)) c.summary = pick([{ on: true, hour: 7 }, { on: true }, { on: true, hour: 24 }, { on: false, hour: 0, x: 1 }]);
  if (maybe(0.5)) c.offline = pick([{ on: true, min: 30, clear: true }, { on: true, min: 9 }, { on: true }, { on: false, min: 240 }, { min: 30 }]);
  if (maybe(0.03)) c.evil = 1;
  return c;
}
const sv = (o) => JSON.parse(JSON.stringify(o).replace(/"SV"/g, '{".sv":"timestamp"}'));
const local = (o) => JSON.parse(JSON.stringify(o).replace(/"SV"/g, '{}'));
let agree = 0, disagree = 0, okCount = 0;
async function check(kind, doc, clientErrs, path) {
  const isEmpty = (v) => v && typeof v === 'object' && Object.keys(v).length === 0;
  const code = await put(path, sv(doc));
  // An empty object is stored as "nothing" (a delete) by RTDB: skip those.
  if (isEmpty(doc)) return;
  const server = code === 200, client = clientErrs.length === 0;
  if (server) okCount++;
  if (server === client) agree++; else { disagree++; if (disagree <= 15) console.log('DISAGREE', kind, code, JSON.stringify(doc), clientErrs); }
}
for (const p of PRESETS) {
  const doc = { ...p.rule, upd: 'SV' };
  await check('preset', doc, validateRule('r02', local(doc)), 'alerts/rules/r02');
}
for (let i = 0; i < 1500; i++) { const d = rule(); await check('rule', d, validateRule('r02', local(d)), 'alerts/rules/r02'); }
for (let i = 0; i < 600; i++) { const d = email(); await check('email', d, validateEmailCfg(d), 'alerts/email'); }
// near-boundary mutations of valid presets (mostly accepted)
const MUT = {
  name: ['Short', 'a'.repeat(40), 'a'.repeat(41), 'line\nbreak'],
  value: [...VALS], clr: [...VALS], forSec: [0, 30, 86400, 86401], clrSec: [0, 3600, 3601], coolMin: [5, 4, 10080],
  repeatMin: [0, 30, 1440, 29], maxDay: [1, 10, 11], minCov: [0, 100, 101], sev: [1, 5, 0], rc: [1, 3, 7, 8],
  op: ['gt', 'ge', 'lt', 'le'], mode: ['hold', 'at'], at: ['0000', '2359', '2360'], scope: ['inst', 'day', 'month'],
  metric: METRICS, email: [true, false], ntfy: [true, false], clrMail: [true, false],
  win: [{ from: '2300', to: '0600', days: 96 }, { from: '0000', to: '2359', days: 127 }, { from: '1200', to: '1200', days: 1 }],
};
for (let i = 0; i < 2500; i++) {
  const base = JSON.parse(JSON.stringify(pick(PRESETS).rule));
  const n = Math.floor(rnd() * 3);
  for (let k = 0; k < n; k++) { const f = pick(Object.keys(MUT)); if (maybe(0.15)) delete base[f]; else base[f] = pick(MUT[f]); }
  const d = { ...base, upd: 'SV' };
  await check('mut', d, validateRule('r03', local(d)), 'alerts/rules/r03');
}
const EBASE = { on: true, to: 'owner@gmail.com,family.member@gmail.com', fromName: 'Solar logger', maxPerDay: 40, monthStart: 1, builtin: { minSev: 4, clear: true }, summary: { on: true, hour: 7 }, offline: { on: true, min: 30, clear: true } };
const EMUT = { on: [true, false], to: ['', 'a@b.cc', 'a@b.cc,c@d.ee,e@f.gg,h@i.jj', 'bad'], fromName: ['x', 'x'.repeat(40), 'x'.repeat(41)], maxPerDay: [1, 40, 41], monthStart: [1, 28, 29],
  builtin: [{ minSev: 0, clear: false }, { minSev: 3 }, { minSev: 1 }], summary: [{ on: false, hour: 23 }, { on: true, hour: -1 }], offline: [{ on: true, min: 10 }, { on: true, min: 241, clear: true }] };
for (let i = 0; i < 800; i++) {
  const base = JSON.parse(JSON.stringify(EBASE));
  const n = Math.floor(rnd() * 3);
  for (let k = 0; k < n; k++) { const f = pick(Object.keys(EMUT)); if (maybe(0.15)) delete base[f]; else base[f] = pick(EMUT[f]); }
  await check('emut', base, validateEmailCfg(base), 'alerts/email');
}
// slot keys
for (const rid of ['r00', 'r01', 'r16', 'r17', 'x1']) { const d = { ...PRESETS[1].rule, upd: 'SV' }; await check('slot', d, validateRule(rid, local(d)), `alerts/rules/${rid}`); }
console.log(`parity: ${agree} agree, ${disagree} disagree (${okCount} accepted by the server)`);
process.exit(disagree ? 1 : 0);
