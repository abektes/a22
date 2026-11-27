# Plan: projects, four tapes kept on the FM-1

Written 2026-11-27, after 0.7.2. It answers issue [#12](https://github.com/abektes/a22/issues/12) ("multiple
projects?"). The owner chose four projects in fixed slots at the top of the free part of the data area (100 KB),
keeping the rest free. The release that carries it is the next one after the USB stems (PR abektes/a22#21).

## What a project is

A project is the tape and the settings together, as A22 keeps them now: `H1State` (2,888 bytes) and the tape blob
(at most `H1_LOOP_MAX`, 16,396 bytes). The tape and settings A22 plays stay as they are, saving themselves as
before (the ring at 0xC8000, the state at 0xFC000). A project is a copy of them that you save to or load from. A tape
from 0.7.2 and earlier keeps working: it is the working tape, and no project holds it until you save one.

## Owner decisions

Decided 2026-11-27: the owner took all four as recommended.

1. **The gesture.** SAVE + white key is the snapshots, FX + white key the punches, hold SAVE the reset. Decided:
   **SAVE + one of the first four black keys** from the lowest F (F#, G#, A#, C#) loads that project, and
   **SAVE + EDIT + that key** saves to it, as snapshots do. Today SAVE + black key plays the note (`h1_key` in
   `core/ui.c` only takes the white keys with SAVE held).
2. **What a project carries.** Decided: the whole state, the eight snapshots included, except TUNE and BRIGHT
   (`st.glo[2]`, `st.glo[3]`), which belong to the unit and the room rather than the song. TEMPO and SWING go with the
   project.
3. **Loading over unsaved work.** Decided: a project loads only while the tape is stopped and nothing is
   recording. When the working tape or settings changed since it was last saved to a project or loaded from one, the
   screen asks `LOAD P2? AGAIN`: the same keys again within three seconds loads it, anything else keeps the work (as
   CLEAR ALL? does). Otherwise it loads at once.
4. **Project files over USB (P4) in the same release or the next.** Decided: the same release, if P1 to P3 leave
   the SRAM it needs (below).

## Flash

| Range | What | Size |
|---|---|---|
| 0x97000..0xAEFFF | free: left for a bigger app slot (`app_end`, at most 0xAF000 now) or other data | 96 KB |
| **0xAF000..0xC7FFF** | **projects: five slots of five sectors (four projects and a spare)** | **100 KB** |
| 0xC8000..0xDBFFF | the tape's ring (`app/app.c` RING_BASE), as now | 80 KB |
| 0xDC000, 0xDE000 | the panel's calibration, the old loop pair (`APP_STORE_MAP`), as now | 16 KB |

fm1-base uses nothing in 0xAF000..0xC7FFF for A22. A22 has no resource region (`APP_RES_BASE`) and no store pool,
and the crash history is at 0xE2000. The build checks it: `PROJ_BASE >= PLAT_DATA_LO` and
`PROJ_BASE + 5 * PROJ_SLOT_BYTES == RING_BASE`, as `ring_in_the_data_area` does for the ring.

**A record** is one 256-byte header page, then the state and then the tape, page-aligned, as the ring's records are.
- **The header:** magic "A22P", a version, the project number (0 to 3), a sequence number, the state's and the tape's
  lengths, a CRC-32 of the body and the header's own CRC, written last.
- **Fit:** 256 + 2,888 + 16,396 = 19,540 bytes of the slot's 20,480, so 940 to spare. A build check ties them:
  `RING_BODY + sizeof(H1State) + H1_LOOP_MAX <= PROJ_SLOT_BYTES`. A state that grows by more than 940 bytes breaks the
  build, not the projects.

**A save** goes into the spare slot. Once its header is whole, it is that project, and the slot that held it before
becomes the spare. A power cut at any point leaves the old copy in charge. At start the five slots are read once: for
each project the newest whole record wins, an older one for the same project is the spare, and anything else counts
as an empty slot and is never loaded. That covers an erased slot, a torn save, and another firmware's bytes.

**The shared data area.** Another firmware installed after A22 may write here. Its bytes read as an empty slot, so
the projects it overwrote are gone. The README says so, and P4's backup is the way to keep them.

## RAM and code

| | flash | SRAM |
|---|---|---|
| after #21 (its CI, release test build) | 556,256 B of the 581,564 B slot (25.3 KB free) | 10,200 B free, 8,192 kept spare: about 2 KB |
| P1 to P3 (estimate) | 2 to 3 KB | about 100 B (the slots' table and the save's progress). A save writes from `st` and the tape where they lie, and a load reads the slot in place through `plat_data` as the ring's does: no buffer |
| P4, fm1-base's companion (its COMPANION.md, measured on the FM-1) | +456 B for the query, +3,496 B for files with its own hooks | 592 B of the pool for the answers, 32 B of .data. The `APP_FILES` hooks write each 512-byte frame straight to flash, so no 3,840 B file buffer |

The numbers come from #21's CI build and fm1-base's COMPANION.md. Each step's CI build gives the real ones. P4 goes
in only if the build after P3 leaves its 624 B above the spare.

**As built** (PR abektes/a22#23's CI, `5d49a25`, P1 to P4 together): image 563,536 B (+7,296 B on #21's), about
18 KB of the slot left; SRAM 9,336 B free (864 B more used), 1,144 B past the 8 KB spare. So P4 fits, and the next
feature that needs SRAM has about 1 KB.

## Steps

- [x] **P1. The slots** (`app/app.c`, beside the ring; `06528d0`).
  - Done as planned: scan at start, a page a frame through the ring's deferral (`flash_go`), a free slot erased a
    sector a frame. One flash writer a frame: `proj_step` runs before `ring_step` and has the frame while a save
    is under way; it starts only when the ring is not part-way through one. A take mid-save starts it again.
  - The header is 32 bytes at the start of its page: magic "A22P", version 1, project, sequence number, the
    state's and the loop's lengths, the body's CRC-32, the header's own CRC-32. `H1_STATE_MAX` (3,584, in
    `core/h1.h`) bounds the state so that header, state and a full loop fit 20,480 bytes at most; `H1State` is 2,888.
  - **Host tests:** `tests/projects.txt`. It covers four projects into the four erased slots, P1 again into the
    spare with its old slot erased a sector a frame, a restart, and a power-off part-way through a save's body (the
    old copy kept, the torn slot erased). It also covers another firmware's bytes in a free slot, a take during a
    save, and a full tape (2,048 events).
  - The fuzz (`tests/fuzz.c`) runs the core only, so it can't reach `app.c`'s slots; the simulator script stands
    in for it there. P2's load is in the core.
- [x] **P2. Loading while the sound runs** (`2e9566f`).
  - `h1_silence` stops the tape and every voice as hold HOME does, sending CC 123 on the four channels.
  - The load waits three frames for the render to take that, because `eng_init` empties the engine's queue (found by
    the test: CC 123 never went out when the load came in the same frame). It then waits for the tails, half a
    second at most.
  - `h1_project_load` then takes the state and the loop, read in place from the slot, under `plat_lock`. That's the
    same path as a start (`h1_state_load`, `h1_loop_load`), with TUNE and BRIGHT kept.
  - Both count as changed, so the ring and the state's save keep them as what plays.
  - A load waits for a project's save under way. It is refused while a take goes in, and when the record no longer
    checks.
  - **Host tests:** `tests/projload.txt` (loaded stopped, then playing with a key held; the next bar plays the
    project's note and not the other's; an empty project and a load while recording refused; a restart after keeps
    it) and `tests/state.c` (TUNE and BRIGHT kept, the rest to the byte, a refused state changes nothing).
  - **Left for P5:** how long the render is locked out on the FM-1 (`eng_init` clears the effects' buffers), and
    whether LATE counts one at a load.
- [x] **P3. On the FM-1's panel** (decisions 1 and 3).
  - **Gestures:** SAVE + F#, G#, A# or C# (keys 1, 3, 5, 8: `PROJ_KEYS` in `core/ui.c`) loads P1 to P4, and SAVE +
    EDIT + the key saves. EDIT let go after a save doesn't pin the knob strip, and SAVE let go doesn't save the
    settings or reset.
  - **Toasts:** `SAVING P2`, then `P2 SAVED` (or `P2 NOT SAVED`); `P2 LOADED` (or `P2 NOT LOADED`); `P2 EMPTY`;
    `LOAD P2? AGAIN`; `STOP TAPE FIRST` for a load while the tape plays; `RECORDING` for either while a take goes in.
  - **The confirm:** the same keys again within three seconds load it, and any other key or button cancels it.
    "Changed" means a setting changed (`mark_dirty`) or the tape did (`h1_loop_gen`) since the last project save or
    load. At start, a tape with anything on it counts as changed.
  - **Lights:** with SAVE held, the black keys that hold a project are lit, and the one being saved blinks.
  - **Screen reader:** each project toast is said once (`h1_proj_said`, taken in `app.c`'s `speak`).
  - **Between the core and the app:** the core keeps only the asks (`h1_proj_take`); the app tells it each frame
    what the slots hold (`h1_proj_news`) and how each ask ended (`h1_proj_done`).
  - **Host test:** `tests/projpanel.txt`. The simulator gained `expect toast` and `expect key_leds`. The reader's
    words are not checked: the host has no reader.
  - **Note for P5:** hold SAVE resets the algorithm's knobs at 0.9 s (`HOLD_RESET_ALGO`). The lights show at once,
    but looking at them for longer than that resets the knobs. A save chord that takes longer than that to land
    resets them first, then saves the reset knobs into the project. Check on the FM-1 whether that is a trap in
    practice.
  - A port that keeps no projects (it never calls `h1_proj_news`) gets nothing from the keys: no `SAVING P1` that
    goes nowhere.
- [x] **P4. Project files over USB** (`companion = true` in `app/fm1.toml`; `APP_FILES` in `app/app.h`).
  - `project1.a22` to `project4.a22` (named with `.a22` after P5, so a download has an extension), kind project, room
    20,480 bytes. A file is the project's record as its slot holds it
    (header page, state, loop), read in place.
  - **A write** goes into a free slot. Each 512-byte frame is written straight to flash as it comes, the sectors
    erased as the data reaches them (sliced), so there is no file buffer. The header is kept in RAM (32 bytes) and
    written last, at commit. That happens only if the header is this version's, the lengths add up to the file's
    size, and the body's CRC-32 matches the header's. The header then gets this project's number and the next
    sequence number, so a file saved as P1 can be put back as P3.
  - A put given up halfway leaves the project as it was and a free slot that is erased before it is used.
  - **The panel wins:** a panel save that finds no free slot because a put holds it (four projects saved, a put in
    flight or abandoned) gives the put up. The put's next frame is refused, and the companion forgets it, so the
    computer sends it again from the start. A start drops a put cut by it. Found in review: before this, that save
    erased the tape ring's first slot, a sector a frame.
  - **A delete** writes the record's magic to 0 in place (no erase), so it stays deleted after a restart.
  - **Busy** (`app_file_busy`) while the tape plays, a take goes in, or a project saves or loads. Reads go any time.
  - A project written from a computer shows `P2 FROM USB` and leaves the work that plays alone.
  - fm1-base builds everything as one file, and its companion has a static `pf`, so `app.c`'s PERF counters are
    now `perf`.
  - **Host test:** `tests/projfiles.py` drives the simulator (`a22_host --serve`) with fm1-base's own client
    (`base/tools/fm1_companion.py`). It covers the list, a CRC-checked read, P1's file put as P2, a damaged body
    and another version refused, a put cut short then whole, a put refused as busy while the tape plays, and a
    delete. `tests/projects.txt` adds a delete kept through a restart (`proj rm`).
  - **Not done here:** the companion page on A22's own site (`tools/build_site.py`; fm1-base's `web/companion/`).
    That's P6. The flash and SRAM cost comes from this branch's CI build.
- [x] **P5. On an FM-1** (the owner's, 2026-11-27; CI test package of `5d49a25`, release.yml 46045658099, installed
      first try over 0.7.2; no crash recorded throughout). The panel was driven over USB (`fm1_remote`), with PERF read
      around each step and the tape's notes counted as MIDI out.
  - **Saves** (SAVE + EDIT + F# / G#), playing and stopped: `SAVING P1`, `P1 SAVED`; LATE 0, HELD 0; 15 page writes of
    at most 2.06 ms. The tape plays on (14-16 notes in 4 s, before and after).
  - **A save right after a save** while playing: both in, LATE 0. The spare's erase ran 5 sectors, at most 22-24 ms in
    a frame, with the tape playing: LATE 0, HELD 0.
  - **Loads**, stopped, nothing changed: `P1 LOADED` at once; LATE 0, AUDIO peak 29% in that window. So the render
    locked out at a load (`eng_init`) makes no block late. The tape plays the same after (14 notes in 4 s).
  - **The question and refusals:** a knob turned there and back, then SAVE + F#, gives `LOAD P1? AGAIN`, and HOME
    keeps the work. A load while playing gives `STOP TAPE FIRST`.
  - **A power cycle** (the owner's hands) after the saves: all three projects listed by the companion afterwards. The
    same run from the fresh start gave the same results.
  - **Seen once:** in the first run, right after the install, the tape seemed stopped after the first save while
    playing (0 notes after it, and the PLAY taps after that toggled the other way). It never came back in four runs
    of the same steps, before and after the power cycle.
  - **Companion over USB** (`base/tools/fm1_companion.py` against the FM-1): `ls` shows project1-4; `get project1` is
    CRC-checked (3,836 bytes); `put` as project3 reads back with the same body; a put while the tape plays is refused
    as busy (exit 5).
  - **Delete failed on the device** ("the flash refused"): `app_file_delete` wrote its zeros from a `static const`
    array, which lives in flash, and the driver writes only from RAM. The simulator doesn't care where the bytes come
    from. Fixed in the next commit; checked on the device below.
  - **With the fix** (release.yml 46048446680, installed first try, no crash): `rm project3` deleted it, and `ls`
    shows project1 and project2 left.
  - **Timing, against 0.7.2** (the same build, with #21's stems in it too, never timed on the FM-1 before):
    - idle 12-16% by effect (0.7.2: 11-15%; LOFI 16);
    - all 112 algorithms, eight-note chords, PLATE: average 49.9% (49.5), peak 74% PERC GAMELAN (73), 47.3 frames a
      second on average (45), lowest 24 STACK CHOIR;
    - `jam --clear-tape` (the owner's tape kept in P1 and P2): peak 77% (75), LATE 0 and SHED 0 at every stage.
  - **Found in STEP mode:** the first jam ran with the FM-1 in STEP mode, so REC wrote steps and the numbers were
    void; it was rerun in TAPE mode. P1 and P2 hold TAPE mode (`seq[0]` 0), so no project load set it. When it
    switched between the reruns and the jam was not found.
  - **A real track:** an original 2-bar techno loop at 130 BPM recorded over USB on all four tracks (KIT 909, MONO
    ACID, SHAPER CZ-RES, STACK CHORD) and saved to P4 (4,556 bytes): 73% average, 85% peak, LATE 0. One REC toggle
    got out of step in the recording script's first pass (the overdubs caught stray notes); the tracks were cleared
    (hold REC) and overdubbed again.
  - **Not checked:** the lights with SAVE held by eye, hold SAVE's 0.9 s reset against a slow save chord by hand,
    the companion page in Chrome against the device (the terminal client was), a delete across a power cycle.
- [x] **P6. Docs**, all but the answer on issue #12 (when the owner says).
  - README: a Projects section (the keys, the question, the lights, backing up), the hotkey row, and the warning in
    "Leave A22 again". The compatibility line's image and SRAM numbers wait for this branch's CI build.
  - The site: the manual's Projects section, the hotkey row and a question. The build now also publishes fm1-base's
    companion page at `companion/` (with the `fm1ports.js` it imports), so a backup needs no terminal.
  - CHANGELOG Unreleased: Projects.
  - ARCHITECTURE: the projects, and the data area's map.

## Not in this plan

- Saving projects at their real size to fit more than four.
- Demo projects on first start.
- A song mode that chains projects.
- A page on the site for sharing tapes.
- The bigger app slot.

Each one builds on this plan or stays possible after it: the 96 KB below the projects stays free.
