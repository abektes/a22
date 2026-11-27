#!/usr/bin/env python3
"""A22's USB audio as a computer records it: ten channels, the mix on 1 and 2, T1 to T4 on 3-4, 5-6, 7-8 and 9-10
(core/h1.h h1_stems). Standard library only.

    tools/a22_stems.py REC.wav                 each pair's level, a tenth of a second at a time, and how far the four
                                               stems are from the mix (what is left is the mix's own effects)
    tools/a22_stems.py REC.wav --split DIR     the mix and the four tracks as five stereo WAVs in DIR
    tools/a22_stems.py REC.wav --check         tests/run_tests.sh, on tests/stems.txt's recordings: each track on its
                                               own pair and in order, none without the mix, and the stems adding up to
                                               the mix but for its master bus; stems_mute.wav beside it: T1 silent

Most DAWs split a ten-channel input into tracks themselves; --split is for an app that records the file whole."""
import math
import os
import struct
import sys
import wave

NAMES = ["mix", "T1", "T2", "T3", "T4"]
SUB_K = 2.0 * math.pi * 18.0 / 44100.0       # the mix's 18 Hz filter (core/synth.c SUB_K), which the stems are before


def read(path):
    w = wave.open(path)
    if w.getnchannels() != 10 or w.getsampwidth() != 2:
        sys.exit(f"{path}: {w.getnchannels()} channels of {8 * w.getsampwidth()} bits, not A22's ten of 16")
    n = w.getnframes()
    s = struct.unpack(f"<{n * 10}h", w.readframes(n))
    return w.getframerate(), [s[c::10] for c in range(10)]


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x)) if x else 0.0


def pair_rms(ch, p, a, b):
    return (rms(ch[2 * p][a:b]) + rms(ch[2 * p + 1][a:b])) / 2


def rows_of(ch, step):
    return [[pair_rms(ch, p, a, a + step) for p in range(5)] for a in range(0, len(ch[0]), step)]


def fit(ch, a, b):
    """the stems with the mix's 18 Hz filter put on them, against the mix from frame a to b: the one gain that fits
    best (the master bus's: its drive is about +1 dB) and what is left after it, of the mix"""
    num = den = err = sig = 0.0
    ys, ms = [], []
    for c in (0, 1):
        sub = 0.0
        for i in range(b):
            s = ch[2 + c][i] + ch[4 + c][i] + ch[6 + c][i] + ch[8 + c][i]
            sub += SUB_K * (s - sub)
            if i >= a:
                ys.append(s - sub)
                ms.append(ch[c][i])
    for y, m in zip(ys, ms):
        num += y * m
        den += y * y
    g = num / den if den else 0.0
    for y, m in zip(ys, ms):
        err += (m - g * y) ** 2
        sig += m * m
    return g, math.sqrt(err / sig) if sig else 0.0


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rate, ch = read(sys.argv[1])
    n = len(ch[0])
    if "--split" in sys.argv:
        out = sys.argv[sys.argv.index("--split") + 1]
        os.makedirs(out, exist_ok=True)
        base = os.path.splitext(os.path.basename(sys.argv[1]))[0]
        for p, name in enumerate(NAMES):
            w = wave.open(os.path.join(out, f"{base}-{name}.wav"), "wb")
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(rate)
            fr = [0] * (2 * n)
            fr[0::2], fr[1::2] = ch[2 * p], ch[2 * p + 1]
            w.writeframes(struct.pack(f"<{2 * n}h", *fr))
            w.close()
        print(f"{n} frames: {', '.join(base + '-' + x + '.wav' for x in NAMES)} in {out}")
        return
    step = rate // 10
    rows = rows_of(ch, step)
    raw = rms([ch[0][i] - sum(ch[c][i] for c in (2, 4, 6, 8)) for i in range(n)]) / max(rms(ch[0]), 1e-9)
    dc = " ".join(f"{sum(ch[c]) / n:.1f}" for c in range(10))
    if "--check" not in sys.argv:
        print("   s  " + " ".join(f"{x:>7}" for x in NAMES))
        for i, r in enumerate(rows):
            print(f"{i * 0.1:4.1f}  " + " ".join(f"{v:7.0f}" for v in r))
        g, left = fit(ch, 0, n)
        print(f"the mix less the stems: {raw:.0%} of the mix; with its 18 Hz filter on them and a gain of {g:.3f}, "
              f"{left:.1%}")
        print(f"each channel's mean: {dc}")
        return
    # tests/stems.txt: a key on T1, T2, T3, T4 about 1.2 s apart, then a loud chord by hand on T4 from about 5.2 s
    bad = []
    for p in range(1, 5):
        if max(r[p] for r in rows) < 20:
            bad.append(f"{NAMES[p]} silent")
    onset = []
    for p in range(1, 5):                            # where each track first comes in
        top = max(r[p] for r in rows)
        onset.append(next(i for i, r in enumerate(rows) if r[p] > 0.3 * top) * 0.1)
    if onset != sorted(onset) or len(set(onset)) != 4:
        bad.append(f"the tracks come in at {onset} s, not T1 to T4 in turn")
    for i, r in enumerate(rows):
        s = sorted(r[1:], reverse=True)
        if r[0] > 20 and (s[0] < 0.6 * r[0] or s[1] > 0.1 * s[0]):   # one track at a time here, on its own pair
            bad.append(f"{i * 0.1:.1f} s: the pairs at {[round(v) for v in r]}")
        if r[0] < 2 and s[0] > 5:                                    # none sounds while the mix is silent
            bad.append(f"{i * 0.1:.1f} s: a track without the mix")
    chord = int(onset[3] * rate) + int(1.2 * rate)   # the four notes, up to the chord
    g, left = fit(ch, 0, chord)
    if not 1.0 < g < 1.3 or left > 0.05:
        bad.append(f"the stems with the 18 Hz filter fit the mix at a gain of {g:.3f}, {left:.1%} left (not 1..1.3 "
                   "and under 5%)")
    mute = os.path.join(os.path.dirname(sys.argv[1]), "stems_mute.wav")
    if os.path.exists(mute):                         # T1 muted: its pair silent, the other three on theirs
        _, mc = read(mute)
        mr = rows_of(mc, step)
        if max(max(abs(v) for v in mc[c]) for c in (2, 3)):
            bad.append("T1 muted, but its pair is not silent")
        for p in (2, 3, 4):
            if max(r[p] for r in mr) < 20:
                bad.append(f"T1 muted: {NAMES[p]} silent")
    else:
        bad.append(f"no {mute}")
    if bad:
        sys.exit("stems: " + "; ".join(bad))
    print(f"stems: T1 to T4 each on its own pair, in turn; with the mix's 18 Hz filter on them they fit the mix at a "
          f"gain of {g:.3f} (its master bus) to {left:.1%}; T1 muted, its pair silent; each channel's mean: {dc}")


if __name__ == "__main__":
    main()
