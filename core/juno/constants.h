// Engine constants (SPEC §7). Every (D) decided and (M) measured constant is one named value here,
// tagged with its provenance, so that a hardware measurement can replace it without touching logic
// (SPEC preface, §15.4). (SN) service-literature and (CA) circuit-analysis values are listed too, tagged,
// for completeness; they are never retuned. Retuned values are recorded in DECISIONS.md.
#pragma once

namespace vjn6::k {

// ---------------------------------------------------------------- R-E1 rates and control
constexpr int kControlStep60 = 32;          // spec: 60-mode control step in host samples (glossary)
constexpr double kTick106Ms = 4.2;          // (D) 106 control tick, emulator-derived
constexpr double kRc106TauS = 0.001;        // (D) 106 DAC S&H / CV smoothing, 1-pole RC tau
constexpr double kDecimatorStopDb = 100.0;  // spec: half-band stop-band attenuation (minimum)
constexpr double kCornerClampRatio = 0.45;  // spec: filter corners clamped to 0.45 x their rate

// ---------------------------------------------------------------- R-E2 levels (1.0 = 6 V peak)
constexpr double kSawPeak = 1.0;           // (SN) 12 Vp-p
constexpr double kPulsePeak = 1.0;         // (SN) 12 Vp-p
constexpr double kSubMax = 1.0;            // (D) sub maximum level, equals the saw
constexpr double kNoiseRms = 0.11;         // (SN, D) white noise RMS at NOISE = 10 (4 Vp-p, crest factor 3)
constexpr double kSelfOscPeak106 = 0.40;   // (SN) 4.8 Vp-p
constexpr double kSelfOscPeak60 = 0.33;    // (SN) 4 Vp-p
constexpr double kOutputRefDbfs = -24.0;   // (D) single-voice output reference

// ---------------------------------------------------------------- R-E3 DCO
constexpr double kClk106_16 = 1.0e6;       // (SN) 106 DCO clock for 16'
constexpr double kClk106_8 = 2.0e6;        // (SN) 8'
constexpr double kClk106_4 = 4.0e6;        // (SN) 4'
constexpr int kDivisorMin = 2;             // (SN) 8253 counter limits
constexpr int kDivisorMax = 65535;         // (SN)
constexpr double kDutyCentre = 0.50;       // (SN) 50 % at CV 0
constexpr double kDutySpan = 0.45;         // (SN) 95 % at max CV
constexpr double kPitchMinHz60 = 8.0;      // spec: 60-mode pitch clamp, lower end
constexpr double kDcBlockHz = 0.35;        // (CA 106: 10 uF into 44.9 kOhm; D: used for both modes)
constexpr double kRangeCents16 = -1200.0;  // spec
constexpr double kRangeCents4 = 1200.0;    // spec
constexpr double kTuneCentsPerStep = 50.0 / 64.0;  // (SN) +-50 cents

// ---------------------------------------------------------------- R-E4 HPF (CA, single schematic reading)
constexpr double kHpf60Pos1Hz = 122.0;     // (CA)
constexpr double kHpf60Pos2Hz = 269.0;     // (CA)
constexpr double kHpf60Pos3Hz = 571.0;     // (CA)
constexpr double kHpf106Pos2Hz = 236.0;    // (CA)
constexpr double kHpf106Pos3Hz = 754.0;    // (CA)
constexpr double kShelfPole1Hz = 59.4;     // (CA) 106 position 0 low shelf
constexpr double kShelfPole2Hz = 72.3;     // (CA)
constexpr double kShelfZero1Hz = 72.0;     // (CA)
constexpr double kShelfZero2Hz = 170.0;    // (CA)
constexpr double kShelfHfGainDb = 1.4;     // (CA) +1.4 dB at HF (+10.5 dB at DC follows)

// ---------------------------------------------------------------- R-E5 VCF
constexpr double kVsat = 1.06;             // (CA) stage saturation voltage in units
constexpr double kVfb = 0.59;              // (CA) feedback OTA saturation in units
constexpr double kKmax106 = 4.5;           // (D) resonance law maximum, 106 mode
constexpr double kKmax60 = 4.35;           // (D) resonance law maximum, 60 mode
constexpr double kResExponent = 1.5;       // (D) k = k_max * r^1.5
constexpr double kQComp = 0.23;            // (D) g_in = 1 + 0.23 k, fitted to KR-106 data
// c_cal: derived, measured on this solver (DECISIONS.md "c_cal"). Index [oversample == 4].
constexpr double kCcal106[2] = {1.126, 1.126};  // (derived) 106 mode, 2x / 4x
constexpr double kCcal60[2] = {1.092, 1.092};   // (derived) 60 mode, 2x / 4x
constexpr double kCutoffBase60Hz = 28.1;   // (SN/CA) 60 cutoff at FREQ 0, note 60
constexpr double kCutoffSpan60Oct = 10.5;  // (SN/CA) 60 FREQ slider span
constexpr double kEnvDepth60Oct = 10.0;    // (SN)
constexpr double kLfoDepth60Oct = 6.0;     // (SN)
constexpr double kBendVcf60Oct = 4.0;      // (SN)
constexpr int kW106PerByte = 128;          // (CA) cutoff word per FREQ byte
constexpr double kW106PerOct = 1143.0;     // (CA) counts per octave
constexpr double kEnvDepth106Oct = 14.2;   // (CA)
constexpr double kLfoDepth106Oct = 3.5;    // (SN)
constexpr double kBendVcf106Oct = 3.5;     // (SN)
constexpr double kCutoffBase106Hz = 5.53;  // (CA) fc at W = 0
constexpr int kWMax = 16383;               // (CA) 14-bit cutoff word
constexpr double kVcfMinHz = 5.0;          // spec: c_cal*fc lower clamp

// ---------------------------------------------------------------- R-E6 VCA
constexpr double kPnpKnee = 0.1;           // (CA) linear above 10 %
constexpr double kPnpSlope = 1.3;          // (CA) 26 dB per 10 % of envelope below the knee
constexpr double kVcaOff106 = 0.001;       // (CA) g = 0 below
constexpr double kGateRampS = 0.0015;      // (D) GATE ramp time

// ---------------------------------------------------------------- R-E7 envelope
constexpr double kAttackMin60 = 0.001;     // (SN)
constexpr double kAttackMax60 = 3.0;       // (SN)
constexpr double kDecRelMin60 = 0.002;     // (SN)
constexpr double kDecRelMax60 = 12.0;      // (SN)
constexpr double kAttackMin106 = 0.0015;   // (SN)
constexpr double kAttackMax106 = 3.0;      // (SN)
constexpr double kDecRelMin106 = 0.0015;   // (SN)
constexpr double kDecRelMax106 = 12.0;     // (SN)
constexpr double kEnvLawExponent = 2.0;    // (D) slider -> time square law
constexpr double kAttackTarget60 = 1.5;    // (D) IR3R01 attack charges toward 1.5, ends at 1.0
constexpr double kEnvTimeConstants = 5.0;  // (D) a slider time is five time constants (decay/release)
constexpr double kFreeLevel = 0.001;       // spec: voice free below this level in release

// ---------------------------------------------------------------- R-E8 LFO
constexpr double kLfoMin60Hz = 0.3;        // (SN)
constexpr double kLfoRatio60 = 66.7;       // (SN) 0.3 .. 20 Hz
constexpr double kLfoDelayMax60S = 2.0;    // (SN)
constexpr double kLfoDelayMax106S = 3.0;   // (SN)
constexpr double kLfoDelayRampS = 0.2;     // (D) delay fade-in ramp
// (D) 106 rate table at bytes 0, 12.7, ..., 127 (firmware table as read by one developer)
constexpr double kLfoTable106[11] = {0.04, 1.07, 1.99, 2.91, 3.84, 4.76, 6.26, 7.73, 10.95, 18.67, 29.76};
constexpr int kLfoPhaseBits106 = 32;       // format: >= 24-bit fixed-point phase
constexpr double kLfoQuant106 = 4095.0;    // (CA) 13-bit LFO output
constexpr double kLfoDcoCents60 = 300.0;   // (SN)
constexpr double kLfoDcoCents106 = 400.0;  // (SN)
// LFO depth taper: linear in cents (D) -- implemented as depth * cents, no further constant.

// ---------------------------------------------------------------- R-E9 chorus
constexpr double kChorusCentreMs = 3.3;    // (M) I and II
constexpr double kChorusSwingMs = 1.9;     // (M)
constexpr double kChorusRateIHz = 0.513;   // (M)
constexpr double kChorusRateIIHz = 0.863;  // (M)
constexpr double kChorusCentreI2Ms = 3.4;  // (M) I+II (60)
constexpr double kChorusSwingI2Ms = 0.2;   // (M)
constexpr double kChorusRateI2Hz = 9.5;    // (M)
// 106 uses the same chorus values as the 60 (D) -- no separate constants.
constexpr int kBbdStages = 256;            // (CA) MN3009
constexpr double kBbdGainDb = 2.3;         // (M)
constexpr double kInPoleHz = 7410.0;       // (M) input filter real pole
constexpr double kInBq1Hz = 9690.0;        // (M)
constexpr double kInBq1Q = 0.55;           // (M)
constexpr double kInBq2Hz = 10340.0;       // (M)
constexpr double kInBq2Q = 1.24;           // (M)
constexpr double kOutPoleHz = 28100.0;     // (M) output filter real pole
constexpr double kOutBq1Hz = 8870.0;       // (M)
constexpr double kOutBq1Q = 0.54;          // (M)
constexpr double kOutBq2Hz = 10380.0;      // (M)
constexpr double kOutBq2Q = 1.24;          // (M)
constexpr double kWetHpHz = 20.0;          // (D) wet-path high-pass
constexpr double kWetGain = 1.12;          // (M) G, output-referred wet/dry 1.46 / BBD gain
constexpr double kChorusNoiseOrigDbfs = -57.0;  // (M)
constexpr double kChorusNoiseHalfDbfs = -63.0;  // (D) HALF setting
constexpr double kNoiseShelfHz = 2000.0;   // (D)
constexpr double kNoiseShelfDb = 12.0;     // (D)

// ---------------------------------------------------------------- R-E10 output
constexpr double kOutputGain = 0.063;      // (D) -24 dB, the single-voice reference

// ---------------------------------------------------------------- R-E11 voices
constexpr int kVoices = 6;                 // spec
constexpr int kNoteLow = 24;               // (SN 106; D for 60)
constexpr int kNoteHigh = 108;             // (SN 106; D for 60)
constexpr double kPortaMaxS = 3.0;         // (D) portamento 3 s x (t/127)^2
constexpr double kPortaExponent = 2.0;     // (D)
constexpr double kBendDco60Cents = 700.0;  // (SN)
constexpr double kBendDco106Cents = 1200.0;  // (SN)
constexpr double kArpMinHz = 1.5;          // (SN)
constexpr double kArpRatio = 33.3;         // (SN) 1.5 .. 50 steps/s
constexpr double kArpGate = 0.5;           // (D) arpeggio gate length
constexpr int kArpClocksPerStep = 6;       // spec: every 6th F8
constexpr double kClockTimeoutS = 0.5;     // spec: MIDI clock running window

// ---------------------------------------------------------------- R-E12 variation
constexpr double kVarSawAmp = 0.04;        // (SN) 12 +- 1 Vp-p
constexpr double kVarDuty = 0.015;         // (SN) 47-52 %
constexpr double kVarCutoffCents = 15.0;   // (SN + D) trim tolerance plus drift
constexpr double kVarVcaDb = 0.5;          // (D)
constexpr double kVarEnvTime = 0.05;       // (D)
constexpr unsigned kSeedVariation = 0x564A4E36u;  // spec
constexpr unsigned kSeedDcoNoise = 0x4E4F4953u;   // spec
constexpr unsigned kSeedChorusL = 0x43484C31u;    // spec
constexpr unsigned kSeedChorusR = 0x43484C32u;    // spec

// ---------------------------------------------------------------- R-E13
constexpr double kDenormOffset = 1e-20;    // spec

// ---------------------------------------------------------------- R-M3 transmit
constexpr double kTxIntervalS = 0.010;     // spec: at most one message per parameter every 10 ms

}  // namespace vjn6::k
