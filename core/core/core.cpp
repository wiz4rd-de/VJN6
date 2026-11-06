#include "core/core.h"

#include <cmath>
#include <cstring>

#include "sysex/sysex.h"

namespace vjn6 {

namespace {
Origin originOf(Source s) {
  switch (s) {
    case Source::Panel: return Origin::Panel;
    case Source::Overlay: return Origin::Overlay;
    case Source::Keyboard: return Origin::Keyboard;
    case Source::Host: return Origin::Host;
    default: return Origin::System;
  }
}
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int three(int v) { return v <= 42 ? 0 : (v <= 85 ? 1 : 2); }  // 0-42 / 43-85 / 86-127
}  // namespace

Core::Core(double fs, int maxBlock) : fs_(fs), maxBlock_(maxBlock < 1 ? 1 : maxBlock) {
  pending_.reset(new Pending[kPendingCap]);
  for (auto& b : bulks_) b.reset(new Bulk());
  firstRunState(st_, kbdPlayDefault_);
  engine_.allocate(maxBlock_);
  prepare(fs, maxBlock_);
}

void Core::setKeyboardPlayDefault(bool on) {
  kbdPlayDefault_ = on;
  st_.settings.v[S_KBD_PLAY] = on ? 1 : 0;
  dirty_ = true;
  publishSnapshot();
}

void Core::prepare(double fs, int maxBlock) {
  fs_ = fs;
  if (maxBlock > maxBlock_) {
    maxBlock_ = maxBlock;
    engine_.allocate(maxBlock_);
  }
  engine_.prepare(fs, &st_.edit, &st_.perf, &st_.settings);
  engine_.holdChanged(st_.perf.v[P_HOLD] != 0);
  latency_.store(engine_.latencySamples(), std::memory_order_relaxed);
  pendHead_ = pendCount_ = 0;
  for (auto& p : parsers_) p.reset();
  writeArmed_ = ktArmed_ = false;
  if (msg_ != LedMessage::None) msgEnd_ = static_cast<int64_t>(2.0 * fs_);
  lever_ = 0.0;
  leverDragging_ = false;
  pushPanel_ = pushKey_ = pushMidi_ = false;
  trigPanel_ = trigKey_ = trigMidi_ = false;
  pedalMidi_ = pedalKey_ = false;
  std::memset(kbdNotes_, 0, sizeof kbdNotes_);
  for (int i = 0; i < kTxChannels; ++i) {
    txLast_[i] = INT64_MIN / 4;
    txPending_[i] = false;
  }
  dirty_ = true;
  publishSnapshot();
}

int Core::acquireBulk() {
  for (int i = 0; i < kBulkCount; ++i) {
    int expected = 0;
    if (bulks_[i]->busy.compare_exchange_strong(expected, 1)) {
      bulks_[i]->clear();
      return i;
    }
  }
  return -1;
}

void Core::snapshot(Snapshot& out) const {
  std::lock_guard<std::mutex> lock(readMutex_);
  snap_.update();
  out = snap_.front();
}

// ------------------------------------------------------------------------------------------------
// timeline

void Core::enqueuePending(const Pending& p) {
  if (pendCount_ >= kPendingCap) {
    ++dropped_;
    return;
  }
  // insert keeping (pos, seq) order; usually an append
  int i = pendCount_;
  while (i > 0) {
    const Pending& prev = pending_[(pendHead_ + i - 1) % kPendingCap];
    if (prev.pos <= p.pos) break;
    pending_[(pendHead_ + i) % kPendingCap] = prev;
    --i;
  }
  pending_[(pendHead_ + i) % kPendingCap] = p;
  ++pendCount_;
}

void Core::applyPendingUpTo(int64_t pos) {
  while (pendCount_ > 0) {
    Pending& p = pending_[pendHead_];
    if (p.pos > pos) break;
    const Pending cur = p;
    pendHead_ = (pendHead_ + 1) % kPendingCap;
    --pendCount_;
    if (cur.isMidi) {
      handleMidi(cur.msg, cur.len, cur.port);
    } else {
      handleCommand(cur.cmd);
    }
  }
}

void Core::midi(const uint8_t* bytes, int len, int sampleOffset) {
  if (!bytes || len <= 0) return;
  const int64_t pos = engine_.now() + (sampleOffset < 0 ? 0 : sampleOffset);
  parsers_[0].feed(bytes, len, [&](const uint8_t* m, int n) {
    Pending p;
    p.pos = pos;
    p.seq = seq_++;
    p.isMidi = true;
    p.port = 0;
    p.len = static_cast<uint8_t>(n);
    std::memcpy(p.msg, m, static_cast<std::size_t>(n));
    enqueuePending(p);
  });
}

void Core::hostParam(int index, int value, int sampleOffset) {
  Pending p;
  p.pos = engine_.now() + (sampleOffset < 0 ? 0 : sampleOffset);
  p.seq = seq_++;
  p.isMidi = false;
  if (index < kNumFields) {
    p.cmd = Command::setField(index, value, Source::Host);
  } else if (index < kNumFields + kNumHostPerf) {
    p.cmd = Command::setPerf(index - kNumFields, value, Source::Host);
  } else if (index == kNumFields + kNumHostPerf) {
    p.cmd = Command::setPerf(P_TUNE, value, Source::Host);
  } else {
    return;
  }
  enqueuePending(p);
}

void Core::applyNow(const Command& c) { handleCommand(c); }

void Core::render(float* L, float* R, int n) {
  if (n <= 0 || !L || !R) return;
  {
    Command c;
    const int64_t now = engine_.now();
    while (cmdQ_.pop(c)) {
      Pending p;
      p.pos = now;
      p.seq = seq_++;
      p.isMidi = false;
      p.cmd = c;
      enqueuePending(p);
    }
  }
  int done = 0;
  while (done < n) {
    const int chunk = (n - done) < maxBlock_ ? (n - done) : maxBlock_;
    const int64_t start = engine_.now();
    const int64_t end = start + chunk;
    while (engine_.now() < end) {
      const int64_t t = engine_.now();
      if (t == engine_.nextBoundary()) {
        applyPendingUpTo(t);
        flushRateLimited(false);
        if (msg_ != LedMessage::None && t >= msgEnd_) {
          msg_ = LedMessage::None;
          dirty_ = true;
        }
        engine_.controlStep();
      }
      if (engine_.nextArpEvent() == t) engine_.arpEvent();
      int64_t segEnd = end;
      if (engine_.nextBoundary() < segEnd) segEnd = engine_.nextBoundary();
      const int64_t arp = engine_.nextArpEvent();
      if (arp > t && arp < segEnd) segEnd = arp;
      const int off = done + static_cast<int>(t - start);
      engine_.renderSegment(L + off, R + off, static_cast<int>(segEnd - t));
    }
    done += chunk;
  }
  if (stateDirty_) {
    stateDirty_ = false;
    Event e;
    e.type = EventType::StateDirty;
    emit(e);
  }
  if (dirty_) publishSnapshot();
}

// ------------------------------------------------------------------------------------------------
// commands

void Core::cancelArming(bool write, bool kt) {
  if (write && writeArmed_) {
    writeArmed_ = false;
    dirty_ = true;
  }
  if (kt && ktArmed_) {
    ktArmed_ = false;
    dirty_ = true;
  }
}

void Core::handleCommand(const Command& c) {
  const bool panel = c.src == Source::Panel;
  const Origin o = originOf(c.src);
  if (panel && msg_ != LedMessage::None && c.type != CommandType::Release && c.type != CommandType::Gesture) {
    msg_ = LedMessage::None;  // any panel input dismisses a message, then acts (§5.4)
    dirty_ = true;
  }
  const bool is106 = mode() == Mode::J106;
  switch (c.type) {
    case CommandType::SetField: {
      if (c.a >= kNumFields) return;
      if (panel) cancelArming(true, true);
      setField(c.a, c.i, o, panel);
      return;
    }
    case CommandType::SetPerf: {
      if (c.a >= P_COUNT) return;
      if (panel) cancelArming(true, true);
      setPerf(c.a, c.i, o);
      return;
    }
    case CommandType::SetSetting: {
      if (c.a >= S_COUNT) return;
      if (panel) cancelArming(true, true);
      setSetting(c.a, c.i, o);
      return;
    }
    case CommandType::Press: {
      const Button bt = static_cast<Button>(c.a);
      const int idx = c.b;
      const bool shift = (c.flags & 1) != 0;
      const bool bankLike = bt == Button::Bank || bt == Button::Group || bt == Button::PatchBtn;
      if (panel) {
        if (writeArmed_ && bt != Button::Write && !bankLike) cancelArming(true, false);
        if (ktArmed_ && bt != Button::KeyTranspose) cancelArming(false, true);
      }
      const Patch& e = st_.edit;
      auto eff = [&](int f) { return sysex::eff(e, f); };
      switch (bt) {
        case Button::Range:
          if (idx <= 2) setField(W_RANGE, idx, o, panel);
          return;
        case Button::Pulse: setField(W_PULSE, eff(W_PULSE) ? 0 : 1, o, panel); return;
        case Button::Saw: setField(W_SAW, eff(W_SAW) ? 0 : 1, o, panel); return;
        case Button::Sub:
          if (!is106) setField(W_SUB, eff(W_SUB) ? 0 : 1, o, panel);
          return;
        case Button::ChorusOff:
          if (is106) setField(W_CHORUS, 0, o, panel);
          return;
        case Button::ChorusI:
          if (is106) setField(W_CHORUS, 1, o, panel);
          else setField(W_CHORUS, eff(W_CHORUS) ^ 1, o, panel);
          return;
        case Button::ChorusII:
          if (is106) setField(W_CHORUS, 2, o, panel);
          else setField(W_CHORUS, eff(W_CHORUS) ^ 2, o, panel);
          return;
        case Button::Portamento:
          if (is106) setPerf(P_PORTA_ON, st_.perf.v[P_PORTA_ON] ? 0 : 1, o);
          return;
        case Button::Hold:
          if (!is106) setPerf(P_HOLD, st_.perf.v[P_HOLD] ? 0 : 1, o);
          return;
        case Button::ArpOn:
          if (!is106) setPerf(P_ARP_ON, st_.perf.v[P_ARP_ON] ? 0 : 1, o);
          return;
        case Button::Bank: {
          if (is106) {
            if (idx >= 1 && idx <= 8 && st_.pendingBank != idx) {
              st_.pendingBank = idx;
              markDirty();
            }
            return;
          }
          if (idx < 1 || idx > 5) return;
          const int pb = st_.pendingBank;
          auto lit = [&](int b) {
            if (pb == 6) return b == 1 || b == 5;
            if (pb == 7) return b == 2 || b == 5;
            return b == pb;
          };
          int target = idx;
          if (shift) {
            // combination press (R-U4): BANK 1 <-> 5 (bank 6), BANK 2 <-> 5 (bank 7)
            if (idx == 5 && lit(1)) target = 6;
            else if (idx == 5 && lit(2)) target = 7;
            else if (idx == 1 && lit(5)) target = 6;
            else if (idx == 2 && lit(5)) target = 7;
          }
          if (st_.pendingBank != target) {
            st_.pendingBank = target;
            st_.pendingGroup = 0;
            markDirty();
          }
          return;
        }
        case Button::Group:
          if (is106 && idx <= 1 && st_.pendingGroup != idx) {
            st_.pendingGroup = idx;
            markDirty();
          }
          return;
        case Button::PatchBtn: {
          if (idx < 1 || idx > 8) return;
          const int target = is106 ? st_.pendingGroup * 64 + (st_.pendingBank - 1) * 8 + idx
                                   : (clampi(st_.pendingBank, 1, 7) - 1) * 8 + idx;
          if (writeArmed_) doWrite(target);
          else selectSlot(target, o, true);
          return;
        }
        case Button::Manual: doManual(); return;
        case Button::Write:
          writeArmed_ = !writeArmed_;
          dirty_ = true;
          return;
        case Button::Poly1:
        case Button::Poly2: {
          if (!is106) return;
          const int km = st_.perf.v[P_KEY_MODE];
          const bool lit1 = km == 0 || km == 2, lit2 = km == 1 || km == 2;
          const bool partnerLit = bt == Button::Poly1 ? lit2 : lit1;
          if (shift && partnerLit) {
            if (km != 2) setPerf(P_KEY_MODE, 2, o);
          } else {
            setPerf(P_KEY_MODE, bt == Button::Poly1 ? 0 : 1, o);
          }
          return;
        }
        case Button::KeyTranspose:
          if (!is106) return;
          ktArmed_ = !ktArmed_;
          dirty_ = true;
          return;
        case Button::LfoTrig:
          if (is106) return;
          trigPanel_ = true;
          updateLfoTrig();
          return;
      }
      return;
    }
    case CommandType::Release:
      if (static_cast<Button>(c.a) == Button::LfoTrig) {
        trigPanel_ = false;
        updateLfoTrig();
      }
      return;
    case CommandType::Lever: {
      double v = c.f;
      if (v > 1.0) v = 1.0;
      if (v < -1.0) v = -1.0;
      if (panel) {
        cancelArming(true, true);
        leverDragging_ = (c.flags & 1) != 0;
        if (c.flags & 4) {
          pushPanel_ = is106 && (c.flags & 2) != 0;
          updatePush();
        }
      }
      if (v != lever_) {
        lever_ = v;
        engine_.setLever(lever_);
        dirty_ = true;
        if (is106 && midiFunction() >= 2) {
          const int bend = static_cast<int>(std::lround(lever_ * 8191.0)) + 8192;
          const uint8_t m[3] = {static_cast<uint8_t>(0xE0 | ((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15)),
                                static_cast<uint8_t>(bend & 0x7F), static_cast<uint8_t>((bend >> 7) & 0x7F)};
          txRateLimited(18, m, 3);
        }
      }
      return;
    }
    case CommandType::Push:
      if (is106) {
        pushKey_ = c.b != 0;
        updatePush();
      } else {
        trigKey_ = c.b != 0;
        updateLfoTrig();
      }
      return;
    case CommandType::Note:
      if (c.b > 0) noteOnFrom(kSrcKeyboard, c.a, c.b, true);
      else noteOffFrom(kSrcKeyboard, c.a, true);
      return;
    case CommandType::Pedal: {
      const bool down = c.b != 0;
      if (down != pedalKey_) {
        pedalKey_ = down;
        updatePedal();
        if (canTx()) {
          const uint8_t m[3] = {static_cast<uint8_t>(0xB0 | ((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15)), 64,
                                static_cast<uint8_t>(down ? 127 : 0)};
          tx(m, 3);
        }
      }
      return;
    }
    case CommandType::MidiIn: {
      const int port = c.port < kMaxPorts ? c.port : kMaxPorts - 1;
      const int len = c.len <= 24 ? c.len : 24;
      parsers_[port].feed(c.data, len, [&](const uint8_t* m, int n) { handleMidi(m, n, port); });
      return;
    }
    case CommandType::SelectSlot:
      if (c.i >= 1 && c.i <= kNumSlots) {
        if (c.src == Source::Host && c.i == st_.currentSlot) return;  // host restore keeps an EDITED buffer
        selectSlot(c.i, o, c.src == Source::Overlay || c.src == Source::Panel);
      }
      return;
    case CommandType::Rename: {
      if (c.i < 1 || c.i > kNumSlots) return;
      if (protect()) {
        showMessage(LedMessage::Protected);
        notice(NoticeKind::Protected);
        return;
      }
      Name n;
      int len = 0;
      for (int i = 0; i < c.len && i < 24 && len < kNameMax; ++i) {
        const uint8_t ch = c.data[i];
        n.s[len++] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '?';
      }
      while (len > 0 && n.s[len - 1] == ' ') --len;
      n.s[len] = 0;
      if (st_.bank[c.i - 1].name != n) {
        st_.bank[c.i - 1].name = n;
        slotChanged(c.i);
        if (c.i == st_.currentSlot) setEditName(n);
        markDirty();
      }
      return;
    }
    case CommandType::CopySlot: {
      const int from = c.i, to = c.b2 + 1;
      if (from < 1 || from > kNumSlots || to < 1 || to > kNumSlots) return;
      if (protect()) {
        showMessage(LedMessage::Protected);
        notice(NoticeKind::Protected);
        return;
      }
      st_.bank[to - 1] = st_.bank[from - 1];
      slotChanged(to);
      markDirty();
      return;
    }
    case CommandType::SlotToEdit: {
      if (c.i < 1 || c.i > kNumSlots) return;
      const Slot s = st_.bank[c.i - 1];
      setEditPatch(s.patch, o);
      setEditName(s.name);
      st_.manual = true;
      st_.edited = false;
      markDirty();
      return;
    }
    case CommandType::SendPatch:
      if (is106 && midiFunction() == 3) txDump(0x31, 0, st_.edit);
      return;
    case CommandType::LoadBulk:
    case CommandType::LoadState: {
      Bulk* b = bulk(c.i);
      if (!b) return;
      applyBulk(*b);
      b->busy.store(0, std::memory_order_release);
      return;
    }
    case CommandType::ShowMessage:
      showMessage(static_cast<LedMessage>(c.a));
      return;
    case CommandType::ReleaseAll:
      releaseKeyboard();
      return;
    case CommandType::MidiPortGone:
      // acts as CC123 for the notes of every channel of that input; clears the HOLD set and arpeggio
      for (uint32_t ch = 0; ch < 16; ++ch) engine_.releaseSource(1u + static_cast<uint32_t>(c.port) * 16u + ch);
      return;
    case CommandType::Gesture:
    case CommandType::None:
      return;
  }
}

// ------------------------------------------------------------------------------------------------
// state changes

void Core::emit(const Event& e) {
  if (!evQ_.push(e)) ++dropped_;
}

void Core::notice(NoticeKind k, int i, int j) {
  Event e;
  e.type = EventType::Notice;
  e.a = static_cast<uint8_t>(k);
  e.i = i;
  e.j = j;
  emit(e);
}

void Core::showMessage(LedMessage m) {
  msg_ = m;
  msgEnd_ = engine_.now() + static_cast<int64_t>(2.0 * fs_);
  dirty_ = true;
}

void Core::slotChanged(int slot) {
  Event e;
  e.type = EventType::SlotChanged;
  e.i = slot;
  emit(e);
  ++bankVersion_;
  dirty_ = true;
}

bool Core::setField(int f, int v, Origin o, bool transmit) {
  v = clampi(v, 0, 127);
  if (st_.edit.v[f] == v) return false;  // a write of an unchanged value is not a change
  st_.edit.v[f] = static_cast<uint8_t>(v);
  st_.edited = true;
  Event e;
  e.type = EventType::FieldChanged;
  e.a = static_cast<uint8_t>(f);
  e.i = v;
  e.b = static_cast<uint8_t>(o);
  emit(e);
  markDirty();
  if (transmit && canTx() && midiFunction() == 3) txParamForField(f);
  return true;
}

bool Core::externalFieldWrite(int f, int v, Origin o) { return setField(f, v, o, false); }

void Core::setEditPatch(const Patch& p, Origin o) {
  for (int f = 0; f < kNumFields; ++f) {
    if (st_.edit.v[f] == p.v[f]) continue;
    st_.edit.v[f] = p.v[f];
    Event e;
    e.type = EventType::FieldChanged;
    e.a = static_cast<uint8_t>(f);
    e.i = p.v[f];
    e.b = static_cast<uint8_t>(o);
    emit(e);
  }
  markDirty();
}

void Core::setEditName(const Name& n) {
  if (st_.editName != n) {
    st_.editName = n;
    markDirty();
  }
}

bool Core::setPerf(int p, int v, Origin o) {
  int lo, hi;
  perfRange(p, lo, hi);
  v = clampi(v, lo, hi);
  const int old = st_.perf.v[p];
  if (old == v) return false;
  st_.perf.v[p] = static_cast<int16_t>(v);
  Event e;
  e.type = EventType::PerfChanged;
  e.a = static_cast<uint8_t>(p);
  e.i = v;
  e.b = static_cast<uint8_t>(o);
  emit(e);
  markDirty();
  switch (p) {
    case P_KEY_MODE:
      // a 106 setting: no effect in 60 mode (§4.2); entering 106 mode releases everything anyway (F-12)
      if (mode() == Mode::J106) engine_.keyModeChanged();
      break;
    case P_HOLD: engine_.holdChanged(v != 0); break;
    case P_ARP_ON: engine_.arpChanged(v != 0); break;
    case P_MIDI_CHANNEL:
    case P_OMNI:
    case P_MIDI_FUNCTION:
      // acts as CC123 for the notes of the affected sources; clears the HOLD set and arpeggio
      engine_.releaseSource(Engine::kAllMidi);
      pedalMidi_ = false;
      pushMidi_ = false;
      trigMidi_ = false;
      updatePedal();
      updatePush();
      updateLfoTrig();
      break;
    default: break;
  }
  return true;
}

bool Core::setSetting(int s, int v, Origin o) {
  int lo, hi;
  settingRange(s, lo, hi);
  v = clampi(v, lo, hi);
  if (s == S_OVERSAMPLE) v = v >= 3 ? 4 : 2;
  if (s == S_VARIATION) v = (v + 5) / 10 * 10;
  if (s == S_ZOOM) v = v < 50 ? 0 : (v + 5) / 10 * 10;
  const int old = st_.settings.v[s];
  if (old == v) return false;
  st_.settings.v[s] = static_cast<int16_t>(v);
  if (s == S_MODE) {
    cancelArming(true, true);  // a mode switch cancels WRITE and key-transpose arming (F-12)
    engine_.switchMode(v == 1 ? Mode::J106 : Mode::J60);
    recomputePendingBank();
    // performance sources specific to one mode are released
    pushPanel_ = pushKey_ = pushMidi_ = false;
    trigPanel_ = trigKey_ = trigMidi_ = false;
    engine_.setPush(false);
    engine_.setLfoTrig(false);
  } else if (s == S_OVERSAMPLE) {
    engine_.setOversample(v);
    engine_.holdChanged(st_.perf.v[P_HOLD] != 0);
    latency_.store(engine_.latencySamples(), std::memory_order_relaxed);
    Event e;
    e.type = EventType::Latency;
    e.i = engine_.latencySamples();
    emit(e);
  }
  Event e;
  e.type = EventType::SettingChanged;
  e.a = static_cast<uint8_t>(s);
  e.i = v;
  e.b = static_cast<uint8_t>(o);
  emit(e);
  markDirty();
  return true;
}

int Core::bankOf(int slot, Mode m) {
  if (m == Mode::J106) return ((slot - 1) % 64) / 8 + 1;
  return slot <= 56 ? (slot - 1) / 8 + 1 : 0;
}

void Core::recomputePendingBank() {
  const Mode m = mode();
  const int b = bankOf(st_.currentSlot, m);
  if (b == 0) {
    st_.pendingBank = 1;
    st_.pendingGroup = 0;
  } else {
    st_.pendingBank = b;
    st_.pendingGroup = m == Mode::J106 ? groupOf(st_.currentSlot) : 0;
  }
  dirty_ = true;
}

void Core::selectSlot(int slot, Origin o, bool fromPanelOrManager) {
  const Slot s = st_.bank[slot - 1];
  setEditPatch(s.patch, o);
  setEditName(s.name);
  st_.stored = s.patch;
  st_.edited = false;
  st_.manual = false;
  st_.currentSlot = slot;
  recomputePendingBank();
  Event e;
  e.type = EventType::ProgramChanged;
  e.i = slot;
  emit(e);
  markDirty();
  if (fromPanelOrManager && canTx()) {
    if (midiFunction() == 3) {
      txDump(0x30, slot - 1, s.patch);
    } else if (midiFunction() == 2) {
      const uint8_t m[2] = {static_cast<uint8_t>(0xC0 | ((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15)),
                            static_cast<uint8_t>(slot - 1)};
      tx(m, 2);
    }
  }
}

void Core::doWrite(int target) {
  writeArmed_ = false;
  dirty_ = true;
  if (protect()) {
    showMessage(LedMessage::Protected);
    return;
  }
  st_.bank[target - 1].patch = st_.edit;
  st_.bank[target - 1].name = st_.editName;
  st_.stored = st_.edit;
  st_.currentSlot = target;
  st_.edited = false;
  st_.manual = false;
  recomputePendingBank();
  slotChanged(target);
  Event e;
  e.type = EventType::ProgramChanged;
  e.i = target;
  emit(e);
  showMessage(LedMessage::WriteOk);
  markDirty();
}

void Core::doManual() {
  st_.manual = true;
  st_.edited = false;
  markDirty();
  if (canTx() && midiFunction() == 3) txDump(0x31, 0, st_.edit);
}

void Core::resetToFirstRun(bool withMessage) {
  CoreState fresh;
  firstRunState(fresh, kbdPlayDefault_);
  const Mode oldMode = mode();
  const int oldOs = st_.settings.v[S_OVERSAMPLE];
  for (int i = 0; i < kNumSlots; ++i) st_.bank[i] = fresh.bank[i];
  setEditPatch(fresh.edit, Origin::Load);
  st_.editName = fresh.editName;
  st_.stored = fresh.stored;
  st_.edited = fresh.edited;
  st_.manual = fresh.manual;
  st_.currentSlot = fresh.currentSlot;
  st_.pendingBank = fresh.pendingBank;
  st_.pendingGroup = fresh.pendingGroup;
  for (int p = 0; p < P_COUNT; ++p) setPerf(p, fresh.perf.v[p], Origin::Load);
  for (int s = 0; s < S_COUNT; ++s)
    if (s != S_MODE && s != S_OVERSAMPLE) setSetting(s, fresh.settings.v[s], Origin::Load);
  if (fresh.settings.v[S_MODE] != (oldMode == Mode::J106 ? 1 : 0)) setSetting(S_MODE, fresh.settings.v[S_MODE], Origin::Load);
  if (fresh.settings.v[S_OVERSAMPLE] != oldOs) setSetting(S_OVERSAMPLE, fresh.settings.v[S_OVERSAMPLE], Origin::Load);
  ++bankVersion_;
  Event e;
  e.type = EventType::BankChanged;
  emit(e);
  e.type = EventType::ProgramChanged;
  e.i = st_.currentSlot;
  emit(e);
  if (withMessage) showMessage(LedMessage::Reset);
  markDirty();
}

void Core::applyBulk(Bulk& b) {
  switch (b.kind) {
    case BulkKind::PatchToEdit: {
      setEditPatch(b.patch, Origin::Load);
      setEditName(b.name);
      st_.manual = true;
      st_.edited = false;
      notice(NoticeKind::Loaded);
      break;
    }
    case BulkKind::Bank: {
      if (protect()) {
        showMessage(LedMessage::Protected);
        notice(NoticeKind::Protected);
        return;
      }
      for (int i = 0; i < kNumSlots; ++i) st_.bank[i] = b.bank[i];
      ++bankVersion_;
      Event e;
      e.type = EventType::BankChanged;
      emit(e);
      notice(NoticeKind::BankLoaded);
      markDirty();
      break;
    }
    case BulkKind::SyxImport: {
      if (protect()) {
        showMessage(LedMessage::Protected);
        notice(NoticeKind::Protected);
        return;  // nothing written, 32 messages not applied
      }
      bool written[kNumSlots] = {};
      int nWritten = 0, nDropped = 0;
      const Patch init = initPatch();
      const Name initName = makeName("INIT");
      for (const SyxRecord& r : b.msgs) {
        int slot = -1;
        if (r.bytes[2] == 0x30 && !written[r.bytes[4]]) {
          slot = r.bytes[4] + 1;
        } else {
          for (int s = 0; s < kNumSlots; ++s) {
            if (!written[s] && st_.bank[s].patch == init && st_.bank[s].name == initName) {
              slot = s + 1;
              break;
            }
          }
        }
        if (slot < 0) {
          ++nDropped;
          continue;
        }
        Patch p = initPatch();
        sysex::applyDump(p, r.bytes);
        Slot& dst = st_.bank[slot - 1];
        dst.patch = p;
        // name by source in 106 notation (§4.3)
        char nm[16];
        if (r.bytes[2] == 0x30) {
          const int pp = r.bytes[4];
          nm[0] = pp < 64 ? 'A' : 'B';
          nm[1] = static_cast<char>('1' + (pp % 64) / 8);
          nm[2] = static_cast<char>('1' + pp % 8);
          nm[3] = 0;
        } else {
          const int index = static_cast<int>(&r - b.msgs.data()) + 1;
          std::memcpy(nm, "MANUAL ", 7);
          int len = 7;
          char digits[12];
          int nd = 0;
          int x = index;
          do {
            digits[nd++] = static_cast<char>('0' + x % 10);
            x /= 10;
          } while (x > 0);
          while (nd > 0 && len < kNameMax) nm[len++] = digits[--nd];
          nm[len] = 0;
        }
        dst.name = makeName(nm);
        written[slot - 1] = true;
        ++nWritten;
      }
      ++bankVersion_;
      Event e;
      e.type = EventType::BankChanged;
      emit(e);
      notice(NoticeKind::Imported, nWritten, nDropped);
      markDirty();
      break;
    }
    case BulkKind::State: {
      const auto res = static_cast<BlobResult>(b.stateResult);
      if (res == BlobResult::Empty || res == BlobResult::Invalid) {
        resetToFirstRun(res == BlobResult::Invalid);
        return;
      }
      const CoreState& s = b.state;
      for (int i = 0; i < kNumSlots; ++i) st_.bank[i] = s.bank[i];
      setEditPatch(s.edit, Origin::Load);
      st_.editName = s.editName;
      st_.stored = s.stored;
      st_.edited = s.edited;
      st_.manual = s.manual;
      st_.currentSlot = s.currentSlot;
      st_.pendingBank = s.pendingBank;
      st_.pendingGroup = s.pendingGroup;
      for (int p = 0; p < P_COUNT; ++p) setPerf(p, s.perf.v[p], Origin::Load);
      if (res == BlobResult::Ok)
        for (int k = 0; k < S_COUNT; ++k) setSetting(k, s.settings.v[k], Origin::Load);
      // a mode change above recomputes the pending bank; the saved one is part of the state (§4.7)
      st_.pendingBank = s.pendingBank;
      st_.pendingGroup = s.pendingGroup;
      engine_.holdChanged(st_.perf.v[P_HOLD] != 0);
      ++bankVersion_;
      Event e;
      e.type = EventType::BankChanged;
      emit(e);
      e.type = EventType::ProgramChanged;
      e.i = st_.currentSlot;
      emit(e);
      markDirty();
      return;
    }
    default: return;
  }
  // F0 41 32 messages of an accepted file, in file order, as external changes
  for (const ParamRecord& pr : b.params) applyParamMessage(pr.pp, pr.vv, Origin::Load);
}

void Core::applyParamMessage(int pp, int vv, Origin o) {
  if (pp < kNumSliders) {
    externalFieldWrite(pp, vv, o);
  } else if (pp == 0x10) {
    Patch p = st_.edit;
    sysex::applySw1(p, static_cast<uint8_t>(vv));
    const int fs[4] = {W_RANGE, W_PULSE, W_SAW, W_CHORUS};
    for (int f : fs) externalFieldWrite(f, p.v[f], o);
  } else if (pp == 0x11) {
    Patch p = st_.edit;
    sysex::applySw2(p, static_cast<uint8_t>(vv));
    const int fs[4] = {W_PWM_SRC, W_VCF_POL, W_VCA_MODE, W_HPF};
    for (int f : fs) externalFieldWrite(f, p.v[f], o);
  }
}

// ------------------------------------------------------------------------------------------------
// MIDI receive (R-M1, F-11)

void Core::handleMidi(const uint8_t* m, int len, int port) {
  if (len <= 0) return;
  const uint8_t s = m[0];
  if (s >= 0xF8) {
    if (s == 0xF8) engine_.clockTick();
    else if (s == 0xFA) engine_.clockStart();
    return;
  }
  if (s == 0xF0) {
    handleSysex(m, len);
    return;
  }
  if (s >= 0x80 && s < 0xF0) handleChannelMessage(m, len, port);
}

void Core::handleChannelMessage(const uint8_t* m, int len, int port) {
  const int ch = m[0] & 0x0F;
  const int type = m[0] & 0xF0;
  if (!st_.perf.v[P_OMNI] && ch + 1 != st_.perf.v[P_MIDI_CHANNEL]) return;
  const uint32_t source = 1u + static_cast<uint32_t>(port) * 16u + static_cast<uint32_t>(ch);
  const int fn = mode() == Mode::J106 ? midiFunction() : 3;
  switch (type) {
    case 0x90:
      if (len < 3) return;
      if (m[2] == 0) noteOffFrom(source, m[1], false);
      else noteOnFrom(source, m[1], m[2], false);
      return;
    case 0x80:
      if (len < 3) return;
      noteOffFrom(source, m[1], false);
      return;
    case 0xB0: {
      if (len < 3) return;
      const int cc = m[1];
      if (cc == 120 || cc == 123 || cc == 64) {
        handleCC(cc, m[2], source);
      } else if (cc == 1) {
        if (fn >= 2) handleCC(cc, m[2], source);
      } else if (fn >= 3) {
        handleCC(cc, m[2], source);
      }
      return;
    }
    case 0xE0: {
      if (len < 3 || fn < 2) return;
      if (leverDragging_) return;  // incoming pitch bend ignored while the mouse drags the lever
      const int v = m[1] | (m[2] << 7);
      double l = (v - 8192) / 8191.0;
      if (l > 1.0) l = 1.0;
      if (l < -1.0) l = -1.0;
      if (l != lever_) {
        lever_ = l;
        engine_.setLever(l);
        dirty_ = true;
      }
      return;
    }
    case 0xC0:
      if (len < 2 || fn < 2) return;
      selectSlot(m[1] + 1, Origin::Midi, false);
      return;
    default: return;
  }
}

void Core::handleCC(int cc, int v, uint32_t /*source*/) {
  const bool is106 = mode() == Mode::J106;
  const Origin o = Origin::Midi;
  auto fld = [&](int f, int val) { externalFieldWrite(f, val, o); };
  switch (cc) {
    case 1:
      if (is106) {
        pushMidi_ = v >= 64;
        updatePush();
      } else {
        trigMidi_ = v >= 64;
        updateLfoTrig();
      }
      return;
    case 3: fld(B_LFO_RATE, v); return;
    case 9: fld(B_LFO_DELAY, v); return;
    case 12: fld(W_RANGE, three(v)); return;
    case 13: fld(B_DCO_LFO, v); return;
    case 14: fld(B_DCO_PWM, v); return;
    case 15: fld(W_PWM_SRC, three(v)); return;
    case 16: fld(W_PULSE, v >= 64 ? 1 : 0); return;
    case 17: fld(W_SAW, v >= 64 ? 1 : 0); return;
    case 28: fld(W_SUB, v >= 64 ? 1 : 0); return;
    case 18: fld(B_SUB_LEVEL, v); return;
    case 19: fld(B_NOISE, v); return;
    case 20: fld(W_HPF, v / 32); return;
    case 21: fld(W_VCF_POL, v >= 64 ? 1 : 0); return;
    case 25: fld(W_VCA_MODE, v >= 64 ? 1 : 0); return;
    case 22: fld(B_VCF_ENV, v); return;
    case 23: fld(B_VCF_LFO, v); return;
    case 24: fld(B_VCF_KYBD, v); return;
    case 74: fld(B_VCF_FREQ, v); return;
    case 71: fld(B_VCF_RES, v); return;
    case 26: fld(B_VCA_LEVEL, v); return;
    case 73: fld(B_ENV_A, v); return;
    case 75: fld(B_ENV_D, v); return;
    case 27: fld(B_ENV_S, v); return;
    case 72: fld(B_ENV_R, v); return;
    case 93: fld(W_CHORUS, v / 32); return;
    case 30:
      if (!is106) setPerf(P_LFO_TRIG_MODE, v >= 64 ? 1 : 0, o);
      return;
    case 86:
      if (is106) setPerf(P_KEY_MODE, three(v), o);
      return;
    case 87: setPerf(P_BEND_DCO, v, o); return;
    case 5:
      if (is106) setPerf(P_PORTA_TIME, v, o);
      return;
    case 65:
      if (is106) setPerf(P_PORTA_ON, v >= 64 ? 1 : 0, o);
      return;
    case 64:
      pedalMidi_ = v >= 64;
      updatePedal();
      return;
    case 7: setPerf(P_VOLUME, v, o); return;
    case 120:
    case 123:
      engine_.allNotes(cc == 120);
      pedalMidi_ = pedalKey_ = false;
      return;
    default: return;  // unlisted CCs are ignored
  }
}

void Core::handleSysex(const uint8_t* m, int len) {
  const bool is106 = mode() == Mode::J106;
  if (is106 && midiFunction() != 3) return;
  if (sysex::isDump(m, len)) {
    Patch p = st_.edit;
    sysex::applyDump(p, m);
    if (m[2] == 0x30 && is106) {
      const int slot = m[4] + 1;
      if (slot > kNumSlots) return;
      st_.currentSlot = slot;
      recomputePendingBank();
      if (!protect()) {
        st_.bank[slot - 1].patch = p;
        slotChanged(slot);
        setEditPatch(p, Origin::Sysex);
        st_.stored = p;
        st_.edited = false;
      } else {
        setEditPatch(p, Origin::Sysex);
        st_.stored = st_.bank[slot - 1].patch;
        st_.edited = true;
      }
      setEditName(st_.bank[slot - 1].name);
      st_.manual = false;
      Event e;
      e.type = EventType::ProgramChanged;
      e.i = slot;
      emit(e);
    } else {
      setEditPatch(p, Origin::Sysex);
      st_.manual = true;
      st_.edited = false;
    }
    showMessage(LedMessage::Received);
    markDirty();
    return;
  }
  if (sysex::isParam(m, len)) {
    const int pp = m[4];
    if (pp <= 0x11) applyParamMessage(pp, m[5], Origin::Sysex);
  }
}

// ------------------------------------------------------------------------------------------------
// notes and performance sources

void Core::noteOnFrom(uint32_t source, int note, int vel, bool fromKeyboard) {
  if (note < k::kNoteLow || note > k::kNoteHigh) return;  // ignored, including the note-off
  if (ktArmed_ && mode() == Mode::J106) {
    ktArmed_ = false;
    dirty_ = true;
    setPerf(P_KEY_TRANSPOSE, clampi(note - 60, -12, 12), originOf(fromKeyboard ? Source::Keyboard : Source::System));
    return;  // does not sound, is not transmitted
  }
  if (fromKeyboard) {
    kbdNotes_[note] = true;
    if (canTx()) {
      const uint8_t m[3] = {static_cast<uint8_t>(0x90 | ((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15)),
                            static_cast<uint8_t>(note), 64};
      tx(m, 3);
    }
  }
  engine_.noteOn(Engine::makeKey(source, note), note, vel);
}

void Core::noteOffFrom(uint32_t source, int note, bool fromKeyboard) {
  if (note < k::kNoteLow || note > k::kNoteHigh) return;
  if (fromKeyboard) {
    if (!kbdNotes_[note]) return;
    kbdNotes_[note] = false;
    if (canTx()) {
      const uint8_t m[3] = {static_cast<uint8_t>(0x90 | ((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15)),
                            static_cast<uint8_t>(note), 0};
      tx(m, 3);
    }
  }
  engine_.noteOff(Engine::makeKey(source, note), note);
}

void Core::updatePedal() { engine_.pedal(pedalMidi_ || pedalKey_); }

void Core::updatePush() {
  const bool all = pushPanel_ || pushKey_ || pushMidi_;
  const bool local = pushPanel_ || pushKey_;
  engine_.setPush(all);
  // transmit CC1 when the locally held push changes (mouse or key 3), functions II and III
  if (local != localPushSent_) {
    localPushSent_ = local;
    if (canTx() && midiFunction() >= 2) {
      const uint8_t m[3] = {static_cast<uint8_t>(0xB0 | ((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15)), 1,
                            static_cast<uint8_t>(local ? 127 : 0)};
      tx(m, 3);
    }
  }
  dirty_ = true;
}

void Core::updateLfoTrig() {
  engine_.setLfoTrig(trigPanel_ || trigKey_ || trigMidi_);
  dirty_ = true;
}

void Core::releaseKeyboard() {
  for (int n = 0; n < 128; ++n)
    if (kbdNotes_[n]) noteOffFrom(kSrcKeyboard, n, true);
  if (pedalKey_) {
    Command c;
    c.type = CommandType::Pedal;
    c.src = Source::Keyboard;
    c.b = 0;
    handleCommand(c);
  }
  pushPanel_ = pushKey_ = false;
  updatePush();
  trigPanel_ = trigKey_ = false;
  updateLfoTrig();
  leverDragging_ = false;
  if (lever_ != 0.0) {
    Command c;
    c.type = CommandType::Lever;
    c.src = Source::Keyboard;
    c.f = 0.0f;
    handleCommand(c);
  }
}

// ------------------------------------------------------------------------------------------------
// transmit (R-M3)

void Core::tx(const uint8_t* bytes, int len) {
  if (!canTx()) return;  // 60 mode transmits nothing
  Event e;
  e.type = EventType::MidiOut;
  e.len = static_cast<uint8_t>(len);
  std::memcpy(e.data, bytes, static_cast<std::size_t>(len));
  emit(e);
}

void Core::txRateLimited(int channel, const uint8_t* bytes, int len) {
  const int64_t now = engine_.now();
  const int64_t interval = static_cast<int64_t>(std::ceil(k::kTxIntervalS * fs_));
  if (now - txLast_[channel] >= interval) {
    tx(bytes, len);
    txLast_[channel] = now;
    txPending_[channel] = false;
  } else {
    std::memcpy(txBytes_[channel], bytes, static_cast<std::size_t>(len));
    txLen_[channel] = static_cast<uint8_t>(len);
    txPending_[channel] = true;
  }
}

void Core::flushRateLimited(bool force) {
  const int64_t now = engine_.now();
  const int64_t interval = static_cast<int64_t>(std::ceil(k::kTxIntervalS * fs_));
  for (int c = 0; c < kTxChannels; ++c) {
    if (!txPending_[c]) continue;
    if (force || now - txLast_[c] >= interval) {
      tx(txBytes_[c], txLen_[c]);
      txLast_[c] = now;
      txPending_[c] = false;
    }
  }
}

void Core::txParamForField(int f) {
  const int pp = sysex::paramFor(f);
  if (pp < 0) return;
  int vv;
  if (pp < kNumSliders) vv = st_.edit.v[pp];
  else if (pp == 0x10) vv = sysex::encodeSw1(st_.edit);
  else vv = sysex::encodeSw2(st_.edit);
  const uint8_t m[7] = {0xF0, 0x41, 0x32, static_cast<uint8_t>((st_.perf.v[P_MIDI_CHANNEL] - 1) & 15),
                        static_cast<uint8_t>(pp), static_cast<uint8_t>(vv & 0x7F), 0xF7};
  txRateLimited(pp, m, 7);
}

void Core::txDump(uint8_t type, int pp, const Patch& p) {
  // A dump carries every parameter: F0 41 32 values still held back by the 10 ms limit are superseded
  // and must not reach the receiver after it (they would overwrite the dumped patch there).
  for (int c = 0; c <= 0x11; ++c) txPending_[c] = false;
  uint8_t m[24];
  sysex::encodeDump(p, type, st_.perf.v[P_MIDI_CHANNEL] - 1, pp, m);
  tx(m, 24);
}

// ------------------------------------------------------------------------------------------------
// display

LedState Core::led() const {
  LedState L;
  const Mode m = mode();
  if (msg_ != LedMessage::None) {
    static const char* texts[] = {"  ", "Pr", "Er", "rc", "ok", "rE"};
    const char* t = texts[static_cast<int>(msg_)];
    L.text[0] = t[0];
    L.text[1] = t[1];
    L.dp = false;
    L.blink = false;
    L.message = msg_;
    return L;
  }
  L.dp = st_.edited;
  if (ktArmed_ && m == Mode::J106) {
    L.text[0] = 't';
    L.text[1] = '-';
    return L;
  }
  L.blink = writeArmed_;
  if (st_.manual) {
    L.text[0] = '-';
    L.text[1] = '-';
  } else {
    const int b = bankOf(st_.currentSlot, m);
    if (b == 0) {
      L.text[0] = ' ';
      L.text[1] = ' ';
    } else {
      L.text[0] = static_cast<char>('0' + b);
      L.text[1] = static_cast<char>('0' + patchOf(st_.currentSlot));
    }
  }
  return L;
}

uint64_t Core::buttonLeds(uint64_t* blink) const {
  uint64_t on = 0, bl = 0;
  auto set = [&](int bit) { on |= (1ull << bit); };
  const Mode m = mode();
  const bool is106 = m == Mode::J106;
  const Patch& e = st_.edit;
  auto eff = [&](int f) { return sysex::eff(e, f); };
  set(L_RANGE16 + eff(W_RANGE));
  if (eff(W_PULSE)) set(L_PULSE);
  if (eff(W_SAW)) set(L_SAW);
  int ch = eff(W_CHORUS);
  if (is106) {
    if (ch == 3) ch = 1;
    set(ch == 0 ? L_CHORUS_OFF : ch == 1 ? L_CHORUS_I : L_CHORUS_II);
    if (st_.perf.v[P_PORTA_ON]) set(L_PORTA);
    const int km = st_.perf.v[P_KEY_MODE];
    if (km == 0 || km == 2) set(L_POLY1);
    if (km == 1 || km == 2) set(L_POLY2);
    set(st_.pendingGroup ? L_GROUP_B : L_GROUP_A);
    set(L_BANK1 + st_.pendingBank - 1);
    if (ktArmed_) bl |= 1ull << L_KEY_TRANSPOSE;
    else if (st_.perf.v[P_KEY_TRANSPOSE] != 0) set(L_KEY_TRANSPOSE);
  } else {
    if (eff(W_SUB)) set(L_SUB);
    if (ch == 1 || ch == 3) set(L_CHORUS_I);
    if (ch == 2 || ch == 3) set(L_CHORUS_II);
    if (st_.perf.v[P_HOLD]) set(L_HOLD);
    if (st_.perf.v[P_ARP_ON]) set(L_ARP);
    const int pb = st_.pendingBank;
    if (pb == 6) {
      set(L_BANK1);
      set(L_BANK1 + 4);
    } else if (pb == 7) {
      set(L_BANK1 + 1);
      set(L_BANK1 + 4);
    } else {
      set(L_BANK1 + pb - 1);
    }
    if (trigPanel_ || trigKey_ || trigMidi_) set(L_LFO_TRIG);
  }
  const int cb = bankOf(st_.currentSlot, m);
  const bool sameBank = cb != 0 && cb == st_.pendingBank && (!is106 || groupOf(st_.currentSlot) == st_.pendingGroup);
  if (!st_.manual && sameBank) set(L_PATCH1 + patchOf(st_.currentSlot) - 1);
  if (st_.manual) set(L_MANUAL);
  if (writeArmed_) bl |= 1ull << L_WRITE;
  if (blink) *blink = bl;
  return on;
}

void Core::fillSnapshot(Snapshot& s) const {
  s.seq = snapSeq_;
  s.edit = st_.edit;
  s.editName = st_.editName;
  s.stored = st_.stored;
  s.edited = st_.edited;
  s.manual = st_.manual;
  s.currentSlot = static_cast<int16_t>(st_.currentSlot);
  s.pendingBank = static_cast<int8_t>(st_.pendingBank);
  s.pendingGroup = static_cast<int8_t>(st_.pendingGroup);
  s.perf = st_.perf;
  s.settings = st_.settings;
  s.led = led();
  s.buttonLeds = buttonLeds(&s.buttonBlink);
  s.writeArmed = writeArmed_;
  s.ktArmed = ktArmed_;
  s.lfoTrigHeld = trigPanel_ || trigKey_ || trigMidi_;
  s.push = pushPanel_ || pushKey_ || pushMidi_;
  s.lever = static_cast<float>(lever_);
  s.latency = engine_.latencySamples();
  s.droppedEvents = dropped_;
  for (int i = 0; i < kNumSlots; ++i) s.bank[i] = st_.bank[i];
  s.bankVersion = bankVersion_;
}

void Core::publishSnapshot() {
  ++snapSeq_;
  fillSnapshot(snap_.back());
  snap_.publish();
  dirty_ = false;
}

}  // namespace vjn6
