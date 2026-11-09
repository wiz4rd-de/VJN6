#include "vjn6/instrument.h"

#include <chrono>
#include <thread>

#include "core/core.h"
#include "files/bulk.h"
#include "state/state.h"

namespace vjn6 {

Instrument::Instrument(double hostSampleRate, int maxBlock) : core_(new Core(hostSampleRate, maxBlock)) {}
Instrument::~Instrument() = default;

void Instrument::prepare(double hostSampleRate, int maxBlock) { core_->prepare(hostSampleRate, maxBlock); }
bool Instrument::post(const Command& c) { return core_->post(c); }
bool Instrument::pollEvent(Event& e) { return core_->pollEvent(e); }
void Instrument::midi(const uint8_t* bytes, int len, int sampleOffset) { core_->midi(bytes, len, sampleOffset); }
void Instrument::render(float* L, float* R, int n) { core_->render(L, R, n); }
int Instrument::latencySamples() const { return core_->latencySamples(); }
void Instrument::hostParam(int index, int value, int sampleOffset) { core_->hostParam(index, value, sampleOffset); }
void Instrument::snapshot(Snapshot& out) const { core_->snapshot(out); }
int Instrument::acquireBulk() { return core_->acquireBulk(); }
Bulk* Instrument::bulk(int index) { return core_->bulk(index); }
double Instrument::sampleRate() const { return core_->sampleRate(); }
Core& Instrument::core() { return *core_; }

static void stateFromSnapshot(const Snapshot& s, CoreState& st) {
  for (int i = 0; i < kNumSlots; ++i) st.bank[i] = s.bank[i];
  st.edit = s.edit;
  st.editName = s.editName;
  st.stored = s.stored;
  st.edited = s.edited;
  st.manual = s.manual;
  st.currentSlot = s.currentSlot;
  st.pendingBank = s.pendingBank;
  st.pendingGroup = s.pendingGroup;
  st.perf = s.perf;
  st.settings = s.settings;
}

void Instrument::saveState(StateBlob& blob) const {
  auto snap = std::make_unique<Snapshot>();
  core_->snapshot(*snap);
  auto st = std::make_unique<CoreState>();
  stateFromSnapshot(*snap, *st);
  blob.bytes = encodeState(*st);
}

bool Instrument::loadState(const StateBlob& blob) {
  int idx = -1;
  for (int tries = 0; tries < 200 && idx < 0; ++tries) {
    idx = core_->acquireBulk();
    if (idx < 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (idx < 0) return false;
  Bulk* b = core_->bulk(idx);
  b->kind = BulkKind::State;
  // decode on top of the first-run settings so that a newer-version blob keeps defaults it lacks
  firstRunState(b->state, true);
  {
    Snapshot* cur = new Snapshot();
    core_->snapshot(*cur);
    b->state.settings = cur->settings;
    delete cur;
  }
  const BlobResult r = decodeState(blob.bytes.data(), blob.bytes.size(), b->state);
  b->stateResult = static_cast<int>(r);
  Command c;
  c.type = CommandType::LoadState;
  c.src = Source::System;
  c.i = idx;
  if (!core_->post(c)) {
    b->busy.store(0);
    return false;
  }
  return r == BlobResult::Ok || r == BlobResult::OkNewer || r == BlobResult::Empty;
}

}  // namespace vjn6
