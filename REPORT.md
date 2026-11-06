# VJN6 build report

Branch `develop/vjn6` (not pushed; no pull request opened, so GitHub Actions has not run). All numbers below
were measured on this machine (macOS 26.3, Apple M4 Max, Xcode 26.5, emcc 6.0.13, Node 26) on 2026-11-06.

## What was built

VJN6, the six-voice virtual-analog synth of SPEC.md, in all three builds from one engine and one UI:

| Part | Where | What |
| --- | --- | --- |
| Core | `core/` (`vjn6_core`, C++17, no JUCE) | The §9.4 `Instrument` API with lock-free command/event queues and a triple-buffered snapshot; the engine (DCO with host-rate BLEP band-limiting, DC blocker, HPF, zero-delay-feedback OTA VCF, VCA, ADSR, LFO, 273/31-tap decimator, Holters & Parker-style BBD chorus, output stage) with the 60 analog and 106 firmware control paths; voice assignment, HOLD, CC64, UNISON, portamento, arpeggio with MIDI clock; MIDI parser and R-M1 gating; transmit with the 10 ms limit; SysEx codec; the panel state machine (slots, pending bank, Manual, EDITED, WRITE, key transpose, LED and button LEDs, LED messages, memory protect); state blob; `.vjn6`/`.vjn6bank`/`.syx` files with NFC name normalisation; R-P5/§4.8 display strings; the JSON UI bridge. Own deterministic math (`core/juno/dmath.h`), every (M)/(D) constant in `core/juno/constants.h`. |
| UI | `ui/` (Svelte 5 + TypeScript) | One bundle for desktop and web: both skins on the 1400 × 560 canvas, LED (§5.3 glyphs), every control with tooltips and typed entry, lever, bank manager, Settings, computer keyboard (`keyboard.ts`), shortcuts, desktop (JUCE) and web transports, IndexedDB persistence, Web MIDI. Vector only; fonts bundled. |
| Plugin + standalone | `plugin/` (JUCE 9.0.5) | VST3, AU and Standalone with the §9.3 identity; 40 host parameters (§4.8), 128 programs, state, latency reporting, MIDI in/out, `WebBrowserComponent` editor serving `ui/dist` from binary data, the WebView2/WebKitGTK fallback label; standalone state files (R-D2, F-1, F-13) and device selection. |
| Web | `web/` | `worklet.cpp` (Wasm Audio Worklet, main-thread fallback driver), `build.sh` → `ui/public/vjn6.{js,wasm}`; `build_tests.sh` runs the Catch2 suite and the benchmark under Node. |
| Tests | `tests/`, `plugin/tests/`, `ui/tests/`, `ui/e2e/` | Every §13 criterion as an automated test named with its AC ID (tables below). |
| Tooling | `tools/`, `banks/`, `.github/`, `docs/` | Default bank generator and bank, NFC table generator, display-table generator, benchmark, licence check, `syx_inspect.py`, `tools/capture/` (§15.4 protocol as MIDI + analysis), CI workflows, README, `docs/LICENSES.md`, `docs/STATE.md`, `docs/BRIDGE.md`, PLAN.md, DECISIONS.md. |

## How to run

Full instructions are in README.md. In short:

```sh
git submodule update --init --recursive
npm ci && npm run build                      # UI bundle -> ui/dist (needed before the plugin build)
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"   # or Ninja / Visual Studio
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure    # core, plugin and licence tests
web/build.sh && npm run web                  # http://localhost:5173 with COOP/COEP
web/build_tests.sh                           # the Catch2 suite and the benchmark under WASM/Node
npm test && npm run test:e2e                 # Vitest, Playwright (Chromium + WebKit)
```

The standalone app is `build/plugin/VJN6_artefacts/Release/Standalone/VJN6.app`; the VST3 and AU are copied to
`~/Library/Audio/Plug-Ins` by the build.

## Test results

| Suite | Result |
| --- | --- |
| Catch2 native (`vjn6_tests`: engine, MIDI, panel, flows, files, state, goldens) | **123/123 test cases, 33 810/33 810 assertions** (39 s) |
| Catch2 under WASM (same binary built with emcc, Node) | **123/123, 33 810/33 810**: identical to native, including the golden hashes |
| Plugin tests (`vjn6_plugin_tests`, real `AudioProcessor`) | **27/27, 11 079/11 079** |
| Licence ctests (AC-L1.1 ×2, AC-L2.1, AC-L4.1, AC-L5.1 ×2) | **6/6** |
| ctest total (clean build tree) | **8/8** |
| Vitest (UI) | **54/54** (7 files); `svelte-check` 0 errors, 0 warnings |
| Playwright, macOS (Chromium + WebKit) | **79 passed, 0 failed**, 20 skipped (AC-SC1 runs only in Chromium on Linux) |
| Playwright, Linux (official 1.65.0 image, amd64, `CI=1`) | **89 passed, 0 failed**, 10 skipped (AC-SC1 in WebKit), run by the e2e agent |
| pluginval, strictness 5 (AC-PL1, macOS) | **VST3: SUCCESS, AU: SUCCESS** (VST3 also passes strictness 10) |
| Golden renders (AC-E1.3, 48 SHA-256 hashes) | **Bit-identical** on macOS arm64 (clang), macOS x86_64 (clang, Rosetta), Linux x86_64 (GCC 13, Docker) and WASM |
| Clean checkout (AC-B1, macOS) | Fresh clone with submodules from GitHub, README steps (Xcode universal build of all targets, ctest): **all exit 0, 8/8 tests** |
| Manual walkthrough of §6 (F-1 … F-13, both modes, scripted through the real UI in headless Chromium) | **38/38 checks** after one UI fix (below) |

### Benchmarks (§9.1, AC-N1.1)

106 mode, UNISON × 6, BRASS with Chorus I, 2×, 48 kHz, 10 s, median of 5:

| Build | Real-time share | µs per 128-sample block | Gate (on a GitHub ubuntu-24.04 runner) |
| --- | --- | --- | --- |
| Native (Release) | **3.12 %** | 83.3 | < 8 % |
| WASM (Node 26) | **3.38 %** | 90.1 | < 35 % |

The render started at 6.65 % native. It came down through a one-step Newton solve, a single-division secant gain,
a cached cutoff coefficient, exact power-of-two scaling and, mainly, running the six voices' VCFs interleaved
(division latency no longer serialises).
Golden hashes were unchanged by the interleaving. The GitHub runner is expected to be about 2× slower per thread
(≈ 6–7 % native); it was not measured here.

## c_cal and constants

**c_cal, measured on the implemented solver** (self-osc patch, 3 s pre-roll, zero crossings of the VCF
output). It is one value per mode, the same for 2× and 4× and at 44.1, 48 and 96 kHz:

| Mode | Measured self-osc / small-signal ratio | c_cal | Result at 44.1 / 48 / 96 kHz, 2× and 4× |
| --- | --- | --- | --- |
| 60 | 0.9160 | **1.0917** | 248.016–248.023 Hz and 991.73–992.07 Hz (−0.48 … +0.16 cents) |
| 106 | 0.8882 | **1.1259** | 248.009–248.016 Hz and 991.69–992.04 Hz (−0.55 … +0.11 cents) |

The steady self-oscillation amplitude is 0.341 (60) and 0.405 (106). That is exactly the spec's reference
simulation, and AC-E5.2 needs 0.33 / 0.40 ± 15 %.

**Retuned (D)/(M) constants: none.** Every (D) and (M) value of §7 is used as written. Getting there needed a solver
change rather than a retune: with §14's previous-sample secant gains, the self-oscillation frequency drifted with
fc/fo (+31 cents at note 84, 44.1 kHz/2×), so no c_cal could hold AC-E5.1. Secant gains taken at the linearly
predicted stage input fixed it.

## Spec conflicts and how they were resolved

- **"Half-band decimator" vs AC-E3.6.** A strict half-band filter is −6 dB at fs/2, so harmonics just above
  fs/2 fold back at −34 to −52 dB. The final 2→1 stage is a 273-tap lowpass that starts ≥ 100 dB at fs/2; the
  4→2 stage stays half-band. Latency is 68 / 72 host samples at 2× / 4×.
- **BLEP band and the saturating VCF vs AC-E3.6.** Band-limiting only to 0.45·fo let the VCF intermodulate
  content above fs/2 back into the band (−61 dB). The BLEP kernel is now defined in host samples (cutoff 0.47·fs),
  and measured aliases are ≤ −95 dB.
- **§14 solver recommendation vs AC-E5.1:** the predicted-secant solver, described above.
- **§15.2 emcc flags:** emcc 6 calls the environment `web,worker,worklet` (`audioworklet` is rejected), and
  `-pthread` is added for the core's mutex (no thread is ever started).
- **§15.1 "CI runs every criterion" vs the owner's CI rule.** The full matrix runs only on pull requests into
  `main` and on manual dispatch; a smoke job runs after a merge; nothing runs on development-branch pushes.
- **Measurement readings.** "Saw peak" is the ramp extrapolated to the edge: the 2 % edge exclusion of §14
  otherwise caps a saw at 0.96. "Flat ± 1 dB" means the levels fit a 2 dB band. AC-E5.4's P is the gain *at*
  fx.

## DECISIONS.md entries

Every entry, one line each; the reasons are in DECISIONS.md.

- **Architecture:**
  - `Instrument` adds `hostParam`, `snapshot`, `acquireBulk`/`bulk` and `sampleRate`.
  - Queues are SPSC with 4096 entries plus a triple-buffered snapshot.
  - Two preallocated bulk payload slots sized for 1 MiB files.
  - A fallback driver renders silently when no audio runs.
  - Commands act at the first control boundary.
  - One JSON UI protocol in C++; lever values are integers.
  - Goldens are SHA-256 hashes.
- **Sound engine:**
  - The predicted-secant ZDF solver, and a [9/8] Padé tanh (6.8e−6 error).
  - Own deterministic math.
  - Host-rate BLEP kernel.
  - 273/31-tap decimator.
  - DCO noise from three summed uniform draws, half-band upsampled.
  - 60-mode one-step control interpolation.
  - 106 tick grid ceil(k·42·fs/10000).
  - An oversampling change keeps the absolute counter.
  - Holters & Parker-style BBD with an exact noise scale.
  - Measured c_cal.
  - Offsets on the real and imaginary chorus states.
- **Test conventions:** saw peak by ramp fit, flatness as a 2 dB band, P at fx, power-spectrum wet response,
  allocation hook plus structural lock and log check.
- **Behaviour gaps:**
  - Hold-pedal sources are OR'ed.
  - CC120/123 act on every source.
  - ARPEGGIO on/off rules.
  - A mode or key-mode change forgets held keys.
  - The 106 PATCH LED needs the group to match.
  - Host program selections are reported to the host.
  - File type by extension.
  - NFC from generated tables.
  - One rounding rule shared by C++ and TS.
  - Arpeggio start, RANGE changes, velocity and clock fallback.
  - Performance writes for the other mode.
  - Per-key velocity in UNISON.
  - The KEY TRANSPOSE LED blinks when armed.
  - A dump drops held-back parameter messages.
  - `.syx` `MANUAL nnnn` numbering.
  - Merged standalone MIDI inputs.
- **CI:** the owner's PR-into-main and smoke rule.
- **UI:**
  - ARIA legends with a section prefix.
  - Switch order.
  - Drag, wheel and HPF snapping units.
  - Typed entry commits on blur.
  - Tooltip contents.
  - Visual choices.
  - Bank-manager list keyboard, notices and Send-patch reasons.
  - File size cut.
  - Web MIDI port assignment and `midiInputGone`.
  - Web runtime: probe rate, save scheduler, rAF polling, inert panel before the start gesture.
  - Shortcut and keyboard repeat rules.
  - Modal overlays.
  - Desktop zoom.
  - Own JUCE interop, so no AGPL JavaScript is bundled.
- **Web build:**
  - Environment name.
  - `-pthread`.
  - Memory and stack sizes.
  - Worklet start-up.
  - Fallback driver constants.
  - WASM test flags.
  - Benchmark stimulus.
- **Plugin and standalone:**
  - Host-write detection.
  - Reporting to the host with `Origin::HostProgram`.
  - Gesture end delay.
  - Synchronous apply without audio.
  - Desktop fallback driver.
  - Window size and Fit.
  - Standalone MIDI.
  - State-file atomicity and retry.
  - `env` and `writeFailed` messages.
  - Parameter types.
  - The UI bundle embedded as binary data.
- **End-to-end tests and web persistence:**
  - Final save on `beforeunload` and only when the state changed.
  - The R-S3 load wait.
  - AC-SC1 baseline setup.
  - Option+Tab in WebKit.
  - "No note sounds" threshold.
  - The Web MIDI double and simulated inputs.
  - Lever rounding.

## Bugs found during verification

Beyond the per-slice test failures fixed while building:
- **Nine core logic bugs**, found by the core acceptance tests, all fixed: arpeggio, UNISON velocity, LFO-delay
  trigger order, other-mode performance writes, the pending bank after a state load, held-back parameters after
  a dump.
- **A chorus subnormal:** a real-pole residue with a rounding imaginary part.
- **A web data-loss bug.** A page closed while still starting overwrote the saved state, and WebKit dropped the
  save on reload. Both found by the e2e tests.
- **A UI stale-value bug.** Fast key repeats on a slider stepped from a stale value; found in the §6 walkthrough.

During my first walkthrough attempt in a visible Chrome tab, the tab took keyboard focus, so typing meant for
other windows played notes and moved sliders in the test page. Nothing outside that page was affected. The
walkthrough was then run headless.

## Known gaps

- **Not run locally (CI or other machines only):**
  - Windows (MSVC) and Ubuntu (WebKitGTK) desktop builds, including their WebView and failed-write code paths.
  - pluginval on Windows and Linux.
  - The benchmark gates on the GitHub runner.
  - Playwright's AC-SC1 check on the actual CI runner. The baselines were made in the matching Docker image; one
    glyph ("→" in Settings) relies on a system fallback font, a few pixels at most.
- **GitHub Actions has never run.** Nothing was pushed, per the owner's CI rule; the workflows were validated
  only for syntax and triggers. The first PR into `main` will be the first CI run.
- **The standalone UI was not driven by hand.** Interaction with the native window was covered by the plugin
  tests (real `AudioProcessor`, the editor loading the real UI and sending `ready`, state files) and by the
  same UI bundle in the browser walkthrough.
- **Loaded-state signal on the web.** `loadState()` applies at the next `render()`, and the web transport
  detects completion by comparing serialised state (at most 500 ms). An explicit completion signal from the core
  would be cleaner.
- **§15.4 human follow-ups** (they need hardware, a person or credentials):
  - Measuring every (D)/(M) constant on a real Juno-60 and Juno-106 with `tools/capture/`: resonance law and
    onset, Q compensation, the 60 cutoff and envelope slider laws, the 106 LFO table and delay shape, LFO depth
    taper, sub level and noise band, output level reference, chorus wet/dry and 106 chorus rates, chorus noise
    spectrum, 4.2 ms update interval, pulse edge, POLY 2 and portamento curves, arpeggio gate, HPF vs VCA level
    (60).
  - Panel colours and typeface against licensed photos.
  - Real Web MIDI devices, audio devices, Safari 17 and Firefox.
  - Signed and notarised installers.
  - Public web hosting with COOP/COEP.
  - A trade-dress and name check, including replacing `com.marcofunk.vjn6` with an owned reverse domain.
