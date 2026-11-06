// AC-PL2 (host parameters and programs), AC-K2.2 (plugin first-run keyboard play), gestures.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>

#include "helpers.h"
#include "vjn6/files.h"

using namespace vjn6test;

namespace {
// SPEC §4.8, in table order.
const char* const kIds[40] = {
    "p_lfo_rate", "p_lfo_delay", "p_dco_lfo", "p_dco_pwm", "p_noise", "p_vcf_freq", "p_vcf_res", "p_vcf_env",
    "p_vcf_lfo", "p_vcf_kybd", "p_vca_level", "p_env_a", "p_env_d", "p_env_s", "p_env_r", "p_sub_level",
    "s_range", "s_pulse", "s_saw", "s_sub", "s_chorus", "s_pwm_src", "s_vcf_pol", "s_vca_mode", "s_hpf",
    "f_key_mode", "f_porta_on", "f_porta_time", "f_bend_dco", "f_bend_vcf", "f_lfo_trig_sens", "f_lfo_trig_mode",
    "f_hold", "f_arp_on", "f_arp_mode", "f_arp_range", "f_arp_rate", "f_oct_transpose", "f_key_transpose", "g_tune"};

// §4.1 "Parameter" names with an initial capital, then the §4.8 performance names.
const char* const kNames[40] = {
    "LFO rate", "LFO delay time", "DCO LFO depth", "DCO PWM", "Noise level", "VCF frequency", "VCF resonance",
    "VCF envelope depth", "VCF LFO depth", "VCF keyboard follow", "VCA level", "ENV attack", "ENV decay",
    "ENV sustain", "ENV release", "Sub-oscillator level", "Range", "Pulse on", "Saw on", "Sub on", "Chorus",
    "PWM source", "VCF envelope polarity", "VCA mode", "HPF position", "Key mode", "Portamento on",
    "Portamento time", "Bender DCO sensitivity", "Bender VCF sensitivity", "LFO trigger sensitivity",
    "LFO trigger mode", "Hold", "Arpeggio on", "Arpeggio mode", "Arpeggio range", "Arpeggio rate",
    "Octave transpose", "Key transpose", "Master tune"};

enum Kind { Int, Choice, Bool };
Kind kindOf(int i) {
  if (i < 16) return Int;
  if (i < 25) return Choice;
  switch (i) {
    case 25: return Choice;  // key mode
    case 26: return Bool;    // portamento on
    case 31: return Choice;  // LFO trigger mode
    case 32: return Bool;    // hold
    case 33: return Bool;    // arpeggio on
    case 34: return Choice;  // arpeggio mode
    case 35: return Choice;  // arpeggio range
    case 37: return Choice;  // octave transpose
    default: return Int;
  }
}

std::vector<std::string> choicesOf(int i) {
  switch (i) {
    case 16: return {"16'", "8'", "4'"};
    case 17: case 18: case 19: return {"OFF", "ON"};
    case 20: return {"OFF", "I", "II", "I+II (60)"};
    case 21: return {"LFO", "MANUAL", "ENV (60)"};
    case 22: return {"NORMAL", "INVERTED"};
    case 23: return {"ENV", "GATE"};
    case 24: return {"0", "1", "2", "3"};
    case 25: return {"POLY 1", "POLY 2", "UNISON"};
    case 26: case 32: case 33: return {"OFF", "ON"};
    case 31: return {"AUTO", "MANUAL"};
    case 34: return {"UP", "U&D", "DOWN"};
    case 35: return {"1", "2", "3"};
    case 37: return {"D", "N", "U"};  // DOWN / NORMAL / UP (v3.2 C-3)
    default: return {};
  }
}

int minOf(int i) { return i == 35 ? 1 : (i == 38 ? -12 : 0); }
int maxOf(int i) {
  if (i == 38) return 12;
  const auto c = choicesOf(i);
  return c.empty() ? 127 : minOf(i) + static_cast<int>(c.size()) - 1;
}

std::string oneDecimal(double tenths, bool sign) {
  const double r = std::round(tenths);  // half away from zero
  char buf[32];
  std::snprintf(buf, sizeof buf, sign ? "%+.1f" : "%.1f", r / 10.0);
  std::string s = buf;
  if (s == "-0.0") s = "+0.0";
  return s;
}

// R-P5 / R-P6 value strings computed from the spec rules, in the active mode.
std::string expected(int i, int v, bool is106) {
  const auto c = choicesOf(i);
  if (!c.empty()) return c[static_cast<size_t>(v - minOf(i))];
  if (i == 10 && !is106) {  // 60 VCA LEVEL: v / 12.7 - 5, signed, zero shown as 0.0 (v3.2 C-4)
    const std::string s = oneDecimal(v * 100.0 / 127.0 - 50.0, true);
    return s == "+0.0" ? "0.0" : s;
  }
  if (i == 39) return oneDecimal((v - 64) * 500.0 / 64.0, true);  // cents = (v - 64) * 50/64
  if (i == 38) return v > 0 ? "+" + std::to_string(v) : std::to_string(v);
  return oneDecimal(v * 100.0 / 127.0, false);  // v / 12.7
}
}  // namespace

TEST_CASE("AC-PL2 40 host parameters with the §4.8 IDs, order, names, types and value strings", "[AC-PL2]") {
  auto p = makeProcessor();
  TestHost host(*p);
  HostListener listener(*p);
  const auto& params = p->getParameters();
  REQUIRE(params.size() == 40);
  // the value strings follow the active mode (§4.8): first run is 60 mode, then 106 mode
  for (const bool is106 : {false, true}) {
    if (is106) {
      listener.reset();
      p->handleUiMessage(R"({"t":"setting","s":0,"v":1})");
      host.run(2);
      REQUIRE(snapshotOf(*p)->settings.v[vjn6::S_MODE] == 1);
      CHECK(listener.parameterInfoChanges >= 1);  // the host is told that the strings changed
    } else {
      REQUIRE(snapshotOf(*p)->settings.v[vjn6::S_MODE] == 0);
    }
    for (int i = 0; i < 40; ++i) {
      INFO((is106 ? "106" : "60") << " mode, parameter " << i);
      auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[i]);
      REQUIRE(withId != nullptr);
      CHECK(withId->paramID == kIds[i]);
      CHECK(params[i]->getName(100) == kNames[i]);
      CHECK(params[i]->isAutomatable());
      switch (kindOf(i)) {
        case Int: CHECK(dynamic_cast<juce::AudioParameterInt*>(params[i]) != nullptr); break;
        case Choice: CHECK(dynamic_cast<juce::AudioParameterChoice*>(params[i]) != nullptr); break;
        case Bool: CHECK(dynamic_cast<juce::AudioParameterBool*>(params[i]) != nullptr); break;
      }
      auto& np = p->hostParam(i);
      for (int v = minOf(i); v <= maxOf(i); ++v) {
        const float norm = np.normalisedFor(v);
        const juce::String text = params[i]->getText(norm, 100);
        CHECK(text.toStdString() == expected(i, v, is106));
        // typed entry in the host: the text maps back to a value showing the same text (R-U1: sliders
        // commit round(v x 12.7), so several bytes share one text); exact where the text is unique
        const float back = params[i]->getValueForText(text);
        CHECK(params[i]->getText(back, 100) == text);
        if (kindOf(i) != Int || i == 38 || i == 39) CHECK(std::abs(back - norm) < 1e-6f);
      }
    }
    // AC-P5.1 / AC-U1.1 host side: p_vca_level at bytes 0, 64, 127 and typed values
    auto vca = [&](int b) { return params[10]->getText(p->hostParam(10).normalisedFor(b), 100).toStdString(); };
    auto typed = [&](const char* t) { return params[10]->getValueForText(t); };
    if (is106) {
      CHECK(vca(0) == "0.0");
      CHECK(vca(64) == "5.0");
      CHECK(vca(127) == "10.0");
      CHECK(typed("5.0") == p->hostParam(10).normalisedFor(64));
    } else {
      CHECK(vca(0) == "-5.0");
      CHECK(vca(64) == "0.0");
      CHECK(vca(127) == "+5.0");
      CHECK(typed("-2") == p->hostParam(10).normalisedFor(38));
      CHECK(typed("+5") == p->hostParam(10).normalisedFor(127));
      CHECK(typed("9") == p->hostParam(10).normalisedFor(127));
    }
  }
  // spot checks from the spec text
  CHECK(params[0]->getText(p->hostParam(0).normalisedFor(127), 100) == "10.0");
  CHECK(params[39]->getText(p->hostParam(39).normalisedFor(77), 100) == "+10.2");
  CHECK(params[38]->getText(p->hostParam(38).normalisedFor(-12), 100) == "-12");
  CHECK(params[38]->getText(p->hostParam(38).normalisedFor(12), 100) == "+12");
}

TEST_CASE("AC-PL2 128 programs named after the slots; a rename updates the program name", "[AC-PL2]") {
  auto p = makeProcessor();
  TestHost host(*p);
  HostListener listener(*p);
  REQUIRE(p->getNumPrograms() == 128);
  auto snap = snapshotOf(*p);
  for (int i = 0; i < 128; ++i) CHECK(p->getProgramName(i) == juce::String(snap->bank[i].name.s));
  CHECK(p->getProgramName(0) == "INIT");
  CHECK(p->getProgramName(1) == "BRASS");
  CHECK(p->getCurrentProgram() == 0);

  p->handleUiMessage(R"({"t":"perf","p":19,"v":0,"src":"overlay"})");  // memory protect off
  p->handleUiMessage(R"({"t":"rename","slot":3,"name":"My Strings"})");
  host.run(2);
  CHECK(p->getProgramName(2) == "My Strings");
  CHECK(listener.programChanges >= 1);

  // a bank load updates the program names too
  auto bank = snapshotOf(*p);
  bank->bank[127].name = vjn6::makeName("Last");
  bank->bank[63].name = vjn6::makeName("Middle");
  const std::string file = vjn6::files::bankJson(bank->bank);
  const std::string b64 = vjn6::Bridge::base64(reinterpret_cast<const uint8_t*>(file.data()), file.size());
  p->handleUiMessage(juce::String(R"({"t":"loadFile","name":"test.vjn6bank","b64":")") + b64 + "\"}");
  host.run(2);
  CHECK(p->getProgramName(127) == "Last");
  CHECK(p->getProgramName(63) == "Middle");
}

TEST_CASE("AC-PL2 host program selection selects slot i+1 without transmitting", "[AC-PL2]") {
  auto p = makeProcessor();
  TestHost host(*p);
  p->handleUiMessage(R"({"t":"setting","s":0,"v":1})");  // 106 mode, MIDI function III (default)
  host.run(2);
  p->setCurrentProgram(4);
  host.run(2);
  CHECK(snapshotOf(*p)->currentSlot == 5);
  CHECK(p->getCurrentProgram() == 4);
  host.process();
  CHECK(host.outMidi.isEmpty());  // §4.8: never transmits
}

TEST_CASE("AC-K2.2 desktop: the plugin's first-run state has computer keyboard play off", "[AC-K2.2]") {
  auto p = makeProcessor(false);
  CHECK(snapshotOf(*p)->settings.v[vjn6::S_KBD_PLAY] == 0);
  // a zero-length blob (new instance) is first-run state too
  TestHost host(*p);
  p->handleUiMessage(R"({"t":"setting","s":6,"v":1})");
  host.run(2);
  CHECK(snapshotOf(*p)->settings.v[vjn6::S_KBD_PLAY] == 1);  // M turns it on (Settings / shortcut)
  p->setStateInformation(nullptr, 0);
  host.run(2);
  CHECK(snapshotOf(*p)->settings.v[vjn6::S_KBD_PLAY] == 0);
}

TEST_CASE("Panel drags report begin/end gestures to the host (R-S3)", "[R-S3]") {
  auto p = makeProcessor();
  TestHost host(*p);
  HostListener listener(*p);
  p->handleUiMessage(R"({"t":"gesture","f":5,"begin":true})");
  CHECK(listener.gestureBegins[5] == 1);
  p->handleUiMessage(R"({"t":"field","f":5,"v":40})");
  p->handleUiMessage(R"({"t":"field","f":5,"v":41})");
  p->handleUiMessage(R"({"t":"gesture","f":5,"begin":false})");
  host.run(2);
  juce::Thread::sleep(80);
  host.run(1);
  CHECK(listener.gestureEnds[5] == 1);
  CHECK(listener.changes[5] >= 1);
  CHECK(p->hostParam(5).nativeValue() == 41);
  // performance knob
  p->handleUiMessage(R"({"t":"gesture","p":14,"begin":true})");
  CHECK(listener.gestureBegins[39] == 1);
  p->handleUiMessage(R"({"t":"gesture","p":14,"begin":false})");
  juce::Thread::sleep(80);
  host.run(1);
  CHECK(listener.gestureEnds[39] == 1);
}
