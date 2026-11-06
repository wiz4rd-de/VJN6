// AC-S1.1 (five paths, identical state, one event per change), AC-S3.1 (host round trip on slot
// selection), R-M3 plugin MIDI output.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "helpers.h"
#include "sysex/sysex.h"

using namespace vjn6test;

namespace {
int ccFor(int f) {
  static const int map[25] = {3, 9, 13, 14, 19, 74, 71, 22, 23, 24, 26, 73, 75, 27, 72, 18,
                              12, 16, 17, 28, 93, 15, 21, 25, 20};
  return map[f];
}

int ccValue(int f, int v) {
  if (f < vjn6::kNumSliders) return v;
  switch (f) {
    case vjn6::W_RANGE:
    case vjn6::W_PWM_SRC: return v == 0 ? 0 : (v == 1 ? 64 : 127);
    case vjn6::W_CHORUS:
    case vjn6::W_HPF: return v * 32;
    default: return v != 0 ? 127 : 0;
  }
}

// Values every path can express: sliders 0-127, switches in their 106 range (W3 reads 1 in 106 mode).
int drawValue(std::mt19937& rng, int f) {
  const uint32_t r = rng();
  if (f < vjn6::kNumSliders) return static_cast<int>(r % 128u);
  switch (f) {
    case vjn6::W_RANGE: return static_cast<int>(r % 3u);
    case vjn6::W_SUB: return 1;
    case vjn6::W_CHORUS: return static_cast<int>(r % 3u);
    case vjn6::W_HPF: return static_cast<int>(r % 4u);
    default: return static_cast<int>(r % 2u);
  }
}

juce::MidiBuffer sysexFor(const vjn6::Patch& current, int f, int v) {
  vjn6::Patch target = current;
  target.v[f] = static_cast<uint8_t>(v);
  int pp, vv;
  if (f < vjn6::kNumSliders) {
    pp = f;
    vv = v;
  } else if (f == vjn6::W_RANGE || f == vjn6::W_PULSE || f == vjn6::W_SAW || f == vjn6::W_CHORUS) {
    pp = 0x10;
    vv = vjn6::sysex::encodeSw1(target);
  } else if (f == vjn6::W_SUB) {
    return {};  // no SysEx form; the drawn value always equals the current one (1)
  } else {
    pp = 0x11;
    vv = vjn6::sysex::encodeSw2(target);
  }
  return midiOf({0xF0, 0x41, 0x32, 0x00, static_cast<uint8_t>(pp), static_cast<uint8_t>(vv), 0xF7});
}

struct Path {
  std::unique_ptr<VJN6AudioProcessor> proc;
  std::unique_ptr<TestHost> host;
  std::unique_ptr<HostListener> listener;
  int fieldEvents = 0;
};

void runFivePaths(bool mode106) {
  std::array<Path, 5> paths;
  for (auto& p : paths) {
    p.proc = makeProcessor();
    p.host = std::make_unique<TestHost>(*p.proc);
    p.listener = std::make_unique<HostListener>(*p.proc);
    auto* counter = &p.fieldEvents;
    p.proc->eventObserver = [counter](const vjn6::Event& e) {
      if (e.type == vjn6::EventType::FieldChanged) ++*counter;
    };
    if (mode106) p.proc->postCommand(vjn6::Command::setSetting(vjn6::S_MODE, 1));
    p.host->run(2);
    p.fieldEvents = 0;
    p.listener->reset();
  }
  vjn6::Patch expected = snapshotOf(*paths[0].proc)->edit;

  std::mt19937 rng(1);
  int changes = 0;
  for (int round = 0; round < 8; ++round) {
    for (int f = 0; f < vjn6::kNumFields; ++f) {
      const int v = drawValue(rng, f);
      const bool change = expected.v[f] != v;
      INFO("round " << round << " field " << f << " value " << v);

      // 1. core command
      paths[0].proc->postCommand(vjn6::Command::setField(f, v, vjn6::Source::Overlay));
      // 2. panel (Bridge JSON from the UI)
      paths[1].proc->handleUiMessage(juce::String(R"({"t":"field","f":)") + juce::String(f) + ",\"v\":" + juce::String(v) + "}");
      // 3. SysEx F0 41 32 through the host's MIDI input
      const juce::MidiBuffer syx = sysexFor(snapshotOf(*paths[2].proc)->edit, f, v);
      // 4. MIDI CC through the host's MIDI input
      const juce::MidiBuffer cc = midiOf({0xB0, static_cast<uint8_t>(ccFor(f)), static_cast<uint8_t>(ccValue(f, v))});
      // 5. host automation on the real AudioProcessor parameter
      auto& hp = paths[4].proc->hostParam(f);
      hp.parameter().setValue(hp.normalisedFor(v));

      for (int i = 0; i < 5; ++i) {
        const int before = paths[static_cast<size_t>(i)].fieldEvents;
        paths[static_cast<size_t>(i)].host->process(i == 2 ? syx : (i == 3 ? cc : juce::MidiBuffer()));
        paths[static_cast<size_t>(i)].proc->pollNow();
        CHECK(paths[static_cast<size_t>(i)].fieldEvents - before == (change ? 1 : 0));
      }
      expected.v[f] = static_cast<uint8_t>(v);
      if (change) ++changes;
      for (auto& p : paths) CHECK(snapshotOf(*p.proc)->edit == expected);
    }
  }
  REQUIRE(changes > 100);
  // identical core state on all five paths
  for (auto& p : paths) {
    auto s = snapshotOf(*p.proc);
    CHECK(s->edit == expected);
    CHECK(s->edited);
    CHECK(s->currentSlot == 1);
    CHECK(!s->manual);
  }
  // R-S3: changes from the panel, core, SysEx and CC are reported to the host once each; host
  // automation is not reported back.
  for (int i = 0; i < 4; ++i) CHECK(paths[static_cast<size_t>(i)].listener->total() == changes);
  CHECK(paths[4].listener->total() == 0);
  for (int f = 0; f < vjn6::kNumFields; ++f)
    for (auto& p : paths) CHECK(p.proc->hostParam(f).nativeValue() == expected.v[f]);
}
}  // namespace

TEST_CASE("AC-S1.1 command, panel, SysEx, CC and host parameter give identical state, one event per change (60)",
          "[AC-S1.1]") {
  runFivePaths(false);
}

TEST_CASE("AC-S1.1 command, panel, SysEx, CC and host parameter give identical state, one event per change (106)",
          "[AC-S1.1]") {
  runFivePaths(true);
}

namespace {
void checkSlotSelection(const std::function<void(VJN6AudioProcessor&)>& select, int slot, bool hostOrigin) {
  auto p = makeProcessor();
  TestHost host(*p);
  HostListener listener(*p);
  int fieldEvents = 0, hostEvents = 0;
  p->eventObserver = [&](const vjn6::Event& e) {
    if (e.type != vjn6::EventType::FieldChanged && e.type != vjn6::EventType::PerfChanged) return;
    ++fieldEvents;
    if (static_cast<vjn6::Origin>(e.b) == vjn6::Origin::Host) ++hostEvents;
  };
  host.run(2);
  listener.reset();
  fieldEvents = hostEvents = 0;
  const auto before = snapshotOf(*p);
  const vjn6::Patch from = before->edit, to = before->bank[slot - 1].patch;
  int changed = 0;
  for (int f = 0; f < vjn6::kNumFields; ++f) changed += from.v[f] != to.v[f] ? 1 : 0;
  REQUIRE(changed > 5);

  select(*p);
  host.run(static_cast<int>(2.0 * 48000 / 512) + 4);  // the selection, then 2 s

  CHECK(snapshotOf(*p)->currentSlot == slot);
  for (int i = 0; i < 40; ++i) {
    INFO("host parameter " << i);
    const bool fieldChanged = i < vjn6::kNumFields && from.v[i] != to.v[i];
    CHECK(listener.changes[static_cast<size_t>(i)] == (fieldChanged ? 1 : 0));
    if (i < vjn6::kNumFields) CHECK(p->hostParam(i).nativeValue() == to.v[i]);
  }
  CHECK(listener.total() == changed);
  CHECK(fieldEvents == changed);                    // nothing re-applied to the core
  CHECK(hostEvents == (hostOrigin ? changed : 0));  // host programs act as host (no transmit)
}
}  // namespace

TEST_CASE("AC-S3.1 slot selection: each changed host parameter notified once, nothing more within 2 s", "[AC-S3.1]") {
  SECTION("bank manager double-click") {
    checkSlotSelection([](VJN6AudioProcessor& p) { p.handleUiMessage(R"({"t":"selectSlot","slot":2})"); }, 2, false);
  }
  SECTION("PATCH button") {
    checkSlotSelection([](VJN6AudioProcessor& p) { p.handleUiMessage(R"({"t":"press","b":"patch","i":3})"); }, 3, false);
  }
  SECTION("MIDI program change") {
    checkSlotSelection(
        [](VJN6AudioProcessor& p) {
          TestHost h(p);
          h.process(midiOf({0xC0, 4}));
        },
        5, false);
  }
  SECTION("host program") {
    checkSlotSelection([](VJN6AudioProcessor& p) { p.setCurrentProgram(5); }, 6, true);
  }
}

TEST_CASE("Host writes are applied once and never reported back (R-S3)", "[R-S3]") {
  auto p = makeProcessor();
  TestHost host(*p);
  HostListener listener(*p);
  int events = 0;
  p->eventObserver = [&](const vjn6::Event& e) {
    if (e.type == vjn6::EventType::FieldChanged) ++events;
  };
  auto& hp = p->hostParam(vjn6::B_VCF_FREQ);
  for (int v = 10; v < 60; ++v) {
    hp.parameter().setValue(hp.normalisedFor(v));
    host.step();
  }
  host.run(50);
  CHECK(events == 50);
  CHECK(listener.total() == 0);
  CHECK(snapshotOf(*p)->edit.v[vjn6::B_VCF_FREQ] == 59);
  // an unchanged host write has no effect
  hp.parameter().setValue(hp.normalisedFor(59));
  host.run(4);
  CHECK(events == 50);
}

TEST_CASE("R-M3 plugin: panel edits in 106 mode go to the host's MIDI output; host automation does not", "[R-M3]") {
  auto p = makeProcessor();
  TestHost host(*p);
  p->handleUiMessage(R"({"t":"setting","s":0,"v":1})");
  host.run(2);
  p->handleUiMessage(R"({"t":"field","f":5,"v":50})");
  int found = 0;
  // the event reaches processBlock's MIDI output through the consumer within a few blocks
  for (int b = 0; b < 6; ++b) {
    host.step();
    for (const auto meta : host.outMidi) {
      const std::vector<uint8_t> m(meta.data, meta.data + meta.numBytes);
      if (m == std::vector<uint8_t>{0xF0, 0x41, 0x32, 0x00, 0x05, 50, 0xF7}) ++found;
    }
  }
  CHECK(found == 1);

  auto& hp = p->hostParam(vjn6::B_VCF_FREQ);
  hp.parameter().setValue(hp.normalisedFor(90));
  int out = 0;
  for (int b = 0; b < 6; ++b) {
    host.step();
    out += host.outMidi.getNumEvents();
  }
  CHECK(out == 0);
  CHECK(snapshotOf(*p)->edit.v[vjn6::B_VCF_FREQ] == 90);
}
