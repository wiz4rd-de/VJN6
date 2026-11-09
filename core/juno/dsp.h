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
// Band-limited step (BLEP) residual table. The kernel is a Kaiser-windowed sinc defined in HOST
// samples, so the DCO is band-limited to about the host Nyquist frequency whatever the oversampling:
// the oversampling then stays free as headroom for the VCF's saturating stages, whose products of
// in-band content cannot fold back below fs/2 (DECISIONS.md "Band-limiting"). A step at fractional
// time t_d = n - frac changes oversampled samples n-half .. n+half-1; the DCO output is delayed by
// half samples.
class BlepTable {
 public:
  static constexpr int kRing = 512;  // per-voice ring size (power of two, > 2 * half)
  static constexpr uint32_t kMask = kRing - 1;
  void build(double hostHalfLen, double cutoffHost, double beta, int os, int phases);
  int delay() const { return half_; }
  // Adds delta * residual to ring entries for samples n-half..n+half-1.
  inline void add(double* ring, uint32_t n, double frac, double delta) const {
    double fp = frac * phases_;
    int p = static_cast<int>(fp);
    if (p >= phases_) p = phases_ - 1;
    const double t = fp - p;
    const double* row = r_.data() + p;
    const int stride = phases_ + 1;
    uint32_t idx = n - static_cast<uint32_t>(half_);
    for (int j = 0; j < 2 * half_; ++j, row += stride, ++idx) {
      const double v = row[0] + (row[1] - row[0]) * t;
      ring[idx & kMask] += delta * v;
    }
  }

 private:
  int half_ = 0, phases_ = 0;
  std::vector<double> r_;  // [(j + half) * (phases + 1) + p] = residual at tau = j + p / phases
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
// Final decimate-by-2 stage: symmetric Kaiser lowpass FIR with its stop band starting at the output
// Nyquist frequency (a strict half-band filter is only -6 dB there and would fold the DCO's
// harmonics just above fs/2 back into the band; DECISIONS.md "Decimator").
class LowpassDecimator {
 public:
  void design(int taps, double cutoff, double beta);  // cutoff relative to the input rate
  int centre() const { return c_; }
  void resetState();
  inline double decimate(double x0, double x1) {
    push(x0);
    push(x1);
    const double* b = &buf_[pos_];  // b[0] oldest .. b[n_-1] newest
    double acc = h_[static_cast<std::size_t>(c_)] * b[c_];
    for (int k = 0; k < c_; ++k) acc += h_[static_cast<std::size_t>(k)] * (b[k] + b[n_ - 1 - k]);
    return acc;
  }

 private:
  inline void push(double x) {
    buf_[pos_] = x;
    buf_[pos_ + n_] = x;
    pos_ = pos_ + 1 == n_ ? 0 : pos_ + 1;
  }
  int n_ = 0, c_ = 0, pos_ = 0;
  std::vector<double> h_, buf_;
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
