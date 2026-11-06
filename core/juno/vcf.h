// R-E5 VCF: four cascaded OTA integrator stages with a saturating feedback OTA, solved as a
// zero-delay-feedback system (TPT integrators). Each stage nonlinearity V_sat*tanh(v/V_sat) is
// linearised with its secant gain from the previous sample; the feedback tanh is solved with
// Newton-Raphson (2 iterations at 2x, 1 at 4x; DECISIONS.md "Filter solver").
#pragma once

#include "juno/constants.h"
#include "juno/dmath.h"

namespace vjn6 {

struct Vcf {
  double s[4] = {0, 0, 0, 0};    // TPT integrator states
  double tg[4] = {1, 1, 1, 1};   // secant gains tanh(v)/v of the stage inputs (previous sample)
  double y4 = 0.0;               // last output
  double tfb = 1.0;              // secant gain of the feedback tanh (initial guess)

  void reset() {
    for (int i = 0; i < 4; ++i) {
      s[i] = 0.0;
      tg[i] = 1.0;
    }
    y4 = 0.0;
    tfb = 1.0;
  }

  static inline double secant(double v) {
    // tanh(v)/v, -> 1 for small v
    if (v < 1e-9 && v > -1e-9) return 1.0;
    return dm::tanh(v) / v;
  }

  // g = tan(pi * fc_eff / fo), kfb feedback gain, gin input gain, iters Newton iterations.
  inline double process(double x, double g, double kfb, double gin, int iters, double dn) {
    constexpr double Vs = k::kVsat;
    constexpr double Vf = k::kVfb;
    double a[4], b[4];
    for (int i = 0; i < 4; ++i) {
      const double G = g * tg[i];
      const double inv = 1.0 / (1.0 + G);
      a[i] = G * inv;
      b[i] = s[i] * inv;
    }
    const double A = a[0] * a[1] * a[2] * a[3];
    const double B = a[3] * (a[2] * (a[1] * b[0] + b[1]) + b[2]) + b[3];
    const double u = gin * x;
    // initial estimate with the previous feedback secant gain
    double y = (A * u + B) / (1.0 + A * kfb * tfb);
    for (int it = 0; it < iters; ++it) {
      const double t = dm::tanh(y / Vf);
      const double F = y - A * (u - kfb * Vf * t) - B;
      const double dF = 1.0 + A * kfb * (1.0 - t * t);
      y -= F / dF;
    }
    const double tf = dm::tanh(y / Vf);
    const double x0 = u - kfb * Vf * tf;
    // forward through the stages, update integrators and secant gains
    double in = x0;
    for (int i = 0; i < 4; ++i) {
      const double yi = a[i] * in + b[i];
      s[i] = 2.0 * yi - s[i] + dn;
      tg[i] = secant((in - yi) / Vs);
      in = yi;
    }
    y4 = in;
    const double vf = in / Vf;
    tfb = secant(vf);
    return in;
  }
};

}  // namespace vjn6
