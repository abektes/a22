# Plan: chord pads and a chord loop

Written 2026-11-27. **Design approved 2026-11-27.** Sprint 1's task plan is
[superpowers/plans/2026-11-27-chords-pads.md](superpowers/plans/2026-11-27-chords-pads.md). The design is
[superpowers/specs/2026-11-27-chords-design.md](superpowers/specs/2026-11-27-chords-design.md): its controls, screens
and behaviour are what each sprint builds.

Four sprints, each a branch that can ship on its own. Each gets a detailed task plan before its code is written.
Sprint 1 is built on top of the projects branch ([abektes/a22#23](https://github.com/abektes/a22/pull/23), the
owner's choice, 2026-11-27) while that waits for its FM-1 run: both change the key handling in `core/ui.c`, and the
budget below is measured with projects in. Its pull request goes on top of that one.

| Sprint | Branch | What a player gets | Needs |
|---|---|---|---|
| 1 | `chords-pads` | Eight chords under eight keys, sets, names, smooth voicing, the CHORDS page | the projects branch |
| 2 | `chords-edit` | Their own chords: variations and capture, saved | 1, and projects merged |
| 3 | `chords-loop` | A progression that plays itself, chords and bass to the DAW, MIDI-only tracks | 2, and the flash gate |
| 4 | `chords-feel` | Playing styles, humanize, suggestions | 3 |

## The budget

CI's build of the projects branch (`d4c7016`): flash 563,856 B of the 581,564 B slot (**17,708 B free**), SRAM 9,336 B
free with 8,192 kept spare (**1,144 B**). Estimated for all four sprints: 15 KB of flash, 360 B of SRAM. No sprint
adds a RAM buffer.

## Rules for every sprint

A sprint is done when all of these are ticked:
- host tests for each behaviour in its list, and `tests/run_tests.sh` and `tests/run_fuzz.sh` pass;
- CI's flash and SRAM line recorded here, against the estimate, and the flash gate checked: would the sprints left
  still leave 4 KB free?
- the screens checked in the simulator, and every new column and word spoken (`app/words.tsv`);
- on the FM-1 (**the owner's hands, each step asked first**): install a CI test package, play it, time it
  (`tools/fm1_profile.py`: eight-note chords, idle, and from sprint 3 the loop jam), LATE 0, crash history empty, and
  end with `release-all` and `master pot`;
- README, the site manual (`tools/build_site.py`) and CHANGELOG Unreleased.

## Sprint 1: chord pads

- [ ] **Sets.** A table of chord types and 24 sets of eight pads, our own, in flash. A test builds every pad of every
      set in all twelve roots: in range, at most six notes, a name for each.
- [ ] **MODE PADS** (`core/synth.c`, beside chord memory). The eight white keys from the lowest F are caught by key,
      before scale snapping and octave; OCT moves the chord. Notes go the way chord memory's do, to the tape, STEP, arp,
      latch, strum and MIDI out; pads need slot and latch identities of their own. Off on KIT and SKINS.
- [ ] **SMOOTH** voicing.
- [ ] **The CHORDS page:** PADS (MODE, SET) and PLAY (VOICING, STRUM), the eight cards with name and numeral, the pad
      in hand, the sounding pad. Pad keys light while held.
- [ ] **Controls.** Hold ARP opens the page (ARP's tap moves to release; EDIT + ARP unchanged). HOLD ARP: CHORDS on the
      SEQ page. GLO's CHORD subpage removed. EDIT + GLO unchanged.
- [ ] **Saved:** MODE and SET come back after a restart, no version change. A state from 0.7.x loads as before.
- [ ] **Tests:** every note-off matches its note-on across set, ROOT, mode, track and octave changes with pads held;
      SAVE + key and FX + key still win; MIDI in unchanged; RIFF record and STEP write with pads, as chord memory; two
      pads on one root, and a pad with a played key at the same pitch, held and latched, released in either order.

**Check by ear and hand before sprint 2:** the sets, the screen at arm's length, whether eight pads on the low keys
leave enough room to play over them.

## Sprint 2: your own chords

- [ ] **TYPE and INV** on PADS edit the pad in hand, heard on the next press.
- [ ] **Capture:** hold keys beside the pads and press EDIT + GLO to store them in the pad in hand, named when the
      notes match a known type, MEM when not.
- [ ] **State version 6:** the eight pads, with bytes reserved for the loop's tracks, MIDI only, STYLE, HUMAN and
      SUGGEST so sprints 3 and 4 need no further version. Version 5 and 4 states load; project records still fit their slot (the
      build check).
- [ ] **NEW SET? TURN AGAIN** when SET would replace edited pads.
- [ ] **Tests:** a 0.7.x state and a project saved before this sprint load; edits survive a restart and a project
      save and load; capture of every known type in every inversion.
- [ ] **Docs:** that firmware older than this cannot read the new settings (it starts with its defaults).

## Flash gate (after every sprint; expected before sprint 3)

- [ ] Read the flash left in the sprint's CI build. If the sprints left would leave less than 4 KB free, the owner
      picks: cut
      scope (sprint 4's STYLE first), win flash back elsewhere, or grow the app slot (`app_end`, at most 0xAF000),
      tried on the FM-1 first: install over the old slot size, back to V15, and to another firmware.

## Sprint 3: the chord loop

- [ ] **First, the empty tape:** a tape with only lane entries has a length, plays, saves and loads.
- [ ] **CHORD in the lane:** a pad, REST or none per bar, through `lane_set`, `lane_build` and the loader. TAPE LANE's
      cells show it. A chord in the scale gives the bar its degree unless the bar has a DEG.
- [ ] **CHORD LOOP** subpage: BAR and CHORD, eight cells with the playhead; EDIT + chord key writes the bar and moves
      on, and the page says EDIT+KEY: SET BAR.
- [ ] **Playing it:** at each bar with a CHORD, release and start on the chord track and the bass track; release on
      stop, the jump back to IN, hold HOME, a swap to STEP, a project load. PLAY's key line shows the chord sounding.
- [ ] **CHORDS and BASS** tracks on PLAY; a drum track set to either sends MIDI only.
- [ ] **MIX OUT:** SOUND or MIDI per track, applied to the tape, STEP, the keys, the arpeggiator and the loop. HOME says
      MIDI for such a track, and a key with no computer listening says `MIDI ONLY: NO USB`.
- [ ] **Tests:** MIDI out checked message by message over a loop with Start and Stop; no note left on after any
      transport change; no voices used by a MIDI track; REV, 0.5X, 2X; an older tape loads unchanged; a KEY track follows a chord bar, a DEG on the same bar wins, and a
      note recorded over a chord bar plays back as played.
- [ ] **In a DAW** (the owner): four bars recorded in time from bar 1, chords and bass on their own channels, no stuck
      notes.

## Sprint 4: feel and suggestions

- [ ] **SUGGEST:** up to three pads blink and are outlined, ranked from the root's movement.
- [ ] **HUMAN:** small delays and lower velocities, never past the next note-on of the same pitch, off at 0 to the bit.
- [ ] **STYLE:** BLOCK, PULSE, UP, OFFBEAT for the loop.
- [ ] **FEEL** subpage with the three.
- [ ] **Tests:** HUMAN 0 and STYLE BLOCK give sprint 3's output exactly; offs matched in every style.

## Not in this plan

Following a DAW's clock, more than eight scales, a `.mid` export, chord detection, a web editor for chord sets.
