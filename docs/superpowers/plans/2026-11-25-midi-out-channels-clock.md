# MIDI out: a channel per track, and MIDI clock — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A DAW recording A22's USB MIDI out gets T1–T4 on channels 1–4 (notes and knob CCs, recorded automation
included), and follows A22's tempo, Start and Stop through MIDI clock.

**Architecture:** The core (`core/synth.c`, `core/ui.c`) puts the channel into the status byte it already hands
to `eng_midi_fn`, and sends 0xF8 / 0xFA / 0xFC through the same function from `block()`. The device app
(`app/app.c`, `midi_send`) lets real-time messages through the USB queue the way it lets note-offs through.
No new screen, no new setting, no change to `H1State`.

**Tech Stack:** C99, no libc in `core/`; tests are C programs on the core alone (`tests/*.c`, built with
`-fsanitize=address,undefined`) and scripts for fm1-base's host simulator (`tests/*.txt`), both run by
`sh tests/run_tests.sh`.

**Spec:** `docs/superpowers/specs/2026-11-25-midi-out-channels-clock-design.md`

## Global Constraints

- Channel = track: T1 → channel 1 (status low nibble 0), T2 → 2, T3 → 3, T4 → 4.
- Playing the keys on T1 sends exactly what it sends today (0x90 / 0x80 / 0xB0).
- A note-off always goes out on the channel its note-on went on.
- Clock: 0xF8 every 4 sequencer ticks (96 ticks a beat → 24 a beat), at TEMPO (`st.glo[0]`), never scaled
  by the tape's SPEED, untouched by SWING. It runs whether stopped or playing.
- Start 0xFA when the tape or step pattern goes from stopped to running; Stop 0xFC when it stops. Never re-sent
  as the loop goes round. No Song Position Pointer.
- Knob CCs stay CC 20–23, value `(k * 127 + 50) / 100` from the 0..100 knob.
- HUSH (hold HOME) sends CC 123 on channels 1–4.
- Nothing that came in over MIDI is sent back (unchanged).
- MIDI in is unchanged; no clock in.
- `core/` stays C99 with no libc and no malloc.

## Review Focus

1. **A key held while the track changes** (EDIT + PRESETS with a key down): its note-off must go out on the
   old channel, not the new one, or the DAW holds the note for ever. Test: Task 1 `key_held_across_track_change`.
2. **The arpeggiator running while the track changes:** notes it started on T1 must end on channel 1 when it
   releases them on T2. Test: Task 1 `arp_across_track_change`.
3. **The tape stopped, cut, cleared or wrapped with notes sounding** (`tape_release_seq`, `tape_release_trk`):
   their offs must go on each voice's own track. Test: Task 1 `tape_tracks_on_their_channels` (stops mid-note
   and checks ons = offs per channel).
4. **A busy USB queue:** clock must not be dropped while notes and CCs are being held back. The host
   simulator's queue never fills (`plat_midi_out_room` gives 64 there), so no automated test can reach it:
   Task 3 Step 5 checks it on the FM-1 (a DAW synced to A22 holds its tempo with a chord and knob moves going on),
   and the reviewer reads `midi_send`.
5. **Swapping TAPE and STEP or HUSH while playing** must send Stop once, and a later PLAY must send Start once.
   Test: Task 2 `start_stop_edges`.

---

### Task 1: The channel of every note and CC

**Files:**
- Modify: `core/synth.c` (`mout` at :115 and its callers: :520, :531, :545, :567, :591, :627, :631, :759, :777,
  :785, :806, :888, :899, :922, :925, :1204, :1221; HUSH at :1599; `tape_fire_ev`'s `LEV_AUTO` branch at :907)
- Modify: `core/ui.c:930` (`set_knob`)
- Modify: `core/h1.h:27-33` (the comment on MIDI out)
- Create: `tests/midiout.c`
- Modify: `tests/run_tests.sh` (build and run `tests/midiout.c` after `basics`, and list it in the header comment)

**Interfaces:**
- Produces, in `core/synth.c` (file-static):
  - `static void mout_trk(int trk, int on, int note, int vel)` — note on/off on channel `trk & 3`.
  - `static void mout_live(int on, int note, int vel)` — for keys, chords, arp, Riff, Shake: at note-on stores
    `cur_trk` in `static uint8_t live_ch[128]` and sends on it; at note-off sends on `live_ch[note]`.
  - `mout` is removed; every caller uses one of the two.
- Which caller uses which:
  - `mout_live`: arp (:520, :531), Shake (:545, :567, :591, :627, :631), `route` (:759, :777, :785, :806).
  - `mout_trk(voices[n].trk, …)`: `tape_release_seq` (:888), `tape_release_trk` (:899).
  - `mout_trk(trk, …)`: `tape_fire_ev` (:922, :925), `step_fire` (:1221, `trk` there).
  - `mout_trk(step_st[i], …)`: `step_release` (:1204).
- Tape automation: in `tape_fire_ev`'s `LEV_AUTO` branch, send `eng_midi_fn(0xB0 | trk, 20 + k, (v * 127 + 50) / 100)`
  with `v` the clamped 0..100 value, muted or not.
- `set_knob` in `core/ui.c`: status `0xB0 | (st.track & 3)`.
- HUSH: `0xB0 | c, 123, 0` for c = 0..3, still only when `!a` (from the panel).

- [ ] **Step 1: Write the failing tests in `tests/midiout.c`**

Harness copied from `tests/basics.c` (`go`, `tap`, `note`, `mo_fn`, `CHECK`, `loop_state`) and `tests/tracks.c`
(`track(int d)`), plus:

```c
static int mo_n(uint8_t status, int d1);            /* (d1 < 0: any) count of exact status */
static int balanced(void);                          /* for every channel 0..3 and note: ons == offs */
```

Tests, each starting `h1_init(44100); nmo = 0; h1_midi_out(mo_fn); go(100);`:

```c
/* keys on T2 */
track(+1); note(7, 100);
CHECK(mo_n(0x91, 60) == 1 && mo_n(0x81, 60) == 1, "a key on T2 goes out on channel 2");
CHECK(mo_n(0x90, -1) == 0, "and nothing on channel 1");

/* key_held_across_track_change */
h1_key(7, 1); go(100); track(+1); go(100); h1_key(7, 0); go(100);
CHECK(mo_n(0x90, 60) == 1 && mo_n(0x80, 60) == 1, "a key held across a track change ends on its own channel");
CHECK(mo_n(0x81, 60) == 0, "not on the new one");

/* arp_across_track_change: ARP_UP on T1, keys 7 and 11 held, 600 ms, track(+1), 600 ms, keys let go, 500 ms */
CHECK(mo_n(0x90, -1) > 0 && balanced(), "the arpeggiator's notes end on the channel they began on");

/* tape_tracks_on_their_channels: a take of three keys on T1 (REC, keys, REC), track(+1), REC while playing
 * (overdub) three keys and a K1 turn, REC; nmo = 0; go(9000) (a pass of the 4-bar tape); PLAY mid-pass to stop */
CHECK(mo_n(0x90, -1) > 0 && mo_n(0x91, -1) > 0, "the tape plays T1 on channel 1 and T2 on channel 2");
CHECK(balanced(), "every tape note ends on its own channel, stopped mid-note too");
CHECK(mo_n(0xB1, 20) > 0 && mo_n(0xB0, 20) == 0, "T2's recorded knob goes out as CC 20 on channel 2");

/* step_notes_on_their_track */
st.seq[0] = SEQ_STEP; memset(st.steps, 0, sizeof st.steps);         /* (as tests/tracks.c:188) */
st.steps[0][0] = 61; st.step_trk[0][0] = 2;                         /* note 60 on T3, step 1 */
tap(H1_BTN_PLAY); go(3000); tap(H1_BTN_PLAY); go(200);
CHECK(mo_n(0x92, 60) > 0 && balanced(), "a step on T3 goes out on channel 3, with its offs");

/* knob_cc_on_track_channel */
track(+2); h1_encoder(H1_ENC_K1, 5); go(100);
CHECK(mo_n(0xB2, 20) > 0 && mo_n(0xB0, 20) == 0, "a knob on T3 goes out as CC 20 on channel 3");

/* hush_on_four_channels */
h1_button(H1_BTN_HOME, 1); go(1000); h1_button(H1_BTN_HOME, 0); go(100);
CHECK(mo_n(0xB0, 123) == 1 && mo_n(0xB1, 123) == 1 && mo_n(0xB2, 123) == 1 && mo_n(0xB3, 123) == 1,
      "HUSH sends CC 123 on channels 1 to 4");

/* t1_unchanged */
note(7, 100);
CHECK(mo_n(0x90, 60) == 1 && mo_n(0x80, 60) == 1, "a key on T1 goes out on channel 1 as before");
```

- [ ] **Step 2: Run it and see it fail**

Run: `cc -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w core/*.c tests/midiout.c -o build/a22midiout -lm && build/a22midiout`
Expected: FAIL lines for channel 2/3/4 checks (everything goes out on channel 1 today).

- [ ] **Step 3: Add `mout_trk`, `mout_live` and `live_ch`, change every caller, the `LEV_AUTO` CC, HUSH, and `set_knob`, as in Interfaces**

Update the `h1.h` comment: notes on the channel of their track, CC 20–23 on the track in hand's channel and the
tape's automation on its track's, CC 123 on channels 1–4.

- [ ] **Step 4: Run it and see it pass, then the whole suite**

Run: `build/a22midiout` → no FAIL lines. Then `sh tests/run_tests.sh` → exits 0 (`basics`, `tracks`, `tape`,
`chords`, `looper`, `dice`, `a22_test` mask the channel already and must still pass).

- [ ] **Step 5: Commit**

```bash
git add core/synth.c core/ui.c core/h1.h tests/midiout.c tests/run_tests.sh
git commit -m "MIDI out: each track on its own channel, knob and tape automation CCs too, HUSH on all four"
```

### Task 2: MIDI clock, Start and Stop from the core

**Files:**
- Modify: `core/synth.c` (`block()` at :1910-1940)
- Modify: `core/h1.h:27-33` (the comment)
- Modify: `tests/midiout.c`, `tests/basics.c` (`mo_fn`: real-time bytes counted apart, not stored)

**Interfaces:**
- Consumes: `eng_midi_fn` (unchanged signature); `loop_state` (`LOOP_REC`, `LOOP_PLAY`, `LOOP_DUB` are running),
  `step_playing`, `st.seq[0]`.
- Produces, file-static in `core/synth.c`:
  - `static f32 clk_acc;` ticks since the last 0xF8.
  - `static int xport_was;` running last block.
  - `static int xport_running(void)` — `st.seq[0] == SEQ_STEP ? step_playing : (loop_state == LOOP_REC || loop_state == LOOP_PLAY || loop_state == LOOP_DUB)`.
  - `static void clock_tick(f32 dticks)` — called in `block()` after `step_tick` / `tape_tick` and `arp_tick`,
    with the unscaled `dticks` computed from TEMPO. On a stopped→running edge: send 0xFA, then 0xF8, and set
    `clk_acc = 0`. On running→stopped: send 0xFC. Then `clk_acc += dticks`; while `clk_acc >= 4`: subtract 4, send 0xF8.
  - Real-time sends are `eng_midi_fn(0xF8 / 0xFA / 0xFC, 0, 0)`, only when `eng_midi_fn` is set.
  - `clk_acc`, `xport_was` (and Task 1's `live_ch`) are cleared where `eng_init` clears the sequencer's state
    (`core/synth.c:2236` area), so `h1_init` starts stopped with no Start owed.
- Test harness: `mo_fn` puts status >= 0xF8 into `static int rt_n[8]` (index `status - 0xF8`) and a
  `static int rt_seq[64], nrt;` ordered log of 0xFA / 0xFC, instead of `mo[]`.

- [ ] **Step 1: Write the failing tests in `tests/midiout.c`**

```c
/* clock_per_bar: st.glo[0] = 120; stopped; go(2000) (one bar of 4/4 at 120) */
CHECK(rt_n[0] >= 95 && rt_n[0] <= 97, "96 clocks a bar at 120, stopped");
/* the same at st.glo[0] = 90: go(2667) */
CHECK(rt_n[0] >= 95 && rt_n[0] <= 97, "96 clocks a bar at 90");

/* clock_ignores_speed: a take on T1, st.tape[0] = TAPE_SPD_2X, playing, st.glo[0] = 120, go(2000) */
CHECK(rt_n[0] >= 95 && rt_n[0] <= 97, "the clock stays at TEMPO when the tape plays at 2X");

/* start_stop_edges */
/* REC then a key: one Start */           CHECK(nrt == 1 && rt_seq[0] == 0xFA, "the first key of a take sends Start");
/* go(9000) across a loop wrap */          CHECK(nrt == 1, "no Start again as the loop goes round");
/* PLAY (stop) */                          CHECK(nrt == 2 && rt_seq[1] == 0xFC, "PLAY stops, Stop goes out");
/* PLAY (play) */                          CHECK(nrt == 3 && rt_seq[2] == 0xFA, "PLAY plays, Start goes out");
/* hold HOME 1000 ms */                    CHECK(nrt == 4 && rt_seq[3] == 0xFC, "HUSH stops, one Stop");
/* PLAY, then hold SEQ (swap to STEP) */   CHECK(rt_seq[nrt - 1] == 0xFC, "swapping TAPE and STEP stops");
/* step pattern: PLAY, then PLAY */        CHECK(rt_seq[nrt - 2] == 0xFA && rt_seq[nrt - 1] == 0xFC, "the step pattern sends Start and Stop");

/* armed_is_not_running: REC (armed), go(500) */
CHECK(nrt == 0, "REC armed and waiting sends no Start");
```

- [ ] **Step 2: Run it and see it fail**

Run: the Task 1 build command for `tests/midiout.c`.
Expected: FAIL on every clock and Start/Stop check (`rt_n[0] == 0`).

- [ ] **Step 3: Implement `clock_tick` and its state as in Interfaces; give `tests/basics.c`'s `mo_fn` the same real-time filter**

Update the `h1.h` comment: clock 24 a beat at TEMPO all the time, Start and Stop with the tape or the step pattern.

- [ ] **Step 4: Run it and see it pass, then the whole suite**

Run: `build/a22midiout` → no FAIL lines; `sh tests/run_tests.sh` → exits 0. Then `sh tests/run_fuzz.sh` → no
sanitizer report.

- [ ] **Step 5: Commit**

```bash
git add core/synth.c core/h1.h tests/midiout.c tests/basics.c
git commit -m "MIDI out: clock at TEMPO, Start and Stop with the tape and the step pattern"
```

### Task 3: Clock through the USB queue first, on the device; the docs

**Files:**
- Modify: `app/app.c:280-293` (`midi_send` and its comment)
- Create: `tests/midiclock.txt`
- Modify: `tests/run_tests.sh` (run `tests/midiclock.txt` in the host app after `dice.txt`)
- Modify: `README.md` ("USB audio and MIDI", the MIDI out paragraph; "Compatibility" if it names channel 1)
- Modify: `CHANGELOG.md` (the unreleased 0.6.0 section)

**Interfaces:**
- Consumes: Task 1's statuses (`0x8n`, `0x9n`, `0xBn`) and Task 2's real-time bytes, all through `midi_send`.
- `midi_send`'s priority test becomes: sent while `room > 0` when `status >= 0xF8`, or `(status & 0xF0) == 0x80`,
  or `(status & 0xF0) == 0xB0 && d1 == 123`; otherwise only while `room > MIDI_SPARE`. The packet packing is unchanged
  (code index 0xF for 0xF8 / 0xFA / 0xFC comes out of `status >> 4`).

- [ ] **Step 1: Write `tests/midiclock.txt` for the host simulator: the whole path, core to USB**

This script proves Tasks 1 and 2 reach USB through `app.c`, so it passes as soon as it is written (the host's
queue never fills, so it cannot show the priority change; Step 5 does). Using the host's script words (`out`,
`expect_out USB B0 B1 B2`, `expect clocks_out …`, as in `tests/dice.txt` and the list at the top of
`base/host/plat_host.c`):
- At TEMPO 120, wait 2000 ms: `clocks_out` rises by 95 to 97 (`expect clocks_out >N` and `<M` around it).
- Press REC, then the key for note 60: `expect_out USB FA 00 00`.
- Press PLAY (stop): `expect_out USB FC 00 00`.
- EDIT + PRESETS one detent (T2), press and release the key for note 60: `expect_out USB 91 3C 7F` and
  `expect_out USB 81 3C 00`.
- Hold HOME 1000 ms: `expect_out USB B3 7B 00`.

- [ ] **Step 2: Run it and see it pass**

Run: `./build.sh --host && app/build/host/a22_host tests/midiclock.txt app/build/host`
Expected: every `expect` ok, no `FAIL`.

- [ ] **Step 3: Change `midi_send` as in Interfaces, and its comment**

- [ ] **Step 4: Run the script and the whole suite**

Run: `sh tests/run_tests.sh` → exits 0, `midiclock.txt` with no `FAIL`.

- [ ] **Step 5: Check on the FM-1 with a DAW** (by hand, using the remote test tools in memory `a22-hardware-testing`)

Install the dev build, open a DAW with external MIDI sync: its tempo reads A22's TEMPO, PLAY on A22 starts the
DAW's transport and stops it, a tape with parts on T1 and T2 records onto two channels, and CC 119's perf
readout shows no change in audio load from the clock. Note the result in the commit message.

- [ ] **Step 6: The docs**

README, "MIDI out" paragraph: over USB only; notes on the channel of their track (T1 on 1 … T4 on 4), keys and the
arpeggiator on the track in hand's; CC 20–23 on the same channels as the knobs turn and as the tape plays
their automation back; MIDI clock at TEMPO all the time, Start and Stop with the tape and the step pattern;
HUSH sends CC 123 on channels 1–4. One line that a synth driven from A22 needs to listen on all channels (omni)
to hear T2–T4. CHANGELOG 0.6.0: the same in two or three lines.

- [ ] **Step 7: Commit**

```bash
git add app/app.c tests/midiclock.txt tests/run_tests.sh README.md CHANGELOG.md
git commit -m "MIDI out: clock and Start/Stop never dropped for a busy queue; README and CHANGELOG"
```
