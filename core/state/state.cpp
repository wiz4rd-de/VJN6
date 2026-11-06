#include "state/state.h"

#include <cstring>

namespace vjn6 {

namespace {
#include "state/default_bank.inc"  // kDefaultBankData[128] { name, 25 field values }

struct DefaultBank {
  Slot slots[kNumSlots];
  DefaultBank() {
    for (int i = 0; i < kNumSlots; ++i) {
      slots[i].name = makeName(kDefaultBankData[i].name);
      for (int f = 0; f < kNumFields; ++f) slots[i].patch.v[f] = kDefaultBankData[i].v[f];
    }
  }
};

void putU16(std::vector<uint8_t>& b, int v) {
  b.push_back(static_cast<uint8_t>(v & 0xFF));
  b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
void putPatch(std::vector<uint8_t>& b, const Patch& p) {
  for (int f = 0; f < kNumFields; ++f) b.push_back(p.v[f]);
}
void putName(std::vector<uint8_t>& b, const Name& n) {
  const std::size_t len = std::strlen(n.s);
  b.push_back(static_cast<uint8_t>(len));
  for (int i = 0; i < kNameMax; ++i) b.push_back(i < static_cast<int>(len) ? static_cast<uint8_t>(n.s[i]) : 0);
}

struct Reader {
  const uint8_t* p;
  std::size_t n, i = 0;
  bool ok = true;
  uint8_t u8() {
    if (i >= n) {
      ok = false;
      return 0;
    }
    return p[i++];
  }
  int u16() {
    const int lo = u8();
    const int hi = u8();
    return lo | (hi << 8);
  }
  int s16() {
    const int v = u16();
    return v >= 32768 ? v - 65536 : v;
  }
  bool patch(Patch& out) {
    for (int f = 0; f < kNumFields; ++f) {
      out.v[f] = u8();
      if (out.v[f] > 127) ok = false;
    }
    return ok;
  }
  bool name(Name& out) {
    const int len = u8();
    if (len > kNameMax) ok = false;
    char tmp[kNameMax + 1] = {};
    for (int i2 = 0; i2 < kNameMax; ++i2) {
      const uint8_t c = u8();
      if (i2 < len) {
        if (c < 0x20 || c > 0x7E) ok = false;
        tmp[i2] = static_cast<char>(c);
      }
    }
    if (len > 0 && tmp[len - 1] == ' ') ok = false;
    if (ok) {
      std::memcpy(out.s, tmp, sizeof tmp);
      out.s[len < 0 ? 0 : len] = 0;
    }
    return ok;
  }
};

// v1 payload size after the 6-byte header: bank 128 x 38, edit 38, stored 25, flags 1, slot 1,
// pending 2, perf 20 x 2, settings 8 x 2.
constexpr std::size_t kV1Payload = 128 * 38 + 38 + 25 + 1 + 1 + 2 + P_COUNT * 2 + S_COUNT * 2;
}  // namespace

const Slot* defaultBank() {
  static const DefaultBank bank;
  return bank.slots;
}

void firstRunState(CoreState& s, bool keyboardPlayDefault) {
  const Slot* b = defaultBank();
  for (int i = 0; i < kNumSlots; ++i) s.bank[i] = b[i];
  s.edit = b[0].patch;
  s.editName = b[0].name;
  s.stored = b[0].patch;
  s.edited = false;
  s.manual = false;
  s.currentSlot = 1;
  s.pendingBank = 1;
  s.pendingGroup = 0;
  s.perf = PerfSettings{};
  s.settings = SettingsValues{};
  s.settings.v[S_KBD_PLAY] = keyboardPlayDefault ? 1 : 0;
}

uint32_t crc32(const uint8_t* data, std::size_t len) {
  uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    c ^= data[i];
    for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

std::vector<uint8_t> encodeState(const CoreState& s) {
  std::vector<uint8_t> b;
  b.reserve(6 + kV1Payload + 4);
  b.push_back('V');
  b.push_back('J');
  b.push_back('N');
  b.push_back('6');
  putU16(b, kStateVersion);
  for (int i = 0; i < kNumSlots; ++i) {
    putPatch(b, s.bank[i].patch);
    putName(b, s.bank[i].name);
  }
  putPatch(b, s.edit);
  putName(b, s.editName);
  putPatch(b, s.stored);
  b.push_back(static_cast<uint8_t>((s.edited ? 1 : 0) | (s.manual ? 2 : 0)));
  b.push_back(static_cast<uint8_t>(s.currentSlot));
  b.push_back(static_cast<uint8_t>(s.pendingBank));
  b.push_back(static_cast<uint8_t>(s.pendingGroup));
  for (int p = 0; p < P_COUNT; ++p) putU16(b, s.perf.v[p] & 0xFFFF);
  for (int p = 0; p < S_COUNT; ++p) putU16(b, s.settings.v[p] & 0xFFFF);
  putU32(b, crc32(b.data(), b.size()));
  return b;
}

BlobResult decodeState(const uint8_t* data, std::size_t len, CoreState& out) {
  if (len == 0) return BlobResult::Empty;
  if (len < 6 + 4) return BlobResult::Invalid;
  if (data[0] != 'V' || data[1] != 'J' || data[2] != 'N' || data[3] != '6') return BlobResult::Invalid;
  const uint32_t stored = static_cast<uint32_t>(data[len - 4]) | (static_cast<uint32_t>(data[len - 3]) << 8) |
                          (static_cast<uint32_t>(data[len - 2]) << 16) | (static_cast<uint32_t>(data[len - 1]) << 24);
  if (crc32(data, len - 4) != stored) return BlobResult::Invalid;
  const int version = data[4] | (data[5] << 8);
  if (version < 1) return BlobResult::Invalid;
  if (version == 1 && len != 6 + kV1Payload + 4) return BlobResult::Invalid;
  if (version > 1 && len < 6 + kV1Payload + 4) return BlobResult::Invalid;
  Reader r{data + 6, len - 10};
  CoreState s = out;
  for (int i = 0; i < kNumSlots; ++i) {
    r.patch(s.bank[i].patch);
    r.name(s.bank[i].name);
  }
  r.patch(s.edit);
  r.name(s.editName);
  r.patch(s.stored);
  const int flags = r.u8();
  s.edited = (flags & 1) != 0;
  s.manual = (flags & 2) != 0;
  s.currentSlot = r.u8();
  s.pendingBank = r.u8();
  s.pendingGroup = r.u8();
  if (s.currentSlot < 1 || s.currentSlot > kNumSlots || s.pendingBank < 1 || s.pendingBank > 8 ||
      s.pendingGroup < 0 || s.pendingGroup > 1)
    r.ok = false;
  for (int p = 0; p < P_COUNT; ++p) {
    const int v = r.s16();
    int lo, hi;
    perfRange(p, lo, hi);
    if (v < lo || v > hi) r.ok = false;
    s.perf.v[p] = static_cast<int16_t>(v);
  }
  SettingsValues set;
  for (int p = 0; p < S_COUNT; ++p) {
    const int v = r.s16();
    int lo, hi;
    settingRange(p, lo, hi);
    if (v < lo || v > hi) r.ok = false;
    set.v[p] = static_cast<int16_t>(v);
  }
  if (!r.ok) return BlobResult::Invalid;
  if (version == 1) s.settings = set;
  out = s;
  return version == 1 ? BlobResult::Ok : BlobResult::OkNewer;
}

}  // namespace vjn6
