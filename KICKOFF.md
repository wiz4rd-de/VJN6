Build the application specified in SPEC.md. Work fully autonomously from start to finish. Do not ask questions or wait for confirmation at any point.

## Source of truth
- SPEC.md defines the product. Build exactly what it specifies, with no extra features, screens, settings or controls.
- Section 14 (Implementer's Latitude) lists what is yours to decide. Section 9.4 lists binding technical constraints. Everything else is decided.
- The engine formulas and constants in §7.1, the SysEx layout in §4.5, the LED texts and glyphs in §5.3–§5.4 and the host parameter table in §4.8 are exact. Treat them as data, not as suggestions.
- Every engine constant in §7 is tagged SN, CA, M or D. Put every M and D constant in `core/juno/constants.h` as one named value, with its tag in a comment.

## When something is unclear
If the spec does not cover a situation, choose the simplest behaviour consistent with the spec's overview (§1), glossary (§2) and existing rules — the one that adds no new state, setting or UI. Prefer what the original Juno-60 / Juno-106 does, where the spec's sources (§15.5) make that clear. Record the choice in DECISIONS.md (what, why, which spec section it relates to) and continue. Never stop to ask.

If two parts of the spec contradict each other, the more specific one wins: an acceptance criterion over a rule, and a rule over a table example. One exception, from the SPEC.md preface: SN and CA values and the §7 formulas are never changed to make a test pass. If an acceptance criterion cannot pass with them, retune the D or M constant involved by the smallest change that passes. Record every conflict and every retuned constant (old value, new value, reason) in DECISIONS.md.

Measure the VCF calibration factor c_cal on your own filter solver (R-E5) before you write the calibration tests, and record the measured values.

## External services
There are none (§10). VJN6 makes no network requests and needs no credentials. Tests use the Web MIDI and audio test doubles that §10 describes, and the test-build injection and capture points of §9.4. Never block on missing hardware, browsers, devices or signing secrets. Anything that needs them is listed in §15.4 and is not part of your acceptance. The GPL-3 projects named in R-L2 (KR-106, junox, Hera, mpc-vst-jn106) are sources of numbers only: never clone, copy, fetch or link them.

## How to work
1. Read SPEC.md fully. Write PLAN.md: a task list ordered into vertical slices, each mapped to the R-/F-/AC- IDs it satisfies. The recommended order in §14 is a good default.
2. Set up the repository so it builds, runs and tests from a clean checkout on macOS, Windows, Ubuntu and with emcc. Set up CI (§15.1).
3. Implement slice by slice. After each slice, run all tests, fix failures, and commit.
4. Implement every acceptance criterion in §13 as an automated test, with the AC ID in the test name. Engine criteria run natively and under WASM with identical expectations. User-facing flows get end-to-end tests that drive the real UI (Playwright, Chromium and WebKit). Commit golden files (AC-E1.3, AC-P4.1, AC-SC1) only once the criteria they depend on pass, and regenerate them only together with an intended change.
5. When all slices are done:
   1. Run the full suite and the benchmarks (§9.1).
   2. Start the standalone app and `npm run web`.
   3. Exercise every flow in §6 yourself through the front panel and the bank manager, in both 60 and 106 mode.
   4. Fix anything broken.

## Definition of done
- Every AC in SPEC.md §13 has a passing automated test, natively and under WASM where §13 says so.
- The plugin passes pluginval at strictness 5 (AC-PL1).
- The licence check passes, and docs/LICENSES.md and docs/STATE.md exist.
- Everything builds and runs from a clean checkout with the commands documented in README.md.
- No TODOs, placeholder screens or stubbed features remain, except the test doubles defined in §10 and the test-build interface of §9.4.

## Final report
When done, write REPORT.md:
- what was built;
- how to run it;
- test results (count and pass rate per suite, benchmark numbers);
- the measured c_cal values and every D or M constant you retuned;
- every entry in DECISIONS.md;
- known gaps, including anything from §15.4 a human now needs to check.
