// Front-panel layout (§5.2): the section boxes and every control with its position in the
// 1400 × 560 logical canvas. Both modes share this geometry; a control listed for one mode only is
// drawn only in that mode, and controls both modes have stay where they are, except in the VCF box
// (C-5, see vcfDepths). The order of CONTROLS is the Tab order (R-U3): sections left to right, then the
// memory row, then the right edge.

import { Field, Led, Perf, type ButtonName } from '../protocol';

export const CANVAS_W = 1400;
export const CANVAS_H = 560;
export const OVERLAY_W = 960;
export const OVERLAY_H = 520;

export const SLIDER_W = 20;
export const SLIDER_H = 300;
export const SLIDER_TOP = 126; // track top for every section slider
export const SLIDER_CAP_H = 14;
export const KNOB = 44;
export const BUTTON = 26;
export const SWITCH_W = 12;
export const SWITCH_STEP = 16; // height per position of a slide switch
export const HEADER_H = 22;

export type ModeSet = 'both' | '60' | '106';
export type CapColor = 'orange' | 'yellow' | 'grey' | 'white' | 'blue';
export type SectionId = 'controller' | 'lfo' | 'dco' | 'hpf' | 'vcf' | 'vca' | 'env' | 'chorus' | 'memory' | 'right';

export interface BoxDef {
  id: SectionId;
  title: string | null;
  x: number;
  y: number;
  w: number;
  h: number;
}

/** §5.2 boxes, exactly. */
export const BOXES: readonly BoxDef[] = [
  { id: 'controller', title: 'CONTROLLER', x: 20, y: 60, w: 180, h: 400 },
  { id: 'lfo', title: 'LFO', x: 216, y: 60, w: 100, h: 400 },
  { id: 'dco', title: 'DCO', x: 332, y: 60, w: 300, h: 400 },
  { id: 'hpf', title: 'HPF', x: 648, y: 60, w: 60, h: 400 },
  { id: 'vcf', title: 'VCF', x: 724, y: 60, w: 230, h: 400 },
  { id: 'vca', title: 'VCA', x: 970, y: 60, w: 90, h: 400 },
  { id: 'env', title: 'ENV', x: 1076, y: 60, w: 160, h: 400 },
  { id: 'chorus', title: 'CHORUS', x: 1252, y: 60, w: 60, h: 400 },
  { id: 'memory', title: null, x: 20, y: 476, w: 1292, h: 76 },
  { id: 'right', title: null, x: 1328, y: 60, w: 52, h: 492 },
];

export interface SwitchPos {
  value: number;
  label: string;
}

interface Common {
  /** Stable key, also the data-testid suffix (`ctl-<id>`). Shared by the 60 and 106 variant. */
  id: string;
  modes: ModeSet;
  /** ARIA name and tooltip legend (R-P5, R-U3). */
  aria: string;
}

export interface SliderDef extends Common {
  kind: 'slider';
  field: number;
  /** Panel legend above the track ("\n" breaks lines); per mode where it differs. */
  label: string;
  label106?: string;
  x: number; // track left
  y: number; // track top
}

export interface HpfSliderDef extends Common {
  kind: 'hpfSlider';
  x: number;
  y: number;
}

export interface KnobDef extends Common {
  kind: 'knob';
  perf: number;
  label: string;
  x: number; // top-left of the 44 × 44 knob
  y: number;
}

export interface SwitchDef extends Common {
  kind: 'switch';
  target: { field: number } | { perf: number };
  /** Positions from top to bottom. */
  positions: readonly SwitchPos[];
  positions106?: readonly SwitchPos[];
  /** Position labels beside the body ('side') or above and below it ('ends', two positions). */
  labels: 'side' | 'ends';
  legend?: string;
  legendAt?: 'above' | 'below';
  x: number; // body left
  y: number; // body top
}

export interface ButtonDef extends Common {
  kind: 'button';
  b: ButtonName;
  i?: number;
  led: number;
  /** The modes in which the button has an LED (§5.2; default both). */
  ledModes?: ModeSet;
  cap60?: CapColor;
  cap106?: CapColor;
  label: string;
  x: number; // cap left
  y: number; // cap top
  momentary?: boolean;
  /** Tooltip naming the combination press (R-U4), in the modes where the combination exists. */
  combo?: string;
  comboModes?: ModeSet;
}

export interface LeverDef extends Common {
  kind: 'lever';
  label: string;
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface LedDef {
  kind: 'led';
  id: 'led';
  modes: ModeSet;
  x: number;
  y: number;
}

export type ControlDef = SliderDef | HpfSliderDef | KnobDef | SwitchDef | ButtonDef | LeverDef | LedDef;

export interface TextDef {
  id: string;
  modes: ModeSet;
  text: string;
  x: number;
  y: number;
  align: 'left' | 'center';
  kind?: 'group' | 'wordmark' | 'badge';
}

const slider = (
  id: string,
  aria: string,
  field: number,
  label: string,
  x: number,
  label106?: string,
): SliderDef => ({ kind: 'slider', id, modes: 'both', aria, field, label, label106, x, y: SLIDER_TOP });

const knob = (id: string, modes: ModeSet, aria: string, perf: number, label: string, x: number, y: number): KnobDef => ({
  kind: 'knob',
  id,
  modes,
  aria,
  perf,
  label,
  x,
  y,
});

const MEM_CAP_Y = 494;
const bankX = (i: number) => 206 + (i - 1) * 34;
const patchX = (i: number) => 502 + (i - 1) * 34;

/**
 * The VCF box is §5.2's one exception to the shared-position rule (C-5): the 60 order is FREQ, RES,
 * polarity switch, ENV, LFO, KYBD; the 106 order is FREQ, RES, ENV, LFO, KYBD, polarity switch. Each
 * mode has its own defs (same ids, so state and tests address the same field) in its own order, which
 * keeps CONTROLS the Tab order of both modes.
 */
const vcfDepths = (modes: ModeSet, x: number): SliderDef[] => [
  { ...slider('vcf-env', 'VCF ENV', Field.VCF_ENV, 'ENV', x), modes },
  { ...slider('vcf-lfo', 'VCF LFO', Field.VCF_LFO, 'LFO', x + 36), modes },
  { ...slider('vcf-kybd', 'VCF KYBD', Field.VCF_KYBD, 'KYBD', x + 72), modes },
];

const vcfPolarity = (modes: ModeSet, x: number, positions: readonly SwitchPos[]): SwitchDef => ({
  kind: 'switch',
  id: 'vcf-pol',
  modes,
  aria: 'VCF POLARITY',
  target: { field: Field.VCF_POL },
  positions,
  labels: 'ends',
  legend: 'POLARITY',
  legendAt: 'above',
  x,
  y: 236,
});

function bankCombo(i: number): string | undefined {
  if (i === 1) return 'Shift+click with BANK 5 lit: bank 6';
  if (i === 2) return 'Shift+click with BANK 5 lit: bank 7';
  if (i === 5) return 'Shift+click with BANK 1 lit: bank 6, with BANK 2 lit: bank 7';
  return undefined;
}

export const CONTROLS: readonly ControlDef[] = [
  // ---- Controller ----
  knob('volume', 'both', 'VOLUME', Perf.VOLUME, 'VOLUME', 32, 94),
  {
    kind: 'switch',
    id: 'oct-transpose',
    modes: '60',
    aria: 'OCTAVE TRANSPOSE',
    target: { perf: Perf.OCT_TRANSPOSE },
    positions: [
      { value: 2, label: 'U' },
      { value: 1, label: 'N' },
      { value: 0, label: 'D' },
    ],
    labels: 'side',
    legend: 'OCTAVE\nTRANSPOSE',
    legendAt: 'below',
    x: 106,
    y: 92,
  },
  {
    kind: 'button',
    id: 'lfo-trig',
    modes: '60',
    aria: 'LFO TRIG',
    b: 'lfoTrig',
    led: Led.LFO_TRIG,
    cap60: 'white',
    label: 'LFO TRIG',
    x: 154,
    y: 106,
    momentary: true,
  },
  knob('bend-dco', 'both', 'BENDER DCO', Perf.BEND_DCO, 'DCO', 48, 196),
  knob('bend-vcf', 'both', 'BENDER VCF', Perf.BEND_VCF, 'VCF', 128, 196),
  knob('lfo-trig-sens', '106', 'LFO TRIG', Perf.LFO_TRIG_SENS, 'LFO TRIG', 48, 272),
  knob('porta-time', '106', 'PORTAMENTO TIME', Perf.PORTA_TIME, 'PORTAMENTO', 128, 272),
  { kind: 'lever', id: 'lever', modes: 'both', aria: 'BENDER', label: 'BENDER', x: 26, y: 356, w: 148, h: 60 },

  // ---- LFO ----
  slider('lfo-rate', 'LFO RATE', Field.LFO_RATE, 'RATE', 234),
  slider('lfo-delay', 'LFO DELAY TIME', Field.LFO_DELAY, 'DELAY\nTIME', 270),
  {
    kind: 'switch',
    id: 'lfo-mode',
    modes: '60',
    aria: 'LFO TRIG MODE',
    target: { perf: Perf.LFO_TRIG_MODE },
    positions: [
      { value: 0, label: 'AUTO' },
      { value: 1, label: 'MAN' },
    ],
    labels: 'ends',
    legend: 'MODE',
    legendAt: 'above',
    x: 300,
    y: 236,
  },

  // ---- DCO ----
  slider('dco-lfo', 'DCO LFO', Field.DCO_LFO, 'LFO', 350),
  slider('dco-pwm', 'DCO PWM', Field.DCO_PWM, 'PWM', 386),
  {
    kind: 'switch',
    id: 'pwm-mode',
    modes: 'both',
    aria: 'PWM MODE',
    target: { field: Field.PWM_SRC },
    positions: [
      { value: 0, label: 'LFO' },
      { value: 1, label: 'MAN' },
      { value: 2, label: 'ENV' },
    ],
    positions106: [
      { value: 0, label: 'LFO' },
      { value: 1, label: 'MAN' },
    ],
    labels: 'side',
    legend: 'PWM\nMODE',
    legendAt: 'above',
    x: 418,
    y: 200,
  },
  // RANGE exists in 106 mode only (60 mode has no DCO RANGE, C-2); in 60 mode its place stays
  // empty panel, because PULSE and SAW are shared with 106 mode and do not move (§5.2)
  ...([0, 1, 2] as const).map(
    (i): ButtonDef => ({
      kind: 'button',
      id: ['range-16', 'range-8', 'range-4'][i],
      modes: '106',
      aria: ['RANGE 16′', 'RANGE 8′', 'RANGE 4′'][i],
      b: 'range',
      i,
      led: Led.RANGE16 + i,
      cap106: 'white',
      label: ['16′', '8′', '4′'][i],
      x: 468,
      y: 144 + i * 56,
    }),
  ),
  {
    kind: 'button',
    id: 'pulse',
    modes: 'both',
    aria: 'PULSE',
    b: 'pulse',
    led: Led.PULSE,
    cap60: 'yellow',
    cap106: 'blue',
    label: 'PULSE',
    x: 512,
    y: 144,
  },
  {
    kind: 'button',
    id: 'saw',
    modes: 'both',
    aria: 'SAW',
    b: 'saw',
    led: Led.SAW,
    cap60: 'yellow',
    cap106: 'blue',
    label: 'SAW',
    x: 512,
    y: 200,
  },
  { kind: 'button', id: 'sub', modes: '60', aria: 'SUB', b: 'sub', led: Led.SUB, cap60: 'yellow', label: 'SUB', x: 512, y: 256 },
  slider('sub-level', 'SUB LEVEL', Field.SUB_LEVEL, 'SUB\nLEVEL', 562, 'SUB'),
  slider('noise', 'NOISE', Field.NOISE, 'NOISE', 598),

  // ---- HPF ----
  {
    kind: 'switch',
    id: 'hpf',
    modes: '60',
    aria: 'HPF',
    target: { field: Field.HPF },
    positions: [
      { value: 3, label: '3' },
      { value: 2, label: '2' },
      { value: 1, label: '1' },
      { value: 0, label: '0' },
    ],
    labels: 'side',
    x: 670,
    y: 236,
  },
  { kind: 'hpfSlider', id: 'hpf', modes: '106', aria: 'HPF', x: 680, y: SLIDER_TOP },

  // ---- VCF ----
  slider('vcf-freq', 'VCF FREQ', Field.VCF_FREQ, 'FREQ', 742),
  slider('vcf-res', 'VCF RES', Field.VCF_RES, 'RES', 778),
  vcfPolarity('60', 815, [
    { value: 0, label: '+' },
    { value: 1, label: '−' },
  ]),
  ...vcfDepths('60', 856),
  ...vcfDepths('106', 814),
  vcfPolarity('106', 922, [
    { value: 0, label: 'NORMAL' },
    { value: 1, label: 'INVERTED' },
  ]),

  // ---- VCA ----
  {
    kind: 'switch',
    id: 'vca-mode',
    modes: 'both',
    aria: 'VCA MODE',
    target: { field: Field.VCA_MODE },
    positions: [
      { value: 0, label: 'ENV' },
      { value: 1, label: 'GATE' },
    ],
    labels: 'ends',
    legend: 'MODE',
    legendAt: 'above',
    x: 988,
    y: 236,
  },
  slider('vca-level', 'VCA LEVEL', Field.VCA_LEVEL, 'LEVEL', 1032),

  // ---- ENV ----
  slider('env-a', 'ENV A', Field.ENV_A, 'A', 1096),
  slider('env-d', 'ENV D', Field.ENV_D, 'D', 1132),
  slider('env-s', 'ENV S', Field.ENV_S, 'S', 1168),
  slider('env-r', 'ENV R', Field.ENV_R, 'R', 1204),

  // ---- Chorus ----
  // OFF, I, II are radio buttons in both modes; the 60 OFF has no LED, and in 60 mode I+II is the
  // combination press CHORUS I <-> CHORUS II (§5.2, R-U4, C-6)
  {
    kind: 'button',
    id: 'chorus-off',
    modes: 'both',
    aria: 'CHORUS OFF',
    b: 'chorusOff',
    led: Led.CHORUS_OFF,
    ledModes: '106',
    cap60: 'white',
    cap106: 'white',
    label: 'OFF',
    x: 1269,
    y: 144,
  },
  {
    kind: 'button',
    id: 'chorus-i',
    modes: 'both',
    aria: 'CHORUS I',
    b: 'chorusI',
    led: Led.CHORUS_I,
    cap60: 'white',
    cap106: 'white',
    label: 'I',
    x: 1269,
    y: 200,
    combo: 'Shift+click with CHORUS II lit: I+II',
    comboModes: '60',
  },
  {
    kind: 'button',
    id: 'chorus-ii',
    modes: 'both',
    aria: 'CHORUS II',
    b: 'chorusII',
    led: Led.CHORUS_II,
    cap60: 'white',
    cap106: 'white',
    label: 'II',
    x: 1269,
    y: 256,
    combo: 'Shift+click with CHORUS I lit: I+II',
    comboModes: '60',
  },

  // ---- Memory row ----
  { kind: 'led', id: 'led', modes: 'both', x: 34, y: 491 },
  ...([0, 1] as const).map(
    (i): ButtonDef => ({
      kind: 'button',
      id: i ? 'group-b' : 'group-a',
      modes: '106',
      aria: i ? 'GROUP B' : 'GROUP A',
      b: 'group',
      i,
      led: Led.GROUP_A + i,
      cap106: 'white',
      label: i ? 'B' : 'A',
      x: 128 + i * 34,
      y: MEM_CAP_Y,
    }),
  ),
  ...[1, 2, 3, 4, 5, 6, 7, 8].map(
    (i): ButtonDef => ({
      kind: 'button',
      id: `bank-${i}`,
      modes: i <= 5 ? 'both' : '106',
      aria: `BANK ${i}`,
      b: 'bank',
      i,
      led: Led.BANK1 + i - 1,
      cap60: 'orange',
      cap106: 'blue',
      label: String(i),
      x: bankX(i),
      y: MEM_CAP_Y,
      combo: bankCombo(i),
      comboModes: '60',
    }),
  ),
  ...[1, 2, 3, 4, 5, 6, 7, 8].map(
    (i): ButtonDef => ({
      kind: 'button',
      id: `patch-${i}`,
      modes: 'both',
      aria: `PATCH ${i}`,
      b: 'patch',
      i,
      led: Led.PATCH1 + i - 1,
      cap60: 'yellow',
      cap106: 'white',
      label: String(i),
      x: patchX(i),
      y: MEM_CAP_Y,
    }),
  ),
  {
    kind: 'button',
    id: 'manual',
    modes: 'both',
    aria: 'MANUAL',
    b: 'manual',
    led: Led.MANUAL,
    cap60: 'white',
    cap106: 'blue',
    label: 'MANUAL',
    x: 798,
    y: MEM_CAP_Y,
  },
  {
    kind: 'button',
    id: 'write',
    modes: 'both',
    aria: 'WRITE',
    b: 'write',
    led: Led.WRITE,
    cap60: 'grey',
    cap106: 'white',
    label: 'WRITE',
    x: 842,
    y: MEM_CAP_Y,
  },
  { kind: 'button', id: 'hold', modes: '60', aria: 'HOLD', b: 'hold', led: Led.HOLD, cap60: 'orange', label: 'HOLD', x: 910, y: MEM_CAP_Y },
  {
    kind: 'button',
    id: 'arp-on',
    modes: '60',
    aria: 'ARPEGGIO ON',
    b: 'arpOn',
    led: Led.ARP,
    cap60: 'grey',
    label: 'ON',
    x: 972,
    y: MEM_CAP_Y,
  },
  {
    kind: 'switch',
    id: 'arp-mode',
    modes: '60',
    aria: 'ARPEGGIO MODE',
    target: { perf: Perf.ARP_MODE },
    positions: [
      { value: 0, label: 'UP' },
      { value: 1, label: 'U&D' },
      { value: 2, label: 'DOWN' },
    ],
    labels: 'side',
    x: 1040,
    y: 484,
  },
  {
    kind: 'switch',
    id: 'arp-range',
    modes: '60',
    aria: 'ARPEGGIO RANGE',
    target: { perf: Perf.ARP_RANGE },
    positions: [
      { value: 3, label: '3' },
      { value: 2, label: '2' },
      { value: 1, label: '1' },
    ],
    labels: 'side',
    x: 1112,
    y: 484,
  },
  knob('arp-rate', '60', 'ARPEGGIO RATE', Perf.ARP_RATE, 'RATE', 1168, 478),
  {
    kind: 'button',
    id: 'poly-1',
    modes: '106',
    aria: 'POLY 1',
    b: 'poly1',
    led: Led.POLY1,
    cap106: 'blue',
    label: 'POLY 1',
    x: 910,
    y: MEM_CAP_Y,
    combo: 'Shift+click with POLY 2 lit: UNISON',
  },
  {
    kind: 'button',
    id: 'poly-2',
    modes: '106',
    aria: 'POLY 2',
    b: 'poly2',
    led: Led.POLY2,
    cap106: 'blue',
    label: 'POLY 2',
    x: 954,
    y: MEM_CAP_Y,
    combo: 'Shift+click with POLY 1 lit: UNISON',
  },
  {
    kind: 'button',
    id: 'portamento',
    modes: '106',
    aria: 'PORTAMENTO',
    b: 'portamento',
    led: Led.PORTA,
    cap106: 'white',
    label: 'PORTAMENTO',
    x: 1014,
    y: MEM_CAP_Y,
  },
  {
    kind: 'button',
    id: 'key-transpose',
    modes: '106',
    aria: 'KEY TRANSPOSE',
    b: 'keyTranspose',
    led: Led.KEY_TRANSPOSE,
    cap106: 'blue',
    label: 'KEY TRANSPOSE',
    x: 1092,
    y: MEM_CAP_Y,
  },

  // ---- Right edge ----
  knob('tune', 'both', 'TUNE', Perf.TUNE, 'TUNE', 1332, 94),
];

export const TEXTS: readonly TextDef[] = [
  { id: 'bender-sens', modes: 'both', text: 'BENDER SENS', x: 110, y: 178, align: 'center', kind: 'group' },
  { id: 'range', modes: '106', text: 'RANGE', x: 481, y: 116, align: 'center', kind: 'group' },
  { id: 'group', modes: '106', text: 'GROUP', x: 128, y: 538, align: 'left', kind: 'group' },
  { id: 'bank', modes: 'both', text: 'BANK', x: 206, y: 538, align: 'left', kind: 'group' },
  { id: 'patch', modes: 'both', text: 'PATCH', x: 502, y: 538, align: 'left', kind: 'group' },
  { id: 'arpeggio', modes: '60', text: 'ARPEGGIO', x: 972, y: 538, align: 'left', kind: 'group' },
  { id: 'arp-mode', modes: '60', text: 'MODE', x: 1056, y: 538, align: 'center', kind: 'group' },
  { id: 'arp-range', modes: '60', text: 'RANGE', x: 1122, y: 538, align: 'center', kind: 'group' },
  { id: 'wordmark', modes: 'both', text: 'VJN6', x: 1354, y: 466, align: 'center', kind: 'wordmark' },
];

export function inMode(modes: ModeSet, is106: boolean): boolean {
  return modes === 'both' || (modes === '106') === is106;
}
