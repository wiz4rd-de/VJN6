// Panel acceptance tests (core part): AC-P1.1, AC-P2.1, AC-P4.2, AC-P7.1.
#include <catch2/catch_test_macros.hpp>
#include <functional>

#include "state/state.h"
#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }
Snapshot snap(Rig& r) {
  Snapshot s;
  r.inst->snapshot(s);
  return s;
}
}  // namespace

TEST_CASE("AC-P1.1 every patch field and performance control set from the panel changes its field", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    r.toBoundary();
    for (int f = 0; f < kNumFields; ++f) {
      INFO("field " << f);
      const int cur = r.st().edit.v[f];
      const int v = f < kNumSliders ? (cur + 37) % 128 : (sysex::eff(r.st().edit, f) + 1) % switchCount(f);
      r.apply(Command::setField(f, v, Source::Panel));
      CHECK(r.st().edit.v[f] == v);
      r.apply(Command::setField(f, f < kNumSliders ? 127 : switchCount(f) - 1, Source::Panel));
      CHECK(r.st().edit.v[f] == (f < kNumSliders ? 127 : switchCount(f) - 1));
    }
    for (int p = 0; p < P_COUNT; ++p) {
      INFO("perf " << p);
      int lo, hi;
      perfRange(p, lo, hi);
      const int cur = r.st().perf.v[p];
      const int v = cur == hi ? lo : cur + 1;
      r.apply(Command::setPerf(p, v, Source::Panel));
      CHECK(r.st().perf.v[p] == v);
      r.apply(Command::setPerf(p, hi + 5, Source::Panel));  // clamped to its range
      CHECK(r.st().perf.v[p] == hi);
    }
  }
}

TEST_CASE("AC-P1.1 60-mode buttons; 106-only buttons are ignored in 60 mode; chorus OFF/I/II and I+II", "[panel]") {
  Rig r(48000.0, Mode::J60);
  useV31BankLayout(r);
  r.toBoundary();
  auto press = [&](Button b, int i = 0, bool shift = false) { r.apply(Command::press(b, i, shift)); };
  const Patch& e = r.st().edit;
  // no RANGE control in 60 mode (C-2): a RANGE press changes nothing and no RANGE LED is lit
  REQUIRE(e.v[W_RANGE] == 1);
  press(Button::Range, 0);
  press(Button::Range, 2);
  CHECK(e.v[W_RANGE] == 1);
  r.skip(64);
  for (int i = 0; i < 3; ++i) CHECK_FALSE(snap(r).buttonLeds & (1ull << (L_RANGE16 + i)));
  press(Button::Pulse);
  CHECK(e.v[W_PULSE] == 1);
  press(Button::Pulse);
  CHECK(e.v[W_PULSE] == 0);
  press(Button::Saw);
  CHECK(e.v[W_SAW] == 0);
  press(Button::Sub);
  CHECK(e.v[W_SUB] == 0);
  press(Button::Sub);
  CHECK(e.v[W_SUB] == 1);
  // chorus OFF, I, II are radio buttons; I+II is the CHORUS I <-> II combination press (v3.2 C-6)
  REQUIRE(e.v[W_CHORUS] == 0);
  press(Button::ChorusI);
  CHECK(e.v[W_CHORUS] == 1);
  press(Button::ChorusII);
  CHECK(e.v[W_CHORUS] == 2);
  press(Button::ChorusI, 0, true);  // Shift+click I with II lit
  CHECK(e.v[W_CHORUS] == 3);
  press(Button::ChorusII);  // a plain click from I+II selects that single mode
  CHECK(e.v[W_CHORUS] == 2);
  press(Button::ChorusOff);
  CHECK(e.v[W_CHORUS] == 0);
  press(Button::ChorusII, 0, true);  // Shift+click without a lit partner = plain click
  CHECK(e.v[W_CHORUS] == 2);
  press(Button::ChorusOff);
  CHECK(e.v[W_CHORUS] == 0);
  press(Button::Hold);
  CHECK(r.st().perf.v[P_HOLD] == 1);
  press(Button::ArpOn);
  CHECK(r.st().perf.v[P_ARP_ON] == 1);
  press(Button::LfoTrig);
  r.skip(64);
  CHECK(snap(r).lfoTrigHeld);
  r.apply(Command::release(Button::LfoTrig));
  r.skip(64);
  CHECK_FALSE(snap(r).lfoTrigHeld);
  // 106-only buttons do nothing in 60 mode
  const Patch before = r.st().edit;
  const PerfSettings pb = r.st().perf;
  press(Button::Portamento);
  press(Button::Poly2);
  press(Button::Poly1, 0, true);
  press(Button::KeyTranspose);
  press(Button::Group, 1);
  press(Button::Bank, 8);
  press(Button::Bank, 6);
  CHECK(r.st().edit == before);
  for (int p = 0; p < P_COUNT; ++p) CHECK(r.st().perf.v[p] == pb.v[p]);
  CHECK_FALSE(r.core().ktArmed());
  CHECK(r.st().pendingBank == 1);
  CHECK(r.st().pendingGroup == 0);
  press(Button::Bank, 5);
  CHECK(r.st().pendingBank == 5);
}

TEST_CASE("AC-P1.1 106-mode buttons; 60-only buttons are ignored in 106 mode; radio buttons write when different",
          "[panel]") {
  Rig r(48000.0, Mode::J106);
  r.toBoundary();
  r.events();
  auto press = [&](Button b, int i = 0, bool shift = false) { send(r, Command::press(b, i, shift)); };
  const Patch& e = r.st().edit;
  press(Button::ChorusI);
  CHECK(e.v[W_CHORUS] == 1);
  press(Button::ChorusII);
  CHECK(e.v[W_CHORUS] == 2);
  press(Button::ChorusOff);
  CHECK(e.v[W_CHORUS] == 0);
  press(Button::Portamento);
  CHECK(r.st().perf.v[P_PORTA_ON] == 1);
  press(Button::Poly2);
  CHECK(r.st().perf.v[P_KEY_MODE] == 1);
  press(Button::Poly1);
  CHECK(r.st().perf.v[P_KEY_MODE] == 0);
  press(Button::Poly2, 0, true);  // combination press with POLY 1 lit
  CHECK(r.st().perf.v[P_KEY_MODE] == 2);
  press(Button::Poly1, 0, true);  // repeating the active combination does nothing
  CHECK(r.st().perf.v[P_KEY_MODE] == 2);
  press(Button::Poly1);
  CHECK(r.st().perf.v[P_KEY_MODE] == 0);
  press(Button::Group, 1);
  CHECK(r.st().pendingGroup == 1);
  press(Button::Bank, 8);
  CHECK(r.st().pendingBank == 8);
  press(Button::KeyTranspose);
  CHECK(r.core().ktArmed());
  press(Button::KeyTranspose);
  CHECK_FALSE(r.core().ktArmed());
  // 60-only buttons do nothing in 106 mode
  const Patch before = r.st().edit;
  const PerfSettings pb = r.st().perf;
  press(Button::Sub);
  press(Button::Hold);
  press(Button::ArpOn);
  press(Button::LfoTrig);
  CHECK(r.st().edit == before);
  for (int p = 0; p < P_COUNT; ++p) CHECK(r.st().perf.v[p] == pb.v[p]);
  CHECK_FALSE(snap(r).lfoTrigHeld);
  // a radio click writes its value when it differs from the stored field
  send(r, Command::selectSlot(1, Source::Overlay));
  r.events();
  press(Button::Range, 1);  // already 8'
  CHECK_FALSE(r.st().edited);
  CHECK(r.midiOut().empty());
  r.inst->hostParam(W_CHORUS, 3, 0);  // I+II: shown as I in 106 mode
  settle(r);
  r.st().edited = false;
  r.events();
  press(Button::ChorusI);  // the lit Chorus I
  CHECK(e.v[W_CHORUS] == 1);
  CHECK(r.st().edited);
  auto out = r.midiOut();
  REQUIRE(out.size() == 1);
  CHECK(out[0] == paramMsg(0, 0x10, sysex::encodeSw1(r.st().edit)));
  r.inst->hostParam(W_RANGE, 7, 0);  // out of range: read as 16'
  settle(r);
  press(Button::Range, 0);
  CHECK(e.v[W_RANGE] == 0);
}

TEST_CASE("AC-P1.1 (§4.2) a performance setting the active mode does not use has no effect", "[panel]") {
  SECTION("106 mode: HOLD and ARPEGGIO ON written by the host") {
    Rig r(48000.0, Mode::J106);
    r.testPatch();
    r.st().edit.v[B_ENV_R] = 127;
    r.toBoundary();
    r.noteOn(60);
    r.noteOn(64);
    settle(r);
    r.inst->hostParam(kNumFields + P_HOLD, 1, 0);
    r.inst->hostParam(kNumFields + P_ARP_ON, 1, 0);
    r.skip(4800);
    CHECK(heldNotes(r) == std::vector<int>{60, 64});  // nothing released, no arpeggio
    for (int i = 0; i < k::kVoices; ++i) CHECK(Engine::sourceOf(r.eng().voice(i).key) != Engine::kSrcArp);
    r.noteOff(60);
    r.noteOff(64);
    r.skip(4800);
    CHECK(countHeld(r) == 0);  // HOLD does not latch in 106 mode
    // switching to 60 mode: HOLD and ARPEGGIO apply there
    send(r, Command::setSetting(S_MODE, 0));
    r.noteOn(60);
    settle(r);
    r.noteOff(60);
    r.skip(4800);
    CHECK(r.eng().arpRunning());
  }
  SECTION("60 mode: key mode written by the host does not release the voices") {
    Rig r(48000.0, Mode::J60);
    r.toBoundary();
    r.noteOn(60);
    r.noteOn(64);
    settle(r);
    r.inst->hostParam(kNumFields + P_KEY_MODE, 2, 0);
    settle(r);
    CHECK(r.st().perf.v[P_KEY_MODE] == 2);
    CHECK(heldNotes(r) == std::vector<int>{60, 64});
  }
}

TEST_CASE("AC-P2.1 EDITED from every edit source; an unchanged write keeps CLEAN; Manual clears it", "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    r.toBoundary();
    REQUIRE_FALSE(r.st().edited);
    // unchanged values first: host, panel, CC, F0 41 32
    const int freq = r.st().edit.v[B_VCF_FREQ];
    r.inst->hostParam(B_VCF_FREQ, freq, 0);
    settle(r);
    send(r, Command::setField(B_VCF_FREQ, freq, Source::Panel));
    r.cc(74, freq);
    r.sysex(paramMsg(0, 0x05, freq));
    r.sysex(paramMsg(0, 0x10, sysex::encodeSw1(r.st().edit)));
    settle(r);
    CHECK_FALSE(r.st().edited);
    CHECK(countEvents(r.events(), EventType::FieldChanged) == 0);
    // a performance control never sets EDITED
    send(r, Command::setPerf(P_VOLUME, 90, Source::Panel));
    send(r, Command::setPerf(P_BEND_DCO, 10, Source::Panel));
    send(r, Command::press(m == Mode::J106 ? Button::Portamento : Button::Hold));
    r.inst->hostParam(kNumFields + P_PORTA_TIME, 99, 0);
    r.cc(7, 20);
    settle(r);
    CHECK_FALSE(r.st().edited);

    struct Src {
      const char* name;
      std::function<void(int)> write;
    };
    const Src sources[] = {
        {"panel slider", [&](int v) { send(r, Command::setField(B_VCF_RES, v, Source::Panel)); }},
        {"panel switch", [&](int) { send(r, Command::press(Button::Pulse)); }},
        {"host automation", [&](int v) { r.inst->hostParam(B_VCF_RES, v, 0); settle(r); }},
        {"SysEx F0 41 32", [&](int v) { r.sysex(paramMsg(0, 0x06, v)); settle(r); }},
        {"MIDI CC", [&](int v) { r.cc(71, v); settle(r); }},
    };
    int v = 10;
    for (const Src& s : sources) {
      INFO(s.name);
      s.write(v += 7);
      CHECK(r.st().edited);
      CHECK(r.core().led().dp);
      send(r, Command::press(Button::Manual));
      CHECK_FALSE(r.st().edited);
      CHECK(r.st().manual);
    }
  }
}

TEST_CASE("AC-P4.2 a panel input dismisses an LED message and acts; a new message replaces it and restarts 2 s",
          "[panel]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    INFO("mode " << modeName(m));
    Rig r(48000.0, m);
    r.toBoundary();
    // `Pr` from a WRITE under memory protect
    send(r, Command::press(Button::Write));
    send(r, Command::press(Button::PatchBtn, 5));
    REQUIRE(r.core().led().message == LedMessage::Protected);
    CHECK(ledText(r) == "Pr");
    // inputs that are not panel input do not dismiss it
    r.noteOn(60);
    r.inst->hostParam(B_VCF_RES, 40, 0);
    r.cc(74, 50);
    send(r, Command::setPerf(P_PROTECT, 0, Source::Overlay));
    send(r, cmdOf(CommandType::ReleaseAll, Source::System));
    CHECK(r.core().led().message == LedMessage::Protected);
    // a panel input dismisses it, then acts
    send(r, Command::setField(B_VCF_RES, 33, Source::Panel));
    CHECK(r.core().led().message == LedMessage::None);
    CHECK(r.st().edit.v[B_VCF_RES] == 33);
    CHECK(ledText(r) == "11");
    CHECK(r.core().led().dp);
    // `ok` dismissed by WRITE, which then arms
    send(r, Command::press(Button::Write));
    send(r, Command::press(Button::PatchBtn, 5));
    REQUIRE(r.core().led().message == LedMessage::WriteOk);
    send(r, Command::press(Button::Write));
    CHECK(r.core().led().message == LedMessage::None);
    CHECK(r.core().writeArmed());
    send(r, Command::press(Button::Write));
    // a second message replaces the first and restarts its 2 s
    send(r, messageCmd(LedMessage::Format));
    CHECK(ledText(r) == "Er");
    r.skip(48 * 1500);
    CHECK(ledText(r) == "Er");
    r.sysex(dumpMsg(0x31, 0, 0, initPatch()));
    for (int i = 0; i < 1000 && r.core().led().message != LedMessage::Received; ++i) r.skip(1);
    REQUIRE(r.core().led().message == LedMessage::Received);
    const int64_t t0 = r.eng().now() - 1;
    r.skip(48 * 1950 - (r.eng().now() - t0));
    CHECK(ledText(r) == "rc");  // 3.5 s after the first message
    r.skip(48 * 100);
    CHECK(r.core().led().message == LedMessage::None);
    CHECK(ledText(r) == "--");
  }
}

TEST_CASE("AC-P7.1 an empty slot of the default bank equals the R-P7 INIT patch", "[panel]") {
  // R-P7, written out by hand: sliders 0 except VCF FREQ 127, VCA LEVEL 127, SUSTAIN 127; range 8',
  // pulse off, saw on, sub on, chorus off, PWM LFO, polarity normal, VCA ENV, HPF 0; name INIT.
  const uint8_t expect[kNumFields] = {0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 127, 0, 0, 127, 0, 0,  // B0-B15
                                      1, 0, 1, 1, 0, 0, 0, 0, 0};                           // W0-W8
  Rig r(48000.0, Mode::J60);
  for (int slot : {57, 66, 67, 100, 128}) {  // the empty slots of §15.3 (v3.2): 57, 66-128
    INFO("slot " << slot);
    const Slot& s = r.st().bank[slot - 1];
    for (int f = 0; f < kNumFields; ++f) CHECK(s.patch.v[f] == expect[f]);
    CHECK(std::string(s.name.s) == "INIT");
    CHECK(s.patch == initPatch());
  }
  for (int f = 0; f < kNumFields; ++f) CHECK(defaultBank()[127].patch.v[f] == expect[f]);
}

TEST_CASE("AC-U4.1 (core part) 60 mode: CHORUS I <-> CHORUS II combination press gives I+II", "[panel][U4]") {
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  auto press = [&](Button b, bool shift = false) { r.apply(Command::press(b, 0, shift)); };
  auto leds = [&]() { return r.core().buttonLeds(); };
  const Patch& e = r.st().edit;
  r.st().edit.v[W_CHORUS] = 2;  // II lit
  press(Button::ChorusI, true);
  CHECK(e.v[W_CHORUS] == 3);
  CHECK((leds() >> L_CHORUS_I) & 1u);
  CHECK((leds() >> L_CHORUS_II) & 1u);
  CHECK_FALSE((leds() >> L_CHORUS_OFF) & 1u);  // the 60 OFF button has no LED
  CHECK(r.st().edited);
  r.st().edited = false;
  press(Button::ChorusI, true);  // repeating the active combination does nothing
  press(Button::ChorusII, true);
  CHECK(e.v[W_CHORUS] == 3);
  CHECK_FALSE(r.st().edited);
  press(Button::ChorusI);  // a plain click selects I
  CHECK(e.v[W_CHORUS] == 1);
  press(Button::ChorusII, true);  // Shift+click II while I is lit
  CHECK(e.v[W_CHORUS] == 3);
  press(Button::ChorusOff);
  CHECK(e.v[W_CHORUS] == 0);
  CHECK_FALSE((leds() >> L_CHORUS_OFF) & 1u);
  // 106 mode has no chorus combination: Shift+click acts as a plain click (R-U4)
  Rig q(48000.0, Mode::J106);
  q.toBoundary();
  q.st().edit.v[W_CHORUS] = 2;
  q.apply(Command::press(Button::ChorusI, 0, true));
  CHECK(q.st().edit.v[W_CHORUS] == 1);
}
