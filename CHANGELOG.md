# Changelog

A22's own history starts here. It is derived from h1 as of its 0.3.0; h1's history is in
[jakehandy/h1's CHANGELOG](https://github.com/jakehandy/h1/blob/main/CHANGELOG.md).

## Unreleased

**PUNCH**
- Hold FX and one of the eight white keys from the lowest F for a punch on the whole mix, while the key is held: BRAKE,
  ST 1/8, ST 1/16, ST 1/32, LPF, HPF, CRUSH, GATE. Other keys play notes, a second key replaces the first, and the
  key holds the punch after FX is let go. FX tapped alone opens its page on release; one used for a punch does not.
  With FX held first, SAVE and EDIT do nothing; hold EDIT first, then FX, for the bypass as before.
- A strip at the bottom of the screen shows the eight punches, the held one lit, with a toast (PUNCH ST 1/16); FX's
  light is on while it is held, and the screen reader says PUNCH once FX is held (not on a tap), then the punch's name.
- Recording or overdubbing on the tape records the punches on the track in hand, and they play back on the whole mix
  (not on a muted track). CUT, PASTE and clear treat them like knob moves, REV swaps their starts and ends, and a
  punch ends at stop and at the wrap (and where a take ends, if you still hold it). One held by hand wins over the tape's. Punches do not go over MIDI.
- Tapes saved with punches load in 0.6.0 without them (it drops what it does not know).

**LANE**
- TAPE has a third subpage, LANE (SEQ, then SELECT twice): BAR, ROOT, SCALE and DEG, one entry at most a bar, 32 bars.
  Tracks with KEY on play the tape in the key in force and move each note DEG - 1 steps up the scale (IV: C E G in
  C major plays F A C; DEG does nothing in CHROM). A field no entry has set comes from GLO's ROOT and SCALE, and
  DEG I: each field holds from the latest entry that sets it, from bar 1, with no wrap. Drums never follow.
- A change applies from its bar's first tick; sounding notes keep their pitch. Live keys and chord memory's SCALE
  mode snap to the key in force while the tape plays (not DEG), GLO's when it is stopped; STEP ignores the lane.
- Recording over a DEG bar on a KEY-on track stores the note moved down DEG - 1 steps, so it plays as played; a note off the bar's scale is stored as played.
- Entries belong to the whole tape: CUT, PASTE and holding REC leave them, CLEAR ALL clears them, a tape with only
  entries is not empty, an entry keeps its tick when METER changes, and BARS turned down keeps entries past the end.
- The cells show a bar each (entry bars bright, inherited bars dim, a minor degree's numeral underlined), the DECK
  subpage marks entry bars and shows the key in force while playing (D MAJ . IV). The screen reader says NONE for "-".
- Tapes saved with a lane load in 0.6.0 without it and without punches; their notes play as recorded.

**The tape follows the key**
- MIX has a fifth subpage, KEY (SEL, then SELECT four times): T1 to T4, OFF or ON (DRUM on a KIT or SKINS
  track). A track with KEY on plays its tape in GLO's KEYS ROOT and SCALE, from the key each note was recorded in:
  a seven-note scale keeps a note's place in the scale, any other shifts it by ROOT and snaps. Drums never move.
- The tape remembers each note's key, and the step pattern has one (its first written step's; clearing the whole
  pattern clears it). KEY is off to begin with, and tapes and settings from 0.6.0 and earlier play as recorded.
- The screen reader says the columns as KEY's knobs turn (T1 ON), and MIDI out sends the notes as they sound.

**MIDI out for a DAW**
- Each track goes out on its own MIDI channel, T1 on 1 to T4 on 4: the tape's and the step sequencer's notes on
  their track's, the keys' and the arpeggiator's on the track in hand's, each note-off on its note-on's channel.
  The knobs' CC 20 to 23 go out on the same channels, and so do the tape's recorded knob moves as it plays.
  HOME held sends CC 123 on all four. Playing on T1 sends what it sent before.
- MIDI clock, 24 a beat at TEMPO, all the time, with Start as the tape or the step pattern starts (before the note
  that starts it) and Stop as it stops. The tape jumping back to IN while it plays (BARS turned down, IN or OUT
  moved past it) sends Stop and Start again, and a computer that connects while the tape plays gets them when the
  tape next comes round to IN. When the USB queue is busy the FM-1 keeps its last places for note-offs, CC 123,
  Start and Stop, then the clock, then notes and knobs. A keyboard on USB host gets no clock, Start or Stop.
- A knob set over MIDI while recording plays back from the tape but is never sent back out (the tape marks it;
  earlier firmware loads such a tape as before).

**The tape's timing**
- The tape keeps its place in whole ticks and a fraction. Added up in one float, it lost a little of every 0.7 ms
  far into a long tape, and a 32-bar tape came round up to 7 MIDI clocks (about 150 ms at 120) off the clock in five
  passes. It now keeps to the clock pass after pass, at every SPEED and in REV. Notes on a long tape land a fraction
  of a tick from where 0.6.0 put them.

## 0.6.0

Identity `FM-1_4122000600`. Released 2026-11-25. More room for the audio in a dense loop jam
(`docs/PLAN-cpu-headroom.md`), and the sound otherwise the same to the bit (the two-core and corestat hashes are
unchanged from 0.5.0, and `tests/sleep.c` holds the two savings below that could have changed it). Timed and run on
one FM-1 (CI's test package of the same code, 2660a1e), no crash recorded:
- eight-note chords on all 112 algorithms with PLATE: 47.7% of the audio time on average, 72% at the peak, 45 frames a
  second (0.5.0: 50%, 75%, 42);
- the dense loop jam (`tools/fm1_profile.py jam`), which missed 35 and 22 halves on 0.5.0 at 104 and 106%: no half
  missed and no voice let go in two runs, 84 and 87% at the peak with four keys held on top, and none in 18 minutes
  of its tape playing (65% on average);
- DELAY, SHIFT, CHORUS and TANK with nothing playing: 10% each, as PLATE (13 to 15% on 0.5.0).

Settings and the tape saved by 0.1.0 to 0.5.0 load as before: the state is still version 5, and a tape saved while
the jam played came back after a power-off.

**Audio time**
- PLATE's two halves, the master bus's EQ and drive, each voice's gain, pan and declick, and the output now run from
  RAM on the FM-1, out of the way of the instruction cache both cores share, which the drawing keeps pushing the sound
  out of. About 5 KB. The engines' own renders stay in flash for now.
- The second core renders the larger half of an odd number of a part's voices, and each core does its own voices'
  gain, pan and declick, so the first core only adds them up.
- DELAY, SHIFT, CHORUS and TANK sleep once their line has run out, as PLATE does: with nothing playing they no longer
  walk their whole line every sample.
- A WIRE note clears only the part of its string it can read before it writes there itself, not all 2,048 samples
  (eight times over on a chord's attack, right when the audio is busiest).
- The tape's flash writes, a page a frame while a take is saved, wait while the renders are heavy and then go just
  after one, since each page holds the interrupts (the audio's too) off while it is written.
- The input scan runs at 5 kHz instead of 10, which frees about 4% of the first core. A key is counted within 2.2 to
  4.4 ms instead of 1.1 to 2.2, a knob spun past about 110 clicks a second loses steps, and the unlit buttons' glow
  is dimmer.

**Inside**
- PLATE and the echo effects share one 64 KB buffer (one effect runs at a time, and each clears its own on the way
  in): 48 KB of SRAM back, which the RAM code above uses part of.
- The PERF line (MIDI CC 119 to a listening screen reader) ends with `STAGE seq voices wait mix fx master`, the
  slowest render since the last PERF split by stage in percent of its block, and `FLASH n max`, the tape's flash
  writes and the longest in microseconds. `tools/fm1_profile.py` prints both.
- The tape's picture and white keys read the recorded events only when the tape has changed, not twice a frame.

## 0.5.0

Identity `FM-1_4122000500`. Released 2026-11-23. Timed and run on one FM-1 (CI's build of the same code,
52bd8db). Settings and the tape saved by 0.1.0 to 0.4.0 load with everything kept.

**Known issue**
- Now and then the FM-1 does not come back after an update. On one FM-1, two of five installs between 0.4.0
  and this release stumbled: once its USB went away at the switch to update mode, once it took about two and
  a half minutes to come back. If the installer says the FM-1 did not come back, give it a couple of minutes;
  if it still has not, unplug the USB cable and plug it in again (switch the FM-1 off and on if it still
  does not show) and run the install again: the update loader is waiting and finishes the write. Nothing is
  lost, and no crash was recorded. The cause is not found yet.

**Sound**
- A thirteenth engine, CYCLE: WAVE's player on four banks of fifteen single cycles of recorded instruments,
  from Adventure Kid Waveforms (AKWF, CC0 1.0, by Kristoffer Ekstrand; `docs/WAVETABLES.md`). STRING (cello,
  violin), REED (clarinet, flute, alto sax, oboe), KEYS (piano, electric piano) and VOICE, each from dark to
  bright, and the same four stepped (STEP-STR, STEP-RD, STEP-KEY, STEP-VOX): the place moves from whole wave
  to whole wave, with a glide of a few milliseconds between so nothing clicks, and SCAN set further so a note
  steps through the bank. The knobs are WAVE's. Each wave keeps its first 48 harmonics and its bank's
  shared phases, so a morph is within about 1 dB of the waves either side (`tools/akwf.py` says how they
  were picked). About 64 KB of flash. WAVE sounds the same to the bit: its tables are rewritten unchanged
  and its renders were compared before and after. `tests/basics.c` checks SPREAD on every CYCLE bank and
  that the STEP- algorithms step, on both sides alike (with SPREAD up the right side once jumped from wave to
  wave while the left glided); the two-core and DICE tests cover CYCLE with the rest.
- A fourteenth engine, SKINS: two drum kits made only of recordings, in KIT's layout (C kick, C# rim, D snare,
  E F G toms, F# hat, A# open hat, B ride or cymbal). STUDIO is an acoustic drum set: the house kit of
  [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums) (CC0, Versilian Studios and Karoryfer
  Samples), close mics: a snares-off kick, a centre snare and its cross-stick, a hand-muted low tom and a high
  tom played as three, closed and open hats, a ride; with VCSL's clap, tambourine and cowbell. (Picked by ear
  on a minimal lo-fi beat over VCSL's, whose kick was a concert bass drum cut short.) ORCH a marching and
  orchestral kit (concert bass drum, marching snare and rimshot, a rope snare, two tenor drums as three toms,
  clash cymbals held short and let ring, triangle, timpani, suspended cymbal). Each kit four ways: dry
  (STUDIO, ORCH), in a room (LIVE, HALL: a longer ring and two early reflections, read from the hit itself a
  little later, so no delay line, with no low end, so a kick is not thinned by copies of itself), tight
  (TIGHT, MARCH: gated, with a harder start) and vintage (VINTAGE, SHELLAC: fewer samples and steps, darker,
  driven). The closed hat cuts the open one short, the short clash the long one. Knobs TUNE, DECAY, TONE,
  ROOM; the ENV page's PUNCH (0 at the start) hardens the kick, snare and toms, as on KIT. Fourteen new hits
  (Virtuosity Drums' eight, pinned to one commit, and VCSL's six for ORCH), cut by `tools/samples/drums.py`
  into the pool KIT already uses (the rest are KIT's): 18,541 bytes of flash. KIT sounds the same to the bit:
  its 33 hits are cut again unchanged. Level matched to KIT on a bar of beats below the limiter; at full track
  volume SKINS is 0.4 to 2 dB under KIT (its hits peak higher). `tests/skins.c` checks every drum on every
  algorithm sounds and ends, the chokes, ROOM and the treatments; `tests/state.c` that a state from before
  SKINS brings it up as new. STUDIO's picks are settled; the algorithm names and ORCH's picks wait to be
  judged by ear. Its picture is a ghost in a shell, three layers: in the middle the ghost (the low end), a
  glow that swells on the kick; round it the skin, a membrane the snare, rim, clap and toms dent where their
  pan puts them, springing back; outside the shell, metal segments the hats and cymbals shimmer, pushed apart
  by the open hat and snapped shut by the closed one. ORCH's layers are octagons, STUDIO's round; LIVE and
  HALL send ghost shells drifting out, TIGHT and MARCH pull the layers in with short, sharp dents, VINTAGE and
  SHELLAC are gold, coarse and a little broken. TUNE spaces the layers, DECAY sets how slowly a dent springs
  back, TONE how bright the shell is, ROOM how many ghost shells.

- PERC rings with eight modes a note where it had four: each algorithm keeps its four as they were and
  gains four quieter, shorter ones (HANDPAN's and STEEL's beating doublets, MARIMBA's and KALIMBA's
  upper bar and tine modes, TABLA's head on up its near-harmonic series, WOOD's higher block modes,
  GAMELAN's split bronze, GLITCH's square roots), heard 13 to 25 dB under the whole. **Saved sounds change
  a little:** a tape or snapshot on PERC plays the new modes too (as loud as before, within 0.2 dB).
  SPACE from 60 up brings in two sympathetic strings, an octave and a twelfth over the note, struck by the
  mallet and the modes and ringing on after them; at SPACE's default (52) and below 60 there are none. A
  mode that has died away is not worked out until the next strike. On the FM-1, eight-note chords under
  PLATE peak at 74% of the audio time (GAMELAN; 51% before), 78% (HANDPAN) with RING and SPACE at 100.
  `tests/perc.c` checks that the new modes are heard on every algorithm, the strings, and every note
  sounding and ending.

**Playing**
- DICE. **EDIT + LFO** rolls the four knobs of the algorithm in hand. Each knob moves only so far from that
  algorithm's own setting, by a span chosen per engine knob: pitch-like and stepped knobs (KIT TUNE, FM-4
  RATIO, STACK CHORD) barely move, a feature an algorithm has off (CHIP's SWEEP and ARP) stays off, filters
  cannot close the sound and feedback, resonance, fold and grit stay below where they run away. For another
  algorithm, turn ALGORITHM and roll again. **EDIT + ENV** goes back to the knobs from before the last roll,
  and again to the roll, to compare them. A roll is a hand on the knobs: recorded while the tape records,
  sent as CC 20-23. `tests/dice.c` rolls every algorithm and checks the spans, the panel, and that a roll
  stays within 6 dB of how loud the algorithm is at its own setting (it measures -4.5 to +4.5 dB).

**Picture**
- Each engine's picture follows the track in hand: its share of the mix's level, its own sound before the
  effect, its own notes. It followed the whole mix, so in a jam a silent FM-4 pulsed with another track's chord.
  The top bar's meter and the FX page keep the mix. Played alone, a track looks as it did.
- FM-4's operators glide to a new algorithm's places (about 170 ms, and on to the next if ALGO keeps turning)
  instead of jumping. A new engine, or another track's, comes in over 140 ms as a page does; the engine's name
  leaves in 0.2 s instead of 0.43. When a busy jam slows the screen, the pictures keep their pace (they ran up
  to a fifth slow below 20 frames a second).

**Fixes**
- Keys and MIDI notes are read while the screen is drawn, at most a millisecond apart. They were read once a
  frame, and a busy jam draws a frame in 60 to 175 ms: a note shorter than that could start and end between two
  reads and never sound, nor go on the tape (in 0.1.0 to 0.4.0, and the takes of a dense jam came out lighter
  than played). A key waited up to a frame to sound, too. Keys are left to the frame while a button is held
  (SAVE and a key take a snapshot); MIDI from the first message that is not a note, with all after it, keeps its
  order and waits for the frame.
- CUT (EDIT + REC) of a track holding more than 512 events, knob moves included, wrote past the end of the
  clip's memory (in 0.1.0 to 0.4.0): only notes checked for room, so a take with a lot of knob movement could
  overrun it. Every event now needs a place; what does not fit stays on the tape, as notes always did.
  Found by the crash hunt; `tests/tape.c` cuts and pastes a take of 1,200 knob moves.

**Audio time**
- FM-4, ANALOG, STACK, SHAPER and ELECTRIC each have a render of their own for every algorithm, so the
  sample loop no longer asks which algorithm it is playing. The sound is the same to the bit
  (`tests/cores.c`'s section hashes equal 0.4.0's). On the FM-1, eight-note chords under PLATE: ANALOG 5.1
  points of the audio time lower, FM-4 3.2, STACK 3.0, SHAPER 2.2, ELECTRIC 2.1. About 53 KB of flash.
  (CHIP and MONO gained nothing within the drift between runs and keep their one render.)
- Measured on an FM-1 (eight-note chords held on all 112 algorithms with PLATE on, CC 119): average 49%,
  peak 74% (PERC GAMELAN). 0.4.0's 96 were 48% and 73% (FM-4 BASS); FM-4 now peaks at 71%, SHAPER at 70%,
  the new SKINS at 66% (HALL) and CYCLE at 43%. `tools/fm1_profile.py` times an engine,
  or all of them, on an FM-1 and fails past a limit.
- Loop jams hold far better than 0.4.0's, which a tester heard stutter, and slow to half tempo, at four layers
  of SHAPER CZ-SAW, ELECTRIC FELT and KIT WORLD. `tools/fm1_profile.py jam` builds that jam on an FM-1, as full
  as it gets: four-note chords, three notes or three drums on every sixteenth on each of the four tracks, two
  overdubs in other algorithms, and keys held on top, with PLATE. 0.4.0 missed 63 halves over it (from the third
  layer on; 113% of the audio time at its peak) and let 8 voices go to catch up, and as it read keys once a
  frame, its takes may have lost some of their shortest notes (Fixes, above). This release, with every note
  kept, missed 35 and 22 in two runs, from the overdubs on, peaking at 104 and 106%: at that density it is at
  its limit, and the screen slows to 11 to 25 frames a second. The renders above are the likely reason for the
  difference: the jam plays three of their five engines.

**Inside**
- The saved state is version 5: it has room for sixteen engines and twelve effects (there are fourteen and
  eight), and records how many effects it was saved with, as it already did engines. An engine or effect
  added later comes up as new in a state saved before it, without another version. Settings saved by
  0.1.0 to 0.4.0 (version 4) load with everything kept: `tests/state.c` loads a state written by 0.4.0's
  own core (`tests/state_v4.bin`, made by `tests/state_v4_gen.c`) and compares every table. Before, the
  loader took only its own version, so any change to the state would have reset everyone's settings.
  The state grows from 2,648 to 2,888 bytes of RAM. Going back to 0.4.0 or older afterwards starts with
  settings as new (they refuse a version 5 state); the tape is kept, as it is saved apart.

## 0.4.0

Identity `FM-1_4122000400`. Released 2026-11-22. Installed and run on one FM-1 (a development build of the
same code). The numbers go from 0.1.0 to 0.4.0 to keep clear of h1's own tags (0.1.0 to 0.3.0). Settings and
the tape saved by 0.1.0 load as they were.

**Sound**
- WAVE's SPREAD is heard over its whole turn. At 0 a note is now centred; before, each note's right side
  started at a random point in its wave, so most banks were already about as wide at 0 as at 100. Turned
  up, the right side blends in from a second oscillator that starts further into its wave, reads further
  along the bank and is detuned by up to about 15 cents (about 6 before). `tests/basics.c` checks that
  every bank is centred at 0 and grows wider at every quarter of the turn.
- DELAY's right echo stayed in its line only up to a point: from a base time of 0.495 s to 0.689 s (120
  BPM at 1/4 or 1/2, 60 BPM at 1/8) its 1.5x tap wrapped round to a 7 to 160 ms echo, a metallic comb with
  feedback. The right side is 1.5x the left where that fits, else 0.75x. `tests/echo.c` checks every TIME
  at 40 to 240 BPM.
- An effect brought back (another chosen, FX bypassed, or MIX turned to 0 and up again) started with its
  old tail: its buffers were kept frozen. It starts from silence now.
- An engine on two tracks ran its own clocks (ELECTRIC's tremolo and wow, STACK's and HAZE's drift,
  ANALOG's and MONO's PWM) twice as fast, three times on three tracks. Once a block now.
- STUTTER's 1/8 was cut short below 108 BPM (1/16 below 54) and went out of time: a length too long for
  its memory is halved instead.
- The README says which effects are sends (PLATE, DELAY, SHIFT, CHORUS, TANK) and which work on the
  whole mix (FILTER, LOFI, THUMP).

**Audio running late**
- A late audio half no longer plays at full level. With HAZE and two tracks on the tape, the FM-1 ran
  out of audio time from five held keys. Each half was written straight into the buffer the speaker reads
  and only then turned down by MASTER, so a late half went out at full level, whatever MASTER was set to
  (a tester's "full-volume static"). Each half is now rendered into a buffer of its own and copied in only
  while there is still time (fm1-base's `audio_aside`); a late one leaves the half before playing whole.
- Running late half after half also starved the screen, the keys and the MASTER knob. Now two late
  halves in a row let one voice go in about 10 ms: one already let go if there is one, then the tape's
  or another track's, and only then a key that is held. One at a time, and none for a single late half.
  CC 119's PERF line says LATE and SHED. `tests/overload.c`.
- HAZE works out its airy low-pass once a block instead of once a voice (the sound is the same to the bit).

**Tape and sequencers**
- Holding REC past three seconds no longer clears the whole tape at once, which was easy to do by accident
  (jakehandy/h1#49). It asks CLEAR ALL?, and the question stays on screen (and is spoken to a screen
  reader) until it is answered. A tap of REC within three seconds of letting go clears the whole tape.
  Anything else keeps it: waiting, another button, a key, an encoder, or another message covering the
  question. The track in hand is still cleared at 0.7 s, as before.
- SHAKE could not be left from the SEQ page's knobs (abektes/a22#1). Its settings took all four columns, so the knob
  that had just turned SHAKE on became GRAV, and in the simulator nothing on the page could change the
  mode. KNOB 1 is now MODE in every mode. In SHAKE the page has two subpages: MODE, GRAV, SPIN, BOUNCE,
  then (SELECT, or ARP again) MODE, CAGE, and SHAKE always opens on the first. GRAV, SPIN and BOUNCE each
  moved one knob to the right. In SHAKE, ARP on its own page now turns to the other subpage instead of going
  home, as on MIX, ENV and GLO.
- The simulator's ALGORITHM slider on the SEQ page sets the sequencer mode, as the FM-1's encoder does. It
  used to say PRESET and change the engine's algorithm.
- SHAKE's PLAY KEYS TO DROP sits inside the cage, on two lines; it ran across the cage's walls.
- Recorded notes no longer hang or burst (`tests/tape.c`):
  - With SNAP (on by default), a short note played just before a grid line got its end before its start,
    and sounded until the tape came round. Its end now moves with its start.
  - Muting a track while its note sounded left the note playing silently, and back when unmuted.
  - Turning BARS down, or moving IN or OUT, while the tape played past the new end fired every note at
    once. The tape starts again at the loop's beginning.
  - A take recorded in REV played back as held notes. REC in REV now says NO REC IN REV.
  - A key held into a take and let go during it recorded a lone end, which could cut a later note.
  - CUT left the notes it cut sounding and cut the end off a note begun before IN; past what its clip
    holds, it lost events. PASTE could paste a start without its end when the tape filled; now it
    pastes all or nothing (TAPE FULL).
  - Clearing one track let go of every track's notes; now that track's alone.
  - A take's first note was silent on the pass right after the take.
- MIDI's all notes off (CC 123), which DAWs send on stop and A22 sends when a USB keyboard is unplugged,
  stopped the tape and the pattern. Now it ends the notes played over MIDI, and the rest plays on.
- The TAPE EDIT page counts IN BAR from 1, as its toast does, and shows END for an OUT BAR that plays to
  the end (at or before IN BAR).

**Saving**
- After a start that failed (a crash or hang in its first 30 seconds), the saved tape was erased as soon
  as nothing sounded. It is kept until a new tape is saved over it.
- Step patterns, riffs, snapshots and knobs set over MIDI were not saved unless some other setting changed
  afterwards. They are saved as any setting is.
- A save erases flash, which stalls the sound for a moment, and it came while a reverb or delay tail, a
  rest of a playing tape, or the metronome was still heard. It waits for real silence now. SAVE says SAVE
  WHEN QUIET while it waits (it said SAVED).
- Holding SAVE to reset an engine left the track's step pattern on the old algorithm.
- A snapshot's LFO settings are checked on load, as a track's are.
- The engine's event queue keeps room for note-offs: full, it dropped one, and that note hung.

## 0.1.0

Identity `FM-1_4122000100`. Released 2026-11-21. Installed and run on one FM-1.

**Sound**
- Twelve engines in place of h1's sixteen instruments, synthesised but for KIT's short drum recordings (h1's
  PLUNK hits, CC0): FM-4, ELECTRIC,
  STACK, ANALOG, SHAPER, WIRE, HAZE, MONO (monophonic, with glide), KIT, PERC, WAVE and CHIP. Each has
  eight algorithms, and each algorithm starts from its own four knobs and keeps them.
- WAVE: eight banks of fifteen single-cycle waves (made by `tools/gen_wavetables.py`: eight main ones and
  seven real in-between shapes), each kept at six octave levels so high notes don't fold back, morphed with
  WAVE, moved along by SCAN as a note dies, spread in stereo.
- CHIP: 8-bit console voices: band-limited pulses in sixteenth duty steps, a stepped triangle, a noise
  register (long and short), arpeggios, pitch sweeps and CRUSH.
- Eight selectable effects (PLATE, DELAY, FILTER, SHIFT, CHORUS, TANK, LOFI, THUMP) with a send from each
  track, and a master bus with a three-band EQ and drive.

**Tape and sequencers**
- h1's loop becomes a four-track tape: each track keeps its own engine, algorithm, knobs, volume, pan, send
  and mute. 1 to 32 bars, 2,048 events, and knob moves recorded as automation.
- Tape speed (0.5x, 1x, 2x, reverse), tricks (brake, stutters), loop in and out bars, and cutting a track's notes to a clipboard and
  pasting them back.
- A MIX page: each track's volume, pan and FX send, and EDIT + SELECT or EDIT + MASTER for the track in hand.
  This replaces h1's volume per instrument.
- Riff and Shake join the arpeggiator's modes.
- Eight patch snapshots on SAVE + the white keys.
- Chord memory: one key plays a chord. MEMORY (hold a chord and press EDIT + GLO to store it), SCALE
  (chords from the KEYS root and scale) or SHAPE (eight fixed chords), with four voicings and a strum.
  Settings on GLO's new CHORD subpage.

**Panel**
- An EDIT + button shortcut's message is no longer covered by the track volume ring while EDIT is held.

**Picture**
- Colour vector graphics: each pixel is one of eight colour banks at one of 32 shades (h1 had one ink in
  256 levels).

**Performance** (measured on an FM-1, eight-note chords held on every algorithm with PLATE on)
- PLATE runs its two halves on the two cores, to the bit the same as on one, and sleeps while it and its
  input are silent: the audio's busiest moment went from 96% of its time to 81%, the average from 73% to
  54%, and with nothing playing from 38% to 10%. The screen draws about twice as many frames under load.
- Drawing does the same pixels in less time: an ellipse works out only the pixels near its edge, the top bar
  and knob strip clear without blending each pixel, and glows and gradient fills keep their state in
  registers. The big glows of ELECTRIC, STACK, HAZE, WIRE and PERC follow their knob, no longer the level, so
  they aren't drawn and sent again every frame. Over all 96 algorithms the screen went from 24 frames a
  second to 41 on average (STACK 13 to 27, KIT 20 to 48), and the audio's busiest moment from 84% to 74%.
- A faster sine table lookup for every engine; SHAPER's phase distortions and CRUSH no longer divide every
  sample.

**Fixes**
- Changing track (EDIT + PRESETS) kept the new track's engine and algorithm on the old one: going round
  T1 to T4 left each with the next one's engine. Each track keeps its own now.
- A key held while the track or the sequencer mode changed never let go of its note.
- A key pressed with the sequencer OFF and let go after the mode changed sent no note off over MIDI out, so
  a synth listening kept the note on (jakehandy/h1#56).
- A SHAKE ball that bounced out left the note it had just struck sounding for good, on the FM-1 and over
  MIDI out. It now leaves the picture and lets its note ring out first; a reused ball also ends its note
  on the tape.
- The tape's knob automation and varispeed (with pitch following speed) reached the other tracks' recorded
  parts but not the track in hand's.
- The white keys show all of a 64-step pattern (only its first 32 steps lit before), and the tape's notes and
  place while it plays.
- A loop left playing keeps every take through a power-off: saves pack into the ring's slots as records
  (h1 0.3.0's fix), where only three were kept before.
- A tape recorded long and then shortened (fewer bars, or new loop in and out bars) keeps the notes past its
  new end through a power cycle. The loader used to drop them while the running tape still had them.

**Under the hood**
- Saved state and tape have their own formats (`2A22`, `A22T`, ring `A22R`). h1's saves are refused, so
  a unit coming from h1 starts as new.
- Tests rewritten for A22 (`tests/run_tests.sh`), and the CI and release workflows build `a22.fwsc`.
