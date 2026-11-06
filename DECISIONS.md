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
  V_sat·tanh is linearised by its secant gain from the previous sample; the feedback tanh is solved with
  Newton–Raphson from a secant initial estimate, 2 iterations at 2×, 1 at 4× (§14 recommendation).
- **Rational tanh.** [9/8] Padé approximant clamped at |x| ≥ 6.3, max error 6.8e−6 (≤ 1e−4, §9.1); the
  same code natively and in WASM.
- **Deterministic math.** Own exp2/log2/exp/sin/cos/tan (range reduction + polynomials using only
  + − × ÷, sqrt, floor, ldexp, frexp), compiled with `-ffp-contract=off`, no fast-math (MSVC `/fp:precise
  /fp:contract-`). Coefficient computations use them too, so renders are bit-identical everywhere.
- **Band-limiting (R-E3, AC-E3.6).** Table BLEP: Kaiser-windowed sinc kernel, 8 samples long, cutoff
  0.45·fo, β = 6 (−0.11 dB at 0.25·fo, ≥ 65 dB rejection above 0.7·fo), 256 phases, linear
  interpolation; the DCO output is delayed by 4 oversampled samples. All discontinuities (saw reset, pulse
  edges, sub toggle) are corrected.
- **Half-band filters.** Kaiser half-band FIRs: 135 taps, β 10.5 (≥ 104 dB stop band from 0.55·fs,
  ripple 5e−5 dB) for 2fs↔fs; 31 taps, β 10.5 (≥ 101 dB) for 4fs↔2fs. Latency = decimator group delay
  rounded half up: 2× 33.5 → **34**, 4× 37.25 → **37** host samples.
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
- **Standalone MIDI inputs** arrive merged through JUCE's device manager and count as one MIDI source.

## CI (owner rule, ~/.claude/CLAUDE.md)

- The full §15.1 matrix runs on pull requests into `main` (opened, reopened, synchronize) and
  `workflow_dispatch` only; after a merge to `main` a smoke-only job runs. No workflow triggers on pushes
  to development branches or on PRs into `develop/**` / `epic/**`. Per-slice verification uses the local
  gates. This narrows §15.1 ("CI runs every §13 criterion") to the PR-into-main run.
