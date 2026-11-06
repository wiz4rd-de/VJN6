// Display values (R-P5, R-P6): the TypeScript twin of core/panel/display.cpp. Every string equals
// the C++ formatter's for the same byte and mode (tests/golden/display_table.json, AC-P5.1).
// Rounding rule: round half away from zero on the double value, digits assembled from the integer.

import * as dm from './dmath';
import { Field, Perf, switchCount } from '../protocol';

// ---- engine constants used for display (core/juno/constants.h) ----
const DUTY_CENTRE = 0.5;
const DUTY_SPAN = 0.45;
const TUNE_CENTS_PER_STEP = 50.0 / 64.0;
const KMAX_106 = 4.5;
const KMAX_60 = 4.35;
const CUTOFF_BASE_60_HZ = 28.1;
const CUTOFF_SPAN_60_OCT = 10.5;
const ENV_DEPTH_60_OCT = 10.0;
const LFO_DEPTH_60_OCT = 6.0;
const BEND_VCF_60_OCT = 4.0;
const W106_PER_BYTE = 128;
const W106_PER_OCT = 1143.0;
const ENV_DEPTH_106_OCT = 14.2;
const LFO_DEPTH_106_OCT = 3.5;
const BEND_VCF_106_OCT = 3.5;
const CUTOFF_BASE_106_HZ = 5.53;
const ATTACK_MIN_60 = 0.001;
const ATTACK_MAX_60 = 3.0;
const DECREL_MIN_60 = 0.002;
const DECREL_MAX_60 = 12.0;
const ATTACK_MIN_106 = 0.0015;
const ATTACK_MAX_106 = 3.0;
const DECREL_MIN_106 = 0.0015;
const DECREL_MAX_106 = 12.0;
const LFO_MIN_60_HZ = 0.3;
const LFO_RATIO_60 = 66.7;
const LFO_DELAY_MAX_60_S = 2.0;
const LFO_DELAY_MAX_106_S = 3.0;
const LFO_TABLE_106 = [0.04, 1.07, 1.99, 2.91, 3.84, 4.76, 6.26, 7.73, 10.95, 18.67, 29.76];
const LFO_DCO_CENTS_60 = 300.0;
const LFO_DCO_CENTS_106 = 400.0;
const PORTA_MAX_S = 3.0;
const BEND_DCO_60_CENTS = 700.0;
const BEND_DCO_106_CENTS = 1200.0;
const ARP_MIN_HZ = 1.5;
const ARP_RATIO = 33.3;

const PM = '±'; // ±

export function roundAway(x: number): number {
  return x < 0 ? -Math.floor(-x + 0.5) : Math.floor(x + 0.5);
}

/** n >= 0 holds the value * 10^decimals. */
function digits(n: number, decimals: number): string {
  let s = String(n);
  if (decimals === 0) return s;
  while (s.length <= decimals) s = '0' + s;
  return s.slice(0, s.length - decimals) + '.' + s.slice(s.length - decimals);
}

function scale10(d: number): number {
  let s = 1.0;
  for (let i = 0; i < d; ++i) s *= 10.0;
  return s;
}

// ---- number formats (R-P5) ----

export function fixed(x: number, decimals: number): string {
  const r = roundAway(x * scale10(decimals));
  const neg = r < 0;
  const s = digits(neg ? -r : r === 0 ? 0 : r, decimals);
  return neg ? '-' + s : s;
}

export function signedFixed(x: number, decimals: number): string {
  const r = roundAway(x * scale10(decimals));
  if (r < 0) return '-' + digits(-r, decimals);
  return '+' + digits(r === 0 ? 0 : r, decimals);
}

/** Three significant digits: "248", "5.53", "40700". */
export function sig3(x: number): string {
  if (x === 0.0) return '0';
  const neg = x < 0;
  const a = neg ? -x : x;
  let k = 0;
  let v = a;
  while (v < 100.0) {
    v *= 10.0;
    ++k;
  }
  while (v >= 1000.0) {
    v /= 10.0;
    --k;
  }
  let n = Math.floor(v + 0.5);
  if (n >= 1000) {
    n = Math.trunc(n / 10);
    --k;
  }
  let s: string;
  if (k > 0) {
    s = digits(n, k);
  } else {
    s = String(n);
    for (let i = 0; i < -k; ++i) s += '0';
  }
  return neg ? '-' + s : s;
}

export function hz(x: number): string {
  return sig3(x) + ' Hz';
}

export function seconds(s: number): string {
  if (s < 1.0) {
    const ms = roundAway(s * 1000.0);
    if (ms < 1000.0) return String(ms === 0 ? 0 : ms) + ' ms';
  }
  return fixed(s, 2) + ' s';
}

export function db(gain: number): string {
  if (gain <= 0.0) return '-inf dB';
  return signedFixed(20.0 * dm.log10(gain), 1) + ' dB';
}

export function percent(x: number): string {
  const r = roundAway(x);
  return String(r === 0 ? 0 : r) + ' %';
}

function octaves(v: number, plusMinus: boolean): string {
  if (plusMinus) return PM + fixed(v, 2) + ' oct';
  return signedFixed(v, 2) + ' oct';
}

function cents(v: number, plusMinus: boolean): string {
  if (plusMinus) return PM + fixed(v, 1) + ' cents';
  return signedFixed(v, 1) + ' cents';
}

// ---- control values ----

/** byte / 12.7 with one decimal: "5.0". */
export function sliderV(byte: number): string {
  return fixed(byte / 12.7, 1);
}

/** Master tune in cents, one decimal with sign: "+10.2". */
export function tuneV(byte: number): string {
  return signedFixed((byte - 64) * TUNE_CENTS_PER_STEP, 1);
}

function lfo106Hz(b: number): number {
  const x = (b / 127.0) * 10.0;
  let i = Math.floor(x);
  if (i > 9) i = 9;
  return LFO_TABLE_106[i] + (LFO_TABLE_106[i + 1] - LFO_TABLE_106[i]) * (x - i);
}

function sliderTime(b: number, tmin: number, tmax: number): number {
  const x = b / 127.0;
  return tmin + (tmax - tmin) * x * x;
}

const HPF_60 = ['flat', '122 Hz', '269 Hz', '571 Hz'];
const HPF_106 = ['bass boost', 'flat', '236 Hz', '754 Hz'];

/**
 * Engine value of a patch field at a byte for the active mode (R-P5). `ctx` is the edit buffer
 * (25 fields): it supplies the PWM source (for PWM) and the polarity (for VCF ENV).
 */
export function fieldEngineValue(field: number, b: number, is106: boolean, ctx: readonly number[]): string {
  const x = b / 127.0;
  const effSw = (f: number): number => {
    let v = ctx[f] ?? 0;
    if (v >= switchCount(f)) v = 0;
    if (is106 && f === Field.PWM_SRC && v === 2) v = 1;
    return v;
  };
  switch (field) {
    case Field.LFO_RATE:
      return hz(is106 ? lfo106Hz(b) : LFO_MIN_60_HZ * dm.pow(LFO_RATIO_60, x));
    case Field.LFO_DELAY:
      return seconds((is106 ? LFO_DELAY_MAX_106_S : LFO_DELAY_MAX_60_S) * x);
    case Field.DCO_LFO:
      return cents((is106 ? LFO_DCO_CENTS_106 : LFO_DCO_CENTS_60) * x, true);
    case Field.DCO_PWM:
      if (effSw(Field.PWM_SRC) === 1) return percent(100.0 * (DUTY_CENTRE + DUTY_SPAN * x));
      return PM + percent(100.0 * DUTY_SPAN * x);
    case Field.NOISE:
    case Field.SUB_LEVEL:
    case Field.VCF_KYBD:
    case Field.ENV_S:
      return percent(100.0 * x);
    case Field.VCF_FREQ:
      return hz(
        is106
          ? CUTOFF_BASE_106_HZ * dm.exp2((W106_PER_BYTE * b) / W106_PER_OCT)
          : CUTOFF_BASE_60_HZ * dm.exp2(CUTOFF_SPAN_60_OCT * x),
      );
    case Field.VCF_RES:
      return 'k ' + fixed((is106 ? KMAX_106 : KMAX_60) * x * Math.sqrt(x), 2);
    case Field.VCF_ENV: {
      const oct = (is106 ? ENV_DEPTH_106_OCT : ENV_DEPTH_60_OCT) * x;
      return octaves(effSw(Field.VCF_POL) ? -oct : oct, false);
    }
    case Field.VCF_LFO:
      return octaves((is106 ? LFO_DEPTH_106_OCT : LFO_DEPTH_60_OCT) * x, true);
    case Field.VCA_LEVEL:
      return db(x);
    case Field.ENV_A:
      return seconds(
        is106 ? sliderTime(b, ATTACK_MIN_106, ATTACK_MAX_106) : sliderTime(b, ATTACK_MIN_60, ATTACK_MAX_60),
      );
    case Field.ENV_D:
    case Field.ENV_R:
      return seconds(
        is106 ? sliderTime(b, DECREL_MIN_106, DECREL_MAX_106) : sliderTime(b, DECREL_MIN_60, DECREL_MAX_60),
      );
    case Field.HPF: {
      const p = b < 4 ? b : 0;
      return is106 ? HPF_106[p] : HPF_60[p];
    }
    default:
      return '';
  }
}

/** Engine value of a knob (VOLUME, TUNE, bender sensitivities, LFO TRIG, PORTAMENTO, ARP RATE). */
export function perfEngineValue(perf: number, v: number, is106: boolean): string {
  const x = v / 127.0;
  switch (perf) {
    case Perf.VOLUME:
      return db(x * x);
    case Perf.TUNE:
      return signedFixed((v - 64) * TUNE_CENTS_PER_STEP, 1) + ' cents';
    case Perf.BEND_DCO:
      return cents((is106 ? BEND_DCO_106_CENTS : BEND_DCO_60_CENTS) * x, true);
    case Perf.BEND_VCF:
      return octaves((is106 ? BEND_VCF_106_OCT : BEND_VCF_60_OCT) * x, true);
    case Perf.LFO_TRIG_SENS:
      return cents(LFO_DCO_CENTS_106 * x, true);
    case Perf.PORTA_TIME:
      return seconds(PORTA_MAX_S * x * x);
    case Perf.ARP_RATE:
      return sig3(ARP_MIN_HZ * dm.pow(ARP_RATIO, x)) + ' steps/s';
    default:
      return '';
  }
}

/** The panel's v for a field: position 0-3 for HPF, byte/12.7 otherwise. */
export function fieldV(field: number, byte: number): string {
  return field === Field.HPF ? String(byte < 4 ? byte : 0) : sliderV(byte);
}

/** The panel's v for a knob: cents for TUNE, byte/12.7 otherwise. */
export function perfV(perf: number, value: number): string {
  return perf === Perf.TUNE ? tuneV(value) : sliderV(value);
}

/** R-P5 tooltip: `<legend> <v> = <engine value>`. */
export function fieldTooltip(legend: string, field: number, byte: number, is106: boolean, ctx: readonly number[]): string {
  return `${legend} ${fieldV(field, byte)} = ${fieldEngineValue(field, byte, is106, ctx)}`;
}

export function perfTooltip(legend: string, perf: number, value: number, is106: boolean): string {
  return `${legend} ${perfV(perf, value)} = ${perfEngineValue(perf, value, is106)}`;
}

// ---- typed entry (R-U1) ----

/**
 * Parses a typed value. Sliders and knobs: round(v * 12.7); TUNE: cents -> byte; HPF: position.
 * Returns null for anything that is not a number (the entry reverts); out-of-range values clamp.
 */
export function parseTyped(kind: 'byte' | 'tune' | 'hpf', text: string, min: number, max: number): number | null {
  const t = text.trim();
  if (t === '' || !/^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?$/i.test(t)) return null;
  const v = Number(t);
  if (!Number.isFinite(v)) return null;
  let b: number;
  if (kind === 'tune') b = roundAway(v / TUNE_CENTS_PER_STEP) + 64;
  else if (kind === 'hpf') b = roundAway(v);
  else b = roundAway(v * 12.7);
  if (b < min) b = min;
  if (b > max) b = max;
  return b === 0 ? 0 : b;
}

// ---- host parameters (§4.8): value strings, identical to the panel's v (R-P6) ----

export interface HostParam {
  id: string;
  name: string;
  isChoice: boolean;
  min: number;
  max: number;
  choices: string[];
}

const ONOFF = ['OFF', 'ON'];

export const HOST_PARAMS: readonly HostParam[] = (() => {
  const sliderIds = [
    'p_lfo_rate', 'p_lfo_delay', 'p_dco_lfo', 'p_dco_pwm', 'p_noise', 'p_vcf_freq', 'p_vcf_res', 'p_vcf_env',
    'p_vcf_lfo', 'p_vcf_kybd', 'p_vca_level', 'p_env_a', 'p_env_d', 'p_env_s', 'p_env_r', 'p_sub_level',
  ];
  const sliderNames = [
    'LFO rate', 'LFO delay time', 'DCO LFO depth', 'DCO PWM', 'Noise level', 'VCF frequency', 'VCF resonance',
    'VCF envelope depth', 'VCF LFO depth', 'VCF keyboard follow', 'VCA level', 'ENV attack', 'ENV decay',
    'ENV sustain', 'ENV release', 'Sub-oscillator level',
  ];
  const p: HostParam[] = sliderIds.map((id, i) => ({
    id,
    name: sliderNames[i],
    isChoice: false,
    min: 0,
    max: 127,
    choices: [],
  }));
  const choice = (id: string, name: string, min: number, choices: string[]): HostParam => ({
    id,
    name,
    isChoice: true,
    min,
    max: min + choices.length - 1,
    choices,
  });
  const int = (id: string, name: string, min: number, max: number): HostParam => ({
    id,
    name,
    isChoice: false,
    min,
    max,
    choices: [],
  });
  p.push(
    choice('s_range', 'Range', 0, ["16'", "8'", "4'"]),
    choice('s_pulse', 'Pulse on', 0, ONOFF),
    choice('s_saw', 'Saw on', 0, ONOFF),
    choice('s_sub', 'Sub on', 0, ONOFF),
    choice('s_chorus', 'Chorus', 0, ['OFF', 'I', 'II', 'I+II (60)']),
    choice('s_pwm_src', 'PWM source', 0, ['LFO', 'MANUAL', 'ENV (60)']),
    choice('s_vcf_pol', 'VCF envelope polarity', 0, ['NORMAL', 'INVERTED']),
    choice('s_vca_mode', 'VCA mode', 0, ['ENV', 'GATE']),
    choice('s_hpf', 'HPF position', 0, ['0', '1', '2', '3']),
    choice('f_key_mode', 'Key mode', 0, ['POLY 1', 'POLY 2', 'UNISON']),
    choice('f_porta_on', 'Portamento on', 0, ONOFF),
    int('f_porta_time', 'Portamento time', 0, 127),
    int('f_bend_dco', 'Bender DCO sensitivity', 0, 127),
    int('f_bend_vcf', 'Bender VCF sensitivity', 0, 127),
    int('f_lfo_trig_sens', 'LFO trigger sensitivity', 0, 127),
    choice('f_lfo_trig_mode', 'LFO trigger mode', 0, ['AUTO', 'MANUAL']),
    choice('f_hold', 'Hold', 0, ONOFF),
    choice('f_arp_on', 'Arpeggio on', 0, ONOFF),
    choice('f_arp_mode', 'Arpeggio mode', 0, ['UP', 'U&D', 'DOWN']),
    choice('f_arp_range', 'Arpeggio range', 1, ['1', '2', '3']),
    int('f_arp_rate', 'Arpeggio rate', 0, 127),
    choice('f_oct_transpose', 'Octave transpose', 0, ['D', 'N', 'U']),
    int('f_key_transpose', 'Key transpose', -12, 12),
    int('g_tune', 'Master tune', 0, 127),
  );
  return p;
})();

export const NUM_HOST_PARAMS = 40;

/** Host value string for a native value (§4.8, R-P6). */
export function hostValueString(index: number, value: number): string {
  const p = HOST_PARAMS[index];
  if (p.isChoice) {
    const i = value - p.min;
    if (i >= 0 && i < p.choices.length) return p.choices[i];
    return p.choices.length ? p.choices[0] : '';
  }
  if (index === NUM_HOST_PARAMS - 1) return tuneV(value);
  if (index === NUM_HOST_PARAMS - 2) return value > 0 ? '+' + String(value) : String(value);
  return sliderV(value);
}

/** Key transpose as shown in the KEY TRANSPOSE tooltip: "+3", "0", "-12". */
export function keyTransposeV(value: number): string {
  return hostValueString(NUM_HOST_PARAMS - 2, value);
}
