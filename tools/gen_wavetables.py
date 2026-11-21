#!/usr/bin/env python3
"""Writes core/wave_data.c: WAVE's eight banks of fifteen single-cycle waves (inst_wave.c).

    python3 tools/gen_wavetables.py

Every wave is made here from its harmonics (nothing is read from another synth) and given the same loudness
(RMS) at its fullest. A bank's waves at the even places (0, 2 .. 14) are its eight main ones; the seven
between them are waves in their own right (a duty, ratio, vowel or registration halfway), not crossfades,
so K1 sweeps through real shapes. Presets keep their sound: K1 at k/7 is still main wave k.

Each wave is kept at six levels, an octave apart: level k has WT_H >> k harmonics in WT_N >> k samples (not
fewer than 32). inst_wave.c reads the level whose highest harmonic stays under ~18 kHz at the note it plays,
so a high note has nothing above the Nyquist frequency to fold back (mipmapped wavetables). The samples are
16-bit; wave_unit turns them back into the units the generator made them in. About 141 KB of flash.
"""
import math
import os

WT_N = 256          # samples a cycle at level 0
WT_H = 48           # harmonics at level 0
WT_WAVES = 15       # a bank: eight main waves and seven between
WT_LEVELS = 6
WT_MIN = 32         # no level shorter than this
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


# ---- spectra: a wave is a list of WT_H (sine, cosine) amplitudes, harmonic 1 first ----------------------
def spectrum(amps, phases=None):
    """harmonic amplitudes, sine phase unless phases says"""
    out = []
    for h in range(1, WT_H + 1):
        a = amps[h - 1] if h <= len(amps) else 0.0
        ph = phases[h - 1] if phases else 0.0
        out.append((a * math.cos(ph), a * math.sin(ph)))      # a sin(x + ph) = a cos ph sin x + a sin ph cos x
    return out


def project(f, res=4096):
    """the harmonics of a cycle given by f(t), t in 0..1: a band-limited copy"""
    xs = [f(i / res) for i in range(res)]
    mean = sum(xs) / res
    xs = [x - mean for x in xs]
    tw = [2.0 * math.pi * i / res for i in range(res)]
    out = []
    for h in range(1, WT_H + 1):
        s = sum(x * math.sin(h * t) for x, t in zip(xs, tw)) * 2.0 / res
        c = sum(x * math.cos(h * t) for x, t in zip(xs, tw)) * 2.0 / res
        out.append((s, c))
    return out


def saw_amps(count):
    """a saw's harmonics up to count (a fraction lets the last one in partly)"""
    amps = []
    for h in range(1, WT_H + 1):
        g = 1.0 if h <= count else max(0.0, 1.0 - (h - count))
        amps.append(g * ((-1) ** (h + 1)) / h)
    return amps


def formant_amps(f0, formants):
    """harmonics of f0 shaped by resonances (freq, bandwidth, gain): a vowel at that pitch"""
    amps = []
    for h in range(1, WT_H + 1):
        f = f0 * h
        g = sum(gain / (1.0 + ((f - fc) / bw) ** 2) for fc, bw, gain in formants)
        amps.append(g / math.sqrt(h))
    return amps


def halfway(xs, mid=lambda a, b: (a + b) / 2.0):
    """eight main parameters -> fifteen: each pair's middle between them"""
    out = []
    for i, x in enumerate(xs):
        out.append(x)
        if i + 1 < len(xs):
            out.append(mid(x, xs[i + 1]))
    return out


# ---- the banks -----------------------------------------------------------------------------------------
def bank_harmon():
    """HARMON: a sine gaining harmonics until it is a full saw (between: the geometric middle count)"""
    counts = halfway([1, 2, 3, 5, 8, 13, 24, 48], lambda a, b: math.sqrt(a * b))
    return [spectrum(saw_amps(c)) for c in counts]


def bank_organ():
    """ORGAN: drawbar registrations (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') as harmonics"""
    feet = [0.5, 1.5, 1, 2, 3, 4, 5, 6, 8]          # each drawbar's harmonic of the 8' fundamental
    regs = ["008000000", "008400000", "888000000", "878500000", "868000000", "838000444", "800000888",
            "804000868", "688600000", "788800300", "888888888", "868666644", "806000003", "846300012",
            "876543211"]
    out = []
    for r in regs:
        amps = [0.0] * WT_H
        for bar, d in zip(feet, r):
            h = int(bar * 2)                          # 16' is the table's fundamental (one octave down)
            if int(d) and 1 <= h <= WT_H:
                amps[h - 1] += int(d) / 8.0
        out.append(spectrum(amps))
    return out


def bank_vox():
    """VOX: vowels A, E, I, O, U and the ones between, at a voice's pitch (between: formants halfway)"""
    f0 = 110.0
    vow = {
        "A": [(730, 90, 1.0), (1090, 110, 0.5), (2440, 160, 0.25)],
        "E": [(530, 70, 1.0), (1840, 120, 0.45), (2480, 160, 0.3)],
        "I": [(270, 60, 1.0), (2290, 130, 0.35), (3010, 200, 0.25)],
        "O": [(570, 80, 1.0), (840, 90, 0.6), (2410, 170, 0.15)],
        "U": [(300, 60, 1.0), (870, 90, 0.35), (2240, 170, 0.1)],
    }

    def mix(a, b, t):
        return [(fa + (fb - fa) * t, ba + (bb - ba) * t, ga + (gb - ga) * t) for (fa, ba, ga), (fb, bb, gb) in zip(a, b)]
    main = [vow["A"], mix(vow["A"], vow["E"], 0.5), vow["E"], vow["I"], mix(vow["I"], vow["O"], 0.5), vow["O"],
            vow["U"], mix(vow["U"], vow["A"], 0.5)]
    return [spectrum(formant_amps(f0, f)) for f in halfway(main, lambda a, b: mix(a, b, 0.5))]


def bank_digi():
    """DIGI: bands of upper harmonics with gaps, hard and glassy-digital (between: the band halfway up)"""
    out = []
    for j in range(WT_WAVES):
        k = j / 2.0
        amps = [0.0] * WT_H
        amps[0] = 0.6
        lo = int(round(3 + k * 4))
        for h in range(lo, min(WT_H, lo + 4 + int(k))):
            amps[h - 1] = 0.9 * (1 if (h - lo) % 2 == 0 else -0.6)
        out.append(spectrum(amps))
    return out


def bank_pulse():
    """PULSE: a square narrowing to a thin pulse"""
    duties = halfway([0.5, 0.42, 0.34, 0.27, 0.2, 0.14, 0.09, 0.05])
    return [project(lambda t, d=d: 1.0 if t < d else -1.0) for d in duties]


def bank_sync():
    """SYNC: a saw hard-synced at ratios 1 to 4.5 (the bright, tearing sweep)"""
    ratios = halfway([1.0, 1.4, 1.8, 2.25, 2.7, 3.2, 3.8, 4.5])
    return [project(lambda t, r=r: 2.0 * ((t * r) % 1.0) - 1.0) for r in ratios]


def bank_glass():
    """GLASS: sparse high partials over the fundamental, like struck glass and bells"""
    sets = [[1, 4], [1, 3, 5], [1, 3, 7], [1, 4, 8], [1, 5, 9], [1, 4, 7, 11], [1, 3, 8, 13], [1, 5, 10, 15],
            [1, 6, 11, 17], [1, 4, 10, 15, 19], [2, 5, 9, 14, 21], [2, 6, 11, 16, 24], [1, 7, 12, 19, 27],
            [2, 8, 13, 21, 29, 36], [3, 8, 14, 22, 31, 41]]
    out = []
    for s in sets:
        amps = [0.0] * WT_H
        for i, h in enumerate(s):
            amps[h - 1] = 1.0 / (1.0 + 0.35 * i)
        out.append(spectrum(amps))
    return out


def bank_growl():
    """GROWL: a saw through a comb of notches that close in, a snarl"""
    out = []
    for j in range(WT_WAVES):
        period = 2.0 + (j / 2.0) * 0.85
        amps = []
        for h in range(1, WT_H + 1):
            notch = 0.5 + 0.5 * math.cos(2.0 * math.pi * h / period)
            amps.append(((-1) ** (h + 1)) / h * (0.15 + 0.85 * notch))
        out.append(spectrum(amps))
    return out


BANKS = [("HARMON", bank_harmon), ("ORGAN", bank_organ), ("VOX", bank_vox), ("DIGI", bank_digi),
         ("PULSE", bank_pulse), ("SYNC", bank_sync), ("GLASS", bank_glass), ("GROWL", bank_growl)]


def level_shape(k):
    return max(1, WT_H >> k), max(WT_MIN, WT_N >> k)


def levels_of(spec):
    """the wave at every level: its first H harmonics in N samples, the last sample the first again; the
    loudness set by the whole wave, so a high note is as loud as the harmonics it keeps"""
    rms = math.sqrt(sum((s * s + c * c) / 2.0 for s, c in spec)) or 1.0
    g = 0.42 / rms                                    # (every wave as loud as a sine of peak ~0.6)
    out = []
    for k in range(WT_LEVELS):
        nh, n = level_shape(k)
        w = []
        for i in range(n):
            x = 2.0 * math.pi * i / n
            w.append(g * sum(s * math.sin(h * x) + c * math.cos(h * x) for h, (s, c) in enumerate(spec[:nh], 1)))
        out.append(w + [w[0]])
    return out


def main():
    waves = []
    for name, fn in BANKS:
        spec = fn()
        assert len(spec) == WT_WAVES, name
        waves.append((name, [levels_of(s) for s in spec]))
    peak = max(abs(x) for _, ws in waves for lv in ws for w in lv for x in w)
    unit = peak / 32000.0                             # (a little under full scale)
    lens = [level_shape(k)[1] + 1 for k in range(WT_LEVELS)]
    offs = [sum(lens[:k]) for k in range(WT_LEVELS)]
    stride = sum(lens)
    lines = ["/* A22 - WAVE's wavetables: 8 banks x 15 waves, each at 6 levels an octave apart (fewer harmonics in",
             " * fewer samples), 16-bit. Written by tools/gen_wavetables.py: do not edit. */",
             '#include "wave_data.h"', "",
             f"const f32 wave_unit = {unit!r}f;" if "e" in repr(unit) or "." in repr(unit) else f"const f32 wave_unit = {unit}.0f;",
             "const uint16_t wave_len[WT_LEVELS] = {" + ", ".join(str(n - 1) for n in lens) + "};",
             "const uint16_t wave_off[WT_LEVELS] = {" + ", ".join(str(o) for o in offs) + "};",
             f"typedef char wave_stride_ok[WT_STRIDE == {stride} ? 1 : -1];", "",
             "const int16_t wave_pcm[WT_BANKS * WT_WAVES * WT_STRIDE] = {"]
    for name, ws in waves:
        lines.append(f"    /* {name} */")
        for lv in ws:
            vals = [int(round(x / unit)) for w in lv for x in w]
            for i in range(0, len(vals), 22):
                lines.append("    " + ", ".join(str(v) for v in vals[i:i + 22]) + ",")
    lines.append("};")
    path = os.path.join(ROOT, "core", "wave_data.c")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote core/wave_data.c: {len(BANKS) * WT_WAVES} waves x {stride} samples "
          f"({len(BANKS) * WT_WAVES * stride * 2 // 1024} KB), peak {peak:.3f}")


if __name__ == "__main__":
    main()
