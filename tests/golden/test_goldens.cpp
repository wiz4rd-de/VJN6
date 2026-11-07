// AC-E1.3: 48 renders (the 8 original default-bank patches x both modes x chorus off / I / II) at 48 kHz,
// note 60 held 1 s and released 1 s, variation 50 %, bit-identical to the committed hashes in
// tests/golden/renders.json. The originals are selected by name (slots 58-65 since v3.2, §15.3; names are
// matched exactly, the factory `Brass` in slot 7 is a different patch). Regenerate only with an intended
// change:
//   VJN6_UPDATE_GOLDENS=1 ./vjn6_tests "[golden]"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "state/state.h"
#include "util/analysis.h"
#include "util/harness.h"
#include "util/json.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
const char* const kOriginals[8] = {"BRASS", "STRINGS", "PAD", "BASS", "PLUCK", "ORGAN", "SWEEP", "SFX"};

const Slot* defaultSlotNamed(const std::string& name) {
  for (int i = 0; i < kNumSlots; ++i)
    if (defaultBank()[i].name.s == name) return &defaultBank()[i];
  return nullptr;
}

std::string renderHash(const Patch& patch, Mode m, int chorus) {
  const double fs = 48000.0;
  Rig r(fs, m, 2);
  r.st().edit = patch;
  r.st().edit.v[W_CHORUS] = static_cast<uint8_t>(chorus);
  r.st().settings.v[S_VARIATION] = 50;
  r.noteOn(60);
  auto on = r.render(48000);
  r.noteOff(60);
  auto off = r.render(48000);
  std::vector<uint8_t> bytes;
  bytes.reserve(4 * 2 * 96000);
  auto put = [&](const std::vector<float>& v) {
    for (float f : v) {
      uint8_t b[4];
      std::memcpy(b, &f, 4);
      bytes.insert(bytes.end(), b, b + 4);
    }
  };
  put(on.L);
  put(off.L);
  put(on.R);
  put(off.R);
  return sha256(bytes.data(), bytes.size());
}

std::string goldenPath() { return std::string(VJN6_SOURCE_DIR) + "/tests/golden/renders.json"; }
}  // namespace

TEST_CASE("AC-E1.3 golden renders are bit-identical", "[golden][E1]") {
  std::map<std::string, std::string> got;
  std::vector<std::string> order;
  for (const char* name : kOriginals) {
    const Slot* slot = defaultSlotNamed(name);
    INFO(name);
    REQUIRE(slot);
    for (int mi = 0; mi < 2; ++mi) {
      for (int chorus = 0; chorus < 3; ++chorus) {
        const Mode m = mi ? Mode::J106 : Mode::J60;
        std::string id = std::string(slot->name.s) + (mi ? "-106" : "-60") +
                         (chorus == 0 ? "-off" : chorus == 1 ? "-I" : "-II");
        got[id] = renderHash(slot->patch, m, chorus);
        order.push_back(id);
      }
    }
  }
  REQUIRE(order.size() == 48);
  const char* update = std::getenv("VJN6_UPDATE_GOLDENS");
  if (update && std::string(update) == "1") {
    std::string o = "{\n  \"renders\": [\n";
    for (std::size_t i = 0; i < order.size(); ++i) {
      o += "    {\"id\": \"" + order[i] + "\", \"sha256\": \"" + got[order[i]] + "\"}";
      o += i + 1 < order.size() ? ",\n" : "\n";
    }
    o += "  ]\n}\n";
    std::ofstream out(goldenPath(), std::ios::binary);
    out << o;
    WARN("golden hashes written to " << goldenPath());
    return;
  }
  std::ifstream in(goldenPath(), std::ios::binary);
  REQUIRE(in.good());
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  json::JValue root;
  json::JParser p(text.data(), text.size());
  REQUIRE(p.parse(root));
  const json::JValue* arr = root.get("renders");
  REQUIRE(arr);
  REQUIRE(arr->arr.size() == 48);
  for (const auto& e : arr->arr) {
    const std::string id = e.get("id")->s, h = e.get("sha256")->s;
    INFO(id);
    REQUIRE(got.count(id) == 1);
    CHECK(got[id] == h);
  }
}
