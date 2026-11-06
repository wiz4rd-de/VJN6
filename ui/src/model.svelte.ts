// Application model: the latest core snapshot, host environment, overlays, notices, tooltips and
// the transport. Every component reads it; every action goes through it to the core.

import { DesktopTransport, hasJuceBackend, unbase64, type Transport } from './bridge';
import { ComputerKeyboard } from './keyboard';
import {
  hasBit,
  Setting,
  type BankEntry,
  type ButtonName,
  type CoreMessage,
  type CoreState,
  type EnvInfo,
  type ExportKind,
  type NoticeKind,
  type Src,
  type UiMessage,
} from './protocol';
import { handleShortcut } from './shortcuts';
import { WebTransport } from './web/transport';
import { noticeText } from './bank/notices';
import { isTestMode } from './testmode';

export type OverlayKind = 'none' | 'bank' | 'settings';

export interface Tip {
  owner: string;
  text: () => string;
  /** Anchor in logical canvas coordinates: the tooltip's top centre below the control. */
  x: number;
  y: number;
  /** Top edge of the control: the tooltip goes above it when there is no room below. */
  top: number;
}

export interface TypedEntryRequest {
  owner: string;
  initial: string;
  /** Box in logical canvas coordinates where the field is drawn. */
  x: number;
  y: number;
  w: number;
  /** Commits the text; returns false if it was not a number (the entry reverts). */
  commit(text: string): void;
  /** Element to refocus when the entry closes. */
  returnFocus: HTMLElement | null;
}

export type WebEnv = EnvInfo & { midiPending?: boolean };

const NOTICE_MS = 4000;
const BLINK_MS = 400;
const MAX_FILE_BYTES = 1024 * 1024;


class AppModel {
  /** Latest `state` (render only after the first one, R-S3). */
  core = $state.raw<CoreState | null>(null);
  bank = $state.raw<BankEntry[]>([]);
  env = $state.raw<WebEnv>({
    build: 'web',
    midiOutAvailable: false,
    sysexPermitted: false,
    midiAvailable: false,
    inputs: [],
    outputs: [],
    output: '',
    saveError: '',
  });
  /** Web: false until the first gesture (LED blank, button LEDs off, start card shown). */
  started = $state(true);
  fatal = $state<string | null>(null);
  storageBanner = $state(false);
  audioBanner = $state(false);
  overlay = $state<OverlayKind>('none');
  notice = $state<{ text: string; kind: NoticeKind | 'local'; id: number } | null>(null);
  status = $state('');
  tip = $state.raw<Tip | null>(null);
  typed = $state.raw<TypedEntryRequest | null>(null);
  scale = $state(1);
  blinkOn = $state(true);

  transport: Transport | null = null;
  web: WebTransport | null = null;
  keyboard = new ComputerKeyboard({ send: (m) => this.send(m) });

  private noticeTimer: ReturnType<typeof setTimeout> | null = null;
  private noticeId = 0;
  private blinkTimer: ReturnType<typeof setInterval> | null = null;
  private blinkStart = 0;
  private fileInput: HTMLInputElement | null = null;

  get is106(): boolean {
    return this.core?.mode === 1;
  }

  get isDesktop(): boolean {
    return this.transport?.kind === 'desktop';
  }

  // ---- connection ----

  boot(): void {
    if (hasJuceBackend()) {
      // until the host's `env` arrives: a desktop build whose MIDI output is not known yet
      this.env = { ...this.env, build: 'standalone', midiAvailable: true, sysexPermitted: true };
      this.transport = new DesktopTransport();
      this.transport.connect((msgs) => this.handle(msgs));
    } else {
      this.started = false;
      const web = new WebTransport({
        fatal: (text) => (this.fatal = text),
        storageFailed: () => (this.storageBanner = true),
        audioFailed: () => (this.audioBanner = true),
      });
      this.web = web;
      this.transport = web;
      this.installStartKeys();
      void web.connect((msgs) => this.handle(msgs));
    }
    this.keyboard.attach(window);
    window.addEventListener('keydown', (e) => this.onShortcutKey(e));
    // Window blur / editor close release keyboard notes, LFO TRIG, push and the lever (R-K2, F-13).
    const releaseAll = () => this.send({ t: 'releaseAll' });
    window.addEventListener('blur', releaseAll);
    window.addEventListener('pagehide', releaseAll);
    this.installTestHook();
  }

  /**
   * Web: until the start gesture, every key is swallowed (capture phase, before the computer
   * keyboard and the shortcuts); the first key once the panel is shown dismisses the card (§5.7).
   * The dismissing key's key-up is swallowed too.
   */
  private installStartKeys(): void {
    let swallowUp: string | null = null;
    const down = (e: KeyboardEvent) => {
      if (this.started || this.fatal) return;
      e.preventDefault();
      e.stopImmediatePropagation();
      if (!this.core) return;
      swallowUp = e.code;
      this.startWeb();
    };
    const up = (e: KeyboardEvent) => {
      if (swallowUp !== null && e.code === swallowUp) {
        e.preventDefault();
        e.stopImmediatePropagation();
        swallowUp = null;
      } else if (!this.started && !this.fatal) {
        e.preventDefault();
        e.stopImmediatePropagation();
      }
    };
    window.addEventListener('keydown', down, true);
    window.addEventListener('keyup', up, true);
  }

  /** The first click or key on the web start card (§5.7). */
  startWeb(): void {
    if (this.started) return;
    this.started = true;
    this.web?.start();
  }

  handle(msgs: CoreMessage[]): void {
    for (const m of msgs) {
      switch (m.t) {
        case 'state': {
          if (m.bank) this.bank = m.bank;
          this.core = m;
          this.keyboard.setPlayFromState((m.settings?.[Setting.KBD_PLAY] ?? 1) !== 0);
          this.updateBlink();
          break;
        }
        case 'notice':
          if (m.kind !== 'renamed') this.showNotice(noticeText(m.kind, m.file, m.i ?? 0, m.j ?? 0), m.kind);
          break;
        case 'exportData':
          this.download(m.name, unbase64(m.b64));
          break;
        case 'status':
          this.status = m.text;
          break;
        case 'env': {
          const { t: _t, ...rest } = m;
          void _t;
          this.env = { ...this.env, ...rest };
          if (rest.saveError) this.status = rest.saveError;
          break;
        }
        case 'midiOut':
          break; // the web transport forwards these before they reach the UI
      }
    }
  }

  send(msg: UiMessage): void {
    this.transport?.send(msg);
  }

  // ---- panel actions ----

  setField(f: number, v: number): void {
    this.send({ t: 'field', f, v });
  }

  setPerf(p: number, v: number, src: Src = 'panel'): void {
    this.send(src === 'panel' ? { t: 'perf', p, v } : { t: 'perf', p, v, src });
  }

  setSetting(s: number, v: number): void {
    this.send({ t: 'setting', s, v });
    if (s === Setting.ZOOM && this.isDesktop) this.send({ t: 'zoom', v });
  }

  press(b: ButtonName, i?: number, shift = false): void {
    const msg: UiMessage = { t: 'press', b };
    if (i !== undefined) msg.i = i;
    if (shift) msg.shift = true;
    this.send(msg);
  }

  release(b: ButtonName): void {
    this.send({ t: 'release', b });
  }

  gesture(target: { f?: number; p?: number }, begin: boolean): void {
    this.send({ t: 'gesture', ...target, begin });
  }

  /** Button LED: lit, or blinking 400 ms on / 400 ms off; all off before the web start gesture. */
  ledOn(bit: number): boolean {
    const c = this.core;
    if (!c || !this.started) return false;
    if (hasBit(c.blinking, bit)) return this.blinkOn;
    return hasBit(c.lit, bit);
  }

  private updateBlink(): void {
    const c = this.core;
    const blinking = !!c && this.started && ((c.blinking?.length ?? 0) > 0 || c.led?.blink);
    if (blinking && !this.blinkTimer) {
      this.blinkStart = performance.now();
      this.blinkOn = true;
      this.blinkTimer = setInterval(() => {
        this.blinkOn = Math.floor((performance.now() - this.blinkStart) / BLINK_MS) % 2 === 0;
      }, 50);
    } else if (!blinking && this.blinkTimer) {
      clearInterval(this.blinkTimer);
      this.blinkTimer = null;
      this.blinkOn = true;
    }
  }

  // ---- overlays ----

  openOverlay(kind: Exclude<OverlayKind, 'none'>): void {
    this.hideTip();
    this.overlay = kind;
  }

  closeOverlay(): boolean {
    if (this.overlay === 'none') return false;
    this.overlay = 'none';
    return true;
  }

  toggleOverlay(kind: Exclude<OverlayKind, 'none'>): void {
    if (this.overlay === kind) this.closeOverlay();
    else this.openOverlay(kind);
  }

  private onShortcutKey(e: KeyboardEvent): void {
    if (!this.core || this.fatal || !this.started) return;
    handleShortcut(e, {
      toggleBankManager: () => this.toggleOverlay('bank'),
      toggleSettings: () => this.toggleOverlay('settings'),
      closeOverlay: () => {
        if (this.typed) return false;
        return this.closeOverlay();
      },
      manual: () => this.press('manual'),
      write: () => this.press('write'),
      patch: (i) => this.press('patch', i),
    });
  }

  // ---- notices (bank manager, 4 s) ----

  showNotice(text: string, kind: NoticeKind | 'local'): void {
    if (!text) return;
    this.notice = { text, kind, id: ++this.noticeId };
    if (this.noticeTimer) clearTimeout(this.noticeTimer);
    this.noticeTimer = setTimeout(() => {
      this.notice = null;
      this.noticeTimer = null;
    }, NOTICE_MS);
  }

  // ---- files ----

  registerFileInput(el: HTMLInputElement | null): void {
    this.fileInput = el;
  }

  /** Load file…: the OS dialog on the desktop (host), the browser's file picker on the web. */
  openFilePicker(): void {
    if (this.isDesktop) this.send({ t: 'openFile' });
    else this.fileInput?.click();
  }

  /** Files from the picker or a drop anywhere on the window: only the first is used (F-10). */
  async loadFiles(files: FileList | readonly File[] | null | undefined): Promise<void> {
    const file = files && files.length ? files[0] : null;
    if (!file) return;
    let bytes: Uint8Array;
    try {
      // Files over 1 MiB are rejected by the core; it only needs to see that they are too long.
      bytes = new Uint8Array(await file.slice(0, MAX_FILE_BYTES + 1).arrayBuffer());
    } catch {
      this.send({ t: 'message', m: 2 }); // Er
      this.showNotice(noticeText('unreadable', file.name), 'unreadable');
      return;
    }
    this.transport?.loadFile(file.name, bytes);
  }

  exportFile(what: ExportKind): void {
    this.send({ t: 'export', what });
  }

  private download(name: string, bytes: Uint8Array): void {
    const blob = new Blob([bytes as BlobPart], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.style.display = 'none';
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
  }

  // ---- tooltips and typed entry ----

  showTip(tip: Tip): void {
    this.tip = tip;
  }

  hideTip(owner?: string): void {
    if (!owner || this.tip?.owner === owner) this.tip = null;
  }

  openTyped(req: TypedEntryRequest): void {
    this.hideTip();
    this.typed = req;
  }

  closeTyped(): void {
    const r = this.typed;
    this.typed = null;
    r?.returnFocus?.focus();
  }

  // ---- test hook (§10: test builds only) ----

  private installTestHook(): void {
    if (!isTestMode()) return;
    (window as unknown as { __vjn6Test: unknown }).__vjn6Test = {
      state: () => this.core,
      bank: () => this.bank,
      env: () => this.env,
      started: () => this.started,
      send: (msg: UiMessage) => this.send(msg),
      rmsDb: () => this.web?.rmsDb() ?? null,
      audioContext: () => this.web?.audioContext() ?? null,
      keyboard: () => ({ base: this.keyboard.base, velocity: this.keyboard.velocity, play: this.keyboard.play }),
    };
  }
}

export const app = new AppModel();
