/**
 * Lib.gs - the pure logic of the Savx logger mailer. No Apps Script service and no I/O in this file.
 *
 * Apps Script loads Lib.gs and Code.gs into one shared global scope. Node loads this file for the tests
 * (test/_load.js), which proves it runs without MailApp, UrlFetchApp, PropertiesService, CacheService,
 * LockService, ScriptApp or Utilities. Only Code.gs talks to those services.
 *
 * Design: research/r8-email-alerts.md section 5 (source of truth). Data contract: r8 section 3.
 * Rule of this file: top-level code only DECLARES (var / function). Nothing here may call into Code.gs at load
 * time, because Apps Script does not guarantee the order in which it loads the files.
 */

// ============================================================================================ constants
var VERSION = '1.0.0';               // mailer/ver (rules: <= 16 chars)
var TZ = 'Asia/Dhaka';
var TZ_OFFSET_S = 6 * 3600;          // Asia/Dhaka is UTC+6 all year; Bangladesh has had no DST since 2009 (IANA tzdata)
var DEF = { DEVICE: 'inv1', SUBJECT_TAG: 'Solar', MAX_PER_DAY: '40' };   // script-property defaults
var HARD_MAX_PER_DAY = 40;           // 2 x 40 = 80 < 100 recipients per rolling 24 h (r8c C2)
var RESERVE_SEV4 = 5;                // the last 5 recipient-deliveries of a day are kept for severity >= 4
var QUOTA_FLOOR = 10;                // MailApp quota below this: only severity >= 4
var MAX_AGE_S = 12 * 3600;           // queued alerts older than this expire (severity >= 4: twice as long)
var MAX_TRIES = 5;                   // MailApp failures per job before em = "fail"
var TESTS_PER_DAY = 10;
var TEST_MIN_GAP_S = 60;             // one test e-mail per minute
var TEST_MAX_AGE_S = 900;            // a test request older than 15 min is answered "expired"
var OFFLINE_MAILS_PER_DAY = 4;       // offline + back-online mails per local day (flapping guard)
var ONLINE_AGE_S = 300;              // live.ts younger than this = the logger is back
var LEDGER_KEEP_S = 72 * 3600;       // de-dup ledger keeps sent keys for 72 h
var LEDGER_MAX_CHARS = 8000;         // a script property holds 9 KB
var CFG_CACHE_S = 600;               // alerts/email is cached 10 min (tests always read it fresh)
var DELAY_NOTE_S = 600;              // events older than 10 min get a "Delayed ..." line
var NUDGE_MIN_KEY = 24;              // NUDGE_KEY shorter than this = doGet refuses everything
var NUDGE_GAP_S = 20;                // at most one nudge-run per 20 s

var RE_JOB_KEY = /^[0-9]{10}-[A-Za-z0-9_]{1,24}-[ocri]$/;   // = the mailq/$k rule (r8 section 3.11)
var RE_DATE_KEY = /^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/;     // = the mailq/$k/d rule
var RE_RID = /^r(0[1-9]|1[0-6])$/;
var RE_EMAIL = /^[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,24}$/;   // same shape as the alerts/email/to rule

// Copied from shared/codes.json ("severity", "qmod", "eventTypes"). test/lib.test.js fails if they drift apart.
var SEVN = ['Debug', 'Info', 'Notice', 'Warning', 'Alert', 'Critical'];
var MODES = { P: 'Power on', S: 'Standby', L: 'Line (grid) mode', B: 'Battery (inverter) mode', F: 'Fault',
              H: 'Power saving (REVO: sleep)', D: 'Shutdown', C: 'Charge mode', Y: 'Bypass', E: 'ECO mode' };
var EVENT_LABELS = {
  INVERTER_FAULT: 'Inverter fault', BATT_CRITICAL: 'Battery critical', BATT_LOW: 'Battery low',
  BATT_HIGH: 'Battery voltage high', GRID_LOSS: 'Grid lost', GRID_RESTORED: 'Grid restored',
  GRID_ABNORMAL: 'Grid voltage abnormal', OVERLOAD: 'Overload', OVER_TEMP: 'Over temperature',
  PV_OVERVOLT: 'PV over-voltage', PV_LOW_DAY: 'Low solar today', LINK_LOST: 'Inverter link lost',
  LOGGER_REBOOT: 'Logger rebooted', LOGGER_OFFLINE: 'Logger was offline', QUOTA_GUARD: 'Quota guard active', DATA_DROPPED: 'Data dropped',
  BACKLOG_STUCK: 'Upload backlog stuck', DAILY_SUMMARY: 'Daily summary', TEST_ALERT: 'Test alert',
  RULE: 'Rule alert', RULE_INVALID: 'Rule ignored by the logger'          // new in r8 section 3.6
};
// Metric catalogue, r8 section 4.3 (acat 1): id -> [label, unit, estimated]. Used only for the "Value" line.
var METRICS = {
  w: ['Load power', 'W'], pvw: ['Solar power', 'W'], gw: ['Grid import power', 'W', 1],
  bdw: ['Battery discharge power', 'W'], bw: ['Battery net power', 'W'], soc: ['Battery SOC', '%'],
  ld: ['Load % of rating', '%'], bv: ['Battery voltage', 'V'], gv: ['Grid voltage', 'V'], ov: ['Output voltage', 'V'],
  pvv: ['Solar voltage', 'V'], bdi: ['Battery discharge current', 'A'], tmp: ['Inverter temperature', 'C'],
  gf: ['Grid frequency', 'Hz'], onbat: ['On battery', 'bool'], nogrid: ['Grid absent', 'bool'],
  eld: ['Load energy', 'Wh'], egr: ['Grid energy', 'Wh', 1], epv: ['Solar energy', 'Wh'],
  edis: ['Battery discharged', 'Wh'], ech: ['Battery charged', 'Wh'], gout: ['Grid outage time', 'min']
};
var WDAY = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
var MON = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

// ============================================================================================ config
/** alerts/email (r8 section 3.2) -> normalised config. Absent fields get the contract's defaults; values are clamped. */
function normCfg_(c, maxProp) {
  c = obj_(c);
  var s = obj_(c.summary), o = obj_(c.offline);
  var hard = Math.max(1, Math.min(Math.floor(Number(maxProp)) || HARD_MAX_PER_DAY, HARD_MAX_PER_DAY));
  return {
    on: c.on === true,
    to: typeof c.to === 'string' ? c.to : '',
    fromName: clip_(String(c.fromName || 'Solar logger').replace(/[\r\n\t]+/g, ' '), 40),
    maxPerDay: Math.min(hard, int_(c.maxPerDay, 1, HARD_MAX_PER_DAY, hard)),
    hard: hard,
    monthStart: int_(c.monthStart, 1, 28, 1),
    summary: { on: s.on !== false, hour: int_(s.hour, 0, 23, 7) },
    offline: { on: o.on !== false, min: int_(o.min, 10, 240, 30), clear: o.clear !== false }
  };
}

/**
 * Recipients: alerts/email/to (max 3, position i = bit i of rc; rc 0/absent = all), de-duplicated and filtered by
 * the ALLOW_TO script property (case-insensitive). Returns {to: allowed, bad: listed but not allowed}.
 */
function pickRecipients_(toStr, rc, allowStr) {
  var allow = list_(allowStr).map(function (a) { return a.toLowerCase(); });
  var out = [], bad = [], seen = {};
  rc = Number(rc) || 0;
  list_(toStr).slice(0, 3).forEach(function (a, i) {
    if (rc && !(rc & (1 << i))) return;
    var l = a.toLowerCase();
    if (seen[l]) return;
    seen[l] = 1;
    (allow.indexOf(l) >= 0 ? out : bad).push(a);
  });
  return { to: out, bad: bad };
}

// ============================================================================================ gates
/**
 * Daily recipient cap and Google quota. '' = may send; else the em code 'cap' or 'quota'.
 * sent = recipient-deliveries today, cap = maxPerDay, n = recipients of this mail, quota = MailApp quota left.
 * The reserve for severity >= 4 is 5, but never more than a quarter of a small cap (maxPerDay 4 still lets 3 through).
 */
function budgetGate_(sent, cap, sev, n, quota) {
  var reserve = Math.min(RESERVE_SEV4, Math.floor(cap / 4));
  var limit = sev >= 4 ? cap : cap - reserve;
  if (sent + n > limit) return 'cap';
  if (quota < n || (sev < 4 && quota < QUOTA_FLOOR)) return 'quota';
  return '';
}

/** First check of a queued job: 'drop' (malformed: delete the job only), 'sent' (in the ledger) or ''. */
function jobCheck_(k, it, ledger) {
  if (!RE_JOB_KEY.test(k) || !it || typeof it !== 'object' || !RE_DATE_KEY.test(String(it.d))) return 'drop';
  if (ledger && ledger[k]) return 'sent';
  return '';
}

/** Check of the job's event: 'drop' (gone or already handled), 'off', 'exp' or '' (go on). */
function eventCheck_(ev, on, now) {
  if (!ev || typeof ev !== 'object' || (ev.em && ev.em !== 'q')) return 'drop';
  if (!on) return 'off';
  var sev = Number(ev.sev) || 0;
  if (now - (Number(ev.ts) || now) > (sev >= 4 ? 2 : 1) * MAX_AGE_S) return 'exp';
  return '';
}

/** After a MailApp error: the next try count, or 'fail' after MAX_TRIES. */
function nextTry_(n) {
  n = (Number(n) || 0) + 1;
  return n >= MAX_TRIES ? 'fail' : n;
}

/** Jobs found in one run for the same recipient set become one digest. Keeps first-seen order. */
function groupJobs_(items) {
  var by = {}, out = [];
  items.forEach(function (x) {
    var g = x.to.join(',');
    if (!by[g]) { by[g] = { to: x.to, list: [] }; out.push(by[g]); }
    by[g].list.push(x);
  });
  return out;
}

/** mailq/test request -> '' (go on) or the reason it is refused. */
function testGate_(t, now, st) {
  if (!t || typeof t !== 'object' || !(Number(t.ts) > 0)) return 'bad request';
  if (t.rid !== undefined && !RE_RID.test(String(t.rid))) return 'bad request';
  if (now - t.ts > TEST_MAX_AGE_S) return 'expired: requested ' + dur_(now - t.ts) + ' ago';
  if (st.lastTest && now - st.lastTest < TEST_MIN_GAP_S) return 'rate limited: one test per minute';
  if ((st.tests || 0) >= TESTS_PER_DAY) return 'rate limited: ' + TESTS_PER_DAY + ' tests per day';
  return '';
}

/** Text for a refused test e-mail. */
function testGateMsg_(gate) {
  if (gate === 'norc') return 'no recipients: add one in Alerts > E-mail and to ALLOW_TO in the mailer';
  if (gate === 'cap') return 'daily e-mail limit reached';
  return 'Google sending quota is low';
}

/** Logger-offline detector step: 'offline', 'online' or ''. off = {min}, offSince = 0 while online. */
function offlineStep_(off, offSince, liveTs, now) {
  if (!(liveTs > 0)) return '';                                   // never seen: nothing to compare
  var age = now - liveTs;
  if (!offSince && age > off.min * 60) return 'offline';
  if (offSince && age < ONLINE_AGE_S) return 'online';
  return '';
}

/** True when the daily summary for yesterday is due in this run. */
function summaryDue_(cf, sumDay, today, now) {
  return cf.on && cf.summary.on && sumDay !== today && localHour_(now) >= cf.summary.hour;
}

/** Days the summary reads: y = yesterday, y2 = the day before, cs = cycle start, from = min(cs, y2). */
function summaryRange_(today, monthStart) {
  var y = addDays_(today, -1), y2 = addDays_(today, -2), cs = cycleStart_(y, monthStart);
  return { y: y, y2: y2, cs: cs, from: cs < y2 ? cs : y2 };
}

/** Constant-time string compare (the nudge key). */
function safeEq_(a, b) {
  a = String(a); b = String(b);
  if (a.length !== b.length) return false;
  var d = 0;
  for (var i = 0; i < a.length; i++) d |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return d === 0;
}

/** The nudge's ?k= is accepted only when NUDGE_KEY has >= 24 chars and matches exactly. */
function nudgeOk_(given, key) {
  key = String(key === undefined || key === null ? '' : key);
  return key.length >= NUDGE_MIN_KEY && safeEq_(String(given === undefined || given === null ? '' : given), key);
}

// ============================================================================================ state + ledger
/** STATE property -> today's counters (reset at local midnight; offSince, sumDay and lastTest survive). */
function stateParse_(json, today) {
  var s = parseObj_(json);
  if (s.day !== today) { s.day = today; s.sent = 0; s.capNote = 0; s.tests = 0; s.offMails = 0; }
  ['sent', 'capNote', 'tests', 'offMails', 'offSince', 'lastTest'].forEach(function (k) { s[k] = Number(s[k]) || 0; });
  return s;
}

/** SENT property -> de-dup ledger {eventKey: sentEpoch}, without entries older than 72 h. */
function ledgerParse_(json, now) {
  var l = parseObj_(json);
  Object.keys(l).forEach(function (k) { if (!num_(l[k]) || now - l[k] > LEDGER_KEEP_S) delete l[k]; });
  return l;
}

/** Ledger -> JSON of at most maxChars, dropping the oldest keys first (mutates l). */
function ledgerDump_(l, maxChars) {
  var keys = Object.keys(l).sort(function (a, b) { return l[a] - l[b]; });
  var s = JSON.stringify(l);
  while (keys.length && s.length > (maxChars || LEDGER_MAX_CHARS)) { delete l[keys.shift()]; s = JSON.stringify(l); }
  return s;
}

/** Billing-cycle sums from daily docs, cycle start cs .. yesterday y (inclusive). */
function mtd_(days, cs, y) {
  var sum = { epv: 0, eld: 0, egr: 0, ech: 0, edis: 0, gout: 0 }, n = 0;
  Object.keys(days || {}).forEach(function (k) {
    if (k < cs || k > y || !days[k]) return;
    n++;
    Object.keys(sum).forEach(function (f) { if (num_(days[k][f])) sum[f] += days[k][f]; });
  });
  return { sum: sum, days: n, from: cs };
}

/** "Alerts" line of the summary: opened events (not repeats) per rule (by name when known) or built-in type (by label). */
function alertCounts_(evs, ruleNames) {
  var counts = {}, order = [];
  Object.keys(evs || {}).sort().forEach(function (k) {
    var v = evs[k];
    if (!v || v.st !== 'open' || /-r$/.test(k)) return;          // a repeat ("still active") is not a new alert
    var n = v.rid ? ((ruleNames && ruleNames[v.rid]) || v.rid) : typeLabel_(v.type);
    if (!counts[n]) { counts[n] = 0; order.push(n); }
    counts[n]++;
  });
  return order.map(function (n) { return n + ' x' + counts[n]; });
}

// ============================================================================================ e-mail content
// m = mail environment {now, tag, device, appUrl}; Code.gs builds it from the clock and the script properties.

/** One alert e-mail. x = {k, ev, sev}; rows = "Right now" snapshot; rule = alerts/rules/<rid> (optional). */
function eventMail_(m, x, rows, rule) {
  var ev = x.ev, kind = x.k.slice(-1), msg = evMsg_(ev);
  var pre = kind === 'c' ? 'Cleared: ' : (kind === 'r' && !/^still active/i.test(msg)) ? 'Still active: ' :
            x.sev >= 5 ? 'CRITICAL: ' : x.sev >= 4 ? 'ALERT: ' : '';
  var lines = ['When: ' + when_(ev.ts) + ' (' + TZ + ')',
               'Severity: ' + sevName_(x.sev) + (ev.rid ? ' | rule ' + ev.rid : ' | ' + typeLabel_(ev.type))];
  var v = valueLine_(ev, rule, kind);
  if (v) lines.push(v);
  if (num_(ev.dur) && ev.dur > 0) lines.push('It lasted ' + dur_(ev.dur) + '.');
  var delay = m.now - (Number(ev.ts) || m.now);
  if (delay > DELAY_NOTE_S) lines.push('Delayed ' + dur_(delay) + ': the logger or the internet was offline.');
  return layout_(m, msg, lines, rows, pre + msg);
}

/** Several alerts for the same recipients found in one run. */
function digestMail_(m, list, rows) {
  var names = list.map(function (x) { return evMsg_(x.ev); });
  var lines = list.map(function (x, i) {
    var k = x.k.slice(-1);
    var pre = k === 'c' ? 'Cleared: ' : (k === 'r' && !/^still active/i.test(names[i])) ? 'Still active: ' : '';
    return pre + names[i] + '  (' + hhmm_(x.ev.ts) + ', ' + sevName_(x.sev) + ')';
  });
  var oldest = Math.min.apply(null, list.map(function (x) { return Number(x.ev.ts) || m.now; }));
  if (m.now - oldest > DELAY_NOTE_S) lines.push('Some of these are delayed up to ' + dur_(m.now - oldest) + ' (logger or internet offline).');
  return layout_(m, list.length + ' alerts', lines, rows, list.length + ' alerts: ' + names.join('; '));
}

/** Test e-mail. o = {ts, src, to, on, sent, maxPerDay, rid, rule}. */
function testMail_(m, o, rows) {
  var lines = ['This is a test e-mail from your solar logger mailer, requested from ' + (o.src || 'the dashboard') +
               ' at ' + hhmm_(o.ts) + '.',
               'If you can read this, alert e-mails reach you.',
               'Recipients: ' + o.to.join(', ') + '.',
               'Alert e-mails are ' + (o.on ? 'ON' : 'OFF') + '. E-mails sent today: ' + o.sent + ' of ' + o.maxPerDay + '.'];
  var subj = 'Test e-mail - alerts are working';
  if (o.rule) {
    subj = '[TEST] ' + clip_(o.rule.name, 60);
    lines.unshift('Rule ' + o.rid + ': "' + clip_(o.rule.name, 60) + '" (' + (o.rule.on ? 'on' : 'off') +
                  '). A real alert from this rule looks like this one.');
  }
  return layout_(m, o.rule ? 'Test of rule: ' + clip_(o.rule.name, 60) : 'Test e-mail', lines, rows, subj);
}

/** "Daily e-mail limit reached" notice (once per day). */
function capMail_(m, maxPerDay, hard) {
  return layout_(m, 'Daily e-mail limit reached',
    ['The logger reached today\'s limit of ' + maxPerDay + ' alert e-mails.',
     'Until midnight only Alert/Critical (severity 4-5) e-mails are sent. Everything is still listed in the dashboard (Alerts > History).',
     'Raise or lower the limit in Alerts > E-mail; the hard ceiling is ' + hard + ' (Apps Script allows 100 recipients per day).'],
    null, 'Daily e-mail limit reached');
}

/** "Logger offline" e-mail. ts = last live.ts, age = now - ts. */
function offlineMail_(m, ts, age, clear) {
  return layout_(m, 'Logger offline since ' + hhmm_(ts),
    ['No data from the logger since ' + when_(ts) + ' (' + dur_(age) + ' ago).',
     'Likely causes: power cut with the router off (load-shedding), home internet down, Wi-Fi changed, or the logger crashed.',
     'Data is still recorded locally while the logger has power and is uploaded when it reconnects.',
     clear ? 'You will get one e-mail when it is back.' : ''],
    null, 'ALERT: Logger offline since ' + hhmm_(ts));
}

/** "Logger back online" e-mail. down = seconds it was silent. */
function onlineMail_(m, ts, down) {
  return layout_(m, 'Logger back online',
    ['The logger is sending data again (' + when_(ts) + '). It was silent for about ' + dur_(down) + '.',
     'Any alerts from that time arrive separately, marked "Delayed".'],
    null, 'Logger back online after ' + dur_(down));
}

/** Daily summary for yesterday. r = summaryRange_(), days = daily docs from r.from to r.y, evs = events/<y>. */
function summaryMail_(m, r, days, evs, monthStart, ruleNames) {
  days = days || {};
  var d = days[r.y], d0 = days[r.y2], mt = mtd_(days, r.cs, r.y);
  var e = function (k) { return function (o) { return o && num_(o[k]) ? kwh_(o[k]) : '-'; }; };
  var rows = [['', 'Yesterday', 'Day before', 'This cycle (' + mt.days + ' d)']];
  var row = function (label, f) { rows.push([label, f(d), f(d0), f(mt.sum)]); };
  row('Solar (logger)', e('epv'));
  if (d && num_(d.epvi) && d.epvi >= 0) row('Solar (inverter)', e('epvi'));
  row('Load', e('eld'));
  row('Grid (est.)', e('egr'));
  row('Battery charged', e('ech'));
  row('Battery discharged', e('edis'));
  row('Grid outage', function (o) { return o && num_(o.gout) ? durMin_(o.gout) : '-'; });
  var lines = [];
  if (!d) lines.push('No data for ' + r.y + ': the logger was offline all day, or it has not uploaded yet.');
  else {
    if (num_(d.eld) && d.eld > 0 && num_(d.egr)) lines.push('Solar share of load (est.): ' + Math.max(0, Math.round(100 * (1 - d.egr / d.eld))) + ' %');
    lines.push('Peak load / peak solar: ' + (num_(d.wx) ? n0_(d.wx) + ' W' : '-') + ' / ' + (num_(d.pvx) ? n0_(d.pvx) + ' W' : '-'));
    lines.push('Battery V min / max: ' + f1_(d.bvn) + ' / ' + f1_(d.bvx) + ' V; SOC min / max: ' +
               (num_(d.socn) ? Math.round(d.socn) : '-') + ' / ' + (num_(d.socx) ? Math.round(d.socx) : '-') + ' %');
    lines.push('Data coverage: ' + (num_(d.n) ? Math.round(d.n / 14.4) + ' % (' + d.n + ' of 1440 minutes)' : '-'));
  }
  var alerts = alertCounts_(evs, ruleNames);
  lines.push('Alerts: ' + (alerts.length ? alerts.join(', ') : 'none'));
  lines.push('Cycle = since ' + mt.from + ' (billing day ' + monthStart + '), ' + mt.days + ' day(s) with data.');
  var subj = fmtDay_(r.y) + ': PV ' + e('epv')(d) + ' | Load ' + e('eld')(d) + ' | Grid ' + e('egr')(d) + ' est.';
  return layout_(m, 'Your solar day - ' + fmtDay_(r.y), lines, null, subj, rows, '#/energy');
}

/** "Right now" rows from live (d1 section 4.2). */
function liveRows_(l) {
  if (!l || typeof l !== 'object') return [['Logger', 'no live data']];
  var bw = l.bw, bwWord = !num_(bw) ? '-' : bw >= 0 ? 'charging ' + n0_(bw) + ' W' : 'discharging ' + n0_(-bw) + ' W';
  return [
    ['Battery', (num_(l.soc) ? Math.round(l.soc) + ' %' : '-') + ' / ' + (num_(l.bv) ? l.bv.toFixed(1) + ' V' : '-') + ' / ' + bwWord],
    ['Solar', (num_(l.pvw) ? n0_(l.pvw) + ' W' : '-') + ', today ' + (num_(l.epv) ? kwh_(l.epv) : '-')],
    ['Load', (num_(l.w) ? n0_(l.w) + ' W' : '-') + ', today ' + (num_(l.eld) ? kwh_(l.eld) : '-')],
    ['Grid', (num_(l.gv) ? Math.round(l.gv) + ' V' : '-') + ', today ' + (num_(l.egr) ? kwh_(l.egr) : '-') + ' (est.)'],
    ['Mode', MODES[l.md] || (l.md ? String(l.md) : '-')],
    ['As of', num_(l.ts) ? when_(l.ts) : '-']];
}

/**
 * Builds {subject, text, html}. lines = paragraphs; rows = [[label, value]] snapshot; table = [[c0, c1, ...]]
 * (summary); route = PWA hash route for the "Open dashboard" button (default #/alerts). Every value is
 * HTML-escaped; the subject has CR/LF removed and is at most 120 chars.
 */
function layout_(m, title, lines, rows, subject, table, route) {
  lines = (lines || []).filter(function (s) { return s; });
  var app = String(m.appUrl || ''), link = /^https:\/\/[^\s"<>]+$/.test(app) ? app.replace(/\/?$/, '/') + (route || '#/alerts') : '';
  var foot = 'Sent by the solar logger mailer (' + m.device + '). You get this because the owner added you to the alert list. ' +
             'To stop, ask the owner to remove you (Alerts > E-mail) or to turn the rule off. Grid figures are estimates from the inverter\'s readings.';
  var t = [title, ''];
  var h = '<div style="max-width:560px;margin:0 auto;font:15px/1.45 -apple-system,Segoe UI,Roboto,Arial,sans-serif;color:#1a1a1a">' +
          '<p style="margin:0 0 4px;font-size:12px;color:#666">' + esc_(m.tag) + ' &middot; ' + esc_(m.device) + '</p>' +
          '<h1 style="margin:0 0 10px;font-size:20px">' + esc_(title) + '</h1>';
  if (table) {
    h += '<table role="presentation" style="border-collapse:collapse;font-size:14px;margin:8px 0 14px">';
    table.forEach(function (r, i) {
      t.push(pad_(r[0], 20) + r.slice(1).map(function (c) { return pad_(c, 14); }).join('').replace(/\s+$/, ''));
      h += '<tr>' + r.map(function (c, j) {
        var tag = i ? 'td' : 'th';
        return '<' + tag + ' style="padding:3px 12px 3px 0;text-align:' + (j ? 'right' : 'left') + ';color:' + (j ? '#1a1a1a' : '#666') + '">' +
               esc_(c) + '</' + tag + '>';
      }).join('') + '</tr>';
    });
    h += '</table>';
    t.push('');
  }
  t = t.concat(lines);
  h += lines.map(function (s) { return '<p style="margin:0 0 8px">' + esc_(s) + '</p>'; }).join('');
  if (rows) {
    t.push('', 'Right now:');
    h += '<h2 style="margin:16px 0 6px;font-size:15px">Right now</h2><table role="presentation" style="border-collapse:collapse;font-size:14px">';
    rows.forEach(function (r) {
      t.push('  ' + pad_(r[0], 10) + r[1]);
      h += '<tr><td style="padding:2px 12px 2px 0;color:#666">' + esc_(r[0]) + '</td><td>' + esc_(r[1]) + '</td></tr>';
    });
    h += '</table>';
  }
  if (link) {
    t.push('', 'Dashboard: ' + link);
    h += '<p style="margin:18px 0"><a href="' + esc_(link) + '" style="display:inline-block;padding:10px 16px;background:#0b6bcb;color:#fff;text-decoration:none;border-radius:8px">Open dashboard</a></p>';
  }
  t.push('', '--', foot);
  h += '<p style="margin:16px 0 0;font-size:12px;color:#666">' + esc_(foot) + '</p></div>';
  return { subject: clip_('[' + m.tag + '] ' + subject, 120), text: t.join('\n'), html: h };
}

/** "Value: 5.01 kWh, limit 5.00 kWh [Grid energy, today, est.]" for rule events whose metric is known. */
function valueLine_(ev, rule, kind) {
  var mt = rule && METRICS[rule.metric];
  if (!mt || !num_(ev.val)) return '';
  var s = 'Value: ' + fmtVal_(rule.metric, ev.val);
  if (num_(ev.thr)) s += ', ' + (kind === 'c' ? 'clear level ' : 'limit ') + fmtVal_(rule.metric, ev.thr);
  var scope = rule.scope === 'day' ? ', today' : rule.scope === 'month' ? ', this billing cycle' : '';
  return s + ' [' + mt[0] + scope + (mt[2] ? ', est.' : '') + ']';
}

/** Number formats of r8 section 4.5, in the metric's canonical unit. */
function fmtVal_(metric, v) {
  var mt = METRICS[metric];
  if (!num_(v)) return '-';
  if (!mt) return String(v);
  switch (mt[1]) {
    case 'W': return n0_(v) + ' W';
    case 'Wh': return Math.abs(v) >= 1000 ? kwh_(v) : Math.round(v) + ' Wh';
    case 'V': return v.toFixed(1) + ' V';
    case 'Hz': return v.toFixed(1) + ' Hz';
    case '%': return Math.round(v) + ' %';
    case 'A': return Math.round(v) + ' A';
    case 'C': return Math.round(v) + ' °C';
    case 'min': return durMin_(v);
    case 'bool': return v ? 'yes' : 'no';
  }
  return String(v);
}

// ============================================================================================ small helpers
function evMsg_(ev) { return clip_(ev.msg || typeLabel_(ev.type) || 'Alert', 200); }
function sevName_(s) { return SEVN[s] || String(s); }
function typeLabel_(t) { return t ? (EVENT_LABELS[t] || String(t)) : ''; }
function obj_(v) { return v && typeof v === 'object' ? v : {}; }
function parseObj_(json) { try { var v = JSON.parse(json || '{}'); return obj_(v); } catch (e) { return {}; } }
function int_(v, lo, hi, def) { return num_(v) ? Math.min(hi, Math.max(lo, Math.round(v))) : def; }
function list_(s) { return String(s || '').split(/[,;\s]+/).filter(function (a) { return a; }); }
function enc_(s) { return encodeURIComponent(s); }
function num_(v) { return typeof v === 'number' && isFinite(v); }
function clip_(s, n) { s = String(s === undefined || s === null ? '' : s).replace(/[\r\n]+/g, ' '); return s.length > n ? s.slice(0, n - 1) + '…' : s; }
function pad_(s, n) { s = String(s); return s.length >= n ? s + ' ' : s + new Array(n - s.length + 1).join(' '); }
function n0_(v) { return String(Math.round(v)).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }
function f1_(v) { return num_(v) ? v.toFixed(1) : '-'; }
function kwh_(wh) { return (wh / 1000).toFixed(2) + ' kWh'; }
function esc_(s) {
  return String(s === undefined || s === null ? '' : s).replace(/[&<>"']/g, function (c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
  });
}
function z2_(n) { return (n < 10 ? '0' : '') + n; }

// Local time = UTC+6, computed without Utilities.formatDate so the same code runs in Node.
function loc_(s) { return new Date((Number(s) + TZ_OFFSET_S) * 1000); }
function dkey_(s) { var d = loc_(s); return d.getUTCFullYear() + '-' + z2_(d.getUTCMonth() + 1) + '-' + z2_(d.getUTCDate()); }
function hhmm_(s) { if (!(Number(s) > 0)) return '--:--'; var d = loc_(s); return z2_(d.getUTCHours()) + ':' + z2_(d.getUTCMinutes()); }
function when_(s) {
  if (!(Number(s) > 0)) return 'unknown time';
  var d = loc_(s);
  return WDAY[d.getUTCDay()] + ' ' + d.getUTCDate() + ' ' + MON[d.getUTCMonth()] + ' ' + d.getUTCFullYear() + ', ' + hhmm_(s);
}
function localHour_(s) { return loc_(s).getUTCHours(); }
function dayUtc_(dkey) { var p = String(dkey).split('-').map(Number); return new Date(Date.UTC(p[0], p[1] - 1, p[2], 12)); }
function fmtDay_(dkey) { var d = dayUtc_(dkey); return WDAY[d.getUTCDay()] + ' ' + d.getUTCDate() + ' ' + MON[d.getUTCMonth()]; }
function addDays_(dkey, n) {
  var d = new Date(dayUtc_(dkey).getTime() + n * 86400000);
  return d.getUTCFullYear() + '-' + z2_(d.getUTCMonth() + 1) + '-' + z2_(d.getUTCDate());
}
/** First day (yyyy-mm-dd) of the billing cycle that contains dkey, for billing day sd (1-28). */
function cycleStart_(dkey, sd) {
  var p = String(dkey).split('-').map(Number), y = p[0], mo = p[1];
  if (p[2] < sd) { mo--; if (mo < 1) { mo = 12; y--; } }
  return y + '-' + z2_(mo) + '-' + z2_(sd);
}
/** Minutes -> "42 min", "2 h 14 min", "1 d 3 h". */
function durMin_(mins) { mins = Math.max(0, Math.round(mins)); return mins < 60 ? mins + ' min' : dur_(mins * 60); }
function dur_(s) {
  s = Math.max(0, Math.round(s));
  if (s >= 86400) return Math.floor(s / 86400) + ' d ' + Math.floor((s % 86400) / 3600) + ' h';
  if (s >= 3600) return Math.floor(s / 3600) + ' h ' + z2_(Math.floor((s % 3600) / 60)) + ' min';
  if (s >= 60) return Math.floor(s / 60) + ' min';
  return s + ' s';
}
