# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

- **Deciders:** owners of an M-VAVE FM-1 who have heard of custom firmware and are weighing whether to install
  A22. They want to know what it sounds and looks like, whether it is safe, and how to install it.
- **Players:** people already running A22 who come back to look up an engine's knobs and algorithms, a hotkey,
  or how the tape, step sequencer, Riff and Shake work.

The website serves both, split: a short front page for deciders and a separate manual for players.

## Product Purpose

A22 is free, open-source custom firmware for the M-VAVE FM-1 that turns it into an FM workstation. The website's
job is to let a decider understand A22 and install it with confidence, and to give players a manual they can
find things in. Success: a decider reaches the download or the in-browser simulator; a player finds a control
or an engine's page in seconds.

## Positioning

Lead with the sound engines: fourteen engines (FM-4, ELECTRIC, STACK, ANALOG, SHAPER, WIRE, HAZE, MONO, KIT,
PERC, WAVE, CHIP, CYCLE, SKINS), eight algorithms each (112 in all), four live knobs each. The four-track tape,
effects, mixer, sequencers and USB audio/MIDI support that, and the animated colour pictures show it.

## Operating Context

- The FM-1: a small synth with a 240 × 240 screen, 27 keys, seven encoders (PRESETS, ALGORITHM, four knobs,
  SELECT), MASTER and fourteen buttons. Knobs 1–4 are coloured blue, green, white, orange on the device.
- Installing: `base/tools/fm1_install.py` from a terminal, works from stock V15 and other custom firmwares.
- Releases on GitHub (`a22-X.Y.Z.fwsc`); a browser simulator (`web/` + `dist/a22.rfm1`) plays the real core.
- The site is built by `tools/build_site.py` and published to GitHub Pages (https://abektes.github.io/a22/).

## Capabilities and Constraints

- Static site, no framework: Python generator writes HTML; GitHub Pages hosting; the simulator page is the
  existing `web/index.html`, served at `sim/`.
- Facts live in the README and the engine sources (`core/inst_*.c`); the site must not claim more than they do.
- HAZE's algorithms have no per-algorithm descriptions in the code; list them by name only.
- Listed on The Firmware Hub since 0.4.0, which offers each new release a few hours after it is out. A22 is
  still early (installed and run on one FM-1).

## Brand Commitments

- Signed as **A22**. No personal name.
- GitHub and Buy Me a Coffee (https://buymeacoffee.com/abektes) links are present but never dominant.
- No credits section or "related to h1" FAQ on the site: the repository carries the credits. A small licence
  link is enough.
- Device vocabulary in capitals as on the FM-1 (PRESETS, ALGORITHM, REC, CLEAR ALL?).

## Evidence on Hand

- Real 240 × 240 screen captures of all fourteen engines: `docs/img/a22/*.png`; page captures `fx.png`, `mix.png`,
  `tape.png`.
- Measured on an FM-1 with 0.5.0 (2026-11-24): 42 FPS average over its 112 algorithms with an eight-note chord and PLATE
  (audio 50% average, 75% at its peak); a 30-minute soak with a two-track loop playing (STACK and KIT), steady at 24 FPS
  and 60% audio, no crash. (0.4.0: 41 FPS over 96.)
- No testimonials, users counts, press or reviews exist: never invent them.

## Product Principles

1. Sound first: every surface points back to the engines and what they do.
2. Calm, not crowded: one idea per view; the manual is for depth, the front page is for deciding.
3. Truthful: only what the code and the device have shown.
4. The FM-1 is the reference: its names, its colours, its screen.
