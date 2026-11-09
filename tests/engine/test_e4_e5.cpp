// AC-E4.1, AC-E5.1 ... AC-E5.7
#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <cstdio>

#include "engine/engine_util.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }

// white noise (own generator), RMS = rms
std::vector<double> whiteNoise(std::size_t n, double rmsLevel, uint32_t seed = 12345) {
  XorShift32 g(seed);
  std::vector<double> v(n);
  for (double& x : v) x = rmsLevel * std::sqrt(3.0) * g.nextBipolar();
  return v;
}

// Injects x at `inj` for voice 0 and captures `cap`, returns the capture (oversampled).
std::vector<double> injectCapture(Rig& r, const double* EngineHooks::*inj, double* EngineHooks::*cap,
                                  const std::vector<double>& x) {
  const int os = r.eng().oversample();
  const long nHost = static_cast<long>(x.size()) / os;
  std::vector<double> out(x.size() + 16, 0.0);
  EngineHooks& H = r.eng().hooks;
  H.voice = 0;
  H.osPos = 0;
  H.*inj = x.data();
  H.*cap = out.data();
  r.skip(nHost);
  H.*inj = nullptr;
  H.*cap = nullptr;
  out.resize(x.size());
  return out;
}

// Analytic HPF magnitude (R-E4).
double hpfMag(Mode m, int pos, double f) {
  auto hp = [](double f0, double fc) { return f0 / std::sqrt(f0 * f0 + fc * fc); };
  if (m == Mode::J60) {
    const double c[4] = {0, 122.0, 269.0, 571.0};
    return pos == 0 ? 1.0 : hp(f, c[pos]);
  }
  if (pos == 1) return 1.0;
  if (pos == 2) return hp(f, 236.0);
  if (pos == 3) return hp(f, 754.0);
  const std::complex<double> s(0.0, f);
  const double K = std::pow(10.0, 1.4 / 20.0);
  const auto H = K * (s + 72.0) * (s + 170.0) / ((s + 59.4) * (s + 72.3));
  return std::abs(H);
}

double ccalFor(Mode m) { return m == Mode::J106 ? k::kCcal106[0] : k::kCcal60[0]; }
double fx64(Mode m) {
  return m == Mode::J106 ? ccalFor(m) * 5.53 * std::pow(2.0, 8192.0 / 1143.0)
                         : ccalFor(m) * 28.1 * std::pow(2.0, 10.5 * 64.0 / 127.0);
}
double interpGain(const std::vector<double>& h, double binHz, double f) {
  const double x = f / binHz;
  const std::size_t i = static_cast<std::size_t>(x);
  const double t = x - i;
  return h[i] * (1 - t) + h[i + 1] * t;
}
}  // namespace

TEST_CASE("AC-E4.1 HPF positions match the analytic response", "[engine][E4]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    double shelfAt5k = 0, flatAt5k = 0;
    for (int pos = 0; pos < 4; ++pos) {
      const double fs = 96000.0;
      Rig r(fs, m, 2);
      r.testPatch();
      r.st().edit.v[W_HPF] = static_cast<uint8_t>(pos);
      r.toBoundary();
      r.skip(64);
      const int os = r.eng().oversample();
      const std::size_t seg = 32768;
      const auto x = whiteNoise(seg * 34, 0.1);
      const auto y = injectCapture(r, &EngineHooks::injHpfIn, &EngineHooks::capVcfIn, x);
      const auto h = transfer(x, y, seg);
      const double fo = fs * os;
      const double binHz = fo / seg;
      double worst = 0;
      for (double fc = 20.0; fc <= 20000.0; fc *= std::pow(2.0, 1.0 / 3.0)) {
        const double f0 = fc / std::pow(2.0, 1.0 / 6.0), f1 = fc * std::pow(2.0, 1.0 / 6.0);
        double sm = 0, sa = 0;
        int n = 0;
        for (std::size_t i = 1; i < h.size(); ++i) {
          const double f = i * binHz;
          if (f < f0 || f >= f1) continue;
          sm += h[i] * h[i];
          sa += hpfMag(m, pos, f) * hpfMag(m, pos, f);
          ++n;
        }
        if (!n) continue;
        const double d = 10.0 * std::log10(sm / sa);
        worst = std::max(worst, std::fabs(d));
      }
      INFO(modeName(m) << " pos " << pos << " worst band deviation " << worst << " dB");
      CHECK(worst <= 0.5);
      if (m == Mode::J106 && pos == 0) shelfAt5k = interpGain(h, binHz, 5000.0);
      if (m == Mode::J106 && pos == 1) flatAt5k = interpGain(h, binHz, 5000.0);
    }
    if (m == Mode::J106) {
      const double rel = db(shelfAt5k / flatAt5k);
      INFO("106 position 0 vs 1 at 5 kHz: " << rel << " dB");
      CHECK(std::fabs(rel - 1.4) <= 0.3);
    }
  }
}

TEST_CASE("AC-E5.1 calibration: self-oscillation at 248 Hz and 992 Hz", "[engine][E5]") {
  CHECK(k::kCcal60[0] >= 1.0);
  CHECK(k::kCcal60[0] <= 1.25);
  CHECK(k::kCcal106[0] >= 1.0);
  CHECK(k::kCcal106[0] <= 1.25);
  CHECK(k::kCcal60[1] >= 1.0);
  CHECK(k::kCcal60[1] <= 1.25);
  CHECK(k::kCcal106[1] >= 1.0);
  CHECK(k::kCcal106[1] <= 1.25);
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
      for (int os : {2, 4}) {
        for (int note : {60, 84}) {
          Rig r(fs, m, os);
          r.selfOscPatch();
          r.toBoundary();
          r.noteOn(note);
          r.skip(static_cast<long>(3.0 * fs));
          const auto y = captureOs(r, &EngineHooks::capVcfOut, static_cast<long>(0.5 * fs));
          const double f = frequency(y, fs * os);
          const double expect = note == 60 ? 248.0 : 992.0;
          INFO(modeName(m) << " fs " << fs << " os " << os << " note " << note << " f " << f);
          CHECK(std::fabs(cents(f, expect)) <= 10.0);
        }
      }
    }
  }
}

TEST_CASE("AC-E5.2 self-oscillation amplitude, purity and flatness", "[engine][E5]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 96000.0;
    Rig r(fs, m, 2);
    r.selfOscPatch();
    r.toBoundary();
    r.noteOn(60);
    r.skip(static_cast<long>(3.0 * fs));
    const std::size_t N = 1u << 17;
    const auto y = captureOs(r, &EngineHooks::capVcfOut, static_cast<long>(N / 2));
    const double pk = peakAbs(y);
    const double expect = m == Mode::J106 ? 0.40 : 0.33;
    INFO(modeName(m) << " self-osc peak " << pk);
    CHECK(std::fabs(pk - expect) <= 0.15 * expect);
    const auto mag = magnitudeSpectrum(y, 0, N);
    const double fo = fs * 2, bin = fo / N;
    const double f0 = peakFrequency(mag, fo, N, 100.0);
    auto at = [&](double f) {
      const std::size_t c = static_cast<std::size_t>(std::lround(f / bin));
      double v = 0;
      for (std::size_t i = c - 6; i <= c + 6; ++i) v = std::max(v, mag[i]);
      return v;
    };
    const double h3 = db(at(3 * f0) / at(f0));
    INFO("H3 " << h3 << " dB");
    CHECK(h3 <= -40.0);
    // level flat +-1 dB from 110 Hz to 14 kHz (FREQ moves the oscillation, KYBD at note 60 adds 0)
    std::vector<double> levels;
    for (double target : {110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0, 14000.0}) {
      Rig q(fs, m, 2);
      q.selfOscPatch();
      // choose the FREQ byte nearest to the target self-oscillation frequency
      int best = 0;
      double bestErr = 1e9;
      for (int b = 0; b < 128; ++b) {
        const double fc = m == Mode::J106 ? 5.53 * std::pow(2.0, 128.0 * b / 1143.0) : 28.1 * std::pow(2.0, 10.5 * b / 127.0);
        const double e = std::fabs(std::log2(fc / target));
        if (e < bestErr) {
          bestErr = e;
          best = b;
        }
      }
      q.st().edit.v[B_VCF_FREQ] = static_cast<uint8_t>(best);
      q.toBoundary();
      q.noteOn(60);
      q.skip(static_cast<long>(3.0 * fs));
      const auto z = captureOs(q, &EngineHooks::capVcfOut, static_cast<long>(0.3 * fs));
      levels.push_back(db(peakAbs(z)));
    }
    // flat +-1 dB: all levels fit in a 2 dB wide band
    const double lo = *std::min_element(levels.begin(), levels.end());
    const double hi = *std::max_element(levels.begin(), levels.end());
    INFO(modeName(m) << " self-osc level range " << lo << " .. " << hi << " dB");
    CHECK(hi - lo <= 2.0);
  }
}

TEST_CASE("AC-E5.3 -24 dB/oct slope and the -12 dB point", "[engine][E5]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 96000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    r.st().edit.v[B_VCF_FREQ] = 64;
    r.toBoundary();
    r.skip(64);
    const std::size_t seg = 65536;
    const auto x = whiteNoise(seg * 34, 0.01);
    const auto y = injectCapture(r, &EngineHooks::injVcfIn, &EngineHooks::capVcfOut, x);
    const auto h = transfer(x, y, seg);
    const double fo = fs * 2, binHz = fo / seg;
    const double g20 = interpGain(h, binHz, 20.0);
    const double fx = fx64(m);
    const double s = (db(interpGain(h, binHz, 16 * fx)) - db(interpGain(h, binHz, 4 * fx))) / 2.0;
    INFO(modeName(m) << " slope " << s << " dB/oct");
    CHECK(std::fabs(s - (-24.0)) <= 1.5);
    // -12 dB point
    double f12 = 0;
    for (std::size_t i = 2; i < h.size(); ++i) {
      if (db(h[i] / g20) <= -12.0) {
        const double a = db(h[i - 1] / g20), b = db(h[i] / g20);
        f12 = ((i - 1) + (a + 12.0) / (a - b)) * binHz;
        break;
      }
    }
    INFO(modeName(m) << " -12 dB point " << f12 << " Hz, fx " << fx);
    CHECK(std::fabs(f12 / fx - 1.0) <= 0.02);
  }
}

TEST_CASE("AC-E5.4 resonance law from the peak height", "[engine][E5]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double kmax = m == Mode::J106 ? 4.5 : 4.35;
    for (int res : {0, 64, 110}) {
      const double fs = 96000.0;
      Rig r(fs, m, 2);
      r.testPatch();
      r.st().edit.v[B_VCF_FREQ] = 64;
      r.st().edit.v[B_VCF_RES] = static_cast<uint8_t>(res);
      r.toBoundary();
      r.skip(64);
      const std::size_t seg = 65536;
      const auto x = whiteNoise(seg * 34, 0.01);  // -40 dBFS
      const auto y = injectCapture(r, &EngineHooks::injVcfIn, &EngineHooks::capVcfOut, x);
      const auto h = transfer(x, y, seg);
      const double fo = fs * 2, binHz = fo / seg;
      const double g20 = interpGain(h, binHz, 20.0);
      const double fx = fx64(m);
      // peak height at fx relative to the 20 Hz gain: |H(fx)| / |H(0)| = (1 + k) / (4 - k)
      const double P = interpGain(h, binHz, fx) / g20;
      if (res == 0) {
        bool mono = true;
        for (std::size_t i = static_cast<std::size_t>(20.0 / binHz) + 1; i < static_cast<std::size_t>(20000.0 / binHz); ++i)
          if (h[i] > h[i - 1] * 1.002) mono = false;
        CHECK(mono);
        continue;
      }
      const double kMeas = (4.0 * P - 1.0) / (P + 1.0);
      const double r15 = std::pow(res / 127.0, 1.5);
      INFO(modeName(m) << " RES " << res << " P " << P << " k " << kMeas << " expected " << kmax * r15);
      CHECK(std::fabs(kMeas / (kmax * r15) - 1.0) <= 0.05);
    }
    // self-oscillation at 127, none at 110 (no input)
    for (int res : {110, 127}) {
      const double fs = 48000.0;
      Rig r(fs, m, 2);
      r.selfOscPatch();
      r.st().edit.v[B_VCF_RES] = static_cast<uint8_t>(res);
      r.toBoundary();
      r.noteOn(60);
      r.skip(static_cast<long>(3.0 * fs));
      const auto y = captureOs(r, &EngineHooks::capVcfOut, static_cast<long>(0.2 * fs));
      const double pk = peakAbs(y);
      INFO(modeName(m) << " RES " << res << " peak without input " << pk);
      if (res == 127) CHECK(pk > 0.2);
      else CHECK(pk < 1e-6);
    }
  }
}

TEST_CASE("AC-E5.5 Q compensation: gain at 20 Hz at RES 110", "[engine][E5]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 96000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    r.st().edit.v[B_VCF_FREQ] = 64;
    r.st().edit.v[B_VCF_RES] = 110;
    r.toBoundary();
    r.skip(64);
    // 20 Hz sine at -40 dBFS through the VCF
    const int os = 2;
    const std::size_t n = static_cast<std::size_t>(fs * os * 1.0);
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) x[i] = 0.01 * std::sin(2.0 * kPi * 20.0 * i / (fs * os));
    const auto y = injectCapture(r, &EngineHooks::injVcfIn, &EngineHooks::capVcfOut, x);
    const double g = rms(std::vector<double>(y.begin() + n / 2, y.end())) / rms(std::vector<double>(x.begin() + n / 2, x.end()));
    const double kk = (m == Mode::J106 ? 4.5 : 4.35) * std::pow(110.0 / 127.0, 1.5);
    INFO(modeName(m) << " 20 Hz gain " << db(g) << " dB (k " << kk << ")");
    CHECK(std::fabs(db(g) - (-8.0)) <= 0.5);
    CHECK(std::fabs(db(g) - db((1 + 0.23 * kk) / (1 + kk))) <= 0.1);
  }
}

TEST_CASE("AC-E5.6 106 cutoff word matches the formula", "[engine][E5]") {
  for (int pol : {0, 1}) {
    for (double lever : {-1.0, 1.0}) {
      for (double lfo : {-1.0, 1.0}) {
        for (int freq : {0, 64, 127}) {
          Rig r(48000.0, Mode::J106, 2);
          r.testPatch();
          Patch& p = r.st().edit;
          p.v[B_VCF_ENV] = 127;
          p.v[W_VCF_POL] = static_cast<uint8_t>(pol);
          p.v[B_VCF_KYBD] = 127;
          p.v[B_VCF_LFO] = 127;
          p.v[B_VCF_FREQ] = static_cast<uint8_t>(freq);
          r.perf(P_BEND_VCF, 127);
          r.eng().setLever(lever);
          r.eng().testForceLfo = true;
          r.eng().testLfoValue = lfo;
          r.eng().testDelayGain = 1.0;
          r.toBoundary();
          r.noteOn(108);
          r.skip(2000);
          r.eng().voice(0).forceEnv = true;
          r.eng().voice(0).forcedEnv = 1.0;
          r.skip(2000);
          const double envTerm = 14.2 * (127 / 127.0) * 1.0 * (pol ? -1.0 : 1.0);
          const double w = std::round(128.0 * freq + 1143.0 * (envTerm + 3.5 * (127 / 127.0) * lfo +
                                                               3.5 * (127 / 127.0) * (std::round(lever * 127.0) / 127.0) +
                                                               (127 / 127.0) * (108 - 60.0) / 12.0));
          const int expect = static_cast<int>(std::min(16383.0, std::max(0.0, w)));
          INFO("pol " << pol << " lever " << lever << " lfo " << lfo << " freq " << freq);
          CHECK(r.eng().voice(0).W == expect);
        }
      }
    }
  }
  // the clamp at 16383 is reached
  Rig r(48000.0, Mode::J106, 2);
  r.testPatch();
  Patch& p = r.st().edit;
  p.v[B_VCF_ENV] = 127;
  p.v[B_VCF_KYBD] = 127;
  p.v[B_VCF_FREQ] = 127;
  r.toBoundary();
  r.noteOn(108);
  r.skip(2000);
  r.eng().voice(0).forceEnv = true;
  r.eng().voice(0).forcedEnv = 1.0;
  r.skip(2000);
  CHECK(r.eng().voice(0).W == 16383);
}

TEST_CASE("AC-E5.7 60 mode ENV 127: fc ratio 2^10 between env 0 and env 1", "[engine][E5]") {
  const double fs = 96000.0;
  double fcs[2];
  for (int e = 0; e < 2; ++e) {
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[B_VCF_ENV] = 127;
    r.st().edit.v[B_VCF_FREQ] = 10;
    r.toBoundary();
    r.noteOn(60);
    r.skip(960);
    r.eng().voice(0).forceEnv = true;
    r.eng().voice(0).forcedEnv = e;
    r.skip(960);
    const auto c = captureOs(r, &EngineHooks::capCut, 960);
    fcs[e] = c.back();
  }
  INFO("fc env0 " << fcs[0] << " env1 " << fcs[1]);
  CHECK(std::fabs(fcs[1] / fcs[0] / 1024.0 - 1.0) <= 0.02);
}
