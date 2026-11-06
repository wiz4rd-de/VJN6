import { describe, expect, it } from 'vitest';
import { decimalPoint, glyph, GLYPHS, LED_H, LED_W, SEGMENTS, segmentPoints } from '../src/led/segments';

const sorted = (s: readonly string[]) => [...s].sort().join('');

describe('LED glyphs (§5.3)', () => {
  it('digits 0–9 use the standard segments', () => {
    const standard: Record<string, string> = {
      '0': 'abcdef',
      '1': 'bc',
      '2': 'abdeg',
      '3': 'abcdg',
      '4': 'bcfg',
      '5': 'acdfg',
      '6': 'acdefg',
      '7': 'abc',
      '8': 'abcdefg',
      '9': 'abcdfg',
    };
    for (const [ch, segs] of Object.entries(standard)) expect(sorted(glyph(ch))).toBe(segs);
  });

  it('letters and signs are exactly the §5.3 table', () => {
    expect(sorted(glyph('P'))).toBe('abefg');
    expect(sorted(glyph('r'))).toBe('eg');
    expect(sorted(glyph('E'))).toBe('adefg');
    expect(sorted(glyph('o'))).toBe('cdeg');
    expect(sorted(glyph('k'))).toBe('acefg');
    expect(sorted(glyph('c'))).toBe('deg');
    expect(sorted(glyph('t'))).toBe('defg');
    expect(sorted(glyph('-'))).toBe('g');
    expect(glyph(' ')).toEqual([]);
  });

  it('covers every LED character of the protocol and every §5.4 message', () => {
    expect(Object.keys(GLYPHS).sort()).toEqual(
      ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'P', 'r', 'E', 'o', 'k', 'c', 't', '-', ' '].sort(),
    );
    for (const m of ['Pr', 'Er', 'rc', 'ok', 'rE', 't-', '--']) for (const ch of m) expect(glyph(ch).length).toBeGreaterThan(0);
    expect(glyph('x')).toEqual([]);
  });

  it('geometry stays inside the 72 × 46 display, two decimal points', () => {
    for (const ox of [9, 39]) {
      const pts = segmentPoints(ox, 5);
      for (const s of SEGMENTS) {
        for (const pair of pts[s].split(' ')) {
          const [x, y] = pair.split(',').map(Number);
          expect(x).toBeGreaterThanOrEqual(0);
          expect(x).toBeLessThanOrEqual(LED_W);
          expect(y).toBeGreaterThanOrEqual(0);
          expect(y).toBeLessThanOrEqual(LED_H);
        }
      }
    }
    for (const i of [0, 1] as const) {
      const d = decimalPoint(i);
      expect(d.cx + d.r).toBeLessThanOrEqual(LED_W);
    }
  });
});
