// Web MIDI test double (SPEC §10): replaces navigator.requestMIDIAccess before the page's scripts run
// (page.addInitScript). It records every request and every byte sent to its output, injects messages
// on its input, and can reject the request or grant it without SysEx (AC-SC2).
//
// The function below is serialised into the page, so it must not reference anything outside itself.

export type MidiMode = 'grant' | 'reject' | 'nosysex';

export const MIDI_IN_ID = 'vjn6-test-in';
export const MIDI_OUT_ID = 'vjn6-test-out';

export interface MidiDoubleApi {
  /** The options of every requestMIDIAccess call. */
  requests(): unknown[];
  /** Every message sent to the output, in order. */
  sent(): number[][];
  clearSent(): void;
  /** Delivers one MIDI message on the input, as a midimessage event. */
  inject(bytes: number[]): void;
  /** True once the page listens to the input. */
  listening(): boolean;
}

export function installMidiDouble(cfg: { mode: MidiMode; inId: string; outId: string }): void {
  const requests: unknown[] = [];
  const sent: number[][] = [];

  class FakePort extends EventTarget {
    readonly manufacturer = 'VJN6 tests';
    readonly version = '1';
    state = 'connected';
    connection = 'closed';
    onstatechange: ((e: Event) => void) | null = null;
    constructor(
      readonly id: string,
      readonly name: string,
      readonly type: 'input' | 'output',
    ) {
      super();
    }
    open(): Promise<this> {
      this.connection = 'open';
      return Promise.resolve(this);
    }
    close(): Promise<this> {
      this.connection = 'closed';
      return Promise.resolve(this);
    }
  }

  class FakeInput extends FakePort {
    onmidimessage: ((e: Event) => void) | null = null;
    constructor(id: string, name: string) {
      super(id, name, 'input');
    }
    receive(bytes: number[]): void {
      const e = new Event('midimessage');
      Object.defineProperty(e, 'data', { value: new Uint8Array(bytes) });
      this.dispatchEvent(e);
      if (typeof this.onmidimessage === 'function') this.onmidimessage(e);
    }
  }

  class FakeOutput extends FakePort {
    constructor(id: string, name: string) {
      super(id, name, 'output');
    }
    send(data: ArrayLike<number>): void {
      sent.push(Array.from(data));
    }
    clear(): void {}
  }

  const input = new FakeInput(cfg.inId, 'VJN6 Test Keys');
  const output = new FakeOutput(cfg.outId, 'VJN6 Test Synth');
  const access = Object.assign(new EventTarget(), {
    inputs: new Map([[input.id, input]]),
    outputs: new Map([[output.id, output]]),
    sysexEnabled: cfg.mode !== 'nosysex',
    onstatechange: null as ((e: Event) => void) | null,
  });

  function requestMIDIAccess(options?: unknown): Promise<typeof access> {
    requests.push(options ?? null);
    if (cfg.mode === 'reject') {
      return Promise.reject(new DOMException('Web MIDI denied by the test double', 'SecurityError'));
    }
    return Promise.resolve(access);
  }

  Object.defineProperty(Navigator.prototype, 'requestMIDIAccess', {
    value: requestMIDIAccess,
    configurable: true,
    writable: true,
  });

  const api: MidiDoubleApi = {
    requests: () => requests.slice(),
    sent: () => sent.map((m) => m.slice()),
    clearSent: () => {
      sent.length = 0;
    },
    inject: (bytes) => input.receive(bytes),
    listening: () => typeof input.onmidimessage === 'function',
  };
  (window as unknown as { __midiDouble: MidiDoubleApi }).__midiDouble = api;
}
