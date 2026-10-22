# r7: Web hosting and CI/CD decision for the Solar Logger PWA

**Status: DECIDED, 2026-10-21 (Asia/Dhaka).** This file is the decision. The inputs are [r7a](r7a-hosting-landscape.md) (landscape), [r7b](r7b-hosting-integration.md) (integration) and [r7c](r7c-hosting-verification.md) (verification). Where they disagree, r7c wins. The design it plugs into is [d1](d1-architecture.md) §6, under the hard rules in [user-constraints.md](user-constraints.md): zero-cost cloud, no card, no new hardware.

**Tags.**
- **[CONFIRMED-primary]**: an official page, or registry or source metadata, read on 2026-10-21. This is either here or in r7c.
- **[OBSERVED]**: measured on this Mac today.
- **[OBSERVED-local]**: Cloudflare's local runtime (`wrangler dev` 4.146.0 / workerd), not the production edge.
- **[INFERRED]** and **[UNVERIFIED]** carry their usual meanings.

---

## 0. Decision at a glance

| | Decision |
|---|---|
| **Primary host** | **Cloudflare Workers static assets.** An assets-only Worker named `savx-logger`, served at **`https://solar.<acct>.workers.dev`**. Static-asset requests are free and unlimited [CONFIRMED-primary]. It is served from Cloudflare's Dhaka and Chittagong PoPs [CONFIRMED-primary]. |
| **Fallback host** | **Firebase Hosting (Spark), cold standby.** It is the `hosting` block in `firebase.json` and serves `https://<project-id>.web.app`. It is deployed by hand only, if Cloudflare becomes unusable. Switching changes the origin, so every phone must re-add the app (§2.5). |
| **Not chosen** | **Vercel Hobby (your suggestion) is a close second.** It loses on latency from Dhaka, on its 30-day lockout, its non-commercial clause and its preview origins (§1.2). Netlify Free loses on credits (≈19 production deploys/month). GitHub Pages needs a public repo and has no custom headers. Cloudflare Pages works, but Cloudflare now says "Start new projects with Workers". |
| **CI/CD** | **GitHub Actions is the single control plane** (private repo on your personal account). It has three workflows: `ci.yml` (all checks), `deploy-web.yml` (main → production) and `deploy-rules.yml` (PR → dry run, main → deploy). Every action is pinned to a SHA that runs on Node 24. Every CLI is pinned to an exact version at least two weeks old. |
| **PR previews** | **Off by default.** Cloudflare Version URLs are public, and protecting them with Cloudflare Access needs a card (r7c C6). Previews are opt-in with one repository variable (§4.3). |
| **Secrets** | Two required: `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID`. A third, `GCP_SA_KEY`, is needed only if keyless Workload Identity Federation can't be used. Everything else is a non-secret repository **Variable** (§4.6). |
| **Code changes** | Two lines in `web/vite.config.ts` (the precache list). New files: `web/wrangler.jsonc`, `web/public/_headers`, `web/public/.assetsignore`, `tools/render-rules.sh` and the workflows. `firebase.json` loses its `database` block, so rules deploy only through `tools/render-rules.sh` (§3, §6.4). |
| **Firebase side** | No authorized-domain change. Use **two API keys**: a browser key restricted to the web origins, and an unrestricted device key for the ESP32 and the e-mail relay (§6.2). No App Check. |
| **The one irreversible step** | Choosing the Cloudflare **account subdomain** `<acct>` and the Worker name. Together they *are* the PWA origin. **Do not tap "Add to Home Screen" until §7.1 steps 8–10 pass (step 11 is the install).** |

```
 Mac ──git push──► GitHub (private repo, personal account)
                     │
     ┌───────────────┼──────────────────────────┬──────────────────────────────────┐
     ▼               ▼                          ▼                                  ▼
  ci.yml          deploy-web.yml             deploy-rules.yml                 (manual, Mac)
  checks ─► web   gen_codes --check          render UIDs ─► .deploy/          npx wrangler / firebase-tools
         └► fw    typecheck ─► build         WIF login (no key)               (§5)
                  npx wrangler deploy        firebase deploy --only database
                     │                          │
                     ▼                          ▼
     Cloudflare edge (DAC / CGP PoPs)      Firebase RTDB asia-southeast1 (rules)
     solar.<acct>.workers.dev             ▲               ▲
                     │ shell once,              │ wss/https     │ HTTPS REST (device key)
                     ▼ then SW cache            │               │
               iPhone PWA ──────────────────────┘           ESP32 logger
               (Auth: identitytoolkit / securetoken.googleapis.com, browser key)
```

---

## 1. Primary and fallback: the decision and why

### 1.1 Comparison against the alternatives

All facts below come from r7c (verified) unless marked otherwise. The latency figures are r7b's [OBSERVED] samples from this Mac on a Bangladeshi network (`colo=DAC`), 5 per host.

| Criterion | **Cloudflare Workers static assets** (PRIMARY) | **Firebase Hosting, Spark** (FALLBACK) | Vercel Hobby (your suggestion) | Cloudflare Pages | Netlify Free (credits) | GitHub Pages |
|---|---|---|---|---|---|---|
| Price / card | $0. Static requests "free and unlimited"; asset storage free. Card not asked [INFERRED] | $0. 10 GB storage; 360 MB/day ≈ 10 GB/month transfer | $0. 100 GB transfer, 1M CDN requests/month. Card not asked [INFERRED] | $0. Static requests unlimited; 500 builds/month | $0. 300 credits/month; a production deploy costs 15 | $0 |
| At the limit | Static assets are never metered. Only Worker *scripts* have a 100k/day cap, and this Worker has none | Short grace period, then the site is **disabled** until next month | "Wait until 30 days have passed"; Hobby can't buy more | 500 builds/month cap | **All** sites paused: "Site not available" | Soft limits, then 429 |
| Use restrictions | None in the pages reviewed [INFERRED] | None | **"Non-commercial personal use only"** | None [INFERRED] | None | Free plan needs a **public** repo. The docs say "shouldn't be used for sensitive transactions like sending passwords" |
| Private repo → deploy | ✅ via Actions + API token | ✅ via Actions | ✅ only if the repo is under your personal account, not an org | ✅ | ✅ personal only | ❌ on Free |
| Edge from Dhaka (TTFB, observed) | **49–58 ms** (`cf-ray …-DAC`) | 52–55 ms (Fastly Kolkata) | 177–196 ms (`bom1` Mumbai; "the Vercel region … handles TLS") | Same as Workers | 155–222 ms | 53–61 ms |
| Headers / CSP / SPA as code | ✅ `_headers` + `not_found_handling` (tested, §3.9) | ✅ `firebase.json` (already in the repo) | ✅ `vercel.json` | ✅ `_headers` | ✅ | ❌ no custom headers |
| PR previews | Public Version URLs; protecting them needs Access, which needs a card → **off** | Public channels that expire | ✅ **Best:** automatic, and Vercel Authentication can protect them on Hobby | Public by default | Free and unmetered, but no password on Free | None |
| Preview origins safe to whitelist on the API key | ✅ `*.<acct>.workers.dev` is yours alone | ❌ would need `*.web.app` | ❌ would need `*.vercel.app` | ✅ `*.savx-logger.pages.dev` | ❌ | – |
| New vendor | +1 (Cloudflare) | 0 | +1 | +1 | +1 | 0 |
| Platform direction | Cloudflare's main path ("Start new projects with Workers") | Stable; already configured | Hobby unchanged since 2024 | Maintained, no longer the recommended start | Credit plans since 2025-09 | Stable |
| **Verdict** | **Pick** | **Standby** | Runner-up | Equivalent; lost on direction | Reject | Reject |

### 1.2 Why Cloudflare beats Vercel for this user

Vercel Hobby would work. The app is personal and non-commercial, so it is within Vercel's terms, and r7b §3.4 and §5.2 hold a complete `vercel.json` and workflow. Cloudflare still wins on the points that matter for a family iPhone app used from Dhaka:

1. **Latency where the users are.** Cloudflare answers from Dhaka at 49–58 ms TTFB. Vercel routes to Mumbai at 177–196 ms, because the Vercel region terminates TLS [CONFIRMED-primary, r7c #15]. That sample was vercel.com, a dynamic site, so treat it as indicative. The gap shows on the first install (about 130 KB gzip) and on every app update.
2. **No lockout cliff.** Vercel Hobby over a limit means waiting 30 days with no option to pay. Cloudflare static-asset requests are not metered at all. Usage would be about 0.5 GB/month (r7a §1), far inside both, but only one of them has no cliff.
3. **No terms risk.** Hobby is "non-commercial personal use only". If the logger ever helps a relative's shop, that is a grey zone. The Cloudflare pages reviewed carry no such clause [INFERRED].
4. **Previews fit the security model.** Vercel previews live on the shared `*.vercel.app`, so the browser key can't safely allow them (r7b §1.3). The Vercel Toolbar on previews also needs CSP holes (`vercel.live`, `wss://ws-us3.pusher.com`). Cloudflare previews live under your own `<acct>.workers.dev`.
5. **Same pipeline effort.** Both use config-as-code and one `npx` CLI call from Actions, so Vercel's DX advantage disappears once CI is GitHub Actions.

**Where Vercel is better:** free *protected* PR previews (Vercel Authentication on Hobby). This project turns previews off: there is a single owner, and every preview would use the production Firebase project. So that advantage does not apply.

If you still prefer Vercel, decide **before** installing the app on any phone, because switching later changes the origin (§2). Use r7b §3.4 `vercel.json` and r7b §5.2 `web-vercel.yml`, with the r7c-verified pins and the `X-Robots-Tag` header added.

### 1.3 Why Firebase Hosting is the fallback (and not Pages or Vercel)

- It is **already configured** (`firebase.json`), adds **no new vendor**, and is the second-fastest from Dhaka (Fastly Kolkata, 52–55 ms) (r7c "Missed options" 5).
- It **stops instead of billing** when over quota, and needs no new account.
- It is a *cold* standby. A fallback on any other host is also a new origin, so it can't take over an installed PWA transparently. Its value is "one command away if Cloudflare is gone", not instant failover.
- **Short Cloudflare outages need no action.** The 2025-12-05 outage lasted about 25 minutes [CONFIRMED-primary]. The service worker serves the app shell from cache after a 4 s network timeout, and live data comes straight from Firebase. An outage only blocks first installs and updates.

### 1.4 What verification changed (r7c → this decision)

| r7c item | Effect here |
|---|---|
| C1: Node 20 actions removed on 2026-09-23; wrangler-action is now v4 | Only SHA-pinned node24 actions are used (§4). The deploy runs `npx wrangler@4.146.0` directly; no third-party deploy action is used. |
| C2/C3: preview mechanisms; Version URLs public by default | `"preview_urls": false` is set explicitly. PR previews are opt-in through `versions upload --preview-alias` (§4.3). |
| C4: `_headers` precached, `/index.html` 307 | The two `vite.config.ts` edits (§3.4). The 307 was confirmed locally (§3.9). |
| C5: authorized domains overstated | No authorized-domain change (§6.1). |
| C6: Access needs a card | Previews stay off. |
| C11: manifest `Content-Type` line unnecessary | Dropped from `_headers` (wrangler sets `application/manifest+json`, observed §3.9). |
| Missed 3: `workers.dev` is on the Public Suffix List | Keep this Cloudflare account for this app (risk H15). |
| Missed 6: the origin is permanent | §2 and the gate in §7. |

---

## 2. Origin and domain plan

### 2.1 The final origin

```
https://solar.<acct>.workers.dev
        └────┬────┘ └─┬─┘
     Worker "name"   Cloudflare account subdomain (set once in the dashboard)
     (wrangler.jsonc)
```

The URL format is `<WORKER_NAME>.<ACCOUNT_SUBDOMAIN>.workers.dev`. You change the account subdomain under **Workers & Pages → Your subdomain → Change** [CONFIRMED-primary]. The docs do not say what happens to existing URLs; assume they all change at once [INFERRED].

**Choosing `<acct>`:**
- Use lowercase letters, digits and hyphens, and make it short and neutral, for example `jr-solar`. That would give `https://solar.<your-subdomain>.workers.dev`.
- Do **not** put your address, phone number or family names in it. The hostname is public: it appears in certificate logs and in links you share [INFERRED].
- Write the chosen origin into `docs/site-survey.md` (build record) **before** the first deploy.

### 2.2 What is bound to the origin

The origin owns all of the following:

- The Home Screen icon's `start_url`.
- The service worker and Cache Storage.
- The Firebase Auth session (IndexedDB).
- The history cache (`lib/idb.ts`).
- RTDB's cached host and transport flags (localStorage).

There is **no migration across origins** (r7b §6). A changed origin means that on every phone you delete the icon, add it again, sign in again, and download history again.

### 2.3 What changes the origin (and what doesn't)

| Action | Origin changes? |
|---|---|
| New deploys, rollbacks, `wrangler.jsonc` edits other than `name` | No |
| Renaming the Worker (`"name"`) | **Yes** |
| Changing the account subdomain | **Yes** [INFERRED] |
| Adding a custom domain later | **Yes**. It also needs a bought domain on Cloudflare nameservers ("Workers does not support any domain whose nameservers are not managed by Cloudflare" [CONFIRMED-primary]). That breaks the zero-cost rule, so it is not planned. |
| Failing over to Firebase Hosting | **Yes** (`<project-id>.web.app`) |
| Switching to Vercel or Pages later | **Yes** |

### 2.4 Rules that follow from this

1. Pick `<acct>` before anything else (§7 phase 1). Never change it.
2. The Worker name `savx-logger` is final. It is written in `web/wrangler.jsonc` with a "never rename" comment.
3. Test with `?demo=1` in Safari **without** adding it to the Home Screen until §7.1 steps 8–10 are green.
4. `start_url`, `scope` and `id` stay `/`. The app is hosted at the origin root, which is already true in `manifest.webmanifest`.

### 2.5 Fallback origin

The fallback is `https://<project-id>.web.app` (the `<project-id>.firebaseapp.com` alias also works). It is already in the browser key's referrer list (§6.2), so switching needs no Google Cloud change. It does mean every phone re-installs the app (§7.3 C).

---

## 3. Config files (exact content)

### 3.1 `web/wrangler.jsonc` (new)

There is no `main` key: "`main` is optional for assets-only Workers" [CONFIRMED-primary, r7b §3.4]. `compatibility_date` is `2026-04-01`, for two reasons. Any date on or after 2025-04-01 gives the documented SPA navigation behaviour [CONFIRMED-primary, r7c #4], and this date also works with wrangler 4.86.0, the Node-20 fallback in §5.2.

Both wrangler 4.146.0 (Node 24.21.0) and 4.86.0 (Node 20.17.0) pass `deploy --dry-run` on this file [OBSERVED].

```jsonc
{
  "$schema": "https://unpkg.com/wrangler@4.146.0/config-schema.json",
  // Assets-only Worker: no "main" script, so requests never count against the
  // Workers Free script quota (100,000/day). Static-asset requests are free.
  // The name is part of the PWA origin (solar.<account-subdomain>.workers.dev):
  // never rename it after the app is on a Home Screen.
  "name": "savx-logger",
  "compatibility_date": "2026-04-01",
  "workers_dev": true,
  "preview_urls": false,            // Version URLs are public by default: keep them off
  "assets": {
    "directory": "./dist",
    "not_found_handling": "single-page-application",  // unknown paths -> /index.html (200)
    "html_handling": "auto-trailing-slash"            // the default; /index.html -> 307 -> /
  }
}
```

### 3.2 `web/public/_headers` (new)

Vite copies it to `dist/`, and Cloudflare reads it but never serves it. The limits are 100 rules and 2,000 characters per line; values from several matching rules are comma-joined [CONFIRMED-primary]. That is why every path sets `Cache-Control` in exactly one rule.

`wrangler dev` reported "Parsed 7 valid header rules", so the `#` comment lines are accepted [OBSERVED-local].

```
# Cloudflare Workers static assets: response headers (https://developers.cloudflare.com/workers/static-assets/headers/)
# Not served itself. Max 100 rules, 2,000 chars per line. Set Cache-Control in exactly one rule per path.
/*
  Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app; manifest-src 'self'; worker-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'
  Strict-Transport-Security: max-age=63072000; includeSubDomains
  Referrer-Policy: strict-origin-when-cross-origin
  Permissions-Policy: camera=(), microphone=(), geolocation=(), payment=(), usb=()
  X-Content-Type-Options: nosniff
  X-Frame-Options: DENY
  Cross-Origin-Opener-Policy: same-origin
  X-Robots-Tag: noindex, nofollow

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

### 3.3 `web/public/.assetsignore` (new)

"Wrangler will not upload asset files that match lines in this file", which uses `.gitignore` syntax and sits in the root of the assets directory [CONFIRMED-primary]. It keeps macOS junk such as `.DS_Store` (which Vite copies from `public/`) off the CDN. `publicFiles()` already skips dotfiles, so it is never precached.

```
# Files in the assets directory that wrangler must never upload (.gitignore syntax).
.DS_Store
```

### 3.4 `web/vite.config.ts`: two required edits (r7c C4)

These edits are required on **every** host. The current build still lists `"/index.html"` in `dist/sw.js` [OBSERVED].

```diff
@@ function publicFiles(dir: string): string[] {
-      if (name.startsWith('.')) continue;
+      if (name.startsWith('.') || name.startsWith('_')) continue; // dotfiles, _headers, _redirects: host config, never served
@@ generateBundle(_options, bundle) {
-      const urls = Array.from(new Set(['/', '/index.html', ...built, ...publicFiles(resolve(root, 'public'))])).sort();
+      const urls = Array.from(new Set(['/', ...built, ...publicFiles(resolve(root, 'public'))])).sort();
```

Also update the comment above `serviceWorker()` to say "(hashed /assets/*, icons, manifest, `/`)".

- **Why the first edit:** without it, `cache.addAll` would precache `/_headers`, which the host answers with the SPA `index.html`.
- **Why the second edit:** `/index.html` answers 307 → `/` on Workers (§3.9). A precached redirect can break offline launch. The service worker's fallback, `cache.match('/index.html') || cache.match('/')`, keeps working unchanged.

I applied both edits to a scratch copy, and `tsc --noEmit` and `npm run build` passed. The resulting `sw.js` precaches `/`, 8 hashed assets, 4 icons and the manifest, with no `/_headers`, `/.assetsignore` or `/index.html` [OBSERVED].

### 3.5 `firebase.json` (replace): fallback hosting only

This keeps the `hosting` block as the cold standby, at parity with `_headers`. Firebase already sends HSTS itself (r7b §3.4).

It **removes the `database` block.** Rules now deploy only through `tools/render-rules.sh` → `.deploy/` (§3.6). A stray `firebase deploy` at the repo root therefore can never push the placeholder rules. Those would lock out the logger and the owner, because no UID equals `OWNER_UID`.

```json
{
  "hosting": {
    "public": "web/dist",
    "ignore": ["firebase.json", "**/.*", "**/node_modules/**", "**/_headers", "**/_redirects"],
    "cleanUrls": false,
    "trailingSlash": false,
    "rewrites": [{ "source": "**", "destination": "/index.html" }],
    "headers": [
      { "source": "/assets/**", "headers": [{ "key": "Cache-Control", "value": "public, max-age=31536000, immutable" }] },
      { "source": "/icons/**", "headers": [{ "key": "Cache-Control", "value": "public, max-age=604800" }] },
      { "source": "/sw.js", "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "/", "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "**/*.html", "headers": [{ "key": "Cache-Control", "value": "no-cache" }] },
      { "source": "**/*.webmanifest", "headers": [
        { "key": "Cache-Control", "value": "no-cache" },
        { "key": "Content-Type", "value": "application/manifest+json" }
      ] },
      { "source": "**", "headers": [
        { "key": "Content-Security-Policy", "value": "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app; manifest-src 'self'; worker-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'" },
        { "key": "Referrer-Policy", "value": "strict-origin-when-cross-origin" },
        { "key": "Permissions-Policy", "value": "camera=(), microphone=(), geolocation=(), payment=(), usb=()" },
        { "key": "X-Content-Type-Options", "value": "nosniff" },
        { "key": "X-Frame-Options", "value": "DENY" },
        { "key": "Cross-Origin-Opener-Policy", "value": "same-origin" },
        { "key": "X-Robots-Tag", "value": "noindex, nofollow" }
      ] }
    ]
  }
}
```

### 3.6 `tools/render-rules.sh` (new)

The committed `firebase/database.rules.json` keeps the `OWNER_UID` and `DEVICE_UID` placeholders. This script writes a filled-in copy plus a minimal `firebase.json` into the gitignored `.deploy/` folder. CI (§4.4) and the Mac (§5.3) both use it.

Checks run on it [OBSERVED]:
- `shellcheck -s sh` is clean, and `dash -n` parses it.
- On the real rules file it substitutes 5 lines.
- It rejects a UID containing `/`, an empty UID, and identical UIDs.

```sh
#!/bin/sh
# Render firebase/database.rules.json with the real Auth UIDs into .deploy/ (gitignored).
# The committed rules keep the OWNER_UID / DEVICE_UID placeholders (firebase/README.md section 3).
#
#   OWNER_UID=<owner uid> DEVICE_UID=<device uid> sh tools/render-rules.sh
#   cd .deploy && npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id>
set -eu
: "${OWNER_UID:?set OWNER_UID (Firebase console > Authentication > Users > User UID)}"
: "${DEVICE_UID:?set DEVICE_UID (Auth user of the logger)}"
for uid in "$OWNER_UID" "$DEVICE_UID"; do
  case "$uid" in
    *[!A-Za-z0-9]*) echo "render-rules: a UID may only contain letters and digits: '$uid'" >&2; exit 1 ;;
  esac
done
if [ "$OWNER_UID" = "$DEVICE_UID" ]; then echo "render-rules: OWNER_UID and DEVICE_UID must differ" >&2; exit 1; fi

cd "$(dirname "$0")/.."
src=firebase/database.rules.json
out=.deploy
if ! grep -q OWNER_UID "$src" || ! grep -q DEVICE_UID "$src"; then
  echo "render-rules: $src has no placeholders (real UIDs committed?)" >&2; exit 1
fi
mkdir -p "$out"
sed -e "s/OWNER_UID/${OWNER_UID}/g" -e "s/DEVICE_UID/${DEVICE_UID}/g" "$src" > "$out/database.rules.json"
if grep -q 'OWNER_UID\|DEVICE_UID' "$out/database.rules.json"; then echo "render-rules: placeholder left" >&2; exit 1; fi
python3 -m json.tool "$out/database.rules.json" > /dev/null
printf '{\n  "database": { "rules": "database.rules.json" }\n}\n' > "$out/firebase.json"
echo "render-rules: wrote $out/database.rules.json and $out/firebase.json"
```

### 3.7 `.gitignore` and `web/.env.example`

Add to the root `.gitignore`:

```gitignore
# Hosting / CI (research/r7-web-hosting.md)
.deploy/
.wrangler/
web/.wrangler/
gha-creds-*.json
```

Change the top comment of `web/.env.example` to say:

```
# VITE_FIREBASE_API_KEY is the BROWSER key (HTTP-referrer restricted, research/r7-web-hosting.md §6.2).
# For `npm run dev` on localhost or the LAN, put the DEVICE key in web/.env.local instead (no referrer needed).
# CI does not read this file: deploy-web.yml takes the values from GitHub repository Variables.
```

### 3.8 The Content Security Policy, line by line

This is r7b's policy. It was tested in Chromium with zero violations, a live `wss` handshake to an asia-southeast1 host, and injected JSONP and foreign `fetch` blocked [OBSERVED, r7b §3.1]. In CSP3, `*.` matches subdomains at any depth.

| Directive | Value | Why |
|---|---|---|
| `default-src` | `'self'` | Everything is first-party. Firebase is bundled, not loaded from a CDN. |
| `script-src` | `'self'` | No inline scripts or `eval` in the build. RTDB's JSONP long-poll fallback is **deliberately blocked** (risk H10). |
| `style-src` | `'self'` | CSS files only. Preact and uPlot set styles through the CSSOM, which CSP does not block. |
| `img-src` | `'self' data: blob:` | Icons, chart and CSV blobs |
| `connect-src` | `'self' https://identitytoolkit.googleapis.com https://securetoken.googleapis.com https://*.asia-southeast1.firebasedatabase.app wss://*.asia-southeast1.firebasedatabase.app` | Auth sign-in and token refresh. Also your RTDB host plus the `s-gke-apse1-…` hosts the handshake redirects to (r7b §1.4). |
| `manifest-src`, `worker-src` | `'self'` | `/manifest.webmanifest`, `/sw.js` |
| `frame-src`, `object-src` | `'none'` | No iframes, because `initializeAuth` is used without the popup resolver (r7b §1.2). |
| `base-uri`, `form-action`, `frame-ancestors` | `'none'` | Forms are handled in JS (`preventDefault`). The app cannot be embedded; `X-Frame-Options: DENY` is the legacy twin. |

**E-mail alerts (r8):** the PWA only reads and writes RTDB (`cfg/...`, test-mail request), so the CSP is unchanged. If a later design calls the Apps Script relay *from the browser*, add `https://script.google.com https://script.googleusercontent.com` to `connect-src` in both `_headers` and `firebase.json`.

### 3.9 Cache policy, verified locally

I ran `wrangler dev` 4.146.0 on Node 24.21.0, serving a scratch build that had the §3.1–3.4 changes [OBSERVED-local]:

| Request | Status | `Cache-Control` | `Content-Type` |
|---|---|---|---|
| `/` | 200 | `no-cache` | `text/html; charset=utf-8` |
| `/index.html` | **307 → `/`** | `no-cache` | – |
| `/sw.js` | 200 | `no-cache` | `text/javascript` |
| `/manifest.webmanifest` | 200 | `no-cache` | `application/manifest+json` (no `_headers` line needed) |
| `/assets/index-<hash>.js` | 200 | `public, max-age=31536000, immutable` | `text/javascript` |
| `/icons/icon-192.png` | 200 | `public, max-age=604800` | `image/png` |
| `/history` (deep link) | 200, index.html | `public, max-age=0, must-revalidate` (platform default) | `text/html` |
| `/_headers`, `/.assetsignore` | 200, index.html (the files are **not** served) | default | `text/html` |
| `/assets/missing.js` (navigate, no-cors, or no `Sec-Fetch-*` header) | **200, index.html** | **`immutable`** (path rule applies to the fallback) | `text/html` |

Every response checked carried the CSP, HSTS, Permissions-Policy, `nosniff` and `X-Robots-Tag`; `/` also showed `X-Frame-Options`, `Referrer-Policy` and COOP (all from the single `/*` rule).

**Finding: the SPA fallback is served with the immutable cache header.** With `not_found_handling: "none"` the missing asset becomes a 404, but still with `immutable`, so the alternative does not fix this.

**Effect:** a browser that asks for a hashed file absent from the live version caches HTML (or a 404) under that URL. That only matters after a rollback to the version that contains the file. It self-heals because the service worker precaches with `cache: 'reload'` (risk H11).

**Why the SPA fallback is kept:**
- Routing is hash-based (`/#/today`, `app.tsx`), so it is not strictly needed.
- It keeps parity with `firebase.json` and d1, and tolerates stray deep links.

Re-check these headers on the real edge after the first deploy (§7.2).

---

## 4. CI/CD (GitHub Actions)

### 4.1 Overview

| Workflow | Trigger | What it does | Credentials | Typical billed minutes |
|---|---|---|---|---|
| `ci.yml` | Push to `main` and every PR. Docs-only changes are skipped. Also manual. | `checks` (change detection, `gen_codes.py --check`, rules lint) → `web` (typecheck + build + dist checks) and `firmware` (`pio run -e savx`, `pio test -e native`), each only if its area changed | none | 1 + 2 + 5 (warm cache) |
| `deploy-web.yml` | Push to `main` touching `web/`, `shared/` or `tools/gen_codes.py`; manual. PRs only if `CF_PREVIEWS == 'true'`. | Gate (variables, codes, typecheck, build, dist), then `wrangler deploy` (main) or `versions upload --preview-alias pr-N` (opt-in PR) | `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` | 2 |
| `deploy-rules.yml` | Push to `main` / PR touching the rules or the render script; manual | Render UIDs → keyless Google auth → `firebase deploy --only database` (`--dry-run` on PRs) | WIF (or `GCP_SA_KEY`) | 1–2 |

Ground rules (r7b §5.2, corrected by r7c):

- **Pinning.** Actions are pinned by commit SHA. I re-resolved each with `git ls-remote` today, and every one declares `using: node24` [OBSERVED].
- **CLIs** run through `npx --yes <cli>@<exact>`: wrangler 4.146.0 (Node ≥ 22) and firebase-tools 15.32.1 (Node ≥ 20). PlatformIO is installed with `pip install platformio==6.2.0`, the same version as the local scratch toolchain [OBSERVED].
- **Node 24 in CI.** Wrangler needs ≥ 22 and Firebase 13 asks for 24. Vite 6.4.4 accepts `^18 || ^20 || >=22` [CONFIRMED-primary, npm metadata]. The lockfile already contains the Linux x64 binaries for esbuild, rollup and TypeScript 7 [OBSERVED].
- **No `environment:`.** GitHub Free only allows environments on public repos. Deploys are gated with `if:` on `refs/heads/main`.
- **No required checks.** GitHub Free has no branch protection on private repos; protected branches in private repos come with Pro [CONFIRMED-primary, GitHub plans page]. CI is therefore advisory, and **each deploy workflow repeats its own gate** before deploying.
- **Lint.** `actionlint` 1.7.12 with `shellcheck` passes on all three files [OBSERVED].

### 4.2 `.github/workflows/ci.yml`

The change detection is plain git, with no third-party action. It diffs `origin/<base>...HEAD` for PRs and `before...HEAD` for pushes. If the base is missing (a new branch, a force-push, or a manual run), it checks everything.

I simulated it on a scratch repo:
- A push that touched only `web/` gave `web=true`, `firmware=false`.
- An all-zero `before` gave everything `true`.

[OBSERVED]

```yaml
# CI for every push to main and every pull request. Docs-only changes are skipped.
# checks (change detection, codes.ts freshness, RTDB rules lint) -> web, firmware (only if touched).
# research/r7-web-hosting.md section 4.2
name: ci

on:
  push:
    branches: [main]
    paths-ignore: ['**/*.md', 'docs/**', 'research/**', 'reference/**']
  pull_request:
    paths-ignore: ['**/*.md', 'docs/**', 'research/**', 'reference/**']
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}

jobs:
  checks:
    name: changes, codes, rules
    runs-on: ubuntu-latest
    timeout-minutes: 5
    outputs:
      web: ${{ steps.changes.outputs.web }}
      firmware: ${{ steps.changes.outputs.firmware }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          fetch-depth: 0            # full history, so the diff base below exists
          persist-credentials: false

      - name: Detect changed areas
        id: changes
        env:
          EVENT: ${{ github.event_name }}
          BASE_REF: ${{ github.base_ref }}
          BEFORE: ${{ github.event.before }}
        run: |
          base=""
          if [ "$EVENT" = pull_request ]; then base="origin/$BASE_REF"; fi
          if [ "$EVENT" = push ]; then base="$BEFORE"; fi
          if [ -z "$base" ] || ! git rev-parse --quiet --verify "${base}^{commit}" > /dev/null; then
            echo "No usable diff base for '$EVENT': checking everything."
            { echo "web=true"; echo "firmware=true"; } >> "$GITHUB_OUTPUT"
            exit 0
          fi
          files="$(git diff --name-only "$base"...HEAD)"
          printf '%s\n' "$files"
          has() { if grep -qE "$1" <<< "$files"; then echo true; else echo false; fi; }
          {
            echo "web=$(has '^(web/|shared/|tools/gen_codes\.py|\.github/workflows/ci\.yml)')"
            echo "firmware=$(has '^(firmware/|shared/|\.github/workflows/ci\.yml)')"
          } >> "$GITHUB_OUTPUT"

      - name: web/src/codes.ts is up to date with shared/codes.json
        run: python3 tools/gen_codes.py --check

      - name: RTDB rules lint (valid JSON, placeholders kept, no open rules, renders)
        run: |
          rules=firebase/database.rules.json
          python3 -m json.tool "$rules" > /dev/null
          if ! grep -q OWNER_UID "$rules" || ! grep -q DEVICE_UID "$rules"; then
            echo "::error file=$rules::OWNER_UID / DEVICE_UID placeholders missing. Keep them in git; deploy-rules fills them in."
            exit 1
          fi
          if grep -nE '"\.(read|write)"[[:space:]]*:[[:space:]]*(true|"true"|"auth != null")' "$rules"; then
            echo "::error file=$rules::open rule (true, or a bare \"auth != null\"). See firebase/README.md."
            exit 1
          fi
          if python3 -c 'import json, sys; sys.exit(0 if "database" in json.load(open("firebase.json")) else 1)'; then
            echo "::error file=firebase.json::no database block here: rules deploy only through tools/render-rules.sh (.deploy/)"
            exit 1
          fi
          OWNER_UID=ciOwnerUid0000000000000000000 DEVICE_UID=ciDeviceUid000000000000000000 sh tools/render-rules.sh

  web:
    name: web typecheck + build
    needs: checks
    if: needs.checks.outputs.web == 'true'
    runs-on: ubuntu-latest
    timeout-minutes: 10
    defaults:
      run:
        working-directory: web
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: web/package-lock.json

      - run: npm ci
      - run: npm run typecheck
      - name: Build (no VITE_* needed; a missing Firebase config only shows the in-app setup notice)
        run: npm run build

      - name: Check dist (host files present, service-worker precache list)
        run: |
          for f in index.html sw.js manifest.webmanifest _headers .assetsignore; do
            test -s "dist/$f" || { echo "::error::dist/$f missing"; exit 1; }
          done
          if grep -qE '"/(_headers|_redirects|index\.html|\.assetsignore)"' dist/sw.js; then
            echo "::error::sw.js must not precache /_headers, /_redirects, /.assetsignore or /index.html (vite.config.ts)"
            exit 1
          fi
          if awk 'length > 2000 { bad = 1 } END { exit !bad }' dist/_headers; then
            echo "::error::a _headers line is longer than 2,000 characters"; exit 1
          fi

  firmware:
    name: firmware build + native tests
    needs: checks
    if: needs.checks.outputs.firmware == 'true'
    runs-on: ubuntu-latest
    timeout-minutes: 30
    defaults:
      run:
        working-directory: firmware
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97 # v7.0.0
        with:
          python-version: '3.13'

      - name: Cache PlatformIO platform, toolchain and libraries
        uses: actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9 # v6.1.0
        with:
          path: |
            ~/.platformio/platforms
            ~/.platformio/packages
            firmware/.pio/libdeps
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
          restore-keys: |
            pio-${{ runner.os }}-

      - name: Install PlatformIO Core 6.2.0
        run: python -m pip install --disable-pip-version-check "platformio==6.2.0"

      - name: Build ESP32-S3 firmware (env savx)
        run: pio run -e savx

      - name: Host unit tests (env native)
        run: pio test -e native
```

The **`.assetsignore`** check needs §3.3. The **precache** check needs §3.4: the current `web/dist/sw.js` would fail it, and that is intended. The rules lint passed on the current `firebase/database.rules.json`. When I replaced one rule with a bare `"auth != null"`, it failed as expected (5 hits) [OBSERVED].

### 4.3 `.github/workflows/deploy-web.yml`

```yaml
# Build the PWA and deploy it to Cloudflare Workers static assets.
#   push to main / manual run on main -> production (https://solar.<account-subdomain>.workers.dev)
#   pull request                      -> nothing, unless repository variable CF_PREVIEWS == 'true'
#                                        (then a PUBLIC preview URL; needs "preview_urls": true)
# research/r7-web-hosting.md section 4.3
name: deploy-web

on:
  push:
    branches: [main]
    paths: ['web/**', '!web/**/*.md', 'shared/**', 'tools/gen_codes.py', '.github/workflows/deploy-web.yml']
  pull_request:
    paths: ['web/**', '!web/**/*.md', 'shared/**', 'tools/gen_codes.py', '.github/workflows/deploy-web.yml']
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: deploy-web-${{ github.event_name == 'pull_request' && format('pr-{0}', github.event.pull_request.number) || 'production' }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}

env:
  WRANGLER_VERSION: 4.146.0      # needs Node >= 22; published 2026-10-01
  WRANGLER_SEND_METRICS: 'false'

jobs:
  deploy:
    # PRs from forks never get secrets; PR previews are opt-in.
    if: >-
      github.event_name != 'pull_request' ||
      (vars.CF_PREVIEWS == 'true' && github.event.pull_request.head.repo.full_name == github.repository)
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
      IS_PROD: ${{ github.event_name != 'pull_request' && github.ref == 'refs/heads/main' }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: web/package-lock.json

      - name: Require the build variables
        run: |
          for v in VITE_FIREBASE_API_KEY VITE_FIREBASE_AUTH_DOMAIN VITE_FIREBASE_PROJECT_ID \
                   VITE_FIREBASE_APP_ID VITE_FIREBASE_DATABASE_URL VITE_DEVICE_ID; do
            [ -n "${!v}" ] || { echo "::error::repository variable $v is not set"; exit 1; }
          done
          case "$VITE_FIREBASE_DATABASE_URL" in
            https://*.asia-southeast1.firebasedatabase.app) ;;
            *) echo "::error::VITE_FIREBASE_DATABASE_URL must be the asia-southeast1 URL (the CSP only allows that region)"; exit 1 ;;
          esac

      - name: codes.ts is up to date
        working-directory: .
        run: python3 tools/gen_codes.py --check

      - run: npm ci
      - run: npm run typecheck
      - run: npm run build

      - name: Check dist
        run: |
          for f in index.html sw.js manifest.webmanifest _headers; do
            test -s "dist/$f" || { echo "::error::dist/$f missing"; exit 1; }
          done
          if grep -qE '"/(_headers|_redirects|index\.html|\.assetsignore)"' dist/sw.js; then
            echo "::error::sw.js precache list contains host files or /index.html"; exit 1
          fi

      - name: Deploy to production
        if: env.IS_PROD == 'true'
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          CLOUDFLARE_ACCOUNT_ID: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
        run: |
          set -o pipefail
          short="${GITHUB_SHA::12}"
          npx --yes "wrangler@${WRANGLER_VERSION}" deploy \
            --tag "$short" --message "main@${short} run ${GITHUB_RUN_ID}" 2>&1 | tee deploy.log
          url="$(grep -oE 'https://[a-z0-9.-]+\.workers\.dev' deploy.log | head -n 1 || true)"
          echo "Production: ${url:-see log} (commit ${short})" >> "$GITHUB_STEP_SUMMARY"

      - name: Upload a PR preview version (opt-in, PUBLIC URL, production Firebase)
        if: github.event_name == 'pull_request'
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          CLOUDFLARE_ACCOUNT_ID: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          PR: ${{ github.event.pull_request.number }}
        run: |
          set -o pipefail
          if ! grep -qE '"preview_urls"[[:space:]]*:[[:space:]]*true' wrangler.jsonc; then
            echo "::error::CF_PREVIEWS is 'true' but web/wrangler.jsonc has \"preview_urls\": false"; exit 1
          fi
          npx --yes "wrangler@${WRANGLER_VERSION}" versions upload \
            --preview-alias "pr-${PR}" --tag "pr-${PR}" --message "PR #${PR} ${GITHUB_SHA::12}" 2>&1 | tee preview.log
          url="$(grep -oE 'https://[a-z0-9.-]+\.workers\.dev' preview.log | tail -n 1 || true)"
          echo "Preview (public, uses production Firebase): ${url:-see log}" >> "$GITHUB_STEP_SUMMARY"
```

**Turning PR previews on (optional, not recommended):**
1. Set `"preview_urls": true` in `web/wrangler.jsonc`. This also makes each production version reachable at its own public Version URL.
2. Add the repository variable `CF_PREVIEWS = true`.
3. To sign in on a preview, add `*.<acct>.workers.dev/*` to the browser key (§6.2).

Each PR then gets `https://pr-<N>-solar.<acct>.workers.dev`. Previews are **public** and use the **production** Firebase project: same data, same rules, and a separate sign-in.

r7b's `wrangler preview` alternative is labelled "[open beta]" in `wrangler preview --help` [OBSERVED], so it is not used.

### 4.4 `.github/workflows/deploy-rules.yml`

```yaml
# Deploy firebase/database.rules.json (UIDs filled in from repository variables).
#   pull request          -> server-side dry run (validates syntax against the real project)
#   push to main / manual -> deploy
# Credential: keyless Workload Identity Federation (preferred) or a service-account key secret.
# research/r7-web-hosting.md section 4.4
name: deploy-rules

on:
  push:
    branches: [main]
    paths: ['firebase/database.rules.json', 'tools/render-rules.sh', '.github/workflows/deploy-rules.yml']
  pull_request:
    paths: ['firebase/database.rules.json', 'tools/render-rules.sh', '.github/workflows/deploy-rules.yml']
  workflow_dispatch:

permissions:
  contents: read
  id-token: write   # OIDC token for Workload Identity Federation

concurrency:
  group: deploy-rules
  cancel-in-progress: false

env:
  FIREBASE_TOOLS_VERSION: 15.32.1   # Node >= 20; published 2026-09-30

jobs:
  rules:
    if: github.event_name != 'pull_request' || github.event.pull_request.head.repo.full_name == github.repository
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - uses: actions/setup-node@820762786026740c76f36085b0efc47a31fe5020 # v7.0.0
        with:
          node-version: 24

      - name: Render the rules with the real UIDs into .deploy/
        env:
          OWNER_UID: ${{ vars.FIREBASE_OWNER_UID }}
          DEVICE_UID: ${{ vars.FIREBASE_DEVICE_UID }}
        run: sh tools/render-rules.sh

      - name: Authenticate to Google Cloud
        uses: google-github-actions/auth@7c6bc770dae815cd3e89ee6cdf493a5fab2cc093 # v3.0.0
        with:
          project_id: ${{ vars.FIREBASE_PROJECT_ID }}
          workload_identity_provider: ${{ vars.GCP_WIF_PROVIDER }}
          service_account: ${{ vars.GCP_RULES_SA }}
          # Key fallback (section 4.6): delete the two lines above and use instead
          # credentials_json: ${{ secrets.GCP_SA_KEY }}

      - name: Dry run (PR / other branches) or deploy (main)
        working-directory: .deploy
        env:
          PROJECT: ${{ vars.FIREBASE_PROJECT_ID }}
          MODE: ${{ (github.event_name != 'pull_request' && github.ref == 'refs/heads/main') && 'deploy' || 'dry-run' }}
        run: |
          [ -n "$PROJECT" ] || { echo "::error::repository variable FIREBASE_PROJECT_ID is not set"; exit 1; }
          args=(deploy --only database --project "$PROJECT" --non-interactive)
          if [ "$MODE" != deploy ]; then args+=(--dry-run); fi
          npx --yes "firebase-tools@${FIREBASE_TOOLS_VERSION}" "${args[@]}"
          echo "RTDB rules: $MODE OK for $PROJECT at ${GITHUB_SHA::12}" >> "$GITHUB_STEP_SUMMARY"
```

`--dry-run` validates against the real project on the server (r7b §5.2, from the firebase-tools 15.32.1 source).

`roles/firebasedatabase.admin` is the narrowest predefined role that can update rules, but it can also read and write all data. That is why the credential is keyless WIF (r7b §5.2). If the CLI reports a missing permission such as `serviceusage…`, add exactly what it names.

### 4.5 `.github/dependabot.yml` (optional)

This file keeps the SHA pins and npm dependencies current with one PR per ecosystem per month. Vite majors are excluded because the Mac's Node 20.17 can't run Vite 7+ ([web/README.md](../web/README.md)).

```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: /
    schedule:
      interval: monthly
  - package-ecosystem: npm
    directory: /web
    schedule:
      interval: monthly
    ignore:
      # Vite 7+ needs Node >= 20.19; the Mac runs 20.17 (web/README.md). Bump by hand.
      - dependency-name: vite
        update-types: ['version-update:semver-major']
```

### 4.6 Repository secrets and variables

Set them in GitHub under **Settings → Secrets and variables → Actions**: secrets in the **Secrets** tab, everything else in the **Variables** tab. Variables are not masked in logs, which helps debugging. None of them are secret, because the `VITE_*` values ship inside the PWA anyway (r7b §2).

| Name | Kind | Value / where to get it | Used by |
|---|---|---|---|
| `CLOUDFLARE_API_TOKEN` | **Secret** | §4.6.1 | deploy-web |
| `CLOUDFLARE_ACCOUNT_ID` | **Secret** | §4.6.2 | deploy-web |
| `GCP_SA_KEY` | **Secret, fallback only** | §4.6.4 | deploy-rules |
| `VITE_FIREBASE_API_KEY` | Variable | The **browser** key: Firebase console → Project settings → General → Your apps → Web app → `apiKey` | deploy-web |
| `VITE_FIREBASE_AUTH_DOMAIN` | Variable | `<project-id>.firebaseapp.com` (unchanged off Firebase Hosting) | deploy-web |
| `VITE_FIREBASE_PROJECT_ID` | Variable | `<project-id>` | deploy-web |
| `VITE_FIREBASE_APP_ID` | Variable | `1:…:web:…` from the same panel | deploy-web |
| `VITE_FIREBASE_DATABASE_URL` | Variable | `https://<project-id>-default-rtdb.asia-southeast1.firebasedatabase.app` (the workflow rejects other regions) | deploy-web |
| `VITE_DEVICE_ID` | Variable | `inv1` | deploy-web |
| `FIREBASE_PROJECT_ID` | Variable | `<project-id>` | deploy-rules |
| `FIREBASE_OWNER_UID` | Variable | Firebase console → Authentication → Users → your **User UID** | deploy-rules |
| `FIREBASE_DEVICE_UID` | Variable | The logger's **User UID** (same list) | deploy-rules |
| `GCP_WIF_PROVIDER` | Variable | Printed by §4.6.3 | deploy-rules |
| `GCP_RULES_SA` | Variable | Printed by §4.6.3 | deploy-rules |
| `CF_PREVIEWS` | Variable, optional | `true` only to enable PR previews (§4.3) | deploy-web |

#### 4.6.1 `CLOUDFLARE_API_TOKEN`

1. In the Cloudflare dashboard, open the API tokens page and select **Create Token**. Cloudflare's GitHub Actions guide uses the account-owned "Account API tokens" page; a user token from My Profile → API Tokens also works.
2. Choose **Edit Cloudflare Workers** [CONFIRMED-primary](https://developers.cloudflare.com/workers/ci-cd/external-cicd/github-actions/).
3. Scope **Account Resources** to *your one account* only. The guide recommends limiting the token to the account you deploy to. If Zone Resources is required, choose all zones of that account; it has none.
4. Optional: set an end date about 12 months out and add a calendar reminder. An expired token makes `deploy-web` fail at the deploy step.
5. **Create**, copy the token (shown once), and save it as the repository secret `CLOUDFLARE_API_TOKEN`. If the dashboard shows a `curl …/tokens/verify` test command, run it.

> ⚠️ This token can replace the code on your origin. Malicious JS there could take over the signed-in owner session and change `cfg` or delete history. Keep it only in GitHub Secrets, turn on 2FA on GitHub and Cloudflare, and revoke and recreate it if it is ever exposed (risk H2).

#### 4.6.2 `CLOUDFLARE_ACCOUNT_ID`

Copy the 32-hex account ID from the dashboard (account menu → *Copy account ID*, or from the dashboard URL). See [Find account and zone IDs](https://developers.cloudflare.com/fundamentals/account/find-account-and-zone-ids/). After `wrangler login` (§5.1), `npx --yes wrangler@4.146.0 whoami` also prints it.

#### 4.6.3 Keyless Google credential (Workload Identity Federation), in Cloud Shell

Run this in **Cloud Shell**, so nothing is installed on the Mac. Open the Google Cloud console as the Firebase project owner and click the `>_` icon. "Cloud Shell is free for users with a Google Cloud account" [CONFIRMED-primary](https://cloud.google.com/shell/pricing).

Whether WIF works on a Spark project with no billing account is [UNVERIFIED]. IAM, STS and IAM Credentials are free APIs. If any step asks for billing, use §4.6.4 instead.

```bash
PROJECT_ID=<your-firebase-project-id>
REPO=<github-user>/<repo>                       # exact owner/name
gcloud config set project "$PROJECT_ID"
gcloud services enable iam.googleapis.com iamcredentials.googleapis.com sts.googleapis.com cloudresourcemanager.googleapis.com
gcloud iam service-accounts create gha-rtdb-rules --display-name="GitHub Actions: RTDB rules deploy"
SA="gha-rtdb-rules@${PROJECT_ID}.iam.gserviceaccount.com"
gcloud projects add-iam-policy-binding "$PROJECT_ID" --member="serviceAccount:${SA}" --role="roles/firebasedatabase.admin" --condition=None
gcloud iam workload-identity-pools create github --location=global --display-name="GitHub Actions"
gcloud iam workload-identity-pools providers create-oidc repo --location=global --workload-identity-pool=github \
  --display-name="GitHub repo" \
  --issuer-uri="https://token.actions.githubusercontent.com" \
  --attribute-mapping="google.subject=assertion.sub,attribute.repository=assertion.repository" \
  --attribute-condition="assertion.repository=='${REPO}'"
PN=$(gcloud projects describe "$PROJECT_ID" --format='value(projectNumber)')
gcloud iam service-accounts add-iam-policy-binding "$SA" --role="roles/iam.workloadIdentityUser" \
  --member="principalSet://iam.googleapis.com/projects/${PN}/locations/global/workloadIdentityPools/github/attribute.repository/${REPO}"
echo "GCP_WIF_PROVIDER = projects/${PN}/locations/global/workloadIdentityPools/github/providers/repo"
echo "GCP_RULES_SA     = ${SA}"
```

Save the two printed values as repository **Variables**.

The attribute condition trusts any workflow in this one repository, including PR branches. That is fine for a single-owner private repo. Tighten it to `assertion.ref=='refs/heads/main'` if anyone else ever gets push access, and accept that PR dry runs then fail.

#### 4.6.4 Fallback: service-account key as `GCP_SA_KEY`

Use this only if §4.6.3 is impossible.

1. In the Cloud console, open **IAM & Admin → Service Accounts → Create service account** and name it `gha-rtdb-rules`.
2. Give it the role **Firebase Realtime Database Admin**.
3. Open the account, then **Keys → Add key → Create new key → JSON**.
4. Paste the whole JSON as the secret `GCP_SA_KEY`, then delete the downloaded file.
5. In `deploy-rules.yml`, replace the two WIF lines with `credentials_json: ${{ secrets.GCP_SA_KEY }}`.

> ⚠️ This key bypasses the database rules and has full read/write. Rotate it yearly, or switch to WIF later.

Personal Google accounts have no organisation, so key creation is normally allowed [INFERRED].

### 4.7 GitHub Actions minutes (private repo)

**Allowance** [CONFIRMED-primary](https://docs.github.com/en/billing/concepts/product-billing/github-actions):
- GitHub Free gives **2,000 min/month**, 500 MB of artifacts and 10 GB of cache per repository. Pro gives 3,000 minutes. Public repos are free.
- "If your account does not have a valid payment method on file, usage is blocked once you use up your quota."
- "GitHub rounds the minutes and partial minutes each job uses up to the nearest whole minute" CONFIRMED-primary. Short jobs are therefore merged into one `checks` job.

**Estimate for a busy month** [INFERRED]:

| Workflow | Runs × billed minutes | Minutes |
|---|---|---|
| CI | 60 runs × 1 (`checks`) + 40 × 2 (`web`) + 25 × 6 (`firmware`) | ≈ 290 |
| deploy-web | 25 × 2 | ≈ 50 |
| deploy-rules | 6 × 2 | ≈ 12 |
| **Total** | | **≈ 350 min (~18 %)** |

A cold PlatformIO cache adds about 5–10 minutes. Caches unused for 7 days are evicted CONFIRMED-primary. PlatformIO's packages are 3.8 GB unpacked on this Mac [OBSERVED], so expect a cache of a few GB compressed; it stays inside the 10 GB.

**If the quota runs out:** `ci.yml` and the deploys stop until the 1st. The Mac path (§5) still works.

### 4.8 Considered and not used

- **Workers Builds (Cloudflare's Git integration).** It would create a second control plane and deploy without the typecheck gate (r7b §5.1). Actions already runs rules and firmware.
- **`cloudflare/wrangler-action@v4`.** It is fine (r7c C1), but one direct `npx wrangler@<exact>` call means one less third-party action.
- **`workflow_run` chaining (deploy only after CI is green).** It adds coupling. A web deploy should not wait for a firmware build, and each deploy has its own gate.

---

## 5. Manual deploy from the Mac (npx only, no global installs)

> ⚠️ You run the `login` commands yourself. Each opens a browser for OAuth, and the agent never runs them.

### 5.1 Web → Cloudflare (recommended: Node 24 from nvm)

wrangler 4.146.0 refuses Node 20: its `bin/wrangler.js` has `MIN_NODE_VERSION = "22.0.0"` [CONFIRMED-primary]. The Mac's default is v20.17.0, but **nvm already has v24.21.0** [OBSERVED]. `nvm use` only changes this terminal's `PATH`.

```zsh
source ~/.nvm/nvm.sh && nvm use 24.21.0          # this terminal only
cd ~/Office/CodeBase/Personal/Datalogger/web
cp -n .env.example .env                            # once; fill in the production values (browser key)
npm ci && npm run typecheck && npm run build
npx --yes wrangler@4.146.0 login                   # once; opens the browser (Cloudflare OAuth)
npx --yes wrangler@4.146.0 deploy --dry-run        # shows what would upload
npx --yes wrangler@4.146.0 deploy --tag "mac-$(date +%Y%m%d%H%M)" --message "manual deploy from Mac"
npx --yes wrangler@4.146.0 deployments list        # confirm the active version
```

**Token instead of `login`:** this keeps the token out of shell history.

```zsh
read -rs "CLOUDFLARE_API_TOKEN?Cloudflare API token: "; export CLOUDFLARE_API_TOKEN
export CLOUDFLARE_ACCOUNT_ID=<account-id>
npx --yes wrangler@4.146.0 deploy
unset CLOUDFLARE_API_TOKEN
```

### 5.2 Web on plain Node 20.17 (no nvm)

wrangler **4.86.0** (2026-04-28) is the last release that accepts Node 20; 4.87.0 raised the minimum to 22 [CONFIRMED-primary, npm metadata]. Its `deploy --dry-run` passes on §3.1 under Node 20.17.0 [OBSERVED].

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/web && npm run build
npx --yes wrangler@4.86.0 deploy
```

Use this only as an emergency path; CI stays on 4.146.0.

### 5.3 RTDB rules

```zsh
cd ~/Office/CodeBase/Personal/Datalogger
OWNER_UID=<owner-uid> DEVICE_UID=<device-uid> sh tools/render-rules.sh
cd .deploy
npx --yes firebase-tools@15.32.1 login             # once; opens the browser
npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id> --dry-run
npx --yes firebase-tools@15.32.1 deploy --only database --project <project-id>
```

firebase-tools 15.32.1 runs on Node 20 or 24 [CONFIRMED-primary, npm metadata]. Afterwards, run the refusal tests in [firebase/README.md](../firebase/README.md) §5 **with the device key**.

### 5.4 Fallback: web → Firebase Hosting

Use this only for a host failover (§7.3 C).

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/web && npm run build && cd ..
npx --yes firebase-tools@15.32.1 deploy --only hosting --project <project-id>
```

This serves `https://<project-id>.web.app`, which is a **new origin**: every phone must re-add the app.

### 5.5 Local test with production headers

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/web && npm run build
npx --yes wrangler@4.146.0 dev        # Node 24 shell; serves dist/ with _headers on http://localhost:8787
curl -sI http://localhost:8787/sw.js | grep -i cache-control
```

`npm run preview` (Vite, port 4173) is quicker, but it ignores `_headers`.

---

## 6. Firebase-side changes

### 6.1 Authorized domains: no change

- **Email/password sign-in does not consult the authorized-domain list.** Only redirect and popup flows and `ActionCodeSettings` continue URLs do (r7c C5). The app calls only `signInWithEmailAndPassword` [OBSERVED, `web/src/data/firebaseSource.ts`].
- **Do not add** `*.workers.dev`.
- **Remove `localhost`** if the list has it (Google discourages it in production).
- **If you ever add** Google sign-in, email-link sign-in or reset links with a continue URL, add `solar.<acct>.workers.dev` then.

### 6.2 API keys: split into a browser key and a device key

| Key | Application restriction | API restriction | Used by |
|---|---|---|---|
| **Browser key** (Firebase's "Browser key (auto created by Firebase)", shown as the web `apiKey`) | **Websites** (list below) | Keep Firebase's auto-restricted list. Narrowing to Identity Toolkit + Token Service is possible, but test on a copy first. | `VITE_FIREBASE_API_KEY` (GitHub Variable) |
| **Device key** (new: Google Cloud console → APIs & Services → Credentials → Create credentials → API key, named `savx-device`) | **None** (the ESP32 sends no `Referer`) | **Identity Toolkit API + Token Service API only** | ESP32 NVS `cloud.apiKey`. Also the e-mail relay from r8 if it signs in, the `curl` tests in `firebase/README.md` §5, and `web/.env.local` for local and LAN dev |

**Browser key → Websites list.** Patterns follow the documented `example.com/*` form [CONFIRMED-primary, r7c #23].

1. `solar.<acct>.workers.dev/*`: production.
2. `<project-id>.web.app/*`: the fallback host (§2.5).
3. `<project-id>.firebaseapp.com/*`: the fallback alias, and Firebase's default e-mail action pages, such as a console-sent password reset. Those pages use the same web key [INFERRED].
4. *Only with previews on:* `*.<acct>.workers.dev/*`.
5. *Not added:* `localhost:5173/*` and LAN IPs. Port handling is undocumented [UNVERIFIED], so dev uses the device key in `web/.env.local`.

**Order of operations**, so nothing breaks:
1. Create the device key, and use it from the start in firmware provisioning and the `curl` tests. `Uplink.cpp` is still a stub, so no device needs re-provisioning.
2. First web deploy with the browser key still unrestricted, then verify sign-in (§7.1 step 9).
3. Add the Websites restriction.
4. Sign in again on the Mac and the iPhone at the production origin. If sign-in now fails with a referrer or API-key error, fix the list. Changes can take a few minutes to apply [INFERRED].
5. Keep `Referrer-Policy: strict-origin-when-cross-origin`. `no-referrer` would strip the `Referer` the restriction checks (r7b §1.3).

Firebase notes that keys restricted to Firebase APIs "do not need to be treated as secrets" [CONFIRMED-primary]. The restriction is abuse friction, not security; the UID-pinned rules are the security boundary.

### 6.3 App Check and reCAPTCHA: leave off

Enforcing App Check, or Auth's reCAPTCHA protection for email/password, would reject the ESP32's plain REST sign-in. A custom provider needs a Blaze backend (r7b §1.5). Nothing to do; just don't enable them.

### 6.4 `firebase.json`: what to keep and what to remove

| Block | Decision | Why |
|---|---|---|
| `database` | **Remove** | Rules deploy only from `.deploy/` (rendered UIDs). Placeholder rules can no longer be deployed from the root, and the committed file keeps placeholders, as `firebase/README.md` asks. `ci.yml` fails if the block comes back. |
| `hosting` | **Keep, update** (§3.5) | Cold standby with parity headers: CSP, `X-Robots-Tag`, `/sw.js` `no-cache`. `_headers` and `_redirects` are in `ignore`. Not deployed unless §7.3 C. |
| `.firebaserc.example` | Keep | Default project for the manual fallback deploy |

### 6.5 What does not change

- RTDB location, rules content, and the `inv1` device path.
- Auth persistence (`initializeAuth` with IndexedDB, no popup resolver).
- The ESP32 → RTDB path.
- The Spark plan. RTDB doesn't check `Origin`, so the host has no effect on data security (r7b §1.4).

---

## 7. First-deploy checklist and rollback

### 7.1 Checklist

**Phase 1: accounts and names** (one-time, about 45 min)
- [ ] 1. **GitHub:** create a **private** repository under your **personal** account and turn on 2FA. Settings → Actions → General → Workflow permissions: leave it on *Read repository contents*.
- [ ] 2. **Cloudflare:** sign up for the free plan, verify the e-mail and turn on 2FA. Under **Workers & Pages**, choose the account subdomain `<acct>` (§2.1). ⚠️ This choice is permanent. Write `https://solar.<acct>.workers.dev` into `docs/site-survey.md`. Do this in the dashboard; CI can't answer an interactive prompt.
- [ ] 3. Create the Cloudflare token and account ID, and save them as secrets (§4.6.1–4.6.2).
- [ ] 4. **Google Cloud:** create the **device key** (§6.2), then set up WIF in Cloud Shell (§4.6.3), or the key fallback (§4.6.4).
- [ ] 5. **GitHub Variables:** add the 11 variables in §4.6 (`CF_PREVIEWS` stays unset).

**Phase 2: repository changes** (implementation hand-off, §9.1). Push them in this order:
- [ ] 6. A PR with `tools/render-rules.sh`, the `firebase.json` change and the three workflows. On the PR:
  - `ci` should be green;
  - `deploy-rules` should run a **dry run** and pass, which proves the WIF login.
  
  Merge it; `deploy-rules` then **deploys** the rules. Run the `firebase/README.md` §5 refusal tests with the device key.
- [ ] 7. A PR with the web changes (`wrangler.jsonc`, `_headers`, `.assetsignore`, `vite.config.ts`). `ci` should be green. Merge it; `deploy-web` deploys to production. The job summary should show the URL, and it must match step 2 **exactly**.

**Phase 3: verify before any phone installs it**
- [ ] 8. Run the header and edge checks in §7.2. Everything must match §3.9 on the real edge.
- [ ] 9. Open the origin in Mac Safari with the Web Inspector:
  - the Console shows **no CSP violations**;
  - the Network tab shows a `wss://…asia-southeast1.firebasedatabase.app/.ws` connection;
  - you can sign in as the owner, and the Live page updates (or `?demo=1` works, before the logger is online).
- [ ] 10. Restrict the browser key (§6.2 steps 3–4). Sign in again in a private window.
- [ ] 11. **Gate passed → install.**
  1. On the iPhone, open the origin in Safari, then Share → **Add to Home Screen**, open it, and sign in.
  2. Turn on Airplane Mode, force-quit and relaunch: the shell must load, showing stale data.
  3. Turn Airplane Mode off.
- [ ] 12. **Update drill.**
  1. Change a visible string and merge it; wait for `deploy-web`.
  2. Bring the app to the foreground: the "A new version of the app is ready → Reload" bar should appear. Tap it; the new version should show.
- [ ] 13. **Rollback drill:** run §7.3 A once with a harmless version, then roll forward.

### 7.2 Verification commands (Mac)

```zsh
O=https://solar.<acct>.workers.dev
curl -sI "$O/"                     | grep -iE '^HTTP|cache-control|content-security-policy|strict-transport|x-robots'
curl -sI "$O/index.html"           | grep -iE '^HTTP|location'            # 307, location: /
curl -sI "$O/sw.js"                | grep -iE '^HTTP|cache-control'       # 200, no-cache
curl -sI "$O/manifest.webmanifest" | grep -iE 'content-type|cache-control' # application/manifest+json, no-cache
A=$(curl -s "$O/" | grep -oE '/assets/index-[A-Za-z0-9_-]+\.js' | head -n 1)
curl -sI "$O$A"                    | grep -i cache-control                # public, max-age=31536000, immutable
curl -s  "$O/cdn-cgi/trace"        | grep -E '^colo='                     # DAC or CGP ideal; CCU/SIN/BOM = ISP routes elsewhere [INFERRED]
```

### 7.3 Rollback

**A. A bad web deploy**

1. Find the last good version. Each CI deploy is tagged with its commit SHA and messaged `main@<sha>`.
   ```zsh
   source ~/.nvm/nvm.sh && nvm use 24.21.0
   cd ~/Office/CodeBase/Personal/Datalogger/web
   npx --yes wrangler@4.146.0 deployments list
   npx --yes wrangler@4.146.0 versions list
   ```
2. Roll back. You can only roll back to the 100 most recently published versions [CONFIRMED-primary].
   - **CLI:** `npx --yes wrangler@4.146.0 rollback <VERSION_ID> --message "rollback: <why>"`.
   - **Dashboard:** Workers & Pages → `savx-logger` → Deployments → ⋯ → **Rollback**.

   **Always pass an explicit `VERSION_ID`.** Without one, wrangler picks "the version uploaded before the latest version" [CONFIRMED-primary]. With PR previews enabled, that may be a PR version.

   Static assets are deployed with the code "in a single operation" [CONFIRMED-primary], so a rollback restores that version's files [INFERRED]. Confirm it with §7.2.
3. On `main`, `git revert` the bad commit and push. `deploy-web` then deploys the reverted code ("roll forward"), and production matches git again.
4. **Phones:** the next foreground triggers the service-worker update, then "Reload".
   - If a phone shows a blank screen: force-quit and reopen it twice. This is risk H11; the precache uses `cache: 'reload'`.
   - **Last resort:** delete the icon, add it again and sign in.

**B. Bad RTDB rules**

1. `git revert` the rules commit and push; `deploy-rules` redeploys.
2. **Emergency** (CI unavailable): `git checkout <good-sha> -- firebase/database.rules.json`, then run §5.3, then commit.
3. Never "fix" access by loosening a rule to `auth != null` (`firebase/README.md` ⚠️).

**C. Host failover** (Cloudflare unusable for days, or the account is lost)

1. Run §5.4. The new URL is `https://<project-id>.web.app`, already on the key's list.
2. On every phone: add the new icon, sign in, and delete the old icon.
3. **Short outages:** do nothing. Installed apps keep working from the service-worker cache, with live Firebase data.
4. Switching back later costs another reinstall, so move back only if it is worth it.

---

## 8. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| H1 | The origin changes after phones installed the app (subdomain or Worker rename, custom domain, host switch) | Medium / High | §2 rules; write the origin down before the first deploy; the gate in §7 step 11 |
| H2 | Leaked Cloudflare token → malicious JS on your origin, which can act as the signed-in owner (`cfg` writes, `ack`, deletes where the rules allow) | Low / High | Token only in GitHub Secrets, scoped to one account, optional expiry; 2FA on GitHub and Cloudflare; revoke and recreate on suspicion |
| H3 | Leaked service-account key (fallback only) bypasses the rules | Low / High | Prefer WIF (no key exists); otherwise rotate yearly |
| H4 | Public Version or Preview URLs expose a build | Low / Low | `preview_urls: false`; `CF_PREVIEWS` unset; even if exposed, same code and the same rules. The workers.dev page also mentions "Deployment URLs" without defining them [UNVERIFIED]: after the first deploy, check Worker → Settings → Domains & Routes shows only `workers.dev`. |
| H5 | Cloudflare outage (2025-11-18, 2025-12-05) | Low / Low | The SW shell keeps working; data comes from Firebase; failover only for long outages (§7.3 C) |
| H6 | Cloudflare changes free static-asset terms, or the account is suspended | Low / Medium | Firebase Hosting standby; `dist/` is plain static; switching = new origin |
| H7 | Actions minutes run out (private repo, no card) → CI and deploys blocked until the 1st | Low / Low | ≈ 350 min/month estimate (§4.7); the Mac path (§5) |
| H8 | Node drift: Mac 20.17 vs CI 24; wrangler ≥ 4.87 needs Node 22 | Medium / Low | nvm 24.21.0 (§5.1) or wrangler 4.86.0 (§5.2); Vite pinned at 6.4.4; Dependabot ignores Vite majors |
| H9 | The referrer restriction blocks sign-in (wrong pattern, policy change, Safari quirk) | Medium / Medium | Restrict only after a working deploy, re-test on the iPhone, and revert in the console in one click (§6.2) |
| H10 | The CSP blocks something new: r8 Alerts calls, Safari CSP differences, or the RTDB long-poll fallback on WebSocket-hostile networks | Medium / Medium | Safari Web Inspector check (§7 step 9); documented additions (§3.8); if needed, add the RTDB host to `script-src` |
| H11 | A missing hashed asset is served as index.html (or 404) with `immutable` [OBSERVED-local] → stale browser-cache entry after deploy plus rollback | Low / Low | The SW precache bypasses the HTTP cache (`cache: 'reload'`); force-quit and reopen; optional hardening: fetch SW cache misses for `/assets/*` with `cache: 'reload'` in `sw.template.js` |
| H12 | Deploy order between rules and code (r8c M6): web or firmware writes new fields before the rules allow them | Medium / Low–Medium | Rules changes ship in their own PR, merged first; rules stay backward-compatible (expand → migrate → contract) |
| H13 | Placeholder rules deployed → logger and owner locked out | Low / High | `render-rules.sh` refuses placeholders and bad UIDs; no `database` block at the root; CI guard |
| H14 | No branch protection on GitHub Free private → a red CI doesn't block merges | Medium / Low | Deploy workflows repeat their gates; read the CI status before merging |
| H15 | `workers.dev` is on the Public Suffix List, so other Workers on the same account subdomain are same-site with the app | Low / Low | Use this Cloudflare account only for this app, or only for trusted Workers |
| H16 | Supply chain (npx CLIs, actions, npm packages) | Low / High | Exact versions ≥ 2 weeks old, SHA-pinned actions, `npm ci` with the lockfile, monthly Dependabot PRs reviewed by hand |
| H17 | PlatformIO cache cold or evicted → slower firmware CI | High / Low | Accept (+5–10 min); the cache key is `platformio.ini` |
| H18 | WIF on a no-billing Spark project doesn't work [UNVERIFIED] | Medium / Low | SA-key fallback (§4.6.4) |
| H19 | The URL is discoverable (certificate logs, guessing) | High / Low | Auth plus UID-pinned rules protect the data; `X-Robots-Tag: noindex, nofollow` keeps it out of search results [CONFIRMED-primary, Google robots docs] |

---

## 9. Hand-off, deviations, open items

### 9.1 Files for the implementation phase

**New:**
- `web/wrangler.jsonc` (§3.1)
- `web/public/_headers` (§3.2)
- `web/public/.assetsignore` (§3.3)
- `tools/render-rules.sh` (§3.6, executable)
- `.github/workflows/ci.yml`, `deploy-web.yml` and `deploy-rules.yml` (§4.2–4.4)
- optional `.github/dependabot.yml` (§4.5)

**Edit:**
- `web/vite.config.ts` (§3.4)
- `firebase.json` (§3.5)
- `.gitignore` and `web/.env.example` (§3.7)

**Docs:**
- `firebase/README.md` §3–§5: the render script replaces in-place `sed` and root `firebase deploy --only database`; `curl` tests use the device key.
- `web/README.md` §3 "Deploy": Cloudflare via CI, the Mac path and the fallback.
- New `docs/06-cloud-and-dashboard-guide.md`: Cloudflare, GitHub, Firebase and Google setup, from §4.6, §6 and §7.
- `docs/04`: the hosting row and the security matrix (two keys).
- `docs/07`: the M6 web deploy now goes through CI, and the iPhone install gate.
- `docs/08`: rollback and failover.
- `README.md` and `docs/00`: ADR "web host = Cloudflare Workers static assets".
- d1 §6.7 steps 7–8 are superseded.

**Do not:**
- run `wrangler`, `firebase` or `vercel` `login` or `deploy`;
- `git init`, commit or push;
- create Cloudflare, GitHub or Google resources.

The user does all of that by following §7.

**Verify after implementing:**
- `cd web && npx tsc --noEmit && npm run build` passes.
- `dist/sw.js` has no `"/index.html"`, `"/_headers"` or `"/.assetsignore"`.
- `actionlint` passes on `.github/workflows/*.yml`.
- `sh tools/render-rules.sh` runs with dummy UIDs; delete `.deploy/` afterwards.

### 9.2 Deviations from r7a and r7b

1. **`firebase.json` loses `database`; rules render into `.deploy/`.** r7b kept the block and used `sed -i` in CI. This change avoids deploying placeholders by accident and keeps UIDs out of git.
2. **`compatibility_date` is `2026-04-01`** (r7b: `2026-10-01`), so the Node 20 emergency wrangler 4.86.0 also accepts it.
3. **Added:** `.assetsignore`, `X-Robots-Tag`, an explicit `html_handling`, the browser-key entry `<project-id>.firebaseapp.com/*`, and the `ci.yml` guards (open-rule lint, no root `database` block, `_headers` line length, the asia-southeast1 URL check).
4. **PR previews** use the GA `versions upload --preview-alias` behind `CF_PREVIEWS` and `preview_urls: true`. r7b's opt-in `wrangler preview` is "[open beta]".
5. **Workflows:** r7b's separate `firmware.yml` is merged into `ci.yml` with git-based change detection, and the workflow files are named `deploy-web.yml` and `deploy-rules.yml`.
6. **New observation:** the SPA fallback is served with the `/assets/*` immutable header (§3.9). `not_found_handling: "none"` does not avoid it.
7. **Mac deploy path:** Node 24 from the existing nvm install, or wrangler 4.86.0 on Node 20.17. r7a and r7b did not cover the Mac's Node 20.

### 9.3 Still unverified (check during §7)

- No card needed for Cloudflare's free plan.
- WIF on Spark without billing.
- The workers.dev "Deployment URLs" mention.
- Rollback restoring assets (inferred from "single operation").
- Safari's handling of the CSP (tested only in Chromium).
- `/cdn-cgi/trace` on `workers.dev`.
- Referrer patterns with ports.
- The production edge matching the §3.9 local results.

---

## 10. Sources (fetched 2026-10-21 unless noted)

**This decision**
- Cloudflare:
  - [Workers + GitHub Actions (token permission, secret names)](https://developers.cloudflare.com/workers/ci-cd/external-cicd/github-actions/)
  - [Rollbacks (100 versions, dashboard path)](https://developers.cloudflare.com/workers/configuration/versions-and-deployments/rollbacks/)
  - [Wrangler commands: rollback, versions, deployments](https://developers.cloudflare.com/workers/wrangler/commands/workers/)
  - [workers.dev routing and the account subdomain](https://developers.cloudflare.com/workers/configuration/routing/workers-dev/)
  - [Version URLs / preview_urls](https://developers.cloudflare.com/workers/configuration/previews/)
  - [Static assets (deployed "in a single operation")](https://developers.cloudflare.com/workers/static-assets/)
  - [Assets binding / .assetsignore](https://developers.cloudflare.com/workers/static-assets/binding/)
  - [Find account and zone IDs](https://developers.cloudflare.com/fundamentals/account/find-account-and-zone-ids/) (not fetched; linked from the Actions guide)
- GitHub:
  - [Actions billing (2,000 min, cache, blocked without a card)](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
  - Runner pricing (per-job rounding)
  - Dependency caching (7-day eviction, 10 GB)
  - [GitHub's plans (protected branches in private repos = Pro)](https://docs.github.com/en/get-started/learning-about-github/githubs-plans)
- Google:
  - [Cloud Shell pricing](https://cloud.google.com/shell/pricing)
  - [Robots meta tag / X-Robots-Tag](https://developers.google.com/search/docs/crawling-indexing/robots-meta-tag)
- Registries:
  - `https://registry.npmjs.org/wrangler`: engines per version; 4.146.0 `>=22.0.0`, 4.86.0 `>=20.3.0`.
  - `https://unpkg.com/wrangler@4.146.0/bin/wrangler.js`: `MIN_NODE_VERSION = "22.0.0"`; 4.86.0 is `"20.0.0"`.
  - `firebase-tools@15.32.1` (`>=20.0.0 || >=22.0.0 || >=24.0.0`) and `vite@6.4.4`.
  - `git ls-remote` and `action.yml` for checkout v7.0.1, setup-node v7.0.0, setup-python v7.0.0, cache v6.1.0 and google-github-actions/auth v3.0.0 (all node24).

**Local evidence (scratch, not in the repo)**

All under `<scratch>`:
- `web/`: the patched build plus `wrangler.jsonc` and `_headers`.
- `dev.log` and `dev-none.log`: the `wrangler dev` runs.
- `repo/`: the workflows, linted with actionlint 1.7.12 + shellcheck, plus `firebase.json` and `render-rules.sh`.
- `gitsim/`: the change-detection simulation.

**Inherited (see those files for the full lists)**
- [r7a](r7a-hosting-landscape.md) §Sources
- [r7b](r7b-hosting-integration.md) §Sources: CSP test, latency samples, SDK source reads, Vercel and Netlify configs
- [r7c](r7c-hosting-verification.md): corrections C1–C11 and confirmations 1–25
