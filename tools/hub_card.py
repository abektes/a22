#!/usr/bin/env python3
"""Renders A22's card picture for The Firmware Hub: six of its screens round the name.

    python3 tools/hub_card.py       # writes docs/img/a22/hub-card.webp (fmhub.json's "image") and its .json

Each screen is the core's own 240 x 240 frame at 1:1 (tools/hub_card_shot.c, built against core/*.c with cc), taken
while it plays and after every name box has cleared: FM-4 KEYS, STACK CLOUD and SKINS STUDIO above, CYCLE STRING,
PERC HANDPAN and the four-track tape with all four tracks recorded below. The name is drawn in the firmware's own
stroke font (core/gfx.c's glyphs, read from the source, round-ended lines 0.13 of the cap height as the UI draws
large text), with the A as a closed triangle; the two lines under it are Barlow Semi Condensed (site/fonts). All in
fmhub.json's colour. The Hub wants a square of 240 to 1024 px, 300 KB at most; lossless WebP
keeps every pixel and comes to about 42 KB. Needs cc and Pillow. Pick other screens in SHOTS.
"""
import json
import os
import re
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
NAME_CAP, NAME_GAP, LINE_STEP = 160, 34, 44   # the name's cap height, the room under it, the tag line to the next
LINE = "FOURTEEN ENGINES  ·  EIGHT EFFECTS  ·  FOUR-TRACK TAPE"


def colour():
    c = json.load(open(os.path.join(ROOT, "fmhub.json")))["color"].lstrip("#")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))


def stroke_font():
    """core/gfx.c's glyphs: {name: [[(x, y), ...], ...]}, each a polyline on the 8 x 12 grid."""
    src = open(os.path.join(ROOT, "core", "gfx.c")).read()
    out = {}
    for name, body in re.findall(r"static const uint8_t F_(\w+)\[\] = \{(.*?)\};", src):
        lines, cur = [], []
        for tok in re.findall(r"P\((\d+),(\d+)\)|(UP|END)", body):
            if tok[2]:
                if cur:
                    lines.append(cur)
                cur = []
            else:
                cur.append((int(tok[0]), int(tok[1])))
        out[name] = lines
    return out


def stroke_name(card, text, top, cap, fill):
    """text in the stroke font, centred, its caps `cap` px tall from `top`; the A a closed triangle."""
    font = stroke_font()
    font["A"] = [[(0, 12), (4, 0), (8, 12), (0, 12)]]
    ss = 4                                          # drawn 4x over and taken down, for smooth edges
    u, w = cap / 12 * ss, cap * 0.13 * ss            # gfx.c: a grid unit, and the UI's width for large text
    adv, gap = cap * 8 / 12, cap * 3.4 / 12          # gfx.c: GLYPH_W, GLYPH_GAP
    width = len(text) * (adv + gap) - gap
    pad = int(w)
    mask = Image.new("L", (int((width + 2 * pad / ss) * ss) + 1, int(cap * ss) + 2 * pad), 0)
    d = ImageDraw.Draw(mask)
    for i, ch in enumerate(text):
        ox = pad + i * (adv + gap) * ss
        for line in font[ch]:
            pts = [(ox + x * u, pad + y * u) for x, y in line]
            for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
                d.line([(x0, y0), (x1, y1)], fill=255, width=round(w))
            for x, y in pts:                        # round ends and joins, as gfx.c's line_seg
                d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=255)
    mask = mask.resize((mask.width // ss, mask.height // ss), Image.LANCZOS)
    x, y = round((SIZE - mask.width) / 2), round(top - pad / ss)
    card.paste(Image.new("RGB", mask.size, fill), (x, y), mask)


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
    medium = os.path.join(FONTS, "BarlowSemiCondensed-Medium.ttf")
    tag_font, line_font = ImageFont.truetype(medium, 30), ImageFont.truetype(medium, 19)
    tag_top, tag_bot = d.textbbox((0, 0), "F", font=tag_font)[1::2]     # where the caps sit under a text's y
    line_bot = d.textbbox((0, 0), "F", font=line_font)[3]
    # The name, then the tag close under it, then the line: the three as one block, centred in the gap between the
    # rows of screens, so there is as much room above it as below.
    gap = YS[1] - (YS[0] + 240)
    block = NAME_CAP + NAME_GAP + (tag_bot - tag_top) + (LINE_STEP - tag_bot + line_bot)
    top = YS[0] + 240 + (gap - block) / 2
    stroke_name(card, "A22", round(top), NAME_CAP, orange)
    tag_y = top + NAME_CAP + NAME_GAP - tag_top
    spaced(d, tag_y, TAG, tag_font, orange, 3.2)
    spaced(d, tag_y + LINE_STEP, LINE, line_font, (150, 156, 164), 2.4)
    card.save(OUT, "WEBP", lossless=True, method=6)
    with open(OUT + ".json", "w") as f:
        json.dump({"prompt": "origin: The Firmware Hub card for A22, composed by tools/hub_card.py: six of its "
                   "screens rendered by the core while playing (FM-4, STACK, SKINS, CYCLE, PERC, the four-track tape), "
                   "the name in the firmware's stroke font with the A as a triangle. Not generated."}, f, indent=2)
    print(f"wrote {OUT}, {os.path.getsize(OUT)} bytes")


if __name__ == "__main__":
    main()
