// AC-E1.1, AC-E1.2, AC-E1.4, AC-E2.x, AC-E3.x
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <set>

#include "engine/engine_util.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }
}

TEST_CASE("AC-E1.1 impulse at the VCF input arrives latencySamples later, L = R", "[engine][E1]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
      for (int os : {2, 4}) {
        Rig r(fs, m, os);
        r.testPatch();
        r.st().edit.v[B_VCF_FREQ] = 127;
        r.st().edit.v[W_SAW] = 0;  // only the injected impulse reaches the VCF
        r.toBoundary();
        r.skip(256);
        r.eng().voice(0).forceVcaOpen = true;
        const long n = 1024;
        const long k0 = 100;
        std::vector<double> inj(static_cast<std::size_t>(n * os), 0.0);
        inj[static_cast<std::size_t>(k0 * os)] = 1.0;
        EngineHooks& H = r.eng().hooks;
        H.voice = 0;
        H.osPos = 0;
        H.injVcfIn = inj.data();
        auto s = r.render(n);
        H.injVcfIn = nullptr;
        std::size_t best = 0;
        for (std::size_t i = 0; i < s.L.size(); ++i)
          if (std::fabs(s.L[i]) > std::fabs(s.L[best])) best = i;
        const long arrival = static_cast<long>(best) - k0;
        INFO(modeName(m) << " fs " << fs << " os " << os << " arrival " << arrival << " latency "
                         << r.inst->latencySamples());
        CHECK(std::labs(arrival - r.inst->latencySamples()) <= 1);
        CHECK(s.L == s.R);
      }
    }
  }
}

TEST_CASE("AC-E1.2 106 envelope steps at exactly 4.2 ms and follows a 1 ms RC", "[engine][E1]") {
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    Rig r(fs, Mode::J106, 2);
    r.testPatch();
    r.st().edit.v[B_ENV_A] = 127;
    r.toBoundary();
    r.noteOn(60);
    std::vector<double> env, rc;
    const long n = static_cast<long>(0.2 * fs);
    captureOs2(r, &EngineHooks::capEnv, env, &EngineHooks::capEnvRc, rc, n);
    const int os = r.eng().oversample();
    const double fo = fs * os;
    // change positions of the pre-RC envelope
    std::vector<long> changes;
    for (std::size_t i = 1; i < env.size(); ++i)
      if (env[i] != env[i - 1]) changes.push_back(static_cast<long>(i));
    REQUIRE(changes.size() > 10);
    const double tick = 0.0042 * fo;
    for (std::size_t c = 1; c < changes.size(); ++c) {
      const double d = static_cast<double>(changes[c] - changes[0]);
      const double k = std::round(d / tick);
      CHECK(std::fabs(d - k * tick) <= os + 1e-9);  // +-1 host sample
    }
    // after the RC: the step response between two ticks follows tau = 1 ms
    const long a = changes[3], b = changes[4];
    const double target = env[static_cast<std::size_t>(a)];
    const double start = rc[static_cast<std::size_t>(a - 1)];
    // find the time where the RC covered 1 - 1/e of the step
    const double goal = start + (target - start) * (1.0 - std::exp(-1.0));
    long t = a;
    while (t < b && rc[static_cast<std::size_t>(t)] < goal) ++t;
    const double tau = static_cast<double>(t - a + 1) / fo;
    INFO("fs " << fs << " tau " << tau);
    CHECK(std::fabs(tau - 0.001) <= 0.05 * 0.001 + 1.0 / fo);
  }
}

TEST_CASE("AC-E1.4 oversampling change stops voices and updates the latency", "[engine][E1]") {
  Rig r(48000.0, Mode::J60, 2);
  r.testPatch();
  const int l2 = r.inst->latencySamples();
  r.toBoundary();
  r.noteOn(60);
  r.skip(4800);
  REQUIRE(r.eng().voice(0).active);
  r.inst->post(Command::setSetting(S_OVERSAMPLE, 4));
  r.skip(256);
  const int l4 = r.inst->latencySamples();
  CHECK(l2 == Engine::latencyFor(2));
  CHECK(l4 == Engine::latencyFor(4));
  CHECK(l4 != l2);
  for (int i = 0; i < 6; ++i) CHECK_FALSE(r.eng().voice(i).active);
  bool sawLatency = false;
  for (const Event& e : r.events())
    if (e.type == EventType::Latency && e.i == l4) sawLatency = true;
  CHECK(sawLatency);
}

TEST_CASE("AC-E2.1 saw peak 1.0 at the VCF input and -24 dBFS at the output", "[engine][E2]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    Rig r(48000.0, m, 2);
    r.testPatch();
    r.st().edit.v[B_VCF_FREQ] = 127;
    r.toBoundary();
    r.noteOn(60);
    r.skip(4800);
    std::vector<double> vin, phase, inc;
    const long n = 9600;
    std::vector<double> ph, ic;
    captureOs2(r, &EngineHooks::capVcfIn, vin, &EngineHooks::capPhase, ph, n);
    const int os = r.eng().oversample();
    // phase increments for reset times: re-capture is not needed: inc constant for the test patch
    const double incv = ph[10] - ph[9] > 0 ? ph[10] - ph[9] : ph[11] - ph[10];
    std::vector<double> incs(ph.size(), incv);
    auto resets = resetTimes(ph, incs);
    for (double& t : resets) t += r.eng().blepDelay();
    const double period = 1.0 / incv;
    const double pk = rampPeak(vin, resets, period, 0.02);
    INFO(modeName(m) << " saw peak at VCF input " << pk);
    CHECK(std::fabs(pk - 1.0) <= 0.01);
    // engine output
    auto s = r.render(9600);
    const double hostPeriod = 48000.0 / noteHz(60);
    auto edges = jumpPositions(s.L, 0.02, static_cast<std::size_t>(hostPeriod / 2));
    for (double& e : edges) e -= 0.5;  // the jump lies between samples i-1 and i
    const double opk = rampPeak(s.L, edges, hostPeriod, 0.02);
    INFO(modeName(m) << " output peak " << db(opk) << " dBFS, os " << os);
    CHECK(std::fabs(db(opk) - (-24.0)) <= 0.5);
  }
}

TEST_CASE("AC-E2.2 noise source RMS, flat at the VCF input, identical in all voices", "[engine][E2]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 48000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    r.st().edit.v[W_SAW] = 0;
    r.st().edit.v[B_NOISE] = 127;
    r.toBoundary();
    r.noteOn(60);
    r.skip(4800);
    const long n = static_cast<long>(10 * fs);
    std::vector<double> host(static_cast<std::size_t>(n) + 16);
    std::vector<double> vin;
    r.eng().hooks.capNoiseHost = host.data();
    r.eng().hooks.hostPos = 0;
    vin = captureOs(r, &EngineHooks::capVcfIn, n, 0);
    r.eng().hooks.capNoiseHost = nullptr;
    host.resize(static_cast<std::size_t>(n));
    const double hr = rms(host);
    INFO(modeName(m) << " noise RMS " << hr);
    CHECK(std::fabs(hr - 0.11) <= 0.11 * 0.05);
    // flatness at the VCF input, 1/3-octave bands from 100 Hz to 0.4 fs
    const double fo = fs * r.eng().oversample();
    const std::size_t seg = 8192;
    auto psd = welch(vin, seg);
    std::vector<double> bands;
    for (double fc = 100.0; fc * std::pow(2.0, 1.0 / 6.0) <= 0.4 * fs; fc *= std::pow(2.0, 1.0 / 3.0)) {
      const double f0 = fc / std::pow(2.0, 1.0 / 6.0), f1 = fc * std::pow(2.0, 1.0 / 6.0);
      double s = 0;
      int k = 0;
      for (std::size_t i = 1; i < psd.size(); ++i) {
        const double f = i * fo / seg;
        if (f >= f0 && f < f1) {
          s += psd[i];
          ++k;
        }
      }
      if (k) bands.push_back(10.0 * std::log10(s / k));
    }
    double mean = 0;
    for (double b : bands) mean += b;
    mean /= bands.size();
    for (double b : bands) CHECK(std::fabs(b - mean) <= 1.0);
    // identical in all six voices
    for (int v = 1; v < 6; ++v) {
      Rig q(fs, m, 2);
      q.testPatch();
      q.st().edit.v[W_SAW] = 0;
      q.st().edit.v[B_NOISE] = 127;
      q.toBoundary();
      q.noteOn(60);
      q.skip(4800);
      const auto a = captureOs(q, &EngineHooks::capVcfIn, 24000, 0);
      Rig w(fs, m, 2);
      w.testPatch();
      w.st().edit.v[W_SAW] = 0;
      w.st().edit.v[B_NOISE] = 127;
      w.toBoundary();
      w.noteOn(60);
      w.skip(4800);
      const auto b = captureOs(w, &EngineHooks::capVcfIn, 24000, v);
      CHECK(correlation(a, b) > 0.999);
    }
  }
}

TEST_CASE("AC-E3.1 DCO pitch per range and note", "[engine][E3]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int range = 0; range < 3; ++range) {
      for (int note : {24, 60, 108}) {
        const double fs = 96000.0;
        Rig r(fs, m, 2);
        r.testPatch();
        r.st().edit.v[W_RANGE] = static_cast<uint8_t>(range);
        r.toBoundary();
        r.noteOn(note);
        r.skip(9600);
        std::vector<double> ph, inc;
        captureOs2(r, &EngineHooks::capPhase, ph, &EngineHooks::capInc, inc, static_cast<long>(fs * 2));
        const auto t = resetTimes(ph, inc);
        REQUIRE(t.size() > 20);
        const double fo = fs * r.eng().oversample();
        const double f = fo * (t.size() - 1) / (t.back() - t.front());
        const double rangeC = range == 0 ? -1200.0 : range == 2 ? 1200.0 : 0.0;
        double expect = 440.0 * std::pow(2.0, (note - 69) / 12.0 + rangeC / 1200.0);
        double tol = 0.5;
        if (m == Mode::J106) {
          const double fclk = range == 0 ? 1e6 : range == 2 ? 4e6 : 2e6;
          expect = fclk / std::round(fclk / expect);
          tol = 0.1;
        }
        INFO(modeName(m) << " range " << range << " note " << note << " f " << f << " expect " << expect);
        CHECK(std::fabs(cents(f, expect)) <= tol);
      }
    }
  }
}

TEST_CASE("AC-E3.2 106 bend sweep: <= 255 pitches, changes only at resets", "[engine][E3]") {
  const double fs = 48000.0;
  Rig r(fs, Mode::J106, 2);
  r.testPatch();
  r.perf(P_BEND_DCO, 127);
  r.bend(0);
  r.toBoundary();
  r.noteOn(69);
  r.skip(4800);
  const int os = r.eng().oversample();
  const long n = static_cast<long>(2.0 * fs);
  std::vector<double> ph(static_cast<std::size_t>(n * os + 64)), inc(static_cast<std::size_t>(n * os + 64));
  EngineHooks& H = r.eng().hooks;
  H.voice = 0;
  H.osPos = 0;
  H.capPhase = ph.data();
  H.capInc = inc.data();
  for (long blk = 0; blk < n / 64; ++blk) {
    const int v = static_cast<int>(std::lround(16383.0 * blk / (n / 64 - 1)));
    r.bend(v);
    r.skip(64);
  }
  H.capPhase = H.capInc = nullptr;
  ph.resize(static_cast<std::size_t>((n / 64) * 64 * os));
  inc.resize(ph.size());
  std::set<double> distinct(inc.begin(), inc.end());
  INFO("distinct pitches " << distinct.size());
  CHECK(distinct.size() <= 255);
  CHECK(distinct.size() > 200);
  // each change of the increment follows a counter reload (reset) in the previous sample
  for (std::size_t i = 2; i < inc.size(); ++i) {
    if (inc[i] != inc[i - 1]) {
      const bool resetBefore = ph[i - 1] < ph[i - 2];
      CHECK(resetBefore);
    }
  }
}

TEST_CASE("AC-E3.3 pulse duty under MANUAL PWM; high-going edge at the saw reset", "[engine][E3]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int b : {0, 64, 127}) {
      const double fs = 96000.0;
      Rig r(fs, m, 2);
      r.testPatch();
      Patch& p = r.st().edit;
      p.v[W_SAW] = 0;
      p.v[W_PULSE] = 1;
      p.v[W_PWM_SRC] = 1;
      p.v[B_DCO_PWM] = static_cast<uint8_t>(b);
      r.toBoundary();
      r.noteOn(60);
      r.skip(48000);
      std::vector<double> x, ph;
      captureOs2(r, &EngineHooks::capVcfIn, x, &EngineHooks::capPhase, ph, 48000);
      const double hi = *std::max_element(x.begin(), x.end()), lo = *std::min_element(x.begin(), x.end());
      const double mid = 0.5 * (hi + lo);
      auto rise = risingCrossings(x, mid);
      std::vector<double> neg(x.size());
      for (std::size_t i = 0; i < x.size(); ++i) neg[i] = -x[i];
      auto fall = risingCrossings(neg, -mid);
      REQUIRE(rise.size() > 10);
      const double period = (rise.back() - rise.front()) / (rise.size() - 1);
      double dsum = 0;
      int dn = 0;
      for (double rr : rise) {
        auto it = std::upper_bound(fall.begin(), fall.end(), rr);
        if (it == fall.end()) break;
        dsum += (*it - rr) / period;
        ++dn;
      }
      const double duty = 100.0 * dsum / dn;
      const double expect = 100.0 * (0.5 + 0.45 * b / 127.0);
      INFO(modeName(m) << " byte " << b << " duty " << duty);
      CHECK(std::fabs(duty - expect) <= 0.5);
      // high-going edge vs. reset (phase wrap), allowing for the fixed 4-sample BLEP delay
      std::vector<double> incs(ph.size());
      for (std::size_t i = 1; i < ph.size(); ++i) incs[i] = ph[i] > ph[i - 1] ? ph[i] - ph[i - 1] : 1.0 / period;
      auto resets = resetTimes(ph, incs);
      int matched = 0;
      for (double t : resets) {
        const double edge = t + r.eng().blepDelay();
        auto it = std::lower_bound(rise.begin(), rise.end(), edge - 2.0);
        if (it != rise.end() && std::fabs(*it - edge) < 0.6) ++matched;
      }
      CHECK(matched >= static_cast<int>(resets.size()) - 2);
    }
  }
}

TEST_CASE("AC-E3.4 PWM by LFO sweeps 5-95 % as a triangle; the pulse never disappears", "[engine][E3]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 48000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    Patch& p = r.st().edit;
    p.v[W_SAW] = 0;
    p.v[W_PULSE] = 1;
    p.v[W_PWM_SRC] = 0;
    p.v[B_DCO_PWM] = 127;
    p.v[B_LFO_RATE] = 0;
    r.toBoundary();
    r.noteOn(60);
    const double lfoHz = m == Mode::J106 ? 0.04 : 0.3;
    const long n = static_cast<long>(fs / lfoHz * 1.05);
    auto x = captureOs(r, &EngineHooks::capVcfIn, n);
    // per-cycle duty from mid-level crossings of each cycle (local mid between plateaus)
    const int os = r.eng().oversample();
    const double per = fs * os / noteHz(60);
    double dmin = 1, dmax = 0;
    int cycles = 0, missing = 0;
    const std::size_t P = static_cast<std::size_t>(per);
    for (std::size_t s = P; s + 2 * P < x.size(); s += P) {
      double hi = -9, lo = 9;
      for (std::size_t i = s; i < s + P; ++i) {
        hi = std::max(hi, x[i]);
        lo = std::min(lo, x[i]);
      }
      if (hi - lo < 1.0) {
        ++missing;
        continue;
      }
      const double mid = 0.5 * (hi + lo);
      std::size_t above = 0;
      for (std::size_t i = s; i < s + P; ++i)
        if (x[i] > mid) ++above;
      const double d = static_cast<double>(above) / P;
      dmin = std::min(dmin, d);
      dmax = std::max(dmax, d);
      ++cycles;
    }
    INFO(modeName(m) << " duty range " << dmin << " .. " << dmax << " cycles " << cycles);
    CHECK(missing == 0);
    CHECK(std::fabs(dmin - 0.05) <= 0.01);
    CHECK(std::fabs(dmax - 0.95) <= 0.01);
  }
}

TEST_CASE("AC-E3.5 sub oscillator: f/2 square, 50 % duty, peak 1.0, edges at resets", "[engine][E3]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 96000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    Patch& p = r.st().edit;
    p.v[W_SAW] = 0;
    p.v[W_SUB] = 1;
    p.v[B_SUB_LEVEL] = 127;
    r.toBoundary();
    r.noteOn(60);
    r.skip(48000);
    std::vector<double> x, ph;
    captureOs2(r, &EngineHooks::capVcfIn, x, &EngineHooks::capPhase, ph, 48000);
    const int os = r.eng().oversample();
    const double fo = fs * os;
    auto rise = risingCrossings(x, 0.0);
    std::vector<double> neg(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) neg[i] = -x[i];
    auto fall = risingCrossings(neg, 0.0);
    const double f = fo * (rise.size() - 1) / (rise.back() - rise.front());
    const double fsaw = m == Mode::J106 ? 2e6 / std::round(2e6 / noteHz(60)) : noteHz(60);
    CHECK(std::fabs(cents(f, fsaw / 2)) < 0.5);
    const double period = fo / f;
    double dsum = 0;
    int dn = 0;
    for (double rr : rise) {
      auto it = std::upper_bound(fall.begin(), fall.end(), rr);
      if (it == fall.end()) break;
      dsum += (*it - rr) / period;
      ++dn;
    }
    CHECK(std::fabs(100.0 * dsum / dn - 50.0) <= 0.5);
    std::vector<double> edges(rise);
    edges.insert(edges.end(), fall.begin(), fall.end());
    std::sort(edges.begin(), edges.end());
    const double pk = peakExcludingEdges(x, edges, period, 0.02, 0, x.size());
    INFO(modeName(m) << " sub peak " << pk);
    CHECK(std::fabs(pk - 1.0) <= 0.01);
    // edges coincide with saw resets (+ BLEP delay)
    std::vector<double> incs(ph.size(), (fsaw / fo));
    auto resets = resetTimes(ph, incs);
    int matched = 0;
    for (double e : edges) {
      bool ok = false;
      for (double t : resets)
        if (std::fabs(t + r.eng().blepDelay() - e) < 0.6) ok = true;
      if (ok) ++matched;
    }
    CHECK(matched >= static_cast<int>(edges.size()) - 2);
  }
}

TEST_CASE("AC-E3.6 saw alias components below -70 dB", "[engine][E3]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int note : {60, 96}) {
      const double fs = 96000.0;
      Rig r(fs, m, 2);
      r.testPatch();
      r.toBoundary();
      r.noteOn(note);
      r.skip(static_cast<long>(3 * fs));
      const std::size_t N = 1u << 17;
      auto s = r.render(static_cast<long>(N));
      auto mag = magnitudeSpectrum(s.L, 0, N);
      const double f0 = m == Mode::J106 ? 2e6 / std::round(2e6 / noteHz(note)) : noteHz(note);
      const double binHz = fs / N;
      const double fund = mag[static_cast<std::size_t>(std::lround(f0 / binHz))];
      double worst = 0;
      double worstF = 0;
      for (std::size_t i = 20; i < mag.size() - 1; ++i) {
        const double f = i * binHz;
        const double h = f / f0;
        if (std::fabs(h - std::round(h)) * f0 < 8 * binHz) continue;  // harmonic (window main lobe)
        if (mag[i] > worst) {
          worst = mag[i];
          worstF = f;
        }
      }
      INFO(modeName(m) << " note " << note << " worst alias " << db(worst / fund) << " dB at " << worstF);
      CHECK(db(worst / fund) < -70.0);
    }
  }
}

TEST_CASE("AC-E3.7 60 and 106 saws have opposite polarity, same shape", "[engine][E3]") {
  std::vector<double> seg[2];
  for (int mi = 0; mi < 2; ++mi) {
    const Mode m = mi ? Mode::J106 : Mode::J60;
    const double fs = 96000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    r.toBoundary();
    r.noteOn(60);
    r.skip(9600);
    std::vector<double> x, ph;
    captureOs2(r, &EngineHooks::capVcfIn, x, &EngineHooks::capPhase, ph, 9600);
    std::vector<double> incs(ph.size(), 0.0);
    for (std::size_t i = 1; i < ph.size(); ++i) incs[i] = ph[i] > ph[i - 1] ? ph[i] - ph[i - 1] : incs[i - 1];
    incs[0] = incs[1];
    auto t = resetTimes(ph, incs);
    REQUIRE(t.size() > 4);
    const std::size_t start = static_cast<std::size_t>(std::ceil(t[1])) + r.eng().blepDelay();
    const std::size_t len = static_cast<std::size_t>(t[2] - t[1]) - 2;
    seg[mi].assign(x.begin() + static_cast<long>(start), x.begin() + static_cast<long>(start + len));
  }
  const std::size_t n = std::min(seg[0].size(), seg[1].size());
  seg[0].resize(n);
  seg[1].resize(n);
  for (double& v : seg[1]) v = -v;
  CHECK(correlation(seg[0], seg[1]) > 0.999);
  // same amplitude
  CHECK(std::fabs(rms(seg[0]) - rms(seg[1])) < 0.005);
}
