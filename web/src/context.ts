import { createContext } from 'preact';
import { useContext, useEffect } from 'preact/hooks';
import type { AuthUser, DataSource } from './data/source';
import type { Meta } from './data/types';
import type { Lang } from './lib/i18n';

export interface AppCtx {
  source: DataSource;
  user: AuthUser;
  meta: Meta | null;
  visible: boolean;
  /** Resolved colour theme; charts re-create when it changes. */
  theme: 'light' | 'dark';
  themePref: 'auto' | 'light' | 'dark';
  setThemePref: (t: 'auto' | 'light' | 'dark') => void;
  /** UI language (localStorage 'lang', default 'en'); pages re-render when it changes. */
  lang: Lang;
  setLang: (l: Lang) => void;
  /** Report a data error (permission denied etc.) to the global banner. */
  reportError: (where: string, err: Error) => void;
}

export const Ctx = createContext<AppCtx | null>(null);

export function useApp(): AppCtx {
  const v = useContext(Ctx);
  if (!v) throw new Error('App context missing');
  return v;
}

/** Forward a data error to the global banner (from an effect, never during render). */
export function useReportError(where: string, err: Error | null | undefined) {
  const { reportError } = useApp();
  useEffect(() => {
    if (err) reportError(where, err);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [err]);
}
