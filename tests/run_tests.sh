#!/bin/sh
# h1's tests: the device app in fm1-base's host simulator (the platform simulated, the same app.c
# and core as the firmware). The release workflow runs this after the build; a failure stops it.
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
out=app/build/host
"$out/h1_host" tests/basic.txt "$out"
