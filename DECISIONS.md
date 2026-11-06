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

## UI (Svelte)

- **ARIA names / tooltip legends** use the panel legend prefixed by its section where the short legend
  repeats ("LFO RATE", "VCF FREQ", "ENV A", "BENDER DCO", "RANGE 16′", "GROUP A"); the panel shows the
  short legend under the section header (R-U3, R-P5).
- **Switch order**: named positions top to bottom in the spec's order (AUTO/MAN, LFO/MAN/ENV, +/−, ENV/GATE,
  UP/U&D/DOWN); graded positions put the highest at the top (HPF 3…0 per §5.2, OCTAVE U/N/D (H/N/L before v3.2 C-3), ARP RANGE 3/2/1);
  Up moves to the position visually above.
- **Drag/wheel** (R-U1): distances and the lever's ±60 px are logical canvas px (screen px ÷ scale); one
  wheel event is one step whatever its delta; the 106 HPF slider snaps one position per (300 − 14)/3 px.
- **Typed entry** commits on blur (like the bank-manager rename).
- **Tooltips**: switches and buttons show their legend; the 60 HPF switch shows "HPF n = corner";
  combination buttons name the Shift+click only in the mode where the combination exists; KEY TRANSPOSE shows
  its value; the lever shows "BENDER v".
- **Visuals** (§14): button LEDs use `--lamp-on` / `--led-off`; section titles CONTROLLER…CHORUS; header and
  cap colours in `ui/src/skins/skins.ts`; 12 px end cheeks; the favicon is `data:,` (no raster request).
- **Bank manager**: the list is one Tab stop (Up/Down move, Enter selects, F2 renames); an unchanged name is
  not sent; a `.vjn6bank` load shows "Loaded ‹file›"; the filter text is a hint line under Load file…;
  Send patch uses `aria-disabled` with its reason as tooltip (mode/function first, then "SysEx not
  permitted", then "MIDI output not available"). Only the first 1 MiB + 1 byte of a file is passed on
  (enough for the size rejection); a browser read error shows `Er` and "Could not read ‹file›".
- **Web MIDI**: one request with `{sysex: true}` (rejection = MIDI not available); ports 1…15 in first-seen
  order (later inputs share 15); a disabled or vanished input sends `midiInputGone`; IndexedDB `devices` =
  `{disabledInputs, output}`, new devices start enabled.
- **Web runtime**: `vjn6_init` uses a probe AudioContext's default rate; the save scheduler follows the core
  `seq` (F-13 timing) and uses explicit IndexedDB `commit()`; after a failed write the banner shows and
  saving stops; polling by rAF (≤ 60 Hz), 250 ms while hidden; before the start gesture all keys are
  swallowed and the panel is inert (the dismissing key's key-up too).
- **Shortcuts** are ignored in text fields and on key repeat; Alt+B / Alt+, toggle their overlay.
- **BANKS and SETTINGS buttons** (2026-11-06): §5 gives the overlays no on-panel control, so the bank
  manager and Settings (with the 60/106 mode choice, F-12) could only be opened with Alt+B / Alt+, and the
  mode could not be found by clicking (reported on the standalone). Two outlined text buttons sit in the
  top strip above the y = 40 rule, right-aligned to the right edge box (BANKS 1232–1292, SETTINGS
  1300–1380, y 10–32), drawn in both modes; the tooltip names the shortcut (⌥B / ⌥, on macOS, Alt+B /
  Alt+, elsewhere). They come after the right edge in the Tab order, so the R-U3 panel order is
  unchanged. The AC-SC1 baselines were regenerated for them.
- **Computer keyboard**: Z, X, C, V, M and F9 ignore key repeat; key-up always releases its note even with
  modifiers held; a play toggle applies at once and wins until the core state confirms it (≤ 1 s).
- **Overlays** are modal (panel inert, Tab trapped, focus returns on close); Settings shows MIDI function
  (106) in both modes; the plugin has no device section; device names longer than 40 characters are
  shortened in the middle with the full name as tooltip.
- **Zoom on the desktop**: the canvas always fits the window; a zoom choice is also sent to the host as
  `zoom`, which sizes the window (§4.4).
- **JUCE interop**: the UI implements `window.__JUCE__.backend` invocation itself instead of bundling JUCE's
  interop JavaScript (dual-licensed JUCE EULA / AGPL; R-L1 forbids shipping AGPL).

## Web build

- **Environment name**: emcc 6.0.13 rejects `-sENVIRONMENT=web,worker,audioworklet` (§15.2); the same
  environment is now called `web,worker,worklet`.
- **`-pthread`** is added to the §15.2 flags for the core's `std::mutex` (snapshot readers) and
  `std::this_thread::sleep_for` (`loadState`); no pthread is ever started (pool size 0).
- **Memory**: 64 MB initial, growth to 512 MB, 4 MB main stack (ccall puts string arguments on the stack; a
  `loadFile` message carries up to 1.4 MB of base64).
- **Start-up**: the worklet scope is loaded in `vjn6_init` (allowed while the context is suspended); the
  node is created and the context resumed in `vjn6_start_audio` on the gesture.
- **Fallback driver** (web): 10 ms interval, at most 0.25 s catch-up per tick, worklet considered stalled
  after 250 ms without a quantum (e.g. suspended context); an atomic flag guarantees one renderer at a time,
  a worklet quantum that finds the flag taken outputs silence.
- **WASM tests**: `-fwasm-exceptions` (Catch2 REQUIRE), `-msimd128` like the web module, NODERAWFS so the
  goldens and fixtures are read from the repo. Results: identical to native (all 123 test cases, 33 810
  assertions; golden hashes bit-identical).
- **Benchmark stimulus** (§9.1): UNISON BRASS with Chorus I and one legato note change per second so all six
  voices sound for the full 10 s.

## Plugin and standalone

- **Host writes (R-S3)**: every `setValue` outside the plugin's own reports (marked with a thread-local
  flag) is a host write; writes are coalesced per block and applied at offset 0 before `render()`.
- **Reporting to the host**: core changes are reported once per changed parameter, except changes whose
  origin is host automation (`Origin::Host`), which the host already knows — unless the plugin's own report
  has since overwritten the host's last write. Host program selections carry `Origin::HostProgram` and are
  always reported, so the host's values match the core.
- **Gestures**: the end of a panel drag is delayed by 50 ms so the final value reaches the host inside the
  gesture; open gestures are ended, and `releaseAll` posted, when the page unloads or the editor closes.
- **Synchronous apply**: when no audio callback runs, `setStateInformation` and `setCurrentProgram` render
  5 ms of silence so that the change takes effect at once.
- **Fallback driver (desktop)**: a 5 ms tick thread takes over 100 ms after the last `processBlock`, renders
  at most 100 ms per tick and drops host MIDI out; `processBlock` waits at most 2 ms for the render guard,
  then outputs silence for that block; `prepareToPlay` takes the guard.
- **Window size**: the plugin editor's last size is kept per instance in memory (the state blob is the
  core's §4.7 blob); the standalone stores it in JUCE's properties file. Choosing Fit does not resize an
  open window, percentages do; Fit leaves 40 × 80 px for the window frame.
- **Standalone MIDI**: all inputs are enabled on first run and new inputs are enabled; inputs are merged by
  JUCE (one source); an unplugged input posts `MidiPortGone` for port 0; without an audio device input MIDI
  goes straight to the core; the single MIDI output is driven from the message thread (JUCE's own default
  MIDI output stays empty); the choice is saved as `midiOutputId` / `midiOutputName` in the properties file.
- **State files (F-13, R-D2)**: writes go to a per-process temp file renamed over the target; a zero-length
  `state.bin` is a first run (no `rE`, no rename); the final save on quit happens after 10 ms of silent
  render; after a failed save the next change retries, and a successful save clears the status line.
  `VJN6_STATE_DIR` overrides the directory (tests).
- **Messages to the UI**: `env` after the first full state and again when outputs, the chosen output or
  the save error change; `inputs` is `[]` on the desktop; an export write failure sends notice
  `writeFailed` with the full path.
- **Parameter types**: porta on, hold and arpeggio on are `AudioParameterBool` (OFF/ON); switches and other
  choices (including arpeggio range) are `AudioParameterChoice`; typed slider text maps back via
  round(v × 12.7). Host renames of programs are ignored. Settings, slot/bank and non-parameter performance
  changes call `updateHostDisplay(nonParameterStateChanged)`.
- **UI bundle in the binary**: every file of `ui/dist` is embedded with `juce_add_binary_data`, staged as
  `ui<N>.<ext>` with a generated path table (MSBuild-safe names); without `ui/dist/index.html` the plugin is
  skipped with a configure warning (`npm run build` first).

## End-to-end tests and web persistence

- **Web final save (F-13)** also runs on `beforeunload` (WebKit keeps an IndexedDB write across a reload only
  when it starts there) and only when the state changed since the last save, so a page closed while still
  starting cannot overwrite the saved state with the first-run state. On load, the UI waits until the core
  has applied the loaded state (its serialised state changes; at most 500 ms) before it sends `ready`, so
  the first-run state is never shown (R-S3).
- **AC-SC1** baselines: Chromium on Linux (generated in the official Playwright 1.65.0 Docker image,
  amd64), the viewport sized to the canvas × scale with Zoom on Fit; both modes at 50/100/200 % plus the
  bank manager and Settings; threshold 0.1 % of pixels. Skipped on other platforms and in WebKit.
- **AC-U3.1 in WebKit** uses Option+Tab (Safari's plain Tab skips buttons by default, a browser
  preference); the test tabs until VOLUME, then checks the complete R-U3 order.
- **"No note sounds" (AC-U2.1)** = analyser RMS below −60 dBFS with chorus noise OFF.
- **Web MIDI double** is installed in both browsers (AC-SC2, AC-S3.3); AC-B2's MIDI note is Chromium-only
  (Safari has no Web MIDI). Drops of several files use a synthetic DataTransfer; "Cancel" is a file chooser
  closed with no files; "IndexedDB disabled" hides `window.indexedDB`.
- **Lever rounding**: ±30 px gives −4095 / +4096 (Math.round of ±4095.5); both are +-0.5 of the lever range.

## v3.2 — Juno-60 corrections (VJN6-J60-CORRECTIONS-KICKOFF.md)

Guardrail for every item: 106 mode unchanged. The regression guard is the baseline recorded from 209a8ab
before any change (native 123 test cases / 33 810 assertions, plugin 27 / 11 079, WASM identical, Vitest 54,
Playwright 81 passed / 20 skipped, AC-SC1 10/10, the 24 106-mode golden hashes, the 106 rows of
`tests/golden/display_table.json`, the 106 screenshots).

### C-2 No RANGE in 60 mode
- **Where W0 is ignored:** `Engine::sw(W_RANGE)` returns 1 in 60 mode, so both 60-mode pitch paths (control
  step and note-on) get range = 0 cents from one place; the stored byte is untouched (§4.1). A RANGE press
  (`Button::Range`) is ignored in 60 mode, and the RANGE LEDs are lit in 106 mode only (§5.2). CC12 and
  host writes of `s_range` still store W0 in 60 mode (R-M1, §4.8).
- **LED matrix (AC-P4.1):** every row that ends in 60 mode loses `R8`, including the two rows that start in
  106 mode and switch to 60 (`106 -> 60 …`), because their expectation is a 60-mode state. The 60 rows that
  pressed RANGE now check that the press changes nothing. The old 60 row "stored range 5 is read as 16′"
  cannot show anything in 60 mode any more; its out-of-range coverage moved to a new 106 row with the same
  steps (an added row; no existing 106 row changed). AC-M1.2 likewise checks the RANGE 4′ LED after CC12 100
  in 106 mode only; in 60 mode it checks the stored W0 = 2 and that no RANGE LED is lit.
- **Originals that change octave in 60 mode:** of the eight original patches, `BASS` (W0 = 0, 16′) now plays
  an octave higher in 60 mode and `SFX` (W0 = 2, 4′) an octave lower; the other six are 8′. In 106 mode
  both are unchanged. Goldens regenerated: `BASS-60-off/I/II` and `SFX-60-off/I/II`; the other 42 hashes
  (all 24 106-mode ones included) are bit-identical.
- **DCO gap in 60 mode left as empty panel** (§5.2): the RANGE column (x 468–494, y 116–306) is not
  closed, because closing it would move PULSE and SAW, which 106 mode shares. PULSE, SAW, SUB, SUB LEVEL and
  NOISE keep their positions; the `RANGE` legend is drawn in 106 mode only.
- **UI reading of W0:** `effectiveSwitch` in `ui/src/protocol.ts` (the UI's copy of `Engine::sw`) reads W0
  as 1 in 60 mode, for consistency; nothing in the 60 panel shows W0 and the stored value is never rewritten.
- **Testable legends:** panel group legends carry `data-testid="text-<id>"` (an attribute only; they stay
  `aria-hidden`), so the e2e tests can check which legends each mode draws.
- **Screenshots:** only `panel-60-50/100/200` changed (the 960 px overlays cover the DCO box, so the 60
  bank-manager and Settings shots did not change); every 106 PNG is byte-identical.

### C-3 Octave transpose D/N/U
- Only the labels change: `f_oct_transpose` host strings `D`, `N`, `U` (§4.8) and the switch legends; the
  values (0 = D −12, 1 = N, 2 = U +12) and the default (1) are unchanged, so saved state stays compatible.
  `tests/golden/display_table.json` changed in its two `f_oct_transpose` host rows only.
- The switch keeps the "graded positions put the highest at the top" rule (UI section): U, N, D from top to
  bottom, Up moves towards U. Screenshots: `panel-60-*`, plus `settings-60` and `bank-manager-60`, where the
  switch shows dimmed behind the overlay; every 106 PNG is byte-identical.

### C-4 60 VCA LEVEL is centre-zero (−5…+5)
- **One rule in the core** (`core/panel/display.cpp`): `fieldV(field, byte, mode)` is the v a slider shows
  (byte/12.7, or byte/12.7 − 5 for B10 in 60 mode) and `fieldByteFromV` the typed-entry inverse (60 VCA
  LEVEL: v clamped to −5…+5, then floor((v + 5) × 12.7 + 0.5); others unchanged). The host string and
  parse (`hostValueString`, `hostValueFromString`) now take the mode; only `p_vca_level` differs. The UI's
  `format.ts` reproduces the same rule; the engine, the byte and the tooltip's dB are unchanged (a new core
  test checks that byte 64 still renders at 64/127 of byte 127 and byte 0 is silent).
- **Zero shows without a sign:** a value that rounds to 0.0 shows `0.0` (bytes 63 and 64), every other value
  carries `+` or `-` (`+0.1` at byte 65); `-0.0` and `+0.0` never appear. Typed `0` commits 64.
- **Display table:** `fields` rows for 60 mode, VCA LEVEL changed (128 rows); the `host` section is the
  106-mode strings and is unchanged; a new `host60` section holds the 60-mode strings (it differs from
  `host` only in `p_vca_level`), so R-P6 is checked within each mode.
- **Plugin strings follow the mode:** the processor keeps the active mode in an atomic (from the first
  snapshot, from `SettingChanged(S_MODE)` events and from `setStateInformation`), the parameter string
  lambdas read it, and a mode change calls `updateHostDisplay(...withParameterInfoChanged(true))` so hosts
  refresh the displayed strings. Between the mode switch and the next poll of the core's events the host
  may still see the previous mode's strings.
- **AC-PL2 (plugin):** the value-string loop runs in 60 mode (first run) and again after switching to 106
  mode; in 106 mode every expectation is the one before v3.2.
- **UI tick scale:** the 60 VCA LEVEL ticks read `−5 … −1, 0, +1 … +5` on the same 11 positions as 0–10;
  negatives use U+2212 (the minus already on the 60 polarity switch), positives carry `+` like the value
  strings. 106 VCA LEVEL keeps 0–10.
- **UI typed entry:** the field opens on the ASCII-signed v (`-5.0`, `+2.0`), the same string as the
  tooltip and the host; it accepts ASCII `+`/`-` (not U+2212). The UI's formatter functions take the mode as
  a required argument, so no caller can fall back to the 106 form; the scale carries
  `data-testid="scale-<id>"` for AC-P5.1. Screenshots: `panel-60-*` only.

### C-5 60 VCF order
- **Conflict with the §5.2 position rule:** C-5 puts the polarity switch after RES in 60 mode while 106
  mode keeps it last, so the switch and the ENV, LFO and KYBD sliders cannot keep their positions across a
  mode switch. The more specific rule (C-5) wins; SPEC §5.2 now names the VCF box as the one exception.
  FREQ and RES do not move.
- **Geometry (60 mode):** FREQ 742, RES 778, polarity switch body x 815 (y 236 like the other switches),
  ENV 856, LFO 892, KYBD 928 (track ends at 948, the right edge of the 106 polarity legend). The first idea
  (switch 818, ENV 850) made the 40 px `POLARITY` legend overlap the ENV tick digits; these positions leave
  ≈ 3 px to the RES cap and ≈ 4 px to the ENV digits. The 60 legend stays `POLARITY`.
- **Implementation:** the polarity switch and the ENV/LFO/KYBD sliders have one definition per mode with the
  same ids (like the 60 HPF switch and the 106 HPF slider), so `CONTROLS` stays the Tab order of each mode
  (R-U3: 60 FREQ, RES, polarity, ENV, LFO, KYBD; 106 unchanged). Screenshots: `panel-60-*` only.
