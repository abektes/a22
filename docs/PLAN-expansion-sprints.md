# Plan: what the spare flash goes to

Written 2026-11-22, after 0.4.0. Each sprint is a branch (1 and 2 share `dice`; 3 is `akwf`, cut from it), tested on the host (`tests/run_tests.sh`), and,
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

## Sprint 1: DICE (randomizer) — no flash or RAM to speak of, no audio cost (done)

- **EDIT + LFO** rolls the four knobs of the algorithm in hand: each moves up to a span per engine knob from
  the algorithm's own setting, inside a floor and a ceiling, so a roll moves the sound but does not leave it.
- **EDIT + ENV** swaps back to the knobs from before the last roll, and again to the roll: an A/B compare.
  Only for the engine and algorithm that were rolled.
- The roll goes through the same path as a hand on the knobs: it is recorded as automation while the tape
  records, and sent as CC 20–23.
- For a new algorithm, turn ALGORITHM and roll: no second gesture.
- Test (`tests/dice.c`): rolls stay in range, differ from each other, the swap is exact, and every one of
  the 96 algorithms, rolled 24 times, still sounds (not silent, no NaN, no stuck output).
- Cost: about 100 bytes of table and a few hundred of code.

## Sprint 2: room for more engines and effects — state version 5 (done)

- `NSLOT` 12 → 16 and the effect parameters 8 → 12 slots. Version 4 states load (by offsets, as the older
  versions do); the new slots come up as new.
- No new sound. Test: version 4 states from odd addresses load unchanged, old tapes play the same engines.
- Cost: 240 bytes of SRAM (the state grows from 2,648 to 2,888 bytes), nothing else.

## Sprint 3: AKWF wavetables — about 66 KB of flash (done: CYCLE, 64 KB)

- Four new banks of fifteen waves from Adventure Kid Waveforms (CC0, the author's repository), cut by
  `tools/gen_wavetables.py` into WAVE's format (six band-limited levels, 16.5 KB a bank).
- WAVE's eight algorithms are its eight banks, so the new banks go in a new engine slot (13), reusing
  WAVE's player: four banks, each as a smooth morph and as a stepped scan. (Earlier this was described as
  "no new playback code": the player is reused, but it is a new engine, with its own picture, and it needs
  sprint 2.)
- The engine's name, banks and which AKWF waves are picked are decided by ear before the bank is cut.
- Device: time the new engine at eight voices (it should land at WAVE's cost).

Done as CYCLE (engine 13): STRING, REED, KEYS, VOICE and their STEP- versions. The names and the 60 waves
are placeholders until judged by ear (`tools/akwf.py` holds the list; the picture is WAVE's). At release
the site (`tools/build_site.py`: banks A to M), `fmhub.json`, `PRODUCT.md`, `DESIGN.md`'s bank inks and the
release checklist's engine count need CYCLE too.

## Sprint 4: two sampled kits, STUDIO and ORCH — about 25–30 KB of flash (done: SKINS, 17.9 KB of hits)

Decided 2026-11-22: both kits from VCSL (CC0), in a new engine slot (engine 14), so nothing saved changes.

- **STUDIO, an acoustic drum set,** the gap KIT has (it synthesizes everything, and its 33 recorded hits
  are world and orchestral percussion). From VCSL: "Snare Drum, Modern 1-3", "Tom 1" and "Tom 2" (stick),
  the hi-hat, clash and suspended cymbals, and a bass drum cut short and tightened into a kick (the one
  part that has to be judged by ear: VCSL's bass drums are concert drums).
- **ORCH, a marching and orchestral kit:** the marching snare and tenor toms ("Legacy Snares", "Legacy
  Toms"), clash cymbals, a concert bass drum, with the timpani, suspended cymbal and rope snare already
  among the 33.
- Each kit in KIT's layout (C kick, C# rim, D snare, D# clap, E/F/G toms, F# hat, A# open hat, B crash or
  ride, and two extras), 12 to 14 hits, cut by `tools/samples/drums.py` as now (IMA ADPCM, cymbals with a
  loop). The engine's eight algorithms: each kit in four treatments made in the player (dry, room, tight,
  vintage), as h1's PLUNK made eight kits from one pool.
- Skipped: FreePats world percussion (8 of its 10 instruments are already among the 33 hits) and FreePats
  synthesizer percussion (KIT synthesizes those sounds live). Rhoda's found sounds were the alternative to
  ORCH; its licence is unverified.
- Device: a sampled kit is cheap to play; time it anyway with eight-note chords.

Done as SKINS (engine 14), on branch `kits`: STUDIO, LIVE, TIGHT, VINTAGE, ORCH, HALL, MARCH, SHELLAC, knobs
TUNE, DECAY, TONE, ROOM. Thirteen new hits (17,879 bytes) appended to `sources.DRUMS`, so KIT's 33 are cut
again unchanged and KIT renders the same to the bit; the kits reuse ten of KIT's hits too. LIVE and HALL's
room is two more readers of the same hit started 9.7 and 17.3 ms late (no buffer). The names, the picks and
the kick are placeholders until judged by ear (`build/samples-preview/`); the picture is KIT's. Still to do:
CI's flash number, and timing on the FM-1 with eight-note chords.
: GRAIN, a granular send effect — no new RAM

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

A Plaits-style engine (chord-in-one-voice, wave terrain, vowel, phase distortion variants) in slot 15 (the
kits take 14).
Planned after sprints 1–6, against the flash and audio time they leave.

## Flash at the end

| Sprint | Flash |
|---|---|
| 1 DICE | < 1 KB |
| 2 State v5 | ~0.5 KB |
| 3 AKWF banks (CYCLE) | 69 KB, measured by CI (image 479,660 B) |
| 4 STUDIO + ORCH kits (SKINS) | 17.9 KB of hits + code, CI to measure |
| 5 GRAIN | ~3 KB |
| 6 PERC | ~1 KB |
| Later: progression generator, multi-model engine | ~1 KB + 5–8 KB |
| Total | ~108–115 KB of 170, leaving ~55–62 KB (keep at least 45 KB) |
