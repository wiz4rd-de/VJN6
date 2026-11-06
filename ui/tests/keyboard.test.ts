// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { ComputerKeyboard, NOTE_KEYS } from '../src/keyboard';
import type { UiMessage } from '../src/protocol';

let sent: UiMessage[];
let kb: ComputerKeyboard;
let detach: () => void;

function key(type: 'keydown' | 'keyup', code: string, init: KeyboardEventInit = {}): KeyboardEvent {
  const e = new KeyboardEvent(type, { code, bubbles: true, cancelable: true, ...init });
  (document.activeElement ?? document.body).dispatchEvent(e);
  return e;
}
const down = (code: string, init: KeyboardEventInit = {}) => key('keydown', code, init);
const up = (code: string, init: KeyboardEventInit = {}) => key('keyup', code, init);
const tap = (code: string) => {
  down(code);
  up(code);
};
const notes = () => sent.filter((m): m is Extract<UiMessage, { t: 'note' }> => m.t === 'note');

beforeEach(() => {
  document.body.innerHTML = '';
  sent = [];
  kb = new ComputerKeyboard({ send: (m) => sent.push(m) });
  kb.setPlayFromState(true);
  detach = kb.attach(window);
});

afterEach(() => detach());

describe('AC-K1.1 computer keyboard map (play on)', () => {
  it('AC-K1.1 white and black keys play the R-K1 notes from C3 = MIDI 60', () => {
    const expected: Record<string, number> = {
      KeyA: 60, KeyW: 61, KeyS: 62, KeyE: 63, KeyD: 64, KeyF: 65, KeyT: 66, KeyG: 67, KeyY: 68, KeyH: 69,
      KeyU: 70, KeyJ: 71, KeyK: 72, KeyO: 73, KeyL: 74, KeyP: 75, Semicolon: 76, Quote: 77,
    };
    expect(Object.keys(NOTE_KEYS).sort()).toEqual(Object.keys(expected).sort());
    for (const [code, n] of Object.entries(expected)) {
      sent = [];
      const d = down(code);
      expect(d.defaultPrevented).toBe(true);
      up(code);
      expect(sent).toEqual([
        { t: 'note', n, vel: 100 },
        { t: 'note', n, vel: 0 },
      ]);
    }
  });

  it('AC-K1.1 Z / X move the base in octaves within 0…108', () => {
    tap('KeyZ');
    tap('KeyA');
    expect(notes()[0]).toEqual({ t: 'note', n: 48, vel: 100 });
    for (let i = 0; i < 10; ++i) tap('KeyZ');
    expect(kb.base).toBe(0);
    sent = [];
    tap('KeyA');
    expect(notes()[0].n).toBe(0);
    for (let i = 0; i < 20; ++i) tap('KeyX');
    expect(kb.base).toBe(108);
    sent = [];
    tap('Quote'); // 108 + 17 = 125
    expect(notes()[0].n).toBe(125);
  });

  it('AC-K1.1 C / V change the velocity by 20, default 100, clamped 1–127', () => {
    expect(kb.velocity).toBe(100);
    tap('KeyC');
    tap('KeyA');
    expect(notes()[0]).toEqual({ t: 'note', n: 60, vel: 80 });
    tap('KeyV');
    tap('KeyV');
    tap('KeyV');
    expect(kb.velocity).toBe(127);
    for (let i = 0; i < 10; ++i) tap('KeyC');
    expect(kb.velocity).toBe(1);
  });

  it('AC-K1.1 Space holds the pedal (CC64 127 / 0)', () => {
    down('Space');
    expect(sent).toEqual([{ t: 'pedal', on: true }]);
    up('Space');
    expect(sent).toEqual([
      { t: 'pedal', on: true },
      { t: 'pedal', on: false },
    ]);
  });

  it('AC-K1.1 1 / 2 hold the lever fully left / right, centre on release', () => {
    down('Digit1');
    up('Digit1');
    down('Digit2');
    up('Digit2');
    expect(sent).toEqual([
      { t: 'lever', v: -8191, drag: false, src: 'keyboard' },
      { t: 'lever', v: 0, drag: false, src: 'keyboard' },
      { t: 'lever', v: 8191, drag: false, src: 'keyboard' },
      { t: 'lever', v: 0, drag: false, src: 'keyboard' },
    ]);
  });

  it('AC-K1.1 3 holds push (106) / LFO TRIG (60)', () => {
    down('Digit3');
    up('Digit3');
    expect(sent).toEqual([
      { t: 'push', on: true },
      { t: 'push', on: false },
    ]);
  });

  it('AC-K1.1 M and F9 toggle keyboard play (setting 6)', () => {
    tap('KeyM');
    expect(sent).toEqual([{ t: 'setting', s: 6, v: 0 }]);
    expect(kb.play).toBe(false);
    tap('F9');
    expect(sent[1]).toEqual({ t: 'setting', s: 6, v: 1 });
    expect(kb.play).toBe(true);
  });

  it('AC-K1.1 keys 4–8 do nothing', () => {
    for (const code of ['Digit4', 'Digit5', 'Digit6', 'Digit7', 'Digit8']) {
      const d = down(code);
      up(code);
      expect(d.defaultPrevented).toBe(false);
    }
    expect(sent).toEqual([]);
  });
});

describe('AC-K2.1 computer keyboard behaviour', () => {
  it('AC-K2.1 key repeat sends no second note-on', () => {
    down('KeyA');
    down('KeyA', { repeat: true });
    down('KeyA', { repeat: true });
    expect(notes()).toEqual([{ t: 'note', n: 60, vel: 100 }]);
  });

  it('AC-K2.1 an octave change while held releases the original note', () => {
    down('KeyA');
    tap('KeyX');
    up('KeyA');
    expect(notes()).toEqual([
      { t: 'note', n: 60, vel: 100 },
      { t: 'note', n: 60, vel: 0 },
    ]);
    sent = [];
    tap('KeyA');
    expect(notes()[0].n).toBe(72);
  });

  it('AC-K2.1 window blur releases every key', () => {
    down('KeyA');
    down('KeyD');
    down('Space');
    down('Digit2');
    down('Digit3');
    sent = [];
    window.dispatchEvent(new Event('blur'));
    expect(sent).toEqual([
      { t: 'note', n: 60, vel: 0 },
      { t: 'note', n: 64, vel: 0 },
      { t: 'pedal', on: false },
      { t: 'lever', v: 0, drag: false, src: 'keyboard' },
      { t: 'push', on: false },
    ]);
  });

  it('AC-K2.1 a text field gaining focus releases all and suspends the map', () => {
    down('KeyA');
    const input = document.createElement('input');
    input.type = 'text';
    document.body.appendChild(input);
    sent = [];
    input.focus();
    expect(sent).toEqual([{ t: 'note', n: 60, vel: 0 }]);
    sent = [];
    const d = down('KeyS');
    up('KeyS');
    tap('KeyM');
    expect(sent).toEqual([]);
    expect(d.defaultPrevented).toBe(false);
  });

  it('AC-K2.1 toggling play off releases all; play off plays nothing', () => {
    down('KeyA');
    sent = [];
    tap('KeyM');
    expect(sent).toEqual([
      { t: 'setting', s: 6, v: 0 },
      { t: 'note', n: 60, vel: 0 },
    ]);
    sent = [];
    const d = down('KeyS');
    up('KeyS');
    expect(sent).toEqual([]);
    expect(d.defaultPrevented).toBe(false);
  });

  it('AC-K2.1 Ctrl/Cmd/Alt combinations and isComposing never play', () => {
    down('KeyA', { ctrlKey: true });
    down('KeyS', { metaKey: true });
    down('KeyD', { altKey: true });
    down('KeyF', { isComposing: true });
    expect(sent).toEqual([]);
  });

  it('AC-K2.1 Tab is never captured', () => {
    const d = down('Tab');
    expect(d.defaultPrevented).toBe(false);
    expect(sent).toEqual([]);
  });

  it('AC-K2.1 Space is the hold pedal while play is on (Enter activates buttons); with play off it is not captured', () => {
    const button = document.createElement('button');
    document.body.appendChild(button);
    button.focus();
    const d = down('Space');
    const u = up('Space');
    expect(d.defaultPrevented).toBe(true);
    expect(u.defaultPrevented).toBe(true);
    expect(sent).toEqual([
      { t: 'pedal', on: true },
      { t: 'pedal', on: false },
    ]);
    const enter = down('Enter');
    expect(enter.defaultPrevented).toBe(false);
    kb.setPlayFromState(false);
    sent = [];
    const d2 = down('Space');
    const u2 = up('Space');
    expect(d2.defaultPrevented).toBe(false);
    expect(u2.defaultPrevented).toBe(false);
    expect(sent).toEqual([]);
  });

  it('AC-K2.1 a play key while WRITE is armed only sends the note (not panel input)', () => {
    tap('KeyA');
    expect(sent.every((m) => m.t === 'note')).toBe(true);
    expect(sent.length).toBe(2);
  });
});

describe('AC-K2.2 plugin build: keyboard play off on first open', () => {
  it('AC-K2.2 play is off from the plugin first-run state; M turns it on', () => {
    // the plugin's first-run core state carries setting 6 = 0 (§4.4 default for the plugin)
    kb.setPlayFromState(false);
    expect(kb.play).toBe(false);
    tap('KeyA');
    expect(sent).toEqual([]);
    tap('KeyM');
    expect(sent).toEqual([{ t: 'setting', s: 6, v: 1 }]);
    expect(kb.play).toBe(true);
    // the core's next state confirms the setting
    kb.setPlayFromState(true);
    sent = [];
    tap('KeyA');
    expect(notes()).toEqual([
      { t: 'note', n: 60, vel: 100 },
      { t: 'note', n: 60, vel: 0 },
    ]);
  });
});
