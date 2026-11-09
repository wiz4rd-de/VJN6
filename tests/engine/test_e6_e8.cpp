// AC-E6.1, AC-E6.2, AC-E7.1 ... AC-E7.4, AC-E8.1 ... AC-E8.5
#include <catch2/catch_test_macros.hpp>
#include <cstdio>

#include "engine/engine_util.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }

// first index >= from where pred(x[i])
template <typename P>
long firstWhere(const std::vector<double>& x, long from, P pred) {
  for (long i = from; i < static_cast<long>(x.size()); ++i)
    if (pred(x[static_cast<std::size_t>(i)])) return i;
  return -1;
}

double sliderT(Mode m, int stage, int b) {  // stage 0 attack, 1 decay/release
  const double x = b / 127.0;
  double tmin, tmax;
  if (m == Mode::J60) {
    tmin = stage == 0 ? 0.001 : 0.002;
    tmax = stage == 0 ? 3.0 : 12.0;
  } else {
    tmin = 0.0015;
    tmax = stage == 0 ? 3.0 : 12.0;
  }
  return tmin + (tmax - tmin) * x * x;
}

// Advances until the next control boundary has been applied; returns the boundary position.
long nextControl(Rig& r) {
  const long n = static_cast<long>(r.eng().nextBoundary() - r.eng().now());
  if (n > 0) r.skip(n);  // at the boundary, not yet applied
  const long b = static_cast<long>(r.eng().now());
  r.skip(1);  // applies the boundary
  return b;
}

// Frequency of a sequence sampled on an arbitrary time axis (rising zero crossings).
double freqOnAxis(const std::vector<double>& t, const std::vector<double>& v, double fs) {
  std::vector<double> c;
  for (std::size_t k = 1; k < v.size(); ++k)
    if (v[k - 1] < 0 && v[k] >= 0) c.push_back(t[k - 1] + (t[k] - t[k - 1]) * (-v[k - 1]) / (v[k] - v[k - 1]));
  if (c.size() < 2) return 0.0;
  return fs * (c.size() - 1) / (c.back() - c.front());
}
}  // namespace

TEST_CASE("AC-E6.1 106 VCA PNP converter law", "[engine][E6]") {
  const double env[] = {0.05, 0.1, 0.3, 1.0};
  const double expect[] = {0.0112, 0.1, 0.3, 1.0};
  for (int i = 0; i < 4; ++i) {
    Rig r(48000.0, Mode::J106, 2);
    r.testPatch();
    r.toBoundary();
    r.noteOn(60);
    r.skip(480);
    r.eng().voice(0).forceEnv = true;
    r.eng().voice(0).forcedEnv = env[i];
    r.skip(2400);  // > 10 RC time constants
    const auto g = captureOs(r, &EngineHooks::capVca, 64);
    INFO("env " << env[i] << " gain " << g.back());
    CHECK(std::fabs(g.back() / expect[i] - 1.0) <= 0.03);
  }
}

TEST_CASE("AC-E6.2 GATE ramp 1.5 ms without overshoot", "[engine][E6]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 48000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    r.st().edit.v[W_VCA_MODE] = 1;
    r.toBoundary();
    r.noteOn(60);
    const auto on = captureOs(r, &EngineHooks::capVca, 2400);
    r.noteOff(60);
    const auto off = captureOs(r, &EngineHooks::capVca, 2400);
    const double fo = fs * r.eng().oversample();
    const long a0 = firstWhere(on, 0, [](double v) { return v > 0.0; });
    const long a1 = firstWhere(on, 0, [](double v) { return v >= 1.0; });
    const long b0 = firstWhere(off, 0, [](double v) { return v < 1.0; });
    const long b1 = firstWhere(off, 0, [](double v) { return v <= 0.0; });
    REQUIRE(a0 >= 0);
    REQUIRE(a1 >= 0);
    REQUIRE(b0 >= 0);
    REQUIRE(b1 >= 0);
    const double tOn = (a1 - a0 + 1) / fo, tOff = (b1 - b0 + 1) / fo;
    INFO(modeName(m) << " ramp on " << tOn * 1000 << " ms, off " << tOff * 1000 << " ms");
    CHECK(std::fabs(tOn - 0.0015) <= 0.0001);
    CHECK(std::fabs(tOff - 0.0015) <= 0.0001);
    CHECK(peakAbs(on) <= 1.0);
    CHECK(*std::min_element(off.begin(), off.end()) >= 0.0);
  }
}

TEST_CASE("AC-E7.1 envelope times", "[engine][E7]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 44100.0;
    const double tick = 0.0042;
    for (int b : {0, 64, 127}) {
      // ---- attack (0 -> 1)
      {
        Rig r(fs, m, 2);
        r.testPatch();
        r.st().edit.v[B_ENV_A] = static_cast<uint8_t>(b);
        r.st().edit.v[B_ENV_S] = 64;
        r.toBoundary();
        r.noteOn(60);
        const double T = sliderT(m, 0, b);
        const auto e = captureOs(r, &EngineHooks::capEnv, static_cast<long>((T * 1.1 + 0.02) * fs));
        const double fo = fs * 2;
        const long s0 = firstWhere(e, 0, [](double v) { return v > 0.0; });
        const long s1 = firstWhere(e, 0, [](double v) { return v >= 1.0; });
        REQUIRE(s1 >= 0);
        const double t = (s1 - s0 + 1) / fo;
        INFO(modeName(m) << " attack byte " << b << " t " << t << " expected " << T);
        if (m == Mode::J106 && b == 0) {
          CHECK(t <= tick + 1.0 / fs);
        } else {
          const double tol = m == Mode::J106 ? std::max(0.03 * T, tick) : 0.03 * T;
          CHECK(std::fabs(t - T) <= tol + 1.0 / fo);
        }
      }
      // ---- decay (1 -> S + 0.0067 (1 - S)) and release (L -> 0.0067 L)
      for (int stage : {1, 2}) {
        Rig r(fs, m, 2);
        r.testPatch();
        Patch& p = r.st().edit;
        p.v[B_ENV_A] = 0;
        p.v[B_ENV_S] = 64;
        p.v[stage == 1 ? B_ENV_D : B_ENV_R] = static_cast<uint8_t>(b);
        p.v[stage == 1 ? B_ENV_R : B_ENV_D] = 0;
        const double T = sliderT(m, 1, b);
        const double S = 64 / 127.0;
        r.toBoundary();
        r.noteOn(60);
        std::vector<double> e;
        if (stage == 1) {
          e = captureOs(r, &EngineHooks::capEnv, static_cast<long>((T * 1.1 + 0.05) * fs));
        } else {
          r.skip(static_cast<long>(0.2 * fs));  // decay byte 0: at sustain
          r.noteOff(60);
          e = captureOs(r, &EngineHooks::capEnv, static_cast<long>((T * 1.1 + 0.05) * fs));
        }
        const double fo = fs * 2;
        long s0, s1;
        if (stage == 1) {
          s0 = firstWhere(e, 0, [](double v) { return v >= 1.0; });
          const double goal = S + 0.0067 * (1.0 - S);
          s1 = firstWhere(e, s0, [&](double v) { return v <= goal; });
        } else {
          const double L = e[0];
          s0 = firstWhere(e, 0, [&](double v) { return v < L; }) - 1;
          if (m == Mode::J106) s0 = std::max<long>(0, s0);
          s1 = firstWhere(e, std::max<long>(s0, 0), [&](double v) { return v <= 0.0067 * L; });
        }
        REQUIRE(s0 >= 0);
        REQUIRE(s1 >= 0);
        const double t = (s1 - s0) / fo;
        INFO(modeName(m) << (stage == 1 ? " decay" : " release") << " byte " << b << " t " << t << " expected " << T);
        const double tol = m == Mode::J106 ? std::max(0.03 * T, tick) : 0.03 * T;
        CHECK(std::fabs(t - T) <= tol + 2.0 / fo);
      }
    }
  }
}

TEST_CASE("AC-E7.2 60 attack shape 1.5 (1 - exp(-t/tau))", "[engine][E7]") {
  const double fs = 48000.0;
  Rig r(fs, Mode::J60, 2);
  r.testPatch();
  r.st().edit.v[B_ENV_A] = 64;
  r.toBoundary();
  r.noteOn(60);
  const double T = sliderT(Mode::J60, 0, 64);
  const double tau = T / std::log(3.0);
  const auto e = captureOs(r, &EngineHooks::capEnv, static_cast<long>(T * 1.05 * fs));
  const double fo = fs * 2;
  const long s0 = firstWhere(e, 0, [](double v) { return v > 0.0; });
  double worst = 0;
  for (long i = s0; i < static_cast<long>(e.size()); ++i) {
    if (e[static_cast<std::size_t>(i)] >= 1.0) break;
    const double t = (i - s0 + 1) / fo;
    worst = std::max(worst, std::fabs(e[static_cast<std::size_t>(i)] - 1.5 * (1.0 - std::exp(-t / tau))));
  }
  INFO("worst deviation " << worst);
  CHECK(worst <= 0.01);
}

TEST_CASE("AC-E7.3 106 attack is linear and stepped at 4.2 ms", "[engine][E7]") {
  const double fs = 48000.0;
  Rig r(fs, Mode::J106, 2);
  r.testPatch();
  r.st().edit.v[B_ENV_A] = 127;
  r.toBoundary();
  r.noteOn(60);
  const auto e = captureOs(r, &EngineHooks::capEnv, static_cast<long>(3.1 * fs));
  const double fo = fs * 2, tick = 0.0042 * fo;
  // collect step values and positions
  std::vector<double> pos, val;
  for (std::size_t i = 1; i < e.size(); ++i)
    if (e[i] != e[i - 1]) {
      pos.push_back(static_cast<double>(i));
      val.push_back(e[i]);
    }
  REQUIRE(pos.size() > 100);
  double worstLin = 0, worstStep = 0;
  for (std::size_t k = 0; k < pos.size(); ++k) {
    if (val[k] >= 1.0) break;
    const double t = (pos[k] - pos[0]) / fo;
    worstLin = std::max(worstLin, std::fabs(val[k] - (val[0] + t / 3.0)));
    const double d = pos[k] - pos[0];
    worstStep = std::max(worstStep, std::fabs(d - std::round(d / tick) * tick));
  }
  INFO("linearity " << worstLin << " step jitter " << worstStep);
  CHECK(worstLin <= 0.01);
  CHECK(worstStep <= 2.0);  // +-1 host sample
}

TEST_CASE("AC-E7.4 retrigger during release starts the attack from the current level", "[engine][E7]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 48000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    Patch& p = r.st().edit;
    p.v[B_ENV_A] = 20;
    p.v[B_ENV_R] = 100;
    r.toBoundary();
    r.noteOn(60);
    r.skip(static_cast<long>(0.5 * fs));
    r.noteOff(60);
    while (r.eng().voice(0).env > 0.5) r.skip(32);
    const double before = r.eng().voice(0).env;
    r.noteOn(60);
    const auto e = captureOs(r, &EngineHooks::capEnv, 4800);
    const double lowest = *std::min_element(e.begin(), e.end());
    INFO(modeName(m) << " level before " << before << " lowest after " << lowest << " first " << e.front());
    CHECK(std::fabs(before - 0.5) <= 0.02);
    CHECK(lowest >= before - 0.02);
    CHECK(e.back() > e.front());
  }
}

TEST_CASE("AC-E8.1 60 LFO rates", "[engine][E8]") {
  const double expect[] = {0.3, 2.49, 20.0};
  const int bytes[] = {0, 64, 127};
  for (int i = 0; i < 3; ++i) {
    const double fs = 48000.0;
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[B_LFO_RATE] = static_cast<uint8_t>(bytes[i]);
    r.eng().testSkipVoices = true;
    r.toBoundary();
    std::vector<double> t, v;
    const long end = r.eng().now() + static_cast<long>(12.0 / expect[i] * fs);
    while (r.eng().now() < end) {
      t.push_back(static_cast<double>(nextControl(r)));
      v.push_back(r.eng().lfoValue());
    }
    const double f = freqOnAxis(t, v, fs);
    INFO("byte " << bytes[i] << " f " << f);
    CHECK(std::fabs(f / expect[i] - 1.0) <= 0.02);
  }
}

TEST_CASE("AC-E8.2 106 LFO rate table", "[engine][E8]") {
  const double table[11] = {0.04, 1.07, 1.99, 2.91, 3.84, 4.76, 6.26, 7.73, 10.95, 18.67, 29.76};
  for (int b : {0, 13, 25, 38, 51, 64, 76, 89, 102, 114, 127}) {
    const double x = b / 12.7;
    int i = static_cast<int>(std::floor(x));
    if (i > 9) i = 9;
    const double expect = table[i] + (table[i + 1] - table[i]) * (x - i);
    const double fs = 48000.0;
    Rig r(fs, Mode::J106, 2);
    r.testPatch();
    r.st().edit.v[B_LFO_RATE] = static_cast<uint8_t>(b);
    r.eng().testSkipVoices = true;
    r.toBoundary();
    std::vector<double> t, v;
    const long end = r.eng().now() + static_cast<long>(22.0 / expect * fs);
    while (r.eng().now() < end) {
      t.push_back(static_cast<double>(nextControl(r)));
      v.push_back(r.eng().lfoValue());
    }
    const double f = freqOnAxis(t, v, fs);
    INFO("byte " << b << " f " << f << " expected " << expect);
    CHECK(std::fabs(f / expect - 1.0) <= 0.02);
  }
}

TEST_CASE("AC-E8.3 LFO pitch depth", "[engine][E8]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 44100.0;
    Rig r(fs, m, 2);
    r.testPatch();
    Patch& p = r.st().edit;
    p.v[B_DCO_LFO] = 127;
    p.v[B_LFO_RATE] = 64;
    p.v[B_LFO_DELAY] = 0;
    r.toBoundary();
    r.noteOn(69);
    r.skip(static_cast<long>(0.3 * fs));
    const double lfoHz = m == Mode::J106 ? 4.76 + 1.5 * (64 / 12.7 - 5) : 2.49;
    const long n = static_cast<long>(51.0 / lfoHz * fs);
    double fmin = 1e9, fmax = 0;
    const int os = 2;
    std::vector<double> inc(static_cast<std::size_t>(8192 * os + 16));
    EngineHooks& H = r.eng().hooks;
    H.voice = 0;
    for (long done = 0; done < n; done += 8192) {
      H.osPos = 0;
      H.capInc = inc.data();
      r.skip(8192);
      H.capInc = nullptr;
      for (std::size_t i = 0; i < static_cast<std::size_t>(8192 * os); ++i) {
        fmin = std::min(fmin, inc[i]);
        fmax = std::max(fmax, inc[i]);
      }
    }
    const double fo = fs * os;
    const double base = m == Mode::J106 ? 2e6 / std::round(2e6 / 440.0) : 440.0;
    const double up = cents(fmax * fo, base), down = cents(fmin * fo, base);
    const double depth = m == Mode::J106 ? 400.0 : 300.0;
    INFO(modeName(m) << " deviation +" << up << " / " << down << " cents");
    CHECK(std::fabs(up - depth) <= 3.0);
    CHECK(std::fabs(down + depth) <= 3.0);
  }
}

TEST_CASE("AC-E8.4 LFO delay in AUTO", "[engine][E8]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double fs = 48000.0;
    Rig r(fs, m, 2);
    r.testPatch();
    r.st().edit.v[B_LFO_DELAY] = 127;
    r.toBoundary();
    const long t0 = r.eng().now();
    r.noteOn(60);
    std::vector<double> t, g;
    const long end = t0 + static_cast<long>(4.0 * fs);
    bool second = false;
    while (r.eng().now() < end) {
      t.push_back(static_cast<double>(nextControl(r) - t0) / fs);
      g.push_back(r.eng().delayGain());
      if (!second && r.eng().now() - t0 > static_cast<long>(1.0 * fs)) {
        r.noteOn(64);  // a second note while the first is held does not restart the delay
        second = true;
      }
    }
    const double D = m == Mode::J106 ? 3.0 : 2.0;
    double lastZero = 0, firstOne = -1;
    for (std::size_t i = 0; i < g.size(); ++i) {
      if (g[i] == 0.0) lastZero = t[i];
      if (g[i] >= 1.0 && firstOne < 0 && t[i] > 0.5) firstOne = t[i];
    }
    INFO(modeName(m) << " zero until " << lastZero << " s, one from " << firstOne << " s");
    CHECK(std::fabs(lastZero - D) <= 0.02 + (m == Mode::J106 ? 0.0042 : 0.0));
    CHECK(std::fabs(firstOne - (D + 0.2)) <= 0.02 + (m == Mode::J106 ? 0.0042 : 0.0));
  }
  // a running arpeggio keeps the delay running (60)
  {
    const double fs = 48000.0;
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[B_LFO_DELAY] = 127;
    r.perf(P_ARP_ON, 1);
    r.perf(P_ARP_RATE, 127);
    r.toBoundary();
    r.noteOn(60);
    r.noteOn(64);
    r.skip(static_cast<long>(2.5 * fs));
    CHECK(r.eng().arpRunning());
    CHECK(r.eng().delayGain() >= 1.0);
    r.skip(static_cast<long>(1.0 * fs));
    CHECK(r.eng().delayGain() >= 1.0);
  }
}

TEST_CASE("AC-E8.5 LFO TRIG ramp (60) and lever push vibrato (106)", "[engine][E8]") {
  {
    const double fs = 48000.0;
    Rig r(fs, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[B_LFO_DELAY] = 64;
    r.perf(P_LFO_TRIG_MODE, 1);
    r.toBoundary();
    r.noteOn(60);
    r.skip(4800);
    CHECK(r.eng().delayGain() == 0.0);
    r.inst->post(Command::press(Button::LfoTrig));
    const long t0 = r.eng().now();
    std::vector<double> t, g;
    const long end = t0 + static_cast<long>(1.5 * fs);
    while (r.eng().now() < end) {
      t.push_back(static_cast<double>(nextControl(r) - t0) / fs);
      g.push_back(r.eng().delayGain());
    }
    double firstOne = -1;
    for (std::size_t i = 0; i < g.size(); ++i)
      if (g[i] >= 1.0 && firstOne < 0) firstOne = t[i];
    INFO("ramp reaches 1 at " << firstOne);
    CHECK(std::fabs(firstOne - 2.0 * 64 / 127.0) <= 0.02);
    r.inst->post(Command::release(Button::LfoTrig));
    r.skip(64);
    CHECK(r.eng().delayGain() == 0.0);
  }
  {
    const double fs = 44100.0;
    Rig r(fs, Mode::J106, 2);
    r.testPatch();
    Patch& p = r.st().edit;
    p.v[B_LFO_DELAY] = 127;
    p.v[B_LFO_RATE] = 13;
    p.v[B_DCO_LFO] = 0;
    r.perf(P_LFO_TRIG_SENS, 64);
    r.toBoundary();
    r.noteOn(69);
    r.skip(static_cast<long>(0.2 * fs));
    Command push;
    push.type = CommandType::Push;
    push.src = Source::Keyboard;
    push.b = 1;
    r.inst->post(push);
    const auto inc = captureOs(r, &EngineHooks::capInc, static_cast<long>(2.0 * fs));
    CHECK(r.eng().delayGain() == 0.0);  // still inside the delay
    const double fo = fs * 2, base = 2e6 / std::round(2e6 / 440.0);
    const double up = cents(*std::max_element(inc.begin(), inc.end()) * fo, base);
    const double down = cents(*std::min_element(inc.begin(), inc.end()) * fo, base);
    INFO("push deviation +" << up << " / " << down);
    CHECK(std::fabs(up - 201.6) <= 3.0);
    CHECK(std::fabs(down + 201.6) <= 3.0);
  }
}
