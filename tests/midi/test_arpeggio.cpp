// Arpeggio acceptance tests (R-E11, F-8): AC-E11.5. The recorder steps the engine from event to event
// (control boundaries and arpeggio events) so every arpeggio note-on/off is timed to the exact sample.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "util/core_helpers.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
struct ArpEv {
  int64_t t;
  int note;
  bool on;
};

class ArpRec {
 public:
  explicit ArpRec(Rig& r) : r_(r) { sample(-1); }

  // Advances to `until` (not processing events at `until` itself).
  void run(int64_t until) {
    while (r_.eng().now() < until) {
      const int64_t now = r_.eng().now();
      const int64_t e = std::min(r_.eng().nextBoundary(), r_.eng().nextArpEvent());
      if (e <= now) {
        r_.skip(1);
        sample(now);
      } else {
        r_.skip(static_cast<long>(std::min(e, until) - now));
      }
    }
  }
  void runFor(int64_t n) { run(r_.eng().now() + n); }
  // Runs until `count` more note-ons were recorded (or `limit` samples passed).
  void runOns(int count, int64_t limit) {
    const std::size_t want = ons().size() + static_cast<std::size_t>(count);
    const int64_t end = r_.eng().now() + limit;
    while (ons().size() < want && r_.eng().now() < end) run(std::min(end, r_.eng().now() + 32));
  }
  // MIDI realtime byte applied at `t` (a 60-mode control boundary: a multiple of 32 samples).
  void realtimeAt(int64_t t, uint8_t b) {
    run(t);
    r_.inst->midi(&b, 1, 0);
  }

  std::vector<ArpEv> log;
  std::vector<ArpEv> ons() const {
    std::vector<ArpEv> v;
    for (const ArpEv& e : log)
      if (e.on) v.push_back(e);
    return v;
  }
  std::vector<int> onNotes() const {
    std::vector<int> v;
    for (const ArpEv& e : log)
      if (e.on) v.push_back(e.note);
    return v;
  }
  // The note-off that follows the note-on at index i of log.
  int64_t offAfter(std::size_t i) const {
    for (std::size_t j = i + 1; j < log.size(); ++j)
      if (!log[j].on && log[j].note == log[i].note) return log[j].t;
    return -1;
  }
  void clear() { log.clear(); }

 private:
  void sample(int64_t t) {
    for (int i = 0; i < k::kVoices; ++i) {
      const Voice& v = r_.eng().voice(i);
      const bool on = v.held && Engine::sourceOf(v.key) == Engine::kSrcArp;
      if (t >= 0) {
        if (on_[i] && (!on || v.onOrder != order_[i])) log.push_back({t, note_[i], false});
        if (on && (!on_[i] || v.onOrder != order_[i])) log.push_back({t, v.note, true});
      }
      on_[i] = on;
      order_[i] = v.onOrder;
      note_[i] = v.note;
    }
  }
  Rig& r_;
  bool on_[k::kVoices] = {};
  uint64_t order_[k::kVoices] = {};
  int note_[k::kVoices] = {};
};

double periodSamples(double fs, int rate) { return fs / (1.5 * std::pow(33.3, rate / 127.0)); }

std::unique_ptr<Rig> arpRig(int mode, int range, int rate) {
  std::unique_ptr<Rig> r(new Rig(48000.0, Mode::J60));
  r->testPatch();
  r->apply(Command::setPerf(P_ARP_MODE, mode, Source::Panel));
  r->apply(Command::setPerf(P_ARP_RANGE, range, Source::Panel));
  r->apply(Command::setPerf(P_ARP_RATE, rate, Source::Panel));
  r->apply(Command::press(Button::ArpOn));
  r->toBoundary();
  return r;
}

std::vector<int> expectedCycle(const std::vector<int>& notes, int mode, int range) {
  std::vector<int> p;
  for (int o = 0; o < range; ++o)
    for (int n : notes)
      if (n + 12 * o <= 127) p.push_back(n + 12 * o);
  std::sort(p.begin(), p.end());
  p.erase(std::unique(p.begin(), p.end()), p.end());
  if (mode == 0) return p;
  if (mode == 2) return std::vector<int>(p.rbegin(), p.rend());
  std::vector<int> c = p;
  for (int i = static_cast<int>(p.size()) - 2; i >= 1; --i) c.push_back(p[static_cast<std::size_t>(i)]);
  return c;
}
}  // namespace

TEST_CASE("AC-E11.5 arpeggio order: UP / U&D / DOWN over 1-3 octaves, U&D without repeated turnaround notes", "[assign]") {
  const std::vector<int> chord = {60, 64, 67};
  for (int mode = 0; mode <= 2; ++mode) {
    for (int range = 1; range <= 3; ++range) {
      INFO("mode " << mode << " range " << range);
      auto r = arpRig(mode, range, 127);
      ArpRec rec(*r);
      for (int n : chord) r->noteOn(n);  // a chord: all three notes reach the same control boundary
      const std::vector<int> cycle = expectedCycle(chord, mode, range);
      rec.runOns(static_cast<int>(cycle.size() * 2), 48000 * 2);
      std::vector<int> expect = cycle;
      expect.insert(expect.end(), cycle.begin(), cycle.end());
      CHECK(rec.onNotes() == expect);
      const auto on = rec.onNotes();
      for (std::size_t i = 1; i < on.size(); ++i) CHECK(on[i] != on[i - 1]);  // never the same note twice in a row
    }
  }
}

TEST_CASE("AC-E11.5 arpeggio order with a chord played top-down and steps above 127 skipped", "[assign]") {
  auto r = arpRig(0, 3, 127);
  ArpRec rec(*r);
  for (int n : {104, 96, 88}) r->noteOn(n);  // played downwards: UP still starts at the lowest note
  rec.runOns(8, 48000);
  // octaves: 88 96 104 | 100 108 116 | 112 120 (104 + 24 = 128 is skipped)
  CHECK(rec.onNotes() == std::vector<int>{88, 96, 100, 104, 108, 112, 116, 120});
  rec.clear();
  rec.runOns(1, 48000);
  CHECK(rec.onNotes() == std::vector<int>{88});
}

TEST_CASE("AC-E11.5 arpeggio rate 1.5 and 50 steps/s, note-off at 50 % of the step", "[assign]") {
  for (int rate : {0, 127}) {
    INFO("rate " << rate);
    auto r = arpRig(0, 1, rate);
    ArpRec rec(*r);
    r->noteOn(60);
    r->noteOn(64);
    rec.runOns(5, 48000 * 4);
    const auto on = rec.ons();
    REQUIRE(on.size() == 5);
    const double measured = 48000.0 * 4.0 / static_cast<double>(on.back().t - on.front().t);
    const double nominal = rate == 0 ? 1.5 : 50.0;
    CHECK(std::fabs(measured / nominal - 1.0) < 0.02);
    rec.runFor(static_cast<int64_t>(periodSamples(48000.0, rate)));
    const double P = periodSamples(48000.0, rate);
    int checked = 0;
    for (std::size_t i = 0; i < rec.log.size(); ++i) {
      if (!rec.log[i].on) continue;
      const int64_t off = rec.offAfter(i);
      if (off < 0) continue;
      INFO("step at " << rec.log[i].t << " off at " << off << " half period " << P / 2);
      CHECK(std::fabs(static_cast<double>(off - rec.log[i].t) - P / 2) <= 1.0);
      ++checked;
    }
    CHECK(checked >= 5);
  }
}

TEST_CASE("AC-E11.5 order changes: adding D to C-E-G while at E (UP) plays G next; removing takes effect at the next step",
          "[assign]") {
  auto r = arpRig(0, 1, 127);
  ArpRec rec(*r);
  for (int n : {60, 64, 67}) r->noteOn(n);
  rec.runOns(2, 48000);
  REQUIRE(rec.onNotes() == std::vector<int>{60, 64});
  r->noteOn(62);  // D added while E plays
  rec.runOns(3, 48000);
  CHECK(rec.onNotes() == std::vector<int>{60, 64, 67, 60, 62});
  r->noteOff(64);  // E removed while D plays: the nearest note above D is G
  rec.runOns(2, 48000);
  CHECK(rec.onNotes() == std::vector<int>{60, 64, 67, 60, 62, 67, 60});
  // U&D keeps its direction: falling, a note added above the last pitch is skipped until the turn
  auto u = arpRig(1, 1, 127);
  ArpRec ru(*u);
  for (int n : {60, 64, 67}) u->noteOn(n);
  ru.runOns(4, 48000);
  REQUIRE(ru.onNotes() == std::vector<int>{60, 64, 67, 64});
  u->noteOn(65);  // falling from 64: next is the nearest note below 64
  ru.runOns(3, 48000);
  CHECK(ru.onNotes() == std::vector<int>{60, 64, 67, 64, 60, 64, 65});
}

TEST_CASE("AC-E11.5 no arpeggio notes: nothing plays; notes arriving again start from the first step", "[assign]") {
  auto r = arpRig(2, 1, 127);  // DOWN
  ArpRec rec(*r);
  for (int n : {60, 64, 67}) r->noteOn(n);
  rec.runOns(2, 48000);
  CHECK(rec.onNotes() == std::vector<int>{67, 64});
  for (int n : {60, 64, 67}) r->noteOff(n);
  rec.runFor(9600);
  rec.clear();
  rec.runFor(48000);
  CHECK(rec.ons().empty());
  CHECK(countHeld(*r) == 0);
  for (int n : {62, 69, 65}) r->noteOn(n);
  rec.runOns(2, 48000);
  CHECK(rec.onNotes() == std::vector<int>{69, 65});
}

TEST_CASE("AC-E11.5 MIDI clock: a step on the first F8 after the notes appear, then every 6th F8; gate 50 % of 6 F8",
          "[assign]") {
  const int64_t I = 1024;  // F8 interval (a multiple of the 60-mode control step)
  auto r = arpRig(0, 1, 127);
  ArpRec rec(*r);
  int64_t t = 0;
  for (int k = 1; k <= 3; ++k) rec.realtimeAt(t = k * I, 0xF8);  // clock running
  rec.run(3500);
  r->noteOn(60);
  r->noteOn(64);
  rec.run(4 * I);
  CHECK(rec.ons().empty());  // waits for the next F8
  for (int k = 4; k <= 17; ++k) rec.realtimeAt(t = k * I, 0xF8);
  rec.run(t + 64);
  auto on = rec.ons();
  REQUIRE(on.size() == 3);
  CHECK(on[0].t == 4 * I);
  CHECK(on[1].t == 10 * I);
  CHECK(on[2].t == 16 * I);
  CHECK(rec.onNotes() == std::vector<int>{60, 64, 60});
  for (std::size_t i = 0; i < rec.log.size(); ++i) {
    if (!rec.log[i].on) continue;
    const int64_t off = rec.offAfter(i);
    if (off >= 0) CHECK(std::llabs(off - rec.log[i].t - 3 * I) <= 1);
  }
  // FA: the next F8 steps
  rec.realtimeAt(t = 18 * I, 0xF8);
  rec.realtimeAt(18 * I + 512, 0xFA);
  rec.realtimeAt(t = 19 * I, 0xF8);
  rec.run(t + 64);
  on = rec.ons();
  REQUIRE(on.size() == 4);
  CHECK(on[3].t == 19 * I);
  for (int k = 20; k <= 25; ++k) rec.realtimeAt(t = k * I, 0xF8);
  rec.run(t + 64);
  on = rec.ons();
  REQUIRE(on.size() == 5);
  CHECK(on[4].t == 25 * I);
}

TEST_CASE("AC-E11.5 MIDI clock stops: the next step at max(500 ms after the last F8, 1/f after the last clocked step)",
          "[assign]") {
  const int64_t I = 1024;
  SECTION("rate 127: 500 ms after the last F8, then internal timing") {
    auto r = arpRig(0, 1, 127);
    ArpRec rec(*r);
    r->noteOn(60);
    r->noteOn(64);
    rec.run(I);
    int64_t t = 0;
    for (int k = 1; k <= 9; ++k) rec.realtimeAt(t = k * I + 2048, 0xF8);  // last F8 is not a stepping one
    const int64_t lastF8 = t;
    rec.clear();
    rec.runOns(3, 48000 * 2);
    const auto on = rec.ons();
    REQUIRE(on.size() == 3);
    CHECK(std::llabs(on[0].t - (lastF8 + 24000)) <= 32);
    const double P = periodSamples(48000.0, 127);
    CHECK(std::fabs(static_cast<double>(on[1].t - on[0].t) - P) <= 1.0);
    CHECK(std::fabs(static_cast<double>(on[2].t - on[1].t) - P) <= 1.0);
  }
  SECTION("rate 0: 1/f after the last clocked step") {
    auto r = arpRig(0, 1, 0);
    ArpRec rec(*r);
    r->noteOn(60);
    r->noteOn(64);
    rec.run(I);
    int64_t t = 0;
    for (int k = 1; k <= 7; ++k) rec.realtimeAt(t = k * I + 2048, 0xF8);  // the 1st and the 7th F8 step
    rec.run(t + 64);
    const auto clocked = rec.ons();
    REQUIRE(clocked.size() >= 2);
    const int64_t lastStep = clocked.back().t;
    REQUIRE(lastStep == t);
    rec.clear();
    rec.runOns(1, 48000 * 2);
    const auto on = rec.ons();
    REQUIRE(on.size() == 1);
    CHECK(std::llabs(on[0].t - (lastStep + static_cast<int64_t>(std::ceil(periodSamples(48000.0, 0))))) <= 32);
  }
  SECTION("notes appear while the clock runs and the clock stops before the next F8") {
    auto r = arpRig(0, 1, 127);
    ArpRec rec(*r);
    int64_t t = 0;
    for (int k = 1; k <= 3; ++k) rec.realtimeAt(t = k * I, 0xF8);
    r->noteOn(60);
    r->noteOn(64);
    rec.runOns(1, 48000 * 2);
    const auto on = rec.ons();
    REQUIRE(on.size() == 1);
    CHECK(std::llabs(on[0].t - (t + 24000)) <= 32);
  }
  SECTION("FA and then no clock: internal timing resumes") {
    auto r = arpRig(0, 1, 127);
    ArpRec rec(*r);
    int64_t t = 0;
    for (int k = 1; k <= 3; ++k) rec.realtimeAt(t = k * I, 0xF8);
    r->noteOn(60);
    r->noteOn(64);
    rec.realtimeAt(t = 4 * I, 0xF8);  // steps
    rec.realtimeAt(4 * I + 512, 0xFA);
    rec.runOns(1, 48000 * 2);
    const auto on = rec.ons();
    REQUIRE(on.size() == 2);
    CHECK(on[0].t == 4 * I);
    CHECK(std::llabs(on[1].t - (t + 24000)) <= 32);
  }
}

TEST_CASE("AC-E11.5 RANGE and MODE changes while the arpeggio runs take effect at the next step", "[assign]") {
  auto r = arpRig(0, 1, 127);
  ArpRec rec(*r);
  for (int n : {60, 64, 67}) r->noteOn(n);
  rec.runOns(3, 48000);
  REQUIRE(rec.onNotes() == std::vector<int>{60, 64, 67});
  r->apply(Command::setPerf(P_ARP_RANGE, 2, Source::Panel));  // at G: the next step is the nearest note above
  rec.runOns(4, 48000);
  CHECK(rec.onNotes() == std::vector<int>{60, 64, 67, 72, 76, 79, 60});
  r->apply(Command::setPerf(P_ARP_MODE, 2, Source::Panel));  // DOWN from 60: wraps to the top
  rec.runOns(2, 48000);
  CHECK(rec.onNotes() == std::vector<int>{60, 64, 67, 72, 76, 79, 60, 79, 76});
  r->apply(Command::setPerf(P_ARP_RANGE, 1, Source::Panel));  // at 76: the nearest note below is 67
  rec.runOns(2, 48000);
  CHECK(rec.onNotes() == std::vector<int>{60, 64, 67, 72, 76, 79, 60, 79, 76, 67, 64});
}
