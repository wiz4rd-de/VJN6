// MIDI receive acceptance tests: AC-M1.1 (core part), AC-M1.2, AC-M1.3, AC-M1.4, AC-M1.5, AC-M2.1.
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "util/analysis.h"
#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }

void pc(Rig& r, int program, int ch = 0) {
  const uint8_t m[2] = {static_cast<uint8_t>(0xC0 | ch), static_cast<uint8_t>(program)};
  r.inst->midi(m, 2, 0);
}
void rt(Rig& r, uint8_t b) { r.inst->midi(&b, 1, 0); }

Snapshot snap(Rig& r) {
  Snapshot s;
  r.inst->snapshot(s);
  return s;
}
}  // namespace

TEST_CASE("AC-M1.1 receive channel 3: omni off accepts only channel 3, omni on accepts both", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int omni : {0, 1}) {
      INFO("mode " << modeName(m) << " omni " << omni);
      Rig r(48000.0, m);
      r.apply(Command::setPerf(P_MIDI_CHANNEL, 3, Source::Overlay));
      r.apply(Command::setPerf(P_OMNI, omni, Source::Overlay));
      r.toBoundary();
      r.noteOn(60, 100, 0);  // channel 1
      r.noteOn(64, 100, 2);  // channel 3
      settle(r);
      CHECK(voiceOf(r, 64) >= 0);
      CHECK((voiceOf(r, 60) >= 0) == (omni == 1));
      // the sounding notes are audible
      const Stereo s = r.render(4800);
      CHECK(peakAbs(s.L) > 0.01);
      // other channel messages follow the same filter (CC74 on channel 1)
      r.cc(74, 5, 0);
      settle(r);
      CHECK((r.st().edit.v[B_VCF_FREQ] == 5) == (omni == 1));
      r.cc(74, 9, 2);
      settle(r);
      CHECK(r.st().edit.v[B_VCF_FREQ] == 9);
    }
  }
}

TEST_CASE("AC-M1.2 patch-field CCs write the stored value in both modes", "[midi]") {
  struct CcCase {
    int cc, value, field, expect;
  };
  const CcCase cases[] = {
      {3, 77, B_LFO_RATE, 77},  {9, 66, B_LFO_DELAY, 66},  {12, 0, W_RANGE, 0},     {12, 42, W_RANGE, 0},
      {12, 43, W_RANGE, 1},     {12, 85, W_RANGE, 1},      {12, 86, W_RANGE, 2},    {12, 100, W_RANGE, 2},
      {13, 55, B_DCO_LFO, 55},  {14, 44, B_DCO_PWM, 44},   {15, 42, W_PWM_SRC, 0},  {15, 43, W_PWM_SRC, 1},
      {15, 85, W_PWM_SRC, 1},   {15, 86, W_PWM_SRC, 2},    {16, 64, W_PULSE, 1},    {16, 63, W_PULSE, 0},
      {17, 0, W_SAW, 0},        {17, 127, W_SAW, 1},       {28, 10, W_SUB, 0},      {28, 64, W_SUB, 1},
      {18, 33, B_SUB_LEVEL, 33}, {19, 22, B_NOISE, 22},    {20, 31, W_HPF, 0},      {20, 32, W_HPF, 1},
      {20, 63, W_HPF, 1},       {20, 64, W_HPF, 2},        {20, 95, W_HPF, 2},      {20, 96, W_HPF, 3},
      {21, 64, W_VCF_POL, 1},   {21, 63, W_VCF_POL, 0},    {25, 64, W_VCA_MODE, 1}, {25, 0, W_VCA_MODE, 0},
      {22, 11, B_VCF_ENV, 11},  {23, 12, B_VCF_LFO, 12},   {24, 13, B_VCF_KYBD, 13}, {74, 14, B_VCF_FREQ, 14},
      {71, 15, B_VCF_RES, 15},  {26, 16, B_VCA_LEVEL, 16}, {73, 17, B_ENV_A, 17},   {75, 18, B_ENV_D, 18},
      {27, 19, B_ENV_S, 19},    {72, 20, B_ENV_R, 20},     {93, 31, W_CHORUS, 0},   {93, 32, W_CHORUS, 1},
      {93, 50, W_CHORUS, 1},    {93, 63, W_CHORUS, 1},     {93, 64, W_CHORUS, 2},   {93, 95, W_CHORUS, 2},
      {93, 96, W_CHORUS, 3},    {93, 100, W_CHORUS, 3},
  };
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    const bool is106 = m == Mode::J106;
    (void)is106;
    Rig r(48000.0, m);
    r.toBoundary();
    r.events();

      for (const CcCase& c : cases) {
        INFO("CC" << c.cc << " = " << c.value);
        const int before = r.st().edit.v[c.field];
        r.cc(c.cc, c.value);
        settle(r);
        CHECK(r.st().edit.v[c.field] == c.expect);
        const auto ev = r.events();
        int changed = 0;
        for (const Event& e : ev)
          if (e.type == EventType::FieldChanged && e.a == c.field) {
            ++changed;
            CHECK(e.b == static_cast<uint8_t>(Origin::Midi));
          }
        CHECK(changed == (before != c.expect ? 1 : 0));
        CHECK(midiOutOf(ev).empty());  // external changes are never transmitted (R-S2)
      }
      }
}

TEST_CASE("AC-M1.2 CC20 64 -> HPF 2, CC93 50 -> I, CC93 100 -> I+II (shown as I in 106), CC12 100 -> 4'", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    const bool is106 = m == Mode::J106;
    (void)is106;
    Rig r(48000.0, m);
    r.toBoundary();
    r.events();

      r.cc(20, 64);
      r.cc(93, 50);
      r.cc(12, 100);
      settle(r);
      CHECK(r.st().edit.v[W_HPF] == 2);
      CHECK(r.st().edit.v[W_CHORUS] == 1);
      CHECK(r.st().edit.v[W_RANGE] == 2);  // 4', stored in both modes
      CHECK(lit(r, L_RANGE4) == is106);      // 60 mode has no RANGE LED (v3.2 C-2)
      r.cc(93, 100);
      settle(r);
      CHECK(r.st().edit.v[W_CHORUS] == 3);  // stored in both modes
      CHECK(lit(r, L_CHORUS_I));            // 60: I and II lit; 106: shown as I
      CHECK(lit(r, L_CHORUS_II) == !is106);
      CHECK(r.eng().chorus().mode() == 1 + (is106 ? 0 : 2));  // rendered as I in 106 mode
      }
}

TEST_CASE("AC-M1.2 performance CCs (CC30, CC86, CC87, CC5, CC65, CC7) per mode", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    const bool is106 = m == Mode::J106;
    (void)is106;
    Rig r(48000.0, m);
    r.toBoundary();
    r.events();

      r.cc(30, 64);
      settle(r);
      CHECK(r.st().perf.v[P_LFO_TRIG_MODE] == (is106 ? 0 : 1));  // ignored in 106 mode
      r.cc(30, 63);
      settle(r);
      CHECK(r.st().perf.v[P_LFO_TRIG_MODE] == 0);
      r.cc(86, 50);
      settle(r);
      CHECK(r.st().perf.v[P_KEY_MODE] == (is106 ? 1 : 0));  // ignored in 60 mode
      r.cc(86, 86);
      settle(r);
      CHECK(r.st().perf.v[P_KEY_MODE] == (is106 ? 2 : 0));
      r.cc(86, 42);
      settle(r);
      CHECK(r.st().perf.v[P_KEY_MODE] == 0);
      r.cc(87, 99);
      settle(r);
      CHECK(r.st().perf.v[P_BEND_DCO] == 99);
      r.cc(5, 77);
      r.cc(65, 64);
      settle(r);
      CHECK(r.st().perf.v[P_PORTA_TIME] == (is106 ? 77 : 32));  // ignored in 60 mode
      CHECK(r.st().perf.v[P_PORTA_ON] == (is106 ? 1 : 0));
      r.cc(65, 63);
      settle(r);
      CHECK(r.st().perf.v[P_PORTA_ON] == 0);
      r.cc(7, 50);
      settle(r);
      CHECK(r.st().perf.v[P_VOLUME] == 50);
      // a performance control never sets EDITED
      CHECK_FALSE(r.st().edited);
      }
}

TEST_CASE("AC-M1.2 notes, velocity 0, pitch bend, CC1, CC64, CC120, CC123", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    const bool is106 = m == Mode::J106;
    (void)is106;
    Rig r(48000.0, m);
    r.toBoundary();
    r.events();

      r.noteOn(60, 1);
      settle(r);
      REQUIRE(voiceOf(r, 60) >= 0);
      r.noteOn(60, 0);  // velocity 0 = off
      settle(r);
      CHECK(voiceOf(r, 60) < 0);
      r.noteOn(62);
      settle(r);
      r.noteOff(62);
      settle(r);
      CHECK(voiceOf(r, 62) < 0);

      r.bend(0);
      settle(r);
      CHECK(r.core().lever() == -1.0f);
      r.bend(8192);
      settle(r);
      CHECK(r.core().lever() == 0.0f);
      r.bend(16383);
      settle(r);
      CHECK(r.core().lever() == 1.0f);
      r.bend(8192 + 4096);
      settle(r);
      CHECK(std::fabs(r.core().lever() - 4096.0f / 8191.0f) < 1e-6f);

      r.cc(1, 64);
      settle(r);
      Snapshot s = snap(r);
      CHECK(s.push == is106);          // 106: lever push
      CHECK(s.lfoTrigHeld == !is106);  // 60: LFO TRIG held
      CHECK(lit(r, L_LFO_TRIG) == !is106);
      r.cc(1, 63);
      settle(r);
      s = snap(r);
      CHECK_FALSE(s.push);
      CHECK_FALSE(s.lfoTrigHeld);

      // CC64 hold pedal
      r.noteOn(64);
      settle(r);
      r.cc(64, 64);
      r.noteOff(64);
      settle(r);
      CHECK(voiceOf(r, 64) >= 0);
      r.cc(64, 63);
      settle(r);
      CHECK(voiceOf(r, 64) < 0);

      // CC123: voices release (still active while releasing)
      Patch p = r.st().edit;
      r.cc(72, 100);  // long release
      r.noteOn(65);
      r.noteOn(67);
      settle(r);
      CHECK(countHeld(r) == 2);
      r.cc(123, 0);
      settle(r);
      CHECK(countHeld(r) == 0);
      CHECK(countActive(r) == 2);
      // CC120: voices stop at once
      r.noteOn(65);
      r.noteOn(67);
      settle(r);
      r.cc(120, 0);
      settle(r);
      CHECK(countHeld(r) == 0);
      CHECK(countActive(r) == 0);
      (void)p;
      }
}

TEST_CASE("AC-M1.2 program change and SysEx", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    const bool is106 = m == Mode::J106;
    (void)is106;
    Rig r(48000.0, m);
    r.toBoundary();
    r.events();

      pc(r, 5);
      settle(r);
      CHECK(r.st().currentSlot == 6);
      CHECK(r.st().edit == r.st().bank[5].patch);
      pc(r, 127);
      settle(r);
      CHECK(r.st().currentSlot == 128);
      Patch p = initPatch();
      p.v[B_VCF_FREQ] = 33;
      r.sysex(dumpMsg(0x31, 0, 0, p));
      settle(r);
      CHECK(r.st().edit.v[B_VCF_FREQ] == 33);
      CHECK(r.st().manual);
      }
}

TEST_CASE("AC-M1.2 an unlisted CC changes nothing", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    const bool is106 = m == Mode::J106;
    (void)is106;
    Rig r(48000.0, m);
    r.toBoundary();
    r.events();

      const Patch before = r.st().edit;
      const PerfSettings perfBefore = r.st().perf;
      for (int c : {0, 2, 4, 6, 8, 10, 11, 29, 31, 32, 63, 66, 67, 70, 76, 80, 85, 88, 91, 94, 100, 101, 121, 122, 124, 127}) {
        r.cc(c, 127);
        r.cc(c, 0);
        r.cc(c, 70);
      }
      settle(r);
      CHECK(r.st().edit == before);
      for (int p = 0; p < P_COUNT; ++p) CHECK(r.st().perf.v[p] == perfBefore.v[p]);
      const auto ev = r.events();
      CHECK(countEvents(ev, EventType::FieldChanged) == 0);
      CHECK(countEvents(ev, EventType::PerfChanged) == 0);
      CHECK_FALSE(r.st().edited);
      }
}

TEST_CASE("AC-M1.2 F8 / FA drive the arpeggio, FB / FC have no effect", "[midi]") {
  Rig r(48000.0, Mode::J60);
  r.apply(Command::setPerf(P_ARP_RATE, 0, Source::Panel));  // 1.5 steps/s: internal steps 667 ms apart
  r.apply(Command::press(Button::ArpOn));
  r.toBoundary();
  r.noteOn(60);
  r.noteOn(67);
  settle(r);
  auto arpNote = [&] {
    for (int i = 0; i < k::kVoices; ++i) {
      const Voice& v = r.eng().voice(i);
      if (v.held && Engine::sourceOf(v.key) == Engine::kSrcArp) return v.note;
    }
    return -1;
  };
  CHECK(arpNote() == 60);
  r.skip(4800);  // 100 ms: still the first step
  CHECK(arpNote() == 60);
  rt(r, 0xFB);
  rt(r, 0xFC);
  settle(r);
  CHECK(arpNote() == 60);  // continue / stop: no effect
  rt(r, 0xF8);             // the first F8 after the notes appear plays a step
  settle(r);
  CHECK(arpNote() == 67);
  // the next steps come every 6th F8
  for (int i = 0; i < 5; ++i) {
    r.skip(1024);
    rt(r, 0xF8);
    settle(r, 64);
  }
  r.skip(1024);
  rt(r, 0xF8);
  settle(r, 64);
  CHECK(arpNote() == 60);
  // FA: the next F8 steps
  r.skip(1024);
  rt(r, 0xF8);
  settle(r, 64);
  r.skip(1024);
  rt(r, 0xFA);
  settle(r, 64);
  rt(r, 0xF8);
  settle(r, 64);
  CHECK(arpNote() == 67);
}

TEST_CASE("AC-M1.3 106 MIDI functions I / II / III gate the message classes", "[midi]") {
  for (int fn = 1; fn <= 3; ++fn) {
    INFO("function " << fn);
    Rig r(48000.0, Mode::J106);
    r.apply(Command::setPerf(P_MIDI_FUNCTION, fn, Source::Overlay));
    r.toBoundary();
    // note: every function
    r.noteOn(60);
    settle(r);
    CHECK(voiceOf(r, 60) >= 0);
    // CC64: every function
    r.cc(64, 127);
    r.noteOff(60);
    settle(r);
    CHECK(voiceOf(r, 60) >= 0);
    // program change: II, III
    pc(r, 9);
    settle(r);
    CHECK((r.st().currentSlot == 10) == (fn >= 2));
    // pitch bend: II, III
    r.bend(16383);
    settle(r);
    CHECK((r.core().lever() == 1.0f) == (fn >= 2));
    // CC1: II, III
    r.cc(1, 127);
    settle(r);
    CHECK(snap(r).push == (fn >= 2));
    // CC74: III
    const int freq = r.st().edit.v[B_VCF_FREQ];
    r.cc(74, freq == 3 ? 4 : 3);
    settle(r);
    CHECK((r.st().edit.v[B_VCF_FREQ] != freq) == (fn == 3));
    // SysEx: III
    Patch p = r.st().edit;
    p.v[B_VCF_RES] = 99;
    r.sysex(dumpMsg(0x31, 0, 0, p));
    settle(r);
    CHECK((r.st().edit.v[B_VCF_RES] == 99) == (fn == 3));
    CHECK(r.st().manual == (fn == 3));
    // CC123: every function (also clears the CC64 hold)
    REQUIRE(countHeld(r) == 1);
    r.cc(123, 0);
    settle(r);
    CHECK(countHeld(r) == 0);
    // CC120: every function
    r.noteOn(62);
    settle(r);
    r.cc(120, 0);
    settle(r);
    CHECK(countActive(r) == 0);
  }
}

TEST_CASE("AC-M1.3 60 mode accepts everything whatever the MIDI function setting", "[midi]") {
  {
    Rig r(48000.0, Mode::J60);
    r.apply(Command::setPerf(P_MIDI_FUNCTION, 1, Source::Overlay));
    r.toBoundary();
    pc(r, 9);
    r.bend(16383);
    r.cc(74, 3);
    settle(r);
    CHECK(r.st().currentSlot == 10);
    CHECK(r.core().lever() == 1.0f);
    CHECK(r.st().edit.v[B_VCF_FREQ] == 3);
    Patch p = r.st().edit;
    p.v[B_VCF_RES] = 99;
    r.sysex(dumpMsg(0x31, 0, 0, p));
    settle(r);
    CHECK(r.st().edit.v[B_VCF_RES] == 99);
  }
}

TEST_CASE("AC-M1.4 velocity to VCA off: identical; on: gain (v/127)^0.5", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    std::vector<float> ref;
    double refPeak[2] = {0, 0};
    for (int on : {0, 1}) {
      for (int vel : {127, 64, 1}) {
        INFO("velocity->VCA " << on << " velocity " << vel);
        Rig r(48000.0, m);
        r.testPatch();
        r.apply(Command::setSetting(S_VELOCITY, on));
        r.toBoundary();
        r.noteOn(60, vel);
        const Stereo s = r.render(24000);
        const int vi = voiceOf(r, 60);
        REQUIRE(vi >= 0);
        const double expect = on ? std::sqrt(vel / 127.0) : 1.0;
        CHECK(std::fabs(r.eng().voice(vi).velGain - expect) < 1e-12);
        const double pk = peakAbs(s.L, 12000);
        if (vel == 127) refPeak[on] = pk;
        if (!on) {
          if (vel == 127) ref = s.L;
          else CHECK(s.L == ref);  // identical renders whatever the velocity
        } else {
          CHECK(std::fabs(pk / refPeak[1] / expect - 1.0) < 0.01);
        }
      }
    }
  }
}

TEST_CASE("AC-M1.4 velocity to VCA applies to every UNISON voice", "[midi]") {
  Rig r(48000.0, Mode::J106);
  r.testPatch();
  r.apply(Command::setSetting(S_VELOCITY, 1));
  r.apply(Command::setPerf(P_KEY_MODE, 2, Source::Panel));
  r.toBoundary();
  r.noteOn(60, 32);
  settle(r);
  for (int i = 0; i < k::kVoices; ++i) CHECK(std::fabs(r.eng().voice(i).velGain - std::sqrt(32 / 127.0)) < 1e-12);
  r.noteOn(64, 100);
  settle(r);
  for (int i = 0; i < k::kVoices; ++i) CHECK(std::fabs(r.eng().voice(i).velGain - std::sqrt(100 / 127.0)) < 1e-12);
  r.noteOff(64);  // return to 60: its own velocity
  settle(r);
  for (int i = 0; i < k::kVoices; ++i) {
    CHECK(r.eng().voice(i).note == 60);
    CHECK(std::fabs(r.eng().voice(i).velGain - std::sqrt(32 / 127.0)) < 1e-12);
  }
}

TEST_CASE("AC-M1.5 CC120 / CC123 / receive-channel change clear notes, the HOLD set and the arpeggio", "[midi]") {
  enum Action { kCC120, kCC123, kChannel, kOmni };
  for (int arp : {0, 1}) {
    for (Action a : {kCC120, kCC123, kChannel, kOmni}) {
      INFO("arpeggio " << arp << " action " << a);
      Rig r(48000.0, Mode::J60);
      r.testPatch();
      r.st().edit.v[B_ENV_R] = 90;  // a release that lasts
      r.apply(Command::setPerf(P_OMNI, 0, Source::Overlay));
      r.apply(Command::press(Button::Hold));
      if (arp) {
        r.apply(Command::setPerf(P_ARP_RATE, 127, Source::Panel));
        r.apply(Command::press(Button::ArpOn));
      }
      r.toBoundary();
      for (int n : {60, 64, 67}) r.noteOn(n);
      settle(r);
      for (int n : {60, 64, 67}) r.noteOff(n);
      r.skip(4800);
      if (arp) {
        CHECK(r.eng().arpRunning());
      } else {
        CHECK(heldNotes(r) == std::vector<int>{60, 64, 67});  // latched by HOLD
      }
      switch (a) {
        case kCC120: r.cc(120, 0); break;
        case kCC123: r.cc(123, 0); break;
        case kChannel: r.inst->post(Command::setPerf(P_MIDI_CHANNEL, 2, Source::Overlay)); break;
        case kOmni: r.inst->post(Command::setPerf(P_OMNI, 1, Source::Overlay)); break;
      }
      const Stereo s = r.render(1024);
      CHECK(countHeld(r) == 0);
      if (a == kCC120) {
        CHECK(countActive(r) == 0);
        // silence at once (after the decimator's group delay)
        CHECK(peakAbs(s.L, 300) == 0.0);
      } else if (!arp) {
        CHECK(countActive(r) == 3);  // released, still sounding their release
      }
      CHECK_FALSE(r.eng().arpRunning());
      // the HOLD set and the arpeggio stay cleared; the HOLD setting is kept
      for (int i = 0; i < 20; ++i) {
        r.skip(2400);
        CHECK(countHeld(r) == 0);
      }
      CHECK(r.st().perf.v[P_HOLD] == 1);
      // a new note latches again (HOLD still on); after a channel change it must be on the new channel
      r.noteOn(62, 100, a == kChannel ? 1 : 0);
      settle(r);
      r.noteOff(62, a == kChannel ? 1 : 0);
      r.skip(4800);
      if (!arp) CHECK(heldNotes(r) == std::vector<int>{62});
      else CHECK(r.eng().arpRunning());
    }
  }
}

TEST_CASE("AC-M1.5 a receive-channel change keeps computer-keyboard keys that are down", "[midi]") {
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  r.apply(Command::press(Button::Hold));
  r.noteOn(60);
  r.inst->post(Command::note(72, 100));
  settle(r);
  r.noteOff(60);
  settle(r);
  CHECK(heldNotes(r) == std::vector<int>{60, 72});
  send(r, Command::setPerf(P_MIDI_CHANNEL, 5, Source::Overlay));
  CHECK(heldNotes(r) == std::vector<int>{72});  // the key is still down
}

TEST_CASE("AC-M2.1 SysEx in 1-byte chunks with F8, a 30-byte SysEx, running status", "[midi]") {
  SECTION("a 24-byte patch in 1-byte chunks with an interleaved F8 loads; the F8 is acted on") {
    Rig r(48000.0, Mode::J60);
    // a running arpeggio shows that the F8 inside the SysEx is acted on (it plays a step at once)
    r.apply(Command::setPerf(P_ARP_RATE, 0, Source::Panel));
    r.apply(Command::press(Button::ArpOn));
    r.toBoundary();
    r.noteOn(60);
    r.noteOn(67);
    settle(r);
    const int v0 = voiceOf(r, 60);
    REQUIRE(v0 >= 0);
    Patch p = initPatch();
    p.v[B_VCF_FREQ] = 21;
    p.v[B_ENV_D] = 99;
    p.v[W_RANGE] = 2;
    p.v[W_PULSE] = 1;
    const auto msg = dumpMsg(0x31, 0, 0, p);
    for (std::size_t i = 0; i < msg.size(); ++i) {
      r.inst->midi(&msg[i], 1, 0);
      if (i == 11) rt(r, 0xF8);
      r.skip(7);
    }
    settle(r);
    CHECK(r.st().edit == p);
    CHECK(r.st().manual);
    CHECK(r.core().led().message == LedMessage::Received);
    CHECK(voiceOf(r, 67) >= 0);  // the F8 played the next step (internal steps are 667 ms apart)
  }
  SECTION("a 30-byte SysEx is discarded") {
    for (Mode m : {Mode::J60, Mode::J106}) {
      Rig r(48000.0, m);
      r.toBoundary();
      const Patch before = r.st().edit;
      Patch p = initPatch();
      p.v[B_VCF_FREQ] = 21;
      auto msg = dumpMsg(0x31, 0, 0, p);
      msg.pop_back();
      for (int i = 0; i < 6; ++i) msg.push_back(0x11);
      msg.push_back(0xF7);
      REQUIRE(msg.size() == 30);
      r.sysex(msg);
      settle(r);
      CHECK(r.st().edit == before);
      CHECK_FALSE(r.st().manual);
      CHECK(r.core().led().message == LedMessage::None);
      // the stream recovers: the next message loads
      r.sysex(dumpMsg(0x31, 0, 0, p));
      settle(r);
      CHECK(r.st().edit.v[B_VCF_FREQ] == 21);
    }
  }
  SECTION("running-status notes all play") {
    for (Mode m : {Mode::J60, Mode::J106}) {
      Rig r(48000.0, m);
      r.toBoundary();
      const uint8_t on[] = {0x90, 60, 100, 64, 100, 0xF8, 67, 100};
      r.inst->midi(on, 3, 0);
      r.inst->midi(on + 3, 2, 0);  // running status across calls
      r.inst->midi(on + 5, 3, 0);  // a realtime byte does not cancel running status
      settle(r);
      CHECK(heldNotes(r) == std::vector<int>{60, 64, 67});
      const uint8_t off[] = {0x80, 60, 0, 64, 0, 67, 0};
      for (uint8_t b : off) r.inst->midi(&b, 1, 0);
      settle(r);
      CHECK(countHeld(r) == 0);
      const uint8_t on0[] = {0x90, 70, 1, 72, 1, 72, 0};  // velocity 0 = note off, also in running status
      r.inst->midi(on0, 7, 0);
      settle(r);
      CHECK(heldNotes(r) == std::vector<int>{70});
    }
  }
}

TEST_CASE("AC-M1.5 a vanished MIDI input releases only its own notes (R-M1)", "[midi]") {
  vjn6::test::Rig r(48000.0, vjn6::Mode::J60, 2);
  r.testPatch();
  r.toBoundary();
  vjn6::Command a;
  a.type = vjn6::CommandType::MidiIn;
  a.src = vjn6::Source::System;
  a.port = 1;
  a.len = 3;
  a.data[0] = 0x90;
  a.data[1] = 60;
  a.data[2] = 100;
  r.inst->post(a);
  vjn6::Command b = a;
  b.port = 2;
  b.data[1] = 64;
  r.inst->post(b);
  r.skip(256);
  int held = 0;
  for (int v = 0; v < 6; ++v) held += r.eng().voice(v).held ? 1 : 0;
  REQUIRE(held == 2);
  vjn6::Command gone;
  gone.type = vjn6::CommandType::MidiPortGone;
  gone.src = vjn6::Source::System;
  gone.port = 1;
  r.inst->post(gone);
  r.skip(256);
  int note60 = 0, note64 = 0;
  for (int v = 0; v < 6; ++v) {
    if (r.eng().voice(v).held && r.eng().voice(v).note == 60) ++note60;
    if (r.eng().voice(v).held && r.eng().voice(v).note == 64) ++note64;
  }
  CHECK(note60 == 0);
  CHECK(note64 == 1);
}
