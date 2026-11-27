# Plan: 0.7.2, performance and the newer base

Written 2026-11-27, after 0.7.1. It takes the open item from `docs/PLAN-cpu-headroom.md` (J6's second step, the
engines' renders in RAM), what the FM-1 developer wiki says about this chip
([Programming Guide](https://sbegueria.github.io/fm1-dev-wiki/Programming-Guide.html),
[Performance](https://sbegueria.github.io/fm1-dev-wiki/Performance.html),
[Flash Storage](https://sbegueria.github.io/fm1-dev-wiki/Flash-Storage.html),
[Display](https://sbegueria.github.io/fm1-dev-wiki/Display.html)), and what h1's issues showed
(`docs/notes/h1-issues-vs-a22.md`). The release that carries it is 0.7.2.

## Status

- [x] **K0. SHIFT and TANK sleep on the FM-1** (branch `fx-sleep-denormals`, `43d230c`, CI device build 45912377638).
      The FM-1's FPU keeps denormals: a filter state decays to 1.4e-45 and stays there, so an effect that waits for
      exactly 0 never sleeps once something has gone through it (read off an FM-1 with a debug build: SHIFT's first
      allpass at bit pattern 1, awake for good, 16% of the audio time idle instead of 11%). `snap0` in `core/fx.c`,
      `tests/sleep.c denormals`. 0.6.0's J7 numbers (idle 10% for every echo effect) were measured before anything
      had sounded through them, so they never held on the device after the first note until now.
      On the FM-1 (that package, 2026-11-27): after a start with SHIFT chosen 12% idle, effect stage 1% (16% and 6%
      before); after notes through SHIFT, back to 12% and 1% within 15 s. No crash.
- [ ] K1. The newer fm1-base
- [ ] K2. Sliced flash erase
- [ ] K3. The newer FM-1's screen
- [ ] K4. Denormal speed, measured
- [ ] K5. The engines' renders in RAM
- [ ] K6. (only if time) compiler flags, measured
- [ ] K7. Release

## Where things stand (0.7.1, CI 45877821782)

- Image 554,928 B of the 581,564 B slot: **26.6 KB left**.
- SRAM 403,080 of 475,136 B: RAM code 9,780 B (A22's own `.app_ram` 4,352 B, 5 functions), `.data`+`.bss` 80,448 B,
  pool 312,840 B. **72,056 B free, 8,192 of it kept spare: about 64 KB to use.**
- Base: fm1-base `c8e0599` (2026-11-13), bsp felucca-bsp `18eadad`. fm1-base `main` is `3039770` (2026-11-26), 161
  commits on, a fast-forward (ours has no commits of its own). h1 0.3.1 and 0.3.2 ship on `548a2b0`, which `main`
  contains.
- Idle 11% of the audio time, eight-note chords on all 112 algorithms with PLATE 47.7% average and 72% peak (0.6.0's
  numbers; 0.7.0 moved STACK and ELECTRIC back to one render, STACK peaking at 70%).
- A22's `core/` and `app/` hold no 64-bit integer arithmetic, so the wiki's 64-bit multiply fault (an inline 32x32->64
  multiply among 16-bit loads leaves a register one bit off at 360 MHz) does not apply. Keep it that way: any
  `int64_t`/`uint64_t` product goes in a `noinline` function.

## K1. The newer fm1-base

Move `base` to fm1-base `main` (or the newest commit that builds and passes), in one PR of its own before anything
that depends on it.

What it brings that A22 wants:
- USB host (h1 #54, #72): felucca-bsp `503f509` (a 50 ms root reset, more resets with longer recovery for a keyboard
  that does not answer) and `7cc64ce` (a bulk MIDI endpoint declaring a high-speed size used as 64). A Casio PX-160
  says NOT MIDI on our base and works on h1 0.3.1.
- `usb_serial` (`4a82842`): a USB serial number from the chip, so macOS stops naming A22's port "Felucca" and keeping
  another firmware's audio setup. Needs `usb_serial = true` in `app/fm1.toml`.
- Sliced erase, opt-in (`bfafc67`, `03fe71b`, felucca-bsp #46): K2.
- `PLAT_SPARE` (`3039770`): about 13.8 KB of NOINIT's room zeroed at boot, outside the SRAM budget, for a buffer that
  need not survive a reset. Room for K5.
- A bigger app slot, opt-in (`app_end`, `app_clear_end`, `03fe71b`). **Investigate, do not turn on in 0.7.2 without
  an answer:** the update loader writes the app area, so whether an install from The Firmware Hub, M-UPGRADE or
  another firmware's loader handles a bigger A22 decides it. Read `base/docs` and felucca-bsp's loader notes; write
  the answer here.
  **Answer (2026-11-27, fm1-base README "A bigger app slot", `tools/build.py` app_end): off in 0.7.2, and not simply
  turned on later either.** The package carries its own update loader, and that loader writes the whole bigger area,
  so an install from The Firmware Hub or the installer page would write it. But: (1) A22's tape ring is at
  0xC8000..0xDBFFF (`app/app.c` RING_BASE) and its loop and state pairs at 0xDC000/0xDE000, inside the data area a
  bigger slot takes from the bottom: any `app_end` above 0xC8000 writes code over the ring, so the ceiling is
  0xC8000 (212 KB more) unless the ring moves, and moving it loses every tape on update. (2) Going back is unsafe
  with older loaders: only fm1-base's newer loaders compare slots and erase the bigger app's code above their end.
  Felucca's, M-VAVE's (M-UPGRADE) and older fm1-base loaders don't, so a smaller firmware installed after a bigger
  A22 finds A22's code in its data area. (3) m-bootloader's MIDI mode writes [0x4000, app_end) without any loader,
  so it doesn't compare slots or clear anything. With 26.6 KB of image left, 0.7.2 doesn't need it.
- Smaller fixes A22 likely meets: a SysEx stream never ended (`336e462`), MASTER zippering while USB audio is full
  (`68fdaf8`), MIDI to a computer that does not read dropped rather than queued (felucca-bsp `4649f1e`, A22 sends
  clock all the time since 0.7.0), interrupts off around `fm1_clock_restore` (`0068dfe`).

Steps:
1. Read fm1-base's own changelog or `git log c8e0599..main` for anything that changes an app's API, its build
   (`tools/build.py`, `fm1.toml` keys), the linker scripts, or `plat.h`. h1 hit a name clash on the newer base
   (`panel_setup`); A22 vendors `app/hw/panel.h`, which reaches into `lcd.c`'s internals: check it first.
2. Bump the submodule; `./build.sh --host` and `tests/run_tests.sh` locally (a venv with numpy, scipy, pillow on
   PATH); push the branch to `a22` and let CI build the firmware (no JieLi toolchain here). Note the image and SRAM
   lines from the CI log against the numbers above.
3. On the FM-1 (see "Testing on the FM-1"): install, no crash, idle and `tools/fm1_profile.py all` against 0.7.1's
   numbers, the tape and settings kept across the install, a power cycle with the tape playing.
4. USB host: needs a keyboard that said NOT MIDI (ask the owner; the Casio PX-160 reporter on h1 #54 is the known
   case). Without one, check a keyboard that worked still works.

**Done when:** CI green, the FM-1 runs it with nothing slower than 0.7.1 (peak within 1-2 points), the tape and
settings survive, and a USB keyboard still plays.

## K2. Sliced flash erase

Turn on fm1-base's sliced erase (`FM1B_ERASE_SLICED`, see `03fe71b` for how `fm1.toml` asks for it): the 4 KB erase
suspended and resumed, the interrupts off about 1 ms at a time instead of about 18 ms (measured 18,562 us on an FM-1
with 0.7.1's `ERASE`). The wiki's test: audio underruns 6 to 0, each sector 21-25 ms in all instead of 17.9.

Then, in this order, each measured:
1. SAVE while the tape plays: the window with SAVE says LATE 1 on 0.7.1; the aim is LATE 0.
2. The ring's erase after a stop: `ERASE` keeps working (it times wall time, so expect about 21-25 ms a frame now;
   the audio-side measure is LATE). Keep one sector a frame.
3. Only then, a product question for the owner: with no stall, the settings could save by themselves while the tape
   plays (h1 0.3.1 saves after half a minute at most), and the ring's erase would not need silence. Not without
   the owner's yes.

Risk: the wiki says sliced erase is untested on the newer flash part (UC25HQ80 / B36014). Find out whether
felucca-bsp checks the part; if not, keep the old erase on that part.

**Done when:** SAVE mid-play shows LATE 0 on the FM-1, two tries out of two, and a power cycle keeps what was saved.

## K3. The newer FM-1's screen

The wiki: the newer FM-1s (B36014 / UC25HQ80 flash) show washed-out colours or a blank screen unless the panel gets
the full ST7789 setup (porch, gate, VCOM 0x18, supplies, 60 Hz, gammas). A22 sends none of it (`lcd_panel` is not in
`app/fm1.toml`), and runs the panel's SPI at `LCD_BAUD 0` (60 MHz nominal, about 44 MHz measured). h1 0.3.1 added
the setup and slowed the newer units to 12 MHz; 0.3.2 put every unit back on the fast clock, with a SETUP > SCREEN
SAFE choice for a screen that still shows noise.

1. `lcd_panel = true` (`base/device/lcd.c` LCD_SEQ: the datasheet registers; off, the init is byte-identical).
2. On the owner's FM-1: colours and FPS unchanged (note which flash part it has: `fm1_install.py --info` or the
   identity reply).
3. A newer unit: ask in the release notes or the Hub listing for a report; if the fast clock shows noise there, add
   a way to pick 12 MHz (A22 has no SETUP page: a held-button combination at start, or a GLO subpage).

**Done when:** `lcd_panel` is on, the owner's FM-1 looks and runs as before, and the release notes ask newer-unit
owners to report.

## K4. Denormal speed, measured

K0 proved the FPU keeps denormals. Whether arithmetic on them is slow is open (the wiki's Open Questions, item 8).
It matters: the master EQ, the glue envelope and the voices' filters run every sample, and their states decay into
denormals in silence and in long releases.

1. A debug build (a scratch branch, never merged, like `debug-shift-sleep`): time a loop of multiply-adds on normal
   operands and on denormal ones, from the main loop, with TIMER4 (24 MHz) or `plat_ticks24`; say the two times on the
   PERF line or a DBG message.
2. If denormals cost more than about 2x: `snap0` (block rate) on the states that run in silence: `eq_lo`, `eq_hi`,
   `eq_mid`, `glue_env` in `core/fx.c`, and the voices' filter states where a voice can sit in a long release.
   Measure idle and a long PERC/WIRE release before and after.
3. Either way, write the result here (and it answers the wiki's open question).

**Done when:** the number is known and written down, and snaps are added only if it pays.

## K5. The engines' renders in RAM

The tactic measured on this chip (DUB-1: peaks down 6-35%, means 5-25%, dropouts gone, same arithmetic). The voices
are the largest stage in a dense jam (up to 47% of a block, `docs/PLAN-cpu-headroom.md`). A22 has about 64 KB of
SRAM to use (more with `PLAT_SPARE` after K1, and `cache_ram_ways = 2` gives 8 KB of data-cache RAM for a buffer,
freeing SRAM; never 7 ways: flash data reads 57x slower).

Rules (fm1-base `tools/build.py`, `PLAT_RAM_CODE`, `tools/ramcode.py`): RAM code calls only RAM code; nothing through
a pointer unless `ram_code_allow` in `app/fm1.toml` vouches for it; a switch's jump table is data (fine); constant
tables stay in flash (data reads, fine except during an erase). A22 calls each engine's render through a pointer.

Steps:
1. **Measure first.** Which functions are hot and how big: a CI step (or the release workflow's artifacts) listing
   symbol sizes from the ELF (`pi32v2` `nm --size-sort`), and `tools/fm1_profile.py all` with the PERF line's STAGE
   split per engine to rank the peaks (PERC GAMELAN 72%, STACK CHOIR 70%, FM-4 at 0.6.0).
2. **The voices' inner loops of the top engines**, per algorithm where an engine has one render per algorithm
   (FM-4, ANALOG, SHAPER since 0.5.0), with `H1_HOT` (`core/mathx.h`, section `.app_ram`). Add only what fits with
   8 KB kept spare, and keep the dispatcher's pointer call allowed by `ram_code_allow` (or the dispatcher in RAM
   too).
3. **Time each step on the FM-1**: `fm1_profile.py all` and `jam` (the jam clears the tape: ask the owner first).
   Keep a step only if the peak or LATE falls, two runs out of two; otherwise take it out.
4. The sound must be the same to the bit: `tests/cores.c` (one core and two), `tests/a22_test.c` (two cores
   bit-exact on all 112 presets), `tests/sleep.c`.
5. If they do not all fit, the wiki's fallback (copy the engine in hand into a reserved SRAM window when PRESETS
   changes it) is a bigger change: a plan of its own, not 0.7.2.

**Done when:** the FM-1's peak on all algorithms and the jam's peak and LATE fall, SRAM keeps its 8 KB spare, and
every hash holds.

## K6. Compiler flags, measured (only if time)

M-VAVE's FM1 Open build adds `-pi32v2-enable-simd=true`, `-enable-ipra=true`, `-pi32v2-enable-rep-memop`, `-flto`
and `-inline-threshold=5`. fm1-base builds A22's core at `-O2` with none of them. Nobody has benchmarked them on this
chip. One build per flag on a scratch branch, `fm1_profile.py all` against the base line; keep a flag only if it pays
and every hash holds. LTO can grow code and the image has 26.6 KB left.

## K7. Release 0.7.2

The release steps (`docs/RELEASE-CHECKLIST.md` section 7 and the memory of 0.7.0/0.7.1): the CHANGELOG's top section
`## 0.7.2` with the FM-1's numbers, `tools/build_rfm1.sh 0.7.2` and `dist/a22.rfm1` committed, the PR's checks
(`host`, `firmware`) green, merge into `main` (protected), an annotated tag `v0.7.2` on the merge pushed alone
(`git push a22 refs/tags/v0.7.2`; never `--tags`, never to `origin`, which is jakehandy/h1). The workflow builds,
attests and publishes the release itself. Then install the released package on the owner's FM-1.

## Testing on the FM-1

The owner's FM-1 is on USB. Tools (a venv with mido and python-rtmidi; run from `base/tools` or the repo top):
- `fm1_install.py --info`, `fm1_install.py PKG.fwsc --yes`. A device package comes from
  `gh workflow run release.yml -R abektes/a22 --ref BRANCH` (artifact `packages`, about 10 minutes).
- `tools/fm1_profile.py` (`all`, `idle`, `jam --clear-tape`), and its `Unit` class for scripts: `perf(secs)` sends
  HELLO then CC 119 and parses the PERF line (STAGE, FLASH, ERASE).
- `fm1_remote.py` for keys, buttons, encoders, `screen`, `crashes`, `release-all`, `master pot`.

Rules learnt the hard way:
- An install can stumble at the switch to update mode (USB gone, CoreMIDI error -304, or the screen stuck at
  "updating 0"): poll `--info`; if the loader answers ("update not finished"), rerun the install; if USB is gone,
  ask the owner to replug, then rerun. Nothing is lost.
- Scripts using mido can hang on exit on macOS closing the ports: end them with `os._exit`.
- Never press REC or SAVE+key, never CLEAR ALL, without the owner's yes (the tape and snapshots are theirs). End
  every device session with `release-all` and `master pot`, and put back any setting changed for a test.
- A power cycle needs the owner: ask, one step at a time.
- The released package differs from CI's test package of the same code: install the release itself afterwards.
