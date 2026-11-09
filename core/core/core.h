// Core: owns all synth and panel state (glossary "Core", §9.4). Every method except the queue
// producer/consumer ends runs on the audio thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "engine/engine.h"
#include "files/bulk.h"
#include "midi/parser.h"
#include "state/state.h"
#include "util/lockfree.h"
#include "vjn6/instrument.h"

namespace vjn6 {

class Core {
 public:
  Core(double fs, int maxBlock);
  void prepare(double fs, int maxBlock);

  // producer / consumer ends
  bool post(const Command& c) { return cmdQ_.push(c); }
  bool pollEvent(Event& e) { return evQ_.pop(e); }
  void snapshot(Snapshot& out) const;
  int acquireBulk();
  Bulk* bulk(int i) { return (i >= 0 && i < kBulkCount) ? bulks_[i].get() : nullptr; }
  void setKeyboardPlayDefault(bool on);

  // audio thread
  void midi(const uint8_t* bytes, int len, int sampleOffset);
  void hostParam(int index, int value, int sampleOffset);
  void render(float* L, float* R, int n);
  int latencySamples() const { return latency_.load(std::memory_order_relaxed); }
  double sampleRate() const { return fs_; }

  // test / bridge access (audio thread or the test thread)
  Engine& engine() { return engine_; }
  CoreState& state() { return st_; }
  const CoreState& state() const { return st_; }
  LedState led() const;
  uint64_t buttonLeds(uint64_t* blink = nullptr) const;
  bool writeArmed() const { return writeArmed_; }
  bool ktArmed() const { return ktArmed_; }
  float lever() const { return static_cast<float>(lever_); }
  void applyNow(const Command& c);   // test helper: apply a command at the current position
  int64_t position() const { return engine_.now(); }
  uint32_t droppedEvents() const { return dropped_; }

  // slot display helpers (§2, §5.3)
  static int bankOf(int slot, Mode m);    // 1..8 (106) or 1..7 (60); 0 = none
  static int groupOf(int slot) { return (slot - 1) / 64; }
  static int patchOf(int slot) { return (slot - 1) % 8 + 1; }

 private:
  static constexpr int kBulkCount = 2;
  static constexpr int kPendingCap = 8192;
  static constexpr int kMaxPorts = 16;
  static constexpr uint32_t kSrcKeyboard = Engine::kSrcKeyboard;

  struct Pending {
    int64_t pos;
    uint32_t seq;
    bool isMidi;
    uint8_t port;
    uint8_t len;
    Command cmd;     // when !isMidi
    uint8_t msg[24]; // complete MIDI message when isMidi
  };

  Mode mode() const { return st_.settings.v[S_MODE] == 1 ? Mode::J106 : Mode::J60; }
  int midiFunction() const { return st_.perf.v[P_MIDI_FUNCTION]; }
  bool protect() const { return st_.perf.v[P_PROTECT] != 0; }

  void enqueuePending(const Pending& p);
  void applyPendingUpTo(int64_t pos);
  void handleCommand(const Command& c);
  void handleMidi(const uint8_t* m, int len, int port);
  void handleChannelMessage(const uint8_t* m, int len, int port);
  void handleSysex(const uint8_t* m, int len);
  void handleCC(int cc, int v, uint32_t source);

  // state changes
  bool setField(int f, int v, Origin o, bool transmit);
  bool setPerf(int p, int v, Origin o);
  bool setSetting(int s, int v, Origin o);
  void setEditPatch(const Patch& p, Origin o);
  void setEditName(const Name& n);
  void selectSlot(int slot, Origin o, bool fromPanelOrManager);
  void doWrite(int target);
  void doManual();
  void recomputePendingBank();
  void showMessage(LedMessage m);
  void notice(NoticeKind k, int i = 0, int j = 0);
  void emit(const Event& e);
  void markDirty() { dirty_ = true; stateDirty_ = true; }
  void cancelArming(bool write, bool kt);
  void slotChanged(int slot);
  void resetToFirstRun(bool withMessage);
  void applyBulk(Bulk& b);
  void applyParamMessage(int pp, int vv, Origin o);
  bool externalFieldWrite(int f, int v, Origin o);

  // notes
  void noteOnFrom(uint32_t source, int note, int vel, bool fromKeyboard);
  void noteOffFrom(uint32_t source, int note, bool fromKeyboard);
  void updatePedal();
  void updatePush();
  void updateLfoTrig();
  void releaseKeyboard();

  // transmit (R-M3)
  void tx(const uint8_t* bytes, int len);
  void txRateLimited(int channel, const uint8_t* bytes, int len);
  void flushRateLimited(bool force);
  void txParamForField(int f);
  void txDump(uint8_t type, int pp, const Patch& p);
  bool canTx() const { return mode() == Mode::J106; }

  void publishSnapshot();
  void fillSnapshot(Snapshot& s) const;

  double fs_;
  int maxBlock_;
  Engine engine_;
  CoreState st_;
  bool kbdPlayDefault_ = true;

  // transient panel state (not saved)
  bool writeArmed_ = false;
  bool ktArmed_ = false;
  LedMessage msg_ = LedMessage::None;
  int64_t msgEnd_ = 0;
  double lever_ = 0.0;
  bool leverDragging_ = false;
  bool pushPanel_ = false, pushKey_ = false, pushMidi_ = false;
  bool trigPanel_ = false, trigKey_ = false, trigMidi_ = false;
  bool pedalMidi_ = false, pedalKey_ = false;
  bool localPushSent_ = false;
  bool kbdNotes_[128] = {};

  // rate-limited transmit channels: 0..0x11 SysEx params, 18 = pitch bend
  static constexpr int kTxChannels = 19;
  int64_t txLast_[kTxChannels];
  bool txPending_[kTxChannels] = {};
  uint8_t txBytes_[kTxChannels][8] = {};
  uint8_t txLen_[kTxChannels] = {};

  // queues
  SpscQueue<Command> cmdQ_{4096};
  SpscQueue<Event> evQ_{4096};
  uint32_t dropped_ = 0;
  std::unique_ptr<Pending[]> pending_;
  int pendHead_ = 0, pendCount_ = 0;
  uint32_t seq_ = 0;
  MidiParser parsers_[kMaxPorts];
  std::unique_ptr<Bulk> bulks_[kBulkCount];

  // snapshot
  mutable TripleBuffer<Snapshot> snap_;
  mutable std::mutex readMutex_;
  mutable Snapshot readCopy_;
  bool dirty_ = true;
  bool stateDirty_ = false;
  uint32_t snapSeq_ = 0;
  uint32_t bankVersion_ = 0;
  std::atomic<int> latency_{0};
};

}  // namespace vjn6
