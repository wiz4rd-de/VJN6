// Bit-exact TypeScript port of the deterministic math in core/juno/dmath.h, used by the display
// formatter so that every tooltip value equals the C++ string (R-P6, AC-P5.1). Only IEEE-754
// add, subtract, multiply, divide, floor and exact power-of-two scaling are used, in the same
// order as the C++ code (JavaScript never contracts a*b+c into a fused multiply-add).

export const LN2 = 0.69314718055994530942;
export const LOG2E = 1.44269504088896340736;
export const LOG2_10 = 3.32192809488736234787;

const view = new DataView(new ArrayBuffer(8));

/** Exactly 2^n for an integer n in [-1022, 1023]. */
function pow2(n: number): number {
  view.setUint32(0, ((n + 1023) & 0x7ff) << 20);
  view.setUint32(4, 0);
  return view.getFloat64(0);
}

/** std::ldexp(x, n): x * 2^n, exact unless the result leaves the normal range. */
export function ldexp(x: number, n: number): number {
  let r = x;
  let e = n;
  while (e > 1023) {
    r *= pow2(1023);
    e -= 1023;
  }
  while (e < -1022) {
    r *= pow2(-1022);
    e += 1022;
  }
  return r * pow2(e);
}

/** std::frexp(x): [m, e] with x = m * 2^e and |m| in [0.5, 1) (finite, non-zero x). */
export function frexp(x: number): [number, number] {
  if (x === 0 || !Number.isFinite(x)) return [x, 0];
  let v = x;
  let adjust = 0;
  view.setFloat64(0, v);
  let hi = view.getUint32(0);
  if (((hi >>> 20) & 0x7ff) === 0) {
    // subnormal: scale into the normal range first
    v *= pow2(54);
    adjust = -54;
    view.setFloat64(0, v);
    hi = view.getUint32(0);
  }
  const exp = (hi >>> 20) & 0x7ff;
  const e = exp - 1022 + adjust;
  view.setUint32(0, (hi & 0x800fffff) | (1022 << 20));
  return [view.getFloat64(0), e];
}

const F13 = 1.0 / 6227020800.0;
const F12 = 1.0 / 479001600.0;
const F11 = 1.0 / 39916800.0;
const F10 = 1.0 / 3628800.0;
const F9 = 1.0 / 362880.0;
const F8 = 1.0 / 40320.0;
const F7 = 1.0 / 5040.0;
const F6 = 1.0 / 720.0;
const F5 = 1.0 / 120.0;
const F4 = 1.0 / 24.0;
const F3 = 1.0 / 6.0;

/** 2^x (dm::exp2). */
export function exp2(x: number): number {
  if (x > 1023.0) return Infinity;
  if (x < -1074.0) return 0.0;
  const n = Math.floor(x + 0.5);
  const y = (x - n) * LN2;
  let p = F13;
  p = p * y + F12;
  p = p * y + F11;
  p = p * y + F10;
  p = p * y + F9;
  p = p * y + F8;
  p = p * y + F7;
  p = p * y + F6;
  p = p * y + F5;
  p = p * y + F4;
  p = p * y + F3;
  p = p * y + 0.5;
  p = p * y + 1.0;
  p = p * y + 1.0;
  return ldexp(p, n);
}

const SQRT_HALF = 0.70710678118654752440;
const R23 = 1.0 / 23.0;
const R21 = 1.0 / 21.0;
const R19 = 1.0 / 19.0;
const R17 = 1.0 / 17.0;
const R15 = 1.0 / 15.0;
const R13 = 1.0 / 13.0;
const R11 = 1.0 / 11.0;
const R9 = 1.0 / 9.0;
const R7 = 1.0 / 7.0;
const R5 = 1.0 / 5.0;
const R3 = 1.0 / 3.0;

/** log2(x) for x > 0 (dm::log2). */
export function log2(x: number): number {
  if (!(x > 0.0)) return -Infinity;
  let [m, e] = frexp(x);
  if (m < SQRT_HALF) {
    m *= 2.0;
    e -= 1;
  }
  const s = (m - 1.0) / (m + 1.0);
  const s2 = s * s;
  let p = R23;
  p = p * s2 + R21;
  p = p * s2 + R19;
  p = p * s2 + R17;
  p = p * s2 + R15;
  p = p * s2 + R13;
  p = p * s2 + R11;
  p = p * s2 + R9;
  p = p * s2 + R7;
  p = p * s2 + R5;
  p = p * s2 + R3;
  p = p * s2 + 1.0;
  const ln = 2.0 * s * p;
  return e + ln * LOG2E;
}

export function log10(x: number): number {
  return log2(x) / LOG2_10;
}

export function pow(a: number, b: number): number {
  if (a === 0.0) return b > 0.0 ? 0.0 : Infinity;
  return exp2(b * log2(a));
}
