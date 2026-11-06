// AC-PL3 (host state round trip during playback, render timing, restore keeps an EDITED buffer),
// AC-E1.4 plugin part, AC-M1.1 plugin part, the fallback driver.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "helpers.h"

using namespace vjn6test;

TEST_CASE("AC-PL3 state saved during playback restores into a new instance; renders stay under 50 % of real time",
          "[AC-PL3]") {
  constexpr int kBlock = 256;
  constexpr double kFs = 48000.0;
  auto a = makeProcessor(false, kFs, kBlock);
  TestHost ha(*a, kBlock);
  // a non-default state: 106 mode, slot 4, an edit (EDITED), a performance setting
  a->handleUiMessage(R"({"t":"setting","s":0,"v":1})");
  a->handleUiMessage(R"({"t":"selectSlot","slot":4})");
  a->handleUiMessage(R"({"t":"field","f":5,"v":33})");
  a->handleUiMessage(R"({"t":"perf","p":13,"v":-5})");
  ha.run(4);
  {
    auto s = snapshotOf(*a);
    REQUIRE(s->edited);
    REQUIRE(s->currentSlot == 4);
  }

  std::atomic<bool> stop{false};
  std::atomic<int> saves{0};
  juce::MemoryBlock saved;
  std::mutex savedMutex;
  std::thread saver([&] {
    while (!stop.load()) {
      juce::MemoryBlock m;
      a->getStateInformation(m);
      {
        std::lock_guard<std::mutex> lock(savedMutex);
        saved = m;
      }
      ++saves;
    }
  });

  const double blockSeconds = kBlock / kFs;
  double worst = 0.0;
  juce::MidiBuffer chord;
  for (int n : {48, 55, 60, 64, 67, 72}) {
    const uint8_t on[3] = {0x90, static_cast<uint8_t>(n), 100};
    chord.addEvent(on, 3, 0);
  }
  for (int b = 0; b < 600; ++b) {
    const auto t0 = std::chrono::steady_clock::now();
    ha.process(b == 0 ? chord : juce::MidiBuffer());
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (dt > worst) worst = dt;
  }
  stop = true;
  saver.join();
  REQUIRE(saves.load() > 10);
  INFO("worst render " << worst * 1000.0 << " ms of " << blockSeconds * 1000.0 << " ms");
  CHECK(worst < 0.5 * blockSeconds);
  CHECK(ha.process() > 1e-4);  // still playing

  // restore into a new instance
  auto b = makeProcessor(false, kFs, kBlock);
  TestHost hb(*b, kBlock);
  b->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  hb.run(2);
  CHECK(stateOf(*b) == saved);
  CHECK(stateOf(*a) == saved);
  auto sb = snapshotOf(*b);
  CHECK(sb->edited);
  CHECK(sb->currentSlot == 4);
  CHECK(sb->edit.v[5] == 33);
  CHECK(sb->perf.v[vjn6::P_KEY_TRANSPOSE] == -5);
  CHECK(b->getCurrentProgram() == 3);
  CHECK(b->hostParam(5).nativeValue() == 33);
  CHECK(b->hostParam(38).nativeValue() == -5);
  CHECK(b->getLatencySamples() == a->getLatencySamples());

  // the host's restore call: selecting the current program keeps the EDITED buffer
  b->setCurrentProgram(b->getCurrentProgram());
  hb.run(4);
  auto after = snapshotOf(*b);
  CHECK(after->edited);
  CHECK(after->edit == sb->edit);
  CHECK(stateOf(*b) == saved);
}

TEST_CASE("AC-E1.4 plugin: oversampling 2x -> 4x changes the latency reported to the host", "[AC-E1.4]") {
  auto p = makeProcessor(false, 48000.0, 512);
  TestHost host(*p);
  HostListener listener(*p);
  const int at2x = p->instrument().latencySamples();
  CHECK(at2x > 0);
  CHECK(p->getLatencySamples() == at2x);
  CHECK(host.run(20, midiOf({0x90, 60, 100})) > 1e-4);
  listener.reset();
  p->handleUiMessage(R"({"t":"setting","s":5,"v":4})");
  host.run(2);
  const int at4x = p->instrument().latencySamples();
  CHECK(at4x != at2x);  // the 4x value of the core (DECISIONS: half-band group delays)
  CHECK(p->getLatencySamples() == at4x);
  CHECK(listener.latencyChanges >= 1);
  CHECK(host.run(20) < 1e-5);  // voices stop
}

TEST_CASE("AC-M1.1 plugin: channel 3 with omni off / on", "[AC-M1.1]") {
  auto p = makeProcessor();
  TestHost host(*p);
  p->handleUiMessage(R"({"t":"perf","p":16,"v":3,"src":"overlay"})");
  p->handleUiMessage(R"({"t":"perf","p":17,"v":0,"src":"overlay"})");
  host.run(2);
  auto play = [&](uint8_t channel) {
    const double rms = host.run(20, midiOf({static_cast<uint8_t>(0x90 | channel), 60, 100}));
    host.run(2, midiOf({static_cast<uint8_t>(0x80 | channel), 60, 0}));
    host.run(60);  // release tail
    return rms;
  };
  CHECK(play(0) < 1e-6);   // channel 1 ignored
  CHECK(play(2) > 1e-3);   // channel 3 sounds
  p->handleUiMessage(R"({"t":"perf","p":17,"v":1,"src":"overlay"})");
  host.run(2);
  CHECK(play(0) > 1e-3);
  CHECK(play(2) > 1e-3);
}

TEST_CASE("Fallback driver: with no audio callback the core still applies commands", "[fallback]") {
  auto p = makeProcessor(false, 48000.0, 512, true);
  REQUIRE(!p->isRenderingFromDevice());
  p->handleUiMessage(R"({"t":"field","f":7,"v":99})");
  bool applied = false;
  for (int i = 0; i < 100 && !applied; ++i) {
    juce::Thread::sleep(10);
    p->pollNow();
    applied = snapshotOf(*p)->edit.v[7] == 99;
  }
  CHECK(applied);
  CHECK(p->hostParam(7).nativeValue() == 99);
  // a host that processes takes over; both never render at the same time (guarded)
  TestHost host(*p);
  for (int b = 0; b < 50; ++b) host.step();
  CHECK(p->isRenderingFromDevice());
}
