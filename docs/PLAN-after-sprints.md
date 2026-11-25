# Plan: from the six sprints to a release

Written 2026-11-23, after a review of `virtuosity` (7356451) against `main` (4c0eb47). It follows
[PLAN-expansion-sprints.md](PLAN-expansion-sprints.md), which stays as the record of sprints 1 to 6.
Changed the same day: **GRAIN (sprint 5) comes out**, the owner's decision after findings 2 and 3.

**For an agent picking up a task:** read "Rules for every task" and your own task; each task says what it
needs from the others. Tasks in phase B are independent and can run side by side. Tick a box when the step
is done and verified, not before.

## What the review found

Checked on the host for this review: `main` merged into `virtuosity` in a scratch copy (`git merge-tree`:
no conflicts); the core tests from `tests/run_tests.sh` all pass on the merged tree; `tests/run_fuzz.sh 8`
finds nothing; `tests/cores.c`'s 117 section hashes are the same as `virtuosity`'s. GRAIN was then taken
out of `virtuosity` in a second scratch copy (task A2's recipe): the core tests pass and all 117 sections
are the same again. Not run: the four host-simulator scripts (`tests/basic.txt` and the others), which
need Pillow with libraqm, and anything on an FM-1.

### 1. Flash: the merge leaves about 15 KB, not 67 KB

`main` holds three commits `virtuosity` does not (ed6544f, 6494cf7, 4c0eb47: one render per algorithm for
FM-4, ANALOG, ELECTRIC, SHAPER and STACK). They cost 53.5 KB of flash, and every sprint was measured
without them.

| Build | Image | Free of the 581,564 B slot |
|---|---|---|
| 0.4.0 (064492c, CI 45040570956) | 410,720 B | 170,844 B |
| `main` (4c0eb47, CI 45048374323) | 464,240 B | 117,324 B |
| `virtuosity` (71d7616, CI 45172547638) | 513,260 B | 68,304 B |
| the two merged (the sum; CI has not built it) | about 566,780 B | about 14,800 B |

Taking GRAIN out gives back about 3 KB (the sprint plan's estimate; CI measures it in A3), so about 18 KB
free with all five specialised renders in. What each of those costs (function sizes in the two builds'
`a22.elf`) against what it gained on the FM-1 (4c0eb47's message: points of audio time, eight-note chords
under PLATE):

| Engine | Flash | Audio time | Points a KB |
|---|---|---|---|
| FM-4 | 3.2 KB | -3.2 | 1.0 |
| ANALOG | 14.1 KB | -5.1 | 0.36 |
| STACK | 15.0 KB | -3.0 | 0.20 |
| SHAPER | 10.8 KB | -2.2 | 0.20 |
| ELECTRIC | 10.3 KB | -2.1 | 0.20 |

So the plan's rule (45 KB kept free) is broken by about 27 KB once `main` is merged, and neither later
feature fits. Decision D1 below. Removing GRAIN does not settle this.

### 2. GRAIN was timed at under one grain (settled: GRAIN comes out)

`tools/fm1_profile.py` sets an engine's knobs (`--knob`) but not an effect's, so every GRAIN number in the
sprint plan was taken with GRAIN's knobs as they stood on the unit. At its defaults (SIZE 45, DENSITY 50:
63 ms grains, 11 a second) under one grain plays at a time on average. On the host, for 20 s of audio:
GRAIN at its defaults 5.5 ms, with SIZE and DENSITY at 100 (eight grains always) 22 ms, DELAY 7.4 ms. Eight
grains cost four times what was timed, with GAMELAN already at 79% beside the default.

### 3. FREEZE clicked and thumped when pressed or let go while grains played (settled the same way)

With a 440 Hz sine going in and FREEZE pressed and let go every 150 ms, the largest step between samples
rose 3.5 to 4.8 times, and at PITCH 100 a grain was held on one sample until its window closed (a mean of
0.49 over 9 ms, from a sine at 0.5). `tests/grain.c` checked FREEZE only held and let go into silence. A
fix was tried in a scratch copy and worked; it is not needed now. If GRAIN comes back in a later release,
it comes back with that fix, a check for it, and timing at SIZE and DENSITY 100.

### 4. CYCLE's STEP- algorithms: the right side jumps

`wt_render` rounds the right side's place from the left's gliding one, so the left glides from wave to
wave and the right snaps. Largest second difference over rms, a held note, SCAN 100:

| | Left | Right | Right, with the fix below |
|---|---|---|---|
| STEP-STR, SPREAD 100 | 0.35 | 1.14 | 0.35 |
| STEP-RD, SPREAD 100 | 0.22 | 1.05 | 0.24 |
| STEP-KEY, SPREAD 100 | 0.17 | 0.41 | 0.17 |
| STEP-VOX, SPREAD 100 | 0.67 | 1.19 | 0.73 |
| STRING (smooth), SPREAD 100 | 0.12 | 0.17 | unchanged |

The CHANGELOG's "a glide of a few milliseconds between so nothing clicks" is true of the left only. Whether
it is heard is for ears; the fix is six lines. Task B1.

### 5. Smaller

- `CHANGELOG.md`'s Unreleased section says three things that are no longer so: "there are twelve and eight"
  (fourteen and eight, with GRAIN out), PERC's "the FM-1 decides whether it stays" (it stayed), and SKINS'
  "placeholders until judged by ear" (STUDIO is settled).
- Release items missing from the list: `firmware.json` at the top (the same "12 engines" line as
  `app/fm1.toml`), `fmhub.json`'s `sounds` line (VCSL only: SKINS adds Virtuosity Drums, CYCLE adds AKWF),
  and the site needs fourteen banks, A to N, not A to M.
- `docs/ARCHITECTURE.md`'s paragraph on the saved state is h1's (version 9, 3,344 bytes), not A22's
  version 5. The release checklist sends readers to that file.
- Installs: if the two that needed a power cycle were the two `virtuosity` builds (513,100 and 513,260 B),
  they were also the two largest images installed, the first past 512,000 B. A guess to test, not a
  finding: task C2. The merged image is about 50 KB larger again.

Looked at and found sound: version 4 states reach `h1_state_load` with their own size (`app/app.c`); a
voice changing engine is cleared, so PERC's strings never read SKINS' reader words as floats; SKINS costs
the same at TUNE 100 as at 50 (ADPCM decoding is not what it spends its time on); DICE's clamps; nothing
maps a MIDI value to an engine or effect number.

## Decisions

Made:

- **GRAIN is removed** (2026-11-23). Task A2. The state stays version 5 with its twelve effect slots, so an
  effect added later still needs no new version.
- **D1: all five specialised renders stay** (the owner, 2026-11-23): 0.5.0 ships with 17,872 B free. There
  are no new features waiting; flash is won back when one needs it. C1 says which renders earn their place:
  FM-4 (71% with, 75% without) and SHAPER (70%, 77%); ANALOG, STACK and ELECTRIC peak at 50 to 65% with.
- **D3: 0.5.0** (the owner, 2026-11-23).

The owner's, still open:

- **D1. Which specialised renders stay.** (Decided above.) With GRAIN out: all five, about 18 KB free; FM-4
  only, about 68 KB free (23 KB over the reserve); FM-4 and SHAPER, about 57 KB free. Without GRAIN the peaks nearest
  80% in the sprint plan's tables are SHAPER (77% under PLATE, without its specialised render), PERC
  GAMELAN (76%) and FM-4 (75%). Suggested: keep FM-4's; keep another engine's only if task C1 shows its
  peak past 75% without it, which on those numbers means SHAPER's is the one to look at. Decide after A3
  and C1.
- **D2. By ear:** CYCLE's bank names and 60 waves, SKINS' algorithm names and ORCH's picks, SKINS' PUNCH
  (task B2) once it is in, and GAMELAN's four new modes: 71d7616 shortened them because GAMELAN ran to 81%
  beside GRAIN. Under PLATE the longer ones peaked at 78%, so they could come back; it costs two points of
  the room PERC has.
- **D3. The version number**, and whether the two later features go in this release or the next.
  Decided, above: **0.5.0**. The two later features wait for the next release (none is built).

## Rules for every task

- Work on a branch cut from `virtuosity` after phase A. One task, one branch, one commit or a few.
- The core tests must pass before a commit. The whole of `sh tests/run_tests.sh` needs Pillow with libraqm
  for the host simulator's font; without it, the core part alone (3.5 minutes):
  `sed -n '/^mkdir -p build/,$p' tests/run_tests.sh | CC=cc SAN="-O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w" ASAN_OPTIONS=detect_leaks=0 sh -eu`
  CI runs all of it on a pull request.
- A fix comes with a check in the engine's own test file that fails before the fix and passes after. Say
  both runs in the commit message.
- Bit-exact where nothing was meant to change: `build/a22cores > x.txt` before and after, and name the
  sections that differ. `tests/cores.c` carries each section's tail into the next, so a change in one
  section changes the hashes after it too.
- No new RAM buffer. Say what the task costs in flash if CI built it.
- Do not edit `CHANGELOG.md` or the plan docs in a phase B branch: give the sentence for the CHANGELOG in
  the commit message. Task E2 writes them all in, so branches do not collide.
- Anything on the FM-1 is said as measured, with the command; anything not run is said as not run.
- Never push to `main`, tag, or install on the FM-1 without the owner saying so.

## Phase A: merge, take GRAIN out, measure (first, in order)

### A1. `main` into `virtuosity`

*Done in ee85141. Checked again 2026-11-23 on fbb941d: `tests/run_tests.sh` all passed (117 sections the
same on one core and two), `tests/run_fuzz.sh 8` nothing found.*

- [x] `git checkout virtuosity && git merge main` (no conflicts expected: `main`'s three commits touch
      `core/inst_{analog,electric,fm4,shaper,stack}.c` only).
- [x] Run the core tests and `sh tests/run_fuzz.sh 8`. Expected: all pass, nothing found.
- [x] `build/a22cores` against `virtuosity`'s before the merge: all 117 sections the same.

### A2. GRAIN out

*Done in 3cab816 (GRAIN out, `tests/state.c`'s nine-effect check, `tests/grain.c` gone, `fm1_profile.py`'s
FXK without it). `rg` finds only KIT's "grit grains" and the comet's grains in `core/gfx.c`.*

Tried in a scratch copy of `virtuosity` for this plan: the steps below, then the core tests pass (`state`
74 checks, `echo` without its GRAIN lines) and all 117 sections of `tests/cores.c` are the same as with
GRAIN.

- [x] `git revert --no-commit 73df9b0`. It stops on conflicts, all one-sided:
  - `core/fx.c`, `core/internal.h`: take the reverted side in every hunk (GRAIN's state, `grain_clear`,
    `fx_grain`, the three `fx_grain_*` declarations: 71d7616's sleep in silence goes with them).
  - `tests/run_tests.sh`: drop the two `grain` lines of the header and the two that build and run it; keep
    the `perc` lines beside them.
  - `tests/grain.c`: `git rm`.
  - `CHANGELOG.md`, `docs/PLAN-expansion-sprints.md`: keep HEAD's text for now; E2 rewrites both.
  - `core/ui.c`, `tests/echo.c`, `tests/state.c`, `README.md` revert cleanly.
- [x] `tools/fm1_profile.py`: GRAIN's line out of `FXK`, and out of the `--fx` help text.
- [x] `rg -n -i "grain|freeze|fx_tap_waits" core tests tools app/words.tsv README.md`: nothing left but
      `tests/echo.c`'s "grainy" and KIT's FM-GRIT.
- [x] Keep, from the same commits: state version 5's `NFXSLOT` and `nfx` (45fa9e0), GAMELAN's shorter modes
      and the skipped dead modes (71d7616's PERC half), `tools/fm1_profile.py` itself.
- [x] One check to add in `tests/state.c`: a version 5 state saved with nine effects and effect 8 in hand
      (what the owner's FM-1 holds from the development builds) loads with PLATE in hand, `nfx` 8 and slots
      8 to 11 empty. `h1_state_load` already does this (`st.fx_type >= NFX`, the `NFX..NFXSLOT` loop); the
      check keeps it so. The path for an effect newer than the state has no test until there is such an
      effect again.
- [x] Core tests, `sh tests/run_fuzz.sh 8`, `build/a22cores` (all 117 the same as before the revert).
- [x] Rebuild the simulator (`tools/build_rfm1.sh`), commit `dist/a22.rfm1` with it, push, and open a pull
      request to `main` (do not merge it): CI builds the firmware.

### A3. The flash number

*CI's image: 563,708 B (45191550090), 563,692 B with phase B (45195150042). D1 kept all five renders, so
none was taken out.*

- [x] Read the image size from the CI firmware job's line `ok    image N B; SRAM ...`
      (`gh run view RUN --log | rg "ok +image"`). Expected: about 563,800 B.
- [x] Put the number, and the two tables of finding 1, in front of the owner for D1.
- [x] To take an engine's specialised render out: restore its file to 0.4.0's
      (`git checkout 064492c -- core/inst_stack.c`, as 4c0eb47 did for CHIP and MONO: `virtuosity` never
      touched these five files), run `build/a22cores` (bit-exact, as the specialisation was), and read CI's
      image size again. Do this only for the engines D1 names, after C1 if D1 waits on it.

## Phase B: fixes (independent, side by side, after phase A)

### B1. CYCLE: the STEP- algorithms' right side glides as the left does

*Done in 3073075; `tests/basics.c` checks the right side glides.*

Files: `core/inst_wave.c`, `tests/basics.c` (beside its "CYCLE's STEP-" checks).

- [x] Check first (fails now): STEP-STR, K1 0, SCAN 100, SPREAD 100, FILTER 100, a note held 2.5 s; the
      right channel's largest second difference over its rms is within 1.5 times the left's (3.3 times
      now, 1.0 with the fix).
- [x] The fix, tried in a scratch copy (`v->s[5]` is free in this player: WAVE's and the smooth CYCLE
      sections of `tests/cores.c`, 80 to 87 and 96 to 99, stay the same to the bit):

```diff
--- a/core/inst_wave.c
+++ b/core/inst_wave.c
@@ -65,6 +65,7 @@
         for (i = 0; i < 16; i++) v->s[i] = 0.0f;         /* (both sides start together: SPREAD sets them apart) */
     }
     v->s[4] = wave_place(e, v, b);
+    v->s[5] = (f32)(int)(minf((f32)(WT_WAVES - 1), v->s[4] + 2.2f * e->spread) + 0.5f);   /* (STEP-: the right side's place) */
 }
 
 /* a wave at x (0 <= x < its length), between samples */
@@ -97,7 +98,13 @@
     v->s[4] += (wave_place(e, v, b) - v->s[4]) * 0.3f;
     pl = v->s[4];
     pr = minf((f32)(WT_WAVES - 1), pl + 2.2f * spread);
-    if (e->step) pr = (f32)(int)(pr + 0.5f);               /* (STEP-: the right side on a whole wave too) */
+    if (e->step) {                                         /* (STEP-: the right side on a whole wave too, and it
+                                                            * glides there as the left does: rounding the left's
+                                                            * gliding place made the right jump from wave to wave) */
+        f32 to = (f32)(int)(minf((f32)(WT_WAVES - 1), wave_place(e, v, b) + 2.2f * spread) + 0.5f);
+        v->s[5] += (to - v->s[5]) * 0.3f;
+        pr = v->s[5];
+    }
     ial = imin((int)pl, WT_WAVES - 2);
     iar = imin((int)pr, WT_WAVES - 2);
     ml = pl - (f32)ial;
```

- [x] `build/a22cores` before and after: sections 0 to 99 the same; 100 to 103 (the STEP- algorithms)
      differ, and the ones after them through their tails. Confirm SKINS itself is unchanged with
      `build/a22skins` (its takes run in their own processes).
- [x] `build/a22basics` (its "plays the same whole wave for nearby K1" check must still pass), the core
      tests.

### B2. SKINS hears PUNCH (the ENV page's), as KIT does

*Done in 2be3594, with PUNCH at full strength (`tr->punch + b->punch`) rather than 0.6 times; by ear in D.*

Files: `core/inst_skins.c`, `tests/skins.c`.

KIT adds `b->punch` to its kick's and toms' pitch drop and click (`core/inst_kit.c`, `kit_render`). SKINS'
`Treat.punch` is extra level over the first 6 ms, on TIGHT and MARCH (0.90) and VINTAGE and SHELLAC (0.20).

- [x] `sk_render`: the start's extra level becomes `tr->punch + 0.6f * b->punch` on the kick, snare and
      toms (0.6 is a first value, for D2).
- [x] SKINS' default mod envelope has PUNCH at 35, copied from KIT (`{50, 40, 75, 35}`): make it 0, so
      every algorithm at its defaults is the same to the bit as now and nothing picked by ear moves.
      (A unit that already saved SKINS keeps its 35 and will sound a little harder at the start: the
      owner's FM-1 only, nothing released.)
- [x] Checks: at PUNCH 0 each algorithm's hash is what it was before the change; at PUNCH 100 the kick's
      first 6 ms hold more energy than at 0 and the rest of the hit is the same within 0.5 dB; level stays
      inside the limiter.
- [x] `build/a22skins`, `build/a22cores` (SKINS' sections 104 to 111 differ only because their default
      moved, if at all: name them), the core tests.

### B3. PERC: the test's switch out of the firmware

*Done in e4584fd.*

Files: `core/inst_perc.c`, `core/internal.h`.

- [x] `perc_nmode` and `perc_test_modes` only where `H1_DEVICE` is not defined (the device and the host
      simulator build with it; the unit tests and the simulator's package do not); with it,
      `#define perc_nmode NMODE`.
- [x] `build/a22perc`, `build/a22cores` (all the same), `./build.sh --host` if Pillow is there, else leave
      the device build to CI.

## Phase C: on the FM-1 (after phase B is merged into `virtuosity`; the owner installs)

### C1. Time the merged build

- [x] Install CI's build of `virtuosity` after phases A and B. Note whether the FM-1 comes back by itself
      (for C2). 52bd8db (CI 45195150042, image 563,692 B); see C2.
- [x] `tools/fm1_profile.py all --fx PLATE`: every engine, the table for the release notes and for D1.
      The five specialised engines should be lower than the sprint plan's numbers, the rest within a point.
      2026-11-23: every algorithm under 80%; all 112 average 49%, peak 74% (PERC GAMELAN). With their
      renders FM-4 peaks 71% (was 75% without), SHAPER 70% (was 77%). WIRE BOWED 63% against 59% in
      `docs/notes/HANDOFF-perf-audio.md`'s table, nothing in WIRE changed; the rest within a point. (`all` skipped ELECTRIC, timed
      alone at 50%: its listing counted an engine twice when a PRESET turn did not land; fixed.)
- [x] `tools/fm1_profile.py PERC --fx PLATE --knob 3=100 --knob 4=100`: RING and SPACE at their tops
      together (the sprint plan timed SPACE 100 alone). `--max 80` is the pass mark.
      PASS: peak 78% (HANDPAN), GAMELAN 77%, STEEL 76%; average 64%.
- [x] `tools/fm1_profile.py idle`: the eight effects. PLATE 10%, DELAY 13, FILTER 13, SHIFT 15, CHORUS 15,
      TANK 14, LOFI 14, THUMP 13. (`idle` left the effect on THUMP; it now puts back the one it found.)
- [x] Write the tables into `PLAN-expansion-sprints.md` with the build's commit. If any line is past 80%,
      stop and take it to the owner. ("On the FM-1 at the end"; nothing past 80%.)

### C2. The restart after an install

- [x] From the owner: which builds were the last two installs, and what `fm1_install.py` said
      ("the FM-1 did not come back: power-cycle it" is its own message after 40 s without the app).
- [x] `python3 base/tools/fm1_remote.py crashes` after such an install and a power cycle: a crash record
      means the app started and fell over; none means it never started or never reached USB.
- [x] The guess to test: image size. Install 0.4.0's package (410,720 B), then CI's merged build, then
      0.4.0's again, twice round, noting each time whether it came back. If only the large image fails,
      look at what `base` does between the loader's restart and USB coming up for an image this size.
      2026-11-23, five installs (`fm1_install.py <pkg> --yes`), `crashes` after each: none saved, ever.
      1. 0.4.0 → merged: back by itself, ~19 s.
      2. merged → 0.4.0: USB gone at the switch to update mode, no loader, nothing for 199 s; after a
         replug the loader was there ("update not finished") and the install run again finished it.
      3. 0.4.0 → merged: written, then "did not come back"; it came back by itself ~2.5 min after the restart.
      4. merged → 0.4.0: back by itself, ~25 s.
      5. 0.4.0 → merged: back by itself, ~30 s.
      Both stumbles had the merged build on one side, but it also went through cleanly three times out of
      five, once each way: size not shown, not ruled out. Intermittent; a replug always recovers it.
- [x] This blocks the release: The Firmware Hub's checklist needs an install that ends in the firmware
      running (`docs/RELEASE-CHECKLIST.md`, sections 1 and 3). Installs 1 and 5 did (0.4.0 → merged, the
      way a user updates). The owner chose a known issue in the CHANGELOG over looking into `base` first
      (task E2).

## Phase D: by ear (the owner; an agent prepares, does not decide)

- [ ] CYCLE: render each bank's 15 waves and the alternatives in `tools/akwf.py` to `build/` for
      listening; the owner names the banks and picks; cut again with `tools/gen_wavetables.py`; WAVE must
      stay the same to the bit.
- [ ] SKINS: the algorithm names and ORCH's picks, from `build/samples-preview/`; KIT's 33 hits must be cut
      again unchanged.
- [ ] After either changes a name: `app/words.tsv` and `app/web/words/` (the screen reader's words), the
      engine's picture in `docs/img/a22/`, and DICE's row if a knob's meaning moved.
- [ ] GAMELAN's longer modes, if D2 wants them back: `MA[5].dec` in `core/inst_perc.c`, then C1's PERC
      lines again.

## Phase E: the release (after A to D; in order)

### E1. What names the engines

*Done in 2040bd9: the site, `app/fm1.toml`, `firmware.json`, `fmhub.json` (265 characters; VCSL,
Virtuosity Drums, AKWF), `PRODUCT.md`, `DESIGN.md`, `docs/ARCHITECTURE.md`'s version 5 paragraph.*

The effects are eight again, so what says "eight effects" is right as it stands.

- [x] `tools/build_site.py`: fourteen banks, A to N (CYCLE is M, SKINS N); its counts in words ("Twelve
      engines", "ninety-six") become fourteen and 112.
- [x] `app/fm1.toml`'s `description` and the top-level `firmware.json` (the build writes
      `app/build/firmware.json` from the toml: copy it, do not edit by hand).
- [x] `fmhub.json`: `what` (fourteen engines; keep it under the Hub's 300 characters, as 5ea9fce did) and
      `sounds` (VCSL, Virtuosity Drums and AKWF, all CC0).
- [x] `PRODUCT.md` (lines on twelve engines, the captures of "all twelve engines", "over 96 algorithms"),
      `DESIGN.md` (twelve tabs A to L, `bank-a-fm4` to `bank-l-chip`, "Twelve banks share nine distinct
      inks": add CYCLE's and SKINS' inks from their `InstDef`).
- [x] `docs/RELEASE-CHECKLIST.md`: "all twelve engines", "88 of the 96 algorithms", "over all 96"; add
      lines for DICE and a version 4 state loading with everything kept.
- [x] `docs/ARCHITECTURE.md`: the saved-state paragraph rewritten for A22 (version 5, 2,888 bytes, sixteen
      engine slots and twelve effect slots, version 4 read by offsets).
- [x] `rg -n -i "twelve|12 engines|96 |grain" README.md PRODUCT.md DESIGN.md docs tools app/fm1.toml fmhub.json firmware.json`
      and read what is left: h1's history and the plans may keep theirs.

### E2. The CHANGELOG and the plans

- [x] Unreleased becomes the version (D3) with its date and identity line, as 0.4.0's heading. With a
      "Known issue" for C2's installs (the owner's choice over looking into `base` first).
- [x] GRAIN's entry out. The state's sentence becomes "room for sixteen engines and twelve effects (there
      are fourteen and eight)"; the line "records how many effects it was saved with" stays.
- [x] Correct PERC's and SKINS' stale sentences (finding 5); add each phase B fix from its commit message;
      put C1's numbers where PERC's entry says "measured on a computer".
- [x] `PLAN-expansion-sprints.md`: sprint 5 marked built, measured and taken out, with why (findings 2 and
      3 here) and what it would need to come back; "Flash at the end" with CI's measured sizes and what D1
      left free.

### E3. Version, simulator, merge

- [x] `tools/build_rfm1.sh X.Y.Z` and commit `dist/a22.rfm1`. *fbb941d, version 0.5.0.*
- [x] CI green on the pull request from A2, with the release's test package (`a22-release-test`) installed
      on the FM-1 and the checklist's sections 0 to 3 gone through.
      *2026-11-23: CI 45235662579 green on fbb941d (PR 3). `a22-0.5.0-test.fwsc` installed over 0.4.0 in 21 s,
      back by itself: `FM-1_4122000500`, USB name `A22 FM-1`, no crash saved; 0.4.0's settings (four tracks'
      engines and algorithms, PLATE) kept. Checklist sections 0 to 3 otherwise as for 0.4.0: what needs ears
      or hands is the owner's. A loop jam a tester reported stuttering on 0.4.0, measured on the FM-1 with
      `tools/fm1_profile.py jam` (dense, four tracks): 0.4.0 63 late halves, 113% peak; 0.5.0 as first built 1, 1
      and 11 in three runs, but in the first two the tape had stopped by the end (its takes lost notes: keys
      were read once a frame, and frames took up to 175 ms). With the branch `pictures` merged (keys read
      while drawing, every note kept) 35 and 22, peaks 104 and 106%. Each engine alone costs what it did
      (SHAPER, ELECTRIC, STACK within a point of C1). In the CHANGELOG.*
- [ ] With the owner's word: merge to `main` (the website republishes from `dist/a22.rfm1` and
      `tools/build_site.py`), tag, and the Hub's submission as `docs/notes/SUBMIT-0.4.0.md` did it.

## Phase F: later, only inside what D1 leaves

- The progression generator (about 1 KB) fits under any choice.
- The multi-model engine in slot 15 (5 to 8 KB) fits under the 45 KB rule with FM-4's specialised render
  alone, or FM-4's and one of the two smaller ones (SHAPER or ELECTRIC: about 48 KB left at 8 KB). FM-4's
  with ANALOG's or STACK's lands on the 45 KB line; three or more do not fit. It also needs its own timing
  on the FM-1 at eight notes before it is kept.
- Any change to PERC is timed again with C1's commands.

## Phase G: loop jams, more headroom (the release after 0.5.0)

*Taken up, with the stage timers that rank it, in `docs/PLAN-cpu-headroom.md` (J0 to J7).*

0.5.0 holds the tester's jam (E3), but at full density it peaks at 87 to 91% with one late half, and the
screen falls to 15 to 25 frames a second. Each change below is measured with `tools/fm1_profile.py jam
--clear-tape` (run it twice: its peaks vary by run; LATE and SHED are the steadier numbers), and with
`tests/corestat.c` (scenarios `reddit3`, `reddit4`) for the output staying the same to the bit. A host gain is
not an FM-1 gain: one job list per block once cost the FM-1 1 to 6 points (corestat's header).

- [ ] `block()`: of an odd number of voices, the larger half to the second core, a voice alone kept on the
      first. Bit-exact on the host (all nine corestat scenarios, one core = two); 1 to 3% on the host. Tried
      2026-11-23, not on the FM-1 yet.
- [ ] Each voice's gain, declick and mix into the track done by the core that rendered it, summed in voice
      order by the first (bit-exact): today the first core does that for every voice, group by group.
- [ ] `ring_step`: the tape's pages written while the audio is light, or spaced out (each page write turns the
      interrupts and XIP off for the page program, the audio's included).
- [ ] `input_hz = 5000` (fm1-base: ~4% of the first core; velocity measured at 10000 only, the glow dimmer:
      hands and eyes).
- [ ] `audio_ahead = true` (one half of latency more; absorbs a spike, not a load past 100%).
- [ ] Tape notes keep the part they were recorded with (`tape_fire_ev`), so an overdub after a change of
      ALGO or engine splits its track into two groups, and `track_set` gives the second the first's knobs.
      Decide first: does a track's tape follow the track's engine, as the step sequencer does?
- [ ] The per-frame scans of the tape's events (`eng_seq_info`, `eng_seq_leds`) only when the tape changed.
