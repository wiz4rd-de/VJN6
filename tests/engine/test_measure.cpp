// Hidden measurement helpers ([.measure]): c_cal and self-oscillation characteristics on this solver.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>

#include "util/analysis.h"
#include "util/harness.h"

using namespace vjn6;
using namespace vjn6::test;

namespace {
struct SelfOsc {
  double freq, peak;
};
SelfOsc measureSelfOsc(double fs, Mode m, int os, int note) {
  Rig r(fs, m, os);
  r.selfOscPatch();
  r.toBoundary();
  r.noteOn(note);
  r.skip(static_cast<long>(3.0 * fs));
  // capture the VCF output of voice 0 (oversampled) and the engine output
  const long n = static_cast<long>(1.0 * fs);
  std::vector<double> vcf(static_cast<std::size_t>(n * os + 64));
  auto& H = r.eng().hooks;
  H.voice = -1;
  H.capVcfOut = vcf.data();
  H.osPos = 0;
  r.skip(n);
  H.capVcfOut = nullptr;
  vcf.resize(static_cast<std::size_t>(n * os));
  SelfOsc s;
  s.freq = frequency(vcf, fs * os);
  s.peak = peakAbs(vcf);
  return s;
}
}  // namespace

TEST_CASE("measure c_cal and self-oscillation", "[.measure]") {
  for (Mode m : {Mode::J60, Mode::J106}) {
    for (int os : {2, 4}) {
      for (double fs : {44100.0, 48000.0, 96000.0}) {
        const SelfOsc a = measureSelfOsc(fs, m, os, 60);
        const SelfOsc b = measureSelfOsc(fs, m, os, 84);
        std::printf("%s os%d fs%6.0f  n60 %.3f Hz (%+.2f c) pk %.3f   n84 %.3f Hz (%+.2f c) pk %.3f\n",
                    m == Mode::J106 ? "106" : " 60", os, fs, a.freq, cents(a.freq, 248.0), a.peak, b.freq,
                    cents(b.freq, 992.0), b.peak);
      }
    }
  }
}
