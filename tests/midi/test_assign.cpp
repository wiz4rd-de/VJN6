// Voice assignment, hold and pedal acceptance tests (R-E11): AC-E11.1, AC-E11.2, AC-E11.6, AC-E11.7.
// Engine state probes (voice index, held, active, onOrder, stage) instead of audio analysis.
#include <catch2/catch_test_macros.hpp>

#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
// Test patch with a release byte: 0 = voices free within a few ms, 127 = voices release for seconds.
Rig* makeRig(Mode m, int keyMode, int release) {
  Rig* r = new Rig(48000.0, m);
  r->testPatch();
  r->st().edit.v[B_ENV_R] = static_cast<uint8_t>(release);
  if (m == Mode::J106) r->apply(Command::setPerf(P_KEY_MODE, keyMode, Source::Panel));
  r->toBoundary();
  return r;
}
int play(Rig& r, int note) {
  r.noteOn(note);
  settle(r);
  return voiceOf(r, note);
}
void stop(Rig& r, int note, long wait = 512) {
  r.noteOff(note);
  r.skip(wait);
}
bool releasing(Rig& r, int vi) { return r.eng().voice(vi).active && !r.eng().voice(vi).held; }
}  // namespace

TEST_CASE("AC-E11.1 60 mode rotary assignment: next free voice after the last assigned one", "[assign]") {
  std::unique_ptr<Rig> r(makeRig(Mode::J60, 0, 0));
  std::vector<int> got;
  for (int i = 0; i < 8; ++i) {
    got.push_back(play(*r, 60 + i));
    stop(*r, 60 + i, 2400);  // 50 ms: the voice is free again
    CHECK(countActive(*r) == 0);
  }
  CHECK(got == std::vector<int>{0, 1, 2, 3, 4, 5, 0, 1});
}

TEST_CASE("AC-E11.1 60 mode: free first, then the releasing voice with the earliest note-on, then the earliest held",
          "[assign]") {
  SECTION("a free voice is taken before a releasing one") {
    std::unique_ptr<Rig> r(makeRig(Mode::J60, 0, 127));
    for (int i = 0; i < 5; ++i) CHECK(play(*r, 60 + i) == i);
    stop(*r, 60);
    REQUIRE(releasing(*r, 0));
    CHECK(play(*r, 70) == 5);
  }
  SECTION("releasing voice with the earliest note-on (not the earliest release, not a held voice)") {
    std::unique_ptr<Rig> r(makeRig(Mode::J60, 0, 127));
    for (int i = 0; i < 6; ++i) CHECK(play(*r, 60 + i) == i);
    stop(*r, 64);  // voice 4 released first
    stop(*r, 62);  // voice 2 has the earlier note-on
    const uint64_t order2 = r->eng().voice(2).onOrder;
    CHECK(play(*r, 70) == 2);
    CHECK(r->eng().voice(2).onOrder > order2);
    CHECK(play(*r, 71) == 4);
  }
  SECTION("all six held: the held voice with the earliest note-on") {
    std::unique_ptr<Rig> r(makeRig(Mode::J60, 0, 127));
    for (int i = 0; i < 6; ++i) CHECK(play(*r, 60 + i) == i);
    CHECK(play(*r, 70) == 0);
    CHECK(play(*r, 71) == 1);
    CHECK(heldNotes(*r) == std::vector<int>{62, 63, 64, 65, 70, 71});
  }
}

TEST_CASE("AC-E11.1 106 POLY 1: lowest free voice, then the releasing voice with the earliest note-on, then the earliest held",
          "[assign]") {
  SECTION("lowest-numbered free voice") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 0, 0));
    CHECK(play(*r, 60) == 0);
    CHECK(play(*r, 62) == 1);
    CHECK(play(*r, 64) == 2);
    stop(*r, 62, 2400);
    REQUIRE_FALSE(r->eng().voice(1).active);
    CHECK(play(*r, 65) == 1);
    CHECK(play(*r, 67) == 3);
  }
  SECTION("a free voice before a releasing one") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 0, 127));
    CHECK(play(*r, 60) == 0);
    CHECK(play(*r, 62) == 1);
    stop(*r, 60);
    REQUIRE(releasing(*r, 0));
    CHECK(play(*r, 64) == 2);
  }
  SECTION("7th note: the releasing voice with the earliest note-on") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 0, 127));
    for (int i = 0; i < 6; ++i) CHECK(play(*r, 60 + i) == i);
    stop(*r, 63);
    stop(*r, 61);
    CHECK(play(*r, 70) == 1);
    CHECK(play(*r, 71) == 3);
  }
  SECTION("7th note with all six held: the earliest held") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 0, 127));
    for (int i = 0; i < 6; ++i) CHECK(play(*r, 60 + i) == i);
    CHECK(play(*r, 70) == 0);
    CHECK(play(*r, 71) == 1);
  }
}

TEST_CASE("AC-E11.1 106 POLY 2: the oldest releasing voice even when free voices exist", "[assign]") {
  SECTION("releasing before free") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 1, 127));
    CHECK(play(*r, 60) == 0);
    stop(*r, 60);
    REQUIRE(releasing(*r, 0));
    CHECK(play(*r, 62) == 0);  // voices 1-5 are free
  }
  SECTION("oldest note-on first among releasing voices, then free voices in rotary order") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 1, 127));
    CHECK(play(*r, 60) == 0);
    CHECK(play(*r, 62) == 1);
    CHECK(play(*r, 64) == 2);
    stop(*r, 62);
    stop(*r, 60);
    CHECK(play(*r, 65) == 0);  // voice 0 has the older note-on
    CHECK(play(*r, 67) == 1);
    CHECK(play(*r, 69) == 3);  // nothing releasing: next free voice after the last assigned one
    CHECK(play(*r, 71) == 4);
  }
  SECTION("all six held: the earliest held") {
    std::unique_ptr<Rig> r(makeRig(Mode::J106, 1, 127));
    for (int i = 0; i < 6; ++i) CHECK(play(*r, 60 + i) == i);
    CHECK(play(*r, 70) == 0);
    CHECK(play(*r, 71) == 1);
  }
}

TEST_CASE("AC-E11.1 a stolen voice retriggers from its current level", "[assign]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    std::unique_ptr<Rig> r(makeRig(m, 0, 127));
    r->st().edit.v[B_ENV_A] = 60;
    for (int i = 0; i < 6; ++i) play(*r, 60 + i);
    r->skip(4800);
    const double level = r->eng().voice(0).env;
    REQUIRE(level > 0.05);
    r->noteOn(70);
    settle(*r);
    const Voice& v = r->eng().voice(0);
    CHECK(v.note == 70);
    CHECK(v.stage == 1);
    CHECK(v.env >= level * 0.95);  // the attack starts from the current level, not from 0
  }
}

TEST_CASE("AC-E11.2 UNISON: all six voices on the newest note, return to the most recent held note with a retrigger",
          "[assign]") {
  std::unique_ptr<Rig> r(makeRig(Mode::J106, 2, 0));
  r->st().edit.v[B_ENV_A] = 30;  // 0.17 s attack from sustain 0.31: a retrigger is visible as a new attack stage
  r->st().edit.v[B_ENV_S] = 40;
  auto allOn = [&](int note) {
    for (int i = 0; i < k::kVoices; ++i) {
      const Voice& v = r->eng().voice(i);
      if (!v.held || v.note != note) return false;
    }
    return true;
  };
  play(*r, 60);
  CHECK(allOn(60));
  play(*r, 64);
  CHECK(allOn(64));
  play(*r, 67);
  CHECK(allOn(67));
  r->skip(24000);
  uint64_t order[k::kVoices];
  for (int i = 0; i < k::kVoices; ++i) {
    order[i] = r->eng().voice(i).onOrder;
    REQUIRE(r->eng().voice(i).stage == 2);
  }
  // the top (newest) note released: back to 64, the most recent remaining held note, with a retrigger
  r->noteOff(67);
  settle(*r);
  CHECK(allOn(64));
  for (int i = 0; i < k::kVoices; ++i) {
    CHECK(r->eng().voice(i).onOrder > order[i]);
    CHECK(r->eng().voice(i).stage == 1);
    order[i] = r->eng().voice(i).onOrder;
  }
  // releasing a note that is not sounding changes nothing
  r->noteOff(60);
  settle(*r);
  CHECK(allOn(64));
  for (int i = 0; i < k::kVoices; ++i) CHECK(r->eng().voice(i).onOrder == order[i]);
  // the last key up releases all six
  r->noteOff(64);
  settle(*r);
  CHECK(countHeld(*r) == 0);
}

TEST_CASE("AC-E11.6 60 mode: notes held by CC64, by HOLD, by both; notes 23 and 109 ignored", "[assign]") {
  std::unique_ptr<Rig> rp(makeRig(Mode::J60, 0, 127));
  Rig& r = *rp;
  SECTION("CC64 alone") {
    play(r, 60);
    r.cc(64, 127);
    stop(r, 60);
    CHECK(voiceOf(r, 60) >= 0);
    r.cc(64, 0);
    settle(r);
    CHECK(voiceOf(r, 60) < 0);
  }
  SECTION("HOLD alone") {
    send(r, Command::press(Button::Hold));
    play(r, 62);
    stop(r, 62);
    CHECK(voiceOf(r, 62) >= 0);
    send(r, Command::press(Button::Hold));
    CHECK(voiceOf(r, 62) < 0);
  }
  SECTION("both: CC64 up first, then HOLD off") {
    send(r, Command::press(Button::Hold));
    r.cc(64, 127);
    play(r, 64);
    stop(r, 64);
    r.cc(64, 0);
    settle(r);
    CHECK(voiceOf(r, 64) >= 0);
    send(r, Command::press(Button::Hold));
    CHECK(voiceOf(r, 64) < 0);
  }
  SECTION("both: HOLD off first, then CC64 up") {
    send(r, Command::press(Button::Hold));
    r.cc(64, 127);
    play(r, 65);
    stop(r, 65);
    send(r, Command::press(Button::Hold));
    CHECK(voiceOf(r, 65) >= 0);
    r.cc(64, 0);
    settle(r);
    CHECK(voiceOf(r, 65) < 0);
  }
  SECTION("notes 23 and 109 are ignored including their note-off; 24 and 108 play") {
    play(r, 60);
    for (int n : {23, 109}) {
      r.noteOn(n);
      settle(r);
      CHECK(voiceOf(r, n) < 0);
      CHECK(countActive(r) == 1);
      r.noteOff(n);
      settle(r);
      CHECK(heldNotes(r) == std::vector<int>{60});
    }
    CHECK(play(r, 24) >= 0);
    CHECK(play(r, 108) >= 0);
    stop(r, 24);
    stop(r, 108);
    CHECK(heldNotes(r) == std::vector<int>{60});
  }
}

TEST_CASE("AC-E11.7 key mode change releases all voices; the same note from two sources sounds twice", "[assign]") {
  std::unique_ptr<Rig> rp(makeRig(Mode::J106, 0, 127));
  Rig& r = *rp;
  play(r, 60);
  play(r, 64);
  play(r, 67);
  CHECK(countHeld(r) == 3);
  // POLY 1 -> UNISON (combination press)
  send(r, Command::press(Button::Poly2, 0, true));
  REQUIRE(r.st().perf.v[P_KEY_MODE] == 2);
  CHECK(countHeld(r) == 0);
  CHECK(countActive(r) == 3);  // released, ringing out
  play(r, 72);
  CHECK(countHeld(r) == 6);
  CHECK(heldNotes(r) == std::vector<int>(6, 72));
  // the keys that were down before the change are forgotten: their note-offs change nothing
  stop(r, 64);
  CHECK(countHeld(r) == 6);
  stop(r, 72);
  CHECK(countHeld(r) == 0);

  // back to POLY 1: note 60 from the computer keyboard and from MIDI
  send(r, Command::press(Button::Poly1));
  REQUIRE(r.st().perf.v[P_KEY_MODE] == 0);
  send(r, Command::note(60, 100));
  play(r, 60);
  CHECK(heldNotes(r) == std::vector<int>{60, 60});
  stop(r, 60);  // the MIDI note-off releases only its own voice
  REQUIRE(countHeld(r) == 1);
  const int vi = voiceOf(r, 60);
  CHECK(Engine::sourceOf(r.eng().voice(vi).key) == Engine::kSrcKeyboard);
  send(r, Command::note(60, 0));
  CHECK(countHeld(r) == 0);

  // the same note twice from one source: the voice retriggers; one note-off releases it
  const int a = play(r, 62);
  const uint64_t o = r.eng().voice(a).onOrder;
  CHECK(play(r, 62) == a);
  CHECK(r.eng().voice(a).onOrder > o);
  CHECK(countHeld(r) == 1);
  stop(r, 62);
  CHECK(countHeld(r) == 0);
}
