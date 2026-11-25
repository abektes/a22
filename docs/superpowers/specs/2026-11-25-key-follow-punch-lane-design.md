# The tape follows the key, punch-in effects, and a chord lane

Design, 2026-11-25. Three phases, built and shipped in this order, each usable on its own:
1. **KEY FOLLOW:** the tape and the step sequencer follow ROOT and SCALE.
2. **PUNCH:** momentary effects on held keys.
3. **LANE:** a key and chord change per bar that the tracks follow.

## Why

Players' feedback after 0.6.0:
- "an option to make the notes in sequencer mode transpose if you change the key/scale";
- "more OP-Z and OP-XY type 'brain' settings, that layers chord changes on a master sequence lane";
- "punch effects".

Today ROOT and SCALE (GLO → KEYS) only snap the keys as they are pressed (`scale_snap`, `core/ui.c:477`). The tape and the step sequencer store the note that came out, so a recorded part can't be moved to another key, and there is no way to play a progression over a loop. TRICK (TAPE page: BRAKE and three stutters) exists, but as a setting left on, not something played.

## Goals

- **Phase 1, KEY FOLLOW:** a track with KEY on plays its tape and step notes in the current ROOT and SCALE. Notes keep their place in the scale (C–E–G recorded in C major plays C–E♭–G in C minor). Changing ROOT moves the part to the new key.
- **Phase 2, PUNCH:** holding FX and one of the eight white keys plays an effect on the whole mix for as long as the key is held:
  - BRAKE and stutters at 1/8, 1/16 and 1/32, which TRICK already has;
  - LOW-PASS sweep, HIGH-PASS sweep, CRUSH and GATE 1/16, which are new.

  Punches play live and record onto the tape like knob moves.
- **Phase 3, LANE:** one lane on the tape with an optional entry per bar (ROOT, SCALE, DEG I–VII). Tracks with KEY on follow the lane's key bar by bar, so I–IV–V–vi is four bars of DEG.

## Non-goals

- MIDI in driving the tracks (channels 1–4 playing T1–T4, following a DAW's clock): parked, a separate design.
- A lane for the step sequencer: in STEP mode the pattern follows GLO's ROOT and SCALE only.
- Punch effects in the step sequencer's recording, or sent over MIDI.
- A key recorded per note for tapes made before Phase 1: they play as recorded.

## Anti-goals

- **A note left sounding,** in the FM-1 or on MIDI out, because the key changed while it played. A note's off ends the note that was actually played and sent.
- **Drums moving.** KIT and SKINS put a different drum on each note: they never follow.
- **A tape or setting from 0.6.0 or earlier that sounds different** before the player turns KEY on. KEY is OFF by default.
- **A tape that older firmware can no longer load.** Older firmware may lose the lane and the punches, but must still play the notes.
- **A new hidden gesture.** Every control sits on a page as the existing ones do, or uses a gesture A22 already teaches (a button held with a white key, like SAVE + key).
- **Audio overload.** PUNCH effects stay within the FM-1's audio budget (measured with CC 119 on the device: LATE 0, SHED 0 with an eight-note chord and PLATE while a punch is held).

## Constraints

- The FM-1:
  - a 240 × 240 screen with four knob columns a page, SELECT for subpages;
  - 27 keys (the eight white keys from the lowest F are SAVE's snapshot keys);
  - buttons with a hold tap when let go (README "Hotkeys").
- `core/`: portable C99, no libc, no malloc; 440 KB of 475 KB SRAM in use; the firmware image is 568 KB of a 582 KB flash slot (0.6.0 + MIDI out). New tables must be small.
- Saved state stays version 5. The two spare bytes `H1State.rsv[2]` and each `Track.rsv` are used, and the struct's layout is unchanged.
- The tape blob (`eng_loop` / `eng_loop_load`) stays readable by 0.6.0. Its header's spare byte `h[5]` and each event's spare byte `LEv.rsv` are used, plus new event kinds 3 and 4. (Flags bit 3 is `LEV_MIDI`, from the MIDI out branch; bits 4–6 are free.)
- The screen reader (fm1-reader) speaks each page, subpage and control as they change. New subpages and controls must be named so it can.

## Approaches

**Phase 1: how a recorded note remembers its key.**
- **A. Picked: each note carries the key it was recorded in,** in the event's spare byte. On playback it's mapped from that key into the current one. Old tapes (no key) and older firmware (which zeroes the byte) are unaffected, and overdubs made in different keys each keep their own.
- B. One key for the whole tape, notes converted into it as they're recorded: a note outside a five- or six-note scale, or recorded in another key, loses detail.
- C. Store scale positions instead of notes: older firmware could no longer read the tape.

**Phase 2: how a punch is played.**
- **A. Picked: hold FX and press one of the eight white keys.** It's the gesture SAVE already teaches.
- B. A page of knobs: knobs can't be momentary.
- C. Keys trigger punches while the FX page is open: the keys could no longer play on that page.

**Phase 3: where the lane lives and how it's set.**
- **A. Picked: entries stored as tape events, set on a third TAPE subpage, LANE.** They're saved with the tape and survive a power cut while it plays.
- B. Recorded by turning ROOT while recording: imprecise, and one bar can't be fixed afterwards. It could be added on top of A later.
- C. In the saved settings: separated from its tape, and it needs a settings upgrade.

## Phase 1: KEY FOLLOW

### Behaviour

- **The KEY switch.** Each track has a KEY setting, OFF by default.
  - With KEY on, the tape's and the step sequencer's notes on that track play mapped from the key they were recorded in into the current key.
  - With KEY off, they play as recorded, as today.
- **The current key** is GLO → KEYS ROOT and SCALE. In Phase 3, it's the lane's key for the bar.
- **Drums.** Tracks whose engine is KIT or SKINS show DRUM and never follow. Each note is judged by the engine it was recorded on (the event's `part`), so a note recorded on KIT never moves, even on a track that later holds a melodic engine.
- **Two recorded notes on one pitch.** Two notes that map onto one pitch on one track sound once, and end with the last of them, as chord memory's shared notes do (`ch_cnt`). The table of what was sent counts them.
- **Every recorded note stores the key in force when it was recorded.** That covers notes from the keys, chords, the arpeggiator, Riff, Shake and MIDI in, through `tape_record_note` and `seq_to_tape`.
- **A recorded key that's absent** (a tape from before Phase 1) means the note plays as recorded, whatever KEY says.
- **A key change applies to the next note-on.** A note sounding when ROOT or SCALE changes keeps its pitch until its off, and its off ends that pitch.
- **What's heard is what goes out.** The mapped note is what the voice plays, what MIDI out sends (on the track's channel), and what a note-off ends.
- **Live keys, the arpeggiator and Riff are unchanged.** They already follow ROOT and SCALE as they're played.
- **The step sequencer** has one key for its whole pattern (`H1State.rsv[0]`):
  - It's set to the current key on the first step written when it has none: into an empty pattern, or into one that came back from 0.6.0 (steps, but no key). That pattern's earlier steps are then taken to be in that key.
  - A step written later in another key is mapped into the pattern's key as it's stored, so it plays back as written when the key returns. This happens only for a step on a track with KEY on and a melodic engine. A step on a KEY-off or drum track is stored as played, as today.
  - Clearing the whole pattern clears the pattern's key.

### The mapping

Notes are mapped from the recorded key (root R₀, scale S₀) to the current key (R₁, S₁).

1. **Both scales have seven notes** (MAJOR, MINOR, DORIAN): find the note's octave, its degree in S₀ from R₀, and its accidental if it's outside the scale. An accidental is how many semitones it sits above the scale note below it. Play the same octave, degree and accidental in S₁ from R₁. So C major → C minor maps E → E♭ and B → B♭, and C minor → C major maps E♭ → E.
2. **The scales have different sizes,** or either is CHROM, PENTA, MPENT, BLUES or WHOLE: move the note by R₁ − R₀ semitones, then, unless S₁ is CHROM, snap it to S₁ as `scale_snap` does.
3. **The same key:** the note as recorded.
4. **Octaves:** the root moves by the smallest interval, from −6 to +5 semitones, so a part never jumps an octave when the root goes from B to C.
5. **Range:** the result is clamped to 0–127.

### Data

- **`LEv.rsv`** (tape events):
  - bit 7: a key is recorded;
  - bits 4–6: the scale (0–7);
  - bits 0–3: the root (0–11).

  It's written by `tape_record_note` and for the notes `seq_to_tape` records. CUT, PASTE and the clip keep it as part of the event.
- **The tape header `h[5]`:** bit 0 means "the events carry keys". It's written by `eng_loop`. `eng_loop_load` keeps `e[7]` only when that bit is set, and otherwise zeroes it, as today.
- **`H1State.rsv[0]`:** the step pattern's key, same encoding.
- **`Track.rsv`:** bit 0 is KEY on.
- **Validation:** `h1_state_load` and the defaults must stop zeroing `rsv[0]`, and must accept only valid values (a valid bit with a scale ≤ 7 and a root ≤ 11, otherwise 0; KEY bit 0, other bits cleared). `rsv[1]` stays zeroed.
- **What the tape sent:** a table `tape_sent[NTRACK][128]` (512 B) records, per track and recorded note, the note that went out at its on, 0xFF for none. The off and every release path end that note. That covers `tape_release_seq`, `tape_release_trk`, CUT, clearing, stop, and the tape's `tape_open` record of open MIDI notes from the MIDI out branch, which now holds sent notes.
- **The step sequencer** already ends what it sent (`step_snd`).

### UI

- **MIX gets a fifth subpage, KEY,** after the master EQ (SEL, then SELECT four times). Its four columns are T1–T4, as on VOL, PAN and SEND. Each shows OFF or ON, or DRUM for a KIT or SKINS track, and DRUM can't be changed. The screen reader names it "KEY", with "T1 ON" and so on.
```
 MIX · KEY                         5/5
   T1      T2      T3      T4
   ON      ON     DRUM     OFF
  (K1)    (K2)    (K3)    (K4)
```
- **GLO → KEYS is unchanged.** Turning ROOT or SCALE there is the gesture that moves the following tracks.
- **README:**
  - the Pages table gets MIX's KEY subpage;
  - "The tape" says how KEY works and that tapes from earlier versions play as recorded;
  - "STEP" says the pattern has a key.

## Phase 2: PUNCH

### Behaviour

- **Holding FX** shows the punch strip over the screen. The eight white keys from the lowest F (SAVE's snapshot keys, `SNAP_KEYS`) are then punches 1–8:
  1. BRAKE;
  2. ST 1/8;
  3. ST 1/16;
  4. ST 1/32;
  5. LPF (a low-pass sweeping down);
  6. HPF (a high-pass sweeping up);
  7. CRUSH (bits and rate down);
  8. GATE (the mix chopped at 1/16, in time with TEMPO).

  The other keys play notes as usual while FX is held, as they do while SAVE is held.
- **Holding the key holds the punch.** A punch starts when its key goes down and ends when it comes up, even if FX was let go first.
- **One punch at a time.** A second key replaces the first, and letting go of the second doesn't bring back the first.
- **FX now has a hold, so a tap opens the FX page when FX is let go,** as GLO and SEQ do. FX let go after it was used for a punch doesn't open the page. EDIT + FX (bypass) is unchanged.
- **A punch overrides TRICK while held.** TRICK (TAPE page) stays a setting. While a punch is held it replaces TRICK, and when it ends, TRICK's setting is back.
- **No clicks.** Starting and ending a punch don't click: ~10 ms ramps, and the stutter and brake buffers as TRICK uses them.
- **Recording.** While the tape records or overdubs, a punch's start and end are recorded on the track in hand as tape events, kind `LEV_PUNCH` (3):
  - d1 is the punch, 0–7; d2 is 1 for its start and 0 for its end.
  - On playback they act on the whole mix as if played.
  - A muted track's punches don't play.
  - Holding REC (clear the track), CUT and PASTE treat them like that track's knob moves.
  - In REV their starts and ends swap, as notes' do.
  - A punch still held when the tape stops, or goes round, ends.
  - A punch held by hand wins: the tape's punch events are ignored while one is held.
- **Not over MIDI:** punches aren't sent over MIDI out.

### Effects

Effects 1–4 are TRICK's (`fx_master_block`, `TRICK_BRAKE`, `TRICK_STUT8..32`), with the punch's number in place of `st.tape[3]` while it's held. Effects 5–8 are new stages in `fx_master_block` after the TRICK stage:
- **LPF:** a 2-pole low-pass sweeping from fully open to ~250 Hz over one bar at TEMPO, then holding there.
- **HPF:** a high-pass sweeping from ~20 Hz to ~2 kHz over one bar, then holding.
- **CRUSH:** 6 bits and a sample-and-hold at ~1/6 of the rate.
- **GATE:** the mix's level 1 for the first half of each 1/16 and 0 for the second, with 2 ms edges, in phase with the tape (or with the free-running beat when the tape is stopped).

The exact values are the builder's, tuned by ear within these. The new stages run only while their punch is held: with no punch, they cost nothing.

### UI

- **The strip** draws while FX is held, in the bottom third of any page, over the knob strip:
```
 holding FX:
  F    G    A    B    C    D    E    F      (white keys 1–8)
 BRK  ST8 ST16 ST32  LPF  HPF CRSH GATE
```
  - The held punch is lit in the FX colour, and a toast names it ("PUNCH ST 1/16").
  - The FX button's light stays on while FX is held.
  - The screen reader says "PUNCH" when FX is held, and the punch's name when one starts.
- **README:** the Hotkeys table gets "hold FX + white key 1–8: punch effect while held" and "FX: opens the FX page when let go". The Effects section gets a PUNCH paragraph.

## Phase 3: LANE

### Behaviour

- **The lane** is one per tape, with up to one entry a bar.
- **An entry** has ROOT (— or C to B), SCALE (— or CHROM to WHOLE) and DEG (— or I to VII). An entry with all three at "—" is no entry.
- **The key in force at bar b** comes from the latest entry at or before b in the loop (from bar 1, not wrapping):
  - its ROOT, or GLO's ROOT where it has "—";
  - its SCALE, or GLO's SCALE where it has "—";
  - its DEG, or I where it has "—".

  Before any entry, it's GLO's key and DEG I.
- **Tracks with KEY on follow it.** Their notes are mapped into that key (Phase 1's mapping), then moved DEG − 1 steps up the scale within that key. IV means three steps up, so C–E–G in C major becomes F–A–C. DEG does nothing when the scale in force is CHROM.
- **Timing.** A change applies from its bar's first tick: a note whose on fires at or after it uses the new key. Notes still sounding keep their pitch (Phase 1).
- **Live keys** snap to the root and scale in force while the tape plays (so playing along fits), not to DEG. When the tape is stopped they snap to GLO's key as today. Chord memory's SCALE mode (`ch_scale_up`) uses the same root and scale as the live keys.
- **Recording over a lane bar keeps what was heard.** A note recorded on a KEY-on track over a bar whose DEG isn't I is stored with the root and scale in force, moved down DEG − 1 steps (the reverse of playback). It then plays back as played, and follows later changes. On a KEY-off track it's stored as played, with the root and scale in force as its key, so turning KEY on later maps it from there.
- **Tape mode only.** STEP mode ignores the lane.
- **Lane entries belong to the whole tape,** not a track:
  - CUT, PASTE and holding REC on a track leave them;
  - a tape that holds only lane entries isn't empty: the next take overdubs from IN at the tape's BARS, so the lane is kept;
  - an entry keeps its tick when METER changes, and the bar it falls in is the one it applies to;
  - CLEAR ALL clears them;
  - BARS turned down keeps entries past the end, as it keeps notes there.

### Data

- **Entries are tape events,** kind `LEV_KEY` (4):
  - tick: the bar's first tick;
  - flags: bit 4 `LEV_GLOBAL` (not a track's);
  - d1: ROOT (0–11, 127 for "—");
  - d2: SCALE (0–7, 15 for "—") in bits 0–3 and DEG (0–6, 7 for "—") in bits 4–6;
  - trk: 0.

  The packing is the builder's if it's documented and round-trips through save and load.
- **The UI sets entries through an engine event,** `EV_LANE` with bar, field and value, so the engine stays the tape's one writer. Entries count against the 2,048 tape events (32 at most).
- **A per-bar cache of the key in force** (32 × 2 bytes) is rebuilt when an entry changes, the tape loads, or BARS or GLO's key changes. Playback reads it at each note-on.

### UI

- **The SEQ page in tape mode gets a third subpage, LANE,** after DECK and EDIT (SEQ, SELECT twice):
  - **BAR (K1):** 1 to the tape's BARS. Turning it moves the lane's cursor, and it isn't saved.
  - **ROOT (K2), SCALE (K3), DEG (K4):** that bar's entry. Turning any of them past its first value gives "—".
```
 TAPE · LANE                       3/3
 BAR 5   ROOT D   SCALE MAJOR   DEG IV
 │I │I │IV│V │I │— │— │vi│…   (one cell a bar, the cursor's lit, the playhead's outlined)
  (K1)    (K2)       (K3)        (K4)
```
- **The cells:** each bar's cell shows DEG's numeral in force (lower case for a minor chord in the scale), and "—" where no entry starts. Only entries are drawn in full colour; inherited bars are dim.
- **The DECK subpage** draws a thin lane under T1–T4 with a mark at each bar that has an entry. While the tape plays and the lane has entries, its header shows the key in force ("D MAJ · IV").
- **The screen reader** speaks "LANE", then "BAR 5", "ROOT D", "SCALE MAJOR", "DEG IV" as they change.
- **README:** the Pages table's TAPE row gets LANE. "The tape" gets a LANE paragraph: how entries inherit, that KEY must be on for a track to follow, and that older firmware loses the lane and punches.

## Compatibility

- **0.6.0 loading a tape saved after these phases:**
  - it zeroes `LEv.rsv`, so notes play as recorded;
  - it never sees a punch or a lane entry. In the saved blob, kinds 3 and 4 are written with their tick plus 15,360 (32 × 480). 0.6.0's loader drops any event at 15,360 or more (`if (tick >= 32u * 480u) continue;`), and the new loader takes 15,360 off again. Otherwise 0.6.0 would play them as notes, in REV as note-ons that are never ended. In memory, the ticks are the real ones;
  - the lane and punches are lost.
- **0.6.0 loading a state saved after these phases:** it zeroes `rsv`, and ignores `Track.rsv`, so KEY is lost and nothing else.
- **These phases loading 0.6.0's tapes and states:** `h[5]` is 0, so notes play as recorded; `rsv` is 0, so KEY is off and the step pattern has no key.

## Tests

The tests are core-alone C programs in `tests/`, built with the address and undefined-behaviour checkers and run by `tests/run_tests.sh`, plus host-simulator scripts and the crash hunt.

### Phase 1: `tests/keyfollow.c`
- **The mapping, as a table:**
  - C major ↔ C minor ↔ D dorian for every note 0–127;
  - accidentals kept;
  - roots B → C and C → B moving by a semitone, not an octave;
  - different-size scales by shift and snap;
  - CHROM by shift only;
  - clamped at 0 and 127.
- **KEY off:** a take in C major plays the same notes after ROOT goes to D.
- **KEY on:** ROOT D moves it up two semitones; SCALE MINOR flattens the third, sixth and seventh, checked on MIDI out per channel.
- **Drums:** a KIT track with KEY on never moves, and the KEY column shows DRUM.
- **A tape from before:** `h[5]` 0 plays as recorded with KEY on.
- **ROOT changed while a note sounds:** its off ends the note that sounded, with exact balance on MIDI out and no voice left gated. The same while the tape stops, wraps, CUTs and clears mid-note.
- **Overdubs in two keys:** each follows from its own.
- **The step pattern:** the pattern key is set on its first step; a step written in another key is stored mapped; the pattern follows; clearing it clears its key.
- **Save and reload:** KEY and the pattern key survive a power cycle (`h1_state`/`h1_state_load`), and invalid `rsv` values load as 0.
- **Tape blob:** a save, then a load, keeps keys (`h[5]` set). A blob with `h[5]` 0 but junk in `e[7]` loads with the keys dropped.
- **`tests/state.c`:** 0.4.0's and 0.5.0's states still load, to the byte where they did.

### Phase 2: `tests/punch.c`
- **Holding FX and a white key** starts that punch, and releasing the key ends it, with FX let go first or last. The other keys play no note while FX is held. A second key replaces the first.
- **The FX tap:** FX tapped alone opens the FX page on release; FX used for a punch doesn't.
- **Each punch changes the sound,** measured: BRAKE lowers the pitch over time; each stutter repeats at its length at TEMPO; LPF lowers the brightness over a bar; HPF lowers the energy below 200 Hz; CRUSH raises the error against the clean signal; GATE leaves silent halves of each 1/16.
- **No click:** at a punch's start and end, no sample-to-sample step above a threshold.
- **TRICK is back** after a punch ends.
- **Recording:** a punch played while recording is on the tape and plays back at the same place. A muted track's punch doesn't play. CUT and holding REC on the track remove it. REV swaps its start and end. A punch held when the tape stops ends.
- **No MIDI:** nothing about punches goes out over MIDI.
- **Cost:** with no punch, the sound is the same to the bit as before Phase 2. `tests/sleep.c`'s comparison style is the model, so the master bus is unchanged when nothing is punched.

### Phase 3: `tests/lane.c`
- **Entries:**
  - setting one through the LANE subpage's knobs;
  - "—" on all three removes it;
  - entries survive a save, a power cycle and a load;
  - CUT, PASTE and holding REC on a track keep them; CLEAR ALL removes them;
  - BARS turned down and up again keeps one past the end.
- **The key in force:** inheritance (ROOT only on bar 3 keeps SCALE and DEG), nothing before the first entry, and no wrapping from the last bar.
- **Following:** with KEY on, a tape follows bar by bar (I–IV–V–vi checked on MIDI out), and DEG does nothing in CHROM. With KEY off, the lane changes nothing.
- **At bar boundaries:** a change takes effect at the bar's first tick, and a note sounding across a boundary ends on the note that sounded.
- **Live keys** snap to the lane's root and scale while the tape plays, and to GLO's when it's stopped.
- **STEP mode** ignores the lane.

### Every phase
- `sh tests/run_tests.sh` passes, and `sh tests/run_fuzz.sh 16` finds nothing.
- The fuzzer's panel input reaches the new subpages and FX + key, and its tape blobs carry kinds 3 and 4 and `h[5]`.
- A host-simulator script per phase in `tests/` drives the device app: the KEY subpage, FX + key, the LANE subpage, read back with `screen`.
- On the FM-1:
  - an install;
  - CC 119 with an eight-note chord and PLATE while each punch is held (LATE 0, SHED 0, AUDIO peak within 85%);
  - a tape following ROOT changes and a four-bar lane, recorded in Studio One on four channels.

## Flash budget

About 14 KB of the 582 KB flash slot is left after the MIDI out branch. The image size is read from the CI firmware job after each phase, since there's no local JieLi toolchain.

**Step 0, before Phase 1 (the user's decision, 2026-11-25): STACK's and ELECTRIC's specialised renders come out.** `cluster_render_a` (`core/inst_stack.c`) and `elec_render_a` (`core/inst_electric.c`) go back to one render that takes the algorithm as a variable, as before 6494cf7.
- **Flash:** this frees about 25 KB (15.0 + 10.3 KB, measured from the two builds' `a22.elf` in `docs/PLAN-after-sprints.md`), so about 39 KB in all.
- **Sound:** it stays the same to the bit; `tests/cores.c`'s section hashes and `tests/sleep.c` are unchanged.
- **Audio time:** the cost measured in 0.5.0 was +3.0 points on STACK and +2.1 on ELECTRIC (eight-note chords, PLATE), with peaks of 50–65% before. CC 119 on the FM-1 re-measures both engines' eight algorithms after the change: no algorithm's peak past 80%, LATE 0, SHED 0.
- **Estimate:** the three phases come to about 12–19 KB, so the fallback below is not expected to be needed.
- **Keep:** FM-4's and ANALOG's specialised renders stay, and so does SHAPER's (77% without it). If a phase doesn't fit (the user's choice): fewer punch effects first. Keep BRAKE, the three stutters, LPF and HPF; drop CRUSH and GATE, whose keys then do nothing. KEY FOLLOW and the LANE stay whole. If it still doesn't fit, stop and ask before cutting anything else.

## What's left to the builder

- **Phase 2's effect values** within the ranges above, by ear and measurement.
- **The lane event's exact packing, and the per-bar cache's form,** if documented and round-tripping.
- **How the punch strip and the LANE cells are drawn,** in the existing vector style (`core/ui.c` draw functions, the FX colour for punches, the four knob colours for the LANE columns).
- **Whether the effects come from `seq_to_tape`'s callers or `tape_record_note` itself,** if every recorded note gets its key.
- **Small refactors** that keep `core/synth.c` readable, such as the mapping in a file of its own (`core/keymap.c`) with its own table test.
