# Verification of the hosting reports, checked 2026-10-21

I fetched each claim from official pages today. Some were also checked with `git ls-remote`, the npm and PyPI registries, `action.yml` files, and the local `@firebase/auth` 1.13.7 source in `web/node_modules`. Claims I could not confirm are listed as unverified.

## Corrections

| # | Claim (report) | What is actually true | Source |
|---|---|---|---|
| C1 | The Landscape workflow uses `actions/checkout@v4`, `actions/setup-node@v4` and `cloudflare/wrangler-action@v3`. It also says "wrangler-action@v3 is the current major". | **Refuted.** All three declare `using: node20` in `action.yml`. Node 20 was removed from GitHub Actions runners on 2026-09-23 ("Runners now use Node 24 for JavaScript actions"; the opt-out is gone). The current major of wrangler-action is **v4**: the README says `uses: cloudflare/wrangler-action@v4`, v4.1.3 is "Latest" (24 Sep) and it uses node24. v4.1.0 and v4.1.1 are flagged broken. v3 installs Wrangler 3.90.0 unless Wrangler is already installed in the project. **Fix:** use the Integration report's pins, or `wrangler-action` SHA `953926a2e2182532811c01a25e53647d93bf07c0` (v4.1.3). | https://github.blog/changelog/2026-09-23-node-20-is-no-longer-available-in-github-actions/ · https://github.com/cloudflare/wrangler-action · https://github.com/cloudflare/wrangler-action/releases · raw `action.yml` of each tag [CONFIRMED-primary] |
| C2 | Landscape: "Workers Builds comments the preview URL on PRs… Needs `preview_urls: true`". | **Wrong mechanism.** Workers Builds posts a build-status comment. "A Preview URL will be provided for builds which perform `wrangler preview`", and the default preview command is `npx wrangler preview`. Version URLs (`preview_urls`, `versions upload --preview-alias`) are a separate feature. There is also a version mismatch: the docs say Previews need Wrangler ≥4.135.0, while wrangler-action v4 says `preview` needs **≥4.136.3**. Pinning 4.146.0 satisfies both. | https://developers.cloudflare.com/workers/ci-cd/builds/git-integration/github-integration/ · https://developers.cloudflare.com/workers/ci-cd/builds/configuration/ · https://developers.cloudflare.com/workers/previews/ [CONFIRMED-primary] |
| C3 | Landscape `wrangler.jsonc`: `"preview_urls": true`. | **Risky.** Version URLs are public ("the URL is public and available after version creation"). They are also **on by default whenever `workers_dev` is enabled**, and if `preview_urls` is omitted Wrangler leaves any existing setting unchanged. To get no public previews you must set `"preview_urls": false` explicitly, as the Integration report does. | https://developers.cloudflare.com/workers/configuration/previews/ [CONFIRMED-primary] |
| C4 | Landscape puts `_headers` in `web/public` and makes no change to the service worker. | **Missed a bug; the Integration report is right.** In the current `web/vite.config.ts`, `publicFiles()` skips only names that start with `.`, and line 38 hard-codes `'/index.html'` into PRECACHE. Cloudflare says `_headers` "will not itself be served as a static asset". The default `html_handling` is `auto-trailing-slash`, under which `/file/index.html` and `/folder/index.html` get a 307. The docs table does not list the root `/index.html`; the Integration report observed the 307 for it. Apply the Integration report's §3.3 edits (skip names starting with `_`; drop `/index.html` from the precache list). | local `web/vite.config.ts` · https://developers.cloudflare.com/workers/static-assets/headers/ · https://developers.cloudflare.com/workers/static-assets/routing/advanced/html-handling/ [CONFIRMED-primary; the root `/index.html` 307 is OBSERVED only] |
| C5 | Landscape step 4: add the origin to Authorized domains, or "password-reset links… will not [work]". | **Overstated.** In the SDK, `_validateOrigin` is called only from `_openRedirect()` and the popup flow (`index-LY22yodV.js`, lines 9761 and 10779). The docs tie authorized domains to redirect sign-in and to **continue URLs** ("you need to add the domain for the URL as an authorized domain"). Email/password sign-in does not need the domain. Password reset needs it only if you pass an `ActionCodeSettings.url` on the new origin. | local SDK source · https://firebase.google.com/docs/auth/faq-and-troubleshooting · https://firebase.google.com/docs/auth/web/passing-state-in-email-actions [CONFIRMED-primary] |
| C6 | Integration: "Whether Zero Trust onboarding asks for a card: unverified." | **It does ask.** The docs say "Complete your onboarding by selecting a subscription plan and entering your payment details… If you chose the Zero Trust Free plan, this step is still needed but you will not be charged." So protecting previews with Cloudflare Access breaks the no-card rule. Keep previews off. | https://developers.cloudflare.com/cloudflare-one/setup/ [CONFIRMED-primary] |
| C7 | Both reports cite the Pages→Workers migration guide for "Pages treats a project as an SPA when there is no `404.html`". | The claim is **true, but the source is wrong**. The migration guide only says Pages looks at `404.html`/`index.html` "as signals". The behaviour is stated on serving-pages: "Pages assumes that you are deploying a single-page application." | https://developers.cloudflare.com/pages/configuration/serving-pages/ [CONFIRMED-primary] |
| C8 | Landscape (Netlify): when credits run out, "no production deploys are possible" and the pause lasts "until the next cycle". | **Not stated.** The page says only: "all of your web projects (sites/apps) are paused and visitors… will find a `Site not available` page", and "The balance resets at the start of each cycle". | https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/how-credits-work/ |
| C9 | Landscape (Render): 5 GB, "down from 100 GB". | 5 GB, $0.15/GB, and "spins down your workspace's services until the start of the next month" are all confirmed. "Down from 100 GB" is **not on the fetched pages**. The changelog says only "less bandwidth than their legacy counterparts". | https://render.com/docs/outbound-bandwidth · https://render.com/changelog/updated-plans-for-render-workspaces |
| C10 | Landscape (Firebase): the pricing page and the quota docs "state the limit differently". | **Minor correction.** The pricing page itself treats 360 MB/day as roughly 10 GB/month, so the two numbers describe the same allowance. Whether it is enforced per day or per month is not documented. | https://firebase.google.com/pricing · https://firebase.google.com/docs/hosting/usage-quotas-pricing |
| C11 | Landscape `_headers`: `Content-Type: application/manifest+json` on `/*.webmanifest`. | **Not needed.** Wrangler sets the MIME type from the file extension, and mime-db maps `webmanifest` to `application/manifest+json`. `_headers` values "override what Cloudflare ordinarily sends", so the line is harmless, but it can be dropped. | https://developers.cloudflare.com/workers/static-assets/headers/ · mime-db@1.54.0 |

## Confirmed

1. **Cloudflare static-asset requests are free.** "Requests to static assets are free and unlimited"; "There is no additional cost for storing Assets." Requests that invoke a Worker script are billed under Workers pricing. If a `run_worker_first` route goes over the free limit it returns 429. Sources: https://developers.cloudflare.com/workers/static-assets/billing-and-limitations/ · https://developers.cloudflare.com/workers/platform/pricing/
2. **Workers Free limits.** 100,000 requests/day, reset at midnight UTC, Error 1027 when exceeded. Static assets: 20,000 files per version on Free (needs Wrangler ≥4.34.0) and 25 MiB per file. Source: https://developers.cloudflare.com/workers/platform/limits/
3. **PoPs near Dhaka.** The network page lists "Dhaka, BD" and "Chittagong, BD", plus Kolkata, Kathmandu and Thimphu (348 cities in total). [OBSERVED] today from this Mac: `cf-ray …-DAC` on unpkg.com. Source: https://www.cloudflare.com/network/
4. **SPA mode.** `"assets": { "not_found_handling": "single-page-application" }` serves `/index.html` with 200. The navigation-preference behaviour applies with a compatibility date of 2025-04-01 or later. Source: https://developers.cloudflare.com/workers/static-assets/routing/single-page-application/
5. **`_headers` rules.**
   - Up to 100 rules, 2,000 characters per line, one splat per pattern, and `! Header` removes a header.
   - Values from several matching rules are comma-joined, and the file is not served itself.
   - The default is `public, max-age=0, must-revalidate` plus an ETag.
   - `_headers` does not apply to responses generated by Worker code.

   Source: https://developers.cloudflare.com/workers/static-assets/headers/
6. **Workers Builds (Free).** 3,000 minutes/month, 1 concurrent build, 20-minute timeout, 2 vCPU / 8 GB. The GitHub App can be limited to "Only select repositories", and both personal accounts and organisations work. Private repos are **not mentioned**. "Build variables will not be accessible at runtime." Sources: https://developers.cloudflare.com/workers/ci-cd/builds/limits-and-pricing/ · https://developers.cloudflare.com/workers/ci-cd/builds/configuration/
7. **Custom domains on Workers.** "Workers does not support any domain whose nameservers are not managed by Cloudflare." Source: https://developers.cloudflare.com/workers/static-assets/migration-guides/migrate-from-pages/
8. **Cloudflare Pages.**
   - 500 builds/month, 1 concurrent build, 20 minutes, 20,000 files, 25 MiB per file.
   - 100 custom domains per project, 100 projects, 100 header rules, 2,000 + 100 redirects.
   - The Pages landing page says "Start new projects with Workers" (updated Aug 25, 2026). It does **not** call Pages deprecated.

   Sources: https://developers.cloudflare.com/pages/platform/limits/ · https://developers.cloudflare.com/pages/
9. **Workers Previews and Version URLs.**
   - URL format: `<prefix>-<worker>.<sub>.workers.dev`.
   - `--preview-alias` exists; Previews need Wrangler ≥4.135.0; 100 Previews per Worker on Free; "Preview URLs are public by default".
   - The CI example uses `npx wrangler preview --name "pr-N" --json`.

   Sources: https://developers.cloudflare.com/workers/previews/ · https://developers.cloudflare.com/workers/previews/automation-examples/
10. **Vercel Hobby limits.**
    - Usage: 100 GB Fast Data Transfer, 10 GB Fast Origin Transfer, 1,000,000 CDN requests.
    - Deploys and builds: 100 deployments/day, 100 builds/hour, 1 concurrent build, 45-minute builds.
    - Uploads and files: 100 MB static upload, 15,000 source files per CLI deploy.
    - Account: 200 projects, 50 domains per project.
    - Going over: "you will have to wait until 30 days have passed".

    Sources: https://vercel.com/docs/plans/hobby · https://vercel.com/docs/limits
11. **Vercel commercial-use terms.** "Hobby teams are restricted to non-commercial personal use only." Commercial means financial gain for anyone involved; ads count; "Asking for Donations does not fall under commercial usage." Source: https://vercel.com/docs/limits/fair-use-guidelines
12. **Vercel and Git.**
    - "Vercel does not support connecting a project on your Hobby team to Git repositories owned by Git organizations."
    - For a personal repo you must be its **Owner**.
    - The documented Actions flow is `vercel pull`, then `vercel build --prod`, then `vercel deploy --prebuilt --prod`.

    Sources: https://vercel.com/docs/limits · https://vercel.com/docs/git/vercel-for-github
13. **Vercel rewrites.** "precedence is given to the filesystem prior to rewrites being applied." Source: https://vercel.com/docs/project-configuration/vercel-json
14. **Vercel Deployment Protection.** On Hobby, Vercel Authentication can protect both preview and production ("All Deployments" is available on all plans). Standard Protection covers everything except production domains. Password Protection is "Not available on Hobby". Source: https://vercel.com/docs/deployment-protection
15. **Vercel network.** 126 PoPs in 94 cities and 51 countries, 19 regions; the nearest regions are bom1 and sin1, and there is no Dhaka region. The page also says "The Vercel region the request is routed to handles TLS", which explains the ~100 ms TLS time observed from Dhaka. Source: https://vercel.com/docs/regions
16. **Netlify credit plans.**
    - Credit plans apply to new accounts from "September 4, 2025". Free is 300 credits/month with a hard limit and no recharge.
    - Costs: a production deploy is 15 credits, 1 GB is 20 credits, 10,000 requests are 2 credits.
    - Deploy previews and branch deploys are not metered. Organisation-owned private repos are "N/A" on Free.
    - "You cannot revert to a Legacy plan."

    Source: https://docs.netlify.com/manage/accounts-and-billing/billing/billing-for-credit-based-plans/credit-based-pricing-plans/
17. **GitHub Pages.**
    - On GitHub Free "the repository must be public". A private site needs an organisation on Enterprise Cloud.
    - Limits: 1 GB site, 100 GB/month soft bandwidth, 10 builds/hour soft (not applied to custom Actions workflows), 10-minute deploy timeout, 429 when rate-limited.
    - "GitHub Pages sites shouldn't be used for sensitive transactions like sending passwords." That sentence is on the **limits** page, not the what-is page.

    Sources: https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site · https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits · https://docs.github.com/en/get-started/learning-about-github/githubs-plans
18. **GitHub Actions and environments.** Private repos on Free get 2,000 minutes and 500 MB storage; Pro gets 3,000 minutes. "usage is blocked once you use up your quota" when no payment method is on file. "Users with GitHub Free plans can only configure environments for public repositories." Sources: https://docs.github.com/en/billing/concepts/product-billing/github-actions · https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments
19. **Integration report action pins.** `git ls-remote` gives the same SHAs the report lists, and every one declares `using: node24`:
    - checkout v7.0.1 = `3d3c42e5…`
    - setup-node v7.0.0 = `82076278…`
    - setup-python v7.0.0 = `5fda3b95…`
    - cache v6.1.0 = `55cc8345…`
    - google-github-actions/auth v3.0.0 = `7c6bc770…`

    The Firebase hosting action's `expires` default is `7d`.
20. **CLI version pins** (npm and PyPI registries). wrangler 4.146.0 was published 2026-10-01 (Node ≥22), vercel 62.4.0 on 2026-10-05, firebase-tools 15.32.1 on 2026-09-30, and platformio 6.2.0 on 2026-09-05 (Python ≥3.9). All are at least two weeks old. setup-node v7.1.0 is from Oct 8, so it is too new; keep v7.0.0.
21. **Firebase Hosting on Spark.**
    - 10 GB storage; transfer listed as 360 MB/day on the pricing page and 10 GB/month in the quota docs.
    - Past the quota: "a short grace period but then your sites will be disabled" until the next month.
    - 2 GB maximum per file, and a redeploy "automatically clears all your cached content across the CDN".
    - RTDB on Spark allows 100 simultaneous connections; App Hosting is "Not applicable" on Spark.

    Sources: the pricing and quota pages above · https://firebase.google.com/docs/hosting/manage-cache
22. **`firebase init hosting:github`.** Works with "public or private" repos. It creates a service account and stores its JSON key as a GitHub secret, deploys a preview channel per PR, and optionally deploys live on merge. Source: https://firebase.google.com/docs/hosting/github-integration
23. **Firebase API keys.**
    - Keys restricted to Firebase services "do not need to be treated as secrets". Since May 2024 new keys are auto-restricted to Firebase APIs. Test restriction changes on a test key first. The page was updated 2026-10-20.
    - "You can set only one type of client restrictions per API key." Documented referrer patterns are `example.com/*` and `*.example.com/*`.
    - `localhost` is not authorized by default in projects created after Apr 28, 2025.

    Sources: https://firebase.google.com/docs/projects/api-keys · https://docs.cloud.google.com/api-keys/docs/add-restrictions-api-keys · https://firebase.google.com/docs/auth/faq-and-troubleshooting
24. **Other platforms.**
    - Azure sign-up needs "a phone number, a credit card or a debit card (non-prepaid)". The SWA Dedicated plan was retired on Oct 31, 2025. SWA Free: 250 MB per app, 2 custom domains, 3 staging environments, no SLA.
    - Deno Deploy Classic was "shut down on July 20, 2026". The new Free tier: 1M requests, 20 GiB, 15 builds/hour, 1 concurrent build, 5 domains.
    - Fastly lists Kolkata, Guwahati and Siliguri, but no Dhaka or anywhere in Bangladesh.
    - Cloudflare's 2025-12-05 outage lasted about 25 minutes and hit about 28% of HTTP traffic.

    Sources: https://azure.microsoft.com/en-us/pricing/purchase-options/azure-account · https://learn.microsoft.com/en-us/azure/static-web-apps/plans · https://docs.deno.com/deploy/classic/ · https://deno.com/deploy/pricing · https://www.fastly.com/network-map/ · https://blog.cloudflare.com/5-december-2025-outage/
25. **Build-time configuration.** In Vite, variables that already exist in the environment "have the highest priority". In the Firebase CLI, Application Default Credentials are "(Recommended)" and `FIREBASE_TOKEN` is "(Legacy)… no longer recommended". Sources: https://vite.dev/guide/env-and-mode · https://firebase.google.com/docs/cli

## Unverifiable (treat as unconfirmed)

- **No card for Cloudflare's free plan or for Vercel Hobby.** No fetched page says this. Cloudflare's create-account page asks only for an email and password [INFERRED].
- **Private-repo support** in Workers Builds and in Vercel Hobby with a personal GitHub account is not stated (GitHub Free is listed as a supported product) [INFERRED: yes].
- **`wrangler preview` with an assets-only Worker** is not documented.
- **Root `/index.html` → 307 on Workers** was observed by the Integration report only; it is not in the docs table.
- **Vercel's 100 MB / 15,000-file CLI limits.** The limits page links a changelog titled "CLI deployment limits removed", so these numbers may be stale. This app is far below them either way.
- **Referrer restrictions:** patterns with ports (`localhost:5173/*`) and wildcards inside a label are undocumented.
- **Not re-fetched:** Netlify having only a Singapore PoP in Asia; GitHub Pages' fixed Cache-Control; whether Workload Identity Federation works on a Spark project with no billing; Deno's overage behaviour and card requirement; Azure's 100 GB per subscription; Codeberg, Surge and GitLab; Firebase Hosting being served by Fastly; RTDB not checking `Origin`; the secret-scanning pattern flags.

## Missed options and risks

1. **Node 24 runners (from C1).** Any workflow with a node20 action now runs it on Node 24 without being tested on it. Use only the Integration report's SHA-pinned v7/v6/v3 actions and wrangler-action v4.
2. **Cloudflare Access needs a card (from C6).** So "free, card-less, protected previews" is not possible on Cloudflare. Keep `preview_urls: false` and no `wrangler preview` step by default.
3. **`workers.dev` is on the Public Suffix List**, so `<acct>.workers.dev` is the registrable "site". Every Worker and preview on that account subdomain is same-site with the app. IndexedDB and the service worker stay per-origin, so this is low risk for a single owner. pages.dev, web.app, vercel.app and netlify.app are also on the list. Source: https://publicsuffix.org/list/public_suffix_list.dat
4. **Keep the Worker assets-only.** If you add a `main` script later, script invocations count toward the 100k/day limit, and `_headers` stops applying to script-generated responses.
5. **Firebase Hosting is a stronger standby than the scores suggest.** It is already configured, adds no new vendor, gets a Fastly Kolkata edge (Integration report: TTFB 52–55 ms against Cloudflare DAC's 49–58 ms), and stops when over quota rather than billing. Its only real drawbacks are less convenient CI and public preview channels.
6. **The origin is permanent.** Both reports are right that the `workers.dev` account subdomain must be chosen before anyone adds the app to their Home Screen.