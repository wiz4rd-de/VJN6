// VJN6 render benchmark (SPEC §9.1, AC-N1.1).
//
// 106 mode, six voices in UNISON (key mode 2, the worst case) playing the default BRASS patch (slot 2
// of the default bank, Chorus I), 2x oversampling, 48 kHz, 10 s of audio rendered in 128-sample blocks;
// the median of 5 runs is reported as % of real time and as microseconds per 128-sample block.
// The same source runs natively and compiled with emcc under Node. Exit status 1 if the median is at
// or above the gate for this build (native 8 %, WASM 35 %) or the instrument is not in the benchmark
// configuration.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "vjn6/instrument.h"

namespace {

using vjn6::Command;
using vjn6::Instrument;
using vjn6::Snapshot;
using vjn6::Source;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 128;
constexpr double kSeconds = 10.0;
constexpr int kRuns = 5;
constexpr int kBrassSlot = 2;
#ifdef __EMSCRIPTEN__
constexpr double kGatePct = 35.0;
constexpr const char* kBuild = "wasm";
#else
constexpr double kGatePct = 8.0;
constexpr const char* kBuild = "native";
#endif

// Puts a fresh instrument into the benchmark configuration through the public command API and
// checks the result in the published snapshot.
bool configure(Instrument& inst, std::vector<float>& L, std::vector<float>& R) {
  inst.post(Command::setSetting(vjn6::S_MODE, 1));
  inst.post(Command::setSetting(vjn6::S_OVERSAMPLE, 2));
  inst.post(Command::setPerf(vjn6::P_KEY_MODE, 2, Source::Overlay));
  inst.post(Command::selectSlot(kBrassSlot, Source::Overlay));
  // 0.1 s settles the commands (they act at the first control boundary).
  for (int i = 0; i < 38; ++i) inst.render(L.data(), R.data(), kBlock);
  vjn6::Event e;
  while (inst.pollEvent(e)) {
  }
  auto s = std::make_unique<Snapshot>();
  inst.snapshot(*s);
  const vjn6::Slot& brass = s->bank[kBrassSlot - 1];
  const bool ok = s->settings.v[vjn6::S_MODE] == 1 && s->settings.v[vjn6::S_OVERSAMPLE] == 2 &&
                  s->perf.v[vjn6::P_KEY_MODE] == 2 && s->currentSlot == kBrassSlot && s->edit == brass.patch &&
                  std::strcmp(brass.name.s, "BRASS") == 0 && s->edit.v[vjn6::W_CHORUS] == 1;
  if (!ok) {
    std::fprintf(stderr,
                 "benchmark configuration not reached: mode %d, oversample %d, key mode %d, slot %d, "
                 "slot 2 name \"%s\", chorus %d\n",
                 s->settings.v[vjn6::S_MODE], s->settings.v[vjn6::S_OVERSAMPLE], s->perf.v[vjn6::P_KEY_MODE],
                 s->currentSlot, brass.name.s, s->edit.v[vjn6::W_CHORUS]);
  }
  return ok;
}

void midi3(Instrument& inst, uint8_t status, int note, int vel) {
  const uint8_t m[3] = {status, static_cast<uint8_t>(note), static_cast<uint8_t>(vel)};
  inst.midi(m, 3, 0);
}

// One run: 10 s of audio; a new note every second (legato, so all six unison voices sound throughout).
// Returns the render time in seconds, or a negative value if the configuration failed.
double run(double& peak) {
  auto inst = std::make_unique<Instrument>(kSampleRate, kBlock);
  std::vector<float> L(kBlock), R(kBlock);
  if (!configure(*inst, L, R)) return -1.0;

  static const int kNotes[] = {48, 55, 52, 60, 53, 57, 50, 59, 55, 48};
  const long blocks = static_cast<long>(kSeconds * kSampleRate) / kBlock;
  const long blocksPerNote = static_cast<long>(kSampleRate) / kBlock;
  int current = -1;
  peak = 0.0;
  double sink = 0.0;

  const auto t0 = std::chrono::steady_clock::now();
  for (long b = 0; b < blocks; ++b) {
    if (b % blocksPerNote == 0) {
      const int next = kNotes[(b / blocksPerNote) % 10];
      midi3(*inst, 0x90, next, 100);
      if (current >= 0 && current != next) midi3(*inst, 0x80, current, 0);
      current = next;
    }
    inst->render(L.data(), R.data(), kBlock);
    sink += static_cast<double>(L[0]) + static_cast<double>(R[kBlock - 1]);
    for (int i = 0; i < kBlock; ++i) peak = std::max(peak, static_cast<double>(std::max(std::abs(L[i]), std::abs(R[i]))));
  }
  const auto t1 = std::chrono::steady_clock::now();
  if (sink != sink) return -1.0;  // NaN in the output
  return std::chrono::duration<double>(t1 - t0).count();
}

}  // namespace

int main() {
  const long blocks = static_cast<long>(kSeconds * kSampleRate) / kBlock;
  std::printf("VJN6 benchmark (%s): 106 mode, UNISON x6, BRASS (slot %d), Chorus I, 2x oversampling, %.0f Hz, %.0f s, "
              "%d-sample blocks, median of %d runs\n",
              kBuild, kBrassSlot, kSampleRate, kSeconds, kBlock, kRuns);
  std::vector<double> times;
  for (int r = 0; r < kRuns; ++r) {
    double peak = 0.0;
    const double t = run(peak);
    if (t < 0.0) {
      std::printf("run %d failed\n", r + 1);
      return 1;
    }
    if (peak <= 0.0) {
      std::printf("run %d produced silence\n", r + 1);
      return 1;
    }
    times.push_back(t);
    std::printf("run %d: %8.2f ms  %6.2f %% of real time  %7.2f us per block  (peak %.3f)\n", r + 1, t * 1e3,
                100.0 * t / kSeconds, 1e6 * t / static_cast<double>(blocks), peak);
  }
  std::sort(times.begin(), times.end());
  const double med = times[kRuns / 2];
  const double pct = 100.0 * med / kSeconds;
  const double usBlock = 1e6 * med / static_cast<double>(blocks);
  const bool pass = pct < kGatePct;
  std::printf("median (%s): %.2f %% of real time, %.2f us per 128-sample block (gate < %.0f %%): %s\n", kBuild, pct,
              usBlock, kGatePct, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
