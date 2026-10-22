// Minimal IndexedDB key-value cache for immutable history (d1 §6.4).
// Every call fails soft: if IndexedDB is unavailable (private mode, quota,
// thumbnails) the app just reads from the network again.

const DB_NAME = 'savx-cache';
const STORE = 'kv';
let dbPromise: Promise<IDBDatabase | null> | null = null;

function open(): Promise<IDBDatabase | null> {
  if (dbPromise) return dbPromise;
  dbPromise = new Promise((resolve) => {
    try {
      if (typeof indexedDB === 'undefined') return resolve(null);
      const req = indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = () => req.result.createObjectStore(STORE);
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => resolve(null);
      req.onblocked = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
  return dbPromise;
}

function run<T>(mode: IDBTransactionMode, fn: (s: IDBObjectStore) => IDBRequest): Promise<T | undefined> {
  return open().then(
    (db) =>
      new Promise<T | undefined>((resolve) => {
        if (!db) return resolve(undefined);
        try {
          const tx = db.transaction(STORE, mode);
          const req = fn(tx.objectStore(STORE));
          req.onsuccess = () => resolve(req.result as T);
          req.onerror = () => resolve(undefined);
        } catch {
          resolve(undefined);
        }
      }),
  );
}

export function idbGet<T>(key: string): Promise<T | undefined> {
  return run<T>('readonly', (s) => s.get(key));
}

export function idbSet(key: string, value: unknown): Promise<void> {
  return run<void>('readwrite', (s) => s.put(value, key)).then(() => undefined);
}

export function idbClear(): Promise<void> {
  return run<void>('readwrite', (s) => s.clear()).then(() => undefined);
}
