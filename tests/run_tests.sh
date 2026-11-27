#!/bin/sh
# a22's tests. First the device app in fm1-base's host simulator (the platform simulated, the same app.c
# and core as the firmware), with the second core and without. Then the core alone, most of it under the
# compiler's address and undefined-behaviour checkers:
#   a22_test   two cores bit-exact on all 112 presets, MONO mono, the tape saved and loaded from odd
#              addresses, snapshots, each algorithm's knobs, varispeed, Riff and Shake, a stress run
#   basics     the limiter on every algorithm, velocity, WAVE's SPREAD, the tape through a power cycle, MIDI out
#   midiout    MIDI out as a DAW records it: each track on its own channel, a note's off on its on's channel,
#              knob and tape automation CCs, HUSH on all four channels
#   keyfollow  KEY FOLLOW's data: recorded notes carry GLO's key (an off its on's), the tape blob's header bit, junk
#              keys dropped on load, the step pattern's key and each track's KEY bit in the saved state
#   miditiers  the device's MIDI out queue: what still goes when it is nearly full (app/midi_tiers.h)
#   keymap     the key mapping (core/keymap.c): a note in one key played in another, by scale steps, clamped
#   mixer      each track's volume: the knob, saved, heard (squared), muted, no click
#   looper     the tape's room (2048 events, TAPE FULL), 1 to 32 bars, a shortened tape kept through a load
#   chords     chord memory: its modes and voicings, release, strum, EDIT + GLO capture, the CHORD page
#   state      saved state version 5: 0.4.0's version 4 (tests/state_v4.bin) loads with every table kept,
#              version 5 to the byte, wrong ones refused, engines and effects past what a state had come up new
#              (and a project's state: taken whole but TUNE and BRIGHT)
#   dice       EDIT + LFO rolls the knobs near the algorithm's home, EDIT + ENV swaps back; every algorithm
#              rolled stays within 6 dB of how loud it is at home
#   skins      SKINS' two kits: every drum on every algorithm sounds and ends, the hat chokes, ROOM spreads,
#              the treatments are shorter, longer and darker than the dry kits
#   perc       PERC's eight modes heard on every algorithm, its sympathetic strings in SPACE's top, every
#              note sounds and ends
#   arpmode    the SEQ / ARP page reaches every mode and leaves SHAKE again (issue #1), on the panel and in
#              the simulator's sliders; SHAKE's two subpages
#   tracks     each track keeps its engine through EDIT + PRESETS, held keys let go across track and mode
#              changes, varispeed reaches the track in hand, the white keys show 64 steps and the tape
#   tape       the tape's edges: SNAP keeps a note's length, mute, BARS turned down while playing, no REC in REV,
#              CUT and clearing one track let go of the right notes, a take's first note on its first pass
#   punch      PUNCH's eight effects on the mix (EV_PUNCH): BRAKE slows, the stutters repeat at 1/8, 1/16 and 1/32,
#              LPF and HPF sweep over a bar, CRUSH, GATE chops each 1/16, no clicks, TRICK back after a punch
#   lane       the key lane (EV_LANE): entries one a bar, inherited field by field, kept in the tape's tail through a
#              save, CUT, PASTE and a track's clear, gone with CLEAR ALL; KEY-on tracks follow bar by bar (I-IV-V-vi),
#              recording over a DEG stores the note down, the live keys and chord SCALE snap to it, STEP ignores it
#   stems      the USB audio input's stems: T1 to T4 each on its own pair, a muted one silent, the mix the same to
#              the bit with them or without, nothing written while nobody records, and they add up to the mix
#              (master bus flat), the limiter's gain on them too
#   overload   the audio over its time: two late halves shed a voice, fast and quiet, one at a time
#   echo       DELAY the same after 25 minutes on as at start
#   cores      two cores sound the same as one, section by section
#   sleep      DELAY, SHIFT, CHORUS and TANK asleep, and WIRE's shorter clear, sound the same to the bit as before
#   lines      g_line, g_poly and g_arc against a distance worked out in floats
# The release workflow runs this after the build; a failure stops it. tests/run_fuzz.sh is the crash hunt;
# tests/corestat.c (not run here) counts how the voices are shared between the two cores, scenario by scenario.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
SAN="-O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w"
export ASAN_OPTIONS=detect_leaks=0

./build.sh --host
out=app/build/host
"$out/a22_host" tests/basic.txt "$out"
# USB host: OCT- and OCT+ held, a keyboard plugged in and played, unplugged, and back to the computer
"$out/a22_host" tests/usbhost.txt "$out"
# the tape kept through a power-off while it plays, and one cut off in the middle of its save
"$out/a22_host" tests/looppower.txt "$out"
# the ring's slots erased a sector a frame, stopped and after a start that failed, and the tape kept
"$out/a22_host" tests/ringerase.txt "$out"
# projects: four slots and a spare, a save cut by a power-off, another firmware's bytes, a take during a save
"$out/a22_host" tests/projects.txt "$out"
# a project loaded stopped, while playing with a key held, refused while recording, and kept through a restart
"$out/a22_host" tests/projload.txt "$out"
# settings while the tape plays: written only in silence on their own, at once after SAVE, and kept through a power cut
"$out/a22_host" tests/statesave.txt "$out"
# DICE: EDIT + LFO rolls the knobs (out as CC 20-23), EDIT + ENV puts them back
"$out/a22_host" tests/dice.txt "$out"
# MIDI out to USB as a DAW records it: clock, Start and Stop, T2 on channel 2, HUSH on all four channels
"$out/a22_host" tests/midiclock.txt "$out"
# KEY FOLLOW: MIX -> KEY turns T1 on (said), a take on T1 plays back two semitones up after ROOT goes up 2
"$out/a22_host" tests/keyfollow.txt "$out"
# PUNCH: hold FX and a snapshot key (said PUNCH, then the name; no note, no FX page), a second key replaces the first
"$out/a22_host" tests/punch.txt "$out"
# LANE: SEQ, SELECT twice is the LANE subpage (said), BAR 2, ROOT D, and a field turned past its first value is said NONE
"$out/a22_host" tests/lane.txt "$out"
# STEMS: the USB audio input's ten channels, a take on each track then a pass recorded at half MASTER, checked by
# tools/a22_stems.py: T1 to T4 on 3-4 to 9-10 in turn, fitting the mix but for its master bus, T1 muted silent
"$out/a22_host" tests/stems.txt "$out"
python3 tools/a22_stems.py "$out/stems.wav" --check
# (that build has the second core, as the firmware does: the simulator runs its jobs at once, in order)
# and the build without it: the same script
FM1B_CPU2=0 ./build.sh --host
"$out/a22_host" tests/basic.txt "$out"
./build.sh --host

mkdir -p build
$CC -std=c99 $SAN core/*.c tests/a22_test.c -o build/a22test -lm -lpthread
build/a22test
$CC -std=c99 $SAN core/*.c tests/basics.c -o build/a22basics -lm
build/a22basics
$CC -std=c99 $SAN core/*.c tests/midiout.c -o build/a22midiout -lm
build/a22midiout
$CC -std=c99 $SAN core/*.c tests/keyfollow.c -o build/a22keyfollow -lm
build/a22keyfollow
$CC -std=c99 $SAN tests/miditiers.c -o build/a22miditiers
build/a22miditiers
$CC -std=c99 $SAN tests/keymap.c core/keymap.c -o build/a22keymap
build/a22keymap
$CC -std=c99 $SAN core/*.c tests/mixer.c -o build/a22mixer -lm
build/a22mixer
$CC -std=c99 $SAN core/*.c tests/looper.c -o build/a22looper -lm
build/a22looper
$CC -std=c99 $SAN core/*.c tests/chords.c -o build/a22chords -lm
build/a22chords
$CC -std=c99 $SAN core/*.c tests/state.c -o build/a22state -lm
build/a22state
$CC -std=c99 $SAN core/*.c tests/dice.c -o build/a22dice -lm
build/a22dice
$CC -std=c99 $SAN core/*.c tests/skins.c -o build/a22skins -lm
build/a22skins
$CC -std=c99 $SAN core/*.c tests/perc.c -o build/a22perc -lm
build/a22perc
$CC -std=c99 $SAN core/*.c tests/arpmode.c -o build/a22arpmode -lm
build/a22arpmode
$CC -std=c99 $SAN core/*.c tests/tracks.c -o build/a22tracks -lm
build/a22tracks
$CC -std=c99 $SAN core/*.c tests/tape.c -o build/a22tape -lm
build/a22tape
$CC -std=c99 $SAN core/*.c tests/punch.c -o build/a22punch -lm
build/a22punch
$CC -std=c99 $SAN core/*.c tests/lane.c -o build/a22lane -lm
build/a22lane
$CC -std=c99 $SAN core/*.c tests/overload.c -o build/a22overload -lm
build/a22overload
$CC -std=c99 $SAN core/*.c tests/stems.c -o build/a22stems -lm
build/a22stems
# (25 minutes of DELAY: without the checkers, or it takes most of an hour)
$CC -std=c99 -O2 -ffp-contract=off -w core/*.c tests/echo.c -o build/a22echo -lm
build/a22echo
# two cores sound the same as one, to the bit (tests/cores.c: the second core is a thread)
$CC -std=c11 -O2 -ffp-contract=off -w core/*.c tests/cores.c -o build/a22cores -lm -lpthread
build/a22cores > build/cores1.txt
build/a22cores 2 > build/cores2.txt
if cmp -s build/cores1.txt build/cores2.txt; then
    echo "cores: $(wc -l < build/cores1.txt | tr -d ' ') sections, the same on one core and two"
else
    echo "FAIL cores: two cores sound different from one, in these sections:"
    diff build/cores1.txt build/cores2.txt | head -20
    exit 1
fi
# 0.6.0's savings: the echo effects asleep and WIRE's shorter clear, the same to the bit as without them
$CC -std=c99 -O2 -ffp-contract=off -w core/*.c tests/sleep.c -o build/a22sleep -lm
build/a22sleep > build/sleep1.txt
build/a22sleep before > build/sleep2.txt
# and they do fall asleep with denormals kept, as the FM-1 keeps them (a filter state stuck at the smallest one)
build/a22sleep denormals
if cmp -s build/sleep1.txt build/sleep2.txt; then
    echo "sleep: $(wc -l < build/sleep1.txt | tr -d ' ') sections, the same asleep and awake"
else
    echo "FAIL sleep: the effects asleep or WIRE's clear changed the sound, in these sections:"
    diff build/sleep1.txt build/sleep2.txt | head -20
    exit 1
fi
$CC -std=c99 $SAN core/gfx.c tests/lines.c -o build/a22lines -lm
build/a22lines
echo "run_tests: all passed"
