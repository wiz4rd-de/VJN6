// Deterministic math for the signal path (SPEC §9.4 "Determinism").
//
// Every function here uses only IEEE-754 add, subtract, multiply, divide, sqrt and exact
// scaling (ldexp / frexp), evaluated in a fixed order. With -ffp-contract=off and no fast-math
// the results are bit-identical on every platform and under WASM. No platform tanh/exp/sin/log
// is used anywhere in the engine.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace vjn6::dm {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kLn2 = 0.69314718055994530942;
constexpr double kLog2e = 1.44269504088896340736;
constexpr double kLog2_10 = 3.32192809488736234787;

// 2^x. Range-reduced to f in [-0.5, 0.5], e^(f ln2) by a degree-13 Taylor polynomial (error < 1e-17).
inline double exp2(double x) {
  if (x > 1023.0) return HUGE_VAL;
  if (x < -1074.0) return 0.0;
  const double n = std::floor(x + 0.5);
  const double y = (x - n) * kLn2;
  double p = 1.0 / 6227020800.0;        // 1/13!
  p = p * y + 1.0 / 479001600.0;        // 1/12!
  p = p * y + 1.0 / 39916800.0;
  p = p * y + 1.0 / 3628800.0;
  p = p * y + 1.0 / 362880.0;
  p = p * y + 1.0 / 40320.0;
  p = p * y + 1.0 / 5040.0;
  p = p * y + 1.0 / 720.0;
  p = p * y + 1.0 / 120.0;
  p = p * y + 1.0 / 24.0;
  p = p * y + 1.0 / 6.0;
  p = p * y + 0.5;
  p = p * y + 1.0;
  p = p * y + 1.0;
  const int e = static_cast<int>(n);
  if (e < -1022 || e > 1023) return std::ldexp(p, e);
  // exact power of two from the exponent bits (same result as ldexp, without the library call)
  const uint64_t bits = static_cast<uint64_t>(e + 1023) << 52;
  double scale;
  std::memcpy(&scale, &bits, sizeof scale);
  return p * scale;
}

inline double exp(double x) { return exp2(x * kLog2e); }

// log2(x) for x > 0. frexp, then ln(m) = 2 atanh((m-1)/(m+1)) with m in [sqrt(.5), sqrt(2)).
inline double log2(double x) {
  if (!(x > 0.0)) return -HUGE_VAL;
  int e = 0;
  double m = std::frexp(x, &e);  // m in [0.5, 1)
  if (m < 0.70710678118654752440) {
    m *= 2.0;
    e -= 1;
  }
  const double s = (m - 1.0) / (m + 1.0);
  const double s2 = s * s;
  double p = 1.0 / 23.0;
  p = p * s2 + 1.0 / 21.0;
  p = p * s2 + 1.0 / 19.0;
  p = p * s2 + 1.0 / 17.0;
  p = p * s2 + 1.0 / 15.0;
  p = p * s2 + 1.0 / 13.0;
  p = p * s2 + 1.0 / 11.0;
  p = p * s2 + 1.0 / 9.0;
  p = p * s2 + 1.0 / 7.0;
  p = p * s2 + 1.0 / 5.0;
  p = p * s2 + 1.0 / 3.0;
  p = p * s2 + 1.0;
  const double ln = 2.0 * s * p;
  return static_cast<double>(e) + ln * kLog2e;
}

inline double log(double x) { return log2(x) * kLn2; }
inline double log10(double x) { return log2(x) / kLog2_10; }
inline double pow(double a, double b) {
  if (a == 0.0) return b > 0.0 ? 0.0 : HUGE_VAL;
  return exp2(b * log2(a));
}

namespace detail {
// sin and cos on [-pi/4, pi/4], Taylor to degree 17/16 (error < 1e-17).
inline double sinPoly(double x) {
  // sin x = x - x^3 * q,  q = 1/3! - x^2/5! + x^4/7! - ... - x^14/17!
  const double x2 = x * x;
  double q = 1.0 / 355687428096000.0;  // 1/17!
  q = -q * x2 + 1.0 / 1307674368000.0;
  q = q * x2 - 1.0 / 6227020800.0;
  q = q * x2 + 1.0 / 39916800.0;
  q = q * x2 - 1.0 / 362880.0;
  q = q * x2 + 1.0 / 5040.0;
  q = q * x2 - 1.0 / 120.0;
  q = q * x2 + 1.0 / 6.0;
  return x - x * x2 * q;
}
inline double cosPoly(double x) {
  const double x2 = x * x;
  double q = 1.0 / 20922789888000.0;  // 1/16!
  q = -q * x2 + 1.0 / 87178291200.0;  // 1/14!
  q = q * x2 - 1.0 / 479001600.0;
  q = q * x2 + 1.0 / 3628800.0;
  q = q * x2 - 1.0 / 40320.0;
  q = q * x2 + 1.0 / 720.0;
  q = q * x2 - 1.0 / 24.0;
  q = q * x2 + 0.5;
  return 1.0 - x2 * q;
}
}  // namespace detail

// sin and cos with octant reduction. Accurate to ~1e-16 for |x| < 1e6.
inline void sincos(double x, double& s, double& c) {
  const double k = std::floor(x * (2.0 / kPi) + 0.5);
  // two-part Cody-Waite reduction
  const double r = (x - k * 1.57079632679489655800) - k * 6.12323399573676603587e-17;
  const int q = static_cast<int>(static_cast<int64_t>(k) & 3);
  const double sp = detail::sinPoly(r);
  const double cp = detail::cosPoly(r);
  switch (q) {
    case 0: s = sp; c = cp; break;
    case 1: s = cp; c = -sp; break;
    case 2: s = -sp; c = -cp; break;
    default: s = -cp; c = sp; break;
  }
}
inline double sin(double x) { double s, c; sincos(x, s, c); return s; }
inline double cos(double x) { double s, c; sincos(x, s, c); return c; }
inline double tan(double x) { double s, c; sincos(x, s, c); return s / c; }

// Signal-path tanh: [9/8] Pade approximant, clamped at |x| >= 6.3. Max abs error 6.8e-6 (< 1e-4, §9.1).
// The same function is used natively and in WASM.
inline double tanh(double x) {
  if (x >= 6.3) return 1.0;
  if (x <= -6.3) return -1.0;
  const double x2 = x * x;
  const double num = x * (34459425.0 + x2 * (4729725.0 + x2 * (135135.0 + x2 * (990.0 + x2))));
  const double den = 34459425.0 + x2 * (16216200.0 + x2 * (945945.0 + x2 * (13860.0 + 45.0 * x2)));
  return num / den;
}

// tanh(x)/x from the same [9/8] Pade approximant (one division, no special case at 0).
inline double tanhOverX(double x) {
  if (x >= 6.3 || x <= -6.3) return 1.0 / (x < 0 ? -x : x);
  const double x2 = x * x;
  const double num = 34459425.0 + x2 * (4729725.0 + x2 * (135135.0 + x2 * (990.0 + x2)));
  const double den = 34459425.0 + x2 * (16216200.0 + x2 * (945945.0 + x2 * (13860.0 + 45.0 * x2)));
  return num / den;
}

// Modified Bessel function I0 (series), for Kaiser windows.
inline double besselI0(double x) {
  double sum = 1.0, term = 1.0;
  const double q = x * x * 0.25;
  for (int k = 1; k < 200; ++k) {
    term *= q / (static_cast<double>(k) * static_cast<double>(k));
    sum += term;
    if (term < sum * 1e-18) break;
  }
  return sum;
}

inline double dbToGain(double db) { return exp2(db * (kLog2_10 / 20.0)); }
inline double gainToDb(double g) { return 20.0 * log10(g); }

}  // namespace vjn6::dm
