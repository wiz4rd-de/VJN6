import { describe, expect, it, vi } from 'vitest';
import { base64, DesktopTransport, hasJuceBackend, unbase64 } from '../src/bridge';
import { effectiveSwitch, Field, ledTooltip, parseBatch, slotDisplay } from '../src/protocol';
import { noticeText, shortenMiddle } from '../src/bank/notices';
import { REASON_MODE, REASON_OUTPUT, REASON_SYSEX, sendPatchBlockReason } from '../src/bank/sendPatch';
import { handleShortcut } from '../src/shortcuts';
import { SaveScheduler } from '../src/web/storage';
import type { CoreState, EnvInfo } from '../src/protocol';

describe('bridge message parsing', () => {
  it('accepts the desktop payload: an array of JSON message strings', () => {
    const msgs = parseBatch(['{"t":"status","text":"a"}', '{"t":"notice","kind":"loaded","file":"x"}']);
    expect(msgs.map((m) => m.t)).toEqual(['status', 'notice']);
  });

  it('accepts that array as one JSON string, and the web poll result (array of objects)', () => {
    expect(parseBatch('["{\\"t\\":\\"status\\",\\"text\\":\\"a\\"}"]')).toEqual([{ t: 'status', text: 'a' }]);
    expect(parseBatch('[{"t":"midiOut","bytes":[144,60,64]}]')).toEqual([{ t: 'midiOut', bytes: [144, 60, 64] }]);
    expect(parseBatch('[]')).toEqual([]);
    expect(parseBatch('')).toEqual([]);
  });

  it('skips malformed entries', () => {
    expect(parseBatch(['{bad', 42, null, { x: 1 }, '{"t":"status","text":"ok"}'])).toEqual([{ t: 'status', text: 'ok' }]);
    expect(parseBatch('not json')).toEqual([]);
  });

  it('base64 round-trips bytes', () => {
    const bytes = new Uint8Array(70000).map((_, i) => (i * 7) & 255);
    expect(unbase64(base64(bytes))).toEqual(bytes);
    expect(base64(new Uint8Array([0xf0, 0x41, 0x31]))).toBe('8EEx');
  });
});

describe('desktop transport (JUCE native integration)', () => {
  it('is detected from window.__JUCE__ with the vjn6Command function', () => {
    const backend = { addEventListener: vi.fn(), emitEvent: vi.fn() };
    expect(hasJuceBackend({})).toBe(false);
    expect(hasJuceBackend({ __JUCE__: { backend, initialisationData: { __juce__functions: ['vjn6Command'] } } })).toBe(true);
    expect(hasJuceBackend({ __JUCE__: { backend, initialisationData: { __juce__functions: ['other'] } } })).toBe(false);
  });

  it('sends ready on connect via __juce__invoke and parses vjn6Events', () => {
    let listener: ((p: unknown) => void) | null = null;
    const backend = {
      addEventListener: vi.fn((id: string, fn: (p: unknown) => void) => {
        if (id === 'vjn6Events') listener = fn;
      }),
      emitEvent: vi.fn(),
    };
    const t = new DesktopTransport({ __JUCE__: { backend } });
    const got: unknown[] = [];
    t.connect((m) => got.push(...m));
    expect(backend.emitEvent).toHaveBeenCalledWith('__juce__invoke', {
      name: 'vjn6Command',
      params: ['{"t":"ready"}'],
      resultId: 1,
    });
    listener!(['{"t":"status","text":"saved"}']);
    expect(got).toEqual([{ t: 'status', text: 'saved' }]);
    t.send({ t: 'press', b: 'patch', i: 3 });
    expect(backend.emitEvent).toHaveBeenLastCalledWith('__juce__invoke', {
      name: 'vjn6Command',
      params: ['{"t":"press","b":"patch","i":3}'],
      resultId: 2,
    });
  });
});

describe('slot display, LED tooltip and switch readings', () => {
  it('shows slots per mode (§2)', () => {
    expect(slotDisplay(1, true)).toBe('A11');
    expect(slotDisplay(64, true)).toBe('A88');
    expect(slotDisplay(65, true)).toBe('B11');
    expect(slotDisplay(128, true)).toBe('B88');
    expect(slotDisplay(23, false)).toBe('37');
    expect(slotDisplay(56, false)).toBe('78');
    expect(slotDisplay(57, false)).toBeNull();
  });

  it('LED tooltip shows the full slot and name (§5.3)', () => {
    expect(ledTooltip({ manual: false, slot: 19, name: 'BRASS', mode: 1 })).toBe('A33 BRASS');
    expect(ledTooltip({ manual: false, slot: 19, name: 'BRASS', mode: 0 })).toBe('33 BRASS');
    expect(ledTooltip({ manual: false, slot: 73, name: 'PAD', mode: 0 })).toBe('slot 73 PAD');
    expect(ledTooltip({ manual: true, slot: 73, name: 'BRASS', mode: 0 })).toBe('MANUAL BRASS');
    expect(ledTooltip({ manual: false, slot: 2, name: '', mode: 0 })).toBe('12');
  });

  it('reads switch fields per mode (§4.1)', () => {
    expect(effectiveSwitch(Field.CHORUS, 3, true)).toBe(1);
    expect(effectiveSwitch(Field.CHORUS, 3, false)).toBe(3);
    expect(effectiveSwitch(Field.PWM_SRC, 2, true)).toBe(1);
    expect(effectiveSwitch(Field.SUB, 0, true)).toBe(1);
    expect(effectiveSwitch(Field.RANGE, 9, false)).toBe(0);
  });
});

describe('bank-manager notices (§5.5)', () => {
  it('shortens names over 40 characters in the middle', () => {
    expect(shortenMiddle('short.syx')).toBe('short.syx');
    const long = 'a'.repeat(30) + 'b'.repeat(30) + '.syx';
    const s = shortenMiddle(long);
    expect(Array.from(s)).toHaveLength(40);
    expect(s.startsWith('a'.repeat(20))).toBe(true);
    expect(s.endsWith('bbbbbbbbbbbbbbb.syx')).toBe(true);
    expect(s).toContain('…');
  });

  it('uses the exact §5.5 texts', () => {
    expect(noticeText('malformed', 'x.wav')).toBe('Not a VJN6 or Juno-106 file: x.wav');
    expect(noticeText('unreadable', 'x.syx')).toBe('Could not read x.syx');
    expect(noticeText('protected', '')).toBe('Memory is protected.');
    expect(noticeText('imported', 'b.syx', 128, 3)).toBe('128 patches imported, 3 dropped');
    expect(noticeText('loaded', 'BRASS.vjn6')).toBe('Loaded BRASS.vjn6');
    expect(noticeText('writeFailed', '/tmp/x')).toBe('Could not write /tmp/x');
    expect(noticeText('renamed', '')).toBe('');
  });
});

describe('Send patch availability (§5.5)', () => {
  const core = (mode: number, fn: number) => ({ mode, perf: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, fn, 1] }) as unknown as CoreState;
  const env = (o: Partial<EnvInfo>): EnvInfo => ({
    build: 'web',
    midiOutAvailable: true,
    sysexPermitted: true,
    midiAvailable: true,
    inputs: [],
    outputs: [],
    output: '',
    saveError: '',
    ...o,
  });

  it('is enabled only in 106 mode with function III, an output and SysEx', () => {
    expect(sendPatchBlockReason(core(1, 3), env({}))).toBe('');
    expect(sendPatchBlockReason(core(0, 3), env({}))).toBe(REASON_MODE);
    expect(sendPatchBlockReason(core(1, 2), env({}))).toBe(REASON_MODE);
    expect(sendPatchBlockReason(core(1, 3), env({ midiOutAvailable: false }))).toBe(REASON_OUTPUT);
    expect(sendPatchBlockReason(core(1, 3), env({ midiAvailable: false, midiOutAvailable: false }))).toBe(REASON_OUTPUT);
    expect(sendPatchBlockReason(core(1, 3), env({ sysexPermitted: false }))).toBe(REASON_SYSEX);
    expect(sendPatchBlockReason(core(1, 3), env({ sysexPermitted: false, midiOutAvailable: false }))).toBe(REASON_SYSEX);
  });
});

describe('shortcuts (R-U2)', () => {
  const ev = (code: string, init: Partial<KeyboardEvent> = {}) =>
    ({ code, altKey: true, ctrlKey: false, metaKey: false, repeat: false, isComposing: false, target: null, preventDefault: vi.fn(), ...init }) as unknown as KeyboardEvent;

  it('maps Alt + B / , / M / W / 1…8 by code, and Esc closes an overlay', () => {
    const calls: string[] = [];
    const a = {
      toggleBankManager: () => calls.push('bank'),
      toggleSettings: () => calls.push('settings'),
      closeOverlay: () => (calls.push('close'), true),
      manual: () => calls.push('manual'),
      write: () => calls.push('write'),
      patch: (i: number) => calls.push(`patch${i}`),
    };
    for (const c of ['KeyB', 'Comma', 'KeyM', 'KeyW', 'Digit1', 'Digit8']) expect(handleShortcut(ev(c), a)).toBe(true);
    expect(handleShortcut(ev('Digit9'), a)).toBe(false);
    expect(handleShortcut(ev('KeyB', { altKey: false }), a)).toBe(false);
    expect(handleShortcut(ev('KeyB', { ctrlKey: true }), a)).toBe(false);
    expect(handleShortcut(ev('Escape', { altKey: false }), a)).toBe(true);
    expect(calls).toEqual(['bank', 'settings', 'manual', 'write', 'patch1', 'patch8', 'close']);
  });
});

describe('save timing (F-13)', () => {
  it('saves 1 s after the last change and at least every 5 s during continuous change', () => {
    const s = new SaveScheduler();
    expect(s.due(0)).toBe(false);
    s.change(0);
    expect(s.due(900)).toBe(false);
    expect(s.due(1000)).toBe(true);
    s.saved();
    for (let t = 2000; t < 7000; t += 500) {
      s.change(t);
      if (t - 2000 < 5000) expect(s.due(t)).toBe(false);
    }
    expect(s.due(7000)).toBe(true);
  });
});
