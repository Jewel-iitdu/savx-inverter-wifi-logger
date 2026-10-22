# Email transport options for usage-threshold alerts (ESP32-S3 → Gmail), checked 2026-10-21

Context read: `<project>/research/d1-architecture.md` (§3.8, §4.1–4.7, §5.13, §7), `<project>/research/v2-platform-verification.md` (I3–I8, F10–F12), `<project>/firmware/platformio.ini` (pioarduino 55.03.312-1 = Arduino-ESP32 3.3.12) and `firmware/include/config.h` (STACK_UPLINK 16384, PUSH_CAP_PER_DAY 40). No project files were modified. Every web page cited was fetched today. The "measured" items come from `openssl s_client`, `dig` and `gh api`, also run today.

---

## 0. Bottom line

- **PRIMARY: a Google Apps Script "mailer"** owned by a dedicated Gmail sender account and sending with `MailApp.sendEmail`.
  - **Pull mode (default):** a time trigger every 5 min reads RTDB `events`, `live` and `daily` and emails anything flagged for email. It also emails "logger offline" when `live` goes stale, and sends the 21:00 daily summary. Nothing new runs on the ESP, no email secret is stored on the device, and evaluation works on the server even when the ESP is offline.
  - **Push mode (optional, for speed):** the ESP POSTs an HMAC-signed JSON to the web app's `/exec` URL and must follow the 302 redirect. Duplicates between the two modes are removed using the event pushId.
  - It costs nothing, needs no domain, and the mail comes from Gmail itself, so it should reach a Gmail inbox well [INFERRED].
  - The limit is 100 recipients/day on a consumer account. Roughly 31 e-mails/day to one recipient uses about 31 % of it.
- **FALLBACK: Gmail SMTP sent directly from the ESP32**, to `smtp.gmail.com:465` (implicit TLS), using an **App Password for the same dedicated sender account**, never the owner's main account.
  - The ESP switches to it only when the mailer heartbeat in RTDB is older than about 20 min, or after 3 failed push attempts.
  - Use ReadyMail 0.4.2 or a small in-house SMTP client. Do not use ESP-Mail-Client, which is deprecated.
  - The limit is 500/day. The trust root, GTS Root R4, is already pinned in `roots.h`.
- **Keep the existing channels unchanged:** ntfy push for instant phone alerts and Healthchecks.io for the dead-man e-mail.
  - Optional third e-mail path for severity 5: ntfy's e-mail forwarding. It needs an ntfy.sh account with a verified address and allows 5 e-mails/day.
- **If you want a fallback outside Google** (so a single Google-account problem cannot break both e-mail paths): **Brevo's HTTPS API**.
  - 300/day, no card. A gmail.com sender is rewritten to a Brevo-owned domain. The account must be approved before it can send.
- **Ruled out:**
  - SMTP2GO: sign-up refuses gmail-type addresses.
  - Postmark: no public-domain senders, 100/month.
  - SendGrid: the free plan was retired.
  - MailerSend: needs a card and a domain.
  - Cloudflare Email: needs a domain on Cloudflare.
  - EmailJS: 200/month is too few.
  - Mailgun: without a card, the sandbox domain can send to only 5 authorised recipients.

---

## 1. Scoring matrix

Scale 0–3, 3 is best. Workload assumed: 0–30 alert e-mails/day + 1 daily summary ≈ 31/day ≈ 930/month for one recipient.

| Option | Zero cost | No domain | Gmail-inbox deliverability | ESP feasibility / maintenance | Setup effort | Quota headroom | Security | Works without ESP online | **Total /24** |
|---|---|---|---|---|---|---|---|---|---|
| **A. Apps Script poller (pull from RTDB)** | 3 | 3 | 3 | 3 (no new ESP code) | 2 | 2 (100 rcpt/day) | 3 | **3** | **22** |
| B. Apps Script webhook (ESP push) | 3 | 3 | 3 | 2 (HTTPS + redirect, 2 handshakes) | 2 | 2 | 3 | 1 | 19 |
| C. Gmail SMTP from ESP (App Password) | 3 | 3 | 3 | 1 (library churn, SMTP state machine) | 2 | 3 (500/day) | 1 (App Password in unencrypted NVS) | 0 | 16 |
| D. Brevo HTTPS API | 3 | 2 (From rewritten) | 2 [INFERRED] | 3 (one POST, GTS R4) | 1 (approval + sender verification) | 3 (300/day) | 2 | 0 | 16 |
| E. Resend via `resend.dev`, sending only to the account owner | 3 | 2 | 2 [INFERRED] | 3 | 3 | 2 (100/day) | 2 | 0 | 17, but the vendor says this domain is for testing only |
| F. Mailjet, single-sender gmail.com | 3 | 2 | 1 (vendor discourages freemail senders) | 2 (new root: DigiCert G2) | 2 | 3 (200/day) | 2 | 0 | 15 |
| G. EmailJS through your own Gmail | 3 | 3 | 3 | 3 | 2 | **0** (200/month) | 1 (third party holds a Gmail send grant) | 0 | 15 |
| H. ntfy.sh `Email:` header | 3 | 3 | 2 [INFERRED] | 3 (one header on the existing POST + token) | 2 (account, verified e-mail) | **0** (5/day) | 2 | 0 | 15, fine as a severity-5 tertiary path |
| I. Mailgun free, sandbox | 2 | 2 | 1 [INFERRED] | 2 (DigiCert G2) | 2 | 2 | 2 | 0 | 13 |
| Healthchecks.io | 3 | 3 | – | 3 | 3 | – | 3 | 3 (offline only) | Dead-man only, keep it |
| Postmark / SendGrid / MailerSend / SMTP2GO / Cloudflare Email | **✗** | | | | | | | | Disqualified, see §2.4 |

---

## 2. Findings per option

### 2.1 Gmail SMTP directly from the ESP32

**Account side**
- An App Password requires 2-Step Verification on the account. [CONFIRMED-primary, support.google.com/accounts/answer/185833]
- The option is missing when 2-Step Verification uses security keys only, on work or school accounts, and under Advanced Protection. [CONFIRMED-primary, same page]
- Google revokes every App Password when the account password changes. [CONFIRMED-primary, same page]
- Google says App Passwords are not recommended and are unnecessary in most cases. No deprecation date is announced. [CONFIRMED-primary, same page]
- Gmail says it no longer supports third-party apps or devices that need your username and password, and points to "Sign in with Google". The App Password is the remaining path. [CONFIRMED-primary, support.google.com/mail/answer/7126229]
- Google could tighten this further later. [INFERRED risk]

**Server and limits**
- Server `smtp.gmail.com`: port 465 is SSL, port 587 is TLS (STARTTLS). [CONFIRMED-primary for Workspace on knowledge.workspace.google.com "Send email from a printer, scanner, or app". The same host for consumer accounts is SECONDARY/common knowledge.]
- That Workspace page lists 2,000/day and says less secure apps ended for Workspace on 2025-05-01. This does not apply to consumer accounts. [CONFIRMED-primary]
- **Consumer sending limit:** more than 500 e-mails in a day, or more than 500 recipients on one e-mail, triggers the limit. Sending resumes after 1–24 h. [CONFIRMED-primary, support.google.com/mail/answer/22839]
  - Hitting this limit blocks sending for the whole account. That is another reason to use a dedicated sender account. [INFERRED]
- **OAuth (XOAUTH2) is not practical on the device.** For an External consent screen in "Testing" status, refresh tokens expire after 7 days. [CONFIRMED-primary, developers.google.com/identity/protocols/oauth2] Gmail scopes are also restricted. That leaves the App Password as the only workable credential for a headless ESP. [INFERRED]

**Library status, 2026** (measured with `gh api`)

| Library | Status |
|---|---|
| mobizt/ESP-Mail-Client | README: "This library is now deprecared and no longer maintained." It points to ReadyMail. Last release v3.4.24 on 2025-01-09. The examples directory was deleted on 2025-10-12. Not archived. MIT. |
| mobizt/ReadyMail | v0.4.2, tagged 2026-05-20. 16 stars. Licence **CC BY-NC 4.0**: free for non-commercial use, USD 25 for commercial use. SMTP and IMAP, SSL and STARTTLS. Takes any Arduino `Client`; examples use `WiFiClientSecure` with `setInsecure()`, which we must replace with `setCACert`. Supports `readymail_auth_password` and `readymail_auth_accesstoken`. |
| ReadyMail open issue #4 (2026-08-25) | When sending several e-mails, every second one fails with "Authentication required". The reporter's workaround is calling `smtp.stop()` after each send. |
| ReadyMail known issue (Troubleshooting guide) | On ESP32 core v3.x, `setPlainStart()` (needed for STARTTLS on 587) can make `connected()` hang for about 30 s because of an lwIP select bug. Its Connection Guide marks 465/SSL as recommended. |

**Choice:** use **port 465 (implicit TLS)** with `NetworkClientSecure::setCACert(GTS Root R4)`.
- Measured chain: `smtp.gmail.com` → WE2 → GTS Root R4. R4 is already pinned for identitytoolkit in D1 §5.13. [CONFIRMED, measured]
- Alternative to ReadyMail: an in-house SMTP-over-TLS client of about 150 lines (EHLO, AUTH PLAIN, MAIL FROM, RCPT TO, DATA with dot-stuffing and an RFC 2047 subject, QUIT). It avoids the NC licence and the young dependency. [INFERRED]

**RAM and stack**
- Espressif's mbedTLS table for the `https_request` example: about 42.2 KB heap by default, about 22.0 KB with dynamic TX/RX buffers. Espressif notes the figures change with configuration and version. [CONFIRMED-primary, docs.espressif.com …/protocols/mbedtls.html]
- Arduino's lib-builder `defconfig.common` (master) enables `CONFIG_MBEDTLS_DYNAMIC_BUFFER` and the free-peer-cert / free-config-data options. It sets `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`. [CONFIRMED-primary for master; INFERRED for the 3.3.12 build]
- A second TLS session next to the open Firebase keep-alive therefore costs roughly 22–42 KB of heap, part of which may land in PSRAM. [INFERRED]
- Run it inside the existing 16 KB uplink task and check `uxTaskGetStackHighWaterMark`. On the S3 this returns bytes. [CONFIRMED-primary, ESP-IDF ram-usage page]

**Security**
- NVS is not encrypted (D1 §5.13). Anyone who opens the box and dumps the flash gets the App Password. That gives SMTP and IMAP access to that Google account. [INFERRED]
- Mitigations:
  - Use a dedicated sender account that holds no other data.
  - Revoke at myaccount.google.com/apppasswords.
  - Never use the owner's main account.

**Network**
- Bangladeshi residential ISPs usually block only port 25. [INFERRED] Before relying on 465, test it from the home LAN with `openssl s_client -connect smtp.gmail.com:465`.

### 2.2 Google Apps Script web app and time-trigger relay

**Quotas for consumer gmail.com accounts** [CONFIRMED-primary, developers.google.com/apps-script/guides/services/quotas]
- E-mail recipients per day (MailApp): **100**. Workspace gets 1,500.
- 50 recipients per message. Body up to 200 KB.
- Script runtime: 6 min per execution.
- **Total trigger runtime: 90 min/day.** 20 triggers per user per script.
- URL Fetch: 20,000 calls/day. 30 simultaneous executions per user.
- Quotas are per user and reset 24 h after the first request (a rolling window, not midnight). Google can change them without notice.
- `MailApp.getRemainingDailyQuota()` returns how many recipients are left today.

**MailApp details**
- `sendEmail` takes advanced options `name`, `replyTo` and `htmlBody`. `noReply` works on Workspace only, not consumer Gmail. [CONFIRMED-primary, reference/mail/mail-app]
- It needs the scope `script.send_mail`. [CONFIRMED-primary] That is narrower than GmailApp's full-mailbox scope, so use MailApp.

**Deployment and access**
- Manifest `webapp.executeAs = USER_DEPLOYING` ("Execute as me") and `access = ANYONE_ANONYMOUS` (anyone, even when not logged in). `ANYONE` would require a Google login. [CONFIRMED-primary, apps-script/manifest/web-app-api-executable]
- The `/dev` URL only works for people with edit access to the script. Devices must use `/exec`. [CONFIRMED-primary, guides/web]
- **`doPost(e)` cannot see request headers.** The event object only has `queryString`, `parameter(s)`, `pathInfo`, `contentLength` and `postData.{length,type,contents}`. The shared secret or HMAC therefore has to travel in the body. [CONFIRMED-primary, guides/web field table]
- `Utilities.computeHmacSha256Signature(value, key)` exists. [CONFIRMED-primary] CacheService entries last at most 21,600 s (6 h), hold up to 100 KB each, with a cap of 1,000 entries. [CONFIRMED-primary] This is enough to drop duplicate event IDs.

**The 302 redirect**
- ContentService output is redirected to a one-time URL on `script.googleusercontent.com`. Clients must follow redirects (curl needs `-L`). [CONFIRMED-primary, guides/content]
- Arduino-ESP32 **3.3.12 `HTTPClient`** behaviour, read from the source at tag 3.3.12 [CONFIRMED-primary]:
  - The default is `HTTPC_DISABLE_FOLLOW_REDIRECTS`, so you must call `setFollowRedirects(...)`.
  - For **302/303**, any mode other than DISABLE follows the redirect, switches the method to GET and drops the payload. This matches what Apps Script expects.
  - 301/307 keep the method only with `HTTPC_FORCE_FOLLOW_REDIRECTS`.
  - `setURL()` accepts an absolute URL on another host if the scheme matches, and keeps the caller's `NetworkClient`. The CA set on it therefore applies to both hops.
  - Each hop opens a new connection, so one send costs two TLS handshakes.
- **Both hosts chain to GTS Root R4** (`script.google.com` and `script.googleusercontent.com` → WE2 → GTS Root R4). One pinned root covers both hops. [CONFIRMED, measured]
- `doPost` runs before the 302 is returned. If the second hop fails, the e-mail has probably already gone out, so retries must be idempotent (event-ID dedupe). [INFERRED]
- If the response is an HTML Google login page, access is not set to anonymous. [INFERRED]

**Triggers**
- `everyMinutes(n)` accepts only n = 1, 5, 10, 15 or 30. `nearMinute()` is ±15 min. Time-driven triggers are slightly randomised. [CONFIRMED-primary, clock-trigger-builder and triggers/installable]
- Installable triggers run as the account that created them. When a run fails, Apps Script e-mails the owner a "Your script has recently failed to finish successfully" notice. That is a free health signal. [CONFIRMED-primary]
- Runtime budget [INFERRED, assuming about 2 s per run]:
  - every 5 min: 288 runs ≈ 10 min/day, 11 % of the 90 min.
  - every 1 min: 1,440 runs ≈ 48 min/day, about 53 %, which is tight.
  - Recommendation: **every 5 min**, and use push mode when latency matters.

**Reading RTDB from Apps Script.** Two ways:
- (a) **Least privilege (recommended):** a third Firebase Auth user "MAILER". The script signs in through the Identity Toolkit REST API (V2 F10 [CONFIRMED-primary]), caches the ID token for about 55 min in CacheService, and reads with `?auth=<idToken>`.
  - The rules give `MAILER_UID` read on `/devices/inv1` and write on `/devices/inv1/mailer`, which holds the heartbeat, last send and quota.
  - The RTDB REST docs describe `?auth=<ID_TOKEN>` for user tokens. [CONFIRMED-primary, firebase.google.com/docs/database/rest/auth]
- (b) **Simpler:** `ScriptApp.getOAuthToken()` with `userinfo.email` and `firebase.database` added to `oauthScopes`, sent as `Authorization: Bearer`.
  - Firebase documents both scopes and the Bearer header. [CONFIRMED-primary]
  - `getOAuthToken` returns the effective user's token, and extra scopes are declared in the manifest. [CONFIRMED-primary]
  - That this works for a user who is a project member, and that it bypasses rules (admin-level), comes from the FirebaseApp Apps Script library pattern. [SECONDARY] It is broader than needed.

**Extra RTDB download cost of polling** [INFERRED]
- Each run makes 1–2 GETs. Each is a new TLS connection costing about 3.5 KB, as measured in V2, plus a small body.
- At a 5-min interval: about 1.2–2.5 MB/day, about 35–75 MB/month, under 1 % of the Spark 10 GB/month.

**Latency and reliability** [INFERRED]
- Push mode: a few seconds, including two handshakes and the script's cold start. Set the HTTP timeout to at least 20 s.
- Pull mode: up to the 5-min interval plus jitter.
- If Apps Script has an outage, push requests fail and the ESP fallback takes over (§3).

**Deliverability** [INFERRED]
- MailApp sends as the script owner through Gmail, so the message is DKIM-aligned to gmail.com. This is as good as it gets for a Gmail inbox without a domain.
- Mail a Gmail account sends to itself may not raise a phone notification. A **dedicated sender account** sending to the owner's main address avoids that.

### 2.3 HTTPS e-mail APIs with free tiers

**Gmail's sender rules**
- All senders need SPF or DKIM. DMARC is mandatory only for bulk senders (more than 5,000/day). Google also tells senders not to impersonate Gmail From: headers, and says doing so may hurt delivery. [CONFIRMED-primary, support.google.com/a/answer/81126]

**DMARC records today** [CONFIRMED, measured with `dig`]
- `_dmarc.gmail.com`: `p=none; sp=quarantine`
- `googlemail.com`: `p=quarantine`
- `yahoo.com`: `p=reject`

So a third party sending `From: …@gmail.com` fails DMARC alignment. It is not rejected today, but it is likely to land in spam. [INFERRED]

| Provider | Free limits | Domain needed? / gmail.com From | Card | Approval / friction | TLS root (measured) | Verdict |
|---|---|---|---|---|---|---|
| **Brevo** | Up to **300/day** once the account is approved for sending; "Free forever, no credit card needed" [CONFIRMED-primary, brevo.com/pricing FAQ] | No domain needed. A gmail.com sender cannot be authenticated, so Brevo **rewrites it** to `name@<id>.t-sender-sib.com` for transactional mail [CONFIRMED-primary, help.brevo.com 14925263522578 and 16045394674066]. API `POST https://api.brevo.com/v3/smtp/email`, `api-key` header, sender must be verified [CONFIRMED-primary, developers.brevo.com] | No | Account approval before sending [CONFIRMED-primary] | WE1 → GTS R4 (already pinned) | Best non-Google fallback |
| **Resend** | 100/day, 3,000/month (sent and received both count), daily reset at midnight UTC, 10 req/s [CONFIRMED-primary, resend.com/docs account-quotas and api-reference] | Without a domain, `onboarding@resend.dev` can only send to **your own account address**. The vendor says the domain is "only available for testing purposes" [CONFIRMED-primary, knowledge-base 403-error-resend-dev-domain] | Not stated | Low. Requests without a User-Agent get a 403 (error 1010) [CONFIRMED-primary, llms.txt index] | WE1 → GTS R4 | Works for a single owner, but it goes against the vendor's stated testing-only purpose |
| **Mailjet** | 6,000/month, **200/day**, APIs and SMTP included, no card [CONFIRMED-primary, mailjet.com/pricing] | Single-address sender validation is possible. Mailjet strongly discourages freemail senders and says spam and junk placement becomes more likely [CONFIRMED-primary, documentation.mailjet.com 19308800104091] | No | Not stated | DigiCert Global Root G2 (new root to pin) | Weak deliverability without a domain |
| **Mailgun** | Free plan 100/day, 1 custom domain [CONFIRMED-primary, mailgun.com/pricing] | With no card on file you can only send to **at most 5 authorised recipients** from the sandbox domain [CONFIRMED-primary, help.mailgun.com 217531258] | Needed to lift the sandbox | Recipients must confirm | DigiCert G2 | Testing tool, not production |
| **EmailJS** | **200 requests/month**, 2 templates, 1 request/s [CONFIRMED-primary, emailjs.com/pricing and docs/rest-api/send] | Sends through the e-mail service **you connect, e.g. your own Gmail**. EmailJS recommends personal services only for very low volume [CONFIRMED-primary, docs/user-guide/connecting-email-services]. Non-browser API calls are off by default and must be enabled under Account → Security; the private key is sent as `accessToken` [CONFIRMED-primary, official emailjs-nodejs README] | No | Low | WE1 → GTS R4 | Quota too small for up to 30/day |
| **Postmark** | 100/month, no overages, does not expire [CONFIRMED-primary, postmarkapp.com/pricing] | Manual account approval. Before approval it can send only to domains you own [CONFIRMED-primary, support article 1084]. Gmail addresses refused as sign-up and sender [SECONDARY, Postmark blog] | No | High | Amazon Root CA 1 | ✗ |
| **SendGrid** | Free plan **retired from 2025-05-27** [CONFIRMED-primary, twilio.com changelog]. Now a 60-day trial at 100/day [CONFIRMED-primary, twilio.com email-api pricing] | – | – | – | GoDaddy | ✗ |
| **MailerSend** | Free plan 500/month, 100 API requests/day, 1 sending domain; **card details required** [CONFIRMED-primary, mailersend.com/pricing]. Sandbox mode: 2 recipients until approved [CONFIRMED-primary, help article] | Domain-based | Yes | Approval | WE1 → GTS R4 | ✗ |
| **SMTP2GO** | 1,000/month, no card [CONFIRMED-primary, smtp2go.com/pricing] | **Sign-up refuses public-domain addresses such as Gmail** [CONFIRMED-primary, same page] | No | Needs a private-domain address | Sectigo R46 / USERTrust RSA | ✗ for this user |

**Bangladesh sign-up:** no published country block for Bangladesh was found for Brevo, Mailjet, Resend, Mailgun or EmailJS. Bangladesh is not under comprehensive US or EU sanctions. Fraud screening at sign-up can still reject accounts. [INFERRED; searched and nothing found]

### 2.4 Other routes

- **ntfy.sh e-mail forwarding**
  - Uses the `X-Email` / `Email` header, one address per message. [CONFIRMED-primary, docs.ntfy.sh/publish]
  - **On ntfy.sh, anonymous e-mail sending is disabled because of abuse.** You must verify an address in your ntfy account, and **free users get 5 e-mails per visitor per day**. [CONFIRMED-primary, same page and docs.ntfy.sh/config]
  - With the default self-hosted configuration, the sender's IP appears in the e-mail body. [CONFIRMED-primary, docs.ntfy.sh/publish; whether ntfy.sh does the same is INFERRED]
  - Paid tiers allow 50, 250 or 500 e-mails/day. [CONFIRMED-primary, ntfy.sh]
  - The ESP would need an ntfy access token. Use it only for a severity-5 tertiary e-mail.
- **Healthchecks.io:** Hobbyist plan has 20 checks and 100 log entries per check. [CONFIRMED-primary, healthchecks.io/pricing] It sends its own "down" e-mails. The docs do not say whether a `/fail` request body is included in the notification e-mail. Request bodies are logged up to 100 kB. [CONFIRMED-primary, docs/attaching_logs] **Use it only as the dead-man switch.**
- **Cloudflare Email Service**
  - Email Sending is **Beta**.
  - Sending to *arbitrary* recipients **requires Workers Paid**. Sending to verified destination addresses is free on the Free plan.
  - The sender domain must be onboarded; the error `E_SENDER_DOMAIN_NOT_AVAILABLE` covers that case. [CONFIRMED-primary, developers.cloudflare.com/email-service and …/send-email-workers]
  - So you need a domain on Cloudflare, which costs money. ✗ unless you already own one. A Worker Cron could also do server-side evaluation, so this is the upgrade path D1 already mentions.

---

## 3. Recommended design

### 3.1 Primary: Apps Script mailer

- **Accounts:** create a new Gmail account to send from, for example `xxx.solar.alerts@gmail.com` (you have to create this yourself). Turn on 2-Step Verification. It owns the script and later the fallback App Password.
- **Manifest (`appsscript.json`):**
  - `"timeZone": "Asia/Dhaka"`
  - `"webapp": {"executeAs":"USER_DEPLOYING","access":"ANYONE_ANONYMOUS"}`
  - `oauthScopes`: `script.send_mail`, `script.external_request`, `script.scriptapp`
  - Add `userinfo.email` and `firebase.database` only if you choose RTDB read method (b).
  - The first deployment shows the "unverified app" consent screen for a personal script. [INFERRED]
- **Script Properties:** `HMAC_KEY`, `TO`, and the MAILER Auth credentials.
  - **Never take the recipient from the request.** A leaked secret can then only e-mail the owner's own configured recipients.
- **Pull poller** (`everyMinutes(5)`):
  - Read `events/{today}` with `orderBy="$key"&startAt="<cursor>"`. At day rollover also read `{yesterday}`.
  - E-mail each event that has `em:1`, then advance the cursor in Script Properties.
  - If `live.ts` is more than 15 min old, send one "logger offline" e-mail. This overlaps with Healthchecks; keep both or pick one.
  - Write `/devices/inv1/mailer = {hb, lastTs, lastId, quotaLeft}` so the PWA can show mailer health and the ESP can make its fallback decision.
- **Daily summary:** `atHour(21).nearMinute(0).everyDays(1)`, which fires within ±15 min. Build it from `/daily/{today}`. If there is no data, the e-mail says so, which also works as an outage signal. This replaces D1's ntfy `DAILY_SUMMARY` for the e-mail channel.
- **Quota guard:**
  - Call `getRemainingDailyQuota()` before each send.
  - Below 5 left, send only severity 5 plus the summary.
  - Group events that arrive within one poll into a single digest e-mail.
  - Keep a separate e-mail cap of about 60/day, apart from D1's 40 pushes/day.
- **Push mode (optional).** Function body:

```js
// Apps Script: verify, dedupe, send. The recipient comes from Script Properties.
function doPost(e) {
  const b = JSON.parse(e.postData.contents);               // {id, ts, sev, subj, body, sig}
  const P = PropertiesService.getScriptProperties();
  const raw = Utilities.computeHmacSha256Signature(`${b.id}|${b.ts}|${b.subj}|${b.body}`, P.getProperty('HMAC_KEY'));
  const hex = raw.map(x => ('0' + (x & 0xff).toString(16)).slice(-2)).join('');
  if (hex !== b.sig || Math.abs(Date.now() / 1000 - b.ts) > 900) return out({ ok: false, err: 'auth' });
  const c = CacheService.getScriptCache();
  if (c.get('ev:' + b.id)) return out({ ok: true, dup: true });          // retried after a lost 2nd hop
  if (b.sev < 5 && MailApp.getRemainingDailyQuota() < 5) return out({ ok: false, err: 'quota' });
  MailApp.sendEmail({ to: P.getProperty('TO'), subject: '[Solar] ' + b.subj, body: b.body, name: 'Solar logger' });
  c.put('ev:' + b.id, '1', 21600);                                      // poller also skips ids in cache
  return out({ ok: true });
}
const out = o => ContentService.createTextOutput(JSON.stringify(o)).setMimeType(ContentService.MimeType.JSON);
```

```cpp
// ESP (uplink task). One root covers both hops (measured: WE2 -> GTS Root R4).
NetworkClientSecure tls; tls.setCACert(GTS_ROOT_R4_PEM);
HTTPClient http;
http.begin(tls, APPS_SCRIPT_EXEC_URL);                    // .../macros/s/<id>/exec
http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // 302 -> GET, payload dropped (HTTPClient.cpp 3.3.12)
http.setTimeout(20000);
http.addHeader("Content-Type", "application/json");
int code = http.POST(json);                               // doPost runs on hop 1; hop 2 fetches its output
bool ok = (code == 200) && http.getString().indexOf("\"ok\":true") >= 0;
http.end(); tls.stop();
```

- `id` is the RTDB pushId of the event that was already written, so the poller and push mode dedupe on the same key.
- HMAC on the ESP: mbedTLS `mbedtls_md_hmac` (SHA-256), which ships with Arduino-ESP32. [INFERRED]

### 3.2 Fallback: Gmail SMTP from the ESP

- **When:** the ESP reads `/devices/inv1/mailer/hb` every 5 min. If it is older than 20 min, or push mode fails 3 times in a row, events with `em:1` are sent over SMTP. A cap of 20/day applies.
- **Credential:** an App Password for the dedicated sender account, stored write-only in NVS like the other secrets (D1 §5.13). It is useless after a password change, so add a "re-enter App Password" field to the local `/api/config`.
- **Transport:** 465 with implicit TLS and GTS Root R4. Close the session after each message, which also works around ReadyMail issue #4. Set the Date header from NTP time.
- **Shared failure:** a password change on the sender account kills the App Password [CONFIRMED-primary]. It may also revoke the script's authorisation [INFERRED]. ntfy push and Healthchecks still work in that case, and Brevo (§2.3) is the option if you want a non-Google e-mail path as well.

### 3.3 Integration notes for the rules and the web app

These are suggestions; the actual rules design belongs to its own workstream.
- **Rules:** `cfg/rules/{id}` = `{en, name, metric (w|ld|eld|egr|epv|soc|bv|pvw|tmp|…), cmp, thr, holdS, hyst, coolMin, from, to, days, sev, ch:{email,ntfy}}`. Recipients go in `cfg/mail = {to[], dailyCap, digestMin}`.
- **Where to evaluate:**
  - Power and voltage rules with hold or hysteresis: on the ESP, which has 5-s samples, using the existing D1 §7.3 state machine. It writes events with `em:1`.
  - Energy-of-day rules, such as "today's load above X kWh", and staleness: these can also run on the server from `live.eld` and `daily`, so they still fire when the ESP is off.
- **"Send test e-mail" button in the PWA:** it writes `cfg.testEmail = ts`. The poller sends a test e-mail and writes the result to `mailer/lastTs`.
- **iPhone delivery:** use the Gmail app for push notifications. iOS Mail fetches Gmail on a schedule rather than by push. [INFERRED] Add a Gmail filter on `[Solar]` set to "Never send to Spam" and "Mark as important".

---

## 4. Things you need to do or decide
1. Create the dedicated sender Gmail account and turn on 2-Step Verification. Google may ask for phone verification.
2. Decide whether the delay from 5-minute polling is acceptable for these e-mails (ntfy stays instant), or whether to build push mode now.
3. Choose how Apps Script reads RTDB: a separate MAILER Auth user with a rules change (recommended), or the owner's OAuth token.
4. Decide whether to add Brevo as a non-Google fallback. It needs a sign-up and account approval.
5. From the home network, test that `smtp.gmail.com:465` and `script.google.com` are reachable.

---

## Sources (all fetched 2026-10-21)
- Google: [App passwords](https://support.google.com/accounts/answer/185833) · [Gmail sending limits](https://support.google.com/mail/answer/22839) · [Gmail with other mail apps](https://support.google.com/mail/answer/7126229) · [Workspace: send email from a printer, scanner, or app](https://knowledge.workspace.google.com/admin/gmail/send-email-from-a-printer-scanner-or-app) · [Email sender guidelines](https://support.google.com/a/answer/81126) · [OAuth 2.0 refresh-token expiry](https://developers.google.com/identity/protocols/oauth2)
- Apps Script: [Quotas](https://developers.google.com/apps-script/guides/services/quotas) · [Web apps](https://developers.google.com/apps-script/guides/web) · [Content service redirects](https://developers.google.com/apps-script/guides/content) · [Web app manifest](https://developers.google.com/apps-script/manifest/web-app-api-executable) · MailApp · ScriptApp · [Installable triggers](https://developers.google.com/apps-script/guides/triggers/installable) · ClockTriggerBuilder · [Scopes](https://developers.google.com/apps-script/concepts/scopes) · Utilities · Cache · Deployments REST resource
- Firebase: [RTDB REST auth](https://firebase.google.com/docs/database/rest/auth) · [FirebaseApp for Apps Script, SECONDARY](https://github.com/RomainVialard/FirebaseApp)
- ESP32: [ESP-Mail-Client](https://github.com/mobizt/ESP-Mail-Client) · [ReadyMail](https://github.com/mobizt/ReadyMail) (README, CONNECTION_GUIDE, TROUBLESHOOTING, library.json, issue #4, read with `gh api`) · [arduino-esp32 HTTPClient.cpp/.h at tag 3.3.12](https://github.com/espressif/arduino-esp32/tree/3.3.12/libraries/HTTPClient/src) · [esp32-arduino-lib-builder defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common) · ESP-IDF mbedTLS memory · [ESP-IDF RAM usage](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html)
- Providers: [Brevo pricing](https://www.brevo.com/pricing/) and [pricing.md](https://www.brevo.com/pricing.md) · [Brevo sender requirements](https://help.brevo.com/hc/en-us/articles/14925263522578) · [Brevo domain troubleshooting](https://help.brevo.com/hc/en-us/articles/16045394674066) · [Brevo transactional API](https://developers.brevo.com/docs/send-a-transactional-email) · [Resend pricing](https://resend.com/pricing) · [Resend quotas](https://resend.com/docs/knowledge-base/account-quotas-and-limits) · [Resend resend.dev 403](https://resend.com/docs/knowledge-base/403-error-resend-dev-domain.md) · [Mailjet pricing](https://www.mailjet.com/pricing/) · [Mailjet on freemail senders](https://documentation.mailjet.com/hc/en-us/articles/19308800104091) · [Mailjet sender validation](https://dev.mailjet.com/docs/email-api/senders-domains/sender-validation) · [Mailgun pricing](https://www.mailgun.com/pricing/) · [Mailgun authorised recipients](https://help.mailgun.com/hc/en-us/articles/217531258-Authorized-Recipients) · [EmailJS pricing](https://www.emailjs.com/pricing/) · [EmailJS /send](https://www.emailjs.com/docs/rest-api/send/) · [EmailJS email services](https://www.emailjs.com/docs/user-guide/connecting-email-services/) · [emailjs-nodejs README](https://github.com/emailjs-com/emailjs-nodejs) · [Postmark pricing](https://postmarkapp.com/pricing) · [Postmark approval](https://postmarkapp.com/support/article/1084-how-does-the-account-approval-process-work) · [Postmark on Gmail sign-ups, SECONDARY](https://postmarkapp.com/blog/why-cant-i-use-gmail-address) · [SendGrid free-plan retirement](https://www.twilio.com/en-us/changelog/sendgrid-free-plan) · [Twilio email pricing](https://www.twilio.com/en-us/products/email-api/pricing) · [MailerSend pricing](https://www.mailersend.com/pricing) · [MailerSend sandbox and trial](https://www.mailersend.com/help/sandbox-mode-and-the-professional-trial) · [SMTP2GO pricing](https://www.smtp2go.com/pricing/)
- Others: [ntfy publish](https://docs.ntfy.sh/publish/) · [ntfy config](https://docs.ntfy.sh/config/) · [ntfy.sh](https://ntfy.sh/) · [Healthchecks pricing](https://healthchecks.io/pricing/) · [Healthchecks signalling failures](https://healthchecks.io/docs/signaling_failures/) · [Healthchecks attaching logs](https://healthchecks.io/docs/attaching_logs/) · [Cloudflare Email Service](https://developers.cloudflare.com/email-service/) · [Cloudflare send_email binding](https://developers.cloudflare.com/email-routing/email-workers/send-email-workers/)
- Measured: TLS chains of `smtp.gmail.com:465`, `script.google.com`, `script.googleusercontent.com` and `api.{brevo,emailjs,resend,mailjet,smtp2go,mailgun,postmarkapp,mailersend,sendgrid}` with `openssl s_client -showcerts`. DMARC TXT for gmail.com, googlemail.com, yahoo.com and outlook.com with `dig`. Repository status for mobizt/ReadyMail, ESP-Mail-Client and ESP_SSLClient, and arduino-esp32 tags, with `gh api`.