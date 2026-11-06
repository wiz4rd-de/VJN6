// VJN6 AudioProcessor: one vjn6::Instrument per instance (SPEC §4.8, §9.3, §9.4, R-S1–R-S3).
//
// Threads:
//  - rendering: processBlock, or the fallback driver while the host/device does not process
//    (an atomic guard lets exactly one thread render the core at a time, DECISIONS "Fallback driver");
//  - producers: every Instrument::post goes through postCommand(), serialised by one mutex
//    (message thread, host threads calling setStateInformation/setCurrentProgram, MIDI threads);
//  - consumer: the message-thread timer (pollNow) is the only caller of Bridge::poll/pollEvent.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "HostParams.h"
#include "StateStore.h"
#include "util/lockfree.h"
#include "vjn6/bridge.h"
#include "vjn6/instrument.h"

namespace vjn6plugin {

// Services only the standalone app provides (devices and the chosen MIDI output, §5.6).
class StandaloneServices {
 public:
  virtual ~StandaloneServices() = default;
  virtual void openDeviceDialog() = 0;
  virtual void chooseMidiOutput(const juce::String& idOrName) = 0;
  virtual void sendMidiOut(const uint8_t* bytes, int len) = 0;  // message thread
  virtual juce::Array<juce::var> midiOutputs() = 0;            // [{id, name}]
  virtual juce::String selectedMidiOutput() = 0;               // id or ""
  virtual bool midiOutputOpen() = 0;
};

class VJN6AudioProcessor final : public juce::AudioProcessor, private HostWriteListener, private juce::Timer {
 public:
  VJN6AudioProcessor();
  ~VJN6AudioProcessor() override;

  // ---- AudioProcessor ----
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "VJN6"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return vjn6::kNumSlots; }
  int getCurrentProgram() override { return currentProgram_.load(); }
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // ---- bridge to the UI and the core (message thread unless noted) ----
  bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
  vjn6::Instrument& instrument() { return *inst_; }

  // Any non-audio thread.
  bool postCommand(const vjn6::Command& c);
  // One UI -> host message (the `vjn6Command` native function).
  void handleUiMessage(const juce::String& json);
  // UI messages go to the sink once the UI has sent "ready" (editor) until it is cleared.
  using UiSink = std::function<void(const std::vector<std::string>&)>;
  void setUiSink(UiSink sink);
  void clearUiSink();
  void uiPageUnloaded();
  bool isUiConnected() const { return uiReady_; }
  // Consumer tick: drains core events, notifies the host, autosaves (standalone), feeds the UI.
  void pollNow();

  // Editor zoom (§4.4): called with every Zoom setting change.
  std::function<void(int zoom)> onZoomChanged;
  int zoomSetting();
  juce::Point<int> lastEditorSize() const { return lastEditorSize_; }
  void setLastEditorSize(juce::Point<int> s) { lastEditorSize_ = s; }
  void editorClosed();

  // Standalone integration.
  void setStandaloneServices(StandaloneServices* s);
  void standaloneEnvironmentChanged() { envPending_ = uiReady_; }
  // MIDI from a non-audio thread (standalone inputs while no audio device runs; CC123 on unplug).
  void injectMidi(const uint8_t* bytes, int len);
  bool isRenderingFromDevice() const;  // processBlock ran in the last 100 ms
  StateStore* stateStore() { return store_.get(); }
  juce::String saveStandaloneStateNow();  // returns "" or the failure reason

  // Test seams (plugin/tests): observe every core event; disable the fallback thread; render
  // silently on the calling thread when no audio thread runs.
  std::function<void(const vjn6::Event&)> eventObserver;
  void setFallbackDriverEnabled(bool on) { fallbackEnabled_ = on; }
  void pumpCore(int samples);
  NativeParam& hostParam(int index) { return *access_[static_cast<size_t>(index)]; }

 private:
  class FallbackDriver;
  struct MidiMsg {
    uint8_t len = 0;
    uint8_t data[24] = {};
  };

  void hostParameterWritten(int index, int nativeValue) override;
  void timerCallback() override { pollNow(); }

  // rendering-thread side
  bool tryAcquireRender();
  void acquireRenderBlocking();
  void releaseRender() { rendering_.store(false, std::memory_order_release); }
  void beforeRender();  // host writes and injected MIDI into the core
  void renderSilently(int samples);
  void fallbackTick(double nowMs);

  // consumer side
  void handleEvent(const vjn6::Event& e);
  void onParameterEvent(int index, int value, vjn6::Origin origin);
  void reportToHost(int index, int value);
  void refreshProgramNames();
  void syncParametersFromState(const vjn6::Patch& edit, const vjn6::PerfSettings& perf, bool notify);
  void flushUi(std::vector<std::string>& out);
  std::string envJson();
  void handleGesture(const juce::var& msg);
  void endAllGestures();
  void releaseUiInput();
  void openFileDialog();
  void exportDialog(const juce::String& what);
  void loadFileFromDisk(const juce::File& f);
  void sendToUi(std::string msg);

  std::unique_ptr<vjn6::Instrument> inst_;
  std::unique_ptr<vjn6::Bridge> bridge_;
  std::mutex producerMutex_;

  std::array<NativeParam*, kNumHostParams> access_{};
  std::array<std::atomic<int>, kNumHostParams> hostValues_{};
  std::atomic<uint64_t> hostDirty_{0};
  std::array<std::atomic<bool>, kNumHostParams> lastWriterIsPlugin_{};
  std::array<bool, kNumHostParams> gestureActive_{};
  std::array<double, kNumHostParams> gestureEndDue_{};
  bool programChanged_ = false, namesDirty_ = false, stateChanged_ = false, programAnnounced_ = false;

  // rendering
  std::atomic<bool> rendering_{false};
  std::atomic<double> lastDeviceRenderMs_{-1.0e12};
  std::atomic<bool> fallbackEnabled_{true};
  std::vector<float> scratchL_, scratchR_;
  vjn6::SpscQueue<MidiMsg> midiOutFifo_{1024};
  vjn6::SpscQueue<MidiMsg> midiInFifo_{1024};
  std::mutex midiInMutex_;
  double fallbackLastMs_ = 0.0;
  std::unique_ptr<FallbackDriver> fallback_;

  // programs
  std::atomic<int> currentProgram_{0};
  std::mutex namesMutex_;
  std::array<juce::String, vjn6::kNumSlots> programNames_;

  // UI
  UiSink uiSink_;
  bool uiReady_ = false;
  bool envPending_ = false;
  std::vector<std::string> pendingUi_;
  juce::Point<int> lastEditorSize_;
  std::unique_ptr<juce::FileChooser> chooser_;
  juce::File lastDirectory_;

  // standalone
  StandaloneServices* services_ = nullptr;
  std::unique_ptr<StateStore> store_;
  juce::String saveError_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VJN6AudioProcessor)
};

}  // namespace vjn6plugin
