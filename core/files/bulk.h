// Bulk payloads handed from a producer thread to the audio thread (file loads, state loads).
// Preallocated once; the producer fills a free slot and posts LoadBulk / LoadState with its index;
// the audio thread applies it and marks it free again.
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "state/state.h"
#include "vjn6/types.h"

namespace vjn6 {

enum class BulkKind : uint8_t { None = 0, PatchToEdit, Bank, SyxImport, State };

struct SyxRecord {
  uint8_t bytes[24];
};
struct ParamRecord {
  uint8_t pp, vv;
};

struct Bulk {
  static constexpr std::size_t kMaxFile = 1u << 20;              // 1 MiB (§4.6)
  static constexpr std::size_t kMaxSyx = kMaxFile / 24 + 1;      // 30/31 messages in a file
  static constexpr std::size_t kMaxParams = kMaxFile / 7 + 1;    // 32 messages in a file

  std::atomic<int> busy{0};  // 0 free, 1 owned by producer or in flight
  BulkKind kind = BulkKind::None;
  // PatchToEdit
  Patch patch;
  Name name;
  // Bank
  Slot bank[kNumSlots];
  // SyxImport (and the F0 41 32 messages of any accepted file)
  std::vector<SyxRecord> msgs;
  std::vector<ParamRecord> params;
  // State
  CoreState state;
  int stateResult = 0;  // BlobResult

  Bulk() {
    msgs.reserve(kMaxSyx);
    params.reserve(kMaxParams);
  }
  void clear() {
    kind = BulkKind::None;
    msgs.clear();
    params.clear();
  }
};

}  // namespace vjn6
