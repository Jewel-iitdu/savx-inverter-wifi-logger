import { useState } from 'preact/hooks';
import type { DataSource } from '../data/source';
import { Banner, LangButton } from '../components/ui';
import { t, tj, type Lang } from '../lib/i18n';

function authMessage(e: unknown): string {
  const code = (e as { code?: string })?.code ?? '';
  switch (code) {
    case 'auth/invalid-credential':
    case 'auth/wrong-password':
    case 'auth/user-not-found':
    case 'auth/invalid-email':
      return t('login.badCreds');
    case 'auth/too-many-requests':
      return t('login.tooMany');
    case 'auth/network-request-failed':
      return t('login.network');
    case 'auth/operation-not-allowed':
      return t('login.notEnabled');
    default:
      return e instanceof Error ? e.message : String(e);
  }
}

export function LoginPage({ source, lang, setLang }: { source: DataSource; lang: Lang; setLang: (l: Lang) => void }) {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const standalone = typeof matchMedia !== 'undefined' && matchMedia('(display-mode: standalone)').matches;

  return (
    <div class="login">
      <img src="/icons/icon-192.png" alt="" width="72" height="72" class="login-icon" />
      <h1>{t('login.title')}</h1>
      <p class="muted">{t('login.lead')}</p>
      <form
        class="form"
        onSubmit={async (e) => {
          e.preventDefault();
          setBusy(true);
          setErr(null);
          try {
            await source.signIn(email, password);
            try {
              await navigator.storage?.persist?.();
            } catch {
              /* best effort */
            }
          } catch (x) {
            setErr(authMessage(x));
          } finally {
            setBusy(false);
          }
        }}
      >
        <label class="field">
          <span class="field-l">{t('login.email')}</span>
          <input type="email" autoComplete="username" required value={email} onInput={(e) => setEmail(e.currentTarget.value)} />
        </label>
        <label class="field">
          <span class="field-l">{t('login.password')}</span>
          <input type="password" autoComplete="current-password" required value={password} onInput={(e) => setPassword(e.currentTarget.value)} />
        </label>
        <button class="btn btn-wide" type="submit" disabled={busy}>
          {busy ? t('login.signingIn') : t('login.signIn')}
        </button>
      </form>
      {err && <Banner tone="bad">{err}</Banner>}
      {!standalone && /iPhone|iPad|iPod/.test(navigator.userAgent) && (
        <p class="muted small">{tj('login.iphone', { add: <strong>Add to Home Screen</strong>, inside: <em>{t('login.inside')}</em> })}</p>
      )}
      <p class="small">
        <a href="?demo=1">{t('demo.preview')}</a>
      </p>
      <p class="small">
        <LangButton lang={lang} setLang={setLang} class="btn-link" full />
      </p>
    </div>
  );
}
