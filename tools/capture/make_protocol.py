#!/usr/bin/env python3
"""Writes the SPEC §15.4 measurement protocol as a Standard MIDI File plus results templates.

  python3 tools/capture/make_protocol.py [-o DIR] [--mode 106|60|both]

For each mode it writes into DIR (default: ./vjn6-capture):
  vjn6-protocol-<mode>.mid         SMF format 1, 120 bpm, 960 ticks per quarter. Track 0 holds the tempo
                                   and the instructions; then one track per measurable §15.4 row, laid out
                                   one after another. Each row track starts with the performance reset
                                   (CCs for VJN6) and a sync click (note 96, 50 ms), then for every step a
                                   marker naming the step, a text event with the panel settings, the
                                   F0 41 31 patch message and the notes / bends / CCs to play. Channel 1.
  results-template-<mode>.csv      one line per step and measurement, with the model value where the
                                   spec gives one; analyze.py writes the same columns with 'measured' filled.
  results-template-<mode>.md       the procedure and the same table per row, for filling in by hand.

106 mode: set the Juno-106 (or VJN6 in 106 mode) to MIDI channel 1 and MIDI function III, play the file
from the start and record the line outputs (stereo, 96 kHz / 24-bit) in one take. 60 mode: the panel is
set by hand from each step's text (a Juno-60 has no MIDI); record one file per step named <step>.wav.
VJN6 in 60 mode takes the same file over MIDI. Standard library only.
"""
import argparse
import csv
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import protocol as P  # noqa: E402


def varlen(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def ticks(seconds):
    return int(round(seconds * P.TICKS_PER_SECOND))


def meta(kind, data):
    return bytes([0xFF, kind]) + varlen(len(data)) + data


def text(kind, s):
    return meta(kind, s.encode("ascii", "replace"))


# order of events that share a tick: meta, SysEx, controllers / bend, note-off, note-on
PRIORITY = {"meta": 0, "sysex": 1, "cc": 2, "bend": 2, "note_off": 3, "note_on": 4}


def channel_event(ev):
    kind = ev[1]
    ch = P.CHANNEL
    if kind == "note_on":
        return bytes([0x90 | ch, ev[2], P.VELOCITY])
    if kind == "note_off":
        return bytes([0x80 | ch, ev[2], 0x40])
    if kind == "cc":
        return bytes([0xB0 | ch, ev[2], ev[3]])
    if kind == "bend":
        return bytes([0xE0 | ch, ev[2] & 0x7F, (ev[2] >> 7) & 0x7F])
    raise ValueError(kind)


def track_chunk(events):
    """events: (seconds, priority, bytes). Returns an MTrk chunk with an end-of-track event."""
    events = sorted(events, key=lambda e: (ticks(e[0]), e[1]))
    body, last = bytearray(), 0
    for t, _, data in events:
        tk = ticks(t)
        body += varlen(tk - last) + data
        last = tk
    body += varlen(0) + meta(0x2F, b"")
    return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def sysex_event(msg):
    assert msg[0] == 0xF0 and msg[-1] == 0xF7
    return bytes([0xF0]) + varlen(len(msg) - 1) + msg[1:]


def instructions(mode):
    if mode == "106":
        return ["VJN6 capture protocol, 106 mode (SPEC 15.4).",
                "Juno-106 or VJN6 in 106 mode: MIDI channel 1, MIDI function III; VJN6 Settings: mode 106.",
                "Record the line outputs L and R (stereo) at 96 kHz / 24-bit in one take from before the start "
                "to after the end, then run tools/capture/analyze.py --recording take.wav --mode 106.",
                "Performance controls named in a step text (key mode, portamento, bender) are set by hand on "
                "the hardware; VJN6 receives them as CCs."]
    return ["VJN6 capture protocol, 60 mode (SPEC 15.4).",
            "Juno-60: set the panel by hand from each step text and play the notes listed; record one file "
            "per step named <step>.wav (96 kHz / 24-bit, stereo), then run tools/capture/analyze.py "
            "--steps-dir DIR --mode 60.",
            "VJN6 in 60 mode receives the panel as the F0 41 31 message and can play this file in one take."]


def build_midi(mode):
    tracks, total = P.schedule(mode)
    conductor = [(0.0, 0, meta(0x03, ("VJN6 capture protocol %s" % mode).encode())),
                 (0.0, 0, meta(0x51, struct.pack(">I", P.TEMPO_US)[1:])),
                 (0.0, 0, meta(0x58, bytes([4, 2, 24, 8])))]
    for line in instructions(mode):
        conductor.append((0.0, 0, text(0x01, line)))
    conductor.append((total, 0, text(0x06, "end of protocol")))
    chunks = [track_chunk(conductor)]
    for tr in tracks:
        evs = [(tr.start, 0, text(0x03, tr.row.item)),
               (tr.start, 0, text(0x06, "%s: %s" % (tr.row.key, tr.row.item))),
               (tr.start, 0, text(0x01, "Measurement: %s. Constants: %s." % (tr.row.measurement, tr.row.constants)))]
        if tr.row.reuses:
            note = "No recording: uses the takes of '%s' (%s)." % (tr.row.reuses, tr.row.how)
            evs.append((tr.start, 0, text(0x01, note)))
        for t, kind, *data in P.reset_events():
            evs.append((tr.start + t, PRIORITY[kind], channel_event((t, kind, *data))))
        sync = P.resolve(P.SYNC, mode)
        evs.append((tr.start + P.SYNC_PATCH_AT, 1, sysex_event(P.sysex_31(sync))))
        evs.append((tr.sync_time, 4, channel_event((0, "note_on", P.SYNC_NOTE))))
        evs.append((tr.sync_time + P.SYNC_LEN, 3, channel_event((0, "note_off", P.SYNC_NOTE))))
        for step, t0 in zip(tr.steps, tr.step_starts):
            p = P.resolve(step.patch, mode)
            evs.append((t0, 0, text(0x06, "%s: %s" % (step.id, step.title))))
            panel = "Panel: " + P.panel_text(p, mode) + (". Set: " + step.perf if step.perf else "")
            evs.append((t0, 0, text(0x01, panel)))
            evs.append((t0, 1, sysex_event(P.sysex_31(p))))
            for ev in step.events:
                evs.append((t0 + ev[0], PRIORITY[ev[1]], channel_event(ev)))
        evs.append((tr.end, 0, text(0x06, "end of %s" % tr.row.key)))
        chunks.append(track_chunk(evs))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), P.TICKS_PER_QUARTER)
    return header + b"".join(chunks), tracks, total


def note_list(step):
    ons = [(ev[0], ev[2]) for ev in step.events if ev[1] == "note_on"]
    offs = {}
    for ev in step.events:
        if ev[1] == "note_off":
            offs.setdefault(ev[2], []).append(ev[0])
    out = []
    for t, n in ons:
        later = [o for o in offs.get(n, []) if o > t]
        dur = (min(later) - t) if later else None
        out.append("note %d at %.2f s%s" % (n, t, " for %.2f s" % dur if dur is not None else ""))
    if any(ev[1] == "bend" for ev in step.events):
        out.append("pitch bend ramp")
    return ", ".join(out) if out else "no notes"


def write_templates(mode, out_dir, tracks):
    rows = P.template_rows(mode)
    csv_path = out_dir / ("results-template-%s.csv" % mode)
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=P.TEMPLATE_COLUMNS)
        w.writeheader()
        w.writerows(rows)
    md = ["# VJN6 measurement results, %s mode" % mode, ""]
    md += ["Protocol (SPEC 15.4): %s" % " ".join(instructions(mode)), "",
           "Recorder: %s. Unit / serial: ________. Date: ________. Recording: ________." % (
               "Juno-106 or VJN6 (106 mode)" if mode == "106" else "Juno-60 or VJN6 (60 mode)"), ""]
    by_row = {}
    for r in rows:
        by_row.setdefault(r["row"], []).append(r)
    steps_by_id = {s.id: (s, t0) for tr in tracks for s, t0 in zip(tr.steps, tr.step_starts)}
    for tr in tracks:
        row = tr.row
        md += ["## %s" % row.item, "",
               "Constants: %s. Measurement: %s." % (row.constants, row.measurement)]
        if row.reuses:
            md.append("No separate recording: %s." % row.how)
        md += ["", "| Step | Start (s) | Patch and notes | Measurement | Unit | Model | Measured |",
               "| --- | ---: | --- | --- | --- | --- | --- |"]
        last = None
        for r in by_row.get(row.key, []):
            step, _ = steps_by_id.get(r["step"], (None, None))
            if r["step"] != last and step is not None:
                p = P.resolve(step.patch, mode)
                desc = "%s. %s. %s%s" % (step.title, note_list(step), P.panel_text(p, mode),
                                         (". Set: " + step.perf) if step.perf else "")
            elif r["step"] != last:
                desc = r["description"]
            else:
                desc = ""
            first = r["step"] != last
            md.append("| %s | %s | %s | %s | %s | %s | |" % (
                r["step"] if first else "", r["start_s"] if first else "", desc.replace("|", "/"),
                r["measurement"], r["unit"], r["model"]))
            last = r["step"]
        notes = sorted({r["notes"] for r in by_row.get(row.key, []) if r["notes"]})
        if notes:
            md += [""] + ["- %s" % n for n in notes]
        md.append("")
    md += ["## Not covered by recordings", ""] + ["- %s" % n for n in P.NOT_RECORDED] + [""]
    md_path = out_dir / ("results-template-%s.md" % mode)
    md_path.write_text("\n".join(md), encoding="utf-8", newline="\n")
    return csv_path, md_path, len(rows)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Write the SPEC 15.4 measurement protocol (MIDI file + templates).")
    ap.add_argument("-o", "--out", default="vjn6-capture", help="output directory (default: ./vjn6-capture)")
    ap.add_argument("--mode", choices=["106", "60", "both"], default="both")
    args = ap.parse_args(argv)
    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    for mode in (P.MODES if args.mode == "both" else (args.mode,)):
        data, tracks, total = build_midi(mode)
        mid = out_dir / ("vjn6-protocol-%s.mid" % mode)
        mid.write_bytes(data)
        csv_path, md_path, n = write_templates(mode, out_dir, tracks)
        n_steps = sum(len(t.steps) for t in tracks)
        print("%s mode: %s (%d tracks, %d steps, %.1f min), %s and %s (%d measurements)" % (
            mode, mid, len(tracks) + 1, n_steps, total / 60.0, csv_path.name, md_path.name, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
