# VJN6 licences

VJN6 is a personal project owned by Marco Funk (© 2026 Marco Funk). Its own code is released under the MIT
License (`LICENSE` in the repository root). The third-party components below keep their own licences and are
not covered by it, and neither are the Juno-60 factory patches in the default bank (© Roland Corporation;
see "Sources of numbers, not code"). This file lists every third-party component that ships inside a VJN6
build (SPEC R-L4), the tools used only to build and test it, and the material used only as a source of
numbers. `tools/check_licences.py` enforces the rules (R-L3, AC-L1.1, AC-L2.1, AC-L4.1, AC-L5.1); the README
says how to run it.

## Shipped components

These are compiled into, or bundled with, the standalone app, the VST3/AU plugin or the web page.

| Component | Version | Licence | Where and how |
| --- | --- | --- | --- |
| JUCE | 9.0.5 | JUCE 9 Starter (JUCE 9 End User Licence Agreement, free tier) | Git submodule `third_party/JUCE`, unmodified. Statically linked into the standalone app and the plugins. Not used by the web build or by `vjn6_core`. |
| VST3 SDK | 3.8.0 (bundled with JUCE) | MIT | Comes with JUCE (`juce_audio_processors_headless/format_types/VST3_SDK`); compiled into the VST3 plugin. |
| AudioUnitSDK | 1.1.0 (bundled with JUCE) | Apache-2.0 | Comes with JUCE; compiled into the AU plugin (macOS only). |
| Svelte | 5.x (see `ui/package.json`) | MIT | Its runtime is compiled into the one UI bundle (`ui/dist`) used by every build. |
| clsx, esm-env | as `ui/package.json` resolves them (Svelte runtime dependencies) | MIT | Imported by the Svelte runtime and compiled into the same UI bundle. |
| Barlow Condensed | woff2, latin subset | SIL OFL 1.1 | Bundled as woff2 in `ui/src/fonts/` with its licence text `OFL-BarlowCondensed.txt`. © 2017 The Barlow Project Authors. |
| Oswald | woff2, latin subset | SIL OFL 1.1 | Bundled as woff2 in `ui/src/fonts/` with its licence text `OFL-Oswald.txt`. © 2016 The Oswald Project Authors. |
| Emscripten runtime and system libraries (JS loader, musl libc, LLVM libc++/libc++abi, compiler-rt) | as the emsdk used for the build | MIT / University of Illinois NCSA; LLVM parts Apache-2.0 WITH LLVM-exception | Compiled into `ui/public/vjn6.wasm` and its loader `vjn6.js` by `web/build.sh` (web build only). |
| Microsoft Edge WebView2 SDK (static loader `WebView2LoaderStatic.lib`) | the NuGet package JUCE asks for | BSD-3-Clause (Microsoft WebView2 SDK licence) | Statically linked into the Windows standalone app and VST3 (`NEEDS_WEBVIEW2`). The WebView2 runtime itself is part of Windows and is not shipped. |

Release packages carry these licences as `NOTICES.txt` in every bundle and next to every standalone
executable: `tools/package/notices.py` writes VJN6's MIT License and the factory-patch attribution first,
then the components above (the WebView2 SDK licence on Windows only) plus the licence of every package that
JUCE's SPDX inventory lists as vendored inside a module VJN6 compiles, and fails when a licence file is
missing.

### JUCE licence terms (R-L1)

- JUCE is dual-licensed: AGPLv3 or the commercial JUCE 9 licence. VJN6 uses the commercial JUCE 9
  **Starter** tier, which is free and applies only while the licensee's revenue or funding over the
  previous 12 months is at most **$20,000** (for an individual: all revenue from use of JUCE; for a company:
  the whole company's and its affiliates').
- If that cap is exceeded, the options are a **JUCE Indie** licence or relicensing VJN6 under the
  **AGPLv3**.
- The JUCE EULA forbids combining JUCE with anything that would force JUCE under an open-source licence,
  so nothing licensed under the GPL or the AGPL is ever linked into VJN6 or shipped with it.
- JUCE vendors third-party code under permissive licences (zlib, MIT, BSD-3-Clause, ISC, Apache-2.0, IJG,
  libpng). Which of these end up in a binary depends on the JUCE modules and module options that
  `plugin/CMakeLists.txt` enables; the complete inventory with versions and licences is
  `third_party/JUCE/JUCE.spdx.json`. JUCE also vendors two SDKs that are dual-licensed with a GPL-3
  option, the ASIO SDK and the AAX SDK; neither is enabled: VJN6 builds no AAX format (§9.3:
  Standalone, VST3, AU) and leaves `JUCE_ASIO` off.

### System libraries (not shipped)

The desktop builds load these from the operating system at run time; VJN6 does not ship them: WebKitGTK
4.1, GTK, ALSA, JACK, FreeType, fontconfig and libcurl on Linux; WKWebView and the Apple frameworks on
macOS; the WebView2 runtime and the Windows SDK libraries on Windows.

## Build and test tools

Used only to build and test VJN6. None of them is linked into or shipped with a VJN6 build.

| Tool | Licence | Use |
| --- | --- | --- |
| Vite | MIT | UI dev server (`npm run web`, COOP/COEP headers) and production bundler (`npm run build`). |
| Vitest | MIT | UI unit tests (`npm test`). |
| Playwright (`@playwright/test` 1.65.0, with its Chromium and WebKit builds) | Apache-2.0 | End-to-end and screenshot tests in Chromium and WebKit (`npm run test:e2e`; CI on Ubuntu). The AC-SC1 baselines are rendered in the official `mcr.microsoft.com/playwright` Docker image (`npm run test:e2e:baselines`); nothing of it is committed or shipped. |
| axe-core and `@axe-core/playwright` 4.14.0 | MPL-2.0 | Accessibility (missing-name) checks in the end-to-end tests (AC-U3.1); test only, unmodified, never shipped. |
| @sveltejs/vite-plugin-svelte | MIT | Svelte compiler integration for Vite. |
| svelte-check | MIT | Type checks of the Svelte/TypeScript sources (`npm run check`). |
| TypeScript | Apache-2.0 | Type checking of the UI sources. |
| jsdom | MIT | DOM environment for Vitest. |
| Node.js | MIT | Runs Vite, Vitest, Playwright and the WASM test and benchmark builds. |
| Catch2 | BSL-1.0 | Native and WASM test framework; Git submodule `third_party/Catch2` (v3.16.1), linked only into `vjn6_tests`. |
| Emscripten (emsdk) | MIT / University of Illinois NCSA | Compiler toolchain for the web module and the WASM tests and benchmark. Its runtime libraries are listed under shipped components. |
| CMake | BSD-3-Clause | Build system (≥ 3.25). |
| Ninja | Apache-2.0 | Optional CMake generator. |
| Python 3 | PSF License | Tools in `tools/` (default bank, NFC tables, licence check, SysEx inspector, capture protocol). |
| pluginval (Tracktion) | GPL-3 | Plugin validator (AC-PL1). A separate executable that CI downloads at run time into the runner's temporary directory and runs against the built plugin. It is never committed to the repository, never linked and never shipped. |
| Inno Setup 6 | Inno Setup License (permissive, free) | Builds the Windows installer in the release workflow (`tools/package/windows.iss`). Installed on the runner and never committed. The installer it makes embeds Inno Setup's own setup program, which its licence lets anyone distribute. |
| Pamplejuce | MIT | Not used. VJN6 has its own CMake setup; listed because SPEC R-L2 names it. |

## Sources of numbers, not code

| Source | Licence | How VJN6 uses it |
| --- | --- | --- |
| Ultramaster KR-106, junox, Hera, mpc-vst-jn106 | GPL-3 | Used only as sources of numbers (constants, measurements, firmware facts) cited in SPEC.md and in `core/juno/constants.h` comments. Their code is never copied. Their repositories are never cloned into the project tree, never added as submodules, never fetched by CI and never linked. `tools/check_licences.py` (AC-L2.1) fails if any of them appears in the tree, in `.gitmodules`, in `.github/` or in another fetch-capable file. |
| pendragon-andyh/Juno60 chorus analysis | MIT | Measurement data (chorus LFO rates and delay swing) used as numbers; attribution: Andy Harman (pendragon-andyh), "Juno60" analysis repository. No code is used. |
| Holters & Parker, DAFx-18; Raffel & Smith, DAFx-10 | papers | Method and published constants for the BBD chorus; the implementation is original. |
| thea.codes, "The design of the Juno DCO" | text CC-BY-SA, code Apache-2.0 | Background reading; nothing copied. The DCO is written from SPEC R-E3. |
| Roland service notes and owner's manuals | Roland copyright | Numbers only (calibration targets, ranges, the SysEx format), plus the Juno-60 sample-sound settings and names of the next row. No scans, images or other text from them are in the repository. |
| Juno-60 Owner's Manual sample sounds (pp. 24–28) | Roland copyright | **Shipped as data** (SPEC R-L2, v3.2): the numeric settings of the 56 sample sounds and their patch names are slots 1–56 of the default bank (`banks/vjn6-default.vjn6bank` and the core's copy), generated from `banks/j60-factory-chart.csv` by `tools/make_default_bank.py`. Attribution, the manual's full names and notes: `docs/FACTORY_BANK.md`. The MIT License of VJN6's own code does not cover them. The VJN6 patch names contain no Roland marks (R-L5). |
| Juno-106 factory patch data ("The Original 128") | Roland | **Not shipped.** VJN6 imports `.syx` files that the user owns. |

## Trademarks

Roland, JUNO, Juno-60 and Juno-106 are trademarks of Roland Corporation. VJN6 is not affiliated with or
endorsed by Roland. These names and Roland's logos do not appear in the UI, the plugin metadata or the
product name (R-L5); the only exceptions are the bank manager's file-filter text "Juno-106 SysEx files
(\*.syx)" and the rejection notice "Not a VJN6 or Juno-106 file", which describe the file format.
