// UI bridge: the JSON protocol between the Svelte UI and the core, shared by the plugin/standalone
// (WebBrowserComponent native function + event) and the web build (WASM main thread).
// Protocol reference: docs/BRIDGE.md. Runs on one non-audio thread (the consumer of pollEvent).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "vjn6/instrument.h"

namespace vjn6 {

class Bridge {
 public:
  explicit Bridge(Instrument& inst);

  // One message from the UI. Returns false if its "t" is not a core message (the host handles it:
  // e.g. "openFile", "openDevices", "midiOutput", "gesture" on desktop). Replies go to out.
  bool handleUiMessage(const std::string& json, std::vector<std::string>& out);

  // Parses a file and posts it to the core; on failure shows Er and appends the notice.
  void loadFile(const std::string& fileName, const uint8_t* data, std::size_t len, std::vector<std::string>& out);
  void fileUnreadable(const std::string& fileName, std::vector<std::string>& out);

  // Export from the latest snapshot. what: "bankJson", "bankSyx", "patchJson", "patchSyx".
  bool exportData(const std::string& what, std::string& fileName, std::vector<uint8_t>& bytes);

  // Drains core events (calling onEvent for each) and appends UI messages: "state" when the
  // snapshot changed (with the bank when it changed or after "ready"), "notice", and "midiOut" if
  // forwardMidiOut is set. Call at up to 60 Hz.
  void poll(std::vector<std::string>& out);
  void requestFullState() { full_ = true; }

  std::function<void(const Event&)> onEvent;
  bool forwardMidiOut = false;

  // helpers
  static std::string base64(const uint8_t* data, std::size_t len);
  static bool unbase64(const std::string& s, std::vector<uint8_t>& out);
  static std::string notice(const std::string& kind, const std::string& file, int i = 0, int j = 0);
  static std::string status(const std::string& text);
  static std::string snapshotJson(const Snapshot& s, bool withBank);
  // Command from a UI JSON message (no file or export handling); false if not a core command.
  static bool commandFromJson(const std::string& json, Command& c);

 private:
  Instrument& inst_;
  bool full_ = true;
  uint32_t lastSeq_ = 0xFFFFFFFFu;
  uint32_t lastBank_ = 0xFFFFFFFFu;
  std::string lastFile_;
  std::vector<uint8_t> scratch_;
};

}  // namespace vjn6
