#!/usr/bin/env python3
"""Renders h1's card picture for The Firmware Hub: every instrument's screen in one 4 x 4 grid.

    python3 tools/hub_card.py      # builds build/h1host first, writes docs/img/hub-card.webp

Each tile is the host renderer's 240 x 240 frame at 1:1, taken mid-phrase while a short tune
plays, so the pictures are the real firmware. With fewer than sixteen instruments the spare
cells at the end hold the name. The Hub wants a square of 240 to 1024 px, 300 KB at most
(fmhub.json's "image"); lossless WebP keeps every pixel and stays well under that.
Needs Pillow and the Inter Display Black font for the name.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guide_media as g  # noqa: E402

OUT = os.path.join(g.ROOT, "docs", "img", "hub-card.webp")
FONT = "/usr/share/fonts/opentype/inter/InterDisplay-Black.otf"
ACCENT = (0xFF, 0xB0, 0x8A)  # fmhub.json "color"
SIZE, MARGIN, GAP, TILE = 1024, 14, 12, 240
# pictures that read better later in the phrase, once their trails have built up
LATE = {"ORBIT", "INK"}


def shot(tmp, i, name):
    at = 2950 if name in LATE else 1100
    words, t = ["inst", str(i), "run", "2000"], 0
    for tt, k, d in sorted(g.PHRASE):
        if tt > at:
            break
        words += ["run", str(tt - t), "key", str(k), str(d)]
        t = tt
    words += ["run", str(at - t + 50 if name in LATE else 120), "shot", name.lower()]
    g.run(tmp, words)
    return Image.open(os.path.join(tmp, name.lower() + ".ppm")).convert("RGB")


def cell(i):
    return MARGIN + (i % 4) * (TILE + GAP), MARGIN + (i // 4) * (TILE + GAP)


def main():
    subprocess.run([os.path.join(g.ROOT, "tools", "build_host.sh")], check=True, stdout=subprocess.DEVNULL)
    names = g.instruments()[:16]
    card = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    with tempfile.TemporaryDirectory() as tmp:
        for i, name in enumerate(names):
            card.paste(shot(tmp, i, name), cell(i))
    if len(names) < 16:
        x, y = cell(len(names))
        w = (16 - len(names)) * (TILE + GAP) - GAP
        draw = ImageDraw.Draw(card)
        font = ImageFont.truetype(FONT, 190)
        l, t, r, b = draw.textbbox((0, 0), "h1", font=font)
        draw.text((x + (w - (r - l)) / 2 - l, y + (TILE - (b - t)) / 2 - t), "h1", font=font, fill=ACCENT)
    card.save(OUT, lossless=True, quality=100, method=6)
    print("wrote", OUT, os.path.getsize(OUT), "bytes:", ", ".join(names))


if __name__ == "__main__":
    main()
