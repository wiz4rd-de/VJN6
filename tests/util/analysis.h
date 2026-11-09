// Signal analysis for the acceptance tests (own code, identical natively and under WASM).
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace vjn6::test {

constexpr double kPi = 3.14159265358979323846;

inline double rms(const float* x, std::size_t n) {
  double s = 0;
  for (std::size_t i = 0; i < n; ++i) s += double(x[i]) * x[i];
  return n ? std::sqrt(s / n) : 0.0;
}
inline double rms(const std::vector<double>& x) {
  double s = 0;
  for (double v : x) s += v * v;
  return x.empty() ? 0.0 : std::sqrt(s / x.size());
}
inline double rms(const std::vector<float>& x) { return rms(x.data(), x.size()); }
inline double db(double g) { return 20.0 * std::log10(g); }
inline double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

template <typename T>
inline double peakAbs(const std::vector<T>& x, std::size_t from = 0, std::size_t to = SIZE_MAX) {
  double p = 0;
  to = std::min(to, x.size());
  for (std::size_t i = from; i < to; ++i) p = std::max(p, std::fabs(double(x[i])));
  return p;
}

template <typename A, typename B>
inline double correlation(const std::vector<A>& a, const std::vector<B>& b) {
  const std::size_t n = std::min(a.size(), b.size());
  double ma = 0, mb = 0;
  for (std::size_t i = 0; i < n; ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= n;
  mb /= n;
  double sab = 0, saa = 0, sbb = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double x = a[i] - ma, y = b[i] - mb;
    sab += x * y;
    saa += x * x;
    sbb += y * y;
  }
  return sab / std::sqrt(saa * sbb);
}

// In-place radix-2 FFT.
inline void fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * kPi / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const auto u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

// 4-term Blackman-Harris window (-92 dB side lobes).
inline std::vector<double> blackmanHarris(std::size_t n) {
  std::vector<double> w(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double x = 2.0 * kPi * i / n;
    w[i] = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
  }
  return w;
}

// Magnitude spectrum (linear amplitude, window-normalised so a full-scale sine bin peak ~ amplitude).
template <typename T>
inline std::vector<double> magnitudeSpectrum(const std::vector<T>& x, std::size_t start, std::size_t n) {
  const auto w = blackmanHarris(n);
  double wsum = 0;
  for (double v : w) wsum += v;
  std::vector<std::complex<double>> a(n);
  for (std::size_t i = 0; i < n; ++i) a[i] = double(x[start + i]) * w[i];
  fft(a);
  std::vector<double> m(n / 2 + 1);
  for (std::size_t i = 0; i <= n / 2; ++i) m[i] = 2.0 * std::abs(a[i]) / wsum;
  return m;
}

// Welch power spectral density estimate (Hann, 50 % overlap), returns power per bin.
template <typename T>
inline std::vector<double> welch(const std::vector<T>& x, std::size_t seg) {
  std::vector<double> w(seg), psd(seg / 2 + 1, 0.0);
  for (std::size_t i = 0; i < seg; ++i) w[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / seg);
  std::size_t count = 0;
  std::vector<std::complex<double>> a(seg);
  for (std::size_t s = 0; s + seg <= x.size(); s += seg / 2) {
    for (std::size_t i = 0; i < seg; ++i) a[i] = double(x[s + i]) * w[i];
    fft(a);
    for (std::size_t i = 0; i <= seg / 2; ++i) psd[i] += std::norm(a[i]);
    ++count;
  }
  for (double& p : psd) p /= count;
  return psd;
}

// Transfer function |H| per bin from input/output Welch spectra (cross spectrum / input power).
template <typename A, typename B>
inline std::vector<double> transfer(const std::vector<A>& in, const std::vector<B>& out, std::size_t seg) {
  std::vector<double> w(seg);
  for (std::size_t i = 0; i < seg; ++i) w[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / seg);
  std::vector<std::complex<double>> sxy(seg / 2 + 1);
  std::vector<double> sxx(seg / 2 + 1, 0.0);
  std::vector<std::complex<double>> a(seg), b(seg);
  const std::size_t n = std::min(in.size(), out.size());
  for (std::size_t s = 0; s + seg <= n; s += seg / 2) {
    for (std::size_t i = 0; i < seg; ++i) {
      a[i] = double(in[s + i]) * w[i];
      b[i] = double(out[s + i]) * w[i];
    }
    fft(a);
    fft(b);
    for (std::size_t i = 0; i <= seg / 2; ++i) {
      sxy[i] += std::conj(a[i]) * b[i];
      sxx[i] += std::norm(a[i]);
    }
  }
  std::vector<double> h(seg / 2 + 1);
  for (std::size_t i = 0; i <= seg / 2; ++i) h[i] = sxx[i] > 0 ? std::abs(sxy[i]) / sxx[i] : 0.0;
  return h;
}

// Average |H| in a band [f0, f1] (power average of bins).
inline double bandGain(const std::vector<double>& h, double fs, std::size_t seg, double f0, double f1) {
  const double df = fs / seg;
  double s = 0;
  int n = 0;
  for (std::size_t i = 1; i < h.size(); ++i) {
    const double f = i * df;
    if (f >= f0 && f <= f1) {
      s += h[i] * h[i];
      ++n;
    }
  }
  return n ? std::sqrt(s / n) : 0.0;
}

// Rising zero crossings with linear interpolation (fractional sample positions).
template <typename T>
inline std::vector<double> risingCrossings(const std::vector<T>& x, double level = 0.0, std::size_t from = 0) {
  std::vector<double> c;
  for (std::size_t i = std::max<std::size_t>(from, 1); i < x.size(); ++i) {
    const double a = double(x[i - 1]) - level, b = double(x[i]) - level;
    if (a < 0 && b >= 0) c.push_back(double(i - 1) + a / (a - b));
  }
  return c;
}

// Frequency from rising crossings: (count-1) periods over their span.
template <typename T>
inline double frequency(const std::vector<T>& x, double fs, double level = 0.0, std::size_t from = 0) {
  const auto c = risingCrossings(x, level, from);
  if (c.size() < 2) return 0.0;
  return fs * (c.size() - 1) / (c.back() - c.front());
}

// Spectral peak frequency with parabolic interpolation on log magnitude.
inline double peakFrequency(const std::vector<double>& mag, double fs, std::size_t n, double fmin = 1.0) {
  std::size_t best = 1;
  for (std::size_t i = 1; i + 1 < mag.size(); ++i)
    if (i * fs / n >= fmin && mag[i] > mag[best]) best = i;
  const double a = std::log(mag[best - 1] + 1e-300), b = std::log(mag[best] + 1e-300), c = std::log(mag[best + 1] + 1e-300);
  const double d = 0.5 * (a - c) / (a - 2 * b + c);
  return (best + d) * fs / n;
}

// SHA-256 (for golden render hashes).
inline std::string sha256(const uint8_t* data, std::size_t len) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
      0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
      0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
      0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<uint8_t> m(data, data + len);
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>(bits >> (8 * i)));
  auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  for (std::size_t o = 0; o < m.size(); o += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(m[o + 4 * i]) << 24) | (uint32_t(m[o + 4 * i + 1]) << 16) | (uint32_t(m[o + 4 * i + 2]) << 8) |
             uint32_t(m[o + 4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = S0 + mj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (uint32_t v : h)
    for (int i = 7; i >= 0; --i) out.push_back(hex[(v >> (4 * i)) & 15]);
  return out;
}

}  // namespace vjn6::test
