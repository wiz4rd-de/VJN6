// Flow acceptance tests (§6): AC-F3.1, AC-F4.1, AC-F4.2, AC-F4.3, AC-F4.4, AC-F5.1, AC-F5.2, AC-F7.1,
// AC-F8.1, AC-F12.1, AC-S2.1.
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }
void press(Rig& r, Button b, int i = 0, bool shift = false) { send(r, Command::press(b, i, shift)); }
// Slot 3 loaded and then edited (EDITED, not Manual).
void editedOnSlot3(Rig& r) {
  press(r, Button::PatchBtn, 3);
  send(r, Command::setField(B_VCF_FREQ, 11, Source::Panel));
  REQUIRE(r.st().currentSlot == 3);
  REQUIRE(r.st().edited);
}
}  // namespace

TEST_CASE("AC-F3.1 MANUAL on an EDITED slot 3 in 106 mode III", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    r.apply(Command::setPerf(P_MIDI_CHANNEL, 4, Source::Overlay));
    r.toBoundary();
    editedOnSlot3(r);
    const Patch edit = r.st().edit;
    const Name name = r.st().editName;
    r.events();
    press(r, Button::Manual);
    CHECK(ledText(r) == "--");
    CHECK_FALSE(r.st().edited);
    CHECK(r.st().manual);
    CHECK(r.st().currentSlot == 3);
    CHECK(r.st().edit == edit);  // the patch bytes are unchanged
    CHECK(r.st().editName == name);
    CHECK(lit(r, L_MANUAL));
    const auto out = r.midiOut();
    if (m == Mode::J106) {
      REQUIRE(out.size() == 1);
      CHECK(out[0] == dumpMsg(0x31, 3, 0, edit));
    } else {
      CHECK(out.empty());
    }
  }
}

TEST_CASE("AC-F4.1 PATCH 7 on an EDITED slot 3 loads slot 7 with its name, CLEAN, LED 17", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    useV31BankLayout(r);
    r.apply(Command::setPerf(P_MIDI_CHANNEL, 2, Source::Overlay));
    r.toBoundary();
    editedOnSlot3(r);
    r.events();
    press(r, Button::PatchBtn, 7);
    CHECK(r.st().currentSlot == 7);
    CHECK(r.st().edit == r.st().bank[6].patch);
    CHECK(r.st().stored == r.st().bank[6].patch);
    CHECK(std::string(r.st().editName.s) == "ORGAN");
    CHECK_FALSE(r.st().edited);
    CHECK_FALSE(r.st().manual);
    CHECK(ledText(r) == "17");
    CHECK(r.st().pendingBank == 1);
    const auto out = r.midiOut();
    if (m == Mode::J106) {
      REQUIRE(out.size() == 1);
      REQUIRE(out[0].size() == 24);
      CHECK(std::vector<uint8_t>(out[0].begin(), out[0].begin() + 5) == std::vector<uint8_t>{0xF0, 0x41, 0x30, 0x01, 0x06});
      CHECK(out[0] == dumpMsg(0x30, 1, 6, r.st().bank[6].patch));
    } else {
      CHECK(out.empty());
    }
  }
}

TEST_CASE("AC-F4.2 60 mode: Shift+click BANK 5 with BANK 1 / BANK 2 lit, then PATCH 2 / PATCH 8", "[panel]") {
  struct Case {
    int bank, patch, slot;
    const char* led;
  };
  for (const Case c : {Case{1, 2, 42, "62"}, Case{2, 8, 56, "78"}}) {
    INFO("BANK " << c.bank);
    Rig r(48000.0, Mode::J60);
    r.toBoundary();
    press(r, Button::Bank, c.bank);
    REQUIRE(lit(r, L_BANK1 + c.bank - 1));
    press(r, Button::Bank, 5, true);
    CHECK(r.st().pendingBank == c.bank + 5);
    press(r, Button::PatchBtn, c.patch);
    CHECK(r.st().currentSlot == c.slot);
    CHECK(r.st().edit == r.st().bank[c.slot - 1].patch);
    CHECK(ledText(r) == c.led);
    CHECK(lit(r, L_BANK1 + c.bank - 1));
    CHECK(lit(r, L_BANK1 + 4));
    CHECK(lit(r, L_PATCH1 + c.patch - 1));
    // repeating the active combination does nothing
    press(r, Button::Bank, 5, true);
    CHECK(r.st().pendingBank == c.bank + 5);
  }
}

TEST_CASE("AC-F4.3 60 mode with current slot 73: digits blank, pending bank 1; BANK 3, PATCH 1 loads slot 17", "[panel]") {
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  send(r, Command::selectSlot(73, Source::Overlay));
  REQUIRE(r.st().currentSlot == 73);
  CHECK(ledText(r) == "  ");
  CHECK(r.st().pendingBank == 1);
  for (int p = 0; p < 8; ++p) CHECK_FALSE(lit(r, L_PATCH1 + p));
  press(r, Button::Bank, 3);
  press(r, Button::PatchBtn, 1);
  CHECK(r.st().currentSlot == 17);
  CHECK(ledText(r) == "31");
  // a program change reaches slots 57-128 in 60 mode too
  const uint8_t pc[2] = {0xC0, 99};
  r.inst->midi(pc, 2, 0);
  settle(r);
  CHECK(r.st().currentSlot == 100);
  CHECK(ledText(r) == "  ");
}

TEST_CASE("AC-F4.4 a sounding voice takes slot 7's patch at the next control boundary, without retrigger", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    useV31BankLayout(r);
    r.st().settings.v[S_VARIATION] = 0;
    r.toBoundary();
    press(r, Button::PatchBtn, 3);  // STRINGS
    r.noteOn(60);
    r.skip(48000);  // in sustain
    const int vi = voiceOf(r, 60);
    REQUIRE(vi >= 0);
    const Voice& v = r.eng().voice(vi);
    REQUIRE(v.stage == 2);
    const uint64_t order = v.onOrder;
    // select slot 7 in the middle of a control step
    r.toBoundary();
    r.skip(5);
    const int64_t boundary = r.eng().nextBoundary();
    r.inst->post(Command::press(Button::PatchBtn, 7));
    std::vector<double> cut(static_cast<std::size_t>((boundary - r.eng().now() + 400) * 4));
    auto& H = r.eng().hooks;
    H.voice = vi;
    H.capCut = cut.data();
    H.osPos = 0;
    const int64_t start = r.eng().now();
    r.skip(static_cast<long>(boundary - start));
    CHECK(r.st().currentSlot == 3);  // not before the boundary
    const double envBefore = v.env;
    r.skip(400);
    H.capCut = nullptr;
    H.voice = -1;
    CHECK(r.st().currentSlot == 7);
    CHECK(v.held);
    CHECK(v.onOrder == order);  // no retrigger
    CHECK(v.stage == 2);
    CHECK(std::fabs(v.env - envBefore) < 0.2);
    // the cutoff (FREQ 72 -> 85, ENV depth 10 -> 0: about +0.4 octave) is constant before the boundary and
  // moves after it
    const int os = r.eng().oversample();
    const long nb = static_cast<long>(boundary - start) * os;
    const double before0 = cut[0], before1 = cut[static_cast<std::size_t>(nb - 1)];
    CHECK(std::fabs(std::log2(before1 / before0)) < 0.01);
    CHECK(std::log2(cut[static_cast<std::size_t>(nb + 300 * os)] / before1) > 0.2);
  }
}

TEST_CASE("AC-F5.1 WRITE, PATCH 5 with protect off / on", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int prot : {0, 1}) {
      INFO("mode " << modeName(m) << " protect " << prot);
      Rig r(48000.0, m);
      useV31BankLayout(r);
      r.apply(Command::setPerf(P_PROTECT, prot, Source::Overlay));
      r.toBoundary();
      press(r, Button::PatchBtn, 2);  // BRASS
      send(r, Command::setField(B_VCF_RES, 77, Source::Panel));
      const Patch edit = r.st().edit;
      const Slot before = r.st().bank[4];
      r.events();
      press(r, Button::Write);
      CHECK(r.core().writeArmed());
      press(r, Button::PatchBtn, 5);
      CHECK_FALSE(r.core().writeArmed());
      if (!prot) {
        CHECK(r.st().bank[4].patch == edit);
        CHECK(std::string(r.st().bank[4].name.s) == "BRASS");
        CHECK(r.st().stored == edit);
        CHECK(r.st().currentSlot == 5);
        CHECK_FALSE(r.st().edited);
        CHECK_FALSE(r.st().manual);
        CHECK(ledText(r) == "ok");
        CHECK(r.core().led().message == LedMessage::WriteOk);
        r.skip(48 * 2100);
        CHECK(ledText(r) == "15");
      } else {
        CHECK(r.st().bank[4].patch == before.patch);
        CHECK(r.st().bank[4].name == before.name);
        CHECK(r.st().currentSlot == 2);
        CHECK(r.st().edited);
        CHECK(ledText(r) == "Pr");
      }
      CHECK(r.midiOut().empty());  // a write transmits nothing
    }
  }
}

TEST_CASE("AC-F5.2 WRITE armed: a note, a BANK press, a slider move, a mode switch", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    useV31BankLayout(r);
    r.toBoundary();
    press(r, Button::Write);
    // a note sounds; WRITE stays armed (MIDI and computer keyboard)
    r.noteOn(60);
    send(r, Command::note(64, 100));
    CHECK(voiceOf(r, 60) >= 0);
    CHECK(voiceOf(r, 64) >= 0);
    CHECK(r.core().writeArmed());
    // a BANK press changes the pending bank; still armed
    press(r, Button::Bank, 3);
    CHECK(r.st().pendingBank == 3);
    CHECK(r.core().writeArmed());
    // host automation and overlays do not cancel either
    r.inst->hostParam(B_VCF_RES, 20, 0);
    send(r, Command::setPerf(P_PROTECT, 0, Source::Overlay));
    CHECK(r.core().writeArmed());
    // a slider move cancels and applies
    send(r, Command::setField(B_VCF_FREQ, 40, Source::Panel));
    CHECK_FALSE(r.core().writeArmed());
    CHECK(r.st().edit.v[B_VCF_FREQ] == 40);
    // a mode switch cancels
    press(r, Button::Write);
    REQUIRE(r.core().writeArmed());
    send(r, Command::setSetting(S_MODE, m == Mode::J106 ? 0 : 1));
    CHECK_FALSE(r.core().writeArmed());
    // the slot was never written
    CHECK(r.st().bank[16].patch == initPatch());
  }
}

TEST_CASE("AC-F7.1 106 key transpose: note 48 sets -12 silently; arming then 60 resets; a slider move disarms", "[panel]") {
  Rig r(48000.0, Mode::J106);
  r.toBoundary();
  r.events();
  press(r, Button::KeyTranspose);
  REQUIRE(r.core().ktArmed());
  CHECK(ledText(r) == "t-");
  r.noteOn(48);
  settle(r);
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == -12);
  CHECK(countActive(r) == 0);  // the note does not sound
  CHECK_FALSE(r.core().ktArmed());
  CHECK(lit(r, L_KEY_TRANSPOSE));
  CHECK(ledText(r) == "11");
  r.noteOff(48);
  r.noteOn(60);  // now plays, transposed
  settle(r);
  CHECK(voiceOf(r, 60) >= 0);
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == -12);
  r.noteOff(60);
  settle(r);
  // arming then note 60 resets to 0 (here from the computer keyboard: consumed, not transmitted)
  press(r, Button::KeyTranspose);
  r.events();
  send(r, Command::note(60, 100));
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == 0);
  CHECK_FALSE(lit(r, L_KEY_TRANSPOSE));
  CHECK(countHeld(r) == 0);
  send(r, Command::note(60, 0));
  CHECK(r.midiOut().empty());
  // notes outside 24-108 are ignored while armed
  press(r, Button::KeyTranspose);
  r.noteOn(20);
  settle(r);
  CHECK(r.core().ktArmed());
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == 0);
  r.noteOn(100);  // clamped to +12
  settle(r);
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == 12);
  // arm, then a slider move disarms and applies
  press(r, Button::KeyTranspose);
  REQUIRE(r.core().ktArmed());
  send(r, Command::setField(B_VCF_RES, 50, Source::Panel));
  CHECK_FALSE(r.core().ktArmed());
  CHECK(r.st().edit.v[B_VCF_RES] == 50);
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == 12);
  // KEY TRANSPOSE while armed disarms without change
  press(r, Button::KeyTranspose);
  press(r, Button::KeyTranspose);
  CHECK_FALSE(r.core().ktArmed());
  CHECK(r.st().perf.v[P_KEY_TRANSPOSE] == 12);
}

TEST_CASE("AC-F8.1 60 HOLD: a chord is held, replaced by the next chord; of 8 keys the newest 6 are latched", "[panel]") {
  Rig r(48000.0, Mode::J60);
  r.testPatch();
  r.st().edit.v[B_ENV_R] = 127;
  r.toBoundary();
  press(r, Button::Hold);
  for (int n : {60, 64, 67}) r.noteOn(n);
  settle(r);
  for (int n : {60, 64, 67}) r.noteOff(n);
  r.skip(4800);
  CHECK(heldNotes(r) == std::vector<int>{60, 64, 67});
  for (int n : {62, 65, 69}) r.noteOn(n);
  settle(r);
  for (int n : {62, 65, 69}) r.noteOff(n);
  r.skip(4800);
  CHECK(heldNotes(r) == std::vector<int>{62, 65, 69});
  // eight keys down together, then released
  for (int i = 0; i < 8; ++i) {
    r.noteOn(48 + 2 * i);
    settle(r, 64);
  }
  for (int i = 0; i < 8; ++i) r.noteOff(48 + 2 * i);
  r.skip(4800);
  CHECK(heldNotes(r) == std::vector<int>{52, 54, 56, 58, 60, 62});
  // HOLD off releases the set
  press(r, Button::Hold);
  CHECK(countHeld(r) == 0);
}

TEST_CASE("AC-F8.1 HOLD on latches the keys that are down at that moment", "[panel]") {
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  r.noteOn(60);
  r.noteOn(64);
  settle(r);
  press(r, Button::Hold);
  r.noteOff(60);
  r.noteOff(64);
  r.skip(4800);
  CHECK(heldNotes(r) == std::vector<int>{60, 64});
  r.noteOn(67);  // a new key while none is down replaces the set
  settle(r);
  CHECK(heldNotes(r) == std::vector<int>{67});
}

TEST_CASE("AC-F12.1 a mode switch with notes held, HOLD latched, arpeggio running and WRITE armed", "[panel]") {
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  send(r, Command::selectSlot(73, Source::Overlay));
  send(r, Command::setField(B_ENV_R, 127, Source::Panel));  // EDITED; a long release
  press(r, Button::Hold);
  r.noteOn(60);
  r.noteOn(64);
  settle(r);
  r.noteOff(64);  // latched by HOLD
  settle(r);
  press(r, Button::ArpOn);
  r.skip(9600);
  REQUIRE(r.eng().arpRunning());
  press(r, Button::Write);
  REQUIRE(r.core().writeArmed());
  REQUIRE(r.st().pendingBank == 1);
  const Patch edit = r.st().edit;
  const Name name = r.st().editName;
  const PerfSettings perf = r.st().perf;
  r.inst->post(Command::setSetting(S_MODE, 1));  // Settings overlay
  r.skip(1);
  r.toBoundary();
  r.skip(256);
  CHECK(r.st().settings.v[S_MODE] == 1);
  CHECK(r.eng().mode() == Mode::J106);
  CHECK(r.st().edit == edit);
  CHECK(r.st().editName == name);
  CHECK(r.st().currentSlot == 73);
  CHECK(r.st().edited);
  CHECK_FALSE(r.st().manual);
  for (int p = 0; p < P_COUNT; ++p) CHECK(r.st().perf.v[p] == perf.v[p]);
  CHECK(r.st().perf.v[P_HOLD] == 1);  // the HOLD setting is kept
  CHECK(countHeld(r) == 0);           // voices released
  CHECK(countActive(r) >= 1);         // their release tails render in the new mode
  CHECK_FALSE(r.eng().arpRunning());
  CHECK_FALSE(r.core().writeArmed());
  CHECK(r.st().pendingBank == 2);  // recomputed: slot 73 = B21
  CHECK(r.st().pendingGroup == 1);
  CHECK(ledText(r) == "21");
  // keys still down do not re-sound; their note-offs change nothing
  r.noteOff(60);
  settle(r);
  CHECK(countHeld(r) == 0);
  // back to 60 mode: the HOLD set is empty, the arpeggio restarts on the next note-on
  send(r, Command::setSetting(S_MODE, 0));
  CHECK(r.st().pendingBank == 1);
  CHECK(countHeld(r) == 0);
  r.noteOn(67);
  settle(r);
  CHECK(r.eng().arpRunning());
}

TEST_CASE("AC-S2.1 external changes keep the slot, Manual and the arming; set EDITED; transmit nothing", "[panel]") {
  auto externalChanges = [](Rig& r, int v) {
    r.inst->hostParam(B_VCF_FREQ, v, 0);
    r.sysex(paramMsg(0, 0x06, v));
    r.cc(73, v);
    r.sysex(paramMsg(0, 0x10, 0x51));
    settle(r);
    CHECK(r.st().edit.v[B_VCF_FREQ] == v);
    CHECK(r.st().edit.v[B_VCF_RES] == v);
    CHECK(r.st().edit.v[B_ENV_A] == v);
    CHECK(r.st().edit.v[W_RANGE] == 0);
  };
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m) << ": Manual with WRITE armed");
    Rig r(48000.0, m);
    r.toBoundary();
    press(r, Button::PatchBtn, 4);
    press(r, Button::Manual);
    press(r, Button::Write);
    REQUIRE(r.st().manual);
    REQUIRE_FALSE(r.st().edited);
    r.events();
    externalChanges(r, 21);
    CHECK(r.st().currentSlot == 4);
    CHECK(r.st().pendingBank == 1);
    CHECK(r.st().manual);
    CHECK(r.core().writeArmed());
    CHECK(r.st().edited);
    CHECK(ledText(r) == "--");
    CHECK(r.midiOut().empty());
  }
  {
    INFO("106 mode with key transpose armed");
    Rig r(48000.0, Mode::J106);
    r.toBoundary();
    press(r, Button::PatchBtn, 4);
    press(r, Button::Bank, 6);
    press(r, Button::KeyTranspose);
    r.events();
    externalChanges(r, 22);
    CHECK(r.st().currentSlot == 4);
    CHECK(r.st().pendingBank == 6);
    CHECK_FALSE(r.st().manual);
    CHECK(r.core().ktArmed());
    CHECK(r.st().edited);
    CHECK(ledText(r) == "t-");
    CHECK(r.midiOut().empty());
  }
}

TEST_CASE("AC-F8.1 (R-E8) replacing a latched HOLD set does not restart the LFO delay; a note after silence does",
          "[panel]") {
  Rig r(48000.0, Mode::J60);
  r.testPatch();
  r.st().edit.v[B_LFO_DELAY] = 127;  // 2 s, AUTO
  r.toBoundary();
  send(r, Command::press(Button::Hold));
  for (int n : {60, 64}) r.noteOn(n);
  settle(r);
  for (int n : {60, 64}) r.noteOff(n);
  r.skip(48000);
  CHECK(r.eng().delayGain() == 0.0);  // counting the delay
  r.skip(48000 * 2);
  REQUIRE(r.eng().delayGain() == 1.0);
  r.noteOn(67);  // no key down: replaces the HOLD set, but voices were held
  settle(r);
  CHECK(heldNotes(r) == std::vector<int>{67});
  CHECK(r.eng().delayGain() == 1.0);
  // after silence the next note-on restarts the delay
  send(r, Command::press(Button::Hold));
  r.noteOff(67);
  r.skip(48000);
  REQUIRE(countHeld(r) == 0);
  r.noteOn(62);
  settle(r);
  CHECK(r.eng().delayGain() == 0.0);
}
