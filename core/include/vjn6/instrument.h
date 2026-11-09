// VJN6 core public API (SPEC §9.4). Signatures of Instrument are binding; Command, Event, StateBlob
// and Snapshot contents are implementation latitude (§14) and documented here.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vjn6/types.h"

namespace vjn6 {

struct Settings {
  Mode mode = Mode::J60;
  Noise chorusNoise = Noise::Original;
  int variationPct = 50;
  bool velocityToVca = false;
  bool mono = false;
  int oversample = 2;
};

// Where a command comes from. Only Panel counts as "panel input" (glossary).
enum class Source : uint8_t {
  Panel = 0,     // front-panel control or the shortcuts Alt+M, Alt+W, Alt+1..8
  Overlay,       // bank manager or Settings
  Keyboard,      // computer keyboard map (§7.6)
  Host,          // plugin host (programs)
  System,        // bridge-internal (editor close, state load, file errors)
};

enum class Button : uint8_t {
  Range = 0,      // b = 0 16', 1 8', 2 4'
  Pulse, Saw, Sub,
  ChorusOff,      // 106 radio
  ChorusI, ChorusII,  // 60 toggles, 106 radio
  Portamento, Hold, ArpOn,
  Bank,           // b = 1..8
  Group,          // b = 0 A, 1 B
  PatchBtn,       // b = 1..8
  Manual, Write,
  Poly1, Poly2,
  KeyTranspose,
  LfoTrig,        // momentary: Press / Release
};

enum class CommandType : uint8_t {
  None = 0,
  SetField,       // a = field, i = value
  SetPerf,        // a = perf, i = value
  SetSetting,     // a = setting, i = value
  Press,          // a = Button, b = index, flags bit0 = shift (combination press)
  Release,        // a = Button (LfoTrig)
  Lever,          // f = -1..1, flags bit0 = drag active (mouse), bit1 = push held, bit2 = push valid
  Push,           // b = 1 held / 0 released (keyboard key 3: lever push in 106, LFO TRIG in 60)
  Note,           // a = note, b = velocity (0 = off)  (computer keyboard)
  Pedal,          // b = 1 down / 0 up  (Space)
  MidiIn,         // port, len, data (Web MIDI / standalone extra ports)
  SelectSlot,     // i = slot 1..128 (Overlay = bank-manager double-click, Host = host program)
  Rename,         // i = slot 1..128, data = name bytes (normalised by the core again)
  CopySlot,       // i = from slot, b2 = to slot (row drop)
  SlotToEdit,     // i = slot (row drop onto the edit-buffer target)
  SendPatch,      // bank manager Send patch
  LoadBulk,       // i = bulk pool index (parsed file)
  LoadState,      // i = bulk pool index (state blob)
  ShowMessage,    // a = LedMessage
  ReleaseAll,     // editor close / window blur: keyboard notes, LFO TRIG, push, lever to centre
  Gesture,        // b = 1 begin / 0 end of a panel drag on field a (rate-limited transmit flush)
  MidiPortGone,   // port: a MIDI input disappeared or was disabled (acts as CC123 for its notes, R-M1)
};

enum class LedMessage : uint8_t { None = 0, Protected, Format, Received, WriteOk, Reset };

struct Command {
  CommandType type = CommandType::None;
  Source src = Source::Panel;
  uint8_t a = 0, b = 0, flags = 0, len = 0, port = 0, b2 = 0;
  int32_t i = 0;
  float f = 0.0f;
  uint8_t data[24] = {};

  static Command setField(int field, int value, Source s = Source::Panel) {
    Command c; c.type = CommandType::SetField; c.a = uint8_t(field); c.i = value; c.src = s; return c;
  }
  static Command setPerf(int perf, int value, Source s = Source::Panel) {
    Command c; c.type = CommandType::SetPerf; c.a = uint8_t(perf); c.i = value; c.src = s; return c;
  }
  static Command setSetting(int id, int value, Source s = Source::Overlay) {
    Command c; c.type = CommandType::SetSetting; c.a = uint8_t(id); c.i = value; c.src = s; return c;
  }
  static Command press(Button bt, int index = 0, bool shift = false, Source s = Source::Panel) {
    Command c; c.type = CommandType::Press; c.a = uint8_t(bt); c.b = uint8_t(index);
    c.flags = shift ? 1 : 0; c.src = s; return c;
  }
  static Command release(Button bt, Source s = Source::Panel) {
    Command c; c.type = CommandType::Release; c.a = uint8_t(bt); c.src = s; return c;
  }
  static Command note(int n, int vel, Source s = Source::Keyboard) {
    Command c; c.type = CommandType::Note; c.a = uint8_t(n); c.b = uint8_t(vel); c.src = s; return c;
  }
  static Command selectSlot(int slot, Source s) {
    Command c; c.type = CommandType::SelectSlot; c.i = slot; c.src = s; return c;
  }
};

enum class EventType : uint8_t {
  None = 0,
  FieldChanged,   // a = field, i = value, b = Origin
  PerfChanged,    // a = perf, i = value, b = Origin
  SettingChanged, // a = setting, i = value, b = Origin
  SlotChanged,    // i = slot 1..128 (patch or name changed)
  BankChanged,    // all slots replaced
  ProgramChanged, // i = current slot 1..128 (host program = slot - 1)
  MidiOut,        // len, data
  Notice,         // a = NoticeKind, i, j counts
  StateDirty,     // saved state changed (autosave timing)
  Latency,        // i = latency in host samples
  Overflow,       // i = number of dropped events so far
};

// Origin of a change reported in FieldChanged/PerfChanged (R-S1, R-S3).
enum class Origin : uint8_t { Panel = 0, Overlay, Keyboard, Host, Midi, Sysex, Load, System, HostProgram };

enum class NoticeKind : uint8_t {
  Loaded = 0,        // a patch file was loaded into the edit buffer
  BankLoaded,        // a .vjn6bank replaced the bank
  Imported,          // i written, j dropped
  Protected,         // "Memory is protected."
  Malformed,         // "Not a VJN6 or Juno-106 file"
  Unreadable,        // "Could not read"
  Renamed,           // i = slot
};

struct Event {
  EventType type = EventType::None;
  uint8_t a = 0, b = 0, len = 0;
  int32_t i = 0, j = 0;
  uint8_t data[24] = {};
};

struct StateBlob {
  std::vector<uint8_t> bytes;
};

// LED display state (§5.3). Characters: '0'..'9', 'P','r','E','o','k','c','t','-',' ' (blank).
struct LedState {
  char text[3] = {' ', ' ', 0};
  bool dp = false;          // both decimal points (EDITED)
  bool blink = false;       // digits blink (WRITE armed)
  LedMessage message = LedMessage::None;
};

// Button LEDs (§5.2), one bit each in Snapshot::buttonLeds / buttonBlink.
enum ButtonLed : int {
  L_RANGE16 = 0, L_RANGE8, L_RANGE4, L_PULSE, L_SAW, L_SUB, L_CHORUS_OFF, L_CHORUS_I, L_CHORUS_II,
  L_PORTA, L_HOLD, L_ARP, L_POLY1, L_POLY2, L_GROUP_A, L_GROUP_B,
  L_BANK1, L_BANK8 = L_BANK1 + 7, L_PATCH1, L_PATCH8 = L_PATCH1 + 7,
  L_MANUAL, L_WRITE, L_KEY_TRANSPOSE, L_LFO_TRIG, L_COUNT
};

// Everything the UI shows and everything the state blob holds, published lock-free by the audio thread.
struct Snapshot {
  uint32_t seq = 0;
  Patch edit;
  Name editName;
  Patch stored;
  bool edited = false;
  bool manual = false;
  int16_t currentSlot = 1;
  int8_t pendingBank = 1;   // 1..8
  int8_t pendingGroup = 0;  // 0 A, 1 B
  PerfSettings perf;
  SettingsValues settings;
  LedState led;
  uint64_t buttonLeds = 0;
  uint64_t buttonBlink = 0;
  bool writeArmed = false;
  bool ktArmed = false;
  bool lfoTrigHeld = false;
  bool push = false;
  float lever = 0.0f;
  int32_t latency = 0;
  uint32_t bankVersion = 0;
  uint32_t droppedEvents = 0;
  Slot bank[kNumSlots];
};

class Engine;
class Core;

class Instrument {
 public:
  explicit Instrument(double hostSampleRate, int maxBlock);  // allocates everything here
  ~Instrument();
  Instrument(const Instrument&) = delete;
  Instrument& operator=(const Instrument&) = delete;

  void prepare(double hostSampleRate, int maxBlock);          // may allocate; not on the audio thread
  bool post(const Command&);                                  // one producer thread
  bool pollEvent(Event&);                                     // one consumer thread
  void midi(const uint8_t* bytes, int len, int sampleOffset); // audio thread, incl. SysEx
  void render(float* L, float* R, int n);                     // audio thread, no alloc/lock/log
  int latencySamples() const;
  void saveState(StateBlob&) const;
  bool loadState(const StateBlob&);

  // ---- additions beyond §9.4 (implementation latitude) ----
  // Audio thread: a host parameter write (plugin), index 0..39 in §4.8 order, value in its native units.
  void hostParam(int index, int value, int sampleOffset);
  // Any non-audio thread (serialised by the caller): copy of the latest published snapshot.
  void snapshot(Snapshot& out) const;
  // Bulk payload pool for LoadBulk / LoadState commands (producer thread only). Returns -1 if busy.
  int acquireBulk();
  struct Bulk* bulk(int index);
  // Current sample rate / settings the instrument was prepared with.
  double sampleRate() const;

  Core& core();  // internal; used by tests and bridges in the same thread as render()

 private:
  std::unique_ptr<Core> core_;
};

}  // namespace vjn6
