// State and first run (§4.7, R-D1, R-D6, §15.3): AC-D1.1, AC-D1.2, AC-D6.1, AC-D7.1.
#include <catch2/catch_test_macros.hpp>

#include "state/state.h"
#include "util/analysis.h"
#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
Patch somePatch(int seed) {
  Patch p;
  for (int f = 0; f < kNumFields; ++f) p.v[f] = static_cast<uint8_t>((seed * 29 + f * 13) % 128);
  return p;
}

// §4.2 defaults, written out by hand.
PerfSettings specPerfDefaults() {
  PerfSettings p;
  const int16_t v[P_COUNT] = {
      0,    // key mode POLY 1
      0,    // portamento off
      32,   // portamento time
      64,   // bender DCO sensitivity
      0,    // bender VCF sensitivity
      64,   // LFO trigger sensitivity
      0,    // LFO trigger mode AUTO
      0,    // hold
      0,    // arpeggio off
      0,    // arpeggio mode UP
      1,    // arpeggio range 1
      64,   // arpeggio rate
      1,    // octave transpose N
      0,    // key transpose
      64,   // master tune
      127,  // master volume
      1,    // MIDI channel 1
      1,    // omni on
      3,    // MIDI function III
      1,    // memory protect on
  };
  for (int i = 0; i < P_COUNT; ++i) p.v[i] = v[i];
  return p;
}
// §4.4 defaults (standalone / web: computer keyboard play on).
SettingsValues specSettingDefaults() {
  SettingsValues s;
  const int16_t v[S_COUNT] = {0 /*60*/, 2 /*ORIGINAL*/, 50, 0 /*velocity off*/, 0 /*stereo*/, 2, 1, 0 /*Fit*/};
  for (int i = 0; i < S_COUNT; ++i) s.v[i] = v[i];
  return s;
}

// Every §4.7 field set to a non-default value.
void nonDefault(Rig& r) {
  CoreState& s = r.st();
  for (int i = 0; i < kNumSlots; ++i) {
    s.bank[i].patch = somePatch(i + 1);
    s.bank[i].name = makeName(("SLOT " + std::to_string(i + 1)).c_str());
  }
  s.edit = somePatch(300);
  s.editName = makeName("EDIT ~ BUF");
  s.stored = somePatch(301);
  s.edited = true;
  s.manual = true;
  s.currentSlot = 77;
  s.pendingBank = 5;  // not the bank number of slot 77 in either mode
  s.pendingGroup = 0;
  const int16_t perf[P_COUNT] = {1, 1, 99, 3, 120, 5, 1, 1, 1, 2, 3, 7, 2, -7, 13, 66, 9, 0, 2, 0};
  for (int p = 0; p < P_COUNT; ++p) s.perf.v[p] = perf[p];
  const int16_t set[S_COUNT] = {1, 1, 30, 1, 1, 4, 0, 120};
  for (int k = 0; k < S_COUNT; ++k) s.settings.v[k] = set[k];
  r.apply(messageCmd(LedMessage::None));  // marks the state for publishing
  r.skip(1);
}

void checkEqual(const CoreState& a, const CoreState& b, bool settings = true) {
  for (int i = 0; i < kNumSlots; ++i) {
    CHECK(a.bank[i].patch == b.bank[i].patch);
    CHECK(a.bank[i].name == b.bank[i].name);
  }
  CHECK(a.edit == b.edit);
  CHECK(a.editName == b.editName);
  CHECK(a.stored == b.stored);
  CHECK(a.edited == b.edited);
  CHECK(a.manual == b.manual);
  CHECK(a.currentSlot == b.currentSlot);
  CHECK(a.pendingBank == b.pendingBank);
  CHECK(a.pendingGroup == b.pendingGroup);
  for (int p = 0; p < P_COUNT; ++p) {
    INFO("perf " << p);
    CHECK(a.perf.v[p] == b.perf.v[p]);
  }
  if (settings)
    for (int k = 0; k < S_COUNT; ++k) {
      INFO("setting " << k);
      CHECK(a.settings.v[k] == b.settings.v[k]);
    }
}

void checkFirstRun(Rig& r) {
  const CoreState& s = r.st();
  const Slot* def = defaultBank();
  for (int i = 0; i < kNumSlots; ++i) {
    CHECK(s.bank[i].patch == def[i].patch);
    CHECK(s.bank[i].name == def[i].name);
  }
  CHECK(s.currentSlot == 1);
  CHECK(s.edit == def[0].patch);
  CHECK(std::string(s.editName.s) == "INIT");
  CHECK(s.stored == def[0].patch);
  CHECK_FALSE(s.edited);
  CHECK_FALSE(s.manual);
  CHECK(s.pendingBank == 1);
  CHECK(s.pendingGroup == 0);
  const PerfSettings pd = specPerfDefaults();
  for (int p = 0; p < P_COUNT; ++p) {
    INFO("perf " << p);
    CHECK(s.perf.v[p] == pd.v[p]);
  }
  const SettingsValues sd = specSettingDefaults();
  for (int k = 0; k < S_COUNT; ++k) {
    INFO("setting " << k);
    CHECK(s.settings.v[k] == sd.v[k]);
  }
}

void fixCrc(std::vector<uint8_t>& b) {
  const uint32_t c = crc32(b.data(), b.size() - 4);
  for (int i = 0; i < 4; ++i) b[b.size() - 4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>((c >> (8 * i)) & 0xFF);
}
StateBlob nonDefaultBlob() {
  Rig a(48000.0, Mode::J60);
  nonDefault(a);
  StateBlob blob;
  a.inst->saveState(blob);
  return blob;
}
}  // namespace

TEST_CASE("AC-D1.1 every §4.7 field set to a non-default value survives saveState -> loadState", "[state]") {
  Rig a(48000.0, Mode::J60);
  nonDefault(a);
  StateBlob blob;
  a.inst->saveState(blob);
  REQUIRE(blob.bytes.size() > 10);
  CHECK(std::string(blob.bytes.begin(), blob.bytes.begin() + 4) == "VJN6");
  CHECK(blob.bytes[4] == 1);
  CHECK(blob.bytes[5] == 0);
  Rig b(48000.0, Mode::J60);
  b.toBoundary();
  CHECK(b.inst->loadState(blob));
  b.skip(512);
  checkEqual(b.st(), a.st());
  CHECK(b.core().led().message == LedMessage::None);
  // the engine follows the loaded settings
  CHECK(b.eng().mode() == Mode::J106);
  CHECK(b.eng().oversample() == 4);
  CHECK(b.inst->latencySamples() == Engine::latencyFor(4));
  // and a second round trip is byte-identical
  b.skip(1);
  StateBlob again;
  b.inst->saveState(again);
  CHECK(again.bytes == blob.bytes);
  // panel transients are not saved
  Rig c(48000.0, Mode::J106);
  c.toBoundary();
  c.apply(Command::press(Button::Write));
  c.skip(1);
  StateBlob armed;
  c.inst->saveState(armed);
  Rig d(48000.0, Mode::J106);
  d.inst->loadState(armed);
  d.skip(512);
  CHECK_FALSE(d.core().writeArmed());
}

TEST_CASE("AC-D1.2 corrupt blobs reset with rE; a newer version loads bank, buffers and performance; zero length",
          "[state]") {
  const StateBlob good = nonDefaultBlob();
  auto target = [] {
    std::unique_ptr<Rig> r(new Rig(48000.0, Mode::J106));
    r->toBoundary();
    send(*r, Command::selectSlot(9, Source::Overlay));
    send(*r, Command::setPerf(P_VOLUME, 10, Source::Overlay));
    send(*r, Command::setSetting(S_VARIATION, 70));
    return r;
  };
  struct Bad {
    const char* name;
    std::vector<uint8_t> bytes;
  };
  std::vector<Bad> bad;
  {
    Bad b{"bad CRC", good.bytes};
    b.bytes[200] ^= 0x01;
    bad.push_back(b);
  }
  {
    Bad b{"bad magic", good.bytes};
    b.bytes[0] = 'X';
    fixCrc(b.bytes);
    bad.push_back(b);
  }
  {
    Bad b{"truncated", good.bytes};
    b.bytes.resize(b.bytes.size() - 40);
    bad.push_back(b);
  }
  {
    Bad b{"truncated with a valid CRC", good.bytes};
    b.bytes.resize(b.bytes.size() - 40);
    fixCrc(b.bytes);
    bad.push_back(b);
  }
  {
    Bad b{"version 0", good.bytes};
    b.bytes[4] = 0;
    fixCrc(b.bytes);
    bad.push_back(b);
  }
  {
    Bad b{"too short", {'V', 'J', 'N', '6', 1, 0}};
    bad.push_back(b);
  }
  SECTION("corrupt blobs: first-run state and rE") {
  for (const Bad& b : bad) {
    INFO(b.name);
    auto r = target();
    StateBlob blob;
    blob.bytes = b.bytes;
    CHECK_FALSE(r->inst->loadState(blob));
    r->skip(512);
    checkFirstRun(*r);
    CHECK(r->core().led().message == LedMessage::Reset);
    CHECK(ledText(*r) == "rE");
    CHECK(r->eng().mode() == Mode::J60);
  }
  }
  SECTION("a newer-version blob: bank, buffers and performance settings loaded; the rest ignored; no message") {
    StateBlob newer = good;
    newer.bytes[4] = 2;
    newer.bytes.insert(newer.bytes.end() - 4, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    fixCrc(newer.bytes);
    auto r = target();
    const SettingsValues before = r->st().settings;
    CHECK(r->inst->loadState(newer));
    r->skip(512);
    Rig a(48000.0, Mode::J60);
    nonDefault(a);
    checkEqual(r->st(), a.st(), false);
    for (int k = 0; k < S_COUNT; ++k) CHECK(r->st().settings.v[k] == before.v[k]);
    CHECK(r->core().led().message == LedMessage::None);
  }
  SECTION("a zero-length blob: first-run state, no message") {
    auto r = target();
    CHECK(r->inst->loadState(StateBlob{}));
    r->skip(512);
    checkFirstRun(*r);
    CHECK(r->core().led().message == LedMessage::None);
  }
}

TEST_CASE("AC-D6.1 empty storage: the R-D6 first-run state with the §4.2 and §4.4 defaults", "[state]") {
  Instrument inst(48000.0, 512);
  float L[64], R[64];
  inst.render(L, R, 64);
  Snapshot s;
  inst.snapshot(s);
  const Slot* def = defaultBank();
  for (int i = 0; i < kNumSlots; ++i) {
    CHECK(s.bank[i].patch == def[i].patch);
    CHECK(s.bank[i].name == def[i].name);
  }
  CHECK(s.currentSlot == 1);
  CHECK(s.edit == def[0].patch);
  CHECK(s.stored == def[0].patch);
  CHECK(std::string(s.editName.s) == "INIT");
  CHECK_FALSE(s.edited);
  CHECK_FALSE(s.manual);
  CHECK(s.pendingBank == 1);
  CHECK(s.pendingGroup == 0);
  const PerfSettings pd = specPerfDefaults();
  for (int p = 0; p < P_COUNT; ++p) CHECK(s.perf.v[p] == pd.v[p]);
  const SettingsValues sd = specSettingDefaults();
  for (int k = 0; k < S_COUNT; ++k) CHECK(s.settings.v[k] == sd.v[k]);
  CHECK(std::string(s.led.text, 2) == "11");
  CHECK(s.led.message == LedMessage::None);
  // a reset to the first-run state (zero-length blob) gives the same
  Rig r(48000.0, Mode::J106);
  nonDefault(r);
  r.inst->loadState(StateBlob{});
  r.skip(512);
  checkFirstRun(r);
}

TEST_CASE("AC-D7.1 default bank: slots 1-9 play note 60 in both modes above -60 dBFS; names as §15.3", "[state]") {
  const char* names[9] = {"INIT", "BRASS", "STRINGS", "PAD", "BASS", "PLUCK", "ORGAN", "SWEEP", "SFX"};
  const Slot* def = defaultBank();
  for (int i = 0; i < 9; ++i) CHECK(std::string(def[i].name.s) == names[i]);
  for (int i = 9; i < kNumSlots; ++i) {
    CHECK(std::string(def[i].name.s) == "INIT");
    CHECK(def[i].patch == initPatch());
  }
  for (int i = 1; i < 9; ++i) CHECK(def[i].patch != initPatch());
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int slot = 1; slot <= 9; ++slot) {
      INFO((m == Mode::J106 ? "106" : "60") << " slot " << slot << " " << names[slot - 1]);
      Rig r(48000.0, m);
      r.toBoundary();
      send(r, Command::selectSlot(slot, Source::Host));
      REQUIRE(r.st().edit == def[slot - 1].patch);
      r.toBoundary();
      r.noteOn(60);
      const Stereo s = r.render(36000);
      const double pk = std::max(peakAbs(s.L), peakAbs(s.R));
      CHECK(pk > 0.001);  // -60 dBFS
    }
  }
}
