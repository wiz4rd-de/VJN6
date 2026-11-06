// Helpers for the engine acceptance tests.
#pragma once

#include <cmath>
#include <vector>

#include "util/analysis.h"
#include "util/harness.h"

namespace vjn6::test {

// Records the oversampled signal at one engine hook for n host samples of voice v.
// which: pointer-to-member of EngineHooks (capVcfIn, capVcfOut, ...).
inline std::vector<double> captureOs(Rig& r, double* EngineHooks::*which, long nHost, int voice = 0) {
  const int os = r.eng().oversample();
  std::vector<double> buf(static_cast<std::size_t>(nHost * os + 16), 0.0);
  EngineHooks& H = r.eng().hooks;
  H.voice = voice;
  H.osPos = 0;
  H.*which = buf.data();
  r.skip(nHost);
  H.*which = nullptr;
  buf.resize(static_cast<std::size_t>(nHost * os));
  return buf;
}

// Two oversampled captures at once (same render).
inline void captureOs2(Rig& r, double* EngineHooks::*a, std::vector<double>& outA, double* EngineHooks::*b,
                       std::vector<double>& outB, long nHost, int voice = 0) {
  const int os = r.eng().oversample();
  outA.assign(static_cast<std::size_t>(nHost * os + 16), 0.0);
  outB.assign(static_cast<std::size_t>(nHost * os + 16), 0.0);
  EngineHooks& H = r.eng().hooks;
  H.voice = voice;
  H.osPos = 0;
  H.*a = outA.data();
  H.*b = outB.data();
  r.skip(nHost);
  H.*a = nullptr;
  H.*b = nullptr;
  outA.resize(static_cast<std::size_t>(nHost * os));
  outB.resize(static_cast<std::size_t>(nHost * os));
}

// Reset (wrap) positions from a captured phase: fractional sample index of each reset.
inline std::vector<double> resetTimes(const std::vector<double>& phase, const std::vector<double>& inc) {
  std::vector<double> t;
  for (std::size_t i = 1; i < phase.size(); ++i)
    if (phase[i] < phase[i - 1]) t.push_back(static_cast<double>(i) - phase[i] / inc[i]);
  return t;
}

// Peak of |x| excluding +-frac*period around each edge position.
template <typename T>
inline double peakExcludingEdges(const std::vector<T>& x, const std::vector<double>& edges, double period,
                                 double frac, std::size_t from, std::size_t to) {
  const double w = frac * period;
  double pk = 0.0;
  std::size_t e = 0;
  for (std::size_t i = from; i < to && i < x.size(); ++i) {
    while (e < edges.size() && edges[e] + w < static_cast<double>(i)) ++e;
    bool near = false;
    for (std::size_t k = (e > 0 ? e - 1 : 0); k < edges.size() && k <= e + 1; ++k)
      if (std::fabs(edges[k] - static_cast<double>(i)) <= w) near = true;
    if (!near) pk = std::max(pk, std::fabs(static_cast<double>(x[i])));
  }
  return pk;
}

// Edge positions in a signal: indices of steep jumps (|dx| > thr), one per cluster.
template <typename T>
inline std::vector<double> jumpPositions(const std::vector<T>& x, double thr, std::size_t minGap) {
  std::vector<double> p;
  std::size_t last = 0;
  bool have = false;
  for (std::size_t i = 1; i < x.size(); ++i) {
    if (std::fabs(double(x[i]) - double(x[i - 1])) > thr) {
      if (!have || i - last > minGap) p.push_back(static_cast<double>(i));
      last = i;
      have = true;
    }
  }
  return p;
}

// Peak of a sawtooth: each ramp between two edges (excluding +-frac*period around the edges, which
// holds the band-limiting ripple) is fitted with a straight line, and the line is evaluated at the
// edges. Returns the largest |value| found (the ideal saw's peak).
template <typename T>
inline double rampPeak(const std::vector<T>& x, const std::vector<double>& edges, double period, double frac) {
  double best = 0.0;
  const double w = frac * period;
  for (std::size_t e = 0; e + 1 < edges.size(); ++e) {
    const double a = edges[e], b = edges[e + 1];
    if (b - a < 0.5 * period) continue;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (long i = static_cast<long>(std::ceil(a + w)); i <= static_cast<long>(std::floor(b - w)); ++i) {
      if (i < 0 || static_cast<std::size_t>(i) >= x.size()) continue;
      const double t = static_cast<double>(i), y = static_cast<double>(x[static_cast<std::size_t>(i)]);
      sx += t; sy += y; sxx += t * t; sxy += t * y; ++n;
    }
    if (n < 8) continue;
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    const double icpt = (sy - slope * sx) / n;
    best = std::max(best, std::max(std::fabs(icpt + slope * a), std::fabs(icpt + slope * b)));
  }
  return best;
}

inline double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

}  // namespace vjn6::test
