// Shared data types of the VJN6 core (SPEC §4).
#pragma once

#include <cstdint>
#include <cstring>

namespace vjn6 {

enum class Mode { J60, J106 };
enum class Noise { Off, Half, Original };

constexpr int kNumSliders = 16;
constexpr int kNumSwitches = 9;
constexpr int kNumFields = kNumSliders + kNumSwitches;  // 25
constexpr int kNumSlots = 128;
constexpr int kNameMax = 12;

// Patch fields: B0..B15 = 0..15, W0..W8 = 16..24 (§4.1).
enum Field : uint8_t {
  B_LFO_RATE = 0, B_LFO_DELAY, B_DCO_LFO, B_DCO_PWM, B_NOISE, B_VCF_FREQ, B_VCF_RES, B_VCF_ENV,
  B_VCF_LFO, B_VCF_KYBD, B_VCA_LEVEL, B_ENV_A, B_ENV_D, B_ENV_S, B_ENV_R, B_SUB_LEVEL,
  W_RANGE = 16, W_PULSE, W_SAW, W_SUB, W_CHORUS, W_PWM_SRC, W_VCF_POL, W_VCA_MODE, W_HPF
};

struct Patch {
  uint8_t v[kNumFields] = {};
  bool operator==(const Patch& o) const { return std::memcmp(v, o.v, sizeof v) == 0; }
  bool operator!=(const Patch& o) const { return !(*this == o); }
};

// A normalised patch name: 0..12 ASCII 0x20..0x7E, no trailing spaces, NUL terminated.
struct Name {
  char s[kNameMax + 1] = {};
  bool operator==(const Name& o) const { return std::strcmp(s, o.s) == 0; }
  bool operator!=(const Name& o) const { return !(*this == o); }
};

struct Slot {
  Patch patch;
  Name name;
};

// Performance settings (§4.2). The first 14 are host parameters f_* in §4.8 order.
enum Perf : uint8_t {
  P_KEY_MODE = 0,     // 0 POLY 1, 1 POLY 2, 2 UNISON (106)
  P_PORTA_ON,         // 0/1 (106)
  P_PORTA_TIME,       // 0..127 (106)
  P_BEND_DCO,         // 0..127
  P_BEND_VCF,         // 0..127
  P_LFO_TRIG_SENS,    // 0..127 (106)
  P_LFO_TRIG_MODE,    // 0 AUTO, 1 MANUAL (60)
  P_HOLD,             // 0/1 (60)
  P_ARP_ON,           // 0/1 (60)
  P_ARP_MODE,         // 0 UP, 1 U&D, 2 DOWN (60)
  P_ARP_RANGE,        // 1..3 (60)
  P_ARP_RATE,         // 0..127 (60)
  P_OCT_TRANSPOSE,    // 0 L, 1 N, 2 H (60)
  P_KEY_TRANSPOSE,    // -12..+12 (106)
  P_TUNE,             // 0..127, 64 = 0 cents
  P_VOLUME,           // 0..127
  P_MIDI_CHANNEL,     // 1..16
  P_OMNI,             // 0/1
  P_MIDI_FUNCTION,    // 1 = I, 2 = II, 3 = III (106)
  P_PROTECT,          // 0/1
  P_COUNT
};
constexpr int kNumHostPerf = 14;

// §4.4 settings held in state (devices live outside the core).
enum SettingId : uint8_t {
  S_MODE = 0,         // 0 = 60, 1 = 106
  S_CHORUS_NOISE,     // 0 OFF, 1 HALF, 2 ORIGINAL
  S_VARIATION,        // 0..100 in steps of 10
  S_VELOCITY,         // 0/1
  S_MONO,             // 0 stereo, 1 mono
  S_OVERSAMPLE,       // 2 or 4
  S_KBD_PLAY,         // 0/1
  S_ZOOM,             // 0 = Fit, else 50..200 in steps of 10
  S_COUNT
};

struct PerfSettings {
  int16_t v[P_COUNT] = {0, 0, 32, 64, 0, 64, 0, 0, 0, 0, 1, 64, 1, 0, 64, 127, 1, 1, 3, 1};
};

struct SettingsValues {
  int16_t v[S_COUNT] = {0, 2, 50, 0, 0, 2, 1, 0};
};

// Value range of a perf setting (inclusive).
inline void perfRange(int p, int& lo, int& hi) {
  switch (p) {
    case P_KEY_MODE: lo = 0; hi = 2; return;
    case P_PORTA_ON: case P_HOLD: case P_ARP_ON: case P_OMNI: case P_PROTECT: case P_LFO_TRIG_MODE:
      lo = 0; hi = 1; return;
    case P_ARP_MODE: lo = 0; hi = 2; return;
    case P_ARP_RANGE: lo = 1; hi = 3; return;
    case P_OCT_TRANSPOSE: lo = 0; hi = 2; return;
    case P_KEY_TRANSPOSE: lo = -12; hi = 12; return;
    case P_MIDI_CHANNEL: lo = 1; hi = 16; return;
    case P_MIDI_FUNCTION: lo = 1; hi = 3; return;
    default: lo = 0; hi = 127; return;
  }
}

inline void settingRange(int s, int& lo, int& hi) {
  switch (s) {
    case S_MODE: lo = 0; hi = 1; return;
    case S_CHORUS_NOISE: lo = 0; hi = 2; return;
    case S_VARIATION: lo = 0; hi = 100; return;
    case S_VELOCITY: case S_MONO: case S_KBD_PLAY: lo = 0; hi = 1; return;
    case S_OVERSAMPLE: lo = 2; hi = 4; return;
    case S_ZOOM: lo = 0; hi = 200; return;
    default: lo = 0; hi = 0; return;
  }
}

// Number of valid values of a switch field (§4.1); a stored value outside is read as 0.
inline int switchCount(int field) {
  switch (field) {
    case W_RANGE: return 3;
    case W_CHORUS: return 4;
    case W_PWM_SRC: return 3;
    case W_HPF: return 4;
    default: return 2;
  }
}

// The R-P7 INIT patch.
inline Patch initPatch() {
  Patch p;
  p.v[B_VCF_FREQ] = 127;
  p.v[B_VCA_LEVEL] = 127;
  p.v[B_ENV_S] = 127;
  p.v[W_RANGE] = 1;
  p.v[W_SAW] = 1;
  p.v[W_SUB] = 1;
  return p;
}

inline Name makeName(const char* s) {
  Name n;
  std::size_t i = 0;
  for (; s[i] && i < static_cast<std::size_t>(kNameMax); ++i) n.s[i] = s[i];
  n.s[i] = 0;
  return n;
}

}  // namespace vjn6
