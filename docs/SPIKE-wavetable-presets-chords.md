# Spike: a wavetable engine, factory presets and chord memory

Research before implementation, 2026-11-20. Since then chord memory (section 3) has been built: the
README's "Chord memory" says how it works, `tests/chords.c` tests it. The capture gesture is EDIT + GLO,
the settings are GLO's CHORD subpage, and MIDI in is not made into chords. The rest is not built yet. The prototype and its
numbers come from a throwaway C file outside the repository (described in section 1).

The budget these ideas have to fit, from the CI build of a22: a 252 KB firmware image in 1 MB of flash,
437 KB of 475 KB SRAM used (about 37 KB free, 8 KB of it kept spare), and a CPU cost not yet measured on
an FM-1. So: flash is plentiful, RAM is tight, and CPU must be measured on hardware before a new engine ships.

## 1. Wavetable engine

### Recommendation: port Plaits' wavetable engine (MIT)

Mutable Instruments' Plaits has an "8x8x3 wave terrain" engine (`plaits/dsp/engine/wavetable_engine.cc`).
Its code is MIT: the eurorack repository's README says "Code (STM32F projects): MIT license", and every
Plaits source file carries the MIT notice with Emilie Gillet's copyright. a22 is MIT, so it can be ported
with that notice kept.

- **The tables:** 192 single-cycle waves, 128 samples each (plus 4 for interpolation), int16, about 50 KB of
  flash. They are made by `plaits/resources/wavetables.py`, also MIT, in three banks of 64:
  1. mild and additive: sines, combs, drawbar organs;
  2. formant-ish: tri/saw blends, bursts, formants, pulses;
  3. Shruthi/Ambika/Braids waves: male voice, choir, digital, drone, metal and others, read from Braids'
     `waves.bin`, which is Emilie Gillet's own MIT work.

  A fourth bank is a permutation of the first three. Nothing in it comes from another synth's ROM.
- **Anti-aliasing that suits a small chip:** the tables are stored integrated (a running sum). The voice
  reads them and differentiates the result, which is first-order "integrated wavetable synthesis"
  (Franck and Välimäki, DAFx-12). There are no per-octave mipmaps (they would multiply the flash) and no
  oversampling (it would multiply the CPU).
- **The controls:** three axes (X, Y and Z, which crossfades between banks), each smoothed. Turning the
  third axis past halfway snaps the morphing to whole waves, giving the stepped, PPG-style sound.

### Measured: the CPU cost, on this Mac

A prototype voice doing the same per-sample work as Plaits' engine (three smoothed axes, eight Hermite
table reads, a trilinear blend, the differentiator), 8 voices, 1 second of audio:

| | ms per second of audio |
|---|---|
| Wave terrain, full (8x8x3, Hermite reads) | 8.5–10 |
| Wave terrain, lite (8x8 only, linear reads) | 2.3–2.8 |
| a22 FM-4 KEYS, the heaviest engine now (whole engine, same machine) | 34 |

The prototype is the oscillator alone. a22's engines add an envelope, filter and mixing, so a WAVE engine
would land at roughly a third to a half of FM-4's worst case. The FM-1 is much slower than this Mac, so
read these as ratios, not device numbers. RAM per voice is a few floats; the engine needs a 64-pointer
wave map. Negligible.

### Extra banks: AKWF (CC0)

Adventure Kid Waveforms' official repository (KristofferKarlAxelEkstrand/AKWF-FREE, by its author) is
CC0-1.0: thousands of single cycles, 600 samples, 16-bit. Third-party pages call it CC-BY 3.0, but the
author's own LICENSE.md says CC0. To use them: resample each to 128 samples and integrate it the same way
offline, in a22's own tool in `tools/`. That is 264 bytes a wave, so a 64-wave bank costs about 17 KB.
Extra banks could cover sampled-instrument cycles (pianos, guitars, voices) that the Plaits banks lack.

### Rejected

- **Surge XT and Vital tables and code:** GPL-3.0. Using them would make a22 GPL; it is MIT and ships
  MPL-2.0 fm1-base.
- **WaveEdit (E352/E370):** the code is GPL-3.0 and no licence was found for its bundled banks.
- **PPG Wave tables:** Waldorf's. Even Surge keeps PPG-derived material out of its main library.

### Fitting it into a22

- **An 11th engine, "WAVE".** `NINST`/`NSLOT` go from 10 to 11. The state grows by one `InstParams` and
  one `knob[8][4]` (about 60 bytes, well under the 3840-byte limit), so `H1_VERSION` is bumped and saves
  from 0.1.0 are refused, as a22 does now. `PART` packing stays under 255 (11 × 8 = 88).
- **Knobs:** K1 X, K2 Y, K3 Z (bank blend; past halfway steps between whole waves), K4 a filter or a
  "lo-fi" control.
- **Algorithms:** eight starting places on the terrain, or eight bank layouts (ADDITIVE, FORMANT, VOX,
  DIGI, ORGAN, plus AKWF banks).
- **Picture:** a 3-D stack of the current waves, drawn with the existing vector primitives.
- **Effort:** about 300 lines of C99 ported from C++ (no libc, no `stmlib`), the table generator rewritten
  in `tools/`, and tests in the style of `tests/basics.c` (limiter, two cores bit-exact, audible on every
  algorithm).

## 2. Factory presets

### What a22 has now

- **80 "algorithm" sounds:** 8 per engine, each with its own default knobs.
- **8 user snapshots:** SAVE + a white key loads one (saves it if empty), and SAVE + EDIT + the key
  overwrites it. A snapshot (`Snap`, 28 bytes) holds the engine, algorithm, four knobs, ENV, mod ENV, LFO
  and the FX type and its four settings.
- **Not in a snapshot:** arpeggiator settings, KEYS (scale, glide), the mixer, the tape.

### Proposal

- **The data:** a `FactoryPatch` table in flash: a `Snap` plus an 8-character name, about 36 bytes. 10
  engines × 8–11 patches is 3–4 KB of flash and no RAM.
- **The gesture: SAVE + a black key** loads factory patch 1–11 of the engine in hand and shows its name.
  The FM-1 has 11 black keys (27 keys, F3..G5), and SAVE + white keys already means user snapshots, so
  "hold SAVE, press a key" stays the one place sounds live. A factory patch is only ever loaded, never
  overwritten; SAVE + EDIT + a white key keeps an edited one. It needs no menu.
- **A later option:** a LIB page that browses every patch by name with the PRESETS encoder, for when one
  engine has more than 11.

### What it needs besides code

- **Sound design** is the real work: about 100 patches that sound finished.
- **An audition tool:** a host tool that renders each patch to a WAV (port/host already writes WAVs)
  so presets can be designed and reviewed without flashing.
- **A decision:** whether patches also set the arpeggiator and KEYS. That decides whether `Snap` grows
  for user snapshots too.

## 3. Chord memory

### How others do it

- **Korg Polysix (and Kronos' chord mode):** hold notes, press CHORD MEMORY. Afterwards one key plays the
  stored shape, with the key as its lowest note, and it is kept until a new chord is stored. Up to 6 notes.
- **Arturia MicroFreak (firmware 2.0):** set a chord shape, then replay it transposed with one key.
  Chromatic: it does not follow the scale.
- **Native Instruments Kontrol S MK3:** chord mode can generate the chord from the scale, so one finger
  plays diatonic triads. a22 already has a root and eight scales (`SCALE_MASK` in `core/synth.c`), so
  it can do this too.

### Proposal: three modes

- **MEMORY:** Polysix-style. A captured shape of up to 6 notes, stored as intervals from its lowest note
  and transposed chromatically.
- **SCALE:** one-finger diatonic chords from the KEYS root and scale: triads, 7ths or sus, built by
  stacking scale degrees.
- **SHAPE:** a few fixed chords: MAJ, MIN, SUS4, 7, MIN7, POWER, OCTAVE.

Settings: VOICING (close, open, drop-2) and STRUM (0–60 ms between notes).

### Where it goes in the code

Every key and MIDI note reaches `eff_note()` through the `EV_NOTE_ON` / `EV_NOTE_OFF` handlers in
`core/synth.c`. Expanding a note into its chord there, before `eff_note()`, means the tape, step
recording, the arpeggiator (it arpeggiates the chord), Riff and MIDI out all receive the chord with no
changes of their own. Things to get right:

- **The note-off:** releasing a key must release the chord it started, even if the chord setting changed
  while it was held. Keep the shape used per held note (27 keys plus MIDI, a few bytes each).
- **Latch (KEYS HOLD):** latched notes are chords too.
- **Voices:** a22 has 8, so a 4-note chord leaves room for 2 held chords. MONO is mono: it plays the
  chord's lowest note.
- **Strum** needs a small queue of delayed note-ons (a few bytes).
- **The tape** records the chord's notes, not the key. It is simpler, and what was recorded plays back as
  heard even if the chord setting later changes.
- **Cost:** no CPU, and about 16 bytes of state (version bump). About 150 lines plus a test.

### To decide

- **The capture gesture for MEMORY:** for example hold the notes and press EDIT + ENV, a combination that
  is free now. Or capture on a new KEYS sub-page.
- **Where the settings live:** a new KEYS sub-page CHORD with columns MODE, TYPE, VOICING and STRUM.
- **MIDI in:** whether notes from a DAW are turned into chords. Probably off by default, so a DAW
  playing chords is not doubled.

## Suggested order

1. **Chord memory:** small, no CPU or flash cost, makes every engine more playable right away.
2. **Factory preset infrastructure,** then the patches. It also gives the next engine somewhere to put its
   sounds.
3. **The WAVE engine,** after measuring FM-4 on a real FM-1 at 8 voices, one core and two. That number
   says how much headroom a new engine has.

## Sources

- Plaits source and licence: github.com/pichenettes/eurorack (README "License"; `plaits/dsp/engine/wavetable_engine.cc`,
  `plaits/dsp/oscillator/wavetable_oscillator.h`, `plaits/resources/wavetables.py`)
- Franck and Välimäki, "Higher-order integrated wavetable synthesis", DAFx-12 (cited in `wavetables.py`)
- AKWF: github.com/KristofferKarlAxelEkstrand/AKWF-FREE (`LICENSE.md`: CC0 1.0)
- WaveEdit: github.com/AndrewBelt/WaveEdit (GPL-3.0)
- Korg Polysix owner's manual, chord memory pages (vintagesynth.com, manualslib.com)
- Arturia MicroFreak chord mode: Wikipedia, MusicRadar NAMM 2020
- Native Instruments Kontrol S MK3 manual, Scales and chord mode
