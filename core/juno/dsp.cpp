#include "juno/dsp.h"

#include <cmath>

namespace vjn6 {

void BlepTable::build(double hostHalfLen, double cutoffHost, double beta, int os, int phases) {
  half_ = static_cast<int>(std::ceil(hostHalfLen * os));
  phases_ = phases;
  const double L = hostHalfLen;  // kernel support [-L, L] host samples
  const double i0b = dm::besselI0(beta);
  auto kernel = [&](double tos) {  // per oversampled sample
    const double th = tos / os;
    if (th <= -L || th >= L) return 0.0;
    const double r = th / L;
    const double w = dm::besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
    const double x = 2.0 * cutoffHost * th;
    const double s = (x > -1e-12 && x < 1e-12) ? 1.0 : dm::sin(dm::kPi * x) / (dm::kPi * x);
    return s * w;
  };
  const int H = half_, P = phases_;
  const int total = 2 * H * P;
  std::vector<double> cum(static_cast<std::size_t>(total) + 1, 0.0);
  constexpr int sub = 8;
  const double h = 1.0 / (static_cast<double>(P) * sub);
  for (int i = 0; i < total; ++i) {
    const double t0 = -H + static_cast<double>(i) / P;
    double acc = 0.0;
    for (int k = 0; k < sub; ++k) {
      const double a = t0 + k * h;
      acc += (kernel(a) + 4.0 * kernel(a + 0.5 * h) + kernel(a + h)) * (h / 6.0);
    }
    cum[static_cast<std::size_t>(i) + 1] = cum[static_cast<std::size_t>(i)] + acc;
  }
  const double norm = cum[static_cast<std::size_t>(total)];
  r_.assign(static_cast<std::size_t>(2 * H * (P + 1)), 0.0);
  for (int j = -H; j < H; ++j) {
    for (int p = 0; p <= P; ++p) {
      const int i = (j + H) * P + p;
      const double blep = cum[static_cast<std::size_t>(i)] / norm;
      r_[static_cast<std::size_t>((j + H) * (P + 1) + p)] = j < 0 ? blep : blep - 1.0;
    }
  }
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

void LowpassDecimator::design(int taps, double cutoff, double beta) {
  n_ = taps;
  c_ = (taps - 1) / 2;
  h_.assign(static_cast<std::size_t>(taps), 0.0);
  const double i0b = dm::besselI0(beta);
  double sum = 0.0;
  for (int kk = 0; kk < taps; ++kk) {
    const int j = kk - c_;
    const double r = static_cast<double>(j) / c_;
    const double w = dm::besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
    const double s = j == 0 ? 2.0 * cutoff : dm::sin(2.0 * dm::kPi * cutoff * j) / (dm::kPi * j);
    h_[static_cast<std::size_t>(kk)] = s * w;
    sum += h_[static_cast<std::size_t>(kk)];
  }
  for (double& v : h_) v /= sum;
  buf_.assign(static_cast<std::size_t>(2 * n_), 0.0);
  pos_ = 0;
}

void LowpassDecimator::resetState() {
  for (double& v : buf_) v = 0.0;
  pos_ = 0;
}

void HalfBand::resetState() {
  for (double& v : buf_) v = 0.0;
  for (double& v : ibuf_) v = 0.0;
  pos_ = 0;
  ipos_ = 0;
}

}  // namespace vjn6
