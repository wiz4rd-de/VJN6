#include "HostParams.h"

#include "vjn6/display.h"
#include "vjn6/types.h"

namespace vjn6plugin {

namespace {
thread_local bool tlsOwnWrite = false;

vjn6::Mode modeOf(const std::atomic<int>& mode) {
  return mode.load(std::memory_order_relaxed) == 1 ? vjn6::Mode::J106 : vjn6::Mode::J60;
}

juce::String valueText(int index, int native, const std::atomic<int>& mode) {
  return juce::String(vjn6::display::hostValueString(index, native, modeOf(mode)));
}

int valueFromText(int index, const juce::String& text, int fallback, const std::atomic<int>& mode) {
  int v = fallback;
  if (vjn6::display::hostValueFromString(index, text.trim().toStdString(), v, modeOf(mode))) return v;
  return fallback;
}

class IntParam final : public juce::AudioParameterInt, public NativeParam {
 public:
  IntParam(int index, const vjn6::display::HostParamInfo& info, int def, HostWriteListener& l,
           const std::atomic<int>& mode)
      : juce::AudioParameterInt(juce::ParameterID{info.id, 1}, info.name, info.minValue, info.maxValue, def,
                                juce::AudioParameterIntAttributes()
                                    .withStringFromValueFunction([index, &mode](int v, int) { return valueText(index, v, mode); })
                                    .withValueFromStringFunction([index, def, &mode](const juce::String& t) {
                                      return valueFromText(index, t, def, mode);
                                    })),
        index_(index),
        listener_(l) {}
  int nativeValue() const override { return get(); }
  float normalisedFor(int v) const override { return convertTo0to1(static_cast<float>(v)); }
  juce::RangedAudioParameter& parameter() override { return *this; }

 protected:
  void valueChanged(int v) override {
    if (!tlsOwnWrite) listener_.hostParameterWritten(index_, v);
  }

 private:
  const int index_;
  HostWriteListener& listener_;
};

class ChoiceParam final : public juce::AudioParameterChoice, public NativeParam {
 public:
  ChoiceParam(int index, const vjn6::display::HostParamInfo& info, int def, HostWriteListener& l,
              const std::atomic<int>& mode)
      : juce::AudioParameterChoice(
            juce::ParameterID{info.id, 1}, info.name, choicesOf(info), def - info.minValue,
            juce::AudioParameterChoiceAttributes()
                .withStringFromValueFunction([index, minV = info.minValue, &mode](int i, int) { return valueText(index, i + minV, mode); })
                .withValueFromStringFunction([index, def, minV = info.minValue, &mode](const juce::String& t) {
                  return valueFromText(index, t, def, mode) - minV;
                })),
        index_(index),
        min_(info.minValue),
        listener_(l) {}
  int nativeValue() const override { return getIndex() + min_; }
  float normalisedFor(int v) const override { return convertTo0to1(static_cast<float>(v - min_)); }
  juce::RangedAudioParameter& parameter() override { return *this; }

 protected:
  void valueChanged(int i) override {
    if (!tlsOwnWrite) listener_.hostParameterWritten(index_, i + min_);
  }

 private:
  static juce::StringArray choicesOf(const vjn6::display::HostParamInfo& info) {
    juce::StringArray a;
    for (const auto& c : info.choices) a.add(juce::String(c));
    return a;
  }
  const int index_, min_;
  HostWriteListener& listener_;
};

class BoolParam final : public juce::AudioParameterBool, public NativeParam {
 public:
  BoolParam(int index, const vjn6::display::HostParamInfo& info, int def, HostWriteListener& l,
            const std::atomic<int>& mode)
      : juce::AudioParameterBool(juce::ParameterID{info.id, 1}, info.name, def != 0,
                                 juce::AudioParameterBoolAttributes()
                                     .withStringFromValueFunction([index, &mode](bool b, int) { return valueText(index, b ? 1 : 0, mode); })
                                     .withValueFromStringFunction([index, def, &mode](const juce::String& t) {
                                       return valueFromText(index, t, def, mode) != 0;
                                     })),
        index_(index),
        listener_(l) {}
  int nativeValue() const override { return get() ? 1 : 0; }
  float normalisedFor(int v) const override { return v != 0 ? 1.0f : 0.0f; }
  juce::RangedAudioParameter& parameter() override { return *this; }

 protected:
  void valueChanged(bool b) override {
    if (!tlsOwnWrite) listener_.hostParameterWritten(index_, b ? 1 : 0);
  }

 private:
  const int index_;
  HostWriteListener& listener_;
};

bool isPerfBool(int index) {
  const int p = perfForHostIndex(index);
  return p == vjn6::P_PORTA_ON || p == vjn6::P_HOLD || p == vjn6::P_ARP_ON;
}
}  // namespace

OwnWriteScope::OwnWriteScope() : previous_(tlsOwnWrite) { tlsOwnWrite = true; }
OwnWriteScope::~OwnWriteScope() { tlsOwnWrite = previous_; }

int hostIndexForPerf(int perf) {
  if (perf >= 0 && perf < vjn6::kNumHostPerf) return vjn6::kNumFields + perf;
  if (perf == vjn6::P_TUNE) return kNumHostParams - 1;
  return -1;
}

int perfForHostIndex(int index) {
  if (index >= vjn6::kNumFields && index < vjn6::kNumFields + vjn6::kNumHostPerf) return index - vjn6::kNumFields;
  if (index == kNumHostParams - 1) return vjn6::P_TUNE;
  return -1;
}

std::unique_ptr<juce::RangedAudioParameter> createHostParameter(int index, int def, HostWriteListener& listener,
                                                                 NativeParam*& access, const std::atomic<int>& mode) {
  const auto& info = vjn6::display::hostParam(index);
  if (isPerfBool(index)) {
    auto p = std::make_unique<BoolParam>(index, info, def, listener, mode);
    access = p.get();
    return p;
  }
  if (info.isChoice) {
    auto p = std::make_unique<ChoiceParam>(index, info, def, listener, mode);
    access = p.get();
    return p;
  }
  auto p = std::make_unique<IntParam>(index, info, def, listener, mode);
  access = p.get();
  return p;
}

}  // namespace vjn6plugin
