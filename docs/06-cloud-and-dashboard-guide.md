# 06 — Cloud and dashboard guide

This guide sets up everything off the board: the Firebase project (Realtime Database, sign-in, API keys), the database rules, the GitHub repository and its Actions pipeline, hosting the dashboard (PWA) on Cloudflare, installing it on the iPhone, the push channels (ntfy, Telegram, Healthchecks), the e-mail mailer and usage-threshold rules, and how to watch the free quotas and export data.

It follows the code and config in this repository: [`web/`](../web), [`firebase/`](../firebase), [`firebase.json`](../firebase.json), [`.github/workflows/`](../.github/workflows), [`tools/render-rules.sh`](../tools/render-rules.sh), [`tools/rules-test/`](../tools/rules-test) and the mailer in [`tools/apps-script/`](../tools/apps-script). The decisions behind it are [r7](../research/r7-web-hosting.md) (hosting, CI/CD) and [r8](../research/r8-email-alerts.md) (e-mail alerts). The board side (portal, local page, OTA) is [05](05-firmware-guide.md). Where [04](04-architecture-and-data-model.md), [07](07-milestones-and-test-plan.md) or [08](08-risks-troubleshooting-maintenance.md) still say "Firebase Hosting" for the PWA or "Events page", **this guide wins**: the PWA is on Cloudflare (Firebase Hosting is the cold fallback) and the page is now **Alerts**.

> ⚠️ **Zero cost, read-only.** Every service here is used on a free plan with **no card on file**. Never upgrade Firebase to Blaze. Nothing in this guide talks to the inverter; the logger stays read-only (`INV_ALLOW_WRITES=0`, COM pins 1/2/8 only, never pin 4, never the BMS jack).

> ⚠️ **You run every login, deploy and push yourself.** Commands that open a browser (`wrangler login`, `firebase-tools login`) or change a live service are marked "you run". Never paste ID tokens, API tokens, passwords or `?auth=` URLs into a chat, an issue or a screenshot.

**Verified on 2026-10-21 (no login, no deploy):** `python3 tools/gen_codes.py --check` OK; `cd web && npx tsc --noEmit && npm run build` OK (Vite 6.4.4, Node 20.17); `cd tools/apps-script && npm test` 44/44 and `npm run check` OK; `tools/render-rules.sh` renders and rejects duplicate UIDs (run on a scratch copy). Re-run in the final consistency check (scratch firebase-tools 15.32.1, Java 21, no login): the rules emulator suite `sh tools/rules-test/run.sh` (`87 passed, 0 failed`, parity `5414 agree, 0 disagree`), the mailer's `e2e.js` against the real rules (`E2E OK`), `actionlint` 1.7.12 on the three workflows and `shellcheck` on both scripts. **Not yet verified anywhere:** the production Cloudflare headers, Safari's handling of the CSP, Workload Identity Federation on a Spark project, and the whole chain on real hardware.

> **Not yet implemented** (workarounds where they exist)
> - **No export script.** `tools/rtdb-export.sh` (named in [04](04-architecture-and-data-model.md), [07](07-milestones-and-test-plan.md) M9, [08](08-risks-troubleshooting-maintenance.md)) does not exist. §12 gives the manual `curl` export and the PWA's CSV exports.
> - **CI does not run the rules emulator suite** (`tools/rules-test/run.sh`, needs Java). Run it on the Mac before changing the rules (§3.5). The mailer's unit tests do run in CI (`ci.yml` job `mailer`).
> - **The mailer is not deployed by CI.** You paste `Code.gs`, `Lib.gs` and `appsscript.json` into script.google.com by hand (Google's `clasp` CLI is optional and not set up).
> - **PR previews are off** by design (Cloudflare preview URLs are public); they can be switched on with one variable (§5.8).
> - **No Web Push on iOS** (no backend on Spark): alerts come by ntfy, Telegram, Healthchecks e-mail and the mailer's e-mail.
> - **Family viewer accounts** need a manual rules edit ([firebase/README.md](../firebase/README.md) §7).
> - **Step 3 of [`tools/apps-script/README.md`](../tools/apps-script/README.md) (§5) still shows an in-place `sed` on `firebase/database.rules.json`.** Do not use it: it would put real UIDs into the committed file (CI then fails). Use the render script or the GitHub variable (§9.2).

---

## Contents

1. [Overview and order](#1-overview-and-order)
2. [Firebase project](#2-firebase-project)
3. [Database rules: render, deploy, test](#3-database-rules-render-deploy-test)
4. [GitHub repository and Actions](#4-github-repository-and-actions)
5. [Hosting the dashboard on Cloudflare](#5-hosting-the-dashboard-on-cloudflare)
6. [Local development and demo mode](#6-local-development-and-demo-mode)
7. [iPhone: install and sign in](#7-iphone-install-and-sign-in)
8. [Push channels: ntfy, Telegram, Healthchecks](#8-push-channels-ntfy-telegram-healthchecks)
9. [E-mail alerts: mailer, recipients and usage-threshold rules](#9-e-mail-alerts-mailer-recipients-and-usage-threshold-rules)
10. [Entering the secrets in the logger](#10-entering-the-secrets-in-the-logger)
11. [Watching the free quotas](#11-watching-the-free-quotas)
12. [Data export](#12-data-export)
13. [Limits and cost summary](#13-limits-and-cost-summary)
14. [Troubleshooting](#14-troubleshooting)
15. [Sources](#15-sources)

---

## 1. Overview and order

```
 Mac ──git push──► GitHub (private repo)
                     ├─ ci.yml           checks: codes, rules lint, web build, firmware build + tests, mailer tests
                     ├─ deploy-rules.yml render UIDs ─► firebase deploy --only database ─┐
                     └─ deploy-web.yml   build ─► npx wrangler deploy ─┐                 │
                                                                    ▼                 ▼
                         Cloudflare: https://solar.<acct>.workers.dev    Firebase RTDB (asia-southeast1)
                                     │ (PWA shell, cached by the SW)             ▲   ▲            ▲
                                     ▼                                           │   │ REST        │ REST
                         iPhone PWA ── owner sign-in (browser key) ──────────────┘   │ device key  │ device key
                                                                         ESP32 logger ┘   Apps Script mailer
                         ntfy / Telegram / Healthchecks ◄── ESP32 logger            (Gmail sender) ──► e-mail
```

Three Firebase Auth users, all email/password: **owner** (you, in the PWA), **device** (only the ESP32), **mailer** (only the Apps Script). Two API keys: a **browser key** (restricted to the dashboard's web origins) and a **device key** (`savx-device`, no referrer restriction, only the two sign-in APIs) for the ESP32, the mailer, `curl` tests and local development ([r7](../research/r7-web-hosting.md) §6.2).

**Order of set-up** (each row depends on the ones above):

| # | Step | Section | Milestone |
|---|---|---|---|
| 1 | Firebase project, database, sign-in, owner + device users, device key | §2 | M5 |
| 2 | GitHub private repo, Secrets and Variables (with `FIREBASE_MAILER_UID=none`) | §4 | M5 |
| 3 | Database rules deployed and refusal-tested | §3 | M5 |
| 4 | Logger portal: device key, DB URL, device user | §10, [05](05-firmware-guide.md) §5 | M5 |
| 5 | Cloudflare account, permanent subdomain, token; first deploy; header checks; browser key restricted | §5 | M6 |
| 6 | iPhone install (only after step 5's checks) | §7 | M6 |
| 7 | ntfy, Healthchecks, optional Telegram → logger portal | §8 | M7 |
| 8 | E-mail: sender Gmail, mailer user, rules with `MAILER_UID`, Apps Script, recipients, rules in the app | §9 | M7 |

> ⚠️ **E-mail deploy order: database rules → mailer → PWA → firmware** ([r8](../research/r8-email-alerts.md) §7). Anything that writes `alerts/*`, `cfg/alertsRev`, `mailq` or the new event fields before the current rules are live is refused (401), and refused requests still count against the download quota.

---

## 2. Firebase project

Use the Google account that will own the project. Record every value in site-survey §18 (the IDs) and in a password manager (the passwords).

### 2.1 Create the project (Spark)

1. Open <https://console.firebase.google.com> → **Create a project** (or *Add project*).
2. Name it, e.g. `savx-logger`. The **project ID** shown under the name is permanent and appears in URLs (`<project-id>-default-rtdb…`, `<project-id>.web.app`). Keep it neutral: no address, phone or family names.
3. Google Analytics: not needed; you can switch it off.
4. **Create project**. New projects are on the **Spark** (no-cost) plan; never add a billing account ([v2](../research/v2-platform-verification.md) F1–F8).

### 2.2 Realtime Database in Singapore

1. **Build → Realtime Database → Create Database**.
2. Location: **Singapore (asia-southeast1)**, the only Asian RTDB region ([v2](../research/v2-platform-verification.md) F5).
3. Security rules: **Start in locked mode** → **Enable**.
4. Copy the URL from the top of the **Data** tab: `https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app`. The dashboard's Content-Security-Policy only allows `*.asia-southeast1.firebasedatabase.app`, and `deploy-web` refuses other regions.

### 2.3 Email/password sign-in

1. **Build → Authentication → Get started → Sign-in method**.
2. **Email/Password → Enable** (leave *Email link (passwordless sign-in)* off) → **Save**.
3. Do **not** enable App Check or reCAPTCHA protection for email/password: they would reject the ESP32's plain REST sign-in ([r7](../research/r7-web-hosting.md) §6.3).
4. **Authorized domains: no change.** Email/password sign-in does not use that list; do not add `*.workers.dev` ([r7](../research/r7-web-hosting.md) §6.1).

### 2.4 The users

**Authentication → Users → Add user**, one per row. Generate passwords on the Mac with `openssl rand -base64 24`.

| User | E-mail (examples) | Password | Used in |
|---|---|---|---|
| owner | your own address | strong, you type it on the iPhone | PWA sign-in |
| device | `inv1@<anything-you-own>` | 24+ random characters | logger portal only |
| mailer (create in §9.1) | `mailer@<anything>.invalid` | 24+ random characters | Apps Script properties only |

Copy the **User UID** column of each user. These are `OWNER_UID`, `DEVICE_UID` and `MAILER_UID` for the rules (§3). Anyone can create an account with the public web key, so access is decided **only** by these pinned UIDs.

### 2.5 Register the web app (dashboard config)

1. **Project settings** (gear icon) → **General** → *Your apps* → **Web** (`</>`).
2. Nickname `savx-pwa`. Do **not** tick "Also set up Firebase Hosting" (Cloudflare is the host). **Register app**.
3. Copy from the config shown: `apiKey` (this is the **browser key**), `authDomain` (`<project-id>.firebaseapp.com`), `projectId`, `appId` (`1:…:web:…`). Take `databaseURL` from §2.2 if it is not shown.

### 2.6 API keys: create the device key now, restrict the browser key later

**Device key** (do this now):

1. Open the Google Cloud console for the same project: <https://console.cloud.google.com/apis/credentials> (project selector at the top = your Firebase project).
2. **Create credentials → API key**. Open the new key (**Edit API key**):
   - Name: `savx-device`.
   - **Application restrictions: None** (the ESP32 and Apps Script send no `Referer`).
   - **API restrictions: Restrict key** → tick **Identity Toolkit API** and **Token Service API** only → **Save**.
3. Copy the key. It goes into the logger portal (§10), the mailer's `FB_API_KEY` (§9.3), `web/.env.local` for local development (§6) and the `curl` tests (§3.4).

**Browser key** (the web `apiKey` from §2.5): leave it as it is until the first web deploy works, then restrict it (§5.6).

Firebase API keys restricted to Firebase APIs "do not need to be treated as secrets"; the restriction is abuse friction, the rules are the security ([r7](../research/r7-web-hosting.md) §6.2).

### 2.7 Where each value goes

| Value | Logger portal ([05](05-firmware-guide.md) §5.3) | GitHub Variable (§4.3) | `web/.env` / `.env.local` (§6) | Mailer property (§9.3) |
|---|---|---|---|---|
| Project ID | – | `FIREBASE_PROJECT_ID`, `VITE_FIREBASE_PROJECT_ID` | `VITE_FIREBASE_PROJECT_ID` | – |
| Database URL | Firebase RTDB URL | `VITE_FIREBASE_DATABASE_URL` | `VITE_FIREBASE_DATABASE_URL` | `FB_DB_URL` |
| Browser key | – | `VITE_FIREBASE_API_KEY` | `.env` (production builds) | – |
| Device key | Firebase Web API key | – | `.env.local` (dev) | `FB_API_KEY` |
| Auth domain, app ID | – | `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_APP_ID` | same names | – |
| Device id `inv1` | (fixed in firmware) | `VITE_DEVICE_ID` | `VITE_DEVICE_ID` | `DEVICE` (default) |
| Owner / device / mailer UID | – | `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`, `FIREBASE_MAILER_UID` | – | – |
| Device user e-mail + password | device user fields | – | – | – |
| Mailer user e-mail + password | – | – | – | `FB_EMAIL`, `FB_PASSWORD` |

---

## 3. Database rules: render, deploy, test

The rules file is [`firebase/database.rules.json`](../firebase/database.rules.json). Its access matrix, validations and the reasons are in [firebase/README.md](../firebase/README.md); the data model is [04](04-architecture-and-data-model.md).

> ⚠️ The committed file keeps three placeholders, **`OWNER_UID`, `DEVICE_UID`, `MAILER_UID`**, so as committed nobody can read or write anything. Only a rendered copy in the git-ignored `.deploy/` folder is ever deployed. Never edit real UIDs into the committed file, and never "fix" access with a bare `auth != null` (CI fails on both).

### 3.1 Render

`tools/render-rules.sh` checks that the UIDs are letters/digits only and all different, substitutes them into `.deploy/database.rules.json`, validates the JSON and writes `.deploy/firebase.json`. Use `MAILER_UID=none` until the mailer user exists (its grants then match no account).

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
OWNER_UID=PASTE_OWNER_UID DEVICE_UID=PASTE_DEVICE_UID MAILER_UID=none sh tools/render-rules.sh
# render-rules: wrote .deploy/database.rules.json and .deploy/firebase.json
```

The root `firebase.json` intentionally has **no `database` block**, so a stray `firebase deploy` from the repository root can never upload the placeholder rules.

### 3.2 Deploy from GitHub (normal path)

Once the repository Variables and the Google credential are set (§4.3–§4.4), the **`deploy-rules`** workflow renders with `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`, `FIREBASE_MAILER_UID`, then:

- on a pull request that touches the rules: `firebase deploy --only database --dry-run` against the real project;
- on a push to `main` (or **Actions → deploy-rules → Run workflow**): the real deploy.

The job summary ends with `RTDB rules: deploy OK for <project> at <sha>`.

### 3.3 Deploy from the Mac (you run)

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/.deploy          # after §3.1
npx --yes firebase-tools@15.32.1 login                    # once; opens the browser
npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id> --dry-run
npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id>
```

firebase-tools 15.32.1 runs on the Mac's Node 20.17. Alternative: paste `.deploy/database.rules.json` into **Realtime Database → Rules** in the console and **Publish**; the console's **Rules Playground** can simulate a read or write as a given UID.

### 3.4 Refusal tests with `curl` (after every rules change)

[firebase/README.md](../firebase/README.md) §5 has the full set (35 tests). Set-up, using the **device key** (the browser key refuses requests without the app's `Referer` once restricted):

```zsh
API_KEY=AIza...                 # device key
DB=https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app
signin() {  # usage: signin EMAIL  -> prints an ID token (1 h)
  read -rs "PW?Password for $1: "; echo >&2
  curl -s "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=$API_KEY" \
    -H 'Content-Type: application/json' \
    -d "{\"email\":\"$1\",\"password\":\"$PW\",\"returnSecureToken\":true}" |
    python3 -c 'import sys,json; print(json.load(sys.stdin)["idToken"])'
}
code() { curl -s -o /dev/null -w '%{http_code}\n' "$@"; }
OWNER_TOKEN=$(signin you@example.com)
DEVICE_TOKEN=$(signin inv1@example.com)
NOW=$(date +%s)
```

The four that matter most (all from §5.2 of the README):

| # | Test | Command | Expect |
|---|---|---|---|
| 1 | Unauthenticated read | `code "$DB/devices/inv1/live.json"` | 401 |
| 5 | Owner writes `live` | `code -X PUT -d "{\"ts\":$NOW,\"lk\":1}" "$DB/devices/inv1/live.json?auth=$OWNER_TOKEN"` | 401 |
| 6 | Device writes `cfg` | `code -X PATCH -d '{"liveSec":10}' "$DB/devices/inv1/cfg.json?auth=$DEVICE_TOKEN"` | 401 |
| 7 | Device writes `live` (valid) | `code -X PUT -d "{\"ts\":$NOW,\"lk\":0}" "$DB/devices/inv1/live.json?auth=$DEVICE_TOKEN&print=silent"` | 204 |

If test 3 of the README (owner reads `live`) fails, the owner UID is wrong; if test 7 fails, the device UID is wrong or the token expired. A refused request returns `401` with `{"error" : "Permission denied"}`.

### 3.5 Full emulator suite (before changing the rules)

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
(cd web && npm ci)                 # the parity fuzz needs web/node_modules
sh tools/rules-test/run.sh         # expect "87 passed, 0 failed" and "parity: … agree, 0 disagree"
```

It needs Java 11+ and downloads the emulator JAR once to `~/.cache/firebase/emulators`; no project, login or deploy. A parity disagreement means the PWA's validation (`web/src/data/alerts.ts`) and the rules drifted apart: fix one before deploying.

---

## 4. GitHub repository and Actions

### 4.1 Create the private repository

1. On <https://github.com>, turn on **two-factor authentication** for your account.
2. **New repository** under your **personal** account (not an organisation), visibility **Private**, no README/licence/.gitignore (the project has them).
3. Settings → Actions → General → **Workflow permissions: Read repository contents** (the default; the workflows ask only for what they need).

### 4.2 First push (you run)

The project folder is not a git repository yet. Before the first commit, check that nothing secret is staged:

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
git init -b main
git add .
git diff --cached --name-only | grep -E '(^|/)\.env(\.local)?$|(^|/)\.firebaserc$|^\.deploy/|secrets\.h$|unpacked_fs/' \
  && echo "STOP: unstage the files above (git rm --cached <file>)" || echo "OK: nothing secret staged"
git commit -m "Initial import: Savx F4 datalogger"
git remote add origin https://github.com/<you>/<repo>.git
git push -u origin main
```

`.gitignore` already excludes `web/.env*` (except `.env.example`), `.firebaserc`, `.deploy/`, `.wrangler/`, build output, `node_modules/`, `firmware/include/secrets.h` and `*.log`. It does **not** exclude `firmware/unpacked_fs/` (created by `-t download_fs`, [05](05-firmware-guide.md) §9.3): delete that folder before committing.

> Set the Secrets and Variables (§4.3) **before** the first push, or the three workflows start at once and `deploy-web` / `deploy-rules` fail with "repository variable … is not set". A failed run is harmless; re-run it from the Actions tab after adding the values.

### 4.3 Secrets and Variables

**Settings → Secrets and variables → Actions**: secrets in the **Secrets** tab, everything else in the **Variables** tab (variables are not masked in logs, which helps debugging; none of them is secret because the `VITE_*` values ship inside the PWA anyway).

| Name | Kind | Value | Used by |
|---|---|---|---|
| `CLOUDFLARE_API_TOKEN` | **Secret** | §5.3 | deploy-web |
| `CLOUDFLARE_ACCOUNT_ID` | **Secret** | §5.3 | deploy-web |
| `GCP_SA_KEY` | **Secret, fallback only** | §4.4 option B | deploy-rules |
| `VITE_FIREBASE_API_KEY` | Variable | the **browser** key (§2.5) | deploy-web |
| `VITE_FIREBASE_AUTH_DOMAIN` | Variable | `<project-id>.firebaseapp.com` | deploy-web |
| `VITE_FIREBASE_PROJECT_ID` | Variable | `<project-id>` | deploy-web |
| `VITE_FIREBASE_APP_ID` | Variable | `1:…:web:…` | deploy-web |
| `VITE_FIREBASE_DATABASE_URL` | Variable | the asia-southeast1 URL (other regions are rejected) | deploy-web |
| `VITE_DEVICE_ID` | Variable | `inv1` | deploy-web |
| `FIREBASE_PROJECT_ID` | Variable | `<project-id>` | deploy-rules |
| `FIREBASE_OWNER_UID` | Variable | owner UID | deploy-rules |
| `FIREBASE_DEVICE_UID` | Variable | device UID | deploy-rules |
| `FIREBASE_MAILER_UID` | Variable | `none` until §9.1, then the mailer UID | deploy-rules |
| `GCP_WIF_PROVIDER` | Variable | printed by §4.4 option A | deploy-rules |
| `GCP_RULES_SA` | Variable | printed by §4.4 option A | deploy-rules |
| `CF_PREVIEWS` | Variable, optional | leave **unset** (§5.8) | deploy-web |

### 4.4 Google credential for `deploy-rules`

**Option A (preferred): keyless Workload Identity Federation**, set up once in **Cloud Shell** (the `>_` icon in the Google Cloud console; free, nothing installed on the Mac). Run the block of [r7](../research/r7-web-hosting.md) §4.6.3 with your project ID and `REPO=<github-user>/<repo>`; it creates the service account `gha-rtdb-rules` with the role *Firebase Realtime Database Admin*, a workload-identity pool `github` with provider `repo` limited to your repository, and prints the two values for `GCP_WIF_PROVIDER` and `GCP_RULES_SA`. Whether WIF works on a Spark project without billing is **unverified**: if any step asks for billing, use option B.

**Option B (fallback): service-account key** ([r7](../research/r7-web-hosting.md) §4.6.4): Cloud console → IAM & Admin → Service Accounts → create `gha-rtdb-rules`, role **Firebase Realtime Database Admin** → Keys → Add key → JSON → paste the whole JSON as the secret `GCP_SA_KEY` and delete the file. In `.github/workflows/deploy-rules.yml` replace the `workload_identity_provider` and `service_account` lines with `credentials_json: ${{ secrets.GCP_SA_KEY }}` (the file has the comment).

> ⚠️ That key bypasses the rules and has full database access. Rotate it yearly, or move to WIF later.

### 4.5 What the workflows do

| Workflow | Runs on | Steps |
|---|---|---|
| [`ci.yml`](../.github/workflows/ci.yml) | every PR and push to `main` (not for `*.md`, `docs/`, `research/`, `reference/`) | **checks**: `gen_codes.py --check`, rules lint (valid JSON, placeholders kept, no open rule, no `database` block in `firebase.json`, test render); **web** (if `web/`, `shared/` or the generator changed): `npm ci`, typecheck, build, dist checks; **firmware** (if `firmware/` or `shared/` changed): PlatformIO 6.2.0, `pio run -e savx`, `pio test -e native`; **mailer** (if `tools/apps-script/` or `shared/` changed): Node 24, `npm run check`, `npm test` |
| [`deploy-web.yml`](../.github/workflows/deploy-web.yml) | push to `main` touching `web/**` (not `*.md`), `shared/**`, `tools/gen_codes.py`; manual | requires the six `VITE_*` variables, repeats codes check + typecheck + build + dist check, then `npx wrangler@4.146.0 deploy --tag <sha> --message "main@<sha> run <id>"`; the job summary shows the production URL |
| [`deploy-rules.yml`](../.github/workflows/deploy-rules.yml) | rules / render-script changes; manual | render with the UID variables → Google auth → dry run (PR) or deploy (`main`) |
| [`dependabot.yml`](../.github/dependabot.yml) | monthly | update PRs for the pinned actions and `web/` npm packages (Vite majors excluded: Node 20.17 on the Mac) |

GitHub Free has no branch protection on private repositories, so a red `ci` does not block a merge; each deploy workflow repeats its own checks before deploying. Expected Actions use: about 350 of the free 2,000 minutes a month ([r7](../research/r7-web-hosting.md) §4.7); a cold PlatformIO cache adds 5–10 minutes. If the minutes run out, the Mac path (§3.3, §5.5) still works.

---

## 5. Hosting the dashboard on Cloudflare

Primary host: **Cloudflare Workers static assets**, an assets-only Worker named `savx-logger` (no script, so requests are never metered) at **`https://solar.<acct>.workers.dev`**. Fallback: Firebase Hosting, deployed by hand only (§5.9). Why: [r7](../research/r7-web-hosting.md) §1 (Dhaka PoP, no lockout cliff, no commercial-use clause, previews under your own subdomain; Vercel Hobby was the runner-up).

> ⚠️ **The origin is permanent.** `savx-logger` (in [`web/wrangler.jsonc`](../web/wrangler.jsonc)) plus your account subdomain `<acct>` *are* the app's identity on the phone: the Home Screen icon, the sign-in, the service worker and the history cache. Renaming either, adding a custom domain or switching hosts means re-installing the app on every phone. **Do not tap "Add to Home Screen" until §5.5 passes.**

### 5.1 The files

| File | Does |
|---|---|
| [`web/wrangler.jsonc`](../web/wrangler.jsonc) | Worker `savx-logger` (never rename), serves `./dist`, SPA fallback (`not_found_handling: single-page-application`), `workers_dev: true`, `preview_urls: false` |
| [`web/public/_headers`](../web/public/_headers) | CSP (self + Identity Toolkit + Secure Token + `*.asia-southeast1.firebasedatabase.app` incl. `wss`), HSTS, `nosniff`, `DENY` framing, COOP, `X-Robots-Tag: noindex`; `immutable` for `/assets/*`, `no-cache` for `/`, `/index.html`, `/sw.js`, the manifest |
| `web/public/.assetsignore` | Keeps `.DS_Store` off the CDN |
| [`firebase.json`](../firebase.json) | Fallback hosting only, same headers, no `database` block |

### 5.2 Cloudflare account and the permanent subdomain

1. Sign up at <https://dash.cloudflare.com/sign-up> (free plan), verify the e-mail, turn on **2FA**.
2. **Workers & Pages** → set your **account subdomain** `<acct>` (shown as *Your subdomain*; change it there only before the first install). Short and neutral, e.g. `jr-solar` → `https://solar.<your-subdomain>.workers.dev`. No address, phone or family names: the host name is public.
3. Write the final origin into site-survey before the first deploy.

### 5.3 API token and account ID

1. Cloudflare dashboard → API tokens → **Create Token** → template **Edit Cloudflare Workers** ([Cloudflare: GitHub Actions](https://developers.cloudflare.com/workers/ci-cd/external-cicd/github-actions/)).
2. **Account Resources**: only your one account. If Zone Resources is required, choose all zones of that account (it has none).
3. Optional: an end date about 12 months out, plus a calendar reminder (an expired token makes `deploy-web` fail at the deploy step).
4. **Create**, copy the token once → GitHub secret `CLOUDFLARE_API_TOKEN`.
5. Copy the 32-hex **account ID** (account menu → *Copy account ID*, [find account IDs](https://developers.cloudflare.com/fundamentals/account/find-account-and-zone-ids/)) → secret `CLOUDFLARE_ACCOUNT_ID`.

> ⚠️ This token can replace the code at your origin, and code there runs inside your signed-in owner session. Keep it only in GitHub Secrets, keep 2FA on both accounts, and revoke and recreate it if it is ever exposed.

### 5.4 First deploy

**Through CI (normal):** with the Secrets and Variables set, a push to `main` that touches `web/` runs `deploy-web`. Open **Actions → deploy-web → the run → Summary**: it shows `Production: https://solar.<acct>.workers.dev (commit …)`. The URL must match §5.2 **exactly**. You can also start it with **Run workflow**.

**From the Mac (you run).** wrangler 4.146.0 needs Node ≥ 22; the Mac's default is 20.17, nvm has 24.21.0 ([r7](../research/r7-web-hosting.md) §5.1):

```zsh
source ~/.nvm/nvm.sh && nvm use 24.21.0            # this terminal only
cd ~/Office/CodeBase/Personal/Datalogger/web
cp -n .env.example .env                              # once; fill in the production values with the BROWSER key
npm ci && npm run typecheck && npm run build
npx --yes wrangler@4.146.0 login                     # once; opens the browser
npx --yes wrangler@4.146.0 deploy --dry-run
npx --yes wrangler@4.146.0 deploy --tag "mac-$(date +%Y%m%d%H%M)" --message "manual deploy from Mac"
npx --yes wrangler@4.146.0 deployments list
```

On plain Node 20.17 only as an emergency: `npx --yes wrangler@4.86.0 deploy` (the last release that runs on Node 20). Local test with the production headers: `npx --yes wrangler@4.146.0 dev` (port 8787; `npm run preview` on port 4173 ignores `_headers`).

### 5.5 Checks before any phone installs it

1. Headers on the real edge (Mac Terminal):
   ```zsh
   O=https://solar.<acct>.workers.dev
   curl -sI "$O/"                     | grep -iE '^HTTP|cache-control|content-security-policy|strict-transport|x-robots'
   curl -sI "$O/index.html"           | grep -iE '^HTTP|location'             # 307, location: /
   curl -sI "$O/sw.js"                | grep -iE '^HTTP|cache-control'        # 200, no-cache
   curl -sI "$O/manifest.webmanifest" | grep -iE 'content-type|cache-control' # application/manifest+json, no-cache
   A=$(curl -s "$O/" | grep -oE '/assets/index-[A-Za-z0-9_-]+\.js' | head -n 1)
   curl -sI "$O$A"                    | grep -i cache-control                 # public, max-age=31536000, immutable
   curl -s  "$O/cdn-cgi/trace"        | grep -E '^colo='                      # DAC or CGP is ideal
   ```
2. **Mac Safari** with the Web Inspector (Develop menu): no CSP violations in the Console; the Network tab shows a `wss://…asia-southeast1.firebasedatabase.app/.ws` connection; sign in as the owner and the Live page updates. Before the logger is online, `?demo=1` must work.
3. Restrict the browser key (§5.6), then sign in again in a private window.

Only then install on the iPhone (§7). Afterwards do one **update drill** (change a visible string, merge, wait for `deploy-web`, open the app: "A new version of the app is ready → Reload") and one **rollback drill** (§5.7) ([r7](../research/r7-web-hosting.md) §7.1 steps 12–13).

### 5.6 Restrict the browser key

Google Cloud console → APIs & Services → Credentials → the **Browser key (auto created by Firebase)** → **Application restrictions: Websites**, add:

1. `solar.<acct>.workers.dev/*`
2. `<project-id>.web.app/*` (fallback host)
3. `<project-id>.firebaseapp.com/*` (fallback alias and Firebase's own e-mail action pages)
4. only if previews are on: `*.<acct>.workers.dev/*`

Keep the API restrictions Firebase set. Do not add `localhost` or LAN IPs (local development uses the device key, §6). Save, wait a few minutes, sign in again on the Mac and the iPhone; a referrer or API-key error means the list is wrong. Keep `Referrer-Policy: strict-origin-when-cross-origin` in `_headers` (a `no-referrer` policy would break the restriction).

### 5.7 Rollback

```zsh
source ~/.nvm/nvm.sh && nvm use 24.21.0
cd ~/Office/CodeBase/Personal/Datalogger/web
npx --yes wrangler@4.146.0 deployments list
npx --yes wrangler@4.146.0 versions list
npx --yes wrangler@4.146.0 rollback <VERSION_ID> --message "rollback: <why>"
```

Always give an explicit `VERSION_ID` (without one wrangler picks "the version uploaded before the latest"). Dashboard alternative: Workers & Pages → `savx-logger` → Deployments → ⋯ → **Rollback**. Then `git revert` the bad commit on `main` so production matches git again. Phones pick up the restored version on the next foreground ("Reload"). Bad rules: `git revert` the rules commit (`deploy-rules` redeploys), or render the last good file and deploy from the Mac (§3.3).

### 5.8 PR previews (off)

Cloudflare preview URLs are public and protecting them needs a card, so previews are off. To enable: set `"preview_urls": true` in `web/wrangler.jsonc`, the variable `CF_PREVIEWS=true`, and add `*.<acct>.workers.dev/*` to the browser key. Previews use the **production** database.

### 5.9 Fallback host: Firebase Hosting (only if Cloudflare is unusable for days)

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/web && npm run build && cd ..
cp -n .firebaserc.example .firebaserc        # then put your project id in it (git-ignored)
npx --yes firebase-tools@15.32.1 deploy --only hosting --project <project-id>
```

This serves `https://<project-id>.web.app`, a **new origin**: every phone adds the new icon, signs in, and deletes the old one. Short Cloudflare outages need no action: installed apps keep running from the service-worker cache with live Firebase data.

---

## 6. Local development and demo mode

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/web
npm install                # exact versions from package-lock.json (EBADENGINE warnings from Firebase 13 are harmless)
npm run dev                # http://localhost:5173 (and http://<Mac-LAN-IP>:5173 for the iPhone)
```

- **Demo mode:** open `http://localhost:5173/?demo=1` (or `<origin>/?demo=1` on the deployed site). Every page runs on a deterministic simulation (3.2 kW / 24 V inverter, ~2.6 kWp PV, 100 Ah LiFePO4, load-shedding outages, seven example rules with e-mail chips, a mailer heartbeat). Firebase is not downloaded, writes stay in memory, a **DEMO · fake data** pill is always visible; tap it to leave. Never size anything from these numbers.
- **Real data in development:** create `web/.env.local` from `.env.example` with the **device key** as `VITE_FIREBASE_API_KEY` (the browser key refuses `localhost` once restricted). `.env*` files are git-ignored; CI never reads them.
- Without any `.env`, the build still succeeds and the app shows "Firebase is not configured" with a link to demo mode.
- Checks before a commit: `python3 ../tools/gen_codes.py --check`, `npx tsc --noEmit`, `npm run build` (writes `web/dist/`; about 147 KB gzip on a cold load). `src/codes.ts` and `src/alertMetrics.ts` are generated from [`shared/`](../shared): regenerate with `npm run codes`, never edit them by hand.

More (bundle sizes, bandwidth rules, icons, layout): [web/README.md](../web/README.md).

---

## 7. iPhone: install and sign in

1. **Safari** → `https://solar.<acct>.workers.dev` (only after §5.5).
2. **Share → Add to Home Screen → Add**. (iOS 26 opens Home Screen sites as web apps by default; on iOS 16.4–18 the same Share menu applies.)
3. Open the app **from the new icon** and sign in with the **owner** e-mail and password. A Home Screen app has its own storage, separate from Safari: signing in inside Safari does not sign in the app. After sign-in the app asks for persistent storage; Home Screen apps are exempt from Safari's 7-day storage cap ([v2](../research/v2-platform-verification.md) I2).
4. Pages (bottom bar): **Live**, **Today**, **History**, **Energy**, **Alerts** (Rules / History / E-mail; `#/events` still opens it), **Settings**.
5. Offline test: Airplane Mode on, force-quit, reopen: the app shell must still load (from the service-worker cache), showing the last data it had; then Airplane Mode off.
6. **Settings** shows your **User UID** (for the rules), the logger and inverter details, **Send test alert**, **Re-probe inverter**, theme, **Clear history cache**, **Sign out**, and a link to the local page `http://solar.local` (with the logger's IP as fallback).

| Banner | Meaning |
|---|---|
| Red "No access … UID …" | The signed-in UID is not the deployed `OWNER_UID`: copy it into `FIREBASE_OWNER_UID` and run `deploy-rules` (§3.2) |
| "Logger offline" | `live.ts` older than 60 s: the logger has no power, Wi-Fi or internet |
| "Inverter link down" | Logger online but `lk = 0` (no valid inverter reply) |
| "A new version of the app is ready → Reload" | A deploy happened; tap it |

The app has no background work and no Web Push: it shows data only while open. Alerts reach the phone through ntfy, Telegram and e-mail (§8, §9).

---

## 8. Push channels: ntfy, Telegram, Healthchecks

These are sent **by the logger** and work without Firebase. Enter their values in the logger portal (§10).

### 8.1 ntfy (primary instant push)

1. Check the App Store: ntfy (iOS 15+) is in the US and IN storefronts but **Bangladesh is not an App Store storefront** ([v2](../research/v2-platform-verification.md) I8). Record what you find in site-survey §17. If you cannot install it, rely on e-mail (§9) and Healthchecks.
2. Make a random topic (the topic is the password): `openssl rand -hex 16` → 32 characters (the portal asks for ≥ 24).
3. ntfy app → **+** → topic name, server `ntfy.sh` → **Subscribe**.
4. Test from the Mac: `curl -d "test from Mac" ntfy.sh/<topic>`. The notification must arrive.
5. Logger portal → *ntfy topic* (or `POST /api/config` with `ntfyTopic`, [05](05-firmware-guide.md) §6.5). Optional: *Dashboard URL for notification links* = `https://solar.<acct>.workers.dev/#/alerts`.

Routing: severity 5 → priority 5, 4 → 4, 3 → 3, ≤ 2 → 2; recoveries at most priority 3; quiet hours suppress severity ≤ 3 (still logged); at most 40 pushes a day; ntfy.sh allows 250 messages a day ([d1](../research/d1-architecture.md) §7.1–7.3).

### 8.2 Telegram (optional, severity ≥ 4)

Telegram may be blocked by Bangladeshi ISPs ([v2](../research/v2-platform-verification.md) §3.4); ntfy and e-mail stay.

1. In Telegram, message **@BotFather** → `/newbot` → follow the prompts → copy the **token**.
2. Send any message to your new bot.
3. On the Mac, read the chat id (`read -rs` keeps the token out of the history):
   ```zsh
   read -rs "TG?Bot token: "; echo
   curl -s "https://api.telegram.org/bot$TG/getUpdates" | python3 -c 'import sys,json; [print(u["message"]["chat"]["id"]) for u in json.load(sys.stdin)["result"] if "message" in u]'
   unset TG
   ```
4. Logger portal → *Telegram bot token* and *Telegram chat id* (portal only).

### 8.3 Healthchecks.io (dead-man's switch)

1. Sign up at <https://healthchecks.io> (free Hobbyist plan, 20 checks), e-mail notifications on.
2. **Add Check**: name `savx-logger`, **Period 5 minutes**, **Grace 15–30 minutes** ([d1](../research/d1-architecture.md) §7.1). Copy the ping URL `https://hc-ping.com/<uuid>`.
3. Logger portal → *Healthchecks ping URL or UUID* (or `POST /api/config` with `hcUrl`).

The logger pings every 5 minutes, independent of Firebase, and pings `/fail` when the link is armed and running but no valid inverter reply came for 5 minutes. Healthchecks e-mails you when pings stop (no power, no Wi-Fi, no internet) and when they resume.

### 8.4 Test the push path

PWA **Settings → Send test alert** (or `test-alert` on the logger's console): within about a minute an ntfy push "Test alert from the dashboard" arrives (quiet hours are ignored for tests), and the event appears in **Alerts → History**.

---

## 9. E-mail alerts: mailer, recipients and usage-threshold rules

Firebase Spark has no Cloud Functions, so e-mail goes through a **Google Apps Script "mailer"** running in a dedicated Gmail account every 5 minutes ([r8](../research/r8-email-alerts.md) §1). The **logger** evaluates your rules (every 5 s, also offline) and queues a job (`mailq`) in the same request as the event; the **mailer** picks recipients, enforces the daily cap, de-duplicates, renders and sends, and writes `em:"sent"` back. The mailer alone sends "logger offline" mails and the daily summary. The step-by-step reference is [`tools/apps-script/README.md`](../tools/apps-script/README.md); below is the order and what is different.

```
 logger ──PATCH {events/<d>/<key>{…,em:"q"}, mailq/<key>}──► RTDB ◄──every 5 min── Apps Script (sender Gmail)
 PWA Alerts ──alerts/rules, alerts/email, cfg/alertsRev────► RTDB        │  GET mailq (null when idle)
 PWA "Send test e-mail" ──mailq/test──────────────────────► RTDB        └─► MailApp ──► your inbox
```

### 9.1 Sender account and mailer user

1. Create a new Gmail account used **only** for sending (e.g. `<you>.solar.alerts@gmail.com`), turn on 2-Step Verification. ⚠️ Sign in to it **at least once a year** (Google may delete accounts inactive for 2 years; script runs may not count).
2. Firebase console → Authentication → Users → **Add user** `mailer@<anything>.invalid` with `openssl rand -base64 24` as the password. Copy its **UID**.

### 9.2 Give the mailer access in the rules

- **GitHub:** set the Variable `FIREBASE_MAILER_UID` to the mailer UID → **Actions → deploy-rules → Run workflow** (on `main`).
- **Or Mac:** `OWNER_UID=… DEVICE_UID=… MAILER_UID=<mailer uid> sh tools/render-rules.sh`, then §3.3.

The mailer may then read `mailq`, `alerts`, `live`, `daily`, `events`, `mailer` and write only `mailq`, `mailer` and the `em`/`emt` fields of existing events; it cannot read `cfg`, `status`, `meta` or `hist` ([firebase/README.md](../firebase/README.md) §1).

### 9.3 Create the Apps Script project (signed in as the sender)

Follow [`tools/apps-script/README.md`](../tools/apps-script/README.md) §6:

1. <https://script.google.com> → **New project** → rename `Savx mailer`. **Project Settings**: tick *Show "appsscript.json" manifest file in editor*; keep the default Google Cloud project.
2. Paste [`appsscript.json`](../tools/apps-script/appsscript.json), [`Code.gs`](../tools/apps-script/Code.gs) (replacing `myFunction`) and a new script file `Lib` with [`Lib.gs`](../tools/apps-script/Lib.gs). Save.
3. **Script properties** (Project Settings):

   | Property | Value |
   |---|---|
   | `FB_API_KEY` | the **device key** (§2.6), never the browser key |
   | `FB_DB_URL` | `https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app` (no path) |
   | `FB_EMAIL`, `FB_PASSWORD` | the mailer user |
   | `ALLOW_TO` | the only addresses that may ever receive mail, comma-separated |
   | `APP_URL` (optional) | `https://solar.<acct>.workers.dev/` (adds an "Open dashboard" button) |
   | `SUBJECT_TAG`, `MAX_PER_DAY`, `DEVICE`, `NUDGE_KEY` (optional) | defaults `Solar`, `40`, `inv1`, unset |

4. Run **`sendSelfTest`** → authorise with the sender account (Advanced → *Go to Savx mailer (unsafe)* is normal for your own script) → allow the three permissions → `[Solar] Mailer self-test` arrives.
5. Run **`setup`**. The log must end with `OK: signed in to RTDB, trigger "poll" installed (every 5 min), …`. `RTDB GET /mailq -> HTTP 401` means §9.2 is not live yet; `Firebase sign-in failed: HTTP 403` means the browser key was used.
6. **Triggers** → the `poll` trigger → *Failure notification settings: Notify me immediately*.

### 9.4 Recipients and e-mail settings in the app

PWA → **Alerts → E-mail**:

1. **Send alert e-mails**: on (master switch for rule, built-in, summary and offline mails; test e-mails always work).
2. **Recipients** (max 3): type an address → **Add**. Every address must **also** be in the mailer's `ALLOW_TO`, or it is skipped (the card shows the mailer's warning). Position = bit of a rule's recipient mask; removing an address remaps the rules that used it.
3. **Sender name**, **Daily limit** (1–40 recipient-deliveries; the last 5 are kept for Alert/Critical), **Billing month starts on day** (1–28, for "This billing month" rules and the summary).
4. **Built-in alerts by e-mail**: Off / Warning+ / Alert+ / Critical; **Recovery e-mails**.
5. **Daily summary e-mail** and its hour (default 07:00, yesterday's totals); **Logger offline e-mail** after N minutes (10–240; raise it if load-shedding makes it noisy) and **"Back online" e-mail**.
6. **Save** → "Saved. The logger and the mailer pick it up within a few minutes."
7. **Mailer** card → **Send test e-mail**. Within about 5 minutes the mail arrives and the card shows the result. The card turns red with **"Mailer not running"** when its heartbeat is older than 15 minutes.

### 9.5 Usage-threshold rules

PWA → **Alerts → Rules → + New rule** → pick a preset (or **Custom rule**) → adjust → **Save** ("Saved "…" as r01. The logger picks it up within 60 s."). Within about a minute the banner reads **"✓ Logger has your latest rules (synced hh:mm)"**.

| Preset | Condition (as shipped) |
|---|---|
| Daily grid usage > 5 kWh | today's grid energy (estimate) > 5 kWh, once a day |
| Load > 2500 W for 5 min | load power, recovers below 2200 W |
| SOC < 30 % | battery SOC for 60 s, recovers at 35 % |
| PV today < 3 kWh by 17:00 | checked once at 17:00, only with ≥ 80 % data coverage |
| Inverter temp > 70 °C | for 2 min, recovers at 65 °C |
| Monthly grid usage > 150 kWh | billing month from the E-mail tab's start day |
| Grid outage today > 3 h | total outage minutes today |
| On battery for 2 h | continuously on battery |
| Evening peak load > 1500 W | 18:00–23:00 only |

The editor's sections: **Name**, **Rule enabled**, **Condition** (metric from the catalogue in [`shared/alert-metrics.json`](../shared/alert-metrics.json): 16 instant values such as load W, SOC, battery V, grid V, and 6 energy totals for **Today** or **This billing month**; comparator and value; "Evaluate continuously, alert after it holds for …" or **Check once at** a local time for "below" energy rules; minimum data coverage), **Recovers when** (auto or custom level and hold), **Active hours** (all day, or from/to and days), **Notify** (E-mail, ntfy push), **Severity** (4–5 may use the reserved last 5 e-mails and also go to Telegram), **Recipients**, **Cooldown**, **Repeat while active**, **Max per day**, **Also e-mail when it recovers**, a **Preview**, and for saved rules **Send test e-mail for this rule** and **Delete rule**. Energy is typed in kWh (comma or dot), stored in Wh. Grid figures are estimates from the inverter, not your meter, and say "est." everywhere. Up to 16 rules.

Rule badges: idle / pending ("above for 3:40 of 5:00") / firing / recovering / fired / checked / invalid. **Alerts → History** shows each event with value vs limit and an e-mail chip (`q` queued, `sent` e-mailed hh:mm, `cool`, `rcap`, `cap`, `quota`, `off`, `norc`, `exp`, `fail`, `skip`; meanings in [`tools/apps-script/README.md`](../tools/apps-script/README.md) §10), filters, and **Acknowledge**.

### 9.6 End-to-end tests

| Test | How | Pass if |
|---|---|---|
| Mailer only | Alerts → E-mail → **Send test e-mail** | Mail within ~5 min, "Sent to N" ([r8](../research/r8-email-alerts.md) A3) |
| Whole chain from the logger | Settings → **Send test alert** (or `test-alert` on the console) | ntfy push within ~1 min **and** an e-mail within ~6 min; the event shows the e-mailed chip (A4) |
| One rule | Rule editor → **Send test e-mail for this rule** | A test mail with that rule's wording |
| Real rule | Preset "Daily grid usage" with a 0.1 kWh limit | Exactly one e-mail that day, none after a reboot (A5) |

The acceptance list A1–A14 is in [r8](../research/r8-email-alerts.md) §8.3; M7 in [07](07-milestones-and-test-plan.md) adds the push tests.

### 9.7 Gmail on the recipients' side, and the optional nudge

- Each recipient: Gmail filter `subject:"[Solar]"` → **Never send it to Spam**, **Always mark it as important**; on the iPhone use the **Gmail app** with notifications on (iOS Mail fetches on a schedule).
- **Nudge (optional, v1.1):** cuts the 0–5 min delay to seconds. Mailer README §9: set `NUDGE_KEY` (`openssl rand -hex 16`), **Deploy → New deployment → Web app** (Execute as Me, access Anyone), then enter `<exec URL>?k=<NUDGE_KEY>` in the logger portal's *Mailer nudge URL*. The firmware supports it (`src/net/MailNudge.cpp`, at most one per minute and 48 a day); it carries no data.
- Before changing `Code.gs`/`Lib.gs`: `cd tools/apps-script && npm test` (44 tests) and `npm run check`; then paste the new files (and make a new web-app version if you use the nudge).

---

## 10. Entering the secrets in the logger

Open the portal ([05](05-firmware-guide.md) §5.2: hold **BOOT 3 s**, or `portal` on the console), join `SavxLogger-XXXX`, **Configure WiFi**, leave SSID and Wi-Fi password empty to keep the Wi-Fi, fill only what you are setting, **Save**, then **Exit**. Secret fields show empty: blank keeps the stored value, `-` clears it.

| Portal field | Value from |
|---|---|
| Firebase Web API key | device key `savx-device` (§2.6) |
| Firebase RTDB URL | §2.2 |
| Firebase device user e-mail / password | §2.4 |
| ntfy topic | §8.1 |
| Telegram bot token / chat id | §8.2 (optional) |
| Healthchecks ping URL or UUID | §8.3 |
| Dashboard URL for notification links | `https://solar.<acct>.workers.dev/#/alerts` (optional) |
| Mailer nudge URL | §9.7 (optional) |

Check on the console (`cloud`) or at `http://solar.local/api/status`: `upState` `ready`, `auth` true, `notify` shows the channels on. In the Firebase console, `devices/inv1/live` changes about every 10 s; open only small nodes such as `live` and `status` (console reads are billed download). The PWA's **Settings → Logger** card shows the firmware, `Cloud OK … ago` and `Requests / TLS today`.

---

## 11. Watching the free quotas

| Where | What to look at | Normal |
|---|---|---|
| Firebase console → **Realtime Database → Usage** | Downloads, storage, connections | about 5–15 MB/day download, about 0.4 MB/day storage growth; far below 10 GB/month and 1 GB ([d1](../research/d1-architecture.md) §4.6, [firebase/README.md](../firebase/README.md) §7). Check daily in the first week |
| PWA → Settings → Logger: **Requests / TLS today** (`status.rq` / `hs`) | Device requests and TLS handshakes | Guard at 1,500 handshakes or 20,000 requests a day → `QUOTA_GUARD` event, `live` slowed; twice that → `live` paused ([05](05-firmware-guide.md) §11) |
| Alerts → History | `QUOTA_GUARD`, `BACKLOG_STUCK`, `DATA_DROPPED` | none |
| Alerts → E-mail → Mailer card | Sent today, Google quota left | ≤ 40 a day; Google allows 100 recipients a day |
| script.google.com (sender) → **Executions** | Run time per run | about 1–2 s idle; the trigger budget is 90 min a day |
| Cloudflare → Workers & Pages → `savx-logger` | Requests | Static-asset requests are free and unlimited |
| GitHub → Settings → Billing → Actions | Minutes | about 350 of 2,000 a month |

⚠️ Keep the Firebase console's data viewer **closed on `hist`** and never leave a browser tab open on a big node: console reads and rule-denied requests count as billed download. Over the Spark quota, the database is shut off for the rest of the month (the logger keeps logging to flash and back-fills later).

---

## 12. Data export

Retention in the cloud: the logger deletes `hist/<date>` (minute data) older than **`keepHistDays`** (default 400) and `events/<date>` older than **`keepEventDays`** (default 730) after 00:05 each day; `daily` and `h15` are kept. Both values are in the PWA's **Settings → Alerts and logging**. Export at least once a year.

1. **In the PWA (CSV):** **Today** → *Export CSV* (today's minutes); **History** → *Export CSV* (a day's minutes); **Energy** → *CSV* (a month or a year of daily totals) and the minute export for one day. On the iPhone the share sheet opens: choose **Save to Files**.
2. **From the logger (LAN, no quota):** `curl -s "http://solar.local/api/hist?date=2026-10-21" > hist-2026-10-21.json` for days still in its flash (uploaded days stay 14 days; `GET /api/days` lists them).
3. **REST export as the owner (Mac).** No script exists yet; this is the manual equivalent. Sign in as in §3.4 (device key, owner account), then:
   ```zsh
   mkdir -p ~/savx-export && cd ~/savx-export
   curl -s "$DB/devices/inv1/daily.json?auth=$OWNER_TOKEN" > daily.json
   curl -s "$DB/devices/inv1/h15.json?auth=$OWNER_TOKEN" > h15.json
   for d in $(python3 -c 'import datetime as t; s=t.date(2026,10,1); print(" ".join(str(s+t.timedelta(i)) for i in range(31) if (s+t.timedelta(i)).month==10))'); do
     curl -s "$DB/devices/inv1/hist/$d.json?auth=$OWNER_TOKEN" > "hist-$d.json"
   done
   ```
   Each `hist` day is roughly 100–150 KB; a full year is about 140 MB of download, about 1.4 % of a month's quota ([d1](../research/d1-architecture.md) §4.5). ID tokens expire after 1 hour: run `signin` again for long exports. Never request `devices/inv1.json` or `hist.json` as a whole.

---

## 13. Limits and cost summary

| Service | Plan | Limits that matter | Use here | Cost |
|---|---|---|---|---|
| Firebase RTDB | Spark | 1 GB stored, 10 GB/month download, 100 simultaneous connections; hard shut-off for the month when exceeded | ~5–15 MB/day, ~150 MB/year growth | $0 |
| Firebase Auth | Spark | 100 sign-ups/hour/IP | 3 users | $0 |
| Firebase Hosting (fallback only) | Spark | 10 GB storage, 360 MB/day transfer | not deployed | $0 |
| Cloudflare Workers static assets | Free | static requests free and unlimited; scripts capped (none used) | the PWA (~147 KB per cold load) | $0 |
| GitHub | Free, private repo | 2,000 Actions minutes/month, 10 GB cache | ~350 min/month | $0 |
| Google Apps Script + Gmail | consumer | 100 recipients/day, 90 min/day trigger runtime, 20,000 URL fetches/day | ≤ 40 mails/day, ~5 % of fetches | $0 |
| ntfy.sh | free | 250 messages/day, 4 KB each | ≤ 40 pushes/day | $0 |
| Telegram Bot API | free | ~1 message/s per chat; may be blocked in BD | severity ≥ 4 only | $0 |
| Healthchecks.io | Hobbyist | 20 checks | 1 check | $0 |

Sources: [v2](../research/v2-platform-verification.md) §2.1, [r7](../research/r7-web-hosting.md) §1.1 and §4.7, [r8](../research/r8-email-alerts.md) §1.1, [d1](../research/d1-architecture.md) §4.6 and §7.1. Google says its Apps Script quotas may change "without notice".

---

## 14. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `deploy-web`: "repository variable … is not set" | A Variable missing | §4.3, then re-run |
| `deploy-web`: "must be the asia-southeast1 URL" | Database in another region | The CSP only allows asia-southeast1; recreate the database there (§2.2) |
| `deploy-web` fails at the deploy step | Token expired/revoked, wrong account ID | New token (§5.3) |
| `deploy-rules`: auth step fails | WIF variables wrong, or WIF not possible on Spark | Re-check §4.4 A, or use option B |
| `ci`: "placeholders missing" | Real UIDs committed into the rules | Restore the placeholders; UIDs belong in Variables only |
| PWA: "Firebase is not configured" | Built without `.env` / Variables | §4.3 or `web/.env` |
| PWA: red "No access … UID …" | Owner UID in the deployed rules differs | Copy the UID shown into `FIREBASE_OWNER_UID`, run `deploy-rules` |
| PWA sign-in fails after restricting the key | Websites list wrong | §5.6; changes take a few minutes |
| Signed out every time | Signed in inside Safari, not the installed app | Sign in from the Home Screen icon |
| CSP errors in Safari's Web Inspector | A new external call, or another database region | Update `web/public/_headers` **and** `firebase.json` together |
| Old version keeps showing | Service worker update pending | Open the app, tap "Reload"; last resort: delete the icon, re-add, sign in |
| Logger `upErr` `sign-in HTTP 403` | Browser key in the portal | Device key (§2.6, §10) |
| Logger `upErr` `GET cfg: HTTP 401` | Rules not deployed or wrong device UID | §3 |
| Alerts: "The database rejected this change" | Current rules not deployed (deploy order) | `deploy-rules` (§3.2) |
| Alerts: "Waiting for the logger to load the new rules" > 2 min | Logger offline, or old firmware ("does not report alert rules yet") | Check the logger; rules apply once it runs firmware 0.6.1 or later |
| Alerts: "The logger could not use the latest rules (…)" | `alerts.json` problem on the device (`status.ruleErr`) | Edit the rule named; the logger keeps the previous set |
| Mailer card red "Mailer not running" | Trigger missing or failing | script.google.com → Triggers/Executions → `showStatus`, `setup` |
| Event chip `norc` | Address not in both `to` and `ALLOW_TO` | Add it to both |
| Event chip stays `q` | Mailer not running | as above |
| No ntfy push | Topic typo, app not subscribed, ISP | `curl -d test ntfy.sh/<topic>` from the Mac; `cloud` on the console |
| Healthchecks says "down" while the logger is up | ISP blocks hc-ping.com, wrong UUID | `notify.hcCode` in `/api/status`; test `curl -fsS https://hc-ping.com/<uuid>` from the Mac |
| Download above 15 MB/day | App left open on Today, console viewer open, a retry loop | §11; check `status.rq`, `upErr` |

More: [web/README.md](../web/README.md) §10, [`tools/apps-script/README.md`](../tools/apps-script/README.md) §11, [firebase/README.md](../firebase/README.md), [08](08-risks-troubleshooting-maintenance.md).

---

## 15. Sources

- Repository: [`web/README.md`](../web/README.md), [`firebase/README.md`](../firebase/README.md), [`tools/apps-script/README.md`](../tools/apps-script/README.md), [`.github/workflows/`](../.github/workflows), [`tools/render-rules.sh`](../tools/render-rules.sh), [`web/wrangler.jsonc`](../web/wrangler.jsonc), [`web/public/_headers`](../web/public/_headers), [`firebase.json`](../firebase.json).
- Decisions: [r7 web hosting](../research/r7-web-hosting.md) (§0–§7; verification in [r7c](../research/r7c-hosting-verification.md)), [r8 e-mail alerts](../research/r8-email-alerts.md) (§1, §3, §5, §7, §8; verification in [r8c](../research/r8c-email-verification.md)), [d1](../research/d1-architecture.md) §4, §6, §7, [r6](../research/r6-cloud-web.md) §9, [v2](../research/v2-platform-verification.md) §2–§3.
- Firebase: [pricing](https://firebase.google.com/pricing), [RTDB locations](https://firebase.google.com/docs/database/locations), [RTDB limits](https://firebase.google.com/docs/database/usage/limits), [REST auth](https://firebase.google.com/docs/database/rest/auth), [security rules](https://firebase.google.com/docs/database/security/core-syntax), [Auth limits](https://firebase.google.com/docs/auth/limits).
- Cloudflare: [Workers static assets headers](https://developers.cloudflare.com/workers/static-assets/headers/), [GitHub Actions](https://developers.cloudflare.com/workers/ci-cd/external-cicd/github-actions/), [account IDs](https://developers.cloudflare.com/fundamentals/account/find-account-and-zone-ids/).
- GitHub Actions billing: <https://docs.github.com/en/billing/concepts/product-billing/github-actions>; Google Cloud Shell pricing: <https://cloud.google.com/shell/pricing>.
- Apps Script: [quotas](https://developers.google.com/apps-script/guides/services/quotas), MailApp, [web apps](https://developers.google.com/apps-script/guides/web).
- ntfy: <https://docs.ntfy.sh/publish/>; Telegram Bot API: <https://core.telegram.org/bots/api>; Healthchecks.io: <https://healthchecks.io/docs/>.
