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

2026-11-23, branch `virtuosity`: STUDIO's hits are now Virtuosity Drums' close mics (CC0, pinned commit),
picked by ear over VCSL's on a minimal lo-fi beat; ORCH stays VCSL. KIT unchanged to the bit.

Done as SKINS (engine 14), on branch `kits`: STUDIO, LIVE, TIGHT, VINTAGE, ORCH, HALL, MARCH, SHELLAC, knobs
TUNE, DECAY, TONE, ROOM. Thirteen new hits (17,879 bytes) appended to `sources.DRUMS`, so KIT's 33 are cut
again unchanged and KIT renders the same to the bit; the kits reuse nine of KIT's hits too. LIVE and HALL's
room is two more readers of the same hit started 9.7 and 17.3 ms late (no buffer). The names, the picks and
the kick are placeholders until judged by ear (`build/samples-preview/`). Its picture (branch `skins-picture`)
is a ghost in a shell: the low end's glow, a skin the drums dent, a shell of metal segments; it draws in KIT's time. Still to do:
CI's flash number, and timing on the FM-1 with eight-note chords (LIVE and HALL run three readers a voice).
At release, as for CYCLE: the site's banks, `fmhub.json`, `PRODUCT.md`, `DESIGN.md`'s inks, the release
checklist's engine count, `app/fm1.toml`'s description, and `docs/img/a22/skins.png`.

## Sprint 5: GRAIN, a granular send effect — no new RAM (built, measured, taken out)

- A ninth effect (slot 8, made room for by sprint 2). DELAY, SHIFT, CHORUS and TANK share `echo_buf`
  (64 KB) and only one effect runs at a time, so GRAIN uses the same buffer: about 0.74 s mono at 44.1 kHz.
- Four to eight grains with windows from a table, knobs POSITION, SIZE, DENSITY, PITCH; FREEZE as a hold.
  Ideas from Clouds (MIT, Emilie Gillet), written for this buffer, not ported.
- Device: time it with eight-note chords. It runs on the first core with the mix, so its budget is what
  PLATE leaves.

Done on branch `grain` (cut from `kits`): effect 9, knobs POSITION, SIZE, DENSITY (0: off), PITCH. Eight grains
at most, started once a block; each grain's read worked out at its start to stay behind the write head and
inside the buffer. FREEZE: FX held on the FX page with GRAIN in hand (momentary, not saved: a latched freeze
would never let the state save; decided with the user 2026-11-22). On the host, eight grains cost about half
of PLATE (22 against 42 ms for 20 s of audio). Still to do: CI's flash number and timing on the FM-1 with
eight-note chords. At release: the site's effect list (`tools/build_site.py`), `PRODUCT.md`, the guide's FX
page.

Taken out before the release (commit 3cab816, on the owner's word; `docs/PLAN-after-sprints.md`, findings 2
and 3). Its timing on the FM-1 was taken with GRAIN's knobs as they stood, under one grain at a time on
average: eight grains cost about four times that on the host, with GAMELAN already at 79% beside the default.
And FREEZE clicked and thumped when pressed or let go while grains played (the largest step between samples
3.5 to 4.8 times larger), which `tests/grain.c` did not catch: it only froze into silence. Effects are eight
again, numbered as before, and a state saved by a development build with GRAIN in hand loads with PLATE
(`tests/state.c`). To come back it needs: the FREEZE fix (tried in a scratch copy, worked) with a check that
fails without it, `tools/fm1_profile.py` setting an effect's knobs, timing at SIZE and DENSITY 100 against
PERC's GAMELAN, and about 3 KB of flash.

## Sprint 6: PERC with more modes — code only, decided by the FM-1 (kept: under 80% on the FM-1)

- PERC is a four-mode modal resonator. Try eight to twelve modes and a sympathetic string option on a
  branch. Kept only if eight-note chords stay under about 80% peak on the device.

Built on branch `perc` (cut from `grain`): eight modes (the voice has room for eight without growing: the
modes in `s[]`, the cavity and mallet moved to `ph[]`, the strings as float bits in `u[2..3]`, `q[0..1]`), the
first four as they were. Sympathetic strings (an octave and a twelfth) in SPACE 60..100. A dead mode is
skipped until the next strike. Measured on the host, voice time only (eight-note chords, RING up): before
39 ms avg / 41 max per 8 s, after 45 / 52 (x1.28 worst), HANDPAN at SPACE 100 57 (x1.43); with every mode
running, as at the strike, x1.6 to x1.8. Against PERC's 51% peak on the unit (idle with PLATE 10%), that
suggests about 62% held, 69% with the strings, and up to about 83% at the instant of a strike with the
strings: the FM-1 decides. If it is over, cut the strings or drop to six modes.

Timed on the FM-1 on 2026-11-23 (`tools/fm1_profile.py`, build of branch `virtuosity`: eight-note chords,
2.2 s each, every algorithm):

| | Average | Worst average | Peak |
|---|---|---|---|
| PERC, PLATE (before sprint 6: 47% / 51% peak) | 60% | 72% (GAMELAN) | 78% (GAMELAN) |
| PERC, PLATE, SPACE 100 (strings) | 63% | 72% (GAMELAN) | 78% (GAMELAN) |
| PERC, GRAIN | 64% | 75% (GAMELAN) | **81% (GAMELAN): over** |
| SKINS, PLATE | 37% | 51% (HALL) | 67% (HALL) |
| FM-4, GRAIN / PLATE | 70% / 69% | 74% / 73% | 76% / 75% |
| SHAPER, GRAIN / PLATE | 68% / 67% | 74% / 74% | 77% / 77% |

Idle, each effect alone: PLATE 10%, DELAY 12%, FILTER 13%, SHIFT 15%, CHORUS 15%, TANK 13%, LOFI 14%, THUMP
13%, **GRAIN 25%** (PLATE sleeps in silence; GRAIN writes and reads on). So: PERC is at the limit, over it
only on GAMELAN with GRAIN (its new modes ring longest, so few are skipped); GRAIN costs what PLATE does
under load but 15 points more in silence. Next: GRAIN asleep in silence (as PLATE), and GAMELAN's new modes
shorter, or six modes on GAMELAN only; then time again.

Done (commit 71d7616: GRAIN asleep in silence; GAMELAN's new modes 0.55/0.35/0.18/0.6 s; modes skipped under
about -90 dB) and timed again on the FM-1 the same day:

| | Average | Worst average | Peak |
|---|---|---|---|
| PERC, PLATE | 59% | 66% (GAMELAN) | 76% (GAMELAN) |
| PERC, GRAIN | 58% | 69% (GAMELAN) | 79% (GAMELAN) |
| PERC, PLATE, SPACE 100 (strings) | 62% | 67% (HANDPAN) | 75% (GAMELAN) |

Idle: GRAIN 10% (was 25%), as PLATE. Every PERC algorithm under 80% with either effect, so sprint 6 stays,
with GAMELAN against GRAIN the closest (79%).

## On the FM-1 at the end: the merged build (task C1)

CI's build of `virtuosity` at 52bd8db (sprints 1–6, `main`'s one render per algorithm, GRAIN out, phase B's
fixes; image 563,692 B), timed on the FM-1 on 2026-11-23 with `tools/fm1_profile.py all --fx PLATE`
(ELECTRIC on its own: the `all` listing then skipped an engine when a PRESET turn did not land; fixed since).
Eight-note chords, 2.2 s each, every algorithm. Before: 0.4.0's numbers from `HANDOFF-perf-audio.md`, same
chord and timing. Engines with one render per algorithm (ed6544f, 6494cf7) marked *.

| Engine | Average | Worst average | Peak | 0.4.0 peak |
|---|---|---|---|---|
| FM-4 * | 64% | 68% | 71% (BASS) | 74% |
| SHAPER * | 60% | 67% | 70% (GLASSY) | 81% |
| HAZE | 65% | 65% | 67% (all) | 66% |
| SKINS (new) | 36% | 50% (HALL) | 66% (HALL) | |
| STACK * | 52% | 62% | 65% (CHOIR) | 66% |
| PERC (sprint 6) | 57% | 64% | **74% (GAMELAN)** | 51% |
| ANALOG * | 53% | 60% | 63% (SUPER) | 66% |
| WIRE | 54% | 59% | 63% (BOWED) | 59% |
| KIT | 48% | 52% | 59% (METAL) | 58% |
| ELECTRIC * | 44% | 48% | 50% (ORGAN, TAPE) | 52% |
| CHIP * | 38% | 42% | 43% (LEAD, DUET) | |
| CYCLE (sprint 3) | 41% | 41% | 43% | |
| WAVE | 41% | 41% | 42% | |
| MONO * | 33% | 34% | 37% | 36% |
| All 112 algorithms | 49% | | 74% (PERC GAMELAN) | 0.4.0: 48%, 73% (FM-4 BASS) |

PERC with RING and SPACE both at 100 (`--knob 3=100 --knob 4=100`, the strings in): average 64%, worst
average 69% (HANDPAN), **peak 78% (HANDPAN)**, GAMELAN 77%, STEEL 76%: under 80%, the closest line in the
release. Idle, each effect alone: PLATE 10% (max 12), DELAY 13, FILTER 13, SHIFT 15, CHORUS 15, TANK 14,
LOFI 14, THUMP 13.

Every line is under 80%. The engines with their own renders are lower than before or level; WIRE's BOWED is
4 points over the earlier table though nothing in WIRE changed (the others of WIRE are at 56%, as before);
HAZE, KIT and MONO within a point.

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
| 4 STUDIO + ORCH kits (SKINS) | 17.9 KB of hits + code (with sprints 5 and 6: image 513,260 B, CI 45172547638) |
| 5 GRAIN | ~3 KB; taken out |
| 6 PERC | ~1 KB |
| Later: progression generator, multi-model engine | ~1 KB + 5–8 KB |
| Total | ~98–105 KB of 170 (sprint 4 at ~20 KB with its code), leaving ~65–72 KB (keep at least 45 KB) |

Measured at the end, by CI: `virtuosity` with the sprints (GRAIN in) was 513,260 B, 68,304 B free of the
581,564 B slot. Merged with `main`'s one render per algorithm (53.5 KB, measured without them) and GRAIN
out, it is 563,708 B (CI 45191550090): 17,856 B free, under the 45 KB this plan keeps. What the release
keeps of those renders is decision D1 in `docs/PLAN-after-sprints.md`.
