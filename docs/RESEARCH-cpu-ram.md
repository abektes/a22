# CPU and RAM tactics for A22 on the FM-1

Researched 2026-11-24. The question: what else can make A22 use less CPU and less RAM on the
M-VAVE FM-1, beyond what the firmware already does.

Sources are first-party where they exist: JieLi’s AC791N SDK overview, the FM-1 Firmware
Developers’ Wiki (measurements on real units), fm1-base and this repo, Julius O. Smith’s
*Physical Audio Signal Processing*, Jon Dattorro’s JAES plate paper, and the Välimäki /
Huovilainen oscillator papers. Community hardware notes (SIDOU, fm1-emu, pi32v2-isa) are
cited as such when no JieLi manual is public. pi32v2 has no public instruction-set manual
([fm1-emu report](https://joakimeriksson.github.io/fm1-emu/fm1-emu-report.pdf)).

This file recommends. It does not change the firmware.

## Where A22 stands

Measured on one FM-1 for 0.5.0: eight-note chords on all 112 algorithms with PLATE, average
49% of the audio block, peak 74% (`docs/notes/SUBMIT-0.5.0.md`). At 100% the audio drops out. The
developers’ wiki states the failure mode directly: dropouts come from a few heavy blocks,
not from a high average
([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html)).

SRAM at the 0.4.0 CI build was 443,088 of 475,136 bytes, about 24 KB free after an 8 KB
spare (`docs/PLAN-expansion-sprints.md`). The plan’s rule since then has been that a sprint
does not add a RAM buffer. Flash is the loose resource (about 170 KB free at that build).

The big pool buffers in the current tree:

| Buffer | Size | Where | When it is actually needed |
|---|---|---|---|
| Framebuffer `g_fb_w` | 57,600 B | `core/gfx.c` | every frame |
| Previous-frame copy `prev_w` | 57,600 B | `core/ui.c` | dirty-tile compare |
| `echo_buf` | 64 KB | `core/fx.c` | DELAY, SHIFT, CHORUS, TANK |
| `rev_buf` | 48 KB | `core/fx.c` | PLATE only |
| `tfx_buf` | 48 KB | `core/fx.c` | tape brake history and stutters; written whenever a stutter is not running |
| `ks_buf` | 32 KB | `core/inst_wire.c` | WIRE, 8 × 2048 int16, sized for a 24 Hz string |

One effect is selected at a time (README). `echo_buf` and `rev_buf` are both reserved anyway.

## What is already in place

These are done. Repeating them will not move the numbers.

- Single-precision float only, `-mcpu=r3 -mfprev1 -ffp-contract=off`, DSP files at `-O2`,
  the rest at `-Os` (`base/tools/build.py`, `app/fm1.toml`). That matches the flags the
  wiki records as required for the FPU, and M-VAVE’s own choice of `-fno-fast-math` plus
  `-ffp-contract=off` so a host build can match the device
  ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html)).
- The device has no fused multiply-add (`base/include/fastmath.h`). Host tests flush
  denormals because the FM-1 already does, and a denormal costs the chip nothing
  (`port/host/main.c`). Flushing denormals is not a CPU tactic here.
- Polynomials instead of libm (`base/include/fastmath.h`). `tsin` is a table. PolyBLEP is
  the band-limited step (`core/dsp.h`). That is the table-free method Välimäki and
  Huovilainen published for subtractive oscillators
  ([IEEE Signal Processing Magazine, 2007](https://doi.org/10.1109/msp.2007.323276)).
- Delay lines that dominate RAM are already int16, not float (`core/fx.c`, `core/inst_wire.c`).
- Voices are split across the two cores, odd and even (`core/synth.c`). PLATE’s two tank
  halves run in parallel, and the plate sleeps once the tank is all zeros (`core/fx.c`,
  `docs/notes/HANDOFF-perf-audio.md`). A job that does not return is dropped and the sound continues
  on one core (`base/docs/SECOND-CORE.md`, `cpu2_fallback` in `app/fm1.toml`).
- Two late audio halves in a row shed one voice (`eng_late` in `core/synth.c`). The wiki’s
  “thin the sound instead of clicking” is already the policy
  ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html)).
- The screen is an 8-bit framebuffer with dirty tiles, at most 20 rows per transfer
  (`core/ui.c`, `docs/ARCHITECTURE.md`). A full RGB565 frame is what the wiki says costs
  a repaint; A22 does not send one.
- Samples are ADPCM or int16 single cycles (`docs/ARCHITECTURE.md`, `docs/WAVETABLES.md`).
- fm1-base restores the 360 MHz boot clock, because a firmware left at m-bootloader’s
  320 MHz is about 11% slower and the SPI dividers go slow with it
  ([Clocks and Power](https://sbegueria.github.io/fm1-dev-wiki/Clocks-and-Power.html)).

## The machine, as far as the sources agree

JieLi’s SDK describes the AC791N as a dual-core floating-point DSP, up to 320 MHz, with
I-cache, D-cache, an MMU, and 578 KB of on-chip SRAM, aimed at audio codecs and
noise reduction
([SDK overview](https://doc.zh-jieli.com/SDKWiki/master/Jieli-Tech/fw-AC79_AIoT_SDK/1-overview/1.1-sdk-overview.html)).
Firmwares on the FM-1 run at 360 MHz when M-VAVE’s bootloader started them. The stock
app itself drops to 240 MHz. The stock firmware is single-core, so both cores at 360 MHz
were not what the unit shipped with
([SIDOU hardware notes](https://github.com/dsablic/sidou/blob/main/docs/hardware.md)).
Raising the clock further is not a tactic. The published ceiling is 320 MHz, and core-1
misreads on some units are already the reason A22 keeps a one-core fallback.

What custom-firmware work has established, with the wiki’s tags:

- Code executes from flash through an instruction cache (XIP). An I-cache miss is
  estimated at 400–600 cycles in the SIDOU notes. Both cores share one instruction cache
  (`base/docs/SECOND-CORE.md`).
- DUB-1, on one FM-1 at 360 MHz, moved a 55 KB audio hot path from flash into a 64 KB
  SRAM region. Peaks fell 6–35%, means 5–25%, and the dropouts in that test stopped.
  The arithmetic was identical. The time was flash fetch. Long loops of small functions
  gained the most. A computer profile of the same code did not predict which stage
  peaked on the device
  ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html)).
- M-VAVE’s FM1 Open marks every engine render, the mixers and the effects
  `sram_required`. ZenLegume’s report on the same chip: with DSP in SRAM the load is
  steady; with some of it back in flash the load jitters and underruns, including when
  the other core fetches flash.
- The I-cache is described as 32 KB, 8-way, 32-byte lines, from the SDK’s cache
  registers. The wiki still marks a published measurement of that size as missing
  ([Open Questions](https://sbegueria.github.io/fm1-dev-wiki/Open-Questions.html)).
  The DUB-1 result does not need the exact size: a hot path larger than the cache,
  chased out by UI code between blocks, shows up as peak audio time.
- SIDOU’s ISA notes, from the SDK’s own inline assembly: no packed SIMD. The compiler
  pairs FPU operations when `-mfprev1` is on. There are hardware loops (`rep`) and a
  64-bit multiply-accumulate with two post-increment memory operands. M-VAVE’s link
  line turns on `-pi32v2-enable-simd=true` and `-pi32v2-enable-rep-memop`. Nobody has
  published what those gain on an FM-1
  ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html),
  [SIDOU](https://github.com/dsablic/sidou/blob/main/docs/hardware.md)).
- A CPU erratum: a 32×32→64 multiply inlined next to a 16-bit load can leave a register
  one bit wrong and crash the firmware. Float DSP has not shown it. The workaround
  (a `noinline` multiply) cost an AAC decoder about half its speed
  ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html),
  felucca-bsp `docs/CPU.md`).
- Direct calls reach about ±4 MB. SRAM near `0x01C00000` and XIP near `0x02000000` sit
  on that edge, so a call between them is an indirect call. There is no cache-lock
  instruction
  ([SIDOU](https://github.com/dsablic/sidou/blob/main/docs/hardware.md)).
- fm1-base’s RAM-code region is 32 KB, 24 KB if Wi-Fi is on (`base/docs/SECOND-CORE.md`).
  A22 does not put DSP there today. `.c1_text` exists for a hot second-core job.

The SDK also documents a hardware EQ (cascaded float biquads, up to 63 sections), an
FFT/vector unit on integer vectors, and a sample-rate converter. SIDOU’s survey found
no FM-1 firmware using them. They are confirmed in SDK headers and untested on this
device. Data for the vector unit has to sit in internal RAM
([SIDOU](https://github.com/dsablic/sidou/blob/main/docs/hardware.md)).

## CPU tactics, in the order they pay

### 1. Put the hot audio path in SRAM, after making room for it

This is the tactic with a measurement on the same chip, and it matches how A22 is
built: DSP in flash, UI on core 0 between 32-sample blocks (`CTRL` is 32, about
0.73 ms), both cores sharing the I-cache.

A 55 KB copy, which is what DUB-1 needed, does not fit. Free SRAM is about 24 KB, and
the RAM-code region is 32 KB. The sequence that fits the linker:

1. Free the 48 KB in tactic RAM-1 below (overlay `echo_buf` and `rev_buf`).
2. Grow the existing RAM-code region by that, or by a slice of it. fm1-base already
   copies `.ram_text` / `.c1_text` at boot and opens the second core’s PC limits.
3. Place the functions that run every sample: the plate halves, the master bus, the
   mix, and the render of whichever engines a stage profile says are the peaks.
   Whole engines that are not selected can stay in flash. The wiki’s fallback when
   they do not all fit is to copy the current engine into a reserved SRAM window
   when the player changes engine
   ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html)).
4. Keep calls inside that window direct. A call out to flash is a function pointer,
   the same constraint SIDOU hit and the reason `.c1_text` is documented as a
   self-contained job (`base/docs/SECOND-CORE.md`).
5. Time peaks on the unit with CC 119, the same way 0.5.0 was timed. A host profile
   will not rank the stages.

Expected result, by the DUB-1 A/B and not by a cycle count we can claim for A22:
the erratic part of the peak shrinks; a peak that is real arithmetic (a chord and a
dense engine in the same block) stays.

### 2. Run the plate tank slower

Dattorro’s plate is the network in `rev_buf` (22,534 int16 samples inside 24,576).
In the paper he writes that convincing reverberation is done at 20–24 kHz, because
the high end is absorbed quickly and the output is mixed with the dry signal, and
that his figure is the smallest network he found that still sounds good. The famous
coefficient set is published around a 29.8 kHz rate, not 44.1
([Effect Design, Part 1, JAES 45(9), 1997](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf),
[AES e-library](https://secure.aes.org/forum/pubs/journal/?elib=10160)).

A22 runs that tank at 44.1 kHz, and on the build before the core split the plate
alone was about 25% of a block with silence in it (`docs/notes/HANDOFF-perf-audio.md`). The
sleep path already removes the idle cost. The remaining cost is the sounding plate.

Decimate into the tank and interpolate back out, at half rate (or at his 20–24 kHz).
The sample walks halve. If the overlay in RAM-1 is not done, the buffer halves too,
for the same decay time in milliseconds. The dry path stays at 44.1 kHz. This is a
sound change, in the direction of the paper’s own design point, and it should be
judged by ear against the current plate. It is a better fit than replacing the
topology: he argues the network is already the small one, and that echo density
matters as much as raw delay memory.

The input diffusers stay in series, as they do now. Only the tank rate changes.
The two-core split of the halves still applies.

### 3. Stop wiping all 2048 WIRE samples on note-on

`string_note` walks the whole `KS_MAX` row on every new note: a full clear when the
envelope is dead, or a `>> 2` of all 2048 samples on a legato re-pluck
(`core/inst_wire.c`). A chord of WIRE notes in one 32-sample block is eight of those
walks, on the audio path.

The waveguide only reads about `D` samples behind the write pointer, and `D` is
`sr / f` clamped to the buffer. At the keybed (F3 and up) that is a few hundred
samples, not 2048. The 24 Hz floor (`mtof` clamped at 24 Hz) does need about 1838
samples, so the 32 KB allocation stays. The wipe does not.

Clear or damp the length that will be read (the new `D`, plus the filter state),
not the allocated row. Smith’s statement of the structure is a delay of the period,
with loss lumped at one point
([Karplus–Strong](https://ccrma.stanford.edu/~jos/pasp/Karplus_Strong_Algorithm.html)).
Samples outside that delay are not part of the loop.

This is a peak-shaped cost: it lands on the attack, which is when `eng_late` is
already closest to shedding a voice.

### 4. Split voices by cost, once a profile shows the split is lopsided

Odd/even is fair when the eight voices are the same engine. Four tracks can put a
heavy engine on one core and a light one on the other. fm1-base already describes
the other pattern: a queue both cores pull from, with `volatile` indexes and
`plat_cpu2_sync()` between the write and the read (`base/docs/SECOND-CORE.md`,
LOOM-8).

Build it only after CC 119, or a stage timer, shows one half finishing while the
other is the peak. A queue has its own sync cost. The wiki’s second-core rule still
holds: the one-core fallback has to sound right, because some units retire core 1
([Second Core](https://sbegueria.github.io/fm1-dev-wiki/Second-Core.html)).

### 5. Let the other effects sleep the way the plate does

PLATE ORs every sample it writes and, once the input has been zero for longer than
the longest segment, skips the tank. DELAY, TANK, CHORUS and SHIFT keep walking
`echo_buf` for as long as they are selected. The same predicate — input at the
noise floor, and every sample written since then equal to zero for one full pass
of the line — turns those into a phase update. Idle-with-effect was the measurement
that fell from 38% to 10% when the plate learned this (`docs/notes/HANDOFF-perf-audio.md`).
The other algorithms can have the same idle.

### 6. Keep shedding, and shed one step earlier if peaks sit in the 80s

`eng_late` waits for two late halves, then drops the quietest released voice, then
a voice the hands are not holding, then the oldest. That is the right order. The
wiki’s version of the same idea is to watch the load and thin out before the DMA
half is missed, so the player hears a thinner chord instead of a click.

A threshold on the running peak (the number CC 119 already reports) can drop one
voice, or skip a send that is barely open, while the peak is still under 100%.
One voice at a time, which is what the code does now, keeps the click out.

### 7. Compiler switches that are worth one timed build, and no more

M-VAVE compiles hot files at `-O2` and links with LTO, `-pi32v2-enable-rep-memop`,
`-enable-ipra` and `-pi32v2-enable-simd=true`. A22’s DSP objects are `-O2` and are
not in that LTO link (`base/tools/build.py` uses the gold plugin for the JieLi
Bluetooth objects). One CI build with the DSP in that LTO link, flags unchanged
otherwise (`-ffp-contract=off`, no fast-math), timed on the unit, is the whole
experiment. The wiki says the gains are unpublished. LTO can also grow code and
make the cache worse, so the peak on the device is the result that counts, not
the host.

Hand-written `rep` and memory-operand MAC loops are real instructions on this core
([SIDOU](https://github.com/dsablic/sidou/blob/main/docs/hardware.md),
[pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa)). They belong in a mix or
a delay walk that a timed build has already shown is the peak, and they have to
stay free of the 64-bit-multiply erratum. Float code is the side that has not
crashed.

### 8. Hardware EQ, FFT and SRC, only as a spike off the audio deadline

The master EQ is a few one-poles and one SVF, and it already turns off when LOW,
MID and HIGH sit at 50 (`fx_master_block` in `core/fx.c`). That is not enough work
to justify a peripheral. A hardware biquad cascade would start to matter if a
future effect were a long linear filter with coefficients that stay still for a
block. Modulated and nonlinear filters (the ladder, the shaper, the plate
dampers) are the case SIDOU says the hardware EQ is not for.

The vector unit is integer, wants its buffers in internal RAM, and no FM-1
firmware in that survey programs it. The sample-rate converter is 16-bit only,
which fights the 24-bit audio path. Any of these is a bring-up on one unit, with
the audio interrupt still doing the current float path until the peripheral has
been shown to finish inside the block. A wrong register sequence on a chip with
one flash bank is a bad place to experiment inside the ISR.

## RAM tactics, in the order they pay

### RAM-1. One delay arena for the selected effect

PLATE owns `rev_buf` (48 KB). DELAY, SHIFT, CHORUS and TANK share `echo_buf`
(64 KB). The product plays one effect. A union of the two arrays reserves 64 KB
instead of 112 KB and returns 48 KB to the pool. That is the room tactic 1 wants
for SRAM code, with 24 KB still left over.

Clear the arena on the effect change. The plate’s silence state and the echo’s
write index reset with it. FILTER, LOFI and THUMP need neither buffer; they
already pay the RAM because the arrays are static. The union does not make that
worse.

`tfx_buf` stays outside the union. Brake and stutter run on top of whatever
effect is selected, and the brake reads history that was written while the trick
was off (`core/fx.c`). Overlaying it with the effect arena would erase one or
the other.

### RAM-2. A shorter tape-trick ring, if 278 ms of brake is more than the trick needs

`TFX_FRAMES` is 12,288 frames of stereo int16: 48 KB, about 278 ms at 44.1 kHz.
The stutter length is halved until it fits (`fx.c`). The ring is written on every
sample except during a stutter, so the brake has something to slow down.

Cutting the ring to 8,192 frames frees 16 KB and shortens the longest brake and
the longest stutter by a third. That is a musical limit, not a free lunch. It is
the clean cut if SRAM is still short after RAM-1.

### RAM-3. Share WIRE’s delay memory across the notes that are actually held

A digital waveguide’s memory is the period
([Smith, PASP](https://ccrma.stanford.edu/~jos/pasp/Karplus_Strong_Algorithm.html)).
A22 instead gives every voice a 24 Hz line (2048 samples), all the time, so eight
sub-bass notes are in tune down to the clamp in `string_render`. On the keybed a
note is a few hundred samples. Eight of those are a few kilobytes, not 32.

A shared pool, with each voice taking `ceil(D)` samples when it is struck and
giving them back when it dies, keeps the 24 Hz case for the voices that ask for
it and stops the others reserving it. If the pool is exhausted, clamp that
voice’s pitch the way `D` is already clamped at `KS_MAX`. The worst case is
unchanged: eight notes at 24 Hz still need the 32 KB. The common case is a large
fraction of it back.

Do this after RAM-1. It is more code on the note path, and the note path is
already where tactic 3 is removing work. Each voice must keep its own row for as
long as the two cores can render different voices; `inst_wire.c` already relies
on that (`v->idx` owns the row).

### RAM-4. Replace the previous-frame framebuffer with a per-row check

`g_fb_w` and `prev_w` are both 240 × 240 bytes, 115,200 bytes together. The copy
exists so a tile is sent only when it changed (`core/ui.c`). The wiki’s warning
about clearing 115 KB a frame is about a 16-bit panel buffer; A22 already sends
dirty tiles. The RAM cost of remembering the last frame is still 56 KB.

A 32-bit hash per row is 960 bytes. Recompute it from the row just drawn. If it
matches, skip the row. A collision leaves a stale row until something else
dirties it, so the hash has to cover every byte of the row (not a sample of
pixels). This is the large RAM cut that does not change the sound. It does touch
the picture, and it wants a minute on the device looking for a stuck region
while the screen is animating.

The live framebuffer itself stays. The panel is 240 × 240 and the UI is drawn
into an 8-bit buffer on purpose (`docs/ARCHITECTURE.md`).

### RAM-5. Leave the plate’s length alone unless the tank rate changes

Dattorro’s argument is that this topology is already the small good-sounding
one, and that buying more delay memory is not what makes it better. Shrinking
`rev_buf` below the 22,534 samples the segments use would change the plate into
a different algorithm. Half rate (tactic 2) is the reduction that stays inside
the paper. Jot’s feedback-delay network can be smaller, and it sounds like a
different room
([first-order delay filters](https://ccrma.stanford.edu/~jos/Reverb/First_Order_Delay_Filter_Design.html)).
That is a new effect, not a slimmed PLATE.

## Tactics that look right and fit this chip badly

- **Fixed-point inner loops.** The FPU is the core’s fast path, the compiler
  pairs float operations, and the 64-bit multiply erratum sits on the integer
  side. A Q31 rewrite pays the erratum or the `noinline` tax. Stay in float.
- **`-ffast-math`.** It turns contractions back on. The device has no FMA, the
  host then disagrees, and the tests that hash samples stop meaning anything.
  M-VAVE ships `-fno-fast-math` for the same reason.
- **Packed SIMD, NEON-style.** The public ISA notes describe none. The
  `-pi32v2-enable-simd` flag is a compiler switch with no published FM-1
  benchmark. Rewriting oscillators into vectors of four assumes a unit the
  chip does not show.
- **A higher CPU clock.** 360 MHz is already above the SDK’s 320 MHz, and it
  depends on the bootloader. Both cores at that clock are the configuration
  some units misread.
- **Replacing PolyBLEP with DPW.** Differentiated parabolic waves are cheaper
  per sample and use no table, and they are the wrong tool where the pitch
  moves quickly: the scale factor depends on frequency and wants a divide on
  each change
  ([Välimäki, Huovilainen, ICMC 2005](http://hdl.handle.net/2027/spo.bbp2372.2005.042);
  the cost comparison is in
  [Nam et al., IEEE TASLP 2010](https://mac.kaist.ac.kr/pubs/jnam-taslp2010.pdf)).
  ANALOG’s thru-zero FM is that case. A22’s PolyBLEP is already the cheap
  table-free residual.
- **Denormal flushing for speed.** The host comment records that a denormal
  costs this FPU nothing. The flushes in `fastmath.h` are there so results stay
  normal, not so the loop goes faster.
- **Cache locking.** SIDOU found no such facility.
- **Using the cache arrays as extra RAM.** The SDK can give D-cache ways back
  as RAM. SIDOU marks that as not validated on the FM-1. The wiki leaves “where
  is the rest of the 578 KB?” open. A22’s 475,136-byte budget is the region
  firmwares actually link.

## How to know a change worked

The developers’ wiki and A22’s own PERF line agree on the method.

- Time the block on the device. CC 119 already reports audio average and peak,
  frame time, and stack spare (`docs/ARCHITECTURE.md`, `app/app.c`). TIMER4 is
  the wiki’s clock because it is the 24 MHz crystal and does not follow the CPU
  PLL ([Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html)).
- Keep the peak of a short window, and the dropout count (a render that finished
  after the DMA had moved on). `eng_late` is that event. Average load hid the
  0.4.0 four-track jam that recorded 63 late halves (`docs/notes/SUBMIT-0.5.0.md`).
- Time stages on the heavy block: voices, plate, echo, master, and the WIRE
  note-on wipe. Rank tactics 2, 3 and 5 by those numbers.
- Read the link map after every RAM change. The budget that matters is SRAM
  used against 475,136, with the 8 KB spare kept, plus the RAM-code region’s
  own ceiling.
- Listen. Half-rate plate, a shorter brake, and a shared string pool change the
  instrument. SRAM placement and the effect-buffer union should not, and the
  sample hashes in `tests/cores.c` are the check that they did not.

## A practical order

1. Union `echo_buf` and `rev_buf` (48 KB back, no sound change if the arena is
   cleared on the effect change).
2. Move the measured hot functions into the RAM-code region that room pays for.
   Time peaks on the unit.
3. Add the plate’s silence predicate to the echo-backed effects, and shorten
   the WIRE note-on wipe to `D`.
4. If the plate is still the heavy stage while it is sounding, run the tank at
   half rate and listen.
5. Spend whatever SRAM is left on purpose: a shorter brake ring, a shared WIRE
   pool, or a row hash instead of `prev_w`. Each of those is a product choice
   with a number on it.

## Sources

- JieLi, [AC791N SDK overview](https://doc.zh-jieli.com/SDKWiki/master/Jieli-Tech/fw-AC79_AIoT_SDK/1-overview/1.1-sdk-overview.html). Dual-core float DSP, 320 MHz stated maximum, caches, 578 KB SRAM.
- FM-1 Firmware Developers’ Wiki: [Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html), [Second Core](https://sbegueria.github.io/fm1-dev-wiki/Second-Core.html), [Clocks and Power](https://sbegueria.github.io/fm1-dev-wiki/Clocks-and-Power.html), [Memory Map](https://sbegueria.github.io/fm1-dev-wiki/Memory-Map.html), [Open Questions](https://sbegueria.github.io/fm1-dev-wiki/Open-Questions.html). DUB-1’s SRAM A/B, the 64-bit multiply erratum, M-VAVE’s flags, core 1’s faults.
- [SIDOU `docs/hardware.md`](https://github.com/dsablic/sidou/blob/main/docs/hardware.md). ISA notes from the SDK’s assembly (dual-issue FPU, `rep`, memory MAC, no packed SIMD), cache and SRAM map, hardware EQ / FFT / SRC, call reach.
- [fm1-emu report](https://joakimeriksson.github.io/fm1-emu/fm1-emu-report.pdf) and [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa). The ISA is unofficial; there is no public JieLi manual.
- Jon Dattorro, “Effect Design, Part 1: Reverberator and Other Filters,” JAES 45(9), 1997. [PDF](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf), [AES](https://secure.aes.org/forum/pubs/journal/?elib=10160).
- Julius O. Smith, [Physical Audio Signal Processing](https://ccrma.stanford.edu/~jos/pasp/), in particular the [Karplus–Strong chapter](https://ccrma.stanford.edu/~jos/pasp/Karplus_Strong_Algorithm.html) and [delay-length choice](https://www.dsprelated.com/freebooks/pasp/Choice_Delay_Lengths.html). [Jot’s first-order loss filters](https://ccrma.stanford.edu/~jos/Reverb/First_Order_Delay_Filter_Design.html).
- Vesa Välimäki and Antti Huovilainen, “New approaches to digital subtractive synthesis,” ICMC 2005, [hdl](http://hdl.handle.net/2027/spo.bbp2372.2005.042). Välimäki and Huovilainen, “Antialiasing oscillators in subtractive synthesis,” IEEE Signal Processing Magazine, 2007, [doi:10.1109/MSP.2007.323276](https://doi.org/10.1109/msp.2007.323276). Juhan Nam, Vesa Välimäki, Jonathan S. Abel, Julius O. Smith, “Efficient antialiasing oscillator algorithms using low-order fractional delay filters,” IEEE TASLP 2010, [PDF](https://mac.kaist.ac.kr/pubs/jnam-taslp2010.pdf).
- This repo: `docs/ARCHITECTURE.md`, `docs/PLAN-expansion-sprints.md`, `docs/notes/SUBMIT-0.5.0.md`, `docs/notes/HANDOFF-perf-audio.md`, `base/docs/SECOND-CORE.md`, `app/fm1.toml`, `core/fx.c`, `core/inst_wire.c`, `core/synth.c`, `core/ui.c`.
