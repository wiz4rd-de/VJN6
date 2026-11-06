// Files and bank manager (core part, through vjn6::Bridge and vjn6::files): AC-F10.1, AC-D3.1, AC-D4.1,
// AC-D4.2. All file and import criteria run with memory protect off unless stated (§13.3).
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "util/core_helpers.h"
#include "vjn6/files.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
std::vector<uint8_t> bytesOf(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
std::string strOf(const std::vector<uint8_t>& b) { return std::string(b.begin(), b.end()); }

struct BRig {
  explicit BRig(Mode m = Mode::J60, bool protect = false) : r(48000.0, m), bridge(*r.inst) {
    if (!protect) protectOff(r);
    r.toBoundary();
    r.skip(64);
    bridge.poll(ui);
    ui.clear();
  }
  void load(const std::string& name, const std::vector<uint8_t>& data) {
    bridge.loadFile(name, data.data(), data.size(), ui);
    r.skip(512);
    bridge.poll(ui);
  }
  void load(const std::string& name, const std::string& text) { load(name, bytesOf(text)); }
  void ui1(const std::string& json) {
    REQUIRE(bridge.handleUiMessage(json, ui));
    r.skip(512);
    bridge.poll(ui);
  }
  std::vector<uint8_t> exportData(const std::string& what, std::string& name) {
    r.skip(1);
    std::vector<uint8_t> b;
    REQUIRE(bridge.exportData(what, name, b));
    return b;
  }
  // True if a notice of this kind (and file, if given) was sent to the UI; clears the UI log.
  bool notice(const std::string& kind, const std::string& file = "", int i = -1, int j = -1) {
    bool found = false;
    for (const std::string& m : ui) {
      if (m.find("\"t\":\"notice\"") == std::string::npos) continue;
      if (m.find("\"kind\":\"" + kind + "\"") == std::string::npos) continue;
      if (!file.empty() && m.find("\"file\":\"" + file + "\"") == std::string::npos) continue;
      if (i >= 0 && m.find("\"i\":" + std::to_string(i) + ",") == std::string::npos) continue;
      if (j >= 0 && m.find("\"j\":" + std::to_string(j) + "}") == std::string::npos) continue;
      found = true;
    }
    ui.clear();
    return found;
  }
  CoreState& st() { return r.st(); }
  Rig r;
  Bridge bridge;
  std::vector<std::string> ui;
};

Patch somePatch(int seed) {
  Patch p;
  for (int f = 0; f < kNumSliders; ++f) p.v[f] = static_cast<uint8_t>((seed * 37 + f * 11) % 128);
  for (int f = kNumSliders; f < kNumFields; ++f) p.v[f] = static_cast<uint8_t>((seed + f) % switchCount(f));
  return p;
}
// A patch that survives the SysEx mapping unchanged (106-expressible switch values, W3 = 1).
Patch syxPatch(int seed) {
  Patch p = somePatch(seed);
  p.v[W_SUB] = 1;
  if (p.v[W_CHORUS] == 3) p.v[W_CHORUS] = 1;
  if (p.v[W_PWM_SRC] == 2) p.v[W_PWM_SRC] = 1;
  return p;
}
std::string allInitBankJson() {
  Slot bank[kNumSlots];
  for (Slot& s : bank) {
    s.patch = initPatch();
    s.name = makeName("INIT");
  }
  return files::bankJson(bank);
}
std::vector<uint8_t> concat(const std::vector<std::vector<uint8_t>>& msgs) {
  std::vector<uint8_t> v;
  for (const auto& m : msgs) v.insert(v.end(), m.begin(), m.end());
  return v;
}
}  // namespace

TEST_CASE("AC-F10.1 load .vjn6: edit buffer with its name, Manual, CLEAN, notice", "[files]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    BRig b(m);
    b.ui1(R"({"t":"press","b":"patch","i":4})");
    b.ui1(R"({"t":"field","f":6,"v":99})");
    const Patch stored = b.st().stored;
    const Patch p = somePatch(3);
    b.load("BRASS 1.vjn6", files::patchJson(p, makeName("BRASS 1")));
    CHECK(b.notice("loaded", "BRASS 1.vjn6"));
    CHECK(b.st().edit == p);
    CHECK(std::string(b.st().editName.s) == "BRASS 1");
    CHECK(b.st().manual);
    CHECK_FALSE(b.st().edited);
    CHECK(b.st().currentSlot == 4);
    CHECK(b.st().stored == stored);
    CHECK(ledText(b.r) == "--");
  }
}

TEST_CASE("AC-F10.1 load .vjn6bank protected / not protected", "[files]") {
  Slot bank[kNumSlots];
  for (int i = 0; i < kNumSlots; ++i) {
    bank[i].patch = somePatch(i);
    bank[i].name = makeName(("P" + std::to_string(i + 1)).c_str());
  }
  const std::string text = files::bankJson(bank);
  SECTION("protected: Pr, nothing written") {
    BRig b(Mode::J60, true);
    const Slot first = b.st().bank[1];
    b.load("mine.vjn6bank", text);
    CHECK(b.notice("protected"));
    CHECK(ledText(b.r) == "Pr");
    CHECK(b.st().bank[1].patch == first.patch);
    CHECK(b.st().bank[1].name == first.name);
  }
  SECTION("not protected: all 128 slots and names replaced; edit buffer, stored, slot and flags unchanged") {
    BRig b(Mode::J106);
    b.ui1(R"({"t":"press","b":"patch","i":3})");
    b.ui1(R"({"t":"field","f":0,"v":5})");
    const Patch edit = b.st().edit, stored = b.st().stored;
    const Name name = b.st().editName;
    b.load("mine.vjn6bank", text);
    CHECK(b.notice("bankLoaded"));
    for (int i = 0; i < kNumSlots; ++i) {
      CHECK(b.st().bank[i].patch == bank[i].patch);
      CHECK(b.st().bank[i].name == bank[i].name);
    }
    CHECK(b.st().edit == edit);
    CHECK(b.st().stored == stored);
    CHECK(b.st().editName == name);
    CHECK(b.st().currentSlot == 3);
    CHECK(b.st().edited);
    CHECK_FALSE(b.st().manual);
  }
}

TEST_CASE("AC-F10.1 a one-message .syx loads the edit buffer named after the file stem", "[files]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    BRig b(m);
    const Patch p = syxPatch(9);
    b.load("My Synth Patch.SYX", dumpMsg(0x30, 0, 77, p));  // a 30 message too: never a slot
    CHECK(b.notice("loaded"));
    CHECK(b.st().edit == p);
    CHECK(std::string(b.st().editName.s) == "My Synth Pat");
    CHECK(b.st().manual);
    CHECK_FALSE(b.st().edited);
    CHECK(b.st().currentSlot == 1);
    CHECK(b.st().bank[77].patch == initPatch());
  }
}

TEST_CASE("AC-F10.1 rename a row to e-acute + emoji + abcdefghijklm, protected / not", "[files]") {
  const char* precomposed = "\xC3\xA9\xF0\x9F\x98\x80" "abcdefghijklm";
  const char* decomposed = "e\xCC\x81\xF0\x9F\x98\x80" "abcdefghijklm";  // NFC composes e + U+0301 first
  SECTION("protected: Pr, unchanged") {
    BRig b(Mode::J60, true);
    b.ui1(std::string(R"({"t":"rename","slot":5,"name":")") + precomposed + "\"}");
    CHECK(b.notice("protected"));
    CHECK(ledText(b.r) == "Pr");
    CHECK(std::string(b.st().bank[4].name.s) == "BASS");
  }
  SECTION("not protected: ??abcdefghij") {
    BRig b(Mode::J60);
    b.ui1(std::string(R"({"t":"rename","slot":5,"name":")") + precomposed + "\"}");
    CHECK(std::string(b.st().bank[4].name.s) == "??abcdefghij");
    b.ui1(std::string(R"({"t":"rename","slot":6,"name":")") + decomposed + "\"}");
    CHECK(std::string(b.st().bank[5].name.s) == "??abcdefghij");
    b.ui1(R"({"t":"rename","slot":7,"name":"\u00e9\ud83d\ude00abcdefghijklm"})");
    CHECK(std::string(b.st().bank[6].name.s) == "??abcdefghij");
    b.ui1(R"({"t":"rename","slot":8,"name":"  pad  "})");  // trailing spaces removed
    CHECK(std::string(b.st().bank[7].name.s) == "  pad");
    // renaming the current slot renames the edit buffer too
    b.ui1(R"({"t":"press","b":"patch","i":2})");
    b.ui1(R"({"t":"rename","slot":2,"name":"Big Brass"})");
    CHECK(std::string(b.st().bank[1].name.s) == "Big Brass");
    CHECK(std::string(b.st().editName.s) == "Big Brass");
    CHECK(files::normaliseName(precomposed) == "??abcdefghij");
    CHECK(files::normaliseName(decomposed) == "??abcdefghij");
  }
}

TEST_CASE("AC-F10.1 row drop 2 -> 9 and a row onto the edit buffer", "[files]") {
  BRig b(Mode::J106);
  b.ui1(R"({"t":"selectSlot","slot":9})");
  b.ui1(R"({"t":"field","f":5,"v":3})");
  const Patch edit = b.st().edit, stored = b.st().stored;
  b.ui1(R"({"t":"copySlot","from":2,"to":9})");
  CHECK(b.st().bank[8].patch == b.st().bank[1].patch);
  CHECK(std::string(b.st().bank[8].name.s) == "BRASS");
  // the current slot was overwritten: edit buffer and stored patch unchanged
  CHECK(b.st().edit == edit);
  CHECK(b.st().stored == stored);
  CHECK(b.st().currentSlot == 9);
  CHECK(b.st().edited);
  // a row onto the edit buffer: patch and name, Manual (memory protect does not apply)
  b.ui1(R"({"t":"perf","p":19,"v":1,"src":"overlay"})");
  b.ui1(R"({"t":"slotToEdit","slot":4})");
  CHECK(b.st().edit == b.st().bank[3].patch);
  CHECK(std::string(b.st().editName.s) == "PAD");
  CHECK(b.st().manual);
  CHECK_FALSE(b.st().edited);
  CHECK(b.st().currentSlot == 9);
  // a row drop under protect: Pr, nothing written
  b.ui1(R"({"t":"copySlot","from":3,"to":10})");
  CHECK(b.notice("protected"));
  CHECK(std::string(b.st().bank[9].name.s) == "INIT");
}

TEST_CASE("AC-F10.1 exports in both formats; a .wav is rejected with Er", "[files]") {
  BRig b(Mode::J60);
  b.ui1(R"({"t":"press","b":"patch","i":3})");
  std::string name;
  auto bank = b.exportData("bankJson", name);
  CHECK(name == "VJN6 bank.vjn6bank");
  CHECK(strOf(bank) == files::bankJson(b.st().bank));
  bank = b.exportData("bankSyx", name);
  CHECK(name == "VJN6 bank.syx");
  REQUIRE(bank.size() == 128 * 24);
  for (int i = 0; i < kNumSlots; ++i)
    CHECK(std::vector<uint8_t>(bank.begin() + 24 * i, bank.begin() + 24 * (i + 1)) == dumpMsg(0x30, 0, i, b.st().bank[i].patch));
  auto patch = b.exportData("patchJson", name);
  CHECK(name == "STRINGS.vjn6");
  CHECK(strOf(patch) == files::patchJson(b.st().edit, b.st().editName));
  patch = b.exportData("patchSyx", name);
  CHECK(name == "STRINGS.syx");
  CHECK(patch == dumpMsg(0x31, 0, 0, b.st().edit));
  // the UI message form answers with exportData (base64)
  b.ui.clear();
  REQUIRE(b.bridge.handleUiMessage(R"({"t":"export","what":"patchSyx"})", b.ui));
  REQUIRE(b.ui.size() == 1);
  CHECK(b.ui[0].find("\"name\":\"STRINGS.syx\"") != std::string::npos);
  CHECK(b.ui[0].find(Bridge::base64(patch.data(), patch.size())) != std::string::npos);
  b.ui.clear();
  // a .wav
  const Patch before = b.st().edit;
  const std::vector<uint8_t> wav = {'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
  b.load("drums.wav", wav);
  CHECK(b.notice("malformed", "drums.wav"));
  CHECK(ledText(b.r) == "Er");
  CHECK(b.st().edit == before);
}

TEST_CASE("AC-D3.1 .vjn6bank import then export is byte-identical; other writers round-trip with identical values",
          "[files]") {
  // a bank as VJN6 writes it, with names that need JSON escaping
  Slot bank[kNumSlots];
  const char* names[] = {"Say \"hi\" \\o/", "", "  lead", "~`!@#$%^&*()", "{}[]:,", "INIT"};
  for (int i = 0; i < kNumSlots; ++i) {
    bank[i].patch = somePatch(i);
    bank[i].name = makeName(names[i % 6]);
  }
  BRig b(Mode::J60);
  b.load("first.vjn6bank", files::bankJson(bank));
  REQUIRE(b.notice("bankLoaded"));
  std::string name;
  const auto a = b.exportData("bankJson", name);
  const std::string text = strOf(a);
  CHECK(text == files::bankJson(bank));
  CHECK(text.rfind("{\n  \"vjn6\": 1,\n  \"patches\": [\n    {\n      \"name\": \"Say \\\"hi\\\" \\\\o/\",\n      \"sliders\": [\n", 0) == 0);
  CHECK(text.find('\r') == std::string::npos);
  CHECK(text.substr(text.size() - 6) == "  ]\n}\n");
  // import into a fresh instrument, export again
  BRig c(Mode::J106);
  c.load("VJN6 bank.vjn6bank", a);
  REQUIRE(c.notice("bankLoaded"));
  CHECK(c.exportData("bankJson", name) == a);
  // a compact file with other key orders, unknown keys and a name needing normalisation
  std::string other = R"({"patches":[)";
  for (int i = 0; i < kNumSlots; ++i) {
    const Patch p = somePatch(i + 50);
    std::string sl;
    for (int f = 0; f < kNumSliders; ++f) sl += (f ? "," : "") + std::to_string(p.v[f]);
    other += std::string(i ? "," : "") + R"({"switches":{"hpf":)" + std::to_string(p.v[W_HPF]) +
             R"(,"vca_mode":)" + std::to_string(p.v[W_VCA_MODE]) + R"(,"vcf_pol":)" + std::to_string(p.v[W_VCF_POL]) +
             R"(,"pwm_src":)" + std::to_string(p.v[W_PWM_SRC]) + R"(,"chorus":)" + std::to_string(p.v[W_CHORUS]) +
             R"(,"sub":)" + std::to_string(p.v[W_SUB]) + R"(,"saw":)" + std::to_string(p.v[W_SAW]) + R"(,"pulse":)" +
             std::to_string(p.v[W_PULSE]) + R"(,"range":)" + std::to_string(p.v[W_RANGE]) + R"(,"x":1},"sliders":[)" +
             sl + R"(],"name":"caf\u00e9 n)" + std::to_string(i) + R"(  ","comment":"hi"})";
  }
  other += R"(],"tool":"other","vjn6":1})";
  BRig d(Mode::J60);
  d.load("other.vjn6bank", other);
  REQUIRE(d.notice("bankLoaded"));
  for (int i = 0; i < kNumSlots; ++i) {
    CHECK(d.st().bank[i].patch == somePatch(i + 50));
    CHECK(std::string(d.st().bank[i].name.s) == ("caf? n" + std::to_string(i)));
  }
  const auto canon = d.exportData("bankJson", name);
  CHECK(strOf(canon) == files::bankJson(d.st().bank));
  // a .vjn6 patch written by VJN6 round-trips byte-identically too
  const std::string pj = files::patchJson(somePatch(7), makeName("A \"B\" C"));
  d.load("x.vjn6", pj);
  CHECK(strOf(d.exportData("patchJson", name)) == pj);
}

TEST_CASE("AC-D3.1 a canonical .syx bank into an all-INIT bank exports byte-identically; others are normalised",
          "[files]") {
  BRig b(Mode::J106);
  b.load("init.vjn6bank", allInitBankJson());
  REQUIRE(b.notice("bankLoaded"));
  std::vector<std::vector<uint8_t>> msgs;
  for (int i = 0; i < kNumSlots; ++i) msgs.push_back(dumpMsg(0x30, 0, i, syxPatch(i)));
  const auto file = concat(msgs);
  b.load("factory.syx", file);
  CHECK(b.notice("imported", "factory.syx", 128, 0));
  std::string name;
  CHECK(b.exportData("bankSyx", name) == file);
  CHECK(std::string(b.st().bank[0].name.s) == "A11");
  CHECK(std::string(b.st().bank[127].name.s) == "B88");

  // a synthetic bank with non-canonical bits on channel 6 (n = 5)
  BRig c(Mode::J60);
  c.load("init.vjn6bank", allInitBankJson());
  const uint8_t d[16] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 0, 1, 2, 3};
  // sw1: range bits 0+1 (several -> 8'), chorus off with bit 6 set; sw2: bits 5-6 set, HPF bits 3
  const auto m1 = rawDump(0x30, 5, 0, d, 0x63, 0x7B);
  // sw1: no range bit (-> 8'), pulse, chorus II; sw2: VCA GATE, bit 6 set
  const auto m2 = rawDump(0x30, 5, 1, d, 0x08, 0x44);
  c.load("odd.syx", concat({m1, m2}));
  CHECK(c.notice("imported", "odd.syx", 2, 0));
  const auto out = c.exportData("bankSyx", name);
  const std::vector<uint8_t> e1(out.begin(), out.begin() + 24), e2(out.begin() + 24, out.begin() + 48);
  // canonical: n = 0, one-hot range (8' = 0x02), bit 6 = 0 with chorus off (bit 5), sw2 bits 5-6 = 0
  CHECK(e1 == rawDump(0x30, 0, 0, d, 0x22, 0x1B));
  CHECK(e2 == rawDump(0x30, 0, 1, d, 0x0A, 0x04));
  // the same through a one-message patch file
  c.load("odd1.syx", m1);
  CHECK(c.exportData("patchSyx", name) == rawDump(0x31, 0, 0, d, 0x22, 0x1B));
}

TEST_CASE("AC-D4.1 .syx and file import edge cases", "[files]") {
  SECTION("0 bytes, 1 MiB + 1, only 32 messages, a .wav: Er") {
    BRig b(Mode::J60);
    const Patch before = b.st().edit;
    b.load("empty.syx", std::vector<uint8_t>());
    CHECK(b.notice("malformed", "empty.syx"));
    CHECK(ledText(b.r) == "Er");
    std::vector<uint8_t> big = concat({dumpMsg(0x31, 0, 0, syxPatch(1)), dumpMsg(0x31, 0, 0, syxPatch(2))});
    big.resize((1u << 20) + 1, 0x00);
    b.load("big.syx", big);
    CHECK(b.notice("malformed", "big.syx"));
    big.resize(1u << 20);  // exactly 1 MiB is accepted
    b.load("big.syx", big);
    CHECK(b.notice("imported", "big.syx", 2, 0));
    b.load("params.syx", concat({paramMsg(0, 5, 10), paramMsg(0, 6, 20)}));
    CHECK(b.notice("malformed"));
    b.load("song.wav", std::vector<uint8_t>(100, 0x52));
    CHECK(b.notice("malformed"));
    b.load("noext", files::patchJson(somePatch(1), makeName("x")));
    CHECK(b.notice("malformed"));
    CHECK(b.st().edit == before);
  }
  SECTION("junk + one patch: edit buffer, Manual; its 32 messages are applied after (EDITED)") {
    BRig b(Mode::J106);
    const Patch p = syxPatch(5);
    std::vector<uint8_t> f = {0x00, 0x13, 0xF0, 0x43, 0x10, 0xF7, 0x99, 0xF0, 0x41, 0x30, 0x00};  // junk, foreign SysEx
    const auto msg = dumpMsg(0x31, 0, 0, p);
    f.insert(f.end(), msg.begin(), msg.end());
    f.push_back(0x7F);
    const auto pm = paramMsg(0, 0x02, 99);
    f.insert(f.end(), pm.begin(), pm.end());
    b.load("junk.syx", f);
    CHECK(b.notice("loaded"));
    Patch expect = p;
    expect.v[2] = 99;
    CHECK(b.st().edit == expect);
    CHECK(b.st().manual);
    CHECK(b.st().edited);
    CHECK(b.st().currentSlot == 1);
  }
  SECTION("128 30-messages (pp 0-127) + 3 more: 128 written, 3 dropped") {
    BRig b(Mode::J60);
    b.ui1(R"({"t":"press","b":"patch","i":2})");
    const Patch edit = b.st().edit;
    std::vector<std::vector<uint8_t>> msgs;
    for (int i = 0; i < 128; ++i) msgs.push_back(dumpMsg(0x30, 0, i, syxPatch(i)));
    for (int i = 0; i < 3; ++i) msgs.push_back(dumpMsg(0x30, 0, 10 + i, syxPatch(200 + i)));
    b.load("bank.syx", concat(msgs));
    CHECK(b.notice("imported", "bank.syx", 128, 3));
    for (int i = 0; i < 128; ++i) CHECK(b.st().bank[i].patch == syxPatch(i));
    CHECK(std::string(b.st().bank[0].name.s) == "A11");
    CHECK(std::string(b.st().bank[63].name.s) == "A88");
    CHECK(std::string(b.st().bank[64].name.s) == "B11");
    // edit buffer, stored patch, current slot and flags unchanged
    CHECK(b.st().edit == edit);
    CHECK(b.st().currentSlot == 2);
    CHECK_FALSE(b.st().edited);
    CHECK_FALSE(b.st().manual);
  }
  SECTION("131 31-messages into an all-INIT bank: 128 written as MANUAL 1..128, 3 dropped") {
    BRig b(Mode::J60);
    b.load("init.vjn6bank", allInitBankJson());
    std::vector<std::vector<uint8_t>> msgs;
    for (int i = 0; i < 131; ++i) msgs.push_back(dumpMsg(0x31, 0, 0, syxPatch(i)));
    b.load("manuals.syx", concat(msgs));
    CHECK(b.notice("imported", "manuals.syx", 128, 3));
    for (int i = 0; i < 128; ++i) {
      CHECK(b.st().bank[i].patch == syxPatch(i));
      CHECK(std::string(b.st().bank[i].name.s) == "MANUAL " + std::to_string(i + 1));
    }
  }
  SECTION("31 messages go to free slots only; a repeated 30 target goes to the next free slot") {
    BRig b(Mode::J60);  // default bank: slots 2-9 are not free
    const auto f = concat({dumpMsg(0x31, 0, 0, syxPatch(1)), dumpMsg(0x30, 0, 0, syxPatch(2)), dumpMsg(0x30, 0, 0, syxPatch(3)),
                           dumpMsg(0x31, 0, 0, syxPatch(4))});
    b.load("mix.syx", f);
    CHECK(b.notice("imported", "mix.syx", 4, 0));
    // the 31 takes the lowest free slot (1); pp 0 was then already written by this import, so both 30s
    // go to the lowest free slots not yet written (10, 11); the last 31 to slot 12
    CHECK(b.st().bank[0].patch == syxPatch(1));
    CHECK(std::string(b.st().bank[0].name.s) == "MANUAL 1");
    CHECK(b.st().bank[9].patch == syxPatch(2));
    CHECK(std::string(b.st().bank[9].name.s) == "A11");
    CHECK(b.st().bank[10].patch == syxPatch(3));
    CHECK(std::string(b.st().bank[10].name.s) == "A11");
    CHECK(b.st().bank[11].patch == syxPatch(4));
    CHECK(std::string(b.st().bank[11].name.s) == "MANUAL 4");
    CHECK(std::string(b.st().bank[1].name.s) == "BRASS");
  }
  SECTION("protect on with a bank: Pr, nothing written, 32 messages not applied") {
    BRig b(Mode::J60, true);
    const Slot before = b.st().bank[0];
    const Patch edit = b.st().edit;
    std::vector<std::vector<uint8_t>> msgs;
    for (int i = 0; i < 4; ++i) msgs.push_back(dumpMsg(0x30, 0, i, syxPatch(i)));
    msgs.push_back(paramMsg(0, 3, 77));
    b.load("bank.syx", concat(msgs));
    CHECK(b.notice("protected"));
    CHECK(ledText(b.r) == "Pr");
    CHECK(b.st().bank[0].patch == before.patch);
    CHECK(b.st().edit == edit);
    CHECK_FALSE(b.st().edited);
  }
}

TEST_CASE("AC-D4.2 export file names and malformed .vjn6 files", "[files]") {
  BRig b(Mode::J60);
  const Patch p = somePatch(4);
  struct NameCase {
    const char* name;
    const char* file;
  };
  for (const NameCase& c : {NameCase{"BRASS 1", "BRASS 1"}, NameCase{"", "patch"}, NameCase{"___", "patch"},
                            NameCase{"con", "con_"}, NameCase{"COM1", "COM1_"}, NameCase{"lpt9", "lpt9_"},
                            NameCase{"Nul", "Nul_"}, NameCase{"a/b:c*d", "a_b_c_d"}, NameCase{"??", "patch"},
                            NameCase{"COM0", "COM0"}, NameCase{"cons", "cons"}}) {
    INFO("name '" << c.name << "'");
    b.load("in.vjn6", files::patchJson(p, makeName(c.name)));
    REQUIRE(std::string(b.st().editName.s) == c.name);
    std::string name;
    b.exportData("patchJson", name);
    CHECK(name == std::string(c.file) + ".vjn6");
    b.exportData("patchSyx", name);
    CHECK(name == std::string(c.file) + ".syx");
  }
  b.ui.clear();
  const std::string good = files::patchJson(p, makeName("GOOD"));
  auto replace = [&](std::string s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
  };
  const std::string sl0 = "\"sliders\": [\n    " + std::to_string(p.v[0]) + ",";
  const std::string bad[] = {
      replace(good, "\"vjn6\": 1", "\"vjn6\": 2"),
      replace(good, "\"vjn6\": 1,", ""),
      replace(good, "\"name\": \"GOOD\",", ""),
      replace(good, "    \"pwm_src\": " + std::to_string(p.v[W_PWM_SRC]) + ",\n", ""),
      replace(good, sl0, "\"sliders\": ["),                                       // 15 sliders
      replace(good, sl0, "\"sliders\": [\n    64.5,"),                            // a float
      replace(good, sl0, "\"sliders\": [\n    64.0,"),                            // a float
      replace(good, sl0, "\"sliders\": [\n    128,"),                             // out of range
      replace(good, sl0, "\"sliders\": [\n    -1,"),
      replace(good, sl0, "\"sliders\": [\n    \"64\","),
      replace(good, "\"name\": \"GOOD\"", "\"name\": 5"),
      good.substr(0, good.size() - 3),                                            // truncated
      "[1,2,3]",
  };
  const Patch before = b.st().edit;
  for (const std::string& t : bad) {
    INFO(t.substr(0, 60));
    b.load("bad.vjn6", t);
    CHECK(b.notice("malformed", "bad.vjn6"));
    CHECK(ledText(b.r) == "Er");
    CHECK(b.st().edit == before);
  }
  // a 20-character name is truncated to 12; an extra key is ignored
  b.load("long.vjn6", replace(good, "\"name\": \"GOOD\"", "\"name\": \"ABCDEFGHIJK MNOPQRST\", \"extra\": [1, {\"a\": 2}]"));
  CHECK(b.notice("loaded"));
  CHECK(std::string(b.st().editName.s) == "ABCDEFGHIJK");
  CHECK(b.st().edit == p);
  b.load("long2.vjn6", replace(good, "\"name\": \"GOOD\"", "\"name\": \"ABCDEFGHIJKLMNOPQRST\""));
  CHECK(std::string(b.st().editName.s) == "ABCDEFGHIJKL");
}
