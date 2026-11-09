// AC-E10.1, AC-E11.3, AC-E11.4, AC-E12.1, AC-E13.1, AC-E13.2, AC-E13.3
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>

#include "engine/engine_util.h"
#include "sysex/sysex.h"

using namespace vjn6;
using namespace vjn6::test;

// ---- allocation hook (AC-E13.2): counts heap allocations while g_inAudio is set
namespace {
thread_local bool g_inAudio = false;
long g_audioAllocs = 0;
}  // namespace
void* operator new(std::size_t n) {
  if (g_inAudio) ++g_audioAllocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  if (g_inAudio) ++g_audioAllocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }

void copyVoiceState(Engine& e, int from, int to) {
  const Voice src = e.voice(from);
  Voice& d = e.voice(to);
  const double uSaw = d.uSaw, uDuty = d.uDuty, uCut = d.uCut, uVca = d.uVca, uEnv = d.uEnv;
  d = src;
  d.uSaw = uSaw;
  d.uDuty = uDuty;
  d.uCut = uCut;
  d.uVca = uVca;
  d.uEnv = uEnv;
}

// The R-E12 generator, recomputed independently.
struct Expected {
  double saw[6], duty[6], cut[6], vca[6], env[6];
};
Expected expectedVariation() {
  Expected e{};
  uint32_t x = 0x564A4E36u;
  auto draw = [&] {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<double>(x >> 8) / 8388608.0 - 1.0;
  };
  for (int v = 0; v < 6; ++v) {
    e.saw[v] = draw() * 0.04;
    e.duty[v] = draw() * 0.015;
    e.cut[v] = draw() * 15.0;
    e.vca[v] = draw() * 0.5;
    e.env[v] = draw() * 0.05;
  }
  return e;
}
}  // namespace

TEST_CASE("AC-E10.1 six in-phase voices and master volume", "[engine][E10]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    double peaks[3];
    const int vols[3] = {127, 64, 0};
    for (int vi = 0; vi < 3; ++vi) {
      const double fs = 48000.0;
      Rig r(fs, m, 2);
      r.testPatch();
      r.perf(P_VOLUME, vols[vi]);
      if (m == Mode::J106) r.perf(P_KEY_MODE, 2);
      r.toBoundary();
      if (m == Mode::J106) {
        r.noteOn(60);
      } else {
        for (int ch = 0; ch < 6; ++ch) r.noteOn(60, 100, ch);
      }
      r.skip(static_cast<long>(3.0 * fs));  // DC blockers settled
      for (int v = 1; v < 6; ++v) copyVoiceState(r.eng(), 0, v);
      r.skip(4800);
      const auto s = r.render(9600);
      const double period = fs / noteHz(60);
      auto edges = jumpPositions(s.L, 0.1 * peakAbs(s.L) + 1e-12, static_cast<std::size_t>(period / 2));
      for (double& e : edges) e -= 0.5;
      peaks[vi] = vols[vi] == 0 ? peakAbs(s.L) : rampAmplitude(s.L, edges, period, 0.02);
    }
    INFO(modeName(m) << " peaks " << peaks[0] << " " << peaks[1] << " " << peaks[2]);
    CHECK(std::fabs(peaks[0] / 0.378 - 1.0) <= 0.01);
    CHECK(std::fabs(db(peaks[1] / (peaks[0] * (64.0 / 127.0) * (64.0 / 127.0)))) <= 0.01);
    CHECK(peaks[2] == 0.0);
  }
}

TEST_CASE("AC-E11.3 portamento", "[engine][E11]") {
  const double fs = 48000.0;
  Rig r(fs, Mode::J106, 2);
  r.testPatch();
  r.perf(P_KEY_MODE, 2);  // UNISON: every new note keeps its voices
  r.perf(P_PORTA_ON, 1);
  r.perf(P_PORTA_TIME, 64);
  r.toBoundary();
  r.noteOn(60);
  r.skip(480);
  CHECK(r.eng().voice(0).porta == 0.0);  // the first note after prepare does not glide
  CHECK(r.eng().voice(0).portaTicks == 0);
  r.skip(4800);
  // 12-semitone glide 60 -> 72
  r.noteOn(72);
  std::vector<double> porta;
  long ticks = 0;
  // the note-on takes effect at the next control tick; the glide starts there
  auto nextTick = [&] {
    const long n = static_cast<long>(r.eng().nextBoundary() - r.eng().now());
    if (n > 0) r.skip(n);
    const long b = static_cast<long>(r.eng().now());
    r.skip(1);  // apply the boundary
    return b;
  };
  long start = 0;
  while (r.eng().voice(0).note != 72) start = nextTick();
  porta.push_back(r.eng().voice(0).porta);
  long endTick = start;
  while (r.eng().voice(0).porta != 0.0 && ticks < 2000) {
    endTick = nextTick();
    porta.push_back(r.eng().voice(0).porta);
    ++ticks;
  }
  r.skip(0);
  const double dur = static_cast<double>(endTick - start) / fs + 0.0042;  // the first tick already moved
  const double expect = 3.0 * (64.0 / 127.0) * (64.0 / 127.0);
  INFO("glide duration " << dur << " expected " << expect);
  CHECK(std::fabs(dur / expect - 1.0) <= 0.03);
  // linear in semitones: constant step
  double step = porta[1] - porta[0], worst = 0;
  for (std::size_t i = 2; i + 1 < porta.size(); ++i) worst = std::max(worst, std::fabs((porta[i] - porta[i - 1]) - step));
  CHECK(worst < 1e-9);
  CHECK(std::fabs(porta.front() - (-12.0 + step)) < 1e-9);  // first tick already stepped
  // all six voices glide together
  for (int v = 1; v < 6; ++v) CHECK(r.eng().voice(v).porta == r.eng().voice(0).porta);
  // a new note mid-glide starts from the current pitch
  r.noteOn(60);
  r.skip(static_cast<long>(0.38 * fs));
  const double current = r.eng().voice(0).note + r.eng().voice(0).porta;
  r.noteOn(84);
  r.skip(static_cast<long>(std::max<int64_t>(1, r.eng().nextBoundary() - r.eng().now())));
  const Voice& v0 = r.eng().voice(0);
  const double startPitch = v0.note + v0.porta - v0.portaStep;  // value before the first step
  INFO("pitch before " << current << " glide starts at " << startPitch);
  CHECK(std::fabs(startPitch - current) <= std::fabs(v0.portaStep) + 1e-9);
}

TEST_CASE("AC-E11.4 bender range and MIDI pitch bend mapping", "[engine][E11]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    const double range = m == Mode::J106 ? 1200.0 : 700.0;
    for (int v : {0, 8192, 16383}) {
      const double fs = 48000.0;
      Rig r(fs, m, 2);
      r.testPatch();
      r.perf(P_BEND_DCO, 127);
      r.toBoundary();
      r.bend(v);
      r.noteOn(69);
      r.skip(9600);
      const auto inc = captureOs(r, &EngineHooks::capInc, 4800);
      const double fo = fs * 2;
      const double lever = std::max(-1.0, std::min(1.0, (v - 8192) / 8191.0));
      double expectHz;
      if (m == Mode::J106) {
        const double lq = std::round(lever * 127.0) / 127.0;
        const double f8 = 440.0 * std::pow(2.0, range * lq / 1200.0);
        expectHz = 2e6 / std::round(2e6 / f8);
      } else {
        expectHz = 440.0 * std::pow(2.0, range * lever / 1200.0);
      }
      const double f = inc.back() * fo;
      INFO(modeName(m) << " bend " << v << " f " << f << " expected " << expectHz << " lever " << lever);
      CHECK(std::fabs(cents(f, expectHz)) <= 2.0);
      if (v == 0) CHECK(std::fabs(cents(f, 440.0) + range) <= 2.0 + (m == Mode::J106 ? 0.5 : 0.0));
      if (v == 16383) CHECK(std::fabs(cents(f, 440.0) - range) <= 2.0 + (m == Mode::J106 ? 0.5 : 0.0));
      if (v == 8192) {
        CHECK(lever == 0.0);
        if (m == Mode::J106) CHECK(f == 2e6 / std::round(2e6 / 440.0) / fo * fo);
      }
    }
  }
}

TEST_CASE("AC-E12.1 voice variation", "[engine][E12]") {
  const Expected ex = expectedVariation();
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int pct : {0, 100}) {
      const double fs = 96000.0;
      const double var = pct / 100.0;
      // ---- saw amplitude, VCA gain, cutoff offset (one render, six voices on note 60)
      Rig r(fs, m, 2);
      r.testPatch();
      r.st().settings.v[S_VARIATION] = static_cast<int16_t>(pct);
      r.st().edit.v[B_VCF_FREQ] = 64;
      r.toBoundary();
      for (int ch = 0; ch < 6; ++ch) r.noteOn(60, 100, ch);
      r.skip(9600);
      for (int v = 1; v < 6; ++v) copyVoiceState(r.eng(), 0, v);  // phases and sub states forced equal
      r.skip(9600);
      std::vector<double> all(static_cast<std::size_t>(480 * 2 * 6 + 64));
      {
        EngineHooks& H = r.eng().hooks;
        H.osPos = 0;
        H.capAllVoices = all.data();
        r.skip(480);
        H.capAllVoices = nullptr;
      }
      std::vector<double> outs[6];
      for (int v = 0; v < 6; ++v)
        for (std::size_t i = 0; i < 960; ++i) outs[v].push_back(all[i * 6 + static_cast<std::size_t>(v)]);
      for (int v = 0; v < 6; ++v) {
        std::vector<double> vin, ph, cut, vcf, vout;
        Rig& q = r;
        captureOs2(q, &EngineHooks::capVcfIn, vin, &EngineHooks::capPhase, ph, 4800, v);
        captureOs2(q, &EngineHooks::capVcfOut, vcf, &EngineHooks::capVoiceOut, vout, 480, v);
        cut = captureOs(q, &EngineHooks::capCut, 64, v);
        // saw amplitude by line fit
        std::vector<double> incs(ph.size(), 0.0);
        for (std::size_t i = 1; i < ph.size(); ++i) incs[i] = ph[i] > ph[i - 1] ? ph[i] - ph[i - 1] : incs[i - 1];
        incs[0] = incs[1];
        auto t = resetTimes(ph, incs);
        for (double& x : t) x += r.eng().blepDelay();
        const double amp = rampAmplitude(vin, t, 1.0 / incs[1], 0.02);
        // cutoff offset in cents relative to the nominal c_cal * fc
        const double ccal = m == Mode::J106 ? k::kCcal106[0] : k::kCcal60[0];
        const double fc = m == Mode::J106 ? 5.53 * std::pow(2.0, 128.0 * 64 / 1143.0) : 28.1 * std::pow(2.0, 10.5 * 64 / 127.0);
        const double cutC = cents(cut.back(), ccal * fc);
        // VCA gain: voice output / VCF output (env = S = 1)
        double num = 0, den = 0;
        for (std::size_t i = 0; i < vcf.size(); ++i) {
          num += vout[i] * vcf[i];
          den += vcf[i] * vcf[i];
        }
        const double vcaDb = db(num / den);
        INFO(modeName(m) << " var " << pct << " voice " << v << " amp " << amp << " cut " << cutC << " c, vca " << vcaDb
                         << " dB");
        CHECK(std::fabs((amp - 1.0) - ex.saw[v] * var) <= 0.0004 + 0.01 * std::fabs(ex.saw[v] * var));
        CHECK(std::fabs(cutC - ex.cut[v] * var) <= 0.01 * 15.0);
        CHECK(std::fabs(vcaDb - ex.vca[v] * var) <= 0.01 * 0.5);
      }
      if (pct == 0) {
        for (int v = 1; v < 6; ++v) CHECK(outs[v] == outs[0]);
      }
      // ---- pulse centre duty and envelope time per voice
      Rig p(fs, m, 2);
      p.testPatch();
      p.st().settings.v[S_VARIATION] = static_cast<int16_t>(pct);
      Patch& pp = p.st().edit;
      pp.v[W_SAW] = 0;
      pp.v[W_PULSE] = 1;
      pp.v[W_PWM_SRC] = 1;
      pp.v[B_DCO_PWM] = 0;
      pp.v[B_ENV_A] = 64;
      p.toBoundary();
      for (int ch = 0; ch < 6; ++ch) p.noteOn(60, 100, ch);
      const double TA = 0.001 + (3.0 - 0.001) * (64 / 127.0) * (64 / 127.0);
      const double TA106 = 0.0015 + (3.0 - 0.0015) * (64 / 127.0) * (64 / 127.0);
      const double T = m == Mode::J106 ? TA106 : TA;
      std::vector<double> envs[6];
      {
        // attack of all voices: capture each voice's envelope in turn would need six renders; instead
        // read the envelope states once per control step
        std::vector<std::vector<double>> trace(6);
        const long end = p.eng().now() + static_cast<long>(T * 1.2 * fs);
        std::vector<double> tAxis;
        while (p.eng().now() < end) {
          p.skip(64);
          tAxis.push_back(static_cast<double>(p.eng().now()));
          for (int v = 0; v < 6; ++v) trace[static_cast<std::size_t>(v)].push_back(p.eng().voice(v).env);
        }
        for (int v = 0; v < 6; ++v) {
          const auto& tr = trace[static_cast<std::size_t>(v)];
          std::size_t k = 0;
          while (k < tr.size() && tr[k] < 1.0) ++k;
          const double tEnd = (tAxis[k] - tAxis[0] + 64) / fs;
          const double expectT = T * (1.0 + ex.env[v] * var);
          INFO(modeName(m) << " var " << pct << " voice " << v << " attack " << tEnd << " expected " << expectT);
          CHECK(std::fabs(tEnd - expectT) <= 0.01 * T * 0.05 + 64.0 / fs + (m == Mode::J106 ? 0.0042 : 0.0));
        }
      }
      p.skip(static_cast<long>(0.2 * fs));
      for (int v = 0; v < 6; ++v) {
        const auto x = captureOs(p, &EngineHooks::capVcfIn, 9600, v);
        const double hi = *std::max_element(x.begin(), x.end()), lo = *std::min_element(x.begin(), x.end());
        const double mid = 0.5 * (hi + lo);
        auto rise = risingCrossings(x, mid);
        std::vector<double> neg(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) neg[i] = -x[i];
        auto fall = risingCrossings(neg, -mid);
        const double period = (rise.back() - rise.front()) / (rise.size() - 1);
        double dsum = 0;
        int dn = 0;
        for (double rr : rise) {
          auto it = std::upper_bound(fall.begin(), fall.end(), rr);
          if (it == fall.end()) break;
          dsum += (*it - rr) / period;
          ++dn;
        }
        const double duty = dsum / dn;
        INFO(modeName(m) << " var " << pct << " voice " << v << " duty " << duty << " expected " << 0.5 + ex.duty[v] * var);
        CHECK(std::fabs(duty - (0.5 + ex.duty[v] * var)) <= 0.01 * 0.015 + 2e-4);
      }
    }
  }
}

TEST_CASE("AC-E13.1 no subnormal state during a 60 s decay", "[engine][E13]") {
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    for (Mode m : {Mode::J60, Mode::J106}) {
      Rig r(fs, m, 2);
      r.testPatch();
      Patch& p = r.st().edit;
      p.v[W_PULSE] = 1;
      p.v[B_VCF_RES] = 100;
      p.v[B_ENV_R] = 60;
      p.v[W_CHORUS] = 1;
      r.st().settings.v[S_CHORUS_NOISE] = 0;
      r.toBoundary();
      for (int n : {48, 55, 60, 64, 67, 72}) r.noteOn(n);
      r.skip(static_cast<long>(0.5 * fs));
      for (int n : {48, 55, 60, 64, 67, 72}) r.noteOff(n);
      long subnormals = 0, checks = 0;
      const long end = static_cast<long>(60.0 * fs);
      for (long done = 0; done < end; done += 4096) {
        r.skip(4096);
        r.eng().visitState([&](double& x) {
          ++checks;
          if (std::fpclassify(x) == FP_SUBNORMAL) ++subnormals;
        });
      }
      INFO(modeName(m) << " fs " << fs << " state checks " << checks);
      CHECK(subnormals == 0);
    }
  }
}

TEST_CASE("AC-E13.2 no allocations, locks or logs in render() and midi()", "[engine][E13]") {
  const double fs = 48000.0;
  Rig r(fs, Mode::J60, 2, 512);
  r.testPatch();
  r.toBoundary();
  std::vector<float> L(512), R(512);
  g_audioAllocs = 0;
  uint8_t dump[24];
  Patch p = initPatch();
  sysex::encodeDump(p, 0x31, 0, 0, dump);
  const uint8_t param[7] = {0xF0, 0x41, 0x32, 0x00, 0x05, 0x40, 0xF7};
  long blocks = static_cast<long>(10.0 * fs / 512);
  for (long b = 0; b < blocks; ++b) {
    // commands from the producer side (not counted: the producer may allocate)
    if (b % 50 == 0) r.inst->post(Command::setField(B_VCF_FREQ, static_cast<int>(b % 128), Source::Panel));
    if (b % 400 == 200) r.inst->post(Command::setSetting(S_MODE, (b / 400) % 2));
    if (b % 300 == 100) r.inst->post(Command::press(Button::Write));
    g_inAudio = true;
    const uint8_t on[3] = {0x90, static_cast<uint8_t>(48 + b % 24), 100};
    const uint8_t off[3] = {0x80, static_cast<uint8_t>(48 + (b + 23) % 24), 0};
    r.inst->midi(on, 3, 10);
    r.inst->midi(off, 3, 100);
    if (b % 37 == 0) r.inst->midi(dump, 24, 200);
    if (b % 11 == 0) r.inst->midi(param, 7, 300);
    r.inst->render(L.data(), R.data(), 512);
    g_inAudio = false;
    r.events();  // consumer side
  }
  INFO("allocations on the audio thread: " << g_audioAllocs);
  CHECK(g_audioAllocs == 0);
  // locks and logs: the audio-thread sources contain no lock or logging call (structural check)
  const char* files[] = {"core/engine/engine.cpp", "core/juno/chorus.cpp", "core/juno/dsp.cpp", "core/juno/vcf.h",
                         "core/midi/parser.h", "core/sysex/sysex.h"};
  const char* banned[] = {"mutex", "lock_guard", "unique_lock", "printf", "std::cout", "std::cerr", "fprintf", "NSLog"};
  for (const char* f : files) {
    std::ifstream in(std::string(VJN6_SOURCE_DIR) + "/" + f);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    for (const char* b : banned) {
      INFO(f << " contains " << b);
      CHECK(text.find(b) == std::string::npos);
    }
  }
}

TEST_CASE("AC-E13.3 host block sizes give bit-identical renders", "[engine][E13]") {
  const double fs = 48000.0;
  const long N = static_cast<long>(1.5 * fs);
  struct Ev {
    long pos;
    int kind;  // 0 midi, 1 command
    std::vector<uint8_t> midi;
    Command cmd;
  };
  std::vector<Ev> evs;
  auto midiEv = [&](long pos, std::vector<uint8_t> m) { evs.push_back({pos, 0, std::move(m), Command{}}); };
  midiEv(0, {0x90, 60, 100});
  midiEv(1000, {0x90, 64, 90});
  midiEv(15001, {0xE0, 0x00, 0x50});
  midiEv(20000, {0x80, 60, 0});
  midiEv(33333, {0xB0, 74, 30});
  midiEv(50000, {0x90, 67, 100});
  evs.push_back({40000, 1, {}, Command::setField(W_CHORUS, 1, Source::Panel)});
  evs.push_back({52011, 1, {}, Command::setField(B_VCF_RES, 90, Source::Panel)});
  std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.pos < b.pos; });
  auto run = [&](int block) {
    Rig r(fs, Mode::J60, 2, 512);
    r.testPatch();
    r.st().settings.v[S_VARIATION] = 50;
    std::vector<float> L(static_cast<std::size_t>(N)), R(static_cast<std::size_t>(N));
    long pos = 0;
    std::size_t next = 0;
    while (pos < N) {
      // commands are posted before the render call at their exact position
      while (next < evs.size() && evs[next].kind == 1 && evs[next].pos == pos) r.inst->post(evs[next++].cmd);
      long end = std::min<long>(N, pos + block);
      for (std::size_t k = next; k < evs.size(); ++k)
        if (evs[k].kind == 1 && evs[k].pos > pos && evs[k].pos < end) {
          end = evs[k].pos;
          break;
        }
      while (next < evs.size() && evs[next].kind == 0 && evs[next].pos < end) {
        r.inst->midi(evs[next].midi.data(), static_cast<int>(evs[next].midi.size()), static_cast<int>(evs[next].pos - pos));
        ++next;
      }
      r.inst->render(L.data() + pos, R.data() + pos, static_cast<int>(end - pos));
      pos = end;
    }
    return std::make_pair(L, R);
  };
  const auto ref = run(64);
  for (int b : {1, 7, 513, 8192}) {
    const auto x = run(b);
    INFO("block " << b);
    CHECK(x.first == ref.first);
    CHECK(x.second == ref.second);
  }
  // n = 0 renders nothing
  Rig r(fs, Mode::J60, 2, 512);
  float l = 1.0f, rr = 2.0f;
  const auto before = r.eng().now();
  r.inst->render(&l, &rr, 0);
  CHECK(r.eng().now() == before);
  CHECK(l == 1.0f);
  CHECK(rr == 2.0f);
}
