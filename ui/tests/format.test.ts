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
} from '../src/panel/format';
import { Field, Perf } from '../src/protocol';

interface FieldRow { mode: number; field: number; ctx: number; byte: number; v: string; engine: string }
interface KnobRow { mode: number; perf: number; byte: number; v: string; engine: string }
interface HostRow { index: number; id: string; value: number; text: string }

const table = JSON.parse(
  readFileSync(fileURLToPath(new URL('../../tests/golden/display_table.json', import.meta.url)), 'utf8'),
) as { fields: FieldRow[]; knobs: KnobRow[]; host: HostRow[] };

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
      const v = fieldV(r.field, r.byte);
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

  it('AC-P5.1 every host value string, and the panel v equals the host string (R-P6)', () => {
    expect(table.host.length).toBeGreaterThan(2000);
    const bad: string[] = [];
    for (const r of table.host) {
      expect(HOST_PARAMS[r.index].id).toBe(r.id);
      const t = hostValueString(r.index, r.value);
      if (t !== r.text) bad.push(`${JSON.stringify(r)} -> ${t}`);
      // sliders and the tune knob: the panel shows exactly the host string as v
      if (r.index < 16 && fieldV(r.index, r.value) !== r.text) bad.push(`panel v ${JSON.stringify(r)}`);
      if (r.index === 39 && perfV(Perf.TUNE, r.value) !== r.text) bad.push(`panel tune ${JSON.stringify(r)}`);
      if (r.index === 24 && fieldV(Field.HPF, r.value) !== r.text) bad.push(`panel hpf ${JSON.stringify(r)}`);
    }
    expect(bad).toEqual([]);
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
});
