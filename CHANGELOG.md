# Changelog

A22's own history starts here. It is derived from h1 as of its 0.3.0; h1's history is in
[jakehandy/h1's CHANGELOG](https://github.com/jakehandy/h1/blob/main/CHANGELOG.md).

## 0.1.0

Identity `FM-1_4122000100`. Not released and not yet tried on an FM-1.

**Sound**
- Ten synthesised engines in place of h1's sixteen instruments and their recorded samples: FM-4, ELECTRIC,
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

**Fixes**
- A tape recorded long and then shortened (fewer bars, or new loop in and out bars) keeps the notes past its
  new end through a power cycle. The loader used to drop them while the running tape still had them.

**Under the hood**
- Saved state and tape have their own formats (`2A22`, `A22T`, ring `A22R`). h1's saves are refused, so
  a unit coming from h1 starts as new.
- Tests rewritten for A22 (`tests/run_tests.sh`), and the CI and release workflows build `a22.fwsc`.
