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

## v3.2 — Juno-60 corrections (VJN6-J60-CORRECTIONS-KICKOFF.md)

Branch `develop/j60-corrections`, from `develop/vjn6` at 209a8ab. One commit per item:

| Commit | Item |
| --- | --- |
| 7ecd4ec | SPEC.md v3.2 (changelog C-1 … C-8), the kickoff, the PLAN.md v3.2 section |
| 7d82945 | C-2: no RANGE in 60 mode |
| 02b4d7a | C-3: octave transpose D/N/U |
| e973331 | C-4: centre-zero 60 VCA LEVEL |
| 0ea98a1 | C-5: 60 VCF order |
| f93685d | C-6: 60 chorus OFF/I/II and I+II |
| faba260 | C-7: the factory bank, with the C-8 test changes it causes |

The C-8 test changes of C-2 … C-6 are in each item's own commit. Every commit was made with the full local
gate set green. Nothing was pushed and no PR was opened, so CI has not run (owner rule).

Only 60 mode changed. 106 mode matches the baseline recorded from 209a8ab before any change, apart from the
default-bank exceptions of C-8 (below).

### What changed, per C item

- **C-1 Square wave (SPEC only):** R-E3 now says there is no separate square control. A square is the pulse
  at PWM 0 (MANUAL), and the sub is a square one octave down. The engine already worked this way, so no code
  changed.
- **C-2 No RANGE in 60 mode:**
  - `Engine::sw` reads W0 as 1 (8′) in 60 mode. The stored byte is kept, and CC12 and host writes still
    store it.
  - A RANGE press is ignored in 60 mode, and the RANGE LEDs, buttons and legend exist in 106 mode only.
  - The empty DCO column stays empty, so no 106 position moves.
- **C-3 Octave transpose D/N/U:**
  - The host strings of `f_oct_transpose` are `D`, `N`, `U`. The panel labels read U, N, D from top to
    bottom.
  - The values (0 = D −12, 1 = N, 2 = U +12) and the default are unchanged, so saved state stays
    compatible.
- **C-4 60 VCA LEVEL −5…+5:**
  - The panel, tooltip, host string and typed entry show v = byte/12.7 − 5.
  - Typed entry commits floor((v + 5) × 12.7 + 0.5) after clamping v to −5…+5.
  - A value that rounds to zero shows `0.0`; every other value carries `+` or `−`.
  - One core rule (`display::fieldV`, `fieldByteFromV`, the mode-aware `hostValueString` and
    `hostValueFromString`) feeds the display table, the plugin and, mirrored in `format.ts`, the UI.
  - The plugin's value strings follow the active mode: an atomic holds the mode, and a mode change calls
    `updateHostDisplay` with parameter info changed.
  - The byte, the R-E6 gain law and the dB in the tooltip are unchanged.
- **C-5 60 VCF order:**
  - The 60 panel and Tab order are FREQ, RES, polarity, ENV, LFO, KYBD.
  - SPEC §5.2 now names the VCF box as the one exception to "shared controls keep their position".
  - FREQ and RES do not move. 106 mode is unchanged.
- **C-6 60 chorus OFF / I / II:**
  - The three are radio buttons in 60 mode; OFF has no LED.
  - Shift+click I while II is lit, or II while I is lit, gives I+II (W4 = 3). Repeating it does nothing, and
    Shift without a lit partner is a plain click.
  - The I and II tooltips name the Shift+click. 106 mode is unchanged.
- **C-7 Factory bank:**
  - The default bank holds the 56 Juno-60 factory patches in slots 1–56, INIT in 57 and the eight originals
    in 58–65.
  - `tools/make_default_bank.py` reads `banks/j60-factory-chart.csv` and writes:
    - one source file per patch, `banks/default/001-…056-*.json`;
    - `banks/vjn6-default.vjn6bank`;
    - `core/state/default_bank.inc`;
    - `docs/FACTORY_BANK.md`.
  - `--check` verifies every generated file. The result equals the kickoff's byte table in all 56 rows.
  - W0 encodes the chart's octave transpose (D → 16′, N → 8′, U → 4′). 60 mode ignores it (C-2), and 106 mode
    plays each patch in its charted octave.
  - LFO TRIG mode and octave transpose stay performance settings. They are listed per patch in the source
    JSON and in `docs/FACTORY_BANK.md`.
  - Existing saved states keep their own bank. The factory bank appears on first run, or by loading
    `banks/vjn6-default.vjn6bank` with Load file….
- **C-8 Tests:**
  - New core tests: AC-D7.2 (slots 1–56 against a hard-coded copy of the §15.3 table, 57 INIT, 58–65
    the v3.1 rows), AC-P5.1 (60 strings and the unchanged gain law), AC-U1.1 (core, 60 typed entry) and
    AC-U4.1 (core, chorus combination).
  - 60-mode expectations were changed in: AC-E3.1, AC-P1.1, AC-P4.1 (LED matrix), AC-M1.2, AC-PL2, AC-SC1
    (60 screenshots) and the UI tests.
  - Default-bank expectations were changed in: AC-D7.1, AC-D6.1, AC-D1.2, AC-P7.1, the AC-PL2 program
    names and AC-SC1 (bank-manager screenshots).
  - Tests that only relied on the v3.1 slot layout keep their assertions. They load that layout through a
    fixture (`useV31BankLayout`, core and plugin).
  - AC-E1.3 and the §9.1 benchmark select the originals by name, so `BRASS` is now in slot 58.

### Test results (baseline 209a8ab → v3.2, final run on faba260)

| Suite | Baseline | v3.2 |
| --- | --- | --- |
| Catch2 native (`vjn6_tests`) | 123/123, 33 810/33 810 assertions | **128/128, 40 371/40 371** |
| Catch2 under WASM (emcc, Node) | 123/123, 33 810/33 810 | **128/128, 40 371/40 371**, identical to native |
| Plugin tests (`vjn6_plugin_tests`, real `AudioProcessor`) | 27/27, 11 079 assertions | **27/27, 17 259/17 259** |
| ctest (licence checks ×6, native, plugin) | 8/8 | **8/8** |
| Generated files (`make_default_bank.py --check`, display table regenerated and diffed) | — | **up to date, identical** |
| Vitest (UI) | 54/54 | **57/57**; `svelte-check` 0 errors, 0 warnings |
| Playwright, macOS (Chromium + WebKit) | 81 passed, 20 skipped | **91 passed, 0 failed, 20 skipped** |
| AC-SC1 screenshots (Linux Docker, `--check`) | 10/10 | **10/10** |
| pluginval, strictness 5, CI flags (`--validate-in-process --timeout-ms 300000`) | — | **VST3: SUCCESS, AU: SUCCESS** |
| Golden renders (AC-E1.3, 48 hashes) | — | 42 unchanged, including all 24 of 106 mode; 6 of 60 mode regenerated (below) |
| Front-panel walkthrough, `npm run web` (headless Chromium, real UI) | — | **19/19 checks** |

Pass rate is 100 % in every suite. Native and WASM agree on every assertion, including the golden hashes.

**Benchmarks** (§9.1: 106 mode, UNISON × 6, `BRASS` from slot 58, Chorus I, 2×, 48 kHz, 10 s, median of 5,
on an idle machine after the final commit):

| Build | Real-time share | µs per 128-sample block | Gate |
| --- | --- | --- | --- |
| Native (Release) | **2.83 %** | 75.3 | < 8 % |
| WASM (Node) | **3.12 %** | 83.1 | < 35 % |

These numbers are about 0.3 points below the v3.1 figures (3.12 % / 3.38 %) and below a run made while the
C-7 suite was busy (3.11 % / 3.36 %). The rendered path is unchanged (106 mode, identical golden hashes),
so the difference is machine load.

### The 106 guard (kickoff step 2)

A script compared the final run with the 209a8ab baseline:

- **Golden hashes:** none of the 24 106-mode hashes changed.
- **`tests/golden/display_table.json`:** the 106-mode field rows (2 436) and knob rows (896) are identical. In
  the 106 host rows only `f_oct_transpose` changed, which is the mode-independent C-3 label the kickoff
  allows.
- **Screenshots:** `panel-106-50/100/200` and `settings-106` are byte-identical to 209a8ab.
  `bank-manager-106` changed, as allowed (names).
- **Test names:** every baseline native, plugin and Playwright test still exists and passes. The only renamed
  tests are the 60-mode chorus tests (AC-P1.1, native and Playwright), whose behaviour C-6 changes.
  - No 106 assertion was changed. Only the C-8 default-bank expectations above changed.
  - Tests that depended on the old slot numbers run on the v3.1 layout through the fixture.

### Manual exercise (kickoff step 4)

**`npm run web`:** the dev server was started (COOP/COEP headers), and a Playwright script drove the real
front panel in headless Chromium with the mouse and keyboard. Headless was used because a visible tab takes
keyboard focus. 19/19 checks passed:

- **60 mode:**
  - First run is `Strings 1` on LED `11`.
  - No RANGE buttons or legend.
  - D/N/U labels, and clicks set 0/2/1.
  - VCA LEVEL:
    - Ticks −5…+5, and hover at byte 64 shows `VCA LEVEL 0.0 = -6.0 dB`.
    - Dragging 20 px up gives `+0.8`, and 60 px down gives `-1.5`.
    - Typed `-2`/`+5`/`-7`/`0`/`2.5` commit 38/127/0/64/95, the field opens on `+2.5`, and `abc` and Esc
      revert.
  - VCF x positions and Tab order are FREQ, RES, polarity, ENV, LFO, KYBD.
  - Chorus radio clicks work, and the I+II combination works from either side; OFF has no LED; the
    tooltips name the Shift+click.
  - All 56 patches 11–78 load from BANK/PATCH, banks 6 and 7 through BANK 1+5 and 2+5. Each shows the right
    LED and name, the generator's bytes, and CLEAN.
  - Stored 16′/4′ bytes are kept.
  - The bank layout is 1–56 factory, 57 INIT, 58–65 originals, 66–128 INIT.
- **106 mode** (after switching via Settings): everything as before.
  - RANGE 16′/8′/4′ buttons write W0 and light their LEDs.
  - There is no octave switch.
  - VCA ticks are 0–10, typed `5` gives 64 (`5.0`), and `-2` clamps to 0.
  - The VCF order is FREQ, RES, ENV, LFO, KYBD, polarity.
  - Chorus OFF has its LED, and Shift+click I with II lit is a plain click.
  - A/B, BANK and PATCH load slots 17, 14, 1, 58 and 65 with the RANGE LED of the stored W0. Bass 1 shows
    16′ and Harpsichd 2 and SFX show 4′.
  - Switching 106 → 60 → 106 restores the 106 panel.
- **Script fixes:** the first pass failed 3 checks. All three were wrong expectations in the script, not app
  defects:
  - a wait that returned before a PATCH press on the already current slot was processed;
  - expecting the position in a switch tooltip, where R-P5 shows the legend;
  - expecting 8′ for slot 65, which is SFX at 4′.

**Standalone:** I launched the Release app. It was built after the last core change, and no core, plugin, UI
or web source is newer than it; the plugins pluginval tested come from the same build.
- For a real first run, the existing files in `~/Library/Application Support/VJN6/` were moved aside and
  restored afterwards (SHA-256 identical).
- The app started and opened its window.
- On quit it wrote `bank.vjn6bank` byte-identical to `banks/vjn6-default.vjn6bank`.
- As in v3.1, the native window was not driven by hand. The window could not be captured either, because the
  terminal has no screen-recording permission.
- The UI it embeds is the bundle the web walkthrough exercised, and the plugin tests cover the processor and
  editor.

### Regenerated 60-mode goldens and screenshots

- **`tests/golden/renders.json`:** `BASS-60-off/I/II` and `SFX-60-off/I/II` were regenerated (C-2). 60 mode
  no longer applies their stored 16′ / 4′. The other 42 hashes are bit-identical; the bank move changed no
  hash, because AC-E1.3 selects the originals by name.
- **`tests/golden/display_table.json`:**
  - the 60-mode VCA LEVEL field rows (C-4);
  - the two `f_oct_transpose` host rows (C-3);
  - a new `host60` section with the 60-mode host strings. It differs from `host` only in `p_vca_level`, so
    R-P6 is checked within each mode.
- **AC-SC1 screenshots:**
  - `panel-60-50/100/200`: C-2 to C-6.
  - `settings-60`: C-3 and C-6, which show dimmed behind the overlay.
  - `bank-manager-60` and `bank-manager-106`: C-7, the new names.
  - The panel and Settings shots are taken with INIT on slot 1, set up through the app's own bank manager.
    Otherwise the first-run `Strings 1` would have changed every 106 panel shot, which the kickoff forbids.

### Originals that change octave in 60 mode

- **BASS** has W0 = 0 (16′). In 60 mode it now plays at 8′, an octave higher than in v3.1.
- **SFX** has W0 = 2 (4′). In 60 mode it now plays at 8′, an octave lower.
- The other six (BRASS, STRINGS, PAD, PLUCK, ORGAN, SWEEP) are 8′ and sound as before.
- In 106 mode all eight are unchanged.

### New DECISIONS.md entries (section "v3.2 — Juno-60 corrections")

- **C-2:**
  - where W0 is ignored (one place in the engine, stored byte kept);
  - the LED-matrix rows and the new 106 out-of-range row;
  - the octave-changing originals;
  - the DCO gap left empty;
  - the UI's `effectiveSwitch`;
  - `data-testid` on legends;
  - which screenshots changed.
- **C-3:** labels only, values and default unchanged; switch orientation U/N/D top to bottom.
- **C-4:**
  - one core rule for v and typed entry;
  - `0.0` without a sign;
  - the `host60` section of the display table;
  - plugin strings that follow the mode, and the short window where a host may still see the old mode's
    strings;
  - AC-PL2 run in both modes;
  - the U+2212 tick labels;
  - typed entry with ASCII signs.
- **C-5:** the conflict with the §5.2 position rule (C-5 wins); the 60 geometry; per-mode definitions with
  shared ids.
- **C-6:** the core press rules; the test changes; the LED-less, `aria-pressed`-less 60 OFF button; the
  tooltips.
- **C-7:**
  - the generator and its checks;
  - W0 carrying the chart octave;
  - octave and trig not stored;
  - the manual full names, from a transcription and not checked against p. 24;
  - attribution;
  - the v3.1-layout fixture and the tests that use it;
  - the default-bank expectations that changed;
  - originals selected by name;
  - AC-SC1 with INIT on slot 1;
  - e2e silence checks with chorus noise off (`Strings 1` has chorus I, whose hiss sits at about
    −59.7 dBFS).

### New §15.4 follow-ups

- **60 VCA LEVEL law:** linear `B10/127` with −5 = silent assumed (D). Measure the gain at −5, 0 and +5.
- **LFO TRIG mode and octave transpose per patch (60):** does the Juno-60 store them in a patch? If it does,
  move them into the patch. This affects factory patches 21, 33, 51, 52, 54 and 57, which use MANUAL trigger
  with DCO vibrato.
- **Factory patch 36 chorus:** chorus II is inferred, because the scan's right edge on p. 26 is cut off. It
  needs a clean copy of p. 26, or the patch recalled on a Juno-60.
- **Factory bank 7 tuning:** patches 71–76 and 78 sound by VCF self-oscillation, so their pitch depends on
  each unit's filter calibration. Compare each on a Juno-60.
- **Not a §15.4 row, but open:** the manual's full patch names in `docs/FACTORY_BANK.md` come from a
  transcription with two evident slips corrected (58, 76). They were not checked against a scan of p. 24.
