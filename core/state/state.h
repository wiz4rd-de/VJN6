// Persistent state (SPEC §4.7, R-D1, R-D6) and the state blob codec (docs/STATE.md).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vjn6/types.h"

namespace vjn6 {

// Everything the state blob holds.
struct CoreState {
  Slot bank[kNumSlots];
  Patch edit;
  Name editName;
  Patch stored;
  bool edited = false;
  bool manual = false;
  int currentSlot = 1;
  int pendingBank = 1;
  int pendingGroup = 0;
  PerfSettings perf;
  SettingsValues settings;
};

// The VJN6 default bank (§15.3), generated from banks/default/*.json by tools/make_default_bank.py.
const Slot* defaultBank();

// R-D6 first-run state.
void firstRunState(CoreState& s, bool keyboardPlayDefault);

uint32_t crc32(const uint8_t* data, std::size_t len);

enum class BlobResult { Ok, OkNewer, Empty, Invalid };

std::vector<uint8_t> encodeState(const CoreState& s);
// Decodes into out. Ok: all fields; OkNewer: bank, buffers and performance settings only (out's
// settings untouched); Empty: zero length; Invalid: bad magic / CRC / truncation / version 0 / ranges.
BlobResult decodeState(const uint8_t* data, std::size_t len, CoreState& out);

constexpr uint16_t kStateVersion = 1;

}  // namespace vjn6
