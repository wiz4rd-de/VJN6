#!/usr/bin/env python3
"""Analyses recordings of the SPEC §15.4 measurement protocol (tools/capture/make_protocol.py).

  python3 tools/capture/analyze.py --recording take.wav --mode 106 [-o results.csv]
      one continuous recording of the whole protocol file (or, with --row KEY, of one row's track);
      every track's sync click aligns the recording with the schedule.
  python3 tools/capture/analyze.py --steps-dir DIR --mode 60 [-o results.csv]
      one recording per step, named <step id>.wav (for example res-127-silent.wav); each file is aligned
      on the first note onset it contains.

Input: WAV, PCM 16/24/32-bit or float 32/64-bit, mono or stereo, any sample rate (the protocol asks for
96 kHz / 24-bit). Most measurements use (L + R) / 2; the chorus measurements use the left output.

Measurements (names and units as in the results template): levels (RMS / peak dBFS), frequency from
rising zero crossings with hysteresis, Welch spectra (pass-band level, resonance peak, -3 / -12 dB
points, first-order HPF corner fit, noise band levels), envelope segment timing from a peak-hold
envelope, LFO rate / depth / delay from a per-period pitch track, chorus delay trajectory (triangle fit)
from a click track, pitch-step interval, waveform edge sizes, portamento glide timing, note presence
(Goertzel) and gate width. Spectral levels from 4 s of noise carry about +-1.5 dB of estimation noise.
Writes the results template with the 'measured' column filled. Standard library only.
"""
import argparse
import array
import cmath
import csv
import math
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import protocol as P  # noqa: E402

# ---------------------------------------------------------------------------------------------
# WAV input


class Wav:
    """Random-access WAV reader (RIFF/WAVE, PCM or IEEE float, incl. WAVE_FORMAT_EXTENSIBLE)."""

    def __init__(self, path):
        self.path = str(path)
        self.f = open(path, "rb")
        head = self.f.read(12)
        if len(head) < 12 or head[:4] != b"RIFF" or head[8:12] != b"WAVE":
            raise ValueError("%s: not a RIFF/WAVE file" % path)
        fmt = None
        while True:
            ch = self.f.read(8)
            if len(ch) < 8:
                raise ValueError("%s: no data chunk" % path)
            cid, size = ch[:4], struct.unpack("<I", ch[4:])[0]
            if cid == b"fmt ":
                fmt = self.f.read(size)
                if size & 1:
                    self.f.read(1)
            elif cid == b"data":
                if fmt is None:
                    raise ValueError("%s: data before fmt" % path)
                self.data_offset = self.f.tell()
                self.data_size = size
                break
            else:
                self.f.seek(size + (size & 1), 1)
        tag, self.channels, self.rate, _, self.block, self.bits = struct.unpack("<HHIIHH", fmt[:16])
        if tag == 0xFFFE and len(fmt) >= 26:
            tag = struct.unpack("<H", fmt[24:26])[0]
        if tag == 1 and self.bits in (16, 24, 32):
            self.kind = "pcm"
        elif tag == 3 and self.bits in (32, 64):
            self.kind = "float"
        else:
            raise ValueError("%s: unsupported WAV format (tag %d, %d bits)" % (path, tag, self.bits))
        self.f.seek(0, 2)
        avail = self.f.tell() - self.data_offset
        self.frames = min(self.data_size, avail) // self.block

    def close(self):
        self.f.close()

    @property
    def duration(self):
        return self.frames / float(self.rate)

    def _decode(self, raw):
        n = len(raw) // (self.bits // 8)
        big = sys.byteorder == "big"
        if self.kind == "float":
            a = array.array("f" if self.bits == 32 else "d")
            a.frombytes(raw[:n * (self.bits // 8)])
            if big:
                a.byteswap()
            return a, 1.0
        if self.bits == 16:
            a = array.array("h")
            a.frombytes(raw[:2 * n])
            if big:
                a.byteswap()
            return a, 1.0 / 32768.0
        code = "i" if array.array("i").itemsize == 4 else "l"
        if self.bits == 24:
            b4 = bytearray(4 * n)
            b4[1::4] = raw[0:3 * n:3]
            b4[2::4] = raw[1:3 * n:3]
            b4[3::4] = raw[2:3 * n:3]
            raw = bytes(b4)
        a = array.array(code)
        a.frombytes(raw[:4 * n])
        if big:
            a.byteswap()
        return a, 1.0 / 2147483648.0

    def read(self, t0, t1, channel="mix"):
        """Samples from t0 to t1 seconds as a list of floats (zeros outside the file)."""
        s0, s1 = int(round(t0 * self.rate)), int(round(t1 * self.rate))
        if s1 <= s0:
            return []
        lo, hi = max(0, s0), min(self.frames, s1)
        out_pre, out_post = [0.0] * (lo - s0) if lo > s0 else [], [0.0] * (s1 - hi) if s1 > hi else []
        if hi <= lo:
            return [0.0] * (s1 - s0)
        self.f.seek(self.data_offset + lo * self.block)
        raw = self.f.read((hi - lo) * self.block)
        a, scale = self._decode(raw)
        nch = self.channels
        if nch == 1:
            x = [v * scale for v in a]
        elif channel == "left" or (channel == "right" and nch < 2):
            x = [v * scale for v in a[0::nch]]
        elif channel == "right":
            x = [v * scale for v in a[1::nch]]
        else:
            half = 0.5 * scale
            x = [(l + r) * half for l, r in zip(a[0::nch], a[1::nch])]
        return out_pre + x + out_post


# ---------------------------------------------------------------------------------------------
# signal helpers


def db(v, floor=1e-12):
    return 20.0 * math.log10(max(v, floor))


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x)) if x else 0.0


def percentile(values, q):
    if not values:
        return None
    s = sorted(values)
    k = (len(s) - 1) * q
    i = int(k)
    return s[i] if i + 1 >= len(s) else s[i] + (s[i + 1] - s[i]) * (k - i)


def median(values):
    return percentile(values, 0.5)


def rising_crossings(x, hyst):
    """Fractional positions of rising zero crossings; re-armed only after the signal fell below -hyst."""
    out, armed = [], False
    prev = x[0] if x else 0.0
    for i in range(1, len(x)):
        v = x[i]
        if v < -hyst:
            armed = True
        elif armed and prev < 0.0 <= v:
            out.append(i - 1 + prev / (prev - v))
            armed = False
        prev = v
    return out


def frequency(x, fs):
    if len(x) < 4:
        return None
    c = rising_crossings(x, 0.2 * rms(x))
    if len(c) < 3:
        return None
    return fs * (len(c) - 1) / (c[-1] - c[0])


def pitch_track(x, fs, t_offset=0.0):
    """[(time s, frequency Hz)] per period, with glitches (> 25 % off the local median) removed."""
    c = rising_crossings(x, 0.15 * rms(x))
    raw = [((c[j] + c[j + 1]) * 0.5 / fs + t_offset, fs / (c[j + 1] - c[j])) for j in range(len(c) - 1)]
    out = []
    for j, (t, f) in enumerate(raw):
        ref = median([g for _, g in raw[max(0, j - 4):j + 5]])
        if abs(f - ref) <= 0.25 * ref:
            out.append((t, f))
    return out


def peak_env(x, fs, win_s, hop_s):
    """Peak-hold envelope: max |x| over win_s, every hop_s. Returns (values, times of window centres)."""
    hop = max(1, int(round(hop_s * fs)))
    blocks = [max(abs(v) for v in x[i:i + hop]) for i in range(0, len(x) - hop + 1, hop)]
    m = max(1, int(math.ceil(win_s / (hop / fs))))
    env = [max(blocks[k:k + m]) for k in range(0, max(0, len(blocks) - m + 1))]
    times = [(k * hop + m * hop * 0.5) / fs for k in range(len(env))]
    return env, times


_FFT_CACHE = {}


def fft(a):
    """In-place iterative radix-2 FFT of a list of complex numbers (length a power of two)."""
    n = len(a)
    if n not in _FFT_CACHE:
        bits = n.bit_length() - 1
        rev = [int(format(i, "0%db" % bits)[::-1], 2) for i in range(n)]
        tw = [cmath.exp(-2j * math.pi * k / n) for k in range(n // 2)]
        _FFT_CACHE[n] = (rev, tw)
    rev, tw = _FFT_CACHE[n]
    for i in range(n):
        j = rev[i]
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        half, step = size // 2, n // size
        for start in range(0, n, size):
            k = 0
            for i in range(start, start + half):
                t = tw[k] * a[i + half]
                a[i + half] = a[i] - t
                a[i] = a[i] + t
                k += step
        size *= 2
    return a


def welch(x, fs, max_segments=48, seg_s=0.17):
    """One-sided power spectral density (units^2 / Hz) with Hann windows of at least seg_s seconds
    (a power of two in samples), 50 % overlap."""
    seg = 1 << max(8, int(math.ceil(math.log2(fs * seg_s))))
    while seg > len(x) and seg > 256:
        seg //= 2
    w = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / seg) for i in range(seg)]
    wpow = sum(v * v for v in w)
    starts = list(range(0, len(x) - seg + 1, seg // 2))
    if len(starts) > max_segments:
        stride = len(starts) / float(max_segments)
        starts = [starts[int(i * stride)] for i in range(max_segments)]
    psd = [0.0] * (seg // 2 + 1)
    for s in starts:
        a = [complex(x[s + i] * w[i]) for i in range(seg)]
        fft(a)
        for i in range(seg // 2 + 1):
            psd[i] += abs(a[i]) ** 2
    scale = 1.0 / (fs * wpow * max(1, len(starts)))
    psd = [p * scale * (1.0 if i in (0, seg // 2) else 2.0) for i, p in enumerate(psd)]
    df = fs / seg
    return [i * df for i in range(len(psd))], psd


def band_mean(freqs, psd, f0, f1):
    vals = [p for f, p in zip(freqs, psd) if f0 <= f <= f1]
    return sum(vals) / len(vals) if vals else None


def smooth_fraction_octave(freqs, psd, frac=6):
    """Mean PSD over +-1/(2 frac) octave around each bin."""
    pre = [0.0]
    for p in psd:
        pre.append(pre[-1] + p)
    df = freqs[1] - freqs[0]
    r = 2.0 ** (1.0 / (2 * frac))
    out = []
    for i, f in enumerate(freqs):
        lo = max(1, int(math.floor(f / r / df)))
        hi = min(len(psd) - 1, max(lo, int(math.ceil(f * r / df))))
        out.append((pre[hi + 1] - pre[lo]) / (hi - lo + 1))
    return out


def crossing_freq(freqs, sm, level, f_from, upward=True):
    """First frequency from f_from (up or down) where the smoothed PSD falls below level (log interp.)."""
    idx = [i for i, f in enumerate(freqs) if (f >= f_from if upward else f <= f_from) and f > 0]
    if not upward:
        idx.reverse()
    prev = None
    for i in idx:
        if sm[i] < level:
            if prev is None:
                return freqs[i]
            a, b = 10 * math.log10(max(sm[prev], 1e-30)), 10 * math.log10(max(sm[i], 1e-30))
            lv = 10 * math.log10(level)
            frac = (a - lv) / (a - b) if a != b else 0.0
            return freqs[prev] + (freqs[i] - freqs[prev]) * frac
        prev = i
    return None


def goertzel_amp(x, fs, f):
    n = len(x)
    if n < 8:
        return 0.0
    k = 2.0 * math.cos(2.0 * math.pi * f / fs)
    s1 = s2 = 0.0
    wsum = 0.0
    for i, v in enumerate(x):
        w = 0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1))
        wsum += w
        s0 = v * w + k * s1 - s2
        s2, s1 = s1, s0
    power = s1 * s1 + s2 * s2 - k * s1 * s2
    return 2.0 * math.sqrt(max(power, 0.0)) / wsum


def first_time(times, values, pred, t_from=-1e9):
    for t, v in zip(times, values):
        if t >= t_from and pred(v):
            return t
    return None


def ms(v):
    return None if v is None else v * 1e3


# ---------------------------------------------------------------------------------------------
# measurements. Each gets the step audio (a callable seg(t0, t1, channel) relative to the anchor), fs,
# the parameters from protocol.py and returns {measurement name: value}.


def m_level(seg, fs, prm):
    x = seg(*prm["window"], prm.get("channel", "mix"))
    return {"rms_dbfs": db(rms(x)), "peak_dbfs": db(max((abs(v) for v in x), default=0.0))}


def m_freq(seg, fs, prm):
    f = frequency(seg(*prm["window"], prm.get("channel", "mix")), fs)
    out = {"freq_hz": f}
    if "ref_hz" in prm:
        out["cents_re_ref"] = None if f is None else 1200.0 * math.log2(f / prm["ref_hz"])
    return out


def m_spectrum(seg, fs, prm):
    freqs, psd = welch(seg(*prm["window"], prm.get("channel", "mix")), fs, seg_s=prm.get("seg_s", 0.17))
    p0, p1 = prm["pass"]
    pas = band_mean(freqs, psd, p0, p1)
    out = {"pass_dbfs_hz": None, "peak_hz": None, "peak_db_re_pass": None, "f_m3_hz": None, "f_m12_hz": None}
    if not pas:
        return out
    # resonance peak: 5-bin moving average of the PSD (narrower than the resonances it has to resolve,
    # wide enough that the maximum is not just the largest random fluctuation)
    sm = [sum(psd[max(0, i - 2):i + 3]) / len(psd[max(0, i - 2):i + 3]) for i in range(len(psd))]
    lo, hi = prm["peak_range"]
    cand = [(sm[i], freqs[i]) for i in range(len(freqs)) if lo <= freqs[i] <= min(hi, fs * 0.45)]
    peak, peak_f = max(cand) if cand else (None, None)
    out["pass_dbfs_hz"] = 10.0 * math.log10(pas)
    out["peak_hz"] = peak_f
    out["peak_db_re_pass"] = 10.0 * math.log10(peak / pas) if peak else None
    sm6 = smooth_fraction_octave(freqs, psd, 6)
    out["f_m3_hz"] = crossing_freq(freqs, sm6, pas * 10 ** -0.3, p1)
    out["f_m12_hz"] = crossing_freq(freqs, sm6, pas * 10 ** -1.2, p1)
    return out


def m_hp_corner(seg, fs, prm):
    """Corner of a first-order high-pass: least-squares fit of g + 10 log10(f^2 / (f^2 + fc^2)) to the
    1/6-octave smoothed spectrum at 48 log-spaced frequencies from 25 Hz to 4 kHz (a first-order slope
    is too shallow near fc for a reliable -3 dB crossing on a noise spectrum)."""
    freqs, psd = welch(seg(*prm["window"], prm.get("channel", "mix")), fs)
    plateau = band_mean(freqs, psd, 2000.0, 5000.0)
    if not plateau:
        return {"hp_m3_hz": None, "plateau_dbfs_hz": None}
    sm = smooth_fraction_octave(freqs, psd, 6)
    df = freqs[1] - freqs[0]

    def fit(f_lo, f_hi):
        pts = []
        for k in range(48):
            f = f_lo * (f_hi / f_lo) ** (k / 47.0)
            i = int(round(f / df))
            if 0 < i < len(sm) and sm[i] > 0:
                pts.append((freqs[i], 10.0 * math.log10(sm[i])))
        best = None
        for k in range(601):
            fc = 10.0 * (6000.0 / 10.0) ** (k / 600.0)
            model = [10.0 * math.log10(f * f / (f * f + fc * fc)) for f, _ in pts]
            g = sum(d - m for (_, d), m in zip(pts, model)) / len(pts)
            err = sum((d - g - m) ** 2 for (_, d), m in zip(pts, model))
            if best is None or err < best[0]:
                best = (err, fc)
        return best

    best = fit(25.0, 4000.0)
    if best:  # refit around the first estimate, away from the noisy deep stop band
        best = fit(max(25.0, best[1] / 4.0), min(4500.0, best[1] * 10.0)) or best
    return {"hp_m3_hz": best[1] if best else None, "plateau_dbfs_hz": 10.0 * math.log10(plateau)}


def _env(seg, fs, prm, win=0.00075, hop=0.0001):
    t0, t1 = prm["window"]
    x = seg(t0, t1, prm.get("channel", "mix"))
    env, times = peak_env(x, fs, win, hop)
    return env, [t + t0 for t in times]


def m_env_attack(seg, fs, prm):
    env, times = _env(seg, fs, prm)
    tail = [v for v, t in zip(env, times) if t >= times[-1] - 0.3] if env else []
    steady = median(tail)
    if not steady:
        return {"t10_ms": None, "t90_ms": None, "t100_ms": None}
    return {"t10_ms": ms(first_time(times, env, lambda v: v >= 0.1 * steady, 0.0)),
            "t90_ms": ms(first_time(times, env, lambda v: v >= 0.9 * steady, 0.0)),
            "t100_ms": ms(first_time(times, env, lambda v: v >= 0.98 * steady, 0.0))}


def m_env_decay(seg, fs, prm):
    env, times = _env(seg, fs, prm)
    if not env:
        return {"td6_ms": None, "td20_ms": None, "td40_ms": None}
    k = max(range(len(env)), key=lambda i: env[i])
    peak, tp = env[k], times[k]
    return {"td%d_ms" % d: ms(first_time(times, env, lambda v, d=d: v < peak * 10 ** (-d / 20.0), tp))
            for d in (6, 20, 40)}


def m_env_release(seg, fs, prm):
    env, times = _env(seg, fs, prm)
    off = prm["off"]
    held = median([v for v, t in zip(env, times) if off - 0.3 <= t <= off - 0.05])
    if not held:
        return {"tr6_ms": None, "tr20_ms": None, "tr40_ms": None}
    out = {}
    for d in (6, 20, 40):
        t = first_time(times, env, lambda v, d=d: v < held * 10 ** (-d / 20.0), off)
        out["tr%d_ms" % d] = ms(None if t is None else t - off)
    return out


def _cents_track(seg, fs, prm):
    t0, t1 = prm["window"]
    tr = pitch_track(seg(t0, t1, prm.get("channel", "mix")), fs, t0)
    if len(tr) < 8:
        return [], []
    ref = median([f for _, f in tr])
    return [t for t, _ in tr], [1200.0 * math.log2(f / ref) for _, f in tr]


def median3(values, half=1):
    """Running median over 2 * half + 1 points (shorter at the ends)."""
    return [median(values[max(0, k - half):k + half + 1]) for k in range(len(values))]


def despike(values):
    """Replaces isolated outliers (more than 3 typical steps off the 3-point median) by that median;
    leaves smooth shapes, including the apexes of a triangle, untouched."""
    if len(values) < 4:
        return list(values)
    med = median3(values)
    step = median([abs(b - a) for a, b in zip(values, values[1:])]) or 1e-12
    return [m if abs(v - m) > 3.0 * step else v for v, m in zip(values, med)]


def _modulation(times, cents):
    """LFO frequency from rising crossings of the de-meaned cents track, and the half peak-to-peak
    (after removing isolated glitches)."""
    if len(cents) < 8:
        return None, None
    mean = sum(cents) / len(cents)
    dev = [c - mean for c in cents]
    sm = despike(dev)
    p2p = max(sm) - min(sm)
    depth = 0.5 * p2p
    hyst = 0.2 * max(p2p * 0.5, 1e-9)
    cross, armed = [], False
    for j in range(1, len(sm)):
        a, b = sm[j - 1], sm[j]
        if b < -hyst:
            armed = True
        elif armed and a < 0.0 <= b:
            cross.append(times[j - 1] + (times[j] - times[j - 1]) * (a / (a - b)))
            armed = False
    rate = (len(cross) - 1) / (cross[-1] - cross[0]) if len(cross) >= 2 else None
    return rate, depth


def m_lfo_rate(seg, fs, prm):
    rate, depth = _modulation(*_cents_track(seg, fs, prm))
    return {"lfo_hz": rate, "depth_cents": depth}


def m_lfo_depth(seg, fs, prm):
    rate, depth = _modulation(*_cents_track(seg, fs, prm))
    return {"depth_cents": depth, "lfo_hz": rate}


def m_lfo_delay(seg, fs, prm):
    times, cents = _cents_track(seg, fs, prm)
    if len(cents) < 16:
        return {"t10_s": None, "t90_s": None, "depth_cents": None}
    t_end = times[-1]
    late = despike([c for t, c in zip(times, cents) if t >= t_end - 1.0])
    centre = 0.5 * (max(late) + min(late))
    final = 0.5 * (max(late) - min(late))
    # amplitude per half cycle: the largest |deviation| between consecutive crossings of the centre
    pts, start, best = [], 0, (0.0, times[0])
    for j in range(1, len(cents)):
        a, b = cents[j - 1] - centre, cents[j] - centre
        if abs(a) > best[0]:
            best = (abs(a), times[j - 1])
        if (a < 0) != (b < 0):
            pts.append((best[1], best[0]))
            best = (0.0, times[j])
    if final <= 0 or len(pts) < 4:
        return {"t10_s": None, "t90_s": None, "depth_cents": final}

    def reach(level):
        for (ta, va), (tb, vb) in zip(pts, pts[1:]):
            if va < level <= vb:
                return ta + (tb - ta) * (level - va) / (vb - va)
        return pts[0][0] if pts[0][1] >= level else None

    return {"t10_s": reach(0.1 * final), "t90_s": reach(0.9 * final), "depth_cents": final}


def m_beat(seg, fs, prm):
    x = seg(*prm["window"], prm.get("channel", "mix"))
    f = frequency(x, fs) or 100.0
    env, _ = peak_env(x, fs, 1.2 / f, 0.25 / f)
    if not env:
        return {"amp_max_dbfs": None, "amp_min_dbfs": None, "ratio": None}
    hi, lo = percentile(env, 0.99), percentile(env, 0.01)
    ratio = (hi + lo) / (hi - lo) if hi > 0 and (hi - lo) / hi > 0.05 else None
    return {"amp_max_dbfs": db(hi), "amp_min_dbfs": db(lo), "ratio": ratio}


def m_chorus_clicks(seg, fs, prm):
    x = seg(*prm["window"], prm.get("channel", "left"))
    a = [abs(v) for v in x]
    out = {"rate_hz": None, "centre_ms": None, "swing_ms": None, "clicks": 0}
    if not a:
        return out
    th = 0.25 * max(a)
    sep = max(1, int(0.0005 * fs))
    peaks = []
    for i in range(1, len(a) - 1):
        if a[i] >= th and a[i] >= a[i - 1] and a[i] > a[i + 1]:
            if peaks and i - peaks[-1] < sep:
                if a[i] > a[peaks[-1]]:
                    peaks[-1] = i
                continue
            peaks.append(i)

    def refine(i):
        y0, y1, y2 = a[i - 1], a[i], a[i + 1]
        den = y0 - 2 * y1 + y2
        return i + (0.5 * (y0 - y2) / den if den else 0.0)

    groups, cur = [], []
    for i in peaks:
        if cur and (i - cur[-1]) / fs > 0.012:
            groups.append(cur)
            cur = []
        cur.append(i)
    if cur:
        groups.append(cur)
    series = []
    for g in groups:
        dry = g[0]
        if dry + int(0.009 * fs) >= len(a) or dry < int(0.013 * fs):
            continue  # a click group cut by the window edges
        wets = [i for i in g[1:] if 0.0008 <= (i - dry) / fs <= 0.008]
        if not wets:
            continue
        wet = max(wets, key=lambda i: a[i])
        series.append((dry / fs, (refine(wet) - refine(dry)) / fs * 1e3))
    if len(series) < 8:
        out["clicks"] = len(series)
        return out
    d = [v for _, v in series]
    smooth = median3(d, 2)
    keep = [(t, v) for (t, v), s in zip(series, smooth) if abs(v - s) <= 0.5]
    times, delays = [t for t, _ in keep], [v for _, v in keep]
    rate, _ = _modulation(times, delays)
    if rate is None:
        out["clicks"] = len(keep)
        return out
    # The click train samples the delay at the DCO rate (~16 Hz), so the extremes fall between samples:
    # fit a triangle d(t) = centre + swing * tri(rate * t + phase) instead of taking max and min.
    rate, centre, swing = fit_triangle(times, delays, rate)
    return {"rate_hz": rate, "centre_ms": centre, "swing_ms": swing, "clicks": len(keep)}


def _tri(ph):
    return 1.0 - 4.0 * abs(ph - math.floor(ph) - 0.5)


def fit_triangle(times, vals, rate0):
    """Least-squares triangle fit: returns (rate, centre, swing) refined around rate0."""
    n = float(len(vals))
    sv = sum(vals)

    def solve(rate, phase):
        g = [_tri(rate * t + phase) for t in times]
        sg, sgg = sum(g), sum(x * x for x in g)
        sgv = sum(x * v for x, v in zip(g, vals))
        det = n * sgg - sg * sg
        if det <= 0:
            return None
        amp = (n * sgv - sg * sv) / det
        c = (sv - amp * sg) / n
        err = sum((v - c - amp * x) ** 2 for x, v in zip(g, vals))
        return err, rate, phase, c, amp

    best = None
    for k in range(-10, 11):
        for m in range(100):
            r = solve(rate0 * (1.0 + 0.004 * k), m / 100.0)
            if r and (best is None or r[0] < best[0]):
                best = r
    _, r0, p0, _, _ = best
    for k in range(-10, 11):
        for m in range(-20, 21):
            r = solve(r0 * (1.0 + 0.0004 * k), p0 + 0.0005 * m)
            if r and r[0] < best[0]:
                best = r
    _, rate, _, centre, amp = best
    return rate, centre, abs(amp)


def m_noise_bands(seg, fs, prm):
    x = seg(*prm["window"], prm.get("channel", "mix"))
    freqs, psd = welch(x, fs)
    lo = band_mean(freqs, psd, 200.0, 2000.0)
    hi = band_mean(freqs, psd, 5000.0, 15000.0) if fs / 2 > 15000 else None
    return {"rms_dbfs": db(rms(x)),
            "band_200_2k_dbfs_hz": 10 * math.log10(lo) if lo else None,
            "band_5k_15k_dbfs_hz": 10 * math.log10(hi) if hi else None,
            "tilt_db": 10 * math.log10(hi / lo) if lo and hi else None}


def m_pitch_steps(seg, fs, prm):
    t0, t1 = prm["window"]
    x = seg(t0, t1, prm.get("channel", "mix"))
    c = rising_crossings(x, 0.15 * rms(x))
    out = {"update_ms": None, "steps": 0, "median_interval_ms": None}
    if len(c) < 20:
        return out
    per = [c[j + 1] - c[j] for j in range(len(c) - 1)]
    dp = [per[j] - per[j - 1] for j in range(1, len(per))]
    mad = median([abs(v - median(dp)) for v in dp]) or 0.0
    thr = max(6.0 * 1.4826 * mad, 1e-3 * median(per))
    flagged = [j for j in range(1, len(per)) if abs(per[j] - per[j - 1]) > thr]
    steps, last = [], -10
    for j in flagged:
        if j - last > 1:
            steps.append(c[j] / fs + t0)
        last = j
    out["steps"] = len(steps)
    if len(steps) < 6:
        return out
    iv = [b - a for a, b in zip(steps, steps[1:])]
    out["median_interval_ms"] = median(iv) * 1e3
    # The update unit is the largest u (1-20 ms) of which at least 90 % of the step intervals are integer
    # multiples, within the jitter of one DCO period (a new divisor takes effect at the end of a cycle);
    # then refined by least squares over those intervals.
    tol = max(1.2 * median(per) / fs, 1e-4)
    u = 0.020
    while u >= 0.001:
        fits = [(v, int(round(v / u))) for v in iv if abs(v - round(v / u) * u) <= tol]
        if len(fits) >= 0.9 * len(iv):
            out["update_ms"] = 1e3 * sum(k * v for v, k in fits) / sum(k * k for _, k in fits)
            break
        u -= 0.000005
    return out


def m_edges(seg, fs, prm):
    x = seg(*prm["window"], prm.get("channel", "mix"))
    out = {"jumps": None, "j1": None, "j1_phase": None, "j2": None, "j2_phase": None, "j3": None}
    if len(x) < 16:
        return out
    c = rising_crossings(x, 0.2 * rms(x))
    p2p = max(x) - min(x)
    if len(c) < 4 or p2p <= 0:
        return out
    per_rows = []
    for j in range(len(c) - 1):
        s, e = int(math.ceil(c[j])), int(c[j + 1])
        if s < 2 or e + 2 >= len(x) or e - s < 12:
            continue
        length = c[j + 1] - c[j]
        d = [(x[i + 2] - x[i - 2], (i - c[j]) / length) for i in range(s, e)]
        found = []  # up to three jumps, at least a tenth of a period apart (circularly)
        for _ in range(3):
            cand = [(abs(v), v, ph) for v, ph in d
                    if all(min(abs(ph - q), 1.0 - abs(ph - q)) > 0.1 for _, q in found)]
            if not cand:
                break
            _, v, ph = max(cand)
            found.append((v / p2p, ph))
        n_big = sum(1 for v, _ in found if abs(v) >= 0.25 * abs(found[0][0]) and abs(v) >= 0.1)
        per_rows.append((n_big, found))
    if not per_rows:
        return out
    out["jumps"] = median([n for n, _ in per_rows])
    for k, name in ((0, "j1"), (1, "j2"), (2, "j3")):
        vals = [r[k] for _, r in per_rows if len(r) > k]
        if vals:
            out[name] = median([v for v, _ in vals])
            if name != "j3":
                out[name + "_phase"] = median([ph for _, ph in vals])
    return out


def m_glide(seg, fs, prm):
    t0, t1 = prm["window"]
    at = prm["at"]
    tr = pitch_track(seg(t0, t1, prm.get("channel", "mix")), fs, t0)
    out = {"t10_ms": None, "t50_ms": None, "t90_ms": None, "interval_st": None}
    if len(tr) < 16:
        return out
    st = [(t, 12.0 * math.log2(f / 440.0) + 69.0) for t, f in tr]
    s0 = median([s for t, s in st if at - 0.5 <= t <= at - 0.05])
    s1 = median([s for t, s in st if t >= t1 - 0.4])
    if s0 is None or s1 is None or abs(s1 - s0) < 0.5:
        return out
    out["interval_st"] = s1 - s0
    for q, name in ((0.1, "t10_ms"), (0.5, "t50_ms"), (0.9, "t90_ms")):
        t = first_time([t for t, _ in st], [(s - s0) / (s1 - s0) for _, s in st], lambda p, q=q: p >= q, at - 0.02)
        out[name] = ms(None if t is None else t - at)
    return out


def m_presence(seg, fs, prm):
    out = {}
    for k, e in enumerate(prm["events"]):
        before = seg(e - 0.3, e - 0.05, prm.get("channel", "mix"))
        after = seg(e + 0.05, e + 0.3, prm.get("channel", "mix"))
        for n in prm["notes"]:
            f = P.note_hz(n)
            a, b = goertzel_amp(before, fs, f), goertzel_amp(after, fs, f)
            out["n%d_at%d_db" % (n, k + 1)] = db(b) - db(a) if a > 0 else None
    return out


def m_gate(seg, fs, prm):
    t0, t1 = prm["window"]
    env, times = peak_env(seg(t0, t1, prm.get("channel", "mix")), fs, 0.002, 0.0005)
    out = {"gate_ms": None, "period_ms": None, "duty": None}
    if not env:
        return out
    thr = 0.5 * percentile(env, 0.99)
    ons, offs, state = [], [], env[0] >= thr
    for t, v in zip(times, env):
        if not state and v >= thr:
            ons.append(t)
            state = True
        elif state and v < thr:
            offs.append(t)
            state = False
    lengths = [b - a for a in ons for b in [next((o for o in offs if o > a), None)] if b is not None]
    periods = [b - a for a, b in zip(ons, ons[1:])]
    if lengths and periods:
        g, p = median(lengths), median(periods)
        out.update({"gate_ms": g * 1e3, "period_ms": p * 1e3, "duty": g / p})
    return out


MEASURE_FUNCS = {"level": m_level, "freq": m_freq, "spectrum": m_spectrum, "hp_corner": m_hp_corner,
                 "env_attack": m_env_attack, "env_decay": m_env_decay, "env_release": m_env_release,
                 "lfo_rate": m_lfo_rate, "lfo_depth": m_lfo_depth, "lfo_delay": m_lfo_delay, "beat": m_beat,
                 "chorus_clicks": m_chorus_clicks, "noise_bands": m_noise_bands, "pitch_steps": m_pitch_steps,
                 "edges": m_edges, "glide": m_glide, "presence": m_presence, "gate": m_gate}

# ---------------------------------------------------------------------------------------------
# alignment


def find_onset(wav, t_from, t_to, channel="mix", above_floor_db=26.0):
    """First time in [t_from, t_to] where the 1 ms peak envelope rises above the floor (the median of the
    first 50 ms) by above_floor_db, and at least to -90 dBFS, and stays up for 5 ms. None if nothing
    sounds. The threshold depends on the floor only, so a loud event later in the window cannot hide a
    quiet click."""
    t_from = max(0.0, t_from)
    x = wav.read(t_from, t_to, channel)
    fs = wav.rate
    hop = max(1, int(0.001 * fs))
    blocks = [max(abs(v) for v in x[i:i + hop]) for i in range(0, len(x) - hop + 1, hop)]
    if len(blocks) < 60:
        return None
    floor = median(blocks[:50])
    th = max(floor * 10 ** (above_floor_db / 20.0), 3.2e-5)
    for k in range(50, len(blocks) - 5):
        if blocks[k] >= th and min(blocks[k:k + 5]) >= th:
            for i in range(max(0, (k - 1) * hop), (k + 1) * hop):
                if abs(x[i]) >= th:
                    return t_from + i / float(fs)
            return t_from + k * hop / float(fs)
    return None


class StepAudio:
    """Audio of one step, addressed in seconds relative to its anchor (first note-on, else step start)."""

    def __init__(self, wav, anchor):
        self.wav, self.anchor = wav, anchor

    def __call__(self, t0, t1, channel="mix"):
        return self.wav.read(self.anchor + t0, self.anchor + t1, channel)


def analyse_step(step, audio, fs):
    results = {}
    for kind, prm in step.measures:
        try:
            values = MEASURE_FUNCS[kind](audio, fs, prm)
        except Exception as e:  # a recording that does not fit the step: report it, keep going
            print("  %s %s: failed (%s: %s)" % (step.id, kind, type(e).__name__, e), file=sys.stderr)
            values = {}
        for name, _ in P.measure_names(kind, prm):
            results[name] = values.get(name)
    return results


def run_recording(path, mode, row_key=None, start=None):
    wav = Wav(path)
    tracks, _ = P.schedule(mode)
    if row_key:
        tracks = [t for t in tracks if t.row.key == row_key]
        if not tracks:
            raise SystemExit("no row %r in %s mode (rows: %s)" % (
                row_key, mode, ", ".join(t.row.key for t in P.schedule(mode)[0])))
    first = tracks[0]
    if start is not None:
        offset = start - first.start
    else:
        t = find_onset(wav, 0.0, min(wav.duration, 120.0))
        if t is None:
            raise SystemExit("%s: no sync click found in the first 120 s" % path)
        offset = t - first.sync_time
    print("%s: %.1f s, %d Hz, %d ch, %d-bit; protocol start at %.3f s in the recording" % (
        path, wav.duration, wav.rate, wav.channels, wav.bits, first.start + offset))
    results = {}
    for tr in tracks:
        if tr.row.reuses:
            continue
        expected = tr.sync_time + offset
        if expected < 0.0 or expected > wav.duration:
            continue
        t = find_onset(wav, expected - 0.6, expected + 0.6)
        if t is not None and abs(t - expected) < 0.5:
            offset = t - tr.sync_time
        else:
            print("  %s: sync click not found near %.2f s, using the previous alignment" % (tr.row.key, expected))
        for step, t0 in zip(tr.steps, tr.step_starts):
            anchor = t0 + offset + (step.first_note or 0.0)
            if t0 + offset < 0.0:
                print("  %s: before the start of the recording" % step.id)
                continue
            if anchor + step.duration > wav.duration + 1.0:
                print("  %s: beyond the end of the recording" % step.id)
                continue
            results[step.id] = analyse_step(step, StepAudio(wav, anchor), wav.rate)
    return results


def run_steps_dir(directory, mode):
    directory = pathlib.Path(directory)
    results = {}
    for tr, step, _ in P.all_steps(mode):
        path = directory / (step.id + ".wav")
        if not path.exists():
            continue
        wav = Wav(path)
        anchor = 0.0
        if step.first_note is not None:
            t = find_onset(wav, 0.0, wav.duration)
            anchor = t if t is not None else step.first_note
        results[step.id] = analyse_step(step, StepAudio(wav, anchor), wav.rate)
        wav.close()
        print("  %s: %s (anchor %.3f s)" % (step.id, path.name, anchor))
    if not results:
        raise SystemExit("%s: no <step id>.wav files for %s mode" % (directory, mode))
    return results


def fmt(v):
    if v is None:
        return ""
    if isinstance(v, float):
        return "%.4g" % v if abs(v) < 1e5 else "%.1f" % v
    return str(v)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Analyse recordings of the VJN6 measurement protocol (SPEC 15.4).")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--recording", help="one WAV of the whole protocol (or of one row's track with --row)")
    src.add_argument("--steps-dir", help="directory of <step id>.wav files")
    ap.add_argument("--mode", choices=P.MODES, default="106")
    ap.add_argument("--row", help="the recording holds only this row's track (e.g. envelope)")
    ap.add_argument("--start", type=float, help="time in the recording where the protocol (or --row track) "
                    "starts, instead of finding the sync click")
    ap.add_argument("-o", "--out", help="results CSV (default: print the table only)")
    args = ap.parse_args(argv)
    try:
        if args.recording:
            results = run_recording(args.recording, args.mode, args.row, args.start)
        else:
            results = run_steps_dir(args.steps_dir, args.mode)
    except (OSError, ValueError) as e:
        print("analyze.py: %s" % e, file=sys.stderr)
        return 1
    rows = P.template_rows(args.mode)
    for r in rows:
        if r["step"] in results:
            r["measured"] = fmt(results[r["step"]].get(r["measurement"]))
    done = [r for r in rows if r["step"] in results]
    width = max([len(r["step"]) for r in done] + [4])
    for r in done:
        print("%-12s %-*s %-22s %12s  %-9s model %s" % (r["row"], width, r["step"], r["measurement"],
                                                       r["measured"] or "-", r["unit"], r["model"]))
    if args.out:
        with open(args.out, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=P.TEMPLATE_COLUMNS)
            w.writeheader()
            w.writerows(rows)
        print("wrote %s (%d of %d measurements filled)" % (args.out, sum(1 for r in rows if r["measured"]), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
