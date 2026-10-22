# Hosting the static PWA off Firebase Hosting: integration specifics (checked 2026-10-21)

Scope: a Vite + TS + Preact PWA (`web/`) talking straight to Firebase Auth (email/password) and RTDB in `asia-southeast1`, used as an iPhone Home Screen app from Dhaka. Every config below was written against the actual repo:

- `web/vite.config.ts`, `web/sw.template.js`, `web/src/data/firebaseSource.ts`, `web/public/manifest.webmanifest`, `firebase.json`, `firebase/database.rules.json`, `firmware/platformio.ini`
- Firebase SDK 13.0.0 (`@firebase/auth` 1.13.7, `@firebase/database` 1.1.5) as installed in `web/node_modules`

Tags: **[CONFIRMED-primary]** means an official page fetched today, or SDK/CLI source code. **[OBSERVED]** means measured from this Mac today; Cloudflare's trace reported `colo=DAC`, so it is on a Bangladesh network. **[SECONDARY]** and **[INFERRED]** as usual.

---

## 0. Summary for the pipeline

**Recommended host: Cloudflare Workers with static assets.** It is an assets-only Worker on `savx-logger.<account-subdomain>.workers.dev`, deployed from GitHub Actions.

- **Vercel Hobby** is the second choice. A YAML is given for it too.
- **Firebase Hosting** stays as a zero-effort fallback (keep the `hosting` block).

Why Cloudflare wins on the integration points below:

1. **Free, with no request cap on static files.** Static-asset requests are "free and unlimited" [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/). Cloudflare's Pages page now says "Start new projects with Workers" (updated Aug 25, 2026) [CONFIRMED-primary](https://developers.cloudflare.com/pages/).
2. **Previews live under your own account subdomain.** That makes an API-key referrer wildcard safe (§1.3). Vercel, Netlify and Firebase preview URLs share a public parent domain, so they can't be wildcarded safely.
3. **Native `_headers` support** and an explicit SPA mode [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/headers/).
4. **Lowest latency from Dhaka** [OBSERVED, 5 samples per host, different sites on each platform]:

| Platform (sample site) | Edge seen | TLS done (ms) | TTFB (ms) |
|---|---|---|---|
| Cloudflare (developers.cloudflare.com) | `cf-ray …-DAC` (Dhaka) | 22–27 | 49–58 |
| Firebase Hosting (firebase-tools.web.app) | Fastly `cache-ccu…` (Kolkata) | 37–40 | 52–55 |
| GitHub Pages (pages.github.com) | Fastly CCU | 37–42 | 53–61 |
| Vercel (vercel.com, a dynamic site) | `x-vercel-id: bom1` (Mumbai) | 101–114 | 177–196 |
| Netlify (docs.netlify.com) | not exposed | 98–115 | 155–222 |

Latency only matters on a cold load. After that the service worker serves the shell.

**Rejected for this project:**

- **Netlify Free (credit plans, for accounts created on or after 2025-09-04):**
  - 300 credits/month, hard limit. A production deploy costs 15 credits, so about 20 deploys/month if nothing else uses credits. Bandwidth costs 20 credits/GB and requests 2 credits per 10k.
  - When credits run out, "all of your web projects … are paused".
  - Sources: [credit plans](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/), [how credits work](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/how-credits-work/) [CONFIRMED-primary]. CI-on-every-push is risky there.
- **GitHub Pages:**
  - On GitHub Free "the repository must be public" [CONFIRMED-primary](https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site).
  - No custom response headers are documented (no CSP header, no `frame-ancestors`). Project sites live under `/<repo>/`.

**Required changes no matter which host you pick** (details in later sections):

1. **Keep `initializeAuth(...)` without a popup/redirect resolver.** `firebaseSource.ts` already does this. Do not switch to `getAuth()` (§1.2).
2. **Use two API keys.** The browser key gets HTTP-referrer restrictions. A new device key for the ESP32 gets no application restriction (§1.3).
3. **Patch `web/vite.config.ts`** (§3.5):
   - Never precache `_headers`/`_redirects`.
   - Precache `/` but not `/index.html`.
4. **Do not enforce App Check, and do not turn on reCAPTCHA protection for email/password.** Either one blocks the ESP32 (§1.5).
5. **Fix the final origin before the first "Add to Home Screen"** (§6).

---

## 1. Firebase Auth and RTDB from a non-Firebase origin

### 1.1 Authorized domains: not needed for email/password

- **The SDK only checks them for popup/redirect.** In `@firebase/auth` 1.13.7, `_validateOrigin()` fetches `authorizedDomains` and fails with `auth/unauthorized-domain`. It is called only from `_openRedirect()` and the popup flow (`resolver._originValidation`). `signInWithEmailAndPassword` posts to `identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=…` and refresh goes to `securetoken.googleapis.com/v1/token`. Neither does an origin check. [CONFIRMED-primary: SDK source `dist/esm/index-LY22yodV.js`]
- **Firebase's own docs tie authorized domains to redirects:** "it uses this key to verify that a sign-in redirect URL is valid according to the list of authorized domains" [CONFIRMED-primary](https://firebase.google.com/docs/auth/faq-and-troubleshooting).
- **They are needed only for:**
  - signInWithRedirect / signInWithPopup (OAuth);
  - email-link sign-in;
  - any `ActionCodeSettings.url` continue URL ("you need to add the domain for the URL as an authorized domain") [CONFIRMED-primary](https://firebase.google.com/docs/auth/web/passing-state-in-email-actions).
- **localhost:** "In projects created after April 28, 2025, Firebase Authentication no longer includes `localhost` as an authorized domain by default", and Google "strongly discourages" keeping it in production [CONFIRMED-primary](https://firebase.google.com/docs/auth/faq-and-troubleshooting). Remove it if present.
- **Action:** you do not have to add `*.workers.dev` or `*.vercel.app`. Add the production origin only if you later use password-reset links with a continue URL.
- **Redirect sign-in caveat:** if Google sign-in is ever added off Firebase Hosting, redirect sign-in breaks under third-party-storage blocking. Firebase's options for non-Firebase hosts are popup, a reverse proxy of `/__/auth/`, self-hosted helper files, or signInWithCredential [CONFIRMED-primary](https://firebase.google.com/docs/auth/web/redirect-best-practices). Firebase Hosting is the only host that avoids this. It is irrelevant for email/password.

### 1.2 Keep `initializeAuth`; `getAuth()` would load a cross-site iframe on iPhone

- **What `getAuth()` does:** it calls `initializeAuth(app, { popupRedirectResolver: browserPopupRedirectResolver, … })`.
- **The resolver initialises eagerly on iPhone:** `get _shouldInitProactively() { return _isMobileBrowser() || _isSafari() || _isIOS(); }`. On iOS it immediately loads `https://apis.google.com/js/api.js` and `https://<authDomain>/__/auth/iframe` [CONFIRMED-primary: SDK source].
- **Off Firebase Hosting that means:** a cross-site iframe on every launch, two extra CSP holes (`script-src apis.google.com`, `frame-src <project>.firebaseapp.com`), and wasted bytes.
- **Current code is correct:** `firebaseSource.ts` uses `initializeAuth(app, { persistence: [indexedDBLocalPersistence, browserLocalPersistence] })`. In a test production build, `__/auth/iframe` does not appear in the bundle (the `apis.google.com` string remains but is never loaded). [OBSERVED: grep of `dist/assets` built from a scratch copy]

### 1.3 API key restrictions: use two keys

Facts:

- "API keys restricted to Firebase services do *not* need to be treated as secrets." Since May 2024 Firebase auto-restricts new keys to Firebase APIs [CONFIRMED-primary](https://firebase.google.com/docs/projects/api-keys), page updated 2026-10-20.
- The web app only calls `identitytoolkit.googleapis.com` and `securetoken.googleapis.com` with the key. RTDB authorises with the ID token, not the key. [CONFIRMED-primary: only those two googleapis hosts appear in the built `firebase-*.js`]
- "You can set only one type of client restrictions per API key." Referrer patterns are documented as `example.com/*` plus `*.example.com/*`. Wildcards inside a label are **not** documented [CONFIRMED-primary](https://docs.cloud.google.com/api-keys/docs/add-restrictions-api-keys).

**The ESP32 problem.** The device signs in over REST with the same Web API key (d1 §5.9; `docs/07` step 3). The ESP32 sends no `Referer`, so a referrer-restricted key would block it [INFERRED from the mechanism]. The fix is two keys.

| Key | Application restriction | API restriction | Used by |
|---|---|---|---|
| **Browser key** (Firebase's "Browser key (auto created by Firebase)", shown as the Web API key) | **Websites**: the production origin(s) below | Keep Firebase's auto-list, or narrow to Identity Toolkit API + Token Service API | `VITE_FIREBASE_API_KEY` |
| **Device key** (new: Cloud console → APIs & Services → Credentials → Create API key) | **None** (a home IP behind CGNAT can't be pinned) | Identity Toolkit API + Token Service API only | ESP32 NVS `cloud.apiKey`, and the `curl` tests in `firebase/README.md` §5 |

Both keys live in the same project, so sign-in works with either [INFERRED]. Firebase warns: test restriction changes on a test key first, and don't remove APIs Firebase needs [CONFIRMED-primary](https://firebase.google.com/docs/projects/api-keys).

**Website entries for the browser key:**

| Host | Production | Previews | Is a preview wildcard safe? |
|---|---|---|---|
| Cloudflare Workers | `savx-logger.<acct>.workers.dev/*` | `*.<acct>.workers.dev/*` (Previews are `<preview>-<worker>.<acct>.workers.dev`, [CONFIRMED-primary](https://developers.cloudflare.com/workers/previews/)) | **Yes.** Only your account can create hosts under `<acct>.workers.dev` |
| Cloudflare Pages | `savx-logger.pages.dev/*` | `*.savx-logger.pages.dev/*` (`<hash>.<project>.pages.dev`, `<branch>.<project>.pages.dev`, [CONFIRMED-primary](https://developers.cloudflare.com/pages/configuration/preview-deployments/)) | **Yes** |
| Vercel | `<project>.vercel.app/*` | would need `*.vercel.app/*` | **No.** Any Vercel user could then use your key. Leave previews out, so they can't sign in. |
| Firebase Hosting | `<project>.web.app/*`, `<project>.firebaseapp.com/*` | channel URLs look like `PROJECT_ID--CHANNEL_ID-HASH.web.app` [CONFIRMED-primary](https://firebase.google.com/docs/hosting/test-preview-deploy) | **No** (it would need `*.web.app`) |
| Netlify | `<site>.netlify.app/*` | `deploy-preview-N--<site>.netlify.app` | **No** |
| Local dev | `localhost:5173/*`, `localhost:4173/*` | | [INFERRED]: port handling is undocumented. If it fails, use the device key or a separate dev key in `web/.env.local`. |

**Coupled header:** keep `Referrer-Policy: strict-origin-when-cross-origin`.

- It sends `Referer: https://<origin>/` on cross-origin fetches, which matches `host/*`.
- `no-referrer` or `same-origin` would strip the header, and every Auth call would then be rejected [INFERRED].

**What the restriction is worth:** the referrer is client-controlled outside browsers, so this is abuse friction, not security. Security still comes from UID-pinned rules [INFERRED]. One useful side effect: unlisted preview origins simply can't sign in.

### 1.4 RTDB websocket from any origin

- **Transport choice.** The SDK uses a WebSocket only, unless a WebSocket previously failed (`previous_websocket_failure` in localStorage) or storage is in-memory. In those cases it adds long-polling [CONFIRMED-primary: `TransportManager.initTransports_`].
- **Long-polling is JSONP.** It injects `<script src="https://<host>/.lp?…">` into a hidden iframe (`newScript.src = url`) [CONFIRMED-primary: SDK source].
- **Host redirect.** The server handshake returns an internal host (`handshake.h`). The SDK reconnects there and caches it as `firebase:host:<ns>` in localStorage [CONFIRMED-primary: SDK source].
- **Observed internal hosts** for real asia-southeast1 instances: `"h":"s-gke-apse1-nssi4-12.asia-southeast1.firebasedatabase.app"` and `s-gke-apse1-nssi3-11…` [OBSERVED: wss handshake to two public instances found by GitHub code search; handshake only, no data read]. CSP therefore needs `wss://*.asia-southeast1.firebasedatabase.app`, not just your instance host.
- **No origin check.** RTDB does not check `Origin`. A Node client with no Origin got a normal handshake [OBSERVED]. Rules are the only boundary, so the web host changes nothing about data security.
- **Spark limit:** 100 simultaneous connections [CONFIRMED-primary](https://firebase.google.com/pricing).

### 1.5 App Check: don't enforce it

- **Enforcement rejects unverified requests.** It is supported for Realtime Database and Authentication, and once on, "all unverified requests to that product will be rejected" [CONFIRMED-primary](https://firebase.google.com/docs/app-check/enable-enforcement). The ESP32 has no attestation.
- **A custom provider needs a backend.** It needs "a secure backend environment that can run the Node.js Firebase Admin SDK" [CONFIRMED-primary](https://firebase.google.com/docs/app-check/custom-provider). That means Functions or Cloud Run, which are Blaze-only (d1 F8).
- **Web provider cost on Spark.** reCAPTCHA Enterprise on Spark is limited to 4 score levels. Billing is required for all 11. Each domain must be listed on the key [CONFIRMED-primary](https://firebase.google.com/docs/app-check/web/recaptcha-enterprise-provider). It would also add `www.google.com`, `www.gstatic.com` and `content-firebaseappcheck.googleapis.com` to the CSP [INFERRED].
- **Related trap: Auth's reCAPTCHA protection.**
  - If reCAPTCHA protection is enabled for email/password, `handleRecaptchaFlow` loads `https://www.google.com/recaptcha/enterprise.js`. If a request comes back with `missing-recaptcha-token`, the SDK retries with reCAPTCHA [CONFIRMED-primary: SDK source].
  - In enforce mode, the ESP32's plain REST sign-in would fail [INFERRED]. Leave it off.
- **Decision:** no App Check on any host. If it is ever wanted, it needs Blaze plus a token-minting backend for the device.

### 1.6 What this means for the iOS PWA

- **Everything stays first-party.** Auth persistence lives in the app origin's own IndexedDB, and API calls are CORS fetches without cookies. Redirect-flow storage partitioning doesn't apply to this app [INFERRED from §1.1–1.2].
- **WebSockets** work in standalone mode. iOS suspends JS in the background, and the SDK reconnects on resume. This doesn't depend on the host [INFERRED].

---

## 2. Injecting `VITE_FIREBASE_*` at build time

Vite facts [CONFIRMED-primary](https://vite.dev/guide/env-and-mode):

- Only `VITE_*` variables reach the client.
- "VITE_* variables should not contain sensitive information". The values are "statically replaced at build time".
- "environment variables that already exist when Vite is executed have the highest priority". So CI can inject them as process env with no `.env` file.

**Recommended practice:** keep values out of git (`web/.env*` is already ignored; `.env.example` stays committed). Put them in the CI or platform settings as **non-secret Variables**. Why, when they aren't secret:

1. **Clones and forks** don't silently point at your project (already noted in `.env.example`).
2. **Secret scanning.** GitHub runs partner scanning for `google_api_key` on public repos (`"isPublic":true`). Push protection is off for that type (`"hasPushProtection":false`), so a leak goes unnoticed until Google flags the key [CONFIRMED-primary: GitHub supported-patterns data](https://docs.github.com/en/code-security/secret-scanning/introduction/supported-secret-scanning-patterns). Keeping it out of git avoids that noise if the repo is ever made public.
3. **One source of truth**, and you can switch to a dev Firebase project with no code change.
4. **GitHub Variables don't mask in logs** (Secrets do), which makes debugging easier. Use Secrets only for deploy tokens. [INFERRED]

| Where | How to set the six values (`VITE_FIREBASE_API_KEY` (browser key), `_AUTH_DOMAIN`, `_PROJECT_ID`, `_APP_ID`, `_DATABASE_URL`, `VITE_DEVICE_ID`) |
|---|---|
| Local | `web/.env` (gitignored) |
| GitHub Actions (recommended path) | Settings → Secrets and variables → Actions → **Variables**. Passed as job `env:` (YAML in §5). |
| Cloudflare Workers Builds (Git path) | Worker → Settings → Build → **Build variables and secrets**. "Build variables will not be accessible at runtime", and runtime vars are a separate section [CONFIRMED-primary](https://developers.cloudflare.com/workers/ci-cd/builds/configuration/). |
| Cloudflare Pages (Git path) | Project → Settings → Variables and Secrets (build) [INFERRED UI path] |
| Vercel | Project → Settings → Environment Variables, scope Production (Preview only if used). Redeploy after changes. With the CLI flow, `vercel pull` downloads them; process env from Actions still wins in Vite. |
| Netlify | Site configuration → Environment variables, scope Builds [INFERRED UI path] |
| Firebase Hosting | Built in CI, same GitHub Variables |

`authDomain` stays `<project>.firebaseapp.com` even off Firebase Hosting. It is only used by OAuth/popup flows, which this app doesn't call [INFERRED].

---

## 3. Security headers, cache headers, SPA fallback

### 3.1 CSP for an asia-southeast1 RTDB (tested)

```
default-src 'self';
script-src 'self';
style-src 'self';
img-src 'self' data: blob:;
font-src 'self';
connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app;
manifest-src 'self';
worker-src 'self';
frame-src 'none';
object-src 'none';
base-uri 'none';
form-action 'none';
frame-ancestors 'none'
```

**Why each entry:**

- **`connect-src` hosts** come from §1.3–1.4.
  - `*.asia-southeast1.firebasedatabase.app` matches both your instance host and the `s-gke-apse1-…` internal hosts.
  - `https://` and `wss://` are listed separately rather than relying on CSP3 scheme upgrading.
- **The RTDB host is deliberately not in `script-src`.**
  - That blocks the JSONP long-poll fallback. Allowing it would add JSONP gadgets to `script-src`, and RTDB REST also serves JSONP via `callback=` [INFERRED].
  - Trade-off: on a network that blocks WebSockets, live data won't load. If that ever happens, add `https://*.asia-southeast1.firebasedatabase.app` to `script-src`.
- **The build fits `'self'`:**
  - no inline scripts in `dist/index.html`;
  - no `eval(` or `new Function` in the bundles;
  - uPlot styles come from a CSS file;
  - Preact and uPlot set styles through the CSSOM, which CSP doesn't block. [OBSERVED: grep of the build]
- **`form-action 'none'`** is safe: `Login.tsx`/`Settings.tsx` call `preventDefault()`. If JS fails, it stops a GET submit that would put the password in the URL.
- **CSV export** uses `blob:` + `<a download>` and `navigator.share`. Neither is restricted by this CSP. Do **not** disable `web-share` in Permissions-Policy.
- **The CSP header also lands on `/sw.js`** and governs the service worker's own fetches. `connect-src 'self'` covers them.

**Test result.** The policy above was served on `dist/` (scratch build) and loaded in the Chromium browser pane, in demo mode with Live and Today charts:

- zero CSP violations;
- a `wss://…asia-southeast1.firebasedatabase.app/.ws` connection was allowed, and the server answered;
- an injected `<script src="https://…firebasedatabase.app/.lp">` was blocked (`script-src-elem`);
- `fetch('https://example.com')` was blocked (`connect-src`).

[OBSERVED, Chromium]. Safari should behave the same per CSP3 [INFERRED]. Re-check in the Safari Web Inspector after the first real deploy.

**Other headers:**

| Header | Value | Note |
|---|---|---|
| `Strict-Transport-Security` | `max-age=63072000; includeSubDomains` | Don't add `preload` on a platform subdomain. Firebase Hosting already sends `max-age=31556926; includeSubDomains; preload` [OBSERVED]. |
| `Referrer-Policy` | `strict-origin-when-cross-origin` | Required by the referrer-restricted key (§1.3) |
| `Permissions-Policy` | `camera=(), microphone=(), geolocation=(), payment=(), usb=()` | Leave `web-share` enabled |
| `X-Content-Type-Options` | `nosniff` | |
| `X-Frame-Options` | `DENY` | Legacy twin of `frame-ancestors` |
| `Cross-Origin-Opener-Policy` | `same-origin` | Fine because there are no auth popups |

**Cache headers** (set Cache-Control in exactly one rule per path; Cloudflare and Netlify comma-join duplicates):

| Path | Cache-Control |
|---|---|
| `/assets/*` (hashed) | `public, max-age=31536000, immutable` |
| `/`, `/index.html` | `no-cache` |
| `/sw.js` | `no-cache`. Browsers already "ignor[e] caching headers when checking for updates" of the SW script [CONFIRMED-primary](https://web.dev/articles/service-worker-lifecycle). |
| `/manifest.webmanifest` | `no-cache`, served as `application/manifest+json` |
| `/icons/*` (not hashed) | `public, max-age=604800` |

**Platform defaults:**

- Vercel: `public, max-age=0, must-revalidate` [CONFIRMED-primary](https://vercel.com/docs/caching/cache-control-headers).
- Cloudflare assets: `public, max-age=0, must-revalidate` plus an ETag [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/headers/).

### 3.2 SPA fallback

- **The app uses hash routes** (`/?demo=1#/today`) [OBSERVED]. The server only ever sees `/`, so a fallback is optional. It is kept only for robustness.
- **Exclude `/assets/` from the fallback where the platform allows it.** A missing old chunk then returns 404 instead of HTML. (`cacheFirst` in the SW already refuses to cache HTML.)

### 3.3 Two required edits to `web/vite.config.ts`

1. **`publicFiles()` precaches everything in `public/`.** Put `_headers` there (Cloudflare, Netlify) and it lands in the precache list.
   - Cloudflare says `_headers` "will not itself be served as a static asset" [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/headers/).
   - So `cache.addAll` either caches index.html under `/_headers` or, if the host returns 404, fails the whole install.
   - Fix: `if (name.startsWith('.') || name.startsWith('_')) continue;`
2. **Drop `/index.html` from the precache list** (keep `/`):
   - Cloudflare's default `html_handling: "auto-trailing-slash"` redirects `…/index.html` with 307 [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/routing/advanced/html-handling/). Live check: `https://developers.cloudflare.com/index.html → 307 → /` [OBSERVED].
   - A precached *redirected* response can be rejected when used to answer a navigation (redirect mode `manual`), which breaks offline launch [INFERRED].
   - Fix: `const urls = Array.from(new Set(['/', ...built, ...publicFiles(...)])).sort();`. The SW fallback `cache.match('/index.html') || cache.match('/')` keeps working.

### 3.4 Config files

**Cloudflare Workers (primary): `web/wrangler.jsonc`.**

- `main` "is optional for assets-only Workers".
- `preview_urls` "Whether we use <version>-<name>.<subdomain>.workers.dev…".
- `not_found_handling` enum: `single-page-application | 404-page | none`.
- Sources: [CONFIRMED-primary: docs](https://developers.cloudflare.com/workers/wrangler/configuration/) plus the wrangler 4.146.0 `config-schema.json`.

```jsonc
{
  "$schema": "https://unpkg.com/wrangler@4.146.0/config-schema.json",
  "name": "savx-logger",
  "compatibility_date": "2026-10-01",
  "workers_dev": true,
  "preview_urls": false,                 // Version URLs off (public by default)
  "assets": {
    "directory": "./dist",
    "not_found_handling": "single-page-application"   // unmatched paths -> /index.html, 200
  }
}
```

**Cloudflare (Workers or Pages): `web/public/_headers`.**

- Vite copies it to `dist/`.
- Limits: 100 rules, 2,000 chars per line [CONFIRMED-primary].
- No `Content-Type` line is needed: wrangler sets it from `mime`, which maps `webmanifest → application/manifest+json` [CONFIRMED-primary: wrangler 4.146.0 `getContentType` source].

```
/*
  Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app; manifest-src 'self'; worker-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'
  Strict-Transport-Security: max-age=63072000; includeSubDomains
  Referrer-Policy: strict-origin-when-cross-origin
  Permissions-Policy: camera=(), microphone=(), geolocation=(), payment=(), usb=()
  X-Content-Type-Options: nosniff
  X-Frame-Options: DENY
  Cross-Origin-Opener-Policy: same-origin

/assets/*
  Cache-Control: public, max-age=31536000, immutable

/icons/*
  Cache-Control: public, max-age=604800

/
  Cache-Control: no-cache

/index.html
  Cache-Control: no-cache

/sw.js
  Cache-Control: no-cache

/manifest.webmanifest
  Cache-Control: no-cache
```

**Cloudflare Pages instead of Workers:**

- Use the same `_headers`.
- No `_redirects` is needed: Pages auto-detects an SPA when there is no `404.html` [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/migration-guides/migrate-from-pages/).
- Pages limits: 500 builds/month, 20,000 files, 25 MiB per file [CONFIRMED-primary](https://developers.cloudflare.com/pages/platform/limits/).

**Vercel: `web/vercel.json`.**

- Put it in the project root, which here is `web/`.
- The filesystem is checked before rewrites [CONFIRMED-primary](https://vercel.com/docs/project-configuration/vercel-json).

```json
{
  "$schema": "https://openapi.vercel.sh/vercel.json",
  "framework": "vite",
  "installCommand": "npm ci",
  "buildCommand": "npm run build",
  "outputDirectory": "dist",
  "cleanUrls": false,
  "trailingSlash": false,
  "git": { "deploymentEnabled": false },
  "rewrites": [{ "source": "/((?!assets/|icons/).*)", "destination": "/index.html" }],
  "headers": [
    { "source": "/(.*)", "headers": [
      { "key": "Content-Security-Policy", "value": "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app; manifest-src 'self'; worker-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'" },
      { "key": "Strict-Transport-Security", "value": "max-age=63072000; includeSubDomains" },
      { "key": "Referrer-Policy", "value": "strict-origin-when-cross-origin" },
      { "key": "Permissions-Policy", "value": "camera=(), microphone=(), geolocation=(), payment=(), usb=()" },
      { "key": "X-Content-Type-Options", "value": "nosniff" },
      { "key": "X-Frame-Options", "value": "DENY" },
      { "key": "Cross-Origin-Opener-Policy", "value": "same-origin" }
    ]},
    { "source": "/assets/(.*)", "headers": [{ "key": "Cache-Control", "value": "public, max-age=31536000, immutable" }] },
    { "source": "/icons/(.*)",  "headers": [{ "key": "Cache-Control", "value": "public, max-age=604800" }] },
    { "source": "/",            "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
    { "source": "/index.html",  "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
    { "source": "/sw.js",       "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
    { "source": "/manifest.webmanifest", "headers": [{ "key": "Cache-Control", "value": "no-cache" }] }
  ]
}
```

`"git": { "deploymentEnabled": false }` is for the Actions path only. For the Git-integration path use `{ "main": true, "*": false }`: "If a branch matches multiple rules and at least one rule is `true`, a deployment will occur" [CONFIRMED-primary](https://vercel.com/docs/project-configuration/git-configuration).

**Netlify: `netlify.toml`** at the repo root (included for completeness, not recommended):

```toml
[build]
  base    = "web"
  command = "npm run build"
  publish = "dist"            # relative to base [INFERRED]
[build.environment]
  NODE_VERSION = "24"

[[redirects]]
  from = "/*"
  to = "/index.html"
  status = 200                # existing files are not shadowed [CONFIRMED-primary]

[[headers]]
  for = "/*"
  [headers.values]
    Content-Security-Policy = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app; manifest-src 'self'; worker-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"
    Strict-Transport-Security = "max-age=63072000; includeSubDomains"
    Referrer-Policy = "strict-origin-when-cross-origin"
    Permissions-Policy = "camera=(), microphone=(), geolocation=(), payment=(), usb=()"
    X-Content-Type-Options = "nosniff"
    X-Frame-Options = "DENY"
    Cross-Origin-Opener-Policy = "same-origin"
[[headers]]
  for = "/assets/*"
  [headers.values]
    Cache-Control = "public, max-age=31536000, immutable"
[[headers]]
  for = "/icons/*"
  [headers.values]
    Cache-Control = "public, max-age=604800"
[[headers]]
  for = "/"
  [headers.values]
    Cache-Control = "no-cache"
[[headers]]
  for = "/index.html"
  [headers.values]
    Cache-Control = "no-cache"
[[headers]]
  for = "/sw.js"
  [headers.values]
    Cache-Control = "no-cache"
[[headers]]
  for = "/manifest.webmanifest"
  [headers.values]
    Cache-Control = "no-cache"
```

Syntax source: [CONFIRMED-primary](https://docs.netlify.com/manage/routing/headers/), [CONFIRMED-primary](https://docs.netlify.com/manage/routing/redirects/rewrites-proxies/).

**Firebase Hosting: `firebase.json`** (current file plus security headers). Keep the `database` block whichever host serves the web.

```json
{
  "database": { "rules": "firebase/database.rules.json" },
  "hosting": {
    "public": "web/dist",
    "ignore": ["firebase.json", "**/.*", "**/node_modules/**", "**/_headers", "**/_redirects"],
    "cleanUrls": false,
    "trailingSlash": false,
    "rewrites": [{ "source": "**", "destination": "/index.html" }],
    "headers": [
      { "source": "/assets/**", "headers": [{ "key": "Cache-Control", "value": "public, max-age=31536000, immutable" }] },
      { "source": "/icons/**",  "headers": [{ "key": "Cache-Control", "value": "public, max-age=604800" }] },
      { "source": "/sw.js",     "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "/",          "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "**/*.html",  "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "**/*.webmanifest", "headers": [
        { "key": "Cache-Control", "value": "no-cache" },
        { "key": "Content-Type", "value": "application/manifest+json" } ] },
      { "source": "**", "headers": [
        { "key": "Content-Security-Policy", "value": "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app; manifest-src 'self'; worker-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'" },
        { "key": "Referrer-Policy", "value": "strict-origin-when-cross-origin" },
        { "key": "Permissions-Policy", "value": "camera=(), microphone=(), geolocation=(), payment=(), usb=()" },
        { "key": "X-Content-Type-Options", "value": "nosniff" },
        { "key": "X-Frame-Options", "value": "DENY" },
        { "key": "Cross-Origin-Opener-Policy", "value": "same-origin" } ] }
    ]
  }
}
```

There is no HSTS line here because Firebase Hosting already sends it [OBSERVED].

---

## 4. Preview deployments

**Data risk:** none beyond production.

- Previews use the production Firebase config, and the rules require the owner UID.
- What a preview *can* do: buggy preview code running while you are signed in as the owner can write `cfg` or delete `hist`, exactly as production can.
- A preview is a separate origin, so you'd sign in there separately [INFERRED].

| Platform | Public by default? | Free-tier protection | How to turn previews off |
|---|---|---|---|
| Cloudflare Workers | Version URLs: "the URL is public"; Previews: "Preview URLs are public by default" [CONFIRMED-primary](https://developers.cloudflare.com/workers/configuration/previews/) | Cloudflare Access on one Worker or all Workers; Previews: 100 per Worker on Free [CONFIRMED-primary](https://developers.cloudflare.com/workers/previews/). Whether Zero Trust onboarding asks for a card: unverified. | `"preview_urls": false`; don't enable preview builds in Workers Builds |
| Cloudflare Pages | "By default, these deployment URLs are public" | Settings → General → **Enable access policy** [CONFIRMED-primary](https://developers.cloudflare.com/pages/configuration/preview-deployments/) | Branch deploy controls (Pages ✅ per the matrix) |
| Vercel Hobby | Yes, unless protected | **Vercel Authentication** on all plans. "Standard Protection" protects everything except production domains. Password Protection is "Not available on Hobby" [CONFIRMED-primary](https://vercel.com/docs/deployment-protection) | `git.deploymentEnabled`; with Actions, just don't deploy previews |
| Netlify Free | Yes | "Password protection isn't available on Credit-based Free and Personal plans" [CONFIRMED-primary](https://docs.netlify.com/manage/security/secure-access-to-sites/password-protection/) | Disable Deploy Previews in deploy contexts [INFERRED UI] |
| Firebase Hosting | "they are public. So, anyone who knows the URL can access it" [CONFIRMED-primary](https://firebase.google.com/docs/hosting/test-preview-deploy) | None; channels expire (action default `7d`, [CONFIRMED-primary action.yml]) | Don't create channels |

**Recommendation:** no previews by default. Test with `npm run preview` plus `?demo=1`.

- If you want them, use Cloudflare Previews with an Access policy, plus the `*.<acct>.workers.dev/*` referrer entry.
- On Vercel, previews also show the Vercel Toolbar. It needs `vercel.live` and `wss://ws-us3.pusher.com` in CSP [CONFIRMED-primary](https://vercel.com/docs/vercel-toolbar/managing-toolbar). Set Toolbar to **Off** for Preview instead.

---

## 5. CI/CD pipeline

### 5.1 Option (a): the platform's Git integration

| Platform | Settings | Free build allowance |
|---|---|---|
| Cloudflare Workers Builds | Root directory `web`; build `npm run build`; deploy `npx wrangler@4.146.0 deploy` (default deploy command `npx wrangler deploy`; Preview command defaults to `npx wrangler preview`) [CONFIRMED-primary](https://developers.cloudflare.com/workers/ci-cd/builds/configuration/) | 3,000 build min/month, 1 concurrent, 20 min timeout [CONFIRMED-primary](https://developers.cloudflare.com/workers/ci-cd/builds/limits-and-pricing/). Private repo support not stated [INFERRED yes]. |
| Vercel | Root Directory `web`, preset Vite; `git.deploymentEnabled: {"main": true, "*": false}` | Hobby: 100 deployments/day, 100 GB Fast Data Transfer, 1M CDN requests. Over a limit you usually "wait until 30 days have passed". Non-commercial personal use only [CONFIRMED-primary](https://vercel.com/docs/plans/hobby). Importing a personal repo requires being its Owner [CONFIRMED-primary](https://vercel.com/docs/git/vercel-for-github). |
| Firebase Hosting | `firebase init hosting:github` | Actions minutes |

Even with (a), rules and firmware still need GitHub Actions. And (a) deploys without first running your typecheck gate.

### 5.2 Option (b), recommended: GitHub Actions as the single control plane

Ground rules:

- **One web workflow only.** Pick `web-cloudflare.yml` **or** `web-vercel.yml`, and disconnect the platform's Git auto-deploys.
- **Pin actions to SHAs** (resolved today with `git ls-remote`):
  - `actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1` (v7.0.1)
  - `actions/setup-node@820762786026740c76f36085b0efc47a31fe5020` (v7.0.0)
  - `actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97` (v7.0.0)
  - `actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9` (v6.1.0)
  - `google-github-actions/auth@7c6bc770dae815cd3e89ee6cdf493a5fab2cc093` (v3.0.0)
  - All run on Node 24 [CONFIRMED-primary: their action.yml].
- **Pin CLIs to versions at least two weeks old:**
  - wrangler 4.146.0 (2026-10-01, needs Node ≥22)
  - vercel 62.4.0 (2026-10-05, Node ≥20)
  - firebase-tools 15.32.1 (2026-09-30, Node ≥20)
  - platformio 6.2.0 (2026-09-05, Python ≥3.9)
  - [CONFIRMED-primary: npm/PyPI metadata]
- **No `environment:`.** "Users with GitHub Free plans can only configure environments for public repositories" [CONFIRMED-primary](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments). Gate deploys with `if:` on `refs/heads/main`.

**Repository Variables:**

- `VITE_FIREBASE_API_KEY`, `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_PROJECT_ID`, `VITE_FIREBASE_APP_ID`, `VITE_FIREBASE_DATABASE_URL`, `VITE_DEVICE_ID`
- `FIREBASE_PROJECT_ID`, `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`
- `GCP_WIF_PROVIDER`, `GCP_RULES_SA`
- optional `CF_PREVIEWS` / `VERCEL_PREVIEWS` = `true`

**Repository Secrets:**

- Cloudflare: `CLOUDFLARE_API_TOKEN` (from the "Edit Cloudflare Workers" template, one account) and `CLOUDFLARE_ACCOUNT_ID`.
- Vercel: `VERCEL_TOKEN`, `VERCEL_ORG_ID`, `VERCEL_PROJECT_ID`.
- Only if you skip Workload Identity Federation (WIF): `GCP_SA_KEY`.

#### `.github/workflows/web-cloudflare.yml`

```yaml
name: web -> Cloudflare Workers

on:
  push:
    branches: [main]
    paths: ['web/**', 'shared/**', '.github/workflows/web-cloudflare.yml']
  pull_request:
    paths: ['web/**', 'shared/**', '.github/workflows/web-cloudflare.yml']
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: web-${{ github.ref }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}

env:
  WRANGLER_VERSION: 4.146.0

jobs:
  build-deploy:
    runs-on: ubuntu-latest
    timeout-minutes: 15
    defaults:
      run:
        working-directory: web
    env:
      VITE_FIREBASE_API_KEY: ${{ vars.VITE_FIREBASE_API_KEY }}
      VITE_FIREBASE_AUTH_DOMAIN: ${{ vars.VITE_FIREBASE_AUTH_DOMAIN }}
      VITE_FIREBASE_PROJECT_ID: ${{ vars.VITE_FIREBASE_PROJECT_ID }}
      VITE_FIREBASE_APP_ID: ${{ vars.VITE_FIREBASE_APP_ID }}
      VITE_FIREBASE_DATABASE_URL: ${{ vars.VITE_FIREBASE_DATABASE_URL }}
      VITE_DEVICE_ID: ${{ vars.VITE_DEVICE_ID }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: web/package-lock.json

      - name: Check build variables
        run: |
          for v in VITE_FIREBASE_API_KEY VITE_FIREBASE_AUTH_DOMAIN VITE_FIREBASE_PROJECT_ID VITE_FIREBASE_APP_ID VITE_FIREBASE_DATABASE_URL VITE_DEVICE_ID; do
            [ -n "${!v}" ] || { echo "::error::repository variable $v is not set"; exit 1; }
          done

      - run: npm ci
      - run: npm run typecheck
      - run: npm run build

      - name: Sanity-check dist
        run: |
          test -s dist/index.html && test -s dist/sw.js && test -s dist/_headers
          if grep -q '"/_headers"\|"/_redirects"\|"/index.html"' dist/sw.js; then
            echo "::error::sw.js precache must not list /_headers, /_redirects or /index.html (see vite.config.ts)"; exit 1
          fi

      - name: Deploy (main only)
        if: github.event_name != 'pull_request' && github.ref == 'refs/heads/main'
        run: npx --yes "wrangler@${WRANGLER_VERSION}" deploy
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          CLOUDFLARE_ACCOUNT_ID: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}

      # Opt-in. `wrangler preview` needs Wrangler >= 4.135.0. Support for assets-only Workers is UNVERIFIED; test once.
      - name: Preview (PRs, opt-in)
        if: github.event_name == 'pull_request' && vars.CF_PREVIEWS == 'true'
        run: npx --yes "wrangler@${WRANGLER_VERSION}" preview --name "pr-${{ github.event.pull_request.number }}"
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          CLOUDFLARE_ACCOUNT_ID: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
```

The `sw.js` check assumes the §3.3 `vite.config.ts` edits are made. The `wrangler preview --name/--json` usage follows Cloudflare's own GitHub example [CONFIRMED-primary](https://developers.cloudflare.com/workers/previews/automation-examples/).

#### `.github/workflows/web-vercel.yml`

Use this instead of `web-cloudflare.yml`. The commands follow the documented pull → build → deploy `--prebuilt` flow [CONFIRMED-primary](https://vercel.com/kb/guide/how-can-i-use-github-actions-with-vercel).

```yaml
name: web -> Vercel

on:
  push:
    branches: [main]
    paths: ['web/**', 'shared/**', '.github/workflows/web-vercel.yml']
  pull_request:
    paths: ['web/**', 'shared/**', '.github/workflows/web-vercel.yml']
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: web-${{ github.ref }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}

env:
  VERCEL_CLI: 62.4.0

jobs:
  build-deploy:
    runs-on: ubuntu-latest
    timeout-minutes: 15
    defaults:
      run:
        working-directory: web        # Vercel project created with `vercel link` inside web/, Root Directory unset
    env:
      VERCEL_ORG_ID: ${{ secrets.VERCEL_ORG_ID }}
      VERCEL_PROJECT_ID: ${{ secrets.VERCEL_PROJECT_ID }}
      VITE_FIREBASE_API_KEY: ${{ vars.VITE_FIREBASE_API_KEY }}
      VITE_FIREBASE_AUTH_DOMAIN: ${{ vars.VITE_FIREBASE_AUTH_DOMAIN }}
      VITE_FIREBASE_PROJECT_ID: ${{ vars.VITE_FIREBASE_PROJECT_ID }}
      VITE_FIREBASE_APP_ID: ${{ vars.VITE_FIREBASE_APP_ID }}
      VITE_FIREBASE_DATABASE_URL: ${{ vars.VITE_FIREBASE_DATABASE_URL }}
      VITE_DEVICE_ID: ${{ vars.VITE_DEVICE_ID }}
      IS_PROD: ${{ github.event_name != 'pull_request' && github.ref == 'refs/heads/main' }}
      DEPLOY: ${{ (github.event_name != 'pull_request' && github.ref == 'refs/heads/main') || (github.event_name == 'pull_request' && vars.VERCEL_PREVIEWS == 'true') }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: web/package-lock.json

      - name: Install, typecheck, build (gate)
        run: |
          npm ci
          npm run typecheck
          npm run build

      - name: Deploy with Vercel CLI
        if: env.DEPLOY == 'true'
        env:
          VERCEL_TOKEN: ${{ secrets.VERCEL_TOKEN }}
        run: |
          npm install --global "vercel@${VERCEL_CLI}"
          if [ "$IS_PROD" = "true" ]; then TARGET=production; PROD=--prod; else TARGET=preview; PROD=; fi
          vercel pull --yes --environment="$TARGET" --token="$VERCEL_TOKEN"
          vercel build $PROD --token="$VERCEL_TOKEN"
          url=$(vercel deploy --prebuilt $PROD --token="$VERCEL_TOKEN")
          echo "Deployed ($TARGET): $url" >> "$GITHUB_STEP_SUMMARY"
```

Also add `web/.vercel/` to `.gitignore`. Running the CLI inside `web/` with Root Directory unset is [INFERRED] (it avoids monorepo root-directory ambiguity).

#### `.github/workflows/firebase-rules.yml`

Rules stay in Firebase whatever the web host. Facts behind this workflow:

- **Credentials:** the Firebase CLI recommends Application Default Credentials. `FIREBASE_TOKEN`/`login:ci` "is no longer recommended" [CONFIRMED-primary](https://firebase.google.com/docs/cli).
- **Permissions:** a database deploy requires `firebase.projects.get` + `firebasedatabase.instances.update`, then reads instance details and PUTs `/.settings/rules.json`. `--dry-run` validates server-side [CONFIRMED-primary: firebase-tools 15.32.1 source]. `roles/firebasedatabase.admin` includes `instances.get/list/update/create`, and product-level roles include `firebase.projects.get` [CONFIRMED-primary](https://firebase.google.com/docs/projects/iam/roles-predefined-product).
- **Blast radius:** that role can also read and write all data. Use keyless WIF so there is no key to leak.
- **WIF needs** `id-token: write`, and is "recommended over Service Account Keys" [CONFIRMED-primary](https://github.com/google-github-actions/auth).

```yaml
name: firebase rules

on:
  push:
    branches: [main]
    paths: ['firebase/**', 'firebase.json', '.github/workflows/firebase-rules.yml']
  pull_request:
    paths: ['firebase/**', 'firebase.json', '.github/workflows/firebase-rules.yml']
  workflow_dispatch:

permissions:
  contents: read
  id-token: write

concurrency:
  group: firebase-rules
  cancel-in-progress: false

env:
  FIREBASE_TOOLS: 15.32.1

jobs:
  rules:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24

      - name: Fill UIDs into the rules (placeholders stay in git)
        env:
          OWNER_UID: ${{ vars.FIREBASE_OWNER_UID }}
          DEVICE_UID: ${{ vars.FIREBASE_DEVICE_UID }}
        run: |
          [ -n "$OWNER_UID" ] && [ -n "$DEVICE_UID" ] || { echo "::error::set FIREBASE_OWNER_UID and FIREBASE_DEVICE_UID"; exit 1; }
          sed -i -e "s/OWNER_UID/${OWNER_UID}/g" -e "s/DEVICE_UID/${DEVICE_UID}/g" firebase/database.rules.json
          if grep -q 'OWNER_UID\|DEVICE_UID' firebase/database.rules.json; then echo "::error::placeholder left"; exit 1; fi
          python3 -m json.tool firebase/database.rules.json > /dev/null

      - uses: google-github-actions/auth@7c6bc770dae815cd3e89ee6cdf493a5fab2cc093 # v3.0.0
        with:
          project_id: ${{ vars.FIREBASE_PROJECT_ID }}
          workload_identity_provider: ${{ vars.GCP_WIF_PROVIDER }}
          service_account: ${{ vars.GCP_RULES_SA }}
          # Key-file fallback instead of the two lines above:
          # credentials_json: ${{ secrets.GCP_SA_KEY }}

      - name: Dry-run on PRs, deploy on main
        env:
          PROJECT: ${{ vars.FIREBASE_PROJECT_ID }}
          MODE: ${{ (github.event_name != 'pull_request' && github.ref == 'refs/heads/main') && 'deploy' || 'dry-run' }}
        run: |
          ARGS="deploy --only database --project $PROJECT --non-interactive"
          if [ "$MODE" = "deploy" ]; then npx --yes "firebase-tools@${FIREBASE_TOOLS}" $ARGS
          else npx --yes "firebase-tools@${FIREBASE_TOOLS}" $ARGS --dry-run; fi
```

**One-time WIF setup** (Mac, `gcloud`). Whether WIF works on a Spark project with no billing account is [INFERRED]. The SA-key fallback works either way; keys are allowed because personal projects have no org policy [INFERRED].

```bash
PROJECT_ID=<project-id>; REPO=<github-user>/<repo>
gcloud config set project "$PROJECT_ID"
gcloud services enable iam.googleapis.com iamcredentials.googleapis.com sts.googleapis.com
gcloud iam service-accounts create gha-rtdb-rules --display-name "GitHub Actions: RTDB rules"
SA="gha-rtdb-rules@${PROJECT_ID}.iam.gserviceaccount.com"
gcloud projects add-iam-policy-binding "$PROJECT_ID" --member "serviceAccount:$SA" --role roles/firebasedatabase.admin
gcloud iam workload-identity-pools create github --location=global --display-name=GitHub
gcloud iam workload-identity-pools providers create-oidc repo --location=global --workload-identity-pool=github \
  --issuer-uri="https://token.actions.githubusercontent.com" \
  --attribute-mapping="google.subject=assertion.sub,attribute.repository=assertion.repository" \
  --attribute-condition="assertion.repository=='${REPO}'"
PN=$(gcloud projects describe "$PROJECT_ID" --format='value(projectNumber)')
gcloud iam service-accounts add-iam-policy-binding "$SA" --role roles/iam.workloadIdentityUser \
  --member "principalSet://iam.googleapis.com/projects/${PN}/locations/global/workloadIdentityPools/github/attribute.repository/${REPO}"
echo "GCP_WIF_PROVIDER=projects/${PN}/locations/global/workloadIdentityPools/github/providers/repo"
echo "GCP_RULES_SA=${SA}"
```

**Firebase Hosting variant:** reuse this auth step and run `npx --yes firebase-tools@15.32.1 deploy --only hosting --project "$PROJECT" --non-interactive` after `npm run build`. Add the Firebase Hosting Admin role to the SA. The IAM page notes extra `serviceusage` needs for CLI Hosting deploys; add whatever the CLI reports as missing.

#### `.github/workflows/firmware.yml`

Environments come from `firmware/platformio.ini` (`savx`, `native`). The steps follow PlatformIO's documented cache + setup-python + `pip install platformio` + `pio run` pattern [CONFIRMED-primary](https://docs.platformio.org/en/latest/integration/ci/github-actions.html).

```yaml
name: firmware

on:
  push:
    branches: [main]
    paths: ['firmware/**', 'shared/**', '.github/workflows/firmware.yml']
  pull_request:
    paths: ['firmware/**', 'shared/**', '.github/workflows/firmware.yml']
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: firmware-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build-test:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    defaults:
      run:
        working-directory: firmware
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9 # v6.1.0
        with:
          path: |
            ~/.cache/pip
            ~/.platformio/.cache
            ~/.platformio/packages
            ~/.platformio/platforms
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
          restore-keys: |
            pio-${{ runner.os }}-

      - uses: actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97 # v7.0.0
        with:
          python-version: '3.12'

      - run: pip install "platformio==6.2.0"

      - name: Build ESP32-S3 firmware
        run: pio run -e savx

      - name: Host unit tests
        if: hashFiles('firmware/test/**') != ''
        run: pio test -e native
```

#### `.github/dependabot.yml` (optional, keeps the pinned SHAs current)

```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: /
    schedule: { interval: monthly }
  - package-ecosystem: npm
    directory: /web
    schedule: { interval: monthly }
```

### 5.3 GitHub Actions minutes for a private repo

- **Allowance:** GitHub Free gives 2,000 min/month and 500 MB artifact storage. Public repos are free. "usage is blocked once you use up your quota" if no payment method is on file [CONFIRMED-primary](https://docs.github.com/en/billing/concepts/product-billing/github-actions).
- **Estimated use:** the web job is about 1–3 min, rules about 1 min, firmware about 3–6 min with a warm cache (the first pioarduino download takes longer). At roughly 60 web + 20 firmware runs a month that is about 300 min [INFERRED].
- **Keep it low:** use Linux runners only, and keep the `paths:` filters. No artifacts are uploaded.

---

## 6. iOS PWA points that depend on the host

| Item | Status / action |
|---|---|
| HTTPS | Every candidate host is HTTPS. `app` and `dev` are HSTS-preloaded at the TLD level, which covers `web.app`, `vercel.app`, `netlify.app`, `pages.dev` and `workers.dev`. `github.io` and `firebaseapp.com` are "unknown" (not on the list) [OBSERVED: hstspreload.org API] |
| Manifest MIME | `<link rel="manifest" href="/manifest.webmanifest">` is present. Cloudflare sends `application/manifest+json` automatically [CONFIRMED-primary: wrangler source]; Firebase sets it via `firebase.json`. Check others after deploy with `curl -sI https://<host>/manifest.webmanifest`. Whether Safari rejects a wrong type: [INFERRED: no], but serve it right anyway. |
| apple-touch-icon | `/icons/apple-touch-icon.png` (180×180) is linked in `index.html` and is served with a 7-day cache. Manifest icons are also used ("If you define your icons in the manifest, they're used!" [CONFIRMED-primary](https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/)). Keep both. |
| id / start_url / scope | All `/`. Host at the origin root. That excludes GitHub Pages project sites (`/<repo>/`) [CONFIRMED-primary](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages) |
| Opening as an app | In iOS/iPadOS 26, "By default, every website added to the Home Screen opens as a web app" [CONFIRMED-primary, WebKit, June 9, 2025] |
| SW scope / update | `/sw.js` gives scope `/` ("default scope … is `./` relative to the script URL"). Update checks happen on in-scope navigations and bypass HTTP caching [CONFIRMED-primary](https://web.dev/articles/service-worker-lifecycle). Standalone apps rarely navigate, so keep the existing `reg.update()` on `visibilitychange` and the "Update available" flow. Never serve `/sw.js` through a redirect. Apply the §3.3 precache edits. |
| Storage | Home Screen web apps "are not part of Safari" and keep their own day count for ITP's 7-day storage cap. "Their days of use will match actual use" [CONFIRMED-primary](https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/). Persistence is granted "based on heuristics like whether the website is opened as a Home Screen Web App" [CONFIRMED-primary](https://webkit.org/blog/14403/updates-to-storage-policy/). `Login.tsx` already calls `navigator.storage.persist()`. |

**Choose the final domain before installing on the iPhone.** Everything is bound to the origin:

- Firebase Auth's IndexedDB session
- the app's IDB history cache (`lib/idb.ts`)
- RTDB's cached `firebase:host:` and transport flags in localStorage
- the service worker and Cache Storage
- the Home Screen icon's start URL

A domain change means: delete the icon, re-add it, sign in again, and re-download history. There is no cross-origin migration path [INFERRED].

Domain options:

- **Workers:** `savx-logger.<account-subdomain>.workers.dev`. Changing the account subdomain later changes the origin, so pick it carefully. Custom domains on Workers need Cloudflare-managed nameservers: "Workers does not support any domain whose nameservers are not managed by Cloudflare" [CONFIRMED-primary](https://developers.cloudflare.com/workers/static-assets/migration-guides/migrate-from-pages/).
- **Pages:** `savx-logger.pages.dev` (supports external DNS for custom domains).
- **Vercel:** `<project>.vercel.app`.

---

## 7. Policy changes 2024–2026 relevant here

| When | Change | Source |
|---|---|---|
| Jan 2024 | Vercel personal accounts converted to Hobby teams; Hobby restricted to non-commercial personal use | [Vercel Hobby](https://vercel.com/docs/plans/hobby) [CONFIRMED-primary] |
| May 2024 | Firebase auto-restricts API keys to Firebase APIs | [Firebase API keys](https://firebase.google.com/docs/projects/api-keys) [CONFIRMED-primary] |
| Apr 28, 2025 | New Firebase projects no longer authorize `localhost` by default | [Auth FAQ](https://firebase.google.com/docs/auth/faq-and-troubleshooting) [CONFIRMED-primary] |
| Jun 2025 | iOS 26: every Home Screen site opens as a web app by default | [WebKit](https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/) [CONFIRMED-primary] |
| Sep 4, 2025 | Netlify credit-based plans for new accounts (details in §0) | [Netlify](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/) [CONFIRMED-primary] |
| 2025–2026 | Cloudflare: "Start new projects with Workers"; `_headers`/`_redirects` native in Workers; "Version URLs (previously called preview URLs)"; Workers Previews (Wrangler ≥4.135.0, 100 per Worker on Free); Workers Builds 3,000 min/month | Cloudflare docs above [CONFIRMED-primary] |
| 2026 | Vercel: Standard Protection covers everything except production domains; Password Protection not on Hobby; Hobby limits 100 GB transfer, 1M CDN requests, 100 deployments/day | [Vercel](https://vercel.com/docs/deployment-protection) [CONFIRMED-primary] |
| Ongoing | Firebase Hosting free limits still disagree: pricing page "360 MB/day", usage doc "10 GB/month" (both read today). One cold load of this build is about 135 KB gzip, so either limit is ample. | [pricing](https://firebase.google.com/pricing), [quotas](https://firebase.google.com/docs/hosting/usage-quotas-pricing) [CONFIRMED-primary] |
| Ongoing | Firebase CLI CI auth: ADC recommended, `FIREBASE_TOKEN` "(Legacy)"; firebase-tools 15 removed `.bolt` rules | [Firebase CLI](https://firebase.google.com/docs/cli) [CONFIRMED-primary] + CLI source |
| Ongoing | GitHub Free: Pages needs a public repo; Environments are public-repo only | GitHub docs above [CONFIRMED-primary] |

---

## 8. Files to add or change in the repo

**Edits:**

- `web/vite.config.ts`: the two precache edits in §3.3.
- `firebase.json`: keep `database`. Keep or extend `hosting` only if Firebase Hosting remains the fallback (§3.4).
- `.gitignore`: add `web/.vercel/` and `.wrangler/`.

**New files:**

- `web/public/_headers` and `web/wrangler.jsonc` (Cloudflare), **or** `web/vercel.json` (Vercel).
- `.github/workflows/web-cloudflare.yml` **or** `web-vercel.yml`, plus `firebase-rules.yml`, `firmware.yml`, and optionally `dependabot.yml`.

**Docs to update:**

- `docs/07` and `firebase/README.md` §5: the ESP32 and the `curl` tests use the new **device key**.
- d1 §6.7 steps 7–8: replace `firebase deploy --only hosting` with the CI deploy and the new origin.

**Scratch evidence, not part of the repo:**

- CSP test server: `<scratch>`
- Scratch build: `<scratch>`
- RTDB handshake probe: `<scratch>`

## Sources (fetched 2026-10-21)

- **Cloudflare:**
  - [SPA routing](https://developers.cloudflare.com/workers/static-assets/routing/single-page-application/)
  - [_headers](https://developers.cloudflare.com/workers/static-assets/headers/)
  - [billing](https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/)
  - [Pages→Workers](https://developers.cloudflare.com/workers/static-assets/migration-guides/migrate-from-pages/)
  - [Pages landing](https://developers.cloudflare.com/pages/)
  - [Version URLs](https://developers.cloudflare.com/workers/configuration/previews/)
  - [Workers Previews](https://developers.cloudflare.com/workers/previews/)
  - [Previews CI example](https://developers.cloudflare.com/workers/previews/automation-examples/)
  - [limits](https://developers.cloudflare.com/workers/platform/limits/)
  - [Builds limits](https://developers.cloudflare.com/workers/ci-cd/builds/limits-and-pricing/)
  - [Builds config](https://developers.cloudflare.com/workers/ci-cd/builds/configuration/)
  - [html_handling](https://developers.cloudflare.com/workers/static-assets/routing/advanced/html-handling/)
  - [assets binding](https://developers.cloudflare.com/workers/static-assets/binding/)
  - [wrangler config](https://developers.cloudflare.com/workers/wrangler/configuration/)
  - [Pages previews](https://developers.cloudflare.com/pages/configuration/preview-deployments/)
  - [Pages limits](https://developers.cloudflare.com/pages/platform/limits/)
  - [network map](https://www.cloudflare.com/network/)
- **Vercel:**
  - [Hobby](https://vercel.com/docs/plans/hobby)
  - [Deployment Protection](https://vercel.com/docs/deployment-protection)
  - [GitHub Actions guide](https://vercel.com/kb/guide/how-can-i-use-github-actions-with-vercel)
  - [Toolbar/CSP](https://vercel.com/docs/vercel-toolbar/managing-toolbar)
  - [Cache-Control](https://vercel.com/docs/caching/cache-control-headers)
  - [vercel.json](https://vercel.com/docs/project-configuration/vercel-json)
  - [Git config](https://vercel.com/docs/project-configuration/git-configuration)
  - [Vercel for GitHub](https://vercel.com/docs/git/vercel-for-github)
- **Netlify:**
  - [credit plans](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/)
  - [how credits work](https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/how-credits-work/)
  - [pricing](https://www.netlify.com/pricing/)
  - [password protection](https://docs.netlify.com/manage/security/secure-access-to-sites/password-protection/)
  - [rewrites](https://docs.netlify.com/manage/routing/redirects/rewrites-proxies/)
  - [headers](https://docs.netlify.com/manage/routing/headers/)
- **GitHub:**
  - [Actions billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
  - [Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits)
  - [creating a Pages site](https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site)
  - [what is Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages)
  - [environments](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments)
  - [secret patterns](https://docs.github.com/en/code-security/secret-scanning/introduction/supported-secret-scanning-patterns)
  - [google-github-actions/auth](https://github.com/google-github-actions/auth)
- **Firebase and Google:**
  - [API keys](https://firebase.google.com/docs/projects/api-keys)
  - [Cloud API key restrictions](https://docs.cloud.google.com/api-keys/docs/add-restrictions-api-keys)
  - [Auth FAQ](https://firebase.google.com/docs/auth/faq-and-troubleshooting)
  - [email action state](https://firebase.google.com/docs/auth/web/passing-state-in-email-actions)
  - [redirect best practices](https://firebase.google.com/docs/auth/web/redirect-best-practices)
  - [App Check reCAPTCHA Enterprise](https://firebase.google.com/docs/app-check/web/recaptcha-enterprise-provider)
  - [App Check enforcement](https://firebase.google.com/docs/app-check/enable-enforcement)
  - [custom provider](https://firebase.google.com/docs/app-check/custom-provider)
  - [Hosting previews](https://firebase.google.com/docs/hosting/test-preview-deploy)
  - [Hosting quotas](https://firebase.google.com/docs/hosting/usage-quotas-pricing)
  - [pricing](https://firebase.google.com/pricing)
  - [CLI](https://firebase.google.com/docs/cli)
  - [IAM product roles](https://firebase.google.com/docs/projects/iam/roles-predefined-product)
- **Web platform:**
  - [Vite env](https://vite.dev/guide/env-and-mode)
  - [PlatformIO GitHub Actions](https://docs.platformio.org/en/latest/integration/ci/github-actions.html)
  - [SW lifecycle](https://web.dev/articles/service-worker-lifecycle)
  - [WebKit storage policy 2023](https://webkit.org/blog/14403/updates-to-storage-policy/)
  - [WebKit ITP 2020](https://webkit.org/blog/10218/full-third-party-cookie-blocking-and-more/)
  - [WebKit WWDC25](https://webkit.org/blog/16993/news-from-wwdc25-web-technology-coming-this-fall-in-safari-26-beta/)
  - [HSTS preload API](https://hstspreload.org/api/v2/status?domain=app)
- **Search results consulted, not relied on:**
  - [Cloudflare Zero Trust pricing (third-party)](https://controld.com/blog/cloudflare-zero-trust-pricing/)
  - [heise on iOS 26 web apps](https://heise.de/-10749652)
  - [firebase-tools issue #3455](https://github.com/firebase/firebase-tools/issues/3455)