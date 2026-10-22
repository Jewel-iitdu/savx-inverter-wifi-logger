# Free static hosting for the SAVX Datalogger PWA: landscape and recommendation (2026-10-21)

## 0. Bottom line

- **Recommended host: Cloudflare Workers with Static Assets, on a `*.workers.dev` URL.**
  - Static-asset requests are free and unlimited [CONFIRMED-primary].
  - It needs no card, and the free plan stops at its limits; it never bills [INFERRED]. There is no non-commercial clause to worry about.
  - Cloudflare has PoPs in Dhaka and Chittagong [CONFIRMED-primary].
  - A `_headers` file and a one-line SPA fallback reproduce the current `firebase.json` exactly.
  - Deploys run from a private GitHub repo, through GitHub Actions or Workers Builds.
- **Runner-up: Vercel Hobby.** It has the best developer experience and automatic previews, and is fine for personal non-commercial use. It loses some points because:
  - Vercel publishes no Dhaka PoP.
  - It has a "non-commercial" ToS clause.
  - Private repos owned by a GitHub organisation are not supported.
- **Keep Firebase Hosting as a warm standby.** It is already configured, and it is a reasonable free host in its own right.
- **Moving away from Firebase Hosting improves the developer experience. It is not required.**
  - The service worker caches the app shell, and all live data comes from RTDB in Singapore.
  - The host therefore matters only for first install and for updates [INFERRED].
- **Avoid for this project:**
  - Netlify Free: every production deploy costs 15 of the 300 monthly credits.
  - GitHub Pages: a private source repo needs Pro, there are no custom headers, and its docs say not to use it for password transactions.
  - Azure Static Web Apps: the Azure sign-up needs a card.
  - Render: Hobby bandwidth was cut to 5 GB in 2026.
  - Deno Deploy: the platform keeps changing (Classic was shut down in 2026).
  - Surge and Codeberg Pages.

---

## 1. Real usage estimate, and which limits matter

Assumptions [INFERRED]:
- 6 devices: the owner's iPhone and Mac, plus about 4 family phones.
- About 10 app launches per device per day.
- The app shell is about 2 MB, and a typical update changes about 0.3–1 MB of hashed JS/CSS.
- The service worker serves the shell from cache. Each launch makes about 3 small host requests: the navigation or index revalidation, the `sw.js` update check and the manifest. These are about 1–30 KB each, often answered with a 304.
- All telemetry goes browser → Firebase, never through the host.

| Item | Per month (high estimate) |
|---|---|
| First installs | 6 × 2 MB ≈ 12 MB |
| Launch traffic | 6 × 10 × 30 × 3 ≈ **5,400 requests**, ≤ ~70 MB |
| Updates after deploys | Each device fetches only the newest build: ≤ 6 × 30 × 1 MB ≈ 180 MB |
| **Total host egress** | **≈ 0.1–0.5 GB/month; < 10k requests/month; peak day < 50 MB** |
| Deploys/builds | 100–300 per month while developing; < 10 per month once stable. Each build takes about 30–90 s (`npm ci` + `vite build`). |
| Files | < 50 output files, all well under 1 MB |

**Limits that do not matter.** Bandwidth and requests on every candidate, file counts and file sizes. The tightest is Render's new 5 GB, and this app uses under 10% of it.

**Limits that do matter:**
1. **Deploy and build budgets during development:**
   - Netlify: 15 credits per production deploy, so about 19 deploys a month.
   - Cloudflare Pages: 500 builds a month.
   - Vercel: 100 deploys a day.
   - Workers Builds: 3,000 minutes a month.
   - GitHub Actions on a private repo: 2,000 minutes a month.
2. **Git auto-deploy from a free private repo.** GitHub Pages and Codeberg fail this.
3. **No card required.** Azure fails this.
4. **Per-path headers.** These are needed for immutable `/assets/*`, `no-cache` on `sw.js` and `index.html`, and the manifest content type. GitHub Pages, Codeberg and free Surge fail this.
5. **What happens past the limit.** All free tiers stop serving or pause rather than bill. They differ in what triggers the stop and how long it lasts.

**An iPhone constraint that outweighs all of the above** [INFERRED from how WebKit handles origins]:
- A Home Screen web app, its IndexedDB data (including the Firebase Auth session) and its service worker all belong to one **origin**.
- Changing host or subdomain later, or adding a custom domain, means everyone must re-add the app and sign in again.
- Pick the final URL **before** family members install the app.

---

## 2. Policy changes in 2024–2026 that affect the choice

| Platform | Change | Tag |
|---|---|---|
| Vercel | Jan 2024: personal accounts converted to Hobby teams, and usage was reset. | [CONFIRMED-primary] (Hobby docs link to the 2024-01 changelog) |
| | Current Hobby allowance: 100 GB Fast Data Transfer, 1M CDN requests, 10 GB Fast Origin Transfer, 100 deployments/day, 1 concurrent build, 45-min builds. Hobby is capped: "cannot purchase additional usage". Going over usually means waiting 30 days. | [CONFIRMED-primary] |
| | Pro plan reworked into credit-based billing; Hobby unchanged. | [CONFIRMED-primary] (pricing page) |
| | Service disruption on 2025-10-20, during the AWS us-east-1 incident. | [CONFIRMED-primary] (Vercel blog title) |
| Netlify | **New accounts since 2025-09-04 use credit-based plans.** Free = 300 credits/month, with a hard limit and no top-ups. A production deploy costs 15 credits, bandwidth 20 credits/GB, and 10k requests cost 2 credits. Deploy previews and branch deploys are not metered. Older accounts keep legacy plans and cannot switch back once they move. | [CONFIRMED-primary] |
| | When credits run out on Free, sites pause and show "Site not available" until the next cycle, and no production deploys are possible. | [CONFIRMED-primary] |
| Cloudflare | Workers Static Assets is now the main path. Static-asset requests are free and unlimited, and SPA mode is set explicitly with `not_found_handling`. Pages still works. Cloudflare's own Pages→Workers migration guide frames Workers as the broader platform. A mirrored copy of Cloudflare's docs says that Workers "will receive the focus of Cloudflare's development efforts". The official page I fetched today does not contain that sentence. | Workers facts [CONFIRMED-primary]; the "Pages in maintenance mode" view [SECONDARY] |
| | Workers Builds free tier: 3,000 build minutes/month, 1 concurrent build, 20-minute timeout. | [CONFIRMED-primary] |
| | Global outages on 2025-11-18 and 2025-12-05. The 12-05 outage lasted about 25 minutes and affected about 28% of HTTP traffic. | 12-05 [CONFIRMED-primary]; 11-18 details [SECONDARY] |
| GitHub Pages | No change. GitHub Free: "GitHub Pages in public repositories" only. A private source repo needs Pro. A site that is itself private needs an organisation on Enterprise Cloud. | [CONFIRMED-primary] |
| Firebase Hosting | Spark unchanged: 10 GB storage and **360 MB/day** transfer on the pricing page. The quota docs instead say "10 GB/month". Past the quota there is a short grace period, then sites are **disabled until the start of the next month**. App Hosting has no Spark allowance. | [CONFIRMED-primary]; the two pages state the limit differently |
| Render | **2026-04-23: new workspace plans.** Hobby now includes **5 GB** of bandwidth (down from 100 GB), with $0.15/GB overage. Legacy workspaces were moved automatically on **2026-08-01**. Without a payment method, Render "spins down your workspace's services until the start of the next month". | [CONFIRMED-primary] |
| Deno | **Deploy Classic was shut down on 2026-07-20.** The new Deno Deploy Free tier: 1M requests/month, 20 GiB egress, 15 builds/hour, 5 custom domains. | [CONFIRMED-primary] |
| Azure SWA | The Dedicated plan was retired on 2025-10-31. Free is unchanged: 100 GB/month per subscription, 250 MB per app, no overage option. | [CONFIRMED-primary] |
| Codeberg Pages | Migrated from Pages Server v2 to "git-pages". v2 is disabled for new users because of abuse. Public repos only. | [CONFIRMED-primary] |

---

## 3. Platform fact sheets

### 3.1 Cloudflare Workers + Static Assets (`<worker>.<account-subdomain>.workers.dev`)

- **Limits:**
  - Static-asset requests are free and unlimited; storage is free.
  - 20,000 files per version (needs Wrangler 4.34 or later); 25 MiB per file.
  - The Worker-script quota is 100k requests/day, but it **only applies if a Worker script runs**. With assets only, no script runs.
  - Workers Builds: 3,000 minutes/month, 1 concurrent build, 20 minutes, 2 vCPU / 8 GB.
  - All [CONFIRMED-primary].
- **Past the limit:**
  - A Worker script that goes over its quota returns Error 1027, or 429 on `run_worker_first` routes.
  - Static assets are not metered.
  - The free plan has no billing. No card is needed to sign up [INFERRED; not stated on the pages fetched].
- **Config:**
  - `wrangler.jsonc` with `assets.directory` and `"not_found_handling": "single-page-application"`.
  - A `_headers` file in the asset directory: up to 100 rules, 2,000 characters per line. The syntax is Netlify-style `path` + indented `Name: value`, with `!` to remove a header.
  - `_redirects` is supported natively.
  - [CONFIRMED-primary]
- **Caching:** Cloudflare caches assets automatically, using tiered caching. Cache-Control is set per path through `_headers` [CONFIRMED-primary].
- **Previews:**
  - Version URLs: `<prefix>-<worker>.<sub>.workers.dev`.
  - Aliases from `wrangler versions upload --preview-alias <name>`.
  - Workers Builds comments the preview URL on PRs.
  - Needs `preview_urls: true` [CONFIRMED-primary].
- **Git:**
  - Workers Builds uses a GitHub App that can be limited to "Only select repositories". Whether private repos work is not stated on that page [INFERRED: they do].
  - Alternative: GitHub Actions with an API token. This is host-agnostic and recommended.
- **Custom domain:** Workers **requires the domain's nameservers to be on Cloudflare**; Pages allows an outside CNAME [CONFIRMED-primary]. This only matters if a domain is bought later.
- **Near Dhaka:** **Dhaka and Chittagong** PoPs, plus Kolkata, Kathmandu and Thimphu nearby [CONFIRMED-primary]. Whether a given Bangladeshi ISP's traffic actually lands in DAC is not guaranteed, so check `colo=` (§7) [INFERRED].
- **Lock-in:** Low. The output is plain `dist/`, and `_headers` uses the same syntax as Netlify.

### 3.2 Cloudflare Pages (`<project>.pages.dev`)

- **Limits:**
  - **500 builds/month**, 1 concurrent build, 20-minute timeout.
  - 20,000 files; 25 MiB per file; 100 custom domains per project; 100 projects.
  - 100 `_headers` rules; 2,100 redirects; unlimited preview deployments.
  - Static requests are "free and unlimited" on all plans.
  - [CONFIRMED-primary]
- **Strengths:**
  - Zero-config Git previews for every branch.
  - SPA fallback works out of the box when there is no `404.html` [SECONDARY, per the migration guide].
  - Custom domains can use an outside CNAME.
  - A shorter, nicer origin.
- **Risk:** New investment goes to Workers [SECONDARY]. Pages is still supported and works today.

### 3.3 Vercel Hobby (`<project>.vercel.app`)

- **Limits:**
  - 100 GB Fast Data Transfer; 1M CDN requests; 10 GB Fast Origin Transfer.
  - 100 deployments/day; 100 builds/hour; 1 concurrent build; 45-minute builds.
  - 100 MB upload limit for CLI deploys; 15,000 source files per CLI deploy.
  - 200 projects; 50 domains per project.
  - [CONFIRMED-primary]
- **Terms:**
  - "Personal, non-commercial use" only. Commercial means financial gain for anyone involved, ads, and so on; asking for donations is allowed.
  - Going over usually means waiting until 30 days have passed. Hobby cannot buy more usage.
  - [CONFIRMED-primary] Card not needed for Hobby [INFERRED].
- **Git:**
  - "Vercel does not support connecting a project on your Hobby team to Git repositories owned by Git organizations" [CONFIRMED-primary].
  - Private repos on a personal account work [INFERRED/SECONDARY].
- **Config:**
  - `vercel.json` `headers` (`source` plus a `{key, value}` list) and `rewrites` (`{"source": "/(.*)", "destination": "/index.html"}`).
  - "precedence is given to the filesystem prior to rewrites being applied", so hashed assets and `sw.js` are never rewritten [CONFIRMED-primary].
- **Previews:** Every push and PR gets a preview. Vercel Authentication can protect previews and production on Hobby [CONFIRMED-primary]. A protected preview opens only when signed in to Vercel [INFERRED].
- **Near Dhaka:**
  - Vercel claims 126 PoPs in 94 cities and 51 countries, but publishes no city list.
  - Caching is concentrated in 19 regions; the nearest are `bom1` Mumbai and `sin1` Singapore [CONFIRMED-primary].
  - No Dhaka PoP is documented.

### 3.4 Firebase Hosting, Spark (`<project>.web.app`), the current design

- **Limits:**
  - 10 GB storage.
  - 360 MB/day on the pricing page, but "10 GB/month" in the quota docs.
  - 2 GB maximum per file.
  - Multiple sites per project; custom domain and SSL included.
  - Past the quota: grace period, then **disabled until next month**, unless upgraded to Blaze.
  - [CONFIRMED-primary]
- **Config:** The existing `firebase.json` headers and rewrites already do everything needed. The CDN caches static content until the next deploy, and a deploy clears that cache [CONFIRMED-primary].
- **Git and previews:**
  - `firebase init hosting:github` creates a service account and stores its JSON key as a GitHub secret.
  - It writes workflows that deploy a preview channel per PR and go live on merge. Works with "public or private" repos.
  - [CONFIRMED-primary]
  - Runs consume private-repo Actions minutes: 2,000/month on GitHub Free [CONFIRMED-primary].
- **Near Dhaka:**
  - Observed to be served by Fastly [SECONDARY].
  - Fastly lists Kolkata, Guwahati and Siliguri, but **no Dhaka** [CONFIRMED-primary, Fastly map].
- **Plus:** One vendor and one console. It shares failures with the backend, so it adds no new dependency. When Firebase is down, the data is gone anyway; see the 2025-06-12 Google Cloud incident on the Firebase status page.

### 3.5 Netlify Free (credit-based) (`<site>.netlify.app`)

- **Limits:** 300 credits/month, hard limit. Production deploy = 15 credits; 1 GB = 20 credits; 10k requests = 2 credits. Previews and branch deploys are free [CONFIRMED-primary].
- **For this app:** About 0.5 GB and about 10k requests use about 12 credits. That leaves room for **about 19 production deploys a month** [INFERRED arithmetic]. Going over pauses the site for the rest of the cycle [CONFIRMED-primary].
- **Config:**
  - `_headers` or `netlify.toml` `[[headers]]`.
  - SPA rule: `/*  /index.html  200`, where existing files take precedence [CONFIRMED-primary].
  - Private organisation repos are listed only under Pro [CONFIRMED-primary].
- **Near Dhaka:** The regular CDN's only Asian PoP is Singapore, per a Netlify staff forum post from Mar 2025 [SECONDARY].

### 3.6 GitHub Pages (`<user>.github.io/<repo>`)

- **Limits:**
  - 1 GB site.
  - Soft limit of 100 GB/month bandwidth.
  - Soft limit of 10 builds/hour, which does not apply to custom Actions workflows.
  - 10-minute deploy timeout; 429 when rate-limited.
  - [CONFIRMED-primary]
- **Blockers:**
  - Free accounts can publish Pages from **public repos only**. A two-repo workaround (private source, public output) works.
  - **No custom headers.** The `Cache-Control` value is set by GitHub and cannot be changed [SECONDARY].
  - The SPA fallback needs the `404.html` copy hack.
  - The docs say Pages "shouldn't be used for sensitive transactions like sending passwords". This app's login form sends a password to Firebase Auth [CONFIRMED-primary].

### 3.7 Others

| Platform | Key facts | Verdict |
|---|---|---|
| **Render static** | Hobby: 5 GB/month bandwidth. Past it, with no payment method, services are spun down until the next month. Up to 2 custom domains with TLS. PR previews. Headers, redirects and rewrites are set in the dashboard. [CONFIRMED-primary] | Works, but the 2026 cut and the spin-down make it a weaker choice |
| **Azure Static Web Apps Free** | 100 GB/month per subscription with no overage; 250 MB per app; 15,000 files; 2 custom domains; 3 preview environments; no SLA. Config is `staticwebapp.config.json`. **The Azure account needs a phone number plus a credit or debit card** (no charge). [CONFIRMED-primary] | Fails the "no card" rule |
| **Deno Deploy (new)** | Free: 1M requests, 20 GiB egress, 15 builds/hour, 1 concurrent build, 5 domains. Classic was shut down on 2026-07-20. [CONFIRMED-primary] Overage behaviour is not stated. | Too much churn; headers need code |
| **Surge** | Free covers "Unlimited publishing", a custom domain and "Basic SSL". Custom redirects, CORS and password protection need Professional at $30/month. [CONFIRMED-primary] CLI only; no Git; no headers [INFERRED]. | No |
| **Codeberg Pages** | Public repos only; being migrated to git-pages; v2 closed to new users. [CONFIRMED-primary] | No |
| **GitLab Pages** | Allows private projects; custom headers per project are an open feature request (gitlab-pages issue #50) [SECONDARY]. Not evaluated further. | No |

---

## 4. Scored comparison

Each criterion is scored 0–5, then weighted; the weights total 100.
- A (20): costs nothing forever, with no card and a safe stop at the limit.
- B (15): free Git auto-deploy from a private repo.
- C (10): limits leave plenty of room for this app.
- D (15): headers and SPA control.
- E (10): edge close to Dhaka.
- F (5): preview deployments.
- G (10): track record and stable policies.
- H (5): low lock-in.
- I (10): simple to set up in this repo.

| # | Platform | A | B | C | D | E | F | G | H | I | **Score** |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Cloudflare Workers + Static Assets** | 5 | 5 | 5 | 5 | 5 | 4 | 4 | 4 | 4 | **94** |
| 2 | Cloudflare Pages | 5 | 5 | 4 | 5 | 5 | 5 | 3 | 4 | 5 | **93** |
| 3 | Vercel Hobby | 4 | 4 | 5 | 5 | 3 | 5 | 4 | 4 | 5 | **86** |
| 3 | Firebase Hosting (Spark) | 5 | 3 | 4 | 5 | 4 | 4 | 4 | 4 | 5 | **86** |
| 5 | Netlify Free (credits) | 4 | 4 | 2 | 5 | 3 | 5 | 3 | 4 | 5 | **78** |
| 6 | Render static (Hobby) | 3 | 5 | 3 | 4 | 3 | 4 | 3 | 4 | 4 | **73** |
| 6 | Azure SWA Free | 2 | 5 | 4 | 5 | 3 | 4 | 4 | 3 | 3 | **73** |
| 8 | GitHub Pages | 5 | 1 | 4 | 1 | 4 | 1 | 5 | 5 | 3 | **64** |
| 8 | Deno Deploy (new) | 3 | 4 | 4 | 3 | 3 | 4 | 2 | 3 | 3 | **64** |
| 10 | Surge | 4 | 1 | 4 | 1 | 2 | 1 | 2 | 5 | 3 | **50** |
| 11 | Codeberg Pages | 5 | 0 | 3 | 1 | 2 | 0 | 2 | 4 | 2 | **45** |

The scores are judgement calls [INFERRED], based on the facts and tags in §3.

## 5. Ranked recommendation

1. **Cloudflare Workers + Static Assets. Deploy from GitHub Actions with `wrangler`, or turn on Workers Builds for a Vercel-like push-to-deploy.**
   - Unlimited free static serving and a Dhaka PoP.
   - No billing exposure and no commercial-use clause.
   - The fastest-growing Cloudflare product.
   - Choose the `workers.dev` account subdomain carefully: it becomes the permanent PWA origin.
2. **Cloudflare Pages**, if you want zero-config branch previews and the shorter `<project>.pages.dev` origin. Note that 500 builds/month is enough for this app.
3. **Vercel Hobby**, if you want the most Vercel-like experience. This app is allowed under the personal, non-commercial terms. Keep the repo under your personal GitHub account, not an organisation.
4. **Firebase Hosting**: keep `firebase.json` as the standby. Use it as the primary only if you would rather have one vendor than native Git deploys.

---

## 6. Pipeline addition (for the Cloudflare primary)

**Files to add:**

1. **`web/public/_headers`.** Vite copies it into `dist/`. It is a direct translation of `firebase.json`:
```
/assets/*
  Cache-Control: public, max-age=31536000, immutable
/icons/*
  Cache-Control: public, max-age=604800
/sw.js
  Cache-Control: no-cache, no-store, must-revalidate
/
  Cache-Control: no-cache
/index.html
  Cache-Control: no-cache
/*.webmanifest
  Cache-Control: no-cache
  Content-Type: application/manifest+json
/*
  X-Content-Type-Options: nosniff
  Referrer-Policy: strict-origin-when-cross-origin
  X-Frame-Options: DENY
```

2. **`web/wrangler.jsonc`.** Also add `wrangler` as a devDependency.
```jsonc
{
  "name": "savx-solar",
  "compatibility_date": "2026-10-01",
  "assets": { "directory": "./dist", "not_found_handling": "single-page-application" },
  "workers_dev": true,
  "preview_urls": true
}
```

3. **`.github/workflows/web.yml`.** It runs from the repo root and uses about 1–2 minutes per run. Private-repo Actions get 2,000 free minutes a month, which is enough for about 1,000 runs.
```yaml
name: web
on:
  push: { branches: [main], paths: ['web/**', '.github/workflows/web.yml'] }
  pull_request: { paths: ['web/**'] }
  workflow_dispatch:
jobs:
  deploy:
    runs-on: ubuntu-latest
    defaults: { run: { working-directory: web } }
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with: { node-version: 'lts/*', cache: npm, cache-dependency-path: web/package-lock.json }
      - run: npm ci
      - run: npm run build
        env:
          VITE_FIREBASE_API_KEY: ${{ vars.VITE_FIREBASE_API_KEY }}
          VITE_FIREBASE_AUTH_DOMAIN: ${{ vars.VITE_FIREBASE_AUTH_DOMAIN }}
          VITE_FIREBASE_DATABASE_URL: ${{ vars.VITE_FIREBASE_DATABASE_URL }}
          VITE_FIREBASE_PROJECT_ID: ${{ vars.VITE_FIREBASE_PROJECT_ID }}
          VITE_FIREBASE_APP_ID: ${{ vars.VITE_FIREBASE_APP_ID }}
      - uses: cloudflare/wrangler-action@v3
        with:
          apiToken: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          accountId: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          workingDirectory: web
          command: ${{ github.event_name == 'pull_request' && format('versions upload --preview-alias pr-{0}', github.event.number) || 'deploy' }}
```
- Match the env var names to the code being written in `web/src`.
- `wrangler-action@v3` is the current major version [SECONDARY].
- The `VITE_FIREBASE_*` values end up in the public bundle anyway; Firebase security comes from RTDB rules and Auth. Keeping them in GitHub Actions `vars` only keeps them out of git.

4. **Optional standby job.** Add a `workflow_dispatch`-only job that runs `firebase deploy --only hosting`, using the service-account secret created by `firebase init hosting:github`. Leave the hosting block in `firebase.json` unchanged.

**One-time manual steps:**
1. Create a **private** GitHub repo under your personal account.
2. Create a Cloudflare account and choose the `workers.dev` subdomain carefully, since it becomes the app's permanent origin.
3. Create an API token from the "Edit Cloudflare Workers" template and add the `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` repo secrets.
4. In Firebase Console → Authentication → Settings, add the new origin to **Authorized domains**. Email/password sign-in may work without it, but password-reset links and any future OAuth will not [INFERRED].
5. If the web API key has HTTP-referrer restrictions in Google Cloud Console, add `https://<origin>/*`.

**Runner-up variant (Vercel).**
- Set Root Directory = `web`, Framework = Vite, and enter the `VITE_FIREBASE_*` vars as project environment variables.
- Add a `web/vercel.json` with:
  - `rewrites: [{ "source": "/(.*)", "destination": "/index.html" }]`
  - `headers` entries mirroring the table above, for `/assets/(.*)`, `/sw.js`, `/(.*).webmanifest` and `/(.*)`.

## 7. Checks after the first deploy

- `curl -sI https://<origin>/sw.js | grep -i cache-control` should return `no-cache, no-store, must-revalidate`.
- `curl -sI https://<origin>/assets/<hash>.js` should return `immutable`.
- `curl -sI https://<origin>/manifest.webmanifest` should return `application/manifest+json`.
- `curl -s https://<origin>/cdn-cgi/trace | grep colo` from a Bangladeshi ISP shows the PoP. `DAC` or `CGP` is ideal; `CCU`, `SIN` or `BOM` means the ISP routes elsewhere [INFERRED].
- A deep link such as `/history` should return 200 with the index page.
- On iPhone: open the URL in Safari, Share → Add to Home Screen, sign in, go offline, and relaunch. The shell must load from the service worker.

## Sources (fetched 2026-10-21)

- Vercel: https://vercel.com/docs/limits · https://vercel.com/docs/plans/hobby · https://vercel.com/pricing · https://vercel.com/docs/limits/fair-use-guidelines · https://vercel.com/docs/regions · https://vercel.com/docs/project-configuration/vercel-json
- Cloudflare: https://developers.cloudflare.com/workers/static-assets/ · https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/ · https://developers.cloudflare.com/workers/static-assets/headers/ · https://developers.cloudflare.com/workers/platform/limits/ · https://developers.cloudflare.com/workers/ci-cd/builds/limits-and-pricing/ · https://developers.cloudflare.com/workers/ci-cd/builds/git-integration/github-integration/ · https://developers.cloudflare.com/workers/configuration/previews/ · https://developers.cloudflare.com/workers/static-assets/migration-guides/migrate-from-pages/ · https://developers.cloudflare.com/pages/platform/limits/ · https://developers.cloudflare.com/pages/functions/pricing/ · https://developers.cloudflare.com/pages/configuration/git-integration/github-integration/ · https://www.cloudflare.com/network/ · https://blog.cloudflare.com/5-december-2025-outage/ · [SECONDARY] https://mecanik.dev/en/posts/cloudflare-pages-vs-workers-which-to-use-in-2026/ · https://www.morphllm.com/comparisons/cloudflare-pages-vs-workers
- Netlify: https://www.netlify.com/pricing/ · https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/ · https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/billing-faq-for-credit-based-plans/ · https://docs.netlify.com/manage/routing/headers/ · https://docs.netlify.com/manage/routing/redirects/rewrites-proxies/ · [SECONDARY] https://answers.netlify.com/t/is-there-a-list-of-where-netlifys-cdn-pops-are-located/855
- Firebase: https://firebase.google.com/pricing · https://firebase.google.com/docs/hosting/usage-quotas-pricing · https://firebase.google.com/docs/hosting/github-integration · https://firebase.google.com/docs/hosting/manage-cache · https://status.firebase.google.com/incidents/aibvHDiSS2AXiszCBL8Q · [SECONDARY, Fastly behind Firebase] https://www.netify.ai/resources/platforms/firebase · https://www.fastly.com/network-map/
- GitHub: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits · https://docs.github.com/en/get-started/learning-about-github/githubs-plans · [SECONDARY] https://github.com/orgs/community/discussions/4444 · https://discourse.gohugo.io/t/is-it-possible-to-add-custom-http-header-in-hugo-using-github-pages/32182
- Render: https://render.com/docs/static-sites · https://render.com/docs/outbound-bandwidth · https://render.com/docs/free · https://render.com/changelog/updated-plans-for-render-workspaces · https://render.com/docs/new-workspace-plans
- Azure: https://learn.microsoft.com/en-us/azure/static-web-apps/plans · https://learn.microsoft.com/en-us/azure/static-web-apps/quotas · https://azure.microsoft.com/en-us/pricing/purchase-options/azure-account
- Deno: https://deno.com/deploy/pricing · https://docs.deno.com/deploy/classic/
- Surge: https://surge.sh/pricing · Codeberg: https://docs.codeberg.org/codeberg-pages/ · GitLab: https://docs.gitlab.com/user/project/pages/ · https://gitlab.com/gitlab-org/gitlab-pages/-/issues/50
- Local context: <project>/firebase.json · <project>/research/d1-architecture.md (§1.2, §6.7)