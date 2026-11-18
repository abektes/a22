#!/usr/bin/env python3
"""Cuts h1's recordings down to what the firmware carries: core/smp_data.c, core/smp_data.h and
docs/SAMPLES.md, from the sources in tools/samples/sources.py.

    python3 tools/samples/build.py [--preview]

Needs ffmpeg, numpy and scipy. The recordings are fetched once into build/samples-src (or the directory
in H1_SAMPLES_CACHE). --preview also writes build/samples-preview/*.wav: every zone as the engine will
play it, for a listen.

Two kinds of bank:
  pluck  The engine's strings are delay lines with a loss filter in the loop (core/str.h). For each
         recorded pluck the loop that best matches the note's decay is fitted, and the recording's first
         tenth of a second is run through that loop backwards: what is left is what the string was fed,
         the pluck and the body's answer to it. That is what is stored. Fed back into the engine's string
         it gives the recording's attack and body, and a decay that is the model's, with the fitted loss.
         For a note off the recording's pitch the engine plays it faster or slower, as a sampler would,
         and the fit is judged there too.
  loop   A held note's first moments, then a stretch that repeats: a whole number of periods, its end
         crossfaded into what came before its start, so it goes round without a seam.

Everything is stored as 8-bit mu-law at 22.05 kHz (or less where a source is dull), with its highs lifted
first (encode): the player lowers them again, and the 8 bits' noise with them.
"""
import hashlib
import os
import subprocess
import sys
import urllib.parse
import wave
from fractions import Fraction

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsp  # noqa: E402
from dsp import SR, ZSR  # noqa: E402
import sources  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CACHE = os.environ.get("H1_SAMPLES_CACHE", os.path.join(ROOT, "build", "samples-src"))
PREVIEW = os.path.join(ROOT, "build", "samples-preview")
PLUCK_RMS = 0.11       # every excitation is scaled so its note comes out this loud
LOOP_RMS = 0.16


def fetch(url):
    os.makedirs(CACHE, exist_ok=True)
    ext = os.path.splitext(url)[1]
    path = os.path.join(CACHE, hashlib.sha1(url.encode()).hexdigest()[:16] + ext)
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        subprocess.run(["curl", "-sS", "-f", "-L", "-m", "180", "-o", path, urllib.parse.quote(url, safe=":/")],
                       check=True)
    return path


def write_wav(path, x, sr=SR):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


# ---- a pluck ---------------------------------------------------------------------------------------
FRAMES = ((0.10, 0.20), (0.20, 0.40), (0.40, 0.80), (0.80, 1.30))


def band_levels(x, f0, nb=99):
    """Level in dB of each octave band from the fundamental's up, in each of FRAMES."""
    out = []
    for t0, t1 in FRAMES:
        seg = x[int(t0 * SR):int(t1 * SR)]
        if len(seg) < int((t1 - t0) * SR):
            break
        X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2 / len(seg)
        f = np.fft.rfftfreq(len(seg), 1 / SR)
        row, lo = [], f0 * 0.7
        while lo < 11000.0 and len(row) < nb:
            row.append(10 * np.log10(X[(f >= lo) & (f < lo * 2)].sum() + 1e-14))
            lo *= 2.0
        out.append(row)
    return np.array(out)


def excite(x, n, f0, g, t60):
    """The first n samples of a note with the string taken out of them, faded and left with no offset."""
    e = dsp.string_inverse(x[:n], dsp.loop_delay(f0, g), g, dsp.rho_of(t60, f0), f0)
    sos = signal.butter(2, min(45.0, f0 * 0.5) / (SR / 2), "high", output="sos")
    e = signal.sosfilt(sos, e)                     # (handling rumble is not part of the pluck)
    win = np.ones(n)
    fd = int(n * 0.6)
    win[-fd:] = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fd))
    e = e * win
    return e - win * e.sum() / win.sum()           # nothing left standing in the loop at 0 Hz


FIT_AT = (0.0, 4.0, -4.0)      # semitones off the recording's pitch a fit is judged at
FIT_G = (0.04, 0.06, 0.09, 0.13, 0.18, 0.25, 0.33, 0.42, 0.52, 0.62, 0.72, 0.8, 0.87, 0.92, 0.96, 0.985)
FIT_T60 = (0.25, 0.4, 0.6, 0.9, 1.3, 1.8, 2.5, 3.5, 5.0, 7.0, 10.0, 14.0)


def shifted(e, st):
    """e played st semitones up, as the engine plays it: faster."""
    if st == 0.0:
        return e
    fr = Fraction(2.0 ** (-st / 12.0)).limit_denominator(160)
    return signal.resample_poly(e, fr.numerator, fr.denominator, window=("kaiser", 9.0))


def pluck_note(e, f0, g, t60, st, nout):
    """The engine's note st semitones off the recording's: the string there, fed e at that speed."""
    f = f0 * 2.0 ** (st / 12.0)
    return dsp.string_run(shifted(e, st), nout, dsp.loop_delay(f, g), g, dsp.rho_of(t60, f), f)


def lv_at(y, f0, st, ref):
    """A note st semitones off the recording's, in the recording's bands moved with it (and where a band
    has gone over the top, the note is taken to be as the recording is)."""
    d = band_levels(y, f0 * 2.0 ** (st / 12.0), ref.shape[1])[:len(ref)]
    out = ref.copy()
    out[:len(d), :d.shape[1]] = d
    return out


def fit_pluck(x, f0, n):
    """The loop (its lowpass g and its t60) whose string, fed the note's start with that loop taken out of
    it, goes on most like the recording: octave bands, over the second that follows. On the recording's
    pitch and a third either side of it, where the same feed is played faster or slower."""
    ref = band_levels(x, f0)
    if len(ref) < 2:
        return 0.7, 2.0
    w = np.clip((ref[0] - (ref[0].max() - 40.0)) / 20.0, 0.0, 1.0)[None, :] * (ref > ref[0].max() - 55.0)
    nout = int(FRAMES[len(ref) - 1][1] * SR) + 8

    def lv(y, st):
        return lv_at(y, f0, st, ref)

    def err_of(g, t60):
        e = excite(x, n, f0, g, t60)
        err = 0.0
        for st in FIT_AT:
            y = pluck_note(e, f0, g, t60, st, nout)
            err += np.sum(w * np.clip(lv(y, st) - ref, -30.0, 30.0) ** 2)
        return err

    best = None
    for gi in range(1, len(FIT_G), 3):             # a coarse grid first
        for ti in range(1, len(FIT_T60), 3):
            err = err_of(FIT_G[gi], FIT_T60[ti])
            if best is None or err < best[0]:
                best = (err, gi, ti)
    _, gi, ti = best
    for a in range(max(0, gi - 2), min(len(FIT_G), gi + 3)):         # then all round the best of it
        for b in range(max(0, ti - 2), min(len(FIT_T60), ti + 3)):
            err = err_of(FIT_G[a], FIT_T60[b])
            if err < best[0]:
                best = (err, a, b)
    return FIT_G[best[1]], FIT_T60[best[2]]


def make_pluck(url, opt):
    x = dsp.load(fetch(url))
    x = x[dsp.onset(x):]
    x = x / np.abs(x[:SR // 2]).max()
    f0 = dsp.f0_any(x[int(0.08 * SR):int(0.45 * SR)])
    n = int(opt.get("ms", 100) * 0.001 * SR) & ~1
    g, t60 = fit_pluck(x, f0, n)
    e22 = dsp.down2(excite(x, n, f0, g, t60))
    q, pre, pk = encode(e22)
    eq = dsp.up2(decode(q, pre)) * pk
    ref = band_levels(x, f0)
    errs, ys = [], []
    for st in FIT_AT + (1.0, 7.0):
        y = pluck_note(eq, f0, g, t60, st, int(1.3 * SR) + 8)
        ys.append(y)
        errs.append(" ".join(f"{v:+.0f}" for v in (lv_at(y, f0, st, ref) - ref)[min(1, len(ref) - 1)]))
    gain = PLUCK_RMS / np.sqrt(np.mean(ys[0][:int(0.4 * SR)] ** 2))
    ys = [y * gain for y in ys]
    gain *= pk                                     # (the player reads the bytes as they are)
    return dict(kind="pluck", data=q, loop=len(q), key=float(dsp.midi(f0)), gain=float(gain), g=g, t60=t60, pre=pre,
                url=url, note=f"f0 {f0:.1f} Hz, g {g:.2f}, t60 {t60:.1f} s, pre {pre:.2f}, octave bands against the "
                              f"recording 0.2 to 0.4 s in, dB, at {', '.join(f'{st:+g}' for st in FIT_AT + (1.0, 7.0))} semitones: "
                              + " | ".join(errs),
                preview=np.concatenate(ys))


def encode(y):
    """y as stored: with its highs lifted as far as its spectrum is tilted (the player takes them down again,
    and the 8 bits' noise with them), then 8-bit mu-law. Returns the bytes, the lift and the scale."""
    r0, r1 = float(np.dot(y, y)), float(np.dot(y[1:], y[:-1]))
    pre = round(float(np.clip(r1 / (r0 + 1e-20), 0.0, 0.97)), 3)
    p = y - pre * np.concatenate([[0.0], y[:-1]])
    pk = np.abs(p).max() / 0.98
    # the player's lowering runs on what the bytes hold, so settle each byte with the error so far in hand
    q = np.zeros(len(p), np.uint8)
    tab = dsp.mulaw_dec(np.arange(256, dtype=np.uint8))
    order = np.argsort(tab)
    st = tab[order]
    acc = 0.0                                      # the player's running value
    for i in range(len(p)):
        want = y[i] / pk - pre * acc               # the byte that lands the lowered signal nearest
        j = int(np.clip(np.searchsorted(st, want), 1, 255))
        j = j if abs(st[j] - want) < abs(st[j - 1] - want) else j - 1
        q[i] = order[j]
        acc = st[j] + pre * acc
    return q, pre, pk


def decode(q, pre):
    """What the player makes of stored bytes."""
    return signal.lfilter([1.0], [1.0, -pre], dsp.mulaw_dec(q))


# ---- a loop ----------------------------------------------------------------------------------------
def f0_track(x, guess, hop):
    w = max(int(SR * 0.04), int(4 * SR / guess))
    out = []
    for i in range(0, len(x) - w, hop):
        out.append(dsp.f0_at(x[i:i + w], guess))
    return np.array(out), w


def seam_of(x, s, Lr, W):
    a, b = x[s - W:s + W], x[s + Lr - W:s + Lr + W]
    return np.sum((a - b) ** 2) / (np.sum(a * a) + np.sum(b * b) + 1e-12)


def resamp(x, num, den):
    fr = Fraction(num, den)
    return signal.resample_poly(x, fr.numerator, fr.denominator, window=("kaiser", 9.0))


def make_loop(url, opt, near):
    rate = opt.get("rate", ZSR)
    x = dsp.load(fetch(url))
    env = dsp.envelope(x, 8.0)
    on = max(0, int(np.argmax(env > 0.06 * env.max())) - int(0.006 * SR))
    x = x[on:]
    env = env[on:]
    f0g = dsp.f0_at(x[int(0.4 * SR):int(1.4 * SR)], dsp.hz(near))
    hop = int(0.01 * SR)
    trk, tw = f0_track(x, f0g, hop)
    att = int(opt["attack_ms"] * 0.001 * SR)
    want = opt["loop_ms"] * 0.001
    last = min(len(x) - int((want + 0.6) * SR), int(2.5 * SR))
    W = int(1.5 * SR / f0g)
    cands = []
    for s in range(att + int(0.1 * SR), max(att + int(0.1 * SR) + 1, last), max(1, int(SR / f0g / 6))):
        fi = min(len(trk) - 1, int((s + want * SR / 2 - tw / 2) / hop))
        fi0, fi1 = max(0, fi - int(want * 50)), min(len(trk), fi + int(want * 50) + 1)
        f0 = float(np.mean(trk[fi0:fi1]))
        nper = max(2, int(round(want * f0)))
        Lr = int(round(nper * SR / f0))
        if s - W < 0 or s + Lr + W >= len(x):
            continue
        e = env[s:s + Lr]
        flat = np.std(e) / (np.mean(e) + 1e-12)
        wob = np.std(trk[fi0:fi1]) / f0
        loud = np.mean(e) / env.max()
        cands.append((seam_of(x, s, Lr, W) + 0.5 * flat + 20.0 * wob + 0.15 * max(0.0, 0.5 - loud), s, Lr, nper, flat, wob))
    if not cands:
        raise SystemExit(f"no loop found in {url}")
    best = None
    for score, s, Lr, nper, flat, wob in sorted(cands)[:12]:        # the length to the sample, for the best few
        for d in range(-int(Lr * 0.012) - 1, int(Lr * 0.012) + 2):
            if s + Lr + d + W >= len(x):
                continue
            sm = seam_of(x, s, Lr + d, W)
            sc = score - seam_of(x, s, Lr, W) + sm
            if best is None or sc < best[0]:
                best = (sc, s, Lr + d, nper, sm, flat, wob)
    _, s, Lr, nper, seam, flat, wob = best
    f0 = nper * SR / Lr
    loop = x[s:s + Lr].copy()
    X = min(Lr // 3, int(3 * SR / f0))
    w = np.linspace(0.0, 1.0, X)
    loop[Lr - X:] = loop[Lr - X:] * (1.0 - w) + x[s - X:s] * w
    # level the loop, so going round it is not a tremolo
    per = int(SR / f0)
    pe = dsp.envelope(np.concatenate([loop, loop, loop]), 1000.0 * 2.0 / f0)[Lr + per:2 * Lr + per]
    loop = loop * np.clip(np.mean(pe) / pe, 0.7, 1.4)
    M = int(round(Lr * rate / SR))                 # the loop's length as stored
    lp3 = resamp(np.concatenate([loop] * 5), M, Lr)[M:4 * M]         # three turns of it, clear of the ends
    # the note's own start, then across into the loop where the two line up best
    a_src = resamp(x[:att + int(0.05 * SR)], rate, SR)
    a_len = int(att * rate / SR)
    Xa = min(a_len // 2, int(4 * rate / f0))
    tail = a_src[a_len - Xa:a_len]
    ext = np.concatenate([lp3, lp3[:Xa]])
    cor = signal.correlate(ext[:M + Xa], tail, mode="valid")
    ph = int(np.argmax(cor))
    wa = 0.5 - 0.5 * np.cos(np.linspace(0.0, np.pi, Xa))
    head = a_src[:a_len].copy()
    head[a_len - Xa:] = tail * (1.0 - wa) + ext[ph:ph + Xa] * wa * (np.sqrt(np.mean(tail ** 2)) / (np.sqrt(np.mean(ext[ph:ph + Xa] ** 2)) + 1e-9)) ** 0.0
    turn = np.concatenate([lp3, lp3])[ph + Xa:ph + Xa + M]
    y = np.concatenate([head, turn])
    y[:8] *= np.linspace(0.0, 1.0, 8)
    q, pre, pk = encode(y)
    d = decode(np.concatenate([q, q[a_len:]]), pre)                  # (the second time round is how it goes on)
    rms = np.sqrt(np.mean(d[a_len + M:] ** 2))
    key = float(dsp.midi(nper * ZSR / M))          # (as if stored at 22.05 kHz: that is how it is played)
    prev = np.concatenate([d[:a_len]] + [d[a_len + M:]] * int(1.5 * rate / M + 1))
    return dict(kind="loop", data=q, loop=a_len, key=key, gain=float(LOOP_RMS / rms), g=0.0, t60=0.0, pre=pre, url=url,
                note=f"f0 {f0:.1f} Hz, {nper} periods from {s / SR:.2f} s, seam {seam:.3f}, level spread {flat:.3f}, "
                     f"pitch spread {wob * 1200 / np.log(2):.0f} cents, pre {pre:.2f}" + (f", stored at {rate} Hz" if rate != ZSR else ""),
                preview=prev * (LOOP_RMS / rms), preview_sr=rate)


# ---- writing it out --------------------------------------------------------------------------------
def c_bytes(b, per=32):
    lines = []
    for i in range(0, len(b), per):
        lines.append(",".join(str(int(v)) for v in b[i:i + per]) + ",")
    return "\n".join(lines)


def main():
    preview = "--preview" in sys.argv
    zones, banks, blob = [], [], []
    off = 0
    doc = []
    for name, kind, what, opt, srcs in sources.BANKS:
        first = len(zones)
        rows = []
        for src in srcs:
            lib, url = src[0], src[1]
            z = make_pluck(url, opt) if kind == "pluck" else make_loop(url, opt, src[2])
            z["off"] = off
            z["lib"] = lib
            n = len(z["data"])
            pad = (-n) % 4
            blob.append(np.concatenate([z["data"], np.full(pad, 128, np.uint8)]))
            off += n + pad
            zones.append(z)
            rows.append(z)
            print(f"{name:12s} key {z['key']:6.2f} {n:6d} B  {z['note']}")
            if preview:
                write_wav(os.path.join(PREVIEW, f"{name}_{len(rows)}.wav"), z["preview"] * 2.0, z.get("preview_sr", SR))
        order = sorted(range(len(rows)), key=lambda i: rows[i]["key"])
        zones[first:] = [rows[i] for i in order]
        banks.append((name, kind, what, first, len(rows)))
        doc.append((name, kind, what, [rows[i] for i in order]))
    blob = np.concatenate(blob)
    mu = dsp.mulaw_dec(np.arange(256, dtype=np.uint8))

    with open(os.path.join(ROOT, "core", "smp_data.h"), "w") as f:
        f.write("/* h1 - the recordings' banks. Written by tools/samples/build.py: do not edit. */\n#pragma once\n\nenum {\n")
        for name, kind, what, first, n in banks:
            f.write(f"    SMP_{name},{' ' * (14 - len(name))}/* {what} */\n")
        f.write("    SMP_NBANK\n};\n")
    with open(os.path.join(ROOT, "core", "smp_data.c"), "w") as f:
        f.write("/* h1 - the recordings, cut down. Written by tools/samples/build.py from the sources in\n"
                " * tools/samples/sources.py: do not edit. Where each came from and its licence: docs/SAMPLES.md. */\n"
                "#include \"smp.h\"\n\n")
        f.write("const f32 smp_mu[256] = {\n")
        for i in range(0, 256, 8):
            f.write("    " + " ".join(f"{v:.6e}f," for v in mu[i:i + 8]) + "\n")
        f.write("};\n\n/* offset, length, loop start, key, gain, a string's g and t60, the lift */\n")
        f.write(f"const SmpZone smp_zones[{len(zones)}] = {{\n")
        for z in zones:
            f.write(f"    {{{z['off']}u, {len(z['data'])}, {z['loop']}, {z['key']:.4f}f, {z['gain']:.5e}f, {z['g']:.4f}f, "
                    f"{z['t60']:.3f}f, {z['pre']:.3f}f}},\n")
        f.write("};\n\nconst SmpBank smp_banks[SMP_NBANK] = {\n")
        for name, kind, what, first, n in banks:
            f.write(f"    {{{first}, {n}}},    /* {name} */\n")
        f.write(f"}};\n\nconst uint8_t smp_data[{len(blob)}] = {{\n")
        f.write(c_bytes(blob))
        f.write("\n};\n")

    with open(os.path.join(ROOT, "docs", "SAMPLES.md"), "w") as f:
        f.write("# The recordings in h1\n\n"
                "BARD and TWANG are built on recordings of real instruments. Every one of them is CC0: the people\n"
                "who made them gave them to the public domain, so they can be used in anything, sold or not, with no\n"
                "credit owed. They are credited here all the same.\n\n"
                "This file is written by `tools/samples/build.py` from `tools/samples/sources.py`, with\n"
                "`core/smp_data.c`. To change a recording, change the sources and run it again.\n\n"
                f"In the firmware they come to {len(blob):,} bytes: {len(zones)} cuts, 8-bit mu-law at 22.05 kHz.\n\n"
                "## Libraries\n\n| Library | By | Licence |\n|---|---|---|\n")
        used = []
        for z in zones:
            if z["lib"] not in used:
                used.append(z["lib"])
        for k in used:
            L = sources.LIBRARIES[k]
            f.write(f"| [{L['name']}]({L['url']}) | {L['by']} | {L['licence']} |\n")
        f.write("\n## What is cut from what\n\n"
                "A plucked bank keeps only the first tenth of a second of each note, with the string taken back out\n"
                "of it: the pluck and the instrument's body. The firmware's own string model plays it back, tuned\n"
                "to the recording's decay. A held bank keeps a note's start and a short stretch that repeats.\n")
        for name, kind, what, rows in doc:
            f.write(f"\n### {what}\n\n`SMP_{name}`, {'plucked' if kind == 'pluck' else 'held'}, "
                    f"{sum(len(z['data']) for z in rows):,} bytes.\n\n| Note | Recording | Library |\n|---|---|---|\n")
            for z in rows:
                fn = urllib.parse.unquote(z["url"].rsplit("/", 1)[1])
                f.write(f"| {z['key']:.1f} | [{fn}]({urllib.parse.quote(z['url'], safe=':/')}) | {sources.LIBRARIES[z['lib']]['name']} |\n")
    print(f"{len(zones)} zones, {len(blob)} bytes")


if __name__ == "__main__":
    main()
