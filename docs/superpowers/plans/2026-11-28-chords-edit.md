# Chord Pads, Your Own Chords (Sprint 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A player edits the pad in hand (TYPE, INV), captures their own chord into it, and it is saved with the settings and projects.

**Architecture:** A pad is a 10-byte record (`ChUser`: root offset, type, inversion, notes). The set supplies it unless
the player edited it; edited ones live in the saved state, version 6, appended at its end. `core/chordsets.c` stays the
pure module (records, notes, names, capture), `core/synth.c` plays whatever record is in force, `core/ui.c` edits it.

**Tech Stack:** C99 core (no libc, no malloc), host tests with sanitizers, simulator scripts, fm1-base device build.

**Spec:** [2026-11-27-chords-design.md](../specs/2026-11-27-chords-design.md) (approved), sprint 2 of
[PLAN-chords.md](../../PLAN-chords.md).

## Calls made here, to check

1. Turning TYPE on a captured chord replaces its notes with the type's. Only capturing again brings them back.
2. A captured chord is named from its lowest note first: C, E, G, A with A lowest reads `A MIN7`, with C lowest `C 6`.
3. In MODE PADS, EDIT + GLO with two or more keys captures into the pad in hand and leaves MEMORY's chord and the mode
   alone (the spec's table). With fewer it still turns chords off and on.
4. A pad edited back to exactly the set's own counts as unedited again, so SET does not ask about it.
5. INV counts 0 to one less than the chord's notes, as the mock-up shows it.
6. On KIT and SKINS in MODE PADS, EDIT + GLO with keys held says `NOT ON DRUMS` and changes nothing; today it stores
   them as MEMORY's chord.

## Global Constraints

- Branch `chords-edit` from `chords-pads` (`49b6cee` or later). Never push to `origin`. Push to `a22`, open a PR or
  touch the FM-1 only on the owner's yes.
- `core/` is portable C99 with no libc and no malloc. The screen font has capitals only; notes are named with sharps.
- Eight voices. A chord has at most six notes (`CHORD_MAX`). A captured chord keeps its first six, low to high.
- One state version change for all remaining sprints: version 6. Versions 4 and 5 load. A project's slot holds the
  state (`H1_STATE_MAX` 3,584 B: the build checks it).
- No note left sounding: a note's off goes where its on went, whatever is edited while it is held.
- Every new column, value and toast is spoken (`app/words.tsv`, `app/web/words/`).
- Flash is short: new functions that neither draw nor render are `SMALL`.
- Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Test commands, from the repository's top (`PY`: a venv's `bin` with numpy, scipy and pillow):
  - one file: `cc -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w core/*.c tests/NAME.c -o build/a22NAME -lm && ASAN_OPTIONS=detect_leaks=0 build/a22NAME`
  - all: `PATH="$PY:$PATH" sh tests/run_tests.sh` and `PATH="$PY:$PATH" sh tests/run_fuzz.sh 4`

## Budget and gate

Estimated: 2 KB of flash, 100 B of SRAM (the record adds 96 B to `st`; spare was 1,032 B). Sprints 3 and 4 must still
leave 4 KB after this sprint, so the image must leave 12,288 B free. Sprint 1 left 10,940 B; the size work on
`chords-pads` is estimated at 3,100 to 3,500 B and is not yet measured. On those figures this sprint ends with about
12,000 to 12,400 B free, so **the gate is likely to fail**: the owner's choice (a CYCLE bank, or a bigger slot) is
then due before sprint 3.

## Review Focus

- TYPE turned on a captured pad: the knob reads MEM first, turning away replaces the notes, nothing sounding changes
  (Task 4).
- A saved state or project with garbage pad records: clamped or dropped, no name or note read out of range (Task 2).
- Capture while the tape records: the held keys are on the tape as played; the edit changes nothing recorded (Task 5).
- SET turned from the screen reader or the simulator's slider over edited pads asks too (Task 6).
- Older firmware meeting a version 6 state starts with defaults and refuses the project; the docs say so (Task 7).

---

### Task 1: Pad records, their notes and names, capture

**Files:**
- Modify: `core/internal.h` (move `NCHTYPE`, `NCHSET` above `H1State`; the declarations), `core/chordsets.c`
- Test: `tests/pads.c`

**Interfaces:**
- Produces (in `internal.h`, above `H1State`):
  `#define CH_MEM NCHTYPE` (a captured chord no type names);
  `typedef struct ChUser { uint8_t off, type, inv, n; int8_t iv[CHORD_MAX]; } ChUser;`
  `off` is the root's semitones above KEYS ROOT; `iv[0..n-1]` are semitones above the pad's base, ascending as stored.
- Produces (in `chordsets.c`):
  - `extern const char *const CH_TYPE_NAMES[NCHTYPE + 1];` (the types' names, then `"MEM"`)
  - `const char *ch_type_name(int type);` (`"MEM"` for `CH_MEM` and anything past it)
  - `void ch_pad_default(int set, int pad, ChUser *out);` (inv 0, the type's notes)
  - `int ch_base(int off, int root, int octave);` = `48 + (root + off) mod 12 + 12 * octave`
  - `int ch_user_notes(const ChUser *u, int root, int octave, int nt[CHORD_MAX]);` base + `iv`, the lowest `inv`
    notes an octave up, ascending; returns the count
  - `int ch_numeral(int minor, const ChUser *u, char out[9]);` (`CH_NUM_*`; a minor chord has an `iv` that is 3 modulo
    12 and none that is 4: captured notes may sit octaves up). `ch_pad_numeral` and `ch_pad_notes` keep their
    signatures, built on these.
  - `void ch_capture(const uint8_t *note, int n, int root, int octave, ChUser *out);` (`note` ascending, no repeats,
    2..`CHORD_MAX`)

- [ ] **Step 1: Branch and baseline.** `git switch -c chords-edit`, commit this plan
  (`Chords: sprint 2's plan`), run both full test commands. Expected: `run_tests: all passed`, fuzz nothing found.
- [ ] **Step 2: Write the version 5 fixture now, while the core is still version 5.** `tests/state_v5_gen.c`, after
  `tests/state_v4_gen.c`: refuses a core whose `st.version != 5u`; sets MODE PADS, set 10, VOICING SMOOTH, STRUM 20,
  `keys` {5, 3, 40, 1}, TEMPO 133, `octave` -1, `rsv[0]` 0x85, MEMORY's chord {0, 4, 7} (`chord_n` 3); writes the
  `h1_state` blob. Build and run it as its header comment says; commit `tests/state_v5.bin` (2,888 B).
- [ ] **Step 3: Write the failing test** in `tests/pads.c` (`records()`, called from `main`):

```c
static int pcs(const int *nt, int n) { int i, m = 0; for (i = 0; i < n; i++) m |= 1 << (nt[i] % 12); return m; }
static void records(void)
{
    ChUser u;
    int nt[CHORD_MAX], want[CHORD_MAX], n, t, r, k, i, m;
    uint8_t note[CHORD_MAX];
    char num[9];
    ch_pad_default(10, 0, &u);                          /* LOFI 2, pad 1: C MIN9 */
    CHECK(u.off == 0 && u.n == 5 && u.inv == 0 && !strcmp(ch_type_name(u.type), "MIN9"), "the set's pad as a record");
    CHECK(ch_user_notes(&u, 0, 0, nt) == 5 && nt[0] == 48 && nt[4] == 62, "its notes");
    u.inv = 1; ch_user_notes(&u, 0, 0, nt);
    CHECK(nt[0] == 51 && nt[3] == 60 && nt[4] == 62, "INV 1: the lowest note an octave up");
    CHECK(!strcmp(ch_type_name(CH_MEM), "MEM") && !strcmp(ch_type_name(200), "MEM"), "no type: MEM");
    for (t = 0; t < NCHTYPE; t++)
        for (r = 0; r < 12; r++) {
            for (m = 0; m < 5 && CH_TYPES[t].iv[m] >= 0; m++) {}
            for (k = 0; k < m; k++) {                    /* every inversion, as hands would hold it */
                for (i = 0; i < m; i++) want[i] = 60 + r + CH_TYPES[t].iv[i] + (i < k ? 12 : 0);
                sort(want, m);                           /* (a small insertion sort, in the test) */
                for (i = 0; i < m; i++) note[i] = (uint8_t)want[i];
                ch_capture(note, m, 0, 0, &u);
                n = ch_user_notes(&u, 0, 0, nt);
                CHECK(u.type < CH_MEM && u.inv == 0, "a known type");
                CHECK(n == m && !memcmp(nt, want, (size_t)m * sizeof nt[0]), "plays as captured");
                CHECK(pcs(nt, n) == pcs(want, m), "the same pitch classes");
                if (!k) CHECK(u.type == t && u.off == r, "root position: named exactly");
            }
        }
    note[0] = 57; note[1] = 60; note[2] = 64; note[3] = 67; ch_capture(note, 4, 0, 0, &u);
    CHECK(u.off == 9 && !strcmp(ch_type_name(u.type), "MIN7"), "A lowest: A MIN7, not C 6");
    note[0] = 60; note[1] = 61; note[2] = 62; ch_capture(note, 3, 0, 0, &u);
    CHECK(u.type == CH_MEM && u.off == 0 && ch_user_notes(&u, 0, 0, nt) == 3 && nt[2] == 62, "unknown: MEM");
    CHECK(ch_user_notes(&u, 2, 1, nt) == 3 && nt[0] == 74, "ROOT and OCT move it");
    ch_capture(note, 3, 2, 0, &u);
    CHECK(u.off == 10 && ch_user_notes(&u, 2, 0, nt) == 3 && nt[0] == 60, "captured in D: the same notes in D");
    note[0] = 72; note[1] = 75; note[2] = 79; ch_capture(note, 3, 0, 0, &u);
    CHECK(ch_numeral(0, &u, num) == CH_NUM_MINOR && !strcmp(num, "I"), "a minor chord captured an octave up: I");
}
```

- [ ] **Step 4: Run it.** Expected: does not compile (`ChUser` unknown).
- [ ] **Step 5: Implement** the interfaces above. `ch_capture`: try each note as the root, lowest first, and each type
  in order; match when the pitch-class sets are equal; no match: `CH_MEM`, root the lowest note. `iv[i]` is
  `note[i] - ch_base(off, root, octave)`, held in -48..48.
- [ ] **Step 6: Run `pads`.** Expected: 0 failed (sprint 1's checks included).
- [ ] **Step 7: Commit** `Chord pads as records: notes, names, capture`.

### Task 2: State version 6

**Files:**
- Modify: `core/internal.h` (`H1State`, `H1_VERSION`), `core/h1.c` (`h1_state_load`, the layout asserts)
- Test: `tests/state.c`; `tests/projfiles.py` (line 86 expects a 2,888 B state: 2,984); `tests/run_tests.sh`'s header
  comment on `state`. The project format (`PROJ_VERSION` 1 in `app/app.c`) does not change: the state inside it carries
  its own version.

**Interfaces:**
- Consumes: `ChUser`, `CH_MEM`, `tests/state_v5.bin`.
- Produces: `#define H1_VERSION 6u`, `#define H1_V5_SIZE 2888u`, appended after `step_vel`:
  `ChUser pad[8]; uint8_t pad_mask; uint8_t spare[15];` (`sizeof(H1State)` 2,984). `pad[p]` is in force only when bit p
  of `pad_mask` is set. `spare` is 0, kept for the loop's tracks, MIDI only, STYLE, HUMAN and SUGGEST (0: default).

- [ ] **Step 1: Failing tests** in `tests/state.c`:
  - `version5()`: `state_v5.bin` loads from an odd address; `st.version == 6`, `pad_mask == 0`, `spare` zero, and mode
    PADS, set 10, SMOOTH, STRUM 20, the keys, TEMPO 133, octave -1, `rsv[0]` 0x85 and MEMORY's chord as saved.
  - `version6()`: size 2,984; a state with `pad_mask` 0x81 and two records saves and loads to the byte.
  - `refused()`: version 5 at version 6's size, and 6 at 2,888, are refused.
  - `pad_clamps()`: with bit 0 set, each of `off` 12, `type` 21, `n` 0, `n` 7, `inv == n`, `iv[0]` 100 loads with that
    pad's bit clear and its record zero; a good record beside it is kept.
  - `project()`: `h1_project_load` takes the version 5 blob (its pads unedited), and a version 6 blob with two edited
    pads, which come back to the byte.
- [ ] **Step 2: Run `state`.** Expected: FAIL (does not compile: no `pad_mask`).
- [ ] **Step 3: Implement.** Version 5: copy `H1_V5_SIZE` bytes, zero the rest. Version 4's path keeps working: its
  tail assert measures to `H1_V5_SIZE`. Clamp as the test says; an unset bit's record is left alone.
- [ ] **Step 4: Run `state pads chords keyfollow looper basics`** and the `projects` simulator script (inside
  `run_tests.sh`). Expected: 0 failed.
- [ ] **Step 5: Commit** `Saved state version 6: edited pads`.

### Task 3: The engine plays the record in force

**Files:**
- Modify: `core/synth.c` (`pad_build`, `eng_chord_pad`), `core/internal.h`, `core/ui.c` (`draw_chord_play`)
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: Tasks 1 and 2.
- Produces: `void eng_pad_get(int pad, ChUser *out);` the record in force: `st.pad[pad]` when its bit is set, else the
  set's; `n` held in 1..`CHORD_MAX` and `inv` below `n` (the panel may be writing it).

- [ ] **Step 1: Failing test** `edited()`: with a record `{off 1, MEM, inv 0, n 3, iv {0, 6, 13}}` on pad 1 (bit set),
  pad 1 plays 49, 55, 62 and releases them; ROOT 2 moves them up two; MONO (preset 7) plays 49 alone; `eng_chord_pad`
  gives pc 1 and `CH_MEM`; CHORD PLAY opened after it draws a name (`CH_TYPES` has no entry for `CH_MEM`: the
sanitizer fails a lookup there); `last_name`'s own `CH_TYPES[type]` becomes `ch_type_name(type)`; VOICING OPEN lifts
the middle note; the record overwritten while the pad is held: the
  release still ends what the press started (`matched()`); bit cleared: the set's chord again.
- [ ] **Step 2: Run `pads`.** Expected: FAIL (the set's notes play).
- [ ] **Step 3: Implement:** `pad_build` takes `eng_pad_get`'s record; mono root and `+BASS`'s note come from
  `ch_base`; `draw_chord_play` names with `ch_type_name`.
- [ ] **Step 4: Run `pads chords tracks`.** Expected: 0 failed.
- [ ] **Step 5: Commit** `Pads play what was edited`.

### Task 4: TYPE and INV, and the cards

**Files:**
- Modify: `core/ui.c` (the CHORDS page's columns, `chord_card`, pad in hand, `ui_init`)
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: `eng_pad_get`, `ch_pad_default`, `ch_numeral`, `ch_type_name`, `CH_TYPE_NAMES`.
- Produces: `static SMALL void pad_store(int pad, const ChUser *u)`: equal to the set's own: clears the bit and the
  record; else writes the record, then sets the bit; marks the state dirty either way.
- Columns, PADS subpage, MODE PADS only: K3 `TYPE` (enum `CH_TYPE_NAMES`, max `CH_MEM` while the pad reads MEM, else
  `CH_MEM - 1`), K4 `INV` (enum `"0"`..`"5"`, max `n - 1`). Both show the pad in hand and follow it when another pad
  is pressed. Choosing a type: its notes, the root kept, `inv` held below the new count.

- [ ] **Step 1: Failing tests** `edit_cols()`, on CHORDS with POP 1:
  - columns read `TYPE MAJ`, `INV 0`; after pad 3 (key 4) is pressed they read `TYPE MIN`;
  - K3 +1 on pad 1: `TYPE MIN`, `pad_mask == 1`, pad 1 then plays 48, 51, 55; K3 -1: `pad_mask == 0`;
  - K4 +1: pad 1 plays 52, 55, 60; K4 cannot pass 2;
  - turning while pad 1 is held changes nothing sounding, and its release ends it;
  - a MEM record: K3 reads `MEM`, K3 -1 reads `MIN11` with `n == 5`, K3 +1 stays `MIN11`;
  - MODE OFF and SCALE: K3 and K4 are empty; loading a state returns the pad in hand to pad 1;
  - the card of an edited pad is drawn differently (`drawn()` over its card before and after).
- [ ] **Step 2: Run `pads`.** Expected: FAIL (`TYPE` column empty).
- [ ] **Step 3: Implement.** `page_value` hands copies (as MODE's does); `set_param` edits through `pad_store`;
  `h1_key` calls `sync_cols(PG_CHORD)` when the pad in hand changes on that page; cards read `eng_pad_get`.
- [ ] **Step 4: Run `pads chords lines`.** Expected: 0 failed.
- [ ] **Step 5: Commit** `CHORDS: TYPE and INV for the pad in hand`.

### Task 5: Capture

**Files:**
- Modify: `core/ui.c` (EDIT + GLO)
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: `ch_capture`, `pad_store`, `pad_hand`.
- Produces: in MODE PADS with two or more keys held beside the pads: toast `PAD n ROOT NAME` (`PAD 1 F# MAJ`).

- [ ] **Step 1: Failing tests** `capture()`:
  - hold keys 1, 5, 8 (F#, A#, C#), EDIT + GLO: toast `PAD 1 F# MAJ`, mode still PADS, `chord_mem` and `chord_n`
    unchanged, `pad_mask == 1`; released, pad 1 plays 54, 58, 61;
  - after pad 2 was pressed, a capture goes to pad 2; a pad key held with them is not captured;
  - keys that match no type: toast ends `MEM`; seven keys: `n == 6`; two keys an octave apart: `n == 2`, MEM;
  - one key held: CHORD OFF, as before; on KIT with keys held: `NOT ON DRUMS`, mode and pads unchanged;
  - REC running: the tape holds the keys' notes, and playback is the same after the capture.
- [ ] **Step 2: Run `pads`.** Expected: FAIL (MEMORY takes the keys).
- [ ] **Step 3: Implement:** sort the held `KD_NOTE` notes, drop repeats, keep six; `st.octave` and
  `key_root(eng_key_live())` go to `ch_capture`.
- [ ] **Step 4: Run `pads chords`.** Expected: 0 failed. (`chords.c`'s MEMORY capture is outside MODE PADS and stays.)
- [ ] **Step 5: Commit** `EDIT + GLO captures into the pad in hand`.

### Task 6: NEW SET? TURN AGAIN

**Files:**
- Modify: `core/ui.c` (`set_param`, `toast`, `h1_key`, `h1_button`, `h1_encoder`, `ui_init`)
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: `st.pad_mask`.
- Produces: with an edited pad, SET's first turn only toasts `NEW SET? TURN AGAIN`; another turn of SET within
  3,000 ms takes the new set and clears `pad_mask` and the records. A key, a button, another encoder, another toast or
  the time passing keeps the pads.

- [ ] **Step 1: Failing tests** `new_set()`: no edit: SET changes at once; edited: first turn toasts, `rsv[1]` and
  the record unchanged; second turn: changed, `pad_mask == 0`; after a pad press, a button tap, a K1 turn, and
  `go(3100)`, the next turn asks again; `h1_control_set(H1_ENC_K2, 4)` asks, and again takes it.
- [ ] **Step 2: Run `pads`.** Expected: FAIL (the set changes at once).
- [ ] **Step 3: Implement**, as CLEAR ALL asks (`clear_all_asked`).
- [ ] **Step 4: Run `pads chords`.** Expected: 0 failed.
- [ ] **Step 5: Commit** `SET asks before it replaces edited pads`.

### Task 7: Words, docs, whole suite, size

**Files:**
- Modify: `app/words.tsv`, `app/web/words/` (a new file, from
  `python3 base/tools/gen_words.py app/words.tsv app/web/words build/ann_words.h`), `tests/pads.txt`, `tests/fuzz.c`,
  `README.md`, `tools/build_site.py`, `docs/ARCHITECTURE.md`, `docs/PLAN-chords.md`, `CHANGELOG.md`

- [ ] **Step 1: Words:** `INV` (inversion), `NEW SET? TURN AGAIN`, and any new type name the reader would spell
  (`MINADD9`, `MIN7-5`, `7SUS4`) if not already listed.
- [ ] **Step 2: Simulator script** `tests/pads.txt`: capture a chord, expect its toast; turn SET twice.
- [ ] **Step 3: Fuzz:** `tests/fuzz.c`'s odd states also set random pad records and `pad_mask`.
- [ ] **Step 4: Docs:** README "Chords" (TYPE, INV, capture, the ask), the site manual, ARCHITECTURE's saved state,
  CHANGELOG Unreleased, PLAN-chords sprint 2 ticked. README and CHANGELOG say: firmware older than this cannot read
  these settings or projects saved with them (it starts with its defaults).
- [ ] **Step 5: Screens.** Render CHORD PADS with an edited pad, a MEM pad and the `NEW SET? TURN AGAIN` toast in the
  simulator and look at them: names fit their cards, the toast fits the screen.
- [ ] **Step 6: Both full test commands.** Expected: `run_tests: all passed`; fuzz nothing found.
- [ ] **Step 7: Commit** `Your own chords: words, script and docs`.
- [ ] **Step 8: Size check (owner's yes needed to push).** Push `chords-edit` to `a22`, run `release.yml` by hand on
  it, read `image ... SRAM ...`, record it in `docs/PLAN-chords.md` against 2 KB and 100 B, and apply the gate: fewer
  than 12,288 B free means stop and ask. The sprint's size is the difference from a build of `chords-pads` at
  `49b6cee` or later: **that build must come first** (it is not pushed or built yet; its push needs the owner's yes
  too).
- [ ] **Step 9: On the FM-1 (the owner; ask before each step).** Install the test package; a 0.8.0 state and project
  still load; capture, edit, restart; play and time as sprint 1; end with `release-all` and `master pot`.
