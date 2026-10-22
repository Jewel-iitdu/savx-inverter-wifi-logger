# Solar Logger PWA (`web/`)

The iPhone dashboard for the Savx F4 datalogger. It is a static Progressive Web App that reads the ESP32's data from the Firebase Realtime Database (RTDB). It is hosted on **Cloudflare Workers static assets** at `https://solar.<acct>.workers.dev` and deployed by GitHub Actions; Firebase Hosting is the cold-standby fallback ([r7](../research/r7-web-hosting.md)). The design is [d1 §6](../research/d1-architecture.md); the Alerts page follows [r8 §6](../research/r8-email-alerts.md). The cloud setup guide is [docs/06-cloud-and-dashboard-guide.md](../docs/06-cloud-and-dashboard-guide.md).

| | |
|---|---|
| Stack | Vite 6.4.4, TypeScript 7.0.2, Preact 11.0.1 (no Babel; esbuild's automatic JSX runtime) |
| Data | Firebase JS SDK 13.0.0 (modular: `app`, `auth`, `database`), **bundled and self-hosted**, never from a CDN |
| Charts | uPlot 1.6.32 for time series, plain SVG for bars |
| Offline shell | Hand-written service worker (`sw.template.js`); no Workbox |
| Auth | Email/password, persisted in IndexedDB (`initializeAuth` with `indexedDBLocalPersistence`) |
| Hosting | Cloudflare Workers static assets (`wrangler.jsonc`, `public/_headers`), CI deploy from `main`; Firebase Hosting fallback (`../firebase.json`) |

## Pages

| Page | Shows | Reads (all date-bounded) |
|---|---|---|
| **Live** (default) | Power flow (PV → inverter → load, battery ↔, grid), mode badge, SOC gauge, battery V/A/W, grid/output V/Hz, load %, heat-sink °C, today's kWh, decoded warnings. A red **"Logger offline"** banner when `live.ts` is more than 60 s old. | `onValue(live)`, `onValue(status)`, `onValue(daily/<today>)` |
| **Today** | Three uPlot charts with a shared cursor (PV/load/battery W; SOC and battery V; grid and output V), 6 h / 12 h / day range buttons, energy tiles, `epvi` vs `epv` reconciliation, CSV export. | `get(hist/<today>)` once, then `onChildAdded(startAfter(lastKey))` |
| **History** | Day (with a second day overlaid for comparison), week (15-min `h15`), month and year tables from `daily`. | `get(hist/<date>)`, `get(h15/<date>)` × 7, `daily` key-range queries |
| **Energy** | Daily kWh bars for a month, monthly bars for a year, CSV export of a month, a year, or one day's minutes. | `daily` key ranges, `hist/<date>` |
| **Alerts** (was Events; `#/events` still works) | Three segments. **Rules**: your usage-threshold rules with on/off switches, the value the logger compares, its state (idle / pending / firing / fired …), presets and a full-screen editor. **History**: open alerts first, the last 7 days (+ "load 7 more"), filters All / Rules / Built-in / E-mail and minimum severity, value vs limit, an e-mail status chip, **Acknowledge** (writes `ack:true`), code legends. **E-mail**: recipients, limits, built-in alerts, daily summary, logger-offline mail, mailer status and **Send test e-mail**. See §4. | `alerts`, `mailer`, `.info/connected`; Rules adds `alertState`, `live`, `status`, `cfg`, `daily/<today>`; History `get(events/<date>)` per day |
| **Settings** | The `cfg` form (same ranges as the rules; the summary hour there is the ntfy **push**), **Send test alert** (also e-mailed when alert e-mails are on), **Re-probe inverter**, logger and inverter info, theme, cache clear, sign out, your UID. | `onValue(cfg)`, `onValue(status)`, `meta` |

The data contract (keys, units, paths) is exactly d1 §4.1–4.2. It is typed in [`src/data/types.ts`](src/data/types.ts) and enforced by [`../firebase/database.rules.json`](../firebase/database.rules.json).

## 1. Develop

You need Node 20.17 or newer and npm (the Mac already has Node v20.17.0 / npm 10.8.2). No global packages are needed.

```bash
cd web
npm install                # exact versions from package-lock.json
npm run dev                # http://localhost:5173  (also on your LAN IP, for the iPhone)
```

Open **http://localhost:5173/?demo=1** to see the whole app with fake data; no Firebase is needed (see §4).

To use your real database, create `web/.env` from the template (for `npm run dev` use `web/.env.local` with the **device** API key, because the browser key only accepts the production origins; see [r7 §6.2](../research/r7-web-hosting.md)):

```bash
cp .env.example .env       # then fill in the values
```

| Variable | Where to find it |
|---|---|
| `VITE_FIREBASE_API_KEY`, `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_PROJECT_ID`, `VITE_FIREBASE_APP_ID` | Firebase console → Project settings → Your apps → Web app → SDK setup and configuration |
| `VITE_FIREBASE_DATABASE_URL` | Realtime Database → Data tab, the URL at the top: `https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app` |
| `VITE_DEVICE_ID` | `inv1` (must match the firmware and the rules) |

These values ship inside the app and are **not secrets**; access control is the UID-pinned rules. `.env` is still git-ignored so that a fork of the repository does not point at your project. CI never reads `.env`: `deploy-web.yml` takes the same six values from GitHub repository **Variables** (§3).

> ⚠️ Without a `.env`, a build still succeeds but shows "Firebase is not configured" with a link to demo mode.

## 2. Check and build

```bash
npx tsc --noEmit           # type check (exit code 0 = OK)
npm run build              # -> web/dist/
npm run preview            # serves web/dist on http://localhost:4173 (with the service worker)
```

Build output on 2026-10-21 with the Alerts page (`npm run build`, sizes as Vite reports them):

| File | Size | gzip |
|---|---|---|
| `assets/firebase-*.js` (app + auth + database) | 324.8 kB | 69.4 kB |
| `assets/index-*.js` (the app, incl. code tables, alert catalogue and the Alerts page) | 120.3 kB | 40.7 kB |
| `assets/uplot-*.js` | 52.3 kB | 23.3 kB |
| `assets/preact-*.js` | 14.1 kB | 5.9 kB |
| `assets/demoSource-*.js` (loaded only with `?demo=1`) | 16.8 kB | 7.6 kB |
| `assets/firebaseSource-*.js` | 3.0 kB | 1.4 kB |
| CSS (app + uPlot) | 18.5 kB | 5.1 kB |
| `sw.js`, `index.html`, manifest | 7.0 kB | – |
| icons (4 PNG) | 20.3 kB | – |

A cold load is therefore about **147 KB** over the wire (the Alerts page added about 17 KB), about 175 KB including the service worker's one-time precache of icons and the demo chunk (d1 §4.6 budgeted 220 KB). After that the service worker serves the shell from the phone.

`dist/` also contains `_headers` and `.assetsignore` (copied from `public/`). They are Cloudflare configuration: never served, and never precached (`vite.config.ts` skips `_*` and dot files, and does not precache `/index.html`, which Cloudflare answers with a 307 to `/`).

`npm install` prints `EBADENGINE` warnings: the Firebase 13 packages declare `node >= 24.12` for their **server-side** use. The browser bundle does not run on Node, so this is harmless; installing Node 24 LTS silences it. `npm audit` reports a `@grpc/grpc-js` advisory through `@firebase/firestore`. Firestore and gRPC are never imported, so they are not in the bundle (check: `grep -l grpc dist/assets/*.js` prints nothing).

## 3. Hosting and CI/CD ([r7](../research/r7-web-hosting.md))

> ⚠️ **The origin is permanent.** `https://solar.<acct>.workers.dev` (Worker name `savx-logger` in `wrangler.jsonc` + your Cloudflare account subdomain) owns the Home Screen icon, the sign-in, the service worker and the history cache. Renaming either, adding a custom domain or switching hosts means re-installing the app on every phone. Do not "Add to Home Screen" before the checks in [r7 §7.1](../research/r7-web-hosting.md) steps 8–10 pass.

### 3.1 Files

| File | What it does |
|---|---|
| [`wrangler.jsonc`](wrangler.jsonc) | Assets-only Worker `savx-logger` (no script, so requests are never metered), serves `./dist`, SPA fallback (`not_found_handling: single-page-application`), `preview_urls: false` |
| [`public/_headers`](public/_headers) | CSP (self + Identity Toolkit + Secure Token + `*.asia-southeast1.firebasedatabase.app` incl. `wss`), HSTS, `nosniff`, `DENY` framing, COOP, `X-Robots-Tag: noindex`; `immutable` for `/assets/*`, `no-cache` for `/`, `/index.html`, `/sw.js`, the manifest |
| [`public/.assetsignore`](public/.assetsignore) | Keeps `.DS_Store` off the CDN |
| [`../firebase.json`](../firebase.json) | **Fallback only**: Firebase Hosting with the same headers. It has no `database` block: rules deploy through `tools/render-rules.sh` (see [firebase/README.md](../firebase/README.md)) |
| [`../.github/workflows/`](../.github/workflows) | `ci.yml`, `deploy-web.yml`, `deploy-rules.yml` (below) |

The CSP needs no change for e-mail alerts: the app only talks to RTDB (`mailq/test`, `alerts/*`), never to Apps Script ([r7 §3.8](../research/r7-web-hosting.md)).

### 3.2 GitHub Actions

| Workflow | Runs on | Does |
|---|---|---|
| `ci.yml` | every PR and push to `main` (not docs-only) | `gen_codes.py --check` (also checks `alert-metrics.json` against the rules), RTDB rules lint + test render; then, if touched, web `npm ci` → `typecheck` → `build` → dist checks, firmware `pio run -e savx` + `pio test -e native`, and the mailer's `npm run check` + `npm test` (`tools/apps-script`) |
| `deploy-web.yml` | push to `main` touching `web/`, `shared/`, `tools/gen_codes.py`; manual | Repeats the gate, then `npx wrangler@4.146.0 deploy` (tagged with the commit). PR previews only if the variable `CF_PREVIEWS` is `true` (off; previews are public) |
| `deploy-rules.yml` | rules or render-script changes; manual | Renders the UIDs into `.deploy/`, keyless Google login (WIF), `firebase deploy --only database` (`--dry-run` on PRs) |

Secrets (by name): `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID`, and `GCP_SA_KEY` only if Workload Identity Federation cannot be used. Variables: `VITE_FIREBASE_API_KEY` (browser key), `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_PROJECT_ID`, `VITE_FIREBASE_APP_ID`, `VITE_FIREBASE_DATABASE_URL` (must be the asia-southeast1 URL), `VITE_DEVICE_ID`, `FIREBASE_PROJECT_ID`, `FIREBASE_OWNER_UID`, `FIREBASE_DEVICE_UID`, `FIREBASE_MAILER_UID` (`none` until the mailer exists), `GCP_WIF_PROVIDER`, `GCP_RULES_SA`, optional `CF_PREVIEWS`. Where each value comes from: [r7 §4.6](../research/r7-web-hosting.md).

GitHub Free has no branch protection on private repositories, so a red `ci` does not block a merge; each deploy workflow repeats its own checks before deploying.

### 3.3 Manual deploy from the Mac (you run these; the agent never does)

wrangler 4.146.0 needs Node ≥ 22. The Mac's default Node is 20.17, but nvm has 24.21.0:

```zsh
source ~/.nvm/nvm.sh && nvm use 24.21.0            # this terminal only
cd ~/Office/CodeBase/Personal/Datalogger/web
npm ci && npm run typecheck && npm run build
npx --yes wrangler@4.146.0 login                     # once; opens the browser
npx --yes wrangler@4.146.0 deploy --dry-run
npx --yes wrangler@4.146.0 deploy --tag "mac-$(date +%Y%m%d%H%M)" --message "manual deploy from Mac"
```

On plain Node 20.17 use `npx --yes wrangler@4.86.0 deploy` (the last release that runs on Node 20; emergency only). Local test with the production headers: `npx --yes wrangler@4.146.0 dev` (port 8787; `npm run preview` ignores `_headers`). Rollback: `npx --yes wrangler@4.146.0 versions list`, then `rollback <VERSION_ID>` ([r7 §7.3](../research/r7-web-hosting.md)).

**Fallback host** (only if Cloudflare is unusable for days; a new origin, so every phone re-installs):

```zsh
cd ~/Office/CodeBase/Personal/Datalogger/web && npm run build && cd ..
npx --yes firebase-tools@15.32.1 deploy --only hosting --project <project-id>
```

Then on the iPhone, follow [docs/06](../docs/06-cloud-and-dashboard-guide.md): Safari → the origin → Share → **Add to Home Screen** → open the new icon → sign in **inside** it.

## 4. Alerts: rules and e-mail ([r8](../research/r8-email-alerts.md))

The logger (ESP32) evaluates the rules every 5 s, also while the internet is down, and writes an event plus an outbox job (`mailq`). A Google Apps Script **mailer** on a dedicated Gmail account sends the e-mails every 5 min (Firebase Spark has no Functions). The app only edits the configuration and shows the results:

| Path | Written by | The app |
|---|---|---|
| `alerts/rules/r01…r16` | owner | Rules segment. New rules claim the first free slot with a **transaction**; every change also writes `cfg/alertsRev = serverTimestamp()` in the same update, so the logger re-reads `alerts.json` on its next 60 s `cfg` poll |
| `alerts/email` | owner | E-mail segment: `on`, `to` (≤ 3 addresses, comma string; position = bit of a rule's `rc` mask), `fromName`, `maxPerDay` (1–40), `monthStart` (1–28), `builtin`, `summary`, `offline`. Removing an address remaps every rule's `rc` in the same update (a rule left with no address goes to everyone, after a confirm) |
| `alertState/<rid>` | logger | Badge: idle / pending (with "above for 3:40 of 5:00") / firing / recovering / fired / checked / invalid |
| `status.arv`, `status.acat` | logger | "✓ Logger has your latest rules (synced hh:mm)" when `arv === cfg.alertsRev`; metrics newer than `acat` are hidden in the editor |
| `events/<date>/<key>` `rid`, `val`, `thr`, `em`, `emt` | logger, mailer (`em`, `emt`) | History: value vs limit and the e-mail chip (`q` queued, `sent` e-mailed hh:mm, `cool`, `rcap`, `cap`, `quota`, `off`, `norc`, `exp`, `fail`, `skip`) |
| `mailq/test` `{ts[, rid]}` | owner | **Send test e-mail** (disabled 60 s); `ts` is server time from `.info/serverTimeOffset` (the rules accept ±300 s). The result appears when `mailer/test.ts` matches; after 6 min "the mailer has not run yet" |
| `mailer` | mailer | Heartbeat card; **red "Mailer not running"** when `hb` is older than 15 min |

**Validation.** [`src/data/alerts.ts`](src/data/alerts.ts) `validateRule()` / `validateEmailCfg()` implement exactly the `.validate` rules in [`../firebase/database.rules.json`](../firebase/database.rules.json), so a "permission denied" on save should be unreachable. The metric ids, the energy set and every value range come from [`../shared/alert-metrics.json`](../shared/alert-metrics.json) via the generated `src/alertMetrics.ts`, and `tools/gen_codes.py --check` fails if the rules file disagrees. `sh ../tools/rules-test/run.sh` fuzzes ~5,400 documents through both and requires zero disagreements ([firebase/README.md](../firebase/README.md) §6).

**Editor details:** energy is typed in kWh and stored in Wh; a comma or a dot works as the decimal mark ("2,500" with exactly three digits after the comma is read as 2500). "Below" rules on today's energy are checked once at a set time (energy only grows), month rules only allow > / ≥, latched energy rules have no recovery and fire at most once per day or billing cycle. Presets are r8 §6.3's nine. Saving is disabled while `.info/connected` is false.

> ⚠️ **Deploy order: rules → mailer → app → firmware** (r8c M6). An app that writes `cfg/alertsRev` or `alerts/*` before the new rules are live gets "The database rejected this change". Grid figures (`gw`, `egr`) are estimates from the inverter, not the utility meter, and are labelled "est." everywhere.

## 5. Demo mode (`?demo=1`)

`https://<your-site>/?demo=1` (or `http://localhost:5173/?demo=1`) runs the whole UI on a deterministic simulation instead of Firebase. The simulation models a 3.2 kW / 24 V inverter with about 2.6 kWp of PV, a 100 Ah LiFePO4 battery, SBU priority (back to grid at 40 %, back to battery at 80 %), daily load-shedding outages and typical household loads. It also produces events (grid lost/restored, battery low, daily summary) and a 7-minute data gap every few days, so that gaps show in the charts.

- The Firebase SDK is **not downloaded** in demo mode (it is a separate chunk loaded only by the Firebase source).
- Writes (`cfg`, ack, rules, e-mail settings) stay in memory; reload to reset.
- **Alerts in demo:** seven rules (one off), replayed minute by minute through a simplified copy of the logger's rule engine over the simulated day, so History shows rule events with "e-mailed", "cooldown" and "queued" chips, the Rules badges and values move, the mailer heartbeat looks alive, a save shows "Waiting for the logger…" and then "synced" about 4 s later, and **Send test e-mail** answers after 3 s.
- A **DEMO · fake data** pill is always visible. Tap it to leave demo mode.
- Every number is fake. Do not use them for sizing decisions.

## 6. iPhone (iOS PWA) notes

- **Install:** iOS 26 opens every site added to the Home Screen as a web app by default. On iOS 16.4–18, use Safari → Share → Add to Home Screen.
- **Sign in inside the installed app.** A Home Screen app keeps its own storage, separate from Safari. After sign-in the app calls `navigator.storage.persist()`. Home Screen apps are exempt from Safari's 7-day storage cap (V2 I2).
- `viewport-fit=cover` with `env(safe-area-inset-*)` padding, a `black-translucent` status bar, a 180×180 `apple-touch-icon`, and 16 px inputs so iOS does not zoom.
- **No background work and no Web Push.** Alerts arrive through ntfy / Telegram / Healthchecks (d1 §7) and e-mail (the mailer). The app only shows data while it is open.
- The rule editor is a full-screen sheet with 16 px inputs (no zoom), `inputmode="decimal"` for values, native time pickers and a Save bar above the home indicator.
- **CSV export** uses the share sheet when iOS supports sharing files ("Save to Files"), and a plain download otherwise.
- **Updates:** after a deploy, the next app open shows "A new version of the app is ready → Reload".

## 7. Bandwidth discipline (d1 §6.4)

Spark gives 10 GB/month of RTDB download. The client keeps its share small:

- It never subscribes to the roots of `hist`, `h15`, `events` or `daily`. Every read names a date or a key range.
- Listeners are attached only while their page is on screen **and** the app is visible. On `visibilitychange` → hidden the app also calls `goOffline()`, closing the WebSocket; it reconnects when visible again.
- Today's minutes are kept in memory for the session. Re-opening Today downloads only the minutes added since (`startAfter(lastKey)`). **Reload** re-reads the whole day, which also picks up minutes the logger back-filled after an outage.
- Finished days (`hist`, `h15`) and finished months (`daily`) are cached in IndexedDB. **Deviation from d1:** d1 caches every `date < today`. This app waits until a day is 2 days old (or has all 1440 minutes), because the logger may still back-fill yesterday after an internet outage. Settings → **Clear history cache** empties it.

- **Alerts** listens to `alerts` (≤ 6 KB, changes only when you edit), `mailer` (≈ 300 B every 5 min) and `.info/connected` (local) only while the page is on screen; Rules adds `alertState` (≈ 1.5 KB, on rule transitions), `live`, `status`, `cfg` and `daily/<today>`, plus one `daily/<cycle start..yesterday>` range read when a month rule exists. History reads `events/<date>` once per day shown.

Expected cost: about 350 KB to open Today at the end of a day, about 2 KB per minute while it stays open, and about 414 B per 10 s on Live or Alerts > Rules.

## 8. Code tables and the alert metric catalogue

`src/codes.ts` and `src/alertMetrics.ts` are **generated**. Do not edit them by hand.

```bash
python3 ../tools/gen_codes.py          # regenerate from ../shared/codes.json and ../shared/alert-metrics.json
python3 ../tools/gen_codes.py --check  # exit 1 if stale or if alert-metrics.json disagrees with the rules (CI runs this)
npm run codes                          # same as the first line
```

`shared/codes.json` holds the QMOD letters, the QPIGS status bits, the QPIWS bit tables (P2014 32-char, PMAX/PGK 36-char, P2015 38-char), the Savx manual's LCD fault and warning codes, the REVO QALL warning and fault codes, and the event types. The **LCD codes, QPIWS bits and REVO codes are different numbering schemes**. The app chooses the table from `meta.dia`: `REVO` → REVO tables; otherwise the Voltronic/LCD table. A 36-char QPIWS uses PMAX for `MAX24` and PGK otherwise.

`shared/alert-metrics.json` lists the 22 rule metrics (id, label, group, canonical unit, scopes, value range, default hysteresis, `est`, `since`). Adding a metric means: add it there **and** to the three places in `firebase/database.rules.json` (metric regex, energy regex if it is a total, value range), bump `version`, regenerate, and teach the firmware (`status.acat`).

## 9. Icons

`public/icons/*.png` are generated by a dependency-free script (a small signed-distance renderer plus Node's built-in zlib PNG encoder):

```bash
npm run icons
```

## 10. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Red **"No access … UID …"** banner after sign-in | The UID shown is not the `OWNER_UID` of the deployed rules. Copy it from the banner or from Settings into the repository variable `FIREBASE_OWNER_UID` and run `deploy-rules` (or render and deploy from the Mac, [firebase/README.md](../firebase/README.md) §3–4). Never put real UIDs into the committed rules file. |
| **"Logger offline"** | `live.ts` is more than 60 s old: the ESP32 has no power, Wi-Fi or internet. Healthchecks.io also e-mails you after its grace period. |
| **"Inverter link down"** | The logger is online, but `lk = 0`: no valid inverter reply. See [docs/08](../docs/08-risks-troubleshooting-maintenance.md). |
| "Firebase is not configured" | `web/.env` was missing at build time (Mac), or a repository Variable is unset (CI fails earlier with "repository variable … is not set"). |
| Alerts: "The database rejected this change" | The new rules are not deployed yet (deploy order: rules first). Run `deploy-rules` or [firebase/README.md](../firebase/README.md) §4. |
| Alerts: "Waiting for the logger to load the new rules" for more than 2 min | The logger is offline or its firmware predates alert rules (then the banner says so). Rules stay saved and apply when it runs the alerts firmware. |
| Alerts: red "Mailer not running" | `mailer.hb` is older than 15 min: open script.google.com as the sender account, check Triggers and Executions ([r8 §7](../research/r8-email-alerts.md)). |
| Test e-mail: "no recipients" | Add the address in Alerts → E-mail **and** to the mailer's `ALLOW_TO` script property. |
| CSP errors in Safari's Web Inspector | The database URL is not in asia-southeast1, or a new external call was added: update `public/_headers` and `../firebase.json` together ([r7 §3.8](../research/r7-web-hosting.md)). |
| Sign-in works in Safari but not in the Home Screen app | They have separate storage. Sign in inside the installed app. |
| An old version keeps showing | Open the app, wait for "A new version… Reload", tap it. As a last resort: Settings app → Safari → Advanced → Website Data → remove the site, then reinstall. |

## 11. Layout

```
web/
  index.html               iOS meta, manifest, apple-touch-icon
  sw.template.js           service worker; vite.config.ts injects the precache list -> dist/sw.js
  vite.config.ts           Preact JSX, vendor chunks, SW plugin (precache skips _headers, dotfiles, /index.html)
  wrangler.jsonc           Cloudflare Workers static assets (assets-only Worker "savx-logger"; never rename)
  public/_headers          CSP, security and cache headers (Cloudflare); public/.assetsignore
  public/manifest.webmanifest, public/icons/
  scripts/gen-icons.mjs
  src/
    main.tsx               picks the data source (demo / Firebase), registers the SW
    app.tsx                auth gate, hash router (#/live …), shell, banners
    codes.ts               GENERATED from shared/codes.json
    alertMetrics.ts        GENERATED from shared/alert-metrics.json (checked against the RTDB rules)
    data/  types.ts (contract) · source.ts (interface) · firebaseSource.ts · demoSource.ts · history.ts (caching)
           alerts.ts (presets, validation = the rules, formats, current values) · demoAlerts.ts (demo rule replay)
    lib/   time.ts (Asia/Dhaka keys) · decode.ts · series.ts · csv.ts · idb.ts · hooks.ts · format.ts
    components/  UChart (uPlot) · Bars (SVG) · PowerFlow · Gauge · DayCharts · ui
    pages/ Live · Today · History · Energy · Alerts · Settings · Login
           alerts/ RulesTab · RuleEditor · HistoryTab (the old Events page) · EmailTab
```
