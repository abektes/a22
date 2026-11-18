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

## The picture

Instruments draw into a 240 x 240 buffer of 8-bit ink levels: 0 is the bezel's black, 255 is
full ink. A 256-entry palette turns ink into the instrument's colour when the frame is sent.
`h1_present` compares each row with what the panel already shows and reports rectangles of at
most 20 rows, so the device sends only what moved. It also returns the byte count, which the
simulator turns into its screen-link meter.

Motion uses springs integrated with the real frame time, so the art moves the same at any frame
rate and never snaps.

## An instrument

One `InstDef`: a name, an ink colour, four knob names, eight algorithm names, defaults, and five
functions: `block` (once per 32 frames), `note`, `render` (one voice), an optional `post` (the
mix), and `draw`. Adding an instrument is one new file, one line in the table and `NINST` plus one.

## The sequencer

Time is 96 ticks per quarter note. A bar is 384, 288, 480, 288 or 336 ticks for 4/4, 3/4, 5/4,
6/8 and 7/8. The loop recorder stores timed events in RAM and is lost at power off. The step
pattern is 64 steps of up to four notes, on a 1/8, 1/16 or 1/32 grid, and is saved with the
rest of the state. `eng_seq_info` gives the picture what it needs to draw the progress line or
the notches.

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
listen`) and it says `PERF. FPS n. DRAW avg MAX max. SEND avg MAX max. AUDIO avg MAX max. STACK main
interrupt`, the times in milliseconds a frame, the audio as a percentage of each block, both since it was
last asked, and the bytes of each stack never used since the start. If RAM gets tight again, the first things to shrink are the string lines and the reverb
(16-bit samples halve both).

## What the FM-1 does not forgive

A computer shrugs at things that stop the FM-1's CPU, and a start that stops twice in a row is safe
mode. The first h1 did exactly that: it read its saved state's magic as a word straight out of a byte
buffer, the buffer sat at an odd address, and from the first save on it never started again. The rules
that came out of it:

- Bytes from outside (a saved state) are copied out a byte at a time before anything reads them.
  `h1_state_load` takes a blob at any address, and clamps every value it keeps.
- A number that came from a float is masked or clamped before it indexes anything (`tsin`, the string
  and echo lines), and a loop that runs on a float has a bound.
- A rectangle sent to the panel is at most 20 rows: the device converts it in a buffer that size.
- After a start that failed, `app_init` leaves the saved state alone and comes up as new, so one bad
  start cannot become two.
- `tests/fuzz.c` holds all of this to account: any saved state from an odd address, any panel input
  on every instrument, and the sound on a second thread, under `-fsanitize=address,undefined`.
- `app/fm1.toml` keeps the crash history in flash (`base/tools/fm1_remote.py crashes`) and has the
  build check the deepest call path against each stack.

## Building

- `tools/build_host.sh` then `build/h1host OUTDIR script...` for images and WAVs.
- `tools/build_rfm1.sh [VERSION]` for `dist/h1.rfm1` (clang with the wasm32 target and wasm-ld). Run it in the
  [FM-1 browser simulator](https://github.com/jakehandy/fm1-browser-simulator): open the page and drop the file
  on it, or run `tools/update-h1.sh` there to make it the built-in firmware.
- `./build.sh` builds the device package: fm1-base's `tools/build.py` with `app/` as the app.
- `sh tests/run_tests.sh` runs the device app in fm1-base's host simulator.
- `sh tests/run_fuzz.sh [SEEDS]` runs the crash hunt.
