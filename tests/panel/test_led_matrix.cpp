// AC-P4.1: the LED display and the button LEDs against a golden matrix derived by hand from SPEC §5.2
// (button LEDs) and §5.3 (LED states, priority: LED message -> `t-` -> WRITE blink -> slot display;
// decimal points = EDITED except during a message; 60-mode slots 57-128 blank; Manual `--`).
//
// Each row starts from the first-run state (slot 1 INIT, CLEAN, pending bank 1 / group A, memory
// protect on) in the given mode, applies its steps and expects:
//   text  the two LED characters ("  " = blank), dp (decimal points), blink (digits blink), msg;
//   lit   every lit button LED, nothing else; blnk  every blinking button LED, nothing else.
// Button LED names: R16 R8 R4 PULSE SAW SUB CHOFF CHI CHII PORTA HOLD ARP POLY1 POLY2 A B BANK1-8
// PATCH1-8 MANUAL WRITE KT LFOTRIG.
// The INIT patch lights SAW in both modes, R8 in 106 mode (60 mode has no RANGE, v3.2 C-2), SUB in 60 mode
// (106 has no SUB button) and CHOFF in 106 mode; key mode POLY 1 lights POLY1 in 106 mode.
//
// Steps (separated by ';'): patch N | bank N | bank+ N (Shift+click) | group A|B | manual | write | kt |
// hold | arp | porta | poly1 | poly2 | poly2+ | lfotrig | range N | pulse | sub | chorusI | chorusII |
// chorusI+ | chorusII+ (Shift+click) | chorusOff | edit (panel slider: RES) | select N (bank-manager double-click) | set FIELD V (host write) |
// hostperf NAME V | protect on|off | msg Pr|Er|rc|ok|rE | rx31 | rx30 PP | mode 60|106 | wait MS.
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <sstream>

#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
struct Row {
  const char* name;
  int mode;  // 60 or 106
  const char* steps;
  const char* text;
  bool dp, blink;
  const char* msg;  // "" or Pr Er rc ok rE
  const char* lit;
  const char* blnk;
};

// clang-format off
const Row kMatrix[] = {
  // ---------------------------------------------------------------- 60 mode
  {"60 first run: slot 1", 60, "", "11", false, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 slot 2 (BRASS: chorus I)", 60, "patch 2", "12", false, false, "", "SAW SUB CHI BANK1 PATCH2", ""},
  {"60 slot 23 via BANK 3, PATCH 7", 60, "bank 3; patch 7", "37", false, false, "", "SAW SUB BANK3 PATCH7", ""},
  {"60 bank 6 = BANK 1+5: slot 42", 60, "bank 1; bank+ 5; patch 2", "62", false, false, "", "SAW SUB BANK1 BANK5 PATCH2", ""},
  {"60 bank 7 = BANK 2+5: slot 56", 60, "bank 2; bank+ 5; patch 8", "78", false, false, "", "SAW SUB BANK2 BANK5 PATCH8", ""},
  {"60 bank 7 from BANK 5 lit: slot 49", 60, "bank 5; bank+ 2; patch 1", "71", false, false, "", "SAW SUB BANK2 BANK5 PATCH1", ""},
  {"60 Shift+click without a lit partner = plain click", 60, "bank 3; bank+ 5", "11", false, false, "", "SAW SUB BANK5", ""},
  {"60 current slot 73: digits blank, pending bank 1", 60, "select 73", "  ", false, false, "", "SAW SUB BANK1", ""},
  {"60 slot 73 with WRITE armed", 60, "select 73; write", "  ", false, true, "", "SAW SUB BANK1", "WRITE"},
  {"60 slot 73 EDITED", 60, "select 73; edit", "  ", true, false, "", "SAW SUB BANK1", ""},
  {"60 pending bank differs from the current slot's", 60, "bank 3", "11", false, false, "", "SAW SUB BANK3", ""},
  {"60 Manual", 60, "manual", "--", false, false, "", "SAW SUB BANK1 MANUAL", ""},
  {"60 EDITED", 60, "edit", "11", true, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 Manual + EDITED", 60, "manual; edit", "--", true, false, "", "SAW SUB BANK1 MANUAL", ""},
  {"60 WRITE armed", 60, "write", "11", false, true, "", "SAW SUB BANK1 PATCH1", "WRITE"},
  {"60 WRITE armed + EDITED", 60, "edit; write", "11", true, true, "", "SAW SUB BANK1 PATCH1", "WRITE"},
  {"60 Manual + WRITE armed", 60, "manual; write", "--", false, true, "", "SAW SUB BANK1 MANUAL", "WRITE"},
  {"60 WRITE armed, then BANK 2: still armed", 60, "write; bank 2", "11", false, true, "", "SAW SUB BANK2", "WRITE"},
  {"60 WRITE twice: cancelled", 60, "write; write", "11", false, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 Pr (WRITE under protect) hides the decimal points", 60, "edit; write; patch 5", "Pr", false, false, "Pr", "SAW SUB BANK1 PATCH1", ""},
  {"60 Pr ends: EDITED slot display returns", 60, "edit; write; patch 5; wait 2100", "11", true, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 ok", 60, "protect off; write; patch 5", "ok", false, false, "ok", "SAW SUB BANK1 PATCH5", ""},
  {"60 ok ends: slot 5", 60, "protect off; write; patch 5; wait 2100", "15", false, false, "", "SAW SUB BANK1 PATCH5", ""},
  {"60 rc (F0 41 31 -> Manual)", 60, "rx31", "rc", false, false, "rc", "SAW SUB BANK1 MANUAL", ""},
  {"60 rc ends: --", 60, "rx31; wait 2100", "--", false, false, "", "SAW SUB BANK1 MANUAL", ""},
  {"60 F0 41 30 in 60 mode -> Manual, rc", 60, "rx30 9", "rc", false, false, "rc", "SAW SUB BANK1 MANUAL", ""},
  {"60 Er over the WRITE blink", 60, "write; msg Er", "Er", false, false, "Er", "SAW SUB BANK1 PATCH1", "WRITE"},
  {"60 Er ends: WRITE blink returns", 60, "write; msg Er; wait 2100", "11", false, true, "", "SAW SUB BANK1 PATCH1", "WRITE"},
  {"60 Er while EDITED", 60, "edit; msg Er", "Er", false, false, "Er", "SAW SUB BANK1 PATCH1", ""},
  {"60 Er in Manual", 60, "manual; msg Er", "Er", false, false, "Er", "SAW SUB BANK1 MANUAL", ""},
  {"60 Er on slot 73", 60, "select 73; msg Er", "Er", false, false, "Er", "SAW SUB BANK1", ""},
  {"60 rE", 60, "msg rE", "rE", false, false, "rE", "SAW SUB BANK1 PATCH1", ""},
  {"60 a new message replaces the current one", 60, "msg Er; msg Pr", "Pr", false, false, "Pr", "SAW SUB BANK1 PATCH1", ""},
  {"60 RANGE 16' ignored; PULSE, chorus II, HOLD, ARP, LFO TRIG held", 60, "range 0; pulse; chorusII; hold; arp; lfotrig", "11", true, false, "",
   "PULSE SAW SUB CHII HOLD ARP LFOTRIG BANK1 PATCH1", ""},
  {"60 RANGE 4' ignored; chorus I+II (II, then Shift+click I), SUB off", 60, "range 2; chorusII; chorusI+; sub", "11", true, false, "", "SAW CHI CHII BANK1 PATCH1", ""},
  {"60 chorus radio: I then II lights II only", 60, "chorusI; chorusII", "11", true, false, "", "SAW SUB CHII BANK1 PATCH1", ""},
  {"60 chorus OFF button has no LED", 60, "chorusII; chorusOff", "11", true, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 a plain click from I+II selects that mode", 60, "chorusI; chorusII+; chorusI", "11", true, false, "", "SAW SUB CHI BANK1 PATCH1", ""},
  {"60 106-only buttons do nothing", 60, "kt; porta; poly2; group B; bank 8", "11", false, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 106-only settings set by the host are not shown", 60, "hostperf keytranspose 5; hostperf porta 1; hostperf keymode 2", "11", false, false, "",
   "SAW SUB BANK1 PATCH1", ""},
  {"60 a stored range lights no RANGE LED", 60, "set W0 5", "11", true, false, "", "SAW SUB BANK1 PATCH1", ""},
  {"60 -> 106 with slot 73: B21, group B", 60, "select 73; mode 106", "21", false, false, "", "R8 SAW CHOFF POLY1 B BANK2 PATCH1", ""},
  {"60 -> 106 cancels WRITE", 60, "write; mode 106", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  // ---------------------------------------------------------------- 106 mode
  {"106 first run: slot 1", 106, "", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 slot 2 (BRASS: chorus I, no SUB LED)", 106, "patch 2", "12", false, false, "", "R8 SAW CHI POLY1 A BANK1 PATCH2", ""},
  {"106 slot 73 = B21", 106, "group B; bank 2; patch 1", "21", false, false, "", "R8 SAW CHOFF POLY1 B BANK2 PATCH1", ""},
  {"106 slot 128 = B88", 106, "group B; bank 8; patch 8", "88", false, false, "", "R8 SAW CHOFF POLY1 B BANK8 PATCH8", ""},
  {"106 slot 73, then group A: PATCH LED off", 106, "group B; bank 2; patch 1; group A", "21", false, false, "", "R8 SAW CHOFF POLY1 A BANK2", ""},
  {"106 pending bank differs from the current slot's", 106, "bank 3", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK3", ""},
  {"106 Manual", 106, "manual", "--", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 MANUAL", ""},
  {"106 EDITED", 106, "edit", "11", true, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 Manual + EDITED", 106, "manual; edit", "--", true, false, "", "R8 SAW CHOFF POLY1 A BANK1 MANUAL", ""},
  {"106 WRITE armed", 106, "write", "11", false, true, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "WRITE"},
  {"106 WRITE armed + EDITED", 106, "edit; write", "11", true, true, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "WRITE"},
  {"106 Manual + WRITE armed", 106, "manual; write", "--", false, true, "", "R8 SAW CHOFF POLY1 A BANK1 MANUAL", "WRITE"},
  {"106 B21 EDITED + WRITE armed", 106, "select 73; edit; write", "21", true, true, "", "R8 SAW CHOFF POLY1 B BANK2 PATCH1", "WRITE"},
  {"106 WRITE armed, group B and BANK 4: still armed", 106, "write; group B; bank 4", "11", false, true, "", "R8 SAW CHOFF POLY1 B BANK4", "WRITE"},
  {"106 key transpose armed: t-", 106, "kt", "t-", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "KT"},
  {"106 t- keeps the EDITED decimal points", 106, "edit; kt", "t-", true, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "KT"},
  {"106 t- in Manual", 106, "manual; kt", "t-", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 MANUAL", "KT"},
  {"106 KEY TRANSPOSE while WRITE armed: WRITE cancelled, t-", 106, "write; kt", "t-", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "KT"},
  {"106 WRITE while t- armed: t- cancelled, WRITE blinks", 106, "kt; write", "11", false, true, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "WRITE"},
  {"106 KEY TRANSPOSE twice: disarmed", 106, "kt; kt", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 key transpose +5: KT lit", 106, "hostperf keytranspose 5", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1 KT", ""},
  {"106 armed while transpose is +5: KT blinks", 106, "hostperf keytranspose 5; kt", "t-", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "KT"},
  {"106 Er over t-", 106, "kt; msg Er", "Er", false, false, "Er", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "KT"},
  {"106 Er ends: t- returns", 106, "kt; msg Er; wait 2100", "t-", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "KT"},
  {"106 Er over the WRITE blink", 106, "edit; write; msg Er", "Er", false, false, "Er", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", "WRITE"},
  {"106 Pr (WRITE under protect)", 106, "edit; write; patch 3", "Pr", false, false, "Pr", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 ok: slot 30 = A46", 106, "protect off; write; bank 4; patch 6", "ok", false, false, "ok", "R8 SAW CHOFF POLY1 A BANK4 PATCH6", ""},
  {"106 ok ends: 46", 106, "protect off; write; bank 4; patch 6; wait 2100", "46", false, false, "", "R8 SAW CHOFF POLY1 A BANK4 PATCH6", ""},
  {"106 rc: protected F0 41 30 pp 9 -> slot 10, EDITED", 106, "rx30 9", "rc", false, false, "rc", "R8 SAW CHOFF POLY1 A BANK2 PATCH2", ""},
  {"106 rc ends: 22 with decimal points", 106, "rx30 9; wait 2100", "22", true, false, "", "R8 SAW CHOFF POLY1 A BANK2 PATCH2", ""},
  {"106 rc: unprotected F0 41 30 pp 72 -> B21, CLEAN", 106, "protect off; rx30 72; wait 2100", "21", false, false, "", "R8 SAW CHOFF POLY1 B BANK2 PATCH1", ""},
  {"106 F0 41 31 -> Manual", 106, "rx31; wait 2100", "--", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 MANUAL", ""},
  {"106 rE", 106, "msg rE", "rE", false, false, "rE", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 4', PULSE, chorus II, PORTAMENTO, POLY 2", 106, "range 2; pulse; chorusII; porta; poly2", "11", true, false, "",
   "R4 PULSE SAW CHII PORTA POLY2 A BANK1 PATCH1", ""},
  {"106 UNISON lights both POLY buttons", 106, "poly2+", "11", false, false, "", "R8 SAW CHOFF POLY1 POLY2 A BANK1 PATCH1", ""},
  {"106 a plain POLY 1 click leaves UNISON", 106, "poly2+; poly1", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 stored range 5 is read as 16'", 106, "set W0 5", "11", true, false, "", "R16 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 chorus I+II patch shows as I", 106, "set W4 3", "11", true, false, "", "R8 SAW CHI POLY1 A BANK1 PATCH1", ""},
  {"106 chorus OFF", 106, "chorusI; chorusOff", "11", true, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 60-only buttons do nothing", 106, "sub; hold; arp; lfotrig", "11", false, false, "", "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 HOLD / ARP set by the host are not shown", 106, "hostperf hold 1; hostperf arp 1", "11", false, false, "",
   "R8 SAW CHOFF POLY1 A BANK1 PATCH1", ""},
  {"106 -> 60 with slot 73: blank, pending bank 1", 106, "select 73; mode 60", "  ", false, false, "", "SAW SUB BANK1", ""},
  {"106 -> 60 cancels t-", 106, "kt; mode 60", "11", false, false, "", "SAW SUB BANK1 PATCH1", ""},
};
// clang-format on

int ledBit(const std::string& n) {
  static const std::map<std::string, int> m = {
      {"R16", L_RANGE16}, {"R8", L_RANGE8},   {"R4", L_RANGE4},     {"PULSE", L_PULSE},  {"SAW", L_SAW},
      {"SUB", L_SUB},     {"CHOFF", L_CHORUS_OFF}, {"CHI", L_CHORUS_I}, {"CHII", L_CHORUS_II}, {"PORTA", L_PORTA},
      {"HOLD", L_HOLD},   {"ARP", L_ARP},     {"POLY1", L_POLY1},   {"POLY2", L_POLY2},  {"A", L_GROUP_A},
      {"B", L_GROUP_B},   {"MANUAL", L_MANUAL}, {"WRITE", L_WRITE},   {"KT", L_KEY_TRANSPOSE},
      {"LFOTRIG", L_LFO_TRIG}};
  auto it = m.find(n);
  if (it != m.end()) return it->second;
  if (n.rfind("BANK", 0) == 0) return L_BANK1 + std::stoi(n.substr(4)) - 1;
  if (n.rfind("PATCH", 0) == 0) return L_PATCH1 + std::stoi(n.substr(5)) - 1;
  FAIL("unknown LED " << n);
  return 0;
}
uint64_t bits(const std::string& s) {
  std::istringstream in(s);
  std::string t;
  uint64_t b = 0;
  while (in >> t) b |= 1ull << ledBit(t);
  return b;
}
std::string names(uint64_t b) {
  static const char* n[L_COUNT] = {"R16", "R8", "R4", "PULSE", "SAW", "SUB", "CHOFF", "CHI", "CHII", "PORTA", "HOLD", "ARP",
                                   "POLY1", "POLY2", "A", "B", "BANK1", "BANK2", "BANK3", "BANK4", "BANK5", "BANK6", "BANK7",
                                   "BANK8", "PATCH1", "PATCH2", "PATCH3", "PATCH4", "PATCH5", "PATCH6", "PATCH7", "PATCH8",
                                   "MANUAL", "WRITE", "KT", "LFOTRIG"};
  std::string s;
  for (int i = 0; i < L_COUNT; ++i)
    if (b & (1ull << i)) s += std::string(s.empty() ? "" : " ") + n[i];
  return s;
}
LedMessage msgOf(const std::string& s) {
  if (s == "Pr") return LedMessage::Protected;
  if (s == "Er") return LedMessage::Format;
  if (s == "rc") return LedMessage::Received;
  if (s == "ok") return LedMessage::WriteOk;
  if (s == "rE") return LedMessage::Reset;
  return LedMessage::None;
}

void runStep(Rig& r, const std::string& step) {
  std::istringstream in(step);
  std::string op, a, b;
  in >> op >> a >> b;
  auto press = [&](Button bt, int i = 0, bool shift = false) { r.apply(Command::press(bt, i, shift)); };
  if (op.empty()) return;
  if (op == "patch") press(Button::PatchBtn, std::stoi(a));
  else if (op == "bank") press(Button::Bank, std::stoi(a));
  else if (op == "bank+") press(Button::Bank, std::stoi(a), true);
  else if (op == "group") press(Button::Group, a == "B" ? 1 : 0);
  else if (op == "manual") press(Button::Manual);
  else if (op == "write") press(Button::Write);
  else if (op == "kt") press(Button::KeyTranspose);
  else if (op == "hold") press(Button::Hold);
  else if (op == "arp") press(Button::ArpOn);
  else if (op == "porta") press(Button::Portamento);
  else if (op == "poly1") press(Button::Poly1);
  else if (op == "poly2") press(Button::Poly2);
  else if (op == "poly2+") press(Button::Poly2, 0, true);
  else if (op == "lfotrig") press(Button::LfoTrig);
  else if (op == "range") press(Button::Range, std::stoi(a));
  else if (op == "pulse") press(Button::Pulse);
  else if (op == "sub") press(Button::Sub);
  else if (op == "chorusI") press(Button::ChorusI);
  else if (op == "chorusII") press(Button::ChorusII);
  else if (op == "chorusOff") press(Button::ChorusOff);
  else if (op == "chorusI+") press(Button::ChorusI, 0, true);
  else if (op == "chorusII+") press(Button::ChorusII, 0, true);
  else if (op == "edit") r.apply(Command::setField(B_VCF_RES, r.st().edit.v[B_VCF_RES] + 10, Source::Panel));
  else if (op == "select") r.apply(Command::selectSlot(std::stoi(a), Source::Overlay));
  else if (op == "set") r.apply(Command::setField((a[0] == 'W' ? kNumSliders : 0) + std::stoi(a.substr(1)), std::stoi(b), Source::Host));
  else if (op == "hostperf") {
    const int p = a == "keytranspose" ? P_KEY_TRANSPOSE : a == "porta" ? P_PORTA_ON : a == "keymode" ? P_KEY_MODE
                : a == "hold" ? P_HOLD : P_ARP_ON;
    r.apply(Command::setPerf(p, std::stoi(b), Source::Host));
  } else if (op == "protect") r.apply(Command::setPerf(P_PROTECT, a == "on" ? 1 : 0, Source::Overlay));
  else if (op == "msg") r.apply(messageCmd(msgOf(a)));
  else if (op == "rx31") {
    r.sysex(dumpMsg(0x31, 0, 0, initPatch()));
    settle(r);
  } else if (op == "rx30") {
    r.sysex(dumpMsg(0x30, 0, std::stoi(a), initPatch()));
    settle(r);
  } else if (op == "mode") {
    r.apply(Command::setSetting(S_MODE, a == "106" ? 1 : 0));
  } else if (op == "wait") {
    r.skip(48L * std::stoi(a));
  } else {
    FAIL("unknown step " << op);
  }
}
}  // namespace

TEST_CASE("AC-P4.1 LED, decimal points and button LEDs match the hand-derived golden matrix", "[panel]") {
  for (const Row& row : kMatrix) {
    INFO("row: " << row.name << "  steps: " << row.steps);
    Rig r(48000.0, row.mode == 106 ? Mode::J106 : Mode::J60);
    r.toBoundary();
    std::istringstream in(row.steps);
    std::string step;
    while (std::getline(in, step, ';')) runStep(r, step);
    const LedState L = r.core().led();
    CHECK(std::string(L.text, 2) == row.text);
    CHECK(L.dp == row.dp);
    CHECK(L.blink == row.blink);
    CHECK(L.message == msgOf(row.msg));
    uint64_t blinkBits = 0;
    const uint64_t litBits = r.core().buttonLeds(&blinkBits);
    CHECK(names(litBits) == names(bits(row.lit)));
    CHECK(names(blinkBits) == names(bits(row.blnk)));
    // the published snapshot (what the UI shows) carries the same values
    r.skip(1);
    Snapshot s;
    r.inst->snapshot(s);
    CHECK(std::string(s.led.text, 2) == row.text);
    CHECK(s.led.dp == row.dp);
    CHECK(s.led.blink == row.blink);
    CHECK(s.led.message == msgOf(row.msg));
    CHECK(names(s.buttonLeds) == names(bits(row.lit)));
    CHECK(names(s.buttonBlink) == names(bits(row.blnk)));
  }
}
