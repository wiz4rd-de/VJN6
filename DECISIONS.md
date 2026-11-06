# Decisions

Implementation choices (SPEC §14 latitude), gaps the spec does not cover (KICKOFF rule: simplest
behaviour, no new state/setting/UI), conflicts and retuned constants. Newest entries are appended.

## Architecture

- **Public API additions (§9.4).** Besides the binding signatures, `Instrument` has `hostParam(index, value,
  offset)` (audio thread, plugin host automation; queued like MIDI so writes act at a control boundary),
  `snapshot()` (non-audio threads, latest published state), `acquireBulk()`/`bulk()` (preallocated payload
  pool for file and state loads) and `sampleRate()`. Why: host automation arrives on the audio thread,
  which must not become a second producer of the single-producer command queue.
- **Queues.** Commands and events: SPSC rings of 4096 entries; overflow drops the newest entry and counts
  it. Snapshot: triple buffer published by the audio thread at the end of every `render()` in which
  something visible changed; readers on other threads serialise on a mutex (never taken by the audio thread).
- **Bulk payloads.** Two preallocated slots, each sized for a 1 MiB file (43 691 dump records, 149 797
  parameter records) plus a full state; the producer parses (file formats, state blob) off the audio
  thread, the audio thread applies.
- **Fallback driver.** When no audio callback runs (standalone without a device, plugin host that does not
  process, web page before the first gesture or without audio), the host bridge drives the core with silent
  `render()` calls from one non-audio thread, guarded by an atomic flag so only one thread renders at a
  time. Why: §10 requires "no device → silent, everything else works" while §9.4 lets only one thread own
  the core.
- **Command timing.** Commands drained at the start of `render()` are stamped with that position and act at
  the first control boundary at or after it; MIDI and host writes carry their sample offset.
- **UI protocol.** One JSON protocol (docs/BRIDGE.md) implemented once in C++ (`core/bridge`) for the
  desktop and web hosts. Lever values travel as integers −8191…8191 so that no float parsing is needed.
- **Golden files (AC-E1.3)** are SHA-256 hashes of the float32 renders, not the 37 MB of raw audio.

## Sound engine

- **Filter solver (R-E5).** TPT integrators with prewarped g = tan(π·c_cal·fc/fo). Each stage's
  V_sat·tanh is linearised by its secant gain tanh(v)/v evaluated at the *linearly predicted* stage input
  2·v[n−1] − v[n−2]; the feedback tanh is solved with Newton–Raphson from a secant initial estimate, 2
  iterations at 2×, 1 at 4×. Why: with the plain previous-sample secant (§14's recommendation) the
  self-oscillation frequency depended on fc/fo (+9 cents at note 60 and +31 cents at note 84 at 44.1 kHz /
  2×), so no single c_cal could hold AC-E5.1; the predicted secant makes the lag second-order and the
  frequency independent of the rate to within ±0.6 cents.
- **Rational tanh.** [9/8] Padé approximant clamped at |x| ≥ 6.3, max error 6.8e−6 (≤ 1e−4, §9.1); the
  same code natively and in WASM.
- **Deterministic math.** Own exp2/log2/exp/sin/cos/tan (range reduction + polynomials using only
  + − × ÷, sqrt, floor, ldexp, frexp), compiled with `-ffp-contract=off`, no fast-math (MSVC `/fp:precise
  /fp:contract-`). Coefficient computations use them too, so renders are bit-identical everywhere.
- **Band-limiting (R-E3, AC-E3.6).** Table BLEP whose kernel is a Kaiser-windowed sinc defined in *host*
  samples: half-length 8 host samples, −6 dB point 0.47·fs, β = 6 (`constants.h` kBlep*), 64 phases per
  oversampled sample, linear interpolation; the DCO output is delayed by half the kernel. All
  discontinuities (saw reset, pulse edges, sub toggle) are corrected. Why host-rate: with a kernel band-
  limited only to 0.45·fo, the saturating VCF stages intermodulated the content between fs/2 and 0.45·fo
  and folded it back (−61 dB aliases at FREQ 127, 2×); band-limiting the DCO to about fs/2 leaves the
  oversampling as headroom for the filter's nonlinear products (measured aliases ≤ −95 dB).
- **Decimator.** Final 2fs→fs stage: symmetric Kaiser lowpass FIR, 273 taps, cutoff 0.2375 of 2fs,
  β 10.6 (ripple 4e−5 dB to 0.45·fs, ≥ 105 dB from fs/2). The 4fs→2fs stage is a 31-tap half-band
  (β 10.5, ≥ 101 dB from 1.45·fs). Why not a half-band for the final stage: a strict half-band is only
  −6 dB at fs/2, so DCO harmonics just above fs/2 folded back at −34…−52 dB (AC-E3.6); the spec's
  "half-band decimator" is read as "decimate-by-two stage with ≥ 100 dB stop band". Latency = decimator
  group delay: 2× **68**, 4× 71.75 → **72** host samples. The noise interpolators remain half-band (135 /
  31 taps); their images above fs/2 are removed by the decimator.
- **DCO noise (R-E2).** Each host sample is 0.11 × (u1 + u2 + u3) with three xorshift32 draws mapped as
  R-E12 (RMS 0.11, crest factor 3), then upsampled with the same half-band filters (flat to 0.45·fs).
- **60-mode control interpolation.** At each control boundary the new LFO / shared pitch multiplier /
  per-voice cutoff base are computed and reached linearly by the end of the step (one step of lag,
  0.67 ms at 48 kHz). A note-on sets its own voice's pitch and cutoff base immediately.
- **106 tick grid.** Tick k is at sample ceil(k·42·fs/10000) on the absolute counter (fs rounded to an
  integer). A switch into 106 mode runs one tick at the switch boundary to initialise the CVs, then follows
  the grid.
- **Oversampling change** restarts voices, LFO, chorus and the variation generator (like `prepare`) but keeps
  the absolute sample counter running so that queued events keep their positions.
- **Chorus (R-E9, BBD model).** Holters & Parker style: input/output filters as parallel continuous-time
  one-poles (impulse-invariant, DC-normalised input; exact staircase response on the output), clock ticks
  at fractional positions, 128-tick bucket FIFO, ZOH output. Chorus noise: per channel 3 summed uniform
  draws (seeds per R-E12) through a first-order +12 dB shelf at 2 kHz; its scale is computed at `prepare`
  from the chain's noise power gain so the output is exactly −57 / −63 dBFS.

- **c_cal (R-E5), measured on this solver** (self-osc patch, 3 s pre-roll, VCF-output zero crossings):
  ratio of self-oscillation frequency to small-signal cutoff 0.9160 (60) / 0.8882 (106), identical at 2×
  and 4× and at 44.1 / 48 / 96 kHz → **c_cal = 1.0917 (60), 1.1259 (106)**, one value per mode for both
  factors (`kCcal60`, `kCcal106`). Resulting self-oscillation: 248.01–248.02 Hz and 991.69–992.07 Hz,
  i.e. within −0.55…+0.16 cents of 248 / 992 Hz at every tested rate and factor. Steady amplitude 0.341
  (60) / 0.405 (106), matching the spec's reference simulation (0.341 / 0.405).
- **Chorus filter states** add the R-E13 offset to both real and imaginary parts, and the real pole's
  residue is forced real (a 1e−17 imaginary rounding residue otherwise decayed into subnormals).

## Measurement conventions in the tests

- "Saw peak" (AC-E2.1, AC-E10.1): a sawtooth peaks at its edge, so excluding 2 % of the period around the
  edge (§14) leaves 1 − 2·0.02. The tests fit a line to each ramp outside the excluded window and evaluate
  it at the edges (AC-E2.1), or take half the fitted ramp span, which is insensitive to the DC blocker's
  start-up offset (AC-E10.1).
- "Flat ± 1 dB" (AC-E5.2) means all levels fit in a 2 dB band (measured spread 1.46 dB at 2×, 0.28 dB at 4×).
- AC-E5.4's P is |H(fx)|/|H(20 Hz)| at fx, not the maximum of the response (at RES 64 the maximum lies
  below fx).
- AC-E9.2 compares output/input power spectra (Welch) rather than a cross spectrum: the BBD delay moves by
  up to 3.8 ms during the 10 s, which would decorrelate a cross-spectral estimate.
- AC-E13.2: allocations are counted with a replaced global operator new around render()/midi(); locks and
  logs are checked structurally (the audio-thread sources contain no lock or logging call).

## Performance and panel behaviour (gaps)

- **Hold pedal sources** (R-E11): MIDI CC64 (any accepted channel or port) and the Space key are OR'ed.
- **CC120 / CC123** act on the notes of every source (not only the sending channel).
- **ARPEGGIO on** releases the directly sounding notes (they become arpeggio notes); **off** sounds the
  still-held notes through the key assigner. Why: no new state, matches the hardware's key assigner.
- **Mode switch / key-mode change** forget the held keys: keys still down do not re-sound and their later
  note-offs are ignored (F-12).
- **PATCH LED in 106 mode** requires the pending group and bank to match the current slot (bank 1 of
  group A ≠ bank 1 of group B).
- **Host program selection** reports the changed parameters to the host like any other slot selection, so
  host parameter values always match the core.
- **File type** is decided by the extension (.vjn6, .vjn6bank, .syx, case-insensitive); anything else is `Er`.
- **NFC** uses tables generated from Python's `unicodedata` (`tools/gen_nfc_tables.py`).
- **Display rounding** (R-P5/R-P6): round half away from zero, digits assembled from the integer, identical
  in C++ (`core/panel/display.cpp`) and TypeScript.
- **Arpeggio start** (R-E11): when a chord arrives, the first step plays after all events of that control
  boundary are applied (so DOWN starts on the top note of the chord). A RANGE change acts like adding or
  removing notes: the next step plays the nearest note in the current direction. Arpeggio steps use
  velocity 64. If MIDI clock stops while the arpeggio waits for its first F8, internal timing resumes.
- **Performance writes for the other mode** (§4.2): HOLD or ARPEGGIO ON written in 106 mode (host, CC,
  overlay) are stored and take effect in 60 mode (HOLD starts with an empty HOLD set, the arpeggio at the
  next note-on); a key-mode write in 60 mode is stored without releasing voices.
- **UNISON velocity**: each held key keeps its own velocity (used when unison returns to it).
- **KEY TRANSPOSE LED** while armed with a non-zero transpose blinks (blinking wins over lit).
- **Patch dump after held-back parameter changes**: a `F0 41 30/31` sent while a rate-limited `F0 41 32`
  is still pending drops the pending one (the dump carries the value).
- **`.syx` import names**: in `MANUAL nnnn`, nnnn counts only the 30/31 patch messages of the file.
- **Standalone MIDI inputs** arrive merged through JUCE's device manager and count as one MIDI source.

## CI (owner rule, ~/.claude/CLAUDE.md)

- The full §15.1 matrix runs on pull requests into `main` (opened, reopened, synchronize) and
  `workflow_dispatch` only; after a merge to `main` a smoke-only job runs. No workflow triggers on pushes
  to development branches or on PRs into `develop/**` / `epic/**`. Per-slice verification uses the local
  gates. This narrows §15.1 ("CI runs every §13 criterion") to the PR-into-main run.
