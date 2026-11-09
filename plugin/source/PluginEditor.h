// The editor: the Svelte UI in a WebBrowserComponent filling the window (§5, §9.4), or a plain
// label when no WebView backend is available (§5.7). Fixed 2.5:1 aspect, 700x280 to 2800x1120 (§5.1),
// zoom per §4.4.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace vjn6plugin {

class VJN6AudioProcessor;

class VJN6Editor final : public juce::AudioProcessorEditor {
 public:
  explicit VJN6Editor(VJN6AudioProcessor& p);
  ~VJN6Editor() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

  // §4.4 Fit: 100 % if that fits the screen, else the largest 10 % step that fits (min 50 %).
  static juce::Point<int> fitSize(juce::Rectangle<int> screenArea);
  static juce::Point<int> zoomSize(int percent);

  static constexpr int kBaseWidth = 1400, kBaseHeight = 560;
  static constexpr int kMinWidth = 700, kMinHeight = 280, kMaxWidth = 2800, kMaxHeight = 1120;

 private:
  class Browser;

  juce::Point<int> initialSize();
  void applyZoom(int zoom);
  void setSizeProgrammatically(juce::Point<int> s);

  VJN6AudioProcessor& proc_;
  std::unique_ptr<Browser> browser_;
  std::unique_ptr<juce::Label> fallbackLabel_;
  bool programmatic_ = false, constructing_ = false;
  juce::Point<int> requested_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VJN6Editor)
};

}  // namespace vjn6plugin
