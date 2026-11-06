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
  double tg[4] = {1, 1, 1, 1};   // secant gains tanh(v)/v used for the current sample
  double v1[4] = {0, 0, 0, 0};   // stage input / V_sat at n-1
  double v2[4] = {0, 0, 0, 0};   // stage input / V_sat at n-2
  double y4 = 0.0;               // last output
  double tfb = 1.0;              // secant gain of the feedback tanh (initial guess)

  void reset() {
    for (int i = 0; i < 4; ++i) {
      s[i] = 0.0;
      tg[i] = 1.0;
      v1[i] = v2[i] = 0.0;
    }
    y4 = 0.0;
    tfb = 1.0;
  }

  static inline double secant(double v) { return dm::tanhOverX(v); }  // tanh(v)/v

  // g = tan(pi * fc_eff / fo), kfb feedback gain, gin input gain, iters Newton iterations.
  inline double process(double x, double g, double kfb, double gin, int iters, double dn) {
    constexpr double Vf = k::kVfb;
    constexpr double invVs = 1.0 / k::kVsat;
    constexpr double invVf = 1.0 / k::kVfb;
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
      const double t = dm::tanh(y * invVf);
      const double F = y - A * (u - kfb * Vf * t) - B;
      const double dF = 1.0 + A * kfb * (1.0 - t * t);
      y -= F / dF;
    }
    const double tf = dm::tanh(y * invVf);
    const double x0 = u - kfb * Vf * tf;
    // forward through the stages, update integrators and secant gains
    double in = x0;
    for (int i = 0; i < 4; ++i) {
      const double yi = a[i] * in + b[i];
      s[i] = 2.0 * yi - s[i] + dn;
      const double v = (in - yi) * invVs;
      // secant gain for the next sample at the linearly predicted stage input (second-order lag)
      tg[i] = secant(2.0 * v - v1[i]);
      v2[i] = v1[i];
      v1[i] = v;
      in = yi;
    }
    y4 = in;
    tfb = secant(in * invVf);
    return in;
  }
};

}  // namespace vjn6
