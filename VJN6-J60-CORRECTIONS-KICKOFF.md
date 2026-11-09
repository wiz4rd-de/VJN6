# VJN6 — Juno-60 mode corrections (kickoff, spec v3.2)

You are working in the existing VJN6 repository, built from SPEC.md v3.1 and KICKOFF.md. This iteration corrects **60 mode only**, using Roland's Juno-60 Owner's Manual as the primary source. It also replaces the default bank with the 56 Juno-60 factory patches. Work fully autonomously from start to finish. Do not ask questions or wait for confirmation.

## Scope and guardrails

- **Only 60 mode changes.** No 106-mode rule, formula, constant, control, legend, layout position, LED behaviour, MIDI/SysEx behaviour or host-parameter string may change. The one exception is the value strings of `f_oct_transpose`, a parameter used only by 60 mode (C-3). Every existing 106-mode test and every 106-mode golden (AC-E1.3) must pass **unchanged and bit-identical**. The only exceptions are the expectations that change because of the new default bank, and C-8 lists them (AC-D7.1 names, bank-manager screenshots).
- **The one shared change** is the default bank (C-7). It is mode-independent data, so 106 mode will also start up with it. That is intended. No 106 code changes to support it.
- **The engine stays as it is.** No change to any §7.1 formula or (SN)/(CA)/(M)/(D) constant. The only engine-adjacent change is that 60 mode ignores RANGE (C-2).
- **The data model stays as it is.** §4.1 fields, §4.5 SysEx, §4.6 file formats, §4.7 state layout and the §4.8 parameter IDs, types and order do not change. Stored bytes stay lossless across mode switches.
- Everything in KICKOFF.md still applies: the "when something is unclear" rule, DECISIONS.md, the precedence rule, the definition of done and the final report.

## Evidence (Juno-60 Owner's Manual)

Page numbers are the manual's printed numbers (PDF page = printed + 2). The fold-out panel diagram is unnumbered (PDF p. 5).

| # | Finding | Where |
| --- | --- | --- |
| E1 | **There is no separate square-wave control, and none is missing.** The DCO has three waveform buttons: pulse, sawtooth and sub. A square is the pulse wave at 50 % (PWM MODE = MAN, PWM at 0); the manual's waveform table draws it that way. The sub-oscillator is "a square wave one octave lower". The spec's R-E3 (duty = 0.50 + 0.45·cv) already does this. **No change.** | p. 12, p. 13, p. 32 |
| E2 | **The Juno-60 has no DCO RANGE (16′/8′/4′) control.** The DCO section is: LFO, PWM, PWM MODE switch, pulse/saw/sub buttons, SUB OSC, NOISE. The panel's only RANGE switch belongs to the arpeggio. Octaves come from OCTAVE TRANSPOSE (DOWN/NORMAL/UP) in the controller section. | panel diagram, p. 12, p. 32 |
| E3 | **Octave transpose is labelled D/N/U** (DOWN/NORMAL/UP), not L/N/H. | p. 24 remarks, p. 32 |
| E4 | **VCA LEVEL is a centre-zero slider marked −5…+5.** Every other slider is 0–10. The factory charts give signed values (−3…+5). The manual describes it as the amount of envelope signal sent to the VCA, used to balance patch volumes ("set too high, a sound distortion might occur"). | p. 16 (slider legend), pp. 25–28 |
| E5 | **The VCF section order is FREQ, RES, polarity switch, ENV, LFO, KYBD.** The spec lists the polarity switch last. | panel diagram, p. 14, chart headers pp. 25–28 |
| E6 | **CHORUS has three buttons: OFF, I and II.** Only I and II have LEDs. The manual does not mention I+II; the spec keeps it from the research (three recording analyses). | panel diagram, p. 22, chart columns |
| E7 | **The other slider and switch orders match the spec:** LFO (RATE, DELAY TIME, TRIG MODE switch); DCO (LFO, PWM, PWM MODE, buttons, SUB OSC, NOISE); HPF (4 positions); VCA (ENV/GATE switch, LEVEL); ENV (A, D, S, R). Patch byte order (§4.1, the 106 SysEx order) is storage, not layout, and does not change. | panel diagram, charts |
| E8 | **The manual prints complete settings for all 56 factory patches** (11–78), with names. The charts also give OCTAVE TRANSPOSE and LFO TRIG MODE for each patch. The manual notes that "the settings shown include the knob positions which are irrelevant to the sound" (e.g. SUB OSC level with the sub button off). Bank 7 (71–78) includes patches whose sound source is VCF self-oscillation, so the manual says to tune them with FREQ. | p. 24 (names, remarks), pp. 25–28 (settings) |

## Changes

Make each change in SPEC.md first (bump to v3.2 and add a short changelog under the title listing C-1…C-8), then in the code and tests. Each item names the spec sections to edit.

### C-1 Square wave: confirm, don't change (E1)
Nothing to build. Add one sentence to R-E3: "The Juno-60 has no separate square control; a square is the pulse at PWM 0 (MANUAL), and the sub is a square one octave down." Do not add a square button or setting.

### C-2 No RANGE in 60 mode (E2)
- §4.1: W0 gets a 60-mode reading. Add a "60 mode reads" note to the W0 row: **1 (8′)**. The stored value is kept (lossless, like W3 in 106 mode).
- R-E3 pitch: in 60 mode the `range` term is always 0 cents. Octave shifts come only from octave transpose.
- §5.2: remove the RANGE buttons from the 60-mode DCO box. The 106-mode DCO box is unchanged. Keep every 60-mode control that 106 mode also has in the same position (§5.2 rule). Close the gap in 60 mode only, as long as no shared control moves. Otherwise leave it as empty panel. Record the choice in DECISIONS.md.
- §5.2 button behaviour and LED table: RANGE applies to 106 mode only.
- R-M1: CC12 in 60 mode still writes W0 (no audible effect). §4.8: `s_range` stays exposed, with no effect in 60 mode (existing rule).
- §8: add the edge case "Patch with 16′ or 4′ loaded in 60 mode → plays at 8′; the stored W0 is kept and applies again in 106 mode."
- Default bank: the eight original patches keep their bytes. For each one with W0 ≠ 1, record in DECISIONS.md that its 60-mode octave changes.

### C-3 Octave transpose labels D/N/U (E3)
- §4.2: `0 D (−12), 1 N, 2 U (+12)`. The values and default (1) are unchanged, so state stays compatible.
- §5.2 controller box (60): OCTAVE TRANSPOSE `D/N/U`. §4.8 value strings for `f_oct_transpose`: `D`, `N`, `U`. This parameter exists only in 60 mode.

### C-4 VCA LEVEL is centre-zero in 60 mode (E4)
- **Engine unchanged:** R-E6 60 mode still uses `out = x_vcf · e · (B10/127)`. The display scale changes; the byte and the gain law do not. So byte 0 = −5 (gain 0), byte 64 = 0 (gain 0.504, −6.0 dB), byte 127 = +5 (gain 1.0, the R-E2 reference). Mark the law as (D) and add it to §15.4: "60 VCA LEVEL: linear, −5 = silent assumed; measure gain at −5, 0, +5".
- R-P5 (60 mode only): the VCA LEVEL slider shows and accepts **v = byte/12.7 − 5**, one decimal, signed (`-5.0`, `0.0`, `+2.0`). Typed entry −5.0…+5.0 commits round((v + 5) × 12.7), with out-of-range values clamped. The tick scale reads −5…+5 with 0 at the centre. The tooltip's engine value is still dB (byte 64 → `-6.0 dB`, byte 0 → `-inf dB`).
- §4.8: the value string of `p_vca_level` follows the active mode: signed −5.0…+5.0 in 60 mode, 0.0–10.0 in 106 mode (unchanged). R-P6 (panel = tooltip = host string) still holds within each mode.
- **Rounding**, used everywhere in this iteration: round(x) = floor(x + 0.5). Every chart value in C-7 round-trips through byte → display → byte exactly.

### C-5 VCF order in 60 mode (E5)
§5.2 VCF box, 60 mode: **FREQ, RES, polarity switch (+/−), ENV, LFO, KYBD**. 106 mode is unchanged. Adjust the R-U3 Tab order for 60 mode to match.

### C-6 Chorus buttons in 60 mode (E6)
- §5.2, 60 mode: three buttons **OFF, I, II**. OFF has no LED. I is lit for W4 = 1 or 3. II is lit for W4 = 2 or 3.
- Behaviour: OFF, I and II are radio buttons writing W4 = 0, 1 or 2. **I+II** (W4 = 3) is a combination press: add `CHORUS I ↔ CHORUS II` to the 60-mode R-U4 partners. Shift+click on I while II is lit, or on II while I is lit, gives W4 = 3. Repeating it while already in I+II does nothing. A plain click on I or II while in I+II selects that single mode.
- The tooltips of I and II name the Shift+click. R-E9 is unchanged.

### C-7 Factory bank as the default bank (E8)
- **R-L2:** replace the "Factory patch data" row with two rows:
  - Juno-60 Owner's Manual sample-sound settings (pp. 24–28): the numeric settings and patch names are **shipped** in the default bank, with attribution in `docs/FACTORY_BANK.md`.
  - Juno-106 "The Original 128": still **not shipped**.
  - Also amend the "Roland service notes and manuals" row to allow these numbers and names.
  - R-L5 applies unchanged, and patch names contain no Roland or Juno marks. The source JSON under `banks/` and the attribution in `docs/` may name the manual.
- **§15.3 default bank layout:**

  | Slots | Content |
  | --- | --- |
  | 1–56 | the 56 factory patches in manual order (slot = (bank − 1) × 8 + patch, so 60-mode LED `11`…`78` matches the manual) |
  | 57 | `INIT` (R-P7) |
  | 58–65 | the eight original patches, unchanged bytes and names, in the existing order `BRASS`, `STRINGS`, `PAD`, `BASS`, `PLUCK`, `ORGAN`, `SWEEP`, `SFX` |
  | 66–128 | `INIT` |

  First run (R-D6) is therefore slot 1 = `Strings 1`.
- **Mapping from chart to patch** (implement it in `tools/make_default_bank.py`, reading the chart CSV below committed as `banks/j60-factory-chart.csv`, and generating `banks/default/*.json`):
  - 0–10 sliders: byte = round(v × 12.7). B0 rate, B1 delay, B2 DCO LFO, B3 PWM, B4 noise, B5 FREQ, B6 RES, B7 ENV, B8 VCF LFO, B9 KYBD, B11–B14 A D S R, B15 SUB OSC level.
  - B10 VCA LEVEL: byte = round((v + 5) × 12.7).
  - W1, W2, W3 pulse, saw, sub (1 = `*`).
  - W4 chorus: OFF 0, I 1, II 2 (`II?` → 2).
  - W5 PWM mode: L 0, M 1, E 2.
  - W6 polarity: N 0, I 1.
  - W7 VCA: E 0, G 1.
  - W8 HPF = the chart value 0–3.
  - **W0 encodes the chart's octave transpose:** D → 0 (16′), N → 1 (8′), U → 2 (4′). 60 mode ignores W0 (C-2), so the 60 sound is unaffected. 106 mode gets each patch in its intended octave. Record this in DECISIONS.md.
  - Keep "irrelevant" knob positions exactly as charted (e.g. SUB OSC level with sub off). In 106 mode those patches play the sub at that level, because 106 mode reads W3 = 1. That is accepted.
  - Names: the 12-character names in the byte table below. The manual's full names go in `docs/FACTORY_BANK.md` and in the source JSON's `manual_name`.
- **Not stored per patch:** the charts' OCTAVE TRANSPOSE and LFO TRIG MODE stay performance settings (§4.2), as on the spec's model of the hardware. Keep them in the source JSON (`chart_performance`) and list them in `docs/FACTORY_BANK.md`, so a player can set the switches. Add to §15.4: "Does the Juno-60 store LFO TRIG MODE (and octave transpose) in a patch? If it does, move it into the patch." This affects the sound of 21, 33, 51, 52, 54 and 57, which use MANUAL trigger with DCO vibrato. In AUTO they get constant vibrato after the delay.
- **Patch 36 (Pizzicato Sound 2):** chorus II is inferred. The scan's right edge on p. 26 is cut off, and OFF and I are both blank. Note it in `docs/FACTORY_BANK.md` and §15.4.
- **Bank 7:** add a note to `docs/FACTORY_BANK.md`. These patches use no DCO waveform. 71–76 and 78 are VCF self-oscillation at RES 10, pitched by FREQ, as the manual says. 77 (Surf) is a noise patch at RES 0. 71, 73, 75 and 76 also add noise.

### C-8 Tests (add or modify; AC IDs in test names)
- **AC-E3.1**, 60 mode: W0 = 0, 1, 2 all give the 8′ pitch. 106 mode is unchanged.
- **AC-P1.1**, 60 mode: no RANGE control. The chorus gives off/I/II through the radio buttons and I+II through the combination press. The VCF control order is as C-5.
- **AC-P4.1:** update the golden matrix for the 60-mode chorus LEDs (OFF has no LED; I+II lights I and II). Leave the 106 rows untouched.
- **AC-P5.1**, 60 mode: VCA LEVEL at bytes 0, 64, 127 shows `-5.0`, `0.0`, `+5.0` on the panel, in the tooltip and in the host string, with the R-P5 dB strings for gains 0, 0.504 and 1.0 (`-inf dB`, `-6.0 dB` and 0 dB in R-P5's format). 106 mode is unchanged.
- **AC-PL2:** `f_oct_transpose` strings `D`, `N`, `U`. `p_vca_level` strings follow the mode.
- **AC-U1.1**, 60 mode: typing `-2` into VCA LEVEL commits byte 38, `+5` commits 127, `9` clamps to 127.
- **AC-U4.1**, 60 mode: with II lit, Shift+click I gives W4 = 3 with I and II lit. Shift+click again: unchanged. A click on I gives W4 = 1.
- **New AC-D7.2:** the generated default bank equals the byte table below for slots 1–56, byte for byte and name for name. Slot 57 equals INIT. Slots 58–65 equal the previous originals.
- **AC-D7.1:** names follow the new §15.3. Slots 1–9 still play note 60 in both modes with peak > −60 dBFS.
- **AC-E1.3 goldens:** keep using the eight original patches (now slots 58–65), selected by content, not slot number. 106-mode goldens must be bit-identical. 60-mode goldens may change only for originals with W0 ≠ 1. Regenerate exactly those and list them in DECISIONS.md.
- **§9.1 benchmark:** keep using the original `BRASS` (slot 58). The factory `Brass` is slot 7, so select by content.
- **AC-SC1:** regenerate the 60-mode screenshots and the bank-manager screenshots (the names changed). The 106-mode panel screenshots must not change.
- **AC-U3.1:** Tab order in 60 mode follows C-5 and C-6.

## Factory patch data

### Chart values as printed (commit as `banks/j60-factory-chart.csv`)
Columns follow the chart left to right. `pulse`, `saw`, `sub` are 1 where the chart has `*`. `level` is the signed VCA LEVEL. `chorus` is OFF, I or II; `II?` marks patch 36 (C-7). This table was transcribed twice, independently, from 300-dpi renders of pp. 25–28, and the two transcriptions agree in all 1,512 cells.

```csv
id,oct,rate,delay,trig,dco_lfo,pwm,pwm_mode,pulse,saw,sub,sub_lvl,noise,hpf,freq,res,pol,env,vcf_lfo,kybd,vca,level,a,d,s,r,chorus
11,N,6,0,A,0,0,L,0,1,0,0,0,0,7,0,N,0,0,10,E,0,4,0,10,4.5,I
12,N,4,0,A,0,6,L,1,1,0,0,0,0,7,0,N,0,0,10,E,-2,4,0,10,4.5,II
13,N,3,8,M,0,7,L,1,1,1,10,0,0,5,0,N,0,0,10,E,-2,3,0,10,6,II
14,N,2,8,A,0,5,M,1,0,1,10,0,0,4,6,N,4.5,0,10,G,0,0,0,0,0,I
15,N,5,4,A,0,5.5,L,1,0,1,8,0,0,3.5,5.5,N,4,0,10,G,0,0,1,0,1,I
16,U,5,4,A,0,5.5,L,1,0,1,8,0,0,3.5,5.5,N,3.5,0,10,G,0,0,1,0,1,II
17,N,5,6.5,A,1.5,0,M,0,1,0,0,0,0,0,0,N,8.5,0,4,E,2,2.5,4,6,2,I
18,N,6,0,A,0,10,E,1,1,0,10,0,0,3,1,N,5.5,0,10,G,-1,2,4,4,3,I
21,N,6,3,M,4.5,6,M,1,0,0,0,0,0,1,0,N,7,0,4,E,2,0,8,1.5,3,OFF
22,U,4,0,A,0,4,M,1,0,1,4.5,0,0,3.5,0,N,2.5,2,8,E,3,0,7.5,0,3.5,OFF
23,N,3.5,6,A,0,5,E,1,1,0,10,0,1,3.5,8,N,0,0,10,E,1,0,6.5,2,5.5,OFF
24,N,5,0,M,0,5,M,1,0,0,10,0,0,3,0,N,2.5,1,9,E,2,1,7.5,2,8.5,I
25,U,5,4,A,0,3,M,1,0,1,7,0,1,3,0,N,5,0,7,E,-1,0,6,3.5,2.5,I
26,U,5.5,6,A,0,2,M,1,0,1,8.5,0,1,5,2.5,N,3,0,10,E,0,0,5,1.5,5,II
27,N,6,6,A,0,6,M,1,0,0,10,0,2,3,0,N,4.5,1.5,5,E,4,0,5.5,3.5,6.5,OFF
28,N,3,8,M,0,0,M,0,1,0,10,0,0,3,0,N,5,0,8,E,1,0,5.5,3,5,I
31,D,5,6,A,0,5,M,1,1,1,3,0,0,3,2.5,N,3.5,0,0,G,0,0,4,1,2.5,I
32,D,5,6,A,0,5,M,1,1,0,3,0,0,3,5,N,4.5,0,5,G,-1,0,3,3.5,2.5,I
33,D,6,2.5,M,4,8,M,1,0,0,0,0,0,0,3,N,8,0,6,E,2,0,5,3.5,1.5,I
34,D,1,0,A,0,8,M,1,0,0,10,0,1,5.5,7,N,2,2.5,7,E,5,0,4.5,2,2,OFF
35,N,5,6,A,0,3.5,M,1,0,0,3,0,0,4.5,3,N,3,3,10,E,3,0,2,3.5,5.5,I
36,U,5,6,A,0,2,M,1,0,1,3,0,0,5,3,N,3,0,10,E,1,0,3,3,4,II?
37,U,5,0,A,0,5,M,0,0,1,10,0,1,4,5,N,3,0,6,E,5,0,3.5,0,3.5,OFF
38,U,5,0,A,0,0,M,1,0,0,0,0,1,4.5,5,N,3,0,6,E,4,0,3,2.5,5,OFF
41,N,6,6,A,2,0,L,0,1,0,0,0,1,6.5,0,N,0,0,10,E,2,4,0,10,4,OFF
42,N,2.5,6.5,A,1.5,0,M,0,1,0,0,0,0,0,0,N,8.5,0,4,E,2,2.5,4,6,2,OFF
43,N,2.5,7,A,0,0,M,0,1,0,0,0,0,2,0,N,5.5,2,4,E,2,4,5,6,3,OFF
44,D,2.5,7,A,1.5,0,M,0,1,0,0,0,0,1.5,0,N,6,0,4,E,5,3,4,4,3,OFF
45,U,5.5,5,A,0,0,M,0,1,0,0,1.5,1,5,0,N,0,2,6,E,5,2,6,5,2.5,OFF
46,N,5,6.5,A,1.5,0,M,1,0,0,0,0,1,5,3,N,2.5,0,6,E,1,2.5,6,6,2.5,OFF
47,N,5.5,6.5,A,1.5,6.5,M,1,0,0,0,0,3,4.5,5,N,2.5,0,5,E,5,2,6,6,2.5,OFF
48,D,5,7,A,2,6.5,M,1,0,0,0,0,3,5,7,N,0,1.5,5,E,5,2,6,6,2.5,OFF
51,N,6,2,M,3,0,M,0,1,0,0,0,1,1.5,7.5,N,5,2,5,E,3,2.5,4,10,1,OFF
52,N,6,2,M,3,0,M,0,1,0,0,0,0,3,7,N,4.5,0,6,G,2,3,3,4,2,OFF
53,N,6,2,M,0,8,M,1,1,0,0,0,0,6,2,N,3,0,2,E,-2,0,7,2,2,I
54,N,6,2,M,4,0,M,1,0,0,0,0,0,1,6,N,7,0,5,G,1,0,8.5,5,1,I
55,U,0,0,A,0,0,M,0,0,1,10,0,1,2.5,2,N,5.5,0,10,E,3,0,3,2,0,OFF
56,U,3,8,M,0,0,M,0,0,1,10,0,0,2,0,N,6,0,8,E,0,0,5.5,3,6,I
57,U,6,2,M,2,5,M,0,0,1,10,0,0,3,2,N,3,0,10,E,1,2.5,0,10,2,OFF
58,N,3,0,M,0,5,L,1,0,1,10,0,0,8,0,N,0,0,10,E,-3,3,0,10,4,II
61,N,4.5,6,A,0,6.5,M,1,0,1,7.5,0,0,2.5,0,N,5,2,7,G,0,0,2,5,2.5,II
62,U,4.5,6,A,1.5,10,M,1,1,1,7,0,0,6.5,0,I,4.5,0,7,E,2,0,5,0,5.5,I
63,N,5.5,9,A,0,3,L,0,1,1,6.5,0,0,6.5,3,I,4,0,1,G,-1,6.5,5.5,2,6.5,I
64,N,5.5,0,A,2,0,E,0,1,0,0,0,0,6.5,5,N,5.5,0,10,E,1,0,8,8,9,I
65,N,3,2.5,A,0,6,M,1,1,1,10,0,0,7.5,6,I,5,0,4.5,G,-3,6,5,0,0,I
66,N,6,7,A,2,4.5,M,1,0,1,10,0,0,6.5,7,I,5.5,0,10,G,-2,0,8,0,3,I
67,N,6,8,A,2,8,E,1,1,0,10,0,0,8,7,I,6,2.5,0,E,0,0,10,0,10,OFF
68,N,3,3,A,0,6,M,1,1,0,8,0,0,2,8.5,N,6,0,10,E,-3,10,10,10,10,I
71,U,0,0,A,0,0,E,0,0,0,0,10,1,4,10,N,1.5,0,10,E,5,0,3,0,4,OFF
72,N,0,0,A,0,0,E,0,0,0,0,0,1,5,10,I,3.5,0,10,E,5,0,3,0,4,OFF
73,U,5.5,5,A,0,0,M,0,0,0,0,2,1,3.5,10,N,1.5,2,10,E,3,3,0,10,1,OFF
74,N,5.5,4,A,0,0,E,0,0,0,0,0,1,3.5,10,N,0,2,10,E,5,0,4,5.5,7,OFF
75,U,6,0,A,0,0,M,0,0,0,0,2,0,0,10,N,7,4,10,E,-1,0,6,10,8,I
76,U,6,0,A,0,0,M,0,0,0,0,2,0,5,10,I,4,0,10,E,0,0,10,0,8,I
77,N,0,0,A,0,0,E,0,0,0,10,10,0,6,0,N,0,6,10,E,4,0,4,10,8,OFF
78,N,0,0,M,0,0,E,0,0,0,0,0,0,2,10,N,4,0,10,E,1,0,5,0,6,OFF
```

### Expected patch bytes (AC-D7.2)
Generated from the chart with the C-7 mapping. Columns: slot, manual patch number, VJN6 name, B0–B15 in §4.1 order, then W0–W8, then the chart's performance settings, which are not stored.

| Slot | J60 | Name | B0–B15 | range pulse saw sub chorus pwm_src pol vca hpf | Oct | Trig |
|---|---|---|---|---|---|---|
| 1 | 11 | Strings 1 | 76 0 0 0 0 89 0 0 0 127 64 51 0 127 57 0 | 1 0 1 0 1 0 0 0 0 | N | A |
| 2 | 12 | Strings 2 | 51 0 0 76 0 89 0 0 0 127 38 51 0 127 57 0 | 1 1 1 0 2 0 0 0 0 | N | A |
| 3 | 13 | Strings 3 | 38 102 0 89 0 64 0 0 0 127 38 38 0 127 76 127 | 1 1 1 1 2 0 0 0 0 | N | M |
| 4 | 14 | Organ 1 | 25 102 0 64 0 51 76 57 0 127 64 0 0 0 0 127 | 1 1 0 1 1 1 0 1 0 | N | A |
| 5 | 15 | Organ 2 | 64 51 0 70 0 44 70 51 0 127 64 0 13 0 13 102 | 1 1 0 1 1 0 0 1 0 | N | A |
| 6 | 16 | Organ 3 | 64 51 0 70 0 44 70 44 0 127 64 0 13 0 13 102 | 2 1 0 1 2 0 0 1 0 | U | A |
| 7 | 17 | Brass | 64 83 19 0 0 0 0 108 0 51 89 32 51 76 25 0 | 1 0 1 0 1 1 0 0 0 | N | A |
| 8 | 18 | Phase Brass | 76 0 0 127 0 38 13 70 0 127 51 25 51 51 38 127 | 1 1 1 0 1 2 0 1 0 | N | A |
| 9 | 21 | Piano 1 | 76 38 57 76 0 13 0 89 0 51 89 0 102 19 38 0 | 1 1 0 0 0 1 0 0 0 | N | M |
| 10 | 22 | Piano 2 | 51 0 0 51 0 44 0 32 25 102 102 0 95 0 44 57 | 2 1 0 1 0 1 0 0 0 | U | A |
| 11 | 23 | Celesta | 44 76 0 64 0 44 102 0 0 127 76 0 83 25 70 127 | 1 1 1 0 0 2 0 0 1 | N | A |
| 12 | 24 | Mellow Piano | 64 0 0 64 0 38 0 32 13 114 89 13 95 25 108 127 | 1 1 0 0 1 1 0 0 0 | N | M |
| 13 | 25 | Harpsichd 1 | 64 51 0 38 0 38 0 64 0 89 51 0 76 44 32 89 | 2 1 0 1 1 1 0 0 1 | U | A |
| 14 | 26 | Harpsichd 2 | 70 76 0 25 0 64 32 38 0 127 64 0 64 19 64 108 | 2 1 0 1 2 1 0 0 1 | U | A |
| 15 | 27 | Guitar | 76 76 0 76 0 38 0 57 19 64 114 0 70 44 83 127 | 1 1 0 0 0 1 0 0 2 | N | A |
| 16 | 28 | Synth Harp | 38 102 0 0 0 38 0 64 0 102 76 0 70 38 64 127 | 1 0 1 0 1 1 0 0 0 | N | M |
| 17 | 31 | Bass 1 | 64 76 0 64 0 38 32 44 0 0 64 0 51 13 32 38 | 0 1 1 1 1 1 0 1 0 | D | A |
| 18 | 32 | Bass 2 | 64 76 0 64 0 38 64 57 0 64 51 0 38 44 32 38 | 0 1 1 0 1 1 0 1 0 | D | A |
| 19 | 33 | Clavichord 1 | 76 32 51 102 0 0 38 102 0 76 89 0 64 44 19 0 | 0 1 0 0 1 1 0 0 0 | D | M |
| 20 | 34 | Clavichord 2 | 13 0 0 102 0 70 89 25 32 89 127 0 57 25 25 127 | 0 1 0 0 0 1 0 0 1 | D | A |
| 21 | 35 | Pizzicato 1 | 64 76 0 44 0 57 38 38 38 127 102 0 25 44 70 38 | 1 1 0 0 1 1 0 0 0 | N | A |
| 22 | 36 | Pizzicato 2 | 64 76 0 25 0 64 38 38 0 127 76 0 38 38 51 38 | 2 1 0 1 2 1 0 0 0 | U | A |
| 23 | 37 | Xylophone | 64 0 0 64 0 51 64 38 0 76 127 0 44 0 44 127 | 2 0 0 1 0 1 0 0 1 | U | A |
| 24 | 38 | Glockenspiel | 64 0 0 0 0 57 64 38 0 76 114 0 38 32 64 0 | 2 1 0 0 0 1 0 0 1 | U | A |
| 25 | 41 | Violine | 76 76 25 0 0 83 0 0 0 127 89 51 0 127 51 0 | 1 0 1 0 0 0 0 0 1 | N | A |
| 26 | 42 | Trumpet | 32 83 19 0 0 0 0 108 0 51 89 32 51 76 25 0 | 1 0 1 0 0 1 0 0 0 | N | A |
| 27 | 43 | Horn | 32 89 0 0 0 25 0 70 25 51 89 51 64 76 38 0 | 1 0 1 0 0 1 0 0 0 | N | A |
| 28 | 44 | Tuba | 32 89 19 0 0 19 0 76 0 51 127 38 51 51 38 0 | 0 0 1 0 0 1 0 0 0 | D | A |
| 29 | 45 | Flute | 70 64 0 0 19 64 0 0 25 76 127 25 76 64 32 0 | 2 0 1 0 0 1 0 0 1 | U | A |
| 30 | 46 | Clarinet | 64 83 19 0 0 64 38 32 0 76 76 32 76 76 32 0 | 1 1 0 0 0 1 0 0 1 | N | A |
| 31 | 47 | Oboe | 70 83 19 83 0 57 64 32 0 64 127 25 76 76 32 0 | 1 1 0 0 0 1 0 0 3 | N | A |
| 32 | 48 | English Horn | 64 89 25 83 0 64 89 0 19 64 127 25 76 76 32 0 | 0 1 0 0 0 1 0 0 3 | D | A |
| 33 | 51 | Funny Cat | 76 25 38 0 0 19 95 64 25 64 102 32 51 127 13 0 | 1 0 1 0 0 1 0 0 1 | N | M |
| 34 | 52 | Wah Brass | 76 25 38 0 0 38 89 57 0 76 89 38 38 51 25 0 | 1 0 1 0 0 1 0 1 0 | N | M |
| 35 | 53 | Phase Combi | 76 25 0 102 0 76 25 38 0 25 38 0 89 25 25 0 | 1 1 1 0 1 1 0 0 0 | N | M |
| 36 | 54 | Reed 1 | 76 25 51 0 0 13 76 89 0 64 76 0 108 64 13 0 | 1 1 0 0 1 1 0 1 0 | N | M |
| 37 | 55 | Popcorn | 0 0 0 0 0 32 25 70 0 127 102 0 38 25 0 127 | 2 0 0 1 0 1 0 0 1 | U | A |
| 38 | 56 | Reed 2 | 38 102 0 0 0 25 0 76 0 102 64 0 70 38 76 127 | 2 0 0 1 1 1 0 0 0 | U | M |
| 39 | 57 | Reed 3 | 76 25 25 64 0 38 25 38 0 127 76 32 0 127 25 127 | 2 0 0 1 0 1 0 0 0 | U | M |
| 40 | 58 | PWM Chorus | 38 0 0 64 0 102 0 0 0 127 25 38 0 127 51 127 | 1 1 0 1 2 0 0 0 0 | N | M |
| 41 | 61 | Synth Organ | 57 76 0 83 0 32 0 64 25 89 64 0 25 64 32 95 | 1 1 0 1 2 1 0 1 0 | N | A |
| 42 | 62 | Effect 1 | 57 76 19 127 0 83 0 57 0 89 89 0 64 0 70 89 | 2 1 1 1 1 1 1 0 0 | U | A |
| 43 | 63 | Effect 2 | 70 114 0 38 0 83 38 51 0 13 51 83 70 25 83 83 | 1 0 1 1 1 0 1 1 0 | N | A |
| 44 | 64 | Space Harp | 70 0 25 0 0 83 64 70 0 127 76 0 102 102 114 0 | 1 0 1 0 1 2 0 0 0 | N | A |
| 45 | 65 | Funk | 38 32 0 76 0 95 76 64 0 57 25 76 64 0 0 127 | 1 1 1 1 1 1 1 1 0 | N | A |
| 46 | 66 | Space 1 | 76 89 25 57 0 83 89 70 0 127 38 0 102 0 38 127 | 1 1 0 1 1 1 1 1 0 | N | A |
| 47 | 67 | Mysterious | 76 102 25 102 0 102 89 76 32 0 64 0 127 0 127 127 | 1 1 1 0 0 2 1 0 0 | N | A |
| 48 | 68 | Space 2 | 38 38 0 76 0 25 108 76 0 127 25 127 127 127 127 102 | 1 1 1 0 1 1 0 0 0 | N | A |
| 49 | 71 | Percussive 1 | 0 0 0 0 127 51 127 19 0 127 127 0 38 0 51 0 | 2 0 0 0 0 2 0 0 1 | U | A |
| 50 | 72 | Percussive 2 | 0 0 0 0 0 64 127 44 0 127 127 0 38 0 51 0 | 1 0 0 0 0 2 1 0 1 | N | A |
| 51 | 73 | Whistle | 70 64 0 0 25 44 127 19 25 127 102 38 0 127 13 0 | 2 0 0 0 0 1 0 0 1 | U | A |
| 52 | 74 | Effect 3 | 70 51 0 0 0 44 127 0 25 127 127 0 51 70 89 0 | 1 0 0 0 0 2 0 0 1 | N | A |
| 53 | 75 | UFO | 76 0 0 0 25 0 127 89 51 127 51 0 76 127 102 0 | 2 0 0 0 1 1 0 0 0 | U | A |
| 54 | 76 | Space 3 | 76 0 0 0 25 64 127 51 0 127 64 0 127 0 102 0 | 2 0 0 0 1 1 1 0 0 | U | A |
| 55 | 77 | Surf | 0 0 0 0 127 76 0 0 76 127 114 0 51 127 102 127 | 1 0 0 0 0 2 0 0 0 | N | A |
| 56 | 78 | Synth Drum | 0 0 0 0 0 25 127 51 0 127 76 0 64 0 76 0 | 1 0 0 0 0 2 0 0 0 | N | M |

## How to work
1. Read SPEC.md, KICKOFF.md, DECISIONS.md and this file. Add a "v3.2 — Juno-60 corrections" section to PLAN.md that maps C-1…C-8 to the files and tests involved.
2. **Before changing anything, record a 106 baseline:** the full 106-mode test results, the 106 goldens and the 106 screenshots. These are the regression guard for the guardrails above.
3. Make the SPEC.md edits (v3.2, changelog), then the code, then the tests. Commit after each C item with all tests green.
4. Run the full suite natively and under WASM, the benchmarks and pluginval. Start the standalone app and `npm run web`. In 60 mode, exercise through the front panel: RANGE is gone, D/N/U, VCA LEVEL −5…+5 (drag, type, hover), the VCF order, OFF/I/II plus the I+II combination press, and loading patches 11–78 from BANK/PATCH. Then switch to 106 mode and confirm that nothing there changed.
5. Record every choice in DECISIONS.md: what, why and which C item.

## Definition of done
- Every C item is in SPEC.md v3.2, implemented and covered by a passing test.
- The 106-mode tests, goldens and panel screenshots match the step-2 baseline exactly, apart from the default-bank exceptions listed in C-8.
- AC-D7.2 passes. `docs/FACTORY_BANK.md` exists, with attribution, the manual names, each patch's octave transpose and LFO trig mode, the patch 36 note and the bank 7 note.
- No TODOs or stubs.

## Report
Append a "v3.2" section to REPORT.md covering:
- what changed, per C item;
- test counts and pass rates, and benchmark numbers;
- the 60-mode goldens and screenshots that were regenerated, and why;
- which of the eight original patches change octave in 60 mode;
- new DECISIONS.md entries;
- the new §15.4 follow-ups: VCA LEVEL law, whether LFO TRIG and octave transpose are stored per patch, patch 36's chorus, and bank 7 tuning.
