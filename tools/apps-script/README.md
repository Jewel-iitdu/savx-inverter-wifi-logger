# Savx mailer: e-mail alerts with Google Apps Script (`tools/apps-script/`)

This folder holds the free e-mail sender for the logger's usage-threshold alerts. It is a small Google Apps Script project that you paste into script.google.com once, signed in as a **dedicated Gmail sender account**. Every 5 minutes it reads a short outbox (`mailq`) in your Firebase Realtime Database (RTDB), sends the queued alerts with Gmail, and writes the result back. It also sends a daily summary and a "logger offline" e-mail.

Firebase Spark (the free plan) has no Cloud Functions, and the logger should not hold a mailbox password, so the mailer lives in your own Google account instead. It costs nothing and needs no domain.

The design, and every number in this file, comes from the decision record [research/r8-email-alerts.md](../../research/r8-email-alerts.md). The code is the r8 §5.3 listing, split into a pure part and an I/O part so it can be tested on the Mac. §14 lists the differences.

> ⚠️ **Nothing here touches the inverter.** The mailer only reads values the logger has already uploaded. The COM wiring (pins 1/2/8 only, never pin 4, never the BMS jack), the idle-level test, the explicit arm and `INV_ALLOW_WRITES=0` are unchanged. No new hardware, and every part runs on a free tier.

> ⚠️ **Deploy order: rules → mailer → PWA → firmware** (r8 §3.11). Until the database rules include the `MAILER_UID` grants, `setup` fails with `HTTP 401`. That is harmless, but do the steps in this order.

---

## Contents

| File | What it is |
|---|---|
| [`appsscript.json`](appsscript.json) | The manifest: time zone `Asia/Dhaka`, V8 runtime, exactly 3 OAuth scopes, web-app settings for the optional nudge |
| [`Code.gs`](Code.gs) | Entry points (`setup`, `poll`, `doGet`, …) and all I/O: RTDB REST, MailApp, Properties, Cache, Lock |
| [`Lib.gs`](Lib.gs) | Pure logic: gates, daily cap, de-dup ledger, recipient allow-list, offline detector, e-mail templates (HTML + text), local time. No Apps Script service, so it also runs in Node |
| [`test/`](test/) | `lib.test.js` (pure logic, with JSON fixtures and golden e-mail texts), `mailer.test.js` (both files against fake Apps Script services and a fake RTDB), `e2e.js` (real rules in the Firebase emulator) |
| [`package.json`](package.json) | `npm test` / `npm run check` only. There are no dependencies and nothing is deployed by npm |

---

## 1. How it works

```
 ESP32 logger                         Firebase RTDB  /devices/inv1/            Apps Script (sender Gmail account)
 ────────────                         ─────────────────────────────            ──────────────────────────────────
 rule fires ──PATCH (one request)──▶  events/<day>/<key> {…,"em":"q"}
                                      mailq/<key>         {d, ts, sev[, rc]}
                                                 ▲                 │  every 5 min (trigger "poll")
                                                 │                 └─GET mailq ── null when idle
                                                 │                   GET events/<day>/<key>, alerts/email, live
                                                 │                   MailApp.sendEmail ──▶ Gmail ──▶ your inbox
                                                 └───────────────────PATCH em:"sent", emt, mailq/<key>:null
                                      mailer {hb, sent, quota, err, warn, last, test} ◀── heartbeat every run
 PWA "Send test e-mail" ──set──▶      mailq/test {ts[, rid]}  ──next run──▶ test mail ──▶ mailer/test {ok, msg}
```

What one run does, in order:

| Step | Reads | Writes | Notes |
|---|---|---|---|
| 1. Outbox | `mailq` | – | One small GET; `null` when there is nothing to do |
| 2. Test request | `alerts/email` (fresh), `alerts/rules/<rid>` | `mailer/test`, deletes `mailq/test` | ≥ 60 s apart, ≤ 10 per day, request ≤ 15 min old |
| 3. Each job | `events/<d>/<k>`, `alerts/email` (cached 10 min), `live`, `alerts/rules` (only for a single rule alert) | `events/<d>/<k>/em` + `emt`, deletes `mailq/<k>` | Jobs for the same recipients in one run become one digest e-mail |
| 4. Logger offline | `live/ts` | – | One "offline" mail after `offline.min` minutes, one "back online" mail, ≤ 4 per day |
| 5. Daily summary | `daily` (yesterday, the day before, the billing cycle), `events/<yesterday>` | – | Once per day at or after `summary.hour` (default 07:00) |
| 6. Heartbeat | – | `mailer` | Always attempted, even after an error |

Who decides what (r8 §2): the **logger** evaluates your rules (hold times, hysteresis, cooldown, per-rule daily limit) and queues a job. The **mailer** decides who receives it, enforces the global daily cap, de-duplicates, and renders the e-mail. Only the mailer can notice that the logger itself has gone silent.

**Limits built into the code** (r8 §5.4):

| Limit | Value |
|---|---|
| Recipient-deliveries per local day | `alerts/email/maxPerDay`, at most 40 (the `MAX_PER_DAY` property can only lower it). The last 5 are kept for severity ≥ 4 (Alert/Critical). One "limit reached" notice per day |
| Google quota floor | Below 10 left in `MailApp.getRemainingDailyQuota()`, only severity ≥ 4 is sent |
| Old jobs | Older than 12 h (severity ≥ 4: 24 h) → `em:"exp"`, not e-mailed. A recovered mailer never floods you |
| Send errors | 5 MailApp failures per job → `em:"fail"` |
| De-dup | Every sent key is kept 72 h in the `SENT` property (≤ 8,000 chars), saved **before** the RTDB write. A job the logger re-queues after a lost reply is not sent again |
| Recipients | Only addresses that are in **both** `alerts/email/to` (≤ 3, set in the PWA) **and** the `ALLOW_TO` script property |

**Google's consumer quotas** (Apps Script quotas page, checked 2026-10-21; Google may change them "without notice"): 100 e-mail recipients per day, 90 min per day of trigger runtime, 20,000 URL fetches per day, 9 KB per property value. An idle run makes 3 requests and takes about 1–2 s, so the mailer uses roughly 5 % of the fetch quota and ≤ 11 % of the runtime quota [INFERRED, r8 §1.1; check the Executions page in week 1].

---

## 2. What you need

| Item | Where it comes from |
|---|---|
| A new Gmail account used only for sending | Step 1 |
| A third Firebase Auth user, `mailer` | Step 2 |
| Database rules with the `MAILER_UID` grants (r8 §3.11), deployed | Step 3 |
| The **device API key** (`savx-device`, no application restriction) | [r7 §6.2](../../research/r7-web-hosting.md). The mailer signs in from Google's servers and sends no `Referer`, so the referrer-restricted browser key would be refused once you restrict it |
| Your dashboard URL | [r7 §2](../../research/r7-web-hosting.md): `https://solar.<acct>.workers.dev/` (fallback `https://<project>.web.app/`) |
| `openssl` in the Mac terminal | Already on macOS, for random passwords |

---

## 3. Step 1: a dedicated sender account

1. Create a new Gmail account, for example `<you>.solar.alerts@gmail.com`. Google may ask for phone verification.
2. Turn on **2-Step Verification** for it.
3. Use it **only** for this script. Alerts go to your normal address, not to this one. (Mail that a Gmail account sends to itself may not raise a phone notification [INFERRED, r8a].)

> ⚠️ **Sign in to the sender account at least once a year.** Google may delete personal accounts that have been inactive for 2 years, and script activity may not count as activity (r8c M4). Put a yearly reminder in your calendar.

## 4. Step 2: the Firebase `mailer` user

1. Firebase console → **Authentication** → **Users** → **Add user**.
2. Email: `mailer@<anything>.invalid`. It never receives mail; it is only a login.
3. Password: generate one in the Mac terminal and copy it:
   ```bash
   openssl rand -base64 24
   ```
4. Copy the new user's **User UID**. You need it in step 3.

## 5. Step 3: database rules with `MAILER_UID`

The rules file gains a third placeholder, `MAILER_UID`, next to `OWNER_UID` and `DEVICE_UID` (r8 §3.11). The mailer may then:

- **read** `mailq`, `alerts`, `live`, `daily`, `events`, `mailer`;
- **write** `mailq`, `mailer`, and only the `em` / `emt` fields of an **existing** event.

It cannot read `cfg`, `status`, `meta` or `hist`, cannot change recipients or rules, and cannot create or edit events.

1. Check that the merge is in the repository:
   ```bash
   grep -c MAILER_UID firebase/database.rules.json    # must print a number > 0
   ```
   If it prints `0`, the rules merge of r8 §3.11 has not been done yet. Stop here; `setup` cannot work without it.
2. Render the rules with all three UIDs. Never edit real UIDs into the committed file: CI refuses a rules file without its placeholders, and only the rendered copy in the git-ignored `.deploy/` folder is deployed ([firebase/README.md](../../firebase/README.md) §3):
   ```bash
   cd ~/Office/CodeBase/Personal/Datalogger
   OWNER_UID=PASTE_OWNER_UID DEVICE_UID=PASTE_DEVICE_UID MAILER_UID=PASTE_MAILER_UID sh tools/render-rules.sh
   grep -c "OWNER_UID\|DEVICE_UID\|MAILER_UID" .deploy/database.rules.json   # must print 0
   ```
3. Publish: on GitHub set the repository Variable `FIREBASE_MAILER_UID` and run **Actions → deploy-rules** ([docs/06](../../docs/06-cloud-and-dashboard-guide.md) §9.2), or deploy `.deploy/` from the Mac ([docs/06](../../docs/06-cloud-and-dashboard-guide.md) §3.3), or paste `.deploy/database.rules.json` into the console (**Realtime Database → Rules → Publish**). Then rerun the refusal tests of firebase/README.md §5 with the device key.

## 6. Step 4: create the Apps Script project

Do all of this **signed in as the sender account** (check the avatar in the top-right corner).

### 6.1 Project and files

1. Open <https://script.google.com> → **New project**. Click "Untitled project" and rename it `Savx mailer`.
2. **Project Settings** (gear icon, left):
   - tick **Show "appsscript.json" manifest file in editor**;
   - leave the Google Cloud project as the default one. Do **not** switch to a standard GCP project: that can bring a 7-day refresh-token expiry (r8c M5).
3. **Editor** (`< >` icon). Paste the three files from this folder:

   | In the editor | Paste |
   |---|---|
   | `appsscript.json` (now visible) | all of [`appsscript.json`](appsscript.json) |
   | `Code.gs` (exists already) | all of [`Code.gs`](Code.gs), replacing the default `myFunction` |
   | **Files `+` → Script**, name it `Lib` (the editor adds `.gs`) | all of [`Lib.gs`](Lib.gs) |

4. Press **Save** (disk icon, or ⌘S). The order of the files in the editor does not matter.

### 6.2 Script properties

**Project Settings → Script Properties → Add script property**, one row each:

| Property | Required | Value |
|---|---|---|
| `FB_API_KEY` | yes | The **device key** (§2). Not a secret, but use this one, not the browser key |
| `FB_DB_URL` | yes | `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app` (no path, no `.json`) |
| `FB_EMAIL` | yes | `mailer@<anything>.invalid` (step 2) |
| `FB_PASSWORD` | yes | The password from step 2 |
| `ALLOW_TO` | yes | The only addresses that may ever receive mail, comma-separated, e.g. `you@gmail.com,family@gmail.com`. This is the second lock behind the PWA's recipient list |
| `APP_URL` | no | Your dashboard URL (§2). Adds an **Open dashboard** button. Must start with `https://` |
| `SUBJECT_TAG` | no | Default `Solar` → subjects start with `[Solar]`. Keep it stable for the Gmail filter in step 6 |
| `MAX_PER_DAY` | no | Default `40`. Can only lower the daily cap |
| `DEVICE` | no | Default `inv1` |
| `NUDGE_KEY` | no | Only for the optional nudge (§9) |

The script writes `RTOK` (Firebase refresh token), `STATE` (today's counters) and `SENT` (de-dup ledger) itself. Do not edit them.

> ⚠️ Script properties are visible to anyone who can edit this project. Keep the project private to the sender account. Never share it, and never paste `FB_PASSWORD` into a chat or a screenshot.

### 6.3 Authorise and check MailApp alone

1. In the editor, choose the function **`sendSelfTest`** in the toolbar drop-down → **Run**.
2. "Authorization required" → **Review permissions** → pick the **sender** account.
3. "Google hasn't verified this app" is normal for your own script [INFERRED, r8 §7]: **Advanced → Go to Savx mailer (unsafe)**.
4. Allow all three permissions. If Google shows checkboxes, tick all of them:

   | Shown as | Manifest scope | Used for |
   |---|---|---|
   | Send email as you | `script.send_mail` | `MailApp.sendEmail` |
   | Connect to an external service | `script.external_request` | `UrlFetchApp` (Firebase sign-in and RTDB) |
   | Allow this application to run when you are not present | `script.scriptapp` | the 5-minute trigger |

5. The log says `Sent to <first ALLOW_TO address>`. Check that the e-mail `[Solar] Mailer self-test` arrives.

### 6.4 Run `setup`

1. Choose **`setup`** → **Run**.
2. The execution log must end with:
   ```
   OK: signed in to RTDB, trigger "poll" installed (every 5 min), first run sent 0 recipient-deliveries. …
   ```
3. If it fails instead:

   | Error | Meaning | Fix |
   |---|---|---|
   | `Script property X is not set` | A required property is missing | §6.2 |
   | `FB_DB_URL must be the bare database URL` | It has a path or `.json` | Use only `https://…firebasedatabase.app` |
   | `ALLOW_TO has an entry that is not an e-mail address` | Typo in `ALLOW_TO` | Fix the list |
   | `Firebase sign-in failed: HTTP 400` | Wrong `FB_EMAIL` / `FB_PASSWORD`, or Email/Password sign-in is disabled | Step 2 |
   | `Firebase sign-in failed: HTTP 403` | The API key refuses the request (referrer-restricted browser key) | Use the device key |
   | `RTDB GET /mailq -> HTTP 401` | The published rules do not grant your mailer UID | Step 3 |
   | An authorization error | One of the three permissions was not granted | Run `setup` again and allow all three |

`setup` can be run again at any time: it replaces the trigger instead of adding a second one.

### 6.5 Failure notifications

1. **Triggers** (alarm-clock icon) → the `poll` trigger → pencil icon.
2. **Failure notification settings: Notify me immediately** → **Save**.

These notices go to the sender account. If you want them in your main inbox, add a forwarding filter there. Apps Script sends one only when a run throws (RTDB or sign-in errors). MailApp errors do not throw; they are recorded in `mailer/err` and retried.

## 7. Step 5: e-mail settings and the first test

### 7.1 With the PWA's Alerts page (r8 §6.4)

1. **Alerts → E-mail**: add the same recipients as in `ALLOW_TO`, switch **Send alert e-mails** on, save.
2. Press **Send test e-mail**. Within about 5 minutes the mail arrives and the app shows "Sent to N recipients".

### 7.2 Before the Alerts page exists

You can write the settings as the owner with `curl`, so the rules still validate them. In the Mac terminal, first run the setup block of [firebase/README.md](../../firebase/README.md) §5.1 (it defines `DB`, `API_KEY` and `OWNER_TOKEN`; use the device key as `API_KEY`). Then:

```bash
curl -s -X PATCH "$DB/devices/inv1.json?auth=$OWNER_TOKEN" -d '{
  "alerts/email": {"on": true, "to": "you@gmail.com", "fromName": "Solar logger", "maxPerDay": 40, "monthStart": 1,
                   "builtin": {"minSev": 4, "clear": true}, "summary": {"on": true, "hour": 7},
                   "offline": {"on": true, "min": 30, "clear": true}},
  "cfg/alertsRev": {".sv": "timestamp"}}'
```

This is the same multi-path write the PWA makes (r8 §3.10): `cfg/alertsRev` tells the logger to reload its alert settings within 60 s. Expect the JSON echoed back. `"Permission denied"` means a value broke a rule (for example more than 3 addresses, or a CR/LF in `fromName`).

Then test, either way:

- In the Apps Script editor, run **`sendTestEmail`**. It uses the same path as the PWA button and logs `OK: sent to 1 recipient`. The result is also written to `mailer/test`.
- Or queue a request as the owner (the rules accept it only within ±300 s of server time) and wait for the next run:
  ```bash
  curl -s -X PUT "$DB/devices/inv1/mailq/test.json?auth=$OWNER_TOKEN" -d "{\"ts\": $(date +%s)}"
  ```

> ⚠️ Do not create these nodes in the Firebase console's data viewer. Console writes bypass the rules, including their validation, so a typo there is not caught.

## 8. Step 6: make sure alerts are seen

For each recipient:

1. In Gmail on the web, create a filter `subject:"[Solar]"` → **Never send it to Spam** and **Always mark it as important**.
2. On the iPhone, use the **Gmail app** with notifications on. iOS Mail fetches Gmail on a schedule, so alerts can arrive late there [INFERRED, r8a].

The logger's ntfy push (d1 §7) stays the instant channel for severity ≥ 3. E-mail is the record and the backup, with 0–5 minutes of delay.

## 9. Optional (v1.1): the nudge

The logger can poke the mailer right after it queues a job, which cuts the delay to about 10–30 s [INFERRED, r8 §1.2]. The poke carries no data (the data is already in RTDB), so it cannot choose content or recipients. The firmware has it (`firmware/src/net/MailNudge.*`, r8 §4.10, firmware 0.6.1): it stays off until you enter the URL below in the setup portal, at most one nudge a minute and 48 a day.

1. Generate a key: `openssl rand -hex 16` (32 characters; at least 24 are required). Add it as the `NUDGE_KEY` script property.
2. **Deploy → New deployment → Select type: Web app**. Execute as **Me**, Who has access **Anyone**. **Deploy**, then copy the URL that ends in `/exec`.
3. In the logger's setup portal, enter `<exec URL>?k=<NUDGE_KEY>` in "Mailer nudge URL".
4. Check it from the Mac (`-o /dev/null` keeps the key out of the output; the logger itself never follows the redirect):
   ```bash
   curl -s -o /dev/null -w '%{http_code}\n' "<exec URL>?k=<NUDGE_KEY>"    # 302 = the mailer ran
   ```

A wrong key gets `{"ok":false,"err":"auth"}`; an empty or short `NUDGE_KEY` refuses every call. At most one nudge-run per 20 s. If the key ever leaks, change `NUDGE_KEY` and the portal field. After you change `Code.gs`, use **Deploy → Manage deployments → Edit → Version: New version** so the URL keeps working with the new code.

---

## 10. Everyday operation

**Functions you can run from the editor:**

| Function | Does |
|---|---|
| `sendSelfTest` | One mail to the first `ALLOW_TO` address. Checks MailApp only |
| `setup` | Checks properties, signs in, checks access, (re)installs the 5-min trigger, runs once |
| `sendTestEmail` | The dashboard's test e-mail, with the same gates and recipients; result in `mailer/test` |
| `showStatus` | Logs the triggers, the quota left, today's counters and the ledger size. Changes nothing |
| `uninstall` | Removes the trigger: the mailer stops. `setup` starts it again |

**Heartbeat** `devices/inv1/mailer`, shown on the PWA's mailer card:

| Field | Meaning |
|---|---|
| `hb` | Last run (epoch s). The PWA shows "Mailer not running" when it is older than 15 min |
| `ver` | Mailer version (`1.0.0`) |
| `sent` / `day` | Recipient-deliveries today / which local day |
| `quota` | `MailApp.getRemainingDailyQuota()` |
| `off` | Last `live.ts` while the logger is offline; `0` = online |
| `err` / `warn` | Last run's error / e.g. `Not on ALLOW_TO: x@y` |
| `last` / `test` | Last e-mail sent / result of the last test request |

**E-mail status on each event** (`events/<d>/<k>/em`, the PWA's chips):

| `em` | Set by | Meaning |
|---|---|---|
| `q` | logger | Queued for the mailer |
| `cool`, `rcap`, `cap`, `skip` | logger | Not queued: cooldown, the rule's daily limit, the logger's 40-job cap, low data coverage |
| `sent` | mailer | E-mailed; `emt` = when |
| `cap` | mailer | The daily recipient cap was reached |
| `quota` | mailer | Google's quota was below the floor |
| `off` | mailer | **Send alert e-mails** is off |
| `norc` | mailer | No recipient is in both `to` and `ALLOW_TO` |
| `exp` | mailer | The job was older than 12 h (24 h for severity ≥ 4) |
| `fail` | mailer | 5 send errors |

**E-mails you can receive:** alert (`[Solar] <message>`, `ALERT:` / `CRITICAL:` for severity 4 / 5), cleared (`Cleared: …`), still active (`Still active: …`), digest (`N alerts: …`), logger offline / back online, daily summary (`Tue 20 Oct: PV … | Load … | Grid … est.`), daily limit reached, and test. Each has an HTML and a plain-text part, the local time (Asia/Dhaka), a "Right now" snapshot from `live`, a "Delayed …" line when the event is older than 10 min, the value and limit for rule alerts, an **Open dashboard** link, and a footer saying why the recipient gets it and how to stop.

Grid figures (`egr`, `gw`) are estimates from the inverter's readings, not your utility meter (d1 §4.4). The e-mails say "est." everywhere.

## 11. Troubleshooting

| Symptom | Check |
|---|---|
| PWA: "Mailer not running" | script.google.com as the sender → **Triggers**: is `poll` there? **Executions**: red runs? Run `showStatus`, then `setup` |
| `mailer/err` starts with `RTDB … HTTP 401` | Rules changed or `MAILER_UID` lost; republish (step 3) |
| `mailer/err` starts with `Firebase sign-in failed` | `FB_PASSWORD` changed, the mailer user was deleted, or the API key is now referrer-restricted (use the device key) |
| `mailer/err` starts with `MailApp:` | Usually the daily quota. `mailer/quota` shows what is left; jobs retry up to 5 times |
| No e-mail, event shows `norc` | Add the address to `ALLOW_TO` **and** to Alerts → E-mail; `mailer/warn` names the missing one |
| No e-mail, event shows `off` | Turn on **Send alert e-mails** |
| No e-mail, event has no `em` | The logger did not queue it: the rule has e-mail off, or the built-in alert is below `builtin.minSev` |
| Event stays `q` | The mailer is not running (see the first row) |
| E-mail lands in Spam | The Gmail filter of step 6 |
| Too many "Logger offline" mails during load-shedding | Raise Alerts → E-mail → offline minutes (e.g. 120) or switch it off. There are at most 4 per day anyway |
| Summary missing | It needs **Send alert e-mails** on and the summary switch on; it arrives at the first run after `summary.hour` (07:00–07:05 by default) |
| "Executions" shows runs near 30 s | RTDB or Google is slow. If the daily runtime approaches 90 min, change `everyMinutes(5)` in `setup` to `10` and run `setup` again |

## 12. Updating and maintenance

- **New code:** paste the new `Code.gs` / `Lib.gs` over the old ones and save. The trigger keeps running and picks the new code at its next run. Run `setup` again only if `appsscript.json` changed (new scopes need consent), and make a new web-app version if you use the nudge (§9).
- **Rotate the mailer password:** Firebase console → Authentication → the mailer user → reset the password; update `FB_PASSWORD`; run `setup` (it discards the old refresh token).
- **Changing the sender account's password** may revoke the script's authorisation. Afterwards run `sendSelfTest` and `setup` again.
- **Yearly:** sign in to the sender account (§3).
- **Stop for good:** run `uninstall`, then delete the project. Optionally delete the `mailer` user in Firebase.

---

## 13. Development and tests

The tests run on the Mac with plain Node (v20.17 here) and need no `npm install`:

```bash
cd tools/apps-script
npm test            # = node --test test/*.test.js
npm run check       # Lib.gs, Code.gs and appsscript.json parse
```

Result on 2026-10-21: **44 tests, 44 pass** (16 in `lib.test.js`, 28 in `mailer.test.js`).

| Suite | Covers |
|---|---|
| `lib.test.js` | `Lib.gs` loaded with **no** globals (it fails if `Lib.gs` uses an Apps Script service). Time in UTC+6 across midnight, month, year and leap-year ends; billing-cycle starts; the r8 §4.5 number formats; clip/escape; constant-time compare and the nudge key; `alerts/email` defaults and clamps; recipients (mask, allow-list, case, de-dup); the daily cap with its reserve and the quota floor; job and event checks against the r8 §3.7 `mailq` fixture; ledger pruning and trimming; the day reset; test gate; offline detector; summary timing; **golden e-mail texts** (`fixtures/expected-alert.txt`, `fixtures/expected-summary.txt`); all templates; and a drift guard that the severity, mode and event-type names equal [`shared/codes.json`](../../shared/codes.json) |
| `mailer.test.js` | `Code.gs` + `Lib.gs` against fake MailApp, UrlFetchApp, Properties, Cache, Lock, ScriptApp, ContentService and an in-memory RTDB that enforces the mailer's read and write grants. Cases 01–18 are the decide-phase harness of r8 §8.1 (setup, single mail, duplicate re-write, digest and `rc` split, prefixes and "Delayed", the 45-job cap test, allow-list, `off`/`exp`/missing event, retries then `fail`, test e-mail and its rate limit, offline/online, summary, token refresh, `doGet`, escaping and ledger size, quota floor, a failing offline mail). Cases 19–28 add: the idle run makes exactly 3 requests and no property writes; the mailer never reads paths outside its grant; malformed jobs; a MailApp error on a test e-mail; `sendTestEmail`; the summary default; duplicate addresses; a small cap; `setup` errors; a busy lock and a 401 run |
| `e2e.js` | The real files against the **real rules** in the Firebase Database emulator. It renders the placeholders with test UIDs, loads the rules over the emulator's REST API, then runs owner and device writes, `setup()`, a re-queued job and a job without an event, and checks that the rules refuse the mailer reading `cfg`, editing an event or changing recipients |

`e2e.js` needs Java and the emulator, so it is not part of `npm test` (started without the emulator, it prints `SKIP` and exits 0). Run it from the repository root, with `HOME` pointed at a scratch folder so the emulator JAR is not cached in your home directory (no login is needed for a `demo-` project):

```bash
HOME=/tmp/fb-home npx --yes firebase-tools@15.32.1 emulators:exec --only database --project demo-savx \
  --config tools/apps-script/test/firebase.emu.json "node tools/apps-script/test/e2e.js"
```

It reads `firebase/database.rules.json` and stops with a message if that file has no `MAILER_UID` yet. `RULES=<file>` tests another rules file. On 2026-10-21 it passed against the r8 merged rules (`E2E OK: rules + Code.gs agree (3 mails, …)`). The emulator first warns that no rules file is configured and that it allows everything; `e2e.js` then loads the real rules itself, and its last three checks (refusals) prove they are active. The emulator writes `database-debug.log` into the current folder; `*.log` is git-ignored.

**How the Node loader works.** Apps Script puts all `.gs` files into one global scope. [`test/_load.js`](test/_load.js) does the same: it concatenates the files, wraps them in one function, passes in only the mocks a test asks for, and returns every top-level `function` and `var`. Keep that working:

- top-level code in a `.gs` file only **declares** things; no file may call another file's functions at load time;
- private functions end in `_`, which also hides them from the editor's Run menu;
- `Lib.gs` never uses `MailApp`, `UrlFetchApp`, `PropertiesService`, `CacheService`, `LockService`, `ScriptApp`, `Utilities`, `ContentService` or `Logger` (a test enforces this).

**Keeping the editor in sync.** The repository copy is the master. After editing here, run `npm test` and paste the changed files into the editor. Google's `clasp` CLI could push them instead, but it needs `clasp login` with the sender account; that is your choice and not required.

---

## 14. Differences from the r8 §5.3 listing

The r8 listing was the tested design; these are the deliberate changes. Behaviour with the default settings is the same except where noted.

| # | Change | Why |
|---|---|---|
| 1 | Split into `Lib.gs` (pure) and `Code.gs` (I/O); local time computed as fixed UTC+6 instead of `Utilities.formatDate` | Testable in Node. Asia/Dhaka has no DST, so the output is identical |
| 2 | `FB_API_KEY` must be the **device key** | r7 §6.2 (written after r8) restricts the browser key by referrer; the mailer sends none |
| 3 | `summary.on` defaults to **on** when `alerts/email` has no `summary` object (the listing defaulted to off) | Matches the r8 §3.2 data contract table. The PWA always writes the object, so normally nothing changes |
| 4 | The severity-4 reserve is `min(5, maxPerDay / 4)` | With the listing, a daily limit of 5 or less blocked every Warning e-mail. Identical for limits ≥ 20 |
| 5 | Single rule alerts gain a line `Value: 5.01 kWh, limit 5.00 kWh [Grid energy, today, est.]` from the rule's metric (one cached GET of `alerts/rules`); built-in types and summary alert counts use their labels from `shared/codes.json` ("Grid lost", rule names) | The task asks for values and thresholds in the e-mail. Formats follow r8 §4.5 |
| 6 | Mode names are the full `shared/codes.json` names ("Line (grid) mode"); battery V min/max are shown with 1 decimal; outage minutes as "2 h 14 min" | One source for names; r8 §4.5 number formats |
| 7 | Repeat events (`…-r` keys) are not counted as new alerts in the summary | A long-running alert would otherwise be counted many times |
| 8 | Duplicate addresses in `to` are mailed once; `ALLOW_TO` may also be separated by spaces or `;` | Avoids double counting against the 100/day quota |
| 9 | Malformed `mailq` keys or dates are deleted without being read; a `rid` in a test request is checked | Defence in depth if the rules are ever wrong |
| 10 | A MailApp error on a test e-mail answers `ok:false` and deletes the request (the listing re-threw, so the request was retried every run) | No failure loop |
| 11 | New editor functions `sendTestEmail` and `showStatus`; `setup` validates `FB_DB_URL` and `ALLOW_TO` and runs under the lock | Testing before the PWA page exists; clearer errors |
| 12 | `STATE` / `SENT` are written only when they changed | An idle run makes no property writes |
| 13 | No HMAC push endpoint (`doPost`) | r8 decision E2 dropped the HMAC payload push in favour of the body-less nudge (`doGet`, constant-time key compare). There is therefore no HMAC format to cross-check with the firmware; the firmware's `MailNudge` (0.6.1) sends only the body-less GET with `?k=` |

## 15. Sources

- Decision record: [research/r8-email-alerts.md](../../research/r8-email-alerts.md) (§1 transport, §3 data contract and rules, §5 mailer, §7 setup, §8 tests); corrections in [r8c](../../research/r8c-email-verification.md); API keys and dashboard URL in [r7](../../research/r7-web-hosting.md) §2 and §6.2.
- Apps Script: [Quotas](https://developers.google.com/apps-script/guides/services/quotas), MailApp, UrlFetchApp, ScriptApp, ClockTriggerBuilder, Lock, PropertiesService, [Manifest](https://developers.google.com/apps-script/manifest), [Scopes](https://developers.google.com/apps-script/concepts/scopes), [Web apps](https://developers.google.com/apps-script/guides/web), [Content service](https://developers.google.com/apps-script/guides/content).
- Firebase: [RTDB REST authentication](https://firebase.google.com/docs/database/rest/auth), REST reference, Security rules reference, [Emulator rules configuration](https://firebase.google.com/docs/emulator-suite/install_and_configure#security_rules_configuration).
- Gmail: [Sending limits](https://support.google.com/mail/answer/22839).
