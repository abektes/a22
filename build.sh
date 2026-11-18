#!/bin/sh
# Build h1 with fm1-base (the submodule in base/, with felucca-bsp in base/bsp):
#   ./build.sh                    app/build/h1.fwsc, a development build (identity FM-1_48100)
#   ./build.sh --release 0.1.0    app/build/h1-0.1.0.fwsc (identity FM-1_481000100)
#   ./build.sh --host             the device app in fm1-base's host simulator (plain cc, no toolchain)
# The firmware needs JieLi's toolchain (JIELI_TOOLCHAIN) and the AC79 SDK (AC79_SDK): base/README.md.
set -eu
cd "$(dirname "$0")"
[ -f base/tools/build.py ] || git submodule update --init --recursive
exec python3 base/tools/build.py app "$@"
