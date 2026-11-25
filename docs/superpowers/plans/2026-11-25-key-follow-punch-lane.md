# KEY FOLLOW, PUNCH and LANE — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the tape and step sequencer follow ROOT/SCALE per track (KEY), punch-in effects on FX + white key, and a per-bar key/DEG lane on the tape — after freeing flash by taking out STACK's and ELECTRIC's specialised renders.

**Architecture:** the note mapping is a small module (`core/keymap.c`). Recorded notes carry their key in `LEv.rsv`, and playback maps them through a per-track table of what was sent. Punch and lane entries are tape events of new kinds stored at tick + 15,360 (a tail the note scans never reach), each with its own scan. The UI adds MIX → KEY, an FX hold with a punch strip, and TAPE → LANE, all in the existing four-column page style.

**Tech Stack:** C99 core (no libc, no malloc); core-alone tests in `tests/*.c` (address and undefined-behaviour checkers); fm1-base host-simulator scripts `tests/*.txt`; `tests/run_tests.sh`, `tests/run_fuzz.sh`; CI `release.yml` for device builds; `tools/fm1_profile.py` and `base/tools/fm1_remote.py` on the FM-1.

**Spec:** `docs/superpowers/specs/2026-11-25-key-follow-punch-lane-design.md`

## Global Constraints

- Saved state stays version 5; `H1State` layout unchanged. `rsv[0]` is the step pattern's key, `rsv[1]` stays 0, `Track.rsv` bit 0 is KEY.
- **Key byte:**
  - bit 7: valid;
  - bits 4–6: scale (0 CHROM, 1 MAJOR, 2 MINOR, 3 PENTA, 4 MPENT, 5 BLUES, 6 DORIAN, 7 WHOLE);
  - bits 0–3: root (0–11).
- **The tape blob:**
  - `h[5]` bit 0 means "events carry keys";
  - kinds `LEV_OFF 0`, `LEV_ON 1`, `LEV_AUTO 2`, `LEV_PUNCH 3`, `LEV_KEY 4`;
  - flags bit 3 is `LEV_MIDI` (exists), bit 4 is `LEV_GLOBAL`;
  - punch and lane events at tick + `LEV_TAIL` (15,360), in memory and in the blob.
- **Engines:** KIT (8) and SKINS (13) are drums and never follow, judged by the event's `part`.
- **KEY** is OFF by default. Tapes and states from 0.6.0 sound unchanged.
- **The mapping:**
  - seven-note to seven-note (MAJOR, MINOR, DORIAN) keeps octave, degree and accidental;
  - otherwise move by R₁ − R₀, then snap to S₁ unless S₁ is CHROM;
  - the root moves by −6 to +5 semitones;
  - clamped to 0–127.
- **Punches:**
  1. BRAKE;
  2. ST 1/8;
  3. ST 1/16;
  4. ST 1/32;
  5. LPF (open to ~250 Hz over one bar);
  6. HPF (~20 Hz to ~2 kHz over one bar);
  7. CRUSH (6 bits, rate ÷6);
  8. GATE (1/16, 2 ms edges).

  ~10 ms ramps. Punch n (1–4) is TRICK n.
- **Gestures:** hold FX + the eight `SNAP_KEYS` play punches; other keys play normally. FX opens its page on release unless a punch was played.
- **LANE:** entries are ROOT (127 = "—"), SCALE (15 = "—") and DEG (7 = "—"), at most one per bar.
  - The key in force is the latest entry at or before the bar (no wrap), with GLO filling any "—", and DEG I by default. DEG does nothing in CHROM.
  - Recording over DEG ≠ I on a KEY-on track stores the note moved down DEG − 1 steps.
- **Flash fallback** if a phase doesn't fit: drop CRUSH and GATE (their keys do nothing); if still over, stop and ask.
- **On the device:** never press REC or SAVE on the user's FM-1 without asking; end every device session with `release-all master pot`; check the track in hand after any hung remote call.

## Review Focus

1. **KEY turned on while the tape plays and notes sound.** Those notes must end on the pitch they started on. Test: Task 3 `key_on_mid_note`.
2. **A step written on a KEY-on track while another track is in hand.** The step's own track decides whether it's mapped, not the track in hand. Test: Task 4 `step_mapping_uses_step_track`.
3. **FX held, a punch key pressed, and SAVE or EDIT pressed too.** No snapshot is saved and no bypass toggled. Test: Task 7 `fx_punch_ignores_save_edit`.
4. **The loop wrapping while a tape punch is mid-way.** The punch ends at the wrap and starts again at its own tick on the next pass, never stuck on. Test: Task 8 `tape_punch_across_wrap`.
5. **BARS turned down below a lane entry, then up.** The entry comes back and applies, and the LANE cursor clamps to the tape's bars. Test: Task 10 `lane_entry_past_bars`.

## File Structure

- `core/inst_stack.c`, `core/inst_electric.c` (Task 0): one render each.
- `core/keymap.c` (new): the key byte and the mapping. Declared in `core/internal.h`.
- `core/synth.c`:
  - recording keys, playback mapping, `tape_sent`/pitch counts;
  - the step key;
  - punch state and its tape events;
  - lane events, cache and the key in force;
  - the blob's `h[5]`, `e[7]` and tail.
- `core/fx.c`: the punch stages; TRICK's override.
- `core/ui.c`: MIX → KEY; the FX hold, punch keys and strip; TAPE → LANE and the deck's lane marks.
- `core/h1.c`: state load and defaults for `rsv[0]` and `Track.rsv`.
- `core/internal.h`: `INST_KIT`, `INST_SKINS`, `LEV_TAIL`, new events `EV_PUNCH`, `EV_LANE`, and the new engine calls.
- **Tests:** `tests/keymap.c`, `tests/keyfollow.c`, `tests/punch.c`, `tests/lane.c` (new); `tests/keyfollow.txt`, `tests/punch.txt`, `tests/lane.txt` (new host scripts); `tests/fuzz.c`, `tests/run_tests.sh`, `tests/state.c` (extended).
- **Docs:** `README.md`, `CHANGELOG.md`, `core/h1.h` comments.

---

## Step 0

### Task 0: STACK's and ELECTRIC's single render

**Files:**
- Modify: `core/inst_stack.c` (`cluster_render_a` and its switch at ~:259–266)
- Modify: `core/inst_electric.c` (`elec_render_a` and its switch at ~:232–239)

**Interfaces:** none changed outside the two files.

- [ ] **Step 1: Record the sound before.** On the current HEAD:
  - `cc -std=c11 -O2 -ffp-contract=off -w core/*.c tests/cores.c -o build/cores_before -lm -lpthread && build/cores_before > build/cores_before.txt`
  - the same for `tests/sleep.c` → `build/sleep_before.txt`.
- [ ] **Step 2: Make each render a plain static function** that takes `int algo` (not `const`, not `always_inline`). The wrapper calls it once with `algo` instead of the eight-case switch.
- [ ] **Step 3: Compare the sound to the bit.**
  - Run: rebuild both programs, then `cmp build/cores_before.txt build/cores_after.txt` and the same for sleep.
  - Expected: no difference.
- [ ] **Step 4: The whole suite.** Run: `sh tests/run_tests.sh` (with the Pillow venv on PATH). Expected: `run_tests: all passed`.
- [ ] **Step 5: Commit**, with the message "STACK and ELECTRIC: one render again, for flash (bit-exact)".
- [ ] **Step 6: The device build and its size.**
  - Run: `git push a22 key-follow-punch-lane` and `gh workflow run release.yml -R abektes/a22 --ref key-follow-punch-lane`.
  - Read the image size in the run's firmware job log.
  - Expected: about 25 KB smaller than 567,884 B.
- [ ] **Step 7: The audio time on the FM-1.**
  - Install the build (`fm1_install.py PKG --yes`).
  - Run: `tools/fm1_profile.py STACK --fx PLATE` and `tools/fm1_profile.py ELECTRIC --fx PLATE`.
  - Expected: every algorithm's peak ≤ 80%, LATE 0, SHED 0.
  - Note the numbers in the commit or the PR.

## Phase 1: KEY FOLLOW

### Task 1: The mapping (`core/keymap.c`)

**Files:**
- Create: `core/keymap.c`, `tests/keymap.c`
- Modify: `core/internal.h`, `tests/run_tests.sh`

**Interfaces:** Produces:
- `#define KEY_VALID 0x80u`
- `uint8_t key_byte(int root, int scale)`, giving `KEY_VALID | scale << 4 | root`
- `int key_root(uint8_t k)`, `int key_scale(uint8_t k)`
- `int key_map(int note, uint8_t from, uint8_t to)`: 0–127; the note unchanged if either key isn't valid, or if they're equal
- `int key_steps(int note, uint8_t key, int steps)`: the note moved `steps` scale steps (negative allowed) in `key`, keeping its accidental; unchanged for CHROM or an invalid key

`SCALE_MASK` moves from `synth.c` to `keymap.c`, made non-static and declared in `internal.h`.

- [ ] **Step 1: Write `tests/keymap.c`** (no engine; links `core/keymap.c` alone):
```c
#define K(r, s) key_byte(r, s)
CHECK(key_map(64, K(0,1), K(0,2)) == 63, "C major E -> C minor Eb");
CHECK(key_map(71, K(0,1), K(0,2)) == 70, "B -> Bb");
CHECK(key_map(63, K(0,2), K(0,1)) == 64, "C minor Eb -> C major E");
CHECK(key_map(61, K(0,1), K(0,2)) == 61, "C# (C + 1) stays C + 1");
CHECK(key_map(60, K(11,1), K(0,1)) == 61, "B major -> C major: up a semitone, not down eleven");
CHECK(key_map(60, K(0,1), K(11,1)) == 59, "C -> B: down a semitone");
CHECK(key_map(62, K(0,1), K(2,6)) == 64, "C major D (degree 2) -> D dorian degree 2 = E");
CHECK(key_map(64, K(0,3), K(2,3)) == 66, "PENTA to PENTA: shift by 2, E -> F#");
CHECK(key_map(64, K(0,1), K(0,3)) == 64, "MAJOR -> PENTA: E stays (in C penta)");
CHECK(key_map(65, K(0,1), K(0,3)) == 64, "F -> snapped down to E in C penta");
CHECK(key_map(65, K(0,0), K(2,0)) == 67, "CHROM: shift only");
CHECK(key_map(127, K(0,1), K(5,1)) == 127 && key_map(0, K(5,1), K(0,1)) >= 0, "clamped");
CHECK(key_map(64, 0, K(0,2)) == 64 && key_map(64, K(0,1), 0) == 64, "no key: unchanged");
CHECK(key_steps(60, K(0,1), 3) == 65 && key_steps(64, K(0,1), 3) == 69 && key_steps(67, K(0,1), 3) == 72, "IV: C-E-G -> F-A-C");
CHECK(key_steps(65, K(0,1), -3) == 60, "and back");
CHECK(key_steps(61, K(0,0), 3) == 61, "CHROM: no steps");
/* round trip: every note of C major's scale, 0..127, to C minor and back (accidentals need not round-trip:
   G# in C major is G + 1, which is Ab, a scale note in C minor, and comes back as A) */
for (n = 0; n < 128; n++) if (SCALE_MASK[1] >> (n % 12) & 1)
    CHECK(key_map(key_map(n, K(0,1), K(0,2)), K(0,2), K(0,1)) == n, "round trip");
```
  (The C# line pins the accidental rule: C# is C + 1 in both scales, so it maps to 61.)
- [ ] **Step 2: Run it and see it fail.** Run: `cc -std=c99 $SAN tests/keymap.c core/keymap.c -o build/a22keymap`. Expected: link errors (no `key_map`).
- [ ] **Step 3: Implement `core/keymap.c`.** Degree = the index of the highest scale note ≤ the note's pitch class from the root; accidental = the semitones above it. Seven-note scales are 1, 2, 6.
- [ ] **Step 4: Run it and see it pass**, then add it to `tests/run_tests.sh` after `miditiers`.
- [ ] **Step 5: Commit**: "keymap: a note's place in one key, played in another".

### Task 2: Recorded notes carry their key; state and blob fields

**Files:**
- Modify: `core/synth.c` (`tape_record_note`, `seq_to_tape`'s recording, `eng_loop` h[5], `eng_loop_load` e[7]), `core/h1.c` (rsv handling at :255 and :393; Track.rsv validation in load)
- Create: `tests/keyfollow.c` (harness from `tests/midiout.c`: `fresh`, `go`, `tap`, `note`, `track`, `mo_fn`, `mo_n`, `exact`)
- Modify: `tests/state.c`, `tests/run_tests.sh`

**Interfaces:**
- Consumes `key_byte` (Task 1).
- Produces `uint8_t eng_key_glo(void)`, which is `key_byte(st.keys[0], st.keys[1])`; Task 10 replaces its callers' source with `eng_key_live()`.

- [ ] **Step 1: Failing tests in `tests/keyfollow.c`:**
```c
/* recorded_notes_carry_key: ROOT D, SCALE MINOR, a take of two keys */
st.keys[0] = 2; st.keys[1] = 2;  ...take...
p = h1_loop(&size); /* events from LOOP_HEAD, 8 bytes each: e[3] kind, e[7] key */
CHECK(p[5] & 1, "the header says the events carry keys");
CHECK(every LEV_ON/LEV_OFF event's e[7] == key_byte(2, 2), "each note carries D minor");
/* blob_round_trip: save, fresh, load, save again: the same bytes */
/* junk_keys_dropped: a blob with h[5] = 0 and e[7] = 0xA5 loads with every rsv 0 */
/* state_rsv_valid: st.rsv[0] = key_byte(4,1); st.trk[2].rsv = 1; save, fresh, load: both kept;
   then rsv[0] = 0x8C (root 12), trk.rsv = 0xFE: load gives 0 and 0 */
```
  Plus in `tests/state.c`: 0.4.0's state (`tests/state_v4.bin`) loads with `rsv[0] == 0` and every `trk[t].rsv == 0`.
- [ ] **Step 2: Run them and see them fail.** Build like `midiout`. Expected: FAIL on the header bit and the keys.
- [ ] **Step 3: Implement it:**
  - `tape_record_note` sets `e->rsv = eng_key_glo()` for ons and offs (an off takes its on's key; keep it in a `rec_key[128]` beside `rec_part`);
  - `eng_loop` sets `h[5] = 1`;
  - `eng_loop_load` keeps `e[7]` when `b[5] & 1`, else 0, and keeps only valid keys (a valid bit with a root ≤ 11, otherwise 0);
  - `h1.c` stops zeroing `rsv[0]` on load; it validates `rsv[0]` like a key and `Track.rsv` to bit 0. Defaults give 0.
- [ ] **Step 4: Run them and see them pass**, then the whole suite.
- [ ] **Step 5: Commit**: "The tape remembers each note's key".

### Task 3: Tracks with KEY on follow; MIX → KEY

**Files:**
- Modify: `core/synth.c` (`tape_fire_ev`, `tape_release_seq`, `tape_release_trk`, `tape_offs`; `tape_open` becomes pitch counts)
- Modify: `core/ui.c` (`page_nsub(PG_MIX)` → 5; `page_param`/`page_value` sub 4; `O_KEYF`, `O_DRUM`)
- Modify: `core/internal.h` (`INST_KIT 8`, `INST_SKINS 13`)
- Test: `tests/keyfollow.c`

**Interfaces:**
- Consumes `key_map`, `eng_key_glo`.
- Produces:
  - `int eng_follows(int trk, int part)`: KEY on and not a drum part;
  - `static uint8_t tape_sent[NTRACK][128]` (0xFF none);
  - `static uint8_t tape_pitch_n[NTRACK][128]` (recorded notes sounding at a sent pitch).
- Task 10 changes the key that `tape_fire_ev` maps into, through `uint8_t eng_key_play(void)`. Here that returns `eng_key_glo()`.

- [ ] **Step 1: Failing tests:**
```c
/* key_off_unchanged: KEY off, take C-E-G in C major, ROOT -> D, play a pass: ch1 notes 60 64 67 */
/* key_on_follows: KEY on T1 (st.trk[0].rsv = 1), ROOT D: 62 66 69; then SCALE MINOR: 62 65 69; exact() */
/* drums_stay: T1 on KIT, KEY on, ROOT D: the same notes; the MIX KEY column for T1 reads "DRUM" (page_param text) */
/* old_tape_unchanged: a blob with h[5] = 0, KEY on, ROOT D: plays as recorded */
/* root_change_mid_note: a long note sounding, ROOT changed: its off is the note that sounded (exact()), no voice gated after */
/* key_on_mid_note (Review Focus 1): KEY turned on while a note sounds: it ends on its own pitch */
/* stop_wrap_cut_clear_mid_note: with ROOT changed mid-note: PLAY, the wrap, CUT, clearing the track: exact() each */
/* two_keys_overdub: a take in C, ROOT E, an overdub; ROOT G: each part moved from its own key */
/* shared_pitch: two recorded notes mapping onto one pitch (C major -> C penta: E and F both -> E): one voice,
   one note-on out, ended with the later of the two offs */
/* key_subpage: SEL, SELECT x4 reaches MIX sub 4 ("KEY"); K2 turns T2 ON (st.trk[1].rsv == 1) */
```
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Implement it.**
  - `tape_fire_ev`'s on maps through `key_map(e->d1, e->rsv, eng_key_play())` when `eng_follows(trk, part)`; records `tape_sent[trk][d1]`; and increments `tape_pitch_n`. Only a 0 → 1 count calls `voice_on_ex` and `mout_trk`.
  - The off uses `tape_sent` and decrements; only 1 → 0 ends the voice and sends the off.
  - The release paths walk `tape_pitch_n` instead of `tape_open`.
- [ ] **Step 4: Run them and see them pass**, plus `midiout` and the whole suite.
- [ ] **Step 5: Commit**: "KEY: a track's tape follows ROOT and SCALE (MIX → KEY)".

### Task 4: The step pattern's key

**Files:** Modify `core/synth.c` (`step_put`, `step_fire`, the clear-all path in `EV_CLEAR`). Test: `tests/keyfollow.c`.

**Interfaces:** Consumes `key_map`, `eng_follows`, `st.rsv[0]`.

- [ ] **Step 1: Failing tests:**
```c
/* step_key_set_on_first_write: empty pattern, ROOT E: the first step written sets st.rsv[0] = key_byte(4, scale) */
/* step_follows: KEY on its track, ROOT moves: the step's note out moves */
/* step_written_in_other_key_stored_mapped: pattern key C major, ROOT D, a step written D (62) on a KEY-on track
   is stored as 60; back in C major it plays 60; in D it plays 62 */
/* step_key_off_stored_as_played: same on a KEY-off track: stored 62 */
/* step_mapping_uses_step_track (Review Focus 2): writing to T2 (KEY on) while T1 (KEY off) is in hand maps by T2 */
/* step_from_060: steps present, rsv[0] = 0: the next write sets the key; earlier steps read as in it */
/* clear_all_clears_key: hold-REC clear in STEP mode: rsv[0] == 0 */
```
- [ ] **Step 2: Run them and see them fail.** **Step 3: Implement it.** **Step 4: Run them and see them pass**, then the whole suite.
- [ ] **Step 5: Commit**: "STEP: the pattern has a key, and follows it".

### Task 5: Phase 1 finished: docs, host script, fuzz, the device

**Files:** `README.md` (Pages: MIX → KEY; "The tape" and "STEP" paragraphs), `CHANGELOG.md` (Unreleased), `core/h1.h` (if any interface comment changes), `tests/keyfollow.txt`, `tests/fuzz.c`, `tests/run_tests.sh`.

- [ ] **Step 1: `tests/keyfollow.txt`:** open SEL, SELECT ×4, then `screen` says "KEY"; turn K1 and `screen` says "T1 ON"; record a take, turn ROOT on GLO → KEYS, PLAY, and `expect_out USB 92 ...` (the moved note) on the right channel.
- [ ] **Step 2: `tests/fuzz.c`:** its panel input reaches MIX sub 4. Its blobs set `h[5]` and random `e[7]`, and random `Track.rsv` and `rsv[0]` in states. Run `sh tests/run_fuzz.sh 16`. Expected: nothing found.
- [ ] **Step 3: The whole suite.** Expected: `run_tests: all passed`.
- [ ] **Step 4: Commit.** Then **Step 5:** the CI build and its size (expected under the slot, with the margin noted). Install it, and on the FM-1 have the user check a KEY-on track follows ROOT by ear, and the tape on four channels in Studio One.

## Phase 2: PUNCH

### Task 6: Punch effects in the engine

**Files:** Modify `core/fx.c`, `core/internal.h` (`EV_PUNCH`; `void fx_punch_block(f32 *l, f32 *r, int punch, f32 bpm, f32 beat)`), `core/synth.c` (punch state; `block()` calls). Create `tests/punch.c`.

**Interfaces:** Produces:
- `EV_PUNCH`: a = punch 0–8 (0 ends), bit 23 = from the tape;
- `static int punch_hand, punch_tape`; `int eng_punch(void)` (hand if held, else tape);
- `fx_master_block` called with `eng_punch()` in 1–4 in place of `st.tape[3]`;
- `fx_punch_block` doing 5–8.

- [ ] **Step 1: Failing tests,** each sound made in its own process like `tests/basics.c`'s `taken()`:
```c
/* brake_lowers_pitch: a held sine-ish note (FM-4 algo 0), punch 1 for 1 s: the zero-crossing rate falls by > 30% */
/* stutters_repeat: punch 2/3/4 at TEMPO 120: the output's autocorrelation peaks at 0.25/0.125/0.0625 s */
/* lpf_darkens: punch 5 for one bar: the energy of the first difference (heard.slope) drops > 6 dB by the bar's end */
/* hpf_thins: punch 6: energy below 200 Hz (a one-pole low-pass of the output) drops > 6 dB */
/* crush_errors: punch 7: the output differs from the same take without it by > -30 dB */
/* gate_chops: punch 8 at 120: in each 1/16 (125 ms) the second half's RMS < 5% of the first's */
/* no_click: at a punch's start and end, no sample step > 4x the take's median step */
/* trick_back: TRICK = ST 1/8 set, punch 5 held then let go: the 1/8 stutter is back */
/* (idle: checked in Step 4 by tests/cores.c and tests/sleep.c's outputs against Task 5's commit, as Task 0 does) */
```
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Implement it.** The ramps are 10 ms. LPF and HPF are `SVF_TICK`-style 2-pole filters, swept by the beat count since the punch began. CRUSH is a 6-bit quantise plus sample-and-hold ÷6. GATE uses `beat_out`'s phase. With `punch == 0`, `fx_punch_block` returns at once.
- [ ] **Step 4: Run them and see them pass**, then the whole suite. Then record `tests/cores.c`'s and `tests/sleep.c`'s outputs on Task 5's commit and on this one, and `cmp` them. Expected: no difference, since the mix is unchanged with no punch.
- [ ] **Step 5: Commit**: "PUNCH: eight effects on the mix, played by EV_PUNCH".

### Task 7: FX + white key, the strip, FX on release

**Files:** Modify `core/ui.c` (`h1_key`'s SAVE branch neighbour; button press/release for FX; the strip in the frame; screen-reader `say`). Test: `tests/punch.c`.

**Interfaces:** Consumes `EV_PUNCH`.

- [ ] **Step 1: Failing tests:**
```c
/* fx_key_starts_and_ends: FX down, key SNAP_KEYS[2] down: eng_punch() == 3; key up: 0 (FX still held) */
/* key_holds_after_fx_up: FX down, key down, FX up: still 3; key up: 0 */
/* second_key_replaces: key 3 then key 5: 5; key 5 up: 0 (not back to 3) */
/* other_keys_play: FX held, a non-snapshot key: a note out over MIDI */
/* fx_tap_opens_on_release: FX down: page unchanged; FX up: page == PG_FX */
/* fx_used_no_page: FX + punch key, FX up: page unchanged */
/* fx_punch_ignores_save_edit (Review Focus 3): FX + punch key held, SAVE tapped: no snapshot (snap[2].used unchanged); EDIT tapped: fx_on unchanged */
/* strip_drawn: FX held: the frame's pixels in the strip's band differ from a frame without */
/* fx_light: FX held: h1_button_leds() has H1_BTN_FX's bit; let go: not */
```
- [ ] **Step 2: Run them and see them fail.** **Step 3: Implement it.** **Step 4: Run them and see them pass**, then the whole suite.
- [ ] **Step 5: Commit**: "PUNCH: hold FX and a white key; FX opens its page on release".

### Task 8: Punches on the tape

**Files:** Modify `core/synth.c` (recording `LEV_PUNCH` at tick + `LEV_TAIL`; a tail scan in `tape_play_fwd`/`rev`'s callers; CUT/PASTE/clear of a track's punches; ending at stop and wrap; mute; REV). Test: `tests/punch.c`.

**Interfaces:**
- Produces `#define LEV_TAIL 15360u`.
- `static int lev_tail_find(f32 t)` finds the first tail event at or after `t + LEV_TAIL`.
- Task 10 reuses both for `LEV_KEY`.

- [ ] **Step 1: Failing tests:**
```c
/* punch_recorded: REC, a take, punch 3 held 300 ms mid-take: the tape holds a LEV_PUNCH on and off at tick + 15360 */
/* punch_plays_back: next pass: eng_punch() == 3 over the same span (±1 block) */
/* punch_tail_in_blob: h1_loop's blob has those events with tick >= 15360 (0.6.0's loader drops them) */
/* muted_punch_silent: mute the track: no punch on playback */
/* clear_track_removes_punch, cut_moves_punch, paste_brings_it */
/* rev_swaps: REV: the punch spans the mirrored span */
/* tape_punch_across_wrap (Review Focus 4): a punch recorded to the loop's end: it ends at the wrap, starts again next pass */
/* hand_wins: a tape punch 3 playing, punch 6 held by hand: 6; let go: back to the tape's 3 if still within its span */
/* stop_ends_punch: PLAY (stop) mid-punch: 0 */
/* no_midi: nothing about punches goes out over MIDI */
```
- [ ] **Step 2: Run them and see them fail.** **Step 3: Implement it.** **Step 4: Run them and see them pass**, plus `tape`, `looper`, `midiout` and the whole suite.
- [ ] **Step 5: Commit**: "PUNCH: punches record onto the tape and play back".

### Task 9: Phase 2 finished

**Files:** `README.md` (Hotkeys: hold FX + white key; FX on release; an Effects → PUNCH paragraph), `CHANGELOG.md`, `tests/punch.txt`, `tests/fuzz.c` (FX holds with keys; blobs with kind-3 tail events), `tests/run_tests.sh`.

- [ ] **Step 1:** `tests/punch.txt`: press FX, `tapkey` a snapshot key, `screen` names the punch, release FX, `screen` shows the FX page didn't open.
- [ ] **Step 2: The fuzz run** (16 seeds) and the whole suite.
- [ ] **Step 3: Commit.**
- [ ] **Step 4: The CI size.** If over the slot, apply the flash fallback (CRUSH and GATE out), re-run Task 6's tests minus those two, and note it.
- [ ] **Step 5: The device.** For each punch held, run CC 119 with an eight-note chord and PLATE (`fm1_profile.py` with a held punch). Expected: LATE 0, SHED 0, peak ≤ 85%.

## Phase 3: LANE

### Task 10: The lane in the engine

**Files:** Modify `core/synth.c` (`EV_LANE`; `LEV_KEY` tail events with `LEV_GLOBAL`; the cache; `eng_key_play`, `eng_key_live`; recording's DEG inverse; `scale_snap` and `ch_scale_up` reading `eng_key_live()`; CLEAR ALL vs a track's clear; lane-only tape overdub), `core/internal.h`. Create `tests/lane.c`.

**Interfaces:** Produces:
- `EV_LANE`: a = bar 0–31; bits 16–17 = field (0 ROOT, 1 SCALE, 2 DEG); bits 18–22 = value (ROOT 0–11 or 31 for "—", SCALE 0–7 or 31, DEG 0–6 or 31);
- `int eng_lane_get(int bar, int field)`: the entry's value or −1 for "—";
- `uint8_t eng_key_at(int bar)`, `int eng_deg_at(int bar)` (0 = I);
- `uint8_t eng_key_play(void)`: the key in force at the playhead's bar (GLO's when the lane is empty);
- `uint8_t eng_key_live(void)`: `eng_key_play()` while the tape plays in tape mode, else `eng_key_glo()`.

- [ ] **Step 1: Failing tests:**
```c
/* lane_set_and_clear: EV_LANE bar 2 ROOT=5: eng_lane_get(2,0) == 5; ROOT, SCALE, DEG all "—": no LEV_KEY event left */
/* lane_inherits: bar 0 SCALE MINOR, bar 2 ROOT 5: eng_key_at(1) = (GLO root, MINOR); eng_key_at(3) = (5, MINOR) */
/* no_wrap: entry only at bar 3: bar 0 is GLO's key */
/* follows_bar_by_bar: KEY on, C-E-G each bar, DEG I,IV,V,vi on bars 0..3: ch1 gets C-E-G, F-A-C, G-B-D, A-C-E */
/* deg_chrom_nothing: SCALE CHROM in force: DEG IV changes nothing */
/* key_off_ignores_lane */
/* boundary: a note sounding across a bar change ends on its own pitch (exact()) */
/* record_over_deg: KEY on, bar with DEG IV, play F (65) while recording: the event stores 60 with the bar's key */
/* live_keys_snap_to_lane: tape playing over a D-minor bar: a pressed key snaps in D minor; stopped: GLO's key */
/* step_mode_ignores_lane */
/* chord_scale_follows_lane: chord MODE SCALE, tape playing over a D-minor bar: a key's chord is built in D minor */
/* glo_change_rebuilds: entries with SCALE "—", GLO's SCALE changed: eng_key_at() follows GLO's new scale */
/* meter_change_keeps_tick: an entry at bar 2 in 4/4, METER to 3/4: the entry keeps its tick; eng_key_at() of the bar it now falls in */
/* lane_tail_in_blob: h1_loop's blob has the LEV_KEY events at tick >= 15360, LEV_GLOBAL set */
/* lane_survives: save, fresh, load: entries kept; CUT, PASTE, clear T1: kept; CLEAR ALL: gone */
/* lane_only_tape_overdubs: a tape with only entries, REC + key: LOOP_DUB from IN, entries kept */
/* lane_entry_past_bars (Review Focus 5): entry at bar 6, BARS 4 then 8: the entry applies again at bar 6 */
```
- [ ] **Step 2: Run them and see them fail.** **Step 3: Implement it.** **Step 4: Run them and see them pass**, plus `keyfollow`, `chords` and the whole suite.
- [ ] **Step 5: Commit**: "LANE: a key and DEG per bar that KEY-on tracks follow".

### Task 11: TAPE → LANE and the deck's lane

**Files:** Modify `core/ui.c` (`page_nsub(PG_SEQ)` → 3 in tape mode; `page_param`/`page_value` sub 2 with a UI mirror and `set_param` posting `EV_LANE`; the cells; the deck's marks and header; screen-reader names). Test: `tests/lane.c`.

**Interfaces:** Consumes `EV_LANE`, `eng_lane_get`, `eng_key_at`, `eng_deg_at`.

- [ ] **Step 1: Failing tests:**
```c
/* lane_subpage: SEQ, SELECT x2: subpage 2, names BAR ROOT SCALE DEG (page_param) */
/* bar_clamps: BARS 4: BAR's max is 4; after BARS 2 the cursor clamps to 2 */
/* knobs_set_entry: BAR 3, K2 to D: eng_lane_get(2,0) == 2; K2 back past C: "—" */
/* cells_drawn: an entry at bar 2: the cell's pixels at bar 2 differ from bar 1's */
/* deck_mark_and_header: playing with an entry: the DECK frame's lane band has a mark; the header text reads the key in force */
/* step_mode_has_no_lane: STEP mode: SEQ has its one step page (page_nsub unchanged) */
```
- [ ] **Step 2: Run them and see them fail.** **Step 3: Implement it.** **Step 4: Run them and see them pass**, then the whole suite.
- [ ] **Step 5: Commit**: "LANE: the TAPE page's LANE subpage, and the lane on the deck".

### Task 12: Phase 3 finished

**Files:** `README.md` (Pages: TAPE → LANE; "The tape" → LANE paragraph, older firmware losing lane and punches), `CHANGELOG.md`, `tests/lane.txt`, `tests/fuzz.c` (LANE knobs; kind-4 tail events in blobs), `tests/run_tests.sh`.

- [ ] **Step 1:** `tests/lane.txt`: SEQ, SELECT ×2, `screen` says "LANE"; turn K1 and K2 and read "BAR 2", "ROOT D".
- [ ] **Step 2: The fuzz run** (16 seeds) and the whole suite.
- [ ] **Step 3: Commit.**
- [ ] **Step 4: The CI size.**
- [ ] **Step 5: The device.** A four-bar I–IV–V–vi lane with KEY on, heard by the user and recorded in Studio One on four channels. Then `crashes` to check for none.
