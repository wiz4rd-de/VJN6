// Helpers for the core (non-DSP) acceptance tests: commands, SysEx builders, voice probes, LED reads.
// Additive to harness.h; header-only.
#pragma once

#include <algorithm>
#include <cstdint>
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

}  // namespace vjn6::test
