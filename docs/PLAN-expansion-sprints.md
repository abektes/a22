# Plan: what the spare flash goes to

Written 2026-11-22, after 0.4.0. Each sprint is a branch, tested on the host (`tests/run_tests.sh`), and,
where it touches the audio, built by CI and timed on the FM-1 (CC 119, `HANDOFF-perf-audio.md`'s profiler)
before it is merged.

## The budget (0.4.0, CI run 45036222031)

| | Used | Free |
|---|---|---|
| Flash (app slot 581,564 B) | 410,720 B | about 170 KB |
| SRAM (475,136 B) | 443,088 B | 32,048 B, of which 8,192 kept spare: **about 24 KB** |
| Audio, eight-note chords, all 96 presets | average 48%, peak 73% (FM-4 BASS) | |

Rule for the plan: keep at least 45 KB of flash free at the end, for fixes. No sprint adds a RAM buffer.

## A constraint found while planning

The saved state (version 4) has exactly `NSLOT` = 12 engine slots and `fx_p[NFX]` = 8 effect slots, and
both are full. A new engine or effect therefore needs a state version first. Sprint 2 makes that room once,
for everything after it.

## Sprint 1: DICE (randomizer) — no flash or RAM to speak of, no audio cost

- **EDIT + LFO** rolls the four knobs of the algorithm in hand, inside a range per engine knob that keeps
  the algorithm recognisable. Each new value lies between the algorithm's own default and a draw from that
  range, so a roll moves the sound but does not leave it.
- **EDIT + ENV** swaps back to the knobs from before the last roll, and again to the roll: an A/B compare.
  Only for the engine and algorithm that were rolled.
- The roll goes through the same path as a hand on the knobs: it is recorded as automation while the tape
  records, and sent as CC 20–23.
- For a new algorithm, turn ALGORITHM and roll: no second gesture.
- Test (`tests/dice.c`): rolls stay in range, differ from each other, the swap is exact, and every one of
  the 96 algorithms, rolled 24 times, still sounds (not silent, no NaN, no stuck output).
- Cost: about 100 bytes of table and a few hundred of code.

## Sprint 2: room for more engines and effects — state version 5

- `NSLOT` 12 → 16 and the effect parameters 8 → 12 slots. Version 4 states load (by offsets, as the older
  versions do); the new slots come up as new.
- No new sound. Test: version 4 states from odd addresses load unchanged, old tapes play the same engines.
- Cost: about 1 KB of SRAM (the state grows), nothing else.

## Sprint 3: AKWF wavetables — about 66 KB of flash

- Four new banks of fifteen waves from Adventure Kid Waveforms (CC0, the author's repository), cut by
  `tools/gen_wavetables.py` into WAVE's format (six band-limited levels, 16.5 KB a bank).
- WAVE's eight algorithms are its eight banks, so the new banks go in a new engine slot (13), reusing
  WAVE's player: four banks, each as a smooth morph and as a stepped scan. (Earlier this was described as
  "no new playback code": the player is reused, but it is a new engine, with its own picture, and it needs
  sprint 2.)
- The engine's name, banks and which AKWF waves are picked are decided by ear before the bank is cut.
- Device: time the new engine at eight voices (it should land at WAVE's cost).

## Sprint 4: more sampled drums — about 30–40 KB of flash

- KIT is synthesized; only CRISP, LOFI and WORLD mix in recorded transients (`plunk_data.c`: 33 VCSL hits,
  IMA ADPCM, about 0.8 KB a hit). Add 40 to 50 hits from VCSL and FreePats (both CC0) through
  `tools/samples/drums.py`, as now.
- Where they go is decided by ear: either more of KIT's algorithms get recorded transients, or two sampled
  kits (one electric/808-adjacent from FreePats, one world percussion) take the place of two KIT algorithms.
- **Saved sounds change:** a tape, a snapshot or a track names an algorithm by number, so a replaced KIT
  algorithm plays the new kit in everything saved before. Adding transients to an algorithm also changes
  how it sounds in saved tapes. Say so in the CHANGELOG, or add the kits in a new engine slot (sprint 2)
  instead.

## Sprint 5: GRAIN, a granular send effect — no new RAM

- A ninth effect (slot 8, made room for by sprint 2). DELAY, SHIFT, CHORUS and TANK share `echo_buf`
  (64 KB) and only one effect runs at a time, so GRAIN uses the same buffer: about 0.74 s mono at 44.1 kHz.
- Four to eight grains with windows from a table, knobs POSITION, SIZE, DENSITY, PITCH; FREEZE as a hold.
  Ideas from Clouds (MIT, Emilie Gillet), written for this buffer, not ported.
- Device: time it with eight-note chords. It runs on the first core with the mix, so its budget is what
  PLATE leaves.

## Sprint 6: PERC with more modes — code only, decided by the FM-1

- PERC is a four-mode modal resonator. Try eight to twelve modes and a sympathetic string option on a
  branch. Kept only if eight-note chords stay under about 80% peak on the device.

## Later: a multi-model engine

A Plaits-style engine (chord-in-one-voice, wave terrain, vowel, phase distortion variants) in slot 14.
Planned after sprints 1–6, against the flash and audio time they leave.

## Flash at the end

| Sprint | Flash |
|---|---|
| 1 DICE | < 1 KB |
| 2 State v5 | ~1 KB |
| 3 AKWF banks | ~66 KB |
| 4 Drums | ~35 KB |
| 5 GRAIN | ~3 KB |
| 6 PERC | ~1 KB |
| Total | ~107 KB of 170, leaving ~63 KB |
