// Test harness: an Instrument with direct access to the core and engine (test-only interface, §9.4).
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/core.h"
#include "engine/engine.h"
#include "vjn6/instrument.h"

namespace vjn6::test {

struct Stereo {
  std::vector<float> L, R;
};

class Rig {
 public:
  explicit Rig(double fs = 48000.0, Mode mode = Mode::J60, int oversample = 2, int maxBlock = 512)
      : inst(std::make_unique<Instrument>(fs, maxBlock)), fs_(fs) {
    // settings first, then prepare so the engine starts clean in the requested configuration
    core().state().settings.v[S_MODE] = mode == Mode::J106 ? 1 : 0;
    core().state().settings.v[S_OVERSAMPLE] = oversample == 4 ? 4 : 2;
    inst->prepare(fs, maxBlock);
  }

  Core& core() { return inst->core(); }
  Engine& eng() { return inst->core().engine(); }
  CoreState& st() { return inst->core().state(); }
  double fs() const { return fs_; }
  bool is106() { return st().settings.v[S_MODE] == 1; }

  // Applies a command now (tests call this at control boundaries).
  void apply(const Command& c) { core().applyNow(c); }
  void field(int f, int v) { apply(Command::setField(f, v, Source::System)); }
  void perf(int p, int v) { apply(Command::setPerf(p, v, Source::System)); }
  void setting(int s, int v) { apply(Command::setSetting(s, v, Source::System)); }

  // §13 "test patch": INIT with saw on, HPF flat, RES 0, A0 D0 S127 R0, VCA ENV, chorus off,
  // variation 0, velocity off.
  void testPatch() {
    Patch p = initPatch();
    p.v[W_SAW] = 1;
    p.v[W_HPF] = is106() ? 1 : 0;
    p.v[B_VCF_RES] = 0;
    p.v[B_ENV_A] = 0;
    p.v[B_ENV_D] = 0;
    p.v[B_ENV_S] = 127;
    p.v[B_ENV_R] = 0;
    p.v[W_VCA_MODE] = 0;
    p.v[W_CHORUS] = 0;
    st().edit = p;
    st().settings.v[S_VARIATION] = 0;
    st().settings.v[S_VELOCITY] = 0;
  }
  // "Self-osc patch": test patch with saw off, RES 127, KYBD 127, ENV 0, LFO 0, FREQ 38 / 49.
  void selfOscPatch() {
    testPatch();
    Patch& p = st().edit;
    p.v[W_SAW] = 0;
    p.v[B_VCF_RES] = 127;
    p.v[B_VCF_KYBD] = 127;
    p.v[B_VCF_ENV] = 0;
    p.v[B_VCF_LFO] = 0;
    p.v[B_VCF_FREQ] = is106() ? 49 : 38;
  }

  // MIDI helpers, applied at the first control boundary at or after the next render position.
  void noteOn(int note, int vel = 100, int ch = 0) {
    const uint8_t m[3] = {static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), static_cast<uint8_t>(vel)};
    inst->midi(m, 3, 0);
  }
  void noteOff(int note, int ch = 0) {
    const uint8_t m[3] = {static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note), 0};
    inst->midi(m, 3, 0);
  }
  void cc(int c, int v, int ch = 0) {
    const uint8_t m[3] = {static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(c), static_cast<uint8_t>(v)};
    inst->midi(m, 3, 0);
  }
  void bend(int v14, int ch = 0) {
    const uint8_t m[3] = {static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(v14 & 127), static_cast<uint8_t>((v14 >> 7) & 127)};
    inst->midi(m, 3, 0);
  }
  void sysex(const std::vector<uint8_t>& m) { inst->midi(m.data(), static_cast<int>(m.size()), 0); }

  // Renders n samples (any length; chunked by the core).
  Stereo render(long n) {
    Stereo s;
    s.L.resize(static_cast<std::size_t>(n));
    s.R.resize(static_cast<std::size_t>(n));
    long done = 0;
    while (done < n) {
      const int c = static_cast<int>(std::min<long>(n - done, 8192));
      inst->render(s.L.data() + done, s.R.data() + done, c);
      done += c;
    }
    return s;
  }
  void skip(long n) { (void)render(n); }
  // Renders until the engine sits exactly on a control boundary.
  void toBoundary() {
    while (eng().now() != eng().nextBoundary()) {
      const long n = static_cast<long>(eng().nextBoundary() - eng().now());
      skip(n);
    }
  }
  std::vector<Event> events() {
    std::vector<Event> v;
    Event e;
    while (inst->pollEvent(e)) v.push_back(e);
    return v;
  }
  std::vector<std::vector<uint8_t>> midiOut() {
    std::vector<std::vector<uint8_t>> v;
    for (const Event& e : events())
      if (e.type == EventType::MidiOut) v.emplace_back(e.data, e.data + e.len);
    return v;
  }

  std::unique_ptr<Instrument> inst;

 private:
  double fs_;
};

}  // namespace vjn6::test
