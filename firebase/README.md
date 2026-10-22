# Firebase Realtime Database rules

`database.rules.json` is the security-rules file for the logger's Realtime Database (RTDB). It implements the decision record [d1 §4.7](../research/d1-architecture.md), validates the documents of [d1 §4.1–4.2](../research/d1-architecture.md), and includes the e-mail-alert additions of [r8 §3.11](../research/r8-email-alerts.md) (alert rules, e-mail settings, engine state, the mail outbox and the mailer heartbeat). The data model is explained in [docs/04-architecture-and-data-model.md](../docs/04-architecture-and-data-model.md).

> ⚠️ **The file contains three placeholders, `OWNER_UID`, `DEVICE_UID` and `MAILER_UID`, and they stay in git.** As committed, nobody can read or write anything. [`tools/render-rules.sh`](../tools/render-rules.sh) writes a filled-in copy into the git-ignored `.deploy/` folder, and only that copy is deployed (by the `deploy-rules` GitHub workflow or from the Mac, §3–4). Never "fix" an access problem by changing a rule to plain `auth != null`: anyone can create an account with the public web API key (Identity Toolkit allows 100 sign-ups per hour per IP), so `auth != null` alone means "anyone". CI fails on such a rule.

## 1. Who may do what

There are three Auth users. All use email/password sign-in.

| User | Used by | Pinned in the rules as |
|---|---|---|
| owner (e.g. `owner@yourdomain`) | You, in the iPhone PWA | `OWNER_UID` |
| device (e.g. `inv1@yourdomain`) | Only the ESP32 (REST sign-in) | `DEVICE_UID` |
| mailer (e.g. `mailer@anything.invalid`) | Only the Apps Script e-mail mailer ([r8 §1.4](../research/r8-email-alerts.md)) | `MAILER_UID` |

Every path sits under `/devices/inv1/`. The owner and the device may **read** everything there. The mailer may read only `live`, `daily`, `events`, `alerts`, `mailq` and `mailer`, never `cfg`, `status`, `meta`, `diag` or `hist`. Nobody else may read anything (`".read": false` at the root).

| Path | Written by | When (d1 §4.3) | What the rules check |
|---|---|---|---|
| `live` | device | `PUT` every 10 s | Must have `ts` and `lk`. `ts` must be a number above 1,700,000,000 and no more than 600 s in the future. `md` ≤ 2 chars, `st` ≤ 12, `st2` ≤ 4, `wf` ≤ 48. Every other key must be a number and its name ≤ 6 chars. |
| `status` | device | in the 60 s `PATCH` | Key names ≤ 8 chars. Values are numbers, or strings ≤ 40 chars. |
| `meta` | device | boot and on change | Flat. Key names ≤ 12 chars. Values are numbers, booleans, or strings ≤ 200 chars. |
| `diag/raw/{cmd}` | device | every 10 min | `cmd` ≤ 12 chars (fits `QED20261021`). Value is a string ≤ 300 chars. Nothing else may exist under `diag`. |
| `hist/{YYYY-MM-DD}/{hhmm}` | device (owner may delete) | 60 s | Date key `20YY-MM-DD`, minute key `0000`–`2359`. Must have `n`. `md` ≤ 2 chars. Every other field is a number with a name ≤ 4 chars. |
| `h15/{YYYY-MM-DD}/{hhmm}` | device (owner may delete) | 15 min | As `hist`, but the minute must be `00`, `15`, `30` or `45`. |
| `daily/{YYYY-MM-DD}` | device | in the 60 s `PATCH` | Every field is a number with a name ≤ 6 chars. |
| `events/{YYYY-MM-DD}/{pushId}` | device; the owner adds `ack` | on alert open/clear | Must have `ts`, `type`, `sev`, `msg`. `sev` is 0–5. `type` ≤ 24, `st` ≤ 8, `msg` ≤ 200, `ch` ≤ 16 chars. `dur` is a number, `ack` a boolean. Unknown fields are rejected. |
| `events/…` (alert additions) | device; the **mailer** writes only `em` and `emt` of an existing event | as above | `rid` = `r01`–`r16`; `val`, `thr`, `emt` numbers; `em` one of `q sent cool rcap cap quota off norc exp fail skip`. New device keys are `<ts>-<rid or TYPE>-<o|c|r|i>`; older push ids stay valid. |
| `cfg` | **owner only** | when you save Settings | Each known key has a range (table below). Unknown keys are rejected. The device only reads `cfg` (`GET` every 60 s). `alertsRev` (ms, ≤ server time) is bumped by the PWA with every alerts change. |
| `alerts/email` | **owner only** (device and mailer read) | Alerts → E-mail | Must have `on`. `to`: 0–3 valid addresses as one comma string, ≤ 300 chars. `fromName` 1–40 chars without CR/LF/TAB. `maxPerDay` 1–40, `monthStart` 1–28, `builtin.minSev` 0/3/4/5, `summary` {on, hour 0–23}, `offline` {on, min 10–240, clear}. Unknown fields rejected. |
| `alerts/rules/{r01…r16}` | **owner only** (device and mailer read) | Alerts → Rules | At most 16 (the key pattern). Required fields, metric/period/comparator/mode consistency, value range by metric, recovery level on the safe side, at least one channel, no CR/LF/TAB in the name, `upd` ≤ server time ([r8 §3.3](../research/r8-email-alerts.md)). The PWA checks exactly the same before saving. |
| `alertState/{rid}` | device | on rule transitions | `st` from a fixed list; numeric fields; `err` ≤ 40 chars. |
| `mailq/{eventKey}` | device creates, mailer deletes | with an e-mailed event | Key `<10-digit ts>-<src>-<o|c|r|i>`; `d` date key, `ts`, `sev` 0–5, optional `rc` 1–7 and retry counter `n` 0–20. |
| `mailq/test` | **owner only** | "Send test e-mail" | `ts` within ±300 s of server time, optional `rid`, nothing else. |
| `mailer` | mailer only | every 5 min | Heartbeat, counters, last result, test result; fixed keys and lengths. |

`cfg` ranges (identical to the client-side validation in the PWA's Settings page, `web/src/data/types.ts`):

| Key | Range | Default | Key | Range | Default |
|---|---|---|---|---|---|
| `liveSec` | 5–300 | 10 | `keepHistDays` | 30–3650 | 400 |
| `socLow` | 5–80 | 30 | `keepEventDays` | 30–3650 | 730 |
| `bvLow` | 20–30 | 22.5 | `quietFrom` | 0–23 | 22 |
| `bvHigh` | 26–34 | 30.0 | `quietTo` | 0–23 | 7 |
| `tempHigh` | 40–100 | 75 | `summaryHour` | −1–23 (−1 = off) | 21 |
| `loadHigh` | 50–120 | 90 | `testAlert` | any number (epoch s) | 0 |
| `notifyRepeatMin` | 5–1440 | 30 | `reprobe` | any number (epoch s) | 0 |

How the rule engine behaves (R6 §4, [CONFIRMED-primary](https://firebase.google.com/docs/database/security/core-syntax)):

- `.read`/`.write` cascade down: a grant at `devices/inv1` covers every child. `.validate` does **not** cascade, so each level validates its own keys.
- `$other` / `$num` / `$f` match only keys that are not listed by name at the same level.
- In a multi-path `PATCH` (the device's minute upload: `{"hist/<d>/<hhmm>":…, "daily/<d>":…, "status":…}`), every key is checked against the rules of its own path, and the whole `PATCH` fails if one key fails.
- `.validate` does not run on deletes. That is why the device's daily pruning (`DELETE hist/<old-date>`) needs only `.write`.
- Rule-denied requests still count towards the 10 GB/month download quota (V2 F9). A firmware bug that keeps hitting a denied path costs quota, so watch the Usage tab.

## 2. Create the users

1. Firebase console → **Authentication** → **Sign-in method** → enable **Email/Password** (not the passwordless "Email link").
2. **Users** → **Add user**: the owner (your email, a strong password).
3. **Add user**: the device, e.g. `inv1@<anything-you-own>`, with a long random password. Generate one on the Mac with `openssl rand -base64 24`. It is entered once in the ESP32 setup page and never typed again.
4. **Add user**: the mailer, e.g. `mailer@<anything>.invalid`, with another 24+ character random password. It goes only into the Apps Script's script properties ([r8 §7](../research/r8-email-alerts.md)). Skip this until you set up e-mail alerts and use `MAILER_UID=none` meanwhile.
5. Copy the **User UID** column of each user.

## 3. Render the rules with your UIDs (never edit the committed file)

The committed `database.rules.json` keeps its placeholders. [`tools/render-rules.sh`](../tools/render-rules.sh) checks the UIDs (letters and digits only, all different), substitutes them into `.deploy/database.rules.json`, validates the JSON and writes a minimal `.deploy/firebase.json`. `.deploy/` is git-ignored.

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
OWNER_UID=PASTE_OWNER_UID DEVICE_UID=PASTE_DEVICE_UID MAILER_UID=PASTE_MAILER_UID sh tools/render-rules.sh
# no mailer yet:  ... MAILER_UID=none sh tools/render-rules.sh   (the mailer grants then match no account)
```

The root `firebase.json` intentionally has **no `database` block** (only the fallback hosting config), so a stray `firebase deploy` from the repository root can never upload the placeholder rules, which would lock out the logger and you ([r7 §6.4](../research/r7-web-hosting.md)).

## 4. Deploy the rules

> ⚠️ **Order for the e-mail alerts: rules → mailer → app → firmware** ([r8 §7](../research/r8-email-alerts.md)). A PWA or firmware that writes the new fields (`cfg/alertsRev`, `alerts/*`, `rid`, `em`) before these rules are live is refused with 401, and refused requests still count against the download quota.

**From GitHub (normal path).** Set the repository variables `FIREBASE_PROJECT_ID`, `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`, `FIREBASE_MAILER_UID` (`none` until the mailer exists) plus the Google credential ([r7 §4.6](../research/r7-web-hosting.md)). The `deploy-rules` workflow then dry-runs every pull request that touches the rules against the real project and deploys on merge to `main`.

**From the Mac** (you run these; `npx` needs no global install):

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/.deploy          # after section 3
npx --yes firebase-tools@15.32.1 login                    # once; opens the browser
npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id> --dry-run
npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id>
```

You can also paste `.deploy/database.rules.json` into **Realtime Database → Rules** in the console and press **Publish** (the console validates it first; if it ever refuses the long rule-level expression of `alerts/rules/$rid`, see [r8 §3.11](../research/r8-email-alerts.md)). The console's **Rules Playground** can simulate reads and writes as a given UID.

Then run the tests in §5 with the **device key**. Before changing the rules, run the emulator suite in §6.

## 5. Negative tests with curl (do these once after every rules change)

These requests prove that the rules refuse what they must refuse. Run them in the Mac terminal. They use only `curl` and `python3`, which macOS already has.

### 5.1 Set up

```bash
API_KEY=AIza...                     # the DEVICE key (no referrer restriction, research/r7-web-hosting.md §6.2);
                                    # the browser key refuses requests without the app's Referer
DB=https://YOUR-PROJECT-default-rtdb.asia-southeast1.firebasedatabase.app
OWNER_EMAIL=you@example.com
DEVICE_EMAIL=inv1@example.com
MAILER_EMAIL=mailer@example.invalid  # skip the MAILER lines until the mailer user exists

# Prompts for the passwords so they do not end up in your shell history.
signin() {  # usage: signin EMAIL  -> prints an ID token (valid for 1 hour)
  read -rs "PW?Password for $1: "; echo >&2
  curl -s "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=$API_KEY" \
    -H 'Content-Type: application/json' \
    -d "{\"email\":\"$1\",\"password\":\"$PW\",\"returnSecureToken\":true}" |
    python3 -c 'import sys,json; print(json.load(sys.stdin)["idToken"])'
}
OWNER_TOKEN=$(signin "$OWNER_EMAIL")
DEVICE_TOKEN=$(signin "$DEVICE_EMAIL")
MAILER_TOKEN=$(signin "$MAILER_EMAIL")
NOW=$(date +%s)
D=$(TZ=Asia/Dhaka date +%F)
RULE='{"name":"Daily grid usage > 5 kWh","on":true,"metric":"egr","scope":"day","op":"gt","value":5000,"mode":"hold","maxDay":1,"sev":3,"email":true,"ntfy":false,"upd":{".sv":"timestamp"}}'
code() { curl -s -o /dev/null -w '%{http_code}\n' "$@"; }   # prints only the HTTP status
```

(`read -rs "PW?prompt"` is zsh syntax, the macOS default shell. In bash use `read -rsp "Password: " PW`.)

> ⚠️ Never paste an ID token or a URL containing `auth=` into a chat, an issue or a screenshot. The token gives full access to that user's data for up to an hour.

### 5.2 Expected results

A refused request returns HTTP **401** with the body `{"error" : "Permission denied"}`. This is the same for a missing `.write` grant and for a failed `.validate`. A request that is allowed returns 200, or 204 with `print=silent`.

| # | Test | Command | Expect |
|---|---|---|---|
| 1 | Unauthenticated read | `code "$DB/devices/inv1/live.json"` | 401 |
| 2 | Unauthenticated root read | `code "$DB/.json?shallow=true"` | 401 |
| 3 | Owner reads `live` | `code "$DB/devices/inv1/live.json?auth=$OWNER_TOKEN"` | 200 |
| 4 | Owner reads the root | `code "$DB/.json?shallow=true&auth=$OWNER_TOKEN"` | 401 |
| 5 | **Owner writes `live`** | `code -X PUT -d "{\"ts\":$NOW,\"lk\":1}" "$DB/devices/inv1/live.json?auth=$OWNER_TOKEN"` | 401 |
| 6 | **Device writes `cfg`** | `code -X PATCH -d '{"liveSec":10}' "$DB/devices/inv1/cfg.json?auth=$DEVICE_TOKEN"` | 401 |
| 7 | Device writes `live` (valid) | `code -X PUT -d "{\"ts\":$NOW,\"lk\":0}" "$DB/devices/inv1/live.json?auth=$DEVICE_TOKEN&print=silent"` | 204 |
| 8 | `live` without `lk` | `code -X PUT -d "{\"ts\":$NOW}" "$DB/devices/inv1/live.json?auth=$DEVICE_TOKEN"` | 401 |
| 9 | `live.ts` 1 h in the future | `code -X PUT -d "{\"ts\":$((NOW+3600)),\"lk\":1}" "$DB/devices/inv1/live.json?auth=$DEVICE_TOKEN"` | 401 |
| 10 | `live` number sent as a string | `code -X PUT -d "{\"ts\":$NOW,\"lk\":1,\"bv\":\"26.4\"}" "$DB/devices/inv1/live.json?auth=$DEVICE_TOKEN"` | 401 |
| 11 | Bad minute key `2460` | `code -X PUT -d '{"n":12,"w":100}' "$DB/devices/inv1/hist/2026-10-21/2460.json?auth=$DEVICE_TOKEN"` | 401 |
| 12 | Bad date key | `code -X PUT -d '{"n":12}' "$DB/devices/inv1/hist/21-10-2026/1435.json?auth=$DEVICE_TOKEN"` | 401 |
| 13 | Minute record without `n` | `code -X PUT -d '{"w":100}' "$DB/devices/inv1/hist/2026-10-21/1435.json?auth=$DEVICE_TOKEN"` | 401 |
| 14 | `h15` at a non-quarter minute | `code -X PUT -d '{"n":180}' "$DB/devices/inv1/h15/2026-10-21/1410.json?auth=$DEVICE_TOKEN"` | 401 |
| 15 | Event with an unknown field | `code -X POST -d "{\"ts\":$NOW,\"type\":\"TEST\",\"sev\":1,\"msg\":\"x\",\"evil\":1}" "$DB/devices/inv1/events/2026-10-21.json?auth=$DEVICE_TOKEN"` | 401 |
| 16 | Event with `sev` 9 | `code -X POST -d "{\"ts\":$NOW,\"type\":\"TEST\",\"sev\":9,\"msg\":\"x\"}" "$DB/devices/inv1/events/2026-10-21.json?auth=$DEVICE_TOKEN"` | 401 |
| 17 | Owner sets `cfg.liveSec` below 5 | `code -X PUT -d '2' "$DB/devices/inv1/cfg/liveSec.json?auth=$OWNER_TOKEN"` | 401 |
| 18 | Owner adds an unknown `cfg` key | `code -X PUT -d '1' "$DB/devices/inv1/cfg/foo.json?auth=$OWNER_TOKEN"` | 401 |
| 19 | Owner sets a valid `cfg` value | `code -X PUT -d '10' "$DB/devices/inv1/cfg/liveSec.json?auth=$OWNER_TOKEN"` | 200 |
| 20 | Device writes a foreign device | `code -X PUT -d "{\"ts\":$NOW,\"lk\":1}" "$DB/devices/inv2/live.json?auth=$DEVICE_TOKEN"` | 401 |
| 21 | Device writes `diag` outside `raw` | `code -X PUT -d '"x"' "$DB/devices/inv1/diag/other.json?auth=$DEVICE_TOKEN"` | 401 |
| 22 | Owner writes `daily` | `code -X PATCH -d '{"epv":1}' "$DB/devices/inv1/daily/2026-10-21.json?auth=$OWNER_TOKEN"` | 401 |

E-mail alerts ([r8 §8.2](../research/r8-email-alerts.md); r8 test ids in brackets). Every write below is refused, so nothing is left behind; tests 34 and 35 only read.

| # | Test | Command | Expect |
|---|---|---|---|
| 23 | Device writes `alerts/email` [N1] | `code -X PUT -d 'true' "$DB/devices/inv1/alerts/email/on.json?auth=$DEVICE_TOKEN"` | 401 |
| 24 | Rule slot `r17` (max 16 rules) [N2] | `code -X PUT -d "$RULE" "$DB/devices/inv1/alerts/rules/r17.json?auth=$OWNER_TOKEN"` | 401 |
| 25 | Rule name with a line break (header injection) [N7] | `code -X PUT -d "${RULE/Daily grid/Daily\\nBcc: x@y.zz}" "$DB/devices/inv1/alerts/rules/r16.json?auth=$OWNER_TOKEN"` | 401 |
| 26 | Four recipients [N8] | `code -X PUT -d '"a@b.cc,c@d.ee,e@f.gg,h@i.jj"' "$DB/devices/inv1/alerts/email/to.json?auth=$OWNER_TOKEN"` | 401 |
| 27 | Mailer reads `cfg` [N10a] | `code "$DB/devices/inv1/cfg.json?auth=$MAILER_TOKEN"` | 401 |
| 28 | Mailer edits an event message [N11] | `code -X PUT -d '"hacked"' "$DB/devices/inv1/events/$D/$NOW-r03-o/msg.json?auth=$MAILER_TOKEN"` | 401 |
| 29 | Test e-mail request 1 h old [N14] | `code -X PUT -d "{\"ts\":$((NOW-3600))}" "$DB/devices/inv1/mailq/test.json?auth=$OWNER_TOKEN"` | 401 |
| 30 | Owner writes a device mail job [N15] | `code -X PUT -d "{\"d\":\"$D\",\"ts\":$NOW,\"sev\":3}" "$DB/devices/inv1/mailq/$NOW-r01-o.json?auth=$OWNER_TOKEN"` | 401 |
| 31 | Unauthenticated read of `mailer` [N22, without a 4th account] | `code "$DB/devices/inv1/mailer.json"` | 401 |
| 32 | Device writes the mailer heartbeat [N25] | `code -X PUT -d "$NOW" "$DB/devices/inv1/mailer/hb.json?auth=$DEVICE_TOKEN"` | 401 |
| 33 | `cfg/alertsRev` 1 h in the future [N20] | `code -X PUT -d "$(( (NOW+3600)*1000 ))" "$DB/devices/inv1/cfg/alertsRev.json?auth=$OWNER_TOKEN"` | 401 |
| 34 | Mailer reads its outbox [P5a] | `code "$DB/devices/inv1/mailq.json?auth=$MAILER_TOKEN"` | 200 |
| 35 | Device reads the rules [P30] | `code "$DB/devices/inv1/alerts.json?auth=$DEVICE_TOKEN"` | 200 |

(`${RULE/a/b}` is parameter substitution in both zsh and bash; test 25 puts a JSON `\n` into the name. Tests 27, 28 and 34 need the mailer user: skip them while the rules use `MAILER_UID=none`.)

Clean-up: test 7 overwrote `live` with a stub, and test 19 set `liveSec` to its default. The logger overwrites `live` within 10 s, so nothing else needs undoing. Tests 11–16, 20–22 and 23–33 are refused, so they leave nothing behind.

If test 3 fails, the owner UID in the rules is wrong. If test 7 fails, the device UID is wrong, or the token expired (tokens last 1 hour; run `signin` again).

## 6. Full test suite in the emulator (before every rules change)

`sh tools/rules-test/run.sh` starts the Realtime Database **emulator** (no project, no login, no deploy; needs Java 11+ and downloads the emulator JAR once) with the committed rules rendered for test UIDs, then runs:

- [`rtest.mjs`](../tools/rules-test/rtest.mjs): 87 allow/deny cases as owner, device, mailer and a stranger (r8's 69 plus the PWA's Alerts writes). Expect `87 passed, 0 failed`.
- [`parity.mjs`](../tools/rules-test/parity.mjs): about 5,400 random and near-boundary rule and e-mail documents, each written to the emulator and checked with the PWA's `validateRule()` / `validateEmailCfg()`. Expect `parity: … agree, 0 disagree`. A disagreement means the app and the rules drifted apart: fix one of them before deploying.

Result on 2026-10-21: `87 passed, 0 failed` and `parity: 5414 agree, 0 disagree (2366 accepted by the server)` (firebase-tools 15.32.1, database emulator 4.11.2, Java 21).

## 7. Day-to-day cautions

- ⚠️ **Do not leave the Firebase console's data viewer open on `hist`.** Console reads count as billed download (V2 F9). Browse a single date node, or use the PWA.
- Check **Realtime Database → Usage** daily during the first week. Expect about 5–15 MB/day of download and about 0.4 MB/day of storage growth (d1 §4.6).
- To add a family viewer later: create another Auth user and add `|| auth.uid === 'VIEWER_UID'` to the `.read` rule of `devices/inv1` only, add `VIEWER_UID` to `tools/render-rules.sh`, and run §6. Do not give them `.write` on `cfg` or `alerts`, and do not give them the device account. (Family members who only want e-mails need no account: add their address in Alerts → E-mail and to the mailer's `ALLOW_TO`.)
