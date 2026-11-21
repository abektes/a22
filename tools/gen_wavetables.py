#!/usr/bin/env python3
"""Writes core/wave_data.c: WAVE's eight banks of eight single-cycle waves (inst_wave.c).

    python3 tools/gen_wavetables.py

Every wave is made here from its harmonics (nothing is read from another synth), band-limited to WT_H
harmonics, given the same loudness (RMS), and stored integrated: entry n is the sum of the wave's first n
samples, so the table runs from 0 back to 0 over a cycle. inst_wave.c reads two points of it a sample apart
and divides by the distance, which is the wave averaged over that step: integrated wavetable synthesis, a
first-order guard against aliasing with no copies of each wave per octave. Stored as float, WT_N + 1 entries
a wave (the last equal to the first), 64 waves, about 33 KB of flash.
"""
import math
import os

WT_N = 128          # samples a cycle
WT_H = 48           # harmonics at most (of WT_N / 2 = 64)
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def additive(amps, phases=None):
    """a wave from harmonic amplitudes amps[0] = the fundamental, sine phase unless phases says"""
    w = [0.0] * WT_N
    for h, a in enumerate(amps[:WT_H], 1):
        if not a:
            continue
        ph = phases[h - 1] if phases else 0.0
        for n in range(WT_N):
            w[n] += a * math.sin(2.0 * math.pi * h * n / WT_N + ph)
    return w


def project(f, res=4096):
    """the harmonics (sine and cosine) of a cycle given by f(t), t in 0..1, to WT_H: a band-limited copy"""
    xs = [f(i / res) for i in range(res)]
    mean = sum(xs) / res
    w = [0.0] * WT_N
    for h in range(1, WT_H + 1):
        s = c = 0.0
        for i, x in enumerate(xs):
            a = 2.0 * math.pi * h * i / res
            s += (x - mean) * math.sin(a)
            c += (x - mean) * math.cos(a)
        s *= 2.0 / res
        c *= 2.0 / res
        for n in range(WT_N):
            a = 2.0 * math.pi * h * n / WT_N
            w[n] += s * math.sin(a) + c * math.cos(a)
    return w


def saw_amps(nh, tilt=1.0):
    return [((-1) ** (h + 1)) / (h ** tilt) for h in range(1, nh + 1)]


def formant_amps(f0, formants, nh=WT_H):
    """harmonics of f0 shaped by resonances (freq, bandwidth, gain): a vowel at that pitch"""
    amps = []
    for h in range(1, nh + 1):
        f = f0 * h
        g = sum(gain / (1.0 + ((f - fc) / bw) ** 2) for fc, bw, gain in formants)
        amps.append(g / math.sqrt(h))
    return amps


def bank_harmon():
    """HARMON: a sine gaining harmonics until it is a full saw"""
    counts = [1, 2, 3, 5, 8, 13, 24, 48]
    return [additive(saw_amps(c)) for c in counts]


def bank_organ():
    """ORGAN: drawbar registrations (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') as harmonics"""
    feet = [0.5, 1.5, 1, 2, 3, 4, 5, 6, 8]          # each drawbar's harmonic of the 8' fundamental
    regs = ["008000000", "888000000", "868000000", "800000888", "688600000",
            "888888888", "806000003", "876543211"]
    out = []
    for r in regs:
        amps = [0.0] * WT_H
        for bar, d in zip(feet, r):
            lv = int(d) / 8.0
            if not lv:
                continue
            h = int(bar * 2)                          # 16' is the table's fundamental (one octave down)
            if 1 <= h <= WT_H:
                amps[h - 1] += lv
        out.append(additive(amps))
    return out


def bank_vox():
    """VOX: vowels A, E, I, O, U and three between, at a voice's pitch (the formants move with the note)"""
    f0 = 110.0
    vow = {
        "A": [(730, 90, 1.0), (1090, 110, 0.5), (2440, 160, 0.25)],
        "E": [(530, 70, 1.0), (1840, 120, 0.45), (2480, 160, 0.3)],
        "I": [(270, 60, 1.0), (2290, 130, 0.35), (3010, 200, 0.25)],
        "O": [(570, 80, 1.0), (840, 90, 0.6), (2410, 170, 0.15)],
        "U": [(300, 60, 1.0), (870, 90, 0.35), (2240, 170, 0.1)],
    }

    def mix(a, b, t):
        return [(fa + (fb - fa) * t, ba + (bb - ba) * t, ga + (gb - ga) * t)
                for (fa, ba, ga), (fb, bb, gb) in zip(vow[a], vow[b])]
    seq = [vow["A"], mix("A", "E", 0.5), vow["E"], vow["I"], mix("I", "O", 0.5), vow["O"], vow["U"],
           mix("U", "A", 0.5)]
    return [additive(formant_amps(f0, f)) for f in seq]


def bank_digi():
    """DIGI: bands of upper harmonics with gaps, hard and glassy-digital"""
    out = []
    for k in range(8):
        amps = [0.0] * WT_H
        amps[0] = 0.6
        lo = 3 + k * 4
        for h in range(lo, min(WT_H, lo + 4 + k)):
            amps[h - 1] = 0.9 * (1 if (h - lo) % 2 == 0 else -0.6)
        out.append(additive(amps))
    return out


def bank_pulse():
    """PULSE: a square narrowing to a thin pulse"""
    duties = [0.5, 0.42, 0.34, 0.27, 0.2, 0.14, 0.09, 0.05]
    return [project(lambda t, d=d: 1.0 if t < d else -1.0) for d in duties]


def bank_sync():
    """SYNC: a saw hard-synced at ratios 1 to 4.5 (the bright, tearing sweep)"""
    ratios = [1.0, 1.4, 1.8, 2.25, 2.7, 3.2, 3.8, 4.5]
    return [project(lambda t, r=r: 2.0 * ((t * r) % 1.0) - 1.0) for r in ratios]


def bank_glass():
    """GLASS: sparse high partials over the fundamental, like struck glass and bells"""
    sets = [[1, 4], [1, 3, 7], [1, 5, 9], [1, 3, 8, 13], [1, 6, 11, 17], [2, 5, 9, 14, 21],
            [1, 7, 12, 19, 27], [3, 8, 14, 22, 31, 41]]
    out = []
    for s in sets:
        amps = [0.0] * WT_H
        for i, h in enumerate(s):
            amps[h - 1] = 1.0 / (1.0 + 0.35 * i)
        out.append(additive(amps))
    return out


def bank_growl():
    """GROWL: a saw through a comb of notches that close in, a snarl"""
    out = []
    for k in range(8):
        period = 2.0 + k * 0.85
        amps = []
        for h in range(1, WT_H + 1):
            notch = 0.5 + 0.5 * math.cos(2.0 * math.pi * h / period)
            amps.append(((-1) ** (h + 1)) / h * (0.15 + 0.85 * notch))
        out.append(additive(amps))
    return out


BANKS = [("HARMON", bank_harmon), ("ORGAN", bank_organ), ("VOX", bank_vox), ("DIGI", bank_digi),
         ("PULSE", bank_pulse), ("SYNC", bank_sync), ("GLASS", bank_glass), ("GROWL", bank_growl)]


def integrated(w):
    mean = sum(w) / WT_N
    w = [x - mean for x in w]
    rms = math.sqrt(sum(x * x for x in w) / WT_N) or 1.0
    w = [0.42 * x / rms for x in w]                  # (every wave as loud as a sine of peak ~0.6)
    out, acc = [], 0.0
    for x in w:
        out.append(acc)
        acc += x
    out.append(0.0)                                  # (the cycle sums to 0: back where it started)
    return out


def cfloat(v):
    t = f"{0.0 if abs(v) < 1e-9 else v:.6g}"
    return (t if ("." in t or "e" in t) else t + ".0") + "f"


def main():
    lines = ["/* A22 - WAVE's wavetables: 8 banks x 8 waves, each WT_N samples, stored integrated (entry n is the",
             " * sum of the first n samples). Written by tools/gen_wavetables.py: do not edit. */",
             '#include "wave_data.h"', "",
             "const f32 wave_tab[WT_BANKS * WT_WAVES][WT_N + 1] = {"]
    for name, fn in BANKS:
        waves = fn()
        assert len(waves) == 8, name
        lines.append(f"    /* {name} */")
        for w in waves:
            vals = integrated(w)
            lines.append("    {" + ", ".join(cfloat(v) for v in vals) + "},")
    lines.append("};")
    path = os.path.join(ROOT, "core", "wave_data.c")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote core/wave_data.c: {len(BANKS) * 8} waves x {WT_N + 1} floats "
          f"({len(BANKS) * 8 * (WT_N + 1) * 4 // 1024} KB)")


if __name__ == "__main__":
    main()
