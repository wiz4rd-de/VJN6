import { describe, expect, it } from 'vitest';
import { WebMidi } from '../src/web/midi';
import type { UiMessage } from '../src/protocol';
import type { DeviceChoices } from '../src/web/storage';

interface FakePort {
  id: string;
  name: string;
  state: string;
  onmidimessage: ((e: { data: Uint8Array }) => void) | null;
  send?: (b: number[]) => void;
}

function setup(sysex = true, choices: DeviceChoices = { disabledInputs: [], output: '' }) {
  const sent: UiMessage[] = [];
  const out: number[][] = [];
  const persisted: DeviceChoices[] = [];
  let changed = 0;
  const port = (id: string, name: string): FakePort => ({ id, name, state: 'connected', onmidimessage: null });
  const inputs = new Map<string, FakePort>([
    ['a', port('a', 'Keys')],
    ['b', port('b', 'Pads')],
  ]);
  const outputs = new Map<string, FakePort>([['o', { ...port('o', 'Synth'), send: (b) => out.push([...b]) }]]);
  const access = { inputs, outputs, sysexEnabled: sysex, onstatechange: null as null | (() => void) };
  const nav = { requestMIDIAccess: () => Promise.resolve(access) } as unknown as Navigator;
  const midi = new WebMidi(
    { send: (m) => sent.push(m), channel: () => 3, changed: () => changed++, persist: (c) => persisted.push(c) },
    choices,
  );
  return { midi, nav, inputs, outputs, access, sent, out, persisted, changed: () => changed };
}

describe('Web MIDI (§5.6, R-M1, R-M2)', () => {
  it('forwards each enabled input on its own port 1…15; messages over 24 bytes are dropped', async () => {
    const t = setup();
    await t.midi.request(t.nav);
    expect(t.midi.available).toBe(true);
    t.inputs.get('a')!.onmidimessage!({ data: new Uint8Array([0x90, 60, 100]) });
    t.inputs.get('b')!.onmidimessage!({ data: new Uint8Array([0xb0, 74, 64]) });
    t.inputs.get('b')!.onmidimessage!({ data: new Uint8Array(30).fill(0) });
    const sysex24 = [0xf0, 0x41, 0x31, ...new Array(20).fill(0), 0xf7];
    t.inputs.get('a')!.onmidimessage!({ data: new Uint8Array(sysex24) });
    expect(t.sent).toEqual([
      { t: 'midiIn', port: 1, bytes: [0x90, 60, 100] },
      { t: 'midiIn', port: 2, bytes: [0xb0, 74, 64] },
      { t: 'midiIn', port: 1, bytes: sysex24 },
    ]);
    expect(t.midi.inputs().map((i) => [i.name, i.enabled])).toEqual([
      ['Keys', true],
      ['Pads', true],
    ]);
  });

  it('a disabled or vanished input stops forwarding and acts as CC123 on the receive channel', async () => {
    const t = setup();
    await t.midi.request(t.nav);
    t.midi.setInputEnabled('a', false);
    expect(t.persisted.at(-1)).toEqual({ disabledInputs: ['a'], output: '' });
    expect(t.sent.at(-1)).toEqual({ t: 'midiIn', port: 1, bytes: [0xb2, 123, 0] });
    expect(t.inputs.get('a')!.onmidimessage).toBeNull();
    t.inputs.get('b')!.state = 'disconnected';
    t.access.onstatechange!();
    expect(t.sent.at(-1)).toEqual({ t: 'midiIn', port: 2, bytes: [0xb2, 123, 0] });
    expect(t.midi.inputs().map((i) => i.id)).toEqual(['a']); // unplugged devices vanish silently
  });

  it('sends midiOut to the chosen output; SysEx only when permitted', async () => {
    const t = setup(false, { disabledInputs: [], output: 'o' });
    await t.midi.request(t.nav);
    expect(t.midi.output()).toBe('o');
    expect(t.midi.outputAvailable()).toBe(true);
    t.midi.sendOut([0x90, 60, 64]);
    t.midi.sendOut([0xf0, 0x41, 0x32, 0, 5, 64, 0xf7]);
    expect(t.out).toEqual([[0x90, 60, 64]]);
    expect(t.midi.sysex).toBe(false);
  });

  it('without Web MIDI or when the request is rejected, MIDI is not available', async () => {
    const t = setup();
    await t.midi.request({} as Navigator);
    expect(t.midi.available).toBe(false);
    const r = setup();
    await r.midi.request({ requestMIDIAccess: () => Promise.reject(new Error('denied')) } as unknown as Navigator);
    expect(r.midi.available).toBe(false);
    expect(r.midi.outputAvailable()).toBe(false);
  });
});
