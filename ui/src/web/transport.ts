// Web transport: runs the core in the WASM module (docs/BRIDGE.md "Web module API"), persists state
// in IndexedDB (R-D5, F-13), connects Web MIDI (§5.6) and starts audio on the first gesture (§5.7).

import type { MessageSink, Transport } from '../bridge';
import { isHostOnly } from '../bridge';
import { parseBatch, Perf, type CoreMessage, type CoreState, type EnvInfo, type UiMessage } from '../protocol';
import { WebMidi } from './midi';
import { loadVjn6, type Vjn6Module } from './module';
import { DEFAULT_DEVICES, KEY_CURRENT, KEY_DEVICES, SaveScheduler, StateStore, type DeviceChoices } from './storage';
import { isTestMode } from '../testmode';

export const MSG_ISOLATION =
  'VJN6 needs to be opened from http://localhost:5173 (cross-origin isolation is off). Start it with "npm run web", ' +
  'or serve it with the headers Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp.';
export const MSG_NO_WORKLET =
  'VJN6 needs a browser with AudioWorklet support (current Chrome, Edge or Safari 17+); this browser has none. ' +
  'Open http://localhost:5173 in one of those browsers.';
export const MSG_WASM =
  'VJN6 could not start its engine: WASM failed to load (vjn6.js / vjn6.wasm are missing or broken). ' +
  'Build them with web/build.sh, then reload the page.';

export interface WebHooks {
  fatal(text: string): void;
  /** IndexedDB unavailable or a write failed: "Changes will not be saved in this browser." */
  storageFailed(): void;
  /** The audio context could not start: "No audio output available." */
  audioFailed(): void;
}

const POLL_MIN_MS = 15; // rAF at most ~60 Hz
const HIDDEN_POLL_MS = 250;
const MAX_FILE_BYTES = 1024 * 1024;
const LOAD_WAIT_MS = 500; // longest wait for the core to apply the loaded state

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

export class WebTransport implements Transport {
  readonly kind = 'web' as const;
  private mod: Vjn6Module | null = null;
  private store: StateStore | null = null;
  private sink: MessageSink = () => {};
  private sched = new SaveScheduler();
  private lastSeq: number | null = null;
  private lastPoll = 0;
  private saving = false;
  private saveAgain = false;
  private analyser: AnalyserNode | null = null;
  private channel = 1;
  midi: WebMidi | null = null;
  started = false;
  latest: CoreState | null = null;

  constructor(private hooks: WebHooks) {}

  async connect(sink: MessageSink): Promise<void> {
    this.sink = sink;
    if (!globalThis.crossOriginIsolated) {
      this.hooks.fatal(MSG_ISOLATION);
      return;
    }
    if (typeof AudioWorkletNode === 'undefined' || typeof AudioContext === 'undefined') {
      this.hooks.fatal(MSG_NO_WORKLET);
      return;
    }
    let blob: Uint8Array = new Uint8Array(0);
    let devices: DeviceChoices = DEFAULT_DEVICES;
    try {
      this.store = await StateStore.open();
      blob = await this.store.loadState();
      devices = await this.store.loadDevices();
    } catch {
      this.store = null;
      this.hooks.storageFailed();
    }
    this.midi = new WebMidi(
      {
        send: (m) => this.send(m),
        channel: () => this.channel,
        changed: () => this.emitEnv(),
        persist: (c) => this.persistDevices(c),
      },
      devices,
    );
    try {
      this.mod = await loadVjn6();
      if (!this.mod.init(probeSampleRate())) throw new Error('vjn6_init failed');
    } catch {
      this.mod = null;
      this.hooks.fatal(MSG_WASM);
      return;
    }
    const before = this.mod.saveState();
    this.mod.loadState(blob);
    if (blob.length > 0) await this.loadApplied(before);
    this.mod.command(JSON.stringify({ t: 'ready' }));
    this.emitEnv();
    requestAnimationFrame(this.frame);
    setInterval(() => {
      if (document.hidden) this.pollOnce();
    }, HIDDEN_POLL_MS);
    // Final save (F-13) on hide and pagehide, and already on beforeunload: WebKit keeps an IndexedDB
    // write across a reload only when it starts in beforeunload (pagehide writes are dropped).
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') this.finalSave();
    });
    window.addEventListener('beforeunload', () => this.finalSave());
    window.addEventListener('pagehide', () => this.finalSave());
  }

  /**
   * Waits until the core has applied the state given to loadState(): it does so at its next render (the
   * main-thread driver renders every 10 ms) and reports the previous state until then, which the UI must
   * never show (R-S3). A loaded state equal to the previous one changes nothing, so the wait is capped.
   */
  private async loadApplied(before: Uint8Array): Promise<void> {
    const t0 = performance.now();
    while (this.mod && performance.now() - t0 < LOAD_WAIT_MS) {
      await new Promise((resolve) => setTimeout(resolve, 5));
      if (!sameBytes(this.mod.saveState(), before)) return;
    }
  }

  /**
   * The final save: starts the IndexedDB write synchronously inside the event, and only when the state
   * changed since the last save. The core applies a loaded state at its next render and saveState()
   * serialises the last published state, so an unconditional save from a page that closes before that
   * render would overwrite the stored state with the first-run state.
   */
  private finalSave(): void {
    if (!this.mod || !this.store) return;
    this.pollOnce(); // picks up a change published since the last frame (and may start a due save)
    if (!this.sched.pending) return;
    this.sched.saved();
    let bytes: Uint8Array;
    try {
      bytes = this.mod.saveState();
    } catch {
      return;
    }
    this.store.put(KEY_CURRENT, bytes).catch(() => {
      this.store = null;
      this.hooks.storageFailed();
    });
  }

  private frame = (t: number): void => {
    if (t - this.lastPoll >= POLL_MIN_MS) {
      this.lastPoll = t;
      this.pollOnce();
    }
    requestAnimationFrame(this.frame);
  };

  private pollOnce(): void {
    if (!this.mod) return;
    const msgs = parseBatch(this.mod.poll());
    const now = performance.now();
    const toUi: CoreMessage[] = [];
    for (const m of msgs) {
      if (m.t === 'midiOut') {
        this.midi?.sendOut(m.bytes);
        continue;
      }
      if (m.t === 'state') {
        // the core's state sequence number is the change signal for the save timing (F-13)
        if (this.lastSeq !== null && m.seq !== this.lastSeq) this.sched.change(now);
        this.lastSeq = m.seq;
        this.channel = m.perf?.[Perf.MIDI_CHANNEL] ?? this.channel;
        this.latest = m;
      }
      toUi.push(m);
    }
    if (toUi.length) this.sink(toUi);
    if (this.sched.due(now)) void this.saveNow();
  }

  send(msg: UiMessage): void {
    if (!this.mod || isHostOnly(msg)) return;
    this.mod.command(JSON.stringify(msg));
  }

  loadFile(name: string, bytes: Uint8Array): void {
    this.mod?.loadFile(name, bytes.length > MAX_FILE_BYTES + 1 ? bytes.subarray(0, MAX_FILE_BYTES + 1) : bytes);
  }

  /** First user gesture (§5.7): start audio, then request Web MIDI with SysEx. */
  start(): void {
    if (this.started || !this.mod) return;
    this.started = true;
    let ok = false;
    try {
      ok = this.mod.startAudio();
    } catch {
      ok = false;
    }
    if (!ok) this.hooks.audioFailed();
    else if (isTestMode()) this.tapWhenReady();
    void this.midi?.request();
  }

  /** Test builds: attach the analyser as soon as the worklet node exists (it is created async). */
  private tapWhenReady(): void {
    let tries = 0;
    const timer = setInterval(() => {
      if (this.ensureAnalyser() || ++tries > 400) clearInterval(timer);
    }, 25);
  }

  private emitEnv(): void {
    const m = this.midi;
    const env: EnvInfo & { midiPending: boolean } = {
      build: 'web',
      midiAvailable: !!m?.available,
      sysexPermitted: !!m?.sysex,
      midiOutAvailable: !!m?.outputAvailable(),
      inputs: m?.inputs() ?? [],
      outputs: m?.outputs() ?? [],
      output: m?.output() ?? '',
      saveError: '',
      midiPending: !this.started || !!m?.pending,
    };
    this.sink([{ t: 'env', ...env }]);
  }

  private async saveNow(): Promise<void> {
    if (!this.mod || !this.store) return;
    if (this.saving) {
      this.saveAgain = true;
      return;
    }
    this.sched.saved();
    this.saving = true;
    try {
      const bytes = this.mod.saveState();
      await this.store.put(KEY_CURRENT, bytes);
    } catch {
      this.store = null;
      this.hooks.storageFailed();
    } finally {
      this.saving = false;
    }
    if (this.saveAgain) {
      this.saveAgain = false;
      void this.saveNow();
    }
  }

  private persistDevices(c: DeviceChoices): void {
    if (!this.store) return;
    this.store.put(KEY_DEVICES, c).catch(() => {
      this.store = null;
      this.hooks.storageFailed();
    });
  }

  // ---- test-only tap (§10: an AnalyserNode on the worklet output) ----

  private ensureAnalyser(): AnalyserNode | null {
    if (this.analyser) return this.analyser;
    const ctx = this.mod?.audioContext();
    const node = this.mod?.audioNode();
    if (!ctx || !node) return null;
    const a = ctx.createAnalyser();
    a.fftSize = 4096;
    node.connect(a);
    const mute = ctx.createGain();
    mute.gain.value = 0;
    a.connect(mute);
    mute.connect(ctx.destination);
    this.analyser = a;
    return a;
  }

  /** RMS of the latest analyser window in dBFS (null before audio runs). */
  rmsDb(): number | null {
    const a = this.ensureAnalyser();
    if (!a) return null;
    const buf = new Float32Array(a.fftSize);
    a.getFloatTimeDomainData(buf);
    let sum = 0;
    for (const v of buf) sum += v * v;
    const rms = Math.sqrt(sum / buf.length);
    return rms > 0 ? 20 * Math.log10(rms) : -Infinity;
  }

  audioContext(): AudioContext | null {
    return this.mod?.audioContext() ?? null;
  }
}

/** The AudioContext's default rate, passed to vjn6_init so the core runs at the context rate. */
function probeSampleRate(): number {
  const ctx = new AudioContext();
  const sr = ctx.sampleRate;
  void ctx.close();
  return sr;
}
