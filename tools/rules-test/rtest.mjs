// Allow/deny tests for firebase/database.rules.json against the Realtime Database EMULATOR.
// Run through ./run.sh (renders the rules with the test UIDs below, starts the emulator, runs this file).
// Cases P1-P23 / N1-N39 are the research/r8-email-alerts.md section 8.1 suite (69 cases, all passed in the
// Decide phase); P24+ / N40+ cover the PWA's Alerts writes (web/src/data/firebaseSource.ts).
// The emulator accepts unsigned JWTs, but only as ?auth=<jwt> (a Bearer header is treated as admin).
const NS = process.env.RTDB_NS || 'demo-savx-default-rtdb';
const BASE = process.env.RTDB_EMULATOR || 'http://127.0.0.1:9123';
const b64 = (o) => Buffer.from(JSON.stringify(o)).toString('base64url');
const tok = (uid) => `${b64({ alg: 'none', typ: 'JWT' })}.${b64({ sub: uid, user_id: uid, uid, iat: 1, exp: 9999999999, aud: 'demo-savx', iss: 'https://securetoken.google.com/demo-savx', firebase: { sign_in_provider: 'password' } })}.`;
// Must match the UIDs run.sh renders into the rules.
const T = { owner: tok('owner1'), device: tok('device1'), mailer: tok('mailer1'), stranger: tok('evil9') };
async function req(who, method, path, body, admin) {
  const url = `${BASE}/devices/inv1${path ? '/' + path : ''}.json?ns=${NS}` + (admin ? '' : `&auth=${T[who]}`);
  const h = { 'Content-Type': 'application/json' };
  if (admin) h.Authorization = 'Bearer owner';
  const r = await fetch(url, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body) });
  return r.status;
}
const now = Math.floor(Date.now() / 1000), nowMs = Date.now();
let pass = 0, fail = 0;
async function t(name, who, method, path, body, expect) {
  const c = await req(who, method, path, body);
  const ok = expect === 'ok' ? c === 200 : c === expect;
  if (ok) pass++; else fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} [${c}] ${name}`);
}
const rule = (o) => Object.assign({ name: 'Daily grid usage > 5 kWh', on: true, metric: 'egr', scope: 'day', op: 'gt', value: 5000, mode: 'hold', forSec: 0, coolMin: 60, maxDay: 1, sev: 3, email: true, ntfy: false, upd: { '.sv': 'timestamp' } }, o);
const EID = `${now}-r03-o`, D = '2026-10-21';
// seed as admin
await req(null, 'PUT', '', { live: { ts: now, lk: 1, w: 512 }, daily: { [D]: { epv: 1 } }, status: { ts: now }, cfg: { liveSec: 10 }, hist: { [D]: { '1435': { n: 12 } } } }, true);

// ---------------- positive
await t('P1 owner writes alerts/email', 'owner', 'PUT', 'alerts/email', { on: true, to: 'owner@gmail.com,family.member@gmail.com', fromName: 'Solar logger', maxPerDay: 40, monthStart: 5, builtin: { minSev: 4, clear: true }, summary: { on: true, hour: 7 }, offline: { on: true, min: 20, clear: true } }, 'ok');
await t('P2 owner saves rule + alertsRev (multi-path)', 'owner', 'PATCH', '', { 'alerts/rules/r03': rule({}), 'cfg/alertsRev': { '.sv': 'timestamp' } }, 'ok');
await t('P3 owner toggles rule (partial)', 'owner', 'PATCH', '', { 'alerts/rules/r03/on': false, 'alerts/rules/r03/upd': { '.sv': 'timestamp' }, 'cfg/alertsRev': { '.sv': 'timestamp' } }, 'ok');
await t('P4 device writes event + mailq (multi-path)', 'device', 'PATCH', '', { [`events/${D}/${EID}`]: { ts: now, type: 'RULE', sev: 3, st: 'open', rid: 'r03', val: 5010, thr: 5000, msg: 'Daily grid usage > 5 kWh: now 5.01 kWh', ch: 'email', em: 'q' }, [`mailq/${EID}`]: { d: D, ts: now, sev: 3, rc: 3 } }, 'ok');
await t('P5a mailer reads mailq', 'mailer', 'GET', 'mailq', undefined, 'ok');
await t('P5b mailer reads alerts', 'mailer', 'GET', 'alerts', undefined, 'ok');
await t('P5c mailer reads live/ts', 'mailer', 'GET', 'live/ts', undefined, 'ok');
await t('P5d mailer reads daily', 'mailer', 'GET', 'daily', undefined, 'ok');
await t('P5e mailer reads events/<d>/<k>', 'mailer', 'GET', `events/${D}/${EID}`, undefined, 'ok');
await t('P6 mailer finishes job (em, emt, mailq delete, hb)', 'mailer', 'PATCH', '', { [`events/${D}/${EID}/em`]: 'sent', [`events/${D}/${EID}/emt`]: now, [`mailq/${EID}`]: null, 'mailer/hb': now, 'mailer/ver': '1.0.0', 'mailer/last': { ts: now, ok: true, msg: 'x' } }, 'ok');
await t('P7 owner requests test e-mail', 'owner', 'PUT', 'mailq/test', { ts: now, rid: 'r03' }, 'ok');
await t('P8 mailer deletes test, writes result', 'mailer', 'PATCH', '', { 'mailq/test': null, 'mailer/test': { ts: now, ok: true, msg: 'sent to 2 recipients' } }, 'ok');
await t('P9 device writes alertState', 'device', 'PUT', 'alertState/r03', { st: 'latch', since: now, val: 5010, last: now, cnt: 1, pk: 20261021 }, 'ok');
await t('P10 at-mode rule (epv day lt at 1700, window)', 'owner', 'PUT', 'alerts/rules/r04', rule({ name: 'PV today < 3 kWh by 17:00', metric: 'epv', op: 'lt', value: 3000, mode: 'at', at: '1700', minCov: 80, win: { from: '0600', to: '2300', days: 127 } }), 'ok');
await t('P11 bool rule onbat ge 1 for 2 h', 'owner', 'PUT', 'alerts/rules/r08', rule({ name: 'On battery for 2 h', metric: 'onbat', scope: 'inst', op: 'ge', value: 1, forSec: 7200, clrSec: 60, coolMin: 120, maxDay: 3, sev: 3 }), 'ok');
await t('P12 month rule egr month gt 150 kWh', 'owner', 'PUT', 'alerts/rules/r06', rule({ name: 'Monthly grid usage > 150 kWh', scope: 'month', value: 150000 }), 'ok');
await t('P13 inst rule with clr + rc + repeat', 'owner', 'PUT', 'alerts/rules/r01', rule({ name: 'Load > 2500 W for 5 min', metric: 'w', scope: 'inst', op: 'gt', value: 2500, forSec: 300, clr: 2200, clrSec: 120, coolMin: 60, repeatMin: 60, maxDay: 3, rc: 1, ntfy: true, clrMail: true }), 'ok');
await t('P14 owner deletes a rule', 'owner', 'PATCH', '', { 'alerts/rules/r08': null, 'cfg/alertsRev': { '.sv': 'timestamp' } }, 'ok');
await t('P15 owner acks rule event', 'owner', 'PUT', `events/${D}/${EID}/ack`, true, 'ok');
await t('P16 empty recipient list allowed', 'owner', 'PUT', 'alerts/email/to', '', 'ok');
await t('P17 mailer deletes an absent mailq item', 'mailer', 'PATCH', '', { [`mailq/${now}-GRID_LOSS-o`]: null }, 'ok');

// ---------------- negative
await t('N1 device writes alerts/email', 'device', 'PUT', 'alerts/email/on', true, 401);
await t('N2 rule key r17', 'owner', 'PUT', 'alerts/rules/r17', rule({}), 401);
await t('N3 load W value 30000', 'owner', 'PUT', 'alerts/rules/r02', rule({ metric: 'w', scope: 'inst', value: 30000 }), 401);
await t('N4 egr day lt without at-mode', 'owner', 'PUT', 'alerts/rules/r02', rule({ op: 'lt' }), 401);
await t('N5 egr month lt', 'owner', 'PUT', 'alerts/rules/r02', rule({ scope: 'month', op: 'lt', mode: 'at', at: '1700' }), 401);
await t('N6 clr on the wrong side', 'owner', 'PUT', 'alerts/rules/r02', rule({ metric: 'w', scope: 'inst', value: 2500, clr: 2600 }), 401);
await t('N7 name with newline', 'owner', 'PUT', 'alerts/rules/r02', rule({ name: 'x\nBcc: a@b.c' }), 401);
await t('N8 four recipients', 'owner', 'PUT', 'alerts/email/to', 'a@b.cc,c@d.ee,e@f.gg,h@i.jj', 401);
await t('N9 invalid address', 'owner', 'PUT', 'alerts/email/to', 'owner@gmail', 401);
await t('N10a mailer reads cfg', 'mailer', 'GET', 'cfg', undefined, 401);
await t('N10b mailer reads status', 'mailer', 'GET', 'status', undefined, 401);
await t('N10c mailer reads hist', 'mailer', 'GET', 'hist', undefined, 401);
await t('N10d mailer reads inv1 root', 'mailer', 'GET', '', undefined, 401);
await t('N11 mailer edits event msg', 'mailer', 'PUT', `events/${D}/${EID}/msg`, 'hacked', 401);
await t('N12 mailer writes em on a missing event', 'mailer', 'PUT', `events/${D}/9999999999-r01-o/em`, 'sent', 401);
await t('N13 mailer writes bad em value', 'mailer', 'PUT', `events/${D}/${EID}/em`, 'xyz', 401);
await t('N14 owner test request 1 h old', 'owner', 'PUT', 'mailq/test', { ts: now - 3600 }, 401);
await t('N15 owner writes a device mailq job', 'owner', 'PUT', `mailq/${now}-r01-o`, { d: D, ts: now, sev: 3 }, 401);
await t('N16 device mailq job with bad key', 'device', 'PUT', 'mailq/-Nabc', { d: D, ts: now, sev: 3 }, 401);
await t('N17 inst metric with day scope', 'owner', 'PUT', 'alerts/rules/r02', rule({ metric: 'w', scope: 'day' }), 401);
await t('N18 no channel', 'owner', 'PUT', 'alerts/rules/r02', rule({ email: false, ntfy: false }), 401);
await t('N19 unknown rule field', 'owner', 'PUT', 'alerts/rules/r02', rule({ evil: 1 }), 401);
await t('N20 alertsRev in the future', 'owner', 'PUT', 'cfg/alertsRev', nowMs + 3600000, 401);
await t('N21 mailer writes unknown mailer key', 'mailer', 'PUT', 'mailer/x', 1, 401);
await t('N22 stranger reads mailer', 'stranger', 'GET', 'mailer', undefined, 401);
await t('N23 rule without upd', 'owner', 'PUT', 'alerts/rules/r02', (() => { const r = rule({}); delete r.upd; return r; })(), 401);
await t('N24 rc = 8', 'owner', 'PUT', 'alerts/rules/r02', rule({ rc: 8 }), 401);
await t('N25 device writes mailer heartbeat', 'device', 'PUT', 'mailer/hb', now, 401);
await t('N26 owner writes alertState', 'owner', 'PUT', 'alertState/r03', { st: 'idle' }, 401);
await t('N27 maxPerDay 41', 'owner', 'PUT', 'alerts/email/maxPerDay', 41, 401);
await t('N28 offline.min 5', 'owner', 'PUT', 'alerts/email/offline', { on: true, min: 5 }, 401);
await t('N29 mailer writes a new event', 'mailer', 'PUT', `events/${D}/${now}-X-i`, { ts: now, type: 'X', sev: 1, msg: 'x' }, 401);
await t('N30 at-mode without at', 'owner', 'PUT', 'alerts/rules/r02', rule({ op: 'lt', mode: 'at' }), 401);
await t('N31 window from == to', 'owner', 'PUT', 'alerts/rules/r02', rule({ win: { from: '0600', to: '0600', days: 127 } }), 401);
await t('N32 bool value 2', 'owner', 'PUT', 'alerts/rules/r02', rule({ metric: 'nogrid', scope: 'inst', op: 'ge', value: 2 }), 401);
await t('N33 device writes rule', 'device', 'PUT', 'alerts/rules/r02', rule({}), 401);
await t('P18 mailer sets retry counter on an existing job', 'mailer', 'PATCH', '', { [`mailq/${now}-r05-o`]: null }, 'ok');
await t('P19 device writes job, mailer PUT n', 'device', 'PUT', `mailq/${now}-r05-o`, { d: D, ts: now, sev: 4 }, 'ok');
await t('P20 mailer PUT n=2', 'mailer', 'PUT', `mailq/${now}-r05-o/n`, 2, 'ok');
await t('P21 legacy push-id event still accepted', 'device', 'POST', `events/${D}`, { ts: now, type: 'GRID_LOSS', sev: 4, st: 'open', msg: 'Grid lost', ch: 'ntfy' }, 'ok');
await t('P22 device still writes live', 'device', 'PUT', 'live', { ts: now, lk: 1, w: 500, gw: 120 }, 'ok');
await t('P23 owner still writes cfg/liveSec', 'owner', 'PUT', 'cfg/liveSec', 10, 'ok');
await t('N34 mailer n = 21', 'mailer', 'PUT', `mailq/${now}-r05-o/n`, 21, 401);
await t('N35 owner still cannot write live', 'owner', 'PUT', 'live', { ts: now, lk: 1 }, 401);
await t('N36 mailer cannot write cfg', 'mailer', 'PUT', 'cfg/liveSec', 10, 401);
// 0.9.0 grid meter: the owner's switch, and the device's meter fields in live / daily / status (no new nodes)
await t('P70 owner switches the grid meter off', 'owner', 'PUT', 'cfg/meter', 0, 'ok');
await t('P71 owner switches the grid meter on', 'owner', 'PUT', 'cfg/meter', 1, 'ok');
await t('N70 cfg/meter only 0 or 1', 'owner', 'PUT', 'cfg/meter', 2, 401);
await t('N71 cfg/meter not a string', 'owner', 'PUT', 'cfg/meter', '1', 401);
await t('P72 device live with meter fields', 'device', 'PUT', 'live', { ts: now, lk: 1, gw: 230, mw: 211, mv: 244.4, mi: 1.29, mpf: 0.66, mhz: 50.4, mrc: 196, mtc: 37, mkwh: 305.03, mon: 1, egm: 2150 }, 'ok');
await t('P73 device daily with egm', 'device', 'PATCH', '', { [`daily/${D}/egm`]: 2150 }, 'ok');
await t('P74 device status with the meter state', 'device', 'PATCH', 'status', { meter: 'not reachable' }, 'ok');
// 0.9.2 remote firmware update: the owner asks for a version; the device reports its state and release repo
await t('P75 owner requests a firmware update', 'owner', 'PATCH', 'cfg', { ota: 903, otaAt: nowMs }, 'ok');
await t('N75 ota version out of range', 'owner', 'PUT', 'cfg/ota', 1000000, 401);
await t('N76 device cannot request an update', 'device', 'PUT', 'cfg/ota', 903, 401);
await t('P76 device status with the update state', 'device', 'PATCH', 'status', { ota: 'updated to 0.9.3', repo: 'owner/savx-inverter-wifi-logger' }, 'ok');
await t('N37 mailer cannot read meta', 'mailer', 'GET', 'meta', undefined, 401);
await t('N38 test request from the future (+1 h)', 'owner', 'PUT', 'mailq/test', { ts: now + 3600 }, 401);
await t('N39 rule name 41 chars', 'owner', 'PUT', 'alerts/rules/r02', rule({ name: 'x'.repeat(41) }), 401);

// ---------------- PWA Alerts writes (web/src/data/firebaseSource.ts)
const SV = { '.sv': 'timestamp' };
const EMAIL = { on: true, to: 'owner@gmail.com,family.member@gmail.com', fromName: 'সোলার লগার', maxPerDay: 40, monthStart: 1, builtin: { minSev: 4, clear: true }, summary: { on: true, hour: 7 }, offline: { on: true, min: 30, clear: true } };
await t('P24 owner saves e-mail settings + remapped rc + alertsRev (multi-path)', 'owner', 'PATCH', '', { 'alerts/email': EMAIL, 'alerts/rules/r01/rc': 1, 'alerts/rules/r01/upd': SV, 'cfg/alertsRev': SV }, 'ok');
await t('P25 owner widens a rule to everyone (rc -> null)', 'owner', 'PATCH', '', { 'alerts/rules/r01/rc': null, 'alerts/rules/r01/upd': SV, 'cfg/alertsRev': SV }, 'ok');
await t('P26 owner creates a rule in a free slot (PUT, transaction payload)', 'owner', 'PUT', 'alerts/rules/r09', rule({ name: 'Evening peak load > 1500 W', metric: 'w', scope: 'inst', op: 'gt', value: 1500, forSec: 60, clr: 1300, clrSec: 120, coolMin: 60, maxDay: 2, win: { from: '1800', to: '2300', days: 127 } }), 'ok');
await t('P27 window across midnight, weekdays only, ntfy only', 'owner', 'PUT', 'alerts/rules/r10', rule({ name: 'Night load', metric: 'w', scope: 'inst', op: 'ge', value: 800, forSec: 600, email: false, ntfy: true, win: { from: '2300', to: '0600', days: 31 } }), 'ok');
await t('P28 owner requests a plain test e-mail (no rid)', 'owner', 'PUT', 'mailq/test', { ts: now }, 'ok');
await t('P29 owner reads mailer, alertState and alerts', 'owner', 'GET', 'mailer', undefined, 'ok');
await t('P30 device reads alerts', 'device', 'GET', 'alerts', undefined, 'ok');
await t('N40 test request with an unknown field', 'owner', 'PUT', 'mailq/test', { ts: now, to: 'x@y.zz' }, 401);
await t('N41 window with days = 0', 'owner', 'PUT', 'alerts/rules/r11', rule({ win: { from: '0600', to: '2300', days: 0 } }), 401);
await t('N42 e-mail settings without "on"', 'owner', 'PUT', 'alerts/email', { to: 'owner@gmail.com' }, 401);
await t('N43 e-mail sender name with a newline', 'owner', 'PUT', 'alerts/email/fromName', 'Solar\nBcc: x@y.zz', 401);
await t('N44 summary hour 24', 'owner', 'PUT', 'alerts/email/summary', { on: true, hour: 24 }, 401);
await t('N45 built-in minSev 2', 'owner', 'PUT', 'alerts/email/builtin/minSev', 2, 401);
await t('N46 repeatMin 10 (must be 0 or 30-1440)', 'owner', 'PUT', 'alerts/rules/r11', rule({ metric: 'w', scope: 'inst', value: 2000, repeatMin: 10 }), 401);
await t('N47 forSec not an integer', 'owner', 'PUT', 'alerts/rules/r11', rule({ metric: 'w', scope: 'inst', value: 2000, forSec: 1.5 }), 401);
await t('N48 stranger writes a rule', 'stranger', 'PUT', 'alerts/rules/r11', rule({}), 401);
await t('N49 mailer writes a rule', 'mailer', 'PUT', 'alerts/rules/r11', rule({}), 401);
await t('N50 stranger requests a test e-mail', 'stranger', 'PUT', 'mailq/test', { ts: now }, 401);
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
