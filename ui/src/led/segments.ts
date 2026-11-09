// The two-digit 7-segment LED (§5.3): glyph table and vector geometry. Segments: a top, b upper
// right, c lower right, d bottom, e lower left, f upper left, g middle.

export type Segment = 'a' | 'b' | 'c' | 'd' | 'e' | 'f' | 'g';
export const SEGMENTS: readonly Segment[] = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];

/** Every character the core can put on the LED (docs/BRIDGE.md), mapped to its lit segments. */
export const GLYPHS: Readonly<Record<string, readonly Segment[]>> = {
  '0': ['a', 'b', 'c', 'd', 'e', 'f'],
  '1': ['b', 'c'],
  '2': ['a', 'b', 'd', 'e', 'g'],
  '3': ['a', 'b', 'c', 'd', 'g'],
  '4': ['b', 'c', 'f', 'g'],
  '5': ['a', 'c', 'd', 'f', 'g'],
  '6': ['a', 'c', 'd', 'e', 'f', 'g'],
  '7': ['a', 'b', 'c'],
  '8': ['a', 'b', 'c', 'd', 'e', 'f', 'g'],
  '9': ['a', 'b', 'c', 'd', 'f', 'g'],
  P: ['a', 'b', 'e', 'f', 'g'],
  r: ['e', 'g'],
  E: ['a', 'd', 'e', 'f', 'g'],
  o: ['c', 'd', 'e', 'g'],
  k: ['a', 'c', 'e', 'f', 'g'],
  c: ['d', 'e', 'g'],
  t: ['d', 'e', 'f', 'g'],
  '-': ['g'],
  ' ': [],
};

/** Lit segments of one character; unknown characters are blank. */
export function glyph(ch: string): readonly Segment[] {
  return GLYPHS[ch] ?? [];
}

/** LED display size (§5.3) and the digit cell layout inside it. */
export const LED_W = 72;
export const LED_H = 46;
export const DIGIT_W = 24;
export const DIGIT_H = 36;
/** Left edges of the two digit cells and their common top edge, in LED coordinates. */
export const DIGIT_X: readonly [number, number] = [9, 39];
export const DIGIT_Y = 5;
/** Horizontal shear of the digits (italic slant), as x offset per unit of height above the base. */
const SLANT = 0.08;
const T = 4; // segment thickness

type Pt = [number, number];

function hSeg(x0: number, x1: number, y: number): Pt[] {
  const h = T / 2;
  return [
    [x0, y],
    [x0 + h, y - h],
    [x1 - h, y - h],
    [x1, y],
    [x1 - h, y + h],
    [x0 + h, y + h],
  ];
}

function vSeg(x: number, y0: number, y1: number): Pt[] {
  const h = T / 2;
  return [
    [x, y0],
    [x + h, y0 + h],
    [x + h, y1 - h],
    [x, y1],
    [x - h, y1 - h],
    [x - h, y0 + h],
  ];
}

/** Polygon points ("x,y x,y …") of every segment of a digit whose cell starts at (ox, oy). */
export function segmentPoints(ox: number, oy: number): Record<Segment, string> {
  const w = DIGIT_W;
  const h = DIGIT_H;
  const g = 0.6; // gap between neighbouring segments
  const left = T / 2;
  const right = w - T / 2;
  const top = T / 2;
  const mid = h / 2;
  const bottom = h - T / 2;
  const raw: Record<Segment, Pt[]> = {
    a: hSeg(left + g, right - g, top),
    b: vSeg(right, top + g, mid - g),
    c: vSeg(right, mid + g, bottom - g),
    d: hSeg(left + g, right - g, bottom),
    e: vSeg(left, mid + g, bottom - g),
    f: vSeg(left, top + g, mid - g),
    g: hSeg(left + g, right - g, mid),
  };
  const out = {} as Record<Segment, string>;
  for (const s of SEGMENTS) {
    out[s] = raw[s]
      .map(([x, y]) => `${(ox + x + (h - y) * SLANT).toFixed(2)},${(oy + y).toFixed(2)}`)
      .join(' ');
  }
  return out;
}

/** Centre of the decimal point after digit `i` (0 or 1), in LED coordinates. */
export function decimalPoint(i: 0 | 1): { cx: number; cy: number; r: number } {
  return { cx: DIGIT_X[i] + DIGIT_W + 3, cy: DIGIT_Y + DIGIT_H - 1.5, r: 2.2 };
}
