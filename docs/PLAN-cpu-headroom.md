# Plan: CPU headroom for loop jams

Written 2026-11-24, after 0.5.0. Nothing in it has been built yet. It takes up `docs/PLAN-after-sprints.md`'s
Phase G and the tactics in `docs/RESEARCH-cpu-ram.md`, put in order for the case that drops audio.

## The case to fix

Single-engine chords are not the problem. Eight-note chords on all 112 algorithms with PLATE averaged 49%
of the audio block on 0.5.0, peaking at 74%, with no late halves (`docs/notes/SUBMIT-0.5.0.md`). The
30-minute soak with a two-track loop held 60% audio and 24 FPS.

A dense four-track loop jam is the problem. That is `tools/fm1_profile.py jam`, after a tester's jam on
r/MVaveFM1, and the scenarios `reddit3` and `reddit4` in `tests/corestat.c`:

| Build | Late halves | Voices shed | Peak audio | FPS |
|---|---|---|---|---|
| 0.4.0 | 63 | 8 | 113% | |
| 0.5.0 (CHANGELOG), two runs | 35, 22 | | 104%, 106% | 11–25 |

Phase G records a lighter reading for 0.5.0 (peaks of 87 to 91%, one late half, 15 to 25 FPS), but not
how it was run. J0's first job is a fresh baseline: two jam runs on 0.5.0 as released, written into this
table before any task starts.

At 100% the audio drops out, and the wiki says dropouts come from a few heavy blocks, not a high average.
The goal of the sprint: **the jam with 0 late halves and peaks under 80%, two runs out of two, and the
chords table no worse.**

## Rules for every task

- One branch a task, or a few small tasks together. Host tests first (`sh tests/run_tests.sh`).
- **Bit-exact unless marked.** A task marked "same sound" must leave `tests/cores.c`, `tests/a22_test.c`'s
  two-core check and every `tests/corestat.c` scenario producing the same output, bit for bit. If a hash
  moves, the task changed the sound and goes back.
- **Timed on the FM-1**, from a CI build (`gh workflow run release.yml -R abektes/a22 --ref <branch>`,
  artifact `packages`). Run `tools/fm1_profile.py jam --clear-tape` twice, because its peaks vary from run
  to run; LATE and SHED are the steadier numbers. Then `tools/fm1_profile.py all --fx PLATE` for the chords
  table. A host gain is not an FM-1 gain: one job list per block once cost the FM-1 1 to 6 points
  (`tests/corestat.c`'s header).
- **`jam --clear-tape` runs CLEAR ALL** and erases whatever is on the FM-1's tape. A22 has no USB backup.
  Ask the owner before every run and keep hands off the unit while it runs.
- Read the build's SRAM line after any task that moves memory: used against 475,136 bytes, with 8,192 kept
  spare.
- A task marked "changes the sound" is kept only if the owner listens and agrees.

## J0. Stage timers in the PERF line (measure first)

CC 119 gives the total audio time a block, average and max. That cannot say which stage is the peak, and
Phase G has four candidates. Add a few timers on the 24 MHz TIMER4 clock (the wiki's choice: it does not
follow the CPU clock), kept as the max since the last CC 119:

- the first core's voices, and its wait for the second core
- the mix of every voice into its track (first core today)
- the effect, the master bus and the limiter
- a tape page write in `ring_step` (`app/app.c`), and whether it landed inside an audio half

Device build only, no sound change, said as one more field on the PERF line. `tools/fm1_profile.py` prints
it. **Done when:** a jam run says which stage is the peak, and the tasks below are re-ranked by it.

## J1. Tape page writes against the audio (likely cause, cheap to test)

After a take, `ring_step` writes the tape to flash a 256-byte page a frame while it plays. Each page write
turns off the interrupts and the code running from flash, the audio's included (Phase G). A jam is exactly
"the tape playing, takes closing", so this is the first suspect.

- Test it first with a build that holds the tape saves off (development build only, never released), and
  compare the jam's LATE with and without.
- If it matters: write pages only while the audio is light (the last block's time under a threshold), or
  space them out, and make sure the tape is still saved within a few seconds of a take. The ring's
  behaviour at power-off stays as it is (`tests/looppower.txt`).

Same sound. **Done when:** the jam's LATE does not change when saves are held off, or the fix brings it
to what it is without saves.

## J2. WIRE's note-on wipe

`string_note` (`core/inst_wire.c:67` and `:73`) clears or halves all 2,048 samples of the voice's row on
every new note. Eight WIRE notes struck in one block is about 16,000 samples of work on the attack, when
the audio is closest to shedding. A rough estimate is 5–10% of one block; J0 will say.

Clear only what the string can read: the delay `D` for the lowest pitch the note can reach (pitch bend and
the LFO lower it and read further back), plus the taps, plus a margin. The read never reaches past that,
so the output is unchanged.

Same sound (check the hashes with WIRE on every algorithm, with bend and LFO on pitch). **Done when:** WIRE
chords on the FM-1 peak lower and the hashes have not moved.

## J3. Share `block()` better between the cores (Phase G)

- Of an odd number of voices, the larger half to the second core, a voice alone kept on the first. Already
  bit-exact on the host in all nine corestat scenarios, 1 to 3% on the host (2026-11-23). Not timed on the
  FM-1.
- Each voice's gain, declick and mix into its track done by the core that rendered it, then summed in voice
  order by the first. Today the first core does that for every voice, group by group.

Same sound. **Done when:** J0's two core timers come closer together in the jam and the jam's peak falls.

## J4. Main-loop work the jam does not need (Phase G)

- The per-frame scans of the tape's events (`eng_seq_info`, `eng_seq_leds`) only when the tape changed
  (`eng_loop_gen`). This gives back frame time, not audio time, but the jam's 15–25 FPS is the other half
  of how it feels.
- `input_hz = 5000` (fm1-base: about 4% of the first core). Velocity has only been measured at 10,000, so
  this needs hands and eyes on the keys and the glow.

Same sound. **Done when:** the jam's FPS rises and key velocity feels the same.

## J5. One delay buffer for the effect in hand (makes room for J6)

PLATE uses `rev_buf` (48 KB). DELAY, SHIFT, CHORUS and TANK share `echo_buf` (64 KB). Only one effect runs in
a block, and `fx_resume` (`core/fx.c:618`) already clears an effect's buffer whenever it starts after not
running. A union of the two arrays is about ten lines and frees 48 KB of SRAM. `tfx_buf` stays outside the
union: the brake and stutters run on top of any effect.

Same sound. Also check that changing effect while a tail rings still starts the new effect from silence
(it already does). **Done when:** the build shows about 48 KB more SRAM free and the hashes have not moved.

## J6. Hot audio code in SRAM

The only tactic measured on this chip: DUB-1 moved a 55 KB audio path from flash to SRAM on an FM-1 and
its peaks fell 6–35%, with the same arithmetic. Code runs from flash through an instruction cache both
cores share, and the UI between blocks pushes the audio out of it.

fm1-base's rules (`base/tools/build.py`, `PLAT_RAM_CODE` in `base/include/plat.h`): code in RAM may call
only RAM code, and nothing through a pointer, unless `ram_code_allow` in `app/fm1.toml` vouches for it.
A22 calls every engine's `render` through a pointer, and all fourteen engines do not fit. So in steps:

1. The code that runs every block whatever plays: the mix, the limiter, `fx_master_block`, PLATE's halves,
   and the small helpers they call. Time it.
2. Only if step 1 pays: the render of the engines J0 shows at the peaks, or the wiki's fallback, copying the
   engine in hand into a reserved SRAM window when PRESETS changes it.

Same sound. Needs J5's room first. **Done when:** the jam's peak and LATE fall on the FM-1, two runs out of
two; otherwise the change is not kept.

## J7. Let DELAY, SHIFT, CHORUS and TANK sleep the way PLATE does

PLATE skips its tank once the input and every sample in it have been zero for a full pass. The other
effects keep walking `echo_buf`. With nothing playing, DELAY measured 13% and CHORUS and SHIFT 15%, against
PLATE's 10%. This helps idle and quiet passages, not the jam's peak, so it comes after J1–J6.

Same sound (the sleep starts only once the line is all zeros). **Done when:** `tools/fm1_profile.py idle`
shows those effects near PLATE's 10%.

## Only if the timers point there (each changes the sound)

- **PLATE's tank at half rate.** Dattorro designed the tank at 20–30 kHz, so this stays inside his paper,
  but it is a different plate. Only if J0 shows the sounding plate as the peak stage, and only with the
  owner's ears.
- **`audio_ahead = true`** (Phase G): one half more latency. It absorbs a spike, not a load past 100%.
- **Shedding a voice earlier**, on the running peak rather than on two late halves. A product choice: a
  thinner chord sooner.
- **One build with the DSP in the LTO link**, flags otherwise unchanged. A single timed try; LTO can grow
  code and make the cache worse.

## Out of scope

From `docs/RESEARCH-cpu-ram.md`, and agreed: no fixed-point rewrite (the float unit is the fast path, and
the 64-bit multiply erratum is on the integer side), no `-ffast-math` (it breaks host/device agreement and
the hashes), no SIMD rewrite (the chip shows no packed SIMD), no higher clock (360 MHz is already above the
SDK's 320, and core-1 misreads are why the one-core fallback exists), no DPW in place of PolyBLEP, no
hardware EQ, FFT or SRC on the audio path. The RAM tactics RAM-2 to RAM-4 (a shorter brake, a shared WIRE
pool, a row hash in place of `prev_w`) are only for if SRAM is still short after J5 and J6.

## Order

J0 first, because it decides the rest. J1 and J2 next: cheap, same sound, and aimed at peaks. Then J3 and
J4. J5 then J6 is the biggest change and the one with evidence from the chip. J7 last. Every task is timed
on the FM-1 before it is merged, and the release that carries them gets the chords table and the jam
table again, as 0.5.0 did.
