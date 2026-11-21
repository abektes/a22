# Changelog

A22's own history starts here. It is derived from h1 as of its 0.3.0; h1's history is in
[jakehandy/h1's CHANGELOG](https://github.com/jakehandy/h1/blob/main/CHANGELOG.md).

## 0.1.0

Identity `FM-1_4122000100`. Not released. Installed and run on one FM-1.

**Sound**
- Ten engines in place of h1's sixteen instruments, synthesised but for KIT's short drum recordings (h1's
  PLUNK hits, CC0): FM-4, ELECTRIC,
  STACK, ANALOG, SHAPER, WIRE, HAZE, MONO (monophonic, with glide), KIT and PERC. Each has
  eight algorithms, and each algorithm keeps its own four knobs.
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

**Performance** (measured on an FM-1, eight-note chords on all 80 algorithms with PLATE on)
- PLATE runs its two halves on the two cores, to the bit the same as on one, and sleeps while it and its
  input are silent: the audio's busiest moment went from 96% of its time to 81%, the average from 73% to
  54%, and with nothing playing from 38% to 10%. The screen draws about twice as many frames under load.
- A faster sine table lookup for every engine; SHAPER's phase distortions and CRUSH no longer divide every
  sample.

**Fixes**
- Changing track (EDIT + PRESETS) kept the new track's engine and algorithm on the old one: going round
  T1 to T4 left each with the next one's engine. Each track keeps its own now.
- A key held while the track or the sequencer mode changed never let go of its note.
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
