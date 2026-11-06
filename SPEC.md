# VJN6 — Product Spec

Nov 6, 2026 · Marco Funk · v3.1

This is the single source of truth for building VJN6. Sections 1–13 and 15 are decided. §14 lists what is deliberately left to the implementer. Anything this spec does not cover is handled by the rule in KICKOFF.md: choose the simplest behaviour consistent with the overview, glossary and existing rules — the one that adds no new state, setting or UI — record it in `DECISIONS.md`, and continue.

## Preface: is the public data enough?

Yes, with one condition: every engine constant in §7 carries a provenance, and the provenance decides what a test may pin. Tier 1 is Roland's own service literature (calibration targets, printed ranges, a worked DCO divisor, PWM voltages, the SysEx format), marked **(SN)** in §7. Tier 2 is circuit analysis with component values and a firmware disassembly, mostly from one GPL-3 project (Ultramaster KR-106) that is used here as a source of numbers, never as code, marked **(CA)**. Tier 3 is fitted to recordings of one unit **(M)** or simply chosen **(D)**; those constants are listed again in §15.4 as measurements a hardware owner can replace later.

The two blocks that define the sound are the best covered: the filter has an exact converter law and two calibration anchors, and the Juno-60 chorus has a peer-reviewed model (Holters & Parker, DAFx-18) with published filter poles plus three independent recording analyses that agree on rates and delay swing. The weakest areas are slider-to-time laws (envelopes, Juno-60 cutoff) and the resonance law, where Roland gives endpoints and nothing in between.

| Block | Established (tier 1–2) | Decided (tier 3) |
| --- | --- | --- |
| DCO | 8253 counter per voice; 60: ≈1.9 MHz analog clock, 106: 8 MHz ÷2/4/8; 12 Vp-p saw; PWM 50 % → 95 %; sub = ÷2 flip-flop; shared white noise 4 Vp-p; LFO ±300 / ±400 cents; bend ±700 / ±1200 cents | reset time, sub max level, noise band limit, LFO-depth taper, 106 modulation update interval |
| HPF | 1-pole RC; 60: flat / 122 / 269 / 571 Hz; 106: +10.5 dB low shelf / flat / 236 / 754 Hz (ngspice from schematic, shelf verified against a hardware sweep) | ±20 % on the corners (single schematic reading) |
| VCF | IR3109 4-pole OTA cascade + BA662 feedback with Q compensation; 68 kΩ / 240 pF / 560 Ω; −19 mV/oct; self-osc 248 Hz at C4 and 992 Hz at C6; 106 cutoff word 1143 counts/oct, 5.53 Hz at 0; env 14 oct (106) / 10 oct (60); LFO 3.5 / 6 oct | resonance → feedback law, self-osc onset, Q-compensation strength, 60 slider law between anchors |
| VCA | BA662 OTA; 106 PNP converter (linear > 30 %, exponential < 10 %); ENV/GATE; 6 Vp-p / 4 Vp-p references | gate ramp 1.5 ms, 60 gain law linear |
| Envelopes | one ADSR per voice; 60 analog IR3R01, 106 firmware (linear attack, multiplicative decay/release); 60: 1 ms–3 s / 2 ms–12 s; 106: 1.5 ms–3 s / 1.5 ms–12 s | slider → time law (square), 106 tick 4.2 ms, retrigger from current level |
| LFO | one global triangle; 60: 0.3–20 Hz, delay 0–2 s, AUTO/MAN; 106: 0.1–30 Hz, delay 0–3 s, lever push | 106 rate table (firmware-derived), delay fade shape |
| Chorus | 2× MN3009 + 2× MN3101, no compander; I 0.51 Hz, II 0.86 Hz, I+II ≈9.5 Hz (60); delay 1.4–5.2 ms, centre 3.3 ms; 5th-order in/out filters with published poles; output-referred wet/dry 1.46 (measured); noise −57 dBFS wet vs −76 dry | 106 rates and swing = 60 values; noise spectrum shape |
| Assign / bend / porta | 60: rotary, steal oldest, HOLD, arpeggio 1.5–50 Hz; 106: POLY 1 / POLY 2 / UNISON, portamento, notes 24–108 | POLY 2 steal rule, portamento 0–3 s, lever curve, arpeggio gate length |
| MIDI / SysEx | 106: F0 41 30/31/32, 16 bytes + 2 switch bytes, velocity fixed 64, PC 0–127 → A11…B88; 60: DCB notes only | SysEx not filtered by channel; CC map from Roland's JU-06A |
| Panel | complete control inventory and legends for both; 106 LED display; 56 / 128 memories | colours, typeface, tick scales |

What this means for a one-shot build: the engine is architected as "exact where Roland is exact, parametric where the record is soft". A firmware-faithful 106 control path (14-bit cutoff word, 4.2 ms envelope/LFO loop, 8-bit lever read) and an analog-faithful 60 control path feed one shared voice model (TPT OTA cascade, BA662 feedback, BBD chorus with Holters & Parker poles). Every (D) and (M) constant is a single named number in `core/juno/constants.h`, so a later measurement replaces it without touching logic.

**Precedence when numbers collide.** If an acceptance criterion that asserts an (SN) or (CA) value cannot be met with the (D) or (M) constants as written, that constant is retuned by the smallest change that passes, and the new value and the reason go into `DECISIONS.md` and `constants.h`. (SN) and (CA) values and the formulas of §7 are never changed to make a test pass. Acceptance criteria that assert a (D) or (M) value assert the value in `constants.h`, so they follow a retuned constant.

Sources for the research behind this preface are in §15.5.

## 1. Overview

VJN6 is a six-voice virtual-analog polysynth that reproduces the sound and the front-panel workflow of the Roland Juno-60 and, as a second engine mode, the Juno-106. One signal path (DCO → HPF → VCF → VCA → chorus) is modelled from the published schematics; the two modes differ in a handful of measured constants (filter chip, envelope and LFO generation, chorus LFO rates, bender and portamento) and in the panel that drives them.

VJN6 runs in three builds, from one engine and one UI:

- a standalone desktop app;
- a VST3/AU instrument plugin;
- a locally hosted web page.

The core loop is: pick a patch → play it (MIDI, computer keyboard or DAW) → move sliders on the panel → write the patch to a memory slot → load and save banks. In 106 mode, patches are also loaded, saved and edited over Juno-106 SysEx.

What VJN6 does not try to be: a per-unit clone of one particular instrument. Without hardware to measure, the engine is a circuit model whose constants come from service-manual component values and from published measurements. The preface above says, block by block, which parts of the model are derived from circuits, which are fitted to published data, and which are open.

## 2. Glossary

Every term below is used with exactly this meaning in the rest of the spec.

| Term | Definition |
| --- | --- |
| **Mode** | The engine and panel variant: **60** or **106** (also "engine mode"). One mode is active at a time (§4.4). Other "modes" are always qualified: key mode, VCA mode, LFO trigger mode, arpeggio mode. |
| **Patch** | One complete sound: 16 slider bytes B0–B15 (0–127) and 9 switch fields W0–W8 (§4.1). "Program" is used only for MIDI program change and host programs. |
| **Patch name** | 0–12 characters, ASCII 0x20–0x7E (§4.3). Carried by every slot and by the edit buffer; not part of the patch bytes and not in SysEx. |
| **Character** | One Unicode code point after NFC normalisation. |
| **Performance setting** | A value that is not part of a patch (§4.2): key mode, portamento, bender sensitivities, LFO trigger, hold, arpeggio, transpose, master tune, volume, MIDI channel/omni/function, memory protect. |
| **Setting** | An option of §4.4. The Settings overlay shows the §4.4 settings plus the MIDI channel, omni, MIDI function and memory protect. |
| **Edit buffer** | The patch currently sounding and being edited, plus its patch name. |
| **Stored patch** | A copy of the slot contents taken when the edit buffer was last loaded from or written to a slot. |
| **EDITED / CLEAN** | EDITED: a patch field of the edit buffer changed since the last event that clears it (R-P2). Otherwise CLEAN. |
| **Manual** | The state after MANUAL (F-3) or after any edit-buffer load that does not select a slot (F-10, F-11). The LED shows `--`. Left by selecting or writing a slot, or by any `F0 41 30` in 106 mode (protected or not). |
| **Bank** | All 128 patches with names, in slots 1–128. |
| **Bank number** | 1–8 (with group A or B) in 106 mode, 1–7 in 60 mode: the middle level of the slot display. |
| **Slot** | One position 1–128. In 106 mode slot *s* is displayed as group A/B, bank number 1–8, patch 1–8: A11 = 1, A88 = 64, B11 = 65, B88 = 128. In 60 mode slots 1–56 are displayed as bank number 1–7 × patch 1–8 (`11`…`78`); slots 57–128 have no 60 display and are reachable only from the bank manager, program change and host programs. |
| **Current slot** | The slot the edit buffer was last loaded from or written to. |
| **Pending bank** | The bank number (and in 106 mode the group) that the next PATCH press uses. Set by BANK and A/B presses; set to the current slot's bank number on slot selection; if the current slot has no bank number in the active mode, it is bank 1 (group A). Recomputed on every mode switch. |
| **Free slot** | A slot whose patch equals the INIT patch (R-P7) and whose name is `INIT`. |
| **Voice** | One of the 6 synthesis voices: DCO → HPF → VCF → VCA. A voice is **held** while its gate is on (key down, or held by CC64 or HOLD), **releasing** after gate-off until it is free, and **free** when its envelope is in release below 0.001 (ENV) or its gate ramp is 0 (GATE), or before its first note. |
| **Control step** | 60 mode: every 32 host-rate samples on the absolute sample counter. 106 mode: the control tick. A **control boundary** is the start of a control step. |
| **Control tick** | 106 mode: the 4.2 ms step at which envelopes, LFO, portamento, pitch divisors and the cutoff word are recomputed (R-E1). |
| **Cutoff word** | 106 mode: the 14-bit integer the firmware sends to the filter DAC (R-E5). |
| **Lever** | The bender: a spring-loaded horizontal control, −1…+1. In 106 mode it also has a push zone that adds LFO vibrato. |
| **Combination press** | Two panel buttons pressed together on the hardware (60 BANK 1+5, 2+5; 106 POLY 1+POLY 2). With a mouse: Shift+click (R-U4). |
| **MIDI clock running** | A MIDI timing clock (F8) was received in the last 500 ms. |
| **Panel input** | A mouse, wheel or typed action on a front-panel control, or one of the shortcuts Alt+M, Alt+W, Alt+1…8. Not panel input: opening or using an overlay, the computer-keyboard map of §7.6, MIDI, host automation. |
| **WRITE armed** | The state between pressing WRITE and its completion or cancellation (F-5). |
| **Front panel** | The UI reproducing the hardware of the active mode (§5). |
| **Overlay** | The bank manager or Settings modal. |
| **LED message** | A 2-second notice on the LED (§5.4). |
| **Core** | The C++ library `vjn6_core` that owns all synth and panel state (§9.4). |
| **Host** | The DAW running the plugin. |
| **SN / CA / M / D** | Provenance of a constant: Roland service literature / circuit analysis / third-party measurement of one unit / decided (preface). |

## 3. Actors & access

- **One local user.** No accounts, no sign-in, no network features; VJN6 never makes a network request at runtime. The web build loads its files from localhost only.
- **Host (plugin build).** Automates the host parameters (§4.8), saves and restores state per instance, and sends MIDI including SysEx where supported.
- **External MIDI devices and librarians.** Send notes, controllers, program changes, clock and Juno-106 SysEx; receive notes, patch dumps and parameter changes in 106 mode (R-M3).
- **Several instances.** Plugin instances are independent. Two standalone processes or two browser tabs run independently and share saved state; the last writer wins.

## 4. Data model

### 4.1 Patch

A patch is 16 slider bytes plus 9 switch fields. In the rest of this spec, slider byte *n* is written **Bn** (B0–B15) and switch field *n* is written **Wn** (W0–W8). The slider order is the Juno-106 SysEx order, so a patch round-trips through SysEx without reordering. Fields marked **60** exist only on the Juno-60; in 106 mode they are read as the value in the last column and are not shown.

| Field | Parameter | Range | 106 mode reads |
| --- | --- | --- | --- |
| B0 | LFO rate | 0–127 |  |
| B1 | LFO delay time | 0–127 |  |
| B2 | DCO LFO depth | 0–127 |  |
| B3 | DCO PWM | 0–127 |  |
| B4 | Noise level | 0–127 |  |
| B5 | VCF frequency | 0–127 |  |
| B6 | VCF resonance | 0–127 |  |
| B7 | VCF envelope depth | 0–127 |  |
| B8 | VCF LFO depth | 0–127 |  |
| B9 | VCF keyboard follow | 0–127 |  |
| B10 | VCA level | 0–127 |  |
| B11 | ENV attack | 0–127 |  |
| B12 | ENV decay | 0–127 |  |
| B13 | ENV sustain | 0–127 |  |
| B14 | ENV release | 0–127 |  |
| B15 | Sub-oscillator level | 0–127 |  |
| W0 | Range | 0 = 16′, 1 = 8′, 2 = 4′ |  |
| W1 | Pulse on | 0–1 |  |
| W2 | Saw on | 0–1 |  |
| W3 | Sub on **60** | 0–1 | 1 |
| W4 | Chorus | 0 off, 1 I, 2 II, 3 I+II **60** | 3 is read as 1 |
| W5 | PWM source | 0 LFO, 1 MANUAL, 2 ENV **60** | 2 is read as 1 |
| W6 | VCF envelope polarity | 0 normal, 1 inverted |  |
| W7 | VCA mode | 0 ENV, 1 GATE |  |
| W8 | HPF position | 0–3 (meaning differs by mode, R-E4) |  |

**Raw bytes and effective values.** All fields hold 0–127 and are stored as received. A switch field outside its range is read as 0. The mode-specific readings above apply only while rendering and displaying; the stored field keeps its value, so switching 106 → 60 → 106 is lossless.

### 4.2 Performance settings (not in a patch)

| Setting | Mode | Range | Default |
| --- | --- | --- | --- |
| Key mode | 106 | 0 POLY 1, 1 POLY 2, 2 UNISON | 0 |
| Portamento on / time | 106 | 0–1 / 0–127 | off / 32 |
| Bender DCO sensitivity | both | 0–127 | 64 |
| Bender VCF sensitivity | both | 0–127 | 0 |
| LFO trigger sensitivity | 106 | 0–127 | 64 |
| LFO trigger mode | 60 | 0 AUTO, 1 MANUAL | 0 |
| Hold | 60 | 0–1 | 0 |
| Arpeggio on / mode / range / rate | 60 | 0–1 / 0 UP, 1 U&D, 2 DOWN / 1–3 oct / 0–127 | off / 0 / 1 / 64 |
| Octave transpose | 60 | 0 L (−12), 1 N, 2 H (+12) | 1 |
| Key transpose | 106 | −12…+12 semitones | 0 |
| Master tune | both | 0–127, 64 = 0; cents = (v − 64) × 50/64 | 64 |
| Master volume | both | 0–127; gain = (v/127)² | 127 |
| MIDI receive channel / omni | both | 1–16 / on–off | 1 / on |
| MIDI function | 106 | I, II, III (R-M1) | III |
| Memory protect | both | on/off | on |

Performance settings are saved in state. Changing mode keeps them. A setting that the active mode does not use has no effect and is not shown on the panel.

### 4.3 Bank, buffers and names

- One bank of 128 slots always exists. On first run it holds the VJN6 default bank (§15.3).
- The edit buffer and the stored patch each hold one patch. On first run both are slot 1, CLEAN, not Manual.
- **Names.** Each slot and the edit buffer carry a patch name of 0–12 characters (ASCII 0x20–0x7E), stored and written to files without trailing spaces. Names are a VJN6 addition; the hardware has none, and SysEx does not carry them. Every name entering VJN6 (typing, pasting, files) is normalised in this order: NFC; each character outside 0x20–0x7E becomes one `?`; truncate to 12 characters; remove trailing spaces. An empty name is allowed and shown blank. Name rules:
  - Loading a slot into the edit buffer copies the slot's name. WRITE copies the edit buffer's name to the target slot. A bank-manager row drop copies the name with the patch.
  - Loading a patch file sets the edit-buffer name from the file (`.vjn6`: its `name`; `.syx`: the file stem).
  - A received `F0 41 30` in 106 mode keeps the name of slot pp + 1 (written or protected) and the edit buffer takes that name. A received `F0 41 31`, and a `F0 41 30` in 60 mode, keep the edit-buffer name.
  - `.syx` bank import names each written slot by its source in 106 notation: `A11`…`B88` for a `30` message (from `pp`), `MANUAL nnnn` for a `31` message (nnnn = 1-based index of that message in the file, no leading zeros).
  - Renaming a row in the bank manager changes that slot's name; if that slot is the current slot, the edit-buffer name changes too. Renaming is a write to the slot and is blocked by memory protect (F-6).

### 4.4 Settings

| Setting | Values | Default | Saved in |
| --- | --- | --- | --- |
| Mode | 60 / 106 | 60 | state |
| Chorus noise | OFF / HALF / ORIGINAL | ORIGINAL | state |
| Voice variation | 0–100 % in steps of 10 (R-E12) | 50 % | state |
| Velocity → VCA | off / on | off (hardware ignores velocity) | state |
| Stereo output | stereo / mono ((L + R)/2 on both channels) | stereo | state |
| Oversampling | 2× / 4× | 2× | state |
| Computer keyboard play | on/off | on (standalone, web), off (plugin) | state |
| Zoom | Fit, 50–200 % in steps of 10 | Fit | state |
| Audio/MIDI devices | standalone: JUCE's standard device selector (output device, sample rate, buffer size, MIDI inputs) plus one MIDI output choice; web: Web MIDI inputs (checkboxes) and one MIDI output choice | standalone: system default output, all MIDI inputs, MIDI output none; web: all inputs, no output | standalone: the JUCE standalone properties file; web: IndexedDB key `devices` (R-D5) |

**Zoom.** Desktop (plugin and standalone): a percentage sets the window size (percentage × 1400 × 560); Fit opens the window at 100 % if that fits the screen, otherwise at the largest 10 % step that fits; resizing the window afterwards sets the scale directly and selects Fit. The desktop window reopens at its last size. Web: a percentage caps the scale and the canvas fits the viewport up to that cap; Fit fits the viewport. Choosing a percentage leaves Fit; choosing Fit returns to it.

### 4.5 Juno-106 SysEx

Manufacturer 0x41 only. `n` = MIDI channel − 1. All data bytes are 0–127.

| Message | Bytes | Notes |
| --- | --- | --- |
| Patch (all parameters) | `F0 41 30 0n pp d0…d15 sw1 sw2 F7` = 24 | `pp` = slot − 1. Receive: F-11. Sent on slot selection from the panel or bank manager in 106 mode with function III (R-M3). |
| Manual (all parameters) | `F0 41 31 0n pp d0…d15 sw1 sw2 F7` = 24 | `pp` ignored on receive, 0 on send. Receive: F-11. Sent on MANUAL and by Send patch, in 106 mode with function III. |
| Parameter change | `F0 41 32 0n pp vv F7` = 7 | `pp` 0x00–0x11, `vv` 0–127. Sent on every panel edit of a patch field in 106 mode with function III (R-M3). |

`d0…d15` are B0–B15. The switch bytes:

| Byte | Bit | Content |
| --- | --- | --- |
| sw1 (pp 0x10) | 0 / 1 / 2 | Range 16′ / 8′ / 4′, one-hot |
|  | 3 | Pulse on = 1 |
|  | 4 | Saw on = 1 |
|  | 5 | Chorus 0 = on, 1 = off |
|  | 6 | 1 = Chorus I, 0 = Chorus II (when bit 5 = 0); 0 when bit 5 = 1 |
| sw2 (pp 0x11) | 0 | PWM source 0 = LFO, 1 = MANUAL |
|  | 1 | VCF polarity 0 = normal, 1 = inverted |
|  | 2 | VCA 0 = ENV, 1 = GATE |
|  | 3–4 | HPF, reversed: `hpf = 3 − ((sw2 >> 3) & 3)` |
|  | 5–6 | 0 |

**Mapping rules.** Range: exactly one of bits 0–2 set → that range; none or several set → 8′. Chorus: bit 5 = 1 → W4 = 0; else bit 6 → W4 = 1 or 2. The 60-only states have no SysEx form: on **export** W3 = 0 is written as d15 = 0, W4 = 3 as Chorus I, W5 = 2 as MANUAL; on **import** of a `30`/`31` message W3 = 1. A received sw1 or sw2 (in a dump or a parameter change) sets only the fields that byte encodes: sw1 → W0, W1, W2, W4; sw2 → W5, W6, W7, W8. Bits 5–6 of sw2 are written as 0 and ignored on receive. **Canonical** form: range one-hot, sw1 bit 6 = 0 when chorus is off, sw2 bits 5–6 = 0.

**Channel.** Received SysEx is accepted on any channel (D: the hardware's behaviour is disputed). SysEx sent over MIDI carries the receive channel; SysEx written to files uses n = 0.

### 4.6 Files

| File | Content |
| --- | --- |
| `.vjn6` patch | JSON: `{"vjn6":1, "name", "sliders":[16], "switches":{…}}` with the 9 switch fields keyed range, pulse, saw, sub, chorus, pwm\_src, vcf\_pol, vca\_mode, hpf (integer values as §4.1). Lossless for both modes. |
| `.vjn6bank` | JSON: `{"vjn6":1, "patches":[128 × patch]}`, each patch as in a `.vjn6` file without the `"vjn6"` key. The primary bank format. |
| `.syx` patch | One 24-byte `F0 41 31` message (`n` = 0, `pp` = 0), canonical, lossy for 60-only fields as in §4.5. |
| `.syx` bank | 128 concatenated canonical 24-byte `F0 41 30` messages, `n` = 0, `pp` = 0…127. |

**Reading `.vjn6` / `.vjn6bank`.** Accepted only with `"vjn6": 1`, all keys present, arrays of the exact length and integer values 0–127; names are normalised (§4.3); unknown keys are ignored. Anything else is malformed.

**Import of `.syx`.** The file is scanned for `F0 41 30/31` messages of exactly 24 bytes and `F0 41 32` messages of exactly 7 bytes (R-D4).

- **No `30`/`31` message:** rejected as malformed.
- **Exactly one `30`/`31` message:** a patch load: it goes to the edit buffer (Manual, F-10), whatever its type.
- **Two or more:** a bank import into slots, in file order. A `30` message goes to slot `pp + 1`, unless that slot was already written by this import; then, like every `31` message, it goes to the lowest-numbered free slot not yet written by this import. When none is left, the message is dropped. The bank-manager notice reports written and dropped counts ("128 patches imported, 3 dropped"). The edit buffer, stored patch, current slot and flags are unchanged. With memory protect on, nothing is written and `Pr` is shown.
- **`F0 41 32` messages** in an accepted file are applied in file order to the edit buffer after the patch load or import, as external changes (R-S2): they set EDITED and never write slots. With memory protect blocking a bank import, they are not applied either.

Import of unknown, unreadable or malformed files: `Er` (FORMAT CONFLICT, §5.4) and the bank-manager notice. Files over 1 MiB are rejected the same way.

**Export names.** Bank: `VJN6 bank.vjn6bank` / `VJN6 bank.syx`. Patch: the edit-buffer name with characters outside `A–Z a–z 0–9 - _ space` replaced by `_`; empty or only `_` → `patch`; a Windows reserved name (CON, PRN, AUX, NUL, COM1–9, LPT1–9, any case) gets `_` appended.

### 4.7 State

State contains the bank with names, the edit buffer with its name and its EDITED and Manual flags, the stored patch, the current slot, the pending bank, the performance settings (§4.2) and the §4.4 settings marked "state". Panel transients (LED messages, WRITE armed, key-transpose arming, the HOLD set) are not saved.

The blob begins with the magic `"VJN6"` and a u16 version (1), and ends with a CRC32 (IEEE 802.3) of everything before it. The field layout is latitude (§14) and documented in `docs/STATE.md`. The version-1 fields form a prefix that later versions only append to. Newer version: load bank, buffers and performance settings, ignore the rest, no message. Bad magic, CRC, truncation or unknown older version: discard, first-run state, LED message `rE`. Zero-length blob (a new plugin instance): first-run state, no message.

### 4.8 Host parameters (plugin)

| Group | Parameters | Type | ID |
| --- | --- | --- | --- |
| Sliders | 16 | `AudioParameterInt` 0–127 | `p_lfo_rate`, `p_lfo_delay`, `p_dco_lfo`, `p_dco_pwm`, `p_noise`, `p_vcf_freq`, `p_vcf_res`, `p_vcf_env`, `p_vcf_lfo`, `p_vcf_kybd`, `p_vca_level`, `p_env_a`, `p_env_d`, `p_env_s`, `p_env_r`, `p_sub_level` |
| Switches | 9 | `AudioParameterChoice` with the §4.1 value names | `s_range`, `s_pulse`, `s_saw`, `s_sub`, `s_chorus`, `s_pwm_src`, `s_vcf_pol`, `s_vca_mode`, `s_hpf` |
| Performance | 14 | Int / Choice / Bool as §4.2 | `f_key_mode`, `f_porta_on`, `f_porta_time`, `f_bend_dco`, `f_bend_vcf`, `f_lfo_trig_sens`, `f_lfo_trig_mode`, `f_hold`, `f_arp_on`, `f_arp_mode`, `f_arp_range`, `f_arp_rate`, `f_oct_transpose`, `f_key_transpose` |
| Master tune | 1 | `AudioParameterInt` 0–127 | `g_tune` |

That is 16 + 9 + 14 + 1 = **40** host parameters, in the order of this table. Display names do not change with the mode: sliders and switches use the §4.1 "Parameter" names with an initial capital (`LFO rate`, `Sub on`); performance parameters: `Key mode`, `Portamento on`, `Portamento time`, `Bender DCO sensitivity`, `Bender VCF sensitivity`, `LFO trigger sensitivity`, `LFO trigger mode`, `Hold`, `Arpeggio on`, `Arpeggio mode`, `Arpeggio range`, `Arpeggio rate`, `Octave transpose`, `Key transpose`; `Master tune`. Value strings follow R-P5 and R-P6: sliders 0.0–10.0 (`v/12.7`, one decimal); choices: range `16'`, `8'`, `4'`; on/off fields `OFF`, `ON`; chorus `OFF`, `I`, `II`, `I+II (60)`; PWM source `LFO`, `MANUAL`, `ENV (60)`; polarity `NORMAL`, `INVERTED`; VCA `ENV`, `GATE`; HPF `0`–`3`; key mode `POLY 1`, `POLY 2`, `UNISON`; LFO trigger mode `AUTO`, `MANUAL`; arpeggio mode `UP`, `U&D`, `DOWN`; arpeggio range `1`–`3`; octave transpose `L`, `N`, `H`; key transpose `-12`…`+12`; tune in cents with one decimal and sign (`+10.2`). Parameters the active mode does not use are still exposed, with their effect as §4.1 and §4.2. A host write of an unchanged value has no effect. Name, bank, MIDI channel, omni, MIDI function, memory protect, master volume and the §4.4 settings live only in state.

**Host programs:** 128, one per slot, named after the slot name; renames and imports update the host's program names. Selecting program *i* selects slot *i* + 1 (F-4) in every mode and MIDI function, and never transmits. Selecting the program that equals the current slot does nothing (so a host's restore call does not reload an EDITED buffer). The current program is the current slot − 1.

## 5. Screens

### 5.1 Canvas and scaling

One fixed layout of **1400 × 560 logical px** (2.5:1), drawn entirely as SVG/HTML vectors; no raster images. The layout is scaled uniformly to fit the window, keeping its aspect ratio and letterboxing the remainder in `--body`; the scale is clamped to 50–200 %, and the layout never reflows. Desktop windows have a fixed-aspect constrainer, minimum 700 × 280, maximum 2800 × 1120; on the web the scale stops at 50 % and the page scrolls. Zoom behaviour is §4.4. Overlays are **960 × 520 logical px**, scaled with the canvas and centred.

There is no separate programmer view: the hardware already shows every parameter, so the front panel is the whole UI. Every slider and knob shows its value on hover and accepts typed entry (R-U1, R-P5).

### 5.2 Panel layout (both modes)

The panel is one row of sections, left to right, each a labelled box, above a memory row. Sections that exist in only one mode are drawn in that mode; the others keep their positions, so switching mode moves nothing that both share. Boxes (x, y, w, h); inside each box the controls appear in the order listed:

| Section | Box | 60 mode contents | 106 mode contents |
| --- | --- | --- | --- |
| Controller | 20, 60, 180, 400 | VOLUME knob; OCTAVE TRANSPOSE L/N/H switch; LFO TRIG button (momentary); DCO / VCF bender sensitivity knobs; lever | VOLUME knob; DCO / VCF bender sensitivity knobs; LFO TRIG sensitivity knob; PORTAMENTO TIME knob; lever with push zone |
| LFO | 216, 60, 100, 400 | RATE, DELAY TIME sliders; AUTO/MAN switch | RATE, DELAY TIME sliders |
| DCO | 332, 60, 300, 400 | LFO, PWM sliders; PWM switch LFO/MAN/ENV; RANGE 16′/8′/4′ buttons; PULSE, SAW, SUB buttons; SUB LEVEL, NOISE sliders | LFO, PWM sliders; PWM switch LFO/MAN; RANGE buttons; PULSE, SAW buttons; SUB, NOISE sliders |
| HPF | 648, 60, 60, 400 | 4-position switch 0–3 (0 at bottom) | 4-position slider 0–3 (0 at bottom) |
| VCF | 724, 60, 230, 400 | FREQ, RES, ENV, LFO, KYBD sliders; polarity switch +/− | same; polarity switch NORMAL/INVERTED |
| VCA | 970, 60, 90, 400 | ENV/GATE switch; LEVEL slider | same |
| ENV | 1076, 60, 160, 400 | A, D, S, R sliders | same |
| Chorus | 1252, 60, 60, 400 | I, II buttons | OFF, I, II buttons |
| Memory | 20, 476, 1292, 76 | 2-digit LED; BANK 1–5, PATCH 1–8 buttons; MANUAL, WRITE, HOLD buttons; ARPEGGIO ON button, MODE (UP/U&D/DOWN) and RANGE (1/2/3) switches, RATE knob | 2-digit LED; A, B group buttons; BANK 1–8, PATCH 1–8 buttons; MANUAL, WRITE; POLY 1, POLY 2; PORTAMENTO button; KEY TRANSPOSE button |
| Right edge | 1328, 60, 52, 492 | TUNE knob, wordmark `VJN6`, mode badge `60` | same, badge `106` |

Sliders are 20 × 300 px with a tick scale 0–10. Every slider's value is the patch byte: position = byte/127 (106 HPF slider: W8/3). Buttons are 26 × 26 with an LED. Knobs are 44 px rotary. Switches with n positions are drawn as a vertical slide switch. The 106 HPF slider snaps to 4 positions.

**Button behaviour.** RANGE: three radio buttons. 60 chorus: I and II each toggle their own state (off / I / II / both lit = I+II). 106 chorus: OFF, I, II are radio buttons. PULSE, SAW, SUB, PORTAMENTO, HOLD, ARPEGGIO ON: toggles. A click on a radio button writes its value when it differs from the stored field (so clicking the lit Chorus I while W4 = 3 in 106 mode writes 1, sets EDITED and transmits). POLY 1 / POLY 2: radio buttons; the combination press (R-U4) selects UNISON; a plain click on either afterwards leaves UNISON. LFO TRIG (60): held from mouse-down until mouse-up anywhere, window blur or editor close; on keyboard focus, held while Enter is held. A/B: radio buttons. Send-patch and similar disabled controls show their reason as a tooltip.

**Button LEDs.**

| Button | LED lit |
| --- | --- |
| RANGE, chorus, PULSE, SAW, SUB, PORTAMENTO, HOLD, ARPEGGIO ON | while their state is on / selected |
| POLY 1, POLY 2 | the selected one; both in UNISON |
| A, B, BANK | the pending group and pending bank number (BANK 1+5 or 2+5 for banks 6/7 in 60 mode) |
| PATCH | the current slot's patch, while not Manual and the pending bank equals the current slot's bank number; otherwise none |
| MANUAL | while Manual |
| WRITE | blinks 400 ms on / 400 ms off while WRITE is armed |
| KEY TRANSPOSE | blinks while armed; lit while key transpose ≠ 0 (its tooltip shows the value) |
| LFO TRIG (60) | while held |

### 5.3 LED display

Two red 7-segment digits, 72 × 46 px, with two decimal points.

| State | 60 mode | 106 mode |
| --- | --- | --- |
| Slot selected | bank number, patch: `23` | bank number, patch: `23` (A/B LEDs show the pending group, §5.2; the LED tooltip shows the slot's group) |
| Current slot 57–128 (60 mode) | both digits blank | — |
| Manual | `--` | `--` |
| EDITED | both decimal points lit | same |
| WRITE armed | the digits blink 400 ms on / 400 ms off | same |
| Key transpose armed | — | `t-` |
| LED message | the message text (§5.4) | same |

**Priority** (highest first): LED message → `t-` → WRITE blink → slot display. The decimal points show EDITED in every state except during an LED message. When a message ends, the state below it shows again. The LED tooltip always shows the full current slot and name (`A23 BRASS`, `23 BRASS`, `slot 73 PAD`, `MANUAL BRASS`).

**Glyphs** (segments a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle): digits 0–9 standard; `P` = a b e f g; `r` = e g; `E` = a d e f g; `o` = c d e g; `k` = a c e f g; `c` = d e g; `t` = d e f g; `-` = g.

### 5.4 LED messages (2 s, shown on the LED and as a tooltip under it)

| Message | Text | Tooltip |
| --- | --- | --- |
| Memory protected | `Pr` | MEMORY PROTECTED |
| Not a valid file | `Er` | FORMAT CONFLICT |
| SysEx patch received (`30`/`31` only) | `rc` | MIDI RECEIVED |
| Write completed | `ok` | WRITE COMPLETED |
| State reset | `rE` | STATE RESET |

A new message replaces the current one and restarts its 2 s. Any panel input dismisses a message, then acts.

### 5.5 Bank manager (overlay, 960 × 520, centred)

- **List:** 128 rows: slot number, slot display per mode (`A11`, `23`, or `—` for 60-mode slots 57–128), patch name. The current slot's row is highlighted. 60 mode greys rows 57–128 but keeps them usable. The list scrolls.
- **Renaming:** a double-click on a name cell edits it (a double-click on the other cells of a row selects the slot; rows are dragged by those cells); Enter or leaving the cell commits (normalised, §4.3); Esc reverts; closing the overlay commits. Under memory protect the commit is refused with `Pr` and the notice.
- **Buttons:** Load file… (`.vjn6`, `.vjn6bank`, `.syx`); Export bank (.vjn6bank); Export bank (.syx); Export patch (.vjn6); Export patch (.syx); Send patch (MIDI: enabled only in 106 mode with function III and an available MIDI output, else disabled with the reason as tooltip: "106 mode, MIDI function III only", "MIDI output not available" or "SysEx not permitted"); Memory protect toggle; Close.
- **Rows:** drag a row onto a row: copies patch and name over the target, without confirmation, honouring memory protect; the edit buffer, stored patch and current slot are unchanged. Double-click the slot cells of a row: selects it (F-4). Drag a row onto the "edit buffer" target: loads patch and name into the edit buffer, Manual.
- **Notices** (inline, 4 s; file names longer than 40 characters are shortened in the middle with `…`): "Not a VJN6 or Juno-106 file: ‹file name›" (with `Er`); "Could not read ‹file name›" (with `Er`); "Memory is protected." (with `Pr`); "‹n› patches imported, ‹m› dropped"; "Loaded ‹file name›"; "Could not write ‹path›" (standalone export failure).
- **File dialogs:** the OS open/save dialog (web: the browser's file picker; exports are downloads). Cancel changes nothing. Esc, Close, or a click outside the overlay closes it.

### 5.6 Settings (overlay, 960 × 520)

- **Contents:** Mode as a two-button choice at the top (changing it redraws the panel immediately, F-12); the other §4.4 settings; MIDI receive channel, omni, MIDI function (106) and memory protect; the device section.
- **Device section, standalone:** an "Audio/MIDI devices…" button that opens JUCE's standard device selector (output device, sample rate, buffer size, MIDI inputs), and a single MIDI output choice (default none).
- **Device section, web:** checkboxes for the Web MIDI inputs and a single choice of MIDI output.
- **States:** no Web MIDI (unsupported or permission denied): the MIDI part reads "MIDI not available — use the computer keyboard."; MIDI granted without SysEx: the line "SysEx not permitted — SysEx disabled." Unplugged devices disappear from the lists without an error.
- A one-line status area shows "Could not save state: ‹reason›" when saving fails (F-13). Settings changes apply immediately.

### 5.7 Web page and fallback states

| State | What is shown |
| --- | --- |
| Before the first user gesture | The full panel with the LED blank and all button LEDs off. A centred card reads "Click or press any key to start VJN6". The click or key that dismisses the card is not passed on to any control or to the keyboard. Then audio starts, Web MIDI is requested with `{ sysex: true }`, and the panel lights. |
| Not cross-origin isolated, no AudioWorklet, or WASM failed to load | The UI is replaced by a centred message naming the problem and how to fix it, for example: "VJN6 needs to be opened from http://localhost:5173 (cross-origin isolation is off)." |
| No audio output possible (the context fails to start) | "No audio output available." Everything else stays usable. |
| Desktop (plugin or standalone) without WebView2 / WebKitGTK | The editor shows a plain JUCE label: "VJN6 needs Microsoft Edge WebView2 / WebKitGTK to show its panel." The engine, MIDI, automation and state keep working. |

## 6. Flows

### F-1 Start

1. **Standalone:** load `state.bin` (R-D2); if it is unreadable or fails §4.7, rename it to `state.bad` (replacing an older one) and start from first-run state with `rE`. Open the window (§4.4 Zoom); the panel shows the current slot (or `--` in Manual). MIDI and notes work from the first render.
2. **Plugin:** first-run state, or the host's saved state. The editor shows the current state when opened.
3. **Web:** load state from IndexedDB (R-D5); show the start card (§5.7); on the first click or key, start audio and request Web MIDI; then the panel lights.

### F-2 Panel edits

1. Any slider, knob or switch move that changes a patch field writes it and sets EDITED (decimal points on); in 106 mode with MIDI function III it sends one `F0 41 32` message (R-M3). Performance controls change their setting and never set EDITED.
2. If the panel was in Manual, it stays in Manual; the LED keeps `--`.
3. Edits never change the current slot.
4. A panel input while WRITE is armed cancels the write (F-5), then acts.

### F-3 Manual

Pressing MANUAL makes the edit buffer equal to the panel — in software the controls already show the edit buffer, so the patch bytes are unchanged. The state becomes Manual and CLEAN; the LED shows `--`; the current slot and the edit-buffer name are unchanged. In 106 mode with function III a `F0 41 31` message is sent.

### F-4 Selecting a slot

A slot is selected by a PATCH button (with the pending bank and group), a MIDI program change, a host program, or a double-click in the bank manager.

1. Load the slot's patch and name into the edit buffer, and its patch into the stored patch.
2. CLEAN, not Manual, current slot = that slot, pending bank recomputed (glossary).
3. Sounding voices keep sounding and take the new patch at the next control boundary.
4. Only for a PATCH press or a bank-manager double-click, in 106 mode: function III sends the slot's `F0 41 30` message; function II sends a program change (R-M3).

BANK and A/B buttons change only the pending bank and do not select. In 60 mode, a combination press 1+5 or 2+5 sets pending bank 6 or 7.

### F-5 Write

1. WRITE arms (LED blinks). WRITE again cancels. Any other panel input except BANK, A/B and PATCH cancels and then acts. A mode switch cancels. Notes, MIDI, host automation and overlays do not cancel.
2. PATCH *p* with the pending bank (and group) → target slot.
3. Memory protect on → `Pr`, nothing written, disarm.
4. Otherwise write the edit buffer (patch and name) to the slot, set the stored patch, current slot = target, CLEAN, not Manual, `ok`.

### F-6 Memory protect

A toggle in the bank manager and in Settings; on by default; saved. It blocks WRITE, renames, bank-manager row drops onto slots, `.vjn6bank` loads and `.syx` bank imports, and received `F0 41 30` messages from writing slots (they still load the edit buffer, F-11). The edit buffer is never protected. A blocked action shows `Pr` (and the notice in the bank manager).

### F-7 Key transpose (106 mode)

KEY TRANSPOSE arms (`t-`). The next note-on in 24–108 from any source sets key transpose = clamp(note − 60, −12, +12), does not sound, is not transmitted, and disarms; notes outside 24–108 are ignored as usual. KEY TRANSPOSE while armed disarms without change. Note 60 while armed sets 0. Any other panel input or a mode switch disarms, then acts. A transpose change retunes sounding voices at the next control boundary.

### F-8 Hold and arpeggio (60 mode)

- **HOLD** on: the notes whose keys are down at that moment, and every note played after, join the **HOLD set**; note-offs do not release notes in the HOLD set. The HOLD set is replaced when a new key is pressed while no keys are physically down. The set holds at most 6 notes; a 7th keeps the newest 6. HOLD off releases every note of the HOLD set whose key is no longer down.
- **ARPEGGIO** on: the arpeggio notes are the distinct notes that are physically down, in the HOLD set, or held by CC64. They are played one at a time per R-E11. With no arpeggio notes, nothing plays; when notes arrive again, the arpeggio starts from its first step.

### F-9 Portamento (106 mode)

PORTAMENTO on: every new note glides from its voice's current pitch over the portamento time (R-E11). A voice's first note after start, `prepare`, a key-mode change or a mode switch does not glide.

### F-10 Bank manager

- **Load file…** (or a file dropped anywhere on the window: only the first file of a drop is used; the bank manager opens to show the result): `.vjn6` or a one-message `.syx` → edit buffer with its name (§4.3), Manual, CLEAN, notice "Loaded ‹file›". `.vjn6bank` → replaces all 128 slots and names (memory protect on: `Pr`, nothing written); the edit buffer, stored patch, current slot and flags are unchanged. Multi-message `.syx` → §4.6 import.
- **Export bank / patch**, **Send patch**, renaming, row drops, double-click: as §5.5. Export of the bank writes the 128 slots; export of a patch writes the edit buffer.
- An import or row drop that overwrites the current slot does not change the edit buffer or the stored patch.

### F-11 SysEx and program change

- `F0 41 30` in 106 mode: current slot = pp + 1, pending bank recomputed; if memory protect is off, slot ← data and the edit buffer loads it (CLEAN, not Manual); if protected, the edit buffer ← data, stored patch = the slot contents, EDITED, not Manual. `rc`. Nothing is sent back.
- `F0 41 30` in 60 mode, and `F0 41 31` in both modes: edit buffer ← data, Manual, CLEAN. `rc`.
- `F0 41 32`: apply the parameter as an external change (R-S2). No LED message.
- Program change 0–127: select slot pc + 1 (F-4), in both modes. Nothing is sent back.
- In 106 mode SysEx is processed only with MIDI function III; program change with II or III (R-M1). In 60 mode SysEx and program change are always processed.

### F-12 Changing mode

1. At the next control boundary the engine switches; sounding voices are released and their release tails render in the new mode. The HOLD set is cleared (the HOLD setting is kept); the arpeggio stops and restarts on the next note-on; WRITE and key-transpose arming are cancelled; keys still down do not re-sound.
2. The panel redraws for the new mode. The edit buffer, bank, current slot, EDITED/Manual flags and performance settings are unchanged; the pending bank is recomputed.
3. Fields the new mode does not use are read as §4.1 says; the LED shows the current slot as the new mode displays it (§5.3).

### F-13 Close, reload, relaunch

- **Standalone:** state is saved 1 s after the last change, at least every 5 s during continuous changes, and on quit, by writing a temporary file and renaming it over `state.bin`; `bank.vjn6bank` (canonical, R-D3) is written beside it at the same time as a copy for the user and is never read back. A bank import or bank load is saved only after it has completed. If the directory cannot be created or written, the Settings status line shows "Could not save state: ‹reason›" and VJN6 keeps running.
- **Plugin:** state is saved whenever the host asks. Closing the editor releases computer-keyboard notes and LFO TRIG, returns the lever to centre, and leaves the engine running.
- **Web:** the same timing as the standalone, plus a final save on `visibilitychange` (hidden) and `pagehide`, into IndexedDB (R-D5). Without IndexedDB, or when a write fails, VJN6 keeps running in memory and shows a one-line banner "Changes will not be saved in this browser."
- An action interrupted by a close or reload is lost, with any edits after the last save. The next start shows the last saved state.

## 7. Rules

The sound-engine rules define what VJN6 sounds like; the engine tests in §13.1 check them numerically. Every constant has a provenance (SN, CA, M, D; glossary). Units: pitch in cents or octaves, times in seconds, levels in a normalised unit where **1.0 = 6 V peak** (the hardware's 12 Vp-p sawtooth). All (D) and (M) constants live in `core/juno/constants.h`, one named value each.

### 7.1 Sound engine

**R-E1 Rates, blocks, control.**

- The engine runs at the host sample rate; supported and tested host rates are 44.1–192 kHz (below 44.1 kHz it runs, with every filter corner clamped to 0.45 × its rate, untested). The voice path (R-E3 to R-E6) runs at 2× or 4× oversampling (Settings) with a half-band decimator of ≥ 100 dB stop-band attenuation; the chorus and output stage run at the host rate.
- **60 mode:** LFO, cutoff CV and pitch modulation are computed at each control step (32 host-rate samples on an absolute sample counter started by prepare(), independent of host block boundaries) and linearly interpolated per sample. Envelopes and the GATE ramp are computed per sample. There is no quantisation anywhere in the 60 control path.
- **106 mode:** envelopes, LFO, portamento, pitch divisors and the cutoff word are computed on the **control tick, every 4.2 ms** (238.1 Hz) (D: emulator-derived, not in the service notes), accumulated as an exact fractional sample count on the same absolute counter, and held between ticks. The cutoff CV (log frequency after W → fc), the VCA envelope and the PWM CV then pass through a 1-pole RC with τ = 1 ms (D), modelling the DAC sample-and-hold and CV filtering; pitch is not smoothed. Values that the firmware holds as integers are integers: the cutoff word (14-bit), the LFO output (13-bit), the lever read (R-E11). The GATE ramp is computed per sample.
- All incoming events (MIDI, commands) take effect at the first control boundary at or after their absolute sample position, so results do not depend on host block size.
- Latency is the decimator's group delay; `latencySamples()` reports it and the plugin calls `setLatencySamples`. Changing the oversampling acts like `prepare` (voices stop, state kept) and updates the reported latency.
- Web: an AudioWorklet runs the same `vjn6_core` compiled to WASM at the context's rate; no resampler.

**R-E2 Levels.**

| Signal | Level | Source |
| --- | --- | --- |
| Sawtooth | peak 1.0 (12 Vp-p) | SN |
| Pulse | ±1.0 (12 Vp-p) | SN |
| Sub | ±(B15/127) × 1.0; max equals saw | D |
| Noise | white, RMS 0.11 × B4/127 (4 Vp-p at NOISE = 10, crest factor 3) | SN, D |
| VCF self-oscillation | peak 0.40 (4.8 Vp-p) in 106 mode, 0.33 (4 Vp-p) in 60 mode | SN |
| Single-voice output reference | saw only, VCA 127, FREQ 127, RES 0, HPF flat, chorus off, note 60, variation 0 → peak **−24 dBFS** at the engine output before master volume | D |

**R-E3 DCO.** One per voice: a 16-bit counter model driving a sawtooth, a pulse comparator, a ÷2 sub-oscillator; one noise generator shared by all voices.

- **Pitch.** `cents = 100·(note − 69) + range + transpose + tune + bend + lfo + porta`, with range = −1200 / 0 / +1200 cents for 16′ / 8′ / 4′, transpose in cents (60: octave transpose; 106: key transpose × 100), tune = (master tune − 64) × 50/64 (SN ±50 cents), bend and lfo from R-E11 and R-E8, porta from R-E11. f = 440 × 2^(cents/1200).
- **60 mode:** all six voices share one clock: bend, lfo and tune are one global multiplier updated per control step and interpolated per sample (SN: ±1050 cents total range of the master clock). Pitch is continuous; f is clamped to 8 Hz…0.45 × the oversampled rate.
- **106 mode:** per voice, the firmware computes the divisor on each control tick: `N = round(f_clk / f)`, `f_clk` = 1 / 2 / 4 MHz for 16′ / 8′ / 4′ (SN), N clamped to 2…65535 (so 16′ cannot go below 15.26 Hz, as on the hardware). The DCO frequency is `f_clk / N` and changes only at the end of the current cycle (counter reload) (CA).
- **Phase.** Each DCO has a phase p ∈ [0, 1) that wraps at the reset. Phase is free-running: a note-on does not reset it (CA); each voice starts at a phase drawn from the variation generator (R-E12) at prepare(). UNISON does not align phases, so the stacked level depends on the phases.
- **Sawtooth:** 60 mode falling, `saw = 1 − 2p`; 106 mode rising, `saw = 2p − 1` (CA); band-limited at the reset (≤ −70 dB alias, AC-E3.6); the reset is instantaneous (D: the hardware's ≈5 µs reset is below one oversampled sample).
- **Pulse:** `+1` for p < duty, else `−1` (both modes, D: the edge at the reset is high-going); `duty = 0.50 + 0.45·cv`, where cv = B3/127 (MANUAL), B3/127 × lfo (LFO; lfo is the bipolar triangle −1…+1, not delayed), B3/127 × env (ENV, 60 mode) (SN: 50 % at CV 0, 95 % at max). Band-limited at both edges. The pulse never disappears. (One source says the 106's slider physically reaches only byte 105; VJN6 uses the full 0–127 in both modes, D.)
- **Sub:** square at f/2 toggling on each saw reset, so it is phase-locked to the saw; its initial state at prepare() is drawn from the variation generator (D). In 60 mode W3 = 0 switches it off.
- **Noise:** one white generator at the host rate, fully correlated across voices (SN: one 2SC945 feeds all voices), upsampled into the voice path. No further band limit (D; a \~5 kHz limit is claimed by one source).
- **Mix:** `x = saw·W2 + pulse·W1 + sub + noise`, then a 1-pole DC blocker at 0.35 Hz (CA: 10 µF into 44.9 kΩ on the 106; used for both modes, D).

**R-E4 HPF.** Passive first-order sections, positions 0–3 (CA, single schematic reading; ±20 %).

| Position | 60 mode | 106 mode |
| --- | --- | --- |
| 0 | flat | low shelf: +10.5 dB at DC, +1.4 dB at HF; poles 59.4 Hz and 72.3 Hz, zeros 72.0 Hz and 170 Hz |
| 1 | 1-pole HP, 122 Hz | flat |
| 2 | 1-pole HP, 269 Hz | 1-pole HP, 236 Hz |
| 3 | 1-pole HP, 571 Hz | 1-pole HP, 754 Hz |

Implemented as TPT/bilinear sections at the oversampled rate. The 60's corner dependence on VCA LEVEL is not modelled (D).

**R-E5 VCF.** Four cascaded OTA integrator stages with a feedback OTA, solved as a zero-delay-feedback system at the oversampled rate.

- **Stage:** `y_i' = ω · V_sat · tanh((x_{i−1} − y_i) / V_sat)` with **V\_sat = 1.06** (CA: 68 kΩ into 560 Ω divides the stage input by 122; 2·V\_T = 52 mV; 122 × 52 mV = 6.3 V = 1.06 units), and `ω = 2π · c_cal · fc`.
- **Feedback:** `x_0 = g_in·x − k · V_fb · tanh(y_4 / V_fb)` with **V\_fb = 0.59** (CA: the BA662 sees y₄ through 100 kΩ : 1.5 kΩ, a division by 67.7; 67.7 × 52 mV = 3.5 V = 0.59 units). The filter output is y₄.
- **Resonance law:** `k = k_max · r^1.5`, r = B6/127, k\_max = 4.5 (106 mode) / 4.35 (60 mode) (D: chosen so that the steady self-oscillation amplitude equals the service-note levels 0.40 / 0.33 of R-E2; a reference simulation of these equations gives 0.405 / 0.341; onset of self-oscillation k = 4 at r = 0.925 / 0.946, i.e. RES byte ≥ 118 / ≥ 121).
- **Calibration factor c\_cal** (derived, one value per mode and oversampling factor): the saturating stages lower the self-oscillation frequency by a constant ratio (reference simulation: 0.888 in 106 mode, 0.916 in 60 mode, independent of fc). c\_cal is the reciprocal of that ratio as measured on the implemented solver (expected ≈ 1.126 / 1.092), so that the calibration identity below holds at every supported host rate — the software equivalent of the service-note trim. It is stored in `constants.h` and recorded in `DECISIONS.md`; if one value per mode and factor cannot hold AC-E5.1 at all tested rates, c\_cal may be a function of the oversampled rate (latitude, §14).
- **Q compensation:** `g_in = 1 + 0.23·k` (D, fitted to KR-106 data: the Junos pass a scaled input copy through the feedback OTA; this gives −8.6 dB pass-band loss at k = 4.5 instead of −14.8 dB).
- **Key note for KYBD:** `n_k` = incoming note + transpose in semitones (60: octave transpose, 106: key transpose) + the voice's portamento offset in semitones (106), excluding RANGE, bend and LFO.
- **Modulation terms:** env = the voice envelope 0…1; pol = +1 / −1 (W6); lfo = the LFO triangle −1…+1 multiplied by its delay gain (R-E8); lever −1…+1 (R-E11); bVCF = bender VCF sensitivity; F = B5, E = B7, L = B8, K = B9.
- **Cutoff, 60 mode:** `fc = 28.1 · 2^( 10.5·F/127 + 10·(E/127)·env·pol + 6·(L/127)·lfo + 4·(bVCF/127)·lever + (K/127)·(n_k − 60)/12 )` Hz (SN: 248 Hz at FREQ 3/10 = byte 38 with KYBD max at C4; env 10 oct, LFO 6 oct, bend ±4 oct; CA: \~28 Hz–40 kHz span).
- **Cutoff, 106 mode:** on each control tick `W = clamp( round( 128·F + 1143·( 14.2·(E/127)·env·pol + 3.5·(L/127)·lfo + 3.5·(bVCF/127)·lever + (K/127)·(n_k − 60)/12 ) ), 0, 16383 )` and `fc = 5.53 · 2^(W/1143)` Hz (CA: slave-CPU routine, 14.2 oct env depth; SN prints 5 Hz–50 kHz, env "14 oct", LFO 3.5 oct, bend 3.5 oct; 248 Hz ↔ W = 6272).
- `c_cal·fc` is clamped to \[5 Hz, 0.45 × oversampled rate\] after modulation.
- **Calibration identity (both modes):** with RES = 127, KYBD = 127, ENV = 0, LFO = 0, the filter self-oscillates at **248 Hz for note 60 and 992 Hz for note 84** when FREQ is at its calibration byte (38 in 60 mode, 49 in 106 mode) (SN, ±10 cents).

**R-E6 VCA.** `out = x_vcf · g(e) · (B10/127)`, e = envelope (ENV) or gate (GATE).

- **60 mode:** g(e) = e (D: BA662 control current is linear in the ADSR voltage).
- **106 mode:** g(e) = e for e ≥ 0.1; g(e) = e · 10^(−1.3·(0.1 − e)/0.1) for e < 0.1 (CA: PNP converter; "26 dB per 10 % of envelope below 10 %"); g = 0 for e < 0.001.
- **GATE:** g = a gate ramp that is 1 while the voice is held, else 0, moving linearly over 1.5 ms, computed per sample (D); in GATE mode neither the g(e) curve nor the 1 ms RC applies.

**R-E7 Envelope.** One ADSR per voice, feeding VCF (with polarity) and VCA. Slider → time: `T(x) = T_min + (T_max − T_min)·x²`, x = byte/127 (D: square law; endpoints SN).

|  | Attack | Decay | Release |
| --- | --- | --- | --- |
| 60 mode T\_min…T\_max | 1 ms … 3 s | 2 ms … 12 s | 2 ms … 12 s |
| 106 mode T\_min…T\_max | 1.5 ms … 3 s | 1.5 ms … 12 s | 1.5 ms … 12 s |

Sustain S = B13/127, linear.

- **60 mode (analog, per sample):** attack charges toward 1.5 with τ\_A = T\_A / ln 3 and ends when the level reaches 1.0 (D, modelled after the IR3R01's comparator; gives the concave attack and the click at zero attack); decay: `e' = (S − e)/τ_D`, τ\_D = T\_D/5; release: `e' = −e/τ_R`, τ\_R = T\_R/5 (D: a slider time is five time constants).
- **106 mode (firmware):** per control tick: attack `e += 4.2 ms / T_A`, clamped to 1.0, until e = 1.0 (CA: linear add); decay `e = S + (e − S)·exp(−5·4.2 ms / T_D)`; release `e = e·exp(−5·4.2 ms / T_R)` (CA: multiplicative). Then the 1 ms RC of R-E1. Visible stepping when T < 21 ms (five ticks) is intended.
- **Retrigger** starts the attack from the current level in both modes (D). Key-off at any stage jumps to release.

**R-E8 LFO.** One global triangle −1…+1, shared by all voices, free-running from prepare().

- **60 mode:** `f = 0.3 · 66.7^(B0/127)` Hz (SN: 0.3–20 Hz). Delay D = 2 s × B1/127 (SN). In AUTO the LFO output used for DCO and VCF is multiplied by a **delay gain** that is 0 for D after the first note-on following a state where no voice was held and no arpeggio was running, then ramps linearly to 1 over 0.2 s (D). In MANUAL the delay gain is 0 unless LFO TRIG is held (button, CC1 ≥ 64 or key 3), and ramps 0→1 linearly over D when it is pressed (immediately 1 when D = 0); releasing LFO TRIG sets it to 0.
- **106 mode:** rate from a table at bytes 0, 12.7, …, 127: **0.04, 1.07, 1.99, 2.91, 3.84, 4.76, 6.26, 7.73, 10.95, 18.67, 29.76 Hz**, linearly interpolated (D: the firmware table as read by one developer; SN prints 0.1–30 Hz). Phase φ ∈ [0, 1) as a fixed-point fraction with at least 24 bits; each control tick φ += f × 4.2 ms; the output is `tri = 1 − 4·|φ − 0.5|` rounded to a multiple of 1/4095 (13-bit). Delay D = 3 s × B1/127 (SN), delay gain as AUTO above.
- **Depth to DCO:** `depth = gain_delay · B2/127 + push · sens/127`, capped at 1, where push = 1 while the lever push is held (106 only) and sens = LFO trigger sensitivity; the push term is not delayed. Pitch deviation = ±300 cents (60) / ±400 cents (106) × depth × lfo (SN; linear in cents, D).
- **Depth to VCF:** 6 oct (60) / 3.5 oct (106) × gain_delay × B8/127 (R-E5).
- **Depth to PWM:** R-E3 (not delayed).

**R-E9 Chorus.** Two bucket-brigade delay lines (L, R), one shared chorus LFO, fixed dry/wet summing (CA; all three Junos use 2× MN3009 + 2× MN3101, no compander). The chorus LFO is free-running and independent of the patch LFO. Turning the chorus on clears both delay lines.

- **Delay trajectory** `d(t)` per channel:

| Mode | Centre | Swing | LFO | L / R |
| --- | --- | --- | --- | --- |
| I | 3.3 ms | ±1.9 ms | 0.513 Hz triangle | R uses −triangle (anti-phase) |
| II | 3.3 ms | ±1.9 ms | 0.863 Hz triangle | R uses −triangle (anti-phase) |
| I+II (60 only) | 3.4 ms | ±0.2 ms | 9.5 Hz sine | R uses the same sine (in-phase) |

(M, three independent Juno-60 recording analyses agree; 106 uses the same values, D.)

- **BBD:** 256 stages, clock `f_c = 128 / d(t)`. The line is modelled as a sample-rate-varying delay: the signal is sampled at f\_c and reconstructed with zero-order hold, so clock-rate-dependent aliasing and the ZOH lowpass at low clock rates are reproduced (Holters & Parker 2018, or any method passing AC-E9.1–E9.2); BBD gain +2.3 dB (M).
- **Input filter** (wet path only): 5th-order lowpass, real pole 7.41 kHz, biquads 9.69 kHz Q 0.55 and 10.34 kHz Q 1.24 (M, DAFx-18 fit to a Juno-60). **Output filter:** real pole 28.1 kHz, biquads 8.87 kHz Q 0.54 and 10.38 kHz Q 1.24 (M). A 1-pole 20 Hz high-pass on the wet path (D).
- **Summing:** `out_L = dry + G·wet_L`, `out_R = dry + G·wet_R`, where `wet` is the output of the wet path above (BBD gain and filters included) and **G = 1.12** (M: KR-106's output-referred measurement on a Juno-6, dry : wet = 0.863 : 1.257, a wet/dry ratio of 1.46 at the output; 1.46 / 1.30 for the +2.3 dB BBD gain = 1.12). With chorus off, wet = 0 and the dry gain is unchanged. Mono setting: (L + R)/2. (The 106 summer resistors give a ratio of 2.13 before wet-path losses; it is not used, §15.4.)
- **Noise:** white noise, independent per channel, through a +12 dB high shelf above 2 kHz, added to each wet channel before the output filter, scaled so the total output noise is −57 dBFS RMS (ORIGINAL), −63 dBFS (HALF), none (OFF), measured at the engine output before master volume with silent input and chorus on (M: −57 dBFS wet vs −76 dBFS dry on a 106, a level relative to that unit's full scale; the shelf is D and does not reproduce the measured +28 dB tilt at 5–15 kHz, §15.4). Chorus off: no chorus noise.

**R-E10 Output stage.** Sum the six voices, apply the chorus (R-E9), multiply by **0.063** (−24 dB, D: the single-voice reference of R-E2), then by the master volume gain (§4.2), and hard-clip to ±1.0. Worst-case bounds before the clip, variation 0: six in-phase saw-only voices 0.38 (−8.4 dBFS); one voice of saw + pulse + sub at full level with chorus 3 × 0.063 × 2.46 = 0.46; typical six-note chords with all waveforms and chorus stay below full scale; fully in-phase stacks of everything can clip, and master volume is the remedy.

**R-E11 Voices, assignment, portamento, bender, arpeggio.**

- **6 voices.** Incoming notes 24–108 (before transpose) are played; others are ignored, including their note-off (SN: 106 MIDI range; D for 60 mode). A note is keyed by (source, note), where the source is the MIDI input port and channel, the computer keyboard, or the host. A note-on for a key whose voice is held or releasing retriggers that voice; a note-off releases the voice holding that key.
- **60 mode (rotary):** try voices in order starting after the last assigned one; take the first free voice; else the releasing voice with the earliest note-on; else the held voice with the earliest note-on (CA/Tubbutec).
- **106 POLY 1:** the lowest-numbered free voice; else the releasing voice with the earliest note-on; else the earliest held (D).
- **106 POLY 2:** rotary order, but releasing voices (oldest note-on first) are taken **before** free voices, so only the most recent notes ring out; with all six held, the earliest held (D, from Roland's description).
- **106 UNISON:** all six voices play the newest note; last-note priority with return to the most recent still-held note on release; envelopes retrigger on every new note and on every return (D). No detune (SN: phase-locked DCOs).
- **Key-mode change** with notes sounding: all voices are released; new notes use the new key mode.
- **A stolen voice** retriggers from its current level (R-E7).
- **Hold (60):** F-8. **Hold pedal (CC64):** voices whose keys go up while CC64 ≥ 64 stay held; they release when CC64 goes below 64, unless HOLD latches them.
- **Portamento (106):** per voice, from the voice's current pitch (none on its first note, F-9), linear in semitones, constant time `T = 3 s × (t/127)²` (D; no printed range). In UNISON all six voices glide together. CC65 ≥ 64 turns it on, < 64 off; CC5 sets the time.
- **Bender:** lever −1…+1. DCO: ±700 cents (60) / ±1200 cents (106) × bDCO/127 (SN). VCF: R-E5. In 106 mode the lever value is read as `round(lever·127)/127` (8-bit read with exact centre and ends, CA). MIDI pitch bend maps v = 0…16383 → lever = clamp((v − 8192)/8191, −1, 1).
- **Arpeggio (60):** rate `f = 1.5 · 33.3^(x)` steps/s, x = rate/127 (SN: 1.5–50 Hz), modes UP / U&D / DOWN over 1–3 octaves (SN), stepping through the arpeggio notes (F-8) in pitch order, each further octave 12 semitones up; steps whose note would exceed 127 are skipped; U&D does not repeat the top and bottom notes at the turnaround. Each step is a note-on through the 60 voice assignment, with its note-off after 50 % of the step period (D). **Internal timing:** the first step plays at the first arpeggio note-on, then every 1/f. **Clock timing:** while MIDI clock is running, steps happen only on F8: the first F8 after the arpeggio notes appear (or after FA) plays a step, then every 6th F8; the step period used for the gate is 6 × the interval between the last two F8. When the clock stops, internal timing resumes and the next step plays at max(500 ms after the last F8, 1/f after the last clocked step). **Order changes:** adding or removing notes takes effect at the next step, which plays the nearest arpeggio note above (rising direction) or below (falling direction) the last played pitch; U&D keeps its current direction and turns at the ends. A running arpeggio counts as a held voice for the LFO delay (R-E8). The plugin uses only F8 in its MIDI input, not the host transport.
- **Transpose:** 60 octave switch ±1200 cents; 106 key transpose −12…+12 semitones (F-7). Changes retune sounding voices at the next control boundary.

**R-E12 Voice variation.** Per-voice deviations, deterministic, scaled by the Settings value (0 = none, 100 % = full):

| Quantity | Full deviation | Source |
| --- | --- | --- |
| Saw amplitude | ±4 % | SN: 12 ± 1 Vp-p |
| Pulse centre duty | ±1.5 % | SN: 47–52 % |
| Cutoff offset | ±15 cents | SN: ±10 cents trim tolerance, plus drift (D) |
| VCA gain | ±0.5 dB | D |
| Envelope times | ±5 % | D |
| DCO pitch | 0 | SN: digital |

**Variation generator:** xorshift32 (`x ^= x << 13; x ^= x >> 17; x ^= x << 5`) seeded with 0x564A4E36 at prepare(). Each draw first advances the generator and then uses x. Draws, in order: for voice 1…6, one draw per table row top to bottom (excluding DCO pitch), mapped to u ∈ [−1, 1) by `u = (x >> 8) / 2^23 − 1`, deviation = u × full × setting; then for voice 1…6 one draw for the DCO start phase `p = (x >> 8) / 2^24` and one draw for the sub start state `x >> 31`. Start phase and sub state do not depend on the setting. Values are fixed per voice for the life of the process; no slow drift (D). The chorus and DCO noise generators are separate xorshift32 instances seeded 0x4E4F4953 (DCO noise), 0x43484C31 and 0x43484C32 (chorus noise L, R).

**R-E13 Real-time safety.** No allocation, locks, logging or exceptions inside `render()` or `midi()`. Every recursive float path (filters, delay interpolators, envelopes) adds a 1e−20 DC offset to its state update, + on even and − on odd control steps of the absolute counter (not host blocks), which keeps values out of the subnormal range without flush-to-zero. Host block sizes 1–8192 are accepted; n = 0 is a no-op; blocks larger than the prepared maximum are processed in chunks.

### 7.2 MIDI

**R-M1 Receive.** Channel messages are accepted on the receive channel, or any channel while omni is on, in every build (including the plugin). In 106 mode the MIDI function setting gates classes as the hardware's rear switch does: **I** notes and CC64 only; **II** adds pitch bend, CC1 and program change; **III** adds SysEx and all other listed CCs. CC120 and CC123 are accepted in every mode and function. In 60 mode everything listed is accepted. System realtime (clock) is accepted in every mode and function. CCs not listed below are ignored.

| Message | Behaviour |
| --- | --- |
| Note on/off | Velocity ignored (hardware); with Velocity → VCA on, VCA gain × (v/127)^0.5. Velocity 0 = off. |
| Pitch bend | lever (R-E11) |
| CC1 | ≥ 64: lever push (106) / LFO TRIG held (60); < 64 released |
| CC3, 9 | LFO rate, LFO delay |
| CC12 | Range: 0–42 16′, 43–85 8′, 86–127 4′ |
| CC13, 14 | DCO LFO, PWM |
| CC15 | PWM source: 0–42 LFO, 43–85 MANUAL, 86–127 ENV (W5 = 2; read as MANUAL in 106 mode) |
| CC16, 17, 28 | Pulse, saw, sub on (W3): ≥ 64 on |
| CC18, 19 | Sub level, noise |
| CC20 | HPF: 0–31 → 0, 32–63 → 1, 64–95 → 2, 96–127 → 3 |
| CC21, 25 | VCF polarity, VCA mode: ≥ 64 = inverted / GATE |
| CC22, 23, 24, 74, 71 | VCF env, LFO, KYBD, FREQ, RES |
| CC26 | VCA level |
| CC73, 75, 27, 72 | ENV A, D, S, R |
| CC93 | Chorus: 0–31 off, 32–63 I, 64–95 II, 96–127 I+II (W4 = 3; read as I in 106 mode) |
| CC30 | LFO trigger mode (60): ≥ 64 MANUAL; ignored in 106 mode |
| CC86 | Key mode (106): 0–42 POLY 1, 43–85 POLY 2, 86–127 UNISON; ignored in 60 mode |
| CC87 | Bender DCO sensitivity |
| CC5, CC65 | Portamento time, portamento on (≥ 64) (106); ignored in 60 mode |
| CC64 | Hold pedal |
| CC7 | Master volume |
| CC120 | All sound off: voices stop at once; HOLD set, CC64 hold and arpeggio cleared |
| CC123 | All notes off: voices release; HOLD set, CC64 hold and arpeggio cleared |
| Program change 0–127 | Slot pc + 1 (F-4) |
| SysEx | §4.5, F-11 |
| F8 / FA | Clock and start drive the arpeggio (R-E11); FB, FC have no effect |

Patch-field CCs write the same stored value in both modes; the §4.1 readings apply. The CC numbers are Roland's JU-06A chart, so a JU-06A controller template drives VJN6 directly. A CC that changes a patch field is an external change (R-S2). Changing the receive channel, omni or MIDI function, or a MIDI input disappearing, acts as CC123 for the notes of the affected sources and clears the whole HOLD set and the arpeggio.

**R-M2 Stream robustness.** SysEx reassembled across `midi()` calls up to 24 bytes; longer messages discarded. Realtime bytes (0xF8–0xFF) inside SysEx are acted on and not stored. Running status supported.

**R-M3 Transmit.** 106 mode only, on the receive channel, through the chosen MIDI output (standalone, web) or the host's MIDI output (plugin; a host that drops instrument MIDI output gets nothing and no error is shown):

- function I, II, III: note on with velocity 64, and note off as note on with velocity 0, for notes played from the computer keyboard, sent as played before any transpose (received MIDI is never echoed; a note consumed by key-transpose arming is not sent) (SN); CC64 127/0 for the Space key;
- function II, III: CC1 0/127 on lever push (mouse or key 3); pitch bend on lever movement (mouse or keys 1/2), value round(lever × 8191) + 8192 from the unquantised lever;
- function II: program change on slot selection from the panel or bank manager (F-4);
- function III: `F0 41 30` on slot selection from the panel or bank manager (no program change), `F0 41 31` on MANUAL and Send patch, `F0 41 32` on every panel edit of a patch field.

A drag sends at most one message per parameter every 10 ms and always the final value. Changes caused by received MIDI, host automation or host programs are never transmitted. 60 mode transmits nothing (the hardware has no MIDI).

### 7.3 Front panel

**R-P1 Control map.** Every slider is bound to its §4.1 byte and every switch to its field; performance controls to §4.2. The binding is the panel legend; there are no hidden pages.

**R-P2 EDITED.** Set by any change to a patch field from any source (panel, host automation, SysEx `F0 41 32`, CC), and by a protected `F0 41 30` (F-11). A write of an unchanged value is not a change. Cleared only by slot selection (F-4), write (F-5), Manual (F-3), patch loads that enter Manual (F-10, F-11), and an unprotected `F0 41 30` in 106 mode (F-11).

**R-P3 Memory protect.** F-6.

**R-P4 LED.** §5.3 for every state; button LEDs as §5.2.

**R-P5 Display values.** Every slider and knob shows `<legend> <v> = <engine value>` in its tooltip, as the engine computes it for the active mode; switches show their legend.

| Control | v shown and typed | Engine value in the tooltip |
| --- | --- | --- |
| Sliders | byte/12.7, one decimal, 0.0–10.0 | LFO RATE Hz; DELAY TIME s; DCO LFO ±cents; PWM duty % (MANUAL source) or ±% (LFO/ENV); NOISE and SUB level %; FREQ fc in Hz at note 60, env, LFO and lever 0, without c\_cal; RES k (two decimals); VCF ENV and LFO octaves; KYBD %; VCA LEVEL dB; A, D, R the slider time T in s; S % |
| HPF | position 0–3 | the corner of the active mode (`flat`, `122 Hz`, `bass boost`) |
| VOLUME knob | 0.0–10.0 | dB |
| TUNE knob | cents, one decimal, −50.0…+49.2 | cents |
| Bender DCO / VCF sensitivity | 0.0–10.0 | ±cents / ±octaves |
| LFO TRIG sensitivity | 0.0–10.0 | ±cents |
| PORTAMENTO TIME | 0.0–10.0 | s |
| ARPEGGIO RATE | 0.0–10.0 | steps/s |

Formats: times under 1 s in ms without decimals, others in s with two decimals; Hz with three significant digits; dB and cents with one decimal and a sign (gain 0 shows `-inf dB`); octaves with two decimals and a sign; % and k as integers / two decimals.

**R-P6 Identical values.** The panel, the host value strings and the tooltips agree for the same byte.

**R-P7 Init patch.** The values used by empty slots: all sliders 0 except VCF FREQ 127, VCA LEVEL 127, SUSTAIN 127; range 8′, saw on, pulse off, sub on (60), chorus off, PWM LFO, polarity normal, VCA ENV, HPF 0 (flat in 60 mode, the default mode; the bass boost in 106 mode, where 1 is flat). Name `INIT`.

### 7.4 Synchronisation

**R-S1** Panel, host automation, SysEx parameter change and MIDI CC change the same core state and produce identical results for the same field and value, with one parameter-changed event per actual change.

**R-S2** External parameter changes (host automation, `F0 41 32`, CC) never change the slot, the pending bank or the Manual state, never cancel an armed WRITE or key transpose, and are never transmitted; they set EDITED when the value changes.

**R-S3 Host round trip, UI snapshot, update rate.**

- Changes that do not come from the host (panel, SysEx, CC, program change, patch and file loads) are reported to the host as parameter changes, one notification per changed host parameter; these reports are not applied back to the core a second time. A host write takes effect at the first control boundary (R-E1). Panel drags report begin/end gestures to the host.
- Whenever the UI connects (editor opens, page loads or reloads), it first receives the complete current state — every patch field, performance setting and setting, the LED and button LEDs, the slot names — and only then shows values.
- The UI receives state changes at up to 60 Hz; a change from any source is visible within 100 ms.

### 7.5 Files and persistence

**R-D1** State as §4.7; one plugin instance = one blob, so DAW projects are self-contained.

**R-D2** Standalone files live in `~/Library/Application Support/VJN6/` (macOS), `%APPDATA%\VJN6\` (Windows) or `~/.config/VJN6/` (Linux), created if missing: `state.bin`, `state.bad` (if any) and `bank.vjn6bank`.

**R-D3** A `.vjn6` or `.vjn6bank` file that VJN6 wrote, imported then exported, is byte-identical; the canonical form is UTF-8, 2-space indent, keys in the order of §4.6 and §4.1, names without trailing spaces, LF line ends, a final newline. Files written by other tools round-trip with identical values. A canonical `.syx` bank (§4.5, n = 0) imported into an all-INIT bank then exported is byte-identical; other `.syx` are normalised.

**R-D4** `.syx` import as §4.6.

**R-D5** Web storage: IndexedDB database `vjn6`, store `state`, keys `current` (the state blob) and `devices` (§4.4). Two tabs each save on their own; the last write wins.

**R-D6** First run (and state reset): the default bank (§15.3), slot 1, CLEAN, defaults of §4.2 and §4.4.

### 7.6 Computer keyboard (`ui/src/keyboard.ts`)

**R-K1 Map.** Keys are matched by `KeyboardEvent.code` (physical position), never by character.

| Keys (QWERTY positions) | Function |
| --- | --- |
| A S D F G H J K L ; ' | White keys C D E F G A B C D E F; the base is C3 = MIDI 60 |
| W E T Y U O P | Black keys C# D# F# G# A# C# D# |
| Z / X | Octave down / up: the base moves in 12s within 0…108; notes above 127 are not played |
| C / V | Velocity −20 / +20, default 100, clamped 1–127 (used only when Velocity → VCA is on) |
| Space (hold) | Hold pedal (CC64 127 / 0) |
| 1 / 2 (hold) | Lever fully left / right; centre on release |
| 3 (hold) | Lever push (106) / LFO TRIG (60) |
| M or F9 | Toggle keyboard play |

Keys 4–8 have no function. These keys are not panel input (glossary).

**R-K2 Behaviour.**

- A note-on happens only if the key is not already held and `event.repeat` is false. The note-off uses the note stored at key-down, even after an octave change.
- Events with Ctrl, Cmd, Alt/Option or Meta held, and events with `isComposing`, never play; with Alt held, only the R-U2 shortcut acts.
- All keys are released on window blur, on toggling play off, when a text field gains focus, and when the editor closes.
- While play is on, mapped keys are captured with `preventDefault`. Tab is never captured. Space is the hold pedal, so focused buttons are activated with Enter while play is on; with play off, Space activates focused buttons as usual.
- A focused text field (bank-manager rename, typed slider entry) suspends the map entirely.
- Play is on in the standalone and web builds and off in the plugin. M or F9 toggles it whenever the UI has keyboard focus and no text field is focused.

### 7.7 UI interaction

**R-U1** Sliders and knobs: drag vertically (1 byte per 2 px, Ctrl/Cmd 1 byte per 8 px), wheel ±1 (Shift ±10), double-click to type (R-P5 scale; Enter commits as round(v × 12.7) or the cents value for TUNE, Esc reverts, out-of-range clamps, non-numbers revert). Hover shows the tooltip (R-P5) and never changes a value. Switches: click a position. Buttons: click. HPF slider (106): drag snaps to positions, typed 0–3. **Lever:** mouse-down anywhere on it starts a drag; horizontal movement sets the lever, full deflection at ±60 px from the mouse-down point; it springs back to centre on release (also when released outside the window, on blur and on editor close). In 106 mode a mouse-down in the lever's upper half also holds push until release; push does not change during the drag. The lever shows the last value from any source (mouse, keys, MIDI pitch bend); while the mouse drags it, incoming pitch bend is ignored.

**R-U1a Keyboard on focused controls** (when keyboard play is off or the key is not in the §7.6 map): sliders and knobs Up/Right +1, Down/Left −1, with Shift ±10, Home/End min/max; switches Up/Down move one position; buttons Enter; the lever Left/Right deflect fully while held.

**R-U2 Shortcuts** (Alt = Option on macOS, the same in every build): Alt+B bank manager, Alt+, settings, Alt+M Manual, Alt+W Write, Alt+1…8 PATCH 1–8 with the pending bank, Esc closes an overlay. Shortcuts never play notes.

**R-U3 Accessibility and contrast.**

- Every control is reachable with Tab in visual order: the sections left to right, then the memory row, then the right edge; overlays in reading order.
- Every control has an ARIA name equal to its legend or label. The visible focus ring is 2 px `--legend`.
- These text/background pairs reach at least 4.5:1: overlay text (`--legend` on `--overlay`) and tooltip text (`--legend` on `--tooltip`). Panel legends, button numbers and the LED are exempt as a hardware replica and keep their ARIA names. If a checked pair fails, the foreground token is adjusted by the smallest change that passes, and the change is recorded in `DECISIONS.md`.

**R-U4 Combination press.** A Shift+click (keyboard: Shift+Enter) on a button while its partner is lit acts as pressing both. Partners are symmetric: BANK 1 ↔ BANK 5 (bank 6) and BANK 2 ↔ BANK 5 (bank 7) in 60 mode; POLY 1 ↔ POLY 2 (UNISON) in 106 mode. Repeating a combination that is already active does nothing. A Shift+click without a lit partner acts as a plain click. The tooltips of these buttons name the Shift+click.

### 7.8 Licensing and branding

**R-L1 JUCE licence.** JUCE 9 **Starter** (free) applies only while the licensee's revenue or funding over the previous 12 months is at most $20,000 (for an individual: all revenue from use of JUCE; for a company: the whole company's and its affiliates'). VJN6 is a personal project owned by Marco Funk. If the cap is exceeded, the options are JUCE Indie or relicensing VJN6 under the AGPLv3. The JUCE EULA forbids combining JUCE with anything that would force JUCE under an open-source licence, so nothing GPL or AGPL is ever linked or shipped.

**R-L2 Sources.**

| Source | Licence | Rule |
| --- | --- | --- |
| JUCE 9.x | Starter EULA | Git submodule, unmodified |
| VST3 SDK | MIT | Via JUCE |
| Svelte | MIT | Shipped in the UI bundle |
| Vite, Vitest, Playwright, Catch2, Pamplejuce | MIT / Apache-2.0 / BSL-1.0 | Build and test only |
| Barlow Condensed, Oswald | SIL OFL 1.1 | Bundled as woff2 |
| pluginval | GPL-3 | Downloaded by CI as a separate tool; never in the repo, never linked |
| Ultramaster KR-106, junox, Hera, mpc-vst-jn106 | GPL-3 | **Never copied.** Numbers cited in this spec are used as data; the repositories are not cloned into the project tree and are not used by CI. |
| pendragon-andyh/Juno60 analysis | MIT | Measurement data may be quoted in `docs/` with attribution; no code needed |
| Holters & Parker 2018, Raffel & Smith 2010 | papers | Method and published constants; implementation is original |
| thea.codes DCO article | CC-BY-SA text, Apache-2 code | Not copied; the DCO is written from R-E3 |
| Roland service notes and manuals | Roland copyright | Numbers only; no scans, images or text in the repo |
| Factory patch data ("The Original 128") | Roland | **Not shipped.** VJN6 ships its own default bank (§15.3) and imports `.syx` the user owns. |

**R-L3 Licence check (CI).** The build fails if any git-tracked first-party file contains the full title of the GNU GPL (the phrase "General Public License" preceded by "GNU") or of the GNU AGPL (the word "Affero" preceded by "GNU"). Excluded from the check: the JUCE submodule, `docs/` (including `docs/LICENSES.md`, which names licences), `SPEC.md` and `KICKOFF.md`.

**R-L4 `docs/LICENSES.md`** lists every shipped component with its licence: JUCE (Starter), VST3 SDK (MIT), Svelte (MIT), the fonts (OFL); and the build and test tools separately.

**R-L5 Branding.** "Roland", "JUNO", "Juno-60", "Juno-106" and Roland's logos never appear in the UI, the plugin metadata or the product name. Within `ui/src`, `plugin/` and the built plugin metadata they appear only in the bank manager's file-filter text "Juno-106 SysEx files (*.syx)" and in the rejection notice "Not a VJN6 or Juno-106 file". The mode badge reads **60** and **106**.

## 8. Edge cases

| Case | Behaviour |
| --- | --- |
| Saw and pulse both off, sub level 0, noise 0 | Silence; the filter still self-oscillates at RES byte ≥ 118 (106) / ≥ 121 (60) (hardware behaviour). |
| RES at 127 with no input | Self-oscillation starts from the filter's numerical noise floor; the 1e−20 offset (R-E13) guarantees it starts; it reaches full level within 1.5 s at 248 Hz, so tests pre-roll 3 s. |
| FREQ 0 in 106 mode, note 108, KYBD 127 | W = 1143 × 4 = 4572 → 88 Hz before c\_cal; W clamps at 0 only below note 60. |
| Cutoff modulation beyond range | W clamped to 0–16383 (106); c\_cal·fc clamped to 5 Hz–0.45 × oversampled rate (both). |
| 7th note in POLY 1 | The releasing voice with the earliest note-on is stolen; if none, the earliest held. The stolen voice retriggers from its current level (R-E7). |
| UNISON with portamento | All six voices glide together from the current unison pitch. |
| Same note from MIDI and computer keyboard | Two voices; each note-off releases its own (keyed by source). |
| Same note twice from one source | The voice retriggers; one note-off releases it. |
| Note outside 24–108 | Ignored, including its note-off. |
| Note + transpose outside 24–108 | Played (the range check is on the incoming note); pitch clamps as R-E3. |
| Lever push while LFO delay is counting | Vibrato from the push appears immediately (R-E8). |
| Chorus I+II patch loaded in 106 mode | Renders and displays as Chorus I; the stored field keeps 3. |
| HPF position 0 patch moved between modes | 60 → 106: flat becomes bass boost; 106 → 60: boost becomes flat. Intended: the byte, not the sound, is preserved. Tooltip names the corner for the active mode. |
| PWM ENV patch in 106 mode | Renders as MANUAL (D; hardware has no ENV source). |
| Received `F0 41 30` / `31` / `32` with wrong length | Ignored. |
| SysEx parameter pp > 0x11 | Ignored. |
| Program change in 106 mode with function I | Ignored (host programs still select, §4.8). |
| Hold pedal and HOLD (60) both active | Either keeps notes; both must be off to release. |
| More than 6 keys with HOLD | The newest 6 are latched. |
| WRITE armed, then a note is played | The note sounds; WRITE stays armed. |
| WRITE armed, then a BANK press | Pending bank changes; WRITE stays armed. |
| Current slot 57–128 in 60 mode | LED digits blank; pending bank 1; WRITE can target slots 1–56. |
| A one-message `.syx` file | Loads the edit buffer (Manual), never a slot. |
| Several files dropped at once | Only the first is used. |
| Renaming a slot that a received `F0 41 30` overwrites meanwhile | The rename commits on the slot afterwards (last write wins). |
| Six loud voices in unison | Up to +15.6 dB over one voice when phases align; master volume is the remedy (R-E10). |
| Host calls `prepare` again or oversampling changes | State kept, voices stop, LFO, chorus and variation generator restart. |
| Corrupt or foreign state | Reset with `rE`; standalone keeps the old file as `state.bad` (F-1). |
| Disk or IndexedDB unavailable | VJN6 keeps running with the F-13 notice. |
| Two standalone instances or tabs | Independent; last writer wins. |
| Long MIDI device names | One line, shortened in the middle with `…`, full name in a tooltip. |
| Name with non-ASCII, emoji or RTL text | Each such character becomes one `?` (§4.3). |

## 9. Non-functional requirements

### 9.1 Performance

- **Native benchmark** (`tools/bench`): 106 mode, 6 voices in UNISON (worst case) of the default BRASS patch with Chorus I, 2× oversampling, 48 kHz, 10 s, on a GitHub `ubuntu-24.04` runner: render time < 8 % of real time, median of 5 runs. The tanh stages are the hot spot; a rational approximation with ≤ 1e−4 absolute error is allowed and must be the same function in native and WASM builds.
- **WASM benchmark:** the same benchmark compiled with emcc, run in Node 22 on the same runner: < 35 % of real time.
- **UI:** a change from any source is visible within 100 ms.

### 9.2 Platforms and formats

- **Desktop:** macOS 13+ (universal binary): Standalone, VST3, AU. Windows 10 22H2+ / 11 x64: Standalone, VST3. Ubuntu 24.04 x64 with webkit2gtk-4.1: Standalone, VST3.
- **Web:** current Chrome and Edge, with Web MIDI; Safari 17+ with the computer keyboard only; Firefox may work but is not tested.

### 9.3 Plugin identity

```cmake
juce_add_plugin(VJN6
    PRODUCT_NAME "VJN6"  PLUGIN_NAME "VJN6"  VERSION 0.1.0
    COMPANY_NAME "Marco Funk"  COMPANY_COPYRIGHT "© 2026 Marco Funk"
    BUNDLE_ID com.marcofunk.vjn6
    PLUGIN_MANUFACTURER_CODE Mfnk  PLUGIN_CODE Vjn6
    DESCRIPTION "6-voice virtual-analog synthesizer"
    FORMATS Standalone VST3 AU  IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE  NEEDS_MIDI_OUTPUT TRUE  IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth  AU_MAIN_TYPE kAudioUnitType_MusicDevice
    NEEDS_WEBVIEW2 TRUE  NEEDS_WEB_BROWSER TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE  HARDENED_RUNTIME_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE)
```

Never change the codes or bundle ID after the first saved DAW project; hosts key saved state on them. Buses: no audio input; stereo output (mono if the host asks: (L + R)/2).

### 9.4 Binding technical constraints

- **Stack:** one C++17 static library `vjn6_core` with no JUCE dependency, used unchanged by the plugin and by the Emscripten web build; JUCE 9 `AudioProcessor` with a `WebBrowserComponent` editor; UI in Svelte 5 + TypeScript, one bundle for all builds.
- **Threading:** `vjn6_core` owns all state; only the audio thread touches it; other threads talk to it through fixed-size, lock-free, allocation-free queues (commands in, events out), applied in order. A command posted before a `render()` call takes effect in that call (at its first control boundary).
- **Public API** (signatures binding; `Command`, `Event`, `StateBlob` contents are latitude):

```cpp
namespace vjn6 {
enum class Mode { J60, J106 };
enum class Noise { Off, Half, Original };
struct Settings { Mode mode = Mode::J60; Noise chorusNoise = Noise::Original;
                  int variationPct = 50; bool velocityToVca = false;
                  bool mono = false; int oversample = 2; };
class Instrument {
public:
  explicit Instrument(double hostSampleRate, int maxBlock);   // allocates everything here
  void prepare(double hostSampleRate, int maxBlock);          // may allocate; not on the audio thread
  bool post(const Command&);                                  // one producer thread
  bool pollEvent(Event&);                                     // one consumer thread
  void midi(const uint8_t* bytes, int len, int sampleOffset); // audio thread, incl. SysEx
  void render(float* L, float* R, int n);                     // audio thread, no alloc/lock/log
  int  latencySamples() const;
  void saveState(StateBlob&) const;  bool loadState(const StateBlob&);
};
}
```

- **Determinism:** the same MIDI, commands and settings render bit-identically on all platforms and in WASM: compile with `-ffp-contract=off`, no fast-math, no platform `tanh`/`exp` in the signal path (own approximations), and all generators are the seeded xorshift32 instances of R-E12.
- **Test builds** expose, through a test-only interface: signal injection and capture at the HPF input, VCF input, VCF output, chorus input, BBD input and output, and the wet path; a switch that mutes the dry path; forcing a voice's envelope value, gate and DCO phase / sub state; reading the envelope before the 1 ms RC, the cutoff word W, the LFO delay gain, and every filter state variable.

### 9.5 Other

English only; no network access or telemetry; no raster assets in the UI; fonts bundled, nothing loads from a CDN.

## 10. External services

None. VJN6 uses no APIs, accounts or keys. Local platform services have defined fallbacks:

| Service | Fallback |
| --- | --- |
| Audio devices (standalone) | JUCE device selector; no device → silent, everything else works |
| Web Audio | §5.7 |
| Web MIDI | §5.6 |
| WebView2 / WebKitGTK | §5.7 |
| IndexedDB / file system | F-13 |

**For tests:** Web MIDI is mocked by a test double for `navigator.requestMIDIAccess` that records sent bytes and can inject messages; audio is checked with an `AnalyserNode` tap exposed only in test builds.

## 11. Visual direction

**Look.** Two skins, chosen by mode, sharing one geometry (§5.2):

- **60:** black panel, dark wood-grain end cheeks (flat vector texture), white legends, coloured section headers, orange/yellow/grey/white button caps (assignment latitude, §14), red LEDs.
- **106:** cool dark-grey panel with red, blue and white section boxes, blue and white button caps, black end cheeks, red LEDs.

**Tokens**, frozen in `ui/src/theme.css`; final colour tuning against period photos is a human follow-up (§15.4):

```css
:root {
  --body: #1C1A18;  --panel-60: #141414;  --panel-106: #4B4F55;
  --overlay: #24221F;  --tooltip: #0E0E0E;
  --wood: #5A3A22;  --legend: #ECE8DC;  --legend-dim: #A9A49A;
  --box-red: #C8403A;  --box-blue: #3E7BC4;  --box-white: #E6E4DE;
  --cap-orange: #E8782E;  --cap-yellow: #E5C540;  --cap-grey: #9A9A96;  --cap-white: #EDEDEA;  --cap-blue: #4A86CC;
  --slider-track: #0E0E0E;  --slider-cap: #D9D9D6;
  --led-on: #FF3B2F;  --led-off: #3A1512;  --lamp-on: #FF5A48;
}
```

**Fonts.** Barlow Condensed 500/600 for legends and tick numbers (`font-variant-numeric: tabular-nums`); Oswald 500 for the wordmark. Both bundled as woff2.

**LED.** Drawn as SVG from `ui/src/led/segments.ts` with the §5.3 glyphs. Slider caps have a 2 px white centre line as on the hardware. Lit buttons use `--lamp-on`.

## 12. Out of scope

- CLAP, AUv3, iOS; signed or notarised installers; public web hosting and a non-threaded web build
- Undo/redo (Manual and patch reload are the hardware's safety nets)
- Tape (cassette) save/load emulation; the 106's SAVE/VERIFY/LOAD buttons are not drawn
- DCB emulation; the Juno-60 has no MIDI and VJN6 adds a CC map instead
- Per-unit ageing, slow drift, temperature; component-tolerance randomisation beyond R-E12
- The 60's HPF corner shift with VCA LEVEL (R-E4)
- Chorus LFO-locked noise bursts and clicks at the clock extremes (R-E9 models the hiss only)
- Velocity, aftertouch and MPE beyond the one Velocity → VCA setting; MIDI learn; mod matrix; second envelope or LFO; effects beyond the chorus
- Factory patch data; localisation; touch layouts; layouts below 700 × 280
- Microtuning
- The Juno-60's KEY TRANSPOSE to any key (its octave switch is kept; key transpose exists in 106 mode)
- The Juno-60's hidden banks 8–9 (Patch Shift jack) and external arpeggio clock jack
- Host transport sync for the arpeggio (only MIDI clock)

## 13. Acceptance criteria

Every criterion is an automated test whose name contains its AC ID. Core criteria (Catch2) run natively and under WASM (Node 22, the same test binary compiled with emcc) with identical expectations; UI criteria run with Playwright against the web build in Chromium and WebKit on Linux; keyboard criteria run in Vitest.

**Test patch.** Where a criterion says "test patch": R-P7 INIT with saw on, HPF flat (position 0 in 60 mode, 1 in 106 mode), RES 0, ENV A 0 D 0 S 127 R 0, VCA ENV, chorus off, variation 0 %, velocity off, 2× oversampling, note 60 placed on a control boundary, measured on the engine output before master volume. "Self-osc patch": test patch with saw off, RES 127, KYBD 127, ENV 0, LFO 0, FREQ 38 (60) / 49 (106). Spectral measurements use a 2^17-point FFT (1.37 s) of steady state at 96 kHz host rate after a 3 s pre-roll, unless stated; "flat" responses are measured with Welch averaging (≥ 64 segments) in 1/3-octave bands at the host rate. Tests use the test-build injection and capture points of §9.4. Unless stated, both modes are tested.

### 13.1 Sound engine

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-E1.1 | Host rates 44.1, 48, 96 kHz; 2× and 4×; chorus off | An impulse is injected at the VCF input (filter at FREQ 127, RES 0, VCA gate forced open) | The output peak arrives `latencySamples()` ± 1 sample later; L/R identical |
| AC-E1.2 | 106 mode, ENV A 127 | The envelope is sampled before the 1 ms RC | It changes value only at multiples of exactly 4.2 ms (fractional samples, ± 1 sample); after the RC it follows a 1 ms time constant ± 5 % |
| AC-E1.3 (goldens) | 48 renders (8 default-bank patches × both modes × chorus off/I/II) at 48 kHz, note 60 held 1 s + 1 s released, variation 50 % | Rendered on all three OSes and WASM | Bit-identical to committed goldens; regenerated only with an intended change |
| AC-E1.4 | 48 kHz, latency read at 2× | Oversampling switched to 4× while a note sounds | Voices stop; `latencySamples()` changes to the 4× value and the plugin reports it to the host |
| AC-E2.1 | Test patch, FREQ 127 | Peaks measured | Saw peak = 1.0 ± 1 % at the VCF input; engine output peak −24 dBFS ± 0.5 dB |
| AC-E2.2 | Test patch, saw off, NOISE 127 | 10 s rendered | Host-rate noise source RMS = 0.11 ± 5 %; at the VCF input flat ± 1 dB from 100 Hz to 0.4 × host rate; identical in all six voices (correlation > 0.999) |
| AC-E3.1 | Test patch, each range, notes 24, 60, 108 | Fundamental measured | 440 × 2^((n − 69)/12 + range/1200) ± 0.5 cent (60 mode); f\_clk / round(f\_clk / f) ± 0.1 cent (106 mode) |
| AC-E3.2 | 106 mode, 8′, note 69 | Bend swept −1 → +1 over 2 s with bender DCO sensitivity 127 | Pitch takes ≤ 255 distinct values; each change happens at a saw reset |
| AC-E3.3 | Pulse only, PWM MANUAL at bytes 0, 64, 127 | Duty measured | 50.0 %, 72.7 %, 95.0 % ± 0.5 %; the high-going edge coincides with the saw reset |
| AC-E3.4 | Pulse only, PWM LFO 127, LFO rate 0 | 1 LFO period | Duty sweeps 5 %–95 % as a triangle; the pulse never disappears |
| AC-E3.5 | Sub only, SUB 127 | Measured | Square at f/2, duty 50 % ± 0.5 %, peak 1.0 ± 1 %; each edge coincides with a saw reset |
| AC-E3.6 | Saw at notes 60 and 96 | Spectrum | All alias components < −70 dB relative to the fundamental |
| AC-E3.7 | Saw, 60 mode vs 106 mode | Waveforms compared | Opposite polarity, otherwise identical shape |
| AC-E4.1 | White noise injected at the HPF input, each position | Response | Within ± 0.5 dB of the analytic R-E4 response from 20 Hz to 20 kHz; 106 position 0 = +1.4 ± 0.3 dB at 5 kHz relative to position 1 |
| AC-E5.1 (calibration) | Self-osc patch, notes 60 and 84, host rates 44.1, 48, 96 kHz, 2× and 4× | Self-oscillation frequency | 248 Hz ± 10 cents and 992 Hz ± 10 cents; c\_cal in 1.00–1.25 |
| AC-E5.2 | Self-osc patch, note 60 | Amplitude and purity at the VCF output | Peak 0.40 ± 15 % (106) / 0.33 ± 15 % (60); H3 ≤ −40 dB relative to the fundamental; level flat ± 1 dB as FREQ moves the oscillation from 110 Hz to 14 kHz |
| AC-E5.3 | Test patch, RES 0, FREQ 64, white noise at the VCF input | Response at the VCF output | −24 dB/oct ± 1.5 dB between 4 fₓ and 16 fₓ; the −12 dB point relative to the 20 Hz gain at fₓ = c\_cal × 28.1 × 2^(10.5 × 64/127) (60) / c\_cal × 5.53 × 2^(8192/1143) (106) ± 2 % |
| AC-E5.4 | RES bytes 0, 64, 110, 127; white noise at the VCF input; FREQ 64 | Peak height P at fₓ relative to the 20 Hz gain | RES 0: no peak (response monotonic); RES 64 and 110: k = (4P − 1)/(P + 1) (small-signal ladder: P = (1 + k)/(4 − k), white noise at −40 dBFS) equals k\_max × r^1.5 ± 5 %; self-oscillation at 127, none at 110 |
| AC-E5.5 | RES 110 (k = 3.63 in 106 mode / 3.51 in 60 mode) | Gain at 20 Hz, VCF input → output | −8.0 dB ± 0.5 dB (Q compensation: (1 + 0.23k)/(1 + k)) |
| AC-E5.6 | 106 mode, ENV 127, polarity ±, KYBD 127, LFO 127, bender VCF sensitivity 127 | W computed at env 1, lever ±1, lfo ±1, note 108 | Matches the R-E5 formula exactly (integer compare); clamped at 16383 |
| AC-E5.7 | 60 mode, ENV 127 | fc at env 0 and env 1 | Ratio 2^10 ± 2 %, subject to the clamp |
| AC-E6.1 | 106 mode, VCA env forced to 0.05, 0.1, 0.3, 1.0 | Gain | 0.0112, 0.1, 0.3, 1.0 ± 3 % |
| AC-E6.2 | GATE mode | Note on/off | Ramp 1.5 ms ± 0.1 ms, no overshoot |
| AC-E7.1 | Sustain 64, A/D/R bytes 0, 64, 127 | Times (attack 0 → 1; decay 1 → S + 0.0067(1 − S); release from key-off level L to 0.0067 L), 106 measured before the RC | 60: A 1 ms / 0.76 s / 3 s; D,R 2 ms / 3.05 s / 12 s; 106: byte 0 completes within one control tick; A 0.76 s / 3 s and D,R 3.05 s / 12 s at bytes 64 / 127; all ± 3 % or ± 1 tick (106), whichever is larger |
| AC-E7.2 | 60 mode attack | Shape | Matches 1.5(1 − e^(−t/τ)) to within 1 % until 1.0 |
| AC-E7.3 | 106 mode attack 127 | Shape | Linear ± 1 %, stepped at 4.2 ms |
| AC-E7.4 | Release at 0.5 then retrigger | Level | Attack starts from 0.5 |
| AC-E8.1 | 60 mode, LFO rate 0, 64, 127 | Frequency over ≥ 10 periods | 0.3 Hz, 2.49 Hz, 20 Hz ± 2 % |
| AC-E8.2 | 106 mode, bytes 0, 13, 25, 38, 51, 64, 76, 89, 102, 114, 127 | Mean frequency over ≥ 20 periods | The R-E8 table linearly interpolated at those bytes, ± 2 % |
| AC-E8.3 | DCO LFO 127, LFO rate 64, note 69 | Maximum pitch deviation over ≥ 50 periods | ± 300 cents (60) / ± 400 cents (106) ± 3 cents |
| AC-E8.4 | Delay 127, AUTO | Note-on after silence | Delay gain 0 for 2.0 s (60) / 3.0 s (106) ± 20 ms, then 1 within 0.2 s ± 20 ms; a second note while the first is held does not restart it; a running arpeggio keeps it running |
| AC-E8.5 | 60 MANUAL, delay 64; 106 push with sensitivity 64, delay 127 | LFO TRIG pressed / released; push during the delay | Gain ramps 0 → 1 over 1.008 s ± 20 ms and drops to 0 on release; push gives ±201.6 cents (64/127 × 400) ± 3 cents at once |
| AC-E9.1 | Chorus I, II, I+II (60), impulse train at the BBD input | Delay of the BBD line alone (BBD input to BBD output tap, excluding filter group delay) | I, II: centre 3.3 ms ± 0.1, swing ± 1.9 ms ± 0.1, rate 0.513 / 0.863 Hz ± 1 %, R anti-phase; I+II: centre 3.4 ms ± 0.1, swing ± 0.2 ms ± 0.05, 9.5 Hz ± 2 %, R in-phase |
| AC-E9.2 | Chorus I, white noise at the chorus input | Wet-path response, time-averaged over 10 s | Within ± 1.5 dB of the R-E9 filter cascade × BBD gain × the ZOH response at the centre clock (38.8 kHz) from 100 Hz to 8 kHz; ≥ 20 dB below the 1 kHz wet gain at 16 kHz |
| AC-E9.3 | Silent input; chorus off; chorus I with noise ORIGINAL / HALF / OFF | Engine output RMS before master volume | below −110 dBFS; −57 / −63 dBFS ± 1 dB / below −110 dBFS; L and R correlation below 0.05 over 10 s |
| AC-E9.4 | 200 Hz sine at the chorus input | Chorus off vs I; wet level with the dry path muted | Dry level unchanged ± 0.1 dB; output wet/dry level ratio 1.46 ± 3 % |
| AC-E9.5 | Chorus I on, input then silence; mono setting | Chorus turned off and on; mono on with chorus I | No output from before the switch-on (delay lines cleared); L = R = (L + R)/2 of the stereo render |
| AC-E10.1 | Variation 0; 6 voices unison, saw only, phases forced equal; volume 127 / 64 / 0 | Peak before the clip | 0.378 ± 1 % at 127; (64/127)² × that ± 0.01 dB at 64; silence at 0 |
| AC-E11.1 | 60 mode / POLY 1 / POLY 2 | 7 notes, release patterns | The R-E11 voice is stolen in each mode (one test per rule); POLY 2 takes the oldest releasing voice even when free voices exist |
| AC-E11.2 | UNISON | Three notes held, top released | All six voices on the newest, then on the most recent remaining held note with an envelope retrigger |
| AC-E11.3 | Portamento time 64, on | 12-semitone glide; a new note mid-glide; the first note after prepare() | Duration 3 × (64/127)² = 0.762 s ± 3 %, linear in semitones; the second glide starts from the current pitch; the first note does not glide |
| AC-E11.4 | Bend sens 127, lever ±1; MIDI bend 0, 8192, 16383 | Pitch | ± 700 cents (60) / ± 1200 cents (106) ± 2 cents; 106 lever rest = 0 cents exactly; 8192 = 0 cents; 16383 = +1 |
| AC-E11.5 | Arpeggio UP / U&D / DOWN, range 1–3, rate 0 / 127 | Note order and period | Correct order; 1.5 and 50 steps/s ± 2 %; U&D does not repeat turnaround notes; note-off at 50 % ± 1 sample of the step; with MIDI clock: a step on the first F8 after the notes appear and every 6th F8 after, gate 50 % of 6 F8 intervals; after FA the next F8 steps; after the clock stops, the next step comes at max(500 ms after the last F8, 1/f after the last clocked step) ± 1 control step; adding D to C-E-G while at E (UP) plays G next |
| AC-E11.6 | 60 mode | Notes held by CC64 alone, by HOLD alone, by both; note 23 and 109 | Released when CC64 < 64 / HOLD off / both off; 23 and 109 ignored including note-off |
| AC-E11.7 | 106 POLY 1, 3 notes held | Key mode → UNISON; note 60 from the computer keyboard and from MIDI, then one note-off | All voices release, new notes in UNISON; two voices in POLY 1 for the same note from two sources, the note-off releases only its own |
| AC-E12.1 | Variation 0 vs 100 %, DCO phases and sub states forced equal | Per-voice measurements | 0 %: six voices identical; 100 %: deviations equal the values computed from the R-E12 generator ± 1 % |
| AC-E13.1 | Native build with subnormal checks | 60 s decay at 44.1/48/96 kHz | No subnormal in any state variable |
| AC-E13.2 | Allocation hook | 10 s of notes, SysEx, commands, mode switches | Zero allocations, locks or logs in `render()`/`midi()` |
| AC-E13.3 | Host block sizes 1, 7, 64, 513, 8192, 0 | Same MIDI and commands | Bit-identical to the render with host block size 64; 0 renders nothing |

### 13.2 MIDI

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-M1.1 | Channel 3, omni off / on | Notes on channels 1 and 3 | Only 3 / both sound; same in the plugin |
| AC-M1.2 | Each R-M1 row, both modes | Sent | The stated effect; CC20 64 → HPF 2; CC93 50 → W4 = 1; CC93 100 → W4 = 3 in both modes (shown as I in 106 mode); CC12 100 → 4′; an unlisted CC → no change |
| AC-M1.3 | 106 mode, functions I / II / III | Note, CC64, PC, bend, CC1, CC74, SysEx, CC123 | Accepted exactly per R-M1 (CC123 always) |
| AC-M1.4 | Velocity → VCA off / on | Velocities 1, 64, 127 | Off: identical; on: gain (v/127)^0.5 |
| AC-M1.5 | Omni off, notes held, HOLD set latched | CC120; CC123; receive-channel change | Silence at once; release; release — HOLD set and arpeggio cleared in each case |
| AC-M2.1 | A 24-byte patch in 1-byte chunks with an interleaved F8; a 30-byte SysEx; running-status notes | Received | Loads; discarded; all notes play |
| AC-M3.1 | 106 mode III | Slider move, 1 s drag, received CC, host automation | 1 message; ≤ 101 ending on the final value; 0; 0. Function I/II: 0 SysEx. 60 mode: nothing transmitted ever |
| AC-M3.2 | 106 mode II / III | PATCH press; MIDI program change received; host program | II: one program change; III: one `F0 41 30`, no PC; received PC and host programs: nothing sent |
| AC-M3.3 | 106 mode I | Computer-keyboard note, Space | Note on velocity 64 / note on velocity 0; CC64 127 / 0 |
| AC-M3.4 | 106 mode II | Lever push by mouse and key 3; lever drag; keys 1/2 | CC1 127 then 0; pitch-bend messages ≤ 1 per 10 ms ending on 8192 after release |

### 13.3 Panel, flows, SysEx mapping, files

All file and import criteria run with memory protect off unless stated.

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-P1.1 | Every control, both modes | Moved | The §4.1/§4.2 field changes; the control is present only in its mode; 60 chorus I/II toggles give off/I/II/I+II |
| AC-P2.1 | CLEAN | Each edit source; an unchanged-value host write; then Manual | EDITED; still CLEAN after the unchanged write (tested first); Manual clears it; a performance control does not set it |
| AC-P4.1 | Every §5.3 state and priority combination, both modes | LED, decimal points and button LEDs read | Match a golden matrix derived by hand from §5.2 and §5.3 |
| AC-P4.2 | An LED message showing | A panel input; a second message | The message is dismissed and the input acts; the new message replaces it and restarts 2 s |
| AC-P5.1 | Every slider and knob at 0, 64, 127 | Panel, tooltip, host strings | Identical values; tooltip values equal the engine's, formatted per R-P5 |
| AC-P7.1 | — | An empty slot of the default bank is read | Bytes and name equal R-P7 |
| AC-F1.1 | Standalone with saved state on slot 5; a corrupt `state.bin`; web | Start | Slot 5 shown, a MIDI note at the first render sounds; corrupt: `rE`, `state.bad` exists; web: the start card shows first, and its dismissing click activates nothing |
| AC-F3.1 | EDITED on slot 3, 106 mode III | MANUAL | LED `--`, CLEAN, slot 3 still current, one `F0 41 31` sent |
| AC-F4.1 | EDITED on slot 3 | PATCH 7 pressed (pending bank 1) | Slot 7 loads with its name, CLEAN, LED `17`; 106 mode III sends one `F0 41 30 0n 06 …` |
| AC-F4.2 | 60 mode, BANK 1 lit / BANK 2 lit | Shift+click BANK 5, then PATCH 2 / PATCH 8 | Slots 42 / 56 load; LED `62` / `78`; both bank LEDs lit |
| AC-F4.3 | 60 mode, current slot 73 | LED read; BANK 3, PATCH 1 | Digits blank, pending bank 1; slot 17 loads |
| AC-F4.4 | Note held on slot 3 | Slot 7 selected | The sounding voice changes to slot 7's patch at the next control boundary, without retrigger |
| AC-F5.1 | Protect off / on | WRITE, PATCH 5 | Written with name, `ok`, current slot 5, CLEAN / `Pr`, unchanged |
| AC-F5.2 | WRITE armed | A note; a BANK press; a slider move; a mode switch | Note sounds, still armed; pending bank changes, still armed; cancelled and the slider applies; cancelled |
| AC-F7.1 | 106 mode, KEY TRANSPOSE armed | Note 48, then 60; arm and move a slider | Transpose −12, the first note silent, LED lit; arming then note 60 resets to 0; a slider move disarms and applies |
| AC-F8.1 | 60 mode HOLD on | Chord, release, new chord; 8 keys | First chord held; replaced by the second; newest 6 latched |
| AC-F10.1 | Bank manager | Load `.vjn6`; `.vjn6bank` protected / not; one-message `.syx`; rename a row to `é😀abcdefghijklm` protected / not; drag row 2 → 9; drag a row → edit buffer; export bank and patch in both formats; a `.wav`; two files dropped; Cancel in the file dialog | Each as F-10/§4.3/§4.6/§5.5 with the stated notices and LED messages; the name becomes `??abcdefghij`; Cancel changes nothing |
| AC-F11.1 | Each §4.5 message, incl. sw1 = 0x51 / sw2 = 0x11 | Received | Range 16′, saw on, chorus I, PWM MANUAL, VCF normal, VCA ENV, HPF 1; `rc` for 30/31 only |
| AC-F11.2 | A 60-mode patch with sub off, I+II, PWM ENV | Exported `.syx`, re-imported | d15 = 0 (sub level 0), Chorus I, MANUAL, W3 = 1; `.vjn6` round trip lossless |
| AC-F11.3 | 106 mode III, protect on / off | `F0 41 30` pp 9 received | Current slot 10, edit buffer = data; off: slot 10 written, CLEAN; on: slot unchanged, EDITED |
| AC-F11.4 | 60 mode, W3 = 0, W4 = 3 | `F0 41 32` pp 0x10 = 0x51 | W0, W1, W2, W4 change (W4 = 1); W3 stays 0 |
| AC-F12.1 | Notes held, HOLD latched, arpeggio running, WRITE armed | Mode switched | Panel redraws; edit buffer, slot and flags unchanged; voices released; HOLD set cleared, HOLD on; arpeggio stopped; WRITE disarmed; pending bank recomputed |
| AC-F13.1 | Standalone (test-overridden state directory) and web, protect off | A slider edit and a WRITE, quit within 1 s / reload; a failed standalone write; IndexedDB disabled | `state.bin` holds both and `bank.vjn6bank` equals the saved bank including the written slot / the same state returns; the failed write leaves the previous `state.bin` intact and shows the status line; the banner shows |
| AC-D1.1 | Every §4.7 field set to non-default values | `saveState` → `loadState` | All fields equal |
| AC-D1.2 | Blobs with bad CRC, bad magic, truncated, version 0; a newer-version blob; a zero-length blob | Loaded | First-run state and `rE`; bank, buffers and performance settings loaded, no message; first-run state, no message |
| AC-D3.1 | `.vjn6bank`; a canonical `.syx` bank into an all-INIT bank; a synthetic `.syx` with non-canonical bits and n = 5 | Import then export | Byte-identical; byte-identical; normalised as R-D3 (n = 0) |
| AC-D4.1 | Files of 0 bytes; 1 MiB + 1; junk + one patch; 128 `30` messages (pp 0–127) + 3 more `30`s; 131 `31` messages into an all-INIT bank; only `32` messages; a `.wav`; protect on with a bank | Imported | `Er`; `Er`; edit buffer, Manual; 128 written, 3 dropped, notice; 128 written as `MANUAL 1`…, 3 dropped; `Er`; `Er`; `Pr`, nothing written |
| AC-D4.2 | Edit-buffer names `BRASS 1`, an empty name, `___`, `con`; `.vjn6` files with `"vjn6": 2`, a missing key, 15 sliders, a float, a 20-character name, an extra key | Export patch; import | `BRASS 1.vjn6`, `patch.vjn6`, `patch.vjn6`, `con_.vjn6`; `Er`, `Er`, `Er`, `Er`, name truncated to 12, extra key ignored |
| AC-D6.1 | Empty storage | Start | The R-D6 state, including the §4.2 defaults |
| AC-D7.1 | The default bank | Slots 1–9 play note 60 in both modes | Peak > −60 dBFS; names as §15.3 |

### 13.4 Plugin, UI, keyboard, licensing, build, performance

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-PL1 | VST3 on macOS, Windows, Linux (xvfb); AU on macOS | pluginval at strictness 5 | Pass |
| AC-PL2 | Plugin | Parameters and programs enumerated; a slot renamed | 40 parameters with §4.8 IDs, order, names and strings; 128 programs named after the slots; the rename updates the program name |
| AC-PL3 | `getStateInformation` during playback → `setStateInformation` into a new instance; then `setCurrentProgram` with the current slot while EDITED | Compared | Identical state; every `render()` call under 50 % of its block's real-time duration; the EDITED buffer is kept |
| AC-S1.1 | Every patch field at random values (seed 1) drawn from the values all five paths can express (sliders 0–127; switches in their 106 range) | Set via command, panel, SysEx, CC, host (plugin-side native test with the real `AudioProcessor`) | Identical core state, one event per change |
| AC-S2.1 | Manual with WRITE armed; separately, 106 mode with key transpose armed | Host automation, `F0 41 32`, CC | Slot, Manual and the arming unchanged; EDITED; nothing transmitted |
| AC-S3.1 | Plugin test host | A slot selection | Each changed host parameter is notified once; within the next 2 s nothing more is notified or re-applied |
| AC-S3.2 | A UI showing a non-default state | Page reload / editor reopen | Every displayed value, the LED and the button LEDs equal the state before |
| AC-S3.3 | Web with mocked MIDI | CC74 64 injected | The FREQ slider shows 5.0 within 100 ms |
| AC-K1.1 | Play on | Each mapped key | The R-K1 note, octave, velocity, hold pedal, lever or push effect occurs; keys 4–8 do nothing |
| AC-K2.1 | Play on | Key repeat; octave change while held; blur; text focus; play toggled off; Ctrl+A; `isComposing`; Tab; Space; a play key while WRITE is armed | No repeat note-on; the original note is released; all released; all released; all released; no note; no note; focus moves; hold pedal (and Enter activates the focused control); WRITE stays armed |
| AC-K2.2 | The plugin build | First open | Play is off; M turns it on |
| AC-U1.1 | Sliders, knobs, TUNE, HPF (106), switches, lever | Drag 20 px, Ctrl-drag 20 px, wheel, Shift-wheel, typing 12 / abc; TUNE typed 10; HPF typed 2; switch position click; lever drag 30 px, release outside, upper-half press; a focused slider with Up, Shift+Up, End | +10, +2–3, +1, +10, clamp 127 / revert; tune byte 77 (+10.2 cents); HPF 2; the switch field; lever +0.5, back to 0, push held; +1, +10, 127 |
| AC-U2.1 | — | Each R-U2 shortcut | Its action occurs and no note sounds |
| AC-U3.1 | Both skins | Tab from the first control to the last; axe-core; contrast script over `theme.css` | Every control receives focus once in R-U3 order with a visible ring; no missing names; R-U3 pairs ≥ 4.5:1 |
| AC-U4.1 | 106 mode, POLY 1 lit | Shift+click POLY 2; Shift+click POLY 1; click POLY 1 | UNISON, both lit; unchanged; POLY 1 |
| AC-SC1 | Chromium on Linux | Screenshots at 50/100/200 % in both modes, bank manager, settings | ≤ 0.1 % pixel difference from committed baselines (generated by the implementer) |
| AC-SC2 | Web | Before the gesture; served without COOP/COEP; `requestMIDIAccess` rejected; granted without SysEx | The start card shows; the isolation message shows; Settings shows "MIDI not available — use the computer keyboard."; Settings shows "SysEx not permitted — SysEx disabled." and Send patch is disabled |
| AC-SC3 | `ui/dist` | Inspected | No png/jpg/gif/webp/bmp files and no `data:image/` raster URIs |
| AC-L1.1 | The repo | The licence check runs; then a test plants the GNU GPL title in a first-party file | Passes; fails |
| AC-L2.1 / L4.1 / L5.1 | Repo | None of the R-L2 GPL-3 projects (KR-106, junox, Hera, mpc-vst-jn106) is in the tree or fetched by CI; `docs/LICENSES.md` inspected; grep `Roland`, `JUNO`, `Juno-` in `ui/src`, `plugin/`, built metadata | None; contains every R-L4 entry; hits only in the R-L5 texts |
| AC-B1 | A clean checkout | The README commands run on macOS, Windows, Ubuntu, and the emcc build | All targets build and all tests pass |
| AC-B2 | `npm run web` | The page is opened (Chromium, WebKit), the start card dismissed, then a key pressed | Served on `http://localhost:5173` with COOP/COEP; analyser RMS > −60 dBFS; in Chromium a mocked Web MIDI note does the same |
| AC-N1.1 | §9.1 benchmarks | Run in CI | Native < 8 %, WASM < 35 % of real time; µs per 128-sample block reported in the CI log |

### 13.5 Traceability

| Rules / flows | Covered by |
| --- | --- |
| R-E1 – R-E13 | AC-E1.x – AC-E13.x (matching numbers); R-E9 mono and clearing AC-E9.5; R-E1 oversampling change AC-E1.4 |
| R-M1 – R-M3 | AC-M1.x – AC-M3.x |
| R-P1, P2, P4, P5/P6, P7 | AC-P1.1, P2.1, P4.1, P4.2, P5.1, P7.1; R-P3 → AC-F5.1, AC-F10.1, AC-F11.3, AC-D4.1 |
| R-S1, S2, S3 | AC-S1.1, AC-S2.1, AC-S3.1–S3.3, AC-PL3 |
| R-D1 – R-D6 | AC-D1.x, AC-F13.1 (R-D2, R-D5), AC-D3.1, AC-D4.x, AC-D6.1 |
| R-K1, R-K2 | AC-K1.1, AC-K2.x |
| R-U1, R-U1a, R-U2–U4 | AC-U1.1–U4.1 |
| R-L1 – R-L5 | AC-L1.1 (R-L1, R-L3), AC-L2.1, AC-L4.1, AC-L5.1 |
| F-1 – F-13 | AC-F1.1, P2.1 (F-2), F3.1, F4.x, F5.x, F5.1 + D4.1 (F-6), F7.1, F8.1 + E11.5, E11.3 (F-9), F10.1, F11.x, F12.1, F13.1 |
| §4.8, §5, §9.1–9.3, §15.1–15.3 | AC-PL1–PL3, AC-SC1–SC3 + AC-P4.x, AC-N1.1, AC-B1/B2, AC-D7.1 |

## 14. Implementer's latitude

These are yours to decide. Record every such choice in `DECISIONS.md`. Where a recommendation is given, you may follow it or replace it.

- **Filter solver.** Any zero-delay-feedback formulation that satisfies AC-E5.x. Recommended: TPT integrators with one Newton–Raphson iteration per sample on the feedback tanh (two at 2×, one at 4×), per-stage tanh evaluated with the previous-sample estimate; a rational tanh with ≤ 1e−4 error, shared by native and WASM. c\_cal is measured on your solver (R-E5), as one value per mode and oversampling factor or as a function of the oversampled rate.
- **Retuning (D) and (M) constants** under the precedence rule of the preface.
- **Band-limiting method** (polyBLEP, minBLEP, …) as long as AC-E3.6 passes; peak criteria exclude 2 % of the period around each edge.
- **BBD model.** Holters & Parker's variable-sample-rate line with ZOH reconstruction, or a fractional delay line with a clock-tracking lowpass plus the published fixed filters, as long as AC-E9.1 and AC-E9.2 pass; the aliasing character between them is latitude.
- **Half-band decimator** design (length, method) as long as stop-band ≥ 100 dB and AC-E1.1 holds.
- **106 control path integer formats** beyond those named (W 14-bit, LFO output 13-bit, lever read 8-bit): internal envelope word width, portamento fraction width.
- **Internal architecture beyond §9.4.**
  - Queues: sizes and overflow policy. Recommended: 4096 entries each; coalesce slider moves per parameter on the producer side; when the event queue is full, drop display events first and count drops.
  - Messages: `Command`/`Event`/`StateBlob` structures. Recommended: ≤ 32-byte PODs, with bulk payloads (banks, files) in preallocated slot pools.
  - Host state save: how state is snapshotted for a host save on the message thread. Recommended: a double-buffered snapshot published by the audio thread.
  - UI bridge. Recommended: desktop: JUCE `WebBrowserComponent` with `withNativeIntegrationEnabled`, a resource provider serving `ui/dist` from BinaryData, a native function `vjn6Command(json)` and a batched event `vjn6Events`; web: `AudioWorkletNode.port` with transferable typed arrays.
- **State blob field layout** (documented in `docs/STATE.md`).
- **Build system details:** Pamplejuce or your own CMake; CI structure.
- **Repository layout.** Recommended:

  ```
  vjn6/
    SPEC.md  KICKOFF.md
    CMakeLists.txt            # options VJN6_BUILD_PLUGIN / VJN6_BUILD_WEB / VJN6_BUILD_TESTS
    core/include/vjn6/ core/juno/ (dco.cpp hpf.cpp vcf.cpp vca.cpp env.cpp lfo.cpp chorus.cpp voice.cpp constants.h)
    core/control/ (j60_control.cpp j106_control.cpp) core/sysex/ core/panel/
    third_party/JUCE (submodule)
    tests/ (engine/ panel/ golden/)  plugin/  web/ (worklet.cpp, build.sh)
    ui/src/ (panel/ led/ bank/ settings/ skins/60/ skins/106/ bridge.ts keyboard.ts theme.css fonts/)
    banks/ (vjn6-default.vjn6bank, default/*.json)
    tools/ (make_default_bank.py, syx_inspect.py, bench/, capture/)
    docs/  .github/workflows/
  ```
- **Order of work.** Recommended, with gates: 1 voice path in 60 mode (the 60-mode cases of AC-E2–E7 green, c\_cal measured) → 2 chorus (AC-E9) → 3 106 control path (AC-E1.2, E3.2, E5.6, E6.1, E7.3, E8.2) → 4 SysEx, state, panel logic → 5 WASM goldens → 6 plugin (pluginval) → 7 UI → 8 web and performance.
- **Visual details** not fixed in §5 and §11: exact spacing inside boxes, slider cap shape, knob pointer, which cap colour each 60-mode button gets, section-header colours, tooltip layout, hover styles.
- **The eight default-bank patches' programming** (sound quality is not gated).
- **`tools/capture/`:** a script that renders the §15.4 measurement protocol as a MIDI file and a results template, so a hardware owner's recordings can be compared to the model with the same analysis code the tests use. Not a gate.

## 15. Delivery

### 15.1 How it is run

The README documents configure, build and test for desktop with CMake ≥ 3.25 (options `VJN6_BUILD_PLUGIN`, `VJN6_BUILD_WEB`, `VJN6_BUILD_TESTS`), the WASM build (`web/build.sh`) and `npm run web`.

**CI** (GitHub Actions): the matrix covers macOS (universal), Windows (MSVC), Ubuntu 24.04 (webkit2gtk-4.1) and wasm; it runs every §13 criterion, the licence check and the benchmarks; signing and notarisation steps are present but skipped unless their secrets exist.

### 15.2 Web (local hosting)

`web/build.sh` runs `emcc -O3 -msimd128 -sAUDIO_WORKLET -sWASM_WORKERS -sENVIRONMENT=web,worker,audioworklet` over `vjn6_core` plus `web/worklet.cpp`, producing `ui/public/vjn6.wasm` and its loader. `npm run web` starts the Vite dev server on `http://localhost:5173` with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`, also for `vite preview`. Page states as §5.7.

### 15.3 Default bank

`banks/vjn6-default.vjn6bank` is generated by `tools/make_default_bank.py` from readable JSON in `banks/default/*.json`:

| Slot | Content |
| --- | --- |
| 1 | `INIT` (R-P7) |
| 2–9 | Eight original patches: `BRASS`, `STRINGS`, `PAD`, `BASS`, `PLUCK`, `ORGAN`, `SWEEP`, `SFX` — written for 60 mode and checked to sound (AC-D7.1) in 106 mode |
| 10–128 | `INIT` |

No Roland factory data is included (R-L2). The bank manager imports any `.syx` the user owns.

### 15.4 Human follow-ups (not agent acceptance)

Each row is a (D) or single-source constant, or something CI cannot check. The measurement protocol: in 106 mode, send the `F0 41 31` patch named, play the notes named, record the line output at 96 kHz / 24-bit, run `tools/capture/analyze.py`; in 60 mode the same with the panel set by hand.

| Item | Constant(s) | Measurement |
| --- | --- | --- |
| Resonance law and onset | `k_max 4.5 / 4.35, V_fb 0.59`, exponent 1.5 | Self-osc patch; RES in 8 steps; noise sweep; fit k from peak height |
| VCF calibration factor | c\_cal (ratio of self-osc frequency to small-signal cutoff) | Self-osc frequency vs −12 dB point at RES 0, same FREQ |
| Q compensation | 0.23 | Same sweeps: DC gain vs k |
| 60 cutoff slider law | 28.1 Hz, 10.5 oct | Self-osc patch, KYBD 0, FREQ in 10 steps |
| Envelope slider law | square law, τ = T/5 | A/D/R in 10 steps, VCA GATE off; onset-to-level timing |
| 106 LFO rate table and delay shape | table, 0.2 s ramp | LFO rate in 10 steps; delay 127 |
| LFO depth taper | linear in cents | DCO LFO in 10 steps at rate 64 |
| Sub level and noise band limit | 1.0, white | Sub only / noise only at 127 |
| Output level reference | −24 dBFS single voice | Line level of a saw at VCA 10 vs a full chord with chorus |
| Chorus wet/dry level | G = 1.12 (Juno-6 measurement; the 106 resistor ratio 2.13 is not used) | Sine through chorus I on a 60 and a 106, dry vs wet level |
| 106 chorus rates and swing | = 60 values | Click track through Chorus I / II |
| Chorus noise spectrum | +12 dB shelf at 2 kHz (measured tilt is +28 dB at 5–15 kHz; not matched) | Silent input, chorus I, 30 s |
| 106 modulation update interval | 4.2 ms | Slow bend; count pitch steps |
| Pulse edge vs saw reset | high-going at the reset | Saw + pulse at 50 % on a scope |
| POLY 2 stealing, portamento range and curve | rotary-release-first; 3 s × square (one emulator uses cube) | Scripted note patterns; glide timing |
| Arpeggio gate length | 50 % | Arpeggio at rate 0, VCA GATE; gate width |
| HPF corner dependence on VCA LEVEL (60) | not modelled | HPF sweeps at VCA 25 / 127 |
| Panel colours and typeface | tokens in §11 | Licensed photos, visual judgement |
| Real devices and browsers | — | Safari, Web MIDI devices, audio devices in the standalone, Firefox (CI has none) |
| Signed / notarised installers | — | Apple Developer ID / Windows signing credentials |
| Public web hosting | — | A host that sets COOP/COEP |
| Trade-dress and name check | `VJN6`, `com.marcofunk.vjn6`, four-character codes | Legal review before public release; replace the bundle ID with an owned reverse domain |

### 15.5 Sources

- **Roland service literature (tier 1):** Juno-6 Service Notes (1982), Juno-60 Service Notes (1983), Juno-106 Service Notes (1984) and owner's manuals, read via ManualsLib and archive.org OCR; Roland Juno-60 and Juno-106 technical-specification pages (support.roland.com); Syntaur DIY0021 (106 calibration transcription).
- **Circuit analysis (tier 2):** Electric Druid "Roland Juno DCOs" and "IR3109"; AS3109 datasheet (Erica Synths); sequence15 Juno-106 DCO and HPF analyses; atosynth filter/VCA/resonance posts; Obsolete Technology 80017a teardown; AMSynths IR3109; Soundforce (schematic-derived clone); thea.codes "The design of the Juno DCO"; fm1juno Juno-106 chorus schematic transcription; Ultramaster KR-106 documentation and source (GPL-3, numbers only); Juno-106 firmware partial disassembly (r/synthdiy); Tubbutec Juno-66 manuals.
- **Measurements and models (tier 2–3):** Holters & Parker, "A Combined Model for a Bucket Brigade Device and its Input and Output Filters", DAFx-18; Raffel & Smith, "Practical Modeling of Bucket-Brigade Device Circuits", DAFx-10; pendragon-andyh/Juno60 chorus analysis (MIT) and junox (GPL-3); KR-106 chorus, HPF, noise and ADSR measurements; GroupDIY Juno-60 chorus LFO thread; florian-anwander.de Roland string choruses.
- **MIDI/SysEx:** Juno-106 owner's manual MIDI chart; ST-Log issue 33 (1989); MIDIbox Juno-106 translator; MIDI Kinetics; KR-106 SysEx decoder; Roland JU-06A MIDI implementation chart (CC map); Wikipedia "Digital Control Bus".
- **Panel:** Roland spec pages; E&MM Jul and Dec 1982, One Two Testing 1982, E&MM Aug 1984 (muzines.co.uk / worldradiohistory.com); taraswolf.com design review; polynominal.com.
- **Toolchain and licences:** JUCE 9 EULA and CMake API; VST3 SDK (MIT); Pamplejuce; pluginval; Emscripten Wasm Audio Worklets; MDN `KeyboardEvent.code`.
