# How A22 is built

A22 is derived from [h1](https://github.com/jakehandy/h1) (as of its 0.3.0) and keeps its shape: a portable
core, ports with no behaviour, the same files on the device and in the simulator. The core's functions are
still named `h1_*`; the engines, the tape and the state are A22's.

```
core/          the whole firmware: portable C99, single-precision float, no libc, no malloc
  h1.h           the public face: panel in, sound and picture out
  h1.c           the saved state (version 5, and loading 0.1.0-0.4.0's version 4), the public calls
  synth.c        voices, envelopes, LFO, arpeggiator, Riff and Shake, the four-track tape, the step sequencer,
                 chord memory, the event queue from the UI, the limiter
  fx.c           the eight effects (sends and whole-mix), the master bus's EQ and drive, the tape tricks
  gfx.c          anti-aliased lines, discs, rings, ellipses, filled polygons, glows and beams, a stroke font
  ui.c           what every control does, the name flash, the knob strip, the settings pages
  dice.c         DICE: an algorithm's knobs rolled near its home
  inst_*.c       one file per engine: its sound and its picture, side by side (inst_wave.c is WAVE and CYCLE)
  plunk_data.c   the recorded drum hits KIT layers in and SKINS is made of (tools/samples/drums.py writes it)
  wave_data.c    WAVE's eight banks (tools/gen_wavetables.py writes it)
  wave_akwf.c    CYCLE's four banks of AKWF single cycles (tools/akwf.py writes it)
port/rfm1/     the core as an .rfm1 (WebAssembly behind the fm1core ABI) for the FM-1 browser simulator
port/host/     a script runner that writes images and WAVs: where art is tuned and tests run
app/           the device, on fm1-base (base/, a submodule): built by ./build.sh
web/           the simulator page the site serves at sim/, which loads dist/a22.rfm1
tools/         build scripts, the site (build_site.py), the device profiler (fm1_profile.py), the data generators
dist/          a22.rfm1, the build the simulator loads
```

## The rule that keeps the simulator honest

The ports contain no behaviour. Everything seen and heard in the browser is `core/`, and the
device compiles the same files. A port only moves bytes: keys, buttons and encoder detents in;
audio frames, LED bits and changed screen rectangles out.

## Two threads

`h1_render` runs in the audio interrupt. Everything else (`h1_key`, `h1_button`, `h1_encoder`,
`h1_frame`, `h1_present`) runs in the main loop. The UI reaches the engine through a small
single-writer event queue (`evq` in `synth.c`); the engine publishes a snapshot (voice envelopes,
levels, two scope buffers) that the picture reads.

## Two cores

The FM-1's chip has a second core, and fm1-base can hand it one job at a time from the audio
interrupt (`base/docs/SECOND-CORE.md`). `h1_two_cores(run, wait)` tells the core a port has one.
The engine then renders voices half on each: a voice's control block (`voice_block`: its envelope,
its glide, the engine's `render`) touches the voice and its own buffer in `vt_l`/`vt_r` and nothing
else, so the second core takes every other voice while the first does the rest, and the first mixes
them afterwards in voice order. PLATE splits the same way (`fx_two_cores`: one half of the tank on each
core). The sound is the same to the bit as on one core. `tests/cores.c` plays everything on one core
and on two (a real thread) and compares, and `tests/a22_test.c` does it on all 112 algorithms.

What that asks of an engine: `render` may write the voice it is given and its output, and read
anything `block` and `note` set up. It must not write memory another voice's `render` reads.

The second core is in the default build (`cpu2 = true` in `app/fm1.toml`). `FM1B_CPU2=0 ./build.sh`
makes the build without it, and CI keeps both packages. Some FM-1s misread memory on the second core.
fm1-base checks each job it hands over, and `app/fm1.toml` sets `cpu2_fallback`, so an FM-1 that fails
a check goes on with one core until it is switched off. A misread inside a job's own data is not
checked. The timing readout ends with `CORES 2` and how deep the second core's stack has gone, or
`CORES 1` and why.

## The picture

Engines draw into a 240 x 240 buffer of 8-bit ink levels: 0 is the bezel's black, 255 is
full ink. A 256-entry palette turns ink into the engine's colour when the frame is sent.
`h1_present` compares the frame with what the panel already shows, in tiles of 8 pixels, and
reports rectangles of at most 20 rows, so the device sends only what moved. Two ink levels that
come out as the same colour count as no change. It also returns the byte count, which the
simulator turns into its screen-link meter.

The device takes the same thing in pieces, because sending is as slow as drawing there (a full
screen is 21 ms of SPI). `h1_present_begin` finds what changed, copies it to the panel's copy and
queues the rectangles; `h1_present_next` hands them out; their pixels are read from `h1_panel()`,
which the next frame's drawing does not touch. The drawing primitives call a hook
(`h1_while_drawing`), and `app/app.c` sends a rectangle from it whenever the panel has finished
the last one. A frame costs the longer of drawing and sending, not their sum. The keys are read from
the same hook, so a key pressed during a long frame is not missed.

Most of a frame is lines, and a pixel of a line costs the FM-1 far more than it costs a desktop.
`g_line` sets up a line in floats and then steps a pixel's distance from it in integers, down each
column and from column to column. In a path (`g_poly`, `g_arc`) a stroke's round start lies inside
the stroke before it, so it is left out. The old way, a float distance at every pixel, is kept for a
line too long, too far off the screen or too wide for the integers. `tests/lines.c` holds `g_line` to
within one ink level of 255 of a float distance at every pixel, over random lines, paths and arcs.

Soft things are not lines. `g_glow` is a round falloff and `g_beam` a stroke whose width and ink both
taper along it; both dither their faint ink on a 4x4 pattern so a gradient costs no float per pixel
and no banding. `g_fillv` fills a polygon with ink that changes down its rows. A soft shape changes
every pixel it covers, so it also costs bytes to the panel: keep the ones that move small. gfx.c also
has a 1-bit mask for clearing a path once (`g_bed_*`) and `g_comet`, which h1 used and no A22 engine
does yet.

Motion uses springs integrated with the real frame time, so the art moves the same at any frame
rate and never snaps.

## An engine

One `InstDef` (`core/internal.h`): a name, an ink colour, four knob names, eight algorithm names,
defaults, a gain, and five functions: `block` (once per control block of 32 frames), `note`, `render`
(one voice), an optional `post` (the engine's own mix), and `draw`. `mono` makes it monophonic with
legato priority (MONO). Adding an engine is one new file, one line in `insts[]`, `NINST` plus one and
its row in the per-algorithm gain table in `synth.c`. It goes last in PRESETS: a recorded note's part
and the saved state name engines by number. The state has room for sixteen (`NSLOT`), so the fifteenth
and sixteenth need no new state version.

More than one algorithm of an engine can sound in the same control block (two tracks on the same
engine, or a track that changed algorithm while its tail rings), so `block` is called again for each
before that algorithm's voices are rendered. `Blk.step` is the time that has passed: the control
block's the first time `block` is called in it, 0 on the later calls. Anything in `block` that moves
on its own (a phase, a wander) must advance by `Blk.step`, or it would run faster with every caller.
WAVE and CYCLE share one player and keep a state each (`WaveEng`), so both can sound at once.

The four home knobs belong to the algorithm: `st.knob[engine][algorithm][4]`, read through
`KNOBS` and `KNOBS_NOW`. Every algorithm starts on its own home (`knobs_home` in `h1.c`). The engine
eases the knobs of the algorithm in hand, and they ease across a change of algorithm too. DICE
(`dice.c`) rolls them near home and keeps the old ones for EDIT + ENV to swap back.
`tests/a22_test.c` checks each algorithm keeps its knobs; `tests/dice.c` that a roll stays within
6 dB of the algorithm at home.

## The tape

Time is 96 ticks per quarter note. A bar is 384, 288, 480, 288 or 336 ticks for 4/4, 3/4, 5/4, 6/8 and
7/8. The tape is 1 to 32 bars (`st.seq[1]` is log2 of the bars) and has four tracks (`NTRACK`). Each
track (`Track`) keeps its engine, algorithm, volume, pan, FX send, mute and solo, and a snapshot of its
knobs, ENV and LFO.

The tape stores timed events in RAM, 2,048 of them across the four tracks (`NLEV`), eight bytes each
(`LEv`: tick, note or knob, kind, track, part, velocity or knob value), kept in order of tick so that
each audio block finds the ones it plays by halving. Knob moves made while recording are events too
and play back as automation. A note-on goes in only with room kept for its note-off; a tape that is
full turns notes away and moves `eng_loop_full`, which the UI shows as TAPE FULL. A part is the engine
and algorithm a note was played on, in one byte: `PART(inst, algo)` is 1 + engine × 8 + algorithm, 0
for none.

The voices are rendered part by part into a mix per track, each engine's `post` runs on its mix, and
the mixes go through the track's volume (squared, eased), pan and send into the one effect and the
master bus. The snapshot for the picture separates the track in hand (`own_level`, `own_scope`: what
the HOME picture reacts to) from the whole mix after the effect (the top bar's meter, the FX page).

SPEED and TRICK work on the whole tape: half and double speed and REV change how fast ticks pass,
and the brake and stutters are `fx.c`'s, from a short buffer of what was just played (`tfx_buf`).
EDIT + REC cuts the track in hand's events between the loop's in and out bars to a clipboard of 512,
and EDIT + PLAY pastes them at the current bar.

The saved tape is the header and those events as they lie in RAM (`eng_loop_blob`, little-endian, no
copy, at most `H1_LOOP_MAX` = 16,396 bytes). The device keeps it in a ring of its own in the data area
(`app/app.c`, from 0xC8000: four slots of five sectors). A store save would erase first, and an erase
stalls the sound for about 50 ms, so a save goes into space that is already erased, a 256-byte page a
frame, with a header (magic, sequence, length, CRC-32) written last. That happens as soon as no take is
going in, while the tape plays. The newest record whose header reads right is the tape. Used slots are
erased at start, before the sound, or once the tape is stopped and nothing sounds.

The step sequencer is 64 steps of up to eight notes, on a 1/8, 1/16 or 1/32 grid, each note with its
track and velocity, and is saved with the rest of the state. Riff (64 steps, transposed from the key
held) and Shake (up to six balls in a spinning cage) are modes of the arpeggiator. `eng_seq_info`
gives the picture what it needs to draw the progress line, the tracks' density and the notches.

## Sound

Every note carries a velocity: the panel's keys 127, MIDI what it was sent with. A voice turns it into
a gain and a two-pole low-pass in the mix (open at 127). A voice taken over while it still sounds
starts the new note from where the old one was and lets the difference go over a millisecond or two
(`Voice.tail`).

At the end of the mix a limiter with one control block of look-ahead keeps everything under the
ceiling, with a soft knee above 0.6 (`limit` in `synth.c`). `tests/basics.c` runs every algorithm
through it.

DELAY, SHIFT, CHORUS and TANK sleep the way PLATE does: once the line has gone round with only zeros written,
nothing comes in and the effect's filters hold exactly 0, an effect only moves its clocks (`echo_asleep` in
`fx.c`). The sound is the same to the bit; `tests/sleep.c` plays each into its sleep and out with the sleep on and
off and compares.

When the audio runs late (`h1_late`: two late halves in a row), a voice lets go fast, one at a time, a
note the player is not holding first, so the sound gets back in time instead of starving the main loop.
`tests/overload.c` holds it.

MIDI out (`h1_midi_out`): what the keys, the arpeggiator and the sequencer play, on channel 1, and
CC 20-23 as the home knobs turn. Never a note or a CC 123 that came in over MIDI, so a computer that
echoes its input does not loop. A note-on or a knob goes out only while the USB queue has room to spare
(app.c), so a full queue never leaves a note-off out. The device sends it over USB; the FM-1's TRS jack
is an input only. OCT- and OCT+ held turn the USB port into a host for a USB-MIDI keyboard
(`h1_usb_host_fn`).

## The saved state

The saved state is version 5 (`H1State` in `internal.h`, magic "2A22", so it never loads an h1 save).
It has room for sixteen engines (`NSLOT`) and twelve effects (`NFXSLOT`), and keeps in `ninst` and `nfx`
how many of each it was saved with: on a load, the slots from there on come up as new, so adding an
engine or an effect needs no new version. New ones go last in PRESETS and the FX list, so that an
engine's number, which a recorded note's part holds, and an effect's, which the state holds, never
change. Version 4 (saved by A22 0.1.0 to 0.4.0) has twelve engine slots and eight effect slots and is
otherwise the same: `h1_state_load` reads it a table at a time, by the offsets of `H1StateV4` in `h1.c`
(compile-time checks hold its size and the spans the two versions share), so everything in it is kept.
Any other version or size is refused and the settings start as new; the tape is saved apart and kept.
`tests/state.c` loads a state written by 0.4.0's own core (`tests/state_v4.bin`) and compares every table.

## Controls as values

`h1_control` reports what each encoder currently means (its name, range, value and value text)
and `h1_control_set` sets it outright. On the device, `app/app.c` reads `h1_control` to tell a
screen reader a control's name and value. The simulator reaches both through the rfm1 port's sim1
exports to show each control as a labelled slider, while its nudge buttons, the mouse wheel and the
number keys still send real encoder detents.

## The recordings and tables

Most of A22 is synthesised. Three things are data, each written by a script in `tools/` and compiled
as it is; the firmware build does not run the scripts.

- **Drum hits** (`core/plunk_data.c`, about 44 KB): 47 single hits from VCSL and Virtuosity Drums, all
  CC0, cut by `tools/samples/drums.py` from `sources.DRUMS`. A drum is short and can take some grit, so
  they are 4-bit IMA ADPCM, each at the lowest rate its top allows (5.5 to 22.05 kHz), a tenth of a
  second or so. The hit's own dying away is divided out before it is stored and the player multiplies
  it back, longer or shorter with DECAY; a ringing hit keeps a short stretch that repeats while the
  player lets it fade. KIT's 909, CRISP, LOFI and WORLD layer them under synthesised drums; SKINS is
  made of nothing else, its room and treatments read from the same hits by more ADPCM readers, so no
  delay line and no RAM. `docs/SAMPLES.md` lists every hit and where it came from.
- **WAVE's banks** (`core/wave_data.c`): eight banks of fifteen waves, each at six band-limited levels
  an octave apart, from `tools/gen_wavetables.py`.
- **CYCLE's banks** (`core/wave_akwf.c`, about 64 KB): four banks of fifteen single cycles from
  Adventure Kid Waveforms (CC0), picked and phase-aligned by `tools/akwf.py`. `docs/WAVETABLES.md`
  says which.

`tools/samples/build.py` and most of `tools/samples/sources.py` are h1's: they cut the recordings of
h1's BARD and TWANG into `core/smp_data.c`, which A22 does not have. Running `build.py` would write that
file (and the `core/*.c` builds would pick it up) and rewrite `docs/SAMPLES.md` with h1's sections.

## RAM and flash on the device

The build prints the app image, RAM code, `.data` + `.bss`, the pool and the SRAM left; CI's build log
and its `a22.elf` artifact have them for every commit. `app/fm1.toml` sets `ram_layout = "packed"`: RAM
code, `.data`/`.bss` and the pool share one budget. `app/app.h` also cuts fm1-base's own canvas to a
48-row strip, since A22 draws with its own framebuffer and the platform only needs the strip for its
update and safe-mode text.

What goes in the pool, from the sources (not from a build), about 286 KB:

| Buffer | KB | Where |
|---|---|---|
| Framebuffer and the copy of what the panel shows | 115 | `g_fb_w` (gfx.c), `prev_w` (ui.c) |
| The effect in hand's buffer: DELAY's, SHIFT's, CHORUS's or TANK's line, or PLATE's tank | 64 | `fx_arena` (fx.c) |
| The tape tricks' buffer | 48 | `tfx_buf` (fx.c) |
| WIRE's string delay lines (8 voices) | 32 | `ks_buf` (inst_wire.c) |
| Two RGB565 bands for the panel | 19 | `band` (app.c) |
| The path mask | 7.5 | `bed` (gfx.c) |

Stack depth is measured on the device. Frame and audio time can be read there too: send MIDI CC 119
to the FM-1 while a screen reader is listening (`base/tools/fm1_remote.py listen`) and it says `PERF.
FPS n. DRAW avg MAX max. SEND avg MAX max. AUDIO avg MAX max. KB n RECTS n. STACK main interrupt`: the
times in milliseconds a frame, the audio as a percentage of each block, the kilobytes and rectangles
sent to the panel a frame, all since it was last asked, and the bytes of each stack never used since
the start. A build with the second core (the default) adds `CORES 2 n` or `CORES 1 why`. Since 0.6.0 it ends with
`STAGE seq voices wait mix fx master`, the slowest render since the last PERF split by stage (`h1_stages`, in percent
of the block), and `FLASH n max`, the tape's flash writes and the longest in microseconds.
`tools/fm1_profile.py` drives every algorithm over USB and collects these. If RAM gets tight, the first
things to shrink are the tape tricks' buffer and the path mask (no A22 engine draws with it).

PLATE and the echo effects share `fx_arena` because one effect runs in a block and an effect that starts again
clears its own buffer first (`fx_resume`). `tests/echo.c` holds each to sounding as from a cold start after the
other has filled the memory.

Code that runs in every block whatever plays is kept in RAM on the device (`H1_HOT` in `core/mathx.h`, fm1-base's
`.app_ram`), out of the way of the instruction cache both cores share: PLATE's halves, the master bus's EQ and
drive, `voice_out` and `eng_out`. Such a function may call only other `H1_HOT` functions and always-inlined helpers
(`H1_AI`), nothing through a pointer; code in flash calls it through `H1_FAR`. fm1-base's build checks the
disassembly. On the host, the simulator and the tests these are plain functions.

## What the FM-1 does not forgive

A computer shrugs at things that stop the FM-1's CPU, and a start that stops twice in a row is safe
mode. The first h1 did exactly that: it read its saved state's magic as a word straight out of a byte
buffer, the buffer sat at an odd address, and from the first save on it never started again. The rules
that came out of it, and that A22 keeps:

- Bytes from outside (a saved state, a saved tape) are copied out a byte at a time before anything
  reads them. `h1_state_load` takes a blob at any address, and clamps every value it keeps.
- Nothing is read or written a word at a time through a byte pointer. A byte array sits wherever the
  linker has room, odd addresses included: what is cleared or compared by words (the framebuffer) is
  declared as words. `tools/build_host.sh` makes a cast that raises alignment an error; a cast through
  `void *` hides from it, so there are none.
- A number that came from a float is masked or clamped before it indexes anything (`tsin`, the string
  and delay lines, the wave banks), and a loop that runs on a float has a bound.
- A counter that runs for as long as the unit is on stays a whole number and is never turned into a
  float: a float keeps 24 bits, so the fraction is gone after a few minutes. The delay's read position
  did that in h1 until its 0.3.0 and the echo went grainy the longer the unit was on (`tests/echo.c`).
- A rectangle sent to the panel is at most 20 rows: the device converts it in a buffer that size.
- After a start that failed, `app_init` leaves the saved state alone and comes up as new, so one bad
  start cannot become two.
- `tests/fuzz.c` holds all of this to account: any saved state from an odd address, any panel input
  on every engine, and the sound on a second thread, under `-fsanitize=address,undefined`.
- `app/fm1.toml` keeps the crash history in flash (`base/tools/fm1_remote.py crashes`), and the stacks
  are painted at every start so the PERF readout can say how much of each was never used.

## Building

- `tools/build_host.sh` then `build/h1host OUTDIR script...` for images and WAVs.
- `tools/build_rfm1.sh [VERSION]` for `dist/a22.rfm1` (clang with the wasm32 target and wasm-ld). The
  site's simulator (`web/`, served at `sim/`) loads it; it also runs in the
  [FM-1 browser simulator](https://github.com/jakehandy/fm1-browser-simulator): open the page and drop
  the file on it.
- `./build.sh` builds the device package: fm1-base's `tools/build.py` with `app/` as the app.
  `./build.sh --release X.Y.Z` builds a release's identity; `./build.sh --host` the host simulator build.
- `sh tests/run_tests.sh` runs the device app in fm1-base's host simulator, then every core test
  (most under the address and undefined-behaviour checkers). Its header lists what each test holds.
- `sh tests/run_fuzz.sh [SEEDS]` runs the crash hunt.
- `python3 tools/build_site.py` builds the website.
