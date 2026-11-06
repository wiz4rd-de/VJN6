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

// Six VCFs advanced together: the same arithmetic as Vcf::process for each filter, interleaved step by
// step across the voices so that their independent division chains overlap (bit-identical results).
inline void processSix(Vcf* const* f, const double* x, const double* g, double kfb, double gin, double dn,
                       double* out) {
  constexpr int N = 6;
  constexpr double Vf = k::kVfb;
  constexpr double invVs = 1.0 / k::kVsat;
  constexpr double invVf = 1.0 / k::kVfb;
  double a[N][4], b[N][4], A[N], B[N], u[N], y[N];
  for (int i = 0; i < 4; ++i) {
    for (int v = 0; v < N; ++v) {
      const double G = g[v] * f[v]->tg[i];
      const double inv = 1.0 / (1.0 + G);
      a[v][i] = G * inv;
      b[v][i] = f[v]->s[i] * inv;
    }
  }
  for (int v = 0; v < N; ++v) {
    A[v] = a[v][0] * a[v][1] * a[v][2] * a[v][3];
    B[v] = a[v][3] * (a[v][2] * (a[v][1] * b[v][0] + b[v][1]) + b[v][2]) + b[v][3];
    u[v] = gin * x[v];
    y[v] = (A[v] * u[v] + B[v]) / (1.0 + A[v] * kfb * f[v]->tfb);
  }
  for (int v = 0; v < N; ++v) {  // one Newton-Raphson step
    const double t = dm::tanh(y[v] * invVf);
    const double F = y[v] - A[v] * (u[v] - kfb * Vf * t) - B[v];
    const double dF = 1.0 + A[v] * kfb * (1.0 - t * t);
    y[v] -= F / dF;
  }
  double in[N];
  for (int v = 0; v < N; ++v) in[v] = u[v] - kfb * Vf * dm::tanh(y[v] * invVf);
  for (int i = 0; i < 4; ++i) {
    for (int v = 0; v < N; ++v) {
      Vcf& fv = *f[v];
      const double yi = a[v][i] * in[v] + b[v][i];
      fv.s[i] = 2.0 * yi - fv.s[i] + dn;
      const double vv = (in[v] - yi) * invVs;
      fv.tg[i] = Vcf::secant(2.0 * vv - fv.v1[i]);
      fv.v2[i] = fv.v1[i];
      fv.v1[i] = vv;
      in[v] = yi;
    }
  }
  for (int v = 0; v < N; ++v) {
    f[v]->y4 = in[v];
    f[v]->tfb = Vcf::secant(in[v] * invVf);
    out[v] = in[v];
  }
}

}  // namespace vjn6
