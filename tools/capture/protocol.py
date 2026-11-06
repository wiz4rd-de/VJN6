"""The SPEC §15.4 measurement protocol as data: rows, steps, patches, notes, timing and model values.

Shared by make_protocol.py (writes the Standard MIDI File and the results templates) and analyze.py
(cuts recordings into steps and measures them), so both always agree on the schedule.

Protocol (§15.4): in 106 mode, send the F0 41 31 patch named, play the notes named, record the line
output at 96 kHz / 24-bit, run analyze.py; in 60 mode the same with the panel set by hand. Each §15.4
row that can be measured from a recording is one track; the tracks follow each other in time, so
playing the whole file runs the whole protocol. Every track starts with a short sync click that
analyze.py uses to align a recording with the schedule.
"""
import math

MODES = ("106", "60")
TICKS_PER_QUARTER = 960
TEMPO_US = 500000                      # 120 bpm: 1920 ticks per second
TICKS_PER_SECOND = TICKS_PER_QUARTER * 1000000 // TEMPO_US
CHANNEL = 0                            # MIDI channel 1; SysEx n = 0
VELOCITY = 64                          # the hardware sends and ignores velocity (fixed 64)
NOTE_AT = 0.3                          # first note-on of a step, seconds after its patch message
SYNC_PATCH_AT = 0.05
SYNC_NOTE_AT = 0.3                     # sync click: note 96 for 50 ms, seconds after the track start
SYNC_NOTE, SYNC_LEN = 96, 0.05
STEPS_AT = 1.0                         # first step of a track, seconds after the track start
TRACK_GAP = 1.0                        # silence after the last step of a track

SLIDERS = ["lfo_rate", "lfo_delay", "dco_lfo", "dco_pwm", "noise", "vcf_freq", "vcf_res", "vcf_env",
           "vcf_lfo", "vcf_kybd", "vca_level", "env_a", "env_d", "env_s", "env_r", "sub_level"]
SWITCHES = ["range", "pulse", "saw", "sub", "chorus", "pwm_src", "vcf_pol", "vca_mode", "hpf"]
PANEL_LABELS = {
    "lfo_rate": "LFO RATE", "lfo_delay": "LFO DELAY", "dco_lfo": "DCO LFO", "dco_pwm": "DCO PWM",
    "noise": "NOISE", "vcf_freq": "VCF FREQ", "vcf_res": "VCF RES", "vcf_env": "VCF ENV", "vcf_lfo": "VCF LFO",
    "vcf_kybd": "VCF KYBD", "vca_level": "VCA LEVEL", "env_a": "A", "env_d": "D", "env_s": "S", "env_r": "R",
    "sub_level": "SUB"}

# Mode-dependent symbolic values: the flat HPF position and the VCF calibration byte (R-E4, R-E5).
HPF_FLAT = {"106": 1, "60": 0}
FREQ_CAL = {"106": 49, "60": 38}
ENV_TIMES = {  # (T_min, T_max) in seconds, R-E7
    "106": {"a": (0.0015, 3.0), "d": (0.0015, 12.0), "r": (0.0015, 12.0)},
    "60": {"a": (0.001, 3.0), "d": (0.002, 12.0), "r": (0.002, 12.0)},
}
LFO106_TABLE = [0.04, 1.07, 1.99, 2.91, 3.84, 4.76, 6.26, 7.73, 10.95, 18.67, 29.76]

# The quiet base patch: saw only at 8', open filter, VCA ENV with full sustain, HPF flat, chorus off.
BASE = dict(lfo_rate=0, lfo_delay=0, dco_lfo=0, dco_pwm=0, noise=0, vcf_freq=127, vcf_res=0, vcf_env=0,
            vcf_lfo=0, vcf_kybd=0, vca_level=127, env_a=0, env_d=0, env_s=127, env_r=0, sub_level=0,
            range=1, pulse=0, saw=1, sub=1, chorus=0, pwm_src=1, vcf_pol=0, vca_mode=0, hpf="flat")
SYNC = dict(BASE, vca_mode=1)
SELF_OSC = dict(BASE, saw=0, vcf_freq="cal", vcf_res=127, vcf_kybd=127)


def steps10(n=10):
    """Slider bytes for 'N steps' over 0..127: round(127 * i / (n - 1))."""
    return [int(math.floor(127.0 * i / (n - 1) + 0.5)) for i in range(n)]


def slider_text(b):
    """Slider byte as the panel shows it: b / 12.7 with one decimal, rounded half up (R-P5)."""
    tenths = (200 * b + 127) // 254
    return "%d.%d" % (tenths // 10, tenths % 10)


def note_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def lfo_hz(mode, b0):
    if mode == "60":
        return 0.3 * 66.7 ** (b0 / 127.0)
    x = b0 / 12.7
    i = min(int(x), 9)
    return LFO106_TABLE[i] + (LFO106_TABLE[i + 1] - LFO106_TABLE[i]) * (x - i)


def env_time(mode, stage, byte):
    lo, hi = ENV_TIMES[mode][stage]
    x = byte / 127.0
    return lo + (hi - lo) * x * x


def resolve(patch, mode):
    """Patch dict with symbolic values resolved for the mode."""
    p = dict(patch)
    if p.get("hpf") == "flat":
        p["hpf"] = HPF_FLAT[mode]
    if p.get("vcf_freq") == "cal":
        p["vcf_freq"] = FREQ_CAL[mode]
    for k in SLIDERS + SWITCHES:
        if not isinstance(p[k], int) or not 0 <= p[k] <= 127:
            raise ValueError("bad patch value %s=%r" % (k, p[k]))
    return p


def sysex_31(p):
    """Canonical F0 41 31 manual message (n = 0, pp = 0) for a resolved patch (SPEC §4.5)."""
    d = [p[k] for k in SLIDERS]
    if p["sub"] == 0:
        d[15] = 0
    sw1 = 1 << (p["range"] if p["range"] < 3 else 0)
    sw1 |= (8 if p["pulse"] else 0) | (16 if p["saw"] else 0)
    ch = p["chorus"] if p["chorus"] < 4 else 0
    sw1 |= 32 if ch == 0 else (64 if ch in (1, 3) else 0)
    sw2 = (1 if p["pwm_src"] in (1, 2) else 0) | (2 if p["vcf_pol"] else 0) | (4 if p["vca_mode"] else 0)
    sw2 |= (3 - (p["hpf"] if p["hpf"] < 4 else 0)) << 3
    return bytes([0xF0, 0x41, 0x31, CHANNEL, 0x00] + d + [sw1, sw2, 0xF7])


def panel_text(p, mode):
    """The panel settings of a resolved patch as a person sets them by hand (slider 0.0-10.0)."""
    parts = ["%s %s" % (PANEL_LABELS[k], slider_text(p[k])) for k in SLIDERS]
    parts.append("RANGE %s" % ["16'", "8'", "4'"][p["range"] if p["range"] < 3 else 1])
    parts.append("PULSE %s" % ("on" if p["pulse"] else "off"))
    parts.append("SAW %s" % ("on" if p["saw"] else "off"))
    if mode == "60":
        parts.append("SUB %s" % ("on" if p["sub"] else "off"))
    parts.append("CHORUS %s" % ["off", "I", "II", "I+II"][p["chorus"] if p["chorus"] < 4 else 0])
    parts.append("PWM %s" % ["LFO", "MANUAL", "ENV"][p["pwm_src"] if p["pwm_src"] < 3 else 0])
    parts.append("VCF ENV %s" % ("inverted" if p["vcf_pol"] else "normal"))
    parts.append("VCA %s" % ("GATE" if p["vca_mode"] else "ENV"))
    parts.append("HPF %d" % p["hpf"])
    return ", ".join(parts)


class Step:
    """One recorded condition. Times are seconds from the step start (when its patch is sent).

    events: (time, kind, *data) with kind note_on/note_off (note), cc (number, value), bend (0..16383).
    measures: (analysis kind, params) pairs analyze.py runs; windows in params are seconds relative to
    the step's first note-on (or to the step start for steps without notes).
    expect: {measurement name: model value} for the results template (None = no single number).
    """

    def __init__(self, sid, title, patch, duration, events=(), measures=(), expect=None, perf=None, note=""):
        self.id, self.title, self.patch, self.duration = sid, title, patch, duration
        self.events = sorted(events, key=lambda e: e[0])
        self.measures = list(measures)
        self.expect = expect or {}
        self.perf = perf or ""      # performance settings a person sets by hand (VJN6 gets them as CCs)
        self.note = note

    @property
    def first_note(self):
        ons = [e[0] for e in self.events if e[1] == "note_on"]
        return min(ons) if ons else None


class Row:
    def __init__(self, key, item, constants, measurement, modes, steps_fn, reuses=None, how=""):
        self.key, self.item, self.constants, self.measurement = key, item, constants, measurement
        self.modes, self.steps_fn, self.reuses, self.how = modes, steps_fn, reuses, how

    def steps(self, mode):
        return [] if self.reuses else self.steps_fn(mode)


def hold(note, dur, at=NOTE_AT):
    return [(at, "note_on", note), (at + dur, "note_off", note)]


# ---------------------------------------------------------------------------------------------
# rows (SPEC §15.4, in table order)


def _resonance(mode):
    k_max = 4.5 if mode == "106" else 4.35
    steps = []
    for r in steps10(8):
        k = k_max * (r / 127.0) ** 1.5
        steps.append(Step("res-%03d-silent" % r, "RES %d, no input: self-oscillation onset (k = %.2f)" % (r, k),
                          dict(SELF_OSC, vcf_res=r), 5.2, hold(60, 4.6),
                          [("level", {"window": (3.2, 4.5)}), ("freq", {"window": (3.2, 4.5)})],
                          {"k_model": round(k, 3)}))
        steps.append(Step("res-%03d-noise" % r, "RES %d, white noise input: resonance peak (k = %.2f)" % (r, k),
                          dict(SELF_OSC, vcf_res=r, noise=127), 5.2, hold(60, 4.6),
                          [("spectrum", {"window": (0.5, 4.5), "pass": (20.0, 60.0), "peak_range": (60.0, 4000.0),
                                         "seg_s": 0.68})],
                          {"k_model": round(k, 3)}))
    return steps


def _ccal(mode):
    steps = []
    for n, f in ((60, 248.0), (84, 992.0)):
        steps.append(Step("ccal-selfosc-n%d" % n, "Self-oscillation at the calibration FREQ, note %d" % n,
                          SELF_OSC, 5.2, hold(n, 4.6), [("freq", {"window": (3.2, 4.5), "ref_hz": f}),
                                                       ("level", {"window": (3.2, 4.5)})],
                          {"freq_hz": f}))
        steps.append(Step("ccal-res0-n%d" % n, "RES 0, white noise, same FREQ, note %d: -12 dB point" % n,
                          dict(SELF_OSC, vcf_res=0, noise=127), 5.2, hold(n, 4.6),
                          [("spectrum", {"window": (0.5, 4.5), "pass": (20.0, 60.0) if n == 60 else (40.0, 160.0),
                                         "peak_range": (20.0, 4000.0)})],
                          {"f_m12_hz": round(f * (1.126 if mode == "106" else 1.092), 1)},
                          note="c_cal = -12 dB frequency at RES 0 / self-oscillation frequency (expected 1.126 in "
                               "106, 1.092 in 60 mode, R-E5)"))
    return steps


def _cutoff60(mode):
    steps = []
    for b in steps10(10):
        fc = 28.1 * 2.0 ** (10.5 * b / 127.0)
        dur = 6.0 if fc >= 40 else 12.0
        steps.append(Step("cut60-f%03d" % b, "Self-oscillation, KYBD 0, FREQ %d (%s)" % (b, slider_text(b)),
                          dict(SELF_OSC, vcf_freq=b, vcf_kybd=0, noise=20), dur, hold(60, dur - 0.6),
                          [("freq", {"window": (dur - 4.4, dur - 0.7)}), ("level", {"window": (dur - 4.4, dur - 0.7)})],
                          {"freq_hz": round(fc, 2)}, note="NOISE 1.6 only starts the oscillation quickly"))
    return steps


def _envelope(mode):
    steps = []
    for b in steps10(10):
        t = env_time(mode, "a", b)
        dur = 0.3 + max(1.5 * t, t + 0.6) + 0.5
        steps.append(Step("env-a%03d" % b, "Attack %d (T = %.0f ms)" % (b, t * 1e3), dict(BASE, env_a=b),
                          dur + 0.3, hold(96, dur - 0.3), [("env_attack", {"window": (-0.05, dur - 0.4)})],
                          {"t_model_ms": round(t * 1e3, 1)}))
    for b in steps10(10):
        t = env_time(mode, "d", b)
        dur = 0.3 + max(1.5 * t, t + 0.6) + 0.5
        steps.append(Step("env-d%03d" % b, "Decay %d to sustain 0 (T = %.0f ms)" % (b, t * 1e3),
                          dict(BASE, env_s=0, env_d=b), dur + 0.3, hold(96, dur - 0.3),
                          [("env_decay", {"window": (-0.05, dur - 0.4)})], {"t_model_ms": round(t * 1e3, 1)}))
    for b in steps10(10):
        t = env_time(mode, "r", b)
        tail = max(1.5 * t, t + 0.6) + 0.3
        steps.append(Step("env-r%03d" % b, "Release %d after 1 s held (T = %.0f ms)" % (b, t * 1e3),
                          dict(BASE, env_r=b), NOTE_AT + 1.0 + tail + 0.3, hold(96, 1.0),
                          [("env_release", {"off": 1.0, "window": (0.2, 1.0 + tail)})],
                          {"t_model_ms": round(t * 1e3, 1)}))
    return steps


def _lfo106(mode):
    steps = []
    for b in steps10(10):
        f = lfo_hz(mode, b)
        dur = max(5.0, 2.5 / f + 2.0)
        steps.append(Step("lfo-rate%03d" % b, "LFO RATE %d (%s), DCO LFO 64" % (b, slider_text(b)),
                          dict(BASE, lfo_rate=b, dco_lfo=64), dur, hold(84, dur - 0.5),
                          [("lfo_rate", {"window": (0.5, dur - 0.6)})], {"lfo_hz": round(f, 3)}))
    delay = (3.0 if mode == "106" else 2.0)
    steps.append(Step("lfo-delay127", "LFO DELAY 127, RATE 64, DCO LFO 127: delay and fade-in",
                      dict(BASE, lfo_rate=64, lfo_delay=127, dco_lfo=127), delay + 4.5, hold(84, delay + 3.9),
                      [("lfo_delay", {"window": (0.0, delay + 3.8)})],
                      {"t10_s": round(delay + 0.02, 3), "t90_s": round(delay + 0.18, 3)},
                      note="model: delay %.1f s, then a 0.2 s linear fade-in" % delay))
    return steps


def _lfo_depth(mode):
    full = 400.0 if mode == "106" else 300.0
    steps = []
    for b in steps10(10):
        steps.append(Step("lfo-depth%03d" % b, "DCO LFO %d (%s) at RATE 64" % (b, slider_text(b)),
                          dict(BASE, lfo_rate=64, dco_lfo=b), 4.0, hold(84, 3.5),
                          [("lfo_depth", {"window": (0.5, 3.4)})], {"depth_cents": round(full * b / 127.0, 1)}))
    return steps


def _sub_noise(mode):
    w = (0.5, 3.4)
    return [
        Step("subnoise-saw", "Reference: saw only, note 60", BASE, 4.0, hold(60, 3.5),
             [("level", {"window": w}), ("freq", {"window": w})], {"freq_hz": round(note_hz(60), 2)}),
        Step("subnoise-sub", "Sub only at 127, note 60", dict(BASE, saw=0, sub_level=127), 4.0, hold(60, 3.5),
             [("level", {"window": w}), ("freq", {"window": w})], {"freq_hz": round(note_hz(60) / 2, 2)},
             note="model: sub peak equals the saw peak"),
        Step("subnoise-noise", "Noise only at 127", dict(BASE, saw=0, noise=127), 4.0, hold(60, 3.5),
             [("level", {"window": w}),
              ("spectrum", {"window": w, "pass": (200.0, 2000.0), "peak_range": (20.0, 20000.0)})],
             note="model: white (no band limit); f_m3_hz is the -3 dB point re 200 Hz-2 kHz"),
    ]


def _output_level(mode):
    chord = [48, 55, 60, 64, 67, 72]
    ev = []
    for n in chord:
        ev += hold(n, 5.0)
    return [
        Step("out-saw", "Saw only, VCA 10, note 60", BASE, 4.0, hold(60, 3.5), [("level", {"window": (0.5, 3.4)})],
             {"peak_dbfs": -24.0}, note="model: -24 dBFS peak at VJN6's engine output (hardware: line level)"),
        Step("out-chord", "Saw + pulse + sub 127, chorus I, six-note chord",
             dict(BASE, pulse=1, sub_level=127, chorus=1),
             5.8, ev, [("level", {"window": (0.5, 4.9)})]),
    ]


def _chorus_level(mode):
    w = (3.5, 8.9)
    return [
        Step("chlvl-off", "Sine (self-oscillation, note 60), chorus off", SELF_OSC, 9.6, hold(60, 9.0),
             [("beat", {"window": w, "channel": "left"}), ("level", {"window": w, "channel": "left"})]),
        Step("chlvl-1", "Sine (self-oscillation, note 60), chorus I", dict(SELF_OSC, chorus=1), 9.6, hold(60, 9.0),
             [("beat", {"window": w, "channel": "left"}), ("level", {"window": w, "channel": "left"})],
             {"ratio": 1.46}, note="wet/dry at the output: (amp_max - dry amp) / dry amp of chlvl-off"),
        Step("chlvl-2", "Sine (self-oscillation, note 60), chorus II", dict(SELF_OSC, chorus=2), 9.6, hold(60, 9.0),
             [("beat", {"window": w, "channel": "left"}), ("level", {"window": w, "channel": "left"})],
             {"ratio": 1.46}),
    ]


def _chorus_rates(mode):
    clicks = dict(BASE, range=0, hpf=3, vca_mode=1)
    return [
        Step("chrate-1", "Click track (16' saw resets through HPF 3, note 24), chorus I", dict(clicks, chorus=1),
             12.6, hold(24, 12.0), [("chorus_clicks", {"window": (0.5, 11.9), "channel": "left"})],
             {"rate_hz": 0.513, "centre_ms": 3.3, "swing_ms": 1.9}),
        Step("chrate-2", "Click track (16' saw resets through HPF 3, note 24), chorus II", dict(clicks, chorus=2),
             12.6, hold(24, 12.0), [("chorus_clicks", {"window": (0.5, 11.9), "channel": "left"})],
             {"rate_hz": 0.863, "centre_ms": 3.3, "swing_ms": 1.9}),
    ]


def _chorus_noise(mode):
    silent = dict(BASE, saw=0)
    return [
        Step("chnoise-off", "Silent input, chorus off, 10 s", silent, 10.5, (),
             [("noise_bands", {"window": (0.4, 10.3)})]),
        Step("chnoise-1", "Silent input, chorus I, 30 s", dict(silent, chorus=1), 30.5, (),
             [("noise_bands", {"window": (0.4, 30.3)})], {"rms_dbfs": -57.0},
             note="model: -57 dBFS at VJN6's engine output, +12 dB shelf at 2 kHz (106 measured: +28 dB tilt)"),
    ]


def _update_interval(mode):
    ev = [(0.0, "cc", 87, 127)] + hold(96, 6.0)
    t0, t1 = NOTE_AT + 1.0, NOTE_AT + 5.0
    n = int(round((t1 - t0) * TICKS_PER_SECOND / 4))  # one pitch-bend message every 4 ticks (2.08 ms)
    for i in range(n + 1):
        ev.append((t0 + 4.0 * i / TICKS_PER_SECOND, "bend", 8192 + int(round(8191.0 * i / n))))
    ev.append((NOTE_AT + 6.05, "bend", 8192))
    return [Step("upd-bend", "Slow pitch bend 0 -> +1 over 4 s, note 96 (bender DCO sensitivity 10)",
                 BASE, 6.8, ev, [("pitch_steps", {"window": (0.9, 5.1)})], {"update_ms": 4.2},
                 perf="bender DCO sensitivity 10 (VJN6: CC87 127)")]


def _pulse_edge(mode):
    w = (0.3, 2.4)
    pulse = dict(BASE, saw=0, pulse=1, pwm_src=1, dco_pwm=0)
    return [
        Step("edge-saw", "Saw only, note 36", dict(BASE, vca_mode=1), 3.0, hold(36, 2.5), [("edges", {"window": w})]),
        Step("edge-pulse", "Pulse only, PWM MANUAL 0 (50 %), note 36", dict(pulse, vca_mode=1), 3.0, hold(36, 2.5),
             [("edges", {"window": w})]),
        Step("edge-both", "Saw + pulse at 50 %, note 36", dict(pulse, saw=1, vca_mode=1), 3.0, hold(36, 2.5),
             [("edges", {"window": w})], note="model: pulse edge high-going at the saw reset"),
    ]


def _poly_porta(mode):
    steps = []
    for name, cc, label in (("poly2", 64, "POLY 2"), ("poly1", 0, "POLY 1")):
        ev = [(0.0, "cc", 86, cc)]
        for k, n in enumerate((50, 53, 57)):
            ev += hold(n, 0.3, NOTE_AT + 0.6 * k)
        ev += hold(61, 1.5, NOTE_AT + 2.5) + hold(66, 0.8, NOTE_AT + 3.5)
        steps.append(Step("steal-%s" % name, "%s: notes 50, 53, 57 released, then 61 and 66" % label,
                          dict(BASE, env_r=70), 9.0, ev,
                          [("presence", {"notes": [50, 53, 57], "events": [2.5, 3.5]})],
                          {"expect": "50 and 53 cut at 61 and 66" if name == "poly2" else "no tail cut"},
                          perf="key mode %s (VJN6: CC86 %d)" % (label, cc)))
    for t in (16, 32, 64, 96, 127):
        T = 3.0 * (t / 127.0) ** 2
        first = T + 1.0
        ev = [(0.0, "cc", 86, 127), (0.0, "cc", 5, t), (0.0, "cc", 65, 0)]
        ev += [(NOTE_AT, "note_on", 48), (NOTE_AT + first - 0.05, "cc", 65, 127),
               (NOTE_AT + first, "note_on", 72), (NOTE_AT + first + 0.1, "note_off", 48),
               (NOTE_AT + first + T + 1.5, "note_off", 72), (NOTE_AT + first + T + 1.6, "cc", 65, 0),
               (NOTE_AT + first + T + 1.6, "cc", 86, 0)]
        steps.append(Step("porta-t%03d" % t, "Portamento time %d (%s): glide 48 -> 72 in UNISON" % (t, slider_text(t)),
                          BASE, first + T + 2.2, ev,
                          [("glide", {"at": first, "window": (first - 0.6, first + T + 1.4)})],
                          {"t90_ms": round(0.9 * T * 1e3, 1)},
                          perf="key mode UNISON, portamento on, time %s (VJN6: CC86 127, CC65, CC5 %d)"
                          % (slider_text(t), t),
                          note="model: T = 3 s x (t/127)^2, linear in semitones (t50 = T/2)"))
    return steps


def _arpeggio(mode):
    return [Step("arp-rate0", "Arpeggio on, rate 0, UP, range 1, note 60 held, VCA GATE", dict(BASE, vca_mode=1),
                 8.6, hold(60, 8.0), [("gate", {"window": (0.2, 8.0)})], {"duty": 0.5, "period_ms": 666.7},
                 perf="ARPEGGIO on, RATE 0, MODE UP, RANGE 1 (set by hand; also in VJN6)")]


def _hpf60(mode):
    steps = []
    for vca in (25, 127):
        for pos in (1, 2, 3):
            steps.append(Step("hpf-p%d-v%03d" % (pos, vca), "Noise through HPF %d at VCA LEVEL %d" % (pos, vca),
                              dict(BASE, saw=0, noise=127, hpf=pos, vca_level=vca), 5.0, hold(60, 4.5),
                              [("hp_corner", {"window": (0.5, 4.4)})],
                              {"hp_m3_hz": {1: 122.0, 2: 269.0, 3: 571.0}[pos]}))
    return steps


ROWS = [
    Row("resonance", "Resonance law and onset", "k_max 4.5 / 4.35, V_fb 0.59, exponent 1.5",
        "Self-osc patch; RES in 8 steps; noise sweep; fit k from peak height", ("106", "60"), _resonance),
    Row("ccal", "VCF calibration factor", "c_cal (ratio of self-osc frequency to small-signal cutoff)",
        "Self-osc frequency vs -12 dB point at RES 0, same FREQ", ("106", "60"), _ccal),
    Row("qcomp", "Q compensation", "0.23", "Same sweeps: DC gain vs k", ("106", "60"), None, reuses="resonance",
        how="pass_dbfs_hz of the res-*-noise steps (the noise sweep of 'Resonance law and onset') against k_model"),
    Row("cutoff60", "60 cutoff slider law", "28.1 Hz, 10.5 oct", "Self-osc patch, KYBD 0, FREQ in 10 steps",
        ("60",), _cutoff60),
    Row("envelope", "Envelope slider law", "square law, tau = T/5",
        "A/D/R in 10 steps, VCA GATE off; onset-to-level timing", ("106", "60"), _envelope),
    Row("lfo106", "106 LFO rate table and delay shape", "table, 0.2 s ramp", "LFO rate in 10 steps; delay 127",
        ("106",), _lfo106),
    Row("lfodepth", "LFO depth taper", "linear in cents", "DCO LFO in 10 steps at rate 64", ("106", "60"), _lfo_depth),
    Row("subnoise", "Sub level and noise band limit", "1.0, white", "Sub only / noise only at 127", ("106", "60"),
        _sub_noise),
    Row("output", "Output level reference", "-24 dBFS single voice",
        "Line level of a saw at VCA 10 vs a full chord with chorus", ("106", "60"), _output_level),
    Row("chorus_level", "Chorus wet/dry level", "G = 1.12",
        "Sine through chorus I on a 60 and a 106, dry vs wet level", ("106", "60"), _chorus_level),
    Row("chorus_rates", "106 chorus rates and swing", "= 60 values", "Click track through Chorus I / II", ("106",),
        _chorus_rates),
    Row("chorus_noise", "Chorus noise spectrum", "+12 dB shelf at 2 kHz", "Silent input, chorus I, 30 s",
        ("106", "60"), _chorus_noise),
    Row("update", "106 modulation update interval", "4.2 ms", "Slow bend; count pitch steps", ("106",),
        _update_interval),
    Row("pulse_edge", "Pulse edge vs saw reset", "high-going at the reset", "Saw + pulse at 50 % on a scope",
        ("106", "60"), _pulse_edge),
    Row("poly_porta", "POLY 2 stealing, portamento range and curve", "rotary-release-first; 3 s x square",
        "Scripted note patterns; glide timing", ("106",), _poly_porta),
    Row("arpeggio", "Arpeggio gate length", "50 %", "Arpeggio at rate 0, VCA GATE; gate width", ("60",), _arpeggio),
    Row("hpf60", "HPF corner dependence on VCA LEVEL (60)", "not modelled", "HPF sweeps at VCA 25 / 127", ("60",),
        _hpf60),
]

# What each analysis kind reports: (name, unit). analyze.py returns exactly these names.
MEASURES = {
    "level": [("rms_dbfs", "dBFS"), ("peak_dbfs", "dBFS")],
    "freq": [("freq_hz", "Hz")],
    "spectrum": [("pass_dbfs_hz", "dBFS/Hz"), ("peak_hz", "Hz"), ("peak_db_re_pass", "dB"), ("f_m3_hz", "Hz"),
                 ("f_m12_hz", "Hz")],
    "hp_corner": [("hp_m3_hz", "Hz"), ("plateau_dbfs_hz", "dBFS/Hz")],
    "env_attack": [("t10_ms", "ms"), ("t90_ms", "ms"), ("t100_ms", "ms")],
    "env_decay": [("td6_ms", "ms"), ("td20_ms", "ms"), ("td40_ms", "ms")],
    "env_release": [("tr6_ms", "ms"), ("tr20_ms", "ms"), ("tr40_ms", "ms")],
    "lfo_rate": [("lfo_hz", "Hz"), ("depth_cents", "cents")],
    "lfo_depth": [("depth_cents", "cents"), ("lfo_hz", "Hz")],
    "lfo_delay": [("t10_s", "s"), ("t90_s", "s"), ("depth_cents", "cents")],
    "beat": [("amp_max_dbfs", "dBFS"), ("amp_min_dbfs", "dBFS"), ("ratio", "")],
    "chorus_clicks": [("rate_hz", "Hz"), ("centre_ms", "ms"), ("swing_ms", "ms"), ("clicks", "")],
    "noise_bands": [("rms_dbfs", "dBFS"), ("band_200_2k_dbfs_hz", "dBFS/Hz"), ("band_5k_15k_dbfs_hz", "dBFS/Hz"),
                    ("tilt_db", "dB")],
    "pitch_steps": [("update_ms", "ms"), ("steps", ""), ("median_interval_ms", "ms")],
    "edges": [("jumps", ""), ("j1", "x p-p"), ("j1_phase", "period"), ("j2", "x p-p"), ("j2_phase", "period"),
              ("j3", "x p-p")],
    "glide": [("t10_ms", "ms"), ("t50_ms", "ms"), ("t90_ms", "ms"), ("interval_st", "semitones")],
    "gate": [("gate_ms", "ms"), ("period_ms", "ms"), ("duty", "")],
}


def measure_names(kind, params):
    """[(name, unit)] that one analysis of a step reports."""
    if kind == "presence":
        return [("n%d_at%d_db" % (n, k + 1), "dB change") for k in range(len(params["events"]))
                for n in params["notes"]]
    names = list(MEASURES[kind])
    if kind == "freq" and "ref_hz" in params:
        names.append(("cents_re_ref", "cents"))
    return names


def template_rows(mode):
    """The results table of one mode: one dict per step and measurement, 'measured' empty."""
    rows = []
    by_key = {}
    for tr, step, t0 in all_steps(mode):
        by_key.setdefault(tr.row.key, []).append((step, t0))
    for row in ROWS:
        if mode not in row.modes:
            continue
        source = row.reuses or row.key
        for step, t0 in by_key.get(source, []):
            measures = step.measures
            if row.reuses:  # Q compensation: the pass-band level of the noise sweep against k
                measures = [m for m in measures if m[0] == "spectrum"]
            notes = [step.note] if step.note else []
            notes += ["%s = %s" % (k, v) for k, v in step.expect.items()
                      if k not in {n for kind, prm in measures for n, _ in measure_names(kind, prm)}]
            if step.perf:
                notes.append("set: " + step.perf)
            for kind, prm in measures:
                for name, unit in measure_names(kind, prm):
                    model = step.expect.get(name, "")
                    if row.reuses:
                        if name != "pass_dbfs_hz":
                            continue
                        k = step.expect["k_model"]  # small-signal DC gain g_in / (1 + k), g_in = 1 + 0.23 k
                        model = "%.1f dB re RES 0" % (20.0 * math.log10((1.0 + 0.23 * k) / (1.0 + k)))
                    rows.append({"mode": mode, "row": row.key, "item": row.item, "constants": row.constants,
                                 "step": step.id, "start_s": "%.3f" % t0, "description": step.title,
                                 "measurement": name, "unit": unit, "model": model,
                                 "measured": "", "notes": "; ".join(notes)})
    return rows


TEMPLATE_COLUMNS = ["mode", "row", "item", "constants", "step", "start_s", "description", "measurement", "unit",
                    "model", "measured", "notes"]

NOT_RECORDED = ["Panel colours and typeface", "Real devices and browsers", "Signed / notarised installers",
                "Public web hosting", "Trade-dress and name check"]


def reset_events():
    """Performance state at every track start, for VJN6 (a Juno-106 ignores the CCs it does not know)."""
    return [(0.0, "cc", 123, 0), (0.0, "cc", 64, 0), (0.0, "cc", 1, 0), (0.0, "bend", 8192),
            (0.0, "cc", 86, 0), (0.0, "cc", 65, 0), (0.0, "cc", 5, 32), (0.0, "cc", 87, 64)]


class Track:
    def __init__(self, row, mode, start, steps):
        self.row, self.mode, self.start, self.steps = row, mode, start, steps
        self.step_starts = []
        t = start + STEPS_AT
        for s in steps:
            self.step_starts.append(t)
            t += s.duration
        self.end = t + TRACK_GAP

    @property
    def sync_time(self):
        return self.start + SYNC_NOTE_AT


def schedule(mode):
    """The tracks of one mode laid out one after another: [Track], total seconds."""
    if mode not in MODES:
        raise ValueError("mode must be 106 or 60")
    tracks, t = [], 0.5
    for row in ROWS:
        if mode not in row.modes:
            continue
        tr = Track(row, mode, t, row.steps(mode))
        tracks.append(tr)
        t = tr.end
    return tracks, t


def all_steps(mode):
    """[(track, step, absolute start seconds)] in schedule order."""
    tracks, _ = schedule(mode)
    return [(tr, s, t0) for tr in tracks for s, t0 in zip(tr.steps, tr.step_starts)]

