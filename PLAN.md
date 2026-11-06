# VJN6 build plan

Vertical slices in the order recommended by SPEC.md §14, each with the requirement and acceptance IDs it
satisfies. A slice is done when its tests pass natively (and under WASM once slice 5 exists) and it is committed.

## Slice 0 — Repository and scaffolding
- git repo, submodules `third_party/JUCE` (9.0.5, unmodified) and `third_party/Catch2` (v3.16.1, test only).
- Top-level `CMakeLists.txt` with `VJN6_BUILD_PLUGIN`, `VJN6_BUILD_WEB`, `VJN6_BUILD_TESTS`; strict FP flags
  (`-ffp-contract=off`, no fast-math) for determinism (§9.4).
- `core/` static library `vjn6_core` (no JUCE), public API of §9.4.
- Deterministic math (`core/juno/dmath.h`): own exp2/log2/exp/sin/cos/tan/tanh (no platform transcendental in the
  signal path).
- `tools/check_licences.py` (R-L3) → AC-L1.1, AC-L2.1 / L4.1 / L5.1 (grep part).
- DECISIONS.md, PLAN.md.

## Slice 1 — Voice path, 60 mode (R-E1, R-E2, R-E3, R-E4, R-E5, R-E6, R-E7, R-E8, R-E10, R-E12, R-E13)
- DCO (counter phase, BLEP-band-limited saw/pulse/sub, shared noise), DC blocker, HPF, VCF (ZDF OTA cascade,
  rational tanh), VCA, ADSR, LFO, half-band interpolator/decimator, output stage, variation generator.
- Control steps on the absolute sample counter, event timing at control boundaries.
- Test-build interface (§9.4): injection/capture points, forcing, probes.
- Measure c_cal per mode and oversampling factor on the solver, record in DECISIONS.md.
- Tests: AC-E1.1, AC-E2.1, AC-E2.2, AC-E3.1/3.3/3.4/3.5/3.6/3.7 (60), AC-E4.1, AC-E5.1–5.5, AC-E5.7, AC-E6.2,
  AC-E7.1/7.2/7.4 (60), AC-E8.1, AC-E8.3/8.4/8.5 (60), AC-E10.1, AC-E12.1, AC-E13.1–13.3.

## Slice 2 — Chorus (R-E9)
- Two BBD lines (clocked sampling + ZOH, continuous-time input/output filters), chorus LFO, wet HP, noise.
- Tests: AC-E9.1–AC-E9.5.

## Slice 3 — 106 control path (R-E1 tick, R-E3 divisors, R-E5 cutoff word, R-E6 PNP law, R-E7 firmware env,
R-E8 table LFO, R-E11 portamento and lever read)
- Tests: AC-E1.2, AC-E3.1/3.2 (106), AC-E5.1–5.6 (106), AC-E6.1, AC-E7.1/7.3/7.4 (106), AC-E8.2, AC-E8.3–8.5 (106),
  AC-E11.3, AC-E11.4.

## Slice 4 — Voices, MIDI, SysEx, panel logic, state, files (R-E11, R-M1–M3, R-P1–P7, R-S1–S2, R-D1–D6, F-2…F-12)
- Voice assignment (rotary / POLY 1 / POLY 2 / UNISON), hold, CC64, arpeggio incl. MIDI clock.
- MIDI parser (running status, SysEx reassembly, realtime inside SysEx), receive gating, transmit (rate limit).
- SysEx encode/decode (§4.5), panel state machine (slots, pending bank, Manual, EDITED, WRITE, key transpose,
  LED and button LEDs, LED messages), memory protect.
- State blob (§4.7, `docs/STATE.md`), file formats and name normalisation incl. NFC tables (§4.3, §4.6),
  default bank (`tools/make_default_bank.py`, `banks/`).
- Tests: AC-E11.1/11.2/11.5/11.6/11.7, AC-M1.1–M1.5, AC-M2.1, AC-M3.1–M3.4, AC-P1.1, AC-P2.1, AC-P4.1, AC-P4.2,
  AC-P5.1 (core side), AC-P7.1, AC-F3.1, AC-F4.1–F4.4, AC-F5.1, AC-F5.2, AC-F7.1, AC-F8.1, AC-F10.1 (core side),
  AC-F11.1–F11.4, AC-F12.1, AC-S2.1, AC-D1.1, AC-D1.2, AC-D3.1, AC-D4.1, AC-D4.2, AC-D6.1, AC-D7.1.

## Slice 5 — WASM tests, goldens, benchmarks
- The Catch2 test binary compiled with emcc, run in Node with identical expectations.
- Goldens AC-E1.3 (SHA-256 of 48 renders) committed once their criteria pass.
- `tools/bench` native + WASM → AC-N1.1.

## Slice 6 — Plugin and standalone (§4.8, §9.3, R-D2, F-1, F-13)
- JUCE `AudioProcessor`: 40 host parameters, 128 programs, state, latency, MIDI in/out, `WebBrowserComponent`
  editor serving `ui/dist`, fallback label, standalone state files (`state.bin`, `state.bad`, `bank.vjn6bank`).
- Tests: AC-E1.4 (plugin report), AC-PL2, AC-PL3, AC-S1.1, AC-S3.1, AC-M1.1 (plugin), AC-F1.1 (standalone),
  AC-F13.1 (standalone); pluginval strictness 5 → AC-PL1.

## Slice 7 — UI (§5, §7.3, §7.6, §7.7, §11)
- Svelte 5 + TypeScript: canvas scaling, both skins, all sections, LED, tooltips, typed entry, lever,
  bank manager, settings, `keyboard.ts`, `bridge.ts` (desktop + web), fonts bundled.
- Vitest: AC-K1.1, AC-K2.1, AC-K2.2, AC-P5.1 (TS formatter vs engine table), contrast part of AC-U3.1.

## Slice 8 — Web build and end-to-end (§5.7, §15.2, R-D5)
- `web/worklet.cpp`, `web/build.sh`, Vite dev/preview server with COOP/COEP, IndexedDB state, Web MIDI.
- Playwright (Chromium + WebKit): AC-B2, AC-SC1–SC3, AC-S3.2, AC-S3.3, AC-U1.1, AC-U2.1, AC-U3.1, AC-U4.1,
  AC-F1.1 (web), AC-F10.1, AC-F13.1 (web), AC-P1.1/P4.x through the UI where they are user-facing.

## Slice 9 — Delivery
- CI workflows (§15.1, within the owner's CI rules), README, docs/LICENSES.md, docs/STATE.md,
  `tools/capture/`, full-suite run, benchmarks, manual walk-through of §6 in both modes, REPORT.md.
