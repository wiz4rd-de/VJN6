// Writes the R-P5 / §4.8 reference table used by the UI formatter test (AC-P5.1):
// every patch field and knob at every byte, both modes, with the contexts that change the text
// (PWM source for PWM, polarity for VCF ENV), plus every host value string: "host" in 106 mode and "host60"
// in 60 mode (they differ only in p_vca_level, v3.2 C-4).
#include <cstdio>
#include <string>

#include "util/json.h"
#include "vjn6/display.h"

using namespace vjn6;

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "tests/golden/display_table.json";
  std::string o = "{\n  \"fields\": [\n";
  bool first = true;
  for (int mode = 0; mode < 2; ++mode) {
    const Mode m = mode ? Mode::J106 : Mode::J60;
    for (int f = 0; f <= W_HPF; ++f) {
      if (f >= kNumSliders && f != W_HPF) continue;
      const int ctxCount = (f == B_DCO_PWM) ? 3 : (f == B_VCF_ENV ? 2 : 1);
      const int maxV = f == W_HPF ? 3 : 127;
      for (int ctx = 0; ctx < ctxCount; ++ctx) {
        Patch p = initPatch();
        if (f == B_DCO_PWM) p.v[W_PWM_SRC] = static_cast<uint8_t>(ctx);
        if (f == B_VCF_ENV) p.v[W_VCF_POL] = static_cast<uint8_t>(ctx);
        for (int v = 0; v <= maxV; ++v) {
          if (!first) o += ",\n";
          first = false;
          o += "    {\"mode\":" + std::to_string(mode) + ",\"field\":" + std::to_string(f) + ",\"ctx\":" +
               std::to_string(ctx) + ",\"byte\":" + std::to_string(v) + ",\"v\":";
          json::appendString(o, f == W_HPF ? std::to_string(v) : display::fieldV(f, v, m));
          o += ",\"engine\":";
          json::appendString(o, display::fieldEngineValue(f, v, m, p));
          o += "}";
        }
      }
    }
  }
  o += "\n  ],\n  \"knobs\": [\n";
  first = true;
  const int knobs[] = {P_VOLUME, P_TUNE, P_BEND_DCO, P_BEND_VCF, P_LFO_TRIG_SENS, P_PORTA_TIME, P_ARP_RATE};
  for (int mode = 0; mode < 2; ++mode) {
    for (int pk : knobs) {
      for (int v = 0; v <= 127; ++v) {
        if (!first) o += ",\n";
        first = false;
        o += "    {\"mode\":" + std::to_string(mode) + ",\"perf\":" + std::to_string(pk) + ",\"byte\":" +
             std::to_string(v) + ",\"v\":";
        json::appendString(o, pk == P_TUNE ? display::tuneV(v) : display::sliderV(v));
        o += ",\"engine\":";
        json::appendString(o, display::perfEngineValue(pk, v, mode ? Mode::J106 : Mode::J60));
        o += "}";
      }
    }
  }
  for (const Mode hm : {Mode::J106, Mode::J60}) {
    o += hm == Mode::J106 ? "\n  ],\n  \"host\": [\n" : "\n  ],\n  \"host60\": [\n";
    first = true;
    for (int i = 0; i < display::kNumHostParams; ++i) {
      const auto& hp = display::hostParam(i);
      for (int v = hp.minValue; v <= hp.maxValue; ++v) {
        if (!first) o += ",\n";
        first = false;
        o += "    {\"index\":" + std::to_string(i) + ",\"id\":\"" + hp.id + "\",\"value\":" + std::to_string(v) + ",\"text\":";
        json::appendString(o, display::hostValueString(i, v, hm));
        o += "}";
      }
    }
  }
  o += "\n  ]\n}\n";
  FILE* fp = std::fopen(out, "wb");
  if (!fp) return 1;
  std::fwrite(o.data(), 1, o.size(), fp);
  std::fclose(fp);
  std::printf("wrote %s\n", out);
  return 0;
}
