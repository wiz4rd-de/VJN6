// MIDI transmit acceptance tests (R-M3): AC-M3.1, AC-M3.2, AC-M3.3, AC-M3.4.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "util/core_helpers.h"
#include "vjn6/display.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
struct TimedMsg {
  int64_t pos;
  std::vector<uint8_t> m;
};

// Renders n samples one control step at a time and collects the transmitted messages with the position
// of the control boundary that sent them (messages are only sent at control boundaries).
void runCollect(Rig& r, long n, std::vector<TimedMsg>& out) {
  const int64_t end = r.eng().now() + n;
  while (r.eng().now() < end) {
    const int64_t now = r.eng().now();
    const int64_t nb = r.eng().nextBoundary();
    if (nb == now) {
      r.skip(1);
    } else {
      r.skip(static_cast<long>(std::min(nb, end) - now));
    }
    for (auto& m : r.midiOut()) out.push_back({now, m});
  }
}

int bendValue(const std::vector<uint8_t>& m) { return m[1] | (m[2] << 7); }
bool isBend(const std::vector<uint8_t>& m) { return m.size() == 3 && (m[0] & 0xF0) == 0xE0; }
bool isCC1(const std::vector<uint8_t>& m) { return m.size() == 3 && (m[0] & 0xF0) == 0xB0 && m[1] == 1; }
}  // namespace

TEST_CASE("AC-M3.1 106 III: a slider move sends one F0 41 32, a 1 s drag <= 101 ending on the final value", "[midi]") {
  Rig r(48000.0, Mode::J106);
  r.apply(Command::setPerf(P_MIDI_CHANNEL, 5, Source::Overlay));
  r.toBoundary();
  r.events();
  // one slider move
  send(r, Command::setField(B_VCF_FREQ, 50, Source::Panel));
  r.skip(4800);
  auto out = r.midiOut();
  REQUIRE(out.size() == 1);
  CHECK(out[0] == paramMsg(4, 0x05, 50));  // on the receive channel
  // switches send their switch byte
  send(r, Command::press(Button::Range, 0));
  send(r, Command::setField(W_VCA_MODE, 1, Source::Panel));
  r.skip(4800);
  out = r.midiOut();
  REQUIRE(out.size() == 2);
  CHECK(out[0] == paramMsg(4, 0x10, sysex::encodeSw1(r.st().edit)));
  CHECK(out[1] == paramMsg(4, 0x11, sysex::encodeSw2(r.st().edit)));
  CHECK((out[0][5] & 7) == 1);   // 16'
  CHECK((out[1][5] & 4) == 4);   // GATE
  // W3 has no SysEx form
  send(r, Command::setField(W_SUB, 0, Source::Panel));
  CHECK(r.midiOut().empty());

  // a 1 s drag: a new value every millisecond
  std::vector<TimedMsg> drag;
  int last = -1;
  for (int ms = 0; ms < 1000; ++ms) {
    last = (ms * 7 + 3) % 128;
    r.inst->post(Command::setField(B_VCF_RES, last, Source::Panel));
    runCollect(r, 48, drag);
  }
  runCollect(r, 4800, drag);
  REQUIRE_FALSE(drag.empty());
  CHECK(drag.size() <= 101);
  for (const auto& t : drag) {
    REQUIRE(isParamMsg(t.m));
    CHECK(t.m[4] == 0x06);
  }
  CHECK(drag.back().m[5] == last);
  for (std::size_t i = 1; i < drag.size(); ++i) CHECK(drag[i].pos - drag[i - 1].pos >= 480);  // 10 ms

  // received CC and host automation: nothing
  r.cc(74, 10, 4);
  r.inst->hostParam(B_VCF_FREQ, 20, 0);
  r.inst->hostParam(display::kNumHostParams - 1, 70, 0);
  r.skip(4800);
  CHECK(r.st().edit.v[B_VCF_FREQ] == 20);
  CHECK(r.midiOut().empty());
}

TEST_CASE("AC-M3.1 106 functions I / II send no SysEx; 60 mode transmits nothing ever", "[midi]") {
  for (int fn : {1, 2}) {
    INFO("function " << fn);
    Rig r(48000.0, Mode::J106);
    r.apply(Command::setPerf(P_MIDI_FUNCTION, fn, Source::Overlay));
    r.toBoundary();
    r.events();
    send(r, Command::setField(B_VCF_FREQ, 50, Source::Panel));
    send(r, Command::press(Button::Pulse));
    send(r, Command::press(Button::Manual));
    send(r, cmdOf(CommandType::SendPatch, Source::Overlay));
    send(r, Command::press(Button::PatchBtn, 4));
    for (const auto& m : r.midiOut()) CHECK(m[0] != 0xF0);
  }
  Rig r(48000.0, Mode::J60);
  r.toBoundary();
  r.events();
  send(r, Command::setField(B_VCF_FREQ, 50, Source::Panel));
  send(r, Command::press(Button::Pulse));
  send(r, Command::press(Button::Manual));
  send(r, Command::press(Button::PatchBtn, 4));
  send(r, Command::selectSlot(9, Source::Overlay));
  send(r, cmdOf(CommandType::SendPatch, Source::Overlay));
  send(r, Command::note(60, 100));
  send(r, Command::note(60, 0));
  send(r, pedalCmd(true));
  send(r, pedalCmd(false));
  send(r, pushCmd(true));
  send(r, pushCmd(false));
  for (int i = 0; i < 50; ++i) send(r, leverCmd(i / 50.0, true, i == 0 ? 1 : -1), 96);
  send(r, leverCmd(0.0, false, 0));
  for (int fn : {1, 2, 3}) {
    send(r, Command::setPerf(P_MIDI_FUNCTION, fn, Source::Overlay));
    send(r, Command::setField(B_VCF_RES, 10 * fn, Source::Panel));
    send(r, Command::press(Button::PatchBtn, fn));
  }
  r.skip(4800);
  CHECK(r.midiOut().empty());
}

TEST_CASE("AC-M3.2 106 II / III: PATCH press sends PC / F0 41 30; received PC and host programs send nothing", "[midi]") {
  for (int fn : {1, 2, 3}) {
    INFO("function " << fn);
    Rig r(48000.0, Mode::J106);
    r.apply(Command::setPerf(P_MIDI_CHANNEL, 2, Source::Overlay));
    r.apply(Command::setPerf(P_MIDI_FUNCTION, fn, Source::Overlay));
    r.toBoundary();
    r.events();
    send(r, Command::press(Button::PatchBtn, 7));
    REQUIRE(r.st().currentSlot == 7);
    auto out = r.midiOut();
    if (fn == 1) {
      CHECK(out.empty());
    } else if (fn == 2) {
      REQUIRE(out.size() == 1);
      CHECK(out[0] == std::vector<uint8_t>{0xC1, 6});
    } else {
      REQUIRE(out.size() == 1);
      CHECK(out[0] == dumpMsg(0x30, 1, 6, r.st().bank[6].patch));
    }
    // bank-manager double-click: the same as a PATCH press
    send(r, Command::selectSlot(9, Source::Overlay));
    out = r.midiOut();
    if (fn == 1) {
      CHECK(out.empty());
    } else if (fn == 2) {
      REQUIRE(out.size() == 1);
      CHECK(out[0] == std::vector<uint8_t>{0xC1, 8});
    } else {
      REQUIRE(out.size() == 1);
      CHECK(isDumpMsg(out[0], 0x30));
      CHECK(out[0][4] == 8);
    }
    // received program change and host programs: nothing is sent
    const uint8_t pcm[2] = {0xC1, 20};
    r.inst->midi(pcm, 2, 0);
    settle(r);
    if (fn >= 2) CHECK(r.st().currentSlot == 21);
    send(r, Command::selectSlot(30, Source::Host));
    CHECK(r.st().currentSlot == 30);
    CHECK(r.midiOut().empty());
  }
}

TEST_CASE("AC-M3.3 106 I: computer-keyboard notes and Space are sent", "[midi]") {
  for (int fn : {1, 2, 3}) {
    INFO("function " << fn);
    Rig r(48000.0, Mode::J106);
    r.apply(Command::setPerf(P_MIDI_FUNCTION, fn, Source::Overlay));
    r.apply(Command::setPerf(P_KEY_TRANSPOSE, 5, Source::Panel));  // sent as played, before transpose
    r.toBoundary();
    r.events();
    send(r, Command::note(60, 100));
    auto out = r.midiOut();
    REQUIRE(out.size() == 1);
    CHECK(out[0] == std::vector<uint8_t>{0x90, 60, 64});
    CHECK(voiceOf(r, 60) >= 0);
    send(r, Command::note(60, 0));
    out = r.midiOut();
    REQUIRE(out.size() == 1);
    CHECK(out[0] == std::vector<uint8_t>{0x90, 60, 0});
    send(r, pedalCmd(true));
    send(r, pedalCmd(false));
    out = r.midiOut();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == std::vector<uint8_t>{0xB0, 64, 127});
    CHECK(out[1] == std::vector<uint8_t>{0xB0, 64, 0});
    // received MIDI is never echoed
    r.noteOn(62);
    r.cc(64, 127);
    settle(r);
    r.noteOff(62);
    r.cc(64, 0);
    settle(r);
    CHECK(r.midiOut().empty());
  }
}

TEST_CASE("AC-M3.4 106 II: lever push sends CC1 127 / 0; bend messages <= 1 per 10 ms ending on 8192", "[midi]") {
  Rig r(48000.0, Mode::J106);
  r.apply(Command::setPerf(P_MIDI_FUNCTION, 2, Source::Overlay));
  r.toBoundary();
  r.events();
  // push by mouse (upper-half press on the lever) and by key 3
  send(r, leverCmd(0.0, true, 1));
  send(r, leverCmd(0.0, false, 0));
  auto out = r.midiOut();
  REQUIRE(out.size() == 2);
  CHECK(out[0] == std::vector<uint8_t>{0xB0, 1, 127});
  CHECK(out[1] == std::vector<uint8_t>{0xB0, 1, 0});
  send(r, pushCmd(true));
  send(r, pushCmd(false));
  out = r.midiOut();
  REQUIRE(out.size() == 2);
  CHECK(out[0] == std::vector<uint8_t>{0xB0, 1, 127});
  CHECK(out[1] == std::vector<uint8_t>{0xB0, 1, 0});

  // a lever drag: a new value every millisecond for 300 ms, then release (spring back to centre)
  std::vector<TimedMsg> msgs;
  for (int ms = 0; ms < 300; ++ms) {
    r.inst->post(leverCmd(ms / 300.0, true));
    runCollect(r, 48, msgs);
  }
  r.inst->post(leverCmd(0.0, false));
  runCollect(r, 4800, msgs);
  REQUIRE(msgs.size() >= 2);
  for (const auto& t : msgs) REQUIRE(isBend(t.m));
  for (std::size_t i = 1; i < msgs.size(); ++i) CHECK(msgs[i].pos - msgs[i - 1].pos >= 480);
  CHECK(msgs.size() <= 32);
  CHECK(bendValue(msgs.back().m) == 8192);

  // keys 1 / 2: full deflection while held, centre on release
  msgs.clear();
  r.inst->post(leverCmd(-1.0, false, -1, Source::Keyboard));
  runCollect(r, 2400, msgs);
  r.inst->post(leverCmd(0.0, false, -1, Source::Keyboard));
  runCollect(r, 2400, msgs);
  r.inst->post(leverCmd(1.0, false, -1, Source::Keyboard));
  runCollect(r, 2400, msgs);
  r.inst->post(leverCmd(0.0, false, -1, Source::Keyboard));
  runCollect(r, 2400, msgs);
  REQUIRE(msgs.size() == 4);
  CHECK(bendValue(msgs[0].m) == 1);
  CHECK(bendValue(msgs[1].m) == 8192);
  CHECK(bendValue(msgs[2].m) == 16383);
  CHECK(bendValue(msgs[3].m) == 8192);

  // function I: no CC1 and no pitch bend
  send(r, Command::setPerf(P_MIDI_FUNCTION, 1, Source::Overlay));
  r.events();
  send(r, leverCmd(0.5, true, 1));
  send(r, leverCmd(0.0, false, 0));
  send(r, pushCmd(true));
  send(r, pushCmd(false));
  r.skip(4800);
  for (const auto& m : r.midiOut()) {
    CHECK_FALSE(isBend(m));
    CHECK_FALSE(isCC1(m));
  }
}

TEST_CASE("AC-M3.2 a slider value still pending in the 10 ms limiter is never sent after a patch dump", "[midi]") {
  for (int which : {0, 1}) {
    INFO((which == 0 ? "PATCH press (F0 41 30)" : "MANUAL (F0 41 31)"));
    Rig r(48000.0, Mode::J106);
    r.toBoundary();
    r.events();
    std::vector<TimedMsg> out;
    r.inst->post(Command::setField(B_VCF_FREQ, 10, Source::Panel));
    runCollect(r, 48, out);
    r.inst->post(Command::setField(B_VCF_FREQ, 20, Source::Panel));  // within 10 ms: held back
    runCollect(r, 48, out);
    r.inst->post(Command::press(which == 0 ? Button::PatchBtn : Button::Manual, 6));
    runCollect(r, 4800, out);
    REQUIRE(out.size() >= 2);
    CHECK(out.front().m == paramMsg(0, 0x05, 10));
    CHECK(isDumpMsg(out.back().m, which == 0 ? 0x30 : 0x31));
    if (which == 1) CHECK(out.back().m[5 + B_VCF_FREQ] == 20);  // the dump carries the final value
  }
}
