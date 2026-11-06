// MIDI byte-stream parser (R-M2): running status, SysEx reassembled across calls up to 24 bytes
// (longer messages discarded), realtime bytes inside SysEx delivered at once and not stored.
#pragma once

#include <cstdint>

namespace vjn6 {

class MidiParser {
 public:
  void reset() {
    status_ = 0;
    have_ = 0;
    inSysex_ = false;
    overflow_ = false;
    sxLen_ = 0;
  }

  // sink(const uint8_t* msg, int len) is called for every complete message.
  template <typename Sink>
  void feed(const uint8_t* b, int n, Sink&& sink) {
    for (int i = 0; i < n; ++i) {
      const uint8_t x = b[i];
      if (x >= 0xF8) {  // realtime
        sink(&x, 1);
        continue;
      }
      if (x == 0xF0) {
        inSysex_ = true;
        overflow_ = false;
        sx_[0] = x;
        sxLen_ = 1;
        status_ = 0;
        continue;
      }
      if (x == 0xF7) {
        if (inSysex_) {
          if (!overflow_ && sxLen_ < kMax) {
            sx_[sxLen_++] = x;
            sink(sx_, sxLen_);
          }
          inSysex_ = false;
          sxLen_ = 0;
        }
        continue;
      }
      if (x & 0x80) {
        // any other status byte ends a SysEx (unterminated: discarded)
        inSysex_ = false;
        sxLen_ = 0;
        if (x >= 0xF1) {  // system common: cancels running status, data ignored
          status_ = 0;
          have_ = 0;
          skip_ = (x == 0xF2) ? 2 : (x == 0xF1 || x == 0xF3) ? 1 : 0;
          continue;
        }
        status_ = x;
        have_ = 0;
        skip_ = 0;
        continue;
      }
      // data byte
      if (inSysex_) {
        if (sxLen_ < kMax - 1) {
          sx_[sxLen_++] = x;
        } else {
          overflow_ = true;
        }
        continue;
      }
      if (skip_ > 0) {
        --skip_;
        continue;
      }
      if (status_ == 0) continue;
      data_[have_++] = x;
      const int need = needed(status_);
      if (have_ >= need) {
        uint8_t m[3] = {status_, data_[0], data_[1]};
        sink(m, 1 + need);
        have_ = 0;  // running status kept
      }
    }
  }

 private:
  static constexpr int kMax = 24;
  static int needed(uint8_t s) {
    const uint8_t t = s & 0xF0;
    return (t == 0xC0 || t == 0xD0) ? 1 : 2;
  }
  uint8_t status_ = 0;
  uint8_t data_[2] = {};
  int have_ = 0;
  int skip_ = 0;
  bool inSysex_ = false;
  bool overflow_ = false;
  uint8_t sx_[kMax] = {};
  int sxLen_ = 0;
};

}  // namespace vjn6
