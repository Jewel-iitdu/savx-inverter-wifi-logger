import { render } from 'preact';
import './styles.css';
import { App } from './app';
import { ConfigMissingError, type DataSource } from './data/source';
import { t, tj } from './lib/i18n';

const root = document.getElementById('app')!;

function ConfigMissing({ missing }: { missing: string[] }) {
  return (
    <div class="login">
      <img src="/icons/icon-192.png" alt="" width="72" height="72" class="login-icon" />
      <h1>{t('cfgMissing.title')}</h1>
      <p>{tj('cfgMissing.body', { src: <code>web/.env.example</code>, dst: <code>web/.env</code> })}</p>
      <p class="muted small">{t('cfgMissing.list', { list: missing.join(', ') })}</p>
      <p>
        <a class="btn" href="?demo=1">
          {t('demo.preview')}
        </a>
      </p>
    </div>
  );
}

async function createSource(): Promise<DataSource> {
  // ?demo=1 renders realistic fake data without loading the Firebase SDK.
  if (new URLSearchParams(location.search).get('demo') === '1') {
    const { createDemoSource } = await import('./data/demoSource');
    return createDemoSource();
  }
  const { createFirebaseSource } = await import('./data/firebaseSource');
  return createFirebaseSource();
}

createSource()
  .then((source) => render(<App source={source} />, root))
  .catch((e: unknown) => {
    if (e instanceof ConfigMissingError) render(<ConfigMissing missing={e.missing} />, root);
    else {
      console.error(e);
      root.textContent = t('app.startFailed', { msg: e instanceof Error ? e.message : String(e) });
    }
  });

// Service worker (production builds only). A new version waits until the
// user taps "Reload" in the app (see sw.template.js).
if (import.meta.env.PROD && 'serviceWorker' in navigator) {
  const hadController = !!navigator.serviceWorker.controller;
  window.addEventListener('load', () => {
    navigator.serviceWorker
      .register('/sw.js')
      .then((reg) => {
        const notify = () => window.dispatchEvent(new CustomEvent('sw-update', { detail: reg }));
        if (reg.waiting && navigator.serviceWorker.controller) notify();
        reg.addEventListener('updatefound', () => {
          const next = reg.installing;
          next?.addEventListener('statechange', () => {
            if (next.state === 'installed' && navigator.serviceWorker.controller) notify();
          });
        });
        document.addEventListener('visibilitychange', () => {
          if (document.visibilityState === 'visible') reg.update().catch(() => undefined);
        });
      })
      .catch((err) => console.warn('Service worker registration failed', err));
  });
  let reloading = false;
  navigator.serviceWorker.addEventListener('controllerchange', () => {
    if (!hadController || reloading) return;
    reloading = true;
    location.reload();
  });
}
