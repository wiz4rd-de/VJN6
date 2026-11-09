// @vitest-environment jsdom
import { describe, expect, it, vi } from 'vitest';
import { app } from '../src/model.svelte';
import type { Transport } from '../src/bridge';
import type { CoreState, UiMessage } from '../src/protocol';

function state(over: Partial<CoreState> = {}): CoreState & { t: 'state' } {
  return {
    t: 'state',
    seq: 1,
    mode: 0,
    edit: [0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 127, 0, 0, 127, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0],
    name: 'INIT',
    stored: [0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 127, 0, 0, 127, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0],
    edited: false,
    manual: false,
    slot: 1,
    pendingBank: 1,
    pendingGroup: 0,
    perf: [0, 0, 32, 64, 0, 64, 0, 0, 0, 0, 1, 64, 1, 0, 64, 127, 1, 1, 3, 1],
    settings: [0, 2, 50, 0, 0, 2, 1, 0],
    led: { text: '11', dp: false, blink: false, message: 0 },
    lit: [1, 4, 5, 16, 24],
    blinking: [],
    writeArmed: false,
    ktArmed: false,
    lfoTrig: false,
    push: false,
    lever: 0,
    latency: 34,
    bankVersion: 1,
    ...over,
  };
}

function fakeTransport(sent: UiMessage[]): Transport {
  return { kind: 'desktop', connect: () => {}, send: (m) => sent.push(m), loadFile: () => {} };
}

describe('AC-K2.2 plugin build through the app model', () => {
  it('AC-K2.2 the plugin first-open state has play off; M turns it on through the core setting', () => {
    const sent: UiMessage[] = [];
    app.transport = fakeTransport(sent);
    const detach = app.keyboard.attach(window);
    app.handle([
      { t: 'env', build: 'plugin', midiOutAvailable: true, sysexPermitted: true, midiAvailable: true },
      state({ settings: [0, 2, 50, 0, 0, 2, 0, 0] }),
    ]);
    expect(app.env.build).toBe('plugin');
    expect(app.keyboard.play).toBe(false);
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyA', cancelable: true }));
    expect(sent).toEqual([]);
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyM', cancelable: true }));
    window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyM', cancelable: true }));
    expect(sent).toEqual([{ t: 'setting', s: 6, v: 1 }]);
    app.handle([state({ seq: 2, settings: [0, 2, 50, 0, 0, 2, 1, 0] })]);
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyS', cancelable: true }));
    expect(sent[1]).toEqual({ t: 'note', n: 62, vel: 100 });
    window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyS', cancelable: true }));
    detach();
  });
});

describe('app model', () => {
  it('keeps the bank from the full state and renders LEDs only after the start gesture', () => {
    const sent: UiMessage[] = [];
    app.transport = fakeTransport(sent);
    const bank = Array.from({ length: 128 }, (_, i) => ({ n: i === 1 ? 'BRASS' : 'INIT', p: new Array(25).fill(0) }));
    app.handle([state({ bank })]);
    expect(app.bank[1].n).toBe('BRASS');
    app.handle([state({ seq: 3 })]); // no bank: kept
    expect(app.bank.length).toBe(128);
    app.started = true;
    expect(app.ledOn(4)).toBe(true);
    expect(app.ledOn(3)).toBe(false);
    app.started = false;
    expect(app.ledOn(4)).toBe(false);
    app.started = true;
  });

  it('shows bank-manager notices for 4 s', () => {
    vi.useFakeTimers();
    app.handle([{ t: 'notice', kind: 'imported', file: 'x.syx', i: 128, j: 3 }]);
    expect(app.notice?.text).toBe('128 patches imported, 3 dropped');
    vi.advanceTimersByTime(3900);
    expect(app.notice).not.toBeNull();
    vi.advanceTimersByTime(200);
    expect(app.notice).toBeNull();
    app.handle([{ t: 'notice', kind: 'renamed', file: '' }]);
    expect(app.notice).toBeNull();
    vi.useRealTimers();
  });

  it('a file that cannot be read shows Er and "Could not read"', async () => {
    const sent: UiMessage[] = [];
    app.transport = fakeTransport(sent);
    const bad = { name: 'broken.syx', slice: () => ({ arrayBuffer: () => Promise.reject(new Error('io')) }) };
    await app.loadFiles([bad as unknown as File]);
    expect(sent).toEqual([{ t: 'message', m: 2 }]);
    expect(app.notice?.text).toBe('Could not read broken.syx');
  });

  it('only the first of several files is loaded, at most 1 MiB + 1 bytes of it', async () => {
    const loaded: [string, number][] = [];
    app.transport = { kind: 'web', connect: () => {}, send: () => {}, loadFile: (n, b) => loaded.push([n, b.length]) };
    const big = new File([new Uint8Array(3 * 1024 * 1024)], 'big.syx');
    const small = new File([new Uint8Array(24)], 'small.syx');
    await app.loadFiles([big, small]);
    expect(loaded).toEqual([['big.syx', 1024 * 1024 + 1]]);
  });
});
