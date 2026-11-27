# Chord Pads (sprint 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eight chords under eight keys, from 24 named sets, with names on screen, smooth voicing and a CHORDS page one
gesture away.

**Architecture:** A new pure module, `core/chordsets.c`, holds the chord types, the sets, numerals and the voicing
arithmetic. Pads are a fifth chord mode in `core/synth.c`: a pad goes through chord memory's slots, so tape, STEP, the
arpeggiator, strum and MIDI out receive its notes. `core/ui.c` catches the pad keys by key index and draws the page.

**Tech Stack:** C99 without libc (`core/`), host tests with `cc` and sanitizers, fm1-base's host simulator for scripts.

**Spec:** `docs/superpowers/specs/2026-11-27-chords-design.md` (sprint list: `docs/PLAN-chords.md`)

## Global Constraints

- `core/` is portable C99: no libc, no malloc. New tables are `const`.
- No saved layout change: the state stays version 5. MODE PADS is `st.chord[0] == 4`, SMOOTH is `st.chord[2] == 4`, the
  set is `st.rsv[1]`. Settings and tapes saved by 0.1.0 to 0.7.x load as before.
- A note's off goes where its on went, whatever changed in between.
- Eight voices; a chord has at most `CHORD_MAX` (6) notes; pads play at velocity 127, as panel keys do.
- Pad keys are the eight white keys from the lowest F: key indexes 0 2 4 6 7 9 11 12 (`SNAP_KEYS`). SAVE + key and
  FX + key win over pads. MIDI in is never made into chords.
- Screen text is capitals and sharps. Every new column and word is spoken (`app/words.tsv`).
- Flash estimate for this sprint: 5 KB of the 17,708 B free after projects. SRAM estimate: 80 B. No RAM buffer.
- The branch is `chords-pads` from `a22/main`, after abektes/a22#23 (projects) is merged. Never push to `origin`. Push to `a22`, open a PR or touch the FM-1 only
  on the owner's yes.
- Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Where this plan decides past the spec

1. While the tape plays over a lane, pads follow the lane's ROOT (`eng_key_live`), as live keys and SCALE chords do.
   The spec says "above KEYS ROOT".
2. Numerals and BORROWED are judged against the set's own major or natural minor, so they don't change with SCALE
   (CHROM is the default). The header still names KEYS ROOT and SCALE.
3. Until sprint 2, EDIT + GLO with two or more non-pad keys held stores MEMORY's chord and switches to MEMORY, as
   today. Pad keys never count.
4. CHORD PLAY shows the voicing keyboard now; its routing lines come with sprint 3. The page counts 1/2 and 2/2.
5. `NOT ON DRUMS` is said on the first pad-key press after arriving on a drum engine (engine, track or mode change),
   not on every hit: the kick and snare sit on pad keys. The spec says playing one says it. The page header stays.
6. Sprint 1 waits for projects (abektes/a22#23): it changes `h1_key` and `h1_key_leds` too, and the budget above is
   measured with it in.

## Review Focus

1. The event queue is full when a pad is let go: the pad must still end (Task 2).
2. The engine changes to a drum or to MONO while a pad is held: the release ends what started (Task 2).
3. HUSH or a state load with pads held or latched: nothing left sounding, no crash on the later release (Task 2).
4. SAVE or FX goes down while a pad is held, then the pad is let go: it ends as a pad, not as a snapshot or punch
   (Task 2).
5. OCT with SMOOTH: OCT + still moves the chord up an octave (Task 3).

## Files

| File | Change |
|---|---|
| `core/chordsets.c` (new) | Chord types, 24 sets, pad notes, numerals, SMOOTH arithmetic |
| `core/internal.h` | Their declarations; `CHORD_PADS`, `VOICE_SMOOTH`, `EV_PAD_ON/OFF`, `PG_CHORD`, `eng_chord_last` |
| `core/synth.c` | Pad slots and latch, voicing, last chord |
| `core/h1.c` | Load clamps |
| `core/ui.c` | Pad keys, the CHORDS page, hold ARP, LEDs, GLO without CHORD |
| `tests/pads.c`, `tests/pads.txt` (new) | Core tests; simulator script |
| `tests/chords.c`, `tests/run_tests.sh` | The page test moves; the new tests run |
| `app/words.tsv`, `app/web/words/`, `README.md`, `tools/build_site.py`, `CHANGELOG.md`, `docs/ARCHITECTURE.md` | Words and docs |

Run one core test (zsh does not split a flags variable, hence `sh -c`); `NAME` is `pads`, `chords`, `arpmode`, ...:

```bash
sh -c 'mkdir -p build && cc -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w core/*.c tests/NAME.c -o build/a22NAME -lm && ASAN_OPTIONS=detect_leaks=0 build/a22NAME'
```

On `8bb887a`: chords 48, arpmode 36, lane 144, state 82 checks, 0 failed.

---

### Task 1: Chord types and sets

**Files:**
- Create: `core/chordsets.c`, `tests/pads.c`
- Modify: `core/internal.h` (after the `keymap.c` declarations), `tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces (`core/internal.h`):

```c
#define NCHTYPE 20
#define NCHSET 24
typedef struct ChType { char name[8]; int8_t iv[5]; } ChType;        /* semitones above the root, -1 ends */
typedef struct ChPad { uint8_t off, type; } ChPad;                   /* off: semitones above the key's root, 0..11 */
typedef struct ChSet { uint8_t minor; ChPad pad[8]; } ChSet;
enum { CH_NUM_MAJOR, CH_NUM_MINOR, CH_NUM_BORROWED };
extern const ChType CH_TYPES[NCHTYPE];
extern const ChSet CH_SETS[NCHSET];
extern const char *const CH_SET_NAMES[NCHSET];
int ch_pad_pc(int set, int pad, int root);                           /* (root + off) % 12 */
int ch_pad_notes(int set, int pad, int root, int octave, int nt[5]); /* 48 + pc + 12 * octave + iv, ascending; count */
int ch_pad_numeral(int set, int pad, char out[9]);                   /* CH_NUM_*; out: "I".."VII" or "BORROWED" */
```

All functions take any `int` for `set` and `pad` and use `set` 0 and `pad & 7` when out of range.

- [ ] **Step 1: Make the branch and bring the documents in**

Check that abektes/a22#23 is merged (`gh pr view 23 --repo abektes/a22 --json state`). If it isn't, stop and ask; if
the owner says to start anyway, Step 8 of Task 6 must take 7,616 B more off the flash left (projects' size: 563,856 −
556,240). Create the worktree (superpowers:using-git-worktrees) on branch `chords-pads` from `a22/main`, run
`git submodule update --init`, copy from the main checkout `docs/PLAN-chords.md`, `docs/img/chords/`,
`docs/superpowers/specs/2026-11-27-chords-design.md` and this plan, add `PLAN-chords.md` to `docs/README.md`'s
Current list, and commit: `Chords: the design, the sprints and sprint 1's plan`. Then ask the owner to let you remove
the main checkout's untracked copies of those files (they would block its next pull), and do so on a yes.

- [ ] **Step 2: Write the failing test**

`tests/pads.c`: copy the harness of `tests/chords.c` (from `#include` to `key()`), end with
`printf("pads: %d checks, %d failed\n", ...)`, and add:

```c
static void sets(void)
{
    int s, p, r, i, n, nt[5];
    char num[9];
    for (s = 0; s < NCHSET; s++) {
        CHECK(CH_SET_NAMES[s][0] && strlen(CH_SET_NAMES[s]) <= 8, "a set has a name of 1 to 8 characters");
        for (p = 0; p < 8; p++) {
            CHECK(CH_SETS[s].pad[p].off < 12 && CH_SETS[s].pad[p].type < NCHTYPE, "a pad is in range");
            for (r = 0; r < 12; r++) {
                n = ch_pad_notes(s, p, r, 0, nt);
                CHECK(n >= 3 && n <= 5 && n <= CHORD_MAX, "three to five notes");
                CHECK(nt[0] == 48 + (r + CH_SETS[s].pad[p].off) % 12, "the root, in the octave from C3");
                for (i = 1; i < n; i++) CHECK(nt[i] > nt[i - 1] && nt[i] <= 127, "ascending");
            }
            i = ch_pad_numeral(s, p, num);
            CHECK(num[0] && (i == CH_NUM_BORROWED) == !strcmp(num, "BORROWED"), "every pad has a numeral");
        }
    }
    n = ch_pad_notes(10, 0, 0, 0, nt);   /* LOFI 2, pad 1: C MIN9 */
    CHECK(!strcmp(CH_SET_NAMES[10], "LOFI 2") && n == 5 && nt[0] == 48 && nt[1] == 51 && nt[2] == 55 &&
          nt[3] == 58 && nt[4] == 62, "LOFI 2 pad 1 is C minor nine");
    CHECK(ch_pad_numeral(10, 0, num) == CH_NUM_MINOR && !strcmp(num, "I"), "I, underlined");
    CHECK(ch_pad_numeral(10, 2, num) == CH_NUM_MAJOR && !strcmp(num, "VI"), "VI in a minor set");
    CHECK(ch_pad_numeral(10, 7, num) == CH_NUM_BORROWED, "the C# chord is borrowed");
    CHECK(ch_pad_notes(0, 0, 0, 1, nt) == 3 && nt[0] == 60, "an octave up");
    CHECK(ch_pad_notes(-1, 8, 0, 0, nt) == 3 && nt[0] == 48, "out of range: set 0, pad 0");
}
```

- [ ] **Step 3: Run it to see it fail** — the test command with `pads`. Expected: compile error, `NCHSET` undeclared.

- [ ] **Step 4: Implement `core/chordsets.c`**

`CH_TYPES`, in this order (index: name, intervals):
0 MAJ 0 4 7 · 1 MIN 0 3 7 · 2 DIM 0 3 6 · 3 AUG 0 4 8 · 4 SUS2 0 2 7 · 5 SUS4 0 5 7 · 6 7 0 4 7 10 ·
7 MAJ7 0 4 7 11 · 8 MIN7 0 3 7 10 · 9 MIN7-5 0 3 6 10 · 10 DIM7 0 3 6 9 · 11 6 0 4 7 9 · 12 MIN6 0 3 7 9 ·
13 ADD9 0 4 7 14 · 14 MINADD9 0 3 7 14 · 15 MAJ9 0 4 7 11 14 · 16 MIN9 0 3 7 10 14 · 17 9 0 4 7 10 14 ·
18 7SUS4 0 5 7 10 · 19 MIN11 0 3 7 10 17.

`CH_SETS`, in this order, each pad `offset:TYPE`:

| # | Name | Mode | Pads 1 to 8 |
|---|---|---|---|
| 0 | POP 1 | maj | 0:MAJ 7:MAJ 9:MIN 5:MAJ 2:MIN 4:MIN 5:ADD9 7:SUS4 |
| 1 | POP 2 | maj | 0:ADD9 4:MIN7 9:MIN7 5:ADD9 2:MIN7 7:SUS4 7:MAJ 10:MAJ |
| 2 | POP 3 | min | 0:MIN 8:MAJ 3:MAJ 10:MAJ 5:MIN 7:MIN 8:ADD9 10:SUS4 |
| 3 | BALLAD 1 | maj | 0:MAJ 4:MIN 5:MAJ7 7:SUS4 9:MIN7 2:MIN7 5:MIN6 7:MAJ |
| 4 | BALLAD 2 | maj | 0:ADD9 9:MINADD9 5:MAJ9 7:7SUS4 4:MIN7 4:7 2:MIN9 5:6 |
| 5 | BALLAD 3 | min | 0:MINADD9 3:MAJ7 8:MAJ7 10:SUS4 5:MIN7 7:7SUS4 7:7 8:ADD9 |
| 6 | SOUL 1 | maj | 0:MAJ7 4:MIN7 9:MIN7 2:MIN9 5:MAJ7 7:9 4:7 8:MAJ7 |
| 7 | SOUL 2 | maj | 0:6 9:7 2:MIN7 7:9 5:MAJ9 6:DIM7 4:MIN7 5:MIN6 |
| 8 | SOUL 3 | min | 0:MIN7 5:9 8:MAJ7 7:7 3:MAJ9 10:6 2:MIN7-5 5:MIN9 |
| 9 | LOFI 1 | maj | 0:MAJ9 2:MIN9 4:MIN7 5:MAJ9 9:MIN9 7:7SUS4 9:7 5:MIN6 |
| 10 | LOFI 2 | min | 0:MIN9 5:MIN7 8:MAJ7 10:7 3:MAJ7 2:MIN7-5 7:7 1:MAJ7 |
| 11 | LOFI 3 | min | 0:MIN11 10:6 8:MAJ9 7:MIN7 5:MIN9 3:MAJ9 2:MIN7 1:MAJ9 |
| 12 | JAZZ 1 | maj | 2:MIN7 7:9 0:MAJ9 9:7 5:MAJ7 11:MIN7-5 4:7 9:MIN7 |
| 13 | JAZZ 2 | maj | 0:MAJ7 3:DIM7 2:MIN9 1:7 4:MIN7 10:9 5:MIN6 8:MAJ7 |
| 14 | JAZZ 3 | min | 2:MIN7-5 7:7 0:MIN6 5:MIN7 10:9 3:MAJ7 8:MAJ7 11:DIM7 |
| 15 | CINEMA 1 | min | 0:MIN 8:MAJ 5:MIN 7:MAJ 3:MAJ 10:MAJ 1:MAJ 0:SUS2 |
| 16 | CINEMA 2 | maj | 0:SUS2 8:MAJ 10:MAJ 0:MAJ 5:MIN 3:MAJ 7:SUS4 2:MAJ |
| 17 | CINEMA 3 | min | 0:MINADD9 4:MAJ 8:MAJ7 3:AUG 5:MINADD9 1:MAJ7 6:MIN 7:SUS4 |
| 18 | HOUSE 1 | min | 0:MIN7 3:MAJ7 5:MIN7 7:MIN7 8:MAJ7 10:6 0:MIN9 5:9 |
| 19 | HOUSE 2 | min | 0:MIN9 10:MIN9 8:MAJ9 7:MIN9 5:MIN9 3:MAJ9 2:MIN7 10:7SUS4 |
| 20 | HOUSE 3 | maj | 0:MAJ7 2:MIN7 4:MIN7 5:MAJ7 7:7SUS4 9:MIN7 5:6 10:MAJ7 |
| 21 | DARK 1 | min | 0:MIN 1:MAJ 5:MIN 7:DIM 8:MAJ 3:MIN 6:MAJ 0:SUS2 |
| 22 | DARK 2 | min | 0:MINADD9 6:DIM 8:MIN 1:MAJ7 5:MIN6 7:7 11:DIM7 3:AUG |
| 23 | DARK 3 | min | 0:MIN7 10:MIN7 8:MAJ7 1:MAJ7 5:MIN11 6:MAJ7 3:MIN 7:7SUS4 |

Numerals: a major set's degrees are offsets 0 2 4 5 7 9 11, a minor set's 0 2 3 5 7 8 10, in order I to VII; any other
offset is BORROWED. `CH_NUM_MINOR` when the type's second interval is 3.

- [ ] **Step 5: Run the test** — expected `pads: N checks, 0 failed`.

- [ ] **Step 6: Add `pads` to `tests/run_tests.sh`** — build and run lines after `chords`, and a header comment line.

- [ ] **Step 7: Commit** — `Chord types and 24 sets (core/chordsets.c)`.

---

### Task 2: Pads play

**Files:**
- Modify: `core/internal.h` (chord enum, event enum), `core/synth.c` (`post`, chord memory section, `handle`,
  `eng_init`), `core/h1.c` (load clamps), `core/ui.c` (`h1_key`, EDIT + GLO capture)
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: Task 1's `ch_pad_notes`.
- Produces: `CHORD_PADS` (4, last of the chord modes); `EV_PAD_ON`, `EV_PAD_OFF` (appended to the event enum, `a` =
  pad 0..7); in `ui.c`, `static int pad_hand` (the pad last pressed, 0..7), `static int pad_of(int key)` (the pad a
  key is now, or -1) and `key_down[k]` values `KD_NOTE` 1, `KD_PAD` 2.

- [ ] **Step 1: Write the failing tests** (add to `tests/pads.c`; keys 0 2 4 6 7 9 11 12 are pads 1 to 8)

```c
static void pads_on(int set) { chord(CHORD_PADS, 0, 0, 0); st.rsv[1] = (uint8_t)set; }
static int matched(void)
{
    int n;
    for (n = 0; n < 128; n++) if (on_n[n] != off_n[n]) return 0;
    return gated() == 0;
}
static void down(int k) { h1_key(k, 1); go(60); }
static void up(int k)   { h1_key(k, 0); go(200); }

static void play(void)
{
    static const int C[3] = {48, 52, 55}, D[3] = {50, 54, 57}, ONE[1] = {54}, MID[1] = {48};
    int i;
    fresh(); pads_on(0);
    down(0); CHECK(only(C, 3) && gated() == 3, "pad 1 of POP 1 is C major");
    up(0);   CHECK(offs_are(C, 3) && gated() == 0, "and ends");
    fresh(); pads_on(0); key(1, 60);
    CHECK(only(ONE, 1) && matched(), "a key beside the pads is one note");
    fresh(); pads_on(0); st.keys[0] = 2; key(0, 60);
    CHECK(only(D, 3), "ROOT moves the pads");
    fresh(); pads_on(0); st.keys[1] = 2; st.keys[0] = 0; key(6, 60);
    CHECK(on_n[53] == 1 && on_n[57] == 1 && on_n[60] == 1, "pads are never snapped to the scale");

    fresh(); pads_on(0); down(0);                 /* everything changes while it is held */
    st.rsv[1] = 9; st.keys[0] = 5; tap(H1_BTN_OCTUP); chord(CHORD_OFF, 0, 0, 0);
    up(0); CHECK(offs_are(C, 3) && matched(), "the release ends what the press started");

    fresh(); pads_on(0); down(6); down(11); up(6);        /* two pads on F: F MAJ and F ADD9 */
    CHECK(gated() == 4 && on_n[53] == 1, "shared notes sound once");
    up(11); CHECK(matched(), "and end with the last");
    fresh(); pads_on(0); down(11); down(14); up(11);      /* a played G4, which F ADD9 also holds */
    CHECK(gated() == 1 && off_n[67] == 0, "the played key keeps its note");
    up(14); CHECK(matched() && on_n[67] == 1, "one on, one off");
    fresh(); pads_on(0); down(14); down(11); up(14);
    CHECK(gated() == 4 && off_n[67] == 0, "either order");
    up(11); CHECK(matched(), "all ended");

    fresh(); pads_on(0); down(0);                 /* the track changes under a held pad */
    h1_button(H1_BTN_EDIT, 1); go(30); h1_encoder(H1_ENC_PRESET, 1); go(60); h1_button(H1_BTN_EDIT, 0); go(60);
    up(0); CHECK(st.track == 1 && matched(), "held pad, new track: ends");

    fresh(); pads_on(0); h1_midi(0x90, 48, 100); go(100);
    CHECK(gated() == 1, "MIDI in is one note");
    h1_midi(0x80, 48, 0); go(200); CHECK(matched(), "and ends");

    fresh(); pads_on(0); h1_button(H1_BTN_SAVE, 1); go(40); key(0, 60); h1_button(H1_BTN_SAVE, 0); go(60);
    CHECK(st.snap[0].used && ons == 0, "SAVE + key wins");
    fresh(); pads_on(0); h1_button(H1_BTN_FX, 1); go(40); key(0, 60); h1_button(H1_BTN_FX, 0); go(60);
    CHECK(ons == 0, "FX + key wins");
    fresh(); pads_on(0); down(0); h1_button(H1_BTN_SAVE, 1); go(40); up(0); h1_button(H1_BTN_SAVE, 0); go(60);
    CHECK(matched() && !st.snap[0].used, "SAVE down after the press: the release is still the pad's");
    fresh(); pads_on(0); down(0); h1_button(H1_BTN_FX, 1); go(40); up(0); h1_button(H1_BTN_FX, 0); go(60);
    CHECK(matched(), "FX too");

    fresh(); pads_on(0); h1_control_set(H1_ENC_PRESET, 7); go(60); clear(); key(0, 60);
    CHECK(only(MID, 1) && matched(), "MONO plays the pad's root");
    fresh(); pads_on(0); h1_control_set(H1_ENC_PRESET, INST_KIT); go(60); clear();
    down(0);
    CHECK(ons == 1 && on_n[53] == 1 && !strcmp(h1_toast_now(), "NOT ON DRUMS"), "KIT: the key plays a drum");
    up(0); go(2000); down(0);
    CHECK(!h1_toast_now()[0], "said once, not on every hit");
    up(0); h1_control_set(H1_ENC_PRESET, 0); go(60); h1_control_set(H1_ENC_PRESET, INST_KIT); go(2000); down(0);
    CHECK(!strcmp(h1_toast_now(), "NOT ON DRUMS"), "and again after arriving on drums");
    up(0);
    for (i = 0; i < 2; i++) {                    /* the engine changes under a held pad */
        fresh(); pads_on(0); down(0); h1_control_set(H1_ENC_PRESET, i ? 7 : INST_KIT); go(60); up(0);
        CHECK(off_n[48] == 1 && off_n[52] == 1 && off_n[55] == 1 && gated() == 0, "held pad, new engine: ends");
    }

    fresh(); pads_on(0); down(0);
    for (i = 0; i < 80; i++) eng_post(EV_PUNCH, 0);      /* the queue full, nothing rendered */
    up(0); CHECK(matched(), "a pad's off gets through a full queue");
}

static void latch(void)
{
    static uint8_t buf[sizeof(H1State)];
    uint32_t size;
    fresh(); pads_on(0); st.keys[3] = 1;
    key(0, 60); CHECK(gated() == 3, "HOLD latches a pad");
    clear(); key(6, 60); CHECK(off_n[48] == 1 && off_n[55] == 1 && on_n[53] == 1, "the next pad replaces it");
    st.keys[3] = 0; eng_post(EV_UNLATCH, 0); go(300); CHECK(gated() == 0, "unlatched");
    fresh(); pads_on(0); st.keys[3] = 1; down(11); down(14); up(11); up(14);
    st.keys[3] = 0; eng_post(EV_UNLATCH, 0); go(300); CHECK(matched(), "a latched pad and key on one pitch");
    fresh(); pads_on(0); st.keys[3] = 1; key(0, 60); down(6);
    h1_button(H1_BTN_HOME, 1); go(1000); h1_button(H1_BTN_HOME, 0); go(200); clear(); up(6);
    CHECK(gated() == 0 && ons == 0, "HUSH with pads held and latched");
    fresh(); pads_on(0); { const void *s = h1_state(&size); memcpy(buf, s, size); }
    down(0); CHECK(h1_state_load(buf, size) == 0, "a load under a held pad"); go(100); up(0);
    CHECK(gated() == 0, "nothing left sounding");
}

static void sequencers(void)
{
    SeqInfo si;
    fresh(); pads_on(0); st.seq[1] = 0;
    tap(H1_BTN_REC); key(0, 150); tap(H1_BTN_REC); chord(CHORD_OFF, 0, 0, 0); clear();
    go(4 * 60000 / st.glo[0] + 100);
    CHECK(ons == 3 && on_n[48] == 1 && on_n[52] == 1 && on_n[55] == 1, "the tape plays the pad's chord");
    tap(H1_BTN_PLAY); go(300);
    fresh(); pads_on(0);
    h1_button(H1_BTN_SEQ, 1); go(700); h1_button(H1_BTN_SEQ, 0); go(60); tap(H1_BTN_REC);
    eng_seq_info(&si); CHECK(si.mode == SEQ_STEP && si.write, "writing steps");
    key(0, 60);
    CHECK(st.steps[0][0] == 49 && st.steps[0][1] == 53 && st.steps[0][2] == 56, "the step holds the chord");
    fresh(); pads_on(0); st.ip[st.inst].arp[0] = ARP_RIFF;
    tap(H1_BTN_ARP); tap(H1_BTN_REC); key(0, 80); tap(H1_BTN_REC);
    CHECK(st.riff_len == 3, "RIFF takes the notes one by one, as with chord memory");
}

static void saved(void)
{
    static uint8_t buf[sizeof(H1State)];
    uint32_t size;
    fresh(); pads_on(10);
    { const void *s = h1_state(&size); memcpy(buf, s, size); }
    fresh(); CHECK(h1_state_load(buf, size) == 0 && st.chord[0] == CHORD_PADS && st.rsv[1] == 10, "mode and set kept");
    buf[offsetof(H1State, chord)] = 200; buf[offsetof(H1State, rsv) + 1] = 200;
    CHECK(h1_state_load(buf, size) == 0 && st.chord[0] == CHORD_OFF && st.rsv[1] == 0, "nonsense clamped");
}
```

- [ ] **Step 2: Run to see it fail** — expected: `CHORD_PADS` undeclared.

- [ ] **Step 3: Engine** (`core/synth.c`)
  - `ChSlot` gains `uint8_t pad` (0: a key's slot, 1..8: a pad's). `chord_key` matches only slots with `pad == 0`;
    new `static void pad_key(int pad, int on, int vel)` matches `pad + 1`. Factor the slot's start (strum due times)
    so both use it. A pad's notes are never `from_midi`.
  - `chord_build`: with `CHORD_PADS` a played key is its own note, through a slot (so `ch_cnt` counts it). A pad's
    notes come from `ch_pad_notes(st.rsv[1], pad, key_root(eng_key_live()), st.octave, nt)`, then the voicing code
    shared with the other modes (+BASS adds the pad's root an octave down). MONO: the root alone.
  - Latch: `static uint8_t latched_pads`. `EV_PAD_ON` / `EV_PAD_OFF` follow the note events' `phys_down` and HOLD
    rules; `unlatch` lets latched pads go too; `eng_init` clears the mask.
  - `post()`: `EV_PAD_OFF` may use the 8 reserved queue slots, as `EV_NOTE_OFF` does.
- [ ] **Step 4: State** (`core/h1.c`): `st.chord[0] > CHORD_PADS` → OFF; replace `st.rsv[1] = 0` with
  `if (st.rsv[1] >= NCHSET) st.rsv[1] = 0`.
- [ ] **Step 5: Keys** (`core/ui.c`): `pad_of` is -1 unless the mode is PADS, the key is in `SNAP_KEYS` and `st.inst`
  is not KIT or SKINS. In `h1_key`, after the SAVE (projects' black keys, snapshots) and punch branches: a pad press stores `KD_PAD` and the pad number,
  sets `pad_hand`, posts `EV_PAD_ON`; the release goes by `key_down`, not by the mode now. A pad key on a drum engine
  in PADS mode plays its note, and says `NOT ON DRUMS` if a `drum_said` flag is clear (then sets it); `set_instrument`,
  `set_track`, a MODE change and EDIT + GLO clear it. EDIT + GLO capture counts `KD_NOTE` keys only.
- [ ] **Step 6: Run `pads`, `chords`, `tracks`, `tape`, `lane`, `state`** — all `0 failed` (tracks and tape print their
  own pass lines).
- [ ] **Step 7: Commit** — `Chord pads: eight chords on the lowest white keys`.

---

### Task 3: SMOOTH voicing

**Files:**
- Modify: `core/chordsets.c`, `core/internal.h`, `core/synth.c`, `core/h1.c`, `core/ui.c` (`O_VOICING`, its max)
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: Task 2's pad path.
- Produces: `#define VOICE_SMOOTH 4`;
  `int ch_centre(const int *nt, int n, int octave)` = `sum * 16 / n - 192 * octave`;
  `int ch_smooth(int *nt, int n, int last, int octave)`: `nt` ascending root position, re-voiced in place; returns its
  centre. `last < 0` leaves it as it is.

- [ ] **Step 1: Write the failing test**

```c
static void smooth(void)
{
    static const int C[3] = {48, 52, 55}, F[3] = {48, 53, 57}, AM[3] = {48, 52, 57}, UP[3] = {60, 64, 67};
    int nt[3] = {53, 57, 60}, first[3] = {48, 52, 55};
    ch_smooth(nt, 3, ch_centre(first, 3, 0), 0);
    CHECK(nt[0] == 48 && nt[1] == 53 && nt[2] == 57, "F after C: second inversion, an octave down");
    fresh(); pads_on(0); st.chord[2] = VOICE_SMOOTH;
    key(0, 60); CHECK(only(C, 3), "the first chord is in root position");
    clear(); key(6, 60); CHECK(only(F, 3), "F stays close to C");
    clear(); key(4, 60); CHECK(only(AM, 3), "A minor stays close to F");
    fresh(); pads_on(0); st.chord[2] = VOICE_SMOOTH;
    key(0, 60); tap(H1_BTN_OCTUP); clear(); key(0, 60);
    CHECK(only(UP, 3), "OCT + still moves it up");
}
```

Add to `saved()`: `buf[offsetof(H1State, chord) + 2] = 4` loads as 4; `200` loads as 0.

- [ ] **Step 2: Run to see it fail** — expected: `ch_smooth` undeclared.
- [ ] **Step 3: Implement.** Candidates are tried as: octave shift 0 then -12; within each, inversion 0 to n-1
  (inversion k raises the lowest k notes by 12). Keep the first with the smallest `|ch_centre - last|`; sort ascending.
  In `synth.c`, keep `static int ch_ctr = -1`: every chord of any mode except MONO and single notes updates it, SMOOTH
  chooses against it, `chord_clear` resets it. Raise the voicing limit to 4 in `h1.c` and `ui.c`; `O_VOICING` adds
  `"SMOOTH"`.
- [ ] **Step 4: Run `pads` and `chords`** — `0 failed`.
- [ ] **Step 5: Commit** — `SMOOTH voicing: each chord close to the last`.

---

### Task 4: The CHORDS page and its controls

**Files:**
- Modify: `core/internal.h` (`PG_CHORD` before `NPAGE`), `core/ui.c` (page tables, `page_param`, `page_value`,
  `set_param`, `h1_button`, `holds`, `h1_control`, EDIT + GLO, `draw_arp_page`, `draw_glo_page`)
- Test: `tests/pads.c`, `tests/chords.c` (replace its "the CHORD page" block)

**Interfaces:**
- Consumes: `CH_SET_NAMES`, `CHORD_PADS`, `VOICE_SMOOTH`.
- Produces: page `PG_CHORD` with subpages 0 PADS, 1 PLAY.

| Subpage | K1 | K2 | K3, K4 |
|---|---|---|---|
| PADS | MODE: OFF, PADS, MEMORY, SCALE, SHAPE (knob order; stored 0, 4, 1, 2, 3) | PADS: SET (`st.rsv[1]`); SCALE, SHAPE: TYPE (`st.chord[1]`); MEMORY: TYPE "MEM"; OFF: empty | empty |
| PLAY | VOICING (`st.chord[2]`) | STRUM (`st.chord[3]`) | empty |

- [ ] **Step 1: Write the failing tests**

```c
static void hold(int b) { h1_button(b, 1); go(700); h1_button(b, 0); go(60); }
static int col(int k, const char *name, const char *text)
{
    const char *n = "", *t = "";
    h1_control(k, 0, 0, &n, &t);
    return !strcmp(n, name) && (!text || !strcmp(t, text));
}
static void turn(int enc, int d) { h1_encoder(enc, d); go(60); }
static void edit_glo(void)
{
    h1_button(H1_BTN_EDIT, 1); go(30); tap(H1_BTN_GLO); h1_button(H1_BTN_EDIT, 0); go(60);
}

static void page_controls(void)
{
    fresh();
    tap(H1_BTN_ARP); CHECK(h1_page() == PG_ARP, "tap ARP: the SEQ page");
    hold(H1_BTN_ARP); CHECK(h1_page() == PG_CHORD, "hold ARP: CHORDS, from the SEQ page too");
    hold(H1_BTN_ARP); CHECK(h1_page() == PG_CHORD && col(H1_ENC_K1, "MODE", "OFF"), "held again: it stays");
    CHECK(col(H1_ENC_K2, "", 0) && col(H1_ENC_SELECT, "SUBPAGE", "1 PADS"), "OFF has no second column");
    turn(H1_ENC_K1, 1); CHECK(st.chord[0] == CHORD_PADS && col(H1_ENC_K1, "MODE", "PADS"), "PADS is next to OFF");
    turn(H1_ENC_K2, 4); CHECK(st.rsv[1] == 4 && col(H1_ENC_K2, "SET", "BALLAD 2"), "K2 picks the set");
    turn(H1_ENC_K1, 1); CHECK(st.chord[0] == CHORD_MEMORY && col(H1_ENC_K2, "TYPE", "MEM"), "MEMORY");
    turn(H1_ENC_K1, 2); turn(H1_ENC_K2, 7);
    CHECK(st.chord[0] == CHORD_SHAPE && st.chord[1] == 7, "SHAPE's type");
    turn(H1_ENC_K1, -1); CHECK(st.chord[0] == CHORD_SCALE && st.chord[1] < CHORD_NSCALE, "kept in SCALE's range");
    turn(H1_ENC_K1, -2); CHECK(st.chord[0] == CHORD_PADS && st.rsv[1] == 4, "the set is kept");
    edit_glo(); CHECK(st.chord[0] == CHORD_OFF, "EDIT + GLO: off");
    edit_glo(); CHECK(st.chord[0] == CHORD_PADS, "and PADS again");
    h1_key(0, 1); h1_key(2, 1); go(60); edit_glo(); h1_key(0, 0); h1_key(2, 0); go(200);
    CHECK(st.chord[0] == CHORD_OFF && st.chord_n == 3 && st.chord_mem[1] == 4, "pad keys are not captured");
    turn(H1_ENC_SELECT, 1);
    CHECK(col(H1_ENC_SELECT, "SUBPAGE", "2 PLAY") && col(H1_ENC_K1, "VOICING", 0) && col(H1_ENC_K3, "", 0), "PLAY");
    turn(H1_ENC_K1, 9); CHECK(st.chord[2] == VOICE_SMOOTH && col(H1_ENC_K1, "VOICING", "SMOOTH"), "to SMOOTH");
    CHECK(col(H1_ENC_K2, "STRUM", 0), "STRUM");
    tap(H1_BTN_HOME); CHECK(h1_page() == PG_HOME, "HOME leaves");
    h1_button(H1_BTN_EDIT, 1); go(30); hold(H1_BTN_ARP); h1_button(H1_BTN_EDIT, 0); go(60);
    CHECK(h1_page() == PG_HOME && st.keys[3] == 1, "EDIT + ARP is still HOLD, and opens no page");
    fresh(); tap(H1_BTN_GLO); turn(H1_ENC_SELECT, 1); turn(H1_ENC_SELECT, 1);
    CHECK(col(H1_ENC_K1, "TEMPO", 0), "GLO has two subpages");
}
```

In `tests/chords.c`, replace the "the CHORD page" block with the same checks as its old ones, reached by holding ARP
(K1 +4 to SHAPE, K2 +7, K1 -1 to SCALE, then SELECT +1 and K2 is STRUM), and update the file's header comment.

- [ ] **Step 2: Run to see it fail** — expected: `PG_CHORD` undeclared.
- [ ] **Step 3: Implement.**
  - `PG_CHORD` makes `pages[]` and `PAGE_BTN[]` nine long. Give `pages[]` a `CHORDS` entry, have `page_param`
    return for `PG_CHORD` on every column (empty ones with name `""`), never from `pages[PG_CHORD]`, and set
    `PAGE_BTN[PG_CHORD]` to `H1_BTN_ARP` (a zero there is FX).
  - `page_value` for K1 returns a mirror byte filled from `st.chord[0]` through
    `static const uint8_t CH_KNOB[5] = {CHORD_OFF, CHORD_PADS, CHORD_MEMORY, CHORD_SCALE, CHORD_SHAPE}`;
    `set_param` writes back through it, keeps today's type clamp for SCALE and SHAPE only, sets `chord_last`, and
    `sync_cols`. Empty columns point at a zero byte.
  - `O_CHMODE` (indexed by the stored value, for the toast) adds `"PADS"`.
  - ARP: drop its press case; open `PG_ARP` on release; in `holds()` after 550 ms open `PG_CHORD` (already there:
    nothing) and mark the button fired.
  - GLO: two subpages; its titles count 1/2 and 2/2; the chord branches go. `sync_cols` after EDIT + GLO is for
    `PG_CHORD`.
  - `h1_control` names SELECT's value `1 PADS` or `2 PLAY` on `PG_CHORD`.
  - `h1_button_leds`: `PG_CHORD` lights ARP, blinking at the REC blink's rate.
  - `draw_arp_page`: `HOLD ARP: CHORDS` centred at y 165, size 8, slate, in every mode but SHAKE (its cage reaches
    there).
  - `ui_frame`: a `PG_CHORD` branch calling `draw_chords_page`, which here draws the top bar (`CHORD PADS` or
    `CHORD PLAY`) and `draw_bottom_cols(PG_CHORD, 182.0f)`.
- [ ] **Step 4: Run `pads`, `chords`, `arpmode`, `punch`** and build `a22_test`
  (`... core/*.c tests/a22_test.c -o build/a22test -lm -lpthread`) — all pass.
- [ ] **Step 5: Commit** — `CHORDS page: hold ARP; GLO's CHORD subpage moves there`.

---

### Task 5: The screens and the key lights

**Files:**
- Modify: `core/ui.c` (`draw_chords_page`, `h1_key_leds`), `core/synth.c`, `core/internal.h`
- Test: `tests/pads.c`

**Interfaces:**
- Consumes: `pad_hand`, `key_down`, `ch_pad_pc`, `ch_pad_numeral`, `CH_TYPES`.
- Produces: `int eng_chord_last(uint8_t out[CHORD_MAX]);` — the notes of the chord last started, ascending; 0 if none
  since `chord_clear`.

- [ ] **Step 1: Write the failing tests**

```c
static int inked(int x, int y, int w, int h)
{
    int i, j, n = 0;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) n += h1_framebuffer()[j * H1_W + i] != 0;
    return n;
}
static void screen(void)
{
    uint8_t last[CHORD_MAX];
    int i, seen = 0, dark = 0;
    fresh(); pads_on(0);
    CHECK(eng_chord_last(last) == 0, "no chord yet");
    down(0);
    CHECK((h1_key_leds() & 0x1AD5u) == 1u, "only the pad held is lit, not the keys its notes sit on");
    CHECK(eng_chord_last(last) == 3 && last[0] == 48 && last[1] == 52 && last[2] == 55, "its notes");
    up(0); CHECK((h1_key_leds() & 0x1AD5u) == 0, "dark again");
    hold(H1_BTN_ARP); go(600);
    for (i = 0; i < 8; i++) CHECK(inked(6 + (i % 4) * 58, 45 + (i / 4) * 63, 54, 59) > 150, "eight cards");
    for (i = 0; i < 40; i++) { go(50); if (h1_button_leds() & (1u << H1_BTN_ARP)) seen++; else dark++; }
    CHECK(seen > 5 && dark > 5, "ARP blinks on CHORDS");
    st.fx_on = 0; go(60);
    CHECK(!(h1_button_leds() & (1u << H1_BTN_FX)), "FX bypassed: FX dark on CHORDS");
}
```

- [ ] **Step 2: Run to see it fail** — expected: `eng_chord_last` undeclared.
- [ ] **Step 3: Implement**, matching `docs/img/chords/pads.png` and `play.png`:
  - PADS in MODE PADS: y 31, set name gold at the left, `ROOT SCALE` of `eng_key_live()` slate at the right. Cards at
    `x = 6 + (i % 4) * 58`, `y = 45 + (i / 4) * 63`, 54 × 59: key letter (F G A B C D E F) 7 px top left; root 17 px at
    y+11; type name 9 px at y+33; numeral 7.5 px at y+47, underlined for `CH_NUM_MINOR`. A 3 px band on top, blue, or
    orange when borrowed. Held: filled at 0.30 in the band's colour. `pad_hand`: white 1.8 px box and 0.14 fill.
    Otherwise a slate 1 px box at 0.35.
  - On a drum engine the header says `NOT ON DRUMS` and the cards draw at 0.35.
  - PADS in the other modes: the mode's name 22 px at y 80; `EDIT+GLO: ON / OFF` 8.5 px at y 120.
  - PLAY: the last chord's name 18 px at y 33 (the pad's root and type; for other modes, the lowest note's name), its
    note names 9 px at y 57, a three-octave keyboard at y 72, 42 px tall, from the C at or below the lowest note, those
    notes lit in the track's colour; with no chord yet, `PLAY A CHORD`.
  - `h1_key_leds` (after projects' SAVE-held lights and `eng_seq_leds`): in PADS mode on a melodic engine, clear the pad keys' bits (`0x1AD5`) from
    `eng_sounding_lo()` and set those of keys held as `KD_PAD`.
- [ ] **Step 4: Run `pads`, `chords`, `lines`** (`lines`: `core/gfx.c tests/lines.c`) — pass.
- [ ] **Step 5: Commit** — `CHORDS screens and pad lights`.

---

### Task 6: Simulator script, words, docs and the size check

**Files:**
- Create: `tests/pads.txt`
- Modify: `tests/run_tests.sh`, `app/words.tsv`, `README.md`, `tools/build_site.py`, `CHANGELOG.md`,
  `docs/ARCHITECTURE.md`, `docs/PLAN-chords.md`

- [ ] **Step 1: Write `tests/pads.txt`**

```text
# Chord pads in fm1-base's host simulator. Hold ARP, MODE PADS, a pad's three notes on USB. Never presses SAVE.
wait 600
listen
press ARP
wait 800
release ARP
wait 400
said PAGE. MODE
spin K1 1
wait 600
said MODE PADS
spin K2 1
wait 600
said SET POP 2
spin K2 -1
wait 600
out
key 0 down
wait 300
expect_out USB 90 30 7F
expect_out USB 90 34 7F
expect_out USB 90 37 7F
shot pads.png
key 0 up
wait 300
expect_out USB 80 30 00
spin SELECT 1
wait 600
said PLAY
shot chordplay.png
```

- [ ] **Step 2: Add it to `tests/run_tests.sh`** after `lane.txt`, with its comment. In `tests/fuzz.c`'s odd states,
  half the time set `chord[0]` to `below(5)` and `rsv[1]` to `below(NCHSET)`, so the fuzz plays pads.
- [ ] **Step 3: Words.** In `app/words.tsv` add `LOFI<TAB>lo fi`. Building the host writes a new
  `app/web/words/<hash>.json`: commit it and keep the old ones.
- [ ] **Step 4: Run everything**

Run: `tests/run_tests.sh` then `tests/run_fuzz.sh 4` (the simulator needs a Python with numpy, scipy and pillow: use a
venv). Expected: `run_tests: all passed`; the fuzz reports no failure.

- [ ] **Step 5: Look at the screens.** Open `app/build/host/pads.png` and `chordplay.png` beside the mock-ups: no text
  clipped or overlapping, in every set (turn SET through all 24 in the simulator) and with SCALE MINOR.
- [ ] **Step 6: Docs.** README: "Chord memory" becomes "Chords" (pads, sets, SMOOTH, the CHORDS page); the Pages table
  (GLOBAL loses CHORD, CHORDS is added); Hotkeys (hold ARP). The same three in `tools/build_site.py` (`PAGES`,
  `HOTKEYS`, the Chord memory section). CHANGELOG Unreleased: **Chord pads**. ARCHITECTURE: the state's `rsv[1]`.
- [ ] **Step 7: Commit** — `Chord pads: script, words and docs`.
- [ ] **Step 8: Size check (owner's yes needed to push).** Push `chords-pads` to `a22`, open a PR, and read CI's
  `image ... SRAM ...` line. Record it in `docs/PLAN-chords.md` under sprint 1 against 5 KB and 80 B (the sprint's
  size is the difference from CI's image of the branch point), and apply the flash gate: would sprints 2 to 4 (10 KB estimated) still leave 4 KB free? If not, stop and ask.
- [ ] **Step 9: On the FM-1 (the owner; ask before each step).** Install CI's test package, play the pads,
  `tools/fm1_profile.py` eight-note chords and idle (LATE 0, no crash), end with `release-all` and `master pot`. Then
  the sprint plan's ear check: the sets, the screen at arm's length, room to play over the pads.
