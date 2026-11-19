#!/usr/bin/env python3
"""Cuts VIBE's hits down to what the firmware carries: core/vibe_data.c and core/vibe_data.h, from
sources.DRUMS. (docs/SAMPLES.md's part on them is written by build.py, from the same list.)

    python3 tools/samples/drums.py [--preview]

Needs ffmpeg, numpy and scipy. Each hit is one recording: trimmed to its start, brought down to the rate
its top allows, and kept for a tenth of a second or so. Its own dying away is taken out of it first (the
player puts it back, longer or shorter with the DECAY knob), which also keeps the quiet end of it out of
the coder's noise. A ringing thing gets a stretch after its start that repeats, its end crossfaded into
what came before it, and the player lets that die away at the ring's length.

They are stored as 4-bit IMA ADPCM: half the bytes of the 8 bits BARD's and TWANG's recordings have, at
the price of some grit, which a drum can take. --preview writes build/samples-preview/VIBE_*.wav: every
hit as the firmware will decode it, at its stored rate.
"""
import os
import subprocess
import sys
import urllib.parse

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sources  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PREVIEW = os.path.join(ROOT, "build", "samples-preview")

STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
         118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
         963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
         5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
         24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm(x, idx0, loop=None):
    """x: int16 values. Returns the nibbles, what they decode to, and the coder's state at the loop's start."""
    pred, idx = 0, idx0
    out = np.zeros(len(x), np.uint8)
    dec = np.zeros(len(x), np.int32)
    at_loop = (0, idx0)
    for i, s in enumerate(x):
        if loop is not None and i == loop:
            at_loop = (pred, idx)
        step = STEPS[idx]
        d = int(s) - pred
        n = 0
        if d < 0:
            n = 8
            d = -d
        q = step >> 3
        if d >= step:
            n |= 4; d -= step; q += step
        if d >= step >> 1:
            n |= 2; d -= step >> 1; q += step >> 1
        if d >= step >> 2:
            n |= 1; q += step >> 2
        pred = max(-32768, min(32767, pred - q if n & 8 else pred + q))
        idx = max(0, min(88, idx + INDEX[n & 7]))
        out[i] = n
        dec[i] = pred
    return out, dec, at_loop


def load(url):
    import build
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", build.fetch(url), "-ac", "1", "-ar", "44100", "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).astype(np.float64)


def tau_of(y, rate, t0, t1):
    """The time constant of y's dying away between two times, from its level in 10 ms windows."""
    w = max(8, int(rate * 0.01))
    a, b = int(t0 * rate) // w, int(t1 * rate) // w
    lv = np.array([np.sqrt(np.mean(y[i * w:(i + 1) * w] ** 2) + 1e-12) for i in range(a, max(b, a + 3))])
    t = (np.arange(len(lv)) + 0.5) * w / rate
    slope = np.polyfit(t, np.log(lv), 1)[0]
    return -1.0 / slope if slope < -1e-6 else 1e6


def make(name, what, opt, src):
    lib, url = src
    x = load(url)
    on = int(np.argmax(np.abs(x) > 0.04 * np.abs(x).max()))
    x = x[max(0, on - 44):]
    rate = opt["rate"]
    y = signal.resample_poly(x[:44100 * 3], rate, 44100, window=("kaiser", 9.0))
    A = int(rate * opt["ms"] / 1000)
    loop_ms = opt.get("loop_ms", 0)
    if loop_ms:
        L = int(rate * loop_ms / 1000)
        if "f0" in opt:                          # whole periods
            per = rate / opt["f0"]
            L = max(1, int(round(round(L / per) * per)))
        nat = tau_of(y, rate, opt["ms"] / 1000 * 0.5, (opt["ms"] + loop_ms) / 1000 * 3.0)
        nat = max(nat, (A + L) / rate / 2.0)
        y = y[:A + L + L] * np.exp(np.arange(A + L + L) / rate / nat)
        X = L if "f0" not in opt else max(8, L // 4)     # crossfade: the stretch's end into what came before its start
        X = min(X, A)
        f = np.linspace(0.0, 1.0, X, endpoint=False)
        z = y[:A + L].copy()
        z[A + L - X:] = z[A + L - X:] * np.sqrt(1.0 - f) + y[A - X:A] * np.sqrt(f) if "f0" not in opt else \
            z[A + L - X:] * (1.0 - f) + y[A - X:A] * f
        tau = opt["ring"] / 6.9
        loop = A
    else:
        nat = tau_of(y, rate, 0.012, opt["ms"] / 1000)
        nat = max(nat, A / rate / 2.8)          # (never lifted by more than 24 dB)
        z = y[:A] * np.exp(np.arange(A) / rate / nat)
        fade = max(8, A // 8)
        z[-fade:] *= np.linspace(1.0, 0.0, fade) ** 2
        tau = min(nat, 2.0)
        loop = A
    if len(z) & 1:
        z = z[:-1]
        if loop > len(z):
            loop = len(z)
    played = z * np.exp(-np.arange(len(z)) / rate / tau)
    level = np.sqrt(np.mean(played[:int(rate * 0.06)] ** 2))
    z = z / np.abs(z).max() * 0.92
    pcm = np.round(z * 32767).astype(np.int32)
    best = None
    for idx0 in range(20, 89, 4):               # the coder's first step: whichever follows the start best
        nib, dec, st = adpcm(pcm[:400], idx0)
        err = float(np.sum((dec - pcm[:400]) ** 2))
        if best is None or err < best[0]:
            best = (err, idx0)
    nib, dec, st = adpcm(pcm, best[1], loop if loop_ms else None)
    snr = 10 * np.log10(np.sum(pcm.astype(float) ** 2) / max(1.0, np.sum((dec - pcm).astype(float) ** 2)))
    data = (nib[0::2] | (nib[1::2] << 4)).astype(np.uint8)
    played = played / np.abs(played).max()
    gain = 0.2 / (np.sqrt(np.mean(played[:int(rate * 0.06)] ** 2)) + 1e-9)       # every hit about as loud as the next
    gain = float(np.clip(gain, 0.6, 2.2))
    return dict(name=name, what=what, lib=lib, url=url, rate=rate, n=len(z), loop=loop if loop_ms else len(z), tau=tau,
                idx0=best[1], lpred=st[0], lidx=st[1], gain=gain, data=data, dec=dec / 32767.0, snr=snr)


def c_bytes(b, per=32):
    return "\n".join("    " + ",".join(str(int(v)) for v in b[i:i + per]) + "," for i in range(0, len(b), per))


def main():
    zones, blob, off = [], [], 0
    for name, what, opt, src in sources.DRUMS:
        z = make(name, what, opt, src)
        z["off"] = off
        off += len(z["data"])
        blob.append(z["data"])
        zones.append(z)
        print(f"{name:10s} {z['rate']:6d} Hz {z['n']:5d} samples {len(z['data']):5d} B  loop {z['loop']:5d}  tau {z['tau']:.3f}  "
              f"gain {z['gain']:.2f}  snr {z['snr']:.1f} dB")
        if "--preview" in sys.argv:
            import build
            build.write_wav(os.path.join(PREVIEW, f"VIBE_{name}.wav"), z["dec"] * 0.9, z["rate"])
    blob = np.concatenate(blob)
    with open(os.path.join(ROOT, "core", "vibe_data.h"), "w") as f:
        f.write("/* h1 - VIBE's hits. Written by tools/samples/drums.py: do not edit. */\n#pragma once\n#include <stdint.h>\n\nenum {\n")
        for z in zones:
            f.write(f"    VZ_{z['name']},{' ' * (11 - len(z['name']))}/* {z['what']} */\n")
        f.write("    VZ_N\n};\n\n"
                "typedef struct VibeZone {\n"
                "    uint32_t off;                /* into vibe_data: two samples to a byte, the low four bits first */\n"
                "    uint16_t len, loop;          /* samples; a ringing hit goes round from loop to the end, another has loop == len */\n"
                "    uint16_t rate;               /* Hz it is stored at */\n"
                "    int16_t lpred;               /* the decoder as it was at loop, to go round with */\n"
                "    uint8_t idx0, lidx;          /* its step at the start, and at loop */\n"
                "    float tau;                   /* seconds: how it dies away, which was taken out of what is stored */\n"
                "    float gain;                  /* makes every hit as loud as the next */\n"
                "} VibeZone;\n\n"
                "extern const VibeZone vibe_zones[VZ_N];\nextern const uint8_t vibe_data[];\nextern const uint16_t vibe_steps[89];\n")
    with open(os.path.join(ROOT, "core", "vibe_data.c"), "w") as f:
        f.write("/* h1 - VIBE's hits, cut down. Written by tools/samples/drums.py from sources.DRUMS in\n"
                " * tools/samples/sources.py: do not edit. Where each came from and its licence: docs/SAMPLES.md. */\n"
                "#include \"vibe_data.h\"\n\n")
        f.write("const uint16_t vibe_steps[89] = {\n")
        for i in range(0, 89, 16):
            f.write("    " + " ".join(f"{v}," for v in STEPS[i:i + 16]) + "\n")
        f.write("};\n\n/* offset, length, loop start, rate, the decoder at loop, its first step and its step at loop, tau, gain */\n")
        f.write("const VibeZone vibe_zones[VZ_N] = {\n")
        for z in zones:
            f.write(f"    {{{z['off']}u, {z['n']}, {z['loop']}, {z['rate']}, {z['lpred']}, {z['idx0']}, {z['lidx']}, "
                    f"{z['tau']:.4f}f, {z['gain']:.3f}f}},    /* {z['name']} */\n")
        f.write(f"}};\n\nconst uint8_t vibe_data[{len(blob)}] = {{\n")
        f.write(c_bytes(blob))
        f.write("\n};\n")
    print(f"{len(zones)} hits, {len(blob)} bytes")


def doc():
    """docs/SAMPLES.md's part on these (build.py asks for it)."""
    total = 0
    rows = []
    for name, what, opt, src in sources.DRUMS:
        n = int(opt["rate"] * (opt["ms"] + opt.get("loop_ms", 0)) / 1000) // 2
        total += n
        fn = urllib.parse.unquote(src[1].rsplit("/", 1)[1])
        rows.append(f"| `VZ_{name}` | {what} | {opt['rate'] / 1000:g} kHz, {opt['ms'] + opt.get('loop_ms', 0)} ms | "
                    f"[{fn}]({urllib.parse.quote(src[1], safe=':/')}) |\n")
    return ("\n## VIBE's hits\n\n"
            "VIBE's kits are picked from one pool of recorded hits, each of them a single stroke from the Versilian\n"
            "Community Sample Library (CC0 1.0, credited above). `tools/samples/drums.py` cuts them from\n"
            f"`sources.DRUMS` into `core/vibe_data.c`: {len(rows)} hits, about {total:,} bytes, 4-bit ADPCM at the rate\n"
            "each one's top allows. A ringing hit keeps its start and a short stretch that repeats.\n\n"
            "| Hit | What it is | Kept | Recording |\n|---|---|---|---|\n" + "".join(rows))


if __name__ == "__main__":
    main()
