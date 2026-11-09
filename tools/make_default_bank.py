#!/usr/bin/env python3
"""Builds the VJN6 default bank (SPEC §15.3, v3.2).

Slots 1-56 are the 56 Juno-60 factory patches, generated from the sample-sound charts of the Juno-60
Owner's Manual (pp. 25-28) as printed in banks/j60-factory-chart.csv; slot 57 is INIT; slots 58-65 are the
eight original VJN6 patches; slots 66-128 are INIT (R-P7).

Reads
  banks/j60-factory-chart.csv     the chart values as printed (one row per factory patch)
  banks/default/0NN-*.json        the eight originals (slots 58-65), hand-written
and writes
  banks/default/0NN-*.json        one readable source file per factory patch (slots 1-56)
  banks/vjn6-default.vjn6bank     canonical .vjn6bank (R-D3)
  core/state/default_bank.inc     the same bank compiled into vjn6_core for first-run state
  docs/FACTORY_BANK.md            attribution, manual names, the charts' performance settings, notes

Run from anywhere:
  python3 tools/make_default_bank.py [--check]
--check exits non-zero if any generated file differs from the committed one.
"""
import csv
import json
import math
import pathlib
import sys
from fractions import Fraction

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHART = ROOT / "banks" / "j60-factory-chart.csv"
SRC_DIR = ROOT / "banks" / "default"
SLIDERS = ["lfo_rate", "lfo_delay", "dco_lfo", "dco_pwm", "noise", "vcf_freq", "vcf_res", "vcf_env",
           "vcf_lfo", "vcf_kybd", "vca_level", "env_a", "env_d", "env_s", "env_r", "sub_level"]
SWITCHES = ["range", "pulse", "saw", "sub", "chorus", "pwm_src", "vcf_pol", "vca_mode", "hpf"]
SOURCE = "Roland Juno-60 Owner's Manual, sample sounds pp. 24-28"
FIRST_ORIGINAL_SLOT = 58

# Juno-60 patch number -> (VJN6 name, at most 12 characters; the manual's full name).
# The VJN6 names are SPEC §15.3. The full names follow the manual's list on p. 24 as transcribed by
# synthmania.com ("juno 60"), with two evident transcription slips corrected (58 "PMW" -> "PWM";
# 76 "Space Sound 2" -> "Space Sound 3"); see docs/FACTORY_BANK.md.
NAMES = {
    11: ("Strings 1", "Strings 1"), 12: ("Strings 2", "Strings 2"), 13: ("Strings 3", "Strings 3"),
    14: ("Organ 1", "Organ 1"), 15: ("Organ 2", "Organ 2"), 16: ("Organ 3", "Organ 3"),
    17: ("Brass", "Brass"), 18: ("Phase Brass", "Phase Brass"),
    21: ("Piano 1", "Piano 1"), 22: ("Piano 2", "Piano 2"), 23: ("Celesta", "Celesta"),
    24: ("Mellow Piano", "Mellow Piano"), 25: ("Harpsichd 1", "Harpsichord 1"),
    26: ("Harpsichd 2", "Harpsichord 2"), 27: ("Guitar", "Guitar"), 28: ("Synth Harp", "Synthesizer Harp"),
    31: ("Bass 1", "Bass 1"), 32: ("Bass 2", "Bass 2"), 33: ("Clavichord 1", "Clavichord 1"),
    34: ("Clavichord 2", "Clavichord 2"), 35: ("Pizzicato 1", "Pizzicato Sound 1"),
    36: ("Pizzicato 2", "Pizzicato Sound 2"), 37: ("Xylophone", "Xylophone"),
    38: ("Glockenspiel", "Glockenspiel"),
    41: ("Violine", "Violine"), 42: ("Trumpet", "Trumpet"), 43: ("Horn", "Horn"), 44: ("Tuba", "Tuba"),
    45: ("Flute", "Flute"), 46: ("Clarinet", "Clarinet"), 47: ("Oboe", "Oboe"),
    48: ("English Horn", "English Horn"),
    51: ("Funny Cat", "Funny Cat"), 52: ("Wah Brass", "Wah Brass"), 53: ("Phase Combi", "Phase Combination"),
    54: ("Reed 1", "Reed 1"), 55: ("Popcorn", "Popcorn"), 56: ("Reed 2", "Reed 2"), 57: ("Reed 3", "Reed 3"),
    58: ("PWM Chorus", "PWM Chorus"),
    61: ("Synth Organ", "Synthesizer Organ"), 62: ("Effect 1", "Effect Sound 1"),
    63: ("Effect 2", "Effect Sound 2"), 64: ("Space Harp", "Space Harp"), 65: ("Funk", "Funk"),
    66: ("Space 1", "Space Sound 1"), 67: ("Mysterious", "Mysterious Invention"),
    68: ("Space 2", "Space Sound 2"),
    71: ("Percussive 1", "Percussive Sound 1"), 72: ("Percussive 2", "Percussive Sound 2"),
    73: ("Whistle", "Whistle"), 74: ("Effect 3", "Effect Sound 3"), 75: ("UFO", "UFO"),
    76: ("Space 3", "Space Sound 3"), 77: ("Surf", "Surf"), 78: ("Synth Drum", "Synthesizer Drum"),
}
MARKS = ("roland", "juno")  # R-L5: never in a patch name

# chart column -> slider field; the 0-10 sliders map as byte = round(v * 12.7)
SLIDER_COLUMNS = [("rate", "lfo_rate"), ("delay", "lfo_delay"), ("dco_lfo", "dco_lfo"), ("pwm", "dco_pwm"),
                  ("noise", "noise"), ("freq", "vcf_freq"), ("res", "vcf_res"), ("env", "vcf_env"),
                  ("vcf_lfo", "vcf_lfo"), ("kybd", "vcf_kybd"), ("a", "env_a"), ("d", "env_d"),
                  ("s", "env_s"), ("r", "env_r"), ("sub_lvl", "sub_level")]
TENTH = Fraction(127, 10)  # 12.7 exactly


def rnd(x):
    """round(x) = floor(x + 0.5) (SPEC R-P5, v3.2), on exact fractions."""
    return math.floor(x + Fraction(1, 2))


def slider_byte(v):
    return rnd(Fraction(v) * TENTH)


def vca_byte(v):
    return rnd((Fraction(v) + 5) * TENTH)


def shown(byte, offset=0):
    """The panel's one-decimal v for a byte (R-P5), as an exact fraction."""
    return Fraction(rnd((Fraction(byte) / TENTH - offset) * 10), 10)


def init_patch():
    s = [0] * 16
    s[5] = 127   # VCF FREQ
    s[10] = 127  # VCA LEVEL
    s[13] = 127  # SUSTAIN
    w = {"range": 1, "pulse": 0, "saw": 1, "sub": 1, "chorus": 0, "pwm_src": 0, "vcf_pol": 0,
         "vca_mode": 0, "hpf": 0}
    return {"name": "INIT", "sliders": s, "switches": w}


def slug(name):
    out = "".join(c.lower() if c.isalnum() else "-" for c in name)
    while "--" in out:
        out = out.replace("--", "-")
    return out.strip("-")


def factory_patches():
    """The chart rows mapped to patches (SPEC §15.3 mapping)."""
    rows = list(csv.DictReader(CHART.read_text(encoding="utf-8").splitlines()))
    assert len(rows) == 56, "the chart has 56 patches"
    out = []
    for r in rows:
        num = int(r["id"])
        bank, patch = divmod(num, 10)
        assert 1 <= bank <= 7 and 1 <= patch <= 8, num
        slot = (bank - 1) * 8 + patch
        sliders = {field: slider_byte(r[col]) for col, field in SLIDER_COLUMNS}
        sliders["vca_level"] = vca_byte(r["level"])
        for col, field in SLIDER_COLUMNS:  # every chart value round-trips through byte -> display -> byte
            assert shown(sliders[field]) == Fraction(r[col]) and slider_byte(shown(sliders[field])) == sliders[field], (num, col)
        assert shown(sliders["vca_level"], 5) == Fraction(r["level"]), (num, "level")
        assert vca_byte(shown(sliders["vca_level"], 5)) == sliders["vca_level"], (num, "level")
        switches = {
            "range": {"D": 0, "N": 1, "U": 2}[r["oct"]],  # W0 carries the chart's octave transpose
            "pulse": int(r["pulse"]),
            "saw": int(r["saw"]),
            "sub": int(r["sub"]),
            "chorus": {"OFF": 0, "I": 1, "II": 2, "II?": 2}[r["chorus"]],
            "pwm_src": {"L": 0, "M": 1, "E": 2}[r["pwm_mode"]],
            "vcf_pol": {"N": 0, "I": 1}[r["pol"]],
            "vca_mode": {"E": 0, "G": 1}[r["vca"]],
            "hpf": int(r["hpf"]),
        }
        assert all(0 <= v <= 127 for v in sliders.values()), num
        name, manual = NAMES[num]
        assert len(name) <= 12 and name == name.strip() and all(0x20 <= ord(c) <= 0x7E for c in name), name
        assert not any(m in name.lower() for m in MARKS), name
        out.append({
            "slot": slot,
            "name": name,
            "manual_number": num,
            "manual_name": manual,
            "source": SOURCE,
            "chart": {k: r[k] for k in r},
            "chart_performance": {
                "octave_transpose": r["oct"],
                "lfo_trig_mode": {"A": "AUTO", "M": "MANUAL"}[r["trig"]],
            },
            "sliders": {k: sliders[k] for k in SLIDERS},
            "switches": {k: switches[k] for k in SWITCHES},
        })
    slots = sorted(p["slot"] for p in out)
    assert slots == list(range(1, 57)), "slots 1-56, one patch each"
    return sorted(out, key=lambda p: p["slot"])


def source_json(p):
    return json.dumps(p, indent=2, ensure_ascii=True) + "\n"


def load_originals():
    out = []
    for f in sorted(SRC_DIR.glob("*.json")):
        d = json.loads(f.read_text(encoding="utf-8"))
        if "manual_number" in d:
            continue  # generated factory source
        out.append((f, d))
    assert [d["slot"] for _, d in out] == list(range(FIRST_ORIGINAL_SLOT, FIRST_ORIGINAL_SLOT + 8)), \
        "the eight originals are slots 58-65"
    return out


def to_bank_entry(d):
    sliders = [int(d["sliders"][k]) for k in SLIDERS]
    switches = {k: int(d["switches"][k]) for k in SWITCHES}
    assert all(0 <= v <= 127 for v in sliders + list(switches.values())), d["name"]
    return {"name": d["name"][:12].rstrip(" "), "sliders": sliders, "switches": switches}


def build_bank(factory, originals):
    bank = [init_patch() for _ in range(128)]
    for d in factory + [d for _, d in originals]:
        slot = int(d["slot"])
        assert 1 <= slot <= 128 and slot != 57, slot
        bank[slot - 1] = to_bank_entry(d)
    return bank


def canonical_bank(bank):
    return json.dumps({"vjn6": 1, "patches": bank}, indent=2, ensure_ascii=True) + "\n"


def inc_file(bank):
    lines = ["// Generated by tools/make_default_bank.py from banks/j60-factory-chart.csv and banks/default/*.json.",
             "// Do not edit.",
             "struct DefaultSlotData { const char* name; unsigned char v[25]; };",
             "const DefaultSlotData kDefaultBankData[128] = {"]
    for p in bank:
        vals = p["sliders"] + [p["switches"][k] for k in SWITCHES]
        lines.append('    {"%s", {%s}},' % (p["name"], ", ".join(str(v) for v in vals)))
    lines.append("};")
    return "\n".join(lines) + "\n"


def factory_doc(factory, originals):
    trig_manual = [p["manual_number"] for p in factory if p["chart_performance"]["lfo_trig_mode"] == "MANUAL"]
    vibrato = [p["manual_number"] for p in factory
               if p["chart_performance"]["lfo_trig_mode"] == "MANUAL" and p["sliders"]["dco_lfo"] > 0]
    rows = []
    for p in factory:
        c = p["chart_performance"]
        oct_text = {"D": "D (DOWN)", "N": "N (NORMAL)", "U": "U (UP)"}[c["octave_transpose"]]
        rows.append("| %d | %d | `%s` | %s | %s | %s |" % (p["slot"], p["manual_number"], p["name"], p["manual_name"],
                                                         oct_text, c["lfo_trig_mode"]))
    orig = ", ".join("%d `%s`" % (d["slot"], d["name"]) for _, d in originals)
    return f"""# Default bank: the Juno-60 factory patches

<!-- Generated by tools/make_default_bank.py. Do not edit; change the tool or banks/j60-factory-chart.csv. -->

Slots 1–56 of the VJN6 default bank (SPEC §15.3) are the 56 sample sounds of the **Roland Juno-60 Owner's
Manual**: the patch settings printed on pp. 25–28 and the patch names listed on p. 24. The settings and
names are © Roland Corporation; VJN6 ships them as data (SPEC R-L2), with this attribution. The MIT
License of VJN6's own code does not cover them. VJN6 is not affiliated with Roland. No Juno-106 factory
data ("The Original 128") is included.

- **Source of the numbers:** `banks/j60-factory-chart.csv` holds the chart values as printed (columns left to
  right as on the charts). It was transcribed twice, independently, from 300-dpi renders of pp. 25–28; the
  two transcriptions agree in all 1,512 cells.
- **Generated files:** `tools/make_default_bank.py` maps the chart to patch bytes (SPEC §15.3) and writes one
  readable source file per patch, `banks/default/001-…json` to `056-…json` (with the chart row as printed,
  `manual_name` and `chart_performance`), the bank `banks/vjn6-default.vjn6bank` and the core's copy
  `core/state/default_bank.inc`. `--check` verifies that all of them, and this file, are up to date.
- **Layout:** slot = (bank − 1) × 8 + patch, so the 60-mode LED `11`…`78` shows the manual's number. Slot 57
  is `INIT`; slots 58–65 are the eight original VJN6 patches ({orig}); slots 66–128 are `INIT`.
- **Names:** VJN6 names are at most 12 characters (SPEC §4.3) and contain no Roland or Juno marks (R-L5); the
  manual's full names are in the table and in each source file's `manual_name`. The full names follow the
  manual's list as transcribed by synthmania.com ("juno 60" page), which matches every VJN6 name; two evident
  slips in that transcription are corrected here: 58 "PMW Chorus" → "PWM Chorus", and 76, listed a second
  time as "Space Sound 2", → "Space Sound 3". They have not been checked against a scan of p. 24.

## How the chart becomes a patch

round(x) = floor(x + 0.5). Every chart value round-trips through byte → display → byte exactly.

| Chart | Patch field |
| --- | --- |
| RATE, DELAY TIME, DCO LFO, PWM, NOISE, FREQ, RES, ENV, VCF LFO, KYBD, A, D, S, R, SUB OSC (0–10) | B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B11, B12, B13, B14, B15 = round(v × 12.7) |
| VCA LEVEL (−5…+5) | B10 = round((v + 5) × 12.7) |
| pulse, saw, sub buttons (`*`) | W1, W2, W3 = 1 |
| CHORUS OFF / I / II | W4 = 0 / 1 / 2 |
| PWM MODE LFO / MAN / ENV | W5 = 0 / 1 / 2 |
| VCF polarity normal / inverted | W6 = 0 / 1 |
| VCA ENV / GATE | W7 = 0 / 1 |
| HPF 0–3 | W8 = 0–3 |
| OCTAVE TRANSPOSE D / N / U | W0 = 0 (16′) / 1 (8′) / 2 (4′) |

- **W0 carries the chart's octave transpose.** 60 mode ignores W0 (the Juno-60 has no RANGE control), so the
  sound in 60 mode is unaffected; in 106 mode each patch plays in the octave the chart intends.
- **Knob positions "irrelevant to the sound"** (the manual's words, for example SUB OSC level with the sub
  button off) are kept exactly as charted. In 106 mode those patches play the sub at that level, because
  106 mode always has the sub on (it reads W3 as 1).

## Settings the charts give that a patch does not store

The charts also give OCTAVE TRANSPOSE and LFO TRIG MODE for each patch. In VJN6 (as in the spec's model of
the hardware) these are performance settings (SPEC §4.2), so loading a patch does not set them. Set the
switches by hand to hear a patch as charted. The charts give MANUAL trigger for
{", ".join(str(n) for n in trig_manual)}; of these, {", ".join(str(n) for n in vibrato)} use DCO vibrato,
so with the switch on AUTO they get constant vibrato after the delay instead of vibrato only while LFO TRIG
is held. Whether the Juno-60 stores LFO TRIG MODE (and octave transpose) in a patch is an open question
(SPEC §15.4).

| Slot | No. | VJN6 name | Manual name | Octave transpose | LFO trig mode |
| --- | --- | --- | --- | --- | --- |
""" + "\n".join(rows) + """

## Notes

- **Patch 36 (Pizzicato Sound 2):** its chorus II is inferred. The scan's right edge on p. 26 is cut off, and
  the chart's OFF and I columns are both blank for this patch (`II?` in the CSV). Open question in
  SPEC §15.4.
- **Bank 7 (71–78):** these patches use no DCO waveform. 71–76 and 78 sound by VCF self-oscillation at RES 10
  and are pitched by FREQ; the manual says to tune them with FREQ, because the pitch depends on each unit's
  filter calibration. 77 (Surf) is a noise patch at RES 0. 71, 73, 75 and 76 also add noise. SPEC §15.4
  lists comparing their pitch on a Juno-60.
"""


def main():
    factory = factory_patches()
    originals = load_originals()
    bank = build_bank(factory, originals)
    assert bank[56] == init_patch(), "slot 57 is INIT"
    outputs = {}
    for p in factory:
        outputs[SRC_DIR / ("%03d-%s.json" % (p["slot"], slug(p["name"])))] = source_json(p)
    outputs[ROOT / "banks" / "vjn6-default.vjn6bank"] = canonical_bank(bank)
    outputs[ROOT / "core" / "state" / "default_bank.inc"] = inc_file(bank)
    outputs[ROOT / "docs" / "FACTORY_BANK.md"] = factory_doc(factory, originals)
    stale = [f for f in SRC_DIR.glob("*.json")
             if f not in outputs and "manual_number" in json.loads(f.read_text(encoding="utf-8"))]
    if "--check" in sys.argv:
        bad = [str(p.relative_to(ROOT)) for p, txt in outputs.items()
               if not p.exists() or p.read_text(encoding="utf-8") != txt]
        bad += [str(f.relative_to(ROOT)) + " (stale)" for f in stale]
        if bad:
            print("out of date:", *bad)
            return 1
        print("default bank up to date")
        return 0
    for f in stale:
        f.unlink()
        print("removed", f.relative_to(ROOT))
    for p, txt in outputs.items():
        p.write_text(txt, encoding="utf-8", newline="\n")
        print("wrote", p.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
