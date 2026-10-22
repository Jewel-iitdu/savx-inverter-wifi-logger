// Code.gs + Lib.gs against fake Apps Script services and a fake RTDB (test/_mocks.js).
// Cases 1-18 are the decide-phase harness of r8 section 8.1; the rest cover this implementation's additions.
'use strict';
const test = require('node:test');
const assert = require('assert');
const { makeEnv, addEvent, baseCfg } = require('./_mocks');

const D = '2026-10-21';

test('01 setup installs one 5-min trigger, signs in and writes the heartbeat', () => {
  const env = makeEnv(); baseCfg(env);
  env.triggers.push('poll');                              // an old trigger is replaced, not duplicated
  env.gs.setup();
  assert.strictEqual(env.trigger, 5);
  assert.deepStrictEqual(env.triggers, ['poll']);
  assert.strictEqual(env.props.RTOK, 'rt1');
  assert(env.getPath('mailer/hb') > 0);
  assert.strictEqual(env.getPath('mailer/ver'), '1.0.0');
  assert.match(env.log.join('\n'), /OK: signed in to RTDB, trigger "poll" installed/);
});

test('02 one rule event -> one mail to both recipients, em=sent, queue empty', () => {
  const env = makeEnv(); baseCfg(env);
  env.setPath('alerts/rules/r03', { name: 'Daily grid usage > 5 kWh', metric: 'egr', scope: 'day' });
  const ts = env.nowS() - 30;
  addEvent(env, D, ts + '-r03-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r03', val: 5010, thr: 5000, msg: 'Daily grid usage > 5 kWh: now 5.01 kWh (est.)' });
  env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  const m = env.sent[0];
  assert.strictEqual(m.to, 'owner@gmail.com,family@gmail.com');
  assert.strictEqual(m.subject, '[Solar] Daily grid usage > 5 kWh: now 5.01 kWh (est.)');
  assert(m.htmlBody.includes('Open dashboard') && m.body.includes('Right now:'));
  assert(m.body.includes('Value: 5.01 kWh, limit 5.00 kWh [Grid energy, today, est.]'));
  assert.strictEqual(m.replyTo, 'owner@gmail.com');
  assert.strictEqual(m.name, 'Solar logger');
  assert.strictEqual(env.getPath('events/' + D + '/' + ts + '-r03-o/em'), 'sent');
  assert.strictEqual(env.getPath('events/' + D + '/' + ts + '-r03-o/emt'), env.nowS());
  assert.strictEqual(env.getPath('mailq'), undefined);
  assert.strictEqual(env.getPath('mailer/sent'), 2);
  assert.strictEqual(env.getPath('mailer/last/ok'), true);
});

test('03 device re-writes an already sent job (lost reply) -> no second mail', () => {
  const env = makeEnv(); baseCfg(env);
  const ts = env.nowS() - 30, k = ts + '-r01-o';
  const ev = { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'Load > 2500 W for 5 min: now 2,710 W' };
  addEvent(env, D, k, ev);
  env.gs.poll();
  addEvent(env, D, k, ev);                                // retry overwrote em back to q
  env.tick(300); env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  assert.strictEqual(env.getPath('events/' + D + '/' + k + '/em'), 'sent');
  assert.strictEqual(env.getPath('mailq'), undefined);
});

test('04 two jobs, same recipients -> one digest; the recipient mask splits groups', () => {
  const env = makeEnv(); baseCfg(env);
  const ts = env.nowS() - 60;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'Load > 2500 W' });
  addEvent(env, D, (ts + 5) + '-GRID_LOSS-o', { ts: ts + 5, type: 'GRID_LOSS', sev: 4, st: 'open', msg: 'Grid lost, on battery (SOC 78%)' });
  addEvent(env, D, (ts + 9) + '-r05-o', { ts: ts + 9, type: 'RULE', sev: 4, st: 'open', rid: 'r05', msg: 'Inverter temp > 70 C' }, 1);
  env.gs.poll();
  assert.strictEqual(env.sent.length, 2);
  assert(env.sent[0].subject.startsWith('[Solar] 2 alerts: Load > 2500 W; Grid lost'));
  assert.strictEqual(env.sent[1].to, 'owner@gmail.com');
  assert.strictEqual(env.sent[1].subject, '[Solar] ALERT: Inverter temp > 70 C');
  assert.strictEqual(env.getPath('mailer/sent'), 3);
});

test('05 clear and repeat prefixes; delayed line', () => {
  const env = makeEnv(); baseCfg(env);
  const ts = env.nowS() - 3600;
  addEvent(env, D, ts + '-r02-c', { ts, type: 'RULE', sev: 4, st: 'clear', rid: 'r02', dur: 7500, msg: 'SOC < 30 %: back to 36 %' }, 1);
  env.gs.poll();
  assert.strictEqual(env.sent[0].subject, '[Solar] Cleared: SOC < 30 %: back to 36 %');
  assert(env.sent[0].body.includes('Delayed 1 h 00 min'));
  assert(env.sent[0].body.includes('It lasted 2 h 05 min.'));
  env.tick(60);
  const t2 = env.nowS() - 5;
  addEvent(env, D, t2 + '-r01-r', { ts: t2, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'Load > 2500 W for 5 min: still 2,710 W for 1 h 10 min' });
  env.gs.poll();
  assert.strictEqual(env.sent[1].subject, '[Solar] Still active: Load > 2500 W for 5 min: still 2,710 W for 1 h 10 min');
});

test('06 cap: 45 sev-3 jobs to 1 recipient -> 35 + 1 cap notice, the rest em=cap; sev 4 from the reserve', () => {
  const env = makeEnv(); baseCfg(env, { to: 'owner@gmail.com' });
  const ts0 = env.nowS() - 3000;
  for (let i = 0; i < 45; i++) {
    const ts = ts0 + i * 60;
    addEvent(env, D, ts + '-r0' + (1 + (i % 9)) + '-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r0' + (1 + (i % 9)), msg: 'm' + i });
    env.gs.poll();                                        // one job per run -> no digest
  }
  assert.strictEqual(env.sent.filter((m) => m.subject.includes('limit reached')).length, 1);
  assert.strictEqual(env.sent.length, 36);
  assert(env.getPath('mailer/sent') <= 40);
  const ems = Object.values(env.getPath('events/' + D)).map((e) => e.em);
  assert.strictEqual(ems.filter((e) => e === 'cap').length, 10);
  const ts = env.nowS();
  addEvent(env, D, ts + '-BATT_LOW-o', { ts, type: 'BATT_LOW', sev: 4, st: 'open', msg: 'Battery low' });
  env.gs.poll();
  assert.strictEqual(env.sent[env.sent.length - 1].subject, '[Solar] ALERT: Battery low');
  assert(env.getPath('mailer/sent') <= 40);
});

test('07 recipient not on ALLOW_TO -> filtered + warn; none left -> norc', () => {
  const env = makeEnv({ props: { ALLOW_TO: 'owner@gmail.com' } }); baseCfg(env, { to: 'owner@gmail.com,stranger@evil.com' });
  const ts = env.nowS() - 5;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  addEvent(env, D, (ts + 1) + '-r02-o', { ts: ts + 1, type: 'RULE', sev: 3, st: 'open', rid: 'r02', msg: 'y' }, 2);
  env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  assert.strictEqual(env.sent[0].to, 'owner@gmail.com');
  assert.strictEqual(env.getPath('events/' + D + '/' + (ts + 1) + '-r02-o/em'), 'norc');
  assert(env.getPath('mailer/warn').includes('stranger@evil.com'));
});

test('08 alerts off -> em=off; expired job -> exp; missing event -> queue item dropped', () => {
  const env = makeEnv(); baseCfg(env, { on: false });
  const ts = env.nowS() - 5;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  env.gs.poll();
  assert.strictEqual(env.getPath('events/' + D + '/' + ts + '-r01-o/em'), 'off');
  const env2 = makeEnv(); baseCfg(env2);
  const old = env2.nowS() - 13 * 3600;
  addEvent(env2, '2026-10-20', old + '-r01-o', { ts: old, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  env2.setPath('mailq/1792000000-r09-o', { d: D, ts: 1792000000, sev: 3 });
  env2.gs.poll();
  assert.strictEqual(env2.getPath('events/2026-10-20/' + old + '-r01-o/em'), 'exp');
  assert.strictEqual(env2.getPath('mailq'), undefined);
  assert.strictEqual(env2.sent.length, 0);
});

test('09 MailApp failure -> retry counter, fail after 5 tries', () => {
  const env = makeEnv(); baseCfg(env);
  env.mailThrows = 99;
  const ts = env.nowS() - 5, k = ts + '-r01-o';
  addEvent(env, D, k, { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  for (let i = 0; i < 4; i++) { env.gs.poll(); env.tick(300); env.setPath('live/ts', env.nowS() - 10); }
  assert.strictEqual(env.getPath('mailq/' + k + '/n'), 4);
  assert(env.getPath('mailer/err').startsWith('MailApp:'));
  env.gs.poll();
  assert.strictEqual(env.getPath('events/' + D + '/' + k + '/em'), 'fail');
  assert.strictEqual(env.getPath('mailq'), undefined);
});

test('10 test e-mail: sent, result written, rate limited within 60 s; test of a rule', () => {
  const env = makeEnv(); baseCfg(env);
  env.setPath('mailq/test', { ts: env.nowS() - 20 });
  env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  assert(env.sent[0].subject.includes('Test e-mail'));
  assert.strictEqual(env.getPath('mailer/test/ok'), true);
  assert.strictEqual(env.getPath('mailer/test/msg'), 'sent to 2 recipients');
  assert.strictEqual(env.getPath('mailq'), undefined);
  env.tick(30); env.setPath('mailq/test', { ts: env.nowS() });
  env.gs.poll();
  assert.strictEqual(env.getPath('mailer/test/ok'), false);
  assert(env.getPath('mailer/test/msg').includes('one test per minute'));
  env.tick(61); env.setPath('alerts/rules/r03', { name: 'Daily grid usage > 5 kWh', on: true, rc: 1 });
  env.setPath('mailq/test', { ts: env.nowS(), rid: 'r03' });
  env.gs.poll();
  assert.strictEqual(env.sent[1].subject, '[Solar] [TEST] Daily grid usage > 5 kWh');
  assert.strictEqual(env.sent[1].to, 'owner@gmail.com');
});

test('11 logger offline after 20 min -> one mail; back online -> one mail', () => {
  const env = makeEnv(); baseCfg(env, { to: 'owner@gmail.com' });
  env.setPath('live/ts', env.nowS() - 25 * 60);
  env.gs.poll(); env.tick(300); env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  assert(env.sent[0].subject.startsWith('[Solar] ALERT: Logger offline since'));
  assert(env.getPath('mailer/off') > 0);
  env.setPath('live/ts', env.nowS() - 5);
  env.gs.poll();
  assert.strictEqual(env.sent.length, 2);
  assert(env.sent[1].subject.startsWith('[Solar] Logger back online after'));
  assert.strictEqual(env.getPath('mailer/off'), 0);
});

test('12 daily summary at 07:xx local for yesterday, once', () => {
  const env = makeEnv({ now: Date.UTC(2026, 9, 21, 1, 2, 0) });   // 07:02 Asia/Dhaka
  baseCfg(env, { to: 'owner@gmail.com', monthStart: 5, summary: { on: true, hour: 7 } });
  env.setPath('daily/2026-10-20', { epv: 12840, eld: 9710, egr: 1220, ech: 4100, edis: 3050, wx: 1840, pvx: 2410, bvn: 24.9, bvx: 28.6, socn: 31, socx: 100, gout: 42, n: 1440 });
  env.setPath('daily/2026-10-19', { epv: 11900, eld: 9200, egr: 950, n: 1440 });
  env.setPath('daily/2026-10-05', { epv: 10000, eld: 9000, egr: 1000, n: 1440 });
  env.setPath('daily/2026-10-04', { epv: 99999, eld: 99999, egr: 99999, n: 1440 });   // previous cycle
  env.setPath('events/2026-10-20', { a: { ts: 1, type: 'RULE', rid: 'r03', st: 'open', sev: 3, msg: 'x' }, b: { ts: 2, type: 'GRID_LOSS', st: 'open', sev: 4, msg: 'y' } });
  env.gs.poll(); env.tick(300); env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  const m = env.sent[0];
  assert.strictEqual(m.subject, '[Solar] Tue 20 Oct: PV 12.84 kWh | Load 9.71 kWh | Grid 1.22 kWh est.');
  assert(m.body.includes('34.74 kWh'), 'cycle PV 12.84 + 11.90 + 10.00');
  assert(m.body.includes('Solar share of load (est.): 87 %'));
  assert(m.body.includes('r03 x1') && m.body.includes('Grid lost x1'));
  assert(m.body.includes('#/energy'));
});

test('13 no summary before the hour; none when disabled', () => {
  const env = makeEnv({ now: Date.UTC(2026, 9, 21, 0, 30, 0) }); baseCfg(env, { summary: { on: true, hour: 7 } });
  env.gs.poll();
  assert.strictEqual(env.sent.length, 0);
  const env2 = makeEnv(); baseCfg(env2);
  env2.gs.poll();
  assert.strictEqual(env2.sent.length, 0);
});

test('14 expired ID token -> one refresh and retry', () => {
  const env = makeEnv(); baseCfg(env);
  env.gs.poll();
  const calls = env.tokenCalls;
  env.tokenValid = 'tok2';                                // cached tok1 is now rejected with 401
  env.tick(300); env.gs.poll();
  assert.strictEqual(env.tokenCalls, calls + 1);
  assert.strictEqual(env.getPath('mailer/hb'), env.nowS());
});

test('15 doGet nudge: wrong key refused; right key runs; rate-limited; empty NUDGE_KEY refuses all', () => {
  const env = makeEnv(); baseCfg(env);
  assert.strictEqual(JSON.parse(env.gs.doGet({ parameter: { k: 'nope' } }).s).err, 'auth');
  const ts = env.nowS() - 2;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  const r = JSON.parse(env.gs.doGet({ parameter: { k: 'k'.repeat(32) } }).s);
  assert.strictEqual(r.ok, true); assert.strictEqual(r.sent, 2);
  assert.strictEqual(JSON.parse(env.gs.doGet({ parameter: { k: 'k'.repeat(32) } }).s).skipped, 'rate');
  const env2 = makeEnv({ props: { NUDGE_KEY: '' } });
  assert.strictEqual(JSON.parse(env2.gs.doGet({ parameter: { k: '' } }).s).err, 'auth');
  assert.strictEqual(JSON.parse(env2.gs.doGet(undefined).s).err, 'auth');
});

test('16 HTML is escaped, subject has no CR/LF, ledger stays under 9 KB', () => {
  const env = makeEnv(); baseCfg(env, { to: 'owner@gmail.com', maxPerDay: 40 });
  const ts = env.nowS() - 5;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: '<script>x</script>\r\nBcc: a@b' });
  env.gs.poll();
  assert(!env.sent[0].htmlBody.includes('<script>'));
  const big = {};
  for (let i = 0; i < 400; i++) big[(1792000000 + i) + '-INVERTER_FAULT-o'] = env.nowS() - i;
  env.props.SENT = JSON.stringify(big);
  env.tick(300); env.gs.poll();
  assert(env.props.SENT.length <= 8000);
});

test('17 quota floor: sev 3 refused below 10 left, sev 5 still sent', () => {
  const env = makeEnv({ quota: 8 }); baseCfg(env, { to: 'owner@gmail.com' });
  const ts = env.nowS() - 5;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  env.gs.poll();
  assert.strictEqual(env.getPath('events/' + D + '/' + ts + '-r01-o/em'), 'quota');
  addEvent(env, D, (ts + 1) + '-BATT_CRITICAL-o', { ts: ts + 1, type: 'BATT_CRITICAL', sev: 5, st: 'open', msg: 'Battery CRITICAL' });
  env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  assert.strictEqual(env.sent[0].subject, '[Solar] CRITICAL: Battery CRITICAL');
});

test('18 MailApp error in the offline mail is recorded, the run continues, heartbeat written', () => {
  const env = makeEnv(); baseCfg(env, { to: 'owner@gmail.com' });
  env.setPath('live/ts', env.nowS() - 3600);
  env.mailThrows = 1;
  env.gs.poll();
  assert(env.getPath('mailer/err').includes('too many'));
  assert.strictEqual(env.getPath('mailer/hb'), env.nowS());
});

// ------------------------------------------------------------------------------------------ additions
test('19 idle run: GET mailq + GET live/ts + PATCH mailer only; no property writes', () => {
  const env = makeEnv(); baseCfg(env);
  env.gs.poll();                                          // first run: cfg cache cold, token fetched
  env.tick(300); env.setPath('live/ts', env.nowS() - 10);
  const f0 = env.fetches.length, w0 = env.propWrites;
  env.gs.poll();
  const calls = env.fetches.slice(f0).map((f) => f.method + ' ' + new URL(f.url).pathname.replace('/devices/inv1', ''));
  assert.deepStrictEqual(calls, ['get /mailq.json', 'get /live/ts.json', 'patch /mailer.json']);
  assert.strictEqual(env.propWrites, w0, 'unchanged STATE / SENT are not rewritten');
});

test('20 the mailer never reads cfg, status, meta, hist or the root (rules would refuse it)', () => {
  const env = makeEnv({ now: Date.UTC(2026, 9, 21, 1, 2, 0) }); baseCfg(env, { summary: { on: true, hour: 7 } });
  env.setPath('mailq/test', { ts: env.nowS(), rid: 'r03' });
  env.setPath('alerts/rules/r03', { name: 'n', on: true, metric: 'egr', scope: 'day' });
  const ts = env.nowS() - 5;
  addEvent(env, D, ts + '-r03-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r03', val: 1, thr: 1, msg: 'x' });
  env.gs.poll();
  for (const f of env.fetches.filter((x) => x.method === 'get' && x.url.includes('/devices/'))) {
    assert.match(new URL(f.url).pathname, /^\/devices\/inv1\/(mailq|alerts|live|daily|events|mailer)[/.]/, f.url);
  }
  assert.strictEqual(env.getPath('mailer/err'), '');
});

test('21 malformed queue keys are dropped without reading events', () => {
  const env = makeEnv(); baseCfg(env);
  env.setPath('mailq/bad-key', { d: D, ts: 1, sev: 3 });
  env.setPath('mailq/1792580003-r03-o', { d: 'yesterday', ts: 1, sev: 3 });
  env.gs.poll();
  assert.strictEqual(env.getPath('mailq'), undefined);
  assert(!env.fetches.some((f) => f.url.includes('/events/')));
  assert.strictEqual(env.sent.length, 0);
});

test('22 MailApp failure on a test e-mail answers ok:false and deletes the request', () => {
  const env = makeEnv(); baseCfg(env);
  env.mailThrows = 1;
  env.setPath('mailq/test', { ts: env.nowS() });
  env.gs.poll();
  assert.strictEqual(env.getPath('mailer/test/ok'), false);
  assert.match(env.getPath('mailer/test/msg'), /^MailApp: /);
  assert.strictEqual(env.getPath('mailq'), undefined);
  assert.strictEqual(env.getPath('mailer/hb'), env.nowS());
});

test('23 sendTestEmail from the editor uses the same path and writes mailer/test', () => {
  const env = makeEnv(); baseCfg(env, { on: false });
  env.gs.sendTestEmail();
  assert.strictEqual(env.sent.length, 1);
  assert.match(env.sent[0].body, /requested from the script editor/);
  assert.strictEqual(env.getPath('mailer/test/msg'), 'sent to 2 recipients (alert e-mails are switched OFF)');
  assert.match(env.log.join('\n'), /^OK: sent to 2 recipients/m);
  env.gs.sendTestEmail();
  assert.match(env.log.join('\n'), /NOT SENT: rate limited/);
});

test('24 summary defaults to on at 07:00 when alerts/email has no summary object (r8 section 3.2)', () => {
  const env = makeEnv({ now: Date.UTC(2026, 9, 21, 1, 30, 0) });
  env.setPath('alerts/email', { on: true, to: 'owner@gmail.com' });
  env.setPath('live', { ts: env.nowS() - 10 });
  env.setPath('daily/2026-10-20', { epv: 1000, eld: 2000, egr: 500, n: 1440 });
  env.gs.poll();
  assert.strictEqual(env.sent.length, 1);
  assert.match(env.sent[0].subject, /^\[Solar\] Tue 20 Oct: PV 1\.00 kWh/);
});

test('25 duplicate address in alerts/email/to is mailed (and counted) once', () => {
  const env = makeEnv(); baseCfg(env, { to: 'owner@gmail.com,OWNER@gmail.com' });
  const ts = env.nowS() - 5;
  addEvent(env, D, ts + '-r01-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r01', msg: 'x' });
  env.gs.poll();
  assert.strictEqual(env.sent[0].to, 'owner@gmail.com');
  assert.strictEqual(env.getPath('mailer/sent'), 1);
});

test('26 a small daily cap keeps a proportional reserve (maxPerDay 4: 3 normal + 1 for sev >= 4)', () => {
  const env = makeEnv(); baseCfg(env, { to: 'owner@gmail.com', maxPerDay: 4 });
  for (let i = 0; i < 4; i++) {
    const ts = env.nowS() - 100 + i;
    addEvent(env, D, ts + '-r0' + (i + 1) + '-o', { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r0' + (i + 1), msg: 'm' + i });
    env.gs.poll();
  }
  const subjects = env.sent.map((m) => m.subject);
  assert.strictEqual(subjects.filter((s) => /\] m\d$/.test(s)).length, 3);
  assert.strictEqual(subjects.filter((s) => s.includes('limit reached')).length, 1);
  assert.strictEqual(env.getPath('mailer/sent'), 4);
});

test('27 setup refuses bad properties and reports a failed sign-in', () => {
  const env = makeEnv({ props: { ALLOW_TO: 'owner@gmail.com,not-an-address' } }); baseCfg(env);
  assert.throws(() => env.gs.setup(), /ALLOW_TO has an entry that is not an e-mail address: not-an-address/);
  const env2 = makeEnv({ props: { FB_DB_URL: 'https://p.firebasedatabase.app/devices/inv1' } });
  assert.throws(() => env2.gs.setup(), /FB_DB_URL must be the bare database URL/);
  const env3 = makeEnv({ props: { FB_PASSWORD: '' } });
  assert.throws(() => env3.gs.setup(), /Script property FB_PASSWORD is not set/);
  const env4 = makeEnv(); env4.badPassword = true;
  assert.throws(() => env4.gs.setup(), /Firebase sign-in failed: HTTP 400/);
  assert.deepStrictEqual(env4.triggers, [], 'no trigger installed after a failed setup');
});

test('28 a busy lock skips the timer run; RTDB failure re-throws after the heartbeat attempt', () => {
  const env = makeEnv(); baseCfg(env);
  env.locked = true;
  env.gs.poll();
  assert.strictEqual(env.fetches.length, 0);
  env.locked = false;
  env.gs.poll();
  env.rejectAll = true;                                   // every request now 401 (rules refuse the mailer)
  env.tick(300);
  assert.throws(() => env.gs.poll(), /RTDB GET \/mailq -> HTTP 401/);
  assert.strictEqual(env.fetches.filter((f) => f.method === 'patch').slice(-1)[0].url.includes('/mailer.json'), true, 'heartbeat attempted');
});
