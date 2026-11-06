#include "juno/chorus.h"

#include <cmath>

namespace vjn6 {

namespace {
Cx conj(Cx a) { return {a.re, -a.im}; }
// complex pole pair of s^2 + (w/Q) s + w^2, upper half plane
Cx bqPole(double hz, double q) {
  const double w = dm::kTwoPi * hz;
  const double re = -w / (2.0 * q);
  const double disc = 1.0 - 1.0 / (4.0 * q * q);
  return {re, w * std::sqrt(disc)};
}
}  // namespace

void ParallelLp5::design(double fReal, double fq1, double q1, double fq2, double q2) {
  const double w0 = dm::kTwoPi * fReal;
  Cx P[5];
  P[0] = {-w0, 0.0};
  P[1] = bqPole(fq1, q1);
  P[2] = conj(P[1]);
  P[3] = bqPole(fq2, q2);
  P[4] = conj(P[3]);
  const double w1 = dm::kTwoPi * fq1, w2 = dm::kTwoPi * fq2;
  const double K = w0 * w1 * w1 * w2 * w2;  // unity DC gain
  const int idx[3] = {0, 1, 3};
  for (int s = 0; s < 3; ++s) {
    const int i = idx[s];
    Cx den{1.0, 0.0};
    for (int j = 0; j < 5; ++j)
      if (j != i) den = cmul(den, csub(P[i], P[j]));
    p[s] = P[i];
    r[s] = cdiv(Cx{K, 0.0}, den);
  }
}

void Chorus::prepare(double fs) {
  fs_ = fs;
  T_ = 1.0 / fs;
  inF_.design(k::kInPoleHz, k::kInBq1Hz, k::kInBq1Q, k::kInBq2Hz, k::kInBq2Q);
  outF_.design(k::kOutPoleHz, k::kOutBq1Hz, k::kOutBq1Q, k::kOutBq2Hz, k::kOutBq2Q);
  // input: impulse invariance with DC normalisation
  double dc = 0.0;
  for (int s = 0; s < 3; ++s) {
    inE_[s] = cexp(cscale(inF_.p[s], T_));
    const Cx one_minus_e = csub(Cx{1.0, 0.0}, inE_[s]);
    const Cx q = cdiv(inF_.r[s], one_minus_e);
    dc += (s == 0 ? 1.0 : 2.0) * q.re;
  }
  dc *= T_;
  for (int s = 0; s < 3; ++s) inR_[s] = cscale(inF_.r[s], T_ / dc);
  // output: exact continuous-time response to a staircase
  for (int s = 0; s < 3; ++s) {
    outE_[s] = cexp(cscale(outF_.p[s], T_));
    outK_[s] = cdiv(outF_.r[s], outF_.p[s]);
  }
  bbdGain_ = dm::dbToGain(k::kBbdGainDb);

  // Noise scale: power gain of shelf -> output filter (held input) -> 20 Hz HP, times G and the
  // output gain, so that the engine output noise is -57 / -63 dBFS RMS (R-E9).
  {
    OnePole shelf, hp;
    shelf.setCutoff(k::kNoiseShelfHz, fs);
    hp.setCutoff(k::kWetHpHz, fs);
    const double A = dm::dbToGain(k::kNoiseShelfDb);
    Cx x[3];
    double power = 0.0;
    const int n = static_cast<int>(fs * 0.5);
    for (int i = 0; i < n; ++i) {
      const double in = i == 0 ? 1.0 : 0.0;
      const double lp = shelf.lp(in, 0.0);
      const double v = lp + A * (in - lp);
      double y = 0.0;
      for (int s = 0; s < 3; ++s) {
        x[s] = cadd(cmul(outE_[s], x[s]), cmul(outK_[s], cscale(csub(outE_[s], Cx{1.0, 0.0}), v)));
        y += (s == 0 ? 1.0 : 2.0) * x[s].re;
      }
      const double o = hp.hp(y, 0.0);
      power += o * o;
    }
    const double chain = k::kWetGain * k::kOutputGain * std::sqrt(power);
    noiseScale_[0] = 0.0;
    noiseScale_[1] = dm::dbToGain(k::kChorusNoiseHalfDbfs) / chain;
    noiseScale_[2] = dm::dbToGain(k::kChorusNoiseOrigDbfs) / chain;
  }
  lfoPhase_ = 0.0;
  lines_[0].rng = XorShift32(k::kSeedChorusL);
  lines_[1].rng = XorShift32(k::kSeedChorusR);
  for (auto& L : lines_) {
    L.hp.setCutoff(k::kWetHpHz, fs);
    L.shelfLp.setCutoff(k::kNoiseShelfHz, fs);
  }
  reset();
  setMode(mode_);
}

void Chorus::reset() {
  for (auto& L : lines_) {
    for (int s = 0; s < 3; ++s) {
      L.xin[s] = Cx{};
      L.xout[s] = Cx{};
    }
    for (double& v : L.fifo) v = 0.0;
    L.fifoPos = 0;
    L.level = 0.0;
    L.clockPhase = 0.0;
    L.hp.s = 0.0;
    L.shelfLp.s = 0.0;
    L.prevInjected = 0.0;
  }
}

void Chorus::setMode(int mode) {
  if (mode != 0 && mode_ == 0) reset();  // turning the chorus on clears both delay lines
  mode_ = mode;
  const double rate = mode == 2 ? k::kChorusRateIIHz : mode == 3 ? k::kChorusRateI2Hz : k::kChorusRateIHz;
  lfoInc_ = rate * T_;
}

void Chorus::setNoise(int noiseLevel) { noise_ = noiseLevel; }

double Chorus::delayMs(int c) const {
  if (mode_ == 3) {
    return k::kChorusCentreI2Ms + k::kChorusSwingI2Ms * dm::sin(dm::kTwoPi * lfoPhase_);
  }
  const double tri = 1.0 - 4.0 * std::fabs(lfoPhase_ - 0.5);
  return k::kChorusCentreMs + (c == 0 ? 1.0 : -1.0) * k::kChorusSwingMs * tri;
}

double Chorus::currentDelayMs(int c) const { return delayMs(c); }

void Chorus::processLine(int c, double in, double noise, double dn, double& wet) {
  Line& L = lines_[c];
  const double d = delayMs(c) * 1e-3;
  const double D = (static_cast<double>(k::kBbdStages) * 0.5 / d) * T_;  // clock periods per sample
  const double a = L.clockPhase;
  const double b = a + D;
  const double vstart = L.level * bbdGain_ + noise;
  Cx xo[3];
  for (int s = 0; s < 3; ++s) {
    xo[s] = cadd(cmul(outE_[s], L.xout[s]), cmul(outK_[s], cscale(csub(outE_[s], Cx{1.0, 0.0}), vstart)));
  }
  const double injCur = injBbdIn ? injBbdIn[hookPos] : 0.0;
  for (double m = std::floor(a) + 1.0; m <= b; m += 1.0) {
    const double tau = (m - a) / D;  // (0, 1]
    double sampled;
    if (injBbdIn) {
      sampled = L.prevInjected + (injCur - L.prevInjected) * tau;
    } else {
      sampled = 0.0;
      for (int s = 0; s < 3; ++s) {
        const Cx e = cexp(cscale(inF_.p[s], tau * T_));
        const Cx v = cmul(e, L.xin[s]);
        sampled += (s == 0 ? 1.0 : 2.0) * v.re;
      }
    }
    const double out = L.fifo[L.fifoPos];
    L.fifo[L.fifoPos] = sampled;
    L.fifoPos = (L.fifoPos + 1) & 127;
    const double dv = (out - L.level) * bbdGain_;
    L.level = out;
    if (dv != 0.0) {
      for (int s = 0; s < 3; ++s) {
        const Cx e = cexp(cscale(outF_.p[s], (1.0 - tau) * T_));
        xo[s] = cadd(xo[s], cmul(outK_[s], cscale(csub(e, Cx{1.0, 0.0}), dv)));
      }
    }
  }
  L.clockPhase = b - std::floor(b);
  double y = 0.0;
  for (int s = 0; s < 3; ++s) {
    xo[s].re += dn;
    L.xout[s] = xo[s];
    y += (s == 0 ? 1.0 : 2.0) * xo[s].re;
  }
  // advance the input filter to this host instant and add the input impulse
  double yin = 0.0;
  for (int s = 0; s < 3; ++s) {
    Cx v = cadd(cmul(inE_[s], L.xin[s]), cscale(inR_[s], in));
    v.re += dn;
    L.xin[s] = v;
    yin += (s == 0 ? 1.0 : 2.0) * v.re;
  }
  if (injBbdIn) L.prevInjected = injCur;
  if (capBbdIn) capBbdIn[hookPos * 2 + c] = injBbdIn ? injCur : yin;
  if (capBbdOut) capBbdOut[hookPos * 2 + c] = L.level * bbdGain_;
  wet = k::kWetGain * L.hp.hp(y, dn);
}

void Chorus::process(double in, double& wetL, double& wetR, double dn) {
  if (mode_ == 0) {
    wetL = wetR = 0.0;
    advanceLfo();
    return;
  }
  double nL = 0.0, nR = 0.0;
  if (noise_ > 0) {
    const double scale = noiseScale_[noise_];
    const double A = dm::dbToGain(k::kNoiseShelfDb);
    for (int c = 0; c < 2; ++c) {
      Line& L = lines_[c];
      const double w = L.rng.nextBipolar() + L.rng.nextBipolar() + L.rng.nextBipolar();
      const double lp = L.shelfLp.lp(w, dn);
      const double v = (lp + A * (w - lp)) * scale;
      if (c == 0) nL = v; else nR = v;
    }
  }
  processLine(0, in, nL, dn, wetL);
  processLine(1, in, nR, dn, wetR);
  if (capWetL) capWetL[hookPos] = wetL;
  if (capWetR) capWetR[hookPos] = wetR;
  advanceLfo();
}

}  // namespace vjn6
