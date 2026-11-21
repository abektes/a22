#!/bin/sh
# a22's tests. First the device app in fm1-base's host simulator (the platform simulated, the same app.c
# and core as the firmware), with the second core and without. Then the core alone, most of it under the
# compiler's address and undefined-behaviour checkers:
#   a22_test   two cores bit-exact on all 96 presets, MONO mono, the tape saved and loaded from odd
#              addresses, snapshots, each algorithm's knobs, varispeed, Riff and Shake, a stress run
#   basics     the limiter on every algorithm, velocity, the tape through a power cycle, MIDI out
#   mixer      each track's volume: the knob, saved, heard (squared), muted, no click
#   looper     the tape's room (2048 events, TAPE FULL), 1 to 32 bars, a shortened tape kept through a load
#   chords     chord memory: its modes and voicings, release, strum, EDIT + GLO capture, the CHORD page
#   tracks     each track keeps its engine through EDIT + PRESETS, held keys let go across track and mode
#              changes, varispeed reaches the track in hand, the white keys show 64 steps and the tape
#   echo       DELAY the same after 25 minutes on as at start
#   cores      two cores sound the same as one, section by section
#   lines      g_line, g_poly and g_arc against a distance worked out in floats
# The release workflow runs this after the build; a failure stops it. tests/run_fuzz.sh is the crash hunt.
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
$CC -std=c99 $SAN core/*.c tests/mixer.c -o build/a22mixer -lm
build/a22mixer
$CC -std=c99 $SAN core/*.c tests/looper.c -o build/a22looper -lm
build/a22looper
$CC -std=c99 $SAN core/*.c tests/chords.c -o build/a22chords -lm
build/a22chords
$CC -std=c99 $SAN core/*.c tests/tracks.c -o build/a22tracks -lm
build/a22tracks
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
$CC -std=c99 $SAN core/gfx.c tests/lines.c -o build/a22lines -lm
build/a22lines
echo "run_tests: all passed"
