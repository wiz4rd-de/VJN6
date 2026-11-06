#include "vjn6/display.h"

#include <cmath>
#include <cstdlib>

#include "juno/constants.h"
#include "juno/dmath.h"

namespace vjn6::display {

namespace {
double roundAway(double x) { return x < 0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5); }

std::string digits(long long n, int decimals) {
  // n >= 0 holds the value * 10^decimals
  std::string s = std::to_string(n);
  if (decimals == 0) return s;
  while (static_cast<int>(s.size()) <= decimals) s.insert(s.begin(), '0');
  s.insert(s.end() - decimals, '.');
  return s;
}

double scale10(int d) {
  double s = 1.0;
  for (int i = 0; i < d; ++i) s *= 10.0;
  return s;
}

double lfo106Hz(int b) {
  const double x = b / 127.0 * 10.0;
  int i = static_cast<int>(std::floor(x));
  if (i > 9) i = 9;
  return k::kLfoTable106[i] + (k::kLfoTable106[i + 1] - k::kLfoTable106[i]) * (x - i);
}

double sliderTime(int b, double tmin, double tmax) {
  const double x = b / 127.0;
  return tmin + (tmax - tmin) * x * x;
}

std::string octaves(double v, bool plusMinus) {
  if (plusMinus) return "\xC2\xB1" + fixed(v, 2) + " oct";
  return signedFixed(v, 2) + " oct";
}
std::string cents(double v, bool plusMinus) {
  if (plusMinus) return "\xC2\xB1" + fixed(v, 1) + " cents";
  return signedFixed(v, 1) + " cents";
}
}  // namespace

std::string fixed(double x, int decimals) {
  const double r = roundAway(x * scale10(decimals));
  const bool neg = r < 0;
  const std::string s = digits(static_cast<long long>(neg ? -r : r), decimals);
  return neg ? "-" + s : s;
}

std::string signedFixed(double x, int decimals) {
  const double r = roundAway(x * scale10(decimals));
  if (r < 0) return "-" + digits(static_cast<long long>(-r), decimals);
  return "+" + digits(static_cast<long long>(r), decimals);
}

std::string sig3(double x) {
  if (x == 0.0) return "0";
  const bool neg = x < 0;
  double a = neg ? -x : x;
  int k = 0;  // a * 10^k in [100, 1000)
  double v = a;
  while (v < 100.0) {
    v *= 10.0;
    ++k;
  }
  while (v >= 1000.0) {
    v /= 10.0;
    --k;
  }
  long long n = static_cast<long long>(std::floor(v + 0.5));
  if (n >= 1000) {
    n /= 10;
    --k;
  }
  std::string s;
  if (k > 0) {
    s = digits(n, k);
  } else {
    s = std::to_string(n);
    for (int i = 0; i < -k; ++i) s += "0";
  }
  return neg ? "-" + s : s;
}

std::string hz(double x) { return sig3(x) + " Hz"; }

std::string seconds(double s) {
  if (s < 1.0) {
    const double ms = roundAway(s * 1000.0);
    if (ms < 1000.0) return std::to_string(static_cast<long long>(ms)) + " ms";
  }
  return fixed(s, 2) + " s";
}

std::string db(double gain) {
  if (gain <= 0.0) return "-inf dB";
  return signedFixed(20.0 * dm::log10(gain), 1) + " dB";
}

std::string percent(double x) { return std::to_string(static_cast<long long>(roundAway(x))) + " %"; }

std::string sliderV(int byte) { return fixed(byte / 12.7, 1); }

std::string tuneV(int byte) { return signedFixed((byte - 64) * k::kTuneCentsPerStep, 1); }

std::string fieldEngineValue(int field, int b, Mode m, const Patch& ctx) {
  const bool is106 = m == Mode::J106;
  const double x = b / 127.0;
  auto effSw = [&](int f) {
    int v = ctx.v[f];
    if (v >= switchCount(f)) v = 0;
    if (is106 && f == W_PWM_SRC && v == 2) v = 1;
    return v;
  };
  switch (field) {
    case B_LFO_RATE: return hz(is106 ? lfo106Hz(b) : k::kLfoMin60Hz * dm::pow(k::kLfoRatio60, x));
    case B_LFO_DELAY: return seconds((is106 ? k::kLfoDelayMax106S : k::kLfoDelayMax60S) * x);
    case B_DCO_LFO: return cents((is106 ? k::kLfoDcoCents106 : k::kLfoDcoCents60) * x, true);
    case B_DCO_PWM:
      if (effSw(W_PWM_SRC) == 1) return percent(100.0 * (k::kDutyCentre + k::kDutySpan * x));
      return "\xC2\xB1" + percent(100.0 * k::kDutySpan * x);
    case B_NOISE:
    case B_SUB_LEVEL:
    case B_VCF_KYBD:
    case B_ENV_S: return percent(100.0 * x);
    case B_VCF_FREQ:
      return hz(is106 ? k::kCutoffBase106Hz * dm::exp2(k::kW106PerByte * b / k::kW106PerOct)
                      : k::kCutoffBase60Hz * dm::exp2(k::kCutoffSpan60Oct * x));
    case B_VCF_RES: return "k " + fixed((is106 ? k::kKmax106 : k::kKmax60) * x * std::sqrt(x), 2);
    case B_VCF_ENV: {
      const double oct = (is106 ? k::kEnvDepth106Oct : k::kEnvDepth60Oct) * x;
      return octaves(effSw(W_VCF_POL) ? -oct : oct, false);
    }
    case B_VCF_LFO: return octaves((is106 ? k::kLfoDepth106Oct : k::kLfoDepth60Oct) * x, true);
    case B_VCA_LEVEL: return db(x);
    case B_ENV_A: return seconds(is106 ? sliderTime(b, k::kAttackMin106, k::kAttackMax106)
                                       : sliderTime(b, k::kAttackMin60, k::kAttackMax60));
    case B_ENV_D:
    case B_ENV_R: return seconds(is106 ? sliderTime(b, k::kDecRelMin106, k::kDecRelMax106)
                                       : sliderTime(b, k::kDecRelMin60, k::kDecRelMax60));
    case W_HPF: {
      static const char* c60[] = {"flat", "122 Hz", "269 Hz", "571 Hz"};
      static const char* c106[] = {"bass boost", "flat", "236 Hz", "754 Hz"};
      const int p = b < 4 ? b : 0;
      return is106 ? c106[p] : c60[p];
    }
    default: return "";
  }
}

std::string perfEngineValue(int perf, int v, Mode m) {
  const bool is106 = m == Mode::J106;
  const double x = v / 127.0;
  switch (perf) {
    case P_VOLUME: return db(x * x);
    case P_TUNE: return signedFixed((v - 64) * k::kTuneCentsPerStep, 1) + " cents";
    case P_BEND_DCO: return cents((is106 ? k::kBendDco106Cents : k::kBendDco60Cents) * x, true);
    case P_BEND_VCF: return octaves((is106 ? k::kBendVcf106Oct : k::kBendVcf60Oct) * x, true);
    case P_LFO_TRIG_SENS: return cents(k::kLfoDcoCents106 * x, true);
    case P_PORTA_TIME: return seconds(k::kPortaMaxS * x * x);
    case P_ARP_RATE: return sig3(k::kArpMinHz * dm::pow(k::kArpRatio, x)) + " steps/s";
    default: return "";
  }
}

namespace {
std::vector<HostParamInfo> buildParams() {
  std::vector<HostParamInfo> p;
  const char* sliderIds[16] = {"p_lfo_rate", "p_lfo_delay", "p_dco_lfo", "p_dco_pwm", "p_noise", "p_vcf_freq",
                               "p_vcf_res", "p_vcf_env", "p_vcf_lfo", "p_vcf_kybd", "p_vca_level", "p_env_a",
                               "p_env_d", "p_env_s", "p_env_r", "p_sub_level"};
  const char* sliderNames[16] = {"LFO rate", "LFO delay time", "DCO LFO depth", "DCO PWM", "Noise level",
                                 "VCF frequency", "VCF resonance", "VCF envelope depth", "VCF LFO depth",
                                 "VCF keyboard follow", "VCA level", "ENV attack", "ENV decay", "ENV sustain",
                                 "ENV release", "Sub-oscillator level"};
  for (int i = 0; i < 16; ++i) p.push_back({sliderIds[i], sliderNames[i], false, 0, 127, {}});
  const std::vector<std::string> onoff = {"OFF", "ON"};
  p.push_back({"s_range", "Range", true, 0, 2, {"16'", "8'", "4'"}});
  p.push_back({"s_pulse", "Pulse on", true, 0, 1, onoff});
  p.push_back({"s_saw", "Saw on", true, 0, 1, onoff});
  p.push_back({"s_sub", "Sub on", true, 0, 1, onoff});
  p.push_back({"s_chorus", "Chorus", true, 0, 3, {"OFF", "I", "II", "I+II (60)"}});
  p.push_back({"s_pwm_src", "PWM source", true, 0, 2, {"LFO", "MANUAL", "ENV (60)"}});
  p.push_back({"s_vcf_pol", "VCF envelope polarity", true, 0, 1, {"NORMAL", "INVERTED"}});
  p.push_back({"s_vca_mode", "VCA mode", true, 0, 1, {"ENV", "GATE"}});
  p.push_back({"s_hpf", "HPF position", true, 0, 3, {"0", "1", "2", "3"}});
  p.push_back({"f_key_mode", "Key mode", true, 0, 2, {"POLY 1", "POLY 2", "UNISON"}});
  p.push_back({"f_porta_on", "Portamento on", true, 0, 1, onoff});
  p.push_back({"f_porta_time", "Portamento time", false, 0, 127, {}});
  p.push_back({"f_bend_dco", "Bender DCO sensitivity", false, 0, 127, {}});
  p.push_back({"f_bend_vcf", "Bender VCF sensitivity", false, 0, 127, {}});
  p.push_back({"f_lfo_trig_sens", "LFO trigger sensitivity", false, 0, 127, {}});
  p.push_back({"f_lfo_trig_mode", "LFO trigger mode", true, 0, 1, {"AUTO", "MANUAL"}});
  p.push_back({"f_hold", "Hold", true, 0, 1, onoff});
  p.push_back({"f_arp_on", "Arpeggio on", true, 0, 1, onoff});
  p.push_back({"f_arp_mode", "Arpeggio mode", true, 0, 2, {"UP", "U&D", "DOWN"}});
  p.push_back({"f_arp_range", "Arpeggio range", true, 1, 3, {"1", "2", "3"}});
  p.push_back({"f_arp_rate", "Arpeggio rate", false, 0, 127, {}});
  p.push_back({"f_oct_transpose", "Octave transpose", true, 0, 2, {"D", "N", "U"}});
  p.push_back({"f_key_transpose", "Key transpose", false, -12, 12, {}});
  p.push_back({"g_tune", "Master tune", false, 0, 127, {}});
  return p;
}
const std::vector<HostParamInfo>& params() {
  static const std::vector<HostParamInfo> p = buildParams();
  return p;
}
}  // namespace

const HostParamInfo& hostParam(int index) { return params()[static_cast<std::size_t>(index)]; }

std::string hostValueString(int index, int value) {
  const HostParamInfo& p = hostParam(index);
  if (p.isChoice) {
    const int i = value - p.minValue;
    if (i >= 0 && i < static_cast<int>(p.choices.size())) return p.choices[static_cast<std::size_t>(i)];
    return p.choices.empty() ? std::string() : p.choices[0];
  }
  if (index == kNumHostParams - 1) return tuneV(value);
  if (index == kNumHostParams - 2) return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
  return sliderV(value);
}

bool hostValueFromString(int index, const std::string& text, int& value) {
  const HostParamInfo& p = hostParam(index);
  if (p.isChoice) {
    for (std::size_t i = 0; i < p.choices.size(); ++i)
      if (p.choices[i] == text) {
        value = p.minValue + static_cast<int>(i);
        return true;
      }
    return false;
  }
  char* end = nullptr;
  const double d = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) return false;
  int v;
  if (index == kNumHostParams - 1) v = static_cast<int>(roundAway(d / k::kTuneCentsPerStep)) + 64;
  else if (index == kNumHostParams - 2) v = static_cast<int>(roundAway(d));
  else v = static_cast<int>(roundAway(d * 12.7));
  if (v < p.minValue) v = p.minValue;
  if (v > p.maxValue) v = p.maxValue;
  value = v;
  return true;
}

}  // namespace vjn6::display
