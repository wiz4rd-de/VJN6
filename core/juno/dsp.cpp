#include "juno/dsp.h"

#include <cmath>

namespace vjn6 {

namespace {
// Kaiser-windowed sinc kernel, length 8 samples, cutoff 0.45 of the sample rate, beta 6.
double blepKernel(double t) {
  constexpr double kHalf = 4.0, fc = 0.45, beta = 6.0;
  if (t <= -kHalf || t >= kHalf) return 0.0;
  const double r = t / kHalf;
  const double w = dm::besselI0(beta * std::sqrt(1.0 - r * r)) / dm::besselI0(beta);
  const double x = 2.0 * fc * t;
  const double s = (x > -1e-12 && x < 1e-12) ? 1.0 : dm::sin(dm::kPi * x) / (dm::kPi * x);
  return 2.0 * fc * s * w;
}

struct BlepBuilder {
  BlepTable table;
  BlepBuilder() {
    constexpr int H = BlepTable::kHalf, P = BlepTable::kPhases;
    constexpr int sub = 16;  // Simpson sub-steps per table step
    const int total = 2 * H * P;
    // cumulative integral at each table point tau = -H + i / P
    static double cum[2 * BlepTable::kHalf * BlepTable::kPhases + 1];
    cum[0] = 0.0;
    const double h = 1.0 / (P * sub);
    for (int i = 0; i < total; ++i) {
      const double t0 = -H + static_cast<double>(i) / P;
      double acc = 0.0;
      for (int s = 0; s < sub; ++s) {
        const double a = t0 + s * h;
        acc += (blepKernel(a) + 4.0 * blepKernel(a + 0.5 * h) + blepKernel(a + h)) * (h / 6.0);
      }
      cum[i + 1] = cum[i] + acc;
    }
    const double norm = cum[total];
    for (int j = -H; j < H; ++j) {
      for (int p = 0; p <= P; ++p) {
        const int i = (j + H) * P + p;
        const double blep = cum[i] / norm;
        table.r[j + H][p] = j < 0 ? blep : blep - 1.0;
      }
    }
  }
};
}  // namespace

const BlepTable& BlepTable::get() {
  static const BlepBuilder builder;
  return builder.table;
}

void HalfBand::design(int taps, double beta) {
  n_ = taps;
  c_ = (taps - 1) / 2;
  h_.assign(static_cast<std::size_t>(taps), 0.0);
  const double i0b = dm::besselI0(beta);
  double oddSum = 0.0;
  for (int kk = 0; kk < taps; ++kk) {
    const int j = kk - c_;
    if (j == 0) {
      h_[kk] = 0.5;
    } else if (j % 2 == 0) {
      h_[kk] = 0.0;
    } else {
      const double r = static_cast<double>(j) / c_;
      const double w = dm::besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
      const double x = dm::kPi * j * 0.5;
      h_[kk] = 0.5 * (dm::sin(x) / x) * w;
      oddSum += h_[kk];
    }
  }
  for (int kk = 0; kk < taps; ++kk)
    if ((kk - c_) % 2 != 0) h_[kk] *= 0.5 / oddSum;
  odd_.clear();
  oddIdx_.clear();
  for (int j = 1; j <= c_; j += 2) {
    odd_.push_back(h_[c_ + j]);
    oddIdx_.push_back(j);
  }
  even_.clear();
  evenOff_.clear();
  for (int kk = 0; kk < taps; kk += 2) {
    even_.push_back(h_[kk]);
    evenOff_.push_back(kk / 2);
  }
  buf_.assign(static_cast<std::size_t>(2 * n_), 0.0);
  pos_ = 0;
  ilen_ = (n_ + 1) / 2;
  ibuf_.assign(static_cast<std::size_t>(2 * ilen_), 0.0);
  ipos_ = 0;
}

void HalfBand::resetState() {
  for (double& v : buf_) v = 0.0;
  for (double& v : ibuf_) v = 0.0;
  pos_ = 0;
  ipos_ = 0;
}

}  // namespace vjn6
