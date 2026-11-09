// IndexedDB persistence for the web build (R-D5): database `vjn6`, store `state`, keys `current`
// (the state blob bytes) and `devices` (the Web MIDI device choices of §4.4).

export const DB_NAME = 'vjn6';
export const STORE = 'state';
export const KEY_CURRENT = 'current';
export const KEY_DEVICES = 'devices';

export interface DeviceChoices {
  /** Ids of the Web MIDI inputs the user unchecked; every other input is enabled. */
  disabledInputs: string[];
  /** Id of the chosen MIDI output, or "" for none. */
  output: string;
}

export const DEFAULT_DEVICES: DeviceChoices = { disabledInputs: [], output: '' };

function req<T>(r: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error ?? new Error('IndexedDB request failed'));
  });
}

export class StateStore {
  private constructor(private db: IDBDatabase) {}

  /** Opens (creating if needed) the database; rejects when IndexedDB is unavailable. */
  static open(factory: IDBFactory | undefined = globalThis.indexedDB): Promise<StateStore> {
    return new Promise((resolve, reject) => {
      if (!factory) {
        reject(new Error('IndexedDB is not available'));
        return;
      }
      let r: IDBOpenDBRequest;
      try {
        r = factory.open(DB_NAME, 1);
      } catch (e) {
        reject(e instanceof Error ? e : new Error(String(e)));
        return;
      }
      r.onupgradeneeded = () => {
        if (!r.result.objectStoreNames.contains(STORE)) r.result.createObjectStore(STORE);
      };
      r.onsuccess = () => resolve(new StateStore(r.result));
      r.onerror = () => reject(r.error ?? new Error('IndexedDB open failed'));
      r.onblocked = () => reject(new Error('IndexedDB open blocked'));
    });
  }

  async get(key: string): Promise<unknown> {
    const tx = this.db.transaction(STORE, 'readonly');
    return req(tx.objectStore(STORE).get(key));
  }

  put(key: string, value: unknown): Promise<void> {
    return new Promise((resolve, reject) => {
      let tx: IDBTransaction;
      try {
        tx = this.db.transaction(STORE, 'readwrite');
        tx.objectStore(STORE).put(value, key);
        // flush now: the final save on pagehide must not wait for the transaction to auto-commit
        (tx as IDBTransaction & { commit?: () => void }).commit?.();
      } catch (e) {
        reject(e instanceof Error ? e : new Error(String(e)));
        return;
      }
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error ?? new Error('IndexedDB write failed'));
      tx.onabort = () => reject(tx.error ?? new Error('IndexedDB write aborted'));
    });
  }

  async loadState(): Promise<Uint8Array> {
    const v = await this.get(KEY_CURRENT);
    if (v instanceof Uint8Array) return v;
    if (v instanceof ArrayBuffer) return new Uint8Array(v);
    return new Uint8Array(0);
  }

  async loadDevices(): Promise<DeviceChoices> {
    const v = (await this.get(KEY_DEVICES)) as Partial<DeviceChoices> | undefined;
    return {
      disabledInputs: Array.isArray(v?.disabledInputs) ? v.disabledInputs.filter((x) => typeof x === 'string') : [],
      output: typeof v?.output === 'string' ? v.output : '',
    };
  }
}

/**
 * Save timing of F-13: 1 s after the last change, at least every 5 s while changes continue.
 * `change()` marks a change, `due()` says whether a save should run now.
 */
export class SaveScheduler {
  private firstPending: number | null = null;
  private lastChange = 0;

  constructor(
    private quietMs = 1000,
    private maxMs = 5000,
  ) {}

  change(now: number): void {
    if (this.firstPending === null) this.firstPending = now;
    this.lastChange = now;
  }

  get pending(): boolean {
    return this.firstPending !== null;
  }

  due(now: number): boolean {
    if (this.firstPending === null) return false;
    return now - this.lastChange >= this.quietMs || now - this.firstPending >= this.maxMs;
  }

  /** Called when a save starts; changes after this point schedule the next save. */
  saved(): void {
    this.firstPending = null;
  }
}
