# Adversarial verification: e-mail alerts for usage thresholds (TRANSPORT and RULES reports)

I re-checked the claims on 2026-10-21 against official pages, GitHub source and repo metadata (read with `gh api` and `curl`), plus my own `openssl` and `dig` runs. Tags: **[CONFIRMED-primary]** (an official page or source read today), **[SECONDARY]**, **[INFERRED]**, **[UNVERIFIED]**. Raw page captures are in `<scratch>`. No project files were modified.

---

## 1. Corrections

### C1. The Apps Script push path breaks on Arduino-ESP32 3.3.12 when it follows the 302 — this changes the design
Both reports say `setFollowRedirects(...)` on a POST "switches to GET and drops the payload, which matches what Apps Script expects". TRANSPORT §2.2 and §3.1 say so, and RULES §8 tags it [CONFIRMED-primary]. Only half of that is true.

**What the source does** [CONFIRMED-primary, `libraries/HTTPClient/src/HTTPClient.cpp` at tag `3.3.12` and on `master`]:
- `sendRequest()` writes the header into the shared `_headers` string, on line 639: `addHeader(F("Content-Length"), String(size));`.
- The 302/303 branch, on lines 710–724, sets `type="GET"; payload=nullptr; size=0;`.
- Nothing removes the old `Content-Length` header. `setURL()` → `beginInternal()` does not call `clear()` for an absolute URL.
- The second hop is therefore a GET that still carries `Content-Length: N` and has no body.

**Upstream report** [CONFIRMED-primary, espressif/arduino-esp32 issue #8300]:
- This is the same Apps Script scenario. The debug log shows `GET /macros/echo?user_content_key=… Content-Length: 32` answered by `HTTP/1.0 400 Bad Request`.
- A bot closed the issue on 2025-01-16 without a fix. A comment on 2025-02-03 says the bug is still there.
- The code is unchanged in 3.3.12 (released 2026-09-18) and on `master`.

**Effect:** `doPost` has already run and sent the e-mail, but the ESP sees 400. Its retry logic then:
- treats the send as failed;
- retries;
- after 3 failures switches to Gmail SMTP, which **sends the alert a second time**.

**Fix:** follow the redirect by hand.
```cpp
http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);   // #8300: stale Content-Length on 302->GET
int c = http.POST(json);                                   // c == 302 => doPost ran
String loc = (c == 302) ? http.getLocation() : String();   // getLocation() exists in 3.3.12 (HTTPClient.h:253)
http.end();
if (loc.startsWith("https://script.googleusercontent.com/")) { http.begin(tls, loc); c = http.GET(); body = http.getString(); http.end(); }
```
Treat 302 as "accepted, probably sent". This must be bench-tested [INFERRED].

The TRANSPORT claim that each hop opens a new connection holds: 3.3.12 calls `disconnect` when the host changes, at L339–343 [CONFIRMED-primary].

### C2. The two reports disagree in ways that would break the build
- **`em` field type.** TRANSPORT's poller selects events with `em:1`, a number. RULES validates `em` as a string enum (`queued|sent|…`). Under the RULES schema, `em:1` would be rejected, and events have `$other:false` in today's `database.rules.json`. Pick one.
- **The poller cursor misses events.** TRANSPORT reads `events/{today}?orderBy="$key"&startAt=<cursor>`. That only works with chronological push IDs. RULES uses deterministic keys `r03-<ts>-o` and `et-<ts>`, which sort by rule id, not by time. A new `r01-…` event sorts before an `r03-…` cursor and is skipped.
  - Use `orderBy="ts"` with `".indexOn":["ts"]` under `events/$date`, or a dedicated outbox node.
  - Without the index, REST returns 400, "index … not defined" [CONFIRMED-primary, REST error list].
- **Daily cap.**
  - TRANSPORT proposes about 60/day. RULES proposes 40/day, with the argument that a rolling 24 h window can hold two local days (2 × 40 = 80 < 100).
  - With 60/day, 2 × 60 = 120, which is over 100.
  - Use 40 or less, and count the poller's own mails (offline mail, summary, test) in the same cap.
- **Daily summary.** RULES has the device send it at 07:00 for yesterday. TRANSPORT has the poller send it at 21:00 for today. Running both doubles the quota use.
- **Recipients.**
  - TRANSPORT: never take the recipient from the request.
  - RULES: `MailJob.to[3]` comes from the device, checked against an allow-list.
  - Choose one. Having the relay resolve recipients is safer.
- **HMAC envelope.**
  - TRANSPORT signs `id|ts|subj|body` rebuilt from parsed JSON. RULES signs the exact payload string `p`. Use the RULES form.
  - In both, add a constant-time compare and `LockService`. Apps Script allows 30 simultaneous executions per user, so two retries can race between the cache check and the cache write.

### C3. Wrong or over-strong tags
- **`doPost` cannot see headers.** TRANSPORT tags this [CONFIRMED-primary]. The guides/web table lists `queryString`, `parameter(s)`, `pathInfo`, `contextPath`, `contentLength` and `postData.{length,type,contents,name}`. It does not say headers are unavailable; they are simply not listed. The right tag is **[CONFIRMED-primary, by absence]**. RULES' [INFERRED] is the more accurate tag.
- **Brevo rewrites a gmail.com sender.** TRANSPORT cites help.brevo.com 14925263522578 and 16045394674066 as [CONFIRMED-primary]. Both returned **HTTP 403** to me. The only support I found is a Brevo community moderator ("temporarily replacing your sender by t-sender-sib.com") and a third-party guide that names `@brevosend.com` instead. Downgrade to **[SECONDARY]**.
- **"GTS Root R4 already pinned in `roots.h`".** `firmware/include` currently holds only `codes.h` and `config.h`, so `roots.h` is still planned. d1 §5.13 plans **R1+R2+R3+R4**. The ESP snippet should use that bundle, not R4 alone (see M8).
- **Apps Script e-mail quota.** The page shows "100 **\*** / day", but the footnote text does not render in the fetched page. There is also a separate "within domain 100\*" row. Treat 100 as a ceiling, not a guarantee.
- **RULES §7: "Resend may require a verified domain (not verified)".** This is now resolved. Without your own domain, `resend.dev` "can only send emails to the email address associated with your Resend account" and is "only available for testing purposes" [CONFIRMED-primary].

---

## 2. Confirmed

| # | Claim | Result | Source |
|---|---|---|---|
| 1 | App passwords | Need 2-Step Verification. Unavailable with security-key-only 2SV, on work/school accounts, and under Advanced Protection. Revoked on password change. "aren't recommended and are unnecessary in most cases". 16 digits. **No deprecation date** [CONFIRMED-primary] | support.google.com/accounts/answer/185833 |
| 2 | Gmail consumer limits | More than 500 e-mails/day, or more than 500 recipients in one e-mail, triggers the limit. Sending resumes "within 1 to 24 hours". The page says **nothing about SMTP or app-password limits** [CONFIRMED-primary] | support.google.com/mail/answer/22839 |
| 3 | Third-party apps | "Gmail no longer supports third-party apps or devices which require you to share your Google username and password". It suggests an app password if you use 2SV [CONFIRMED-primary] | support.google.com/mail/answer/7126229 |
| 4 | SMTP server | `smtp.gmail.com`, 465 SSL / 587 TLS. Workspace: 2,000/day; less secure apps unsupported from 2025-05-01 [CONFIRMED-primary, Workspace page]. The same host for consumer accounts is [SECONDARY] | knowledge.workspace.google.com |
| 5 | Apps Script consumer quotas | 100\* recipients/day (Workspace 1,500\*), 50 per message, 200 KB body, 6 min per execution, **90 min/day trigger runtime**, 20 triggers per user per script, 20,000 URL Fetch/day, 30 simultaneous executions per user. "reset 24 hours after the first request". Can change "without notice". Page last updated 2026-09-03. Properties: 9 KB per value, 500 KB per store [CONFIRMED-primary] | developers.google.com/apps-script/guides/services/quotas |
| 6 | MailApp | `getRemainingDailyQuota()` returns recipients left. `noReply` is "only possible for Google Workspace accounts, not Gmail users". Scope `script.send_mail` [CONFIRMED-primary] | reference/mail/mail-app |
| 7 | Web app deployment | `ANYONE` = "Any logged-in user". `ANYONE_ANONYMOUS` = "Any user, even if not logged in". `USER_DEPLOYING`. `/dev` "can only be accessed by users who have edit access" [CONFIRMED-primary] | manifest/web-app-api-executable, guides/web |
| 8 | ContentService redirect | Output "is redirected to a one-time URL at `script.googleusercontent.com`". Clients must follow redirects; curl needs `-L` [CONFIRMED-primary] | guides/content |
| 9 | Triggers | `everyMinutes`: n must be 1, 5, 10, 15 or 30. `nearMinute` is ±15 min. Times "might be slightly randomized". Installable triggers "always run under the account of the person who created them". Failure mail is titled "Summary of failures for Apps Script" [CONFIRMED-primary] | clock-trigger-builder, triggers/installable |
| 10 | CacheService and HMAC | Keys up to 250 characters, 100 KB per key, expiry up to 21,600 s, cap of 1,000 items. **"may be removed before this time"**. `computeHmacSha256Signature` returns `Byte[]` [CONFIRMED-primary] | reference/cache/cache, utilities |
| 11 | RTDB REST ETag | ETag only with `X-Firebase-ETag: true`. **GET + if-match → 400 "not supported"**. POST + if-match → 400. **PATCH → 400 even when only asking for an ETag**. PUT/DELETE: 200 or 412. Failed conditional requests are not retried automatically. **`if-none-match` / 304 appears nowhere**, so there is no conditional GET [CONFIRMED-primary, by absence]. RULES A3 is correct | firebase.google.com/docs/reference/rest/database (updated 2026-10-20), rest/save-data |
| 12 | RTDB REST misc | `shallow` "cannot be mixed with any other query parameters". `print=silent` → 204 for GET/PUT/POST/PATCH. `?auth=<ID_TOKEN>`. OAuth needs scopes `userinfo.email` + `firebase.database`, sent as `Bearer` or `access_token`, and lets a server "bypass your Realtime Database Rules". Denied operations are billed. Initial SSL handshake about 3.5 KB [CONFIRMED-primary] | REST ref, rest/auth, usage/billing |
| 13 | Rules language | RuleDataSnapshot methods: `val, child, parent, hasChild, hasChildren, exists, getPriority, isNumber, isString, isBoolean`. **No `numChildren`**. String methods: `length, contains, beginsWith, endsWith, replace, toLowerCase, toUpperCase, matches`. `%` and `?:` exist. `now` is in milliseconds. `$` variables are always strings. `newData` merges existing and new data (the "fred" example). `child()` accepts `'a/b/c'`. **".validate rules are only evaluated for non-null values"**, so deletes skip them [CONFIRMED-primary] | reference/security/database, security/rules-conditions |
| 14 | Rules regex | Supports `a\|b`, `?`, groups, `[..]` and `[^..]`. `^`/`$` only as first/last character. `\s \w \d` and negations. Only the `i` flag. Braces "work as normal" (`{2,4}` example) [CONFIRMED-primary]. The RULES §3.4 patterns use only these features | reference/security/database/regex |
| 15 | Trigger Email extension | Needs Blaze, uses Firestore, needs your own SMTP or provider [CONFIRMED-primary] | extensions.dev |
| 16 | Free e-mail APIs | **Brevo**: "up to 300 emails per day" once the account is approved; no credit card. **Resend**: 100/day, 3,000/month, resets at midnight UTC, received mail counts, 10 req/s; the free plan now lists 3 domains. **Mailjet**: 6,000/month, 200/day, no card, API + SMTP. **Mailgun**: 100/day, 1 custom domain. **EmailJS**: 200 requests/month, 1 req/s. **Postmark**: 100/month, no overages, never expires. **SendGrid**: free plan retired from 2025-05-27, plus 60 days of access. **MailerSend**: card needed "to prevent abuse", 500/month. **SMTP2GO**: 1,000/month, no card, "we don't allow email addresses at public domains such as Gmail" [CONFIRMED-primary] | vendor pricing pages |
| 17 | ntfy.sh e-mail | "anonymous email sending was disabled due to abuse"; you must verify your e-mail; "5 emails per visitor per day". Also: the ntfy.sh **daily message limit is 250** [CONFIRMED-primary] | docs.ntfy.sh/publish |
| 18 | ESP-Mail-Client | README: "DEPRECATED … deprecared and no longer maintained", use ReadyMail. Not archived. MIT. Last release v3.4.24 (2025-01-09). Last push 2025-10-12 [CONFIRMED-primary] | gh api |
| 19 | ReadyMail | v0.4.2 released 2026-05-20, no commits since. 16 stars. `library.json` licence `CC-BY-NC-4.0`, with USD 25 for commercial use in LICENSE. 465 SSL marked "Recommended". Known issue: "ESP32 v3.x may hang on `setPlainStart()`", where `connected()` hangs about 30 s from an lwIP select bug. Examples use `WiFiClientSecure` + `setInsecure()`. Issue #4 is open ("every second email" fails; workaround `smtp.stop()`). No other statement about Arduino-ESP32 3.x support beyond that known issue [CONFIRMED-primary] | gh api, README, docs/TROUBLESHOOTING.md |
| 20 | Other | OAuth "Testing" consent screen → refresh token expires in 7 days. Gmail sender rules: SPF or DKIM for everyone, DMARC above 5,000/day, "Don't impersonate Gmail From: headers". mbedTLS `https_request`: 42,196 B by default, 22,013 B with dynamic buffers. lib-builder `master` sets `MBEDTLS_DYNAMIC_BUFFER=y` and `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`. Cloudflare Email Sending is Beta, and sending to arbitrary recipients needs Workers Paid. Healthchecks Hobbyist: 20 checks, 100 log entries. **Measured today:** `_dmarc.gmail.com p=none; sp=quarantine`; `googlemail.com p=quarantine`; `smtp.gmail.com:465` and `*.googleusercontent.com` → WE2 → GTS Root R4, plus a cross-signed R4 issued by GlobalSign Root CA [CONFIRMED-primary / measured] | various |

---

## 3. Unverifiable (default: unverified)

- **Brevo gmail.com sender rewrite, and its exact domain** (`t-sender-sib.com` or `brevosend.com`). Both help articles returned 403. Only [SECONDARY] support.
- **Mailgun "5 authorised recipients without a card".** The help article returned 403. The pricing page does not mention a card.
- **Mailjet's warning about freemail senders.** The article returned 403.
- **EmailJS: non-browser calls off by default.** The official `/send` page does not mention it; the claim rests on the nodejs README alone.
- **Resend: 403 error 1010 without a User-Agent.** Not checked.
- **SendGrid's current 60-day trial at 100/day.** Not checked.
- **Cloudflare: sender domain must be onboarded.** The overview page does not say so.
- **Whether Gmail SMTP with an app password has its own limit below 500/day**, and whether MailApp sends count against Gmail's 500/day. No official statement.
- **Apps Script: does an error in `doPost` still return 302,** or a 200 HTML error page? Undocumented; bench-test it.
- **A per-expression size limit for RTDB rules.** The very long `$rid` `.validate` is not covered on the limits page. Emulator-test it.
- **`ScriptApp.getOAuthToken()` bypassing RTDB rules for a project-member user.** Only [SECONDARY] (FirebaseApp library).
- **The `to` object being returned as a JSON array.** Known SDK behaviour, but not re-read on an official page today.

---

## 4. Missed risks

- **M1. `getString()` can hang forever on 3.3.12.** Upstream fixed this in commit b69d9d3 (#12990, 2026-10-16): "getString() and writeToStream() never returned" when a server stops partway through a body. That fix is 34 commits after 3.3.12 and is not in the pinned build [CONFIRMED-primary]. It affects relay calls and the existing Firebase uplink. Guard reads with Content-Length and a deadline, or a task watchdog.
- **M2. Duplicate mail from the fallback.** Because `doPost` runs before the 302 (made worse by C1), "3 failed pushes → SMTP" sends duplicates. Before switching to SMTP, check `mailer/lastId` or the heartbeat in RTDB.
- **M3. De-duplication is best-effort.** CacheService entries can be evicted early, and the 1,000-item cap trims to 900. Use PropertiesService (9 KB per value, 500 KB per store) or RTDB `mailer/sent/{key}` together with `LockService`.
- **M4. Google inactive-account deletion.** A personal account unused for 2 years can be deleted. The listed activities are signed-in actions; scripts and API use are not mentioned [CONFIRMED-primary policy; INFERRED that a script alone may not count]. Sign in to the dedicated sender account at least once a year.
- **M5. 7-day OAuth expiry trap.** If the Apps Script project is moved to a standard GCP project with an External consent screen in "Testing", refresh tokens expire after 7 days and the triggers stop [INFERRED from the oauth2 page]. Keep the default script-managed project.
- **M6. Rules deploy order.** Today's rules have `$other:false` on `events/$eid` and on `cfg`. Firmware writing `rid/val/thr/em`, or the PWA writing `cfg/alertsRev`, before the new rules are deployed is rejected, and rejected requests are billed. Deploy rules first.
- **M7. Shared Gmail account.** MailApp and SMTP use the same sender account. Hitting Gmail's limit, or a suspicious-activity lock on a new automated account, stops both e-mail paths at once [INFERRED]. ntfy and Healthchecks stay independent; Brevo is the only non-Google e-mail path.
- **M8. Google can switch TLS chains.** Today the chain is ECDSA, WE2 → R4. Google could serve an RSA chain (WR2 → R1) depending on what the client offers [INFERRED]. Pin the d1 R1–R4 bundle on every Google host, not R4 alone.
- **M9. ReadyMail maintenance.** One maintainer, no commits since 2026-05-20, 4 open issues. The fallback rarely runs, so it can break unnoticed. Send a scheduled weekly SMTP self-test, or use the roughly 150-line in-house client.
- **M10. Quota visibility.** The asterisk on "100\*" and the "without notice" clause mean the limit can drop. Publish `getRemainingDailyQuota()` to RTDB (TRANSPORT already plans `mailer/quotaLeft`) and let the device cap follow it.