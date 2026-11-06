// R-E9 chorus: two bucket-brigade lines (L, R) with one free-running chorus LFO.
//
// Model (Holters & Parker 2018 style): the 5th-order input and output filters are continuous-time
// systems in parallel form (one real and two complex one-pole sections each). The input filter is
// driven by the host-rate signal as an impulse train (impulse invariance, DC-normalised) and its
// continuous output is sampled at the BBD clock instants; the 128-tick bucket chain delays the
// samples; the output is a zero-order-hold staircase that drives the output filter exactly in
// continuous time and is sampled at the host rate. This reproduces the clock-dependent aliasing
// and the ZOH low-pass of the real device (DECISIONS.md "BBD model").
#pragma once

#include <cstdint>

#include "juno/constants.h"
#include "juno/dmath.h"
#include "juno/dsp.h"

namespace vjn6 {

struct Cx {
  double re = 0.0, im = 0.0;
};
inline Cx cmul(Cx a, Cx b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
inline Cx cadd(Cx a, Cx b) { return {a.re + b.re, a.im + b.im}; }
inline Cx csub(Cx a, Cx b) { return {a.re - b.re, a.im - b.im}; }
inline Cx cscale(Cx a, double s) { return {a.re * s, a.im * s}; }
inline Cx cdiv(Cx a, Cx b) {
  const double d = b.re * b.re + b.im * b.im;
  return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}
inline Cx cexp(Cx z) {
  const double m = dm::exp(z.re);
  double s, c;
  dm::sincos(z.im, s, c);
  return {m * c, m * s};
}

// Parallel form of a 5th-order all-pole low-pass (real pole + two biquads), unity DC gain.
// Section 0 is real (stored in .re), sections 1 and 2 are complex representatives of conjugate
// pairs; the output is x0 + 2 Re(x1) + 2 Re(x2).
struct ParallelLp5 {
  Cx p[3];   // poles (rad/s)
  Cx r[3];   // residues
  void design(double fReal, double fq1, double q1, double fq2, double q2);
};

class Chorus {
 public:
  void prepare(double fs);
  void reset();               // clears delay lines and filter states (switch-on)
  // mode: 0 off, 1 I, 2 II, 3 I+II. noiseLevel: 0 off, 1 half, 2 original.
  void setMode(int mode);
  void setNoise(int noiseLevel);
  // Processes one host sample. in = chorus input (dry sum); outputs the wet signals
  // (BBD gain, filters, high-pass and G included). When off, outputs 0.
  void process(double in, double& wetL, double& wetR, double dn);
  // Advances only the free-running chorus LFO (used while off).
  inline void advanceLfo() { lfoPhase_ += lfoInc_; if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0; }
  int mode() const { return mode_; }

  // ---- test access (SPEC §9.4) ----
  struct Line;
  Line& line(int c) { return lines_[c]; }
  double currentDelayMs(int c) const;
  // Injection / capture hooks (host rate). inject* replaces the signal at that point when set.
  const double* injBbdIn = nullptr;     // replaces the input-filter output (sampled linearly)
  double* capBbdIn = nullptr;           // input-filter output at host instants
  double* capBbdOut = nullptr;          // staircase level at host instants (after BBD gain)
  double* capWetL = nullptr;            // wet path L / R (after G)
  double* capWetR = nullptr;
  long hookPos = 0;

  struct Line {
    Cx xin[3];            // input-filter states at the last host instant (after the input impulse)
    Cx xout[3];           // output-filter states
    double fifo[128];
    int fifoPos = 0;
    double level = 0.0;   // current staircase value (BBD output, before gain)
    double clockPhase = 0.0;
    OnePole hp;           // 20 Hz wet high-pass
    XorShift32 rng{1};
    OnePole shelfLp;      // noise shelf
    double prevInjected = 0.0;
  };

 private:
  double delayMs(int c) const;
  void processLine(int c, double in, double noise, double dn, double& wet);

  double fs_ = 48000.0, T_ = 1.0 / 48000.0;
  int mode_ = 0;
  int noise_ = 2;
  double lfoPhase_ = 0.0, lfoInc_ = 0.0;
  ParallelLp5 inF_, outF_;
  Cx inE_[3], outE_[3];          // e^{pT}
  Cx inR_[3];                    // r_i * T * dcNorm (impulse weights)
  Cx outK_[3];                   // r_i / p_i
  double bbdGain_ = 1.0;
  double noiseScale_[3] = {0, 0, 0};
  Line lines_[2];
};

}  // namespace vjn6
