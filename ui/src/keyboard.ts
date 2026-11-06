// Computer keyboard (§7.6, R-K1/R-K2). Keys are matched by KeyboardEvent.code (physical position),
// never by character. These keys are not panel input; notes, pedal, lever and push go to the core
// with the keyboard as their source.

import { Setting, type UiMessage } from './protocol';

export interface KeyboardHost {
  send(msg: UiMessage): void;
}

/** Semitone offsets from the base C of the white and black keys (QWERTY positions). */
export const NOTE_KEYS: Readonly<Record<string, number>> = {
  KeyA: 0, // C
  KeyW: 1, // C#
  KeyS: 2, // D
  KeyE: 3, // D#
  KeyD: 4, // E
  KeyF: 5, // F
  KeyT: 6, // F#
  KeyG: 7, // G
  KeyY: 8, // G#
  KeyH: 9, // A
  KeyU: 10, // A#
  KeyJ: 11, // B
  KeyK: 12, // C
  KeyO: 13, // C#
  KeyL: 14, // D
  KeyP: 15, // D#
  Semicolon: 16, // E
  Quote: 17, // F
};

const FUNCTION_KEYS = new Set(['KeyZ', 'KeyX', 'KeyC', 'KeyV', 'Space', 'Digit1', 'Digit2', 'Digit3']);
const TOGGLE_KEYS = new Set(['KeyM', 'F9']);

export const DEFAULT_BASE = 60; // C3 = MIDI 60
export const DEFAULT_VELOCITY = 100;
const LEVER_FULL = 8191;
/** How long an optimistic play toggle wins over a state that still shows the old value. */
const PLAY_PENDING_MS = 1000;

const TEXT_INPUT_TYPES = new Set(['text', 'search', 'email', 'url', 'tel', 'password', 'number']);

/** True for elements that take typed text (bank-manager rename, typed slider entry). */
export function isTextField(el: EventTarget | Element | null | undefined): boolean {
  if (!el || typeof (el as Element).tagName !== 'string') return false;
  const e = el as HTMLElement;
  if (e.isContentEditable) return true;
  const tag = e.tagName.toUpperCase();
  if (tag === 'TEXTAREA') return true;
  if (tag === 'INPUT') return TEXT_INPUT_TYPES.has(((e as HTMLInputElement).type || 'text').toLowerCase());
  return false;
}

/** True if the key is in the §7.6 map (captured while play is on). */
export function isMappedKey(code: string): boolean {
  return code in NOTE_KEYS || FUNCTION_KEYS.has(code) || TOGGLE_KEYS.has(code);
}

export class ComputerKeyboard {
  private host: KeyboardHost;
  private statePlay = true;
  private pendingPlay: boolean | null = null;
  private pendingAt = 0;
  private held = new Map<string, number>(); // code -> note sent at key-down
  private pedalDown = false;
  private leverKeys: string[] = []; // Digit1 / Digit2 in press order
  private pushDown = false;
  private doc: Document | null = null;
  base = DEFAULT_BASE;
  velocity = DEFAULT_VELOCITY;
  now: () => number = () => Date.now();

  constructor(host: KeyboardHost) {
    this.host = host;
  }

  /** Keyboard play as the UI currently applies it (§4.4 setting 6, saved in the core state). */
  get play(): boolean {
    if (this.pendingPlay !== null) {
      if (this.now() - this.pendingAt < PLAY_PENDING_MS) return this.pendingPlay;
      this.pendingPlay = null;
    }
    return this.statePlay;
  }

  /** Called with every core `state` (settings[6]). */
  setPlayFromState(on: boolean): void {
    const wasPlaying = this.play;
    this.statePlay = on;
    if (this.pendingPlay !== null && this.pendingPlay === on) this.pendingPlay = null;
    if (wasPlaying && !this.play) this.releaseAll();
  }

  /** M / F9: toggles keyboard play through the core setting. */
  togglePlay(): void {
    const next = !this.play;
    this.pendingPlay = next;
    this.pendingAt = this.now();
    this.host.send({ t: 'setting', s: Setting.KBD_PLAY, v: next ? 1 : 0 });
    if (!next) this.releaseAll();
  }

  /** Notes currently held by keys (for tests and the lever display). */
  heldNotes(): number[] {
    return [...this.held.values()];
  }

  private textFieldFocused(e?: KeyboardEvent): boolean {
    const active = this.doc?.activeElement ?? (typeof document !== 'undefined' ? document.activeElement : null);
    return isTextField(active) || (!!e && isTextField(e.target));
  }

  handleKeyDown(e: KeyboardEvent): void {
    if (this.textFieldFocused(e)) return; // a focused text field suspends the map entirely
    if (e.isComposing || e.keyCode === 229) return;
    if (e.ctrlKey || e.metaKey || e.altKey) return; // never play; Alt is for the R-U2 shortcuts
    const code = e.code;
    if (code === 'Tab') return; // never captured
    if (TOGGLE_KEYS.has(code)) {
      e.preventDefault();
      if (!e.repeat) this.togglePlay();
      return;
    }
    if (!this.play) return;
    const offset = NOTE_KEYS[code];
    if (offset === undefined && !FUNCTION_KEYS.has(code)) return;
    e.preventDefault();
    if (e.repeat) return;
    if (offset !== undefined) {
      if (this.held.has(code)) return;
      const n = this.base + offset;
      if (n > 127) return;
      this.held.set(code, n);
      this.host.send({ t: 'note', n, vel: this.velocity });
      return;
    }
    switch (code) {
      case 'KeyZ':
        if (this.base - 12 >= 0) this.base -= 12;
        return;
      case 'KeyX':
        if (this.base + 12 <= 108) this.base += 12;
        return;
      case 'KeyC':
        this.velocity = Math.max(1, this.velocity - 20);
        return;
      case 'KeyV':
        this.velocity = Math.min(127, this.velocity + 20);
        return;
      case 'Space':
        if (!this.pedalDown) {
          this.pedalDown = true;
          this.host.send({ t: 'pedal', on: true });
        }
        return;
      case 'Digit1':
      case 'Digit2':
        if (!this.leverKeys.includes(code)) {
          this.leverKeys.push(code);
          this.sendLever();
        }
        return;
      case 'Digit3':
        if (!this.pushDown) {
          this.pushDown = true;
          this.host.send({ t: 'push', on: true });
        }
        return;
    }
  }

  handleKeyUp(e: KeyboardEvent): void {
    const code = e.code;
    // Releases always act, whatever modifiers are held now: the note-off uses the stored note.
    const n = this.held.get(code);
    if (n !== undefined) {
      this.held.delete(code);
      this.host.send({ t: 'note', n, vel: 0 });
    }
    if (code === 'Space' && this.pedalDown) {
      this.pedalDown = false;
      this.host.send({ t: 'pedal', on: false });
    }
    if ((code === 'Digit1' || code === 'Digit2') && this.leverKeys.includes(code)) {
      this.leverKeys = this.leverKeys.filter((k) => k !== code);
      this.sendLever();
    }
    if (code === 'Digit3' && this.pushDown) {
      this.pushDown = false;
      this.host.send({ t: 'push', on: false });
    }
    if (this.play && isMappedKey(code) && !this.textFieldFocused(e) && !e.altKey && !e.ctrlKey && !e.metaKey) {
      e.preventDefault(); // e.g. Space must not activate a focused button while it is the pedal
    }
  }

  private sendLever(): void {
    const last = this.leverKeys[this.leverKeys.length - 1];
    const v = last === 'Digit1' ? -LEVER_FULL : last === 'Digit2' ? LEVER_FULL : 0;
    this.host.send({ t: 'lever', v, drag: false, src: 'keyboard' });
  }

  /** Releases every key-held note, the pedal, the lever and push (blur, play off, text focus, close). */
  releaseAll(): void {
    for (const n of this.held.values()) this.host.send({ t: 'note', n, vel: 0 });
    this.held.clear();
    if (this.pedalDown) {
      this.pedalDown = false;
      this.host.send({ t: 'pedal', on: false });
    }
    if (this.leverKeys.length) {
      this.leverKeys = [];
      this.sendLever();
    }
    if (this.pushDown) {
      this.pushDown = false;
      this.host.send({ t: 'push', on: false });
    }
  }

  /** Installs the listeners on a window; returns a function that removes them. */
  attach(win: Window): () => void {
    this.doc = win.document;
    const down = (e: KeyboardEvent) => this.handleKeyDown(e);
    const up = (e: KeyboardEvent) => this.handleKeyUp(e);
    const blur = () => this.releaseAll();
    const focusIn = (e: FocusEvent) => {
      if (isTextField(e.target)) this.releaseAll();
    };
    win.addEventListener('keydown', down);
    win.addEventListener('keyup', up);
    win.addEventListener('blur', blur);
    win.addEventListener('pagehide', blur);
    win.document.addEventListener('focusin', focusIn);
    return () => {
      win.removeEventListener('keydown', down);
      win.removeEventListener('keyup', up);
      win.removeEventListener('blur', blur);
      win.removeEventListener('pagehide', blur);
      win.document.removeEventListener('focusin', focusIn);
    };
  }
}
