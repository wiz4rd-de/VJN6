// AC-E9.1 ... AC-E9.5 chorus
#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <cstdio>

#include "engine/engine_util.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
std::vector<double> noise(std::size_t n, double r, uint32_t seed) {
  XorShift32 g(seed);
  std::vector<double> v(n);
  for (double& x : v) x = r * std::sqrt(3.0) * g.nextBipolar();
  return v;
}

// Delay trajectory of one BBD line: impulses at the BBD input every `spacing` samples; each
// response centroid at the BBD output tap minus the impulse time.
struct Traj {
  std::vector<double> t, dL, dR;
};
Traj measureDelays(Mode m, int chorus, double fs, double seconds, long spacing) {
  Rig r(fs, m, 2);
  r.testPatch();
  r.st().edit.v[W_CHORUS] = static_cast<uint8_t>(chorus);
  r.st().settings.v[S_CHORUS_NOISE] = 0;
  r.toBoundary();
  r.skip(4800);
  const long n = static_cast<long>(seconds * fs);
  std::vector<double> inj(static_cast<std::size_t>(n), 0.0), out(static_cast<std::size_t>(2 * n + 16), 0.0);
  for (long i = spacing; i < n; i += spacing) inj[static_cast<std::size_t>(i)] = 1.0;
  Chorus& c = r.eng().chorus();
  r.eng().hooks.hostPos = 0;
  c.injBbdIn = inj.data();
  c.capBbdOut = out.data();
  r.skip(n);
  c.injBbdIn = nullptr;
  c.capBbdOut = nullptr;
  Traj tr;
  for (long i = spacing; i + spacing <= n; i += spacing) {
    double cl = 0, wl = 0, cr = 0, wr = 0;
    for (long j = i; j < i + spacing && j < n; ++j) {
      const double l = std::fabs(out[static_cast<std::size_t>(2 * j)]), rr = std::fabs(out[static_cast<std::size_t>(2 * j + 1)]);
      cl += l * j;
      wl += l;
      cr += rr * j;
      wr += rr;
    }
    if (wl <= 0 || wr <= 0) continue;
    tr.t.push_back(i / fs);
    tr.dL.push_back((cl / wl - i) / fs * 1000.0);
    tr.dR.push_back((cr / wr - i) / fs * 1000.0);
  }
  return tr;
}

double trajRate(const std::vector<double>& t, const std::vector<double>& d) {
  double mean = 0;
  for (double v : d) mean += v;
  mean /= d.size();
  std::vector<double> c;
  for (std::size_t k = 1; k < d.size(); ++k) {
    const double a = d[k - 1] - mean, b = d[k] - mean;
    if (a < 0 && b >= 0) c.push_back(t[k - 1] + (t[k] - t[k - 1]) * (-a) / (b - a));
  }
  if (c.size() < 2) return 0.0;
  return (c.size() - 1) / (c.back() - c.front());
}

double lp5(double f, double fr, double f1, double q1, double f2, double q2) {
  const std::complex<double> s(0.0, f);
  auto bq = [&](double fn, double q) { return fn * fn / (s * s + s * fn / q + fn * fn); };
  return std::abs(fr / (s + fr) * bq(f1, q1) * bq(f2, q2));
}
}  // namespace

TEST_CASE("AC-E9.1 BBD delay trajectories", "[engine][E9]") {
  struct Case {
    Mode m;
    int chorus;
    double centre, swing, rate, rateTol, swingTol;
    bool antiPhase;
  } cases[] = {
      {Mode::J60, 1, 3.3, 1.9, 0.513, 0.01, 0.1, true},  {Mode::J60, 2, 3.3, 1.9, 0.863, 0.01, 0.1, true},
      {Mode::J106, 1, 3.3, 1.9, 0.513, 0.01, 0.1, true}, {Mode::J106, 2, 3.3, 1.9, 0.863, 0.01, 0.1, true},
      {Mode::J60, 3, 3.4, 0.2, 9.5, 0.02, 0.05, false},
  };
  for (const Case& c : cases) {
    const double fs = 48000.0;
    const bool fast = c.chorus == 3;
    const Traj tr = measureDelays(c.m, c.chorus, fs, fast ? 2.0 : 8.0, fast ? 240 : 960);
    REQUIRE(tr.dL.size() > 20);
    double lo = 1e9, hi = -1e9, mean = 0;
    for (double d : tr.dL) {
      lo = std::min(lo, d);
      hi = std::max(hi, d);
      mean += d;
    }
    mean /= tr.dL.size();
    const double centre = 0.5 * (lo + hi), swing = 0.5 * (hi - lo);
    const double rate = trajRate(tr.t, tr.dL);
    const double corr = correlation(tr.dL, tr.dR);
    INFO("chorus " << c.chorus << " mode " << (c.m == Mode::J106 ? 106 : 60) << " centre " << centre << " swing "
                   << swing << " rate " << rate << " L/R corr " << corr);
    CHECK(std::fabs(centre - c.centre) <= 0.1);
    CHECK(std::fabs(swing - c.swing) <= c.swingTol);
    CHECK(std::fabs(rate / c.rate - 1.0) <= c.rateTol);
    if (c.antiPhase) CHECK(corr < -0.98);
    else CHECK(corr > 0.98);
  }
}

TEST_CASE("AC-E9.2 chorus I wet-path response", "[engine][E9]") {
  const double fs = 96000.0;
  Rig r(fs, Mode::J60, 2);
  r.testPatch();
  r.st().edit.v[W_CHORUS] = 1;
  r.st().settings.v[S_CHORUS_NOISE] = 0;
  r.toBoundary();
  r.skip(4800);
  const long n = static_cast<long>(10.0 * fs);
  const auto x = noise(static_cast<std::size_t>(n), 0.1, 99);
  std::vector<double> wet(static_cast<std::size_t>(n) + 16);
  EngineHooks& H = r.eng().hooks;
  H.hostPos = 0;
  H.injChorusIn = x.data();
  r.eng().chorus().capWetL = wet.data();
  r.skip(n);
  H.injChorusIn = nullptr;
  r.eng().chorus().capWetL = nullptr;
  wet.resize(static_cast<std::size_t>(n));
  const std::size_t seg = 16384;
  const auto px = welch(x, seg), py = welch(wet, seg);
  const double bin = fs / seg;
  auto measured = [&](double f0, double f1) {
    double a = 0, b = 0;
    for (std::size_t i = 1; i < px.size(); ++i) {
      const double f = i * bin;
      if (f >= f0 && f < f1) {
        a += py[i];
        b += px[i];
      }
    }
    return std::sqrt(a / b) / k::kWetGain;
  };
  const double bbd = std::pow(10.0, 2.3 / 20.0), fclk = 128.0 / 0.0033;
  auto model = [&](double f) {
    const double hin = lp5(f, 7410, 9690, 0.55, 10340, 1.24), hout = lp5(f, 28100, 8870, 0.54, 10380, 1.24);
    const double hp = f / std::sqrt(f * f + 20.0 * 20.0);
    const double x0 = f / fclk;
    const double zoh = std::fabs(std::sin(kPi * x0) / (kPi * x0));
    return hin * hout * hp * bbd * zoh;
  };
  double worst = 0;
  for (double fc = 100.0; fc <= 8000.0; fc *= std::pow(2.0, 1.0 / 3.0)) {
    const double f0 = fc / std::pow(2.0, 1.0 / 6.0), f1 = fc * std::pow(2.0, 1.0 / 6.0);
    double ms = 0;
    int k = 0;
    for (double f = f0; f < f1; f += bin) {
      ms += model(f) * model(f);
      ++k;
    }
    const double d = db(measured(f0, f1) / std::sqrt(ms / k));
    worst = std::max(worst, std::fabs(d));
    INFO("band " << fc << " Hz deviation " << d << " dB");
    CHECK(std::fabs(d) <= 1.5);
  }
  const double g1k = measured(900, 1100), g16k = measured(15500, 16500);
  INFO("16 kHz vs 1 kHz: " << db(g16k / g1k) << " dB; worst band " << worst);
  CHECK(db(g16k / g1k) <= -20.0);
}

TEST_CASE("AC-E9.3 chorus noise levels", "[engine][E9]") {
  struct Case {
    int chorus, noise;
    double target;  // dBFS; 0 = below -110
  } cases[] = {{0, 2, 0}, {1, 2, -57.0}, {1, 1, -63.0}, {1, 0, 0}};
  for (const Case& c : cases) {
    const double fs = 48000.0;
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[W_CHORUS] = static_cast<uint8_t>(c.chorus);
    r.st().settings.v[S_CHORUS_NOISE] = static_cast<int16_t>(c.noise);
    r.toBoundary();
    r.skip(static_cast<long>(0.5 * fs));
    const auto s = r.render(static_cast<long>(10.0 * fs));
    const double l = db(rms(s.L) + 1e-30), rr = db(rms(s.R) + 1e-30);
    INFO("chorus " << c.chorus << " noise " << c.noise << " L " << l << " R " << rr);
    if (c.target == 0) {
      CHECK(l < -110.0);
      CHECK(rr < -110.0);
    } else {
      CHECK(std::fabs(l - c.target) <= 1.0);
      CHECK(std::fabs(rr - c.target) <= 1.0);
      CHECK(std::fabs(correlation(s.L, s.R)) < 0.05);
    }
  }
}

TEST_CASE("AC-E9.4 dry level unchanged and wet/dry ratio 1.46", "[engine][E9]") {
  const double fs = 48000.0;
  const long n = static_cast<long>(4.0 * fs);
  std::vector<double> sine(static_cast<std::size_t>(n));
  for (long i = 0; i < n; ++i) sine[static_cast<std::size_t>(i)] = 0.3 * std::sin(2.0 * kPi * 200.0 * i / fs);
  auto run = [&](int chorus, bool muteDry, std::vector<double>* wetOut) {
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[W_CHORUS] = static_cast<uint8_t>(chorus);
    r.st().settings.v[S_CHORUS_NOISE] = 0;
    r.toBoundary();
    r.skip(480);
    EngineHooks& H = r.eng().hooks;
    H.hostPos = 0;
    H.injChorusIn = sine.data();
    H.muteDry = muteDry;
    if (wetOut) {
      wetOut->assign(static_cast<std::size_t>(n) + 16, 0.0);
      r.eng().chorus().capWetL = wetOut->data();
    }
    auto s = r.render(n);
    H.injChorusIn = nullptr;
    r.eng().chorus().capWetL = nullptr;
    return s;
  };
  const auto off = run(0, false, nullptr);
  std::vector<double> wet;
  const auto on = run(1, false, &wet);
  const auto wetOnly = run(1, true, nullptr);
  const std::size_t from = static_cast<std::size_t>(fs);  // skip the start
  std::vector<double> dryPart(on.L.size());
  for (std::size_t i = 0; i < on.L.size(); ++i) dryPart[i] = on.L[i] - wet[i] * k::kOutputGain;
  const double dOff = rms(std::vector<float>(off.L.begin() + static_cast<long>(from), off.L.end()));
  const double dOn = rms(std::vector<double>(dryPart.begin() + static_cast<long>(from), dryPart.end()));
  const double w = rms(std::vector<float>(wetOnly.L.begin() + static_cast<long>(from), wetOnly.L.end()));
  INFO("dry off " << db(dOff) << " dry with chorus " << db(dOn) << " wet/dry " << w / dOff);
  CHECK(std::fabs(db(dOn) - db(dOff)) <= 0.1);
  CHECK(std::fabs(w / dOff / 1.46 - 1.0) <= 0.03);
}

TEST_CASE("AC-E9.5 switching the chorus on clears the delay lines; mono", "[engine][E9]") {
  const double fs = 48000.0;
  {
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[W_CHORUS] = 1;
    r.st().settings.v[S_CHORUS_NOISE] = 0;
    r.toBoundary();
    const long n = static_cast<long>(0.5 * fs);
    std::vector<double> sine(static_cast<std::size_t>(n) + 4096, 0.0);
    for (long i = 0; i < n; ++i) sine[static_cast<std::size_t>(i)] = 0.3 * std::sin(2.0 * kPi * 440.0 * i / fs);
    EngineHooks& H = r.eng().hooks;
    H.hostPos = 0;
    H.injChorusIn = sine.data();
    H.muteDry = true;
    r.skip(n);  // input, then silence from here
    // chorus off and on again right away (while the old signal is still inside the BBD)
    r.field(W_CHORUS, 0);
    r.skip(32);
    r.field(W_CHORUS, 1);
    const auto s = r.render(2048);
    H.injChorusIn = nullptr;
    H.muteDry = false;
    CHECK(peakAbs(s.L) < 1e-9);
    CHECK(peakAbs(s.R) < 1e-9);
  }
  {
    auto run = [&](bool mono) {
      Rig r(fs, Mode::J60, 2);
      r.testPatch();
      r.st().edit.v[W_CHORUS] = 1;
      r.st().settings.v[S_MONO] = mono ? 1 : 0;
      r.toBoundary();
      r.noteOn(60);
      return r.render(static_cast<long>(0.5 * fs));
    };
    const auto st = run(false), mo = run(true);
    double worst = 0;
    for (std::size_t i = 0; i < st.L.size(); ++i) {
      CHECK(mo.L[i] == mo.R[i]);
      worst = std::max(worst, std::fabs(double(mo.L[i]) - 0.5 * (double(st.L[i]) + double(st.R[i]))));
    }
    CHECK(worst < 1e-7);
  }
}
