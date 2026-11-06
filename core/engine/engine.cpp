#include "engine/engine.h"

#include <cmath>

namespace vjn6 {

namespace {
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double sliderTime(double x, double tmin, double tmax) {
  return tmin + (tmax - tmin) * x * x;  // (D) square law, R-E7
}
inline double tri(double phase) { return 1.0 - 4.0 * std::fabs(phase - 0.5); }
// R-E6 106 PNP converter
inline double pnp(double e) {
  if (e >= k::kPnpKnee) return e;
  if (e < k::kVcaOff106) return 0.0;
  return e * dm::exp2(-k::kPnpSlope * (k::kPnpKnee - e) / k::kPnpKnee * dm::kLog2_10);
}
}  // namespace

Engine::Engine() {
  blep2_.build(k::kBlepHalfHost, k::kBlepCutoffHost, k::kBlepBeta, 2, 64);
  blep4_.build(k::kBlepHalfHost, k::kBlepCutoffHost, k::kBlepBeta, 4, 64);
}

int Engine::latencyFor(int os) {
  // decimator group delay in host samples, rounded half up: 2x 136/2 = 68; 4x 68 + 15/4 = 71.75 -> 72
  const double d = os == 4 ? 136.0 / 2.0 + 15.0 / 4.0 : 136.0 / 2.0;
  return static_cast<int>(std::floor(d + 0.5));
}

void Engine::allocate(int maxBlock) {
  maxBlock_ = maxBlock;
  const std::size_t n = static_cast<std::size_t>(maxBlock);
  noiseHost_.assign(n, 0.0);
  noise2_.assign(n * 2, 0.0);
  noiseOs_.assign(n * 4, 0.0);
  bus_.assign(n * 4, 0.0);
  bus2_.assign(n * 2, 0.0);
  dry_.assign(n, 0.0);
  mult_.assign(n * 4, 0.0);
  lfoPwm_.assign(n * 4, 0.0);
  decFinal_.design(273, 0.2375, 10.6);
  decFirst_.design(31, 10.5);
  intFinal_.design(135, 10.5);
  intFirst_.design(31, 10.5);
}

double Engine::ccal() const {
  const int idx = os_ == 4 ? 1 : 0;
  return mode_ == Mode::J106 ? k::kCcal106[idx] : k::kCcal60[idx];
}

int Engine::sw(int field) const {
  int v = patch_->v[field];
  if (v >= switchCount(field)) v = 0;  // out-of-range switch read as 0 (§4.1)
  if (mode_ == Mode::J106) {
    if (field == W_SUB) return 1;
    if (field == W_CHORUS && v == 3) return 1;
    if (field == W_PWM_SRC && v == 2) return 1;
  }
  return v;
}

void Engine::prepare(double fs, const Patch* patch, const PerfSettings* perf, const SettingsValues* settings) {
  fs_ = fs;
  patch_ = patch;
  perf_ = perf;
  settings_ = settings;
  os_ = settings->v[S_OVERSAMPLE] == 4 ? 4 : 2;
  fo_ = fs_ * os_;
  mode_ = settings->v[S_MODE] == 1 ? Mode::J106 : Mode::J60;
  tickNum_ = 42 * static_cast<int64_t>(std::floor(fs + 0.5));
  pos_ = 0;
  nextBoundary_ = 0;
  tick_ = 0;
  steps_ = 0;
  dn_ = k::kDenormOffset;
  lever_ = 0.0;
  push_ = false;
  lfoTrig_ = false;
  pedalDown_ = false;
  chorus_.prepare(fs);
  for (Voice& vc : v_) {
    vc.dc.setCutoff(k::kDcBlockHz, fo_);
  }
  setOversample(os_);
}

void Engine::setOversample(int os) {
  os_ = os == 4 ? 4 : 2;
  fo_ = fs_ * os_;
  latency_ = latencyFor(os_);
  blep_ = os_ == 4 ? &blep4_ : &blep2_;
  decFinal_.resetState();
  decFirst_.resetState();
  intFinal_.resetState();
  intFirst_.resetState();
  noiseRng_ = XorShift32(k::kSeedDcoNoise);
  lfoPhase60_ = 0.0;
  lfoPhase106_ = 0;
  lfoOut_ = 0.0;
  lfoPrev60_ = 0.0;
  delayGain_ = 1.0;
  delayActive_ = false;
  delayStart_ = -1;
  trigStart_ = -1;
  multCur_ = multVal_ = 1.0;
  multDelta_ = 0.0;
  lfoPwmVal_ = lfoPwmDelta_ = 0.0;
  chorus_.prepare(fs_);
  resetVoices();
  // keys and arpeggio
  nKeys_ = 0;
  arpCount_ = 0;
  arpPatternLen_ = 0;
  arpLastPitch_ = -1;
  arpNextTime_ = -1.0;
  arpOffTime_ = -1;
  arpSoundingNote_ = -1;
  arpWaitFirstClock_ = false;
  arpClockCount_ = 0;
  lastF8_ = prevF8_ = -1;
  lastClockedStep_ = -1;
  nextBoundary_ = pos_;  // control step right away
}

void Engine::resetVoices() {
  XorShift32 r(k::kSeedVariation);
  for (Voice& vc : v_) {
    vc.uSaw = r.nextBipolar();
    vc.uDuty = r.nextBipolar();
    vc.uCut = r.nextBipolar();
    vc.uVca = r.nextBipolar();
    vc.uEnv = r.nextBipolar();
  }
  for (Voice& vc : v_) {
    const uint32_t x = r.next();
    vc.phase = static_cast<double>(x >> 8) / 16777216.0;
    const uint32_t y = r.next();
    vc.sub = (y >> 31) ? 1.0 : -1.0;
  }
  for (Voice& vc : v_) {
    vc.key = 0;
    vc.note = 60;
    vc.held = false;
    vc.active = false;
    vc.hasPitch = false;
    vc.onOrder = 0;
    vc.velGain = 1.0;
    vc.stage = 0;
    vc.env = 0.0;
    vc.gate = 0.0;
    vc.envRc = vc.lfcRc = vc.pwmRc = 0.0;
    vc.lfcTarget = vc.pwmTarget = 0.0;
    vc.W = 0;
    vc.inc = vc.incNext = 0.0;
    vc.divisor = 0;
    vc.pulseHigh = vc.phase < 0.5;
    for (double& x : vc.ring) x = 0.0;
    vc.ringPos = 0;
    vc.baseInc60 = 0.0;
    vc.dc.s = 0.0;
    vc.dc.setCutoff(k::kDcBlockHz, fo_);
    vc.hp1.s = vc.hp2.s = 0.0;
    vc.vcf.reset();
    vc.cutCur = vc.cutDelta = 0.0;
    vc.porta = vc.portaStep = 0.0;
    vc.portaTicks = 0;
    vc.forceEnv = false;
    vc.forceVcaOpen = false;
  }
  orderCounter_ = 0;
  lastAssigned_ = -1;
}

// ------------------------------------------------------------------------------------------------
// control path

double Engine::voiceCutoffBase60(const Voice& vc) const {
  const double var = settings_->v[S_VARIATION] / 100.0;
  const int octT = (perf(P_OCT_TRANSPOSE) - 1) * 12;
  const double nk = vc.note + octT;
  const double lfo = lfoOut_ * delayGain_;
  double oct = k::kCutoffSpan60Oct * byte(B_VCF_FREQ) + k::kLfoDepth60Oct * byte(B_VCF_LFO) * lfo +
               k::kBendVcf60Oct * (perf(P_BEND_VCF) / 127.0) * lever_ +
               byte(B_VCF_KYBD) * (nk - 60.0) / 12.0;
  return dm::log2(k::kCutoffBase60Hz) + oct + vc.uCut * k::kVarCutoffCents * var / 1200.0 + dm::log2(ccal());
}

void Engine::updateEnvCoefficients(Voice& vc) {
  const double var = settings_->v[S_VARIATION] / 100.0;
  const double scale = 1.0 + vc.uEnv * k::kVarEnvTime * var;
  if (mode_ == Mode::J60) {
    const double TA = sliderTime(byte(B_ENV_A), k::kAttackMin60, k::kAttackMax60) * scale;
    const double TD = sliderTime(byte(B_ENV_D), k::kDecRelMin60, k::kDecRelMax60) * scale;
    const double TR = sliderTime(byte(B_ENV_R), k::kDecRelMin60, k::kDecRelMax60) * scale;
    const double tauA = TA / dm::log(3.0);
    const double tauD = TD / k::kEnvTimeConstants;
    const double tauR = TR / k::kEnvTimeConstants;
    vc.cA = 1.0 - dm::exp(-1.0 / (tauA * fo_));
    vc.cD = 1.0 - dm::exp(-1.0 / (tauD * fo_));
    vc.cR = 1.0 - dm::exp(-1.0 / (tauR * fo_));
  } else {
    const double dt = k::kTick106Ms * 1e-3;
    const double TA = sliderTime(byte(B_ENV_A), k::kAttackMin106, k::kAttackMax106) * scale;
    const double TD = sliderTime(byte(B_ENV_D), k::kDecRelMin106, k::kDecRelMax106) * scale;
    const double TR = sliderTime(byte(B_ENV_R), k::kDecRelMin106, k::kDecRelMax106) * scale;
    vc.incA106 = dt / TA;
    vc.mD106 = dm::exp(-k::kEnvTimeConstants * dt / TD);
    vc.mR106 = dm::exp(-k::kEnvTimeConstants * dt / TR);
  }
}

double Engine::arpPeriodSamples() const {
  const double f = k::kArpMinHz * dm::pow(k::kArpRatio, perf(P_ARP_RATE) / 127.0);
  return fs_ / f;
}

void Engine::controlStep() {
  dn_ = (steps_ & 1u) ? -k::kDenormOffset : k::kDenormOffset;
  ++steps_;
  // derived patch values shared by both modes
  const double r = byte(B_VCF_RES);
  const double kmax = mode_ == Mode::J106 ? k::kKmax106 : k::kKmax60;
  kfb_ = kmax * r * std::sqrt(r);  // k = k_max * r^1.5 (D)
  gin_ = 1.0 + k::kQComp * kfb_;
  const int cm = sw(W_CHORUS);
  if (cm != chorus_.mode()) chorus_.setMode(cm);
  chorus_.setNoise(settings_->v[S_CHORUS_NOISE]);
  // HPF coefficients
  const int hpf = sw(W_HPF);
  for (Voice& vc : v_) {
    if (mode_ == Mode::J60) {
      const double f = hpf == 1 ? k::kHpf60Pos1Hz : hpf == 2 ? k::kHpf60Pos2Hz : k::kHpf60Pos3Hz;
      vc.hp1.setCutoff(f, fo_);
    } else {
      if (hpf == 0) {
        vc.hp1.setCutoff(k::kShelfPole1Hz, fo_);
        vc.hp2.setCutoff(k::kShelfPole2Hz, fo_);
      } else {
        vc.hp1.setCutoff(hpf == 2 ? k::kHpf106Pos2Hz : k::kHpf106Pos3Hz, fo_);
      }
    }
    updateEnvCoefficients(vc);
  }
  if (mode_ == Mode::J60) {
    const int64_t next = (pos_ / k::kControlStep60 + 1) * k::kControlStep60;
    stepLen_ = static_cast<int>(next - pos_);
    computeControl60();
    nextBoundary_ = next;
  } else {
    tick106();
    // next grid tick strictly after pos_: P_k = ceil(k * tickNum / 10000)
    int64_t kk = (pos_ * 10000) / tickNum_ + 1;
    auto P = [&](int64_t q) { return (q * tickNum_ + 9999) / 10000; };
    while (P(kk) <= pos_) ++kk;
    while (kk > 1 && P(kk - 1) > pos_) --kk;
    tick_ = static_cast<uint64_t>(kk);
    nextBoundary_ = P(kk);
  }
}

static double delayGainAt(bool manual, bool trig, int64_t trigStart, bool active, int64_t start, int64_t at,
                          double D, double fs) {
  if (manual) {
    if (!trig) return 0.0;
    if (D <= 0.0) return 1.0;
    const double t = static_cast<double>(at - trigStart) / fs;
    return t >= D ? 1.0 : (t <= 0.0 ? 0.0 : t / D);
  }
  if (!active) return 1.0;
  const double t = static_cast<double>(at - start) / fs;
  if (t < D) return 0.0;
  const double u = (t - D) / k::kLfoDelayRampS;
  return u >= 1.0 ? 1.0 : u;
}

void Engine::computeControl60() {
  const int N = stepLen_ * os_;
  const double f = k::kLfoMin60Hz * dm::pow(k::kLfoRatio60, byte(B_LFO_RATE));
  lfoOut_ = tri(lfoPhase60_);
  lfoPhase60_ += f * stepLen_ / fs_;
  lfoPhase60_ -= std::floor(lfoPhase60_);
  const bool manual = perf(P_LFO_TRIG_MODE) == 1;
  delayGain_ = delayGainAt(manual, lfoTrig_, trigStart_, delayActive_, delayStart_, pos_,
                           k::kLfoDelayMax60S * byte(B_LFO_DELAY), fs_);
  if (!manual && delayActive_ && delayGain_ >= 1.0) delayActive_ = false;
  if (testForceLfo) {
    lfoOut_ = testLfoValue;
    delayGain_ = testDelayGain;
  }
  const double tune = (perf(P_TUNE) - 64) * k::kTuneCentsPerStep;
  const double bend = k::kBendDco60Cents * (perf(P_BEND_DCO) / 127.0) * lever_;
  const double lfoC = k::kLfoDcoCents60 * (delayGain_ * byte(B_DCO_LFO)) * lfoOut_;
  const double mult = dm::exp2((tune + bend + lfoC) / 1200.0);
  multCur_ = mult;
  multDeltaStep(N);
  // per voice
  const int range = sw(W_RANGE);
  const double rangeC = range == 0 ? k::kRangeCents16 : range == 2 ? k::kRangeCents4 : 0.0;
  const double octC = (perf(P_OCT_TRANSPOSE) - 1) * 1200.0;
  for (Voice& vc : v_) {
    const double cents = 100.0 * (vc.note - 69) + rangeC + octC;
    vc.baseInc60 = 440.0 * dm::exp2(cents / 1200.0) / fo_;
    const double target = voiceCutoffBase60(vc);
    vc.cutDelta = (target - vc.cutCur) / N;
  }
}

void Engine::multDeltaStep(int N) {
  multDelta_ = (multCur_ - multVal_) / N;
  lfoPwmDelta_ = (lfoOut_ - lfoPwmVal_) / N;
}

void Engine::tick106() {
  const double dt = k::kTick106Ms * 1e-3;
  // LFO: table rate, >= 24-bit fixed-point phase (32 bits here), 13-bit output
  {
    const double x = byte(B_LFO_RATE) * 10.0;
    int i = static_cast<int>(std::floor(x));
    if (i > 9) i = 9;
    const double fr = x - i;
    const double rate = k::kLfoTable106[i] + (k::kLfoTable106[i + 1] - k::kLfoTable106[i]) * fr;
    const double incd = std::floor(rate * dt * 4294967296.0 + 0.5);
    lfoPhase106_ += static_cast<uint32_t>(static_cast<uint64_t>(incd) & 0xFFFFFFFFull);
    const double ph = lfoPhase106_ / 4294967296.0;
    lfoOut_ = std::round(tri(ph) * k::kLfoQuant106) / k::kLfoQuant106;
  }
  delayGain_ = delayGainAt(false, false, -1, delayActive_, delayStart_, pos_, k::kLfoDelayMax106S * byte(B_LFO_DELAY), fs_);
  if (delayActive_ && delayGain_ >= 1.0) delayActive_ = false;
  if (testForceLfo) {
    lfoOut_ = testLfoValue;
    delayGain_ = testDelayGain;
  }
  const double lq = std::round(lever_ * 127.0) / 127.0;  // 8-bit lever read (CA)
  double depth = delayGain_ * byte(B_DCO_LFO) + (push_ ? perf(P_LFO_TRIG_SENS) / 127.0 : 0.0);
  if (depth > 1.0) depth = 1.0;
  const double lfoC = k::kLfoDcoCents106 * depth * lfoOut_;
  const double bendC = k::kBendDco106Cents * (perf(P_BEND_DCO) / 127.0) * lq;
  const double tuneC = (perf(P_TUNE) - 64) * k::kTuneCentsPerStep;
  const int kt = perf(P_KEY_TRANSPOSE);
  const int range = sw(W_RANGE);
  const double fclk = range == 0 ? k::kClk106_16 : range == 2 ? k::kClk106_4 : k::kClk106_8;
  const double F = patch_->v[B_VCF_FREQ];
  const double E = patch_->v[B_VCF_ENV], L = patch_->v[B_VCF_LFO], K = patch_->v[B_VCF_KYBD];
  const double pol = sw(W_VCF_POL) ? -1.0 : 1.0;
  const double bV = perf(P_BEND_VCF);
  const double var = settings_->v[S_VARIATION] / 100.0;
  const double lfoV = lfoOut_ * delayGain_;
  const double S = byte(B_ENV_S);
  const bool pwmLfo = sw(W_PWM_SRC) == 0;
  for (Voice& vc : v_) {
    // envelope (firmware, per tick)
    if (vc.forceEnv) {
      vc.env = vc.forcedEnv;
    } else if (vc.stage == 1) {
      vc.env += vc.incA106;
      if (vc.env >= 1.0) {
        vc.env = 1.0;
        vc.stage = 2;
      }
    } else if (vc.stage == 2) {
      vc.env = S + (vc.env - S) * vc.mD106 + dn_;
    } else if (vc.stage == 3) {
      vc.env = vc.env * vc.mR106 + dn_;
    }
    // portamento
    if (vc.portaTicks > 0) {
      vc.porta += vc.portaStep;
      if (--vc.portaTicks == 0) vc.porta = 0.0;
    }
    // pitch -> divisor
    const double cents = 100.0 * (vc.note - 69) + 100.0 * kt + tuneC + bendC + lfoC + 100.0 * vc.porta;
    const double f8 = 440.0 * dm::exp2(cents / 1200.0);
    double Nd = std::round((k::kClk106_8) / f8);
    if (Nd < k::kDivisorMin) Nd = k::kDivisorMin;
    if (Nd > k::kDivisorMax) Nd = k::kDivisorMax;
    vc.divisor = static_cast<int>(Nd);
    double f = fclk / Nd;
    const double fmax = k::kCornerClampRatio * fo_;
    if (f > fmax) f = fmax;
    vc.incNext = f / fo_;
    if (vc.inc <= 0.0) vc.inc = vc.incNext;
    // cutoff word
    const double nk = vc.note + kt + vc.porta;
    const double env = vc.env;
    const double w = std::round(128.0 * F + 1143.0 * (14.2 * (E / 127.0) * env * pol + 3.5 * (L / 127.0) * lfoV +
                                                     3.5 * (bV / 127.0) * lq + (K / 127.0) * (nk - 60.0) / 12.0));
    vc.W = static_cast<int>(clampd(w, 0.0, static_cast<double>(k::kWMax)));
    vc.lfcTarget = dm::log2(k::kCutoffBase106Hz) + vc.W / k::kW106PerOct + dm::log2(ccal()) +
                   vc.uCut * k::kVarCutoffCents * var / 1200.0;
    vc.pwmTarget = pwmLfo ? byte(B_DCO_PWM) * lfoOut_ : byte(B_DCO_PWM);
  }
}

// ------------------------------------------------------------------------------------------------
// notes and assignment

bool Engine::anyHeld() const {
  for (const Voice& vc : v_)
    if (vc.held) return true;
  return false;
}

Engine::KeyEntry* Engine::findKey(uint32_t key) {
  for (int i = 0; i < nKeys_; ++i)
    if (keys_[i].key == key) return &keys_[i];
  return nullptr;
}

void Engine::removeKey(uint32_t key) {
  for (int i = 0; i < nKeys_; ++i) {
    if (keys_[i].key == key) {
      for (int j = i + 1; j < nKeys_; ++j) keys_[j - 1] = keys_[j];
      --nKeys_;
      return;
    }
  }
}

int Engine::findVoiceForKey(uint32_t key) const {
  for (int i = 0; i < k::kVoices; ++i)
    if (v_[i].active && v_[i].key == key) return i;
  return -1;
}

int Engine::allocateVoice() {
  auto oldest = [&](bool wantHeld) {
    int best = -1;
    for (int i = 0; i < k::kVoices; ++i) {
      const Voice& vc = v_[i];
      if (!vc.active || vc.held != wantHeld) continue;
      if (best < 0 || vc.onOrder < v_[best].onOrder) best = i;
    }
    return best;
  };
  int idx = -1;
  const int km = perf(P_KEY_MODE);
  if (mode_ == Mode::J60) {
    for (int j = 1; j <= k::kVoices && idx < 0; ++j) {
      const int i = (lastAssigned_ + j + k::kVoices) % k::kVoices;
      if (!v_[i].active) idx = i;
    }
    if (idx < 0) idx = oldest(false);
    if (idx < 0) idx = oldest(true);
  } else if (km == 1) {  // POLY 2: releasing first, then free in rotary order, then earliest held
    idx = oldest(false);
    for (int j = 1; j <= k::kVoices && idx < 0; ++j) {
      const int i = (lastAssigned_ + j + k::kVoices) % k::kVoices;
      if (!v_[i].active) idx = i;
    }
    if (idx < 0) idx = oldest(true);
  } else {  // POLY 1
    for (int i = 0; i < k::kVoices && idx < 0; ++i)
      if (!v_[i].active) idx = i;
    if (idx < 0) idx = oldest(false);
    if (idx < 0) idx = oldest(true);
  }
  if (idx < 0) idx = 0;
  lastAssigned_ = idx;
  return idx;
}

void Engine::startNote(Voice& vc, uint32_t key, int note, int vel, bool glide) {
  const double oldPitch = vc.note + vc.porta;
  vc.key = key;
  vc.note = note;
  vc.held = true;
  vc.active = true;
  vc.onOrder = ++orderCounter_;
  vc.velGain = settings_->v[S_VELOCITY] ? std::sqrt(vel / 127.0) : 1.0;
  if (glide && vc.hasPitch && mode_ == Mode::J106 && perf(P_PORTA_ON)) {
    const double t = perf(P_PORTA_TIME) / 127.0;
    const double T = k::kPortaMaxS * t * t;
    const int ticks = static_cast<int>(std::floor(T / (k::kTick106Ms * 1e-3) + 0.5));
    vc.porta = oldPitch - note;
    if (ticks <= 0 || vc.porta == 0.0) {
      vc.porta = 0.0;
      vc.portaTicks = 0;
    } else {
      vc.portaTicks = ticks;
      vc.portaStep = -vc.porta / ticks;
    }
  } else {
    vc.porta = 0.0;
    vc.portaTicks = 0;
  }
  vc.hasPitch = true;
  vc.stage = 1;  // attack from the current level (R-E7)
  if (mode_ == Mode::J60) {
    const int range = sw(W_RANGE);
    const double rangeC = range == 0 ? k::kRangeCents16 : range == 2 ? k::kRangeCents4 : 0.0;
    const double cents = 100.0 * (note - 69) + rangeC + (perf(P_OCT_TRANSPOSE) - 1) * 1200.0;
    vc.baseInc60 = 440.0 * dm::exp2(cents / 1200.0) / fo_;
    vc.cutCur = voiceCutoffBase60(vc);
    vc.cutDelta = 0.0;
  }
}

void Engine::releaseVoice(Voice& vc) {
  if (!vc.active) return;
  vc.held = false;
  vc.stage = 3;
}

void Engine::assignNoteOn(uint32_t key, int note, int vel) {
  int vi = findVoiceForKey(key);
  if (vi < 0) vi = allocateVoice();
  startNote(v_[vi], key, note, vel, true);
}

void Engine::unisonUpdate(bool retrigger) {
  // the most recent still-held key
  const KeyEntry* top = nullptr;
  for (int i = 0; i < nKeys_; ++i) {
    const KeyEntry& e = keys_[i];
    if (!keyGate(e)) continue;
    if (!top || e.order > top->order) top = &e;
  }
  if (!top) {
    for (Voice& vc : v_) releaseVoice(vc);
    return;
  }
  if (!retrigger) return;
  for (Voice& vc : v_) startNote(vc, top->key, top->note, top->vel, true);
}

void Engine::noteOn(uint32_t key, int note, int vel) {
  // R-E8: the state before this note-on (a HOLD-set replacement below releases latched voices first)
  const bool idle = !anyHeld() && !arpRunning();
  KeyEntry* e = findKey(key);
  // HOLD (60): a new key while no keys are physically down replaces the HOLD set
  if (mode_ == Mode::J60 && holdOn_) {
    bool anyDown = false;
    for (int i = 0; i < nKeys_; ++i)
      if (keys_[i].down && keys_[i].key != key) anyDown = true;
    if (!anyDown) {
      for (int i = 0; i < nKeys_;) {
        KeyEntry& h = keys_[i];
        if (h.hold && h.key != key) {
          h.hold = false;
          if (!keyGate(h)) {
            const uint32_t hk = h.key;
            if (!arpOn()) {
              const int vi = findVoiceForKey(hk);
              if (vi >= 0) releaseVoice(v_[vi]);
            }
            removeKey(hk);
            continue;
          }
        }
        ++i;
      }
      e = findKey(key);
    }
  }
  if (!e) {
    if (nKeys_ >= 64) return;
    e = &keys_[nKeys_++];
    *e = KeyEntry{};
    e->key = key;
    e->note = note;
  }
  e->down = true;
  e->pedal = false;
  e->vel = vel;
  e->order = ++keyOrder_;
  if (mode_ == Mode::J60 && holdOn_ && !e->hold) {
    e->hold = true;
    e->holdOrder = e->order;
    // at most 6 notes: keep the newest 6
    int count = 0;
    for (int i = 0; i < nKeys_; ++i)
      if (keys_[i].hold) ++count;
    while (count > k::kVoices) {
      int oldest = -1;
      for (int i = 0; i < nKeys_; ++i)
        if (keys_[i].hold && (oldest < 0 || keys_[i].holdOrder < keys_[oldest].holdOrder)) oldest = i;
      keys_[oldest].hold = false;
      --count;
      if (!keyGate(keys_[oldest])) {
        const uint32_t ok = keys_[oldest].key;
        if (!arpOn()) {
          const int vi = findVoiceForKey(ok);
          if (vi >= 0) releaseVoice(v_[vi]);
        }
        removeKey(ok);
      }
    }
  }
  if (idle) {
    delayActive_ = true;
    delayStart_ = pos_;
  }
  if (arpOn()) {
    arpRebuild();
    return;
  }
  if (mode_ == Mode::J106 && perf(P_KEY_MODE) == 2) {
    unisonUpdate(true);
    return;
  }
  assignNoteOn(key, note, vel);
}

void Engine::noteOff(uint32_t key, int /*note*/) {
  KeyEntry* e = findKey(key);
  if (!e) return;
  e->down = false;
  if (pedalDown_) e->pedal = true;
  refreshKeyGate(key);
}

void Engine::refreshKeyGate(uint32_t key) {
  KeyEntry* e = findKey(key);
  if (!e) return;
  if (keyGate(*e)) return;
  const bool wasTop = [&] {
    for (int i = 0; i < nKeys_; ++i)
      if (keyGate(keys_[i]) && keys_[i].order > e->order) return false;
    return true;
  }();
  removeKey(key);
  if (arpOn()) {
    arpRebuild();
    return;
  }
  if (mode_ == Mode::J106 && perf(P_KEY_MODE) == 2) {
    unisonUpdate(wasTop);
    return;
  }
  const int vi = findVoiceForKey(key);
  if (vi >= 0) releaseVoice(v_[vi]);
}

void Engine::pedal(bool down) {
  pedalDown_ = down;
  if (down) return;
  uint32_t release[64];
  int n = 0;
  for (int i = 0; i < nKeys_; ++i) {
    if (keys_[i].pedal) {
      keys_[i].pedal = false;
      release[n++] = keys_[i].key;
    }
  }
  for (int i = 0; i < n; ++i) refreshKeyGate(release[i]);
}

void Engine::holdChanged(bool on) {
  if (on == holdOn_) return;
  holdOn_ = on;
  // HOLD is a 60-mode setting: in 106 mode it only takes effect after a switch to 60 mode, which starts
  // with an empty HOLD set (§4.2, F-12)
  if (mode_ != Mode::J60) return;
  if (on) {
    for (int i = 0; i < nKeys_; ++i)
      if (keys_[i].down) {
        keys_[i].hold = true;
        keys_[i].holdOrder = keys_[i].order;
      }
    return;
  }
  uint32_t release[64];
  int n = 0;
  for (int i = 0; i < nKeys_; ++i) {
    if (keys_[i].hold) {
      keys_[i].hold = false;
      release[n++] = keys_[i].key;
    }
  }
  for (int i = 0; i < n; ++i) refreshKeyGate(release[i]);
}

void Engine::allNotes(bool soundOff, uint32_t /*sourceMask*/) {
  for (Voice& vc : v_) {
    if (soundOff) {
      vc.held = false;
      vc.active = false;
      vc.stage = 0;
      vc.env = 0.0;
      vc.envRc = 0.0;
      vc.gate = 0.0;
    } else {
      releaseVoice(vc);
    }
  }
  nKeys_ = 0;
  pedalDown_ = false;
  arpCount_ = 0;
  arpPatternLen_ = 0;
  arpStopNote();
  arpNextTime_ = -1.0;
  arpWasRunning_ = false;
}

void Engine::releaseSource(uint32_t source) {
  // CC123 for the notes of one source; the whole HOLD set and the arpeggio are cleared (R-M1)
  auto match = [&](uint32_t s) { return source == kAllMidi ? (s != kSrcKeyboard && s != kSrcArp) : s == source; };
  for (int i = 0; i < nKeys_; ++i) {
    keys_[i].hold = false;
    if (match(sourceOf(keys_[i].key))) {
      keys_[i].down = false;
      keys_[i].pedal = false;
    }
  }
  if (arpOn()) {
    nKeys_ = 0;
    arpCount_ = 0;
    arpPatternLen_ = 0;
    arpStopNote();
    arpNextTime_ = -1.0;
    return;
  }
  uint32_t release[64];
  int n = 0;
  for (int i = 0; i < nKeys_; ++i)
    if (!keyGate(keys_[i])) release[n++] = keys_[i].key;
  for (int i = 0; i < n; ++i) {
    const int vi = findVoiceForKey(release[i]);
    removeKey(release[i]);
    if (mode_ == Mode::J106 && perf(P_KEY_MODE) == 2) continue;
    if (vi >= 0) releaseVoice(v_[vi]);
  }
  if (mode_ == Mode::J106 && perf(P_KEY_MODE) == 2) unisonUpdate(true);
}

void Engine::keyModeChanged() {
  for (Voice& vc : v_) {
    releaseVoice(vc);
    vc.hasPitch = false;
  }
  nKeys_ = 0;
}

void Engine::switchMode(Mode m) {
  for (Voice& vc : v_) {
    releaseVoice(vc);
    vc.hasPitch = false;
    vc.porta = 0.0;
    vc.portaTicks = 0;
  }
  nKeys_ = 0;  // keys still down do not re-sound; HOLD set cleared
  arpCount_ = 0;
  arpPatternLen_ = 0;
  arpStopNote();
  arpNextTime_ = -1.0;
  arpWasRunning_ = false;
  if (m == mode_) return;
  mode_ = m;
  if (m == Mode::J106) {
    // initialise the firmware-path CVs from the current analog state
    for (Voice& vc : v_) {
      vc.envRc = vc.env;
      vc.lfcRc = vc.cutCur;
    }
  } else {
    for (Voice& vc : v_) vc.cutCur = vc.lfcRc;
  }
  nextBoundary_ = pos_;
}

void Engine::setLfoTrig(bool held) {
  if (held && !lfoTrig_) trigStart_ = pos_;
  lfoTrig_ = held;
}

// ------------------------------------------------------------------------------------------------
// arpeggio (60 mode, R-E11, F-8)

void Engine::arpRebuild() {
  bool present[128] = {};
  for (int i = 0; i < nKeys_; ++i)
    if (keyGate(keys_[i])) present[keys_[i].note & 127] = true;
  const int before = arpCount_;
  arpCount_ = 0;
  for (int n = 0; n < 128; ++n)
    if (present[n]) arpNotes_[arpCount_++] = n;
  arpBuildPattern();
  if (before == 0 && arpCount_ > 0) {
    // notes arrive: the arpeggio starts from its first step
    arpLastPitch_ = -1;
    arpDir_ = perf(P_ARP_MODE) == 2 ? -1 : 1;
    const bool clockRunning = lastF8_ >= 0 && static_cast<double>(pos_ - lastF8_) < k::kClockTimeoutS * fs_;
    if (clockRunning) {
      arpWaitFirstClock_ = true;
      arpClockCount_ = 0;
      arpNextTime_ = static_cast<double>(lastF8_) + k::kClockTimeoutS * fs_;  // if the clock stops first
    } else {
      // The first step plays at this note-on's position, but through arpEvent() after every event of
      // this control boundary is applied, so a chord whose notes arrive one by one starts on its
      // first step (UP: lowest, DOWN: highest).
      arpWaitFirstClock_ = false;
      arpNextTime_ = static_cast<double>(pos_);
    }
  } else if (before > 0 && arpCount_ == 0) {
    arpStopNote();
    arpNextTime_ = -1.0;
    arpWaitFirstClock_ = false;
  }
}

int Engine::arpNextPitch() {
  if (arpPatternLen_ == 0) return -1;
  const int mode = perf(P_ARP_MODE);
  const int last = arpLastPitch_;
  auto above = [&](int x) {
    for (int i = 0; i < arpPatternLen_; ++i)
      if (arpPattern_[i] > x) return arpPattern_[i];
    return -1;
  };
  auto below = [&](int x) {
    for (int i = arpPatternLen_ - 1; i >= 0; --i)
      if (arpPattern_[i] < x) return arpPattern_[i];
    return -1;
  };
  const int lo = arpPattern_[0], hi = arpPattern_[arpPatternLen_ - 1];
  if (mode == 0) {  // UP
    if (last < 0) return lo;
    const int a = above(last);
    return a < 0 ? lo : a;
  }
  if (mode == 2) {  // DOWN
    if (last < 0) return hi;
    const int b = below(last);
    return b < 0 ? hi : b;
  }
  // U&D: keeps its direction, turns at the ends without repeating them
  if (last < 0) {
    arpDir_ = 1;
    return lo;
  }
  if (arpDir_ > 0) {
    const int a = above(last);
    if (a >= 0) return a;
    arpDir_ = -1;
    const int b = below(last);
    return b >= 0 ? b : lo;
  }
  const int b = below(last);
  if (b >= 0) return b;
  arpDir_ = 1;
  const int a = above(last);
  return a >= 0 ? a : hi;
}

void Engine::arpStopNote() {
  if (arpSoundingNote_ >= 0) {
    const uint32_t key = makeKey(kSrcArp, arpSoundingNote_);
    const int vi = findVoiceForKey(key);
    if (vi >= 0) releaseVoice(v_[vi]);
  }
  arpSoundingNote_ = -1;
  arpOffTime_ = -1;
}

void Engine::arpBuildPattern() {
  const int range = perf(P_ARP_RANGE) < 1 ? 1 : (perf(P_ARP_RANGE) > 3 ? 3 : perf(P_ARP_RANGE));
  bool pat[128] = {};
  for (int o = 0; o < range; ++o)
    for (int i = 0; i < arpCount_; ++i) {
      const int p = arpNotes_[i] + 12 * o;
      if (p <= 127) pat[p] = true;  // steps above 127 are skipped
    }
  arpPatternLen_ = 0;
  for (int n = 0; n < 128; ++n)
    if (pat[n]) arpPattern_[arpPatternLen_++] = n;
}

void Engine::arpStep(int64_t at) {
  arpStopNote();
  arpBuildPattern();  // a RANGE change takes effect at the next step
  const int pitch = arpNextPitch();
  if (pitch < 0) return;
  arpLastPitch_ = pitch;
  const uint32_t key = makeKey(kSrcArp, pitch);
  int vi = findVoiceForKey(key);
  if (vi < 0) vi = allocateVoice();
  startNote(v_[vi], key, pitch, 64, false);
  arpSoundingNote_ = pitch;
  const bool clockRunning = lastF8_ >= 0 && static_cast<double>(at - lastF8_) < k::kClockTimeoutS * fs_;
  double period = arpPeriodSamples();
  if (clockRunning && prevF8_ >= 0) period = static_cast<double>(k::kArpClocksPerStep) * static_cast<double>(lastF8_ - prevF8_);
  arpOffTime_ = static_cast<int64_t>(std::ceil(static_cast<double>(at) + period * k::kArpGate));
  if (arpOffTime_ <= at) arpOffTime_ = at + 1;
  arpLastStep_ = at;
  if (clockRunning) {
    lastClockedStep_ = at;
    const double a = static_cast<double>(lastF8_) + k::kClockTimeoutS * fs_;
    const double b = static_cast<double>(at) + arpPeriodSamples();
    arpNextTime_ = a > b ? a : b;
  } else {
    arpNextTime_ = static_cast<double>(at) + arpPeriodSamples();
  }
}

int64_t Engine::nextArpEvent() const {
  int64_t t = INT64_MAX;
  if (arpOffTime_ >= 0 && arpOffTime_ < t) t = arpOffTime_;
  // While waiting for the first F8 (notes appeared or FA received), arpNextTime_ holds the time at which
  // the clock counts as stopped and internal timing resumes.
  if (arpNextTime_ >= 0.0 && arpCount_ > 0) {
    const int64_t s = static_cast<int64_t>(std::ceil(arpNextTime_));
    if (s < t) t = s;
  }
  if (t < pos_) t = pos_;
  return t;
}

void Engine::arpEvent() {
  if (arpOffTime_ >= 0 && arpOffTime_ <= pos_) {
    const uint32_t key = makeKey(kSrcArp, arpSoundingNote_ < 0 ? 0 : arpSoundingNote_);
    const int vi = findVoiceForKey(key);
    if (vi >= 0) releaseVoice(v_[vi]);
    arpOffTime_ = -1;
    arpSoundingNote_ = -1;
  }
  if (arpNextTime_ >= 0.0 && arpCount_ > 0 && static_cast<int64_t>(std::ceil(arpNextTime_)) <= pos_) {
    const bool clockRunning = lastF8_ >= 0 && static_cast<double>(pos_ - lastF8_) < k::kClockTimeoutS * fs_;
    if (!clockRunning) {
      arpWaitFirstClock_ = false;  // internal timing (also when the clock stopped before the awaited F8)
      const double exact = arpNextTime_;
      arpStep(pos_);
      // internal timing keeps the exact fractional grid
      arpNextTime_ = exact + arpPeriodSamples();
      arpOffTime_ = static_cast<int64_t>(std::ceil(exact + arpPeriodSamples() * k::kArpGate));
      if (arpOffTime_ <= pos_) arpOffTime_ = pos_ + 1;
    } else {
      arpNextTime_ = static_cast<double>(lastF8_) + k::kClockTimeoutS * fs_;
    }
  }
}

void Engine::clockTick() {
  const bool wasRunning = lastF8_ >= 0 && static_cast<double>(pos_ - lastF8_) < k::kClockTimeoutS * fs_;
  prevF8_ = wasRunning ? lastF8_ : -1;
  lastF8_ = pos_;
  if (!arpOn() || arpCount_ == 0) return;
  if (!wasRunning) arpWaitFirstClock_ = true;
  if (arpWaitFirstClock_) {
    arpWaitFirstClock_ = false;
    arpClockCount_ = 0;
    arpStep(pos_);
    return;
  }
  if (++arpClockCount_ >= k::kArpClocksPerStep) {
    arpClockCount_ = 0;
    arpStep(pos_);
    return;
  }
  const double a = static_cast<double>(lastF8_) + k::kClockTimeoutS * fs_;
  const double b = lastClockedStep_ >= 0 ? static_cast<double>(lastClockedStep_) + arpPeriodSamples() : a;
  arpNextTime_ = a > b ? a : b;
}

void Engine::clockStart() {
  arpWaitFirstClock_ = true;
  arpClockCount_ = 0;
}

void Engine::arpChanged(bool on) {
  // a 60-mode setting: no effect in 106 mode; in 60 mode the arpeggio starts on the next note-on (§4.2, F-12)
  if (mode_ != Mode::J60) return;
  if (on) {
    // directly sounding notes become arpeggio notes
    for (Voice& vc : v_)
      if (vc.held && sourceOf(vc.key) != kSrcArp) releaseVoice(vc);
    arpCount_ = 0;
    arpRebuild();
  } else {
    arpStopNote();
    arpCount_ = 0;
    arpPatternLen_ = 0;
    arpNextTime_ = -1.0;
    // held keys sound again through the key assigner
    for (int i = 0; i < nKeys_; ++i)
      if (keyGate(keys_[i])) assignNoteOn(keys_[i].key, keys_[i].note, keys_[i].vel);
  }
}

// ------------------------------------------------------------------------------------------------
// rendering

void Engine::renderVoice(Voice& vc, int vi, int nOs, double* bus) {
  const BlepTable& bt = *blep_;
  const bool is106 = mode_ == Mode::J106;
  const double var = settings_->v[S_VARIATION] / 100.0;
  const double sawW = sw(W_SAW) ? (k::kSawPeak * (1.0 + vc.uSaw * k::kVarSawAmp * var)) : 0.0;
  const double pulseW = sw(W_PULSE) ? k::kPulsePeak : 0.0;
  const double subW = sw(W_SUB) ? k::kSubMax * byte(B_SUB_LEVEL) : 0.0;
  const double noiseW = byte(B_NOISE);
  const double dutyBase = k::kDutyCentre + vc.uDuty * k::kVarDuty * var;
  const int pwmSrc = sw(W_PWM_SRC);
  const double pwmAmt = byte(B_DCO_PWM);
  const double pol = sw(W_VCF_POL) ? -1.0 : 1.0;
  const double envOct = k::kEnvDepth60Oct * byte(B_VCF_ENV) * pol;
  const bool gateMode = sw(W_VCA_MODE) == 1;
  const double vcaGain = byte(B_VCA_LEVEL) * dm::dbToGain(vc.uVca * k::kVarVcaDb * var) * vc.velGain;
  const double sawJump = is106 ? -2.0 * sawW : 2.0 * sawW;
  const int hpf = sw(W_HPF);
  const bool hpFlat = is106 ? hpf == 1 : hpf == 0;
  const bool shelf = is106 && hpf == 0;
  const double shelfK = dm::dbToGain(k::kShelfHfGainDb);
  const double z1 = k::kShelfZero1Hz / k::kShelfPole1Hz, z2 = k::kShelfZero2Hz / k::kShelfPole2Hz;
  const double rcA = 1.0 - dm::exp(-1.0 / (k::kRc106TauS * fo_));
  const double gateStep = 1.0 / (k::kGateRampS * fo_);
  const double S = byte(B_ENV_S);
  const double incMin = k::kPitchMinHz60 / fo_, incMax = k::kCornerClampRatio;
  const double fcMax = k::kCornerClampRatio * fo_;
  const int iters = os_ == 4 ? 1 : 2;
  const double kfb = kfb_, gin = gin_;
  const double dn = dn_;
  EngineHooks& H = hooks;
  const bool hv = (H.voice < 0 || H.voice == vi);
  const bool capThis = H.voice == vi || (H.voice < 0 && vi == 0);
  const long base = H.osPos;

  for (int i = 0; i < nOs; ++i) {
    // ---- envelope and gate
    if (vc.forceEnv) {
      vc.env = vc.forcedEnv;
    } else if (!is106) {
      if (vc.stage == 1) {
        vc.env += (k::kAttackTarget60 - vc.env) * vc.cA + dn;
        if (vc.env >= 1.0) {
          vc.env = 1.0;
          vc.stage = 2;
        }
      } else if (vc.stage == 2) {
        vc.env += (S - vc.env) * vc.cD + dn;
      } else if (vc.stage == 3) {
        vc.env -= vc.env * vc.cR - dn;
      }
    }
    if (vc.held) {
      vc.gate += gateStep;
      if (vc.gate > 1.0) vc.gate = 1.0;
    } else {
      vc.gate -= gateStep;
      if (vc.gate < 0.0) vc.gate = 0.0;
    }
    double envV;
    if (is106) {
      vc.envRc += (vc.env - vc.envRc) * rcA + dn;
      envV = vc.envRc;
    } else {
      envV = vc.env;
    }
    // ---- pitch
    double inc;
    if (is106) {
      inc = vc.inc;
    } else {
      inc = vc.baseInc60 * mult_[i];
      if (inc < incMin) inc = incMin;
      if (inc > incMax) inc = incMax;
      vc.inc = inc;
    }
    // ---- duty
    double cv;
    if (is106) {
      vc.pwmRc += (vc.pwmTarget - vc.pwmRc) * rcA + dn;
      cv = vc.pwmRc;
    } else {
      cv = pwmSrc == 0 ? pwmAmt * lfoPwm_[i] : pwmSrc == 2 ? pwmAmt * vc.env : pwmAmt;
    }
    const double duty = dutyBase + k::kDutySpan * cv;
    // ---- DCO
    const uint32_t n = vc.ringPos;
    double p = vc.phase + inc;
    if (p >= 1.0) {
      if (vc.pulseHigh && p >= duty) {
        double fr = (p - duty) / inc;
        if (fr >= 1.0) fr = 0.999999;
        bt.add(vc.ring, n, fr, -2.0 * pulseW);
        vc.pulseHigh = false;
      }
      p -= 1.0;
      const double frR = p / inc;
      bt.add(vc.ring, n, frR, sawJump);
      const double ns = -vc.sub;
      bt.add(vc.ring, n, frR, (ns - vc.sub) * subW);
      vc.sub = ns;
      if (!vc.pulseHigh) {
        bt.add(vc.ring, n, frR, 2.0 * pulseW);
        vc.pulseHigh = true;
      }
      if (p >= duty) {
        double fr = (p - duty) / inc;
        if (fr < 0.0) fr = 0.0;
        bt.add(vc.ring, n, fr, -2.0 * pulseW);
        vc.pulseHigh = false;
      }
      if (is106) vc.inc = vc.incNext;  // counter reload: the new divisor takes effect (CA)
    } else {
      if (vc.pulseHigh && p >= duty) {
        double fr = (p - duty) / inc;
        if (fr >= 1.0) fr = 0.999999;
        if (fr < 0.0) fr = 0.0;
        bt.add(vc.ring, n, fr, -2.0 * pulseW);
        vc.pulseHigh = false;
      } else if (!vc.pulseHigh && p < duty) {
        bt.add(vc.ring, n, 0.0, 2.0 * pulseW);  // duty moved above the phase (fast PWM)
        vc.pulseHigh = true;
      }
    }
    vc.phase = p;
    const double saw = is106 ? (2.0 * p - 1.0) : (1.0 - 2.0 * p);
    const double pul = vc.pulseHigh ? 1.0 : -1.0;
    vc.ring[n & BlepTable::kMask] += sawW * saw + pulseW * pul + subW * vc.sub;
    const uint32_t o = (n - static_cast<uint32_t>(bt.delay())) & BlepTable::kMask;
    double x = vc.ring[o];
    vc.ring[o] = 0.0;
    vc.ringPos = n + 1;
    if (capThis && H.capDco) {
      H.capDco[(base + i) * 3 + 0] = saw;
      H.capDco[(base + i) * 3 + 1] = pul;
      H.capDco[(base + i) * 3 + 2] = vc.sub;
    }
    if (capThis && H.capInc) H.capInc[base + i] = inc;
    if (capThis && H.capPhase) H.capPhase[base + i] = p;
    x += noiseOs_[i] * noiseW;
    x = vc.dc.hp(x, dn);
    // ---- HPF
    if (hv && H.injHpfIn) x = H.injHpfIn[base + i];
    if (capThis && H.capHpfIn) H.capHpfIn[base + i] = x;
    if (!hpFlat) {
      if (shelf) {
        const double l1 = vc.hp1.lp(x, dn);
        const double y1 = (x - l1) + z1 * l1;
        const double l2 = vc.hp2.lp(y1, dn);
        x = shelfK * ((y1 - l2) + z2 * l2);
      } else {
        x = vc.hp1.hp(x, dn);
      }
    }
    if (hv && H.injVcfIn) x = H.injVcfIn[base + i];
    if (capThis && H.capVcfIn) H.capVcfIn[base + i] = x;
    // ---- VCF
    double lf;
    if (is106) {
      vc.lfcRc += (vc.lfcTarget - vc.lfcRc) * rcA + dn;
      lf = vc.lfcRc;
    } else {
      vc.cutCur += vc.cutDelta;
      lf = vc.cutCur + envOct * vc.env;
    }
    double fc = dm::exp2(lf);
    if (fc < k::kVcfMinHz) fc = k::kVcfMinHz;
    if (fc > fcMax) fc = fcMax;
    const double g = dm::tan(dm::kPi * fc / fo_);
    double y = vc.vcf.process(x, g, kfb, gin, iters, dn);
    if (hv && H.injVcfOut) y = H.injVcfOut[base + i];
    if (capThis && H.capVcfOut) H.capVcfOut[base + i] = y;
    if (capThis && H.capCut) H.capCut[base + i] = fc;
    // ---- VCA
    double gv;
    if (gateMode) {
      gv = vc.gate;
    } else {
      gv = is106 ? pnp(envV) : envV;
    }
    if (vc.forceVcaOpen) gv = 1.0;
    if (capThis && H.capEnv) H.capEnv[base + i] = vc.env;
    if (capThis && H.capEnvRc) H.capEnvRc[base + i] = envV;
    if (capThis && H.capVca) H.capVca[base + i] = gv;
    const double vout = y * gv * vcaGain;
    if (capThis && H.capVoiceOut) H.capVoiceOut[base + i] = vout;
    if (H.capAllVoices) H.capAllVoices[(base + i) * 6 + vi] = vout;
    bus[i] += vout;
  }
  // free detection (R-E11 glossary)
  if (vc.active && !vc.held) {
    if (gateMode ? vc.gate <= 0.0 : (vc.stage == 3 && vc.env < k::kFreeLevel)) vc.active = false;
  }
}

void Engine::renderSegment(float* L, float* R, int n) {
  const int nOs = n * os_;
  if (testSkipVoices) {
    for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
    pos_ += n;
    return;
  }
  for (int i = 0; i < n; ++i) {
    const double w = noiseRng_.nextBipolar() + noiseRng_.nextBipolar() + noiseRng_.nextBipolar();
    noiseHost_[i] = k::kNoiseRms * w;
    if (hooks.capNoiseHost) hooks.capNoiseHost[hooks.hostPos + i] = noiseHost_[i];
  }
  if (os_ == 2) {
    for (int i = 0; i < n; ++i) intFinal_.interpolate(noiseHost_[i], noiseOs_[2 * i], noiseOs_[2 * i + 1]);
  } else {
    for (int i = 0; i < n; ++i) intFinal_.interpolate(noiseHost_[i], noise2_[2 * i], noise2_[2 * i + 1]);
    for (int j = 0; j < 2 * n; ++j) intFirst_.interpolate(noise2_[j], noiseOs_[2 * j], noiseOs_[2 * j + 1]);
  }
  if (mode_ == Mode::J60) {
    for (int m = 0; m < nOs; ++m) {
      multVal_ += multDelta_;
      mult_[m] = multVal_;
      lfoPwmVal_ += lfoPwmDelta_;
      lfoPwm_[m] = lfoPwmVal_;
    }
  }
  for (int m = 0; m < nOs; ++m) bus_[m] = 0.0;
  for (int vi = 0; vi < k::kVoices; ++vi) renderVoice(v_[vi], vi, nOs, bus_.data());
  if (os_ == 2) {
    for (int i = 0; i < n; ++i) dry_[i] = decFinal_.decimate(bus_[2 * i], bus_[2 * i + 1]);
  } else {
    for (int j = 0; j < 2 * n; ++j) bus2_[j] = decFirst_.decimate(bus_[2 * j], bus_[2 * j + 1]);
    for (int i = 0; i < n; ++i) dry_[i] = decFinal_.decimate(bus2_[2 * i], bus2_[2 * i + 1]);
  }
  const double vol = perf(P_VOLUME) / 127.0;
  const double outGain = k::kOutputGain * vol * vol;
  const bool mono = settings_->v[S_MONO] != 0;
  EngineHooks& H = hooks;
  for (int i = 0; i < n; ++i) {
    double x = dry_[i];
    if (H.capDry) H.capDry[H.hostPos + i] = x;
    if (H.injChorusIn) x = H.injChorusIn[H.hostPos + i];
    if (H.capChorusIn) H.capChorusIn[H.hostPos + i] = x;
    chorus_.hookPos = H.hostPos + i;
    double wl, wr;
    chorus_.process(x, wl, wr, dn_);
    const double d = H.muteDry ? 0.0 : x;
    double l = (d + wl) * outGain;
    double r = (d + wr) * outGain;
    if (mono) l = r = 0.5 * (l + r);
    if (l > 1.0) l = 1.0;
    if (l < -1.0) l = -1.0;
    if (r > 1.0) r = 1.0;
    if (r < -1.0) r = -1.0;
    L[i] = static_cast<float>(l);
    R[i] = static_cast<float>(r);
  }
  H.osPos += nOs;
  H.hostPos += n;
  pos_ += n;
}

}  // namespace vjn6
