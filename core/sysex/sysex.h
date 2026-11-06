// Juno-106 SysEx mapping (SPEC §4.5).
#pragma once

#include <cstdint>

#include "vjn6/types.h"

namespace vjn6::sysex {

constexpr int kDumpLen = 24;
constexpr int kParamLen = 7;

inline int eff(const Patch& p, int field) {
  const int v = p.v[field];
  return v < switchCount(field) ? v : 0;
}

// sw1: range one-hot (bits 0-2), pulse (3), saw (4), chorus off (5), chorus I (6). Canonical.
inline uint8_t encodeSw1(const Patch& p) {
  uint8_t b = static_cast<uint8_t>(1u << eff(p, W_RANGE));
  if (eff(p, W_PULSE)) b |= 1u << 3;
  if (eff(p, W_SAW)) b |= 1u << 4;
  const int ch = eff(p, W_CHORUS);
  if (ch == 0) {
    b |= 1u << 5;
  } else if (ch == 1 || ch == 3) {
    b |= 1u << 6;  // I+II (60 only) is exported as Chorus I
  }
  return b;
}

// sw2: PWM MANUAL (0), VCF inverted (1), VCA GATE (2), HPF reversed (3-4).
inline uint8_t encodeSw2(const Patch& p) {
  uint8_t b = 0;
  if (eff(p, W_PWM_SRC) != 0) b |= 1u;  // ENV (60 only) is exported as MANUAL
  if (eff(p, W_VCF_POL)) b |= 1u << 1;
  if (eff(p, W_VCA_MODE)) b |= 1u << 2;
  b |= static_cast<uint8_t>((3 - eff(p, W_HPF)) << 3);
  return b;
}

// A received sw1 sets only W0, W1, W2, W4.
inline void applySw1(Patch& p, uint8_t b) {
  const int r = b & 7;
  p.v[W_RANGE] = r == 1 ? 0 : r == 2 ? 1 : r == 4 ? 2 : 1;  // none or several -> 8'
  p.v[W_PULSE] = (b >> 3) & 1;
  p.v[W_SAW] = (b >> 4) & 1;
  p.v[W_CHORUS] = ((b >> 5) & 1) ? 0 : (((b >> 6) & 1) ? 1 : 2);
}

// A received sw2 sets only W5, W6, W7, W8 (bits 5-6 ignored).
inline void applySw2(Patch& p, uint8_t b) {
  p.v[W_PWM_SRC] = b & 1;
  p.v[W_VCF_POL] = (b >> 1) & 1;
  p.v[W_VCA_MODE] = (b >> 2) & 1;
  p.v[W_HPF] = 3 - ((b >> 3) & 3);
}

// Encodes a canonical 24-byte F0 41 30/31 message.
inline void encodeDump(const Patch& p, uint8_t type, int channel0, int pp, uint8_t out[kDumpLen]) {
  out[0] = 0xF0;
  out[1] = 0x41;
  out[2] = type;
  out[3] = static_cast<uint8_t>(channel0 & 0x0F);
  out[4] = static_cast<uint8_t>(pp & 0x7F);
  for (int i = 0; i < kNumSliders; ++i) out[5 + i] = static_cast<uint8_t>(p.v[i] & 0x7F);
  if (eff(p, W_SUB) == 0) out[5 + B_SUB_LEVEL] = 0;  // W3 = 0 is written as d15 = 0
  out[21] = encodeSw1(p);
  out[22] = encodeSw2(p);
  out[23] = 0xF7;
}

// True if m is a well-formed F0 41 30/31 message of exactly 24 bytes with 7-bit data.
inline bool isDump(const uint8_t* m, int len) {
  if (len != kDumpLen || m[0] != 0xF0 || m[1] != 0x41 || (m[2] != 0x30 && m[2] != 0x31) || m[23] != 0xF7) return false;
  for (int i = 3; i < 23; ++i)
    if (m[i] & 0x80) return false;
  return true;
}

inline bool isParam(const uint8_t* m, int len) {
  if (len != kParamLen || m[0] != 0xF0 || m[1] != 0x41 || m[2] != 0x32 || m[6] != 0xF7) return false;
  for (int i = 3; i < 6; ++i)
    if (m[i] & 0x80) return false;
  return true;
}

// Applies a dump's data to a patch: B0..B15, sw1, sw2, and W3 = 1 (import of 30/31).
inline void applyDump(Patch& p, const uint8_t* m) {
  for (int i = 0; i < kNumSliders; ++i) p.v[i] = m[5 + i];
  applySw1(p, m[21]);
  applySw2(p, m[22]);
  p.v[W_SUB] = 1;
}

// SysEx parameter number for a patch field (106 transmit); -1 for W3 (no SysEx form).
inline int paramFor(int field) {
  if (field < kNumSliders) return field;
  if (field == W_SUB) return -1;
  if (field == W_RANGE || field == W_PULSE || field == W_SAW || field == W_CHORUS) return 0x10;
  return 0x11;
}

}  // namespace vjn6::sysex
