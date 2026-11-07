// Juno-106 SysEx mapping and receive flows (§4.5, F-11): AC-F11.1, AC-F11.2, AC-F11.3, AC-F11.4.
#include <catch2/catch_test_macros.hpp>

#include "state/state.h"
#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* modeName(Mode m) { return m == Mode::J106 ? "106" : "60"; }
const uint8_t kData[16] = {1, 9, 17, 25, 33, 41, 49, 57, 65, 73, 81, 89, 97, 105, 113, 121};

// sw1 = 0x51 / sw2 = 0x11: range 16', pulse off, saw on, chorus I, PWM MANUAL, VCF normal, VCA ENV, HPF 1.
void checkMapped(const Patch& p) {
  for (int i = 0; i < 16; ++i) CHECK(p.v[i] == kData[i]);
  CHECK(p.v[W_RANGE] == 0);
  CHECK(p.v[W_PULSE] == 0);
  CHECK(p.v[W_SAW] == 1);
  CHECK(p.v[W_CHORUS] == 1);
  CHECK(p.v[W_PWM_SRC] == 1);
  CHECK(p.v[W_VCF_POL] == 0);
  CHECK(p.v[W_VCA_MODE] == 0);
  CHECK(p.v[W_HPF] == 1);
}
// A patch whose switch fields all differ from what sw1 = 0x51 / sw2 = 0x11 set.
Patch opposite() {
  Patch p = initPatch();
  p.v[W_RANGE] = 2;
  p.v[W_PULSE] = 1;
  p.v[W_SAW] = 0;
  p.v[W_CHORUS] = 2;
  p.v[W_PWM_SRC] = 0;
  p.v[W_VCF_POL] = 1;
  p.v[W_VCA_MODE] = 1;
  p.v[W_HPF] = 3;
  return p;
}
}  // namespace

TEST_CASE("AC-F11.1 every §4.5 message with sw1 = 0x51 / sw2 = 0x11; rc for 30/31 only", "[midi]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (uint8_t type : {uint8_t(0x30), uint8_t(0x31)}) {
      INFO("mode " << modeName(m) << " message " << int(type));
      Rig r(48000.0, m);
      const Slot* v31 = useV31BankLayout(r);
      r.apply(Command::setPerf(P_PROTECT, 0, Source::Overlay));
      r.toBoundary();
      r.st().edit = opposite();
      r.st().edit.v[W_SUB] = 0;
      r.sysex(rawDump(type, 7, 5, kData, 0x51, 0x11));  // any channel is accepted
      settle(r);
      checkMapped(r.st().edit);
      CHECK(r.st().edit.v[W_SUB] == 1);  // import of a 30/31 message sets W3 = 1
      CHECK(r.core().led().message == LedMessage::Received);
      CHECK(ledText(r) == "rc");
      if (type == 0x30 && m == Mode::J106) {
        CHECK(r.st().currentSlot == 6);
        CHECK(r.st().bank[5].patch == r.st().edit);
        CHECK(std::string(r.st().bank[5].name.s) == "PLUCK");  // the slot keeps its name
        CHECK(std::string(r.st().editName.s) == "PLUCK");      // and the edit buffer takes it
        CHECK_FALSE(r.st().manual);
      } else {
        CHECK(std::string(r.st().editName.s) == "INIT");  // the edit-buffer name is kept
        CHECK(r.st().currentSlot == 1);
        CHECK(r.st().manual);
        CHECK(r.st().bank[5].patch == v31[5].patch);  // 60 mode: a 30 message never writes a slot
      }
      CHECK_FALSE(r.st().edited);
      CHECK(r.midiOut().empty());  // nothing is sent back
    }
    INFO("mode " << modeName(m) << " message 32");
    Rig r(48000.0, m);
    r.toBoundary();
    r.st().edit = opposite();
    for (int i = 0; i < 16; ++i) r.sysex(paramMsg(i % 16, i, kData[i]));
    r.sysex(paramMsg(3, 0x10, 0x51));
    r.sysex(paramMsg(3, 0x11, 0x11));
    settle(r);
    checkMapped(r.st().edit);
    CHECK(r.core().led().message == LedMessage::None);  // no LED message for 32
    CHECK(r.st().edited);
    CHECK_FALSE(r.st().manual);
    CHECK(r.st().currentSlot == 1);
    // wrong lengths, pp > 0x11 and other manufacturers are ignored
    const Patch before = r.st().edit;
    auto d30 = rawDump(0x30, 0, 3, kData, 0x22, 0x08);
    auto d31 = rawDump(0x31, 0, 0, kData, 0x22, 0x08);
    auto shortDump = d31;
    shortDump.erase(shortDump.begin() + 10);
    auto longParam = paramMsg(0, 0x05, 3);
    longParam.insert(longParam.begin() + 5, 0x01);
    auto shortParam = paramMsg(0, 0x05, 3);
    shortParam.erase(shortParam.begin() + 5);
    auto other = d31;
    other[1] = 0x42;
    for (const auto& msg : {shortDump, longParam, shortParam, paramMsg(0, 0x12, 3), paramMsg(0, 0x7F, 3), other}) {
      r.sysex(msg);
      settle(r);
    }
    CHECK(r.st().edit == before);
    CHECK(r.core().led().message == LedMessage::None);
    (void)d30;
  }
}

TEST_CASE("AC-F11.1 sw1 / sw2 mapping rules: range none or several -> 8', chorus bits, reversed HPF", "[midi]") {
  struct Sw1 {
    uint8_t b;
    int range, pulse, saw, chorus;
  };
  const Sw1 sw1s[] = {{0x01, 0, 0, 0, 2}, {0x02, 1, 0, 0, 2}, {0x04, 2, 0, 0, 2}, {0x00, 1, 0, 0, 2}, {0x07, 1, 0, 0, 2},
                      {0x05, 1, 0, 0, 2}, {0x28, 1, 1, 0, 0}, {0x68, 1, 1, 0, 0}, {0x50, 1, 0, 1, 1}, {0x7F, 1, 1, 1, 0}};
  struct Sw2 {
    uint8_t b;
    int pwm, pol, vca, hpf;
  };
  const Sw2 sw2s[] = {{0x00, 0, 0, 0, 3}, {0x08, 0, 0, 0, 2}, {0x10, 0, 0, 0, 1}, {0x18, 0, 0, 0, 0},
                      {0x07, 1, 1, 1, 3}, {0x60, 0, 0, 0, 3}, {0x7F, 1, 1, 1, 0}};
  Rig r(48000.0, Mode::J106);
  r.toBoundary();
  for (const Sw1& s : sw1s) {
    INFO("sw1 " << int(s.b));
    r.sysex(paramMsg(0, 0x10, s.b));
    settle(r);
    CHECK(r.st().edit.v[W_RANGE] == s.range);
    CHECK(r.st().edit.v[W_PULSE] == s.pulse);
    CHECK(r.st().edit.v[W_SAW] == s.saw);
    CHECK(r.st().edit.v[W_CHORUS] == s.chorus);
  }
  for (const Sw2& s : sw2s) {
    INFO("sw2 " << int(s.b));
    r.sysex(paramMsg(0, 0x11, s.b));
    settle(r);
    CHECK(r.st().edit.v[W_PWM_SRC] == s.pwm);
    CHECK(r.st().edit.v[W_VCF_POL] == s.pol);
    CHECK(r.st().edit.v[W_VCA_MODE] == s.vca);
    CHECK(r.st().edit.v[W_HPF] == s.hpf);
  }
  // canonical export: one-hot range, bit 6 = 0 when chorus is off, sw2 bits 5-6 = 0
  Patch p = initPatch();
  p.v[W_CHORUS] = 0;
  CHECK(sysex::encodeSw1(p) == 0x32);
  p.v[W_RANGE] = 9;  // out of range: read as 16'
  CHECK((sysex::encodeSw1(p) & 7) == 1);
  CHECK(sysex::encodeSw2(initPatch()) == 0x18);
}

TEST_CASE("AC-F11.2 a 60-mode patch with sub off, I+II and PWM ENV through .syx (lossy) and .vjn6 (lossless)", "[midi]") {
  Rig r(48000.0, Mode::J60);
  protectOff(r);
  r.toBoundary();
  Bridge bridge(*r.inst);
  std::vector<std::string> ui;
  Patch p = initPatch();
  p.v[B_SUB_LEVEL] = 90;
  p.v[W_SUB] = 0;
  p.v[W_CHORUS] = 3;
  p.v[W_PWM_SRC] = 2;
  p.v[B_VCF_FREQ] = 66;
  for (int f = 0; f < kNumFields; ++f) send(r, Command::setField(f, p.v[f], Source::Panel), 64);
  REQUIRE(r.st().edit == p);
  r.skip(64);
  std::string name;
  std::vector<uint8_t> syx;
  REQUIRE(bridge.exportData("patchSyx", name, syx));
  REQUIRE(syx.size() == 24);
  CHECK(syx[2] == 0x31);
  CHECK(syx[5 + 15] == 0);              // W3 = 0 is written as d15 = 0
  CHECK((syx[21] & 0x60) == 0x40);      // chorus on, Chorus I
  CHECK((syx[22] & 0x01) == 0x01);      // PWM MANUAL
  CHECK(syx[5 + B_VCF_FREQ] == 66);
  bridge.loadFile("lossy.syx", syx.data(), syx.size(), ui);
  r.skip(512);
  CHECK(r.st().edit.v[B_SUB_LEVEL] == 0);
  CHECK(r.st().edit.v[W_CHORUS] == 1);
  CHECK(r.st().edit.v[W_PWM_SRC] == 1);
  CHECK(r.st().edit.v[W_SUB] == 1);
  CHECK(r.st().edit.v[B_VCF_FREQ] == 66);
  CHECK(std::string(r.st().editName.s) == "lossy");
  // .vjn6 keeps every 60-only value
  for (int f = 0; f < kNumFields; ++f) send(r, Command::setField(f, p.v[f], Source::Panel), 64);
  r.skip(64);
  std::vector<uint8_t> json;
  REQUIRE(bridge.exportData("patchJson", name, json));
  send(r, Command::selectSlot(5, Source::Overlay));
  REQUIRE(r.st().edit != p);
  bridge.loadFile("lossless.vjn6", json.data(), json.size(), ui);
  r.skip(512);
  CHECK(r.st().edit == p);
}

TEST_CASE("AC-F11.3 106 III: F0 41 30 pp 9 with protect on / off", "[midi]") {
  for (int prot : {0, 1}) {
    INFO("protect " << prot);
    Rig r(48000.0, Mode::J106);
    r.apply(Command::setPerf(P_PROTECT, prot, Source::Overlay));
    r.toBoundary();
    send(r, Command::press(Button::PatchBtn, 3));
    send(r, Command::press(Button::Manual));
    r.apply(Command::setPerf(P_PROTECT, prot, Source::Overlay));
    const Slot slot10 = r.st().bank[9];
    r.events();
    r.sysex(rawDump(0x30, 0, 9, kData, 0x51, 0x11));
    settle(r);
    CHECK(r.st().currentSlot == 10);
    checkMapped(r.st().edit);
    CHECK_FALSE(r.st().manual);
    CHECK(r.st().pendingBank == 2);
    CHECK(r.st().pendingGroup == 0);
    CHECK(r.st().editName == slot10.name);  // the edit buffer takes the slot's name
    CHECK(r.core().led().message == LedMessage::Received);
    if (!prot) {
      CHECK(r.st().bank[9].patch == r.st().edit);
      CHECK(r.st().stored == r.st().edit);
      CHECK_FALSE(r.st().edited);
    } else {
      CHECK(r.st().bank[9].patch == slot10.patch);
      CHECK(r.st().stored == slot10.patch);
      CHECK(r.st().edited);
    }
    CHECK(r.midiOut().empty());
    r.skip(48 * 2100);
    CHECK(ledText(r) == "22");
    CHECK(r.core().led().dp == (prot == 1));
  }
  SECTION("function I / II: not processed") {
    for (int fn : {1, 2}) {
      Rig r(48000.0, Mode::J106);
      r.apply(Command::setPerf(P_MIDI_FUNCTION, fn, Source::Overlay));
      r.toBoundary();
      r.sysex(rawDump(0x30, 0, 9, kData, 0x51, 0x11));
      settle(r);
      CHECK(r.st().currentSlot == 1);
      CHECK(r.core().led().message == LedMessage::None);
    }
  }
}

TEST_CASE("AC-F11.4 60 mode: F0 41 32 pp 0x10 = 0x51 changes W0, W1, W2, W4 only; W3 stays 0", "[midi]") {
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  Patch p = opposite();
  p.v[W_RANGE] = 1;
  p.v[W_SUB] = 0;
  p.v[W_CHORUS] = 3;
  p.v[W_PWM_SRC] = 2;
  r.st().edit = p;
  r.events();
  r.sysex(paramMsg(0, 0x10, 0x51));
  settle(r);
  const Patch& e = r.st().edit;
  CHECK(e.v[W_RANGE] == 0);
  CHECK(e.v[W_PULSE] == 0);
  CHECK(e.v[W_SAW] == 1);
  CHECK(e.v[W_CHORUS] == 1);
  CHECK(e.v[W_SUB] == 0);
  for (int f : {W_PWM_SRC, W_VCF_POL, W_VCA_MODE, W_HPF}) CHECK(e.v[f] == p.v[f]);
  for (int f = 0; f < kNumSliders; ++f) CHECK(e.v[f] == p.v[f]);
  // one change event per changed field
  int changed = 0;
  for (const Event& ev : r.events())
    if (ev.type == EventType::FieldChanged) ++changed;
  CHECK(changed == 4);
  // sw2 sets only W5-W8
  const Patch mid = r.st().edit;
  r.sysex(paramMsg(0, 0x11, 0x11));
  settle(r);
  CHECK(e.v[W_PWM_SRC] == 1);
  CHECK(e.v[W_VCF_POL] == 0);
  CHECK(e.v[W_VCA_MODE] == 0);
  CHECK(e.v[W_HPF] == 1);
  for (int f : {W_RANGE, W_PULSE, W_SAW, W_SUB, W_CHORUS}) CHECK(e.v[f] == mid.v[f]);
}
