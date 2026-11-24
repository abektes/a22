#!/usr/bin/env python3
"""Renders A22's card picture for The Firmware Hub: six of its screens round the name.

    python3 tools/hub_card.py       # writes docs/img/a22/hub-card.webp (fmhub.json's "image") and its .json

Each screen is the core's own 240 x 240 frame at 1:1 (tools/hub_card_shot.c, built against core/*.c with cc), taken
while it plays and after every name box has cleared: FM-4 KEYS, STACK CLOUD and SKINS STUDIO above, CYCLE STRING,
PERC HANDPAN and the four-track tape with all four tracks recorded below. The name is in Barlow Semi Condensed
(site/fonts), in fmhub.json's colour. The Hub wants a square of 240 to 1024 px, 300 KB at most; lossless WebP
keeps every pixel and comes to about 42 KB. Needs cc and Pillow. Pick other screens in SHOTS.
"""
import json
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "docs", "img", "a22", "hub-card.webp")
FONTS = os.path.join(ROOT, "site", "fonts")
SIZE = 1024
SHOTS = [["0", "2", "13"], ["12", "9", "tape"]]   # engines by number (FM-4 0 ... SKINS 13), or the tape's page
XS, YS = [64, 392, 720], [56, 728]
TAG = "FM WORKSTATION FOR THE FM-1"
LINE = "FOURTEEN ENGINES  ·  EIGHT EFFECTS  ·  FOUR-TRACK TAPE"


def colour():
    c = json.load(open(os.path.join(ROOT, "fmhub.json")))["color"].lstrip("#")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))


def spaced(d, y, text, font, fill, track):
    ws = [d.textlength(ch, font=font) for ch in text]
    x = (SIZE - sum(ws) - track * (len(text) - 1)) / 2
    for ch, w in zip(text, ws):
        d.text((x, y), ch, font=font, fill=fill)
        x += w + track


def main():
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "shot")
        core = sorted(os.path.join(ROOT, "core", f) for f in os.listdir(os.path.join(ROOT, "core")) if f.endswith(".c"))
        subprocess.run(["cc", "-std=c99", "-O2", "-w", "-I", os.path.join(ROOT, "core"), *core,
                        os.path.join(ROOT, "tools", "hub_card_shot.c"), "-o", exe, "-lm"], check=True)
        card = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
        for r, row in enumerate(SHOTS):
            for c, what in enumerate(row):
                ppm = os.path.join(tmp, f"{what}.ppm")
                subprocess.run([exe, what, ppm], check=True)
                card.paste(Image.open(ppm).convert("RGB"), (XS[c], YS[r]))
    d = ImageDraw.Draw(card)
    orange = colour()
    big = ImageFont.truetype(os.path.join(FONTS, "BarlowSemiCondensed-Bold.ttf"), 250)
    bb = d.textbbox((0, 0), "A22", font=big)
    d.text(((SIZE - (bb[2] - bb[0])) / 2 - bb[0], 340 - bb[1]), "A22", font=big, fill=orange)
    medium = os.path.join(FONTS, "BarlowSemiCondensed-Medium.ttf")
    spaced(d, 614, TAG, ImageFont.truetype(medium, 30), orange, 3.2)
    spaced(d, 658, LINE, ImageFont.truetype(medium, 19), (150, 156, 164), 2.4)
    card.save(OUT, "WEBP", lossless=True, method=6)
    with open(OUT + ".json", "w") as f:
        json.dump({"prompt": "origin: The Firmware Hub card for A22, composed by tools/hub_card.py: six of its "
                   "screens rendered by the core while playing (FM-4, STACK, SKINS, CYCLE, PERC, the four-track tape), "
                   "the name in Barlow Semi Condensed. Not generated."}, f, indent=2)
    print(f"wrote {OUT}, {os.path.getsize(OUT)} bytes")


if __name__ == "__main__":
    main()
