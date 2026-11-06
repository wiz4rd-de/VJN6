// The VJN6 sound engine: six voices (DCO -> DC block -> HPF -> VCF -> VCA) at 2x/4x oversampling,
// half-band decimation, chorus and output stage, driven by the 60 (analog) or 106 (firmware)
// control path. Owned and called only by the audio thread (via Core).
#pragma once

#include <cstdint>
#include <vector>

#include "juno/chorus.h"
#include "juno/constants.h"
#include "juno/dsp.h"
#include "juno/vcf.h"
#include "vjn6/types.h"

namespace vjn6 {

struct Voice {
  // assignment
  uint32_t key = 0;        // source key of the sounding note, 0 = none
  int note = 60;           // incoming note (before transpose)
  bool held = false;       // gate on
  bool active = false;     // not free
  bool hasPitch = false;   // has played since start / prepare / key-mode change / mode switch
  uint64_t onOrder = 0;
  double velGain = 1.0;
  // variation draws u in [-1, 1) (R-E12), scaled by the setting when used
  double uSaw = 0, uDuty = 0, uCut = 0, uVca = 0, uEnv = 0;
  // envelope
  int stage = 0;           // 0 idle, 1 attack, 2 decay, 3 release
  double env = 0.0;
  double gate = 0.0;       // GATE ramp
  double cA = 0, cD = 0, cR = 0;              // 60: per-sample coefficients
  double incA106 = 0, mD106 = 0, mR106 = 0;   // 106: per-tick attack increment and multipliers
  // 106 held values and RC-smoothed CVs
  double envRc = 0, lfcRc = 0, pwmRc = 0;
  double lfcTarget = 0, pwmTarget = 0;
  int W = 0;
  // DCO
  double phase = 0.0;
  double inc = 0.0;        // phase increment (106: current cycle)
  double incNext = 0.0;    // 106: increment loaded at the next counter reload
  int divisor = 0;
  double sub = 1.0;
  bool pulseHigh = true;
  double ring[BlepTable::kRing] = {};
  uint32_t ringPos = 0;
  double baseInc60 = 0.0;  // 60: increment without the shared clock multiplier
  // filters
  OnePole dc, hp1, hp2;
  Vcf vcf;
  double cutCur = 0.0, cutDelta = 0.0;  // 60: interpolated log2 cutoff without the envelope
  // portamento (106), in semitones relative to the note
  double porta = 0.0, portaStep = 0.0;
  int portaTicks = 0;
  // test forcing (§9.4)
  bool forceEnv = false;
  double forcedEnv = 0.0;
  bool forceVcaOpen = false;
};

// Test-build injection and capture points (§9.4). All pointers null in normal operation.
struct EngineHooks {
  int voice = -1;                 // voice for per-voice points (-1 = all for injection, 0 for capture)
  const double* injHpfIn = nullptr;   // oversampled
  const double* injVcfIn = nullptr;
  const double* injVcfOut = nullptr;
  double* capDco = nullptr;           // oversampled, 3 per sample: saw, pulse, sub (raw, unscaled)
  double* capHpfIn = nullptr;
  double* capVcfIn = nullptr;
  double* capVcfOut = nullptr;
  double* capEnv = nullptr;           // oversampled: envelope before RC
  double* capEnvRc = nullptr;         // oversampled: envelope after RC (106) / same (60)
  double* capInc = nullptr;           // oversampled: phase increment
  double* capPhase = nullptr;         // oversampled: phase
  double* capCut = nullptr;           // oversampled: effective cutoff c_cal*fc in Hz
  double* capVca = nullptr;           // oversampled: VCA gain
  double* capVoiceOut = nullptr;      // oversampled: voice output after the VCA
  double* capNoiseHost = nullptr;     // host rate: the shared noise source before upsampling
  const double* injChorusIn = nullptr;  // host rate
  double* capChorusIn = nullptr;
  double* capDry = nullptr;           // host rate, decimated voice sum
  bool muteDry = false;
  long osPos = 0;                     // running index into oversampled buffers
  long hostPos = 0;                   // running index into host-rate buffers
  long capCapacityOs = 0, capCapacityHost = 0;
};

class Engine {
 public:
  Engine();
  // Allocates all buffers for maxBlock host samples at up to 4x.
  void allocate(int maxBlock);
  void prepare(double fs, const Patch* patch, const PerfSettings* perf, const SettingsValues* settings);
  void setOversample(int os);  // acts like prepare (voices stop), allocation-free
  int latencySamples() const { return latency_; }
  static int latencyFor(int os);
  Mode mode() const { return mode_; }

  // ---- timeline (absolute host-sample counter) ----
  int64_t now() const { return pos_; }
  int64_t nextBoundary() const { return nextBoundary_; }
  int64_t nextArpEvent() const;
  bool atBoundary() const { return pos_ == nextBoundary_; }
  void controlStep();          // at a control boundary: LFO, pitch, cutoff, 106 tick, delay gain
  void arpEvent();             // at nextArpEvent()
  void renderSegment(float* L, float* R, int n);

  // ---- note and performance input (applied at control boundaries) ----
  void noteOn(uint32_t key, int note, int vel);
  void noteOff(uint32_t key, int note);
  void pedal(bool down);
  void allNotes(bool soundOff, uint32_t sourceMask = 0xFFFFFFFFu);
  void releaseSource(uint32_t source);
  void holdChanged(bool on);
  void arpChanged(bool on);
  void keyModeChanged();
  void switchMode(Mode m);
  void setLever(double lever) { lever_ = lever; }
  void setPush(bool on) { push_ = on; }
  void setLfoTrig(bool held);
  void clockTick();            // MIDI F8
  void clockStart();           // MIDI FA
  bool anyHeld() const;
  bool arpRunning() const { return arpOn() && arpCount_ > 0; }

  // ---- test access ----
  Voice& voice(int i) { return v_[i]; }
  const Voice& voice(int i) const { return v_[i]; }
  EngineHooks hooks;
  Chorus& chorus() { return chorus_; }
  double lfoValue() const { return lfoOut_; }
  double delayGain() const { return delayGain_; }
  double ccal() const;
  double osRate() const { return fo_; }
  double hostRate() const { return fs_; }
  int oversample() const { return os_; }
  int blepDelay() const { return blep_->delay(); }
  uint64_t tickIndex() const { return tick_; }
  // Test-only: run only the control path (no audio processing at all) for long measurements.
  bool testSkipVoices = false;
  // Test-only: force the LFO triangle value and the delay gain after each control step.
  bool testForceLfo = false;
  double testLfoValue = 0.0, testDelayGain = 1.0;
  // Test-only: visits every recursive state variable (for the subnormal check, AC-E13.1).
  template <typename F>
  void visitState(F&& f) {
    for (Voice& vc : v_) {
      f(vc.env); f(vc.gate); f(vc.envRc); f(vc.lfcRc); f(vc.pwmRc); f(vc.dc.s); f(vc.hp1.s); f(vc.hp2.s);
      f(vc.cutCur); f(vc.porta);
      for (int i = 0; i < 4; ++i) { f(vc.vcf.s[i]); f(vc.vcf.tg[i]); f(vc.vcf.v1[i]); f(vc.vcf.v2[i]); }
      f(vc.vcf.y4); f(vc.vcf.tfb);
      for (double& x : vc.ring) f(x);
    }
    chorus_.visitState(f);
    f(multVal_); f(lfoPwmVal_);
  }
  double denormSign() const { return dn_; }

  static uint32_t sourceOf(uint32_t key) { return key >> 8; }
  static uint32_t makeKey(uint32_t source, int note) { return (source << 8) | static_cast<uint32_t>(note & 0x7F); }
  static constexpr uint32_t kSrcArp = 0xFFFFu;
  static constexpr uint32_t kSrcKeyboard = 0xFFF0u;
  static constexpr uint32_t kAllMidi = 0xFFFFFFFFu;  // releaseSource: every source except keyboard

 private:
  struct KeyEntry {
    uint32_t key = 0;
    int note = 0;
    bool down = false;
    bool pedal = false;   // released while CC64 down
    bool hold = false;    // in the HOLD set (60)
    uint64_t order = 0;   // press order
    uint64_t holdOrder = 0;
  };

  // effective patch readings for the active mode (§4.1)
  int sw(int field) const;
  double byte(int field) const { return patch_->v[field] / 127.0; }
  int perf(int p) const { return perf_->v[p]; }

  void resetVoices();
  void computeControl60();
  void tick106();
  void updateEnvCoefficients(Voice& vc);
  void startNote(Voice& vc, uint32_t key, int note, int vel, bool glide);
  void releaseVoice(Voice& vc);
  int findVoiceForKey(uint32_t key) const;
  int allocateVoice();
  void assignNoteOn(uint32_t key, int note, int vel);
  void multDeltaStep(int N);
  KeyEntry* findKey(uint32_t key);
  void removeKey(uint32_t key);
  bool keyGate(const KeyEntry& e) const { return e.down || e.pedal || e.hold; }
  void refreshKeyGate(uint32_t key);
  void unisonUpdate(bool retrigger);
  // arpeggio (60)
  bool arpOn() const { return mode_ == Mode::J60 && perf(P_ARP_ON) != 0; }
  void arpRebuild();
  void arpStep(int64_t at);
  int arpNextPitch();
  double arpPeriodSamples() const;
  void arpStopNote();

  double voiceCutoffBase60(const Voice& vc) const;  // log2 Hz incl. c_cal and variation
  void renderVoice(Voice& vc, int vi, int nOs, double* bus);

  // configuration
  double fs_ = 48000.0, fo_ = 96000.0;
  int os_ = 2;
  int latency_ = 34;
  Mode mode_ = Mode::J60;
  const Patch* patch_ = nullptr;
  const PerfSettings* perf_ = nullptr;
  const SettingsValues* settings_ = nullptr;
  int maxBlock_ = 0;

  // timeline
  int64_t pos_ = 0;
  int64_t nextBoundary_ = 0;
  uint64_t tick_ = 0;           // 106 tick index on the absolute counter
  int64_t tickNum_ = 0;         // 42 * fs (integer Hz) for exact tick positions
  uint64_t steps_ = 0;          // control boundaries so far (denormal offset sign)
  double dn_ = k::kDenormOffset;

  // voices
  Voice v_[k::kVoices];
  uint64_t orderCounter_ = 0;
  int lastAssigned_ = -1;

  // keys
  KeyEntry keys_[64];
  int nKeys_ = 0;
  uint64_t keyOrder_ = 0;
  bool pedalDown_ = false;
  bool holdOn_ = false;

  // global modulation
  double lever_ = 0.0;
  bool push_ = false;
  bool lfoTrig_ = false;
  double lfoPhase60_ = 0.0;
  uint32_t lfoPhase106_ = 0;
  double lfoOut_ = 0.0;         // current triangle (106: quantised), not delayed
  double lfoPrev60_ = 0.0;      // value at the previous boundary (interpolation start)
  double delayGain_ = 0.0;
  int64_t delayStart_ = -1;     // sample position of the delay restart, -1 = inactive (gain 1)
  bool delayActive_ = false;
  int64_t trigStart_ = -1;      // 60 MANUAL: LFO TRIG press position
  double multCur_ = 1.0, multVal_ = 1.0, multDelta_ = 0.0;  // 60: shared clock multiplier (interpolated)
  double lfoPwmVal_ = 0.0, lfoPwmDelta_ = 0.0;                // 60: undelayed LFO for PWM (interpolated)
  int stepLen_ = 32;            // host samples in the current control step (60)

  // derived patch values (recomputed at each boundary)
  double kfb_ = 0.0, gin_ = 1.0;

  // arpeggio
  int arpNotes_[128];
  int arpCount_ = 0;
  int arpPattern_[128 * 3];
  int arpPatternLen_ = 0;
  int arpLastPitch_ = -1;
  int arpDir_ = 1;
  bool arpWasRunning_ = false;
  double arpNextTime_ = -1.0;   // internal timing: next step (fractional samples), <0 none
  int64_t arpOffTime_ = -1;     // pending note-off
  int arpSoundingNote_ = -1;
  int64_t arpLastStep_ = -1;
  bool arpWaitFirstClock_ = false;
  int arpClockCount_ = 0;
  int64_t lastF8_ = -1, prevF8_ = -1;
  int64_t lastClockedStep_ = -1;

  // noise and resampling
  XorShift32 noiseRng_{k::kSeedDcoNoise};
  LowpassDecimator decFinal_;
  HalfBand decFirst_, intFinal_, intFirst_;
  std::vector<double> noiseHost_, noise2_, noiseOs_, bus_, bus2_, dry_;
  std::vector<double> mult_, lfoPwm_;
  Chorus chorus_;
  BlepTable blep2_, blep4_;
  const BlepTable* blep_ = &blep2_;

 public:
  // test-only: rebuild the BLEP tables with other kernel parameters
  void testBlep(double hostHalf, double cutoff, double beta) {
    blep2_.build(hostHalf, cutoff, beta, 2, 64);
    blep4_.build(hostHalf, cutoff, beta, 4, 64);
  }
};

}  // namespace vjn6
