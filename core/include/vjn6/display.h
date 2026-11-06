// Display values (R-P5, R-P6) and host parameter metadata (§4.8). Not for the audio thread.
//
// Number formatting uses one rule that the UI (ui/src/panel/format.ts) reproduces exactly:
// round half away from zero on the double value, digits assembled from the integer.
#pragma once

#include <string>
#include <vector>

#include "vjn6/types.h"

namespace vjn6::display {

// ---- number formats (R-P5) ----
std::string fixed(double x, int decimals);            // "12.3", "-0.5"
std::string signedFixed(double x, int decimals);      // "+12.3", "-0.5", "+0.0"
std::string sig3(double x);                           // three significant digits: "248", "5.53", "40700"
std::string hz(double x);                             // "248 Hz"
std::string seconds(double s);                        // "760 ms" (< 1 s), "3.05 s"
std::string db(double gain);                          // "+0.0 dB", "-6.0 dB", "-inf dB"
std::string percent(double x);                        // "73 %"

// ---- control values ----
std::string sliderV(int byte);                        // byte / 12.7, one decimal: "5.0"
std::string vcaLevel60V(int byte);                    // 60 VCA LEVEL: byte / 12.7 - 5, signed: "-5.0", "0.0", "+2.0"
// The v a slider shows and accepts for the active mode (R-P5): sliderV, or vcaLevel60V for B10 in 60 mode.
std::string fieldV(int field, int byte, Mode m);
// Typed entry (R-U1): the byte committed for a typed v; out-of-range values are clamped.
int fieldByteFromV(int field, double v, Mode m);
std::string tuneV(int byte);                          // cents, one decimal with sign: "+10.2"
// Engine value of a patch field at a byte for the active mode (R-P5). ctx supplies the PWM source
// (for PWM) and the polarity (for VCF ENV).
std::string fieldEngineValue(int field, int byte, Mode m, const Patch& ctx);
// Engine value of a knob: P_VOLUME, P_TUNE, P_BEND_DCO, P_BEND_VCF, P_LFO_TRIG_SENS,
// P_PORTA_TIME, P_ARP_RATE.
std::string perfEngineValue(int perf, int value, Mode m);

// ---- host parameters (§4.8) ----
constexpr int kNumHostParams = 40;
struct HostParamInfo {
  const char* id;
  const char* name;
  bool isChoice;
  int minValue, maxValue;   // native units (key transpose -12..12, tune 0..127)
  std::vector<std::string> choices;
};
const HostParamInfo& hostParam(int index);
// Host value string for a native value in the active mode (R-P6: identical to the panel's v; only
// p_vca_level differs between the modes).
std::string hostValueString(int index, int value, Mode m);
// Parses a host value string back to a native value; returns false if not understood.
bool hostValueFromString(int index, const std::string& text, int& value, Mode m);
// Host parameter index -> core field / perf.
inline bool hostIsField(int index) { return index < kNumFields; }

}  // namespace vjn6::display
