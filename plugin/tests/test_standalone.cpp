// AC-F1.1 and AC-F13.1 standalone parts (state.bin, state.bad, bank.vjn6bank in a test-overridden
// state directory), autosave timing, the UI connection (R-S3 first full state, env).
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "StateStore.h"
#include "helpers.h"
#include "state/state.h"
#include "vjn6/files.h"

using namespace vjn6test;

namespace {
void setStateDirVariable(const juce::String& value) {
#if JUCE_WINDOWS
  _putenv_s("VJN6_STATE_DIR", value.toRawUTF8());
#else
  if (value.isEmpty()) unsetenv("VJN6_STATE_DIR");
  else setenv("VJN6_STATE_DIR", value.toRawUTF8(), 1);
#endif
}

// A fresh state directory, exported as VJN6_STATE_DIR for the processors created meanwhile.
struct StateDir {
  StateDir() {
    dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("vjn6-state", "");
    dir.createDirectory();
    setStateDirVariable(dir.getFullPathName());
  }
  ~StateDir() {
    unblockWrites();
    dir.deleteRecursively();
    setStateDirVariable({});
  }
  juce::File file(const char* name) const { return dir.getChildFile(name); }

  // Makes writing state.bin fail: a read-only folder (POSIX) or an open handle on state.bin
  // without delete sharing, so it cannot be replaced (Windows).
  void blockWrites() {
#if JUCE_WINDOWS
    lock = std::make_unique<juce::FileInputStream>(file("state.bin"));
#else
    dir.setReadOnly(true, false);
#endif
  }
  void unblockWrites() {
#if JUCE_WINDOWS
    lock.reset();
#else
    dir.setReadOnly(false, false);
#endif
  }

  juce::File dir;
  std::unique_ptr<juce::FileInputStream> lock;
};

std::unique_ptr<vjn6::CoreState> decodeFile(const juce::File& f) {
  juce::MemoryBlock m;
  REQUIRE(f.loadFileAsData(m));
  auto st = std::make_unique<vjn6::CoreState>();
  vjn6::firstRunState(*st, true);
  const auto r = vjn6::decodeState(static_cast<const uint8_t*>(m.getData()), m.getSize(), *st);
  REQUIRE(r == vjn6::BlobResult::Ok);
  return st;
}

struct UiCapture {
  explicit UiCapture(VJN6AudioProcessor& p) {
    p.setUiSink([this](const std::vector<std::string>& msgs) {
      for (const auto& m : msgs) messages.push_back(m);
    });
  }
  bool contains(const std::string& needle) const {
    for (const auto& m : messages)
      if (m.find(needle) != std::string::npos) return true;
    return false;
  }
  std::vector<std::string> messages;
};
}  // namespace

TEST_CASE("AC-F1.1 standalone: saved state on slot 5 is restored and a MIDI note at the first render sounds",
          "[AC-F1.1]") {
  StateDir sd;
  {
    auto s = makeProcessor(true);
    TestHost host(*s);
    s->handleUiMessage(R"({"t":"selectSlot","slot":5})");
    host.run(4);
    REQUIRE(snapshotOf(*s)->currentSlot == 5);
  }  // quit: saves
  REQUIRE(sd.file("state.bin").existsAsFile());

  auto s = makeProcessor(true);
  auto snap = snapshotOf(*s);
  CHECK(snap->currentSlot == 5);
  CHECK(snap->led.message == vjn6::LedMessage::None);
  CHECK(s->getCurrentProgram() == 4);
  TestHost host(*s);
  CHECK(host.process(midiOf({0x90, 48, 100})) > 1e-4);  // the first render already sounds
  CHECK(snapshotOf(*s)->settings.v[vjn6::S_KBD_PLAY] == 1);  // standalone default: keyboard play on
}

TEST_CASE("AC-F1.1 standalone: a corrupt state.bin is kept as state.bad and the start shows rE", "[AC-F1.1]") {
  StateDir sd;
  sd.file("state.bad").replaceWithText("an older bad file");
  sd.file("state.bin").replaceWithText("VJN6 but not a valid state blob");
  auto s = makeProcessor(true);
  auto snap = snapshotOf(*s);
  CHECK(snap->led.message == vjn6::LedMessage::Reset);
  CHECK(std::string(snap->led.text, 2) == "rE");
  CHECK(snap->currentSlot == 1);
  CHECK(sd.file("state.bad").loadFileAsString() == "VJN6 but not a valid state blob");
  CHECK(!sd.file("state.bin").existsAsFile());
  // the next save writes a valid state.bin again
  CHECK(s->saveStandaloneStateNow().isEmpty());
  CHECK(decodeFile(sd.file("state.bin"))->currentSlot == 1);
}

TEST_CASE("AC-F13.1 standalone: a slider edit and a WRITE, quit within 1 s", "[AC-F13.1]") {
  StateDir sd;
  {
    auto s = makeProcessor(true);
    s->handleUiMessage(R"({"t":"perf","p":19,"v":0,"src":"overlay"})");  // protect off
    s->handleUiMessage(R"({"t":"field","f":5,"v":77})");
    s->handleUiMessage(R"({"t":"press","b":"write"})");
    s->handleUiMessage(R"({"t":"press","b":"bank","i":2})");
    s->handleUiMessage(R"({"t":"press","b":"patch","i":2})");
  }  // quit right away (no autosave yet)
  auto st = decodeFile(sd.file("state.bin"));
  CHECK(st->edit.v[5] == 77);
  CHECK(st->bank[9].patch.v[5] == 77);
  CHECK(st->currentSlot == 10);
  CHECK(!st->edited);
  CHECK(sd.file("bank.vjn6bank").loadFileAsString().toStdString() == vjn6::files::bankJson(st->bank));

  // relaunch shows the same state
  auto s = makeProcessor(true);
  auto snap = snapshotOf(*s);
  CHECK(snap->currentSlot == 10);
  CHECK(snap->bank[9].patch.v[5] == 77);
}

TEST_CASE("AC-F13.1 standalone: a failed write keeps the previous state.bin and shows the status line", "[AC-F13.1]") {
  StateDir sd;
  { auto s = makeProcessor(true); }
  juce::MemoryBlock previous;
  REQUIRE(sd.file("state.bin").loadFileAsData(previous));

  auto s = makeProcessor(true);
  UiCapture ui(*s);
  TestHost host(*s);
  s->handleUiMessage(R"({"t":"ready"})");
  host.run(2);
  REQUIRE(ui.contains(R"("t":"env")"));

  sd.blockWrites();
  s->handleUiMessage(R"({"t":"field","f":5,"v":99})");
  host.run(2);
  const juce::String err = s->saveStandaloneStateNow();
  CHECK(err.isNotEmpty());
  host.run(1);
  CHECK(ui.contains(R"("t":"status","text":"Could not save state: )"));
  juce::MemoryBlock now;
  REQUIRE(sd.file("state.bin").loadFileAsData(now));
  CHECK(now == previous);

  // VJN6 keeps running; once writable again the next save succeeds and clears the status
  sd.unblockWrites();
  ui.messages.clear();
  CHECK(s->saveStandaloneStateNow().isEmpty());
  host.run(1);
  CHECK(ui.contains(R"("t":"status","text":"")"));
  CHECK(decodeFile(sd.file("state.bin"))->edit.v[5] == 99);
}

TEST_CASE("Standalone state folder that cannot be created reports a reason", "[AC-F13.1]") {
  StateDir sd;
  sd.file("plain").replaceWithText("x");
  vjn6plugin::StateStore store(sd.file("plain").getChildFile("VJN6"));
  vjn6::StateBlob blob;
  blob.bytes = {1, 2, 3};
  CHECK(store.save(blob, "{}\n").isNotEmpty());
}

TEST_CASE("F-13 autosave timing: 1 s after the last change, at least every 5 s", "[F-13]") {
  vjn6plugin::StateStore store(juce::File::getSpecialLocation(juce::File::tempDirectory));
  CHECK(!store.saveDue(0.0));
  store.markDirty(100.0);
  CHECK(!store.saveDue(1099.0));
  CHECK(store.saveDue(1100.0));
  store.markSaved();
  // continuous changes every 400 ms
  double t = 2000.0;
  store.markDirty(t);
  for (; t < 6900.0; t += 400.0) {
    store.markDirty(t);
    CHECK(!store.saveDue(t));
  }
  store.markDirty(7000.0);
  CHECK(store.saveDue(7000.0));
}

TEST_CASE("Standalone autosave writes state.bin 1 s after a change", "[F-13]") {
  StateDir sd;
  auto s = makeProcessor(true);
  TestHost host(*s);
  s->handleUiMessage(R"({"t":"field","f":3,"v":42})");
  host.run(2);
  CHECK(!sd.file("state.bin").existsAsFile());
  juce::Thread::sleep(1100);
  host.run(1);
  REQUIRE(sd.file("state.bin").existsAsFile());
  CHECK(decodeFile(sd.file("state.bin"))->edit.v[3] == 42);
  CHECK(sd.file("bank.vjn6bank").existsAsFile());
}

TEST_CASE("R-S3: a connecting UI first receives the complete state, then the host description", "[R-S3]") {
  auto p = makeProcessor(false);
  UiCapture ui(*p);
  TestHost host(*p);
  host.run(2);
  CHECK(ui.messages.empty());  // nothing before "ready"
  p->handleUiMessage(R"({"t":"ready"})");
  host.run(1);
  REQUIRE(ui.messages.size() >= 2);
  CHECK(ui.messages[0].find(R"("t":"state")") != std::string::npos);
  CHECK(ui.messages[0].find(R"("bank":[)") != std::string::npos);
  CHECK(ui.messages[1].find(R"("t":"env")") != std::string::npos);
  CHECK(ui.messages[1].find(R"("build":"plugin")") != std::string::npos);
  CHECK(ui.messages[1].find(R"("midiOutAvailable":true)") != std::string::npos);
  // a change from any source reaches the UI on the next poll
  ui.messages.clear();
  host.process(midiOf({0xB0, 74, 64}));
  p->pollNow();
  CHECK(ui.contains(R"("t":"state")"));
  // editor close: the UI is disconnected and keyboard notes are released
  p->editorClosed();
  ui.messages.clear();
  host.run(2);
  CHECK(ui.messages.empty());
}
