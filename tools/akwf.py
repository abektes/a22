#!/usr/bin/env python3
"""Fetches the 60 single cycles CYCLE's four banks are made from, and writes their harmonics to
tools/akwf_banks.json (which tools/gen_wavetables.py reads) and the credits to docs/WAVETABLES.md.

    python3 tools/akwf.py

The cycles are Adventure Kid Waveforms (AKWF) by Kristoffer Ekstrand, CC0 1.0 in his own repository
(LICENSE.md), read at a fixed commit. Each is 600 samples; only its first 48 harmonics are kept (WAVE's
format: what the cycles have above that is under 5% of their energy, 0% for most).

How the 15 of a bank were picked (once, by tools in a scratch directory; the list below is the result):
cycles whose fundamental holds under 12% of their energy were left out (they sound an octave up), then
near-copies (magnitude spectra closer than 0.995), then 15 spaced evenly from the darkest to the brightest
(spectral centroid). By ear is still to come.

Phases: unrelated recordings have unrelated phases, and a morph between two of them partly cancels (2 to
5 dB quieter halfway). So every wave of a bank keeps its own harmonic amplitudes and takes the phases of
the bank's middle wave (place 7, shifted so its fundamental is in sine phase): halfway is then within
about 1 dB of the waves either side, and no wave is spikier than the recordings were.
"""
import hashlib
import json
import os
import subprocess
import wave

import numpy as np

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CACHE = os.environ.get("A22_AKWF_CACHE", os.path.join(ROOT, "build", "akwf-src"))
REPO = "https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE"
COMMIT = "8de90bf94376670947369e69de0af6b9fbd19286"
RAW = f"https://raw.githubusercontent.com/KristofferKarlAxelEkstrand/AKWF-FREE/{COMMIT}/AKWF/"
H = 48

BANKS = [
    ("STRING", "cello, violin and the string box, dark to bright", [
        "AKWF_stringbox/AKWF_cheeze_0001", "AKWF_stringbox/AKWF_cheeze_0002", "AKWF_stringbox/AKWF_cheeze_0004",
        "AKWF_stringbox/AKWF_cheeze_0005", "AKWF_violin/AKWF_violin_0009", "AKWF_cello/AKWF_cello_0006",
        "AKWF_cello/AKWF_cello_0019", "AKWF_cello/AKWF_cello_0005", "AKWF_cello/AKWF_cello_0017",
        "AKWF_violin/AKWF_violin_0008", "AKWF_violin/AKWF_violin_0013", "AKWF_cello/AKWF_cello_0012",
        "AKWF_cello/AKWF_cello_0018", "AKWF_cello/AKWF_cello_0015", "AKWF_cello/AKWF_cello_0007"]),
    ("REED", "clarinet, flute, alto sax and oboe, dark to bright", [
        "AKWF_clarinett/AKWF_clarinett_0019", "AKWF_flute/AKWF_flute_0015", "AKWF_clarinett/AKWF_clarinett_0007",
        "AKWF_altosax/AKWF_altosax_0022", "AKWF_altosax/AKWF_altosax_0021", "AKWF_altosax/AKWF_altosax_0014",
        "AKWF_altosax/AKWF_altosax_0020", "AKWF_altosax/AKWF_altosax_0013", "AKWF_oboe/AKWF_oboe_0005",
        "AKWF_clarinett/AKWF_clarinett_0023", "AKWF_altosax/AKWF_altosax_0011", "AKWF_altosax/AKWF_altosax_0025",
        "AKWF_altosax/AKWF_altosax_0010", "AKWF_clarinett/AKWF_clarinett_0020", "AKWF_altosax/AKWF_altosax_0005"]),
    ("KEYS", "piano and electric piano, dark to bright", [
        "AKWF_piano/AKWF_piano_0010", "AKWF_piano/AKWF_piano_0015", "AKWF_epiano/AKWF_epiano_0012",
        "AKWF_epiano/AKWF_epiano_0010", "AKWF_epiano/AKWF_epiano_0040", "AKWF_epiano/AKWF_epiano_0003",
        "AKWF_epiano/AKWF_epiano_0052", "AKWF_piano/AKWF_piano_0028", "AKWF_epiano/AKWF_epiano_0021",
        "AKWF_epiano/AKWF_epiano_0049", "AKWF_epiano/AKWF_epiano_0032", "AKWF_piano/AKWF_piano_0018",
        "AKWF_epiano/AKWF_epiano_0016", "AKWF_piano/AKWF_piano_0002", "AKWF_epiano/AKWF_epiano_0008"]),
    ("VOICE", "human voice, dark to bright", [
        "AKWF_hvoice/AKWF_hvoice_0007", "AKWF_hvoice/AKWF_hvoice_0024", "AKWF_hvoice/AKWF_hvoice_0087",
        "AKWF_hvoice/AKWF_hvoice_0053", "AKWF_hvoice/AKWF_hvoice_0004", "AKWF_hvoice/AKWF_hvoice_0026",
        "AKWF_hvoice/AKWF_hvoice_0080", "AKWF_hvoice/AKWF_hvoice_0039", "AKWF_hvoice/AKWF_hvoice_0006",
        "AKWF_hvoice/AKWF_hvoice_0020", "AKWF_hvoice/AKWF_hvoice_0078", "AKWF_hvoice/AKWF_hvoice_0092",
        "AKWF_hvoice/AKWF_hvoice_0102", "AKWF_hvoice/AKWF_hvoice_0032", "AKWF_hvoice/AKWF_hvoice_0050"]),
]


def fetch(name):
    url = RAW + name + ".wav"
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, hashlib.sha1(url.encode()).hexdigest()[:16] + ".wav")
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        subprocess.run(["curl", "-sS", "-f", "-L", "-m", "60", "-o", path, url], check=True)
    return path


def harmonics(path):
    """the cycle's first H harmonics, complex (rfft, scaled to amplitudes), DC left out"""
    with wave.open(path) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, path
        n = w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return np.fft.rfft(x)[1:H + 1] / n * 2.0


def main():
    out = {"source": REPO, "commit": COMMIT, "licence": "CC0 1.0", "banks": []}
    md = ["# CYCLE's wavetables", "",
          "CYCLE's four banks are made from single-cycle waves of [Adventure Kid Waveforms (AKWF)]"
          f"({REPO}) by Kristoffer Ekstrand, CC0 1.0 (public domain dedication: free to use in anything, with no",
          "credit owed; credited here all the same). They are read at commit "
          f"`{COMMIT[:7]}`.", "",
          "This file and `tools/akwf_banks.json` are written by `tools/akwf.py`; `tools/gen_wavetables.py` makes",
          "`core/wave_akwf.c` from the JSON. Each wave keeps its first 48 harmonics and takes its bank's shared",
          "phases (see `tools/akwf.py` for why).", ""]
    h = np.arange(1, H + 1)
    for name, what, files in BANKS:
        assert len(files) == 15, name
        X = [harmonics(fetch(f)) for f in files]
        mid = X[7]
        tau = (np.angle(mid[0]) + np.pi / 2) / (2 * np.pi)          # the middle wave, fundamental in sine phase
        ph = np.angle(mid * np.exp(-1j * 2 * np.pi * h * tau))
        waves = []
        for x in X:
            y = np.abs(x) * np.exp(1j * ph)
            # (sine, cosine) amplitudes as gen_wavetables.py's spectra: x(t) = sum Re(y) cos - Im(y) sin
            waves.append([[round(float(-np.imag(v)), 7), round(float(np.real(v)), 7)] for v in y])
        out["banks"].append({"name": name, "files": files, "waves": waves})
        md += [f"## {name}: {what}", "", "| Place | AKWF file |", "|---|---|"]
        md += [f"| {i} | `{f}.wav` |" for i, f in enumerate(files)]
        md.append("")
    with open(os.path.join(ROOT, "tools", "akwf_banks.json"), "w") as f:
        json.dump(out, f, separators=(",", ":"))
        f.write("\n")
    with open(os.path.join(ROOT, "docs", "WAVETABLES.md"), "w") as f:
        f.write("\n".join(md))
    print(f"wrote tools/akwf_banks.json and docs/WAVETABLES.md: {len(BANKS)} banks of 15 from AKWF {COMMIT[:7]}")


if __name__ == "__main__":
    main()
