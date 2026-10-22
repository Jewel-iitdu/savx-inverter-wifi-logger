// Pure-logic tests for Lib.gs (no Apps Script services). Run: node --test test/*.test.js
'use strict';
const test = require('node:test');
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { loadGs, DIR } = require('./_load');

const L = loadGs(['Lib.gs']);                       // no globals at all: Lib.gs must be pure
const fx = (f) => JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', f), 'utf8'));
const golden = (f) => fs.readFileSync(path.join(__dirname, 'fixtures', f), 'utf8');
const T0 = Date.UTC(2026, 9, 21, 12, 42, 30) / 1000;  // Wed 21 Oct 2026, 18:42:30 Asia/Dhaka
const M = { now: T0, tag: 'Solar', device: 'inv1', appUrl: 'https://savx.example.workers.dev' };

test('Lib.gs references no Apps Script service and no Code.gs function', () => {
  const src = fs.readFileSync(path.join(DIR, 'Lib.gs'), 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
  assert.doesNotMatch(src, /\b(MailApp|GmailApp|UrlFetchApp|PropertiesService|CacheService|LockService|ScriptApp|Utilities|ContentService|Logger|Session)\s*\./);
  assert.doesNotMatch(src, /\b(db_|token_|cfg_|SP_|prop_|need_|send_|run_)\(/);
});

test('time: fixed UTC+6, day keys across midnight, month and leap-year ends', () => {
  const utc = (y, mo, d, h, mi) => Date.UTC(y, mo - 1, d, h, mi) / 1000;
  assert.strictEqual(L.dkey_(utc(2026, 10, 20, 17, 59)), '2026-10-20');      // 23:59 local
  assert.strictEqual(L.dkey_(utc(2026, 10, 20, 18, 0)), '2026-10-21');       // 00:00 local
  assert.strictEqual(L.hhmm_(utc(2026, 10, 20, 18, 5)), '00:05');
  assert.strictEqual(L.localHour_(utc(2026, 10, 21, 1, 2)), 7);
  assert.strictEqual(L.when_(T0), 'Wed 21 Oct 2026, 18:42');
  assert.strictEqual(L.when_(0), 'unknown time');
  assert.strictEqual(L.fmtDay_('2026-10-20'), 'Tue 20 Oct');
  assert.strictEqual(L.addDays_('2026-03-01', -1), '2026-02-28');
  assert.strictEqual(L.addDays_('2028-03-01', -1), '2028-02-29');
  assert.strictEqual(L.addDays_('2026-12-31', 1), '2027-01-01');
  assert.strictEqual(L.addDays_('2027-01-01', -2), '2026-12-30');
});

test('billing cycle start for billing days 1, 5 and 28 across month and year ends', () => {
  assert.strictEqual(L.cycleStart_('2026-10-20', 1), '2026-10-01');
  assert.strictEqual(L.cycleStart_('2026-10-04', 5), '2026-09-05');
  assert.strictEqual(L.cycleStart_('2026-10-05', 5), '2026-10-05');
  assert.strictEqual(L.cycleStart_('2027-01-27', 28), '2026-12-28');
  assert.strictEqual(L.cycleStart_('2027-01-03', 5), '2026-12-05');
  assert.deepStrictEqual(L.summaryRange_('2026-10-21', 5), { y: '2026-10-20', y2: '2026-10-19', cs: '2026-10-05', from: '2026-10-05' });
  assert.deepStrictEqual(L.summaryRange_('2026-10-02', 1), { y: '2026-10-01', y2: '2026-09-30', cs: '2026-10-01', from: '2026-09-30' });
});

test('number formats of r8 section 4.5', () => {
  assert.strictEqual(L.dur_(59), '59 s');
  assert.strictEqual(L.dur_(7500), '2 h 05 min');
  assert.strictEqual(L.dur_(90061), '1 d 1 h');
  assert.strictEqual(L.durMin_(0), '0 min');
  assert.strictEqual(L.durMin_(134), '2 h 14 min');
  assert.strictEqual(L.n0_(2710), '2,710');
  assert.strictEqual(L.n0_(-12345.6), '-12,346');
  const cases = [['egr', 5010, '5.01 kWh'], ['egr', 999, '999 Wh'], ['w', 2710, '2,710 W'], ['bv', 26.42, '26.4 V'],
    ['gf', 49.96, '50.0 Hz'], ['soc', 29.6, '30 %'], ['bdi', 41.4, '41 A'], ['tmp', 71.2, '71 °C'],
    ['gout', 134, '2 h 14 min'], ['onbat', 1, 'yes'], ['nogrid', 0, 'no'], ['zzz', 7, '7'], ['w', NaN, '-']];
  for (const [m, v, want] of cases) assert.strictEqual(L.fmtVal_(m, v), want, m + ' ' + v);
});

test('clip, escape, constant-time compare and the nudge key', () => {
  assert.strictEqual(L.clip_('a\r\nBcc: x', 50), 'a Bcc: x');
  assert.strictEqual(L.clip_('abcdef', 4), 'abc…');
  assert.strictEqual(L.esc_('<a href="x">\'&'), '&lt;a href=&quot;x&quot;&gt;&#39;&amp;');
  assert.strictEqual(L.safeEq_('abc', 'abc'), true);
  assert.strictEqual(L.safeEq_('abc', 'abd'), false);
  assert.strictEqual(L.safeEq_('abc', 'abcd'), false);
  const key = 'x'.repeat(24);
  assert.strictEqual(L.nudgeOk_(key, key), true);
  assert.strictEqual(L.nudgeOk_('x'.repeat(23), 'x'.repeat(23)), false, 'short keys are refused');
  assert.strictEqual(L.nudgeOk_('', ''), false);
  assert.strictEqual(L.nudgeOk_(undefined, null), false);
  assert.strictEqual(L.nudgeOk_(key + 'y', key), false);
});

test('alerts/email normalisation: contract defaults and clamps (fixture)', () => {
  const f = fx('alerts-email.json');
  const full = L.normCfg_(f.full, '');
  assert.deepStrictEqual(full, { on: true, to: 'owner@gmail.com,family@gmail.com', fromName: 'Solar logger', maxPerDay: 40, hard: 40,
    monthStart: 1, summary: { on: true, hour: 7 }, offline: { on: true, min: 30, clear: true } });
  const min = L.normCfg_(f.minimal, '25');
  assert.strictEqual(min.maxPerDay, 25, 'MAX_PER_DAY can only lower the cap');
  assert.deepStrictEqual([min.summary, min.offline, min.monthStart, min.fromName],
    [{ on: true, hour: 7 }, { on: true, min: 30, clear: true }, 1, 'Solar logger']);
  const h = L.normCfg_(f.hostile, '500');
  assert.strictEqual(h.on, false, 'on must be the boolean true');
  assert.strictEqual(h.fromName, 'Solar Bcc: x@y.z logger', 'no CR/LF/TAB in the sender name');
  assert.deepStrictEqual([h.maxPerDay, h.hard, h.monthStart, h.summary.hour, h.summary.on, h.offline.min], [40, 40, 28, 23, false, 10]);
  assert.strictEqual(L.normCfg_(null, '0').maxPerDay, 40);
  assert.strictEqual(L.normCfg_({ maxPerDay: 10 }, '-3').maxPerDay, 1);
});

test('recipients: rc mask, ALLOW_TO filter (case-insensitive), de-dup, max 3 (fixture)', () => {
  const f = fx('alerts-email.json');
  assert.deepStrictEqual(L.pickRecipients_(f.full.to, 0, f.allowTo), { to: ['owner@gmail.com', 'family@gmail.com'], bad: [] });
  assert.deepStrictEqual(L.pickRecipients_(f.full.to, 2, f.allowTo).to, ['family@gmail.com']);
  assert.deepStrictEqual(L.pickRecipients_(f.hostile.to, 0, f.allowTo), { to: ['Owner@Gmail.com'], bad: ['stranger@evil.example'] });
  assert.deepStrictEqual(L.pickRecipients_(f.hostile.to, 4, f.allowTo), { to: [], bad: ['stranger@evil.example'] });
  assert.deepStrictEqual(L.pickRecipients_('a@x.io,b@x.io,c@x.io,d@x.io', 0, 'a@x.io b@x.io;c@x.io,d@x.io').to, ['a@x.io', 'b@x.io', 'c@x.io']);
  assert.deepStrictEqual(L.pickRecipients_('', 0, f.allowTo).to, []);
});

test('budget: 40/day cap with the severity-4 reserve, quota floor', () => {
  const rows = [
    // sent, cap, sev, n, quota, expected
    [33, 40, 3, 2, 90, ''], [34, 40, 3, 2, 90, 'cap'], [35, 40, 3, 1, 90, 'cap'], [35, 40, 4, 1, 90, ''],
    [39, 40, 5, 1, 90, ''], [40, 40, 5, 1, 90, 'cap'], [0, 40, 3, 1, 9, 'quota'], [0, 40, 4, 1, 9, ''],
    [0, 40, 4, 2, 1, 'quota'], [2, 4, 3, 1, 90, ''], [3, 4, 3, 1, 90, 'cap'], [3, 4, 4, 1, 90, ''], [0, 1, 3, 1, 90, '']];
  for (const [s, c, v, n, q, want] of rows) assert.strictEqual(L.budgetGate_(s, c, v, n, q), want, JSON.stringify([s, c, v, n, q]));
});

test('job and event checks: malformed jobs, ledger hits, off, expiry (fixtures)', () => {
  const mq = fx('mailq.json'), now = 1792580200;
  for (const [k, it] of Object.entries(mq.malformed)) assert.strictEqual(L.jobCheck_(k, it, {}), 'drop', k);
  assert.strictEqual(L.jobCheck_('1792580003-r03-o', mq.queue['1792580003-r03-o'], {}), '');
  assert.strictEqual(L.jobCheck_('1792580003-r03-o', mq.queue['1792580003-r03-o'], { '1792580003-r03-o': now }), 'sent');
  assert.strictEqual(L.jobCheck_('test', mq.queue.test, {}), 'drop', 'test is handled before the job loop');
  const ev = { ts: now - 60, sev: 3, em: 'q' };
  assert.strictEqual(L.eventCheck_(null, true, now), 'drop');
  assert.strictEqual(L.eventCheck_(Object.assign({}, ev, { em: 'sent' }), true, now), 'drop');
  assert.strictEqual(L.eventCheck_(ev, false, now), 'off');
  assert.strictEqual(L.eventCheck_(ev, true, now), '');
  assert.strictEqual(L.eventCheck_({ ts: now - 13 * 3600, sev: 3, em: 'q' }, true, now), 'exp');
  assert.strictEqual(L.eventCheck_({ ts: now - 13 * 3600, sev: 4, em: 'q' }, true, now), '', 'sev >= 4 keeps 24 h');
  assert.strictEqual(L.eventCheck_({ ts: now - 25 * 3600, sev: 4 }, true, now), 'exp');
  assert.deepStrictEqual([1, 2, 3, 4].map(L.nextTry_), [2, 3, 4, 'fail']);
  const g = L.groupJobs_([{ k: 'a', to: ['x', 'y'] }, { k: 'b', to: ['x'] }, { k: 'c', to: ['x', 'y'] }]);
  assert.deepStrictEqual(g.map((x) => [x.to.join(','), x.list.map((y) => y.k).join('')]), [['x,y', 'ac'], ['x', 'b']]);
});

test('de-dup ledger: 72 h pruning, <= 8000 chars keeping the newest; day state reset', () => {
  const now = 1792580000;
  const l = L.ledgerParse_(JSON.stringify({ old: now - 73 * 3600, fresh: now - 3600, bad: 'x' }), now);
  assert.deepStrictEqual(Object.keys(l), ['fresh']);
  assert.deepStrictEqual(L.ledgerParse_('{not json', now), {});
  const big = {};
  for (let i = 0; i < 400; i++) big[(1792000000 + i) + '-INVERTER_FAULT-o'] = now - i;
  const s = L.ledgerDump_(big, 8000);
  assert(s.length <= 8000);
  const kept = JSON.parse(s);
  assert(kept['1792000000-INVERTER_FAULT-o'], 'newest entry kept');
  assert(!kept['1792000399-INVERTER_FAULT-o'], 'oldest entry dropped');
  const st = L.stateParse_(JSON.stringify({ day: '2026-10-20', sent: 39, capNote: 1, tests: 3, offMails: 2, offSince: 1792500000, sumDay: '2026-10-20' }), '2026-10-21');
  assert.deepStrictEqual([st.sent, st.capNote, st.tests, st.offMails, st.offSince, st.sumDay], [0, 0, 0, 0, 1792500000, '2026-10-20']);
  assert.strictEqual(L.stateParse_(JSON.stringify({ day: '2026-10-21', sent: 7 }), '2026-10-21').sent, 7);
});

test('test-request gate, offline detector, summary due', () => {
  const now = 1792580000;
  assert.strictEqual(L.testGate_(null, now, {}), 'bad request');
  assert.strictEqual(L.testGate_({ ts: now, rid: 'r17' }, now, {}), 'bad request');
  assert.strictEqual(L.testGate_({ ts: now - 20, rid: 'r03' }, now, {}), '');
  assert.strictEqual(L.testGate_({ ts: now - 1000 }, now, {}), 'expired: requested 16 min ago');
  assert.strictEqual(L.testGate_({ ts: now }, now, { lastTest: now - 30 }), 'rate limited: one test per minute');
  assert.strictEqual(L.testGate_({ ts: now }, now, { lastTest: now - 61, tests: 10 }), 'rate limited: 10 tests per day');
  const off = { min: 30 };
  assert.strictEqual(L.offlineStep_(off, 0, 0, now), '');
  assert.strictEqual(L.offlineStep_(off, 0, now - 29 * 60, now), '');
  assert.strictEqual(L.offlineStep_(off, 0, now - 31 * 60, now), 'offline');
  assert.strictEqual(L.offlineStep_(off, now - 31 * 60, now - 40 * 60, now), '', 'still offline: no second mail');
  assert.strictEqual(L.offlineStep_(off, now - 3600, now - 10, now), 'online');
  const cf = L.normCfg_(fx('alerts-email.json').full, '');
  const at = (h, mi) => Date.UTC(2026, 9, 21, h - 6, mi) / 1000;
  assert.strictEqual(L.summaryDue_(cf, '', '2026-10-21', at(6, 59)), false);
  assert.strictEqual(L.summaryDue_(cf, '', '2026-10-21', at(7, 0)), true);
  assert.strictEqual(L.summaryDue_(cf, '2026-10-21', '2026-10-21', at(7, 5)), false, 'once per day');
  assert.strictEqual(L.summaryDue_(Object.assign({}, cf, { on: false }), '', '2026-10-21', at(8, 0)), false);
});

test('golden: single rule alert, plain text + HTML (fixtures)', () => {
  const live = fx('live.json'), rules = fx('rules.json');
  const ts = T0 - 20, k = ts + '-r03-o';
  const ev = { ts, type: 'RULE', sev: 3, st: 'open', rid: 'r03', val: 5010, thr: 5000, msg: 'Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)', em: 'q' };
  const m = L.eventMail_(M, { k, ev, sev: 3 }, L.liveRows_(live), rules.r03);
  assert.strictEqual(m.subject, '[Solar] Daily grid usage > 5 kWh: now 5.01 kWh (limit 5.00 kWh today, est.)');
  assert.strictEqual(m.text, golden('expected-alert.txt'));
  assert.match(m.html, /<a href="https:\/\/savx\.example\.workers\.dev\/#\/alerts"[^>]*>Open dashboard<\/a>/);
  assert.match(m.html, /Value: 5\.01 kWh, limit 5\.00 kWh \[Grid energy, today, est\.\]/);
});

test('golden: daily summary for yesterday with billing-cycle sums (fixtures)', () => {
  const r = L.summaryRange_('2026-10-21', 5);
  const ruleNames = { r03: 'Daily grid usage > 5 kWh' };
  const m = L.summaryMail_(M, r, fx('daily.json'), fx('events-2026-10-20.json'), 5, ruleNames);
  assert.strictEqual(m.subject, '[Solar] Tue 20 Oct: PV 12.84 kWh | Load 9.71 kWh | Grid 1.22 kWh est.');
  assert.strictEqual(m.text, golden('expected-summary.txt'));
  assert.match(m.html, /#\/energy/);
  const none = L.summaryMail_(M, r, {}, {}, 5, {});
  assert.match(none.text, /No data for 2026-10-20/);
  assert.match(none.subject, /PV - \| Load - \| Grid - est\.$/);
});

test('templates: prefixes, delayed line, digest, clear value line, escaping, no-link without https', () => {
  const rules = fx('rules.json'), rows = L.liveRows_(null);
  assert.deepStrictEqual(rows, [['Logger', 'no live data']]);
  const clr = L.eventMail_(M, { k: (T0 - 3600) + '-r02-c', sev: 4, ev: { ts: T0 - 3600, type: 'RULE', sev: 4, st: 'clear', rid: 'r02', dur: 7500,
    val: 36, thr: 35, msg: 'SOC < 30 %: back to 36 % (clear level 35 %)' } }, rows, rules.r02);
  assert.strictEqual(clr.subject, '[Solar] Cleared: SOC < 30 %: back to 36 % (clear level 35 %)');
  assert.match(clr.text, /Value: 36 %, clear level 35 % \[Battery SOC\]/);
  assert.match(clr.text, /It lasted 2 h 05 min\./);
  assert.match(clr.text, /Delayed 1 h 00 min: the logger or the internet was offline\./);
  const crit = L.eventMail_(M, { k: T0 + '-BATT_CRITICAL-o', sev: 5, ev: { ts: T0, type: 'BATT_CRITICAL', sev: 5, st: 'open', msg: 'Battery CRITICAL' } }, rows, null);
  assert.strictEqual(crit.subject, '[Solar] CRITICAL: Battery CRITICAL');
  assert.match(crit.text, /Severity: Critical \| Battery critical/);
  assert.doesNotMatch(crit.text, /Value:/);
  const rep = L.eventMail_(M, { k: T0 + '-r01-r', sev: 3, ev: { ts: T0, sev: 3, rid: 'r01', msg: 'Load > 2500 W for 5 min: still 2,710 W for 1 h 10 min' } }, rows, rules.r01);
  assert.strictEqual(rep.subject, '[Solar] Still active: Load > 2500 W for 5 min: still 2,710 W for 1 h 10 min');
  const dg = L.digestMail_(M, [
    { k: (T0 - 700) + '-r01-o', sev: 3, ev: { ts: T0 - 700, msg: 'Load > 2500 W' } },
    { k: T0 + '-GRID_LOSS-o', sev: 4, ev: { ts: T0, type: 'GRID_LOSS', msg: 'Grid lost, on battery (SOC 78%)' } }], rows);
  assert.strictEqual(dg.subject, '[Solar] 2 alerts: Load > 2500 W; Grid lost, on battery (SOC 78%)');
  assert.match(dg.text, /Load > 2500 W {2}\(18:30, Warning\)/);
  assert.match(dg.text, /delayed up to 11 min/);
  const evil = L.eventMail_(Object.assign({}, M, { appUrl: 'http://not-https.example' }),
    { k: T0 + '-r01-o', sev: 3, ev: { ts: T0, msg: '<script>x</script>\r\nBcc: a@b' } }, rows, null);
  assert.doesNotMatch(evil.html, /<script>/);
  assert.doesNotMatch(evil.subject, /[\r\n]/);
  assert.doesNotMatch(evil.text, /Dashboard:/);
  assert.doesNotMatch(evil.html, /Open dashboard/);
  const long = L.eventMail_(M, { k: T0 + '-r01-o', sev: 3, ev: { ts: T0, msg: 'x'.repeat(300) } }, rows, null);
  assert.strictEqual(long.subject.length, 120);
});

test('templates: test, cap, offline and online mails', () => {
  const t = L.testMail_(M, { ts: T0, src: 'the dashboard', to: ['owner@gmail.com', 'family@gmail.com'], on: false, sent: 3, maxPerDay: 40 }, null);
  assert.strictEqual(t.subject, '[Solar] Test e-mail - alerts are working');
  assert.match(t.text, /requested from the dashboard at 18:42\./);
  assert.match(t.text, /Alert e-mails are OFF\. E-mails sent today: 3 of 40\./);
  const tr = L.testMail_(M, { ts: T0, to: ['owner@gmail.com'], on: true, sent: 0, maxPerDay: 40, rid: 'r03', rule: fx('rules.json').r03 }, null);
  assert.strictEqual(tr.subject, '[Solar] [TEST] Daily grid usage > 5 kWh');
  assert.match(tr.text, /^Test of rule: Daily grid usage > 5 kWh\n\nRule r03: "Daily grid usage > 5 kWh" \(on\)/);
  assert.strictEqual(L.capMail_(M, 40, 40).subject, '[Solar] Daily e-mail limit reached');
  const off = L.offlineMail_(M, T0 - 25 * 60, 25 * 60, true);
  assert.strictEqual(off.subject, '[Solar] ALERT: Logger offline since 18:17');
  assert.match(off.text, /You will get one e-mail when it is back\./);
  assert.doesNotMatch(L.offlineMail_(M, T0 - 1500, 1500, false).text, /when it is back/);
  assert.strictEqual(L.onlineMail_(M, T0, 7260).subject, '[Solar] Logger back online after 2 h 01 min');
});

test('code tables match shared/codes.json (drift guard)', (t) => {
  const p = path.join(DIR, '..', '..', 'shared', 'codes.json');
  if (!fs.existsSync(p)) { t.skip('shared/codes.json not found'); return; }
  const c = JSON.parse(fs.readFileSync(p, 'utf8'));
  assert.deepStrictEqual(L.SEVN, c.severity.slice().sort((a, b) => a.sev - b.sev).map((s) => s.name));
  const q = {};
  c.qmod.forEach((x) => { q[x.code] = x.name; });
  assert.deepStrictEqual(L.MODES, q);
  for (const e of c.eventTypes.types) assert.strictEqual(L.EVENT_LABELS[e.type], e.label, e.type);
});
