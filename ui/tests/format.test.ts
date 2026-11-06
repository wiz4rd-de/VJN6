import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import {
  fieldEngineValue,
  fieldV,
  hostValueString,
  HOST_PARAMS,
  parseTyped,
  perfEngineValue,
  perfV,
  fieldTooltip,
  perfTooltip,
  sig3,
  vcaLevel60V,
} from '../src/panel/format';
import { Field, Perf } from '../src/protocol';

interface FieldRow { mode: number; field: number; ctx: number; byte: number; v: string; engine: string }
interface KnobRow { mode: number; perf: number; byte: number; v: string; engine: string }
interface HostRow { index: number; id: string; value: number; text: string }

const table = JSON.parse(
  readFileSync(fileURLToPath(new URL('../../tests/golden/display_table.json', import.meta.url)), 'utf8'),
) as { fields: FieldRow[]; knobs: KnobRow[]; host: HostRow[]; host60: HostRow[] };

/** The edit-buffer context the generator used: INIT with the PWM source / polarity set to ctx. */
function ctxPatch(field: number, ctx: number): number[] {
  const p = new Array<number>(25).fill(0);
  p[Field.VCF_FREQ] = 127;
  p[Field.VCA_LEVEL] = 127;
  p[Field.ENV_S] = 127;
  p[Field.RANGE] = 1;
  p[Field.SAW] = 1;
  p[Field.SUB] = 1;
  if (field === Field.DCO_PWM) p[Field.PWM_SRC] = ctx;
  if (field === Field.VCF_ENV) p[Field.VCF_POL] = ctx;
  return p;
}

describe('AC-P5.1 TS display formatter reproduces the core table', () => {
  it('AC-P5.1 every patch field at every byte, both modes, every context', () => {
    expect(table.fields.length).toBeGreaterThan(4000);
    const bad: string[] = [];
    for (const r of table.fields) {
      const v = fieldV(r.field, r.byte, r.mode === 1);
      const e = fieldEngineValue(r.field, r.byte, r.mode === 1, ctxPatch(r.field, r.ctx));
      if (v !== r.v || e !== r.engine) bad.push(`${JSON.stringify(r)} -> v=${v} engine=${e}`);
    }
    expect(bad).toEqual([]);
  });

  it('AC-P5.1 every knob at every byte, both modes', () => {
    expect(table.knobs.length).toBeGreaterThan(1000);
    const bad: string[] = [];
    for (const r of table.knobs) {
      const v = perfV(r.perf, r.byte);
      const e = perfEngineValue(r.perf, r.byte, r.mode === 1);
      if (v !== r.v || e !== r.engine) bad.push(`${JSON.stringify(r)} -> v=${v} engine=${e}`);
    }
    expect(bad).toEqual([]);
  });

  // `host` holds the 106-mode strings, `host60` the 60-mode ones (they differ only in p_vca_level)
  for (const [mode, rows] of [
    ['106', () => table.host],
    ['60', () => table.host60],
  ] as const) {
    it(`AC-P5.1 every host value string in ${mode} mode, and the panel v equals the host string (R-P6)`, () => {
      const is106 = mode === '106';
      expect(rows().length).toBeGreaterThan(2000);
      const bad: string[] = [];
      for (const r of rows()) {
        expect(HOST_PARAMS[r.index].id).toBe(r.id);
        const t = hostValueString(r.index, r.value, is106);
        if (t !== r.text) bad.push(`${JSON.stringify(r)} -> ${t}`);
        // sliders and the tune knob: the panel shows exactly the host string as v
        if (r.index < 16 && fieldV(r.index, r.value, is106) !== r.text) bad.push(`panel v ${JSON.stringify(r)}`);
        if (r.index === 39 && perfV(Perf.TUNE, r.value) !== r.text) bad.push(`panel tune ${JSON.stringify(r)}`);
        if (r.index === 24 && fieldV(Field.HPF, r.value, is106) !== r.text) bad.push(`panel hpf ${JSON.stringify(r)}`);
      }
      expect(bad).toEqual([]);
    });
  }

  it('AC-P5.1 60 VCA LEVEL is centre-zero: bytes 0, 64, 127 show -5.0, 0.0, +5.0 with the dB engine value (C-4)', () => {
    const init = ctxPatch(-1, 0);
    const want60: [number, string, string][] = [
      [0, '-5.0', '-inf dB'],
      [64, '0.0', '-6.0 dB'],
      [127, '+5.0', '+0.0 dB'],
    ];
    for (const [b, v, db] of want60) {
      expect(fieldV(Field.VCA_LEVEL, b, false)).toBe(v);
      expect(hostValueString(Field.VCA_LEVEL, b, false)).toBe(v);
      expect(fieldTooltip('VCA LEVEL', Field.VCA_LEVEL, b, false, init)).toBe(`VCA LEVEL ${v} = ${db}`);
    }
    // the sign only on a non-zero rounded value
    expect([1, 38, 63, 65, 126].map(vcaLevel60V)).toEqual(['-4.9', '-2.0', '0.0', '+0.1', '+4.9']);
    // 106 mode is unchanged
    const want106: [number, string, string][] = [
      [0, '0.0', '-inf dB'],
      [64, '5.0', '-6.0 dB'],
      [127, '10.0', '+0.0 dB'],
    ];
    for (const [b, v, db] of want106) {
      expect(hostValueString(Field.VCA_LEVEL, b, true)).toBe(v);
      expect(fieldTooltip('VCA LEVEL', Field.VCA_LEVEL, b, true, init)).toBe(`VCA LEVEL ${v} = ${db}`);
    }
  });

  it('AC-P5.1 tooltips use the <legend> <v> = <engine value> form', () => {
    const init = ctxPatch(-1, 0);
    expect(fieldTooltip('VCF FREQ', Field.VCF_FREQ, 64, false, init)).toBe(
      `VCF FREQ 5.0 = ${fieldEngineValue(Field.VCF_FREQ, 64, false, init)}`,
    );
    expect(perfTooltip('TUNE', Perf.TUNE, 77, true)).toBe('TUNE +10.2 = +10.2 cents');
    expect(perfTooltip('VOLUME', Perf.VOLUME, 0, false)).toBe('VOLUME 0.0 = -inf dB');
    expect(fieldTooltip('HPF', Field.HPF, 0, true, init)).toBe('HPF 0 = bass boost');
  });
});

describe('formatter helpers', () => {
  it('sig3 keeps three significant digits', () => {
    expect(sig3(248.01)).toBe('248');
    expect(sig3(5.53)).toBe('5.53');
    expect(sig3(40712)).toBe('40700');
    expect(sig3(0.3)).toBe('0.300');
    expect(sig3(999.6)).toBe('1000');
  });

  it('typed entry rounds v x 12.7, clamps, and rejects non-numbers (R-U1)', () => {
    expect(parseTyped('byte', '5', 0, 127)).toBe(64);
    expect(parseTyped('byte', '12', 0, 127)).toBe(127);
    expect(parseTyped('byte', '-3', 0, 127)).toBe(0);
    expect(parseTyped('byte', 'abc', 0, 127)).toBeNull();
    expect(parseTyped('byte', '', 0, 127)).toBeNull();
    expect(parseTyped('tune', '10', 0, 127)).toBe(77);
    expect(parseTyped('tune', '-50', 0, 127)).toBe(0);
    expect(parseTyped('tune', '+60', 0, 127)).toBe(127);
    expect(parseTyped('hpf', '2', 0, 3)).toBe(2);
    expect(parseTyped('hpf', '7', 0, 3)).toBe(3);
  });

  it('AC-U1.1 60 VCA LEVEL typed entry commits round((v + 5) x 12.7), clamped to -5...+5 (C-4)', () => {
    expect(parseTyped('vca60', '-2', 0, 127)).toBe(38);
    expect(parseTyped('vca60', '+5', 0, 127)).toBe(127);
    expect(parseTyped('vca60', '9', 0, 127)).toBe(127);
    expect(parseTyped('vca60', '-9', 0, 127)).toBe(0);
    expect(parseTyped('vca60', '-5', 0, 127)).toBe(0);
    expect(parseTyped('vca60', '0', 0, 127)).toBe(64);
    expect(parseTyped('vca60', '+0.1', 0, 127)).toBe(65);
    expect(parseTyped('vca60', 'abc', 0, 127)).toBeNull();
    // every half step -5.0 ... +5.0 (the chart values) round-trips through byte -> display
    for (let k = -10; k <= 10; k++) {
      const v = k / 2;
      const shown = v === 0 ? '0.0' : `${v > 0 ? '+' : '-'}${Math.abs(v).toFixed(1)}`;
      expect(vcaLevel60V(parseTyped('vca60', String(v), 0, 127)!), `v = ${v}`).toBe(shown);
    }
  });
});
