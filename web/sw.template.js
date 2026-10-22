/* Savx logger service worker (hand-written, no Workbox). d1 §6.5.
 *
 * - Precaches the app shell of THIS build: hashed /assets/* (cache-first),
 *   icons and manifest (stale-while-revalidate), the app shell '/' (network-first,
 *   cached copy when offline). '/index.html' is not precached: Cloudflare answers
 *   it with a 307 to '/' (research/r7-web-hosting.md section 3.4).
 * - Never touches cross-origin traffic: Firebase RTDB (*.firebasedatabase.app),
 *   Auth (googleapis.com) and everything else go straight to the network.
 * - A new version waits until the page asks it to take over (the app shows an
 *   "Update available" bar), so a running page never loses its chunks.
 *
 * vite.config.ts replaces the two placeholders below at build time.
 */
const VERSION = '__SW_VERSION__';
const PRECACHE = /*__PRECACHE__*/ [];
const SHELL = 'shell-' + VERSION;
const NAV_TIMEOUT_MS = 4000;

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(SHELL).then((cache) =>
      // cache: 'reload' bypasses the HTTP cache so we never precache a stale index.html.
      cache.addAll(PRECACHE.map((url) => new Request(url, { cache: 'reload' }))),
    ),
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((k) => k.startsWith('shell-') && k !== SHELL).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

self.addEventListener('message', (event) => {
  if (event.data === 'SKIP_WAITING') self.skipWaiting();
});

function isHtml(response) {
  return (response.headers.get('content-type') || '').includes('text/html');
}

async function networkFirstShell(request) {
  const cache = await caches.open(SHELL);
  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), NAV_TIMEOUT_MS);
    // Fetch by URL: a navigate-mode Request cannot be re-used with a RequestInit.
    const response = await fetch(request.url, { signal: controller.signal, cache: 'no-cache', credentials: 'same-origin' });
    clearTimeout(timer);
    if (response.ok && isHtml(response)) cache.put('/index.html', response.clone());
    return response;
  } catch (err) {
    const cached = (await cache.match('/index.html')) || (await cache.match('/'));
    if (cached) return cached;
    throw err;
  }
}

async function cacheFirst(request) {
  const cache = await caches.open(SHELL);
  const hit = await cache.match(request);
  if (hit) return hit;
  let response = await fetch(request);
  // A hashed asset missing from the live version is answered by the host's SPA fallback with
  // index.html, and Cloudflare sends it with the /assets/* "immutable" header, so the browser's
  // HTTP cache may hold HTML under that URL after a deploy + rollback. Retry once past the HTTP
  // cache (research/r7-web-hosting.md risk H11). Never cache HTML here.
  if (response.ok && isHtml(response)) response = await fetch(request, { cache: 'reload' });
  if (response.ok && !isHtml(response)) cache.put(request, response.clone());
  return response;
}

async function staleWhileRevalidate(request, event) {
  const cache = await caches.open(SHELL);
  const hit = await cache.match(request);
  const refresh = fetch(request)
    .then((response) => {
      if (response.ok) cache.put(request, response.clone());
      return response;
    })
    .catch(() => undefined);
  if (hit) {
    event.waitUntil(refresh);
    return hit;
  }
  const fresh = await refresh;
  return fresh || Response.error();
}

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET') return;
  const url = new URL(request.url);
  if (url.origin !== self.location.origin) return; // Firebase, googleapis, ntfy...: not ours
  if (url.pathname.startsWith('/__/')) return; // Firebase Hosting reserved URLs (fallback host)
  if (url.pathname.startsWith('/cdn-cgi/')) return; // Cloudflare reserved URLs (primary host)
  if (url.pathname === '/sw.js') return;

  if (request.mode === 'navigate' || url.pathname === '/' || url.pathname === '/index.html') {
    event.respondWith(networkFirstShell(request));
  } else if (url.pathname.startsWith('/assets/')) {
    event.respondWith(cacheFirst(request));
  } else {
    event.respondWith(staleWhileRevalidate(request, event));
  }
});
