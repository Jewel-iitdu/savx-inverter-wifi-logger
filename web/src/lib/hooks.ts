import { useEffect, useRef, useState } from 'preact/hooks';
import type { Unsub } from '../data/source';
import { nowSec } from './time';

/** True while the page is visible. iOS suspends hidden PWAs anyway. */
export function useVisible(): boolean {
  const [visible, setVisible] = useState(() => typeof document === 'undefined' || document.visibilityState !== 'hidden');
  useEffect(() => {
    const on = () => setVisible(document.visibilityState !== 'hidden');
    document.addEventListener('visibilitychange', on);
    window.addEventListener('pageshow', on);
    return () => {
      document.removeEventListener('visibilitychange', on);
      window.removeEventListener('pageshow', on);
    };
  }, []);
  return visible;
}

/** Wall clock in epoch seconds, re-rendering every `everyMs`. */
export function useNow(everyMs = 1000): number {
  const [now, setNow] = useState(nowSec);
  useEffect(() => {
    const id = setInterval(() => setNow(nowSec()), everyMs);
    return () => clearInterval(id);
  }, [everyMs]);
  return now;
}

/**
 * Subscribes while `active` (page mounted AND visible) and unsubscribes on
 * hide/unmount (d1 §6.4). Keeps the last value while detached.
 */
export function useSubscription<T>(
  subscribe: (cb: (v: T) => void, err: (e: Error) => void) => Unsub,
  deps: unknown[],
  active: boolean,
): { value: T | undefined; error: Error | null } {
  const [value, setValue] = useState<T | undefined>(undefined);
  const [error, setError] = useState<Error | null>(null);
  const sub = useRef(subscribe);
  sub.current = subscribe;
  useEffect(() => {
    if (!active) return;
    setError(null);
    const un = sub.current(
      (v) => setValue(() => v),
      (e) => setError(e),
    );
    return un;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active, ...deps]);
  return { value, error };
}

/** Runs an async loader when deps change; ignores stale results. */
export function useAsync<T>(load: () => Promise<T>, deps: unknown[], enabled = true) {
  const [state, setState] = useState<{ data?: T; error?: Error; loading: boolean }>({ loading: enabled });
  const loader = useRef(load);
  loader.current = load;
  const [nonce, setNonce] = useState(0);
  useEffect(() => {
    if (!enabled) return;
    let alive = true;
    setState((s) => ({ data: s.data, loading: true }));
    loader
      .current()
      .then((data) => alive && setState({ data, loading: false }))
      .catch((e: unknown) => alive && setState({ error: e instanceof Error ? e : new Error(String(e)), loading: false }));
    return () => {
      alive = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [enabled, nonce, ...deps]);
  return { ...state, reload: () => setNonce((n) => n + 1) };
}

/** Per-viewer preference in localStorage; tolerant of blocked storage. */
export function useStored<T extends string>(key: string, fallback: T): [T, (v: T) => void] {
  const [v, setV] = useState<T>(() => {
    try {
      return (localStorage.getItem(key) as T | null) ?? fallback;
    } catch {
      return fallback;
    }
  });
  const set = (nv: T) => {
    setV(nv);
    try {
      localStorage.setItem(key, nv);
    } catch {
      /* ignore */
    }
  };
  return [v, set];
}

const readHashPath = () =>
  location.hash
    .replace(/^#\/?/, '')
    .split('?')[0]
    .split('/')
    .filter(Boolean)
    .map((p) => decodeURIComponent(p));

/** The hash route split into segments: '#/alerts/rules/r03' -> ['alerts', 'rules', 'r03']. */
export function useHashPath(): string[] {
  const [path, setPath] = useState<string[]>(readHashPath);
  useEffect(() => {
    const on = () => setPath(readHashPath());
    window.addEventListener('hashchange', on);
    return () => window.removeEventListener('hashchange', on);
  }, []);
  return path;
}

/** Navigate without adding a history entry (closing a sheet). */
export function replaceHash(hash: string) {
  const url = location.pathname + location.search + hash;
  history.replaceState(history.state, '', url);
  window.dispatchEvent(new HashChangeEvent('hashchange'));
}
