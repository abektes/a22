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
    but looking at them for longer than that resets the knobs. Check on the FM-1 whether that is a trap in practice.
- [ ] **P4. Project files over USB** (decision 4; fm1-base `companion = true`, `APP_FILES`).
  - `project1` to `project4`, kind project: read in place.
  - A write goes into the spare slot frame by frame, the CRC is checked at commit, and it becomes current only then.
  - Delete empties the slot.
  - `app_file_busy` is 1 while recording or while a save runs.
  - This gives backup, restore and swapping tapes from fm1-base's companion page (`web/companion/`, "Back up all"
    zips every file) and `tools/fm1_companion.py`, with no page of A22's own.
  - Host test with `fm1_companion.py` on the host simulator (`--serve`): get, put, a put cut halfway, a put while
    recording.
- [ ] **P5. On an FM-1** (the owner's hands; each step asked first). Install a CI test package, then:
  - save and load all four with the tape playing, then stopped;
  - PERF's LATE, ERASE and HELD during saves and loads (0.7.2's SAVE: LATE 0, HELD 0);
  - a power cycle right after a save (the owner's yes);
  - the companion page's backup and restore, and a project put from a computer;
  - `fm1_profile.py idle` and `jam --clear-tape` within a point of 0.7.2 (the jam clears the tape: ask first);
  - `release-all` and `master pot` at the end.
- [ ] **P6. Docs.**
  - README: a Projects section, the hotkeys, and the shared data area's warning with the backup.
  - The site manual (`tools/build_site.py`).
  - CHANGELOG Unreleased.
  - ARCHITECTURE's flash map.
  - The answer on issue #12, when the owner says.

## Not in this plan

- Saving projects at their real size to fit more than four.
- Demo projects on first start.
- A song mode that chains projects.
- A page on the site for sharing tapes.
- The bigger app slot.

Each one builds on this plan or stays possible after it: the 96 KB below the projects stays free.
