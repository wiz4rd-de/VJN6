// Standalone state files (R-D2, F-1, F-13): state.bin, state.bad and bank.vjn6bank in the per-user
// VJN6 directory. Pure file logic plus the autosave timing, so tests can drive it headlessly.
#pragma once

#include <juce_core/juce_core.h>

#include <string>

#include "vjn6/instrument.h"

namespace vjn6plugin {

class StateStore {
 public:
  explicit StateStore(juce::File directory);

  // $VJN6_STATE_DIR if set, else ~/Library/Application Support/VJN6 (macOS), %APPDATA%\VJN6
  // (Windows), ~/.config/VJN6 (Linux).
  static juce::File defaultDirectory();

  const juce::File& directory() const { return dir_; }
  juce::File stateFile() const { return dir_.getChildFile("state.bin"); }
  juce::File badFile() const { return dir_.getChildFile("state.bad"); }
  juce::File bankFile() const { return dir_.getChildFile("bank.vjn6bank"); }

  enum class Startup {
    FirstRun,  // no state.bin (or an empty one): first-run state, no message
    Loaded,    // a valid blob is in `blob`
    Reset,     // unreadable or invalid: renamed to state.bad; `blob` makes the core reset with rE
  };
  Startup readStartupState(vjn6::StateBlob& blob);

  // Writes state.bin through a temporary file renamed over it, and bank.vjn6bank beside it.
  // Returns an empty string on success, else the reason (the previous files stay intact).
  juce::String save(const vjn6::StateBlob& state, const std::string& bankJson);

  // Autosave timing (F-13): 1 s after the last change, at least every 5 s while changes continue.
  void markDirty(double nowMs);
  bool isDirty() const { return dirty_; }
  bool saveDue(double nowMs) const;
  void markSaved() { dirty_ = false; }

 private:
  juce::String writeReplacing(const juce::File& target, const void* data, size_t size);

  juce::File dir_;
  juce::String tmpSuffix_;
  bool dirty_ = false;
  double firstDirtyMs_ = 0.0, lastDirtyMs_ = 0.0;
};

}  // namespace vjn6plugin
