#include "PluginEditor.h"

#include <cstdlib>

#include "PluginProcessor.h"
#include "UiResources.h"

namespace vjn6plugin {

namespace {
const char* const kNoWebViewText = "VJN6 needs Microsoft Edge WebView2 / WebKitGTK to show its panel.";

juce::WebBrowserComponent::Options browserOptions(VJN6AudioProcessor& proc) {
  using Options = juce::WebBrowserComponent::Options;
  return Options{}
#if JUCE_WINDOWS
      .withBackend(Options::Backend::webview2)
#endif
      .withWinWebView2Options(Options::WinWebView2{}.withUserDataFolder(
          juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("VJN6-WebView2")))
      .withNativeIntegrationEnabled()
      .withNativeFunction("vjn6Command",
                          [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
                            if (args.size() > 0)
                              proc.handleUiMessage(args[0].isString() ? args[0].toString() : juce::JSON::toString(args[0], true));
                            done(juce::var());
                          })
      .withResourceProvider([](const juce::String& url) { return uiResource(url); });
}

// §5.7: WebView2 on Windows (checked by JUCE), WebKitGTK on Linux (loaded at run time).
bool webViewAvailable(const juce::WebBrowserComponent::Options& options) {
  if (!juce::WebBrowserComponent::areOptionsSupported(options)) return false;
#if JUCE_LINUX || JUCE_BSD
  juce::DynamicLibrary webkit;
  return webkit.open("libwebkit2gtk-4.1.so.0") || webkit.open("libwebkit2gtk-4.0.so.37");
#else
  return true;
#endif
}
}  // namespace

// Stays on the embedded page; any (re)load of it disconnects the UI until it sends "ready" again.
class VJN6Editor::Browser final : public juce::WebBrowserComponent {
 public:
  Browser(const Options& o, VJN6AudioProcessor& p) : juce::WebBrowserComponent(o), proc_(p) {}

  bool pageAboutToLoad(const juce::String& url) override {
    const bool own = url.startsWith(getResourceProviderRoot());
    const bool blank = url == "about:blank";  // JUCE unloads the page while the editor is hidden
    if (own || blank) proc_.uiPageUnloaded();
    return own || blank;
  }
  void pageFinishedLoading(const juce::String&) override {
    if (isShowing()) grabKeyboardFocus();
  }
  void visibilityChanged() override {
    juce::WebBrowserComponent::visibilityChanged();
    if (!isVisible()) proc_.uiPageUnloaded();
  }
  void newWindowAttemptingToLoad(const juce::String&) override {}

 private:
  VJN6AudioProcessor& proc_;
};

VJN6Editor::VJN6Editor(VJN6AudioProcessor& p) : juce::AudioProcessorEditor(p), proc_(p) {
  const auto initial = initialSize();
  constructing_ = true;  // the resizes below are not the user's
  setResizable(true, false);
  setResizeLimits(kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
  getConstrainer()->setFixedAspectRatio(static_cast<double>(kBaseWidth) / kBaseHeight);

  proc_.onZoomChanged = [this](int zoom) { applyZoom(zoom); };
  const auto options = browserOptions(proc_);
  if (webViewAvailable(options)) {
    browser_ = std::make_unique<Browser>(options, proc_);
    addAndMakeVisible(*browser_);
    proc_.setUiSink([this](const std::vector<std::string>& msgs) {
      juce::Array<juce::var> batch;
      for (const auto& m : msgs) batch.add(juce::String(juce::CharPointer_UTF8(m.c_str())));
      browser_->emitEventIfBrowserIsVisible("vjn6Events", juce::var(batch));
    });
  } else {
    // §5.7: engine, MIDI, automation and state keep working without a panel.
    fallbackLabel_ = std::make_unique<juce::Label>("noWebView", kNoWebViewText);
    fallbackLabel_->setJustificationType(juce::Justification::centred);
    fallbackLabel_->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(*fallbackLabel_);
  }

  setSizeProgrammatically(initial);
  constructing_ = false;
  if (browser_ != nullptr) browser_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
}

VJN6Editor::~VJN6Editor() { proc_.editorClosed(); }

void VJN6Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff111111)); }

juce::Point<int> VJN6Editor::zoomSize(int percent) {
  return {kBaseWidth * percent / 100, kBaseHeight * percent / 100};
}

juce::Point<int> VJN6Editor::fitSize(juce::Rectangle<int> area) {
  // leave room for the window's title bar and frame
  const int w = area.getWidth() - 40, h = area.getHeight() - 80;
  for (int pct = 100; pct > 50; pct -= 10) {
    const auto s = zoomSize(pct);
    if (s.x <= w && s.y <= h) return s;
  }
  return zoomSize(50);
}

juce::Point<int> VJN6Editor::initialSize() {
  const auto last = proc_.lastEditorSize();
  if (last.x >= kMinWidth && last.y >= kMinHeight) return last;  // reopens at its last size
  const int zoom = proc_.zoomSetting();
  if (zoom >= 50) return zoomSize(zoom);
  juce::Rectangle<int> area(0, 0, kBaseWidth + 40, kBaseHeight + 80);
  if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userBounds.toNearestInt();
  return fitSize(area);
}

void VJN6Editor::setSizeProgrammatically(juce::Point<int> s) {
  requested_ = s;
  const juce::ScopedValueSetter<bool> guard(programmatic_, true);
  setSize(s.x, s.y);
}

void VJN6Editor::applyZoom(int zoom) {
  if (zoom >= 50) setSizeProgrammatically(zoomSize(zoom));  // a percentage sets the window size
}

void VJN6Editor::resized() {
  if (browser_ != nullptr) browser_->setBounds(getLocalBounds());
  if (fallbackLabel_ != nullptr) fallbackLabel_->setBounds(getLocalBounds().reduced(20));
  if (constructing_) return;
  proc_.setLastEditorSize({getWidth(), getHeight()});
  if (programmatic_) return;
  if (std::abs(getWidth() - requested_.x) <= 2 && std::abs(getHeight() - requested_.y) <= 2) return;
  requested_ = {};
  // §4.4: resizing the window sets the scale directly and selects Fit.
  if (proc_.zoomSetting() != 0) proc_.postCommand(vjn6::Command::setSetting(vjn6::S_ZOOM, 0));
}

}  // namespace vjn6plugin
