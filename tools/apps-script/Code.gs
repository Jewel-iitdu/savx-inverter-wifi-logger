/**
 * Savx logger mailer (Google Apps Script, V8 runtime) - entry points and I/O.
 * Owned by the DEDICATED sender Gmail account. Design: research/r8-email-alerts.md section 5.
 * Pure logic (gates, de-dup ledger, caps, templates, time) is in Lib.gs; both files share one global scope.
 *
 * Every 5 min (time trigger `poll`) and on an optional nudge from the logger (`doGet`, v1.1):
 *   1. GET /devices/<DEVICE>/mailq            (usually null)
 *   2. mailq/test (owner's "Send test e-mail") -> test mail, result in mailer/test
 *   3. for each queued job: GET the event, check gates, send with MailApp, then
 *      PATCH events/<d>/<k>/em = "sent" (+ emt) and delete mailq/<k> (one multi-path PATCH)
 *   4. logger-offline / back-online check on live/ts
 *   5. daily summary for yesterday at alerts/email/summary/hour
 *   6. PATCH /devices/<DEVICE>/mailer = heartbeat {hb, ver, day, sent, quota, off, err, warn[, last]}
 * RTDB access: Firebase Auth user "mailer" (email/password) -> ID token -> ?auth=<token>.
 * The rules pin MAILER_UID to: read mailq, alerts, live, daily, events, mailer; write mailq, mailer and
 * events/<d>/<k>/em|emt only. Recipients come ONLY from alerts/email/to, filtered by ALLOW_TO.
 *
 * Script properties (Project Settings -> Script properties):
 *   FB_API_KEY  FB_DB_URL  FB_EMAIL  FB_PASSWORD  ALLOW_TO                     (required)
 *   APP_URL  NUDGE_KEY  DEVICE (inv1)  SUBJECT_TAG (Solar)  MAX_PER_DAY (40)    (optional)
 * Written by the script: RTOK (refresh token), STATE (day counters), SENT (de-dup ledger).
 *
 * Functions to run by hand from the editor: sendSelfTest, setup, sendTestEmail, showStatus, uninstall.
 */

// =============================================================================================== entry points
/** Run once by hand after setting the script properties: signs in, checks access, installs the trigger. */
function setup() {
  ['FB_API_KEY', 'FB_DB_URL', 'FB_EMAIL', 'FB_PASSWORD', 'ALLOW_TO'].forEach(need_);
  if (!/^https?:\/\/[^\s\/]+\/?$/.test(prop_('FB_DB_URL'))) {
    throw new Error('FB_DB_URL must be the bare database URL, e.g. https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app');
  }
  var badAllow = list_(prop_('ALLOW_TO')).filter(function (a) { return !RE_EMAIL.test(a); });
  if (badAllow.length) throw new Error('ALLOW_TO has an entry that is not an e-mail address: ' + badAllow.join(', '));
  if (prop_('APP_URL') && !/^https:\/\//.test(prop_('APP_URL'))) Logger.log('WARNING: APP_URL must start with https:// - the "Open dashboard" button is left out.');
  if (prop_('NUDGE_KEY') && prop_('NUDGE_KEY').length < NUDGE_MIN_KEY) Logger.log('WARNING: NUDGE_KEY is shorter than ' + NUDGE_MIN_KEY + ' chars, so doGet refuses every nudge.');
  ScriptApp.requireAllScopes(ScriptApp.AuthMode.FULL);   // consent for the 3 manifest scopes
  var cache = CacheService.getScriptCache();
  cache.remove('idt');
  cache.remove('mcfg');
  SP_().deleteProperty('RTOK');
  token_(true);                                   // throws if the mailer user cannot sign in
  db_('get', 'mailq');                            // throws "HTTP 401" if MAILER_UID is not in the rules yet
  ScriptApp.getProjectTriggers().forEach(function (t) {
    if (t.getHandlerFunction() === 'poll') ScriptApp.deleteTrigger(t);
  });
  ScriptApp.newTrigger('poll').timeBased().everyMinutes(5).create();
  var r = withLock_(20000, function () { return run_('setup'); });
  if (!r) throw new Error('Another mailer run is busy. Wait a minute and run setup again.');
  Logger.log('OK: signed in to RTDB, trigger "poll" installed (every 5 min), first run sent ' + r.sent +
             ' recipient-deliveries. MailApp quota left today: ' + MailApp.getRemainingDailyQuota());
}

/** Optional manual check of MailApp alone (no RTDB): one mail to the first ALLOW_TO address. */
function sendSelfTest() {
  var to = list_(need_('ALLOW_TO'))[0];
  MailApp.sendEmail({ to: to, subject: '[' + prop_('SUBJECT_TAG') + '] Mailer self-test', name: 'Solar logger',
                      body: 'MailApp works from this account (mailer ' + VERSION + '). Quota left today: ' +
                            MailApp.getRemainingDailyQuota() });
  Logger.log('Sent to ' + to);
}

/** The dashboard's "Send test e-mail", run from the editor: same gates and recipients, result in mailer/test. */
function sendTestEmail() {
  var r = withLock_(20000, function () {
    var ctx = newCtx_(nowS_(), 'editor');
    try { return doTest_(ctx, { ts: ctx.now }, 'the script editor'); }
    finally { saveState_(ctx); }
  });
  Logger.log(!r ? 'Busy: another mailer run holds the lock. Try again in a minute.' : (r.ok ? 'OK: ' : 'NOT SENT: ') + r.msg);
}

/** Logs triggers, quota, today's counters and the ledger size. Changes nothing. */
function showStatus() {
  var trig = ScriptApp.getProjectTriggers().map(function (t) { return t.getHandlerFunction(); });
  var led = SP_().getProperty('SENT') || '{}';
  Logger.log('Mailer ' + VERSION + ' | triggers: ' + (trig.join(', ') || 'none (run setup)') +
             ' | MailApp quota left: ' + MailApp.getRemainingDailyQuota());
  Logger.log('STATE ' + (SP_().getProperty('STATE') || '{}'));
  Logger.log('SENT ledger: ' + Object.keys(parseObj_(led)).length + ' keys, ' + led.length + ' of ' + LEDGER_MAX_CHARS + ' chars');
}

/** Removes the time trigger (stops the mailer). */
function uninstall() {
  ScriptApp.getProjectTriggers().forEach(function (t) { ScriptApp.deleteTrigger(t); });
  Logger.log('All triggers removed.');
}

/** Time trigger, every 5 min. */
function poll() {
  withLock_(20000, function () { run_('timer'); });   // null = another run (nudge) is still busy
}

/** Optional nudge from the logger (v1.1): GET <exec-url>?k=<NUDGE_KEY>. No payload; the data is already in RTDB. */
function doGet(e) {
  var k = (e && e.parameter && e.parameter.k) || '';
  if (!nudgeOk_(k, prop_('NUDGE_KEY'))) return out_({ ok: false, err: 'auth' });
  var cache = CacheService.getScriptCache();
  if (cache.get('nudge')) return out_({ ok: true, skipped: 'rate' });
  cache.put('nudge', '1', NUDGE_GAP_S);
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(2000)) return out_({ ok: true, skipped: 'busy' });
  try {
    var r = run_('nudge');
    return out_({ ok: true, sent: r.sent });
  } catch (err) {
    return out_({ ok: false, err: clip_(err && err.message, 100) });
  } finally {
    lock.releaseLock();
  }
}

// =============================================================================================== one run
function newCtx_(now, why) {
  var stRaw = SP_().getProperty('STATE'), ledRaw = SP_().getProperty('SENT');
  return { now: now, today: dkey_(now), why: why, st: stateParse_(stRaw, dkey_(now)), stRaw: stRaw,
           ledger: ledgerParse_(ledRaw, now), ledRaw: ledRaw, cfg: null, live: undefined, rules: undefined,
           m: null, warn: '', err: '', last: null };
}

function run_(why) {
  var ctx = newCtx_(nowS_(), why), now = ctx.now, st = ctx.st, sent0 = st.sent;
  try {
    var q = db_('get', 'mailq');
    q = q && typeof q === 'object' ? q : {};
    if (q.test) doTest_(ctx, q.test, 'the dashboard');
    delete q.test;
    var keys = Object.keys(q).sort(function (a, b) { return (Number(obj_(q[a]).ts) || 0) - (Number(obj_(q[b]).ts) || 0); });
    var ready = [];
    keys.forEach(function (k) {
      var it = q[k], pre = jobCheck_(k, it, ctx.ledger);
      if (pre === 'drop') { db_('delete', 'mailq/' + enc_(k)); return; }       // malformed job
      if (pre) { finish_(ctx, it.d, k, pre); return; }                         // in the ledger: device re-wrote a sent job
      var ev = db_('get', 'events/' + it.d + '/' + k);
      var gate = eventCheck_(ev, cfg_(ctx).on, now);
      if (gate === 'drop') { db_('delete', 'mailq/' + k); return; }            // event gone (pruned / never written) or handled
      if (gate) { finish_(ctx, it.d, k, gate); return; }                       // off | exp
      var to = recipients_(ctx, cfg_(ctx).to, it.rc);
      if (!to.length) { finish_(ctx, it.d, k, 'norc'); return; }
      ready.push({ k: k, d: it.d, ev: ev, sev: Number(ev.sev) || 0, n: Number(it.n) || 0, to: to });
    });
    groupJobs_(ready).forEach(function (grp) {
      var list = grp.list, to = grp.to;
      var sevMax = Math.max.apply(null, list.map(function (x) { return x.sev; }));
      var gate = budget_(ctx, sevMax, to.length);
      if (gate) {
        list.forEach(function (x) { finish_(ctx, x.d, x.k, gate); });
        if (gate === 'cap') { try { capNotice_(ctx); } catch (err) { ctx.err = 'MailApp: ' + clip_(err && err.message, 90); } }
        return;
      }
      var mail = list.length === 1 ? eventMail_(menv_(ctx), list[0], snap_(ctx), ruleOf_(ctx, list[0].ev))
                                   : digestMail_(menv_(ctx), list, snap_(ctx));
      try {
        send_(ctx, to, mail);
      } catch (err) {
        ctx.err = 'MailApp: ' + clip_(err && err.message, 90);
        list.forEach(function (x) { retry_(ctx, x); });
        return;
      }
      list.forEach(function (x) { ctx.ledger[x.k] = now; });
      saveLedger_(ctx);                                                        // before the RTDB write: no re-send
      list.forEach(function (x) { finish_(ctx, x.d, x.k, 'sent'); });
    });
    [offline_, summary_].forEach(function (f) {
      try { f(ctx); } catch (err) { ctx.err = clip_(err && err.message, 110); }   // keep the run going
    });
  } catch (err) {
    ctx.err = clip_(err && err.message, 110) || 'error';
    throw err;                                    // Apps Script then e-mails the owner a failure summary
  } finally {
    saveState_(ctx);
    saveLedger_(ctx);
    try {
      var hb = { hb: now, ver: VERSION, day: ctx.today, sent: st.sent, quota: MailApp.getRemainingDailyQuota(),
                 off: st.offSince || 0, err: clip_(ctx.err, 120), warn: clip_(ctx.warn, 120) };
      if (ctx.last) hb.last = ctx.last;
      db_('patch', 'mailer', hb);
    } catch (e2) { /* RTDB unreachable: the PWA shows the heartbeat as stale */ }
  }
  return { sent: st.sent - sent0 };
}

// =============================================================================================== jobs
/** Sets events/<d>/<k>/em (+ emt) and deletes mailq/<k> in one PATCH. */
function finish_(ctx, d, k, em) {
  var p = {};
  p['mailq/' + k] = null;
  if (d) { p['events/' + d + '/' + k + '/em'] = em; p['events/' + d + '/' + k + '/emt'] = ctx.now; }
  try { db_('patch', '', p); }
  catch (err) { db_('delete', 'mailq/' + k); }    // e.g. the event no longer exists -> rules refuse em
}

function retry_(ctx, x) {
  var n = nextTry_(x.n);
  if (n === 'fail') finish_(ctx, x.d, x.k, 'fail');
  else db_('put', 'mailq/' + x.k + '/n', n);
}

/** '' = may send; else the em code. Daily cap counts recipient-deliveries per local day. */
function budget_(ctx, sev, n) {
  return budgetGate_(ctx.st.sent, cfg_(ctx).maxPerDay, sev, n, MailApp.getRemainingDailyQuota());
}

function capNotice_(ctx) {
  if (ctx.st.capNote) return;
  var cf = cfg_(ctx), to = recipients_(ctx, cf.to, 1);
  if (!to.length || budget_(ctx, 4, 1)) return;
  ctx.st.capNote = 1;
  send_(ctx, to.slice(0, 1), capMail_(menv_(ctx), cf.maxPerDay, cf.hard));
}

function send_(ctx, to, mail) {
  MailApp.sendEmail({ to: to.join(','), subject: mail.subject, body: mail.text, htmlBody: mail.html,
                      name: cfg_(ctx).fromName, replyTo: to[0] });
  ctx.st.sent += to.length;
  ctx.last = { ts: ctx.now, ok: true, msg: clip_(mail.subject, 100) };
}

// =============================================================================================== test, offline, summary
/** Answers a test request (mailq/test from the PWA, or sendTestEmail). Writes mailer/test, deletes mailq/test. */
function doTest_(ctx, t, src) {
  var ok = false, now = ctx.now, st = ctx.st, msg = testGate_(t, now, st);
  if (!msg) {
    ctx.cfg = null;                                // always fresh recipients for a test
    var cf = cfg_(ctx, true), rule = t.rid ? db_('get', 'alerts/rules/' + t.rid) : null;
    var to = recipients_(ctx, cf.to, rule && rule.rc);
    var gate = to.length ? budget_(ctx, 3, to.length) : 'norc';
    if (gate) msg = testGateMsg_(gate);
    else {
      try {
        send_(ctx, to, testMail_(menv_(ctx), { ts: t.ts, src: src, to: to, on: cf.on, sent: st.sent, maxPerDay: cf.maxPerDay,
                                               rid: t.rid, rule: rule }, snap_(ctx)));
        st.tests++; st.lastTest = now; ok = true;
        msg = 'sent to ' + to.length + ' recipient' + (to.length > 1 ? 's' : '') + (cf.on ? '' : ' (alert e-mails are switched OFF)');
      } catch (err) {
        msg = 'MailApp: ' + clip_(err && err.message, 80);
        ctx.err = msg;
      }
    }
  }
  db_('patch', '', { 'mailq/test': null, 'mailer/test': { ts: Number(t && t.ts) || now, ok: ok, msg: clip_(msg, 100) } });
  return { ok: ok, msg: msg };
}

function offline_(ctx) {
  var cf = cfg_(ctx), st = ctx.st, now = ctx.now;
  if (!cf.on || !cf.offline.on) { st.offSince = 0; return; }
  var ts = ctx.live ? Number(ctx.live.ts) : Number(db_('get', 'live/ts'));
  var step = offlineStep_(cf.offline, st.offSince, ts, now), to;
  if (step === 'offline') {
    st.offSince = ts;
    to = recipients_(ctx, cf.to, 0);
    if (to.length && st.offMails < OFFLINE_MAILS_PER_DAY && !budget_(ctx, 4, to.length)) {
      st.offMails++;
      send_(ctx, to, offlineMail_(menv_(ctx), ts, now - ts, cf.offline.clear));
    }
  } else if (step === 'online') {
    var down = ts - st.offSince;
    st.offSince = 0;
    to = recipients_(ctx, cf.to, 0);
    if (cf.offline.clear && to.length && st.offMails < OFFLINE_MAILS_PER_DAY && !budget_(ctx, 4, to.length)) {
      st.offMails++;
      send_(ctx, to, onlineMail_(menv_(ctx), ts, down));
    }
  }
}

function summary_(ctx) {
  var cf = cfg_(ctx), st = ctx.st;
  if (!summaryDue_(cf, st.sumDay, ctx.today, ctx.now)) return;
  var r = summaryRange_(ctx.today, cf.monthStart);
  var days = db_('get', 'daily', undefined, '&orderBy=' + enc_('"$key"') + '&startAt=' + enc_('"' + r.from + '"') +
                 '&endAt=' + enc_('"' + r.y + '"')) || {};
  var evs = db_('get', 'events/' + r.y) || {};
  var to = recipients_(ctx, cf.to, 0);
  st.sumDay = ctx.today;                                               // once per day, even if refused below
  if (!to.length || budget_(ctx, 1, to.length)) return;
  send_(ctx, to, summaryMail_(menv_(ctx), r, days, evs, cf.monthStart, ruleNames_(ctx, evs)));
}

// =============================================================================================== config, recipients, state
function cfg_(ctx, fresh) {
  if (ctx.cfg && !fresh) return ctx.cfg;
  var cache = CacheService.getScriptCache(), raw = fresh ? null : cache.get('mcfg'), c = null;
  if (raw) { try { c = JSON.parse(raw); } catch (e) { c = null; } }
  if (!c) { c = db_('get', 'alerts/email') || {}; cache.put('mcfg', JSON.stringify(c), CFG_CACHE_S); }
  ctx.cfg = normCfg_(c, prop_('MAX_PER_DAY'));
  return ctx.cfg;
}

/** alerts/email/to filtered by rc and ALLOW_TO; records a warning for listed-but-not-allowed addresses. */
function recipients_(ctx, toStr, rc) {
  var r = pickRecipients_(toStr, rc, prop_('ALLOW_TO'));
  if (r.bad.length) ctx.warn = clip_('Not on ALLOW_TO: ' + r.bad.join(', '), 120);
  return r.to;
}

/** Mail environment for the templates in Lib.gs. */
function menv_(ctx) {
  if (!ctx.m) ctx.m = { now: ctx.now, tag: prop_('SUBJECT_TAG'), device: prop_('DEVICE'), appUrl: prop_('APP_URL') };
  return ctx.m;
}

/** "Right now" rows; live is read at most once per run. */
function snap_(ctx) {
  if (ctx.live === undefined) ctx.live = db_('get', 'live');
  return liveRows_(ctx.live);
}

/** alerts/rules, read at most once per run and only when a mail needs a rule's metric or name. Cosmetic: errors are ignored. */
function rules_(ctx) {
  if (ctx.rules === undefined) { try { ctx.rules = obj_(db_('get', 'alerts/rules')); } catch (e) { ctx.rules = {}; } }
  return ctx.rules;
}
function ruleOf_(ctx, ev) { return ev && ev.rid && RE_RID.test(String(ev.rid)) ? obj_(rules_(ctx)[ev.rid]) : null; }
function ruleNames_(ctx, evs) {
  var any = Object.keys(evs || {}).some(function (k) { return evs[k] && evs[k].rid && evs[k].st === 'open'; });
  if (!any) return {};
  var r = rules_(ctx), names = {};
  Object.keys(r).forEach(function (rid) { if (r[rid] && r[rid].name) names[rid] = String(r[rid].name); });
  return names;
}

function saveState_(ctx) {
  var s = JSON.stringify(ctx.st);
  if (s !== ctx.stRaw) { SP_().setProperty('STATE', s); ctx.stRaw = s; }
}
function saveLedger_(ctx) {
  var s = ledgerDump_(ctx.ledger, LEDGER_MAX_CHARS);
  if (s !== ctx.ledRaw) { SP_().setProperty('SENT', s); ctx.ledRaw = s; }
}

// =============================================================================================== Firebase
function token_(force) {
  var cache = CacheService.getScriptCache();
  if (!force) { var c = cache.get('idt'); if (c) return c; }
  var key = enc_(need_('FB_API_KEY')), tok = null, rt = SP_().getProperty('RTOK');
  if (rt) {
    var r = UrlFetchApp.fetch('https://securetoken.googleapis.com/v1/token?key=' + key, {
      method: 'post', contentType: 'application/x-www-form-urlencoded', muteHttpExceptions: true,
      payload: 'grant_type=refresh_token&refresh_token=' + enc_(rt) });
    if (r.getResponseCode() === 200) {
      var j = JSON.parse(r.getContentText());
      tok = j.id_token;
      if (j.refresh_token && j.refresh_token !== rt) SP_().setProperty('RTOK', j.refresh_token);
    }
  }
  if (!tok) {
    var r2 = UrlFetchApp.fetch('https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=' + key, {
      method: 'post', contentType: 'application/json', muteHttpExceptions: true,
      payload: JSON.stringify({ email: need_('FB_EMAIL'), password: need_('FB_PASSWORD'), returnSecureToken: true }) });
    if (r2.getResponseCode() !== 200) throw new Error('Firebase sign-in failed: HTTP ' + r2.getResponseCode());
    var j2 = JSON.parse(r2.getContentText());
    tok = j2.idToken;
    SP_().setProperty('RTOK', j2.refreshToken);
  }
  cache.put('idt', tok, 3000);                    // ID tokens live 3600 s
  return tok;
}

/** RTDB REST under /devices/<DEVICE>. GET returns parsed JSON; writes use print=silent. */
function db_(method, path, body, query) {
  var base = need_('FB_DB_URL').replace(/\/+$/, '') + '/devices/' + prop_('DEVICE');
  for (var attempt = 0; attempt < 2; attempt++) {
    var url = base + (path ? '/' + path : '') + '.json?auth=' + enc_(token_(attempt > 0)) +
              (method === 'get' ? '' : '&print=silent') + (query || '');
    var opt = { method: method, muteHttpExceptions: true };
    if (body !== undefined) { opt.contentType = 'application/json'; opt.payload = JSON.stringify(body); }
    var r = UrlFetchApp.fetch(url, opt), c = r.getResponseCode();
    if (c === 200) return method === 'get' ? JSON.parse(r.getContentText()) : true;
    if (c === 204) return true;
    if (c === 401 && attempt === 0) continue;     // expired token: retry once; a second 401 = refused by the rules
    var e = new Error('RTDB ' + method.toUpperCase() + ' /' + (path || '') + ' -> HTTP ' + c + ' ' + clip_(r.getContentText(), 60));
    e.code = c;
    throw e;
  }
}

// =============================================================================================== helpers
function SP_() { return PropertiesService.getScriptProperties(); }
function prop_(k) { var v = SP_().getProperty(k); v = v === null || v === undefined ? '' : String(v).trim(); return v === '' ? (DEF[k] !== undefined ? DEF[k] : '') : v; }
function need_(k) { var v = prop_(k); if (!v) throw new Error('Script property ' + k + ' is not set'); return v; }
function out_(o) { return ContentService.createTextOutput(JSON.stringify(o)).setMimeType(ContentService.MimeType.JSON); }
function nowS_() { return Math.floor(Date.now() / 1000); }
function withLock_(ms, fn) {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(ms)) return null;
  try { return fn(); } finally { lock.releaseLock(); }
}
