# VJN6

VJN6 is a six-voice virtual-analog polysynth that reproduces the sound and the front-panel workflow of
two classic early-1980s Roland polysynths, the Juno-60 and, as a second engine mode, the Juno-106 (shown
in VJN6 only as modes **60** and **106**; VJN6 is not affiliated with Roland). One signal path
(DCO → HPF → VCF → VCA → chorus), modelled from the published schematics and service literature, runs in
three builds from one C++ engine (`vjn6_core`) and one Svelte UI: a standalone desktop app, a VST3/AU
instrument plugin (JUCE 9) and a locally hosted web page (WebAssembly AudioWorklet). SPEC.md is the
product specification; DECISIONS.md records the implementation choices.

## Prerequisites

| Tool | Version | Needed for |
| --- | --- | --- |
| Git | any recent | `--recurse-submodules` checkout (JUCE 9.0.5, Catch2 v3.16.1) |
| CMake | ≥ 3.25 | every native and WASM build |
| C++17 compiler | Xcode 15+ (Apple clang) on macOS 13+, Visual Studio 2022 (MSVC) on Windows 10 22H2+ / 11, GCC 13 or Clang 17 on Ubuntu 24.04 | desktop builds |
| Ninja | optional | faster single-config builds (used by the scripts when present) |
| Python 3 | ≥ 3.8 (3.13 to regenerate the NFC tables) | tools, licence check, licence ctest tests |
| Node.js + npm | Node ≥ 22.12 | the UI (bundled into every build), Vitest, Playwright, the WASM tests and benchmark |
| Emscripten (emsdk) | current | the web module and the WASM tests and benchmark |

Ubuntu 24.04 packages (JUCE dependencies, WebKitGTK 4.1 for the editor, xvfb for headless tests):

```sh
sudo apt-get install -y build-essential ninja-build xvfb \
  libasound2-dev libjack-jackd2-dev ladspa-sdk libcurl4-openssl-dev \
  libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libxi-dev \
  libwebkit2gtk-4.1-dev libgtk-3-dev \
  libegl-dev libglu1-mesa-dev mesa-common-dev
```

Windows: the plugin editor links the WebView2 SDK statically. Install the package JUCE looks for once, from
PowerShell:

```powershell
Register-PackageSource -provider NuGet -name nugetRepository -location https://www.nuget.org/api/v2
Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -RequiredVersion 1.0.3485.44 -Source nugetRepository
```

(or `nuget install Microsoft.Web.WebView2 -Version 1.0.3485.44 -OutputDirectory <dir>` and pass
`-DJUCE_WEBVIEW2_PACKAGE_LOCATION=<dir>` when configuring).

## Get the sources

```sh
git clone --recurse-submodules <repository-url> vjn6
cd vjn6
# in an existing clone:
git submodule update --init --recursive
```

## Build the UI bundle first

The plugin editor and the standalone app embed the UI from `ui/dist`, so build it before configuring the
desktop targets (and again after UI changes). Without `ui/dist/index.html`, CMake skips the plugin and the
standalone app with a warning:

```sh
npm ci
npm run build          # -> ui/dist
```

## Desktop build (CMake ≥ 3.25)

CMake options (root `CMakeLists.txt`):

| Option | Default | Effect |
| --- | --- | --- |
| `VJN6_BUILD_PLUGIN` | `ON` | JUCE plugin (VST3, AU on macOS) and standalone app (`plugin/`) |
| `VJN6_BUILD_TESTS` | `ON` | Catch2 suite `vjn6_tests`, benchmark `vjn6_bench`, display-table generator, licence tests |
| `VJN6_BUILD_WEB` | `OFF` | Emscripten web module; only with `emcmake` (use `web/build.sh`) |

### macOS (universal binary: Standalone, VST3, AU)

```sh
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Windows (MSVC: Standalone, VST3)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Ubuntu 24.04 (Standalone, VST3)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
xvfb-run -a ctest --test-dir build --output-on-failure
```

The plugins and the app land in `build/plugin/VJN6_artefacts/Release/` (`Standalone/`, `VST3/`, `AU/`);
the build also copies the plugins into the user's plug-in folders (`~/Library/Audio/Plug-Ins/{VST3,Components}`,
`C:\Program Files\Common Files\VST3`, `~/.vst3`).

### Core and tests only (no JUCE)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVJN6_BUILD_PLUGIN=OFF
cmake --build build --parallel
./build/tests/vjn6_tests              # full Catch2 suite
./build/tests/vjn6_tests "[smoke]"    # smoke tests only
```

With the Xcode or Visual Studio generators the executables are in a `Release/` subfolder
(`build/tests/Release/vjn6_tests`).

## Web build (WASM) and `npm run web`

Activate emsdk first (`source <emsdk>/emsdk_env.sh`; the scripts also try `$EMSDK/emsdk_env.sh` and
`~/emsdk/emsdk_env.sh`), then:

```sh
web/build.sh       # emcc over vjn6_core + web/worklet.cpp -> ui/public/vjn6.js + ui/public/vjn6.wasm (build dir build-web/)
npm ci
npm run web        # Vite dev server on http://localhost:5173 with COOP/COEP headers
```

`npm run build` followed by `npm run preview` serves the production bundle with the same headers. Open the
page in current Chrome or Edge (Web MIDI) or Safari 17+ (computer keyboard only), then click or press a key
to start audio.

## Tests

| Suite | Command | Notes |
| --- | --- | --- |
| Native engine, MIDI, panel, files, goldens | `ctest --test-dir build -C Release --output-on-failure` or `./build/tests/vjn6_tests` | Catch2; every §13 criterion carries its AC ID in the test name, e.g. `./build/tests/vjn6_tests "*AC-E5.1*"`; tags such as `"[smoke]"` select groups |
| WASM (same suite under Node) | `web/build_tests.sh --tests-only` | emcc build in `build-wasm/`, run with Node; Catch2 arguments after `--`, e.g. `web/build_tests.sh --tests-only -- "[smoke]"` |
| Plugin tests | `ctest --test-dir build -C Release -R vjn6_plugin_tests --output-on-failure` | Catch2 executable `vjn6_plugin_tests` against the real `AudioProcessor`; on Linux run under `xvfb-run -a` |
| Licence and branding | `ctest --test-dir build -C Release -L licence` or `python3 tools/check_licences.py` | see "Licence check" below |
| pluginval (AC-PL1) | see below | strictness 5, VST3 on all desktop OSes, AU on macOS |
| Vitest (UI unit tests) | `npm test` | `npm run check` runs svelte-check / TypeScript |
| Playwright (end to end, Chromium + WebKit) | `npx playwright install --with-deps chromium webkit` once, then `npm run test:e2e` | needs `web/build.sh` first; servers on 5173 (COOP/COEP) and 5174 (no isolation, AC-SC2), movable with `VJN6_E2E_PORT` / `VJN6_E2E_NOISO_PORT`; screenshot baselines (AC-SC1, Chromium on Linux) are regenerated in Docker with `npm run test:e2e:baselines --workspace ui` (`-- --check` compares without updating) |

### pluginval

pluginval (Tracktion, GPL-3) is a separate tool: download it, never add it to the repository. Get the
release for your OS from https://github.com/Tracktion/pluginval/releases (`pluginval_macOS.zip`,
`pluginval_Windows.zip`, `pluginval_Linux.zip`), unpack it outside the checkout and run:

```sh
# macOS (the binary is pluginval.app/Contents/MacOS/pluginval)
pluginval --strictness-level 5 --validate-in-process --validate build/plugin/VJN6_artefacts/Release/VST3/VJN6.vst3
pluginval --strictness-level 5 --validate-in-process --validate ~/Library/Audio/Plug-Ins/Components/VJN6.component
# Windows
pluginval.exe --strictness-level 5 --validate-in-process --validate build\plugin\VJN6_artefacts\Release\VST3\VJN6.vst3
# Linux
xvfb-run -a ./pluginval --strictness-level 5 --validate-in-process --validate build/plugin/VJN6_artefacts/Release/VST3/VJN6.vst3
```

### Licence check

```sh
python3 tools/check_licences.py                         # R-L3 GPL/AGPL titles, R-L2 sources, R-L4 docs, R-L5 branding
python3 tools/check_licences.py --self-test             # AC-L1.1: plants the GPL title in a temporary copy, expects failure
python3 tools/check_licences.py --check metadata --metadata build/plugin   # R-L5 in the built plugin metadata
python3 tools/check_licences.py --check gpl             # one check: gpl, sources, docs, branding, metadata
```

The same checks are ctest tests named with their AC IDs (`AC-L1.1_*`, `AC-L2.1_*`, `AC-L4.1_*`, `AC-L5.1_*`,
label `licence`, from `cmake/LicenceTests.cmake`). Licences of everything shipped and used are in
[docs/LICENSES.md](docs/LICENSES.md).

## Benchmarks (§9.1, AC-N1.1)

106 mode, UNISON ×6, default BRASS patch with Chorus I, 2× oversampling, 48 kHz, 10 s, median of 5 runs;
each run prints its % of real time and µs per 128-sample block, and the program exits non-zero at or above
the gate (native 8 %, WASM 35 %):

```sh
./build/tools/bench/vjn6_bench                 # native (Release build; Xcode/VS: build/tools/bench/Release/)
web/build_tests.sh --bench-only                # WASM under Node 22
```

## Generated files

| File | Regenerate with | Check |
| --- | --- | --- |
| `banks/vjn6-default.vjn6bank`, `core/state/default_bank.inc` (default bank, §15.3) | edit `banks/default/*.json`, then `python3 tools/make_default_bank.py` | `python3 tools/make_default_bank.py --check` |
| `core/files/nfc_tables.inc` (NFC data for name normalisation) | `python3 tools/gen_nfc_tables.py` | the header records the Unicode version; regenerate with Python 3.13 (Unicode 15.1.0) to avoid spurious diffs |
| `tests/golden/display_table.json` (R-P5 / §4.8 reference values for AC-P5.1) | `./build/tools/display_table/vjn6_display_table tests/golden/display_table.json` (run from the repository root) | CI regenerates it and diffs |

Golden files are committed only once the criteria they depend on pass and are regenerated only together
with an intended change.

## Other tools

```sh
python3 tools/syx_inspect.py patch.syx     # decode SysEx messages (§4.5) and show how VJN6 would import the file
python3 tools/syx_inspect.py --brief bank.syx

# §15.4 measurement protocol (not a gate): MIDI files + results templates for 106 and 60 mode
python3 tools/capture/make_protocol.py -o vjn6-capture
# analyse a 96 kHz / 24-bit recording of vjn6-protocol-106.mid played in one take ...
python3 tools/capture/analyze.py --recording take.wav --mode 106 -o results-106.csv
# ... or one file per step (<step id>.wav), e.g. for a panel set by hand in 60 mode
python3 tools/capture/analyze.py --steps-dir takes-60 --mode 60 -o results-60.csv
```

The generated `results-template-<mode>.md` lists the procedure, every step's panel settings and notes, and
the model values; `analyze.py` fills the same table (CSV) from recordings of the hardware or of VJN6
itself, so both are measured with the same code. Standard library Python only.

## State and files

The state blob layout, its versioning rule and where each build stores it (standalone:
`~/Library/Application Support/VJN6/`, `%APPDATA%\VJN6\`, `~/.config/VJN6/`; web: IndexedDB `vjn6`) are in
[docs/STATE.md](docs/STATE.md). The UI ↔ core protocol is in [docs/BRIDGE.md](docs/BRIDGE.md).

## Continuous integration

- `.github/workflows/ci.yml`, the full suite, runs only on pull requests into `main` (opened, reopened,
  synchronize) and on manual dispatch: macOS universal (Xcode; Standalone, VST3, AU), Windows (MSVC;
  Standalone, VST3), Ubuntu 24.04 (webkit2gtk-4.1, xvfb; Standalone, VST3) and wasm (emsdk). It runs the
  licence check, ctest (core, plugin and licence tests), pluginval at strictness 5 (VST3 everywhere, AU on
  macOS), the native and WASM benchmarks, the golden check, svelte-check, Vitest and Playwright (Chromium +
  WebKit on Linux). Signing and notarisation steps run only when their secrets exist.
- `.github/workflows/smoke.yml` runs after a merge to `main`: core build, `vjn6_tests "[smoke]"` and the
  licence check.
- Nothing runs on pushes to development branches. Before opening a pull request, run the local gates:
  the desktop build and `ctest`, `web/build_tests.sh`, `npm run check`, `npm test`, `npm run test:e2e` and
  `python3 tools/check_licences.py`.

## Repository layout

```
CMakeLists.txt          options VJN6_BUILD_PLUGIN / VJN6_BUILD_WEB / VJN6_BUILD_TESTS
cmake/                  Determinism.cmake (strict FP flags), LicenceTests.cmake (licence ctest tests)
core/                   vjn6_core (C++17, no JUCE): include/vjn6/ public API, juno/ DSP and constants.h,
                        engine/, core/, panel/, sysex/, midi/, state/, files/, bridge/, util/
plugin/                 JUCE AudioProcessor, WebBrowserComponent editor, standalone app, plugin tests
web/                    worklet.cpp, build.sh (web module), build_tests.sh (WASM tests and benchmark)
ui/                     Svelte 5 + TypeScript UI (src/, tests/), Vite config; one bundle for every build
tests/                  Catch2 suite: engine/, midi/, panel/, files/, golden/, util/
banks/                  vjn6-default.vjn6bank and its sources default/*.json
tools/                  make_default_bank.py, gen_nfc_tables.py, check_licences.py, syx_inspect.py,
                        bench/ (vjn6_bench), display_table/, capture/ (§15.4 measurement protocol)
docs/                   BRIDGE.md, LICENSES.md, STATE.md
third_party/            JUCE (submodule, unmodified), Catch2 (submodule, tests only)
.github/workflows/      ci.yml (PRs into main), smoke.yml (after merge to main)
SPEC.md KICKOFF.md PLAN.md DECISIONS.md
```

## Licence

VJN6 is a personal project by Marco Funk, built on JUCE 9 under the JUCE Starter licence. Third-party
components and their licences are listed in [docs/LICENSES.md](docs/LICENSES.md). Roland, JUNO, Juno-60 and
Juno-106 are trademarks of Roland Corporation.
