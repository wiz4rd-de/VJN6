#include "PluginProcessor.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "PluginEditor.h"
#include "vjn6/files.h"

// Internal core headers (keyboard-play default, state codec); not written for JUCE's warning set.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wsign-conversion", "-Wshorten-64-to-32", "-Wconversion", "-Wshadow-field-in-constructor",
                                    "-Wshadow", "-Wfloat-equal", "-Wimplicit-int-conversion")
#include "core/core.h"
#include "state/state.h"
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

namespace vjn6plugin {

namespace {
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

constexpr double kDeviceTimeoutMs = 100.0;      // fallback driver takes over after this
constexpr double kGestureEndDelayMs = 50.0;     // let the drag's last value reach the host first
constexpr int kDefaultBlock = 512;
constexpr std::size_t kMaxFileRead = (1u << 20) + 1;  // over 1 MiB is rejected by files::parse

int nativeOf(int index, const vjn6::Patch& edit, const vjn6::PerfSettings& perf) {
  if (index < vjn6::kNumFields) return edit.v[index];
  return perf.v[perfForHostIndex(index)];
}

std::unique_ptr<vjn6::CoreState> stateFromSnapshot(const vjn6::Snapshot& s) {
  auto st = std::make_unique<vjn6::CoreState>();
  for (int i = 0; i < vjn6::kNumSlots; ++i) st->bank[i] = s.bank[i];
  st->edit = s.edit;
  st->editName = s.editName;
  st->stored = s.stored;
  st->edited = s.edited;
  st->manual = s.manual;
  st->currentSlot = s.currentSlot;
  st->pendingBank = s.pendingBank;
  st->pendingGroup = s.pendingGroup;
  st->perf = s.perf;
  st->settings = s.settings;
  return st;
}
}  // namespace

// Renders the core silently while no audio callback runs (DECISIONS "Fallback driver").
class VJN6AudioProcessor::FallbackDriver final : public juce::Thread {
 public:
  explicit FallbackDriver(VJN6AudioProcessor& o) : juce::Thread("VJN6 fallback driver"), owner_(o) {}
  ~FallbackDriver() override { stopThread(2000); }
  void run() override {
    while (!threadShouldExit()) {
      wait(5);
      owner_.fallbackTick(nowMs());
    }
  }

 private:
  VJN6AudioProcessor& owner_;
};

VJN6AudioProcessor::VJN6AudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
  inst_ = std::make_unique<vjn6::Instrument>(44100.0, kDefaultBlock);
  // §4.4: computer keyboard play defaults to off in the plugin, on in the standalone.
  if (!isStandalone()) inst_->core().setKeyboardPlayDefault(false);
  scratchL_.assign(kDefaultBlock, 0.0f);
  scratchR_.assign(kDefaultBlock, 0.0f);

  if (isStandalone()) {
    // F-1: load state.bin before the first render so that MIDI works from the first block.
    store_ = std::make_unique<StateStore>(StateStore::defaultDirectory());
    vjn6::StateBlob blob;
    if (store_->readStartupState(blob) != StateStore::Startup::FirstRun) {
      {
        std::lock_guard<std::mutex> lock(producerMutex_);
        inst_->loadState(blob);
      }
      pumpCore(64);
    }
  }

  auto first = std::make_unique<vjn6::CoreState>();
  vjn6::firstRunState(*first, isStandalone());
  auto snap = std::make_unique<vjn6::Snapshot>();
  inst_->snapshot(*snap);
  mode_.store(snap->settings.v[vjn6::S_MODE] == 1 ? 1 : 0);
  for (int i = 0; i < kNumHostParams; ++i) {
    NativeParam* access = nullptr;
    auto p = createHostParameter(i, nativeOf(i, first->edit, first->perf), *this, access, mode_);
    access_[static_cast<size_t>(i)] = access;
    hostValues_[static_cast<size_t>(i)].store(nativeOf(i, snap->edit, snap->perf));
    lastWriterIsPlugin_[static_cast<size_t>(i)].store(false);
    addParameter(p.release());
  }
  syncParametersFromState(snap->edit, snap->perf, false);
  for (int s = 0; s < vjn6::kNumSlots; ++s) programNames_[static_cast<size_t>(s)] = juce::String(snap->bank[s].name.s);
  currentProgram_.store(snap->currentSlot - 1);

  bridge_ = std::make_unique<vjn6::Bridge>(*inst_);
  bridge_->onEvent = [this](const vjn6::Event& e) { handleEvent(e); };
  setLatencySamples(inst_->latencySamples());

  fallback_ = std::make_unique<FallbackDriver>(*this);
  fallback_->startThread();
  startTimerHz(60);
}

VJN6AudioProcessor::~VJN6AudioProcessor() {
  stopTimer();
  fallback_.reset();
  if (store_ != nullptr) {
    // F-13: save on quit, after the commands posted before the quit took effect.
    pumpCore(static_cast<int>(inst_->sampleRate() * 0.01) + 1);
    saveStandaloneStateNow();
  }
  for (int i = 0; i < kNumHostParams; ++i)
    if (gestureActive_[static_cast<size_t>(i)]) access_[static_cast<size_t>(i)]->parameter().endChangeGesture();
}

// ------------------------------------------------------------------------------------------------
// rendering

bool VJN6AudioProcessor::tryAcquireRender() {
  bool expected = false;
  return rendering_.compare_exchange_strong(expected, true, std::memory_order_acquire);
}

void VJN6AudioProcessor::acquireRenderBlocking() {
  while (!tryAcquireRender()) std::this_thread::sleep_for(std::chrono::microseconds(100));
}

bool VJN6AudioProcessor::isRenderingFromDevice() const {
  return nowMs() - lastDeviceRenderMs_.load(std::memory_order_relaxed) < kDeviceTimeoutMs;
}

void VJN6AudioProcessor::beforeRender() {
  uint64_t mask = hostDirty_.exchange(0, std::memory_order_acq_rel);
  while (mask != 0) {
    int i = 0;
    while (((mask >> i) & 1u) == 0) ++i;
    mask &= ~(uint64_t{1} << i);
    inst_->hostParam(i, hostValues_[static_cast<size_t>(i)].load(std::memory_order_acquire), 0);
  }
  MidiMsg m;
  while (midiInFifo_.pop(m)) inst_->midi(m.data, m.len, 0);
}

void VJN6AudioProcessor::renderSilently(int samples) {
  const int cap = static_cast<int>(scratchL_.size());
  while (samples > 0) {
    const int n = samples < cap ? samples : cap;
    inst_->render(scratchL_.data(), scratchR_.data(), n);
    samples -= n;
  }
}

void VJN6AudioProcessor::pumpCore(int samples) {
  acquireRenderBlocking();
  beforeRender();
  renderSilently(samples);
  releaseRender();
}

void VJN6AudioProcessor::fallbackTick(double now) {
  if (!fallbackEnabled_.load() || isRenderingFromDevice()) {
    fallbackLastMs_ = 0.0;
    return;
  }
  if (fallbackLastMs_ <= 0.0) {
    fallbackLastMs_ = now;
    return;
  }
  const double fs = inst_->sampleRate();
  int n = static_cast<int>((now - fallbackLastMs_) * fs / 1000.0);
  if (n <= 0) return;
  const int cap = static_cast<int>(fs * 0.1);
  if (n > cap) n = cap;
  fallbackLastMs_ = now;
  if (!tryAcquireRender()) return;
  beforeRender();
  MidiMsg m;
  while (midiOutFifo_.pop(m)) {
  }  // nobody processes: MIDI output for the host is dropped (R-M3)
  const int chunk = static_cast<int>(scratchL_.size());
  while (n > 0 && !isRenderingFromDevice()) {
    const int k = n < chunk ? n : chunk;
    inst_->render(scratchL_.data(), scratchR_.data(), k);
    n -= k;
  }
  releaseRender();
}

void VJN6AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  const int block = samplesPerBlock > 0 ? samplesPerBlock : kDefaultBlock;
  acquireRenderBlocking();
  inst_->prepare(sampleRate, block);
  const size_t size = static_cast<size_t>(block > kDefaultBlock ? block : kDefaultBlock);
  scratchL_.assign(size, 0.0f);
  scratchR_.assign(size, 0.0f);
  releaseRender();
  setLatencySamples(inst_->latencySamples());
}

bool VJN6AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  if (layouts.inputBuses.size() > 0 && !layouts.getMainInputChannelSet().isDisabled()) return false;
  const auto out = layouts.getMainOutputChannelSet();
  return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void VJN6AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  const int n = buffer.getNumSamples();
  lastDeviceRenderMs_.store(nowMs(), std::memory_order_relaxed);
  if (!tryAcquireRender()) {
    // The fallback driver stops after its current chunk (microseconds); never wait longer than 2 ms.
    const double giveUp = nowMs() + 2.0;
    while (!tryAcquireRender()) {
      if (nowMs() > giveUp) {
        buffer.clear();
        midi.clear();
        return;
      }
      std::this_thread::yield();
    }
  }
  beforeRender();
  for (const auto meta : midi) {
    int pos = meta.samplePosition;
    if (pos < 0) pos = 0;
    if (pos >= n && n > 0) pos = n - 1;
    inst_->midi(meta.data, meta.numBytes, pos);
  }
  const int numCh = buffer.getNumChannels();
  if (n > 0) {
    if (numCh >= 2) {
      inst_->render(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
      for (int c = 2; c < numCh; ++c) buffer.clear(c, 0, n);
    } else {
      float* out = numCh == 1 ? buffer.getWritePointer(0) : nullptr;
      const int cap = static_cast<int>(scratchL_.size());
      int done = 0;
      while (done < n) {
        const int k = (n - done) < cap ? (n - done) : cap;
        inst_->render(scratchL_.data(), scratchR_.data(), k);
        if (out != nullptr)
          for (int i = 0; i < k; ++i) out[done + i] = 0.5f * (scratchL_[static_cast<size_t>(i)] + scratchR_[static_cast<size_t>(i)]);
        done += k;
      }
    }
  }
  releaseRender();
  midi.clear();
  MidiMsg m;
  while (midiOutFifo_.pop(m)) midi.addEvent(m.data, m.len, 0);
}

// ------------------------------------------------------------------------------------------------
// host parameters and programs

void VJN6AudioProcessor::hostParameterWritten(int index, int nativeValue) {
  const auto i = static_cast<size_t>(index);
  lastWriterIsPlugin_[i].store(false, std::memory_order_relaxed);
  hostValues_[i].store(nativeValue, std::memory_order_release);
  hostDirty_.fetch_or(uint64_t{1} << index, std::memory_order_acq_rel);
}

void VJN6AudioProcessor::reportToHost(int index, int value) {
  NativeParam& p = *access_[static_cast<size_t>(index)];
  lastWriterIsPlugin_[static_cast<size_t>(index)].store(true, std::memory_order_relaxed);
  OwnWriteScope own;
  p.parameter().setValueNotifyingHost(p.normalisedFor(value));
}

void VJN6AudioProcessor::syncParametersFromState(const vjn6::Patch& edit, const vjn6::PerfSettings& perf, bool notify) {
  for (int i = 0; i < kNumHostParams; ++i) {
    NativeParam& p = *access_[static_cast<size_t>(i)];
    const int v = nativeOf(i, edit, perf);
    if (p.nativeValue() == v) continue;
    if (notify) {
      reportToHost(i, v);
    } else {
      OwnWriteScope own;
      p.parameter().setValue(p.normalisedFor(v));
    }
  }
}

void VJN6AudioProcessor::setCurrentProgram(int index) {
  if (index < 0 || index >= vjn6::kNumSlots) return;
  currentProgram_.store(index);
  // F-4 via host program: never transmits; the core ignores selecting the current slot.
  postCommand(vjn6::Command::selectSlot(index + 1, vjn6::Source::Host));
  if (!isRenderingFromDevice()) pumpCore(static_cast<int>(inst_->sampleRate() * 0.005) + 1);
}

const juce::String VJN6AudioProcessor::getProgramName(int index) {
  if (index < 0 || index >= vjn6::kNumSlots) return {};
  std::lock_guard<std::mutex> lock(namesMutex_);
  return programNames_[static_cast<size_t>(index)];
}

void VJN6AudioProcessor::refreshProgramNames() {
  auto snap = std::make_unique<vjn6::Snapshot>();
  inst_->snapshot(*snap);
  std::lock_guard<std::mutex> lock(namesMutex_);
  for (int s = 0; s < vjn6::kNumSlots; ++s) programNames_[static_cast<size_t>(s)] = juce::String(snap->bank[s].name.s);
}

// ------------------------------------------------------------------------------------------------
// state (R-D1: one blob per instance)

void VJN6AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
  vjn6::StateBlob blob;
  inst_->saveState(blob);
  destData.replaceAll(blob.bytes.data(), blob.bytes.size());
}

void VJN6AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
  vjn6::StateBlob blob;
  if (data != nullptr && sizeInBytes > 0)
    blob.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + sizeInBytes);
  {
    std::lock_guard<std::mutex> lock(producerMutex_);
    inst_->loadState(blob);
  }
  // The host sees the restored values at once; the core applies the blob at its next control boundary.
  auto st = std::make_unique<vjn6::CoreState>();
  vjn6::firstRunState(*st, isStandalone());
  const auto r = vjn6::decodeState(blob.bytes.data(), blob.bytes.size(), *st);
  if (r == vjn6::BlobResult::Invalid || r == vjn6::BlobResult::Empty) vjn6::firstRunState(*st, isStandalone());
  currentProgram_.store(st->currentSlot - 1);
  {
    std::lock_guard<std::mutex> lock(namesMutex_);
    for (int s = 0; s < vjn6::kNumSlots; ++s) programNames_[static_cast<size_t>(s)] = juce::String(st->bank[s].name.s);
  }
  const int newMode = st->settings.v[vjn6::S_MODE] == 1 ? 1 : 0;
  const bool modeChanged = mode_.exchange(newMode) != newMode;
  syncParametersFromState(st->edit, st->perf, true);
  if (!isRenderingFromDevice()) pumpCore(static_cast<int>(inst_->sampleRate() * 0.005) + 1);
  updateHostDisplay(ChangeDetails().withProgramChanged(true).withParameterInfoChanged(modeChanged));
}

// ------------------------------------------------------------------------------------------------
// producer side

bool VJN6AudioProcessor::postCommand(const vjn6::Command& c) {
  std::lock_guard<std::mutex> lock(producerMutex_);
  return inst_->post(c);
}

void VJN6AudioProcessor::injectMidi(const uint8_t* bytes, int len) {
  if (bytes == nullptr || len <= 0) return;
  std::lock_guard<std::mutex> lock(midiInMutex_);
  for (int off = 0; off < len; off += 24) {
    MidiMsg m;
    m.len = static_cast<uint8_t>((len - off) < 24 ? (len - off) : 24);
    std::memcpy(m.data, bytes + off, m.len);
    midiInFifo_.push(m);
  }
}

// ------------------------------------------------------------------------------------------------
// consumer side (message thread)

void VJN6AudioProcessor::pollNow() {
  std::vector<std::string> out;
  programChanged_ = namesDirty_ = stateChanged_ = paramInfoChanged_ = false;
  bridge_->poll(out);
  if (!programAnnounced_) {
    // tell the wrapper the current program once it listens (AU present preset, VST3 program list)
    programAnnounced_ = true;
    programChanged_ = true;
  }
  if (namesDirty_) refreshProgramNames();
  if (programChanged_ || stateChanged_ || paramInfoChanged_)
    updateHostDisplay(ChangeDetails()
                          .withProgramChanged(programChanged_)
                          .withNonParameterStateChanged(stateChanged_)
                          .withParameterInfoChanged(paramInfoChanged_));

  const double now = nowMs();
  for (int i = 0; i < kNumHostParams; ++i) {
    const auto k = static_cast<size_t>(i);
    if (gestureEndDue_[k] > 0.0 && now >= gestureEndDue_[k]) {
      gestureEndDue_[k] = 0.0;
      if (gestureActive_[k]) {
        gestureActive_[k] = false;
        access_[k]->parameter().endChangeGesture();
      }
    }
  }

  if (store_ != nullptr && store_->saveDue(now)) saveStandaloneStateNow();
  for (auto& s : pendingUi_) out.push_back(std::move(s));
  pendingUi_.clear();
  if (uiReady_ && envPending_) {
    out.push_back(envJson());
    envPending_ = false;
  }
  if (uiReady_ && uiSink_ && !out.empty()) uiSink_(out);
}

void VJN6AudioProcessor::handleEvent(const vjn6::Event& e) {
  if (eventObserver) eventObserver(e);
  switch (e.type) {
    case vjn6::EventType::FieldChanged:
      onParameterEvent(hostIndexForField(e.a), e.i, static_cast<vjn6::Origin>(e.b));
      break;
    case vjn6::EventType::PerfChanged: {
      const int idx = hostIndexForPerf(e.a);
      if (idx >= 0) onParameterEvent(idx, e.i, static_cast<vjn6::Origin>(e.b));
      else stateChanged_ = true;  // volume, MIDI channel/omni/function, protect: state only
      break;
    }
    case vjn6::EventType::SettingChanged:
      stateChanged_ = true;
      if (e.a == vjn6::S_MODE) {
        mode_.store(e.i == 1 ? 1 : 0);
        paramInfoChanged_ = true;  // p_vca_level shows a different string in each mode
      }
      if (e.a == vjn6::S_ZOOM && onZoomChanged) onZoomChanged(e.i);
      break;
    case vjn6::EventType::SlotChanged:
    case vjn6::EventType::BankChanged:
      namesDirty_ = programChanged_ = stateChanged_ = true;
      break;
    case vjn6::EventType::ProgramChanged:
      currentProgram_.store(e.i - 1);
      programChanged_ = true;
      break;
    case vjn6::EventType::MidiOut: {
      MidiMsg m;
      m.len = e.len <= 24 ? e.len : 24;
      std::memcpy(m.data, e.data, m.len);
      midiOutFifo_.push(m);
      if (services_ != nullptr) services_->sendMidiOut(m.data, m.len);
      break;
    }
    case vjn6::EventType::StateDirty:
      if (store_ != nullptr) store_->markDirty(nowMs());
      break;
    case vjn6::EventType::Latency:
      setLatencySamples(e.i);
      break;
    case vjn6::EventType::Notice:    // turned into UI notices by the bridge
    case vjn6::EventType::Overflow:
    case vjn6::EventType::None:
      break;
  }
}

void VJN6AudioProcessor::onParameterEvent(int index, int value, vjn6::Origin origin) {
  if (index < 0 || index >= kNumHostParams) return;
  const auto k = static_cast<size_t>(index);
  bool report = true;
  if (origin == vjn6::Origin::Host) {
    // Host automation is already known to the host; report it only when the plugin has since
    // overwritten the host's last write with a report of its own. Changes caused by a host
    // program selection (Origin::HostProgram) are always reported, so the host's values match.
    report = lastWriterIsPlugin_[k].load(std::memory_order_relaxed);
  }
  if (report && access_[k]->nativeValue() != value) reportToHost(index, value);
}

// ------------------------------------------------------------------------------------------------
// UI messages

void VJN6AudioProcessor::setUiSink(UiSink sink) {
  uiSink_ = std::move(sink);
  uiReady_ = false;
}

void VJN6AudioProcessor::clearUiSink() {
  uiSink_ = nullptr;
  uiReady_ = false;
}

void VJN6AudioProcessor::endAllGestures() {
  for (int i = 0; i < kNumHostParams; ++i) {
    const auto k = static_cast<size_t>(i);
    gestureEndDue_[k] = 0.0;
    if (gestureActive_[k]) {
      gestureActive_[k] = false;
      access_[k]->parameter().endChangeGesture();
    }
  }
}

void VJN6AudioProcessor::releaseUiInput() {
  // F-13: closing the editor releases keyboard notes, LFO TRIG, push and the lever.
  vjn6::Command c;
  c.type = vjn6::CommandType::ReleaseAll;
  c.src = vjn6::Source::System;
  postCommand(c);
}

void VJN6AudioProcessor::editorClosed() {
  clearUiSink();
  onZoomChanged = nullptr;
  endAllGestures();
  releaseUiInput();
}

void VJN6AudioProcessor::uiPageUnloaded() {
  // the page (hidden editor, reload) can no longer release what it holds
  if (uiReady_) {
    endAllGestures();
    releaseUiInput();
  }
  uiReady_ = false;
}

void VJN6AudioProcessor::sendToUi(std::string msg) {
  if (uiReady_ && uiSink_) uiSink_({std::move(msg)});
}

int VJN6AudioProcessor::zoomSetting() {
  auto snap = std::make_unique<vjn6::Snapshot>();
  inst_->snapshot(*snap);
  return snap->settings.v[vjn6::S_ZOOM];
}

void VJN6AudioProcessor::setStandaloneServices(StandaloneServices* s) {
  services_ = s;
  envPending_ = uiReady_;
}

std::string VJN6AudioProcessor::envJson() {
  auto* env = new juce::DynamicObject();
  env->setProperty("t", "env");
  env->setProperty("build", isStandalone() ? "standalone" : "plugin");
  juce::Array<juce::var> outputs;
  juce::String selected;
  bool outAvailable = !isStandalone();  // plugin: the host's MIDI output
  if (services_ != nullptr) {
    outputs = services_->midiOutputs();
    selected = services_->selectedMidiOutput();
    outAvailable = services_->midiOutputOpen();
  }
  env->setProperty("midiOutAvailable", outAvailable);
  env->setProperty("sysexPermitted", true);
  env->setProperty("midiAvailable", true);
  env->setProperty("inputs", juce::Array<juce::var>());
  env->setProperty("outputs", outputs);
  env->setProperty("output", selected);
  env->setProperty("saveError", saveError_.isEmpty() ? juce::String() : "Could not save state: " + saveError_);
  return juce::JSON::toString(juce::var(env), juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::none))
      .toStdString();
}

void VJN6AudioProcessor::handleGesture(const juce::var& msg) {
  int idx = -1;
  if (msg.hasProperty("f")) idx = hostIndexForField(static_cast<int>(msg["f"]));
  else if (msg.hasProperty("p")) idx = hostIndexForPerf(static_cast<int>(msg["p"]));
  if (idx < 0) return;
  const auto k = static_cast<size_t>(idx);
  const bool begin = static_cast<bool>(msg["begin"]);
  if (begin) {
    gestureEndDue_[k] = 0.0;
    if (!gestureActive_[k]) {
      gestureActive_[k] = true;
      access_[k]->parameter().beginChangeGesture();
    }
  } else if (gestureActive_[k]) {
    gestureEndDue_[k] = nowMs() + kGestureEndDelayMs;
  }
}

void VJN6AudioProcessor::handleUiMessage(const juce::String& json) {
  const juce::var msg = juce::JSON::parse(json);
  const juce::String t = msg.getProperty("t", juce::var()).toString();
  if (t == "openFile") {
    openFileDialog();
    return;
  }
  if (t == "export") {
    exportDialog(msg.getProperty("what", juce::var()).toString());
    return;
  }
  if (t == "openDevices") {
    if (services_ != nullptr) services_->openDeviceDialog();
    return;
  }
  if (t == "midiOutput") {
    if (services_ != nullptr) {
      const auto choice = msg.hasProperty("id") ? msg["id"].toString() : msg.getProperty("name", juce::var()).toString();
      services_->chooseMidiOutput(choice);
      envPending_ = uiReady_;
    }
    return;
  }
  if (t == "gesture") {
    handleGesture(msg);
    return;
  }
  if (t == "zoom") {
    const auto v = msg.hasProperty("v") ? msg["v"] : msg.getProperty("zoom", juce::var());
    if (!v.isVoid()) postCommand(vjn6::Command::setSetting(vjn6::S_ZOOM, static_cast<int>(v)));
    return;
  }
  std::vector<std::string> out;
  {
    std::lock_guard<std::mutex> lock(producerMutex_);
    bridge_->handleUiMessage(json.toStdString(), out);
  }
  if (t == "ready") {
    // R-S3: the next poll sends the complete state; the host description follows it.
    uiReady_ = true;
    envPending_ = true;
  }
  for (auto& s : out) sendToUi(std::move(s));
}

void VJN6AudioProcessor::openFileDialog() {
  const auto start = lastDirectory_.isDirectory() ? lastDirectory_
                                                  : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
  chooser_ = std::make_unique<juce::FileChooser>("Load file", start, "*.vjn6;*.vjn6bank;*.syx", true);
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                        [this](const juce::FileChooser& fc) {
                          const auto f = fc.getResult();
                          if (f == juce::File()) return;  // cancel changes nothing
                          lastDirectory_ = f.getParentDirectory();
                          loadFileFromDisk(f);
                        });
}

void VJN6AudioProcessor::loadFileFromDisk(const juce::File& f) {
  std::vector<std::string> out;
  const std::string name = f.getFileName().toStdString();
  juce::FileInputStream in(f);
  bool ok = in.openedOk();
  std::vector<uint8_t> data;
  if (ok) {
    const auto total = in.getTotalLength();
    std::size_t size = total > 0 ? static_cast<std::size_t>(total) : 0;
    if (size > kMaxFileRead) size = kMaxFileRead;
    data.resize(size);
    ok = size == 0 || in.read(data.data(), static_cast<int>(size)) == static_cast<int>(size);
  }
  {
    std::lock_guard<std::mutex> lock(producerMutex_);
    if (ok) bridge_->loadFile(name, data.data(), data.size(), out);
    else bridge_->fileUnreadable(name, out);
  }
  for (auto& s : out) sendToUi(std::move(s));
}

void VJN6AudioProcessor::exportDialog(const juce::String& what) {
  std::string name;
  std::vector<uint8_t> bytes;
  if (!bridge_->exportData(what.toStdString(), name, bytes)) return;
  const auto start = lastDirectory_.isDirectory() ? lastDirectory_
                                                  : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
  const juce::String fileName(juce::CharPointer_UTF8(name.c_str()));
  chooser_ = std::make_unique<juce::FileChooser>("Export", start.getChildFile(fileName),
                                                 "*" + fileName.fromLastOccurrenceOf(".", true, false), true);
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, data = std::move(bytes)](const juce::FileChooser& fc) {
                          const auto f = fc.getResult();
                          if (f == juce::File()) return;
                          lastDirectory_ = f.getParentDirectory();
                          if (!f.replaceWithData(data.data(), data.size()))
                            sendToUi(vjn6::Bridge::notice("writeFailed", f.getFullPathName().toStdString()));
                        });
}

// ------------------------------------------------------------------------------------------------
// standalone state files (F-13)

juce::String VJN6AudioProcessor::saveStandaloneStateNow() {
  if (store_ == nullptr) return {};
  auto snap = std::make_unique<vjn6::Snapshot>();
  inst_->snapshot(*snap);
  auto st = stateFromSnapshot(*snap);
  vjn6::StateBlob blob;
  blob.bytes = vjn6::encodeState(*st);
  const juce::String err = store_->save(blob, vjn6::files::bankJson(snap->bank));
  store_->markSaved();
  if (err != saveError_) {
    saveError_ = err;
    pendingUi_.push_back(vjn6::Bridge::status(err.isEmpty() ? std::string() : ("Could not save state: " + err).toStdString()));
    envPending_ = uiReady_;
  }
  return err;
}

juce::AudioProcessorEditor* VJN6AudioProcessor::createEditor() { return new VJN6Editor(*this); }

}  // namespace vjn6plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vjn6plugin::VJN6AudioProcessor(); }
