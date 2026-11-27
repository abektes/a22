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

- [ ] **P1. The slots** (`app/app.c`, beside the ring).
  - Scan at start; save a page a frame; erase the spare a sector a frame (sliced erase, as the ring since 0.7.2).
  - **One flash writer:** while a project save runs, the state's save and the ring wait, and a project save waits for
    a ring save under way. Never two in one frame.
  - **The tape changes mid-save** (a take: `h1_loop_gen`): the save is dropped and starts again, as the ring does.
  - **The state changes mid-save** (a knob): the bytes are written as they are, and the CRC is the one that went in,
    so the record is whole and holds a moment of the save.
  - **A save right after a save**, before the spare is erased: erase it first, sliced, then write.
  - **Host tests:** `tests/projects.txt` (save, load, four projects, a restart, a save cut at each page, foreign
    bytes in a slot, a full tape) and the fuzz with project saves mixed in. `tests/run_tests.sh` passes.
- [ ] **P2. Loading while the sound runs.** `h1_state_load` and `h1_loop_load` run only at start today, before the
      sound. Snapshots load through the engine's queue (`EV_SNAP_LOAD`). Trace what a whole load needs with the render
      running: stop the tape, silence the voices (as hold HOME does), then hand the state and the tape over where the
      render can't read them half-written (`plat_lock`, or an engine event as snapshots use). The settings then
      redraw and save as after a start. Keep TUNE and BRIGHT (decision 2). Host test: load while notes sound and while
      the tape plays, and the next frame plays the project. The CPU and screen numbers stay within a point.
- [ ] **P3. On the FM-1's panel** (decisions 1 and 3).
  - The gesture and toasts: `P2 SAVED`, `P2 LOADED`, `P2 EMPTY`, `LOAD P2? AGAIN`, `SAVING P2` while it writes.
  - The key lights with SAVE held show which black keys hold a project.
  - The screen reader says the same.
  - A save while recording is refused (`RECORDING`).
  - Host tests for each.
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
