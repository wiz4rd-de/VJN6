# VJN6 state blob

The state blob holds everything SPEC §4.7 lists: the bank with its names, the edit buffer with its name
and its EDITED and Manual flags, the stored patch, the current slot, the pending bank, the performance
settings (§4.2) and the §4.4 settings saved in state. Panel transients (LED messages, WRITE armed,
key-transpose arming, the HOLD set) and the audio/MIDI device choices are not in the blob.

It is written by `vjn6::encodeState` and read by `vjn6::decodeState` (`core/state/state.cpp`), through
`Instrument::saveState` / `Instrument::loadState`. One format serves the plugin, the standalone app and the
web page.

## Encoding

- All multi-byte integers are **little-endian**. `u8`/`u16`/`u32` are unsigned; `s16` is two's
  complement (written as the low 16 bits of the value).
- A **patch** is 25 bytes, the raw field values 0–127 in §4.1 order: B0–B15 (the 16 sliders in Juno-106
  SysEx order), then W0–W8 (range, pulse, saw, sub, chorus, PWM source, VCF polarity, VCA mode, HPF).
  Values are stored as held, not as the mode-specific readings.
- A **name** is 13 bytes: `u8` length 0–12, then 12 character bytes (ASCII 0x20–0x7E, already normalised
  per §4.3, no trailing space); the bytes after the name are 0x00.
- A **slot record** is a patch followed by a name: 38 bytes.

## Version 1 layout (4997 bytes)

| Offset | Size | Type | Field |
| ---: | ---: | --- | --- |
| 0 | 4 | bytes | Magic `"VJN6"` (0x56 0x4A 0x4E 0x36) |
| 4 | 2 | u16 | Version = 1 |
| 6 | 4864 | 128 × slot record | Bank: slot *s* (1–128) at offset 6 + 38·(*s* − 1); patch then name |
| 4870 | 25 | patch | Edit buffer patch |
| 4895 | 13 | name | Edit buffer name |
| 4908 | 25 | patch | Stored patch |
| 4933 | 1 | u8 | Flags: bit 0 = EDITED, bit 1 = Manual; other bits 0 |
| 4934 | 1 | u8 | Current slot, 1–128 |
| 4935 | 1 | u8 | Pending bank number, 1–8 |
| 4936 | 1 | u8 | Pending group, 0 = A, 1 = B (106 mode) |
| 4937 | 40 | 20 × s16 | Performance settings, in the order of the table below |
| 4977 | 16 | 8 × s16 | §4.4 settings, in the order of the table below |
| 4993 | 4 | u32 | CRC32 (IEEE 802.3: reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF) of bytes 0–4992, i.e. of everything before it |

### Performance settings (offset 4937 + 2·i)

| i | Setting | Range | Default |
| ---: | --- | --- | ---: |
| 0 | Key mode: 0 POLY 1, 1 POLY 2, 2 UNISON (106) | 0–2 | 0 |
| 1 | Portamento on (106) | 0–1 | 0 |
| 2 | Portamento time (106) | 0–127 | 32 |
| 3 | Bender DCO sensitivity | 0–127 | 64 |
| 4 | Bender VCF sensitivity | 0–127 | 0 |
| 5 | LFO trigger sensitivity (106) | 0–127 | 64 |
| 6 | LFO trigger mode: 0 AUTO, 1 MANUAL (60) | 0–1 | 0 |
| 7 | Hold (60) | 0–1 | 0 |
| 8 | Arpeggio on (60) | 0–1 | 0 |
| 9 | Arpeggio mode: 0 UP, 1 U&D, 2 DOWN (60) | 0–2 | 0 |
| 10 | Arpeggio range in octaves (60) | 1–3 | 1 |
| 11 | Arpeggio rate (60) | 0–127 | 64 |
| 12 | Octave transpose: 0 L, 1 N, 2 H (60) | 0–2 | 1 |
| 13 | Key transpose in semitones (106) | −12…+12 | 0 |
| 14 | Master tune (64 = 0 cents) | 0–127 | 64 |
| 15 | Master volume | 0–127 | 127 |
| 16 | MIDI receive channel | 1–16 | 1 |
| 17 | Omni | 0–1 | 1 |
| 18 | MIDI function: 1 = I, 2 = II, 3 = III (106) | 1–3 | 3 |
| 19 | Memory protect | 0–1 | 1 |

Entries 0–13 are the 14 performance host parameters `f_*` of §4.8, in the same order.

### Settings (offset 4977 + 2·i)

| i | Setting | Range accepted on load | Default |
| ---: | --- | --- | ---: |
| 0 | Mode: 0 = 60, 1 = 106 | 0–1 | 0 |
| 1 | Chorus noise: 0 OFF, 1 HALF, 2 ORIGINAL | 0–2 | 2 |
| 2 | Voice variation in % (steps of 10) | 0–100 | 50 |
| 3 | Velocity → VCA | 0–1 | 0 |
| 4 | Stereo output: 0 stereo, 1 mono | 0–1 | 0 |
| 5 | Oversampling factor (2 or 4) | 2–4 | 2 |
| 6 | Computer keyboard play | 0–1 | 1 (standalone, web), 0 (plugin) |
| 7 | Zoom: 0 = Fit, else 50–200 % in steps of 10 | 0–200 | 0 |

## Loading

`decodeState` classifies a blob, and the core acts on the result (`Core`, `BulkKind::State`):

| Blob | Result | Effect |
| --- | --- | --- |
| Zero length (a new plugin instance) | Empty | First-run state (R-D6), no message |
| Shorter than 10 bytes; magic ≠ `"VJN6"`; CRC mismatch; version 0; version 1 with a length ≠ 4997; version > 1 shorter than 4997 bytes | Invalid | First-run state and the LED message `rE`; the standalone app renames the file to `state.bad` (F-1) |
| Any field out of range: a patch byte > 127, a name length > 12, a name character outside 0x20–0x7E or a trailing space, current slot outside 1–128, pending bank outside 1–8, pending group outside 0–1, a performance setting or a setting outside its range above | Invalid | As above |
| Version 1, valid | Ok | Everything is loaded |
| Version > 1 whose version-1 prefix is valid | OkNewer | Bank, edit buffer and name, stored patch, flags, current slot, pending bank and group and the performance settings are loaded; the §4.4 settings keep their current values and everything after the version-1 prefix is ignored; no message |

The CRC is checked over the whole blob (everything but its last 4 bytes) before any field is read. The
version-1 fields of a newer blob are range-checked like a version-1 blob, settings included, even though
the settings are not applied.

## Versioning rule

- The version-1 fields form a prefix that later versions never reorder, resize or reinterpret. A later
  version raises the version number, **appends** its new fields after offset 4993 and keeps the CRC32 as
  the last 4 bytes, computed over everything before it.
- A reader that meets a newer version loads the bank, the buffers and the performance settings from the
  version-1 prefix and ignores the rest, without a message (§4.7).
- Bad magic, a bad CRC, truncation or an unknown older version (0) discard the blob: first-run state and
  `rE`. A zero-length blob gives first-run state without a message.

## Where the blob lives

| Build | Location | Notes |
| --- | --- | --- |
| Plugin | Inside the host project, one blob per instance (`getStateInformation` / `setStateInformation`) | Saved whenever the host asks (F-13). A new instance gets a zero-length blob → first-run state. |
| Standalone (R-D2) | macOS `~/Library/Application Support/VJN6/state.bin`; Windows `%APPDATA%\VJN6\state.bin`; Linux `~/.config/VJN6/state.bin` | The directory is created if missing. Saved 1 s after the last change, at least every 5 s during continuous changes, and on quit, by writing a temporary file and renaming it over `state.bin`. An unreadable or invalid `state.bin` is renamed to `state.bad` (replacing an older one). `bank.vjn6bank` (the canonical bank, R-D3) is written beside it at the same time as a copy for the user and is never read back. Audio/MIDI device choices live in the JUCE standalone properties file, not in the blob. |
| Web (R-D5) | IndexedDB database `vjn6`, object store `state`, key `current` | The blob bytes. Same save timing as the standalone, plus a final save on `visibilitychange` (hidden) and `pagehide`. The Web MIDI device choices are under the key `devices` of the same store, not in the blob. Two tabs save independently; the last write wins. Without IndexedDB VJN6 runs in memory and shows "Changes will not be saved in this browser." |
