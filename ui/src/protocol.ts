// The UI <-> core JSON protocol (docs/BRIDGE.md). Ids mirror core/include/vjn6/types.h and
// instrument.h; they are part of the protocol and never renumbered.

/** Patch fields: B0..B15 = 0..15, W0..W8 = 16..24 (§4.1). */
export const Field = {
  LFO_RATE: 0,
  LFO_DELAY: 1,
  DCO_LFO: 2,
  DCO_PWM: 3,
  NOISE: 4,
  VCF_FREQ: 5,
  VCF_RES: 6,
  VCF_ENV: 7,
  VCF_LFO: 8,
  VCF_KYBD: 9,
  VCA_LEVEL: 10,
  ENV_A: 11,
  ENV_D: 12,
  ENV_S: 13,
  ENV_R: 14,
  SUB_LEVEL: 15,
  RANGE: 16,
  PULSE: 17,
  SAW: 18,
  SUB: 19,
  CHORUS: 20,
  PWM_SRC: 21,
  VCF_POL: 22,
  VCA_MODE: 23,
  HPF: 24,
} as const;
export const NUM_SLIDERS = 16;
export const NUM_FIELDS = 25;
export const NUM_SLOTS = 128;

/** Performance settings (§4.2), the `Perf` enum. */
export const Perf = {
  KEY_MODE: 0,
  PORTA_ON: 1,
  PORTA_TIME: 2,
  BEND_DCO: 3,
  BEND_VCF: 4,
  LFO_TRIG_SENS: 5,
  LFO_TRIG_MODE: 6,
  HOLD: 7,
  ARP_ON: 8,
  ARP_MODE: 9,
  ARP_RANGE: 10,
  ARP_RATE: 11,
  OCT_TRANSPOSE: 12,
  KEY_TRANSPOSE: 13,
  TUNE: 14,
  VOLUME: 15,
  MIDI_CHANNEL: 16,
  OMNI: 17,
  MIDI_FUNCTION: 18,
  PROTECT: 19,
} as const;
export const NUM_PERF = 20;

/** §4.4 settings held in the core state, the `SettingId` enum. */
export const Setting = {
  MODE: 0,
  CHORUS_NOISE: 1,
  VARIATION: 2,
  VELOCITY: 3,
  MONO: 4,
  OVERSAMPLE: 5,
  KBD_PLAY: 6,
  ZOOM: 7,
} as const;
export const NUM_SETTINGS = 8;

/** Button LED bit indices (`ButtonLed`). */
export const Led = {
  RANGE16: 0,
  RANGE8: 1,
  RANGE4: 2,
  PULSE: 3,
  SAW: 4,
  SUB: 5,
  CHORUS_OFF: 6,
  CHORUS_I: 7,
  CHORUS_II: 8,
  PORTA: 9,
  HOLD: 10,
  ARP: 11,
  POLY1: 12,
  POLY2: 13,
  GROUP_A: 14,
  GROUP_B: 15,
  BANK1: 16,
  PATCH1: 24,
  MANUAL: 32,
  WRITE: 33,
  KEY_TRANSPOSE: 34,
  LFO_TRIG: 35,
} as const;

/** LED messages (`LedMessage`): §5.4 text and tooltip. */
export const LED_MESSAGES: Record<number, { text: string; tooltip: string }> = {
  1: { text: 'Pr', tooltip: 'MEMORY PROTECTED' },
  2: { text: 'Er', tooltip: 'FORMAT CONFLICT' },
  3: { text: 'rc', tooltip: 'MIDI RECEIVED' },
  4: { text: 'ok', tooltip: 'WRITE COMPLETED' },
  5: { text: 'rE', tooltip: 'STATE RESET' },
};

export type ButtonName =
  | 'range'
  | 'pulse'
  | 'saw'
  | 'sub'
  | 'chorusOff'
  | 'chorusI'
  | 'chorusII'
  | 'portamento'
  | 'hold'
  | 'arpOn'
  | 'bank'
  | 'group'
  | 'patch'
  | 'manual'
  | 'write'
  | 'poly1'
  | 'poly2'
  | 'keyTranspose'
  | 'lfoTrig';

export type ExportKind = 'bankJson' | 'bankSyx' | 'patchJson' | 'patchSyx';
export type Src = 'panel' | 'overlay' | 'keyboard';

/** UI -> core (and host-only) messages. */
export type UiMessage =
  | { t: 'ready' }
  | { t: 'field'; f: number; v: number; src?: Src }
  | { t: 'perf'; p: number; v: number; src?: Src }
  | { t: 'setting'; s: number; v: number }
  | { t: 'press'; b: ButtonName; i?: number; shift?: boolean }
  | { t: 'release'; b: ButtonName }
  | { t: 'lever'; v: number; drag: boolean; push?: boolean; src?: Src }
  | { t: 'push'; on: boolean }
  | { t: 'note'; n: number; vel: number }
  | { t: 'pedal'; on: boolean }
  | { t: 'midiIn'; port: number; bytes: number[] }
  | { t: 'selectSlot'; slot: number }
  | { t: 'rename'; slot: number; name: string }
  | { t: 'copySlot'; from: number; to: number }
  | { t: 'slotToEdit'; slot: number }
  | { t: 'sendPatch' }
  | { t: 'releaseAll' }
  | { t: 'message'; m: number }
  | { t: 'loadFile'; name: string; b64: string }
  | { t: 'export'; what: ExportKind }
  // host-only messages (desktop host / web glue)
  | { t: 'openFile' }
  | { t: 'openDevices' }
  | { t: 'midiOutput'; name: string }
  | { t: 'gesture'; f?: number; p?: number; begin: boolean }
  | { t: 'zoom'; v: number };

export const HOST_ONLY = new Set(['openFile', 'openDevices', 'midiOutput', 'gesture', 'zoom']);

export interface LedState {
  text: string;
  dp: boolean;
  blink: boolean;
  message: number;
}

export interface BankEntry {
  n: string;
  p: number[];
}

/** The `state` message: the complete UI snapshot (R-S3). */
export interface CoreState {
  seq: number;
  mode: number;
  edit: number[];
  name: string;
  stored: number[];
  edited: boolean;
  manual: boolean;
  slot: number;
  pendingBank: number;
  pendingGroup: number;
  perf: number[];
  settings: number[];
  led: LedState;
  lit: number[];
  blinking: number[];
  writeArmed: boolean;
  ktArmed: boolean;
  lfoTrig: boolean;
  push: boolean;
  lever: number;
  latency: number;
  bankVersion: number;
  bank?: BankEntry[];
}

export type NoticeKind =
  | 'loaded'
  | 'bankLoaded'
  | 'imported'
  | 'protected'
  | 'malformed'
  | 'unreadable'
  | 'renamed'
  | 'writeFailed';

export interface MidiPortInfo {
  id: string;
  name: string;
  enabled?: boolean;
}

export type Build = 'plugin' | 'standalone' | 'web';

export interface EnvInfo {
  build: Build;
  midiOutAvailable: boolean;
  sysexPermitted: boolean;
  midiAvailable: boolean;
  inputs: MidiPortInfo[];
  outputs: MidiPortInfo[];
  output: string;
  saveError: string;
}

/** Core (and host) -> UI messages. */
export type CoreMessage =
  | ({ t: 'state' } & CoreState)
  | { t: 'notice'; kind: NoticeKind; file: string; i?: number; j?: number }
  | { t: 'exportData'; what: ExportKind; name: string; b64: string }
  | { t: 'midiOut'; bytes: number[] }
  | { t: 'status'; text: string }
  | ({ t: 'env' } & Partial<EnvInfo>);

function isObject(x: unknown): x is Record<string, unknown> {
  return typeof x === 'object' && x !== null && !Array.isArray(x);
}

function toMessage(x: unknown): CoreMessage | null {
  let v = x;
  if (typeof v === 'string') {
    try {
      v = JSON.parse(v);
    } catch {
      return null;
    }
  }
  if (!isObject(v) || typeof v.t !== 'string') return null;
  return v as unknown as CoreMessage;
}

/**
 * Parses one batch of core -> UI messages. Accepts the desktop event payload (an array of JSON
 * message strings, or that array as one JSON string) and the web poll result (a JSON array of
 * message objects or strings). Unparseable entries are skipped.
 */
export function parseBatch(payload: unknown): CoreMessage[] {
  let v = payload;
  if (typeof v === 'string') {
    if (v.length === 0) return [];
    try {
      v = JSON.parse(v);
    } catch {
      return [];
    }
  }
  if (Array.isArray(v)) {
    const out: CoreMessage[] = [];
    for (const item of v) {
      const m = toMessage(item);
      if (m) out.push(m);
    }
    return out;
  }
  const one = toMessage(v);
  return one ? [one] : [];
}

/** True if bit `bit` is in a `lit` / `blinking` index list. */
export function hasBit(bits: readonly number[] | undefined, bit: number): boolean {
  return !!bits && bits.includes(bit);
}

/** Number of valid values of a switch field (§4.1); a stored value outside is read as 0. */
export function switchCount(field: number): number {
  switch (field) {
    case Field.RANGE:
      return 3;
    case Field.CHORUS:
      return 4;
    case Field.PWM_SRC:
      return 3;
    case Field.HPF:
      return 4;
    default:
      return 2;
  }
}

/** The value of a switch field as the active mode reads it (§4.1). */
export function effectiveSwitch(field: number, stored: number, is106: boolean): number {
  let v = stored;
  if (v >= switchCount(field)) v = 0;
  if (is106) {
    if (field === Field.SUB) return 1;
    if (field === Field.CHORUS && v === 3) return 1;
    if (field === Field.PWM_SRC && v === 2) return 1;
  }
  return v;
}

/** Slot display per mode (§2 Slot): `A23`, `23`, or null for 60-mode slots 57-128. */
export function slotDisplay(slot: number, is106: boolean): string | null {
  if (is106) {
    const group = slot <= 64 ? 'A' : 'B';
    const within = (slot - 1) % 64;
    return `${group}${Math.floor(within / 8) + 1}${(within % 8) + 1}`;
  }
  if (slot >= 1 && slot <= 56) return `${Math.floor((slot - 1) / 8) + 1}${((slot - 1) % 8) + 1}`;
  return null;
}

/** The LED tooltip (§5.3): `A23 BRASS`, `23 BRASS`, `slot 73 PAD`, `MANUAL BRASS`. */
export function ledTooltip(s: Pick<CoreState, 'manual' | 'slot' | 'name' | 'mode'>): string {
  const where = s.manual ? 'MANUAL' : (slotDisplay(s.slot, s.mode === 1) ?? `slot ${s.slot}`);
  return s.name ? `${where} ${s.name}` : where;
}
