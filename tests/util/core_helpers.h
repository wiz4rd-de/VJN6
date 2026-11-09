// Helpers for the core (non-DSP) acceptance tests: commands, SysEx builders, voice probes, LED reads.
// Additive to harness.h; header-only.
#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "sysex/sysex.h"
#include "util/harness.h"
#include "vjn6/bridge.h"

namespace vjn6::test {

// Renders long enough to pass at least one control boundary in either mode (106 tick: 4.2 ms).
inline void settle(Rig& r, long n = 512) { r.skip(n); }

// Posts a command through the queue (as the UI does) and renders past the next control boundary.
inline void send(Rig& r, const Command& c, long n = 512) {
  r.inst->post(c);
  r.skip(n);
}

inline Command cmdOf(CommandType t, Source s = Source::Panel) {
  Command c;
  c.type = t;
  c.src = s;
  return c;
}

// Lever command. push: -1 = no push information, 0 = released, 1 = held.
inline Command leverCmd(double v, bool drag, int push = -1, Source s = Source::Panel) {
  Command c = cmdOf(CommandType::Lever, s);
  c.f = static_cast<float>(v);
  c.flags = static_cast<uint8_t>((drag ? 1 : 0) | (push >= 0 ? 4 : 0) | (push == 1 ? 2 : 0));
  return c;
}
inline Command pushCmd(bool on) {
  Command c = cmdOf(CommandType::Push, Source::Keyboard);
  c.b = on ? 1 : 0;
  return c;
}
inline Command pedalCmd(bool down) {
  Command c = cmdOf(CommandType::Pedal, Source::Keyboard);
  c.b = down ? 1 : 0;
  return c;
}
inline Command messageCmd(LedMessage m) {
  Command c = cmdOf(CommandType::ShowMessage, Source::System);
  c.a = static_cast<uint8_t>(m);
  return c;
}

// ---- SysEx builders ----
inline std::vector<uint8_t> dumpMsg(uint8_t type, int ch0, int pp, const Patch& p) {
  std::vector<uint8_t> m(24);
  sysex::encodeDump(p, type, ch0, pp, m.data());
  return m;
}
// Raw dump with explicit data, sw1, sw2.
inline std::vector<uint8_t> rawDump(uint8_t type, int ch0, int pp, const uint8_t d[16], uint8_t sw1, uint8_t sw2) {
  std::vector<uint8_t> m = {0xF0, 0x41, type, static_cast<uint8_t>(ch0), static_cast<uint8_t>(pp)};
  for (int i = 0; i < 16; ++i) m.push_back(d[i]);
  m.push_back(sw1);
  m.push_back(sw2);
  m.push_back(0xF7);
  return m;
}
inline std::vector<uint8_t> paramMsg(int ch0, int pp, int vv) {
  return {0xF0, 0x41, 0x32, static_cast<uint8_t>(ch0), static_cast<uint8_t>(pp), static_cast<uint8_t>(vv), 0xF7};
}
inline bool isParamMsg(const std::vector<uint8_t>& m) { return m.size() == 7 && m[0] == 0xF0 && m[2] == 0x32; }
inline bool isDumpMsg(const std::vector<uint8_t>& m, uint8_t type) {
  return m.size() == 24 && m[0] == 0xF0 && m[1] == 0x41 && m[2] == type;
}

// ---- voice probes ----
inline int voiceOf(Rig& r, int note) {
  for (int i = 0; i < k::kVoices; ++i) {
    const Voice& v = r.eng().voice(i);
    if (v.held && v.note == note) return i;
  }
  return -1;
}
inline int countHeld(Rig& r) {
  int n = 0;
  for (int i = 0; i < k::kVoices; ++i) n += r.eng().voice(i).held ? 1 : 0;
  return n;
}
inline int countActive(Rig& r) {
  int n = 0;
  for (int i = 0; i < k::kVoices; ++i) n += r.eng().voice(i).active ? 1 : 0;
  return n;
}
inline std::vector<int> heldNotes(Rig& r) {
  std::vector<int> v;
  for (int i = 0; i < k::kVoices; ++i)
    if (r.eng().voice(i).held) v.push_back(r.eng().voice(i).note);
  std::sort(v.begin(), v.end());
  return v;
}

// ---- LED ----
inline std::string ledText(Rig& r) {
  const LedState L = r.core().led();
  return std::string(L.text, 2);
}
inline bool lit(Rig& r, int bit) { return (r.core().buttonLeds() >> bit) & 1u; }
inline bool blinking(Rig& r, int bit) {
  uint64_t b = 0;
  r.core().buttonLeds(&b);
  return (b >> bit) & 1u;
}

// ---- events ----
inline int countEvents(const std::vector<Event>& ev, EventType t) {
  int n = 0;
  for (const Event& e : ev) n += e.type == t ? 1 : 0;
  return n;
}
inline std::vector<std::vector<uint8_t>> midiOutOf(const std::vector<Event>& ev) {
  std::vector<std::vector<uint8_t>> v;
  for (const Event& e : ev)
    if (e.type == EventType::MidiOut) v.emplace_back(e.data, e.data + e.len);
  return v;
}

// A memory-protect-off rig in the given mode (file and import criteria run with protect off, §13.3).
inline void protectOff(Rig& r) { r.apply(Command::setPerf(P_PROTECT, 0, Source::Overlay)); }

// ---- v3.1 default-bank layout ----
// v3.2 (C-7, §15.3) put the 56 Juno-60 factory patches in slots 1-56 and moved the eight original patches
// from slots 2-9 to 58-65, so first run now starts on slot 1 `Strings 1`. The tests that call this were
// written against the v3.1 first-run state and keep their assertions: slot 1 INIT, slots 2-9 BRASS,
// STRINGS, PAD, BASS, PLUCK, ORGAN, SWEEP, SFX (taken by name from defaultBank(), so the shipped bytes),
// 10-128 INIT; edit buffer, stored patch and name of slot 1, current slot 1, CLEAN, not Manual, pending
// bank 1 / group A. It goes in as a state load applied at once, so the snapshot, the bridge and the UI see
// it (with a new bank version); performance settings and settings stay as they are, and the load's events
// are drained (first run has none). Call it right after constructing the rig. Returns that bank.
inline const Slot* useV31BankLayout(Rig& r) {
  struct V31 {
    Slot bank[kNumSlots];
    V31() {
      const char* const originals[8] = {"BRASS", "STRINGS", "PAD", "BASS", "PLUCK", "ORGAN", "SWEEP", "SFX"};
      for (Slot& s : bank) {
        s.patch = initPatch();
        s.name = makeName("INIT");
      }
      const Slot* def = defaultBank();
      for (int k = 0; k < 8; ++k) {
        int i = 0;
        while (i < kNumSlots && std::string(def[i].name.s) != originals[k]) ++i;
        if (i == kNumSlots) throw std::logic_error(std::string("the default bank has no ") + originals[k]);
        bank[k + 1] = def[i];
      }
    }
  };
  static const V31 v31;
  const int idx = r.core().acquireBulk();
  Bulk* b = r.core().bulk(idx);
  if (!b) throw std::logic_error("no free bulk slot for the v3.1 bank layout");
  b->kind = BulkKind::State;
  b->stateResult = static_cast<int>(BlobResult::Ok);
  CoreState& s = b->state;
  s = r.st();
  for (int i = 0; i < kNumSlots; ++i) s.bank[i] = v31.bank[i];
  s.edit = s.stored = v31.bank[0].patch;
  s.editName = v31.bank[0].name;
  s.edited = s.manual = false;
  s.currentSlot = s.pendingBank = 1;
  s.pendingGroup = 0;
  Command c = cmdOf(CommandType::LoadState, Source::System);
  c.i = idx;
  r.apply(c);
  r.events();
  return v31.bank;
}

}  // namespace vjn6::test
