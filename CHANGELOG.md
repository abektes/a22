# Changelog

A22's own history starts here. It is derived from h1 as of its 0.3.0; h1's history is in
[jakehandy/h1's CHANGELOG](https://github.com/jakehandy/h1/blob/main/CHANGELOG.md).

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
