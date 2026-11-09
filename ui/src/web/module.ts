// Loader and typed wrapper for the Emscripten module `vjn6.js` (MODULARIZE, EXPORT_NAME=createVjn6),
// built by web/build.sh into ui/public. Uses exactly the "Web module API" of docs/BRIDGE.md.

/** The parts of the Emscripten module object the wrapper uses. */
interface EmModule {
  ccall: (name: string, ret: string | null, argTypes: string[], args: unknown[]) => unknown;
  HEAPU8: Uint8Array;
  _malloc: (n: number) => number;
  _free: (p: number) => void;
  emscriptenGetAudioObject?: (handle: number) => unknown;
  EmAudio?: Record<number, unknown>;
}

type Factory = (opts?: Record<string, unknown>) => Promise<EmModule>;

export interface Vjn6Module {
  init(sampleRate: number): boolean;
  loadState(bytes: Uint8Array): boolean;
  saveState(): Uint8Array;
  command(json: string): void;
  poll(): string;
  loadFile(name: string, bytes: Uint8Array): void;
  startAudio(): boolean;
  audioContext(): AudioContext | null;
  audioNode(): AudioNode | null;
}

export class ModuleLoadError extends Error {}

const LOAD_TIMEOUT_MS = 30000;

function withTimeout<T>(p: Promise<T>, ms: number, what: string): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const t = setTimeout(() => reject(new ModuleLoadError(`${what} timed out`)), ms);
    p.then(
      (v) => {
        clearTimeout(t);
        resolve(v);
      },
      (e) => {
        clearTimeout(t);
        reject(e);
      },
    );
  });
}

/** Loads vjn6.js as a classic script (so the module can find itself for the audio worklet). */
function loadClassicScript(url: string): Promise<Factory | null> {
  return new Promise((resolve) => {
    const s = document.createElement('script');
    s.src = url;
    s.async = true;
    s.onload = () => resolve(((globalThis as Record<string, unknown>).createVjn6 as Factory) ?? null);
    s.onerror = () => resolve(null);
    document.head.appendChild(s);
  });
}

async function loadFactory(url: string): Promise<Factory> {
  const classic = await loadClassicScript(url);
  if (typeof classic === 'function') return classic;
  // An ES-module build (EXPORT_ES6) cannot run as a classic script; import it instead.
  try {
    const abs = new URL(url, document.baseURI).href;
    const mod = (await import(/* @vite-ignore */ abs)) as { default?: Factory; createVjn6?: Factory };
    const f = mod.default ?? mod.createVjn6;
    if (typeof f === 'function') return f;
  } catch {
    // fall through
  }
  throw new ModuleLoadError(`${url} is missing or is not the VJN6 module`);
}

function wrap(m: EmModule): Vjn6Module {
  const call = <T>(name: string, ret: string | null, types: string[] = [], args: unknown[] = []): T =>
    m.ccall(name, ret, types, args) as T;
  const withBuffer = <T>(bytes: Uint8Array, fn: (ptr: number) => T): T => {
    const ptr = m._malloc(Math.max(1, bytes.length));
    try {
      m.HEAPU8.set(bytes, ptr);
      return fn(ptr);
    } finally {
      m._free(ptr);
    }
  };
  const audioObject = (handle: number): unknown => {
    if (!handle) return null;
    if (typeof m.emscriptenGetAudioObject === 'function') return m.emscriptenGetAudioObject(handle);
    return m.EmAudio?.[handle] ?? null;
  };
  return {
    init: (sr) => call<number>('vjn6_init', 'number', ['number'], [sr]) !== 0,
    loadState: (bytes) =>
      withBuffer(bytes, (ptr) => call<number>('vjn6_load_state', 'number', ['number', 'number'], [ptr, bytes.length])) !==
      0,
    saveState: () => {
      const len = call<number>('vjn6_save_state', 'number');
      const ptr = call<number>('vjn6_state_ptr', 'number');
      return m.HEAPU8.slice(ptr, ptr + len);
    },
    command: (json) => {
      call<void>('vjn6_command', null, ['string'], [json]);
    },
    poll: () => call<string>('vjn6_poll', 'string') ?? '',
    loadFile: (name, bytes) => {
      withBuffer(bytes, (ptr) =>
        call<void>('vjn6_load_file', null, ['string', 'number', 'number'], [name, ptr, bytes.length]),
      );
    },
    startAudio: () => call<number>('vjn6_start_audio', 'number') !== 0,
    audioContext: () => (audioObject(call<number>('vjn6_audio_context', 'number')) as AudioContext) ?? null,
    audioNode: () => (audioObject(call<number>('vjn6_audio_node', 'number')) as AudioNode) ?? null,
  };
}

/** Loads and instantiates the module; throws ModuleLoadError if it is missing or fails. */
export async function loadVjn6(url = './vjn6.js'): Promise<Vjn6Module> {
  const factory = await loadFactory(url);
  let m: EmModule;
  try {
    m = await withTimeout(factory(), LOAD_TIMEOUT_MS, 'vjn6.wasm');
  } catch (e) {
    throw new ModuleLoadError(e instanceof Error ? e.message : String(e));
  }
  if (typeof m?.ccall !== 'function' || !m.HEAPU8 || typeof m._malloc !== 'function') {
    throw new ModuleLoadError('vjn6.js does not export the VJN6 module API');
  }
  return wrap(m);
}
