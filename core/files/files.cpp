#include "vjn6/files.h"

#include <algorithm>
#include <cstring>

#include "files/bulk.h"
#include "util/json.h"
#include "sysex/sysex.h"

namespace vjn6::files {

// ================================================================================================
// NFC (Unicode canonical composition), table-driven
namespace {
#include "files/nfc_tables.inc"

constexpr uint32_t SBase = 0xAC00, LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7;
constexpr uint32_t LCount = 19, VCount = 21, TCount = 28, NCount = VCount * TCount, SCount = LCount * NCount;

int cccOf(uint32_t cp) {
  const CccEntry* b = kCcc;
  const CccEntry* e = kCcc + sizeof(kCcc) / sizeof(kCcc[0]);
  const CccEntry* it = std::lower_bound(b, e, cp, [](const CccEntry& x, uint32_t v) { return x.cp < v; });
  return (it != e && it->cp == cp) ? it->ccc : 0;
}

const DecompEntry* decompOf(uint32_t cp) {
  const DecompEntry* b = kDecomp;
  const DecompEntry* e = kDecomp + sizeof(kDecomp) / sizeof(kDecomp[0]);
  const DecompEntry* it = std::lower_bound(b, e, cp, [](const DecompEntry& x, uint32_t v) { return x.cp < v; });
  return (it != e && it->cp == cp) ? it : nullptr;
}

uint32_t composeOf(uint32_t a, uint32_t b) {
  if (a >= LBase && a < LBase + LCount && b >= VBase && b < VBase + VCount)
    return SBase + ((a - LBase) * VCount + (b - VBase)) * TCount;
  if (a >= SBase && a < SBase + SCount && (a - SBase) % TCount == 0 && b > TBase && b < TBase + TCount)
    return a + (b - TBase);
  const CompEntry* bb = kComp;
  const CompEntry* e = kComp + sizeof(kComp) / sizeof(kComp[0]);
  const CompEntry* it = std::lower_bound(bb, e, std::make_pair(a, b), [](const CompEntry& x, std::pair<uint32_t, uint32_t> v) {
    return x.a < v.first || (x.a == v.first && x.b < v.second);
  });
  return (it != e && it->a == a && it->b == b) ? it->cp : 0;
}

void decompose(uint32_t cp, std::vector<uint32_t>& out) {
  if (cp >= SBase && cp < SBase + SCount) {
    const uint32_t s = cp - SBase;
    out.push_back(LBase + s / NCount);
    out.push_back(VBase + (s % NCount) / TCount);
    if (s % TCount) out.push_back(TBase + s % TCount);
    return;
  }
  const DecompEntry* d = decompOf(cp);
  if (!d) {
    out.push_back(cp);
    return;
  }
  decompose(d->a, out);
  if (d->b) decompose(d->b, out);
}

std::vector<uint32_t> decodeUtf8(const std::string& s) {
  std::vector<uint32_t> cps;
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  const std::size_t n = s.size();
  std::size_t i = 0;
  while (i < n) {
    const unsigned c = p[i];
    uint32_t cp;
    int extra;
    if (c < 0x80) {
      cp = c;
      extra = 0;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      cps.push_back(0xFFFD);
      ++i;
      continue;
    }
    bool ok = true;
    for (int k = 1; ok && k <= extra; ++k) {
      if (i + static_cast<std::size_t>(k) >= n || (p[i + k] & 0xC0) != 0x80) ok = false;
      else cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (ok && (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) {
      cps.push_back(0xFFFD);
      i += static_cast<std::size_t>(extra) + 1;
      continue;
    }
    if (!ok) {
      cps.push_back(0xFFFD);
      ++i;
      continue;
    }
    cps.push_back(cp);
    i += static_cast<std::size_t>(extra) + 1;
  }
  return cps;
}

std::vector<uint32_t> nfc(const std::vector<uint32_t>& in) {
  std::vector<uint32_t> d;
  d.reserve(in.size() * 2);
  for (uint32_t cp : in) decompose(cp, d);
  // canonical ordering: stable sort of non-starter runs by ccc
  for (std::size_t i = 0; i < d.size();) {
    if (cccOf(d[i]) == 0) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < d.size() && cccOf(d[j]) != 0) ++j;
    std::stable_sort(d.begin() + static_cast<long>(i), d.begin() + static_cast<long>(j),
                     [](uint32_t a, uint32_t b) { return cccOf(a) < cccOf(b); });
    i = j;
  }
  // canonical composition
  if (d.empty()) return d;
  std::vector<uint32_t> out;
  out.reserve(d.size());
  long starter = -1;
  int lastCcc = -1;
  for (uint32_t cp : d) {
    const int c = cccOf(cp);
    if (starter >= 0) {
      const bool blocked = lastCcc != -1 && (lastCcc == 0 || lastCcc >= c);
      if (!blocked) {
        const uint32_t comp = composeOf(out[static_cast<std::size_t>(starter)], cp);
        if (comp) {
          out[static_cast<std::size_t>(starter)] = comp;
          continue;
        }
      }
    }
    if (c == 0) {
      starter = static_cast<long>(out.size());
      lastCcc = -1;
    } else {
      lastCcc = c;
    }
    out.push_back(cp);
  }
  return out;
}
}  // namespace

std::string normaliseName(const std::string& utf8) {
  const std::vector<uint32_t> cps = nfc(decodeUtf8(utf8));
  std::string out;
  for (uint32_t cp : cps) {
    if (out.size() >= static_cast<std::size_t>(kNameMax)) break;
    out.push_back((cp >= 0x20 && cp <= 0x7E) ? static_cast<char>(cp) : '?');
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

Name toName(const std::string& utf8) { return makeName(normaliseName(utf8).c_str()); }

using json::JValue;
using json::JParser;

namespace {
const char* kSwitchKeys[kNumSwitches] = {"range", "pulse", "saw", "sub", "chorus", "pwm_src", "vcf_pol", "vca_mode", "hpf"};

bool intIn(const JValue* v, int lo, int hi, int& out) {
  if (!v || v->kind != JValue::Int || v->i < lo || v->i > hi) return false;
  out = static_cast<int>(v->i);
  return true;
}

// One patch object: name, sliders[16], switches{9}.
bool readPatch(const JValue& o, Patch& p, Name& n) {
  if (o.kind != JValue::Object) return false;
  const JValue* name = o.get("name");
  const JValue* sl = o.get("sliders");
  const JValue* sw = o.get("switches");
  if (!name || name->kind != JValue::String) return false;
  if (!sl || sl->kind != JValue::Array || sl->arr.size() != kNumSliders) return false;
  if (!sw || sw->kind != JValue::Object) return false;
  for (int i = 0; i < kNumSliders; ++i) {
    int v;
    if (!intIn(&sl->arr[static_cast<std::size_t>(i)], 0, 127, v)) return false;
    p.v[i] = static_cast<uint8_t>(v);
  }
  for (int i = 0; i < kNumSwitches; ++i) {
    int v;
    if (!intIn(sw->get(kSwitchKeys[i]), 0, 127, v)) return false;
    p.v[kNumSliders + i] = static_cast<uint8_t>(v);
  }
  n = toName(name->s);
  return true;
}

std::string lower(const std::string& s) {
  std::string r = s;
  for (char& c : r)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return r;
}

std::string stemOf(const std::string& fileName) {
  std::string base = fileName;
  const std::size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos) base = base.substr(slash + 1);
  const std::size_t dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
  return base;
}

void jsonString(std::string& out, const char* s) {
  out.push_back('"');
  for (const char* c = s; *c; ++c) {
    if (*c == '"' || *c == '\\') out.push_back('\\');
    out.push_back(*c);
  }
  out.push_back('"');
}

void writePatchBody(std::string& o, const Patch& p, const Name& n, const std::string& ind) {
  o += ind + "\"name\": ";
  jsonString(o, n.s);
  o += ",\n" + ind + "\"sliders\": [\n";
  for (int i = 0; i < kNumSliders; ++i) {
    o += ind + "  " + std::to_string(p.v[i]);
    o += i + 1 < kNumSliders ? ",\n" : "\n";
  }
  o += ind + "],\n" + ind + "\"switches\": {\n";
  for (int i = 0; i < kNumSwitches; ++i) {
    o += ind + "  \"" + kSwitchKeys[i] + "\": " + std::to_string(p.v[kNumSliders + i]);
    o += i + 1 < kNumSwitches ? ",\n" : "\n";
  }
  o += ind + "}\n";
}
}  // namespace

std::string patchJson(const Patch& p, const Name& n) {
  std::string o = "{\n  \"vjn6\": 1,\n";
  writePatchBody(o, p, n, "  ");
  o += "}\n";
  return o;
}

std::string bankJson(const Slot* bank) {
  std::string o = "{\n  \"vjn6\": 1,\n  \"patches\": [\n";
  for (int i = 0; i < kNumSlots; ++i) {
    o += "    {\n";
    writePatchBody(o, bank[i].patch, bank[i].name, "      ");
    o += i + 1 < kNumSlots ? "    },\n" : "    }\n";
  }
  o += "  ]\n}\n";
  return o;
}

std::vector<uint8_t> patchSyx(const Patch& p) {
  std::vector<uint8_t> v(24);
  sysex::encodeDump(p, 0x31, 0, 0, v.data());
  return v;
}

std::vector<uint8_t> bankSyx(const Slot* bank) {
  std::vector<uint8_t> v(24 * kNumSlots);
  for (int i = 0; i < kNumSlots; ++i) sysex::encodeDump(bank[i].patch, 0x30, 0, i, v.data() + 24 * i);
  return v;
}

std::string patchFileName(const Name& n, const char* ext) {
  std::string s;
  for (const char* c = n.s; *c; ++c) {
    const char ch = *c;
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                    ch == '_' || ch == ' ';
    s.push_back(ok ? ch : '_');
  }
  if (s.find_first_not_of('_') == std::string::npos) s = "patch";
  const std::string l = lower(s);
  static const char* reserved[] = {"con", "prn", "aux", "nul"};
  bool isReserved = false;
  for (const char* r : reserved)
    if (l == r) isReserved = true;
  if (l.size() == 4 && (l.compare(0, 3, "com") == 0 || l.compare(0, 3, "lpt") == 0) && l[3] >= '1' && l[3] <= '9')
    isReserved = true;
  if (isReserved) s += "_";
  return s + ext;
}

std::string shortenMiddle(const std::string& s, std::size_t max) {
  // counts code points
  std::vector<std::size_t> starts;
  for (std::size_t i = 0; i < s.size(); ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) starts.push_back(i);
  if (starts.size() <= max) return s;
  const std::size_t keep = max - 1;
  const std::size_t head = (keep + 1) / 2, tail = keep - head;
  return s.substr(0, starts[head]) + "\xE2\x80\xA6" + s.substr(starts[starts.size() - tail]);
}

Result parse(const uint8_t* data, std::size_t len, const std::string& fileName, Bulk& out) {
  out.clear();
  if (len == 0 || len > Bulk::kMaxFile || !data) return Result::Malformed;
  std::string ext;
  {
    std::string base = fileName;
    const std::size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) ext = lower(base.substr(dot));
  }
  if (ext == ".vjn6" || ext == ".vjn6bank") {
    JValue root;
    JParser jp(reinterpret_cast<const char*>(data), len);
    if (!jp.parse(root) || root.kind != JValue::Object) return Result::Malformed;
    int ver;
    if (!intIn(root.get("vjn6"), 1, 1, ver)) return Result::Malformed;
    if (ext == ".vjn6") {
      if (!readPatch(root, out.patch, out.name)) return Result::Malformed;
      out.kind = BulkKind::PatchToEdit;
      return Result::PatchLoad;
    }
    const JValue* pats = root.get("patches");
    if (!pats || pats->kind != JValue::Array || pats->arr.size() != kNumSlots) return Result::Malformed;
    for (int i = 0; i < kNumSlots; ++i)
      if (!readPatch(pats->arr[static_cast<std::size_t>(i)], out.bank[i].patch, out.bank[i].name)) return Result::Malformed;
    out.kind = BulkKind::Bank;
    return Result::BankLoad;
  }
  if (ext == ".syx") {
    for (std::size_t i = 0; i < len; ++i) {
      if (data[i] != 0xF0) continue;
      std::size_t j = i + 1;
      while (j < len && data[j] < 0x80) ++j;
      if (j >= len || data[j] != 0xF7) continue;  // not terminated: junk
      const std::size_t mlen = j - i + 1;
      if (sysex::isDump(data + i, static_cast<int>(mlen))) {
        SyxRecord r;
        std::memcpy(r.bytes, data + i, 24);
        if (out.msgs.size() < out.msgs.capacity()) out.msgs.push_back(r);
      } else if (sysex::isParam(data + i, static_cast<int>(mlen))) {
        if (data[i + 4] <= 0x11 && out.params.size() < out.params.capacity())
          out.params.push_back(ParamRecord{data[i + 4], data[i + 5]});
      }
      i = j;
    }
    if (out.msgs.empty()) {
      out.params.clear();
      return Result::Malformed;
    }
    if (out.msgs.size() == 1) {
      Patch p = initPatch();
      sysex::applyDump(p, out.msgs[0].bytes);
      out.patch = p;
      out.name = toName(stemOf(fileName));
      out.msgs.clear();
      out.kind = BulkKind::PatchToEdit;
      return Result::PatchLoad;
    }
    out.kind = BulkKind::SyxImport;
    return Result::SyxImport;
  }
  return Result::Malformed;
}

}  // namespace vjn6::files
