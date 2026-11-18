#!/bin/sh
# h1's tests: the device app in fm1-base's host simulator (the platform simulated, the same app.c
# and core as the firmware), then LAYERS on the core alone (tests/layers.c). The release workflow
# runs this after the build; a failure stops it.
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
out=app/build/host
"$out/h1_host" tests/basic.txt "$out"
mkdir -p build
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/layers.c -o build/h1layers -lm
ASAN_OPTIONS=detect_leaks=0 build/h1layers
