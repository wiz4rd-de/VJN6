// State and first run (§4.7, R-D1, R-D6, §15.3): AC-D1.1, AC-D1.2, AC-D6.1, AC-D7.1, AC-D7.2.
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
  CHECK(std::string(s.editName.s) == "Strings 1");
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
  CHECK(std::string(s.editName.s) == "Strings 1");
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
  // v3.2: slots 1-56 the factory patches (11-78), 57 INIT, 58-65 the eight original patches, 66-128 INIT
  const char* names[9] = {"Strings 1", "Strings 2", "Strings 3", "Organ 1", "Organ 2", "Organ 3", "Brass", "Phase Brass",
                          "Piano 1"};
  const char* originals[8] = {"BRASS", "STRINGS", "PAD", "BASS", "PLUCK", "ORGAN", "SWEEP", "SFX"};
  const Slot* def = defaultBank();
  for (int i = 0; i < 9; ++i) CHECK(std::string(def[i].name.s) == names[i]);
  for (int i = 0; i < 8; ++i) CHECK(std::string(def[57 + i].name.s) == originals[i]);
  for (int i = 0; i < kNumSlots; ++i) {
    INFO("slot " << i + 1);
    if (i == 56 || i >= 65) {
      CHECK(std::string(def[i].name.s) == "INIT");
      CHECK(def[i].patch == initPatch());
    } else {
      CHECK(def[i].patch != initPatch());
    }
  }
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

TEST_CASE("AC-D7.2 the generated default bank: slots 1-56 equal the §15.3 factory byte table; 57 INIT; 58-65 the originals",
          "[state]") {
  struct Row {
    const char* name;
    uint8_t v[kNumFields];  // B0-B15, then W0-W8 (§4.1 order)
  };
  // §15.3 factory byte table, slots 1-56 in order; the Juno-60 manual's patch number in the comment
  // clang-format off
  const Row factory[56] = {
    {"Strings 1",     {76, 0, 0, 0, 0, 89, 0, 0, 0, 127, 64, 51, 0, 127, 57, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0}},  // 11
    {"Strings 2",     {51, 0, 0, 76, 0, 89, 0, 0, 0, 127, 38, 51, 0, 127, 57, 0, 1, 1, 1, 0, 2, 0, 0, 0, 0}},  // 12
    {"Strings 3",     {38, 102, 0, 89, 0, 64, 0, 0, 0, 127, 38, 38, 0, 127, 76, 127, 1, 1, 1, 1, 2, 0, 0, 0, 0}},  // 13
    {"Organ 1",       {25, 102, 0, 64, 0, 51, 76, 57, 0, 127, 64, 0, 0, 0, 0, 127, 1, 1, 0, 1, 1, 1, 0, 1, 0}},  // 14
    {"Organ 2",       {64, 51, 0, 70, 0, 44, 70, 51, 0, 127, 64, 0, 13, 0, 13, 102, 1, 1, 0, 1, 1, 0, 0, 1, 0}},  // 15
    {"Organ 3",       {64, 51, 0, 70, 0, 44, 70, 44, 0, 127, 64, 0, 13, 0, 13, 102, 2, 1, 0, 1, 2, 0, 0, 1, 0}},  // 16
    {"Brass",         {64, 83, 19, 0, 0, 0, 0, 108, 0, 51, 89, 32, 51, 76, 25, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0}},  // 17
    {"Phase Brass",   {76, 0, 0, 127, 0, 38, 13, 70, 0, 127, 51, 25, 51, 51, 38, 127, 1, 1, 1, 0, 1, 2, 0, 1, 0}},  // 18
    {"Piano 1",       {76, 38, 57, 76, 0, 13, 0, 89, 0, 51, 89, 0, 102, 19, 38, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0}},  // 21
    {"Piano 2",       {51, 0, 0, 51, 0, 44, 0, 32, 25, 102, 102, 0, 95, 0, 44, 57, 2, 1, 0, 1, 0, 1, 0, 0, 0}},  // 22
    {"Celesta",       {44, 76, 0, 64, 0, 44, 102, 0, 0, 127, 76, 0, 83, 25, 70, 127, 1, 1, 1, 0, 0, 2, 0, 0, 1}},  // 23
    {"Mellow Piano",  {64, 0, 0, 64, 0, 38, 0, 32, 13, 114, 89, 13, 95, 25, 108, 127, 1, 1, 0, 0, 1, 1, 0, 0, 0}},  // 24
    {"Harpsichd 1",   {64, 51, 0, 38, 0, 38, 0, 64, 0, 89, 51, 0, 76, 44, 32, 89, 2, 1, 0, 1, 1, 1, 0, 0, 1}},  // 25
    {"Harpsichd 2",   {70, 76, 0, 25, 0, 64, 32, 38, 0, 127, 64, 0, 64, 19, 64, 108, 2, 1, 0, 1, 2, 1, 0, 0, 1}},  // 26
    {"Guitar",        {76, 76, 0, 76, 0, 38, 0, 57, 19, 64, 114, 0, 70, 44, 83, 127, 1, 1, 0, 0, 0, 1, 0, 0, 2}},  // 27
    {"Synth Harp",    {38, 102, 0, 0, 0, 38, 0, 64, 0, 102, 76, 0, 70, 38, 64, 127, 1, 0, 1, 0, 1, 1, 0, 0, 0}},  // 28
    {"Bass 1",        {64, 76, 0, 64, 0, 38, 32, 44, 0, 0, 64, 0, 51, 13, 32, 38, 0, 1, 1, 1, 1, 1, 0, 1, 0}},  // 31
    {"Bass 2",        {64, 76, 0, 64, 0, 38, 64, 57, 0, 64, 51, 0, 38, 44, 32, 38, 0, 1, 1, 0, 1, 1, 0, 1, 0}},  // 32
    {"Clavichord 1",  {76, 32, 51, 102, 0, 0, 38, 102, 0, 76, 89, 0, 64, 44, 19, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0}},  // 33
    {"Clavichord 2",  {13, 0, 0, 102, 0, 70, 89, 25, 32, 89, 127, 0, 57, 25, 25, 127, 0, 1, 0, 0, 0, 1, 0, 0, 1}},  // 34
    {"Pizzicato 1",   {64, 76, 0, 44, 0, 57, 38, 38, 38, 127, 102, 0, 25, 44, 70, 38, 1, 1, 0, 0, 1, 1, 0, 0, 0}},  // 35
    {"Pizzicato 2",   {64, 76, 0, 25, 0, 64, 38, 38, 0, 127, 76, 0, 38, 38, 51, 38, 2, 1, 0, 1, 2, 1, 0, 0, 0}},  // 36
    {"Xylophone",     {64, 0, 0, 64, 0, 51, 64, 38, 0, 76, 127, 0, 44, 0, 44, 127, 2, 0, 0, 1, 0, 1, 0, 0, 1}},  // 37
    {"Glockenspiel",  {64, 0, 0, 0, 0, 57, 64, 38, 0, 76, 114, 0, 38, 32, 64, 0, 2, 1, 0, 0, 0, 1, 0, 0, 1}},  // 38
    {"Violine",       {76, 76, 25, 0, 0, 83, 0, 0, 0, 127, 89, 51, 0, 127, 51, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1}},  // 41
    {"Trumpet",       {32, 83, 19, 0, 0, 0, 0, 108, 0, 51, 89, 32, 51, 76, 25, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0}},  // 42
    {"Horn",          {32, 89, 0, 0, 0, 25, 0, 70, 25, 51, 89, 51, 64, 76, 38, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0}},  // 43
    {"Tuba",          {32, 89, 19, 0, 0, 19, 0, 76, 0, 51, 127, 38, 51, 51, 38, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0}},  // 44
    {"Flute",         {70, 64, 0, 0, 19, 64, 0, 0, 25, 76, 127, 25, 76, 64, 32, 0, 2, 0, 1, 0, 0, 1, 0, 0, 1}},  // 45
    {"Clarinet",      {64, 83, 19, 0, 0, 64, 38, 32, 0, 76, 76, 32, 76, 76, 32, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1}},  // 46
    {"Oboe",          {70, 83, 19, 83, 0, 57, 64, 32, 0, 64, 127, 25, 76, 76, 32, 0, 1, 1, 0, 0, 0, 1, 0, 0, 3}},  // 47
    {"English Horn",  {64, 89, 25, 83, 0, 64, 89, 0, 19, 64, 127, 25, 76, 76, 32, 0, 0, 1, 0, 0, 0, 1, 0, 0, 3}},  // 48
    {"Funny Cat",     {76, 25, 38, 0, 0, 19, 95, 64, 25, 64, 102, 32, 51, 127, 13, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1}},  // 51
    {"Wah Brass",     {76, 25, 38, 0, 0, 38, 89, 57, 0, 76, 89, 38, 38, 51, 25, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0}},  // 52
    {"Phase Combi",   {76, 25, 0, 102, 0, 76, 25, 38, 0, 25, 38, 0, 89, 25, 25, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0}},  // 53
    {"Reed 1",        {76, 25, 51, 0, 0, 13, 76, 89, 0, 64, 76, 0, 108, 64, 13, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0}},  // 54
    {"Popcorn",       {0, 0, 0, 0, 0, 32, 25, 70, 0, 127, 102, 0, 38, 25, 0, 127, 2, 0, 0, 1, 0, 1, 0, 0, 1}},  // 55
    {"Reed 2",        {38, 102, 0, 0, 0, 25, 0, 76, 0, 102, 64, 0, 70, 38, 76, 127, 2, 0, 0, 1, 1, 1, 0, 0, 0}},  // 56
    {"Reed 3",        {76, 25, 25, 64, 0, 38, 25, 38, 0, 127, 76, 32, 0, 127, 25, 127, 2, 0, 0, 1, 0, 1, 0, 0, 0}},  // 57
    {"PWM Chorus",    {38, 0, 0, 64, 0, 102, 0, 0, 0, 127, 25, 38, 0, 127, 51, 127, 1, 1, 0, 1, 2, 0, 0, 0, 0}},  // 58
    {"Synth Organ",   {57, 76, 0, 83, 0, 32, 0, 64, 25, 89, 64, 0, 25, 64, 32, 95, 1, 1, 0, 1, 2, 1, 0, 1, 0}},  // 61
    {"Effect 1",      {57, 76, 19, 127, 0, 83, 0, 57, 0, 89, 89, 0, 64, 0, 70, 89, 2, 1, 1, 1, 1, 1, 1, 0, 0}},  // 62
    {"Effect 2",      {70, 114, 0, 38, 0, 83, 38, 51, 0, 13, 51, 83, 70, 25, 83, 83, 1, 0, 1, 1, 1, 0, 1, 1, 0}},  // 63
    {"Space Harp",    {70, 0, 25, 0, 0, 83, 64, 70, 0, 127, 76, 0, 102, 102, 114, 0, 1, 0, 1, 0, 1, 2, 0, 0, 0}},  // 64
    {"Funk",          {38, 32, 0, 76, 0, 95, 76, 64, 0, 57, 25, 76, 64, 0, 0, 127, 1, 1, 1, 1, 1, 1, 1, 1, 0}},  // 65
    {"Space 1",       {76, 89, 25, 57, 0, 83, 89, 70, 0, 127, 38, 0, 102, 0, 38, 127, 1, 1, 0, 1, 1, 1, 1, 1, 0}},  // 66
    {"Mysterious",    {76, 102, 25, 102, 0, 102, 89, 76, 32, 0, 64, 0, 127, 0, 127, 127, 1, 1, 1, 0, 0, 2, 1, 0, 0}},  // 67
    {"Space 2",       {38, 38, 0, 76, 0, 25, 108, 76, 0, 127, 25, 127, 127, 127, 127, 102, 1, 1, 1, 0, 1, 1, 0, 0, 0}},  // 68
    {"Percussive 1",  {0, 0, 0, 0, 127, 51, 127, 19, 0, 127, 127, 0, 38, 0, 51, 0, 2, 0, 0, 0, 0, 2, 0, 0, 1}},  // 71
    {"Percussive 2",  {0, 0, 0, 0, 0, 64, 127, 44, 0, 127, 127, 0, 38, 0, 51, 0, 1, 0, 0, 0, 0, 2, 1, 0, 1}},  // 72
    {"Whistle",       {70, 64, 0, 0, 25, 44, 127, 19, 25, 127, 102, 38, 0, 127, 13, 0, 2, 0, 0, 0, 0, 1, 0, 0, 1}},  // 73
    {"Effect 3",      {70, 51, 0, 0, 0, 44, 127, 0, 25, 127, 127, 0, 51, 70, 89, 0, 1, 0, 0, 0, 0, 2, 0, 0, 1}},  // 74
    {"UFO",           {76, 0, 0, 0, 25, 0, 127, 89, 51, 127, 51, 0, 76, 127, 102, 0, 2, 0, 0, 0, 1, 1, 0, 0, 0}},  // 75
    {"Space 3",       {76, 0, 0, 0, 25, 64, 127, 51, 0, 127, 64, 0, 127, 0, 102, 0, 2, 0, 0, 0, 1, 1, 1, 0, 0}},  // 76
    {"Surf",          {0, 0, 0, 0, 127, 76, 0, 0, 76, 127, 114, 0, 51, 127, 102, 127, 1, 0, 0, 0, 0, 2, 0, 0, 0}},  // 77
    {"Synth Drum",    {0, 0, 0, 0, 0, 25, 127, 51, 0, 127, 76, 0, 64, 0, 76, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0}},  // 78
  };
  // the eight original patches as generated before v3.2 (209a8ab, slots 2-9 of the v3.1 bank)
  const Row originals[8] = {
    {"BRASS",         {60, 40, 6, 0, 0, 45, 25, 72, 0, 64, 110, 22, 58, 76, 34, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0}},
    {"STRINGS",       {52, 0, 0, 70, 0, 72, 10, 10, 0, 40, 110, 55, 60, 110, 60, 0, 1, 1, 1, 1, 2, 0, 0, 0, 1}},
    {"PAD",           {30, 50, 0, 90, 0, 58, 30, 30, 12, 50, 105, 80, 90, 100, 85, 30, 1, 1, 0, 1, 3, 0, 0, 0, 0}},
    {"BASS",          {64, 0, 0, 0, 0, 30, 45, 70, 0, 30, 120, 0, 50, 30, 20, 100, 0, 0, 1, 1, 0, 0, 0, 0, 0}},
    {"PLUCK",         {64, 0, 0, 40, 0, 40, 50, 75, 0, 80, 115, 0, 45, 0, 40, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1}},
    {"ORGAN",         {70, 30, 4, 32, 0, 85, 0, 0, 0, 64, 105, 0, 0, 127, 10, 70, 1, 1, 0, 1, 1, 1, 0, 1, 1}},
    {"SWEEP",         {25, 0, 0, 0, 0, 40, 95, 20, 70, 64, 110, 30, 80, 90, 70, 40, 1, 0, 1, 1, 2, 0, 0, 0, 1}},
    {"SFX",           {100, 0, 60, 0, 90, 60, 105, 80, 40, 0, 110, 0, 75, 20, 70, 0, 2, 0, 1, 1, 3, 0, 1, 0, 2}},
  };
  // clang-format on
  const Slot* def = defaultBank();
  auto check = [&](int slot, const Row& row) {
    INFO("slot " << slot << " " << row.name);
    CHECK(std::string(def[slot - 1].name.s) == row.name);
    for (int f = 0; f < kNumFields; ++f) {
      INFO("field " << f);
      CHECK(int(def[slot - 1].patch.v[f]) == int(row.v[f]));
    }
  };
  for (int i = 0; i < 56; ++i) check(i + 1, factory[i]);
  CHECK(std::string(def[56].name.s) == "INIT");
  CHECK(def[56].patch == initPatch());
  for (int i = 0; i < 8; ++i) check(58 + i, originals[i]);
}
