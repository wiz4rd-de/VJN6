#include <catch2/catch_test_macros.hpp>
#include <cstdio>

#include "util/analysis.h"
#include "util/harness.h"

using namespace vjn6;
using namespace vjn6::test;

TEST_CASE("smoke: test patch note 60 renders near -24 dBFS", "[smoke]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    Rig r(48000.0, m);
    r.testPatch();
    r.toBoundary();
    r.noteOn(60);
    auto s = r.render(48000);
    const double pk = peakAbs(s.L, 24000);
    std::printf("mode %d peak %.4f (%.2f dBFS)\n", m == Mode::J106, pk, db(pk));
    CHECK(pk > 0.03);
  }
}
