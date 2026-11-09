# UI bridge protocol

The Svelte UI (`ui/`) is one bundle for all builds. It talks to the core only through JSON messages.
The C++ side of the protocol is implemented once in `core/bridge/bridge.cpp` (`vjn6::Bridge`,
header `core/include/vjn6/bridge.h`) and is wrapped by two hosts:

- **Desktop** (plugin and standalone, `plugin/`): JUCE `WebBrowserComponent` with native integration.
  UI → host: native function `vjn6Command(jsonString)`. Host → UI: event `vjn6Events` whose payload is
  a JSON **array of message strings**, emitted by a ~60 Hz timer that calls `Bridge::poll`.
- **Web** (`web/worklet.cpp`, built by `web/build.sh` into `ui/public/vjn6.js` + `ui/public/vjn6.wasm`):
  the same `Bridge` runs on the browser main thread inside the WASM module; the core renders in a Wasm
  Audio Worklet that shares the module memory. See "Web module API" below.

All numbers are integers. Field, perf and setting ids are the enums of `core/include/vjn6/types.h`.

## UI → core messages

| `t` | Fields | Meaning |
| --- | --- | --- |
| `ready` | — | UI connected; the next poll sends the complete state including the bank (R-S3). |
| `field` | `f` 0–24, `v` 0–127, `src`? | Set a patch field (B0–B15 = 0–15, W0–W8 = 16–24). `src` defaults to `"panel"`. |
| `perf` | `p` 0–19, `v`, `src`? | Set a performance setting (`Perf` enum). Panel knobs/switches: `"panel"`; Settings overlay: `"overlay"`. |
| `setting` | `s` 0–7, `v` | Set a §4.4 setting (`SettingId`); source is the overlay. Mode is `s:0`, `v` 0 = 60, 1 = 106. |
| `press` | `b`, `i`?, `shift`? | Button press. `b` ∈ `range` (`i` 0/1/2), `pulse`, `saw`, `sub`, `chorusOff`, `chorusI`, `chorusII`, `portamento`, `hold`, `arpOn`, `bank` (`i` 1–8), `group` (`i` 0 A / 1 B), `patch` (`i` 1–8), `manual`, `write`, `poly1`, `poly2`, `keyTranspose`, `lfoTrig`. `shift: true` = combination press (R-U4). Shortcuts Alt+M/W/1–8 send the same presses. |
| `release` | `b` | Momentary release (`lfoTrig`). |
| `lever` | `v` −8191…8191, `drag`, `push`?, `src`? | Lever value (v/8191). `drag: true` while the mouse holds it (incoming pitch bend ignored). `push` (106) only when it changes: mouse-down in the upper half / release. Keys 1/2 send `src: "keyboard"`. |
| `push` | `on` | Key 3 held/released (106 lever push, 60 LFO TRIG). |
| `note` | `n`, `vel` | Computer-keyboard note; `vel` 0 = note off. |
| `pedal` | `on` | Space (hold pedal). |
| `midiIn` | `port` 0–15, `bytes` | One complete MIDI message ≤ 24 bytes from a Web MIDI input (web only; ports from 1). |
| `midiInputGone` | `port` | A Web MIDI input vanished or was disabled: acts as CC123 for that input's notes and clears the HOLD set and arpeggio (R-M1). |
| `selectSlot` | `slot` 1–128 | Bank-manager double-click (F-4, transmits in 106 mode). |
| `rename` | `slot`, `name` | Bank-manager rename (normalised by the core, §4.3; refused with `Pr` under protect). |
| `copySlot` | `from`, `to` | Row drop onto a row. |
| `slotToEdit` | `slot` | Row drop onto the edit-buffer target. |
| `sendPatch` | — | Bank manager Send patch. |
| `releaseAll` | — | Editor close / window blur: keyboard notes, LFO TRIG, push, lever to centre. |
| `message` | `m` | Show an LED message (`LedMessage` enum: 1 `Pr`, 2 `Er`, 3 `rc`, 4 `ok`, 5 `rE`). |
| `loadFile` | `name`, `b64` | File contents (base64) from the file picker or a drop. |
| `export` | `what` | `bankJson`, `bankSyx`, `patchJson`, `patchSyx`. Web: answered with `exportData`. Desktop: handled by the host (native save dialog). |

Host-only messages (not core; desktop host or web glue handles them): `openFile` (desktop native open
dialog), `openDevices` (standalone JUCE device selector), `midiOutput` (`name`: the chosen output's `id`
from `env.outputs`, `""` = none), `gesture` (`f` field or `p` perf, `begin` true/false: host automation
gesture, plugin; ignored on the web), `zoom` (`v`: 0 = Fit or 50…200).

## Core → UI messages

| `t` | Fields |
| --- | --- |
| `state` | `seq`, `mode` (0/1), `edit` [25], `name`, `stored` [25], `edited`, `manual`, `slot` 1–128, `pendingBank` 1–8, `pendingGroup` 0/1, `perf` [20], `settings` [8], `led` {`text` 2 chars, `dp`, `blink`, `message` 0–5}, `lit` [bit indices], `blinking` [bit indices], `writeArmed`, `ktArmed`, `lfoTrig`, `push`, `lever` (−8191…8191), `latency`, `bankVersion`, `bank`? [128 × {`n` name, `p` [25]}] (present on `ready` and whenever the bank changed). |
| `notice` | `kind` ∈ `loaded`, `bankLoaded`, `imported` (`i` written, `j` dropped), `protected`, `malformed`, `unreadable`, `writeFailed` (desktop export: `file` is the path); `file` (name of the last loaded file) |
| `exportData` | `what`, `name` (export file name, §4.6), `b64` |
| `midiOut` | `bytes` (web only: send through the chosen Web MIDI output) |
| `status` | `text` (Settings status line, e.g. "Could not save state: …") |
| `env` | host description sent after `ready` and again whenever MIDI availability or the device lists change: `build` (`plugin`/`standalone`/`web`), `midiOutAvailable`, `sysexPermitted`, `midiAvailable`, `inputs` [{`id`,`name`,`enabled`}], `outputs` [{`id`,`name`}], `output` (selected id or ""), `saveError` ("" or text) |

LED characters: `0`–`9`, `P`, `r`, `E`, `o`, `k`, `c`, `t`, `-`, space (blank). Button LED bit indices
are the `ButtonLed` enum of `core/include/vjn6/instrument.h`:
`0 R16, 1 R8, 2 R4, 3 PULSE, 4 SAW, 5 SUB, 6 CHORUS_OFF, 7 CHORUS_I, 8 CHORUS_II, 9 PORTA, 10 HOLD, 11 ARP,
12 POLY1, 13 POLY2, 14 GROUP_A, 15 GROUP_B, 16–23 BANK1–8, 24–31 PATCH1–8, 32 MANUAL, 33 WRITE,
34 KEY_TRANSPOSE, 35 LFO_TRIG`. Blinking (WRITE, KEY TRANSPOSE armed) runs 400 ms on / 400 ms off in the UI.

## Web module API (`ui/public/vjn6.js`, Emscripten `MODULARIZE`, `EXPORT_NAME=createVjn6`)

Built with `-sAUDIO_WORKLET -sWASM_WORKERS` (§15.2; emcc 6 names the environment `web,worker,worklet`). Exported C functions (call with `ccall`/`cwrap`;
strings are UTF-8, byte buffers via `_malloc`/`HEAPU8`):

| Function | Thread | Purpose |
| --- | --- | --- |
| `int vjn6_init(double sampleRate)` | main | Creates the instrument at the AudioContext rate (before the user gesture; the context may be suspended). |
| `int vjn6_load_state(uint8_t* data, int len)` | main | State blob from IndexedDB (`vjn6`/`state`/`current`); 0-length = first run. Returns 1 if valid. |
| `int vjn6_save_state(void)` | main | Serialises state; returns length; bytes at `vjn6_state_ptr()`. |
| `uint8_t* vjn6_state_ptr(void)` | main | |
| `void vjn6_command(const char* json)` | main | One UI → core message (`Bridge::handleUiMessage`). |
| `const char* vjn6_poll(void)` | main | JSON array of core → UI message objects (call from `requestAnimationFrame`, ≤ 60 Hz). |
| `void vjn6_load_file(const char* name, uint8_t* data, int len)` | main | File bytes (notices come back through `vjn6_poll`). |
| `int vjn6_start_audio(void)` | main | Called on the first user gesture: creates/resumes the Wasm audio worklet node on the context. Returns 0 on failure ("No audio output available."). |
| `int vjn6_audio_context(void)` | main | Emscripten audio-context handle; JS gets the `AudioContext` with `emscriptenGetAudioObject(handle)` (also used for the test-only `AnalyserNode` tap). |
| `int vjn6_audio_node(void)` | main | Handle of the `AudioWorkletNode`. |

Until audio starts (or if it cannot), the main thread drives the core with silent renders so that state,
LED messages and commands keep working (DECISIONS.md "Fallback driver").
