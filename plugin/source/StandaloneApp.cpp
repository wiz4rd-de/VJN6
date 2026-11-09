// The standalone app (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP): JUCE's StandalonePluginHolder for the
// audio device, MIDI inputs and the JUCE standalone properties file; VJN6's own window, device
// dialog and single MIDI output choice (§4.4, §5.6). The state files (R-D2, F-1, F-13) are handled
// by the processor itself (StateStore) when it runs as the standalone.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace vjn6plugin {

namespace {
const juce::Colour kBackground(0xff111111);

class MainWindow final : public juce::DocumentWindow {
 public:
  MainWindow(juce::AudioProcessor& processor, juce::AudioProcessorEditor* editor)
      : juce::DocumentWindow("VJN6", kBackground, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
        processor_(processor) {
    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(true, false);
    setResizeLimits(VJN6Editor::kMinWidth, VJN6Editor::kMinHeight, VJN6Editor::kMaxWidth, VJN6Editor::kMaxHeight);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(VJN6Editor::kBaseWidth) / VJN6Editor::kBaseHeight);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }
  ~MainWindow() override {
    // as the plugin wrappers do: the processor forgets its editor before the editor is deleted
    if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(getContentComponent()))
      processor_.editorBeingDeleted(editor);
    clearContentComponent();
  }
  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

 private:
  juce::AudioProcessor& processor_;
};
}  // namespace

class StandaloneApp final : public juce::JUCEApplication,
                            public StandaloneServices,
                            private juce::Timer,
                            private juce::MidiInputCallback,
                            private juce::ChangeListener {
 public:
  const juce::String getApplicationName() override { return "VJN6"; }
  const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
  bool moreThanOneInstanceAllowed() override { return true; }  // independent processes (§3)
  void anotherInstanceStarted(const juce::String&) override {}

  void initialise(const juce::String&) override {
    juce::PropertiesFile::Options options;
    options.applicationName = "VJN6";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
#endif
    properties_.setStorageParameters(options);
    auto* settings = properties_.getUserSettings();
    const bool firstRun = !settings->containsKey("audioSetup");

    holder_ = std::make_unique<juce::StandalonePluginHolder>(settings, false, juce::String(), nullptr,
                                                              juce::Array<juce::StandalonePluginHolder::PluginInOuts>(), false);
    processor_ = dynamic_cast<VJN6AudioProcessor*>(holder_->processor.get());
    if (processor_ == nullptr) {
      quit();
      return;
    }

    auto& dm = holder_->deviceManager;
    dm.setDefaultMidiOutputDevice({});  // VJN6's MIDI output is the single choice below, not JUCE's
    dm.addMidiInputDeviceCallback({}, this);
    dm.addChangeListener(this);
    lastInputs_ = juce::MidiInput::getAvailableDevices();
    if (firstRun)  // §4.4 default: all MIDI inputs
      for (const auto& d : lastInputs_) dm.setMidiInputDeviceEnabled(d.identifier, true);

    chosenId_ = settings->getValue("midiOutputId");
    chosenName_ = settings->getValue("midiOutputName");
    lastOutputs_ = juce::MidiOutput::getAvailableDevices();
    openChosenOutput();
    processor_->setStandaloneServices(this);

    const int w = settings->getIntValue("windowWidth"), h = settings->getIntValue("windowHeight");
    if (w >= VJN6Editor::kMinWidth && h >= VJN6Editor::kMinHeight) processor_->setLastEditorSize({w, h});
    if (!juce::Desktop::getInstance().getDisplays().displays.isEmpty())
      window_ = std::make_unique<MainWindow>(*processor_, processor_->createEditorAndMakeActive());
    startTimer(1000);
  }

  void shutdown() override {
    stopTimer();
    auto* settings = properties_.getUserSettings();
    if (processor_ != nullptr) {
      window_ = nullptr;  // closes the editor (releases keyboard notes, F-13)
      const auto size = processor_->lastEditorSize();
      settings->setValue("windowWidth", size.x);
      settings->setValue("windowHeight", size.y);
      processor_->setStandaloneServices(nullptr);
    }
    if (holder_ != nullptr) {
      holder_->deviceManager.removeMidiInputDeviceCallback({}, this);
      holder_->deviceManager.removeChangeListener(this);
    }
    midiOut_ = nullptr;
    processor_ = nullptr;
    holder_ = nullptr;  // stops audio, then the processor saves state.bin on destruction (F-13)
    properties_.saveIfNeeded();
  }

  void systemRequestedQuit() override {
    if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
      juce::Timer::callAfterDelay(100, [] {
        if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
      });
    } else {
      quit();
    }
  }

  // ---- StandaloneServices ----
  void openDeviceDialog() override {
    if (holder_ == nullptr) return;
    juce::DialogWindow::LaunchOptions o;
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(holder_->deviceManager, 0, 0, 1, 2, true, false,
                                                                         true, false);
    selector->setSize(520, 460);
    o.content.setOwned(selector.release());
    o.dialogTitle = "Audio/MIDI devices";
    o.dialogBackgroundColour = o.content->getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
  }

  void chooseMidiOutput(const juce::String& idOrName) override {
    juce::String id, name;
    if (idOrName.isNotEmpty()) {
      for (const auto& d : juce::MidiOutput::getAvailableDevices()) {
        if (d.identifier == idOrName || d.name == idOrName) {
          id = d.identifier;
          name = d.name;
          break;
        }
      }
    }
    chosenId_ = id;
    chosenName_ = name;
    auto* settings = properties_.getUserSettings();
    settings->setValue("midiOutputId", chosenId_);
    settings->setValue("midiOutputName", chosenName_);
    properties_.saveIfNeeded();
    openChosenOutput();
    if (processor_ != nullptr) processor_->standaloneEnvironmentChanged();
  }

  void sendMidiOut(const uint8_t* bytes, int len) override {
    if (midiOut_ != nullptr && len > 0) midiOut_->sendMessageNow(juce::MidiMessage(bytes, len));
  }

  juce::Array<juce::var> midiOutputs() override {
    juce::Array<juce::var> list;
    for (const auto& d : lastOutputs_) {
      auto* o = new juce::DynamicObject();
      o->setProperty("id", d.identifier);
      o->setProperty("name", d.name);
      list.add(juce::var(o));
    }
    return list;
  }
  juce::String selectedMidiOutput() override { return midiOut_ != nullptr ? chosenId_ : juce::String(); }
  bool midiOutputOpen() override { return midiOut_ != nullptr; }

 private:
  void openChosenOutput() {
    midiOut_ = nullptr;
    if (chosenId_.isEmpty()) return;
    for (const auto& d : lastOutputs_) {
      if (d.identifier == chosenId_ || d.name == chosenName_) {
        midiOut_ = juce::MidiOutput::openDevice(d.identifier);
        if (midiOut_ != nullptr) chosenId_ = d.identifier;
        return;
      }
    }
  }

  // Device lists: newly plugged inputs are enabled; an unplugged input releases its notes (R-M1);
  // unplugged outputs disappear from the list without an error (§5.6).
  void timerCallback() override {
    if (holder_ == nullptr || processor_ == nullptr) return;
    const auto inputs = juce::MidiInput::getAvailableDevices();
    if (inputs != lastInputs_) {
      bool removed = false;
      for (const auto& d : lastInputs_) removed = removed || !inputs.contains(d);
      for (const auto& d : inputs)
        if (!lastInputs_.contains(d)) holder_->deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
      lastInputs_ = inputs;
      if (removed) {
        // R-M1: acts as CC123 for the notes of the inputs (merged into MIDI port 0, DECISIONS)
        vjn6::Command c;
        c.type = vjn6::CommandType::MidiPortGone;
        c.src = vjn6::Source::System;
        c.port = 0;
        processor_->postCommand(c);
      }
    }
    const auto outputs = juce::MidiOutput::getAvailableDevices();
    if (outputs != lastOutputs_) {
      lastOutputs_ = outputs;
      bool present = false;
      for (const auto& d : outputs) present = present || d.identifier == chosenId_ || d.name == chosenName_;
      if (!present) midiOut_ = nullptr;
      else if (midiOut_ == nullptr) openChosenOutput();
      processor_->standaloneEnvironmentChanged();
    }
  }

  // MIDI inputs reach processBlock through JUCE's player while the device runs; without an audio
  // device they go to the core directly (§10: no device -> silent, everything else works).
  void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
    if (processor_ != nullptr && !processor_->isRenderingFromDevice()) processor_->injectMidi(m.getRawData(), m.getRawDataSize());
  }

  void changeListenerCallback(juce::ChangeBroadcaster*) override {
    if (holder_ == nullptr) return;
    holder_->saveAudioDeviceState();
    properties_.saveIfNeeded();
  }

  juce::ApplicationProperties properties_;
  std::unique_ptr<juce::StandalonePluginHolder> holder_;
  VJN6AudioProcessor* processor_ = nullptr;
  std::unique_ptr<MainWindow> window_;
  std::unique_ptr<juce::MidiOutput> midiOut_;
  juce::String chosenId_, chosenName_;
  juce::Array<juce::MidiDeviceInfo> lastInputs_, lastOutputs_;
};

}  // namespace vjn6plugin

START_JUCE_APPLICATION(vjn6plugin::StandaloneApp)
