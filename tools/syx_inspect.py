#!/usr/bin/env python3
"""Prints the SysEx messages of a .syx file as SPEC §4.5 defines them, and how VJN6 would import it (§4.6).

  python3 tools/syx_inspect.py FILE [FILE ...]          full decode: every message, d0-d15, sw1 / sw2
  python3 tools/syx_inspect.py --brief FILE             one line per message
  python3 tools/syx_inspect.py --hex FILE               also print the raw bytes of each message

Messages are found the way VJN6 scans a file: each F0 starts a candidate that runs over data bytes
(< 0x80) up to the next status byte, which must be F7. Juno-106 messages are F0 41 30 / 31 (24 bytes:
patch and manual dumps) and F0 41 32 (7 bytes: parameter change); anything else is listed as ignored.

For a file of two or more 30/31 messages the slot each message would be written to is shown for an
import into an all-INIT bank (a 30 message goes to slot pp + 1 unless this import already wrote it;
then, like every 31 message, to the lowest-numbered slot not yet written). Standard library only.
"""
import argparse
import pathlib
import sys

MAX_FILE = 1 << 20  # files over 1 MiB are rejected (§4.6)

SLIDERS = ["LFO rate", "LFO delay time", "DCO LFO depth", "DCO PWM", "Noise level", "VCF frequency",
           "VCF resonance", "VCF envelope depth", "VCF LFO depth", "VCF keyboard follow", "VCA level",
           "ENV attack", "ENV decay", "ENV sustain", "ENV release", "Sub-oscillator level"]
RANGES = ["16'", "8'", "4'"]
CHORUS = ["off", "I", "II", "I+II"]
TYPE_NAMES = {0x30: "patch dump", 0x31: "manual dump", 0x32: "parameter change"}


def slider_display(v):
    """R-P5 display value v/12.7 with one decimal, rounded half up (v >= 0)."""
    tenths = (200 * v + 127) // 254
    return "%d.%d" % (tenths // 10, tenths % 10)


def slot_name(slot):
    """106 notation of slot 1-128: A11..A88, B11..B88."""
    k = (slot - 1) % 64
    return "%s%d%d" % ("A" if slot <= 64 else "B", k // 8 + 1, k % 8 + 1)


def decode_sw1(b):
    """Returns (fields, notes): W0 range, W1 pulse, W2 saw, W4 chorus as VJN6 reads them."""
    notes = []
    bits = b & 7
    if bits in (1, 2, 4):
        rng = {1: 0, 2: 1, 4: 2}[bits]
    else:
        rng = 1
        notes.append("range bits %s: none or several set -> 8'" % format(bits, "03b"))
    pulse = (b >> 3) & 1
    saw = (b >> 4) & 1
    if (b >> 5) & 1:
        chorus = 0
        if (b >> 6) & 1:
            notes.append("bit 6 set while chorus is off (not canonical)")
    else:
        chorus = 1 if (b >> 6) & 1 else 2
    if b & 0x80:
        notes.append("bit 7 set (not a data byte)")
    return {"range": rng, "pulse": pulse, "saw": saw, "chorus": chorus}, notes


def decode_sw2(b):
    """Returns (fields, notes): W5 PWM source, W6 VCF polarity, W7 VCA mode, W8 HPF."""
    notes = []
    if b & 0x60:
        notes.append("bits 5-6 = %d (ignored on receive, not canonical)" % ((b >> 5) & 3))
    return {"pwm_src": b & 1, "vcf_pol": (b >> 1) & 1, "vca_mode": (b >> 2) & 1,
            "hpf": 3 - ((b >> 3) & 3)}, notes


def describe_sw1(b):
    f, notes = decode_sw1(b)
    text = "range %s, pulse %s, saw %s, chorus %s" % (
        RANGES[f["range"]], "on" if f["pulse"] else "off", "on" if f["saw"] else "off", CHORUS[f["chorus"]])
    return text, notes


def describe_sw2(b):
    f, notes = decode_sw2(b)
    text = "PWM %s, VCF %s, VCA %s, HPF %d (bits 3-4 = %d)" % (
        "MANUAL" if f["pwm_src"] else "LFO", "inverted" if f["vcf_pol"] else "normal",
        "GATE" if f["vca_mode"] else "ENV", f["hpf"], (b >> 3) & 3)
    return text, notes


def scan(data):
    """Yields (offset, bytes, status) for every F0 candidate, in file order (VJN6's scanner)."""
    i, n = 0, len(data)
    while i < n:
        if data[i] != 0xF0:
            i += 1
            continue
        j = i + 1
        while j < n and data[j] < 0x80:
            j += 1
        if j >= n or data[j] != 0xF7:
            end = j if j < n else n
            yield i, data[i:end], "unterminated"
            i += 1
            continue
        yield i, data[i:j + 1], "ok"
        i = j + 1


def classify(msg):
    """Returns (kind, detail): kind in dump / param / ignored."""
    if len(msg) < 3 or msg[1] != 0x41:
        man = ("0x%02X" % msg[1]) if len(msg) > 1 else "none"
        return "ignored", "not a Roland message (manufacturer %s)" % man
    t = msg[2]
    if t in (0x30, 0x31):
        if len(msg) == 24:
            return "dump", None
        return "ignored", "F0 41 %02X with %d bytes (must be 24)" % (t, len(msg))
    if t == 0x32:
        if len(msg) != 7:
            return "ignored", "F0 41 32 with %d bytes (must be 7)" % len(msg)
        if msg[4] > 0x11:
            return "ignored", "parameter change with pp 0x%02X > 0x11" % msg[4]
        return "param", None
    return "ignored", "Roland message type 0x%02X (not Juno-106)" % t


def hexdump(msg):
    return " ".join("%02X" % b for b in msg)


def inspect(path, brief=False, show_hex=False, out=sys.stdout):
    data = pathlib.Path(path).read_bytes()
    w = out.write
    w("%s: %d bytes\n" % (path, len(data)))
    messages = list(scan(data))
    dumps = []
    covered = 0
    for idx, (off, msg, status) in enumerate(messages, 1):
        covered += len(msg) if status == "ok" else 0
        if status != "ok":
            w("#%d @0x%04X  unterminated SysEx (%d bytes before a status byte or the end): ignored\n"
              % (idx, off, len(msg)))
            continue
        kind, detail = classify(msg)
        if kind == "ignored":
            w("#%d @0x%04X  %s: ignored\n" % (idx, off, detail))
            if show_hex:
                w("    %s\n" % hexdump(msg))
            continue
        t, ch, pp = msg[2], msg[3], msg[4]
        chan = "channel %d" % (ch + 1) if ch < 16 else "channel byte 0x%02X" % ch
        if kind == "param":
            if pp < 16:
                what = "d%d %s = %d (%s)" % (pp, SLIDERS[pp], msg[5], slider_display(msg[5]))
            elif pp == 0x10:
                text, notes = describe_sw1(msg[5])
                what = "sw1 = 0x%02X: %s%s" % (msg[5], text, "".join("; " + n for n in notes))
            else:
                text, notes = describe_sw2(msg[5])
                what = "sw2 = 0x%02X: %s%s" % (msg[5], text, "".join("; " + n for n in notes))
            w("#%d @0x%04X  F0 41 32 parameter change, %s, pp 0x%02X: %s\n" % (idx, off, chan, pp, what))
            if show_hex:
                w("    %s\n" % hexdump(msg))
            continue
        dumps.append((idx, off, msg))
        head = "#%d @0x%04X  F0 41 %02X %s, %s, pp %d" % (idx, off, t, TYPE_NAMES[t], chan, pp)
        if t == 0x30:
            head += " (slot %d = %s)" % (pp + 1, slot_name(pp + 1))
        elif pp:
            head += " (ignored on receive)"
        sw1_text, sw1_notes = describe_sw1(msg[21])
        sw2_text, sw2_notes = describe_sw2(msg[22])
        if brief:
            w("%s  d=[%s] sw1=0x%02X sw2=0x%02X\n" % (head, " ".join("%d" % b for b in msg[5:21]), msg[21], msg[22]))
            continue
        w(head + "\n")
        if show_hex:
            w("    %s\n" % hexdump(msg))
        for k in range(8):
            a, b = k, k + 8
            w("    d%-2d %-20s %3d (%4s)    d%-2d %-20s %3d (%4s)\n" % (
                a, SLIDERS[a], msg[5 + a], slider_display(msg[5 + a]),
                b, SLIDERS[b], msg[5 + b], slider_display(msg[5 + b])))
        w("    sw1 0x%02X = %s: %s\n" % (msg[21], format(msg[21], "07b"), sw1_text))
        for note in sw1_notes:
            w("        note: %s\n" % note)
        w("    sw2 0x%02X = %s: %s\n" % (msg[22], format(msg[22], "07b"), sw2_text))
        for note in sw2_notes:
            w("        note: %s\n" % note)
        f1, _ = decode_sw1(msg[21])
        f2, _ = decode_sw2(msg[22])
        w("    VJN6 fields: W0 range %d, W1 pulse %d, W2 saw %d, W3 sub 1 (import), W4 chorus %d, "
          "W5 pwm_src %d, W6 vcf_pol %d, W7 vca_mode %d, W8 hpf %d\n"
          % (f1["range"], f1["pulse"], f1["saw"], f1["chorus"], f2["pwm_src"], f2["vcf_pol"], f2["vca_mode"],
             f2["hpf"]))
    skipped = len(data) - covered
    params = sum(1 for _, m, s in messages if s == "ok" and classify(m)[0] == "param")
    w("summary: %d SysEx candidates, %d dumps (%d x 30, %d x 31), %d parameter changes, %d bytes outside messages\n"
      % (len(messages), len(dumps), sum(1 for d in dumps if d[2][2] == 0x30),
         sum(1 for d in dumps if d[2][2] == 0x31), params, skipped))
    w("VJN6 import: %s\n" % import_verdict(path, len(data), dumps, params))
    if len(dumps) >= 2 and not brief:
        for line in bank_placement(dumps):
            w("    " + line + "\n")


def import_verdict(path, size, dumps, params):
    if size > MAX_FILE:
        return "rejected (file over 1 MiB) -> Er"
    if not dumps:
        return "malformed (no F0 41 30/31 message) -> Er"
    tail = "; then %d parameter change(s) applied to the edit buffer as external changes" % params if params else ""
    if len(dumps) == 1:
        return "patch load into the edit buffer (Manual), name '%s'%s" % (pathlib.Path(path).stem[:12], tail)
    return "bank import of %d messages in file order%s" % (len(dumps), tail)


def bank_placement(dumps):
    """Slot targets for an import into an all-INIT bank (§4.6), with the names VJN6 gives them."""
    written = [False] * 128
    lines, n_written, dropped = [], 0, 0
    for k, (idx, off, msg) in enumerate(dumps, 1):
        slot = None
        if msg[2] == 0x30 and not written[msg[4]]:
            slot = msg[4] + 1
        else:
            for s in range(128):
                if not written[s]:
                    slot = s + 1
                    break
        name = slot_name(msg[4] + 1) if msg[2] == 0x30 else "MANUAL %d" % k
        if slot is None:
            dropped += 1
            lines.append("message #%d: dropped (no slot left)" % idx)
            continue
        written[slot - 1] = True
        n_written += 1
        lines.append("message #%d -> slot %d (%s), name '%s'" % (idx, slot, slot_name(slot), name))
    lines.append("into an all-INIT bank: %d patches imported, %d dropped" % (n_written, dropped))
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description="Decode Juno-106 SysEx (.syx) files as SPEC §4.5 defines them.")
    ap.add_argument("files", nargs="+", help=".syx files")
    ap.add_argument("--brief", action="store_true", help="one line per message")
    ap.add_argument("--hex", action="store_true", help="print the raw bytes of each message")
    args = ap.parse_args(argv)
    status = 0
    for i, f in enumerate(args.files):
        if i:
            print()
        try:
            inspect(f, brief=args.brief, show_hex=args.hex)
        except OSError as e:
            print("%s: %s" % (f, e), file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
