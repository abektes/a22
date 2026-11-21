---
version: 1
slug: "tools-build-site-py"
primary_target: "tools/build_site.py"
related_targets: []
---

# A22 website (front page + manual)

Scope: `tools/build_site.py` writes `index.html` (front page, Persuade) and `manual.html` (the manual, Read), plus the simulator at `sim/`.

Audience and job: FM-1 owners deciding whether to install (front page: understand, trust, download or try the simulator); A22 players looking something up (manual: an engine's knobs and voices, a hotkey, the tape). User feedback on the previous site: crowded, looks AI-generated. Signed "A22"; GitHub and Buy Me a Coffee present but quiet; no h1 FAQ or credits section.

## Direction contract

THESIS: A22 is a voice bank: ninety-six voices in twelve banks, presented the way a synth's ROM catalogue lists them, one bank open at a time. It refuses the dark product page with neon accents and a wall of feature cards.

OWN-WORLD: cool coated catalogue paper (#dfe3e8 ground, #f3f5f7 sheets), black print, and twelve bank tabs printed in the engines' own screen inks (the only colour). Ruled voice tables with bank-numbered voices (A1–A8) under printed column heads, the open bank's ink as the sheet's header band, and one face throughout: the FM-1's own Barlow Semi Condensed (Bold for the wordmark and headings, Medium for text), tabular numerals. Amended after the finish review: the wide display grotesk first named here was dropped, because the device's own lettering is the product's truest type; the user confirmed one face (Barlow only) on 2026-11-21. Real 240×240 screen captures sit in black screen windows. Secondary facts live in small-print margins.

STORY: a visitor sees what A22 is in one line, sees the engines immediately as banks they can open, understands it is free and reversible, and downloads it or plays it in the browser. Players use the manual's bank index to jump to an engine or a control.

FIRST VIEWPORT: left edge, the twelve bank tabs A–L stacked full height, each in its engine's ink. Main area, top: "A22" large in Barlow Semi Condensed Bold with one line beneath ("Firmware for the M-VAVE FM-1. Twelve banks, ninety-six voices.") and the primary action Download 0.1.0 beside "Play it in your browser". Below it the open bank: its letter and engine name, its screen capture in a black window, and its eight voices as a ruled table. Clicking a tab swaps the open bank in place; nothing else moves.

FORM: voice-ROM catalogue, my top-ranked grounded candidate (1 of 7), chosen by the user over the assigned cassette insert card. seed:d61f8d9f976973d9. Kept disciplines: only the open bank shows its details (streaming wall); secondary facts in the margins (treasure map); dense table passages alternate with quiet ones (multiplane); the manual points at the FM-1's real controls (zine); the tabs' inks are the only colour, used with total commitment (indoor sun).

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
