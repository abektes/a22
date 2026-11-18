# How h1 is built

```
core/        the whole firmware: portable C99, single-precision float, no libc, no malloc
  h1.h         the public face: panel in, sound and picture out
  synth.c      voices, envelopes, LFO, arp, loop recorder, step sequencer, the event queue from the UI
  fx.c         tone, echo, space
  gfx.c        anti-aliased lines, discs, rings, ellipses, filled polygons, a stroke font
  ui.c         what every control does, the name flash, the knob strip, the settings pages
  inst_*.c     one file per instrument: its sound and its picture, side by side
port/rfm1/   the core as an .rfm1 (WebAssembly behind the fm1core ABI) for the FM-1 browser simulator
port/host/   a script runner that writes images and WAVs: where art is tuned and tests run
app/         the device, on fm1-base (base/, a submodule): built by ./build.sh
tools/       build scripts
dist/        h1.rfm1, the build the simulator loads
```

## The rule that keeps the simulator honest

The ports contain no behaviour. Everything Jake sees and hears in the browser is `core/`, and the
device compiles the same files. A port only moves bytes: keys, buttons and encoder detents in;
audio frames, LED bits and changed screen rectangles out.

## Two threads

`h1_render` runs in the audio interrupt. Everything else (`h1_key`, `h1_button`, `h1_encoder`,
`h1_frame`, `h1_present`) runs in the main loop. The UI reaches the engine through a small
single-writer event queue; the engine publishes a snapshot (voice envelopes, levels, a scope
buffer) that the picture reads.

## Two cores

The FM-1's chip has a second core, and fm1-base can hand it one job at a time from the audio
interrupt (`base/docs/SECOND-CORE.md`). `h1_two_cores(run, wait)` tells the core a port has one.
The engine then renders a part's voices half on each: a voice's control block (`voice_block`: its
envelope, its glide, the instrument's `render`) touches the voice and its own buffer in `vt` and
nothing else, so the second core takes every other voice while the first does the rest, and the
first mixes them afterwards in voice order. The sound is the same to the bit as on one core.
`tests/cores.c` plays everything on one core and on two (a real thread) and compares.

What that asks of an instrument: `render` may write the voice it is given and its output, and read
anything `block` and `note` set up. It must not write memory another voice's `render` reads. (COZY
and BARD each set a `sounding` flag from `render`; both cores only ever set it, and `post` reads it
after they are done.)

The second core is in the default build (`cpu2 = true` in `app/fm1.toml`). On an FM-1 playing 16
notes a second it took the picture from 34 to 44 frames a second on average, and the slowest
setting from 12 to 28. `FM1B_CPU2=0 ./build.sh` makes the build without it, and CI keeps both
packages. Some FM-1s misread memory on the second core. fm1-base checks each job it hands over,
and `app/fm1.toml` sets `cpu2_fallback`, so an FM-1 that fails a check goes on with one core until
it is switched off. A misread inside a job's own data is not checked. The timing readout ends with
`CORES 2` and how deep the second core's stack has gone, or `CORES 1` and why.

## The picture

Instruments draw into a 240 x 240 buffer of 8-bit ink levels: 0 is the bezel's black, 255 is
full ink. A 256-entry palette turns ink into the instrument's colour when the frame is sent.
`h1_present` compares the frame with what the panel already shows, in tiles of 8 pixels, and
reports rectangles of at most 20 rows, so the device sends only what moved. Two ink levels that
come out as the same colour count as no change. It also returns the byte count, which the
simulator turns into its screen-link meter.

The device takes the same thing in pieces, because sending is as slow as drawing there (a full
screen is 21 ms of SPI). `h1_present_begin` finds what changed, copies it to the panel's copy and
queues the rectangles; `h1_present_next` hands them out; their pixels are read from `h1_panel()`,
which the next frame's drawing does not touch. The drawing primitives call a hook
(`h1_while_drawing`), and `app/app.c` sends a rectangle from it whenever the panel has finished
the last one. A frame costs the longer of drawing and sending, not their sum.

KNOT VOID clears a path through the picture before it draws its cord. It marks the path in a
1-bit mask (`g_bed_move`, `g_bed_to`) and clears the framebuffer under the mask once
(`g_bed_clear`), so a cord that crosses itself is not cleared stroke by stroke.

Most of a frame is lines (URCHIN draws about 950 of them over 49,000 pixels), and a pixel of a line
costs the FM-1 far more than it costs a desktop. `g_line` sets up a line in floats and then steps a
pixel's distance from it in integers, down each column and from column to column. In a path
(`g_poly`, `g_arc`) a stroke's round start lies inside the stroke before it, so it is left out. The
old way, a float distance at every pixel, is kept for a line too long, too far off the screen or too
wide for the integers. `tests/lines.c` holds `g_line` to within one ink level of 255 of a float
distance at every pixel, over random lines, paths and arcs.

Motion uses springs integrated with the real frame time, so the art moves the same at any frame
rate and never snaps.

## An instrument

One `InstDef`: a name, an ink colour, four knob names, eight algorithm names, defaults, and five
functions: `block` (once per 32 frames), `note`, `render` (one voice), an optional `post` (the
mix), and `draw`. Adding an instrument is one new file, one line in the table and `NINST` plus one.

An instrument holds the numbers of one algorithm at a time. With LAYERS on, more than one of its
algorithms can sound in the same block, so `block` is called again for each before that
algorithm's voices are rendered. `Blk.step` is the time that has passed: the control block's the
first time `block` is called in it, 0 on the later calls. Anything in `block` that moves on its
own (a phase, a wander) must advance by `Blk.step`, or it would run faster with every layer.
`post` runs once per instrument, on that instrument's own mix.

## The sequencer

Time is 96 ticks per quarter note. A bar is 384, 288, 480, 288 or 336 ticks for 4/4, 3/4, 5/4,
6/8 and 7/8. The loop recorder stores timed events in RAM and is lost at power off. The step
pattern is 64 steps of up to four notes, on a 1/8, 1/16 or 1/32 grid, and is saved with the
rest of the state. `eng_seq_info` gives the picture what it needs to draw the progress line or
the notches.

Every recorded note also stores its part: the instrument and algorithm it was played on, in one
byte (`PART`). Every voice carries the part it sounds and whether the sequencer started it. With
LAYERS off (`st.seq[6] == 0`) the stored parts are ignored and recorded notes go through the same
path as a key, on the instrument in hand. With LAYERS on, the sequencer starts voices on the
stored part directly, past the arpeggiator (the loop records what the arpeggiator played, not the
keys held). The engine then renders the voices part by part into a mix per instrument, runs each
instrument's `post` on its mix, and sums them into the one effects chain, which takes its
settings from the instrument in hand. An instrument that is not in hand and has gone quiet keeps
its `post` running for a third of a second, fading, so its tail is not cut. The snapshot for the picture
leaves out the voices of other instruments.

The saved state is version 5. Version 4 is the same without the step notes' parts at the end:
`h1_state_load` takes it, with LAYERS off and no parts kept. A step note with no part kept
belongs to whichever instrument is in hand.

## Controls as values

`h1_control` reports what each encoder currently means (its name, range, value and value text)
and `h1_control_set` sets it outright. On the device, `app/app.c` reads `h1_control` to tell a
screen reader a control's name and value. The simulator reaches both through the rfm1 port's sim1
exports to
show each control as a labelled slider, while its nudge buttons, the mouse wheel and the number
keys still send real encoder detents.

## RAM and flash on the device

Measured by the first device build (CI, JieLi toolchain 20260730.1, fm1-base c8e0599):

| | Bytes | Of |
|---|---|---|
| App image | 304,848 | the 581,564-byte app slot |
| RAM code | 5,100 | |
| `.data` + `.bss` | 45,556 | |
| Pool | 347,492 | |
| SRAM in all | 398,152 | 475,136, with 76,984 free (the build keeps 8,192 spare) |

`app/fm1.toml` sets `ram_layout = "packed"`: RAM code, `.data`/`.bss` and the pool share one
budget. fm1-base's default map gives the pool a fixed 336 KB, which h1 overflows. `app/app.h`
also cuts fm1-base's own canvas to a 48-row strip (23 KB, down from 69 KB), since h1 draws with
its own framebuffer and the platform only needs the strip for its update and safe-mode text.

What h1 puts in the pool, about 321 KB:

| Buffer | KB |
|---|---|
| Framebuffer and the copy of what the panel shows | 115 |
| String delay lines (8 voices, 2 each) | 66 |
| Echo | 64 |
| Reverb | 48 |
| Two RGB565 bands for the panel | 19 |
| LAVA's field and INK's scroll history | 7 |

These are link-time numbers. Stack depth has not been measured on hardware. Frame and audio time
can be: send MIDI CC 119 to the FM-1 while a screen reader is listening (`base/tools/fm1_remote.py
listen`) and it says `PERF. FPS n. DRAW avg MAX max. SEND avg MAX max. AUDIO avg MAX max. KB n RECTS n.
STACK main interrupt`: the times in milliseconds a frame, the audio as a percentage of each block, the
kilobytes and rectangles sent to the panel a frame, all since it was last asked, and the bytes of each
stack never used since the start. A build with the second core (the default) adds `CORES 2 n` or `CORES 1 why`. If RAM gets tight again, the first things to shrink are the string lines and the reverb
(16-bit samples halve both).

## What the FM-1 does not forgive

A computer shrugs at things that stop the FM-1's CPU, and a start that stops twice in a row is safe
mode. The first h1 did exactly that: it read its saved state's magic as a word straight out of a byte
buffer, the buffer sat at an odd address, and from the first save on it never started again. The rules
that came out of it:

- Bytes from outside (a saved state) are copied out a byte at a time before anything reads them.
  `h1_state_load` takes a blob at any address, and clamps every value it keeps.
- Nothing is read or written a word at a time through a byte pointer. A byte array sits wherever the
  linker has room, odd addresses included: what is cleared or compared by words (the framebuffer) is
  declared as words. `tools/build_host.sh` makes a cast that raises alignment an error; a cast through
  `void *` hides from it, so there are none.
- A number that came from a float is masked or clamped before it indexes anything (`tsin`, the string
  and echo lines), and a loop that runs on a float has a bound.
- A rectangle sent to the panel is at most 20 rows: the device converts it in a buffer that size.
- After a start that failed, `app_init` leaves the saved state alone and comes up as new, so one bad
  start cannot become two.
- `tests/fuzz.c` holds all of this to account: any saved state from an odd address, any panel input
  on every instrument, and the sound on a second thread, under `-fsanitize=address,undefined`.
- `app/fm1.toml` keeps the crash history in flash (`base/tools/fm1_remote.py crashes`), and the stacks
  are painted at every start so the PERF readout can say how much of each was never used.

## Building

- `tools/build_host.sh` then `build/h1host OUTDIR script...` for images and WAVs.
- `tools/build_rfm1.sh [VERSION]` for `dist/h1.rfm1` (clang with the wasm32 target and wasm-ld). Run it in the
  [FM-1 browser simulator](https://github.com/jakehandy/fm1-browser-simulator): open the page and drop the file
  on it, or run `tools/update-h1.sh` there to make it the built-in firmware.
- `./build.sh` builds the device package: fm1-base's `tools/build.py` with `app/` as the app.
- `sh tests/run_tests.sh` runs the device app in fm1-base's host simulator.
- `sh tests/run_fuzz.sh [SEEDS]` runs the crash hunt.
