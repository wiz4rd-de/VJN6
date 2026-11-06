// Small DSP building blocks: xorshift32, band-limited step table, half-band FIR, TPT one-poles.
#pragma once

#include <cstdint>
#include <vector>

#include "juno/constants.h"
#include "juno/dmath.h"

namespace vjn6 {

// R-E12 generator: each draw advances first, then uses x.
struct XorShift32 {
  uint32_t x = 1;
  explicit XorShift32(uint32_t seed = 1) : x(seed) {}
  uint32_t next() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }
  // u in [-1, 1) as R-E12: (x >> 8) / 2^23 - 1
  double nextBipolar() { return static_cast<double>(next() >> 8) / 8388608.0 - 1.0; }
};

// ------------------------------------------------------------------------------------------------
// Band-limited step (BLEP) residual table. Kernel: Kaiser-windowed sinc, length 8 samples,
// cutoff 0.45 of the oversampled rate, beta 6 (pass band -0.11 dB at 0.25 fo, >= 65 dB rejection
// above 0.7 fo; DECISIONS.md "Band-limiting"). A step at fractional time t_d = n - frac changes
// samples n-4 .. n+3; the caller delays its output by kBlepDelay samples.
struct BlepTable {
  static constexpr int kHalf = 4;
  static constexpr int kPhases = 256;
  static constexpr int kDelay = kHalf;
  // r[j + kHalf][p] = residual at tau = j + p / kPhases, j = -4..3, p = 0..kPhases
  double r[2 * kHalf][kPhases + 1];

  static const BlepTable& get();

  // Adds delta * residual to ring entries for samples n-4..n+3. ring has 16 entries, n is the
  // index of the first sample after the step, frac in [0,1) is the time since the step in samples.
  inline void add(double* ring, uint32_t n, double frac, double delta) const {
    double fp = frac * kPhases;
    int p = static_cast<int>(fp);
    if (p >= kPhases) p = kPhases - 1;
    const double t = fp - p;
    for (int j = -kHalf; j < kHalf; ++j) {
      const double* row = r[j + kHalf];
      const double v = row[p] + (row[p + 1] - row[p]) * t;
      ring[(n + static_cast<uint32_t>(j)) & 15u] += delta * v;
    }
  }
};

// ------------------------------------------------------------------------------------------------
// Half-band FIR (Kaiser), polyphase decimate-by-2 and interpolate-by-2.
class HalfBand {
 public:
  void design(int taps, double beta);
  int taps() const { return n_; }
  int centre() const { return c_; }
  // Decimator: push two input samples, returns one output.
  void resetState();
  inline double decimate(double x0, double x1) {
    push(x0);
    push(x1);
    // output aligned with the most recent input x1 at index 2n (newest at pos_ - 1)
    const double* b = &buf_[pos_];  // b[0] oldest ... b[n_-1] newest (buffer doubled)
    double acc = 0.5 * b[n_ - 1 - c_];
    for (std::size_t q = 0; q < odd_.size(); ++q) {
      const int j = oddIdx_[q];
      acc += odd_[q] * (b[n_ - 1 - c_ + j] + b[n_ - 1 - c_ - j]);
    }
    return acc;
  }
  // Interpolator: one input sample -> two outputs (gain 2 applied).
  inline void interpolate(double x, double& y0, double& y1) {
    pushI(x);
    const double* b = &ibuf_[ipos_];  // ilen_ samples, b[ilen_-1] newest
    // y[2n] = 2 * sum_{k even} h[k] x[n - k/2];  y[2n+1] = x[n - (c-1)/2]
    double acc = 0.0;
    for (std::size_t q = 0; q < even_.size(); ++q) acc += even_[q] * b[ilen_ - 1 - evenOff_[q]];
    y0 = 2.0 * acc;
    y1 = b[ilen_ - 1 - (c_ - 1) / 2];
  }

 private:
  inline void push(double x) {
    buf_[pos_] = x;
    buf_[pos_ + n_] = x;
    pos_ = pos_ + 1 == n_ ? 0 : pos_ + 1;
  }
  inline void pushI(double x) {
    ibuf_[ipos_] = x;
    ibuf_[ipos_ + ilen_] = x;
    ipos_ = ipos_ + 1 == ilen_ ? 0 : ipos_ + 1;
  }
  int n_ = 0, c_ = 0;
  std::vector<double> h_;
  std::vector<double> odd_;   // h[c + j] for odd j > 0
  std::vector<int> oddIdx_;   // j
  std::vector<double> buf_;   // doubled ring
  int pos_ = 0;
  std::vector<double> even_;  // h[k] for even k
  std::vector<int> evenOff_;  // k / 2
  std::vector<double> ibuf_;
  int ilen_ = 0, ipos_ = 0;
};

// ------------------------------------------------------------------------------------------------
// TPT one-pole (Zavalishin). g = tan(pi f / fs).
struct OnePole {
  double g = 0.0, G = 0.0, s = 0.0;
  void setCutoff(double hz, double fs) {
    double f = hz;
    const double fmax = k::kCornerClampRatio * fs;
    if (f > fmax) f = fmax;
    g = dm::tan(dm::kPi * f / fs);
    G = g / (1.0 + g);
  }
  // returns lowpass; high-pass = x - lp
  inline double lp(double x, double dn) {
    const double v = (x - s) * G;
    const double y = v + s;
    s = y + v + dn;
    return y;
  }
  inline double hp(double x, double dn) { return x - lp(x, dn); }
};

}  // namespace vjn6
