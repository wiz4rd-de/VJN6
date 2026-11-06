// AC-P5.1 (core part): the 60 VCA LEVEL is a centre-zero slider (v3.2 C-4). The panel v, the typed
// entry and the host string follow the active mode; the byte, the gain law and the tooltip's dB do not.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

#include "util/analysis.h"
#include "util/harness.h"
#include "vjn6/display.h"

using namespace vjn6;
using namespace vjn6::test;

TEST_CASE("AC-P5.1 60 VCA LEVEL shows -5.0 / 0.0 / +5.0 on panel and host, dB unchanged; 106 unchanged", "[panel][P5]") {
  struct Case {
    int byte;
    const char *v60, *v106, *db;
  };
  const Patch p = initPatch();
  for (const Case& c : {Case{0, "-5.0", "0.0", "-inf dB"}, Case{64, "0.0", "5.0", "-6.0 dB"},
                        Case{127, "+5.0", "10.0", "+0.0 dB"}, Case{38, "-2.0", "3.0", "-10.5 dB"},
                        Case{63, "0.0", "5.0", "-6.1 dB"}, Case{65, "+0.1", "5.1", "-5.8 dB"}}) {
    INFO("byte " << c.byte);
    CHECK(display::fieldV(B_VCA_LEVEL, c.byte, Mode::J60) == c.v60);
    CHECK(display::fieldV(B_VCA_LEVEL, c.byte, Mode::J106) == c.v106);
    CHECK(display::hostValueString(B_VCA_LEVEL, c.byte, Mode::J60) == c.v60);    // R-P6 within the mode
    CHECK(display::hostValueString(B_VCA_LEVEL, c.byte, Mode::J106) == c.v106);
    CHECK(display::fieldEngineValue(B_VCA_LEVEL, c.byte, Mode::J60, p) == c.db);  // the law is unchanged
    CHECK(display::fieldEngineValue(B_VCA_LEVEL, c.byte, Mode::J106, p) == c.db);
  }
  // every other slider shows byte/12.7 in both modes
  for (int f = 0; f < kNumSliders; ++f) {
    if (f == B_VCA_LEVEL) continue;
    for (int b = 0; b <= 127; ++b) {
      CHECK(display::fieldV(f, b, Mode::J60) == display::sliderV(b));
      CHECK(display::fieldV(f, b, Mode::J106) == display::sliderV(b));
    }
  }
  // every byte: the shown v typed back commits a byte that shows the same v; never "-0.0"
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int b = 0; b <= 127; ++b) {
      const std::string v = display::fieldV(B_VCA_LEVEL, b, m);
      INFO((m == Mode::J60 ? "60 " : "106 ") << b << " " << v);
      CHECK(v != "-0.0");
      CHECK(v != "+0.0");
      int back = -1;
      REQUIRE(display::hostValueFromString(B_VCA_LEVEL, v, back, m));
      CHECK(display::fieldV(B_VCA_LEVEL, back, m) == v);
    }
  }
}

TEST_CASE("AC-U1.1 (core part) 60 VCA LEVEL typed entry commits round((v + 5) x 12.7), clamped", "[panel][U1]") {
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, -2.0, Mode::J60) == 38);
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, 5.0, Mode::J60) == 127);
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, 9.0, Mode::J60) == 127);
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, -9.0, Mode::J60) == 0);
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, 0.0, Mode::J60) == 64);   // 63.5 -> floor(64.0)
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, -5.0, Mode::J60) == 0);
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, 5.0, Mode::J106) == 64);  // 106: round(v x 12.7)
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, 12.0, Mode::J106) == 127);
  CHECK(display::fieldByteFromV(B_VCA_LEVEL, -2.0, Mode::J106) == 0);
  int v = -1;
  REQUIRE(display::hostValueFromString(B_VCA_LEVEL, "-2", v, Mode::J60));
  CHECK(v == 38);
  REQUIRE(display::hostValueFromString(B_VCA_LEVEL, "+5", v, Mode::J60));
  CHECK(v == 127);
  REQUIRE(display::hostValueFromString(B_VCA_LEVEL, "9", v, Mode::J60));
  CHECK(v == 127);
  REQUIRE(display::hostValueFromString(B_VCA_LEVEL, "5.0", v, Mode::J106));
  CHECK(v == 64);
  // the other sliders parse as before in 60 mode
  REQUIRE(display::hostValueFromString(B_VCF_FREQ, "-2", v, Mode::J60));
  CHECK(v == 0);
  REQUIRE(display::hostValueFromString(B_VCF_FREQ, "5.0", v, Mode::J60));
  CHECK(v == 64);
}

TEST_CASE("AC-P5.1 60 VCA LEVEL gain law unchanged: byte 64 is 0.504 of byte 127, byte 0 is silent", "[panel][P5][engine]") {
  double pk[3] = {};
  const int bytes[3] = {127, 64, 0};
  for (int i = 0; i < 3; ++i) {
    Rig r(48000.0, Mode::J60, 2);
    r.testPatch();
    r.st().edit.v[B_VCA_LEVEL] = static_cast<uint8_t>(bytes[i]);
    r.toBoundary();
    r.noteOn(60);
    r.skip(24000);
    const Stereo s = r.render(24000);
    pk[i] = peakAbs(s.L);
  }
  INFO("peaks " << pk[0] << " " << pk[1] << " " << pk[2]);
  CHECK(std::fabs(pk[1] / pk[0] - 64.0 / 127.0) < 1e-3);
  CHECK(pk[2] < 1e-9);  // -5 = silent (gain 0)
}
