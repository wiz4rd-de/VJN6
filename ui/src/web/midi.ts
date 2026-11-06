// Web MIDI for the web build (§5.6, R-M1): enabled inputs are forwarded to the core as `midiIn`
// messages (one port 1…15 per input), `midiOut` messages from the core go to the chosen output.

import type { MidiPortInfo, UiMessage } from '../protocol';
import type { DeviceChoices } from './storage';

/** Longest message forwarded to the core (R-M2: SysEx up to 24 bytes, longer ones are dropped). */
export const MAX_MIDI_IN = 24;
const MAX_PORT = 15;

export interface MidiDeps {
  send(msg: UiMessage): void;
  /** The core's receive channel 1…16 (for the CC123 of a vanished input, R-M1). */
  channel(): number;
  /** Called whenever the device lists, availability or choices change. */
  changed(): void;
  /** Persists the device choices (IndexedDB key `devices`). */
  persist(choices: DeviceChoices): void;
}

export class WebMidi {
  private access: MIDIAccess | null = null;
  private ports = new Map<string, number>();
  private nextPort = 1;
  private attached = new Map<string, MIDIInput>();
  private disabled: Set<string>;
  private outputId: string;
  /** Web MIDI was requested and its answer is still pending. */
  pending = false;
  available = false;
  sysex = false;

  constructor(
    private deps: MidiDeps,
    choices: DeviceChoices,
  ) {
    this.disabled = new Set(choices.disabledInputs);
    this.outputId = choices.output;
  }

  /** Requests Web MIDI with SysEx (§5.7). Rejection or no support: MIDI not available. */
  async request(nav: Navigator = navigator): Promise<void> {
    if (typeof nav.requestMIDIAccess !== 'function') {
      this.available = false;
      this.deps.changed();
      return;
    }
    this.pending = true;
    this.deps.changed();
    try {
      const access = await nav.requestMIDIAccess({ sysex: true });
      this.access = access;
      this.available = true;
      this.sysex = access.sysexEnabled !== false;
      access.onstatechange = () => this.refresh();
    } catch {
      this.access = null;
      this.available = false;
      this.sysex = false;
    }
    this.pending = false;
    this.refresh();
  }

  private portOf(id: string): number {
    let p = this.ports.get(id);
    if (p === undefined) {
      p = Math.min(this.nextPort, MAX_PORT);
      if (this.nextPort <= MAX_PORT) this.nextPort++;
      this.ports.set(id, p);
    }
    return p;
  }

  /** Acts as CC123 for the notes of an input that went away or was disabled (R-M1). */
  private allNotesOff(id: string): void {
    const port = this.ports.get(id);
    if (port === undefined) return;
    const ch = Math.min(16, Math.max(1, this.deps.channel())) - 1;
    this.deps.send({ t: 'midiIn', port, bytes: [0xb0 | ch, 123, 0] });
  }

  private onMessage(id: string, e: MIDIMessageEvent): void {
    const data = e.data;
    if (!data || data.length === 0 || data.length > MAX_MIDI_IN) return;
    this.deps.send({ t: 'midiIn', port: this.portOf(id), bytes: Array.from(data) });
  }

  /** Re-attaches handlers after a device change; unplugged devices vanish silently. */
  refresh(): void {
    const live = new Map<string, MIDIInput>();
    if (this.access) {
      this.access.inputs.forEach((input) => {
        if (input.state === 'connected' && !this.disabled.has(input.id)) live.set(input.id, input);
      });
    }
    for (const [id, input] of this.attached) {
      if (!live.has(id)) {
        input.onmidimessage = null;
        this.attached.delete(id);
        this.allNotesOff(id);
      }
    }
    for (const [id, input] of live) {
      if (!this.attached.has(id)) {
        this.portOf(id);
        input.onmidimessage = (e: MIDIMessageEvent) => this.onMessage(id, e);
        this.attached.set(id, input);
      }
    }
    this.deps.changed();
  }

  inputs(): MidiPortInfo[] {
    const out: MidiPortInfo[] = [];
    this.access?.inputs.forEach((p) => {
      if (p.state === 'connected') out.push({ id: p.id, name: p.name ?? p.id, enabled: !this.disabled.has(p.id) });
    });
    return out;
  }

  outputs(): MidiPortInfo[] {
    const out: MidiPortInfo[] = [];
    this.access?.outputs.forEach((p) => {
      if (p.state === 'connected') out.push({ id: p.id, name: p.name ?? p.id });
    });
    return out;
  }

  /** The chosen output id if it is connected, else "". */
  output(): string {
    return this.outputs().some((o) => o.id === this.outputId) ? this.outputId : '';
  }

  outputAvailable(): boolean {
    return this.available && this.output() !== '';
  }

  choices(): DeviceChoices {
    return { disabledInputs: [...this.disabled], output: this.outputId };
  }

  setInputEnabled(id: string, on: boolean): void {
    if (on) this.disabled.delete(id);
    else this.disabled.add(id);
    this.deps.persist(this.choices());
    this.refresh();
  }

  setOutput(id: string): void {
    this.outputId = id;
    this.deps.persist(this.choices());
    this.deps.changed();
  }

  /** Sends one core `midiOut` message through the chosen output. */
  sendOut(bytes: number[]): void {
    if (!this.access || !this.outputId || bytes.length === 0) return;
    if (bytes[0] === 0xf0 && !this.sysex) return;
    const out = this.access.outputs.get(this.outputId);
    if (!out || out.state !== 'connected') return;
    try {
      out.send(bytes);
    } catch {
      // a port that went away between the check and the send: dropped silently
    }
  }
}
