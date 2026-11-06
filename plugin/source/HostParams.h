// The 40 host parameters of SPEC §4.8 as JUCE parameters.
//
// Host writes are detected in valueChanged(): every setValue() that does not happen inside an
// OwnWriteScope (the plugin reporting a core change to the host) is a host write and is handed to
// the HostWriteListener, which forwards it to the core on the rendering thread. Changes the plugin
// reports itself are therefore never applied back to the core (R-S3).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace vjn6plugin {

constexpr int kNumHostParams = 40;

class HostWriteListener {
 public:
  virtual ~HostWriteListener() = default;
  // Any thread (host automation thread, audio thread, message thread). Must be lock-free.
  virtual void hostParameterWritten(int index, int nativeValue) = 0;
};

// Marks setValue() calls on this thread as the plugin's own reports (not host writes).
class OwnWriteScope {
 public:
  OwnWriteScope();
  ~OwnWriteScope();
  OwnWriteScope(const OwnWriteScope&) = delete;
  OwnWriteScope& operator=(const OwnWriteScope&) = delete;

 private:
  bool previous_;
};

// Uniform access to the native value (§4.1/§4.2 units) of an Int, Choice or Bool parameter.
class NativeParam {
 public:
  virtual ~NativeParam() = default;
  virtual int nativeValue() const = 0;
  virtual float normalisedFor(int nativeValue) const = 0;
  virtual juce::RangedAudioParameter& parameter() = 0;
};

// Host parameter index (§4.8 order) -> core field (0..24) or perf id; -1 if none.
inline int hostIndexForField(int field) { return field >= 0 && field < 25 ? field : -1; }
int hostIndexForPerf(int perf);
int perfForHostIndex(int index);  // -1 for fields

// Creates parameter `index` with the given default (native units). `access` receives the
// NativeParam view of the returned object.
std::unique_ptr<juce::RangedAudioParameter> createHostParameter(int index, int defaultNative, HostWriteListener& listener,
                                                                 NativeParam*& access);

}  // namespace vjn6plugin
