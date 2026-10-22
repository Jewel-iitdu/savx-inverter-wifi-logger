import type { JSX } from 'preact';
import { useCallback, useEffect, useMemo, useState } from 'preact/hooks';
import type { AuthUser, DataSource } from './data/source';
import type { Meta } from './data/types';
import { Ctx, type AppCtx } from './context';
import { useStored, useSubscription, useVisible } from './lib/hooks';
import { Banner, LangButton, Spinner } from './components/ui';
import { LoginPage } from './pages/Login';
import { LivePage } from './pages/Live';
import { TodayPage } from './pages/Today';
import { HistoryPage } from './pages/History';
import { EnergyPage } from './pages/Energy';
import { AlertsPage } from './pages/Alerts';
import { SettingsPage } from './pages/Settings';
import { LANG_KEY, isLang, setCurrentLang, t, tj, type Key, type Lang } from './lib/i18n';

type RouteId = 'live' | 'today' | 'history' | 'energy' | 'alerts' | 'settings';

// 24x24 stroke icons
const ROUTES: Array<{ id: RouteId; label: Key; icon: string; page: () => JSX.Element }> = [
  { id: 'live', label: 'nav.live', icon: 'M13 2L4 14h7l-1 8 9-12h-7z', page: LivePage },
  { id: 'today', label: 'nav.today', icon: 'M3 17l5-6 4 4 8-9M3 21h18', page: TodayPage },
  { id: 'history', label: 'nav.history', icon: 'M3 12a9 9 0 1 0 3-6.7M3 4v5h5M12 7v5l3 2', page: HistoryPage },
  { id: 'energy', label: 'nav.energy', icon: 'M5 21V11M10 21V5M15 21v-8M20 21v-4', page: EnergyPage },
  { id: 'alerts', label: 'nav.alerts', icon: 'M6 8a6 6 0 1 1 12 0c0 7 3 9 3 9H3s3-2 3-9M10 21h4', page: AlertsPage },
  { id: 'settings', label: 'nav.settings', icon: 'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z', page: SettingsPage },
];

/** '#/alerts/rules/r03' -> 'alerts'. '#/events' (the page's old name) is an alias of Alerts (r8 §6.1). */
function readRoute(): RouteId {
  const first = location.hash.replace(/^#\/?/, '').split(/[/?]/)[0];
  const id = (first === 'events' ? 'alerts' : first) as RouteId;
  return ROUTES.some((r) => r.id === id) ? id : 'live';
}

/** Page + segment ('alerts/rules'); opening or closing a sheet ('alerts/rules/r03') keeps the scroll position. */
const scrollKey = () => location.hash.replace(/^#\/?/, '').split(/[?]/)[0].split('/').slice(0, 2).join('/');

function useRoute(): RouteId {
  const [route, setRoute] = useState<RouteId>(readRoute);
  useEffect(() => {
    let last = scrollKey();
    const on = () => {
      setRoute(readRoute());
      const key = scrollKey();
      if (key !== last && !(key.startsWith(last + '/') || last.startsWith(key + '/'))) window.scrollTo(0, 0);
      last = key;
    };
    window.addEventListener('hashchange', on);
    return () => window.removeEventListener('hashchange', on);
  }, []);
  return route;
}

function useTheme(pref: 'auto' | 'light' | 'dark'): 'light' | 'dark' {
  const mq = typeof matchMedia !== 'undefined' ? matchMedia('(prefers-color-scheme: dark)') : null;
  const [sysDark, setSysDark] = useState(() => !!mq?.matches);
  useEffect(() => {
    if (!mq) return;
    const on = () => setSysDark(mq.matches);
    mq.addEventListener('change', on);
    return () => mq.removeEventListener('change', on);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  useEffect(() => {
    const root = document.documentElement;
    if (pref === 'auto') delete root.dataset.theme;
    else root.dataset.theme = pref;
  }, [pref]);
  return pref === 'auto' ? (sysDark ? 'dark' : 'light') : pref;
}

function useOnline(): boolean {
  const [on, setOn] = useState(() => navigator.onLine);
  useEffect(() => {
    const up = () => setOn(true);
    const down = () => setOn(false);
    window.addEventListener('online', up);
    window.addEventListener('offline', down);
    return () => {
      window.removeEventListener('online', up);
      window.removeEventListener('offline', down);
    };
  }, []);
  return on;
}

function useSwUpdate(): ServiceWorkerRegistration | null {
  const [reg, setReg] = useState<ServiceWorkerRegistration | null>(null);
  useEffect(() => {
    const on = (e: Event) => setReg((e as CustomEvent<ServiceWorkerRegistration>).detail);
    window.addEventListener('sw-update', on);
    return () => window.removeEventListener('sw-update', on);
  }, []);
  return reg;
}

interface DataErr {
  where: string;
  text: string;
  denied: boolean;
}

export function App({ source }: { source: DataSource }) {
  const [user, setUser] = useState<AuthUser | null | undefined>(undefined);
  const visible = useVisible();
  const route = useRoute();
  const online = useOnline();
  const update = useSwUpdate();
  const [themePref, setThemePref] = useStored<'auto' | 'light' | 'dark'>('theme', 'auto');
  const theme = useTheme(themePref);
  const [langRaw, setLang] = useStored<Lang>(LANG_KEY, 'en');
  const lang: Lang = isLang(langRaw) ? langRaw : 'en';
  setCurrentLang(lang); // before any child renders a string
  useEffect(() => {
    document.documentElement.lang = lang;
  }, [lang]);
  const [dataErr, setDataErr] = useState<DataErr | null>(null);

  useEffect(() => source.onUser(setUser), [source]);
  useEffect(() => source.setActive(visible), [source, visible]);
  useEffect(() => setDataErr(null), [user?.uid]);

  const meta = useSubscription<Meta | null>((cb, err) => source.onMeta(cb, err), [source, user?.uid], visible && !!user);

  const reportError = useCallback((where: string, err: Error) => {
    const denied = /permission[_ ]denied/i.test(err.message) || (err as { code?: string }).code === 'PERMISSION_DENIED';
    setDataErr({ where, text: err.message, denied });
  }, []);
  useEffect(() => {
    if (meta.error) reportError('meta', meta.error);
  }, [meta.error, reportError]);

  const ctx: AppCtx | null = useMemo(
    () =>
      user
        ? { source, user, meta: meta.value ?? null, visible, theme, themePref, setThemePref, lang, setLang, reportError }
        : null,
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [source, user, meta.value, visible, theme, themePref, lang, reportError],
  );

  if (user === undefined)
    return (
      <div class="splash">
        <Spinner label={t('app.starting')} />
      </div>
    );
  if (user === null || !ctx) return <LoginPage source={source} lang={lang} setLang={setLang} />;

  const Page = ROUTES.find((r) => r.id === route)!.page;
  return (
    <Ctx.Provider value={ctx}>
      <div class="shell">
        <header class="top">
          <h1>{t(ROUTES.find((r) => r.id === route)!.label)}</h1>
          <div class="top-r">
            {source.kind === 'demo' && (
              <a class="demo-pill" href={location.pathname} title={t('app.demoLeave')}>
                {t('app.demoPill')}
              </a>
            )}
            <LangButton lang={lang} setLang={setLang} />
          </div>
        </header>
        <main class="content">
          {!online && <Banner tone="warn">{t('app.offline')}</Banner>}
          {dataErr && (
            <Banner tone="bad" action={<button class="btn-link" type="button" onClick={() => setDataErr(null)}>{t('app.dismiss')}</button>}>
              {dataErr.denied ? (
                <>
                  <strong>{t('app.noAccessTitle')}</strong>{' '}
                  {tj('app.noAccess', {
                    uid: <code class="uid">{user.uid}</code>,
                    path: <code>devices/{source.deviceId}</code>,
                    owner: <code>OWNER_UID</code>,
                    repoVar: <code>FIREBASE_OWNER_UID</code>,
                  })}
                </>
              ) : (
                <>
                  <strong>{t('app.dataErr', { where: dataErr.where })}</strong> {dataErr.text}
                </>
              )}
            </Banner>
          )}
          {update && (
            <Banner tone="info" action={<button class="btn-link" type="button" onClick={() => update.waiting?.postMessage('SKIP_WAITING')}>{t('common.reload')}</button>}>
              {t('app.update')}
            </Banner>
          )}
          <Page />
        </main>
        <nav class="tabs" aria-label={t('nav.aria')}>
          {ROUTES.map((r) => (
            <a key={r.id} href={`#/${r.id}`} class={r.id === route ? 'on' : ''} aria-current={r.id === route ? 'page' : undefined}>
              <svg viewBox="0 0 24 24" aria-hidden="true">
                <path d={r.icon} />
              </svg>
              <span>{t(r.label)}</span>
            </a>
          ))}
        </nav>
      </div>
    </Ctx.Provider>
  );
}

