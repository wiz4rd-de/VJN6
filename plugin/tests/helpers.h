// Helpers for the plugin tests: a "test host" that drives the real AudioProcessor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "PluginProcessor.h"

namespace vjn6test {

using vjn6plugin::VJN6AudioProcessor;

// Creates a processor as the given wrapper would (plugin formats or the standalone app).
inline std::unique_ptr<VJN6AudioProcessor> makeProcessor(bool standalone = false, double fs = 48000.0, int block = 512,
                                                         bool fallback = false) {
  juce::AudioProcessor::setTypeOfNextNewPlugin(standalone ? juce::AudioProcessor::wrapperType_Standalone
                                                          : juce::AudioProcessor::wrapperType_VST3);
  auto p = std::make_unique<VJN6AudioProcessor>();
  juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
  p->setFallbackDriverEnabled(fallback);
  p->setRateAndBufferSizeDetails(fs, block);
  p->prepareToPlay(fs, block);
  return p;
}

// One processBlock with optional MIDI; returns the RMS of the output.
class TestHost {
 public:
  explicit TestHost(VJN6AudioProcessor& p, int block = 512) : proc(p), buffer(2, block) {}

  double process(const juce::MidiBuffer& in = {}) {
    midi = in;
    buffer.clear();
    proc.processBlock(buffer, midi);
    outMidi = midi;
    double sum = 0.0;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
      for (int i = 0; i < buffer.getNumSamples(); ++i) sum += double(buffer.getSample(c, i)) * buffer.getSample(c, i);
    return std::sqrt(sum / (buffer.getNumChannels() * buffer.getNumSamples()));
  }
  // Processes `blocks` blocks and polls the consumer after each; returns the overall RMS.
  double run(int blocks, const juce::MidiBuffer& first = {}) {
    double sum = 0.0;
    for (int b = 0; b < blocks; ++b) {
      const double r = process(b == 0 ? first : juce::MidiBuffer());
      sum += r * r;
      proc.pollNow();
    }
    return blocks > 0 ? std::sqrt(sum / blocks) : 0.0;
  }
  void step() {
    process();
    proc.pollNow();
  }

  VJN6AudioProcessor& proc;
  juce::AudioBuffer<float> buffer;
  juce::MidiBuffer midi, outMidi;
};

inline juce::MidiBuffer midiOf(std::initializer_list<uint8_t> bytes, int pos = 0) {
  juce::MidiBuffer b;
  std::vector<uint8_t> v(bytes);
  b.addEvent(v.data(), static_cast<int>(v.size()), pos);
  return b;
}

// Counts host notifications per parameter (the plugin reporting changes to the host).
class HostListener final : public juce::AudioProcessorListener {
 public:
  explicit HostListener(juce::AudioProcessor& p) : proc(p) { p.addListener(this); }
  ~HostListener() override { proc.removeListener(this); }
  void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override {
    if (index >= 0 && index < 64) ++changes[static_cast<size_t>(index)];
  }
  void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override {
    if (d.latencyChanged) ++latencyChanges;
    if (d.programChanged) ++programChanges;
  }
  void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override {
    ++gestureBegins[static_cast<size_t>(index)];
  }
  void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index) override {
    ++gestureEnds[static_cast<size_t>(index)];
  }
  int total() const {
    int t = 0;
    for (int c : changes) t += c;
    return t;
  }
  void reset() {
    changes.fill(0);
    latencyChanges = programChanges = 0;
  }

  juce::AudioProcessor& proc;
  std::array<int, 64> changes{};
  std::array<int, 64> gestureBegins{};
  std::array<int, 64> gestureEnds{};
  int latencyChanges = 0, programChanges = 0;
};

inline std::unique_ptr<vjn6::Snapshot> snapshotOf(VJN6AudioProcessor& p) {
  auto s = std::make_unique<vjn6::Snapshot>();
  p.instrument().snapshot(*s);
  return s;
}

inline juce::MemoryBlock stateOf(VJN6AudioProcessor& p) {
  juce::MemoryBlock m;
  p.getStateInformation(m);
  return m;
}

}  // namespace vjn6test
